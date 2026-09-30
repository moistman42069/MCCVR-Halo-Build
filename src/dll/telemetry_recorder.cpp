#include "telemetry_recorder.h"

#include "../common/log.h"
#include "../common/virtual_stock_diagnostic.h"
#include "../common/two_hand_input_smoothing.h"
#include "../common/virtual_stock_settings.h"
#include "../common/virtual_stock_test_profiles.h"

#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <limits>
#include <new>
#include <share.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef HALOMCCVR_BUILD_COMMIT
#define HALOMCCVR_BUILD_COMMIT "unknown"
#endif

namespace
{
    constexpr DWORD kWorkerPollMs = 10;
    constexpr uint64_t kFlushFrameInterval = 256;
    constexpr uint64_t kFlushCheckFrameInterval = 32;
    constexpr uint64_t kFlushTimeIntervalMs = 1000;
    constexpr size_t kFileBufferBytes = 256 * 1024;
    constexpr wchar_t kAnalyserRelativePath[] =
        L"TelemetryAnalyser\\analyse_mccvr_telemetry.py";
    constexpr wchar_t kTelemetryRecordingsLeaf[] =
        L"Telemetry Recordings";

    using TelemetryQueueStorage = std::byte[
        static_cast<size_t>(kTelemetryQueueSlots) * sizeof(TelemetryFrame)];
    alignas(TelemetryFrame) TelemetryQueueStorage g_queueStorage{};
    static_assert(std::is_trivially_default_constructible_v<
        TelemetryQueueStorage>);
    alignas(64) std::atomic<uint32_t> g_headIndex{0};
    alignas(64) std::atomic<uint32_t> g_tailIndex{0};

    // ---- Weapon-order diagnostic events (read-only evidence tranche) ----
    // MPSC event channel owned by this recorder: same worker, same JSONL file,
    // same analyser, same recording-active gate. The global claim counter is
    // the cross-thread event order. Publication uses a dedicated per-slot
    // atomic marker: the producer writes the payload with ordinary stores and
    // then release-stores committedSequence; the consumer only reads payload
    // fields after an acquire load that observed the expected sequence. The
    // payload storage is never touched through atomic aliasing.
    //
    // Safety property (by construction): once the drain advances past
    // sequence N, no producer capable of publishing N remains. The drain gaps
    // N only after observing `g_weaponEventProducersInFlight == 0` and then
    // re-loading the marker; every producer holds its receipt from before the
    // sequential-consistent claim until after the release marker store (or
    // its drop), so quiescence plus a missing marker proves a definitive
    // loss. Accepted claims also satisfy `sequence <= drained + slots`
    // (producers drop beyond capacity), so no later producer can reuse N's
    // slot while N is the drain target.
    struct WeaponEventSlot
    {
        TelemetryWeaponEvent payload{};
        std::atomic<uint64_t> committedSequence{0};
    };
    alignas(64) WeaponEventSlot
        g_weaponEventSlots[kWeaponOrderEventQueueSlots]{};
    // Regular 10 ms worker poll budget for verified-missing gap emission.
    // Session finalization uses the quiescent frontier drain instead.
    constexpr uint64_t kWeaponEventNormalGapBudget = 256;
    // Next sequence to assign (fetch_add returns previous; stored seq starts
    // at 1 so 0 always means a free slot). Never reset across sessions: an
    // in-flight publish must keep a unique sequence past Stop/Start.
    std::atomic<uint64_t> g_weaponEventClaimed{0};
    // Worker only: last sequence serialized (or skipped) in file order.
    uint64_t g_weaponEventDrained = 0;
    // Worker-published watermark so producers can shed load when the worker
    // stalls. Stale reads only cause conservative drops, never corruption:
    // at most two producers (Present/Capture thread, FP thread) can be
    // in flight at once, far below the 1024-slot capacity. Accepted claims
    // satisfy (sequence - actualDrained) <= slots, so a producer can never
    // overwrite the slot the consumer is reading.
    std::atomic<uint64_t> g_weaponEventPostedDrained{0};
    std::atomic<uint64_t> g_weaponEventEnqueued{0};
    std::atomic<uint64_t> g_weaponEventWritten{0};
    std::atomic<uint64_t> g_weaponEventDropped{0};
    std::atomic<uint64_t> g_weaponEventSessionSkipped{0};
    // Producer/session handshake: Stop/finalization waits for this to drain
    // before the final weapon-event drain and file close, so an admitted
    // publisher can never append to a closed session.
    std::atomic<uint32_t> g_weaponEventProducersInFlight{0};
#ifdef HALOMCCVR_TELEMETRY_TESTING
    std::atomic<bool> g_forceWeaponEventsAccepting{false};
    std::atomic<bool> g_pauseWeaponEventProducer{false};
    std::atomic<bool> g_weaponEventProducerReachedPause{false};
    // One-shot: only the FIRST producer to reach the post-claim point waits
    // on the hold gate, so a test can stall exactly one outstanding claim
    // while other producers keep publishing.
    std::atomic<bool> g_armWeaponEventCommitPause{false};
    std::atomic<bool> g_holdWeaponEventCommit{false};
    std::atomic<bool> g_weaponEventCommitReachedPause{false};
#endif

    WeaponEventSlot& WeaponEventSlotFor(uint64_t sequence) noexcept
    {
        return g_weaponEventSlots[(sequence - 1) %
            kWeaponOrderEventQueueSlots];
    }

    std::atomic<bool> g_desiredRecording{false};
    std::atomic<uint32_t> g_producerInFlight{0};
    // Bit zero is acceptance; upper bits form a monotonic session generation.
    // Producers retain the full token across admission so Stop/Start cannot
    // create an acceptance ABA. Token and in-flight handshake operations are
    // sequentially consistent so finalization must observe either a changed
    // token or the producer receipt.
    std::atomic<uint64_t> g_admissionToken{0};
    std::atomic<uint8_t> g_state{
        static_cast<uint8_t>(TelemetryRecorderState::Unavailable)};
    std::atomic<uint8_t> g_error{
        static_cast<uint8_t>(TelemetryErrorCode::None)};
    std::atomic<uint32_t> g_systemError{0};
    std::atomic<int64_t> g_sessionStartQpc{0};
    std::atomic<int64_t> g_qpcFrequency{0};
    std::atomic<uint64_t> g_producerCalls{0};
    std::atomic<uint64_t> g_duplicateSerialSuppressed{0};
    std::atomic<uint64_t> g_enqueued{0};
    std::atomic<uint64_t> g_written{0};
    std::atomic<uint64_t> g_droppedQueueFull{0};
    std::atomic<uint64_t> g_writerFailures{0};

    HANDLE g_controlEvent = nullptr;
    std::wstring g_directory;
    alignas(64) char g_fileBuffer[kFileBufferBytes]{};

    // These are producer-owned. The worker resets them only after acceptance
    // is disabled and producerInFlight reaches zero.
    bool g_haveLastProducerSerial = false;
    uint64_t g_lastProducerPreparedSerial = 0;

#ifdef HALOMCCVR_TELEMETRY_TESTING
    std::atomic<bool> g_forceOpenFailure{false};
    std::atomic<int32_t> g_failWriteAfter{-1};
    std::atomic<int32_t> g_failFlushAfter{-1};
    std::atomic<bool> g_forceCloseFailure{false};
    std::atomic<bool> g_pauseStarting{false};
    std::atomic<bool> g_pauseRecording{false};
    std::atomic<bool> g_workerReachedRecording{false};
    std::atomic<bool> g_pauseRecordingDrain{false};
    std::atomic<bool> g_workerReachedRecordingDrain{false};
    std::atomic<bool> g_pauseFinalizing{false};
    std::atomic<bool> g_pauseProducerAdmission{false};
    std::atomic<bool> g_producerReachedAdmission{false};
    std::atomic<bool> g_pauseProducerSecondCheck{false};
    std::atomic<bool> g_producerReachedSecondCheck{false};
    bool g_testPythonAvailable = true;
    bool g_testPyAvailable = true;
    bool g_testPythonLaunchSucceeds = true;
    bool g_testPyLaunchSucceeds = true;
    bool g_testUseRealAnalyserLauncher = false;
    bool g_testSessionClosed = false;
    std::atomic<bool> g_testPauseAnalyserLaunch{false};
    std::atomic<bool> g_testAnalyserLaunchReached{false};
    std::atomic<uint32_t> g_testAnalyserLaunchRequests{0};
    std::atomic<uint32_t> g_testAnalyserLaunchCompletions{0};
    std::wstring g_testAnalyserModuleDirectory;
    TelemetryAnalyserLauncherTestSnapshot g_testAnalyserLauncherSnapshot{};
#endif

    struct WorkerSession
    {
        FILE* file = nullptr;
        std::string basename;
        std::wstring path;
        int64_t startQpc = 0;
        uint64_t startTickMs = 0;
        uint64_t lastFlushMs = 0;
        uint64_t framesSinceFlush = 0;
        bool haveWrittenSerial = false;
        uint64_t firstWrittenSerial = 0;
        uint64_t lastWrittenSerial = 0;
        uint64_t weaponEventToken = 0;
    };

    TelemetryRecorderState LoadState() noexcept;

    struct AnalyserLaunchPlan
    {
        std::wstring scriptPath;
        std::wstring recordingPath;
        std::wstring commandLine;
    };

    std::wstring QuoteWindowsArgument(const wchar_t* value)
    {
        std::wstring quoted;
        quoted.push_back(L'"');
        size_t backslashes = 0;
        for (const wchar_t* cursor = value ? value : L"";; ++cursor)
        {
            const wchar_t character = *cursor;
            if (character == L'\\')
            {
                ++backslashes;
                continue;
            }
            if (character == L'"')
            {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted.push_back(L'"');
                backslashes = 0;
                continue;
            }
            if (character == L'\0')
            {
                quoted.append(backslashes * 2, L'\\');
                quoted.push_back(L'"');
                return quoted;
            }
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }

    void AppendCommandArgument(std::wstring& commandLine, const wchar_t* value)
    {
        if (!commandLine.empty())
            commandLine.push_back(L' ');
        commandLine += QuoteWindowsArgument(value);
    }

    bool BuildAnalyserLaunchPlan(
        const wchar_t* moduleDirectory, const wchar_t* recordingPath,
        const wchar_t* interpreterPath, bool pyLauncher,
        AnalyserLaunchPlan& plan)
    {
        if (!moduleDirectory || !moduleDirectory[0] ||
            !recordingPath || !recordingPath[0] ||
            !interpreterPath || !interpreterPath[0])
        {
            return false;
        }

        plan = {};
        plan.scriptPath = moduleDirectory;
        if (plan.scriptPath.back() != L'\\' && plan.scriptPath.back() != L'/')
            plan.scriptPath.push_back(L'\\');
        plan.scriptPath += kAnalyserRelativePath;
        plan.recordingPath = recordingPath;

        // The analyser derives the canonical per-capture sidecars
        // (<stem>.manifest.json, <stem>.diagnostics.json/.md and
        // TELEMETRY_AI_GUIDE.md) itself. The automatic path requests no
        // legacy or custom output paths.
        AppendCommandArgument(plan.commandLine, interpreterPath);
        if (pyLauncher)
            AppendCommandArgument(plan.commandLine, L"-3");
        AppendCommandArgument(plan.commandLine, plan.scriptPath.c_str());
        AppendCommandArgument(plan.commandLine, plan.recordingPath.c_str());
        return true;
    }

    bool EnvironmentEntryHasName(
        const std::wstring& entry, const wchar_t* name) noexcept
    {
        const size_t nameLength = std::wcslen(name);
        return entry.size() > nameLength && entry[nameLength] == L'=' &&
            _wcsnicmp(entry.c_str(), name, nameLength) == 0;
    }

    std::vector<wchar_t> BuildUtf8EnvironmentBlock(
        const wchar_t* environmentBlock)
    {
        std::vector<std::wstring> entries;
        if (environmentBlock)
        {
            for (const wchar_t* cursor = environmentBlock; *cursor;)
            {
                std::wstring entry(cursor);
                cursor += entry.size() + 1;
                if (!EnvironmentEntryHasName(entry, L"PYTHONUTF8") &&
                    !EnvironmentEntryHasName(entry, L"PYTHONIOENCODING"))
                {
                    entries.push_back(std::move(entry));
                }
            }
        }
        entries.emplace_back(L"PYTHONUTF8=1");
        entries.emplace_back(L"PYTHONIOENCODING=utf-8");
        std::sort(entries.begin(), entries.end(),
            [](const std::wstring& left, const std::wstring& right) {
                return _wcsicmp(left.c_str(), right.c_str()) < 0;
            });

        size_t characterCount = 1;
        for (const auto& entry : entries)
            characterCount += entry.size() + 1;
        std::vector<wchar_t> block;
        block.reserve(characterCount);
        for (const auto& entry : entries)
        {
            block.insert(block.end(), entry.begin(), entry.end());
            block.push_back(L'\0');
        }
        block.push_back(L'\0');
        return block;
    }

    bool CurrentModuleDirectory(std::wstring& directory)
    {
#ifdef HALOMCCVR_TELEMETRY_TESTING
        if (!g_testAnalyserModuleDirectory.empty())
        {
            directory = g_testAnalyserModuleDirectory;
            return true;
        }
#endif
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&g_state), &module))
        {
            return false;
        }
        std::vector<wchar_t> path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(
            module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size() - 1)
            return false;
        directory.assign(path.data(), length);
        const size_t slash = directory.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return false;
        directory.resize(slash + 1);
        return true;
    }

    bool ResolveInterpreterExecutable(
        const wchar_t* filename, bool pyLauncher,
        std::wstring& executable)
    {
#ifdef HALOMCCVR_TELEMETRY_TESTING
        if (!g_testUseRealAnalyserLauncher)
        {
            if (pyLauncher)
            {
                ++g_testAnalyserLauncherSnapshot.pyResolutionAttempts;
                if (!g_testPyAvailable)
                    return false;
                executable = L"C:\\Windows\\py.exe";
            }
            else
            {
                ++g_testAnalyserLauncherSnapshot.pythonResolutionAttempts;
                if (!g_testPythonAvailable)
                    return false;
                executable = L"C:\\Program Files\\Python Test\\python.exe";
            }
            return true;
        }
#endif
        std::vector<wchar_t> path(32768, L'\0');
        const DWORD length = SearchPathW(
            nullptr, filename, nullptr,
            static_cast<DWORD>(path.size()), path.data(), nullptr);
        if (length == 0 || length >= path.size())
            return false;
        executable.assign(path.data(), length);
        return true;
    }

    bool StartAnalyserProcess(
        const AnalyserLaunchPlan& plan, const std::wstring& applicationName,
        const std::wstring& workingDirectory, bool pyLauncher,
        uint32_t& systemError)
    {
        LPWCH inherited = GetEnvironmentStringsW();
        if (!inherited)
        {
            systemError = GetLastError();
            return false;
        }
        std::vector<wchar_t> environment;
        try
        {
            environment = BuildUtf8EnvironmentBlock(inherited);
        }
        catch (...)
        {
            FreeEnvironmentStringsW(inherited);
            throw;
        }
        FreeEnvironmentStringsW(inherited);

        std::vector<wchar_t> commandLine(
            plan.commandLine.begin(), plan.commandLine.end());
        commandLine.push_back(L'\0');

        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        if (attributeBytes == 0)
        {
            systemError = GetLastError();
            return false;
        }
        std::vector<std::byte> attributeStorage(attributeBytes);
        auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
            attributeStorage.data());
        if (!InitializeProcThreadAttributeList(
                attributes, 1, 0, &attributeBytes))
        {
            systemError = GetLastError();
            return false;
        }
        struct AttributeListCleanup
        {
            PPROC_THREAD_ATTRIBUTE_LIST value;
            ~AttributeListCleanup()
            {
                if (value)
                    DeleteProcThreadAttributeList(value);
            }
        } attributeCleanup{attributes};

        struct HandleCleanup
        {
            HANDLE value = INVALID_HANDLE_VALUE;
            ~HandleCleanup()
            {
                if (value != nullptr && value != INVALID_HANDLE_VALUE)
                    CloseHandle(value);
            }
        } nullInput, nullOutput;

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        nullInput.value = CreateFileW(
            L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nullInput.value == INVALID_HANDLE_VALUE)
        {
            systemError = GetLastError();
            return false;
        }
        nullOutput.value = CreateFileW(
            L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nullOutput.value == INVALID_HANDLE_VALUE)
        {
            systemError = GetLastError();
            return false;
        }

        HANDLE inheritedHandles[] = {nullInput.value, nullOutput.value};
        if (!UpdateProcThreadAttribute(
                attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr))
        {
            systemError = GetLastError();
            return false;
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nullInput.value;
        startup.StartupInfo.hStdOutput = nullOutput.value;
        startup.StartupInfo.hStdError = nullOutput.value;
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION process{};
        constexpr DWORD kCreationFlags =
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
            EXTENDED_STARTUPINFO_PRESENT;

#ifdef HALOMCCVR_TELEMETRY_TESTING
        if (!g_testUseRealAnalyserLauncher)
        {
            if (pyLauncher)
                ++g_testAnalyserLauncherSnapshot.pyProcessAttempts;
            else
                ++g_testAnalyserLauncherSnapshot.pythonProcessAttempts;
            g_testAnalyserLauncherSnapshot.stateAtLastProcessAttempt = LoadState();
            g_testAnalyserLauncherSnapshot.sessionClosedAtLastProcessAttempt =
                g_testSessionClosed;
            g_testAnalyserLauncherSnapshot.standardHandlesValid =
                startup.StartupInfo.hStdInput != nullptr &&
                startup.StartupInfo.hStdInput != INVALID_HANDLE_VALUE &&
                startup.StartupInfo.hStdOutput != nullptr &&
                startup.StartupInfo.hStdOutput != INVALID_HANDLE_VALUE &&
                startup.StartupInfo.hStdError != nullptr &&
                startup.StartupInfo.hStdError != INVALID_HANDLE_VALUE;
            g_testAnalyserLauncherSnapshot.inheritHandles = true;
            g_testAnalyserLauncherSnapshot.restrictedHandleList = true;
            g_testAnalyserLauncherSnapshot.creationFlags = kCreationFlags;
            g_testAnalyserLauncherSnapshot.applicationName = applicationName;
            g_testAnalyserLauncherSnapshot.commandLine = plan.commandLine;
            g_testAnalyserLauncherSnapshot.recordingPath = plan.recordingPath;
            const bool created = pyLauncher
                ? g_testPyLaunchSucceeds : g_testPythonLaunchSucceeds;
            if (!created)
                systemError = ERROR_FILE_NOT_FOUND;
            return created;
        }
#endif
        const bool created = CreateProcessW(
            applicationName.c_str(), commandLine.data(), nullptr, nullptr,
            TRUE, kCreationFlags, environment.data(), workingDirectory.c_str(),
            &startup.StartupInfo, &process) != FALSE;
        if (!created)
            systemError = GetLastError();
        if (!created)
            return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    void TryLaunchTelemetryAnalyser(
        const std::wstring& recordingPath) noexcept
    {
#ifdef HALOMCCVR_TELEMETRY_TESTING
        g_testAnalyserLaunchRequests.fetch_add(1, std::memory_order_relaxed);
        g_testAnalyserLaunchReached.store(true, std::memory_order_release);
        while (g_testPauseAnalyserLaunch.load(std::memory_order_acquire))
            Sleep(1);
        struct CompletionPublisher
        {
            ~CompletionPublisher()
            {
                g_testAnalyserLaunchCompletions.fetch_add(
                    1, std::memory_order_release);
            }
        } completionPublisher;
#endif
        try
        {
            std::wstring moduleDirectory;
            if (!CurrentModuleDirectory(moduleDirectory))
            {
                LOG("telemetry: analyser skipped; DLL directory unavailable (system %lu)",
                    GetLastError());
                return;
            }

            AnalyserLaunchPlan basePlan{};
            if (!BuildAnalyserLaunchPlan(
                    moduleDirectory.c_str(), recordingPath.c_str(),
                    L"python.exe", false, basePlan))
            {
                LOG("telemetry: analyser skipped; launch paths are invalid");
                return;
            }
            const DWORD scriptAttributes = GetFileAttributesW(
                basePlan.scriptPath.c_str());
            if (scriptAttributes == INVALID_FILE_ATTRIBUTES ||
                (scriptAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                LOG("telemetry: analyser skipped; script is unavailable at %ls",
                    basePlan.scriptPath.c_str());
                return;
            }

            uint32_t lastSystemError = ERROR_FILE_NOT_FOUND;
            bool foundInterpreter = false;
            const struct InterpreterChoice
            {
                const wchar_t* filename;
                bool pyLauncher;
                const char* displayName;
            } choices[] = {
                {L"python.exe", false, "python.exe"},
                {L"py.exe", true, "py.exe -3"},
            };
            for (const auto& choice : choices)
            {
                std::wstring interpreter;
                if (!ResolveInterpreterExecutable(
                        choice.filename, choice.pyLauncher, interpreter))
                {
                    continue;
                }
                foundInterpreter = true;
                AnalyserLaunchPlan plan{};
                if (!BuildAnalyserLaunchPlan(
                        moduleDirectory.c_str(), recordingPath.c_str(),
                        interpreter.c_str(), choice.pyLauncher, plan))
                {
                    continue;
                }
                if (StartAnalyserProcess(
                        plan, interpreter, moduleDirectory,
                        choice.pyLauncher, lastSystemError))
                {
                    LOG("telemetry: analyser started with %s for %ls",
                        choice.displayName, recordingPath.c_str());
                    return;
                }
            }
            LOG("telemetry: analyser launch failed; %s (system %u); recording remains valid at %ls",
                foundInterpreter ? "process creation failed"
                                 : "python.exe and py.exe were not found",
                lastSystemError, recordingPath.c_str());
        }
        catch (const std::bad_alloc&)
        {
            LOG("telemetry: analyser skipped; launch allocation failed");
        }
        catch (...)
        {
            LOG("telemetry: analyser skipped; launch preparation failed unexpectedly");
        }
    }

    struct AnalyserLaunchContext
    {
        std::wstring recordingPath;
#ifndef HALOMCCVR_TELEMETRY_TESTING
        HMODULE ownerModule = nullptr;
#endif
    };

    DWORD WINAPI TelemetryAnalyserLaunchWorker(void* rawContext)
    {
        auto* context = static_cast<AnalyserLaunchContext*>(rawContext);
        TryLaunchTelemetryAnalyser(context->recordingPath);
#ifndef HALOMCCVR_TELEMETRY_TESTING
        HMODULE ownerModule = context->ownerModule;
#endif
        delete context;
#ifndef HALOMCCVR_TELEMETRY_TESTING
        FreeLibraryAndExitThread(ownerModule, 0);
#endif
        return 0;
    }

    void QueueTelemetryAnalyserLaunch(
        const std::wstring& recordingPath) noexcept
    {
        try
        {
#ifdef HALOMCCVR_TELEMETRY_TESTING
            g_testAnalyserLauncherSnapshot.stateAtQueue = LoadState();
#endif
            auto* context = new (std::nothrow) AnalyserLaunchContext{
                recordingPath};
            if (!context)
            {
                LOG("telemetry: analyser skipped; launch worker allocation failed");
                return;
            }
#ifndef HALOMCCVR_TELEMETRY_TESTING
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(&g_state),
                    &context->ownerModule))
            {
                const DWORD systemError = GetLastError();
                delete context;
                LOG("telemetry: analyser skipped; module lifetime could not be retained (system %lu)",
                    systemError);
                return;
            }
#endif
            HANDLE worker = CreateThread(
                nullptr, 0, TelemetryAnalyserLaunchWorker,
                context, 0, nullptr);
            if (!worker)
            {
                const DWORD systemError = GetLastError();
#ifndef HALOMCCVR_TELEMETRY_TESTING
                FreeLibrary(context->ownerModule);
#endif
                delete context;
                LOG("telemetry: analyser skipped; launch worker creation failed (system %lu)",
                    systemError);
                return;
            }
            if (!SetThreadPriority(worker, THREAD_PRIORITY_BELOW_NORMAL))
            {
                LOG("telemetry: analyser launch worker priority remained normal (system %lu)",
                    GetLastError());
            }
            CloseHandle(worker);
        }
        catch (...)
        {
            LOG("telemetry: analyser skipped; launch worker preparation failed");
        }
    }

    TelemetryRecorderState LoadState() noexcept
    {
        return static_cast<TelemetryRecorderState>(
            g_state.load(std::memory_order_acquire));
    }

    void StoreState(TelemetryRecorderState state) noexcept
    {
        g_state.store(static_cast<uint8_t>(state), std::memory_order_release);
    }

    void SetProducerAdmission(bool enabled) noexcept
    {
        if (enabled)
            g_admissionToken.fetch_or(
                uint64_t{1}, std::memory_order_seq_cst);
        else
            g_admissionToken.fetch_and(
                ~uint64_t{1}, std::memory_order_seq_cst);
    }

    std::byte* QueueSlot(uint32_t index) noexcept
    {
        return g_queueStorage + static_cast<size_t>(index) *
            sizeof(TelemetryFrame);
    }

    bool PushRing(const TelemetryFrame& frame) noexcept
    {
        const uint32_t head = g_headIndex.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % kTelemetryQueueSlots;
        if (next == g_tailIndex.load(std::memory_order_acquire))
        {
            g_droppedQueueFull.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::memcpy(QueueSlot(head), &frame, sizeof(frame));
        g_headIndex.store(next, std::memory_order_release);
        return true;
    }

    bool PushProducerRing(const TelemetryFrame& frame) noexcept
    {
        const uint32_t head = g_headIndex.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % kTelemetryQueueSlots;
        if (next == g_tailIndex.load(std::memory_order_acquire))
        {
            g_droppedQueueFull.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // Stage the complete record first, then timestamp the accepted queue
        // submission at its publication boundary. The consumer cannot observe
        // this slot until the release-store below.
        std::byte* const slot = QueueSlot(head);
        std::memcpy(slot, &frame, sizeof(frame));
        LARGE_INTEGER captureEnd{};
        QueryPerformanceCounter(&captureEnd);
        std::memcpy(
            slot + offsetof(TelemetryFrame, captureEndQpc),
            &captureEnd.QuadPart, sizeof(captureEnd.QuadPart));
        g_headIndex.store(next, std::memory_order_release);
        return true;
    }

    bool ProducerRingFull() noexcept
    {
        const uint32_t head = g_headIndex.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % kTelemetryQueueSlots;
        return next == g_tailIndex.load(std::memory_order_acquire);
    }

    bool PopRing(TelemetryFrame& frame) noexcept
    {
        const uint32_t tail = g_tailIndex.load(std::memory_order_relaxed);
        if (tail == g_headIndex.load(std::memory_order_acquire))
            return false;
        std::memcpy(&frame, QueueSlot(tail), sizeof(frame));
        g_tailIndex.store(
            (tail + 1) % kTelemetryQueueSlots, std::memory_order_release);
        return true;
    }

    void ResetSessionState() noexcept
    {
        g_headIndex.store(0, std::memory_order_relaxed);
        g_tailIndex.store(0, std::memory_order_relaxed);
        g_producerCalls.store(0, std::memory_order_relaxed);
        g_duplicateSerialSuppressed.store(0, std::memory_order_relaxed);
        g_enqueued.store(0, std::memory_order_relaxed);
        g_written.store(0, std::memory_order_relaxed);
        g_droppedQueueFull.store(0, std::memory_order_relaxed);
        g_writerFailures.store(0, std::memory_order_relaxed);
        g_haveLastProducerSerial = false;
        g_lastProducerPreparedSerial = 0;
        g_weaponEventEnqueued.store(0, std::memory_order_relaxed);
        g_weaponEventWritten.store(0, std::memory_order_relaxed);
        g_weaponEventDropped.store(0, std::memory_order_relaxed);
        g_weaponEventSessionSkipped.store(0, std::memory_order_relaxed);
        g_admissionToken.fetch_add(uint64_t{2}, std::memory_order_seq_cst);
        g_error.store(
            static_cast<uint8_t>(TelemetryErrorCode::None),
            std::memory_order_relaxed);
        g_systemError.store(0, std::memory_order_relaxed);
    }

    void AppendJsonString(std::string& output, const char* value)
    {
        output.push_back('"');
        if (value)
        {
            for (const unsigned char* cursor =
                     reinterpret_cast<const unsigned char*>(value);
                 *cursor; ++cursor)
            {
                const unsigned char c = *cursor;
                switch (c)
                {
                case '"': output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\b': output += "\\b"; break;
                case '\f': output += "\\f"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        constexpr char kHex[] = "0123456789abcdef";
                        output += "\\u00";
                        output.push_back(kHex[(c >> 4) & 0x0f]);
                        output.push_back(kHex[c & 0x0f]);
                    }
                    else
                    {
                        output.push_back(static_cast<char>(c));
                    }
                    break;
                }
            }
        }
        output.push_back('"');
    }

    template <typename Integer>
    void AppendInteger(std::string& output, Integer value)
    {
        char buffer[32]{};
        const auto converted = std::to_chars(
            buffer, buffer + sizeof(buffer), value);
        if (converted.ec == std::errc{})
            output.append(buffer, converted.ptr);
        else
            output += '0';
    }

    void AppendFloat(std::string& output, float value)
    {
        if (!std::isfinite(value))
        {
            output += "null";
            return;
        }
        char buffer[64]{};
        const auto converted = std::to_chars(
            buffer, buffer + sizeof(buffer), value,
            std::chars_format::general,
            std::numeric_limits<float>::max_digits10);
        if (converted.ec == std::errc{})
            output.append(buffer, converted.ptr);
        else
            output += "null";
    }

    void AppendKey(std::string& output, bool& first, const char* key)
    {
        if (!first)
            output.push_back(',');
        first = false;
        AppendJsonString(output, key);
        output.push_back(':');
    }

    void AppendStringField(
        std::string& output, bool& first, const char* key, const char* value)
    {
        AppendKey(output, first, key);
        AppendJsonString(output, value);
    }

    template <typename Integer>
    void AppendIntegerField(
        std::string& output, bool& first, const char* key, Integer value)
    {
        AppendKey(output, first, key);
        AppendInteger(output, value);
    }

    void AppendBoolField(
        std::string& output, bool& first, const char* key, bool value)
    {
        AppendKey(output, first, key);
        output += value ? "true" : "false";
    }

    void AppendFloatField(
        std::string& output, bool& first, const char* key, float value)
    {
        AppendKey(output, first, key);
        AppendFloat(output, value);
    }

    void AppendVec3(
        std::string& output, bool& first, const char* key,
        const TelemetryVec3& value)
    {
        AppendKey(output, first, key);
        output.push_back('[');
        AppendFloat(output, value.x);
        output.push_back(',');
        AppendFloat(output, value.y);
        output.push_back(',');
        AppendFloat(output, value.z);
        output.push_back(']');
    }

    void AppendVec3(
        std::string& output, bool& first, const char* key,
        const AimTraceVec3& value)
    {
        AppendVec3(output, first, key,
            TelemetryVec3{value.x, value.y, value.z});
    }

    // Fixed T-2 shot payload vector (float[3]): identical finite guard to
    // AppendVec3, so a non-finite component renders as JSON null instead of
    // NaN/Infinity. Producers suppress a non-finite final ray instead of
    // publishing it; this guard is the last-resort wire-safety net.
    void AppendShotVec3(
        std::string& output, bool& first, const char* key, const float value[3])
    {
        AppendKey(output, first, key);
        output.push_back('[');
        AppendFloat(output, value[0]);
        output.push_back(',');
        AppendFloat(output, value[1]);
        output.push_back(',');
        AppendFloat(output, value[2]);
        output.push_back(']');
    }

    // Shot slot/barrel wire value: the fixed record stores
    // kTelemetryShotIndexUnknown (0xFF) for "the firing context did not carry
    // it"; the wire contract is -1 so consumers never see the in-memory
    // sentinel, nor a fabricated 0/1 identity.
    int32_t ShotIndexWireValue(uint8_t value)
    {
        return value == kTelemetryShotIndexUnknown ? -1 : int32_t(value);
    }

    void AppendQuat(
        std::string& output, bool& first, const char* key,
        const TelemetryQuat& value)
    {
        AppendKey(output, first, key);
        output.push_back('[');
        AppendFloat(output, value.x);
        output.push_back(',');
        AppendFloat(output, value.y);
        output.push_back(',');
        AppendFloat(output, value.z);
        output.push_back(',');
        AppendFloat(output, value.w);
        output.push_back(']');
    }

    void AppendQuat(
        std::string& output, bool& first, const char* key,
        const AimTraceQuat& value)
    {
        AppendQuat(output, first, key,
            TelemetryQuat{value.x, value.y, value.z, value.w});
    }

    void AppendPose(
        std::string& output, bool& first, const char* key,
        const TelemetryPose& pose)
    {
        AppendKey(output, first, key);
        output.push_back('{');
        bool poseFirst = true;
        AppendVec3(output, poseFirst, "position", pose.position);
        AppendQuat(output, poseFirst, "orientation", pose.orientation);
        output.push_back('}');
    }

    void AppendEffectiveSettings(
        std::string& output, bool& first, const char* key,
        const TelemetryEffectiveSettings& settings)
    {
        AppendKey(output, first, key);
        output.push_back('{');
        bool nested = true;
        AppendBoolField(output, nested, "two_hand_enabled", settings.twoHandEnabled);
        AppendBoolField(output, nested, "two_hand_latched", settings.twoHandLatched);
        AppendBoolField(output, nested, "two_hand_toggle", settings.twoHandToggle);
        AppendBoolField(output, nested, "virtual_stock_enabled", settings.virtualStockEnabled);
        AppendIntegerField(output, nested, "rear_reference", settings.virtualStockRearReference);
        AppendFloatField(output, nested, "stock_strength", settings.virtualStockStrength);
        AppendIntegerField(output, nested, "hybrid_diagnostic_override", settings.hybridDiagnosticOverride);
        AppendFloatField(output, nested, "hybrid_offhand_influence", settings.hybridOffhandInfluence);
        AppendIntegerField(output, nested, "hybrid_ads_reference", settings.hybridAdsReference);
        AppendFloatField(output, nested, "hybrid_seat_full_m", settings.hybridSeatFullM);
        AppendFloatField(output, nested, "hybrid_seat_release_m", settings.hybridSeatReleaseM);
        AppendBoolField(output, nested, "horizontal_release_enabled", settings.horizontalReleaseEnabled);
        AppendFloatField(output, nested, "horizontal_release_full_m", settings.horizontalReleaseFullM);
        AppendFloatField(output, nested, "horizontal_release_release_m", settings.horizontalReleaseReleaseM);
        AppendBoolField(output, nested, "hybrid_inverse_neck_enabled", settings.inverseNeckEnabled);
        AppendFloatField(output, nested, "hybrid_inverse_neck_strength", settings.inverseNeckStrength);
        AppendFloatField(output, nested, "hybrid_inverse_neck_forward_m", settings.inverseNeckForwardM);
        AppendFloatField(output, nested, "hybrid_inverse_neck_up_m", settings.inverseNeckUpM);
        AppendFloatField(output, nested, "hybrid_inverse_neck_lateral_m", settings.inverseNeckLateralM);
        AppendFloatField(output, nested, "rear_height_m", settings.rearHeightM);
        AppendFloatField(output, nested, "shoulder_back_m", settings.shoulderBackM);
        AppendFloatField(output, nested, "shoulder_side_m", settings.shoulderSideM);
        AppendFloatField(output, nested, "chest_height_m", settings.chestHeightM);
        AppendFloatField(output, nested, "chest_back_m", settings.chestBackM);
        AppendFloatField(output, nested, "chest_side_m", settings.chestSideM);
        AppendBoolField(output, nested, "old_fixed_proximity_enabled", settings.proximityRelease);
        AppendFloatField(output, nested, "old_fixed_proximity_full_m", settings.proximityFullM);
        AppendFloatField(output, nested, "old_fixed_proximity_release_m", settings.proximityReleaseM);
        AppendFloatField(output, nested, "gun_yaw_deg", settings.gunYawDeg);
        AppendFloatField(output, nested, "gun_pitch_deg", settings.gunPitchDeg);
        AppendFloatField(output, nested, "gun_roll_deg", settings.gunRollDeg);
        AppendBoolField(output, nested, "support_grip_pose_enabled", settings.supportGripPoseEnabled);
        AppendBoolField(output, nested, "support_endpoint_used_grip", settings.supportEndpointUsedGrip);
        AppendBoolField(output, nested, "left_handed", settings.leftHanded);
        AppendFloatField(output, nested, "aim_stabilization", settings.aimStabilization);
        AppendFloatField(output, nested, "crosshair_distance_m", settings.crosshairDistanceM);
        AppendFloatField(output, nested, "crosshair_size_deg", settings.crosshairSizeDeg);
        AppendBoolField(output, nested, "crosshair", settings.crosshair);
        AppendBoolField(output, nested, "kill_reticle", settings.killReticle);
        output.push_back('}');
    }

    void AppendAimResult(
        std::string& output, bool& first, const char* key,
        const TelemetryAimResult& aim)
    {
        AppendKey(output, first, key);
        output.push_back('{');
        bool nested = true;
        AppendBoolField(output, nested, "valid", aim.valid);
        AppendPose(output, nested, "pose", aim.pose);
        AppendVec3(output, nested, "forward", aim.forward);
        AppendBoolField(output, nested, "two_hand_active", aim.twoHandActive);
        AppendBoolField(output, nested, "rejected_extreme", aim.rejectedExtreme);
        AppendFloatField(output, nested, "rejected_agreement", aim.rejectedAgreement);
        output.push_back('}');
    }

    void AppendAimTrace(
        std::string& output, bool& first, const AimPoseTrace& trace)
    {
        AppendKey(output, first, "aim_trace");
        output.push_back('{');
        bool nested = true;
        AppendIntegerField(output, nested, "path", static_cast<uint8_t>(trace.path));
        AppendBoolField(output, nested, "a_valid", trace.aValid);
        AppendQuat(output, nested, "primary_quaternion", trace.primaryQuaternion);
        AppendVec3(output, nested, "a_direction", trace.primaryDirection);
        AppendBoolField(output, nested, "b_attempted", trace.bAttempted);
        AppendBoolField(output, nested, "b_accepted", trace.bAccepted);
        AppendVec3(output, nested, "b_direction", trace.bDirection);
        AppendFloatField(output, nested, "a_dot_b", trace.bAgreement);
        AppendBoolField(output, nested, "b_extreme_rejected", trace.bExtremeRejected);
        AppendFloatField(output, nested, "b_rejected_agreement", trace.bRejectedAgreement);
        // T13 corrective: true when this attempt was accepted by the retained
        // rule (engaged+trusted persistent grip, Virtual Stock off) after
        // crossing the legacy 0.35 agreement floor; a_dot_b carries the
        // crossed value. Additive field: existing consumers read by name.
        AppendBoolField(output, nested, "b_steering_retained", trace.bSteeringRetained);
        AppendBoolField(output, nested, "hip_blend_attempted", trace.hipBlendAttempted);
        AppendBoolField(output, nested, "hip_blend_succeeded", trace.hipBlendSucceeded);
        AppendBoolField(output, nested, "hip_aim_valid", trace.hipAimValid);
        AppendVec3(output, nested, "hip_aim_direction", trace.hipAimDirection);
        AppendFloatField(output, nested, "offhand_influence_used", trace.offhandInfluenceUsed);
        AppendBoolField(output, nested, "exact_a_endpoint_selected", trace.exactAEndpointSelected);
        AppendIntegerField(output, nested, "requested_stock_target", static_cast<uint8_t>(trace.requestedTarget));
        AppendIntegerField(output, nested, "actual_stock_target", static_cast<uint8_t>(trace.actualTarget));
        AppendBoolField(output, nested, "shoulder_to_head_fallback", trace.shoulderToHeadFallback);
        AppendBoolField(output, nested, "target_valid", trace.targetValid);
        AppendVec3(output, nested, "target", trace.target);
        AppendBoolField(output, nested, "inverse_neck_attempted", trace.inverseNeckAttempted);
        AppendBoolField(output, nested, "inverse_neck_valid", trace.inverseNeckValid);
        AppendBoolField(output, nested, "inverse_neck_neutral_valid", trace.inverseNeckNeutralValid);
        AppendFloatField(output, nested, "inverse_neck_strength", trace.inverseNeckStrength);
        if (trace.inverseNeckNeutralValid)
        {
            AppendQuat(output, nested, "inverse_neck_neutral_orientation", trace.inverseNeckNeutralOrientation);
            AppendIntegerField(output, nested, "inverse_neck_neutral_capture_serial", trace.inverseNeckNeutralCaptureSerial);
            AppendIntegerField(output, nested, "inverse_neck_neutral_capture_contact_space_epoch", trace.inverseNeckNeutralCaptureContactSpaceEpoch);
        }
        if (trace.inverseNeckValid)
        {
            AppendVec3(output, nested, "inverse_neck_current_offset", trace.inverseNeckCurrentOffset);
            AppendVec3(output, nested, "inverse_neck_neutral_offset", trace.inverseNeckNeutralOffset);
            AppendVec3(output, nested, "inverse_neck_predicted_orbit", trace.inverseNeckPredictedOrbit);
            AppendBoolField(output, nested, "inverse_neck_correction_clamped", trace.inverseNeckCorrectionClamped);
            AppendVec3(output, nested, "inverse_neck_applied_correction", trace.inverseNeckAppliedCorrection);
            AppendVec3(output, nested, "inverse_neck_corrected_head_position", trace.inverseNeckCorrectedHeadPosition);
        }
        AppendBoolField(output, nested, "raw_head_target_valid", trace.rawHeadTargetValid);
        if (trace.rawHeadTargetValid)
            AppendVec3(output, nested, "raw_head_target", trace.rawHeadTarget);
        AppendBoolField(output, nested, "corrected_head_target_valid", trace.correctedHeadTargetValid);
        if (trace.correctedHeadTargetValid)
            AppendVec3(output, nested, "corrected_head_target", trace.correctedHeadTarget);
        AppendBoolField(output, nested, "c_attempted", trace.cAttempted);
        AppendBoolField(output, nested, "c_valid", trace.cValid);
        AppendBoolField(output, nested, "virtual_rear_valid", trace.virtualRearValid);
        AppendVec3(output, nested, "virtual_rear", trace.virtualRear);
        AppendVec3(output, nested, "c_direction", trace.cDirection);
        AppendFloatField(output, nested, "stock_strength_used", trace.stockStrengthUsed);
        AppendBoolField(output, nested, "seat_attempted", trace.seatAttempted);
        AppendBoolField(output, nested, "seat_valid", trace.seatValid);
        AppendFloatField(output, nested, "seat_segment_length_m", trace.seatSegmentLength);
        AppendFloatField(output, nested, "seat_raw_projection", trace.seatRawProjection);
        AppendFloatField(output, nested, "seat_clamped_projection", trace.seatClampedProjection);
        AppendBoolField(output, nested, "seat_closest_valid", trace.seatClosestValid);
        AppendVec3(output, nested, "seat_closest", trace.seatClosest);
        AppendFloatField(output, nested, "seat_error_m", trace.seatErrorM);
        AppendBoolField(output, nested, "w_natural_valid", trace.wNaturalValid);
        AppendFloatField(output, nested, "w_natural", trace.wNatural);
        AppendBoolField(output, nested, "w_after_diagnostic_valid", trace.wAfterDiagnosticValid);
        AppendFloatField(output, nested, "w_after_diagnostic", trace.wAfterDiagnostic);
        AppendBoolField(output, nested, "horizontal_release_attempted", trace.horizontalReleaseAttempted);
        AppendBoolField(output, nested, "horizontal_release_valid", trace.horizontalReleaseValid);
        AppendFloatField(output, nested, "rear_horizontal_reach_m", trace.rearHorizontalReachM);
        AppendFloatField(output, nested, "horizontal_release_influence", trace.horizontalReleaseInfluence);
        AppendBoolField(output, nested, "release_geometry_valid", trace.releaseGeometryValid);
        AppendFloatField(output, nested, "rear_to_stock_target_distance_m", trace.rearToStockTargetDistanceM);
        AppendFloatField(output, nested, "primary_to_support_distance_m", trace.primaryToSupportDistanceM);
        AppendFloatField(output, nested, "stock_to_support_distance_m", trace.stockToSupportDistanceM);
        AppendIntegerField(output, nested, "effective_diagnostic_override", trace.effectiveDiagnosticOverride);
        AppendBoolField(output, nested, "w_effective_valid", trace.wEffectiveValid);
        AppendFloatField(output, nested, "w_effective", trace.wEffective);
        AppendBoolField(output, nested, "override_applied", trace.overrideApplied);
        AppendBoolField(output, nested, "force_stock_eligible", trace.forceStockEligible);
        AppendBoolField(output, nested, "stock_blend_attempted", trace.stockBlendAttempted);
        AppendBoolField(output, nested, "stock_blend_succeeded", trace.stockBlendSucceeded);
        AppendBoolField(output, nested, "stock_contributed", trace.stockContributed);
        AppendBoolField(output, nested, "final_direction_valid", trace.finalDirectionValid);
        AppendVec3(output, nested, "final_direction", trace.finalDirection);
        AppendBoolField(output, nested, "orientation_rebuild_attempted", trace.orientationRebuildAttempted);
        AppendBoolField(output, nested, "orientation_rebuild_succeeded", trace.orientationRebuildSucceeded);
        AppendBoolField(output, nested, "final_calibration_valid", trace.finalCalibrationValid);
        AppendBoolField(output, nested, "fixed_target_valid", trace.fixedTargetValid);
        AppendVec3(output, nested, "fixed_target", trace.fixedTarget);
        AppendBoolField(output, nested, "fixed_rear_distance_valid", trace.fixedRearDistanceValid);
        AppendFloatField(output, nested, "fixed_rear_to_target_distance_m", trace.fixedRearToTargetDistanceM);
        AppendBoolField(output, nested, "fixed_proximity_enabled", trace.fixedProximityEnabled);
        AppendBoolField(output, nested, "fixed_proximity_calculated", trace.fixedProximityCalculated);
        AppendFloatField(output, nested, "fixed_proximity_influence", trace.fixedProximityInfluence);
        AppendFloatField(output, nested, "fixed_configured_strength", trace.fixedConfiguredStrength);
        AppendFloatField(output, nested, "fixed_effective_strength", trace.fixedEffectiveStrength);
        AppendBoolField(output, nested, "fixed_direction_valid", trace.fixedDirectionValid);
        output.push_back('}');
    }

    void AppendControl(
        std::string& output, bool& first, const char* key,
        const TelemetryControlResult& control)
    {
        AppendKey(output, first, key);
        output.push_back('{');
        bool nested = true;
        AppendIntegerField(output, nested, "profile_id", control.profileId);
        AppendStringField(output, nested, "profile_name",
            VirtualStockTestProfileName(static_cast<VirtualStockTestProfile>(
                control.profileId)));
        AppendEffectiveSettings(output, nested, "effective_settings",
            control.effectiveSettings);
        AppendAimResult(output, nested, "aim", control.aim);
        AppendIntegerField(output, nested, "path", control.path);
        AppendIntegerField(output, nested, "requested_stock_target", control.requestedTarget);
        AppendIntegerField(output, nested, "actual_stock_target", control.actualTarget);
        AppendBoolField(output, nested, "shoulder_to_head_fallback", control.shoulderToHeadFallback);
        AppendBoolField(output, nested, "fixed_target_valid", control.fixedTargetValid);
        AppendVec3(output, nested, "fixed_target", control.fixedTarget);
        AppendBoolField(output, nested, "fixed_rear_distance_valid", control.fixedRearDistanceValid);
        AppendFloatField(output, nested, "fixed_rear_to_target_distance_m", control.fixedRearToTargetDistanceM);
        AppendBoolField(output, nested, "fixed_proximity_enabled", control.fixedProximityEnabled);
        AppendBoolField(output, nested, "fixed_proximity_calculated", control.fixedProximityCalculated);
        AppendFloatField(output, nested, "fixed_proximity_influence", control.fixedProximityInfluence);
        AppendFloatField(output, nested, "fixed_configured_strength", control.fixedConfiguredStrength);
        AppendFloatField(output, nested, "fixed_effective_strength", control.fixedEffectiveStrength);
        AppendBoolField(output, nested, "fixed_direction_valid", control.fixedDirectionValid);
        AppendBoolField(output, nested, "exact_a_endpoint_selected", control.exactAEndpointSelected);
        AppendBoolField(output, nested, "orientation_rebuild_attempted", control.orientationRebuildAttempted);
        AppendBoolField(output, nested, "orientation_rebuild_succeeded", control.orientationRebuildSucceeded);
        output.push_back('}');
    }

    void SerializeFrameLine(const TelemetryFrame& frame, std::string& output)
    {
        output.clear();
        output.reserve(16384);
        output.push_back('{');
        bool first = true;
        AppendStringField(output, first, "type", "frame");
        AppendIntegerField(output, first, "schema_version", frame.schemaVersion);
        AppendIntegerField(output, first, "prepared_serial", frame.preparedSerial);
        AppendIntegerField(output, first, "predicted_display_time", frame.predictedDisplayTime);
        AppendIntegerField(output, first, "predicted_display_period", frame.predictedDisplayPeriod);
        AppendIntegerField(output, first, "capture_begin_qpc", frame.captureBeginQpc);
        AppendIntegerField(output, first, "capture_end_qpc", frame.captureEndQpc);
        AppendIntegerField(output, first, "active_title", frame.activeTitle);
        AppendIntegerField(output, first, "openxr_session_state", frame.sessionState);
        AppendBoolField(output, first, "should_render", frame.shouldRender);
        AppendBoolField(output, first, "upcoming_views_valid", frame.upcomingViewsValid);
        AppendIntegerField(output, first, "located_view_count", frame.locatedViewCount);
        AppendBoolField(output, first, "focused", frame.focused);
        AppendBoolField(output, first, "stereo_enabled", frame.stereoEnabled);
        AppendBoolField(output, first, "menu_open", frame.menuOpen);
        AppendIntegerField(output, first, "contact_space_epoch", frame.contactSpaceEpoch);
        AppendIntegerField(output, first, "contact_space_change_at_ns", frame.contactSpaceChangeAtNs);

        AppendBoolField(output, first, "semantic_primary_aim_valid", frame.semanticPrimaryValid);
        AppendPose(output, first, "semantic_primary_aim", frame.semanticPrimaryAim);
        AppendVec3(output, first, "semantic_primary_forward", frame.semanticPrimaryForward);
        AppendBoolField(output, first, "semantic_support_aim_valid", frame.semanticSupportValid);
        AppendPose(output, first, "semantic_support_aim", frame.semanticSupportAim);
        AppendBoolField(output, first, "semantic_support_endpoint_valid", frame.supportEndpointValid);
        AppendVec3(output, first, "semantic_support_endpoint", frame.supportEndpoint);
        AppendBoolField(output, first, "support_endpoint_used_grip", frame.supportEndpointUsedGrip);
        AppendBoolField(output, first, "physical_left_aim_valid", frame.physicalLeftAimValid);
        AppendPose(output, first, "physical_left_aim", frame.physicalLeftAim);
        AppendBoolField(output, first, "physical_right_aim_valid", frame.physicalRightAimValid);
        AppendPose(output, first, "physical_right_aim", frame.physicalRightAim);
        AppendBoolField(output, first, "support_grip_valid", frame.supportGripValid);
        AppendVec3(output, first, "support_grip_position", frame.supportGripPosition);
        AppendBoolField(output, first, "semantic_primary_grip_position_valid",
            frame.semanticPrimaryGripPositionValid);
        AppendVec3(output, first, "semantic_primary_grip_position",
            frame.semanticPrimaryGripPosition);
        AppendBoolField(output, first, "head_sample_valid", frame.headSampleValid);
        AppendBoolField(output, first, "stock_head_valid", frame.stockHeadValid);
        AppendPose(output, first, "semantic_hmd", frame.semanticHmd);
        AppendFloatField(output, first, "headset_smoothing", frame.headsetSmoothing);

        AppendKey(output, first, "views");
        output.push_back('[');
        for (int eye = 0; eye < 2; ++eye)
        {
            if (eye)
                output.push_back(',');
            output.push_back('{');
            bool viewFirst = true;
            AppendPose(output, viewFirst, "pose", frame.views[eye].pose);
            AppendKey(output, viewFirst, "fov");
            output.push_back('[');
            AppendFloat(output, frame.views[eye].fovLeft);
            output.push_back(',');
            AppendFloat(output, frame.views[eye].fovRight);
            output.push_back(',');
            AppendFloat(output, frame.views[eye].fovUp);
            output.push_back(',');
            AppendFloat(output, frame.views[eye].fovDown);
            output += "]}";
        }
        output.push_back(']');

        AppendBoolField(output, first, "semantic_primary_linear_velocity_valid", frame.semanticPrimaryVelocityValid);
        AppendVec3(output, first, "semantic_primary_linear_velocity", frame.semanticPrimaryVelocity);
        AppendIntegerField(output, first, "semantic_primary_linear_velocity_at_ms", frame.semanticPrimaryVelocityAtMs);
        AppendBoolField(output, first, "semantic_support_linear_velocity_valid", frame.semanticSupportVelocityValid);
        AppendVec3(output, first, "semantic_support_linear_velocity", frame.semanticSupportVelocity);
        AppendIntegerField(output, first, "semantic_support_linear_velocity_at_ms", frame.semanticSupportVelocityAtMs);

        AppendKey(output, first, "pad");
        output.push_back('{');
        bool padFirst = true;
        AppendBoolField(output, padFirst, "valid", frame.pad.valid);
        AppendFloatField(output, padFirst, "moveX", frame.pad.moveX);
        AppendFloatField(output, padFirst, "moveY", frame.pad.moveY);
        AppendFloatField(output, padFirst, "turnX", frame.pad.turnX);
        AppendFloatField(output, padFirst, "turnY", frame.pad.turnY);
        AppendFloatField(output, padFirst, "trigL", frame.pad.trigL);
        AppendFloatField(output, padFirst, "trigR", frame.pad.trigR);
        AppendFloatField(output, padFirst, "gripL", frame.pad.gripL);
        AppendFloatField(output, padFirst, "gripR", frame.pad.gripR);
        AppendBoolField(output, padFirst, "a", frame.pad.a);
        AppendBoolField(output, padFirst, "b", frame.pad.b);
        AppendBoolField(output, padFirst, "x", frame.pad.x);
        AppendBoolField(output, padFirst, "y", frame.pad.y);
        AppendBoolField(output, padFirst, "clickL", frame.pad.clickL);
        AppendBoolField(output, padFirst, "clickR", frame.pad.clickR);
        AppendBoolField(output, padFirst, "menu", frame.pad.menu);
        AppendBoolField(output, padFirst, "thumbrestDpad", frame.pad.thumbrestDpad);
        AppendFloatField(output, padFirst, "dpadX", frame.pad.dpadX);
        AppendFloatField(output, padFirst, "dpadY", frame.pad.dpadY);
        AppendBoolField(output, padFirst, "exclusiveInput", frame.pad.exclusiveInput);
        AppendIntegerField(output, padFirst, "weapon_buttons", frame.pad.weaponButtons);
        AppendIntegerField(output, padFirst, "weapon_pulse_until_ms", frame.pad.weaponPulseUntilMs);
        AppendIntegerField(output, padFirst, "weapon_generation", frame.pad.weaponGeneration);
        output.push_back('}');

        AppendIntegerField(output, first, "test_profile_id", frame.testProfileId);
        AppendStringField(output, first, "test_profile_name",
            VirtualStockTestProfileName(static_cast<VirtualStockTestProfile>(
                frame.testProfileId)));
        AppendBoolField(output, first, "test_profile_custom", frame.testProfileCustom);
        AppendEffectiveSettings(output, first, "effective_settings", frame.effectiveSettings);
        AppendAimTrace(output, first, frame.aimTrace);
        AppendAimResult(output, first, "canonical_aim", frame.canonicalAim);
        AppendIntegerField(output, first, "transition_stock_mode", frame.transitionStockMode);
        AppendBoolField(output, first, "transition_stock_mode_valid",
            frame.transitionStockModeValid);
        AppendBoolField(output, first, "transition_active", frame.transitionActive);
        AppendIntegerField(output, first, "transition_phase", frame.transitionPhase);
        AppendIntegerField(output, first, "transition_edge_kind", frame.transitionEdgeKind);
        AppendIntegerField(output, first, "transition_anchor_source", frame.transitionAnchorSource);
        AppendBoolField(output, first, "transition_live_calibrated_forward_valid",
            frame.transitionLiveCalibratedForwardValid);
        AppendVec3(output, first, "transition_live_calibrated_forward",
            frame.transitionLiveCalibratedForward);
        AppendBoolField(output, first, "transition_presented_forward_valid",
            frame.transitionPresentedForwardValid);
        AppendVec3(output, first, "transition_presented_forward",
            frame.transitionPresentedForward);
        AppendFloatField(output, first, "transition_initial_correction_deg",
            frame.transitionInitialCorrectionDeg);
        AppendFloatField(output, first, "transition_remaining_correction_deg",
            frame.transitionRemainingCorrectionDeg);
        AppendFloatField(output, first, "transition_elapsed_ms",
            frame.transitionElapsedMs);
        AppendBoolField(output, first, "transition_one_hand_anchor_valid",
            frame.transitionOneHandAnchorValid);
        AppendVec3(output, first, "transition_one_hand_anchor_forward",
            frame.transitionOneHandAnchorForward);
        AppendIntegerField(output, first, "transition_advance_count",
            frame.transitionAdvanceCount);
        AppendIntegerField(output, first, "transition_last_prepared_serial",
            frame.transitionLastPreparedSerial);
        AppendIntegerField(output, first, "transition_applied_serial",
            frame.transitionAppliedSerial);
        AppendBoolField(output, first,
            "two_hand_transition_smoothing_configured",
            frame.twoHandTransitionSmoothingConfigured);
        AppendBoolField(output, first, "two_hand_transition_active",
            frame.twoHandTransitionActive);
        AppendBoolField(output, first, "two_hand_smoothing_configured",
            frame.twoHandSmoothingConfigured);
        AppendBoolField(output, first, "two_hand_smoothing_applied",
            frame.twoHandSmoothingApplied);
        // The free two-hand product setting the frame's own assembly carried,
        // recorded beside the input-smoothing family: it is the offhand
        // directional authority the free two-hand solver consumes (never read
        // by Virtual Stock solves), frozen for this prepared serial.
        AppendFloatField(output, first, "two_hand_offhand_influence",
            frame.twoHandOffhandInfluence);
        AppendFloatField(output, first, "two_hand_smoothing_strength",
            frame.twoHandSmoothingStrength);
        // The wet/dry mix is a pure function of the same frozen strength, so it
        // is emitted here rather than stored: strength 0 reads exactly 0 and
        // strength 25 reads exactly 1.
        AppendFloatField(output, first, "two_hand_smoothing_mix",
            two_hand_input_smoothing::StrengthMix(
                frame.twoHandSmoothingStrength));
        AppendFloatField(output, first, "two_hand_smoothing_alpha",
            frame.twoHandSmoothingAlpha);
        AppendFloatField(output, first,
            "two_hand_smoothing_primary_orientation_error_deg",
            frame.twoHandSmoothingPrimaryOrientationErrorDeg);
        AppendFloatField(output, first,
            "two_hand_smoothing_primary_position_error_m",
            frame.twoHandSmoothingPrimaryPositionErrorM);
        AppendFloatField(output, first,
            "two_hand_smoothing_support_position_error_m",
            frame.twoHandSmoothingSupportPositionErrorM);
        AppendBoolField(output, first, "two_hand_lab_enabled",
            frame.twoHandLabEnabled);
        AppendIntegerField(output, first, "two_hand_lab_anchor_requested",
            frame.twoHandLabAnchorRequested);
        AppendIntegerField(output, first, "two_hand_lab_anchor_resolved",
            frame.twoHandLabAnchorResolved);
        AppendIntegerField(output, first, "two_hand_lab_anchor_fallback",
            frame.twoHandLabAnchorFallback);
        AppendFloatField(output, first, "two_hand_lab_offhand_influence",
            frame.twoHandLabOffhandInfluence);
        AppendIntegerField(output, first, "two_hand_lab_agreement_mode",
            frame.twoHandLabAgreementMode);
        AppendFloatField(output, first, "two_hand_lab_agreement",
            frame.twoHandLabAgreement);
        AppendFloatField(output, first, "two_hand_lab_agreement_confidence",
            frame.twoHandLabAgreementConfidence);
        AppendFloatField(output, first, "two_hand_lab_effective_influence",
            frame.twoHandLabEffectiveInfluence);
        AppendIntegerField(output, first, "two_hand_lab_temporal_mode",
            frame.twoHandLabTemporalMode);
        AppendBoolField(output, first, "two_hand_lab_primary_pivot_valid",
            frame.twoHandLabPrimaryPivotValid);
        AppendVec3(output, first, "two_hand_lab_primary_pivot",
            frame.twoHandLabPrimaryPivot);
        AppendBoolField(output, first, "two_hand_lab_support_pivot_valid",
            frame.twoHandLabSupportPivotValid);
        AppendVec3(output, first, "two_hand_lab_support_pivot",
            frame.twoHandLabSupportPivot);
        AppendBoolField(output, first,
            "two_hand_lab_stateless_direction_valid",
            frame.twoHandLabStatelessDirectionValid);
        AppendVec3(output, first, "two_hand_lab_stateless_direction",
            frame.twoHandLabStatelessDirection);
        AppendBoolField(output, first,
            "two_hand_lab_presented_direction_valid",
            frame.twoHandLabPresentedDirectionValid);
        AppendVec3(output, first, "two_hand_lab_presented_direction",
            frame.twoHandLabPresentedDirection);
        AppendBoolField(output, first, "two_hand_lab_temporal_active",
            frame.twoHandLabTemporalActive);
        AppendFloatField(output, first, "two_hand_lab_temporal_error_deg",
            frame.twoHandLabTemporalErrorDeg);
        AppendBoolField(output, first, "presented_aim_valid",
            frame.presentedAimValid);
        AppendVec3(output, first, "presented_aim_forward",
            frame.presentedAimForward);
        AppendBoolField(output, first, "reticle_presented_valid",
            frame.reticlePresentedValid);
        AppendIntegerField(output, first, "reticle_presented_serial",
            frame.reticlePresentedSerial);
        AppendIntegerField(output, first, "reticle_presented_sample_ms",
            frame.reticlePresentedSampleMs);
        AppendIntegerField(output, first, "reticle_presented_support_epoch",
            frame.reticlePresentedSupportEpoch);
        AppendBoolField(output, first, "reticle_presented_support_trusted",
            frame.reticlePresentedSupportTrusted);
        AppendQuat(output, first, "reticle_presented_orientation",
            frame.reticlePresentedOrientation);
        AppendVec3(output, first, "reticle_presented_position",
            frame.reticlePresentedPosition);
        AppendBoolField(output, first, "engine_aim_valid",
            frame.engineAimValid);
        AppendIntegerField(output, first, "engine_aim_source",
            frame.engineAimSource);
        AppendVec3(output, first, "engine_aim_forward",
            frame.engineAimForward);
        AppendIntegerField(output, first, "engine_aim_serial",
            frame.engineAimSerial);
        AppendIntegerField(output, first, "engine_aim_sample_ms",
            frame.engineAimSampleMs);
        AppendBoolField(output, first, "engine_aim_pitch_valid",
            frame.engineAimPitchValid);
        AppendFloatField(output, first, "engine_aim_pitch_deg",
            frame.engineAimPitchDeg);
        AppendBoolField(output, first, "engine_camera_base_valid",
            frame.engineCameraBaseValid);
        AppendVec3(output, first, "engine_camera_base_position",
            frame.engineCameraBasePosition);
        AppendBoolField(output, first, "engine_camera_eye_valid",
            frame.engineCameraEyeValid);
        AppendVec3(output, first, "engine_camera_eye_position",
            frame.engineCameraEyePosition);
        AppendIntegerField(output, first, "engine_camera_source",
            frame.engineCameraSource);
        AppendIntegerField(output, first, "engine_camera_serial",
            frame.engineCameraSerial);
        AppendBoolField(output, first, "world_scale_valid",
            frame.worldScaleValid);
        AppendFloatField(output, first, "world_scale",
            frame.worldScale);
        AppendBoolField(output, first, "dual_active",
            frame.dualActive);
        // Persistent support grip (PG) frozen solve-time provenance. All eight
        // fields are always emitted by a current recorder; a legacy recording
        // simply lacks the keys (absence is unambiguous, never a fabricated
        // zero). Semantics: configured/applicable are this frame's config knob
        // and the pure title gate; readable/engaged/epoch/trusted/forceOneHand
        // are the frame-local assembly qualification (support_solve_trusted is
        // the PERMISSION bit, before any consumption narrowing); epoch is a
        // plain integer so the 2^64-1 "unreadable" sentinel survives the wire;
        // support_solve_serial is the assembly's stamped solve serial (the last
        // published prepared serial at capture, normally prepared_serial - 1,
        // never asserted equal to it).
        AppendBoolField(output, first, "persistent_support_grip_configured",
            frame.persistentSupportGripConfigured);
        AppendBoolField(output, first, "persistent_support_grip_applicable",
            frame.persistentSupportGripApplicable);
        AppendBoolField(output, first, "support_relationship_readable",
            frame.supportRelationshipReadable);
        AppendBoolField(output, first, "support_relationship_engaged",
            frame.supportRelationshipEngaged);
        AppendIntegerField(output, first, "support_epoch",
            frame.supportEpoch);
        AppendBoolField(output, first, "support_solve_trusted",
            frame.supportSolveTrusted);
        AppendBoolField(output, first, "support_force_one_hand",
            frame.supportForceOneHand);
        AppendIntegerField(output, first, "support_solve_serial",
            frame.supportSolveSerial);
        AppendControl(output, first, "cf_vs_off", frame.cfVsOff);
        AppendControl(output, first, "cf_fixed_head", frame.cfFixedHead);
        AppendControl(output, first, "cf_fixed_shoulder", frame.cfFixedShoulder);
        output.push_back('}');
    }

    void SerializeSessionStartLine(
        const WorkerSession& session, const SYSTEMTIME& utc,
        int64_t frequency, std::string& output)
    {
        char wallClock[40]{};
        std::snprintf(wallClock, sizeof(wallClock),
            "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
            utc.wSecond, utc.wMilliseconds);

        output.clear();
        output.reserve(4096);
        output.push_back('{');
        bool first = true;
        AppendStringField(output, first, "type", "session_start");
        AppendIntegerField(output, first, "schema_version", kTelemetrySchemaVersion);
        AppendStringField(output, first, "build_commit", HALOMCCVR_BUILD_COMMIT);
        AppendIntegerField(output, first, "process_id", GetCurrentProcessId());
        AppendStringField(output, first, "start_wall_clock_utc", wallClock);
        AppendStringField(output, first, "file", session.basename.c_str());
        AppendIntegerField(output, first, "qpc_frequency", frequency);
        AppendIntegerField(output, first, "ring_slots", kTelemetryQueueSlots);
        AppendIntegerField(output, first, "ring_usable_capacity", kTelemetryQueueUsableCapacity);
        AppendStringField(output, first, "drop_policy", "drop_new");
        AppendIntegerField(output, first, "worker_poll_ms", kWorkerPollMs);
        AppendStringField(output, first, "tracking_space", "OpenXR LOCAL");
        AppendStringField(output, first, "position_units", "metres");
        AppendStringField(output, first, "quaternion_order", "x,y,z,w");
        AppendStringField(output, first, "coordinate_handedness", "OpenXR right-handed");
        AppendStringField(output, first, "up_axis", "+Y");
        AppendStringField(output, first, "forward_axis", "-Z");
        AppendStringField(output, first, "semantic_primary_support",
            "aim-action poses after MCC handedness role routing");
        AppendStringField(output, first, "physical_left_right",
            "aim-action poses before semantic handedness role routing");
        AppendStringField(output, first, "semantic_hmd",
            "view-space pose located in LOCAL, normalised, then MCC headset smoothing applied");
        AppendStringField(output, first, "stereo_views",
            "xrLocateViews results in LOCAL at the same predicted display time");
        AppendStringField(output, first, "support_grip_endpoint",
            "position-only grip-action locate, semantically routed to support, and the sample the support-endpoint selector may select for semantic_support_endpoint; support_endpoint_used_grip reports which source that selector chose for the Virtual Stock support endpoint (\"Reduce Support-Hand Rotation\") and for the Two-Hand Lab Production anchor pass-through. This selection is provenance for those VS/Lab paths only, never a consumption receipt for the free two-hand product geometry: with Virtual Stock off (and the Lab inactive) the two-hand solve consumes the fixed primary-Grip -> support-Grip positional pair reported by support_grip_position / semantic_primary_grip_position and does not read this selection at all");
        AppendStringField(output, first, "semantic_grip_positions",
            "position-only OpenXR grip-action locates in LOCAL at controller sample time, routed through MCC semantic primary/support handedness roles; each position is valid only when its corresponding validity flag is true");
        AppendStringField(output, first, "frame_field_domains",
            "presented_aim_forward, reticle_presented_orientation/position, canonical_aim and all semantic/physical/grip/head/view fields are OpenXR LOCAL metres (right-handed, +Y up, -Z forward); engine_aim_forward, engine_camera_base_position and engine_camera_eye_position are ENGINE-WORLD Blam Z-up world units, never OpenXR LOCAL");
        AppendStringField(output, first, "engine_world_axes",
            "Blam Z-up; an engine-world forward is (cos p cos y, cos p sin y, sin p) for pitch p and yaw y");
        AppendStringField(output, first, "engine_world_scale_authority",
            "world_scale is engine world units per metre under the active title's authority: Halo 3/Halo 4 read live g_worldScale; Reach/ODST/Halo 2 record their fixed 1/3.048 constants; any other title records invalid");
        AppendStringField(output, first, "engine_aim_sources",
            "engine_aim_source: 0 None (no publication, e.g. Halo CE), 1 Halo 3 shared g_aimFwd, 2 ODST shared g_aimFwd, 3 Reach seated native unit aim, 4 Reach seated compact fallback, 5 Reach on-foot shared compact fallback, 6 Halo 4 stereo-transaction observer, 7 Halo 2 observer stock, 8 Halo 2 absent; serial 0 means latest-only with no serial; sampleMs 0 means none; engine_aim_pitch_deg is degrees and is valid only for Halo 4; Halo 4's serial may differ from its payload by one writer iteration (treat serial +/-1), so never assert an exact engine_aim_serial-to-payload pairing for source 6");
        AppendStringField(output, first, "engine_camera_sources",
            "engine_camera_source: 0 None, 1 Halo 3 shared g_baseCam/g_cam, 2 ODST shared g_baseCam/g_cam, 3 Reach completed-frame eye, 4 Halo 2 observer stock, 5 Halo 4 absent (the stereo transaction published no usable camera position this frame; the read is unavailable before the first owned frame and after teardown), 6 Halo 4 stereo-transaction observer stock position; base is the pre-lean origin, eye the rendered camera; engine_camera_serial carries the publication serial where one exists (Reach preparedSerial, Halo 2 observer serial, Halo 4 stock camera serial), else 0");
        AppendStringField(output, first, "shot_event_semantics",
            "kind=shot sparse events carry the FINAL ray the engine consumed after the title's own aim call and after any substitution; shot_direction [0,0,0] means the producer's firing call-site carries no direction (ODST's firing-origin hook), never a fabricated one; shot_engine_aim_source uses the engine_aim_source ordinals (0 none, 1 Halo 3 shared, 2 ODST shared, 3/4 Reach seated unit/compact, 5 Reach on-foot compact, 6 Halo 4 observer, 7/8 Halo 2) and 0 (or 8, Halo 2 absent) means no engine aim snapshot was available for that shot (shot_engine_aim is then exactly zero); shot_slot/shot_barrel -1 means the firing context did not carry them, never a fabricated identity");
        AppendStringField(output, first, "reticle_presented_lag",
            "capture runs BEFORE the reticle block publishes in the same frame, so reticle_presented_* is normally the PREVIOUS serial's consumer-visible pose; its own serial travels in reticle_presented_serial and must never be asserted equal to prepared_serial");
        AppendStringField(output, first, "dual_slot_semantics",
            "dual_active is the shared SecondaryWeaponPresentationActive predicate (dual-weapon presentation eligible this frame); per-shot slot identity travels in the sparse shot event (kind=shot), never in this frame field");
        AppendStringField(output, first, "persistent_support_grip_provenance",
            "the persistent_support_grip_configured/applicable and support_* fields are FROZEN solve-time provenance for this frame's own canonical assembly, never a live re-read of persistent-grip state: persistent_support_grip_configured is the config knob read for the frame, persistent_support_grip_applicable is the pure title applicability gate for the active title, support_relationship_readable/engaged/support_epoch are the assembly's frozen relationship qualification (epoch 0 = coherently disengaged, all-ones 18446744073709551615 = the relationship could not be read; an unreadable relationship is NOT a disengaged one and epochs must never be compared across sessions), support_solve_trusted is the qualification PERMISSION (this invocation proved the relationship owner; whether the solve actually consumed support geometry is a separate, unrecorded narrowing), support_force_one_hand is EXPLICIT persistent-grip forcing of this invocation to the one-hand path and is distinct from an ordinary two_hand_enabled false (which conflates config-off, dual presentation and forcing), and support_solve_serial is the solve serial the assembly stamped: at capture it is the last published prepared serial, normally prepared_serial minus one, and must never be asserted equal to prepared_serial");
        AppendStringField(output, first, "two_hand_smoothing_provenance",
            "two_hand_smoothing_configured means the user strength for this frame was non-zero; two_hand_smoothing_applied means this prepared frame's eligible two-hand path (Virtual Stock on or off: latched with a valid support controller) actually consumed it; two_hand_smoothing_strength is the user amount in 0..25 (0 = raw/off controller input, 25 = the full fixed Pavlov-inspired speed-25 input filter) FROZEN for this prepared serial and never a live re-read of the config slider; two_hand_smoothing_mix is strength/25 in [0,1] emitted from that same frozen strength (the applied wet/dry amount, never a filter speed); two_hand_smoothing_alpha is the filter's INTERNAL clamp(25*dt,0,1) temporal coefficient and is never the user amount; the two_hand_smoothing_*_error_* values are raw-to-FULL-filtered input differences, never solver or presented-aim errors");
        AppendStringField(output, first,
            "two_hand_offhand_influence_provenance",
            "two_hand_offhand_influence is the free two-hand (Virtual-Stock-off) offhand directional authority in 0..1 FROZEN for this prepared serial from the frame-local assembly the solve consumed (non-finite reads 0, otherwise clamped to [0,1] exactly as the solver consumes it), never a live re-read of the config slider; 0 = the primary controller's own directional aim is authoritative, 1 = the accepted support direction owns presentation, and zero authority never decides whether the weapon is logically held (acceptance and two_hand_active stay the solved semantics); Virtual Stock solves never read this product value and a Lab-active solve consumes its own two_hand_lab_offhand_influence instead");

        AppendKey(output, first, "test_profile_enum_mapping");
        output.push_back('[');
        for (size_t index = 0;
             index < kVirtualStockTestProfileDefinitions.size(); ++index)
        {
            if (index)
                output.push_back(',');
            const auto& definition =
                kVirtualStockTestProfileDefinitions[index];
            output.push_back('{');
            bool mappingFirst = true;
            AppendIntegerField(output, mappingFirst, "id",
                static_cast<uint8_t>(definition.id));
            AppendStringField(output, mappingFirst, "name",
                definition.stableName);
            AppendStringField(output, mappingFirst, "role",
                VirtualStockTestProfileRoleName(definition.role));
            AppendBoolField(output, mappingFirst, "uses_custom_settings",
                definition.useCustomSettings);
            AppendBoolField(output, mappingFirst, "horizontal_release_enabled",
                definition.settings.hybridHorizontalRearReleaseEnabled);
            output.push_back('}');
        }
        output.push_back(']');

        AppendKey(output, first, "hybrid_diagnostic_override_enum_mapping");
        output += "[{\"id\":0,\"name\":\"Normal\"},"
                  "{\"id\":1,\"name\":\"ForceHip\"},"
                  "{\"id\":2,\"name\":\"ForceStock\"}]";
        output.push_back('}');
    }

    void SerializeSessionEndLine(
        const WorkerSession& session, int64_t stopQpc, std::string& output)
    {
        output.clear();
        output.reserve(1024);
        output.push_back('{');
        bool first = true;
        AppendStringField(output, first, "type", "session_end");
        AppendIntegerField(output, first, "schema_version", kTelemetrySchemaVersion);
        AppendBoolField(output, first, "clean_stop", true);
        AppendStringField(output, first, "stop_reason", "user");
        AppendIntegerField(output, first, "duration_qpc_ticks",
            stopQpc >= session.startQpc ? stopQpc - session.startQpc : 0);
        AppendIntegerField(output, first, "producer_calls",
            g_producerCalls.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "duplicate_serial_suppressed",
            g_duplicateSerialSuppressed.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "enqueued",
            g_enqueued.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "written",
            g_written.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "dropped_queue_full",
            g_droppedQueueFull.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "writer_failures",
            g_writerFailures.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "first_prepared_serial_written",
            session.haveWrittenSerial ? session.firstWrittenSerial : 0);
        AppendIntegerField(output, first, "last_prepared_serial_written",
            session.haveWrittenSerial ? session.lastWrittenSerial : 0);
        AppendIntegerField(output, first, "weapon_events_enqueued",
            g_weaponEventEnqueued.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "weapon_events_written",
            g_weaponEventWritten.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "weapon_events_dropped_queue_full",
            g_weaponEventDropped.load(std::memory_order_relaxed));
        AppendIntegerField(output, first, "weapon_events_session_skipped",
            g_weaponEventSessionSkipped.load(std::memory_order_relaxed));
        output.push_back('}');
    }

    bool WriteLine(FILE* file, const std::string& line) noexcept;

    const char* WeaponOrderEventKindName(uint8_t kind) noexcept
    {
        switch (static_cast<WeaponOrderEventKind>(kind))
        {
        case WeaponOrderEventKind::PresentBegin: return "present_begin";
        case WeaponOrderEventKind::AfterPresentBeforePrepare: return "after_present_before_prepare";
        case WeaponOrderEventKind::CapturePreLatch: return "capture_pre_latch";
        case WeaponOrderEventKind::CaptureProbeBegin: return "capture_probe_begin";
        case WeaponOrderEventKind::CaptureProbeResult: return "capture_probe_result";
        case WeaponOrderEventKind::FpEntry: return "fp_entry";
        case WeaponOrderEventKind::FpWeaponCommit: return "fp_weapon_commit";
        case WeaponOrderEventKind::Shot: return "shot";
        default: return "unknown";
        }
    }

    const char* WeaponOrderEventStatusName(uint8_t status) noexcept
    {
        switch (static_cast<WeaponOrderEventStatus>(status))
        {
        case WeaponOrderEventStatus::NoObservation: return "no_observation";
        case WeaponOrderEventStatus::Success: return "success";
        case WeaponOrderEventStatus::ReaderReturnedFalse: return "reader_returned_false";
        case WeaponOrderEventStatus::GuardRejected: return "guard_rejected";
        case WeaponOrderEventStatus::ExceptionOrFault: return "exception_or_fault";
        case WeaponOrderEventStatus::NotAttemptedNoSafeThread: return "not_attempted_no_safe_thread";
        case WeaponOrderEventStatus::NotAttemptedThreadMismatch: return "not_attempted_thread_mismatch";
        case WeaponOrderEventStatus::DefinitivelyAbsent: return "definitively_absent";
        default: return "unknown";
        }
    }

    void SerializeWeaponEventLine(const TelemetryWeaponEvent& event,
        std::string& output)
    {
        output.clear();
        output.reserve(512);
        output.push_back('{');
        bool first = true;
        AppendStringField(output, first, "type", "weapon_event");
        AppendIntegerField(output, first, "seq", event.sequence);
        AppendIntegerField(output, first, "session", event.session);
        AppendIntegerField(output, first, "qpc", event.timestampQpc);
        AppendIntegerField(output, first, "thread_id", event.threadId);
        AppendStringField(output, first, "kind",
            WeaponOrderEventKindName(event.kind));
        AppendStringField(output, first, "status",
            WeaponOrderEventStatusName(event.status));
        AppendIntegerField(output, first, "title", event.title);
        AppendIntegerField(output, first, "title_generation",
            event.titleGeneration);
        AppendIntegerField(output, first, "prepared_serial",
            event.preparedSerial);
        AppendIntegerField(output, first, "controlled_unit",
            event.controlledUnit);
        AppendIntegerField(output, first, "primary_weapon",
            event.primaryWeapon);
        AppendIntegerField(output, first, "aux0", event.aux0);
        AppendIntegerField(output, first, "aux1", event.aux1);
        // Fixed T-2 shot payload: emitted ONLY for kind Shot so every
        // existing kind serializes byte-for-byte as before. Vectors use the
        // shared finite guard (non-finite renders as null).
        if (event.kind == static_cast<uint8_t>(WeaponOrderEventKind::Shot))
        {
            AppendShotVec3(output, first, "shot_origin", event.shotOrigin);
            AppendShotVec3(output, first, "shot_direction",
                event.shotDirection);
            AppendShotVec3(output, first, "shot_engine_aim",
                event.shotEngineAim);
            AppendShotVec3(output, first, "shot_reticle_direction",
                event.shotReticleDirection);
            AppendIntegerField(output, first, "shot_slot",
                ShotIndexWireValue(event.shotSlot));
            AppendIntegerField(output, first, "shot_barrel",
                ShotIndexWireValue(event.shotBarrel));
            AppendIntegerField(output, first, "shot_flags", event.shotFlags);
            AppendIntegerField(output, first, "shot_engine_aim_source",
                event.shotEngineAimSource);
        }
        output.push_back('}');
    }

    void SerializeWeaponEventGapLine(uint64_t firstMissing,
        uint64_t lastMissing, std::string& output)
    {
        output.clear();
        output.reserve(256);
        output.push_back('{');
        bool first = true;
        AppendStringField(output, first, "type", "event_gap");
        AppendIntegerField(output, first, "first_missing_seq", firstMissing);
        AppendIntegerField(output, first, "last_missing_seq", lastMissing);
        output.push_back('}');
    }

    // Worker only. Serializes due weapon events in global-sequence order.
    // A slot whose sequence does not match is either an in-flight producer
    // (leave it for a later poll: a publish completes in nanoseconds, the
    // worker returns every 10 ms) or an unrecoverable skip (queue-full drop
    // at publish, which consumes a sequence without writing a record). A skip
    // is provable only against the claim frontier: with at most two producers
    // ever in flight, `claimed - expected >= capacity` means every sequence
    // in [expected, claimed - capacity] can never arrive. Skips become
    // explicit gap markers so the analyser treats overlapping transitions as
    // INDETERMINATE instead of drawing ordering conclusions.
    // Worker drain core shared with the test hook: serializes due weapon
    // events in global-sequence order through `writeLine`. Every loss mode
    // becomes an explicit event_gap line, including cross-session skips, so
    // the analyser can never classify a transition through missing records.
    template <typename WriteLineFn>
    bool DrainWeaponEventsImpl(uint64_t sessionToken, std::string& line,
        WriteLineFn&& writeLine, uint64_t gapBudget)
    {
        uint64_t gapsThisCall = 0;
        for (;;)
        {
            const uint64_t expected = g_weaponEventDrained + 1;
            const uint64_t claimed =
                g_weaponEventClaimed.load(std::memory_order_seq_cst);
            if (claimed < expected)
                break; // idle: nothing outstanding
            WeaponEventSlot& slot = WeaponEventSlotFor(expected);
            uint64_t slotSequence =
                slot.committedSequence.load(std::memory_order_acquire);
            if (slotSequence != expected)
            {
                // Safety property: once the consumer advances past sequence N,
                // no producer capable of later publishing N exists. The claim
                // fetch_add is sequenced after the producer's in-flight count
                // increment (both sequential-consistent), so a consumer that
                // observed this claim observes that counter. While any
                // producer is still in flight, the expected sequence may still
                // be committed: stop the pass and retry on the next poll. No
                // number of yields proves a producer dead.
                if (g_weaponEventProducersInFlight.load(
                        std::memory_order_seq_cst) != 0)
                    break;
                // Re-load after observing quiescence so a producer that
                // committed just before decrementing its receipt is seen
                // (the decrement is a release RMW ordered after the marker
                // store).
                slotSequence =
                    slot.committedSequence.load(std::memory_order_acquire);
                if (slotSequence != expected)
                {
                    // Every claimed producer has completed, so this sequence
                    // can never arrive. Gap exactly one verified-missing
                    // sequence; a wider range is never inferred.
                    SerializeWeaponEventGapLine(expected, expected, line);
                    if (!writeLine(line))
                        return false;
                    g_weaponEventDrained = expected;
                    if (++gapsThisCall >= gapBudget)
                        break; // resume on the next worker poll
                    continue;
                }
            }
            // The acquire load above observed this exact committed sequence,
            // so the payload stores that preceded the marker's release store
            // are visible here. The slot cannot be reused while this sequence
            // is still the drain target (producers drop beyond capacity).
            const TelemetryWeaponEvent event = slot.payload;
            if (event.tearGuard != event.sequence)
                break; // defensive: a mismatched commit marker is retried later
            if (event.session != sessionToken)
            {
                // A record published across a Stop/Start boundary is not
                // evidence for this session; mark the sequence lost so the
                // analyser treats any interval covering it as indeterminate.
                g_weaponEventSessionSkipped.fetch_add(1,
                    std::memory_order_relaxed);
                SerializeWeaponEventGapLine(expected, expected, line);
                if (!writeLine(line))
                    return false;
                g_weaponEventDrained = expected;
                continue;
            }
            SerializeWeaponEventLine(event, line);
            if (!writeLine(line))
                return false;
            g_weaponEventWritten.fetch_add(1, std::memory_order_relaxed);
            g_weaponEventDrained = expected;
        }
        g_weaponEventPostedDrained.store(g_weaponEventDrained,
            std::memory_order_release);
        return true;
    }

    bool DrainWeaponEvents(WorkerSession& session, std::string& line)
    {
        return DrainWeaponEventsImpl(session.weaponEventToken, line,
            [&session](const std::string& text) {
                return WriteLine(session.file, text);
            },
            kWeaponEventNormalGapBudget);
    }

    template <typename WriteLineFn>
    bool DrainWeaponEventsToFrontierImpl(uint64_t sessionToken,
        std::string& line, WriteLineFn&& writeLine)
    {
        // Finalization has stronger knowledge than the regular poll:
        // acceptance is already disabled and every producer receipt has
        // drained, so all sequences up to the captured claim frontier are
        // definitive (committed or dropped). The unlimited gap budget
        // consumes that finite frontier in one deterministic pass; no sleep,
        // timeout or yield-count inference is involved.
        return DrainWeaponEventsImpl(sessionToken, line, writeLine,
            std::numeric_limits<uint64_t>::max());
    }

    bool DrainWeaponEventsToFrontier(WorkerSession& session,
        std::string& line)
    {
        return DrainWeaponEventsToFrontierImpl(session.weaponEventToken,
            line, [&session](const std::string& text) {
                return WriteLine(session.file, text);
            });
    }

    bool WriteLine(FILE* file, const std::string& line) noexcept
    {
#ifdef HALOMCCVR_TELEMETRY_TESTING
        const int32_t writesBeforeFailure =
            g_failWriteAfter.load(std::memory_order_acquire);
        if (writesBeforeFailure == 0)
        {
            g_failWriteAfter.store(-1, std::memory_order_release);
            errno = EIO;
            return false;
        }
        if (writesBeforeFailure > 0)
        {
            g_failWriteAfter.store(
                writesBeforeFailure - 1, std::memory_order_release);
        }
#endif
        return file &&
            std::fwrite(line.data(), 1, line.size(), file) == line.size() &&
            std::fputc('\n', file) != EOF;
    }

    bool CloseSession(WorkerSession& session) noexcept
    {
        if (!session.file)
            return true;
        const int closed = std::fclose(session.file);
        session.file = nullptr;
#ifdef HALOMCCVR_TELEMETRY_TESTING
        g_testSessionClosed = closed == 0;
        if (g_forceCloseFailure.exchange(false, std::memory_order_acq_rel))
        {
            g_testSessionClosed = false;
            errno = EIO;
            return false;
        }
#endif
        return closed == 0;
    }

    void PublishError(
        WorkerSession& session, TelemetryErrorCode error,
        uint32_t systemError, const char* message) noexcept
    {
        SetProducerAdmission(false);
        g_desiredRecording.store(false, std::memory_order_release);
        g_error.store(static_cast<uint8_t>(error), std::memory_order_release);
        g_systemError.store(systemError, std::memory_order_release);
        StoreState(TelemetryRecorderState::Error);
        CloseSession(session);
        LOG("telemetry: %s (error %u, system %u)", message,
            static_cast<unsigned>(error), systemError);
    }

    bool OpenUniqueSessionFile(
        WorkerSession& session, SYSTEMTIME& utc, uint32_t& systemError)
    {
#ifdef HALOMCCVR_TELEMETRY_TESTING
        if (g_forceOpenFailure.load(std::memory_order_acquire))
        {
            systemError = ERROR_ACCESS_DENIED;
            return false;
        }
#endif
        GetSystemTime(&utc);
        wchar_t stem[96]{};
        _snwprintf_s(stem, _countof(stem), _TRUNCATE,
            L"HaloMCCVR-Telemetry-%04u%02u%02u-%02u%02u%02u-%03u",
            utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
            utc.wSecond, utc.wMilliseconds);

        for (unsigned suffix = 0; suffix < 1000; ++suffix)
        {
            wchar_t basename[128]{};
            if (suffix == 0)
                _snwprintf_s(basename, _countof(basename), _TRUNCATE,
                    L"%ls.jsonl", stem);
            else
                _snwprintf_s(basename, _countof(basename), _TRUNCATE,
                    L"%ls-%u.jsonl", stem, suffix);
            const std::wstring path = g_directory + basename;
#ifdef HALOMCCVR_TELEMETRY_TESTING
            g_testSessionClosed = false;
#endif
            int descriptor = -1;
            const errno_t opened = _wsopen_s(
                &descriptor, path.c_str(),
                _O_CREAT | _O_EXCL | _O_WRONLY | _O_TEXT,
                _SH_DENYNO, _S_IREAD | _S_IWRITE);
            if (opened == EEXIST)
                continue;
            if (opened != 0 || descriptor < 0)
            {
                systemError = static_cast<uint32_t>(opened);
                return false;
            }
            session.file = _wfdopen(descriptor, L"wt");
            if (!session.file)
            {
                systemError = static_cast<uint32_t>(errno);
                _close(descriptor);
                DeleteFileW(path.c_str());
                return false;
            }
            if (std::setvbuf(
                    session.file, g_fileBuffer, _IOFBF,
                    sizeof(g_fileBuffer)) != 0)
            {
                LOG("telemetry: large file buffer unavailable; using CRT default");
            }
            session.path = path;
            char narrow[128]{};
            const int count = WideCharToMultiByte(
                CP_UTF8, 0, basename, -1, narrow,
                static_cast<int>(sizeof(narrow)), nullptr, nullptr);
            session.basename = count > 0 ? narrow : "HaloMCCVR-Telemetry.jsonl";
            return true;
        }
        systemError = ERROR_FILE_EXISTS;
        return false;
    }

    enum class DrainResult : uint8_t
    {
        Complete,
        WriteFailed,
        FlushFailed,
    };

    bool FlushIfDue(WorkerSession& session, bool force) noexcept;

    DrainResult DrainFrames(
        WorkerSession& session, std::string& line,
        bool interruptForStop = false)
    {
        TelemetryFrame frame{};
        while (PopRing(frame))
        {
            SerializeFrameLine(frame, line);
            if (!WriteLine(session.file, line))
                return DrainResult::WriteFailed;
            if (!session.haveWrittenSerial)
            {
                session.haveWrittenSerial = true;
                session.firstWrittenSerial = frame.preparedSerial;
            }
            session.lastWrittenSerial = frame.preparedSerial;
            g_written.fetch_add(1, std::memory_order_relaxed);
            ++session.framesSinceFlush;
            if (session.framesSinceFlush % kFlushCheckFrameInterval == 0 &&
                !FlushIfDue(session, false))
            {
                return DrainResult::FlushFailed;
            }
#ifdef HALOMCCVR_TELEMETRY_TESTING
            if (interruptForStop)
            {
                g_workerReachedRecordingDrain.store(
                    true, std::memory_order_release);
                while (g_pauseRecordingDrain.load(std::memory_order_acquire))
                    Sleep(1);
            }
#endif
            if (interruptForStop &&
                !g_desiredRecording.load(std::memory_order_acquire))
            {
                break;
            }
        }
        return DrainResult::Complete;
    }

    bool FlushIfDue(WorkerSession& session, bool force) noexcept
    {
        const uint64_t now = GetTickCount64();
        if (!force && session.framesSinceFlush < kFlushFrameInterval &&
            now - session.lastFlushMs < kFlushTimeIntervalMs)
        {
            return true;
        }
#ifdef HALOMCCVR_TELEMETRY_TESTING
        const int32_t flushesBeforeFailure =
            g_failFlushAfter.load(std::memory_order_acquire);
        if (flushesBeforeFailure == 0)
        {
            g_failFlushAfter.store(-1, std::memory_order_release);
            errno = EIO;
            return false;
        }
        if (flushesBeforeFailure > 0)
        {
            g_failFlushAfter.store(
                flushesBeforeFailure - 1, std::memory_order_release);
        }
#endif
        if (!session.file || std::fflush(session.file) != 0)
            return false;
        session.lastFlushMs = now;
        session.framesSinceFlush = 0;
        return true;
    }

    bool StartSession(WorkerSession& session, std::string& line)
    {
        StoreState(TelemetryRecorderState::Starting);
        SetProducerAdmission(false);
#ifdef HALOMCCVR_TELEMETRY_TESTING
        while (g_pauseStarting.load(std::memory_order_acquire))
            Sleep(1);
#endif
        // Wait for both frame and weapon-event producers: a session reset must
        // not race an admitted weapon-event publisher.
        while (g_producerInFlight.load(std::memory_order_seq_cst) != 0 ||
               g_weaponEventProducersInFlight.load(
                   std::memory_order_seq_cst) != 0)
            Sleep(0);
        if (!g_desiredRecording.load(std::memory_order_acquire))
        {
            StoreState(TelemetryRecorderState::Idle);
            return true;
        }
        ResetSessionState();
        session = {};
        session.weaponEventToken =
            g_admissionToken.load(std::memory_order_acquire) >> 1;

        SYSTEMTIME utc{};
        uint32_t systemError = 0;
        if (!OpenUniqueSessionFile(session, utc, systemError))
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            PublishError(session, TelemetryErrorCode::OpenFileFailed,
                systemError, "could not create a new telemetry session file");
            return false;
        }

        LARGE_INTEGER frequency{}, start{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        session.startQpc = start.QuadPart;
        session.startTickMs = GetTickCount64();
        session.lastFlushMs = session.startTickMs;
        g_sessionStartQpc.store(session.startQpc, std::memory_order_release);
        g_qpcFrequency.store(frequency.QuadPart, std::memory_order_release);

        SerializeSessionStartLine(session, utc, frequency.QuadPart, line);
        if (!WriteLine(session.file, line))
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            PublishError(session, TelemetryErrorCode::WriteFailed,
                static_cast<uint32_t>(errno), "failed to write session_start");
            return false;
        }
        if (!FlushIfDue(session, true))
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            PublishError(session, TelemetryErrorCode::FlushFailed,
                static_cast<uint32_t>(errno), "failed to flush session_start");
            return false;
        }

        if (!g_desiredRecording.load(std::memory_order_acquire))
            return true;

        SetProducerAdmission(true);
        StoreState(TelemetryRecorderState::Recording);
        return true;
    }

    bool FinalizeSession(WorkerSession& session, std::string& line)
    {
        StoreState(TelemetryRecorderState::Finalizing);
        SetProducerAdmission(false);
#ifdef HALOMCCVR_TELEMETRY_TESTING
        while (g_pauseFinalizing.load(std::memory_order_acquire))
            Sleep(1);
#endif
        // Quiesce BOTH producer channels before the final drains and close.
        // An admitted weapon-event publisher therefore either completes into
        // this session's final drain or aborts on the changed token; it can
        // never append after the file is closed.
        while (g_producerInFlight.load(std::memory_order_seq_cst) != 0 ||
               g_weaponEventProducersInFlight.load(
                   std::memory_order_seq_cst) != 0)
            Sleep(0);
        // With acceptance disabled and every producer receipt drained the
        // claim frontier is immutable. The final drain must consume it
        // completely: every claimed sequence becomes a serialized record or
        // an explicit gap before session_end. A shortfall is an internal
        // inconsistency and must not produce a false clean stop.
        const uint64_t weaponClaimFrontier =
            g_weaponEventClaimed.load(std::memory_order_seq_cst);
        if (!DrainWeaponEventsToFrontier(session, line))
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            PublishError(session, TelemetryErrorCode::WriteFailed,
                static_cast<uint32_t>(errno),
                "failed while draining weapon-order events");
            return false;
        }
        if (g_weaponEventDrained < weaponClaimFrontier)
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            PublishError(session, TelemetryErrorCode::InternalFailure,
                ERROR_INVALID_DATA,
                "weapon-event final drain stopped short of the claim frontier");
            return false;
        }
        const DrainResult drain = DrainFrames(session, line);
        if (drain != DrainResult::Complete)
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            const bool flushFailed = drain == DrainResult::FlushFailed;
            PublishError(session,
                flushFailed ? TelemetryErrorCode::FlushFailed
                            : TelemetryErrorCode::WriteFailed,
                static_cast<uint32_t>(errno),
                flushFailed ? "failed while flushing drained telemetry frames"
                            : "failed while draining telemetry frames");
            return false;
        }
        LARGE_INTEGER stop{};
        QueryPerformanceCounter(&stop);
        SerializeSessionEndLine(session, stop.QuadPart, line);
        if (!WriteLine(session.file, line))
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            PublishError(session, TelemetryErrorCode::WriteFailed,
                static_cast<uint32_t>(errno), "failed to write session_end");
            return false;
        }
        if (!FlushIfDue(session, true))
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            PublishError(session, TelemetryErrorCode::FlushFailed,
                static_cast<uint32_t>(errno), "failed to flush session_end");
            return false;
        }
        if (!CloseSession(session))
        {
            g_writerFailures.fetch_add(1, std::memory_order_relaxed);
            PublishError(session, TelemetryErrorCode::CloseFailed,
                static_cast<uint32_t>(errno), "failed to close session file");
            return false;
        }
        StoreState(TelemetryRecorderState::Idle);
        QueueTelemetryAnalyserLaunch(session.path);
        return true;
    }

    DWORD WINAPI TelemetryWorker(void*)
    {
        WorkerSession session{};
        std::string line;
        for (;;)
        {
            const TelemetryRecorderState state = LoadState();
            const DWORD waitMs = state == TelemetryRecorderState::Recording
                ? kWorkerPollMs : INFINITE;
            WaitForSingleObject(g_controlEvent, waitMs);

            try
            {
                const bool desired =
                    g_desiredRecording.load(std::memory_order_acquire);
                const TelemetryRecorderState current = LoadState();
                if ((current == TelemetryRecorderState::Idle ||
                     current == TelemetryRecorderState::Error) && desired)
                {
                    if (!StartSession(session, line))
                        continue;
                }

                if (LoadState() == TelemetryRecorderState::Starting)
                {
                    if (!g_desiredRecording.load(std::memory_order_acquire))
                    {
                        FinalizeSession(session, line);
                        continue;
                    }
                    SetProducerAdmission(true);
                    StoreState(TelemetryRecorderState::Recording);
                }

                if (LoadState() == TelemetryRecorderState::Recording)
                {
#ifdef HALOMCCVR_TELEMETRY_TESTING
                    g_workerReachedRecording.store(true, std::memory_order_release);
                    while (g_pauseRecording.load(std::memory_order_acquire))
                        Sleep(1);
#endif
                    if (!g_desiredRecording.load(std::memory_order_acquire))
                    {
                        if (!FinalizeSession(session, line))
                            continue;
                        continue;
                    }
                    if (!DrainWeaponEvents(session, line))
                    {
                        g_writerFailures.fetch_add(1, std::memory_order_relaxed);
                        PublishError(session,
                            TelemetryErrorCode::WriteFailed,
                            static_cast<uint32_t>(errno),
                            "failed to write a weapon-order event");
                        continue;
                    }
                    const DrainResult drain = DrainFrames(session, line, true);
                    if (drain != DrainResult::Complete)
                    {
                        g_writerFailures.fetch_add(1, std::memory_order_relaxed);
                        const bool flushFailed =
                            drain == DrainResult::FlushFailed;
                        PublishError(session,
                            flushFailed ? TelemetryErrorCode::FlushFailed
                                        : TelemetryErrorCode::WriteFailed,
                            static_cast<uint32_t>(errno),
                            flushFailed ? "failed to flush telemetry frames"
                                        : "failed to write a telemetry frame");
                        continue;
                    }
                    if (!g_desiredRecording.load(std::memory_order_acquire))
                    {
                        if (!FinalizeSession(session, line))
                            continue;
                        continue;
                    }
                    if (!FlushIfDue(session, false))
                    {
                        g_writerFailures.fetch_add(1, std::memory_order_relaxed);
                        PublishError(session, TelemetryErrorCode::FlushFailed,
                            static_cast<uint32_t>(errno),
                            "failed to flush telemetry frames");
                        continue;
                    }
                    if (!g_desiredRecording.load(std::memory_order_acquire))
                    {
                        if (!FinalizeSession(session, line))
                            continue;
                    }
                }

                if (LoadState() == TelemetryRecorderState::Idle &&
                    g_desiredRecording.load(std::memory_order_acquire))
                {
                    SetEvent(g_controlEvent);
                }
            }
            catch (const std::bad_alloc&)
            {
                g_writerFailures.fetch_add(1, std::memory_order_relaxed);
                PublishError(session, TelemetryErrorCode::AllocationFailed,
                    ERROR_OUTOFMEMORY, "telemetry worker allocation failed");
            }
            catch (...)
            {
                g_writerFailures.fetch_add(1, std::memory_order_relaxed);
                PublishError(session, TelemetryErrorCode::InternalFailure,
                    ERROR_UNHANDLED_EXCEPTION,
                    "telemetry worker failed unexpectedly");
            }
        }
    }
}

bool Telemetry_Init() noexcept
{
    const wchar_t* directory = LogDirectory();
    if (!directory || !directory[0])
    {
        g_error.store(
            static_cast<uint8_t>(TelemetryErrorCode::InvalidLogDirectory),
            std::memory_order_release);
        StoreState(TelemetryRecorderState::Unavailable);
        return false;
    }
    try
    {
        g_directory.assign(directory);
        if (g_directory.back() != L'\\' && g_directory.back() != L'/')
            g_directory.push_back(L'\\');
        // Canonical layout: recordings live in "Telemetry Recordings", a
        // direct child of the mod folder that holds HaloMCCVR.dll and
        // HaloMCCVR.log, so every version accumulates sessions in one stable
        // location. The analyser stays separate under TelemetryAnalyser\;
        // recordings captured under older toolkit folders are never moved.
        // The leaf check below stays authoritative and fails open to
        // Unavailable on any real failure.
        g_directory += kTelemetryRecordingsLeaf;
        if (!CreateDirectoryW(g_directory.c_str(), nullptr))
        {
            const DWORD directoryError = GetLastError();
            const DWORD attributes = GetFileAttributesW(g_directory.c_str());
            if (directoryError != ERROR_ALREADY_EXISTS ||
                attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                g_error.store(static_cast<uint8_t>(
                    TelemetryErrorCode::CreateDirectoryFailed),
                    std::memory_order_release);
                g_systemError.store(directoryError, std::memory_order_release);
                StoreState(TelemetryRecorderState::Unavailable);
                LOG("telemetry: recording directory creation failed for %ls (system %lu)",
                    g_directory.c_str(), directoryError);
                return false;
            }
        }
        g_directory.push_back(L'\\');
    }
    catch (const std::bad_alloc&)
    {
        g_error.store(
            static_cast<uint8_t>(TelemetryErrorCode::AllocationFailed),
            std::memory_order_release);
        g_systemError.store(ERROR_OUTOFMEMORY, std::memory_order_release);
        StoreState(TelemetryRecorderState::Unavailable);
        return false;
    }
    catch (...)
    {
        g_error.store(
            static_cast<uint8_t>(TelemetryErrorCode::InternalFailure),
            std::memory_order_release);
        g_systemError.store(
            ERROR_UNHANDLED_EXCEPTION, std::memory_order_release);
        StoreState(TelemetryRecorderState::Unavailable);
        return false;
    }

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    g_qpcFrequency.store(frequency.QuadPart, std::memory_order_release);
    g_controlEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_controlEvent)
    {
        g_error.store(
            static_cast<uint8_t>(TelemetryErrorCode::CreateControlEventFailed),
            std::memory_order_release);
        g_systemError.store(GetLastError(), std::memory_order_release);
        StoreState(TelemetryRecorderState::Unavailable);
        return false;
    }
    StoreState(TelemetryRecorderState::Idle);
    HANDLE worker = CreateThread(
        nullptr, 0, TelemetryWorker, nullptr, 0, nullptr);
    if (!worker)
    {
        g_error.store(
            static_cast<uint8_t>(TelemetryErrorCode::CreateWorkerFailed),
            std::memory_order_release);
        g_systemError.store(GetLastError(), std::memory_order_release);
        StoreState(TelemetryRecorderState::Unavailable);
        CloseHandle(g_controlEvent);
        g_controlEvent = nullptr;
        return false;
    }
    if (!SetThreadPriority(worker, THREAD_PRIORITY_BELOW_NORMAL))
        LOG("telemetry: worker priority remained normal (system %lu)", GetLastError());
    CloseHandle(worker);
    return true;
}

void Telemetry_RequestStart() noexcept
{
    if (!g_controlEvent || LoadState() == TelemetryRecorderState::Unavailable)
        return;
    g_desiredRecording.store(true, std::memory_order_release);
    SetEvent(g_controlEvent);
}

void Telemetry_RequestStop() noexcept
{
    if (!g_controlEvent || LoadState() == TelemetryRecorderState::Unavailable)
        return;
    g_desiredRecording.store(false, std::memory_order_release);
    SetEvent(g_controlEvent);
}

TelemetryStatusSnapshot Telemetry_GetStatus() noexcept
{
    TelemetryStatusSnapshot status{};
    status.state = LoadState();
    status.error = static_cast<TelemetryErrorCode>(
        g_error.load(std::memory_order_acquire));
    status.systemError = g_systemError.load(std::memory_order_acquire);
    status.desiredRecording =
        g_desiredRecording.load(std::memory_order_acquire);
    status.accepting =
        (g_admissionToken.load(std::memory_order_acquire) & uint64_t{1}) != 0;
    status.sessionStartQpc = g_sessionStartQpc.load(std::memory_order_acquire);
    status.qpcFrequency = g_qpcFrequency.load(std::memory_order_acquire);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    status.qpcNow = now.QuadPart;
    status.producerCalls = g_producerCalls.load(std::memory_order_relaxed);
    status.duplicateSerialSuppressed =
        g_duplicateSerialSuppressed.load(std::memory_order_relaxed);
    status.enqueued = g_enqueued.load(std::memory_order_relaxed);
    status.written = g_written.load(std::memory_order_relaxed);
    status.droppedQueueFull =
        g_droppedQueueFull.load(std::memory_order_relaxed);
    status.writerFailures = g_writerFailures.load(std::memory_order_relaxed);
    status.weaponEventsEnqueued =
        g_weaponEventEnqueued.load(std::memory_order_relaxed);
    status.weaponEventsWritten =
        g_weaponEventWritten.load(std::memory_order_relaxed);
    status.weaponEventsDroppedQueueFull =
        g_weaponEventDropped.load(std::memory_order_relaxed);
    return status;
}

const char* Telemetry_StateName(TelemetryRecorderState state) noexcept
{
    switch (state)
    {
    case TelemetryRecorderState::Unavailable: return "Unavailable";
    case TelemetryRecorderState::Idle: return "Idle";
    case TelemetryRecorderState::Starting: return "Starting";
    case TelemetryRecorderState::Recording: return "Recording";
    case TelemetryRecorderState::Finalizing: return "Finalizing";
    case TelemetryRecorderState::Error: return "Error";
    default: return "Unknown";
    }
}

const char* Telemetry_ErrorName(TelemetryErrorCode error) noexcept
{
    switch (error)
    {
    case TelemetryErrorCode::None: return "None";
    case TelemetryErrorCode::InvalidLogDirectory: return "Log directory unavailable";
    case TelemetryErrorCode::CreateControlEventFailed: return "Control event creation failed";
    case TelemetryErrorCode::CreateWorkerFailed: return "Worker creation failed";
    case TelemetryErrorCode::OpenFileFailed: return "Session file creation failed";
    case TelemetryErrorCode::WriteFailed: return "File write failed";
    case TelemetryErrorCode::FlushFailed: return "File flush failed";
    case TelemetryErrorCode::CloseFailed: return "File close failed";
    case TelemetryErrorCode::AllocationFailed: return "Memory allocation failed";
    case TelemetryErrorCode::InternalFailure: return "Internal recorder failure";
    case TelemetryErrorCode::CreateDirectoryFailed: return "Recording directory creation failed";
    default: return "Unknown error";
    }
}

bool Telemetry_BeginFrame(uint64_t preparedSerial) noexcept
{
    const uint64_t admission =
        g_admissionToken.load(std::memory_order_seq_cst);
    if ((admission & uint64_t{1}) == 0)
        return false;

#ifdef HALOMCCVR_TELEMETRY_TESTING
    g_producerReachedAdmission.store(true, std::memory_order_release);
    while (g_pauseProducerAdmission.load(std::memory_order_acquire))
        Sleep(0);
#endif
    g_producerInFlight.fetch_add(1, std::memory_order_seq_cst);
#ifdef HALOMCCVR_TELEMETRY_TESTING
    g_producerReachedSecondCheck.store(true, std::memory_order_release);
    while (g_pauseProducerSecondCheck.load(std::memory_order_acquire))
        Sleep(0);
#endif
    if (g_admissionToken.load(std::memory_order_seq_cst) != admission)
    {
        g_producerInFlight.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }

    g_producerCalls.fetch_add(1, std::memory_order_relaxed);
    if (g_haveLastProducerSerial &&
        preparedSerial == g_lastProducerPreparedSerial)
    {
        g_duplicateSerialSuppressed.fetch_add(1, std::memory_order_relaxed);
        g_producerInFlight.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    g_lastProducerPreparedSerial = preparedSerial;
    g_haveLastProducerSerial = true;
    if (ProducerRingFull())
    {
        g_droppedQueueFull.fetch_add(1, std::memory_order_relaxed);
        g_producerInFlight.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    return true;
}

void Telemetry_PublishFrame(const TelemetryFrame& frame) noexcept
{
    if (PushProducerRing(frame))
        g_enqueued.fetch_add(1, std::memory_order_relaxed);
    g_producerInFlight.fetch_sub(1, std::memory_order_seq_cst);
}

bool Telemetry_WeaponEventsAccepting() noexcept
{
#ifdef HALOMCCVR_TELEMETRY_TESTING
    if (g_forceWeaponEventsAccepting.load(std::memory_order_acquire))
        return true;
#endif
    return (g_admissionToken.load(std::memory_order_acquire) &
        uint64_t{1}) != 0;
}

uint64_t Telemetry_CurrentSessionToken() noexcept
{
    const uint64_t admission =
        g_admissionToken.load(std::memory_order_acquire);
    return (admission & uint64_t{1}) ? (admission >> 1) : 0;
}

static uint64_t PublishWeaponEventRecord(
    const TelemetryWeaponEvent& fields) noexcept
{
    // Hot path: one admission load while recording is off, then return with
    // zero diagnostic work performed.
    const uint64_t admission =
        g_admissionToken.load(std::memory_order_acquire);
#ifdef HALOMCCVR_TELEMETRY_TESTING
    const bool forced =
        g_forceWeaponEventsAccepting.load(std::memory_order_acquire);
    if (!forced && (admission & uint64_t{1}) == 0)
        return 0;
#else
    if ((admission & uint64_t{1}) == 0)
        return 0;
#endif
    // Producer/session handshake, same shape as the frame recorder: an
    // admitted publisher is counted in flight, rechecks the admission token
    // after the count, and aborts on a Stop/Start race instead of appending
    // to a finalizing session. Session close waits for this counter to drain
    // before its final weapon-event drain, so both outcomes are safe: either
    // the record is serialized in its session, or the publisher aborts before
    // claiming a sequence (nothing is lost silently).
    g_weaponEventProducersInFlight.fetch_add(1, std::memory_order_seq_cst);
#ifdef HALOMCCVR_TELEMETRY_TESTING
    if (g_pauseWeaponEventProducer.load(std::memory_order_acquire))
    {
        g_weaponEventProducerReachedPause.store(true, std::memory_order_release);
        while (g_pauseWeaponEventProducer.load(std::memory_order_acquire))
            Sleep(0);
    }
#endif
    if (g_admissionToken.load(std::memory_order_seq_cst) != admission)
    {
        g_weaponEventProducersInFlight.fetch_sub(1, std::memory_order_seq_cst);
        return 0;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    // The claim counter is the global cross-thread event order. Unique slot
    // ownership follows while in-flight claims stay below capacity (at most
    // two producers exist: the Present/Capture thread and an FP thread).
    // Sequential-consistent claim: the consumer's claimed load and this RMW
    // order the in-flight receipt before the claim, which is what lets the
    // drain treat "quiescent counter + missing marker" as a definitive gap.
    const uint64_t sequence =
        g_weaponEventClaimed.fetch_add(1, std::memory_order_seq_cst) + 1;
    const uint64_t drained =
        g_weaponEventPostedDrained.load(std::memory_order_acquire);
    if (sequence - drained > kWeaponOrderEventQueueSlots)
    {
        g_weaponEventDropped.fetch_add(1, std::memory_order_relaxed);
        g_weaponEventProducersInFlight.fetch_sub(1, std::memory_order_seq_cst);
        return 0;
    }
#ifdef HALOMCCVR_TELEMETRY_TESTING
    // Test-only pause after an accepted claim and before the commit marker:
    // creates a genuinely outstanding claim for the stalled-claim drain test.
    // The arm is consumed by exactly one producer so later publishers (for
    // example the test's queue-pressure loop) are never caught by it.
    if (g_armWeaponEventCommitPause.exchange(
            false, std::memory_order_acq_rel))
    {
        g_weaponEventCommitReachedPause.store(true, std::memory_order_release);
        while (g_holdWeaponEventCommit.load(std::memory_order_acquire))
            Sleep(0);
    }
#endif
    TelemetryWeaponEvent record = fields;
    record.sequence = sequence;
    record.session = admission >> 1;
    record.timestampQpc = now.QuadPart;
    record.threadId = GetCurrentThreadId();
    record.tearGuard = sequence;
    WeaponEventSlot& slot = WeaponEventSlotFor(sequence);
    // Plain payload stores first; the release-store of the dedicated marker
    // publishes them. The consumer never reads the payload without an
    // acquire load that observed this exact sequence.
    slot.payload = record;
    slot.committedSequence.store(sequence, std::memory_order_release);
    g_weaponEventEnqueued.fetch_add(1, std::memory_order_relaxed);
    g_weaponEventProducersInFlight.fetch_sub(1, std::memory_order_seq_cst);
    return sequence;
}

uint64_t Telemetry_PublishWeaponEvent(uint8_t kind, uint8_t status,
    uint8_t title, uint32_t titleGeneration, uint64_t preparedSerial,
    uint32_t controlledUnit, uint32_t primaryWeapon, uint64_t aux0,
    uint64_t aux1) noexcept
{
    TelemetryWeaponEvent fields{};
    fields.kind = kind;
    fields.status = status;
    fields.title = title;
    fields.titleGeneration = titleGeneration;
    fields.preparedSerial = preparedSerial;
    fields.controlledUnit = controlledUnit;
    fields.primaryWeapon = primaryWeapon;
    fields.aux0 = aux0;
    fields.aux1 = aux1;
    return PublishWeaponEventRecord(fields);
}

uint64_t Telemetry_PublishShotEvent(uint8_t status, uint8_t title,
    uint32_t titleGeneration, uint64_t preparedSerial, uint32_t controlledUnit,
    uint32_t primaryWeapon, const TelemetryShotPayload& shot) noexcept
{
    TelemetryWeaponEvent fields{};
    fields.kind = static_cast<uint8_t>(WeaponOrderEventKind::Shot);
    fields.status = status;
    fields.title = title;
    fields.titleGeneration = titleGeneration;
    fields.preparedSerial = preparedSerial;
    fields.controlledUnit = controlledUnit;
    fields.primaryWeapon = primaryWeapon;
    fields.aux0 = 0;
    fields.aux1 = 0;
    std::memcpy(fields.shotOrigin, shot.origin, sizeof(fields.shotOrigin));
    std::memcpy(fields.shotDirection, shot.direction,
        sizeof(fields.shotDirection));
    std::memcpy(fields.shotEngineAim, shot.engineAim,
        sizeof(fields.shotEngineAim));
    std::memcpy(fields.shotReticleDirection, shot.reticleDirection,
        sizeof(fields.shotReticleDirection));
    fields.shotSlot = shot.slot;
    fields.shotBarrel = shot.barrel;
    fields.shotFlags = shot.flags;
    fields.shotEngineAimSource = shot.engineAimSource;
    return PublishWeaponEventRecord(fields);
}

TelemetryWeaponEventCounters Telemetry_GetWeaponEventCounters() noexcept
{
    TelemetryWeaponEventCounters counters{};
    counters.enqueued = g_weaponEventEnqueued.load(std::memory_order_relaxed);
    counters.written = g_weaponEventWritten.load(std::memory_order_relaxed);
    counters.droppedQueueFull =
        g_weaponEventDropped.load(std::memory_order_relaxed);
    counters.sessionSkipped =
        g_weaponEventSessionSkipped.load(std::memory_order_relaxed);
    return counters;
}

#ifdef HALOMCCVR_TELEMETRY_TESTING
void Telemetry_TestForceWeaponEventsAccepting(bool enabled) noexcept
{
    g_forceWeaponEventsAccepting.store(enabled, std::memory_order_release);
}

void Telemetry_TestResetWeaponEvents() noexcept
{
    for (WeaponEventSlot& slot : g_weaponEventSlots)
        slot.committedSequence.store(0, std::memory_order_relaxed);
    g_weaponEventClaimed.store(0, std::memory_order_relaxed);
    g_weaponEventDrained = 0;
    g_weaponEventPostedDrained.store(0, std::memory_order_relaxed);
    g_weaponEventEnqueued.store(0, std::memory_order_relaxed);
    g_weaponEventWritten.store(0, std::memory_order_relaxed);
    g_weaponEventDropped.store(0, std::memory_order_relaxed);
    g_weaponEventSessionSkipped.store(0, std::memory_order_relaxed);
}

bool Telemetry_TestPopWeaponEvent(TelemetryWeaponEvent& out) noexcept
{
    const uint64_t expected = g_weaponEventDrained + 1;
    WeaponEventSlot& slot = WeaponEventSlotFor(expected);
    const uint64_t slotSequence =
        slot.committedSequence.load(std::memory_order_acquire);
    if (slotSequence != expected)
        return false;
    out = slot.payload;
    if (out.tearGuard != out.sequence)
        return false;
    g_weaponEventDrained = expected;
    g_weaponEventPostedDrained.store(expected, std::memory_order_relaxed);
    return true;
}

uint64_t Telemetry_TestWeaponEventDrops() noexcept
{
    return g_weaponEventDropped.load(std::memory_order_relaxed);
}

bool Telemetry_TestSerializeWeaponEvent(const TelemetryWeaponEvent& event,
    char* output, size_t capacity, size_t& written) noexcept
{
    if (!output || !capacity)
        return false;
    try
    {
        std::string line;
        SerializeWeaponEventLine(event, line);
        if (line.size() + 1 > capacity)
            return false;
        std::memcpy(output, line.data(), line.size());
        output[line.size()] = '\0';
        written = line.size();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool Telemetry_TestDrainWeaponEvents(uint64_t sessionToken,
    std::vector<std::string>& lines) noexcept
{
    lines.clear();
    std::string line;
    try
    {
        return DrainWeaponEventsImpl(sessionToken, line,
            [&lines](const std::string& text) {
                lines.push_back(text);
                return true;
            },
            kWeaponEventNormalGapBudget);
    }
    catch (...)
    {
        return false;
    }
}

bool Telemetry_TestDrainWeaponEventsLimited(uint64_t sessionToken,
    size_t maxLines, std::vector<std::string>& lines) noexcept
{
    lines.clear();
    std::string line;
    try
    {
        size_t written = 0;
        return DrainWeaponEventsImpl(sessionToken, line,
            [&lines, &written, maxLines](const std::string& text) {
                if (written >= maxLines)
                    return false; // sink failure: drain must not advance
                lines.push_back(text);
                ++written;
                return true;
            },
            kWeaponEventNormalGapBudget);
    }
    catch (...)
    {
        return false;
    }
}

bool Telemetry_TestFinalDrainWeaponEvents(uint64_t sessionToken,
    std::vector<std::string>& lines) noexcept
{
    lines.clear();
    std::string line;
    try
    {
        // Same final-drain core the recorder uses at session finalization:
        // producer quiescence plus the unlimited frontier budget.
        return DrainWeaponEventsToFrontierImpl(sessionToken, line,
            [&lines](const std::string& text) {
                lines.push_back(text);
                return true;
            });
    }
    catch (...)
    {
        return false;
    }
}

uint32_t Telemetry_TestWeaponEventProducersInFlight() noexcept
{
    return g_weaponEventProducersInFlight.load(std::memory_order_seq_cst);
}

void Telemetry_TestPauseWeaponEventProducer(bool enabled) noexcept
{
    g_weaponEventProducerReachedPause.store(false, std::memory_order_release);
    g_pauseWeaponEventProducer.store(enabled, std::memory_order_release);
}

bool Telemetry_TestWeaponEventProducerReachedPause() noexcept
{
    return g_weaponEventProducerReachedPause.load(std::memory_order_acquire);
}

void Telemetry_TestPauseWeaponEventCommit(bool enabled) noexcept
{
    g_weaponEventCommitReachedPause.store(false, std::memory_order_release);
    g_holdWeaponEventCommit.store(enabled, std::memory_order_release);
    g_armWeaponEventCommitPause.store(enabled, std::memory_order_release);
}

bool Telemetry_TestWeaponEventCommitReachedPause() noexcept
{
    return g_weaponEventCommitReachedPause.load(std::memory_order_acquire);
}

void Telemetry_TestBumpAdmissionGeneration() noexcept
{
    g_admissionToken.fetch_add(uint64_t{2}, std::memory_order_seq_cst);
}

void Telemetry_TestResetAdmissionToken() noexcept
{
    g_admissionToken.store(0, std::memory_order_seq_cst);
}
#endif

#ifdef HALOMCCVR_TELEMETRY_TESTING
void Telemetry_TestResetRing() noexcept
{
    g_headIndex.store(0, std::memory_order_relaxed);
    g_tailIndex.store(0, std::memory_order_relaxed);
    g_droppedQueueFull.store(0, std::memory_order_relaxed);
}

bool Telemetry_TestPushRing(const TelemetryFrame& frame) noexcept
{
    return PushRing(frame);
}

bool Telemetry_TestPopRing(TelemetryFrame& frame) noexcept
{
    return PopRing(frame);
}

uint64_t Telemetry_TestRingDrops() noexcept
{
    return g_droppedQueueFull.load(std::memory_order_relaxed);
}

void Telemetry_TestSetForceOpenFailure(bool enabled) noexcept
{
    g_forceOpenFailure.store(enabled, std::memory_order_release);
}

void Telemetry_TestFailWriteAfter(int32_t successfulWrites) noexcept
{
    g_failWriteAfter.store(successfulWrites, std::memory_order_release);
}

void Telemetry_TestFailFlushAfter(int32_t successfulFlushes) noexcept
{
    g_failFlushAfter.store(successfulFlushes, std::memory_order_release);
}

void Telemetry_TestSetForceCloseFailure(bool enabled) noexcept
{
    g_forceCloseFailure.store(enabled, std::memory_order_release);
}

void Telemetry_TestPauseStarting(bool enabled) noexcept
{
    g_pauseStarting.store(enabled, std::memory_order_release);
}

void Telemetry_TestPauseRecording(bool enabled) noexcept
{
    g_workerReachedRecording.store(false, std::memory_order_release);
    g_pauseRecording.store(enabled, std::memory_order_release);
}

bool Telemetry_TestWorkerReachedRecording() noexcept
{
    return g_workerReachedRecording.load(std::memory_order_acquire);
}

void Telemetry_TestPauseRecordingDrain(bool enabled) noexcept
{
    g_workerReachedRecordingDrain.store(false, std::memory_order_release);
    g_pauseRecordingDrain.store(enabled, std::memory_order_release);
}

bool Telemetry_TestWorkerReachedRecordingDrain() noexcept
{
    return g_workerReachedRecordingDrain.load(std::memory_order_acquire);
}

void Telemetry_TestPauseFinalizing(bool enabled) noexcept
{
    g_pauseFinalizing.store(enabled, std::memory_order_release);
}

void Telemetry_TestPauseProducerAdmission(bool enabled) noexcept
{
    g_producerReachedAdmission.store(false, std::memory_order_release);
    g_pauseProducerAdmission.store(enabled, std::memory_order_release);
}

bool Telemetry_TestProducerReachedAdmission() noexcept
{
    return g_producerReachedAdmission.load(std::memory_order_acquire);
}

void Telemetry_TestPauseProducerSecondCheck(bool enabled) noexcept
{
    g_producerReachedSecondCheck.store(false, std::memory_order_release);
    g_pauseProducerSecondCheck.store(enabled, std::memory_order_release);
}

bool Telemetry_TestProducerReachedSecondCheck() noexcept
{
    return g_producerReachedSecondCheck.load(std::memory_order_acquire);
}

bool Telemetry_TestSerializeFrame(
    const TelemetryFrame& frame, char* output, size_t capacity,
    size_t& written) noexcept
{
    written = 0;
    if (!output || capacity == 0)
        return false;
    try
    {
        std::string line;
        SerializeFrameLine(frame, line);
        if (line.size() + 1 > capacity)
            return false;
        std::memcpy(output, line.data(), line.size());
        output[line.size()] = '\0';
        written = line.size();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool Telemetry_TestEscapeJson(
    const char* value, char* output, size_t capacity,
    size_t& written) noexcept
{
    written = 0;
    if (!output || capacity == 0)
        return false;
    try
    {
        std::string escaped;
        AppendJsonString(escaped, value);
        if (escaped.size() + 1 > capacity)
            return false;
        std::memcpy(output, escaped.data(), escaped.size());
        output[escaped.size()] = '\0';
        written = escaped.size();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::wstring Telemetry_TestQuoteWindowsArgument(const wchar_t* value)
{
    return QuoteWindowsArgument(value);
}

std::vector<wchar_t> Telemetry_TestBuildUtf8Environment(
    const wchar_t* environmentBlock)
{
    return BuildUtf8EnvironmentBlock(environmentBlock);
}

bool Telemetry_TestBuildAnalyserLaunchPlan(
    const wchar_t* moduleDirectory, const wchar_t* recordingPath,
    const wchar_t* interpreterPath, bool pyLauncher,
    TelemetryAnalyserLaunchPlanTest& plan)
{
    try
    {
        AnalyserLaunchPlan built{};
        if (!BuildAnalyserLaunchPlan(
                moduleDirectory, recordingPath, interpreterPath,
                pyLauncher, built))
        {
            return false;
        }
        plan.scriptPath = built.scriptPath;
        plan.recordingPath = built.recordingPath;
        plan.commandLine = built.commandLine;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void Telemetry_TestResetAnalyserLauncher()
{
    g_testPythonAvailable = true;
    g_testPyAvailable = true;
    g_testPythonLaunchSucceeds = true;
    g_testPyLaunchSucceeds = true;
    g_testUseRealAnalyserLauncher = false;
    g_testSessionClosed = false;
    g_testPauseAnalyserLaunch.store(false, std::memory_order_release);
    g_testAnalyserLaunchReached.store(false, std::memory_order_release);
    g_testAnalyserLaunchRequests.store(0, std::memory_order_relaxed);
    g_testAnalyserLaunchCompletions.store(0, std::memory_order_relaxed);
    g_testAnalyserModuleDirectory.clear();
    g_testAnalyserLauncherSnapshot = {};
}

void Telemetry_TestConfigureAnalyserLauncher(
    bool pythonAvailable, bool pyAvailable,
    bool pythonLaunchSucceeds, bool pyLaunchSucceeds)
{
    g_testPythonAvailable = pythonAvailable;
    g_testPyAvailable = pyAvailable;
    g_testPythonLaunchSucceeds = pythonLaunchSucceeds;
    g_testPyLaunchSucceeds = pyLaunchSucceeds;
}

void Telemetry_TestSetAnalyserModuleDirectory(const wchar_t* directory)
{
    g_testAnalyserModuleDirectory = directory ? directory : L"";
}

const wchar_t* Telemetry_TestRecordingDirectoryPath() noexcept
{
    return g_directory.c_str();
}

void Telemetry_TestUseRealAnalyserLauncher(bool enabled) noexcept
{
    g_testUseRealAnalyserLauncher = enabled;
}

void Telemetry_TestPauseAnalyserLaunch(bool enabled) noexcept
{
    g_testPauseAnalyserLaunch.store(enabled, std::memory_order_release);
}

bool Telemetry_TestAnalyserLaunchReached() noexcept
{
    return g_testAnalyserLaunchReached.load(std::memory_order_acquire);
}

void Telemetry_TestLaunchAnalyser(
    const wchar_t* moduleDirectory, const wchar_t* recordingPath) noexcept
{
    if (!moduleDirectory || !recordingPath)
        return;
    try
    {
        g_testAnalyserModuleDirectory = moduleDirectory;
        TryLaunchTelemetryAnalyser(recordingPath);
    }
    catch (...)
    {
    }
}

uint32_t Telemetry_TestAnalyserLaunchCompletions() noexcept
{
    return g_testAnalyserLaunchCompletions.load(std::memory_order_acquire);
}

TelemetryAnalyserLauncherTestSnapshot
Telemetry_TestGetAnalyserLauncherSnapshot()
{
    TelemetryAnalyserLauncherTestSnapshot snapshot =
        g_testAnalyserLauncherSnapshot;
    snapshot.launchRequests = g_testAnalyserLaunchRequests.load(
        std::memory_order_acquire);
    snapshot.launchCompletions = g_testAnalyserLaunchCompletions.load(
        std::memory_order_acquire);
    return snapshot;
}
#endif
