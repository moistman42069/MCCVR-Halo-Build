// DRIVE CASCADE, the DLL's side (drive_cascade.h).
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "drive_cascade.h"
#include "sigscan.h"
#include "../common/config.h"
#include "../common/log.h"
#include "../common/title_build_identity.h"

namespace
{
bool Readable(DWORD protect)
{
    if (protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;
    const DWORD kind = protect & 0xFFu;
    return kind == PAGE_READONLY || kind == PAGE_READWRITE || kind == PAGE_WRITECOPY || kind == PAGE_EXECUTE_READ ||
           kind == PAGE_EXECUTE_READWRITE || kind == PAGE_EXECUTE_WRITECOPY;
}

// The committed, readable runs of [begin, end), region by region, appended to `runs` as RVAs of `base`. A title's
// mapped image may hold pages that cannot be read; a search must step over them, not fault on them. False when
// there are more runs than `capacity`.
bool ReadableRuns(uintptr_t base, uintptr_t begin, uintptr_t end, drive_resolve::Range* runs, uint32_t& count,
                  uint32_t capacity)
{
    uintptr_t runBegin = 0;
    const auto close = [&](uintptr_t at) {
        if (!runBegin)
            return true;
        if (count >= capacity)
            return false;
        runs[count++] = drive_resolve::Range{static_cast<uint32_t>(runBegin - base), static_cast<uint32_t>(at - runBegin)};
        runBegin = 0;
        return true;
    };
    for (uintptr_t at = begin; at < end;)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) || !info.RegionSize)
            return close(at);
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (regionEnd <= at)
            return close(at);
        if (info.State == MEM_COMMIT && Readable(info.Protect))
        {
            if (!runBegin)
                runBegin = at;
        }
        else if (!close(at))
        {
            return false;
        }
        at = regionEnd < end ? regionEnd : end;
    }
    return close(end);
}

bool WhollyReadable(uintptr_t begin, size_t size)
{
    for (uintptr_t at = begin, end = begin + size; at < end;)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) || !info.RegionSize ||
            info.State != MEM_COMMIT || !Readable(info.Protect))
            return false;
        at = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

// The module's own TLS index, from its PE TLS directory. Guarded on its own: a module whose directory cannot be
// read loses the drives that need the index, not the one that does not (Halo 2's).
void ReadTlsIndex(uintptr_t base, size_t size, drive_resolve::Image& image)
{
    __try
    {
        uint32_t index = 0;
        uintptr_t rva = 0;
        if (addrcascade::tls_index(reinterpret_cast<void*>(base), &index, &rva) && rva && rva < size &&
            sizeof(uint32_t) <= size - rva)
            image.tlsIndexRva = static_cast<uint32_t>(rva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        image.tlsIndexRva = 0;
    }
}

// The module as the resolution reads it. The caller guards this (the headers are read as they stand).
bool Open(uintptr_t base, size_t size, drive_resolve::Image& image, const char*& why)
{
    if (!base || !WhollyReadable(base, 0x1000))
    {
        why = "the module's headers are not readable";
        return false;
    }
    // Outside the readable runs found below, every read asks the system first (WhollyReadable): a page that
    // cannot be read costs the line that needed it, and the rest of the drive's resolution stands. The function
    // table is asked about before any of it is read: one that cannot be read is no table, so nothing that needs
    // a function's start or extent resolves, and the drives that need one are not hooked.
    if (!drive_resolve::ReadImage(reinterpret_cast<const uint8_t*>(base), size, image, why, &WhollyReadable))
        return false;
    // Search only what can be read of each executable section.
    drive_resolve::Range runs[drive_resolve::kMaxExecutableRanges]{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < image.executableCount; ++i)
    {
        const uintptr_t begin = base + image.executable[i].rva;
        if (!ReadableRuns(base, begin, begin + image.executable[i].size, runs, count,
                          drive_resolve::kMaxExecutableRanges))
        {
            why = "the module's code is split into more readable pieces than a search holds";
            return false;
        }
    }
    if (!count)
    {
        why = "none of the module's code is readable";
        return false;
    }
    for (uint32_t i = 0; i < drive_resolve::kMaxExecutableRanges; ++i)
        image.executable[i] = i < count ? runs[i] : drive_resolve::Range{};
    image.executableCount = count;
    ReadTlsIndex(base, size, image);
    // Every compare and every count is of the game's own bytes, whatever this mod has hooked by now.
    image.original = &sig::CopyUnhooked;
    image.correct = &sig::CorrectLiveCountForHooks;
    return true;
}

bool GuardedOpen(uintptr_t base, size_t size, drive_resolve::Image& image, const char*& why)
{
    __try
    {
        return Open(base, size, image, why);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        why = "a read of the module faulted";
        return false;
    }
}

bool GuardedRun(const drive_resolve::Image& image, uint32_t faults, bool recordedBuild,
                const drive_known::Earlier& earlier, drive_known::PlanFn plan, drive_known::KeepFn keep, void* context,
                drive_resolve::Report& report, drive_known::Outcome& outcome, const char*& why)
{
    __try
    {
        outcome = drive_known::Run(image, faults, recordedBuild, earlier, plan, keep, context, report);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        why = "a read of the module faulted";
        return false;
    }
}

// The faults forced on a resolution. DEV BUILDS ONLY (config key drive_fault; drive_resolve.h: the kFault bits):
// each forces what only a game build the tables were not measured on does, so the rung below it, or the refusal,
// can be watched working in the running game. A player build has no key and none of the forcing code:
// drive_resolve::Fault and addrcascade::fault are constant false there.
uint32_t Faults()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    addrcascade::set_fault_mask(g_config.drive_fault);
    return static_cast<uint32_t>(addrcascade::fault_mask());
#else
    return 0;
#endif
}

// ------------------------------------------------------------------------------------------ the cache's file
// Read and written where the drives are installed. Every install is reached from the one cold worker thread
// (the module poll's), so nothing contends for the lock today: that worker takes it, alone, and so do the tests.
// It is kept so that the text buffer and the parsed file below stay whole if an install is ever reached from a
// second thread; they belong to whoever holds it.
wchar_t g_cachePath[MAX_PATH] = L"";
SRWLOCK g_cacheLock = SRWLOCK_INIT;
char g_cacheText[drive_known::kMaxFileText];
drive_known::File g_cacheFile;
// Every open of the file, to read or to write: an open may wait on a synced or a network folder, so there must
// be none that is not needed (drive_cascade::CacheFileOpens).
std::atomic<uint32_t> g_cacheOpens{0};

// DEV BUILDS ONLY: the tests' probe (drive_cascade::SetCacheProbe), called with the lock held before each open.
#if HALOMCCVR_DEV_DIAGNOSTICS
std::atomic<void (*)()> g_cacheProbe{nullptr};
void CacheProbe()
{
    if (const auto probe = g_cacheProbe.load(std::memory_order_relaxed))
        probe();
}
#else
void CacheProbe() {}
#endif

// What a read of the file came to. "There is no file" and "there is a file that was not read" are two things:
// the first is a cold start and the file may be written; the second is somebody's file, whose other entries a
// write would destroy.
enum class CacheRead : uint8_t
{
    NoFile,       // there is none: nothing is held, and one may be written
    Read,         // read (whatever it holds: text that is not a cache holds nothing, and is replaced by a write)
    Unreadable,   // it is there and could not be opened or read (another program holds it, no right to it)
    TooLarge,     // it is there and is larger than any file this mod writes: not this mod's
};

constexpr char kFileUnreadable[] = "the file is there and could not be read";
constexpr char kFileTooLarge[] = "the file is there and is larger than any this mod writes";
constexpr char kFileNotWritten[] = "the file could not be written";

// Reads the file into g_cacheFile. Whatever it comes to, g_cacheFile holds only what was read and understood.
CacheRead ReadCacheLocked()
{
    g_cacheFile = drive_known::File{};
    g_cacheOpens.fetch_add(1, std::memory_order_relaxed);
    CacheProbe();
    const HANDLE handle = CreateFileW(g_cachePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? CacheRead::NoFile
                                                                             : CacheRead::Unreadable;
    }
    DWORD read = 0;
    const BOOL ok = ReadFile(handle, g_cacheText, sizeof(g_cacheText), &read, nullptr);
    CloseHandle(handle);
    if (!ok)
        return CacheRead::Unreadable;
    // A file that fills the buffer is larger than any this mod writes: it is not this mod's.
    if (read >= sizeof(g_cacheText))
        return CacheRead::TooLarge;
    drive_known::Parse(g_cacheText, read, g_cacheFile);
    return CacheRead::Read;
}

// Writes g_cacheFile. NEVER IN PLACE: the text goes to a temporary file beside the cache, which then takes its
// place in one step. Until that step the old file is whole, and a write that fails leaves it so.
bool WriteCacheLocked()
{
    const size_t length = drive_known::Format(g_cacheFile, g_cacheText, sizeof(g_cacheText));
    wchar_t temporary[MAX_PATH + 8];
    if (!length || _snwprintf_s(temporary, _TRUNCATE, L"%ls.tmp", g_cachePath) < 0)
        return false;
    g_cacheOpens.fetch_add(1, std::memory_order_relaxed);
    CacheProbe();
    const HANDLE handle =
        CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool whole = WriteFile(handle, g_cacheText, static_cast<DWORD>(length), &written, nullptr) &&
                       written == length && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (whole && MoveFileExW(temporary, g_cachePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temporary);
    return false;
}

// THE LOCK IS RELEASED WHATEVER HAPPENS UNDER IT (LookUp, Store). LookUp runs inside the rungs, and the rungs run
// under their caller's structured-exception guard: an exception that left here with the lock still held would
// be swallowed there, and the next resolution -- of any drive, of any title -- would wait on the lock for ever,
// with the worker that installs and reports everything. Hence the __finally; and hence nothing in these two
// functions has a destructor, which a function with __try may not.

// This drive's saved addresses for exactly this build of its module, found under the rules this mod searches by.
// `refused`: some were held, and are of another build or of other rules.
bool LookUp(const char* module, const char* id, uint64_t stamp, drive_known::Entry& entry, const char*& refused)
{
    bool have = false;
    AcquireSRWLockExclusive(&g_cacheLock);
    __try
    {
        (void)ReadCacheLocked();   // a file that was not read holds no entry: the drive is searched for
        if (const drive_known::Entry* const usable = drive_known::Usable(g_cacheFile, module, id, stamp, refused))
        {
            entry = *usable;
            have = true;
        }
    }
    __finally
    {
        ReleaseSRWLockExclusive(&g_cacheLock);
    }
    return have;
}

// Puts one drive's addresses into the file. The file is read again first, so every other drive's and every other
// title's entry is kept (a file that is full lets its oldest dead entry give way: drive_known::Put). Null when
// it is saved; else why it is not. A FILE THAT IS THERE AND WAS NOT READ IS NOT WRITTEN: the entries it holds
// are not known, and writing would replace them all with this one.
const char* Store(const drive_known::Entry& entry)
{
    const char* failed = nullptr;
    AcquireSRWLockExclusive(&g_cacheLock);
    __try
    {
        const CacheRead read = ReadCacheLocked();
        if (read == CacheRead::Unreadable)
            failed = kFileUnreadable;
        else if (read == CacheRead::TooLarge)
            failed = kFileTooLarge;
        else
        {
            drive_known::Put(g_cacheFile, entry);
            if (!WriteCacheLocked())
                failed = kFileNotWritten;
        }
    }
    __finally
    {
        ReleaseSRWLockExclusive(&g_cacheLock);
    }
    return failed;
}

// ----------------------------------------------------------------------- what this process already resolved
// The last resolution of each drive, with the module instance it was made in (drive_cascade.h). Taken and left by
// the one cold worker; the lock is for the same reason as the cache's.
struct Remembered
{
    bool held;
    GameTitle title;
    char id[drive_known::kMaxId];
    uintptr_t base;
    uint64_t stamp;
    uint32_t generation;
    uint32_t searchedAgain;   // a result that is not whole: the fresh searches already made for it in this module
    // The report, kept as its bytes: with no initialiser of its own the whole table is zero until something is
    // remembered, and costs the DLL's file nothing.
    alignas(drive_resolve::Report) unsigned char report[sizeof(drive_resolve::Report)];
};
static_assert(std::is_trivially_copyable_v<drive_resolve::Report>, "a report is remembered as its bytes");
Remembered g_remembered[12];   // one for each drive of each title; there are nine. Zero: nothing is held
SRWLOCK g_rememberedLock = SRWLOCK_INIT;
std::atomic<uint32_t (*)(GameTitle)> g_generationOf{nullptr};

Remembered* RememberedFor(const drive_cascade::Drive& drive)
{
    for (Remembered& slot : g_remembered)
        if (slot.held && slot.title == drive.title && !std::strcmp(slot.id, drive.id))
            return &slot;
    return nullptr;
}

// What is remembered of `drive` in exactly this module instance, copied out. What is remembered of it in another
// instance describes a module that is gone, and is dropped.
bool Recall(const drive_cascade::Drive& drive, uintptr_t base, uint64_t stamp, uint32_t generation,
            drive_resolve::Report& out, uint32_t& searchedAgain)
{
    bool have = false;
    searchedAgain = 0;
    AcquireSRWLockExclusive(&g_rememberedLock);
    if (Remembered* const slot = RememberedFor(drive))
    {
        have = slot->base == base && slot->stamp == stamp && slot->generation == generation;
        if (have)
        {
            std::memcpy(&out, slot->report, sizeof(out));
            searchedAgain = slot->searchedAgain;
        }
        else
        {
            slot->held = false;
        }
    }
    ReleaseSRWLockExclusive(&g_rememberedLock);
    return have;
}

void Remember(const drive_cascade::Drive& drive, uintptr_t base, uint64_t stamp, uint32_t generation,
              const drive_resolve::Report& report, uint32_t searchedAgain)
{
    AcquireSRWLockExclusive(&g_rememberedLock);
    Remembered* slot = RememberedFor(drive);
    for (Remembered& unused : g_remembered)
        if (!slot && !unused.held)
            slot = &unused;
    if (slot && drive_known::Word(drive.id, drive_known::kMaxId))
    {
        slot->held = true;
        slot->title = drive.title;
        snprintf(slot->id, sizeof(slot->id), "%s", drive.id);
        slot->base = base;
        slot->stamp = stamp;
        slot->generation = generation;
        slot->searchedAgain = searchedAgain;
        std::memcpy(slot->report, &report, sizeof(report));
    }
    ReleaseSRWLockExclusive(&g_rememberedLock);
}

void Forget(const drive_cascade::Drive& drive)
{
    AcquireSRWLockExclusive(&g_rememberedLock);
    if (Remembered* const slot = RememberedFor(drive))
        slot->held = false;
    ReleaseSRWLockExclusive(&g_rememberedLock);
}

// What asking the cache for one drive's saved addresses needs, and where the answer is kept while the rungs run.
struct Asking
{
    const char* module;
    const char* id;
    uint64_t stamp;
    drive_known::Entry entry;
};

// drive_known::Earlier::look: opens and reads the file. Called by the rungs only when the entry is needed.
const drive_known::Entry* AskCache(void* raw, const char*& refused)
{
    auto* const asking = static_cast<Asking*>(raw);
    return LookUp(asking->module, asking->id, asking->stamp, asking->entry, refused) ? &asking->entry : nullptr;
}

// One entry of the announced list: what a line that is not "as recorded" says.
int DescribeLine(const drive_resolve::Line& line, char* out, size_t size)
{
    using drive_resolve::Verdict;
    switch (line.verdict)
    {
    case Verdict::Moved:
        return snprintf(out, size, "%s MOVED from +0x%X to +0x%X", line.name, line.recorded, line.found);
    case Verdict::RecordAmongSeveral:
        return snprintf(out, size, "%s taken at the recorded +0x%X among %u%s matches", line.name, line.found,
                        line.matches, line.matches >= drive_resolve::kCountCap ? " or more" : "");
    case Verdict::Ambiguous:
        return snprintf(out, size, "%s AMBIGUOUS (%u%s matches, none at the recorded +0x%X)", line.name, line.matches,
                        line.matches >= drive_resolve::kCountCap ? " or more" : "", line.recorded);
    case Verdict::Refused:
        return snprintf(out, size, "%s REFUSED (%s)", line.name, line.note ? line.note : "a proof failed");
    default:
        return snprintf(out, size, "%s %s", line.name, drive_resolve::VerdictText(line.verdict));
    }
}

// The lines of a report that are named in its one line, as many as fit: those not found (`lostOnly`), or every
// one that is not simply where the tables record it.
void ListLines(const drive_resolve::Report& report, bool lostOnly, char* list, size_t size)
{
    size_t used = 0;
    uint32_t named = 0, unnamed = 0;
    list[0] = 0;
    for (uint32_t i = 0; i < report.count; ++i)
    {
        const drive_resolve::Line& line = report.lines[i];
        if (lostOnly ? drive_resolve::Found(line.verdict) : line.verdict == drive_resolve::Verdict::AsRecorded)
            continue;
        char one[200];
        const int text = DescribeLine(line, one, sizeof(one));
        if (text <= 0 || used + static_cast<size_t>(text) + 3 >= size - 24)
        {
            ++unnamed;
            continue;
        }
        used += static_cast<size_t>(snprintf(list + used, size - used, "%s%s", named ? "; " : "", one));
        ++named;
    }
    if (unnamed)
        snprintf(list + used, size - used, "; and %u more", unnamed);
}
} // namespace

namespace drive_cascade
{
void SetCacheFile(const wchar_t* path)
{
    AcquireSRWLockExclusive(&g_cacheLock);
    if (!path || _snwprintf_s(g_cachePath, _TRUNCATE, L"%ls", path) < 0)
        g_cachePath[0] = 0;
    ReleaseSRWLockExclusive(&g_cacheLock);
}

uint32_t CacheFileOpens() { return g_cacheOpens.load(std::memory_order_relaxed); }

#if HALOMCCVR_DEV_DIAGNOSTICS
void SetCacheProbe(void (*probe)()) { g_cacheProbe.store(probe, std::memory_order_relaxed); }

bool CacheLockHeld()
{
    if (!TryAcquireSRWLockExclusive(&g_cacheLock))
        return true;
    ReleaseSRWLockExclusive(&g_cacheLock);
    return false;
}
#endif

void SetModuleGenerationSource(uint32_t (*generationOf)(GameTitle title))
{
    g_generationOf.store(generationOf, std::memory_order_relaxed);
}

void ForgetResolutions()
{
    AcquireSRWLockExclusive(&g_rememberedLock);
    for (Remembered& slot : g_remembered)
        slot.held = false;
    ReleaseSRWLockExclusive(&g_rememberedLock);
}

void SetCacheFolder(const wchar_t* folder)
{
    wchar_t path[MAX_PATH];
    if (!folder || _snwprintf_s(path, _TRUNCATE, L"%lsHaloMCCVR-drive-addresses.txt", folder) < 0)
        path[0] = 0;
    SetCacheFile(path);
}

bool RunPlan(uintptr_t base, size_t size, const Drive& drive, drive_known::PlanFn plan, drive_known::KeepFn keep,
             void* context, drive_resolve::Report& report, Rungs& rungs, const char*& why)
{
    // Every caller is the one cold worker thread, so it sets the library's log sink once. The atomic keeps that
    // true if an install is ever reached from a second thread; nothing relies on there being one today.
    static std::atomic<bool> s_sinkSet{false};
    if (!s_sinkSet.exchange(true))
        addrcascade::set_logger([](const char* message) { LOG("%s", message); });
    // What this resolution costs the worker is measured whole, from here to its last rung.
    LARGE_INTEGER from;
    QueryPerformanceCounter(&from);
    report = drive_resolve::Report{};
    rungs = Rungs{};
    why = nullptr;
    drive_resolve::Image image;
    if (!GuardedOpen(base, size, image, why))
    {
        // A module that does not open is not this drive's module as it was remembered: start afresh next time.
        if (drive.id)
            Forget(drive);
        return false;
    }
    const uint32_t faults = Faults();
    // While ANY bit of the dev key is set the player's file is neither read nor written. What a forced fault
    // makes of a drive is not what the game DLL holds and must never be found there by a later launch; and a
    // bit that forces nothing (the self-test, the silenced watchdog) still marks a launch whose result nobody
    // meant to keep.
    const bool cacheOn = !faults && g_config.drive_address_cache != 0 && g_cachePath[0] != 0;
    const char* const module = drive_known::ModuleName(drive.title);
    const uint64_t stamp = drive_known::Stamp(image);
    rungs.recordedBuild =
        title_build::Compare(drive.title, image.timeDateStamp, image.sizeOfImage) == title_build::Match::Known;
    // The cache is not opened here. The rungs ask for this drive's entry when they need it, which they do not
    // while the recorded addresses verify: a healthy install on the recorded build opens no file.
    Asking asking{module, drive.id, stamp, {}};
    drive_known::Earlier earlier;
    if (cacheOn && drive.id)
    {
        earlier.look = &AskCache;
        earlier.lookContext = &asking;
    }
    // WHAT THIS PROCESS ALREADY RESOLVED IN THIS VERY MODULE (drive_cascade.h): handed to the rungs, which verify
    // it in place before they ask the cache or search. Not while a dev fault is set: what a fault makes of a
    // drive is neither remembered nor allowed to hide behind what was.
    const auto generationOf = g_generationOf.load(std::memory_order_relaxed);
    const uint32_t generation = generationOf ? generationOf(drive.title) : 0;
    drive_resolve::Report remembered;
    uint32_t searchedAgain = 0;
    const bool recalled = !faults && drive.id && Recall(drive, base, stamp, generation, remembered, searchedAgain);
    // A WHOLE result is verified in place. One that is NOT whole is not believed from one search: the drive is
    // resolved afresh, kFreshSearches times in this module instance, before the miss is taken for the module's
    // own and only repeated (drive_known.h). The cache is not asked on those fresh searches: whatever it holds
    // did not serve this drive at its first resolution here, only a whole result is ever written to it, and the
    // search that runs instead needs nothing known. (The recorded addresses are tried first all the same.)
    const bool again = recalled && !drive_known::RepeatRemembered(remembered, searchedAgain);
    if (recalled && !again)
        earlier.remembered = &remembered;
    if (again)
        earlier.look = nullptr;
    if (!GuardedRun(image, faults, rungs.recordedBuild, earlier, plan, keep, context, report, rungs.outcome, why))
    {
        report = drive_resolve::Report{};
        rungs = Rungs{};
        if (drive.id)
            Forget(drive);
        return false;
    }
    // A plan too long for a report is refused whole, with its own reason, as a module that did not open is:
    // nothing of it is kept, remembered or saved.
    if (rungs.outcome.refused)
    {
        why = rungs.outcome.refused;
        report = drive_resolve::Report{};
        rungs = Rungs{};
        if (drive.id)
            Forget(drive);
        return false;
    }
    // REMEMBER WHAT IT CAME TO, whole or not, for the next install of this drive in this module. Rung 1 (a) needs
    // no memory -- it comes first and costs as little -- so a drive it settled leaves none behind.
    if (!faults && drive.id)
    {
        if (rungs.outcome.rung == drive_known::Rung::Recorded)
            Forget(drive);
        else if (rungs.outcome.rung != drive_known::Rung::Remembered)
        {
            // A fresh search that is still not whole counts towards kFreshSearches; one that now finds
            // everything takes the old result's place, and nothing more is counted.
            Remember(drive, base, stamp, generation, report, again && !report.Complete() ? searchedAgain + 1 : 0);
        }
    }
    rungs.searchedAgain = again && rungs.outcome.rung == drive_known::Rung::Search ? searchedAgain + 1 : 0;
    rungs.searches = rungs.outcome.rung == drive_known::Rung::Remembered && !report.Complete() ? 1 + searchedAgain : 0;
    rungs.savedRefused = rungs.outcome.savedRefused;
    drive_known::Entry entry;
    // LEARN AND SAVE: what the search found on a build the tables do not describe is the next launch's rung 1.
    if (rungs.outcome.learn)
    {
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (faults)
            rungs.notSaved = "drive_fault is set: a launch with the dev key in it neither reads nor writes the file";
        else
#endif
        if (!g_config.drive_address_cache)
            rungs.notSaved = "drive_address_cache = 0";
        else if (!g_cachePath[0])
            rungs.notSaved = "the mod's folder is not known";
        else if (!drive_known::MakeEntry(entry, module, drive.id, stamp, report))
            rungs.notSaved = "this drive has no name to save them under";
        else
            rungs.saved = (rungs.notSaved = Store(entry)) == nullptr;
    }
    LARGE_INTEGER to, frequency;
    QueryPerformanceCounter(&to);
    QueryPerformanceFrequency(&frequency);
    rungs.ms = static_cast<double>(to.QuadPart - from.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    return true;
}

void FirstFailure(const drive_resolve::Report& report, char* out, size_t size)
{
    if (!out || !size)
        return;
    for (uint32_t i = 0; i < report.count; ++i)
        if (!drive_resolve::Found(report.lines[i].verdict))
        {
            (void)DescribeLine(report.lines[i], out, size);
            return;
        }
    snprintf(out, size, "nothing");
}

void Announce(const char* drive, const drive_resolve::Report& report, const Rungs& rungs)
{
    using drive_known::Rung;
    using drive_resolve::Verdict;
    const drive_known::Outcome& outcome = rungs.outcome;
    const uint32_t recorded = report.Count(Verdict::AsRecorded), moved = report.Count(Verdict::Moved),
                   several = report.Count(Verdict::RecordAmongSeveral), lost = report.NotFound();
    // WHAT RESOLVING THIS DRIVE'S ADDRESSES COST THE WORKER at this install: its searches of the whole module
    // (every rung that ran), and its time. It goes into the one line below, whichever that is, and is labelled
    // as what it is: whatever else the install binds afterwards searches the module in its own code and is in
    // neither number.
    char cost[72];
    snprintf(cost, sizeof(cost), "(address resolution: %u scans, %.2f ms)", outcome.moduleScans, rungs.ms);
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (const int forced = addrcascade::fault_mask() & static_cast<int>(drive_resolve::kFaultsThatForce))
        LOG("%s: DEV fault injection (drive_fault 0x%X) is forcing what the next line reports; it says nothing about "
            "this game build", drive, static_cast<unsigned>(forced));
#endif
    if (outcome.rung == Rung::Recorded)
    {
        LOG("%s: verified at the recorded addresses, all %u of them, with no search %s (this game DLL is the build "
            "the tables were measured on)", drive, report.count, cost);
    }
    else if (outcome.rung == Rung::Remembered)
    {
        // The same result as the install before, said again with what THIS install cost. What was not found is
        // named again: a drive that is refused is refused in so many words at every install. A result that is
        // not whole is repeated only once the module has been searched afresh kFreshSearches times
        // (rungs.searches: how many searches came to it), and its line says that it is not searched again.
        char list[720];
        ListLines(report, true, list, sizeof(list));
        if (lost)
            LOG("%s: as this session already resolved it in this module, with no search %s: %u of its %u addresses "
                "verified again where they stand, %u were NOT found by its %u searches of this module and are not "
                "searched again in this module: %s",
                drive, cost, report.count - lost, report.count, lost, rungs.searches, list);
        else
            LOG("%s: as this session already resolved it in this module, with no search %s: all %u of its "
                "addresses verified again where they stand", drive, cost, report.count);
    }
    else if (outcome.rung == Rung::Saved)
    {
        // A line saved as the single match in its function has had that function searched again: the rule it
        // was placed by, applied whole (drive_resolve.h).
        if (report.scans)
            LOG("%s: verified at the addresses saved from an earlier launch of this game build, all %u of them, with "
                "no search of the module %s (%u of them are not where the tables record them; %u of its functions "
                "were searched again, each for a line saved as the single match in it)",
                drive, report.count, cost, moved, report.scans);
        else
            LOG("%s: verified at the addresses saved from an earlier launch of this game build, all %u of them, with "
                "no search %s (%u of them are not where the tables record them)", drive, report.count, cost, moved);
    }
    else
    {
        // What the known addresses came to first.
        char before[240] = "";
        size_t length = 0;
        if (outcome.recordedTried)
            length += static_cast<size_t>(snprintf(before, sizeof(before), "the recorded addresses did not all verify "
                                                                          "in this game DLL; "));
        if (rungs.savedRefused && length < sizeof(before))
            snprintf(before + length, sizeof(before) - length, "the addresses saved earlier were not used (%s); ",
                     rungs.savedRefused);
        // Name the ones that are not as recorded, as many as fit.
        char list[720];
        ListLines(report, false, list, sizeof(list));
        // A drive whose remembered result was not whole is resolved afresh a few times before the miss is
        // believed (drive_known::kFreshSearches): its line says which of those this was.
        char again[40] = "";
        if (rungs.searchedAgain)
            snprintf(again, sizeof(again), " again, %u of %u", rungs.searchedAgain, drive_known::kFreshSearches);
        if (lost)
        {
            LOG("%s: %ssearched%s %s: of its %u addresses %u are where the tables record them, %u MOVED, %u were "
                "taken at the recorded address among several matches, %u were NOT found: %s",
                drive, before, again, cost, report.count, recorded, moved, several, lost, list);
        }
        else
        {
            char saving[160];
            if (rungs.saved)
                snprintf(saving, sizeof(saving), "saved for the next launch");
            else if (rungs.notSaved)
                snprintf(saving, sizeof(saving), "NOT saved for the next launch (%s)", rungs.notSaved);
            else
                snprintf(saving, sizeof(saving), "nothing to save");
            char foundAgain[64] = "";
            if (rungs.searchedAgain)
                snprintf(foundAgain, sizeof(foundAgain), "searched%s, and found", again);
            LOG("%s: %s%s %s: of its %u addresses %u are where the tables record them, %u MOVED, %u were taken at "
                "the recorded address among several matches%s%s; %s",
                drive, before, rungs.searchedAgain ? foundAgain : "found by search", cost, report.count, recorded,
                moved, several, list[0] ? ": " : "", list, saving);
        }
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (outcome.searchedToo)
        LOG("%s: DEV self-test (drive_fault 0x%X): the search was run as well, and it %s", drive,
            static_cast<unsigned>(drive_resolve::kFaultSearchToo),
            outcome.searchAgrees ? "chose the same address for every line" : "DISAGREES with the verified addresses");
#endif
}

void HookWatch::Restart()
{
    dog_.reset();
    periodStartMs_ = 0;
    periodPossible_ = false;
    everCalled_ = false;
}

bool HookWatch::Pass(uint64_t install, bool possible, bool called, uint64_t nowMs)
{
    if (install != install_)
    {
        Restart();
        install_ = install;
    }
    if (!install)
        return false;
    everCalled_ = everCalled_ || called;
    if (!periodStartMs_)
    {
        periodStartMs_ = nowMs;
        periodPossible_ = true;
    }
    // A period counts only if every pass in it found the call possible. However long a pass was in coming, the
    // time between two passes is one period at most: a stalled worker saw nothing in between.
    periodPossible_ = periodPossible_ && possible;
    if (nowMs - periodStartMs_ < kPeriodMs)
        return false;
    const bool wholePeriod = periodPossible_;
    periodStartMs_ = nowMs;
    periodPossible_ = true;
    // DEV fault injection (drive_fault 0x10; constant false in a player build): the watchdog is told the hook
    // never ran, so its one line can be watched appearing after the possible time it waits for.
    const bool silenced = addrcascade::fault(static_cast<int>(drive_resolve::kFaultHookSilent));
    return dog_.tick(wholePeriod, everCalled_ && !silenced);
}

void SayNeverCalled(const char* drive, const char* what, uint32_t rva, const char* state)
{
    SayHookNeverCalled(drive, what, rva, state,
                       "Nothing is written through a hook that does not run: the aim stays on the stick servo");
}

void SayHookNeverCalled(const char* feature, const char* what, uint32_t rva, const char* state,
                        const char* consequence)
{
    LOG("%s: INSTALLED BUT NEVER CALLED. The hook on %s at +0x%X has stood through %u s in which %s, and the "
        "game has not called it once. Either that address is not the code this game build runs for it, or the "
        "game was not in that state after all. %s",
        feature, what, rva, HookWatch::kSeconds, state, consequence);
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (addrcascade::fault(static_cast<int>(drive_resolve::kFaultHookSilent)))
        LOG("%s: DEV fault injection (drive_fault 0x%X) told that watchdog its hook never ran; the line above says "
            "nothing about this game build", feature, static_cast<unsigned>(addrcascade::fault_mask()));
#endif
}

void HookWatchBoard::Begin(uint32_t install)
{
    if (install == install_)
        return;
    install_ = install;
    for (auto& slot : slots_)
        slot = Slot{};
    summarised_ = false;
    inPlayMs_ = 0;
    lastInPlayAtMs_ = 0;
}

bool HookWatchBoard::Pass(size_t index, const void* target, bool possible, bool called, uint64_t nowMs)
{
    if (index >= kMaxWatches)
        return false;
    Slot& slot = slots_[index];
    // No install of the set: nothing is watched, whatever the caller passes.
    if (!install_)
        target = nullptr;
    slot.installed = target != nullptr;
    const bool fired = slot.watch.Pass(HookWatch::Install(target, install_), target && possible, called, nowMs);
    // A hook that went away is a new install when it comes back: its verdict went with the old one.
    if (!slot.installed)
        slot.fired = false;
    slot.fired = slot.fired || fired;
    return fired;
}

bool HookWatchBoard::SummaryDue(bool inPlay, uint64_t nowMs)
{
    if (!install_ || summarised_)
        return false;
    // The base state's time, counted as HookWatch counts a period: between two passes that both found it, and
    // never more than one period for a gap (a stalled worker saw nothing in between).
    if (inPlay && lastInPlayAtMs_ && nowMs > lastInPlayAtMs_)
        inPlayMs_ += nowMs - lastInPlayAtMs_ < HookWatch::kPeriodMs ? nowMs - lastInPlayAtMs_ : HookWatch::kPeriodMs;
    lastInPlayAtMs_ = inPlay ? nowMs : 0;
    uint32_t installed = 0, waiting = 0;
    for (size_t i = 0; i < kMaxWatches; ++i)
    {
        const Verdict verdict = VerdictOf(i);
        installed += verdict != Verdict::NotInstalled ? 1 : 0;
        waiting += verdict == Verdict::Waiting ? 1 : 0;
    }
    if (!installed || (waiting && inPlayMs_ < kSummaryAfterMs))
        return false;
    summarised_ = true;
    return true;
}

HookWatchBoard::Verdict HookWatchBoard::VerdictOf(size_t index) const
{
    if (index >= kMaxWatches || !slots_[index].installed)
        return Verdict::NotInstalled;
    const Slot& slot = slots_[index];
    if (slot.fired)
        return Verdict::NeverCalled;
    return slot.watch.everCalled() ? Verdict::Called : Verdict::Waiting;
}

void HookWatchBoard::Summary(const char* title, const char* const* names, size_t count, char* out,
                             size_t size) const
{
    if (!out || !size)
        return;
    out[0] = 0;
    if (count > kMaxWatches)
        count = kMaxWatches;
    uint32_t watched = 0, called = 0, dead = 0, waiting = 0;
    for (size_t i = 0; i < count; ++i)
    {
        switch (VerdictOf(i))
        {
        case Verdict::NotInstalled: break;
        case Verdict::Waiting: ++watched; ++waiting; break;
        case Verdict::Called: ++watched; ++called; break;
        case Verdict::NeverCalled: ++watched; ++dead; break;
        }
    }
    size_t used = 0;
    const auto append = [&](const char* text) {
        while (*text && used + 1 < size)
            out[used++] = *text++;
        out[used] = 0;
    };
    const auto appendNumber = [&](uint32_t value) {
        char digits[16];
        std::snprintf(digits, sizeof(digits), "%u", value);
        append(digits);
    };
    const auto list = [&](Verdict which) {
        bool first = true;
        for (size_t i = 0; i < count; ++i)
            if (VerdictOf(i) == which)
            {
                if (!first)
                    append(", ");
                append(names && names[i] ? names[i] : "?");
                first = false;
            }
    };
    append(title ? title : "?");
    append(" hook watch: ");
    appendNumber(watched);
    append(" watched");
    if (watched && called == watched)
    {
        append(", all called");
        return;
    }
    append(": ");
    appendNumber(called);
    append(" called");
    if (dead)
    {
        append("; ");
        appendNumber(dead);
        append(" NEVER CALLED: ");
        list(Verdict::NeverCalled);
    }
    if (waiting)
    {
        append("; ");
        appendNumber(waiting);
        append(" not yet in the state that calls them: ");
        list(Verdict::Waiting);
    }
}
} // namespace drive_cascade
