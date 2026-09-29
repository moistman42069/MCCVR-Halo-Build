#include "../src/common/log.h"
#include "../src/common/virtual_stock_test_profiles.h"
#include "../src/dll/telemetry_recorder.h"

#include <Windows.h>
#include <openxr/openxr.h>
#include "../src/common/virtual_stock_logic.h"
#include "../src/common/two_hand_lab_logic.h"

// File-local rotate for the extracted production solver below (mirrors the
// solver tests): the fixture calls unqualified Rotate, so it must be
// declared before the fixture include. Static: no linkage clash with the
// recorder object in this same binary.
static XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)
{
    const XrVector3f u{q.x, q.y, q.z};
    const auto cross = [](const XrVector3f& a, const XrVector3f& b) {
        return XrVector3f{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
    };
    XrVector3f c1 = cross(u, v);
    c1.x += q.w * v.x;
    c1.y += q.w * v.y;
    c1.z += q.w * v.z;
    const XrVector3f c2 = cross(u, c1);
    return {v.x + 2.0f * c2.x, v.y + 2.0f * c2.y, v.z + 2.0f * c2.z};
}
#include "aim_pose_functions.inl"
#include "../src/dll/two_hand_lab_temporal_runtime.inl"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{
unsigned checks = 0;
unsigned failures = 0;

void Check(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

bool WaitForState(TelemetryRecorderState expected, DWORD timeoutMs = 3000)
{
    const uint64_t deadline = GetTickCount64() + timeoutMs;
    do
    {
        if (Telemetry_GetStatus().state == expected)
            return true;
        Sleep(1);
    } while (GetTickCount64() < deadline);
    return Telemetry_GetStatus().state == expected;
}

std::wstring MakeTestDirectory()
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    wchar_t leaf[96]{};
    _snwprintf_s(leaf, _countof(leaf), _TRUNCATE,
        L"HaloMCCVR-Telemetry-Test-%lu-%llu-\u6d4b\u8bd5",
        GetCurrentProcessId(),
        static_cast<unsigned long long>(GetTickCount64()));
    std::wstring directory = temp;
    directory += leaf;
    Check(CreateDirectoryW(directory.c_str(), nullptr) != FALSE,
        "Telemetry lifecycle test creates an isolated directory");
    directory += L"\\";
    return directory;
}

std::wstring StripTrailingSeparators(std::wstring path)
{
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

std::wstring RecordingDirectoryLeaf(const std::wstring& path)
{
    const std::wstring stripped = StripTrailingSeparators(path);
    const size_t separator = stripped.find_last_of(L"\\/");
    return separator == std::wstring::npos
        ? stripped
        : stripped.substr(separator + 1);
}

std::wstring RecordingDirectoryParent(const std::wstring& path)
{
    const std::wstring stripped = StripTrailingSeparators(path);
    const size_t separator = stripped.find_last_of(L"\\/");
    return separator == std::wstring::npos
        ? std::wstring{}
        : stripped.substr(0, separator);
}

unsigned CountTelemetryFiles(const std::wstring& directory)
{
    const std::wstring pattern =
        directory + L"HaloMCCVR-Telemetry-*.jsonl";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    unsigned count = 0;
    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            ++count;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count;
}

std::wstring FirstTelemetryFile(const std::wstring& directory)
{
    const std::wstring pattern =
        directory + L"HaloMCCVR-Telemetry-*.jsonl";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return {};
    const std::wstring result = directory + data.cFileName;
    FindClose(find);
    return result;
}

std::vector<std::wstring> ListTelemetryFiles(const std::wstring& directory)
{
    const std::wstring pattern =
        directory + L"HaloMCCVR-Telemetry-*.jsonl";
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return files;
    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            files.push_back(directory + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return files;
}

std::string ReadFileBytes(const std::wstring& path);

bool DirectoryContainsBytes(const std::wstring& directory,
    const std::string& needle)
{
    const std::wstring pattern =
        directory + L"HaloMCCVR-Telemetry-*.jsonl";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return false;
    bool found = false;
    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            ReadFileBytes(directory + data.cFileName).find(needle) !=
                std::string::npos)
        {
            found = true;
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

bool WaitForAnalyserLaunchCompletions(
    uint32_t expected, DWORD timeoutMs = 3000)
{
    const uint64_t deadline = GetTickCount64() + timeoutMs;
    do
    {
        if (Telemetry_TestAnalyserLaunchCompletions() >= expected)
            return true;
        Sleep(1);
    } while (GetTickCount64() < deadline);
    return Telemetry_TestAnalyserLaunchCompletions() >= expected;
}

std::string ReadFileBytes(const std::wstring& path);

bool EnvironmentContains(
    const std::vector<wchar_t>& block, const wchar_t* expected)
{
    if (block.empty())
        return false;
    for (const wchar_t* cursor = block.data(); *cursor;)
    {
        if (std::wcscmp(cursor, expected) == 0)
            return true;
        cursor += std::wcslen(cursor) + 1;
    }
    return false;
}

bool CreateDummyAnalyser(const std::wstring& moduleDirectory)
{
    const std::wstring analyserDirectory =
        moduleDirectory + L"TelemetryAnalyser";
    if (!CreateDirectoryW(analyserDirectory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return false;
    }
    const std::wstring script =
        analyserDirectory + L"\\analyse_mccvr_telemetry.py";
    HANDLE file = CreateFileW(
        script.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(file);
    return true;
}

TelemetryFrame EvidenceFrame(uint64_t serial)
{
    TelemetryFrame frame{};
    frame.preparedSerial = serial;
    frame.predictedDisplayTime = 123456789;
    frame.predictedDisplayPeriod = 11111111;
    frame.captureBeginQpc = 100;
    frame.captureEndQpc = 140;
    frame.contactSpaceEpoch = 9;
    frame.contactSpaceChangeAtNs = 120000000;
    frame.activeTitle = 3;
    frame.sessionState = 5;
    frame.locatedViewCount = 2;
    frame.shouldRender = true;
    frame.upcomingViewsValid = true;
    frame.focused = true;
    frame.stereoEnabled = true;
    frame.menuOpen = false;
    frame.semanticPrimaryValid = true;
    frame.semanticPrimaryAim.position = {1.0f, 2.0f, 3.0f};
    frame.semanticPrimaryAim.orientation = {0.1f, 0.2f, 0.3f, 0.9f};
    frame.semanticPrimaryForward = {4.0f, 5.0f, 6.0f};
    frame.semanticSupportValid = true;
    frame.semanticSupportAim.position = {7.0f, 8.0f, 9.0f};
    frame.supportEndpointValid = true;
    frame.supportEndpoint = {10.0f, 11.0f, 12.0f};
    frame.supportEndpointUsedGrip = true;
    frame.semanticPrimaryGripPositionValid = true;
    frame.semanticPrimaryGripPosition = {1.25f, -2.5f, 3.75f};
    frame.headSampleValid = true;
    frame.stockHeadValid = true;
    frame.semanticHmd.position = {13.0f, 14.0f, 15.0f};
    frame.views[0].pose.position = {16.0f, 17.0f, 18.0f};
    frame.views[1].pose.position = {19.0f, 20.0f, 21.0f};
    frame.semanticPrimaryVelocityValid = true;
    frame.semanticPrimaryVelocity = {NAN, 2.0f, 3.0f};
    frame.pad.valid = true;
    frame.pad.moveX = 0.5f;
    frame.pad.a = true;
    frame.pad.weaponButtons = 17;
    frame.pad.weaponPulseUntilMs = 7654321;
    frame.pad.weaponGeneration = 3;
    frame.testProfileId = static_cast<uint8_t>(
        VirtualStockTestProfile::E_HybridMaxSeat);
    frame.testProfileCustom = false;
    frame.effectiveSettings.virtualStockEnabled = true;
    frame.effectiveSettings.twoHandToggle = false;
    frame.effectiveSettings.virtualStockRearReference = 3;
    frame.effectiveSettings.hybridSeatFullM = 0.6f;
    frame.effectiveSettings.hybridSeatReleaseM = 0.8f;
    frame.effectiveSettings.horizontalReleaseEnabled = false;
    frame.effectiveSettings.horizontalReleaseFullM = 0.27f;
    frame.effectiveSettings.horizontalReleaseReleaseM = 0.425f;
    frame.effectiveSettings.inverseNeckEnabled = true;
    frame.effectiveSettings.inverseNeckStrength = 0.5f;
    frame.effectiveSettings.inverseNeckForwardM = 0.1f;
    frame.effectiveSettings.inverseNeckUpM = 0.04f;
    frame.effectiveSettings.inverseNeckLateralM = 0.0f;
    frame.effectiveSettings.supportEndpointUsedGrip = true;
    frame.effectiveSettings.aimStabilization = 0.48f;
    frame.effectiveSettings.crosshairDistanceM = 41.0f;
    frame.effectiveSettings.crosshairSizeDeg = 10.10f;
    frame.effectiveSettings.crosshair = true;
    frame.effectiveSettings.killReticle = true;
    frame.aimTrace.path = AimSolverPath::Hybrid;
    frame.aimTrace.cAttempted = true;
    frame.aimTrace.cValid = true;
    frame.aimTrace.seatAttempted = true;
    frame.aimTrace.seatValid = true;
    frame.aimTrace.wNaturalValid = true;
    frame.aimTrace.wNatural = 0.375f;
    frame.aimTrace.wAfterDiagnosticValid = true;
    frame.aimTrace.wAfterDiagnostic = 0.375f;
    frame.aimTrace.wEffectiveValid = true;
    frame.aimTrace.wEffective = 0.375f;
    frame.aimTrace.forceStockEligible = true;
    frame.aimTrace.releaseGeometryValid = true;
    frame.aimTrace.rearToStockTargetDistanceM = 0.25f;
    frame.aimTrace.inverseNeckAttempted = true;
    frame.aimTrace.inverseNeckValid = true;
    frame.aimTrace.inverseNeckNeutralValid = true;
    frame.aimTrace.inverseNeckStrength = 0.5f;
    frame.aimTrace.inverseNeckNeutralOrientation = {0.0f, 0.2f, 0.0f, 0.9797959f};
    frame.aimTrace.inverseNeckNeutralCaptureSerial = 19;
    frame.aimTrace.inverseNeckNeutralCaptureContactSpaceEpoch = 9;
    frame.aimTrace.inverseNeckCurrentOffset = {-0.02f, 0.04f, -0.098f};
    frame.aimTrace.inverseNeckNeutralOffset = {0.0f, 0.04f, -0.1f};
    frame.aimTrace.inverseNeckPredictedOrbit = {-0.02f, 0.0f, 0.002f};
    frame.aimTrace.inverseNeckAppliedCorrection = {-0.01f, 0.0f, 0.001f};
    frame.aimTrace.inverseNeckCorrectedHeadPosition = {13.01f, 14.0f, 14.999f};
    frame.aimTrace.rawHeadTargetValid = true;
    frame.aimTrace.rawHeadTarget = {13.0f, 13.8f, 15.0f};
    frame.aimTrace.correctedHeadTargetValid = true;
    frame.aimTrace.correctedHeadTarget = {13.01f, 13.8f, 14.999f};
    frame.canonicalAim.valid = true;
    frame.canonicalAim.forward = {0.0f, 0.0f, -1.0f};
    frame.transitionStockMode = 1;
    frame.transitionStockModeValid = true;
    frame.transitionActive = true;
    frame.transitionPhase = 1;
    frame.transitionEdgeKind = 1;
    frame.transitionAnchorSource = 1;
    frame.transitionLiveCalibratedForwardValid = true;
    frame.transitionLiveCalibratedForward = {0.0f, 0.0f, -1.0f};
    frame.transitionPresentedForwardValid = true;
    frame.transitionPresentedForward = {0.1f, 0.0f, -0.99498743f};
    frame.transitionOneHandAnchorValid = true;
    frame.transitionOneHandAnchorForward = {0.1f, 0.0f, -0.99498743f};
    frame.transitionInitialCorrectionDeg = 12.5f;
    frame.transitionRemainingCorrectionDeg = 4.25f;
    frame.transitionElapsedMs = 33.3f;
    frame.transitionAdvanceCount = 7;
    frame.transitionLastPreparedSerial = serial;
    frame.transitionAppliedSerial = serial;
    // Free two-hand (VS-OFF) product offhand directional authority: the frozen
    // assembly value recorded beside the input-smoothing family.
    frame.twoHandOffhandInfluence = 0.5f;
    // Two-Hand Lab family: an enabled GG-100 Soft-authority Constant-damping
    // frame, coherent by construction (effective == requested at full
    // confidence, presented valid while temporal is active).
    frame.twoHandLabEnabled = true;
    frame.twoHandLabAnchorRequested = 4;
    frame.twoHandLabAnchorResolved = 4;
    frame.twoHandLabAnchorFallback = 0;
    frame.twoHandLabOffhandInfluence = 1.0f;
    frame.twoHandLabAgreementMode = 1;
    frame.twoHandLabAgreement = 0.958f;
    frame.twoHandLabAgreementConfidence = 1.0f;
    frame.twoHandLabEffectiveInfluence = 1.0f;
    frame.twoHandLabTemporalMode = 2;
    frame.twoHandLabPrimaryPivotValid = true;
    frame.twoHandLabPrimaryPivot = {0.0f, 0.0f, 0.0f};
    frame.twoHandLabSupportPivotValid = true;
    frame.twoHandLabSupportPivot = {0.5f, 0.0f, -1.0f};
    frame.twoHandLabStatelessDirectionValid = true;
    frame.twoHandLabStatelessDirection = {0.0f, 0.0f, -1.0f};
    frame.twoHandLabPresentedDirectionValid = true;
    frame.twoHandLabPresentedDirection = {0.05f, 0.0f, -0.998749f};
    frame.twoHandLabTemporalActive = true;
    frame.twoHandLabTemporalErrorDeg = 2.5f;
    // Shots-vs-reticle tranche: a coherent valid observation. The reticle
    // serial (41) deliberately differs from both fixture prepared serials
    // (0, 42): capture precedes the reticle publish, so the validator must
    // accept the lag and never assert equality with prepared_serial.
    frame.presentedAimValid = true;
    frame.presentedAimForward = {0.0f, 0.0f, -1.0f};
    frame.reticlePresentedValid = true;
    frame.reticlePresentedSerial = 41;
    frame.reticlePresentedSampleMs = 1234567;
    frame.reticlePresentedSupportEpoch = 5;
    frame.reticlePresentedSupportTrusted = true;
    frame.reticlePresentedOrientation = {0.0f, 0.0f, 0.0f, 1.0f};
    frame.reticlePresentedPosition = {0.5f, -0.25f, 4.0f};
    frame.engineAimValid = true;
    frame.engineAimSource = 1;
    frame.engineAimForward = {1.0f, 0.0f, 0.0f};
    frame.engineAimSerial = 0;
    frame.engineAimSampleMs = 0;
    frame.engineAimPitchValid = false;
    frame.engineAimPitchDeg = 0.0f;
    frame.engineCameraBaseValid = true;
    frame.engineCameraBasePosition = {10.0f, 20.0f, 30.0f};
    frame.engineCameraEyeValid = true;
    frame.engineCameraEyePosition = {11.0f, 21.0f, 31.0f};
    frame.engineCameraSource = 1;
    frame.engineCameraSerial = 0;
    frame.worldScaleValid = true;
    frame.worldScale = 1.0f / 3.048f;
    frame.dualActive = true;
    // Persistent grip frozen solve-time provenance. The fixture frame is a
    // PG-on, readable, engaged and trusted solve whose stamped solve serial
    // lags the frame's prepared serial by one (serial 0 can only ever carry
    // the initial published serial 0), mirroring the real capture ordering.
    frame.persistentSupportGripConfigured = true;
    frame.persistentSupportGripApplicable = true;
    frame.supportRelationshipReadable = true;
    frame.supportRelationshipEngaged = true;
    frame.supportEpoch = 5;
    frame.supportSolveTrusted = true;
    frame.supportForceOneHand = false;
    frame.supportSolveSerial = serial == 0 ? 0 : serial - 1;
    frame.cfVsOff.profileId = static_cast<uint8_t>(kVsOffControlProfile);
    frame.cfFixedHead.profileId = static_cast<uint8_t>(kFixedHeadControlProfile);
    frame.cfFixedHead.path = static_cast<uint8_t>(AimSolverPath::FixedStock);
    frame.cfFixedHead.requestedTarget =
        static_cast<uint8_t>(AimStockTarget::Head);
    frame.cfFixedHead.actualTarget =
        static_cast<uint8_t>(AimStockTarget::Head);
    frame.cfFixedHead.fixedProximityEnabled = true;
    frame.cfFixedHead.fixedConfiguredStrength = 0.75f;
    frame.cfFixedHead.fixedDirectionValid = true;
    frame.cfFixedHead.orientationRebuildAttempted = true;
    frame.cfFixedHead.orientationRebuildSucceeded = true;
    frame.cfFixedShoulder.profileId = static_cast<uint8_t>(
        kFixedShoulderControlProfile);
    return frame;
}

void TestRing()
{
    static_assert(std::is_trivially_copyable_v<TelemetryFrame>);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);

    Telemetry_TestResetRing();
    for (uint64_t i = 0; i < kTelemetryQueueUsableCapacity; ++i)
    {
        TelemetryFrame frame{};
        frame.preparedSerial = i;
        Check(Telemetry_TestPushRing(frame),
            "SPSC ring accepts every usable slot");
    }
    TelemetryFrame overflow{};
    overflow.preparedSerial = 999999;
    Check(!Telemetry_TestPushRing(overflow),
        "A full SPSC ring drops the new record immediately");
    Check(Telemetry_TestRingDrops() == 1,
        "Queue-full drop counter increments exactly once");

    TelemetryFrame popped{};
    constexpr uint64_t kPoppedBeforeWrap = 2048;
    for (uint64_t i = 0; i < kPoppedBeforeWrap; ++i)
    {
        Check(Telemetry_TestPopRing(popped) && popped.preparedSerial == i,
            "SPSC ring preserves FIFO order before wraparound");
    }
    for (uint64_t i = 0; i < kPoppedBeforeWrap; ++i)
    {
        TelemetryFrame frame{};
        frame.preparedSerial = kTelemetryQueueUsableCapacity + i;
        Check(Telemetry_TestPushRing(frame),
            "SPSC ring accepts wrapped producer writes");
    }
    for (uint64_t i = kPoppedBeforeWrap;
         i < kTelemetryQueueUsableCapacity + kPoppedBeforeWrap; ++i)
    {
        Check(Telemetry_TestPopRing(popped) && popped.preparedSerial == i,
            "SPSC ring preserves FIFO order across wraparound");
    }
    Check(!Telemetry_TestPopRing(popped),
        "SPSC ring is empty after all wrapped records are consumed");
}

void TestConcurrentRing()
{
    Telemetry_TestResetRing();
    constexpr uint64_t kFrameCount = 20000;
    std::atomic<bool> mismatch{false};
    std::thread producer([&]() {
        for (uint64_t serial = 0; serial < kFrameCount; ++serial)
        {
            TelemetryFrame frame{};
            frame.preparedSerial = serial;
            frame.predictedDisplayTime = static_cast<int64_t>(serial * 17);
            frame.semanticPrimaryAim.position.x =
                static_cast<float>(serial % 1024);
            while (!Telemetry_TestPushRing(frame))
                Sleep(0);
        }
    });
    std::thread consumer([&]() {
        for (uint64_t expected = 0; expected < kFrameCount;)
        {
            TelemetryFrame frame{};
            if (!Telemetry_TestPopRing(frame))
            {
                Sleep(0);
                continue;
            }
            if (frame.preparedSerial != expected ||
                frame.predictedDisplayTime != static_cast<int64_t>(expected * 17) ||
                frame.semanticPrimaryAim.position.x !=
                    static_cast<float>(expected % 1024))
            {
                mismatch.store(true, std::memory_order_release);
            }
            ++expected;
        }
    });
    producer.join();
    consumer.join();
    Check(!mismatch.load(std::memory_order_acquire),
        "Concurrent SPSC ring preserves order and complete frame payloads across wraps");
    TelemetryFrame extra{};
    Check(!Telemetry_TestPopRing(extra),
        "Concurrent SPSC ring is empty after the consumer catches up");
}

void TestSerializerHelpers()
{
    char escaped[256]{};
    size_t escapedBytes = 0;
    const char source[] = "quote\" slash\\ newline\n tab\t control\x01";
    Check(Telemetry_TestEscapeJson(
            source, escaped, sizeof(escaped), escapedBytes),
        "JSON string escaping succeeds");
    Check(std::strcmp(
            escaped,
            "\"quote\\\" slash\\\\ newline\\n tab\\t control\\u0001\"") == 0,
        "JSON string escaping covers quotes, slashes, whitespace and controls");

    std::setlocale(LC_NUMERIC, "German_Germany.1252");
    char serialized[32768]{};
    size_t serializedBytes = 0;
    const TelemetryFrame frame = EvidenceFrame(0);
    Check(Telemetry_TestSerializeFrame(
            frame, serialized, sizeof(serialized), serializedBytes),
        "Frame serializer produces a bounded JSON object");
    Check(std::strstr(serialized, "\"moveX\":0.5") != nullptr,
        "Float serializer remains locale-independent");
    Check(std::strstr(serialized, "\"schema_version\":2") != nullptr &&
            std::strstr(serialized,
                "\"test_profile_name\":\"E_HybridMaxSeat\"") != nullptr,
        "Frame serializer emits schema 2 and the selected registry name");
    Check(std::strstr(serialized,
                "\"cf_vs_off\":{\"profile_id\":1,\"profile_name\":\"A_VsOffControl\"") != nullptr &&
            std::strstr(serialized,
                "\"cf_fixed_head\":{\"profile_id\":2,\"profile_name\":\"B_FixedHeadControl\"") != nullptr &&
            std::strstr(serialized,
                "\"cf_fixed_shoulder\":{\"profile_id\":3,\"profile_name\":\"C_FixedShoulderControl\"") != nullptr,
        "Frame serializer derives all control names from their registry IDs");
    Check(std::strstr(serialized,
            "\"semantic_primary_linear_velocity\":[null,2,3]") != nullptr,
        "Non-finite floats serialize as JSON null");
    Check(std::strstr(serialized, "\"two_hand_toggle\":false") != nullptr,
        "Effective settings serialize Toggle versus Hold acquisition mode");
    Check(std::strstr(serialized,
            "\"two_hand_offhand_influence\":0.5") != nullptr,
        "The free two-hand offhand authority serializes as its frozen value");
    Check(std::strstr(serialized,
                "\"horizontal_release_enabled\":false") != nullptr &&
            std::strstr(serialized,
                "\"horizontal_release_full_m\":0.27") != nullptr &&
            std::strstr(serialized,
                "\"horizontal_release_release_m\":0.425") != nullptr,
        "Effective settings serialize the profile-only horizontal release policy");
    Check(std::strstr(serialized,
                "\"hybrid_inverse_neck_enabled\":true") != nullptr &&
            std::strstr(serialized,
                "\"hybrid_inverse_neck_strength\":0.5") != nullptr &&
            std::strstr(serialized,
                "\"hybrid_inverse_neck_forward_m\":") != nullptr &&
            std::strstr(serialized,
                "\"hybrid_inverse_neck_up_m\":") != nullptr &&
            std::strstr(serialized,
                "\"hybrid_inverse_neck_lateral_m\":0") != nullptr,
        "Static inverse-neck configuration serializes in effective settings");
    Check(std::strstr(serialized,
                "\"inverse_neck_neutral_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"inverse_neck_neutral_capture_serial\":19") != nullptr &&
            std::strstr(serialized,
                "\"inverse_neck_neutral_capture_contact_space_epoch\":9") != nullptr &&
            std::strstr(serialized,
                "\"inverse_neck_neutral_orientation\":[0,") != nullptr,
        "Runtime inverse-neck neutral calibration serializes validity-gated in aim trace");
    const char* topSettings = std::strstr(serialized, "\"effective_settings\":{");
    const char* topSettingsEnd = topSettings ? std::strchr(topSettings, '}') : nullptr;
    const char* runtimeNeutral = std::strstr(serialized,
        "\"inverse_neck_neutral_orientation\"");
    Check(topSettings != nullptr && topSettingsEnd != nullptr &&
            runtimeNeutral != nullptr && runtimeNeutral > topSettingsEnd,
        "Runtime Q0, capture serial and capture epoch are excluded from static effective settings identity");
    Check(std::strstr(serialized, "\"w_natural_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"w_after_diagnostic\":0.375") != nullptr &&
            std::strstr(serialized, "\"w_effective_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"horizontal_release_attempted\":false") != nullptr,
        "Hybrid authority stages and non-applicable horizontal release serialize explicitly");
    Check(std::strstr(serialized,
            "\"orientation_rebuild_succeeded\":true") != nullptr,
        "Compact fixed controls serialize their decisive solver cause fields");

    Check(std::strstr(serialized,
                "\"two_hand_lab_enabled\":true") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_anchor_requested\":4") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_anchor_resolved\":4") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_anchor_fallback\":0") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_agreement_mode\":1") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_temporal_mode\":2") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_primary_pivot_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_support_pivot_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_stateless_direction_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_presented_direction_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_temporal_active\":true") != nullptr,
        "Two-Hand Lab identity, validity and temporal state serialize "
        "explicitly");
    Check(std::strstr(serialized,
                "\"two_hand_lab_offhand_influence\":1") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_agreement_confidence\":1") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_effective_influence\":1") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_temporal_error_deg\":2.5") != nullptr,
        "Two-Hand Lab scalars serialize explicitly");
    Check(std::strstr(serialized,
                "\"two_hand_lab_support_pivot\":[0.5,0,-1]") != nullptr,
        "Two-Hand Lab support pivot serializes explicitly");
    Check(std::strstr(serialized,
                "\"two_hand_lab_stateless_direction\":[0,0,-1]") != nullptr,
        "Two-Hand Lab stateless direction serializes explicitly");

    // VS-OFF Two-Hand Transition plus Pavlov-inspired input smoothing: the
    // whole new frame tranche must survive the fixed transport field-for-field
    // and then serialize with the analyser's frozen names.
    Telemetry_TestResetRing();
    {
        TelemetryFrame smoothing = EvidenceFrame(77);
        smoothing.twoHandTransitionSmoothingConfigured = true;
        smoothing.twoHandTransitionActive = true;
        smoothing.twoHandSmoothingConfigured = true;
        smoothing.twoHandSmoothingApplied = true;
        smoothing.twoHandSmoothingStrength = 12.5f;
        smoothing.twoHandSmoothingAlpha = 0.25f;
        smoothing.twoHandSmoothingPrimaryOrientationErrorDeg = 3.5f;
        smoothing.twoHandSmoothingPrimaryPositionErrorM = 0.125f;
        smoothing.twoHandSmoothingSupportPositionErrorM = 0.0625f;
        smoothing.twoHandOffhandInfluence = 0.75f;
        Check(Telemetry_TestPushRing(smoothing),
            "A frame carrying the new smoothing tranche is admitted to the "
            "fixed transport");
        TelemetryFrame poppedSmoothing{};
        Check(Telemetry_TestPopRing(poppedSmoothing) &&
                poppedSmoothing.twoHandTransitionSmoothingConfigured &&
                poppedSmoothing.twoHandTransitionActive &&
                poppedSmoothing.twoHandSmoothingConfigured &&
                poppedSmoothing.twoHandSmoothingApplied &&
                poppedSmoothing.twoHandSmoothingStrength == 12.5f &&
                poppedSmoothing.twoHandOffhandInfluence == 0.75f &&
                poppedSmoothing.twoHandSmoothingAlpha == 0.25f &&
                poppedSmoothing.twoHandSmoothingPrimaryOrientationErrorDeg ==
                    3.5f &&
                poppedSmoothing.twoHandSmoothingPrimaryPositionErrorM == 0.125f &&
                poppedSmoothing.twoHandSmoothingSupportPositionErrorM == 0.0625f,
            "The transport preserves every new smoothing tranche field");
        char smoothingText[32768]{};
        size_t smoothingBytes = 0;
        Check(Telemetry_TestSerializeFrame(poppedSmoothing, smoothingText,
                    sizeof(smoothingText), smoothingBytes) &&
                std::strstr(smoothingText,
                    "\"two_hand_transition_smoothing_configured\":true") !=
                    nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_transition_active\":true") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_configured\":true") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_applied\":true") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_offhand_influence\":0.75") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_strength\":12.5") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_mix\":0.5") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_alpha\":0.25") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_primary_orientation_error_deg\":3.5") !=
                    nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_primary_position_error_m\":0.125") !=
                    nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_support_position_error_m\":0.0625") !=
                    nullptr,
            "The new smoothing tranche serializes explicitly");

        // Feature-off shape: the wiring leaves the tranche at its frozen
        // false/false/false/0 defaults, and that absence serializes as itself.
        TelemetryFrame smoothingOff = EvidenceFrame(78);
        Check(Telemetry_TestSerializeFrame(smoothingOff, smoothingText,
                    sizeof(smoothingText), smoothingBytes) &&
                std::strstr(smoothingText,
                    "\"two_hand_transition_smoothing_configured\":false") !=
                    nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_transition_active\":false") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_configured\":false") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_applied\":false") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_strength\":0") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_mix\":0") != nullptr &&
                std::strstr(smoothingText,
                    "\"two_hand_smoothing_alpha\":0") != nullptr,
            "A frame without the smoothing tranche serializes the frozen "
            "false/zero defaults, never a stale receipt");
    }

    TelemetryFrame disabledLab = frame;
    disabledLab.twoHandLabEnabled = false;
    disabledLab.twoHandLabAnchorRequested = 0;
    disabledLab.twoHandLabAnchorResolved = 0;
    disabledLab.twoHandLabAnchorFallback = 0;
    disabledLab.twoHandLabOffhandInfluence = 0.0f;
    disabledLab.twoHandLabAgreementMode = 0;
    disabledLab.twoHandLabAgreement = 0.0f;
    disabledLab.twoHandLabAgreementConfidence = 0.0f;
    disabledLab.twoHandLabEffectiveInfluence = 0.0f;
    disabledLab.twoHandLabTemporalMode = 0;
    disabledLab.twoHandLabPrimaryPivotValid = false;
    disabledLab.twoHandLabPrimaryPivot = {};
    disabledLab.twoHandLabSupportPivotValid = false;
    disabledLab.twoHandLabSupportPivot = {};
    disabledLab.twoHandLabStatelessDirectionValid = false;
    disabledLab.twoHandLabStatelessDirection = {};
    disabledLab.twoHandLabPresentedDirectionValid = false;
    disabledLab.twoHandLabPresentedDirection = {};
    disabledLab.twoHandLabTemporalActive = false;
    disabledLab.twoHandLabTemporalErrorDeg = 0.0f;
    Check(Telemetry_TestSerializeFrame(
            disabledLab, serialized, sizeof(serialized), serializedBytes) &&
            std::strstr(serialized,
                "\"two_hand_lab_enabled\":false") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_temporal_mode\":0") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_presented_direction_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_stateless_direction_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_primary_pivot_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_support_pivot_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"two_hand_lab_temporal_active\":false") != nullptr,
        "A Lab-inactive frame keeps the family defaults observationally "
        "unavailable rather than stale");

    Check(std::strstr(serialized,
                "\"semantic_primary_grip_position_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"semantic_primary_grip_position\":[1.25,-2.5,3.75]") != nullptr,
        "Semantic primary grip validity and position serialize explicitly");

    TelemetryFrame invalidPrimaryGrip = frame;
    invalidPrimaryGrip.semanticPrimaryGripPositionValid = false;
    invalidPrimaryGrip.semanticPrimaryGripPosition = {};
    Check(Telemetry_TestSerializeFrame(
            invalidPrimaryGrip, serialized, sizeof(serialized), serializedBytes) &&
            std::strstr(serialized,
                "\"semantic_primary_grip_position_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"semantic_primary_grip_position\":[0,0,0]") != nullptr,
        "Invalid semantic primary grip keeps its numeric default observationally unavailable");
    TelemetryFrame invalidNeutral = frame;
    invalidNeutral.aimTrace.inverseNeckValid = false;
    invalidNeutral.aimTrace.inverseNeckNeutralValid = false;
    invalidNeutral.aimTrace.inverseNeckNeutralOrientation = {};
    invalidNeutral.aimTrace.inverseNeckNeutralCaptureSerial = 0;
    invalidNeutral.aimTrace.inverseNeckNeutralCaptureContactSpaceEpoch = 0;
    invalidNeutral.aimTrace.rawHeadTargetValid = false;
    invalidNeutral.aimTrace.correctedHeadTargetValid = false;
    Check(Telemetry_TestSerializeFrame(
            invalidNeutral, serialized, sizeof(serialized), serializedBytes) &&
            std::strstr(serialized,
                "\"inverse_neck_neutral_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"inverse_neck_neutral_orientation\"") == nullptr &&
            std::strstr(serialized,
                "\"inverse_neck_neutral_capture_serial\"") == nullptr &&
            std::strstr(serialized,
                "\"inverse_neck_neutral_capture_contact_space_epoch\"") == nullptr &&
            std::strstr(serialized,
                "\"inverse_neck_current_offset\"") == nullptr &&
            std::strstr(serialized,
                "\"inverse_neck_applied_correction\"") == nullptr &&
            std::strstr(serialized, "\"raw_head_target\"") == nullptr &&
            std::strstr(serialized, "\"corrected_head_target\"") == nullptr,
        "Unavailable inverse-neck runtime values are omitted rather than encoded as default calibration or geometry");
    std::setlocale(LC_NUMERIC, "C");
}

void TestAnalyserLaunchHelpers()
{
    Check(Telemetry_TestQuoteWindowsArgument(L"") == L"\"\"" &&
            Telemetry_TestQuoteWindowsArgument(
                L"C:\\Program Files\\Python\\") ==
                L"\"C:\\Program Files\\Python\\\\\"" &&
            Telemetry_TestQuoteWindowsArgument(L"a\"b") ==
                L"\"a\\\"b\"",
        "Windows argument quoting handles empty values, spaces, quotes and trailing backslashes");

    constexpr wchar_t inheritedEnvironment[] =
        L"Path=C:\\Windows\0"
        L"PYTHONUTF8=0\0"
        L"Mixed=preserved value\0"
        L"pythonioencoding=cp1252\0\0";
    const std::vector<wchar_t> environment =
        Telemetry_TestBuildUtf8Environment(inheritedEnvironment);
    Check(EnvironmentContains(environment, L"Path=C:\\Windows") &&
            EnvironmentContains(environment, L"Mixed=preserved value") &&
            EnvironmentContains(environment, L"PYTHONUTF8=1") &&
            EnvironmentContains(environment, L"PYTHONIOENCODING=utf-8") &&
            !EnvironmentContains(environment, L"PYTHONUTF8=0") &&
            !EnvironmentContains(environment, L"pythonioencoding=cp1252") &&
            environment.size() >= 2 && environment[environment.size() - 1] == L'\0' &&
            environment[environment.size() - 2] == L'\0',
        "Analyser environment preserves inherited entries and replaces UTF-8 overrides in a double-NUL block");

    TelemetryAnalyserLaunchPlanTest pythonPlan{};
    Check(Telemetry_TestBuildAnalyserLaunchPlan(
              L"C:\\VR Mods\\\u6d4b\u8bd5",
              L"D:\\Telemetry Runs\\Halo Test.jsonl",
              L"C:\\Program Files\\Python Test\\python.exe",
              false, pythonPlan) &&
            pythonPlan.scriptPath ==
                L"C:\\VR Mods\\\u6d4b\u8bd5\\TelemetryAnalyser\\analyse_mccvr_telemetry.py" &&
            pythonPlan.recordingPath ==
                L"D:\\Telemetry Runs\\Halo Test.jsonl" &&
            pythonPlan.commandLine.find(
                L"\"D:\\Telemetry Runs\\Halo Test.jsonl\"") !=
                std::wstring::npos &&
            pythonPlan.commandLine.find(L"\"--report\"") ==
                std::wstring::npos &&
            pythonPlan.commandLine.find(L"\"--json-out\"") ==
                std::wstring::npos &&
            pythonPlan.commandLine.find(L"_analysis") ==
                std::wstring::npos &&
            pythonPlan.commandLine.find(L"combined") == std::wstring::npos,
        "Automatic launch plan requests the canonical sidecars with the closed recording only");

    TelemetryAnalyserLaunchPlanTest pyPlan{};
    Check(Telemetry_TestBuildAnalyserLaunchPlan(
              L"C:\\VR Mods", L"D:\\Run.jsonl",
              L"C:\\Windows\\py.exe", true, pyPlan) &&
            pyPlan.commandLine.find(
                L"\"C:\\Windows\\py.exe\" \"-3\" ") == 0,
        "py.exe fallback launch plan selects Python 3 before the analyser script");
}

void TestLifecycleAndAccounting(const std::wstring& outputPath)
{
    const std::wstring directory = MakeTestDirectory();
    Telemetry_TestResetAnalyserLauncher();
    Telemetry_TestSetAnalyserModuleDirectory(directory.c_str());
    const std::wstring logPath = directory + L"HaloMCCVR.log";
    const std::wstring recordingDirectory = directory + L"Telemetry Recordings\\";
    LogInit(logPath.c_str());
    Check(Telemetry_Init(), "Telemetry worker initializes after LogInit");
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Telemetry starts Idle");
    const DWORD recordingDirectoryAttributes =
        GetFileAttributesW(recordingDirectory.c_str());
    Check(recordingDirectoryAttributes != INVALID_FILE_ATTRIBUTES &&
            (recordingDirectoryAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
        "Telemetry initialization creates the canonical Telemetry Recordings subfolder beside the log");
    // Runtime storage is exactly <mod folder>\Telemetry Recordings\: a direct
    // child of the DLL/log directory with no versioned toolkit parent. The
    // legacy name is assembled from adjacent literals so repository-wide
    // searches keep proving production code no longer references it.
    const std::wstring legacyToolkitDirectory =
        directory + L"MCCVR_Telemetry_Analyser_" L"Toolkit_v5.2";
    Check(GetFileAttributesW(legacyToolkitDirectory.c_str()) ==
            INVALID_FILE_ATTRIBUTES,
        "Recorder initialization does not create the versioned toolkit parent");
    const std::wstring observedRecordingDirectory =
        Telemetry_TestRecordingDirectoryPath();
    Check(observedRecordingDirectory == recordingDirectory,
        "Recorder-reported recording directory matches the directory used for session files");
    Check(RecordingDirectoryLeaf(observedRecordingDirectory) ==
                L"Telemetry Recordings" &&
            RecordingDirectoryParent(observedRecordingDirectory) ==
                StripTrailingSeparators(directory),
        "Recorder-reported recording directory is the direct child 'Telemetry Recordings' of the mod folder");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Idle transitions through Starting to Recording");
    TelemetryFrame frame = EvidenceFrame(0);
    Check(Telemetry_BeginFrame(0),
        "Prepared serial zero is accepted as an ordinary first serial");
    Telemetry_PublishFrame(frame);
    Check(!Telemetry_BeginFrame(0),
        "Prepared serial zero duplicates are suppressed with explicit state");
    frame.preparedSerial = 42;
    frame.transitionLastPreparedSerial = 42;
    frame.transitionAppliedSerial = 42;
    // The persistent-grip solve serial is stamped from the last published
    // prepared serial at capture, so it lags this frame's serial by one.
    frame.supportSolveSerial = 41;
    // Two-Hand Smoothing strength receipt: the fixture must carry a live
    // non-zero strength/mix shape so the JSONL validator checks the slider
    // family's ranges, mix = strength/25 and configured agreement end to end.
    frame.twoHandTransitionSmoothingConfigured = true;
    frame.twoHandSmoothingConfigured = true;
    frame.twoHandSmoothingApplied = true;
    frame.twoHandSmoothingStrength = 12.5f;
    frame.twoHandSmoothingAlpha = 0.25f;
    frame.twoHandSmoothingPrimaryOrientationErrorDeg = 3.5f;
    frame.twoHandSmoothingPrimaryPositionErrorM = 0.125f;
    frame.twoHandSmoothingSupportPositionErrorM = 0.0625f;
    // Free two-hand offhand authority receipt: the fixture carries a live
    // non-zero value so the JSONL validator exercises its finite [0,1] range.
    frame.twoHandOffhandInfluence = 0.75f;
    Check(Telemetry_BeginFrame(42),
        "A distinct non-zero serial is accepted");
    Telemetry_PublishFrame(frame);
    Check(!Telemetry_BeginFrame(42),
        "A non-zero duplicate serial is suppressed");
    TelemetryStatusSnapshot status = Telemetry_GetStatus();
    Check(status.producerCalls == 4 &&
            status.duplicateSerialSuppressed == 2 &&
            status.enqueued == 2 && status.droppedQueueFull == 0,
        "Producer accounting identity holds before clean Stop");

    // T-2 sparse shot evidence: a shot published in this recording session
    // must be serialized into the same file and accounted exactly, so the
    // exported fixture proves the recorder-side shot contract end to end.
    TelemetryShotPayload fixtureShot{};
    fixtureShot.origin[0] = 11.5f;
    fixtureShot.origin[1] = -0.25f;
    fixtureShot.origin[2] = 4.0f;
    fixtureShot.direction[0] = 0.0f;
    fixtureShot.direction[1] = 0.0f;
    fixtureShot.direction[2] = -1.0f;
    fixtureShot.engineAim[0] = 0.5f;
    fixtureShot.engineAim[1] = 0.25f;
    fixtureShot.engineAim[2] = -0.75f;
    fixtureShot.reticleDirection[0] = 0.125f;
    fixtureShot.reticleDirection[1] = 0.0f;
    fixtureShot.reticleDirection[2] = -0.9921568f;
    fixtureShot.slot = 0;
    fixtureShot.barrel = 1;
    fixtureShot.flags = 0x0067u;
    fixtureShot.engineAimSource = 1; // Halo 3 shared g_aimFwd
    const uint64_t fixtureShotSequence = Telemetry_PublishShotEvent(
        static_cast<uint8_t>(WeaponOrderEventStatus::Success), 1, 3, 42, 7,
        0x50, fixtureShot);
    Check(fixtureShotSequence != 0,
        "A shot event publishes inside a real recording session");

    // ODST-shaped shot: the firing-origin producer has no direction of its own
    // and no slot/barrel/weapon context, so the event is direction-absent with
    // the in-memory indexes mapped to the wire -1 and no engine aim snapshot
    // (source 0 with an exact zero vector). The Python fixture validator
    // enforces this shape end to end.
    TelemetryShotPayload fixtureOdstShot{};
    fixtureOdstShot.origin[0] = 3.25f;
    fixtureOdstShot.origin[1] = 1.5f;
    fixtureOdstShot.origin[2] = -2.0f;
    fixtureOdstShot.direction[0] = 0.0f;
    fixtureOdstShot.direction[1] = 0.0f;
    fixtureOdstShot.direction[2] = 0.0f;
    fixtureOdstShot.engineAimSource = 0;
    fixtureOdstShot.slot = kTelemetryShotIndexUnknown;
    fixtureOdstShot.barrel = kTelemetryShotIndexUnknown;
    fixtureOdstShot.flags = 0x0002u; // substituted, no aim-authority flags
    const uint64_t fixtureOdstShotSequence = Telemetry_PublishShotEvent(
        static_cast<uint8_t>(WeaponOrderEventStatus::Success), 2, 5, 42, 9,
        UINT32_MAX, fixtureOdstShot);
    Check(fixtureOdstShotSequence != 0,
        "An ODST-shaped shot event publishes inside the recording session");

    // Halo 2-shaped shot: the firing detour publishes the engine's final ray
    // with the firing context's own slot, an absent barrel index, the
    // independent-path substitution flag and the observer engine aim snapshot
    // (source 7, Halo 2 observer). The Python fixture validator enforces this
    // row's shape end to end.
    TelemetryShotPayload fixtureHalo2Shot{};
    fixtureHalo2Shot.origin[0] = 6.0f;
    fixtureHalo2Shot.origin[1] = -1.5f;
    fixtureHalo2Shot.origin[2] = 2.25f;
    fixtureHalo2Shot.direction[0] = 0.5f;
    fixtureHalo2Shot.direction[1] = -0.5f;
    fixtureHalo2Shot.direction[2] = 0.5f;
    fixtureHalo2Shot.engineAim[0] = 0.25f;
    fixtureHalo2Shot.engineAim[1] = 0.5f;
    fixtureHalo2Shot.engineAim[2] = -0.75f;
    fixtureHalo2Shot.reticleDirection[0] = 0.125f;
    fixtureHalo2Shot.reticleDirection[1] = 0.25f;
    fixtureHalo2Shot.reticleDirection[2] = -0.5f;
    fixtureHalo2Shot.slot = 1;
    fixtureHalo2Shot.barrel = kTelemetryShotIndexUnknown;
    fixtureHalo2Shot.flags = 0x00A2u; // substituted, dual, unit aim
    fixtureHalo2Shot.engineAimSource = 7; // Halo 2 observer stock forward
    const uint64_t fixtureHalo2ShotSequence = Telemetry_PublishShotEvent(
        static_cast<uint8_t>(WeaponOrderEventStatus::Success), 6, 11, 42, 0x77,
        0x51, fixtureHalo2Shot);
    Check(fixtureHalo2ShotSequence != 0,
        "A Halo 2-shaped shot event publishes inside the recording session");

    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle) &&
            WaitForAnalyserLaunchCompletions(1),
        "Recording transitions through Finalizing to Idle");
    status = Telemetry_GetStatus();
    Check(status.producerCalls ==
            status.duplicateSerialSuppressed + status.enqueued +
                status.droppedQueueFull,
        "Clean session satisfies the producer accounting identity");
    Check(status.written == status.enqueued,
        "Clean Stop drains all enqueued records before session_end");
    auto analyserSnapshot = Telemetry_TestGetAnalyserLauncherSnapshot();
    Check(analyserSnapshot.pythonResolutionAttempts == 0 &&
            analyserSnapshot.pyResolutionAttempts == 0 &&
            analyserSnapshot.pythonProcessAttempts == 0 &&
            analyserSnapshot.pyProcessAttempts == 0,
        "Absent analyser fails open before interpreter discovery or process creation");

    const std::wstring firstFile = FirstTelemetryFile(recordingDirectory);
    Check(!firstFile.empty() && CountTelemetryFiles(directory) == 0,
        "Clean session creates telemetry only in the recording subfolder");
    const std::string cleanSession = ReadFileBytes(firstFile);
    Check(cleanSession.find(
            "\"semantic_grip_positions\":\"position-only OpenXR grip-action locates in LOCAL at controller sample time, routed through MCC semantic primary/support handedness roles; each position is valid only when its corresponding validity flag is true\"") !=
            std::string::npos,
        "Session provenance documents semantic grip space, timing, routing, and validity");
    Check(cleanSession.find("\"frame_field_domains\":\"") != std::string::npos &&
            cleanSession.find("\"engine_world_axes\":\"") != std::string::npos &&
            cleanSession.find("\"engine_world_scale_authority\":\"") !=
                std::string::npos &&
            cleanSession.find("\"engine_aim_sources\":\"") != std::string::npos &&
            cleanSession.find("\"engine_camera_sources\":\"") != std::string::npos &&
            cleanSession.find("\"reticle_presented_lag\":\"") != std::string::npos &&
            cleanSession.find("\"dual_slot_semantics\":\"") != std::string::npos &&
            cleanSession.find("\"shot_event_semantics\":\"") !=
                std::string::npos,
        "Session provenance declares field domains, engine-world axes/scale authority, per-title sources, reticle lag, dual and shot semantics");
    Check(cleanSession.find(
            "\"two_hand_offhand_influence_provenance\":\"") !=
            std::string::npos,
        "Session provenance declares the frozen free two-hand offhand authority");
    Check(cleanSession.find(
            "never a consumption receipt for the free two-hand product geometry") !=
            std::string::npos,
        "Session provenance states the support-endpoint selection is not the "
        "free two-hand consumption receipt");
    Check(cleanSession.find("\"two_hand_offhand_influence\":0.75") !=
            std::string::npos,
        "The recorded frame carries the frozen free two-hand offhand authority");
    Check(cleanSession.find("\"kind\":\"shot\"") != std::string::npos &&
            cleanSession.find("\"shot_origin\":[11.5,-0.25,4]") !=
                std::string::npos &&
            cleanSession.find("\"shot_direction\":[0,0,-1]") !=
                std::string::npos &&
            cleanSession.find("\"shot_engine_aim\":[0.5,0.25,-0.75]") !=
                std::string::npos &&
            cleanSession.find(
                "\"shot_reticle_direction\":[0.125,0,-0.992156804]") !=
                std::string::npos &&
            cleanSession.find("\"shot_slot\":0") != std::string::npos &&
            cleanSession.find("\"shot_barrel\":1") != std::string::npos &&
            cleanSession.find("\"shot_flags\":103") != std::string::npos &&
            cleanSession.find("\"shot_engine_aim_source\":1") !=
                std::string::npos,
        "The recording session serializes the fixed shot payload into the fixture");
    Check(cleanSession.find("\"shot_origin\":[3.25,1.5,-2]") !=
                std::string::npos &&
            cleanSession.find("\"shot_direction\":[0,0,0]") !=
                std::string::npos &&
            cleanSession.find("\"shot_engine_aim\":[0,0,0]") !=
                std::string::npos &&
            cleanSession.find("\"shot_slot\":-1") != std::string::npos &&
            cleanSession.find("\"shot_barrel\":-1") != std::string::npos &&
            cleanSession.find("\"shot_engine_aim_source\":0") !=
                std::string::npos,
        "The recording session serializes a direction-absent ODST-shaped shot");
    Check(cleanSession.find("\"shot_origin\":[6,-1.5,2.25]") !=
                std::string::npos &&
            cleanSession.find("\"shot_direction\":[0.5,-0.5,0.5]") !=
                std::string::npos &&
            cleanSession.find("\"shot_engine_aim\":[0.25,0.5,-0.75]") !=
                std::string::npos &&
            cleanSession.find("\"shot_reticle_direction\":[0.125,0.25,-0.5]") !=
                std::string::npos &&
            cleanSession.find("\"shot_slot\":1") != std::string::npos &&
            cleanSession.find("\"shot_barrel\":-1") != std::string::npos &&
            cleanSession.find("\"shot_flags\":162") != std::string::npos &&
            cleanSession.find("\"shot_engine_aim_source\":7") !=
                std::string::npos,
        "The recording session serializes a Halo 2-shaped shot with its observer source");
    Check(status.weaponEventsEnqueued == 3 &&
            status.weaponEventsWritten == 3 &&
            status.weaponEventsDroppedQueueFull == 0,
        "Shot-event accounting is exact for a clean recording session");
    Check(!firstFile.empty() &&
            CopyFileW(firstFile.c_str(), outputPath.c_str(), FALSE) != FALSE,
        "Lifecycle test exports its JSONL for strict Python validation");

    // Real-session weapon-event transport: a record published in a recording
    // session must be serialized into that session's file, its accounting
    // must be exact, and the following sessions must not inherit its state.
    {
        Telemetry_RequestStart();
        Check(WaitForState(TelemetryRecorderState::Recording),
            "Weapon-event session reaches Recording");
        const uint64_t published = Telemetry_PublishWeaponEvent(6, 1, 3, 7,
            999, 0xABC, 0x1234, 0, 0);
        Check(published != 0,
            "Weapon event publishes inside a real recording session");
        Telemetry_RequestStop();
        Check(WaitForState(TelemetryRecorderState::Idle),
            "Weapon-event session stops cleanly");
        Check(DirectoryContainsBytes(recordingDirectory,
                "\"kind\":\"fp_weapon_commit\""),
            "Weapon event is serialized into its own session file");
        const TelemetryStatusSnapshot weaponStatus = Telemetry_GetStatus();
        Check(weaponStatus.weaponEventsEnqueued == 1 &&
                weaponStatus.weaponEventsWritten == 1 &&
                weaponStatus.weaponEventsDroppedQueueFull == 0,
            "Weapon-event accounting is exact for a clean session");
        Telemetry_RequestStart();
        Check(WaitForState(TelemetryRecorderState::Recording),
            "Session restart after a weapon-event session reaches Recording");
        Telemetry_RequestStop();
        Check(WaitForState(TelemetryRecorderState::Idle),
            "Restarted session stops cleanly without stale weapon events");
        const TelemetryStatusSnapshot restartedStatus = Telemetry_GetStatus();
        Check(restartedStatus.weaponEventsEnqueued == 0 &&
                restartedStatus.weaponEventsWritten == 0,
            "A restarted session starts with no inherited weapon-event state");
    }

    // Final-drain frontier through the real worker: even a large
    // missing-sequence cascade must close with every claimed sequence
    // represented, and the following session must inherit no tail.
    {
        Telemetry_RequestStart();
        Check(WaitForState(TelemetryRecorderState::Recording),
            "Frontier session reaches Recording");
        constexpr uint32_t kFrontierDropped = 400;
        for (uint32_t i = 0;
             i < kWeaponOrderEventQueueSlots + kFrontierDropped; ++i)
            (void)Telemetry_PublishWeaponEvent(2, 0, 1, 1, 1, 1, 1, 0, 0);
        Telemetry_RequestStop();
        Check(WaitForState(TelemetryRecorderState::Idle),
            "Frontier session stops cleanly");
        const TelemetryStatusSnapshot frontier = Telemetry_GetStatus();
        Check(frontier.weaponEventsEnqueued +
                frontier.weaponEventsDroppedQueueFull ==
                kWeaponOrderEventQueueSlots + kFrontierDropped,
            "Frontier session accounts for every claimed sequence");
        Check(frontier.weaponEventsWritten == frontier.weaponEventsEnqueued,
            "Frontier session serializes every committed record before close");
        Telemetry_RequestStart();
        Check(WaitForState(TelemetryRecorderState::Recording),
            "Session after the frontier close reaches Recording");
        Telemetry_RequestStop();
        Check(WaitForState(TelemetryRecorderState::Idle),
            "Session after the frontier close stops cleanly");
        const TelemetryStatusSnapshot following = Telemetry_GetStatus();
        Check(following.weaponEventsEnqueued == 0 &&
                following.weaponEventsWritten == 0 &&
                following.weaponEventsDroppedQueueFull == 0,
            "No undrained weapon-event tail carries into the next session");
    }

    const unsigned cleanFiles = CountTelemetryFiles(recordingDirectory);
    Telemetry_TestSetForceOpenFailure(true);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Error),
        "File-open failure transitions to Error without affecting VR state");
    status = Telemetry_GetStatus();
    Check(!status.accepting && status.error == TelemetryErrorCode::OpenFileFailed,
        "File-open failure leaves producer acceptance disabled");
    Check(CountTelemetryFiles(recordingDirectory) == cleanFiles,
        "Failed file creation never truncates or appends an existing session");

    Telemetry_TestSetForceOpenFailure(false);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Error to Start retries a new session");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Retried session stops cleanly");
    Check(CountTelemetryFiles(recordingDirectory) == cleanFiles + 1,
        "Error retry creates a distinct session file");

    Telemetry_TestPauseStarting(true);
    const unsigned filesBeforeCancelledStart =
        CountTelemetryFiles(recordingDirectory);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Starting),
        "Start test reaches the controlled Starting edge");
    Telemetry_RequestStop();
    Check(!Telemetry_GetStatus().accepting,
        "Start to Stop before initialization never enables acceptance");
    Telemetry_TestPauseStarting(false);
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Cancelled Starting converges directly to Idle");
    Check(CountTelemetryFiles(recordingDirectory) ==
            filesBeforeCancelledStart,
        "Cancelled Starting before file open creates no empty session file");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Finalizing restart test begins its first session");
    Telemetry_TestPauseFinalizing(true);
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Finalizing),
        "Stop reaches the controlled Finalizing edge");
    Telemetry_RequestStart();
    Telemetry_TestPauseFinalizing(false);
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Stop to Start during Finalizing creates a second recording session");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Second distinct session after Finalizing stops cleanly");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Producer race test begins recording");
    Telemetry_TestPauseProducerSecondCheck(true);
    std::atomic<bool> beginResult{true};
    std::thread producer([&]() {
        beginResult.store(Telemetry_BeginFrame(777),
            std::memory_order_release);
    });
    const uint64_t raceDeadline = GetTickCount64() + 3000;
    while (!Telemetry_TestProducerReachedSecondCheck() &&
           GetTickCount64() < raceDeadline)
    {
        Sleep(1);
    }
    Check(Telemetry_TestProducerReachedSecondCheck(),
        "Producer reaches the controlled point between acceptance checks");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Finalizing),
        "Stop disables acceptance while the producer is in flight");
    Check(Telemetry_GetStatus().producerCalls == 0,
        "Producer calls is not incremented before the second acceptance check");
    Telemetry_TestPauseProducerSecondCheck(false);
    producer.join();
    Check(!beginResult.load(std::memory_order_acquire),
        "Producer losing the Start/Stop race owns no session record");
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Finalizing waits for the producer receipt and then completes");
    Check(Telemetry_GetStatus().producerCalls == 0,
        "Lost race cannot break the producer accounting identity");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Admission ABA test begins its first session");
    Telemetry_TestPauseProducerAdmission(true);
    beginResult.store(true, std::memory_order_release);
    std::thread staleProducer([&]() {
        beginResult.store(Telemetry_BeginFrame(888),
            std::memory_order_release);
    });
    const uint64_t admissionDeadline = GetTickCount64() + 3000;
    while (!Telemetry_TestProducerReachedAdmission() &&
           GetTickCount64() < admissionDeadline)
    {
        Sleep(1);
    }
    Check(Telemetry_TestProducerReachedAdmission(),
        "Producer reaches the controlled point before in-flight admission");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Invisible pre-admission producer cannot block old-session finalization");
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Admission ABA test starts a distinct replacement session");
    Telemetry_TestPauseProducerAdmission(false);
    staleProducer.join();
    Check(!beginResult.load(std::memory_order_acquire),
        "Old-session producer epoch cannot pass admission in a new session");
    Check(Telemetry_GetStatus().producerCalls == 0,
        "Rejected old-session producer is absent from new-session accounting");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Admission ABA replacement session stops cleanly");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Accepted-receipt Stop test begins recording");
    frame = EvidenceFrame(999);
    Check(Telemetry_BeginFrame(frame.preparedSerial),
        "Producer obtains an accepted in-flight receipt");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Finalizing),
        "Stop enters Finalizing while an accepted receipt is outstanding");
    Sleep(20);
    Check(Telemetry_GetStatus().state == TelemetryRecorderState::Finalizing,
        "Finalizing cannot pass an accepted producer that has not published");
    Telemetry_PublishFrame(frame);
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Publishing the accepted receipt lets finalization complete");
    status = Telemetry_GetStatus();
    Check(status.enqueued == 1 && status.written == 1,
        "The late accepted receipt is drained before session_end");

    Telemetry_TestPauseRecording(true);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Stop-order test begins recording with worker work paused");
    const uint64_t recordingPauseDeadline = GetTickCount64() + 3000;
    while (!Telemetry_TestWorkerReachedRecording() &&
           GetTickCount64() < recordingPauseDeadline)
    {
        Sleep(1);
    }
    Check(Telemetry_TestWorkerReachedRecording(),
        "Worker reaches the controlled Recording edge");
    for (uint64_t serial = 0; serial < 3; ++serial)
    {
        frame = EvidenceFrame(5000 + serial);
        Check(Telemetry_BeginFrame(frame.preparedSerial),
            "Stop-order fixture frame obtains admission");
        Telemetry_PublishFrame(frame);
    }
    Telemetry_TestPauseFinalizing(true);
    Telemetry_RequestStop();
    Telemetry_TestPauseRecording(false);
    Check(WaitForState(TelemetryRecorderState::Finalizing),
        "Stop is reconciled before ordinary Recording drain work");
    Check(Telemetry_GetStatus().written == 0,
        "Stop disables admission before the queued frames are drained");
    Telemetry_TestPauseFinalizing(false);
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Finalization drains the Stop-order fixture after producer quiescence");
    Check(Telemetry_GetStatus().written == 3,
        "Stop-order fixture writes every admitted record before session_end");

    Telemetry_TestPauseRecordingDrain(true);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "In-drain Stop test begins recording");
    for (uint64_t serial = 0; serial < 3; ++serial)
    {
        frame = EvidenceFrame(6000 + serial);
        Check(Telemetry_BeginFrame(frame.preparedSerial),
            "In-drain Stop fixture frame obtains admission");
        Telemetry_PublishFrame(frame);
    }
    const uint64_t drainPauseDeadline = GetTickCount64() + 3000;
    while (!Telemetry_TestWorkerReachedRecordingDrain() &&
           GetTickCount64() < drainPauseDeadline)
    {
        Sleep(1);
    }
    Check(Telemetry_TestWorkerReachedRecordingDrain(),
        "Worker reaches the controlled point during a normal drain");
    Telemetry_TestPauseFinalizing(true);
    Telemetry_RequestStop();
    Telemetry_TestPauseRecordingDrain(false);
    Check(WaitForState(TelemetryRecorderState::Finalizing),
        "A Stop arriving during drain interrupts normal draining");
    Check(Telemetry_GetStatus().written == 1,
        "Interruptible normal drain stops after its current record");
    Telemetry_TestPauseFinalizing(false);
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Finalization drains records left by an interrupted normal drain");
    Check(Telemetry_GetStatus().written == 3,
        "Interrupted normal drain loses no accepted records");

    Telemetry_TestPauseRecording(true);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Queue-full preflight test begins recording");
    const uint64_t fullPauseDeadline = GetTickCount64() + 3000;
    while (!Telemetry_TestWorkerReachedRecording() &&
           GetTickCount64() < fullPauseDeadline)
    {
        Sleep(1);
    }
    Check(Telemetry_TestWorkerReachedRecording(),
        "Queue-full preflight pauses the worker before ring consumption");
    bool filledRing = true;
    for (uint64_t serial = 0;
         serial < kTelemetryQueueUsableCapacity; ++serial)
    {
        frame = EvidenceFrame(7000 + serial);
        filledRing = Telemetry_TestPushRing(frame) && filledRing;
    }
    Check(filledRing, "Queue-full preflight fills every usable ring slot");
    Check(!Telemetry_BeginFrame(9999),
        "A full ring rejects admission before frame capture");
    status = Telemetry_GetStatus();
    Check(status.producerCalls == 1 &&
            status.droppedQueueFull == 1 && status.enqueued == 0,
        "Queue-full preflight preserves producer accounting");
    bool emptiedRing = true;
    for (uint64_t serial = 0;
         serial < kTelemetryQueueUsableCapacity; ++serial)
    {
        emptiedRing = Telemetry_TestPopRing(frame) && emptiedRing;
    }
    Check(emptiedRing && !Telemetry_TestPopRing(frame),
        "Queue-full preflight fixture removes its synthetic backlog");
    Telemetry_RequestStop();
    Telemetry_TestPauseRecording(false);
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Queue-full preflight session stops without an in-flight receipt");
    status = Telemetry_GetStatus();
    Check(status.producerCalls ==
            status.duplicateSerialSuppressed + status.enqueued +
                status.droppedQueueFull,
        "Queue-full preflight satisfies the producer accounting identity");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Drain-all Stop test begins recording");
    constexpr uint64_t kStopDrainFrames = 600;
    for (uint64_t serial = 0; serial < kStopDrainFrames; ++serial)
    {
        frame = EvidenceFrame(10000 + serial);
        Check(Telemetry_BeginFrame(frame.preparedSerial),
            "Drain-all fixture frame obtains producer admission");
        Telemetry_PublishFrame(frame);
    }
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Stop drains a backlog larger than the removed 256-frame poll cap");
    status = Telemetry_GetStatus();
    Check(status.enqueued == kStopDrainFrames &&
            status.written == kStopDrainFrames,
        "Drain-all Stop writes every admitted backlog frame");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Duplicate reset test begins first session");
    frame = EvidenceFrame(0);
    Check(Telemetry_BeginFrame(0),
        "First session accepts serial zero");
    Telemetry_PublishFrame(frame);
    Check(!Telemetry_BeginFrame(0),
        "First session suppresses serial zero duplicate");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle),
        "First duplicate-reset session stops");
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Duplicate reset test begins second session");
    Check(Telemetry_BeginFrame(0),
        "New session resets explicit duplicate state while acceptance is disabled");
    Telemetry_PublishFrame(frame);
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle),
        "Second duplicate-reset session stops cleanly");

    Telemetry_TestFailWriteAfter(0);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Error),
        "session_start write failure transitions to Error");
    status = Telemetry_GetStatus();
    Check(status.error == TelemetryErrorCode::WriteFailed &&
            !status.accepting && !status.desiredRecording,
        "Write failure disables producer admission and clears the desired state");

    Telemetry_TestFailFlushAfter(0);
    Telemetry_TestPauseStarting(true);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Starting),
        "Flush-failure retry leaves the prior Error state and reaches Starting");
    Telemetry_TestPauseStarting(false);
    Check(WaitForState(TelemetryRecorderState::Error),
        "session_start flush failure transitions to Error");
    Check(Telemetry_GetStatus().error == TelemetryErrorCode::FlushFailed,
        "Flush failure reports its distinct error code");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Frame-write failure test retries from Error");
    Telemetry_TestFailWriteAfter(0);
    frame = EvidenceFrame(20000);
    Check(Telemetry_BeginFrame(frame.preparedSerial),
        "Frame-write failure fixture obtains admission");
    Telemetry_PublishFrame(frame);
    Check(WaitForState(TelemetryRecorderState::Error),
        "Frame write failure transitions to Error");
    Check(Telemetry_GetStatus().error == TelemetryErrorCode::WriteFailed,
        "Frame write failure reports WriteFailed");

    Telemetry_TestPauseRecording(true);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Periodic flush failure test retries from Error");
    const uint64_t periodicFlushPauseDeadline = GetTickCount64() + 3000;
    while (!Telemetry_TestWorkerReachedRecording() &&
           GetTickCount64() < periodicFlushPauseDeadline)
    {
        Sleep(1);
    }
    Check(Telemetry_TestWorkerReachedRecording(),
        "Periodic flush failure pauses before draining its backlog");
    Telemetry_TestFailFlushAfter(0);
    constexpr uint64_t kPeriodicFlushFrames = 600;
    for (uint64_t serial = 0; serial < kPeriodicFlushFrames; ++serial)
    {
        frame = EvidenceFrame(21000 + serial);
        Check(Telemetry_BeginFrame(frame.preparedSerial),
            "Periodic flush fixture frame obtains admission");
        Telemetry_PublishFrame(frame);
    }
    Telemetry_TestPauseRecording(false);
    Check(WaitForState(TelemetryRecorderState::Error),
        "Periodic flush failure interrupts a continuously nonempty drain");
    status = Telemetry_GetStatus();
    Check(status.error == TelemetryErrorCode::FlushFailed &&
            status.written == 256 &&
            status.enqueued == kPeriodicFlushFrames,
        "Periodic drain failure reports FlushFailed at the frame deadline");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "session_end write failure test retries from Error");
    Telemetry_TestFailWriteAfter(0);
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Error),
        "session_end write failure transitions to Error");
    Check(Telemetry_GetStatus().error == TelemetryErrorCode::WriteFailed,
        "session_end write failure reports WriteFailed");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "session_end flush failure test retries from Error");
    Telemetry_TestPauseRecording(true);
    const uint64_t finalFlushPauseDeadline = GetTickCount64() + 3000;
    while (!Telemetry_TestWorkerReachedRecording() &&
           GetTickCount64() < finalFlushPauseDeadline)
    {
        Sleep(1);
    }
    Check(Telemetry_TestWorkerReachedRecording(),
        "Final-flush test pauses before Recording work");
    Telemetry_TestFailFlushAfter(0);
    Telemetry_RequestStop();
    Telemetry_TestPauseRecording(false);
    Check(WaitForState(TelemetryRecorderState::Error),
        "session_end flush failure transitions to Error");
    Check(Telemetry_GetStatus().error == TelemetryErrorCode::FlushFailed,
        "session_end flush failure reports FlushFailed");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Close failure test retries from Error");
    Telemetry_TestSetForceCloseFailure(true);
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Error),
        "Session close failure transitions to Error rather than Idle");
    Check(Telemetry_GetStatus().error == TelemetryErrorCode::CloseFailed,
        "Session close failure reports CloseFailed");

    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Explicit Start retries successfully after writer lifecycle failures");
    const uint32_t launchesBeforeFinalRetryStop =
        Telemetry_TestAnalyserLaunchCompletions();
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle) &&
            WaitForAnalyserLaunchCompletions(
                launchesBeforeFinalRetryStop + 1),
        "Writer failure retry finishes a fresh clean session");

    Check(CreateDummyAnalyser(directory),
        "Analyser lifecycle test creates the stable installed script path");
    Telemetry_TestResetAnalyserLauncher();
    Telemetry_TestSetAnalyserModuleDirectory(directory.c_str());
    Telemetry_TestConfigureAnalyserLauncher(true, true, true, true);
    const std::vector<std::wstring> filesBeforeAnalyserFixture =
        ListTelemetryFiles(recordingDirectory);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Analyser success fixture starts a recording");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle) &&
            WaitForAnalyserLaunchCompletions(1),
        "Analyser launch occurs after successful recording finalization");
    analyserSnapshot = Telemetry_TestGetAnalyserLauncherSnapshot();
    Check(analyserSnapshot.pythonResolutionAttempts == 1 &&
            analyserSnapshot.pyResolutionAttempts == 0 &&
            analyserSnapshot.pythonProcessAttempts == 1 &&
            analyserSnapshot.pyProcessAttempts == 0,
        "Interpreter discovery and process creation prefer python.exe without probing py.exe after success");
    Check(analyserSnapshot.stateAtLastProcessAttempt ==
              TelemetryRecorderState::Idle &&
            analyserSnapshot.sessionClosedAtLastProcessAttempt &&
            analyserSnapshot.standardHandlesValid &&
            analyserSnapshot.inheritHandles &&
            analyserSnapshot.restrictedHandleList &&
            (analyserSnapshot.creationFlags & CREATE_NO_WINDOW) != 0 &&
            (analyserSnapshot.creationFlags & CREATE_UNICODE_ENVIRONMENT) != 0 &&
            (analyserSnapshot.creationFlags &
                EXTENDED_STARTUPINFO_PRESENT) != 0,
        "Analyser process starts only after close and Idle with only inherited NUL handles and no window");
    // The automatic launch must receive the just-closed recording from the
    // canonical Telemetry Recordings folder, not a legacy toolkit location.
    Check(!analyserSnapshot.recordingPath.empty() &&
            GetFileAttributesW(analyserSnapshot.recordingPath.c_str()) !=
                INVALID_FILE_ATTRIBUTES &&
            analyserSnapshot.recordingPath.compare(
                0, recordingDirectory.size(), recordingDirectory) == 0 &&
            analyserSnapshot.commandLine.find(
                L"\"--report\"") == std::wstring::npos &&
            analyserSnapshot.commandLine.find(
                L"\"--json-out\"") == std::wstring::npos &&
            analyserSnapshot.commandLine.find(L"_analysis") ==
                std::wstring::npos &&
            analyserSnapshot.commandLine.find(L"combined") ==
                std::wstring::npos,
        "Automatic invocation passes exactly one closed recording from the canonical Telemetry Recordings folder and no legacy or combined outputs");
    // Identify the recording this fixture's session closed as the file that
    // appeared in the recording folder after that session ran, then require
    // the automatic invocation to have received exactly that file.
    const std::vector<std::wstring> filesAfterAnalyserFixture =
        ListTelemetryFiles(recordingDirectory);
    std::wstring closedRecording;
    for (const std::wstring& candidate : filesAfterAnalyserFixture)
    {
        if (std::find(filesBeforeAnalyserFixture.begin(),
                filesBeforeAnalyserFixture.end(),
                candidate) == filesBeforeAnalyserFixture.end())
        {
            closedRecording = candidate;
            break;
        }
    }
    Check(!closedRecording.empty() &&
            analyserSnapshot.recordingPath == closedRecording,
        "Automatic invocation receives exactly the recording closed by this session");

    Telemetry_TestResetAnalyserLauncher();
    Telemetry_TestSetAnalyserModuleDirectory(directory.c_str());
    Telemetry_TestConfigureAnalyserLauncher(true, true, true, true);
    Telemetry_TestPauseAnalyserLaunch(true);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Delayed analyser fixture starts its first recording");
    Telemetry_RequestStop();
    const uint64_t analyserPauseDeadline = GetTickCount64() + 3000;
    while (!Telemetry_TestAnalyserLaunchReached() &&
           GetTickCount64() < analyserPauseDeadline)
    {
        Sleep(1);
    }
    Check(WaitForState(TelemetryRecorderState::Idle) &&
            Telemetry_TestAnalyserLaunchReached(),
        "Analyser launch worker can pause after a closed session reaches Idle");
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "A new recording starts while optional analyser setup is paused");
    Telemetry_TestPauseAnalyserLaunch(false);
    Check(WaitForAnalyserLaunchCompletions(1),
        "Paused analyser setup resumes independently of the active recording");
    analyserSnapshot = Telemetry_TestGetAnalyserLauncherSnapshot();
    Check(analyserSnapshot.stateAtQueue == TelemetryRecorderState::Idle &&
            Telemetry_GetStatus().state == TelemetryRecorderState::Recording,
        "Analyser work is queued only from Idle and cannot hold recorder restart");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle) &&
            WaitForAnalyserLaunchCompletions(2),
        "Recording restarted during analyser setup also closes and analyses cleanly");

    Telemetry_TestResetAnalyserLauncher();
    Telemetry_TestSetAnalyserModuleDirectory(directory.c_str());
    Telemetry_TestConfigureAnalyserLauncher(true, true, false, false);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "Analyser process-failure fixture starts a recording");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle) &&
            WaitForAnalyserLaunchCompletions(1),
        "Failed python.exe creation falls back once to py.exe -3 without changing Idle");
    analyserSnapshot = Telemetry_TestGetAnalyserLauncherSnapshot();
    Check(analyserSnapshot.pythonProcessAttempts == 1 &&
            analyserSnapshot.pyProcessAttempts == 1 &&
            Telemetry_GetStatus().state == TelemetryRecorderState::Idle &&
            Telemetry_GetStatus().error == TelemetryErrorCode::None,
        "Both analyser process failures remain outside recorder error state");

    Telemetry_TestResetAnalyserLauncher();
    Telemetry_TestSetAnalyserModuleDirectory(directory.c_str());
    Telemetry_TestConfigureAnalyserLauncher(false, true, false, true);
    Telemetry_RequestStart();
    Check(WaitForState(TelemetryRecorderState::Recording),
        "A new recording starts after both analyser process attempts failed");
    Telemetry_RequestStop();
    Check(WaitForState(TelemetryRecorderState::Idle) &&
            WaitForAnalyserLaunchCompletions(1),
        "py.exe -3 launches when python.exe discovery is unavailable");
    analyserSnapshot = Telemetry_TestGetAnalyserLauncherSnapshot();
    Check(analyserSnapshot.pythonResolutionAttempts == 1 &&
            analyserSnapshot.pythonProcessAttempts == 0 &&
            analyserSnapshot.pyResolutionAttempts == 1 &&
            analyserSnapshot.pyProcessAttempts == 1 &&
            analyserSnapshot.commandLine.find(L"\"-3\"") !=
                std::wstring::npos,
        "Interpreter fallback preserves python.exe then py.exe -3 discovery order");
}

std::string ReadFileBytes(const std::wstring& path)
{
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
        return {};
    std::string bytes;
    char buffer[4096]{};
    for (;;)
    {
        const size_t count = std::fread(buffer, 1, sizeof(buffer), file);
        bytes.append(buffer, count);
        if (count != sizeof(buffer))
            break;
    }
    std::fclose(file);
    return bytes;
}

int RunAnalyserSmoke(
    const wchar_t* moduleDirectory, const wchar_t* recordingPath)
{
    std::wstring sourceRoot = moduleDirectory;
    if (!sourceRoot.empty() && sourceRoot.back() != L'\\')
        sourceRoot += L'\\';
    const std::wstring unicodeModuleDirectory =
        sourceRoot + L"Unicode Module \u6d4b\u8bd5";
    const std::wstring unicodeAnalyserDirectory =
        unicodeModuleDirectory + L"\\TelemetryAnalyser";
    const std::wstring unicodeScript =
        unicodeAnalyserDirectory + L"\\analyse_mccvr_telemetry.py";
    wchar_t uniqueSuffix[40]{};
    _snwprintf_s(uniqueSuffix, _countof(uniqueSuffix), _TRUNCATE,
        L"-%llu", static_cast<unsigned long long>(GetTickCount64()));
    const std::wstring unicodeRecording =
        sourceRoot + L"Unicode Recording \u8bb0\u5f55" + uniqueSuffix + L".jsonl";
    std::wstring recordingStem = unicodeRecording;
    const size_t stemDot = recordingStem.find_last_of(L'.');
    if (stemDot != std::wstring::npos)
        recordingStem.resize(stemDot);
    const std::wstring sidecarManifest =
        recordingStem + L".manifest.json";
    const std::wstring sidecarDiagnosticsJson =
        recordingStem + L".diagnostics.json";
    const std::wstring sidecarDiagnosticsMd =
        recordingStem + L".diagnostics.md";
    const std::wstring sidecarGuide =
        sourceRoot + L"TELEMETRY_AI_GUIDE.md";
    const std::wstring legacyAnalysisDirectory =
        recordingStem + L"_analysis";
    if ((!CreateDirectoryW(unicodeModuleDirectory.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) ||
        (!CreateDirectoryW(unicodeAnalyserDirectory.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) ||
        !CopyFileW(
            (sourceRoot +
                L"TelemetryAnalyser\\analyse_mccvr_telemetry.py").c_str(),
            unicodeScript.c_str(), FALSE) ||
        !CopyFileW(recordingPath, unicodeRecording.c_str(), FALSE))
    {
        std::fprintf(stderr,
            "FAIL: could not stage Unicode analyser smoke paths\n");
        return 1;
    }

    TelemetryAnalyserLaunchPlanTest plan{};
    if (!Telemetry_TestBuildAnalyserLaunchPlan(
            unicodeModuleDirectory.c_str(), unicodeRecording.c_str(),
            L"python.exe", false, plan))
    {
        std::fprintf(stderr, "FAIL: could not build analyser smoke paths\n");
        return 1;
    }
    if (plan.commandLine.find(L"\"--report\"") != std::wstring::npos ||
        plan.commandLine.find(L"\"--json-out\"") != std::wstring::npos)
    {
        std::fprintf(stderr,
            "FAIL: automatic analyser invocation still requests legacy outputs\n");
        return 1;
    }

    Telemetry_TestResetAnalyserLauncher();
    Telemetry_TestUseRealAnalyserLauncher(true);
    Telemetry_TestLaunchAnalyser(
        unicodeModuleDirectory.c_str(), unicodeRecording.c_str());
    if (!WaitForAnalyserLaunchCompletions(1))
    {
        std::fprintf(stderr, "FAIL: analyser smoke launch did not complete\n");
        return 1;
    }

    // Pinned analyser identity for provenance. The automatic invocation emits
    // only the canonical per-capture sidecars: manifest, diagnostics and the
    // capture-independent guide. Lowercase: the analyser emits sha256
    // hexdigest, and this smoke check is a case-sensitive find.
    constexpr char kExpectedHash[] =
        "053cf61671be281551b89a459e0611490f29d0ffd00387b43668043793bae5b4";
    constexpr char kExpectedVersion[] = "6.3.0-standalone";
    const uint64_t deadline = GetTickCount64() + 30000;
    do
    {
        const std::string manifest = ReadFileBytes(sidecarManifest);
        const std::string diagnostics = ReadFileBytes(sidecarDiagnosticsJson);
        const std::string diagnosticsMd = ReadFileBytes(sidecarDiagnosticsMd);
        const std::string guide = ReadFileBytes(sidecarGuide);
        if (!manifest.empty() &&
            !diagnostics.empty() &&
            !diagnosticsMd.empty() &&
            !guide.empty() &&
            manifest.find("\"manifest_schema_version\": 1") !=
                std::string::npos &&
            manifest.find("mccvr.telemetry.manifest") !=
                std::string::npos &&
            manifest.find("\"hash_basis\": \"exact_input_file_bytes\"") !=
                std::string::npos &&
            manifest.find("\"recorder_declared_file\"") !=
                std::string::npos &&
            diagnostics.find(kExpectedHash) != std::string::npos &&
            diagnostics.find(kExpectedVersion) != std::string::npos &&
            diagnostics.find("mccvr.telemetry.diagnostics") !=
                std::string::npos &&
            diagnostics.find("\"source_id\": \"sha256:") !=
                std::string::npos &&
            diagnosticsMd.find("MCCVR telemetry diagnostics") !=
                std::string::npos &&
            diagnosticsMd.find("Diagnostics schema: `1`") !=
                std::string::npos &&
            guide.find("evidentiary source") != std::string::npos)
        {
            if (GetFileAttributesW(legacyAnalysisDirectory.c_str()) !=
                    INVALID_FILE_ATTRIBUTES)
            {
                std::fprintf(stderr,
                    "FAIL: automatic analyser invocation created a legacy _analysis directory\n");
                return 1;
            }
            std::printf(
                "Telemetry analyser smoke: canonical manifest, diagnostics and "
                "guide produced by %s (%s)\n",
                kExpectedVersion, kExpectedHash);
            DeleteFileW(sidecarManifest.c_str());
            DeleteFileW(sidecarDiagnosticsJson.c_str());
            DeleteFileW(sidecarDiagnosticsMd.c_str());
            DeleteFileW(unicodeRecording.c_str());
            return 0;
        }
        Sleep(20);
    } while (GetTickCount64() < deadline);
    std::fprintf(stderr,
        "FAIL: analyser smoke did not produce the canonical sidecar bundle\n");
    return 1;
}
}

struct WeaponEventCoverage
{
    std::vector<uint64_t> records;
    std::vector<std::pair<uint64_t, uint64_t>> gaps;
};

bool ParseWeaponEventLine(const std::string& line,
    WeaponEventCoverage& coverage)
{
    const auto findInt = [&line](const char* key, uint64_t& out) {
        const std::string pattern = std::string("\"") + key + "\":";
        const size_t at = line.find(pattern);
        if (at == std::string::npos)
            return false;
        size_t cursor = at + pattern.size();
        uint64_t value = 0;
        bool any = false;
        while (cursor < line.size() && line[cursor] >= '0' &&
               line[cursor] <= '9')
        {
            value = value * 10 + uint64_t(line[cursor] - '0');
            ++cursor;
            any = true;
        }
        if (!any)
            return false;
        out = value;
        return true;
    };
    if (line.find("\"type\":\"weapon_event\"") != std::string::npos)
    {
        uint64_t seq = 0;
        if (!findInt("seq", seq))
            return false;
        coverage.records.push_back(seq);
        return true;
    }
    if (line.find("\"type\":\"event_gap\"") != std::string::npos)
    {
        uint64_t first = 0, last = 0;
        if (!findInt("first_missing_seq", first) ||
            !findInt("last_missing_seq", last))
            return false;
        coverage.gaps.emplace_back(first, last);
        return true;
    }
    return false; // unexpected record on the weapon-event transport
}

// Every claimed sequence [1, claimed] must be either exactly one serialized
// weapon_event or exactly one explicit gap; duplicates, overlaps and holes
// are all failures. This is the transport's no-silent-loss invariant.
bool VerifyWeaponEventCoverage(uint64_t claimed,
    const std::vector<std::string>& lines, std::string& why)
{
    WeaponEventCoverage coverage;
    for (const std::string& line : lines)
        if (!ParseWeaponEventLine(line, coverage))
        {
            why = "unparseable line: " + line;
            return false;
        }
    std::vector<uint8_t> owner(static_cast<size_t>(claimed) + 2, 0);
    for (uint64_t seq : coverage.records)
    {
        if (seq < 1 || seq > claimed)
        {
            why = "serialized record out of range";
            return false;
        }
        if (owner[seq] != 0)
        {
            why = "duplicate coverage";
            return false;
        }
        owner[seq] = 1;
    }
    for (const auto& gap : coverage.gaps)
    {
        if (gap.first < 1 || gap.second < gap.first || gap.second > claimed)
        {
            why = "gap out of range";
            return false;
        }
        for (uint64_t seq = gap.first; seq <= gap.second; ++seq)
        {
            if (owner[seq] != 0)
            {
                why = "overlapping coverage";
                return false;
            }
            owner[seq] = 2;
        }
    }
    for (uint64_t seq = 1; seq <= claimed; ++seq)
        if (owner[seq] == 0)
        {
            why = "sequence " + std::to_string(seq) +
                " is neither serialized nor gapped";
            return false;
        }
    return true;
}

void TestTwoHandLabLiveSerialization()
{
    // The serialized Lab identity must match the live solve: production
    // solver -> production serializer across the anchor matrix
    // (Production/AA/AG/GA/GG plus a GG grip-fallback), both agreement modes
    // and all four temporal phases. Trace D/S equals the live D/S, the
    // resolved label and effective influence match, and stateless versus
    // presented stay distinguishable.
    std::string jsonFloat;
    const auto expectFloat = [&](float value) {
        char buffer[64]{};
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer),
            value, std::chars_format::general,
            std::numeric_limits<float>::max_digits10);
        Check(converted.ec == std::errc{},
            "Two-Hand Lab expected value formats as JSON");
        return std::string(buffer, converted.ptr);
    };
    const auto rotate = [](const XrQuaternionf& q, const XrVector3f& v) {
        const XrVector3f u{q.x, q.y, q.z};
        const auto cross = [](const XrVector3f& a, const XrVector3f& b) {
            return XrVector3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
        };
        XrVector3f c1 = cross(u, v);
        c1.x += q.w * v.x;
        c1.y += q.w * v.y;
        c1.z += q.w * v.z;
        const XrVector3f c2 = cross(u, c1);
        return XrVector3f{
            v.x + 2.0f * c2.x, v.y + 2.0f * c2.y, v.z + 2.0f * c2.z};
    };
    const auto vecKey = [&](const char* key, float x, float y, float z) {
        return std::string("\"") + key + "\":[" + expectFloat(x) + "," +
            expectFloat(y) + "," + expectFloat(z) + "]";
    };
    const auto buildInputs = [](int anchor, float influence, int agreement,
                                 int temporal, bool gripsValid) {
        AimPoseInputs inputs{};
        inputs.rightValid = true;
        inputs.right.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        inputs.right.position = {0.0f, 0.0f, 0.0f};
        inputs.leftValid = true;
        inputs.left.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        inputs.left.position = {0.3f, 0.0f, -1.0f};
        inputs.supportPosition = {0.3f, 0.0f, -1.0f};
        inputs.twoHandEnabled = true;
        inputs.twoHandLatched = true;
        inputs.virtualStockEnabled = false;
        inputs.twoHandLabEnabled = true;
        inputs.twoHandLabAnchor = anchor;
        inputs.twoHandLabOffhandInfluence = influence;
        inputs.twoHandLabAgreement = agreement;
        inputs.twoHandLabSoftFullAgreement = 0.9f;
        inputs.twoHandLabTemporal = temporal;
        inputs.twoHandLabGeneration = 7;
        inputs.primaryGripValid = gripsValid;
        inputs.primaryGripPosition = {0.05f, -0.02f, -0.15f};
        inputs.supportGripValid = gripsValid;
        inputs.supportGripPosition = {0.32f, -0.02f, -0.95f};
        return inputs;
    };
    const auto toPoint = [](const XrVector3f& v) {
        return virtual_stock::Point3{v.x, v.y, v.z};
    };
    char serialized[32768]{};
    size_t serializedBytes = 0;

    const int anchors[] = {0, 1, 2, 3, 4};
    for (int anchor : anchors)
    {
        for (int agreement = 0; agreement <= 1; ++agreement)
        {
            const float influence = anchor == 4 ? 0.75f : 1.0f;
            const AimPoseInputs inputs =
                buildInputs(anchor, influence, agreement, 0, true);
            AimPoseTrace trace{};
            const AimPoseResult result = ComputeAimPose(inputs, &trace);
            Check(result.valid && trace.labValid,
                "Two-Hand Lab matrix solve is live and traced");
            // Independent pivot oracle: the serialized D/S must equal the
            // live resolved pair.
            two_hand_lab::PivotInputs pivots{};
            pivots.primaryAim = toPoint(inputs.right.position);
            pivots.supportAim = toPoint(inputs.left.position);
            pivots.primaryGripValid = inputs.primaryGripValid;
            pivots.primaryGrip = toPoint(inputs.primaryGripPosition);
            pivots.supportGripValid = inputs.supportGripValid;
            pivots.supportGrip = toPoint(inputs.supportGripPosition);
            pivots.productionSupportUsedGrip =
                inputs.supportEndpointUsedGrip;
            pivots.productionSupportPosition =
                toPoint(inputs.supportPosition);
            const two_hand_lab::ResolvedPivots expected =
                two_hand_lab::ResolvePivots(pivots,
                    static_cast<two_hand_lab::AnchorMode>(anchor));
            Check(trace.lab.resolvedAnchor == expected.resolved,
                "Two-Hand Lab resolved label matches the live resolve");
            Check(trace.lab.pivotsValid == expected.valid,
                "Two-Hand Lab pivot validity matches the live resolve");
            if (expected.valid)
            {
                Check(trace.lab.primaryPivot.x == expected.primary.x &&
                        trace.lab.primaryPivot.y == expected.primary.y &&
                        trace.lab.primaryPivot.z == expected.primary.z &&
                        trace.lab.supportPivot.x == expected.support.x &&
                        trace.lab.supportPivot.y == expected.support.y &&
                        trace.lab.supportPivot.z == expected.support.z,
                    "Two-Hand Lab trace D/S equals the live D/S");
            }
            // Agreement/effective identity.
            const float expectedConfidence = agreement == 1
                ? two_hand_lab::SoftAuthorityConfidence(
                      trace.lab.agreement, 0.9f)
                : 1.0f;
            Check(trace.lab.confidence == expectedConfidence,
                "Two-Hand Lab confidence matches the live solve");
            Check(trace.lab.confidence >= 0.0f &&
                    trace.lab.confidence <= 1.0f,
                "Two-Hand Lab confidence stays in [0,1]");
            const float requested =
                std::clamp(influence, 0.0f, 1.0f);
            Check(trace.lab.effectiveInfluence == requested *
                        expectedConfidence,
                "Two-Hand Lab effective influence matches the live solve");
            Check(trace.lab.effectiveInfluence <= requested + 1.0e-6f,
                "Two-Hand Lab effective never exceeds requested");

            // Serialize through the production serializer with the
            // FillTwoHandLabTelemetry stateless mapping (well-formed live
            // values, so the finite guards are no-ops).
            TelemetryFrame frame{};
            frame.preparedSerial = 11;
            frame.aimTrace = trace;
            frame.canonicalAim.valid = result.valid;
            if (result.valid)
            {
                const XrVector3f forward =
                    rotate(result.pose.orientation, {0.0f, 0.0f, -1.0f});
                frame.canonicalAim.forward = {
                    forward.x, forward.y, forward.z};
            }
            frame.twoHandLabEnabled = trace.labValid;
            frame.twoHandLabAnchorRequested =
                static_cast<uint8_t>(trace.lab.requestedAnchor);
            frame.twoHandLabAnchorResolved =
                static_cast<uint8_t>(trace.lab.resolvedAnchor);
            frame.twoHandLabAnchorFallback =
                static_cast<uint8_t>(trace.lab.fallback);
            frame.twoHandLabOffhandInfluence =
                trace.lab.requestedInfluence;
            frame.twoHandLabAgreementMode =
                static_cast<uint8_t>(trace.lab.agreementMode);
            frame.twoHandLabAgreement = trace.lab.agreement;
            frame.twoHandLabAgreementConfidence = trace.lab.confidence;
            frame.twoHandLabEffectiveInfluence =
                trace.lab.effectiveInfluence;
            frame.twoHandLabTemporalMode =
                static_cast<uint8_t>(trace.lab.temporalMode);
            frame.twoHandLabPrimaryPivotValid = trace.lab.pivotsValid;
            frame.twoHandLabSupportPivotValid = trace.lab.pivotsValid;
            if (trace.lab.pivotsValid)
            {
                frame.twoHandLabPrimaryPivot = {trace.lab.primaryPivot.x,
                    trace.lab.primaryPivot.y, trace.lab.primaryPivot.z};
                frame.twoHandLabSupportPivot = {trace.lab.supportPivot.x,
                    trace.lab.supportPivot.y, trace.lab.supportPivot.z};
            }
            frame.twoHandLabStatelessDirectionValid = result.valid;
            if (result.valid)
                frame.twoHandLabStatelessDirection =
                    frame.canonicalAim.forward;
            Check(Telemetry_TestSerializeFrame(frame, serialized,
                    sizeof(serialized), serializedBytes),
                "Two-Hand Lab matrix frame serializes");
            jsonFloat = "\"two_hand_lab_anchor_resolved\":" +
                std::to_string(static_cast<int>(expected.resolved));
            Check(std::strstr(serialized, jsonFloat.c_str()) != nullptr,
                "Two-Hand Lab resolved label serializes from the live solve");
            jsonFloat = "\"two_hand_lab_effective_influence\":" +
                expectFloat(trace.lab.effectiveInfluence);
            Check(std::strstr(serialized, jsonFloat.c_str()) != nullptr,
                "Two-Hand Lab effective influence serializes from the live "
                "solve");
            jsonFloat = "\"two_hand_lab_agreement\":" +
                expectFloat(trace.lab.agreement);
            Check(std::strstr(serialized, jsonFloat.c_str()) != nullptr,
                "Two-Hand Lab agreement serializes from the live solve");
            if (expected.valid)
            {
                jsonFloat = vecKey("two_hand_lab_primary_pivot",
                    expected.primary.x, expected.primary.y,
                    expected.primary.z);
                Check(
                    std::strstr(serialized, jsonFloat.c_str()) != nullptr,
                    "Two-Hand Lab primary pivot serializes from the live "
                    "D/S");
                jsonFloat = vecKey("two_hand_lab_support_pivot",
                    expected.support.x, expected.support.y,
                    expected.support.z);
                Check(
                    std::strstr(serialized, jsonFloat.c_str()) != nullptr,
                    "Two-Hand Lab support pivot serializes from the live "
                    "D/S");
                Check(std::strstr(serialized,
                        "\"two_hand_lab_primary_pivot_valid\":true") !=
                            nullptr &&
                        std::strstr(serialized,
                            "\"two_hand_lab_support_pivot_valid\":true") !=
                            nullptr,
                    "Two-Hand Lab both pivot flags serialize true when "
                    "pivots resolve");
            }
            else
            {
                Check(std::strstr(serialized,
                        "\"two_hand_lab_primary_pivot_valid\":false") !=
                            nullptr &&
                        std::strstr(serialized,
                            "\"two_hand_lab_support_pivot_valid\":false") !=
                            nullptr,
                    "Two-Hand Lab both pivot flags serialize false when "
                    "pivots do not resolve");
            }
            const XrVector3f liveForward = rotate(
                result.pose.orientation, {0.0f, 0.0f, -1.0f});
            jsonFloat = vecKey("two_hand_lab_stateless_direction",
                liveForward.x, liveForward.y, liveForward.z);
            Check(std::strstr(serialized, jsonFloat.c_str()) != nullptr,
                "Two-Hand Lab stateless direction serializes from the live "
                "solve");
            Check(std::strstr(serialized,
                    "\"two_hand_lab_presented_direction_valid\":false") !=
                    nullptr &&
                    std::strstr(serialized,
                        "\"two_hand_lab_temporal_active\":false") != nullptr,
                "Mode None serializes no presented direction");
        }
    }

    // GG grip fallback: missing grips resolve to AA with a reason.
    {
        const AimPoseInputs inputs = buildInputs(4, 0.5f, 1, 0, false);
        AimPoseTrace trace{};
        const AimPoseResult result = ComputeAimPose(inputs, &trace);
        Check(result.valid && trace.labValid &&
                trace.lab.resolvedAnchor ==
                    two_hand_lab::ResolvedAnchor::AA &&
                trace.lab.fallback ==
                    two_hand_lab::FallbackReason::MissingPrimaryGrip,
            "Two-Hand Lab GG grip fallback resolves AA with a reason");
        Check(trace.lab.pivotsValid,
            "Two-Hand Lab fallback still selects valid pivots");
    }

    // Unresolved pivots: a Lab-active solve whose resolve produced no
    // pivots serializes BOTH pivot flags false under the single
    // pivotsValid authority, and no pivot coordinates are written.
    {
        const AimPoseInputs inputs = buildInputs(4, 0.75f, 1, 0, true);
        AimPoseTrace trace{};
        const AimPoseResult result = ComputeAimPose(inputs, &trace);
        Check(result.valid && trace.labValid,
            "Two-Hand Lab unresolved-pivot fixture solve is live and "
            "traced");
        trace.lab.pivotsValid = false;
        TelemetryFrame frame{};
        frame.preparedSerial = 23;
        frame.aimTrace = trace;
        frame.twoHandLabEnabled = trace.labValid;
        frame.twoHandLabPrimaryPivotValid = trace.lab.pivotsValid;
        frame.twoHandLabSupportPivotValid = trace.lab.pivotsValid;
        if (trace.lab.pivotsValid)
        {
            frame.twoHandLabPrimaryPivot = {trace.lab.primaryPivot.x,
                trace.lab.primaryPivot.y, trace.lab.primaryPivot.z};
            frame.twoHandLabSupportPivot = {trace.lab.supportPivot.x,
                trace.lab.supportPivot.y, trace.lab.supportPivot.z};
        }
        Check(Telemetry_TestSerializeFrame(frame, serialized,
                sizeof(serialized), serializedBytes),
            "Two-Hand Lab unresolved-pivot frame serializes");
        Check(std::strstr(serialized,
                "\"two_hand_lab_primary_pivot_valid\":false") != nullptr &&
                std::strstr(serialized,
                    "\"two_hand_lab_support_pivot_valid\":false") != nullptr,
            "Two-Hand Lab both pivot flags serialize false when pivots "
            "do not resolve");
    }

    // Lab damping phases 2/3 through the production fragment: presented
    // stays distinguishable from stateless while temporal is active.
    const int temporalModes[] = {2, 3};
    for (int temporal : temporalModes)
    {
        TwoHandLabTemporalState state{};
        two_hand_lab::Settings settings =
            two_hand_lab::DefaultSettings();
        settings.enabled = true;
        settings.temporal =
            static_cast<two_hand_lab::TemporalMode>(temporal);
        uint64_t serial = 0;
        bool lastValid = false;
        virtual_stock::Quat4 last{};
        AimPoseInputs inputs = buildInputs(4, 1.0f, 1, temporal, true);
        AimPoseTrace trace{};
        TwoHandLabTemporalFrameSolve solve{};
        solve = AdvanceTwoHandLabTemporalFrame(state, inputs, settings,
            ++serial, 1.0f / 64.0f, lastValid, last,
            TwoHandLabProductPresentation{}, &trace);
        lastValid = solve.presentedValid;
        if (solve.presentedValid)
            last = solve.presented;
        inputs.right.orientation = {0.0f, 0.0871557f, 0.0f, 0.9961947f};
        solve = AdvanceTwoHandLabTemporalFrame(state, inputs, settings,
            ++serial, 1.0f / 64.0f, lastValid, last,
            TwoHandLabProductPresentation{}, &trace);
        Check(solve.temporalActive && solve.presentedValid,
            "Two-Hand Lab temporal phase engages presentation");
        Check(solve.errorDeg > 0.0f,
            "Two-Hand Lab temporal phase reports a live error metric");
        const virtual_stock::Point3 statelessForward =
            virtual_stock::RotatePoint(solve.stateless, {0.0f, 0.0f, -1.0f});
        const virtual_stock::Point3 presentedForward =
            virtual_stock::RotatePoint(solve.presented, {0.0f, 0.0f, -1.0f});
        Check(virtual_stock::Finite(statelessForward) &&
                virtual_stock::Finite(presentedForward),
            "Two-Hand Lab temporal directions are finite");
        TelemetryFrame frame{};
        frame.preparedSerial = serial;
        frame.twoHandLabEnabled = true;
        frame.twoHandLabTemporalMode = static_cast<uint8_t>(temporal);
        frame.twoHandLabStatelessDirectionValid = true;
        frame.twoHandLabStatelessDirection = {statelessForward.x,
            statelessForward.y, statelessForward.z};
        frame.twoHandLabPresentedDirectionValid = true;
        frame.twoHandLabPresentedDirection = {presentedForward.x,
            presentedForward.y, presentedForward.z};
        frame.twoHandLabTemporalActive = true;
        frame.twoHandLabTemporalErrorDeg = solve.errorDeg;
        Check(Telemetry_TestSerializeFrame(frame, serialized,
                sizeof(serialized), serializedBytes),
            "Two-Hand Lab temporal frame serializes");
        const std::string statelessKey = vecKey(
            "two_hand_lab_stateless_direction", statelessForward.x,
            statelessForward.y, statelessForward.z);
        const std::string presentedKey = vecKey(
            "two_hand_lab_presented_direction", presentedForward.x,
            presentedForward.y, presentedForward.z);
        Check(std::strstr(serialized, statelessKey.c_str()) != nullptr &&
                std::strstr(serialized, presentedKey.c_str()) != nullptr &&
                statelessKey != presentedKey,
            "Two-Hand Lab stateless versus presented stay distinguishable");
        jsonFloat = "\"two_hand_lab_temporal_error_deg\":" +
            expectFloat(solve.errorDeg);
        Check(std::strstr(serialized, jsonFloat.c_str()) != nullptr,
            "Two-Hand Lab temporal error serializes from the live state");
    }
}

void TestWeaponOrderEvents()
{
    // Gate off: zero diagnostic work, null sequence, no counters.
    Telemetry_TestForceWeaponEventsAccepting(false);
    Telemetry_TestResetWeaponEvents();
    Check(Telemetry_PublishWeaponEvent(1, 1, 1, 1, 1, 1, 1, 0, 0) == 0,
        "Weapon-order publish is inert while recording is off");
    Check(!Telemetry_WeaponEventsAccepting(),
        "Weapon-order gate reports closed while recording is off");
    const TelemetryWeaponEventCounters idle =
        Telemetry_GetWeaponEventCounters();
    Check(idle.enqueued == 0 && idle.droppedQueueFull == 0,
        "No weapon-order counters move while recording is off");

    // Forced gate: ordered field-preserving publish and pop.
    Telemetry_TestForceWeaponEventsAccepting(true);
    Check(Telemetry_WeaponEventsAccepting(),
        "Weapon-order gate reports open under test force");
    Telemetry_TestResetWeaponEvents();
    const uint64_t first = Telemetry_PublishWeaponEvent(
        static_cast<uint8_t>(WeaponOrderEventKind::CaptureProbeBegin),
        static_cast<uint8_t>(WeaponOrderEventStatus::NoObservation),
        5, 11, 13, 0x11111111u, 0x22222222u, 0, 0);
    const uint64_t second = Telemetry_PublishWeaponEvent(
        static_cast<uint8_t>(WeaponOrderEventKind::CaptureProbeResult),
        static_cast<uint8_t>(WeaponOrderEventStatus::Success),
        5, 11, 13, 0x11111111u, 0x22222222u, first, 0x99u);
    Check(first == 1 && second == 2,
        "Weapon-order sequences start at 1 and increase by claim order");
    TelemetryWeaponEvent popped{};
    Check(Telemetry_TestPopWeaponEvent(popped) && popped.sequence == 1 &&
        popped.kind ==
            static_cast<uint8_t>(WeaponOrderEventKind::CaptureProbeBegin) &&
        popped.title == 5 && popped.titleGeneration == 11 &&
        popped.preparedSerial == 13 &&
        popped.controlledUnit == 0x11111111u &&
        popped.primaryWeapon == 0x22222222u &&
        popped.tearGuard == popped.sequence &&
        popped.threadId == GetCurrentThreadId(),
        "Popped weapon-order event preserves every published field");
    Check(Telemetry_TestPopWeaponEvent(popped) && popped.sequence == 2 &&
        popped.aux0 == first && popped.aux1 == 0x99u,
        "Begin/result pairing survives through aux0");
    Check(!Telemetry_TestPopWeaponEvent(popped),
        "Weapon-order pop reports empty when drained");

    // Two concurrent producers: every sequence 1..N present exactly once,
    // no torn records (tearGuard always matches).
    Telemetry_TestResetWeaponEvents();
    constexpr int kProducerEvents = 200;
    std::atomic<int> producersDone{0};
    auto produce = [&](uint64_t mark) {
        for (int i = 0; i < kProducerEvents; ++i)
            Telemetry_PublishWeaponEvent(5, 1, 6, 7, 8, 9, 10, uint64_t(i),
                mark);
        producersDone.fetch_add(1);
    };
    std::thread left(produce, 0xA11CEu), right(produce, 0xB0Bu);
    left.join();
    right.join();
    Check(producersDone.load() == 2, "Both weapon-order producers finished");
    std::vector<char> seen(2 * kProducerEvents + 1, 0);
    int poppedCount = 0;
    bool torn = false, fieldOk = true;
    while (Telemetry_TestPopWeaponEvent(popped))
    {
        ++poppedCount;
        if (popped.sequence < 1 ||
            popped.sequence > uint64_t(2 * kProducerEvents) ||
            seen[popped.sequence])
            fieldOk = false;
        else
            seen[popped.sequence] = 1;
        if (popped.tearGuard != popped.sequence ||
            popped.title != 6 || popped.preparedSerial != 8)
            torn = true;
    }
    Check(poppedCount == 2 * kProducerEvents && fieldOk,
        "Concurrent weapon-order publish yields a complete contiguous sequence");
    Check(!torn, "No torn weapon-order records under concurrent publish");
    Check(Telemetry_TestWeaponEventDrops() == 0,
        "No drops when the concurrent burst fits the ring");

    // Overflow: drops are counted and reported, never silent.
    Telemetry_TestResetWeaponEvents();
    unsigned admitted = 0;
    for (uint32_t i = 0; i < kWeaponOrderEventQueueSlots + 50; ++i)
        admitted += Telemetry_PublishWeaponEvent(2, 0, 1, 1, 1, 1, 1, 0,
            0) != 0
            ? 1
            : 0;
    Check(admitted == kWeaponOrderEventQueueSlots,
        "Weapon-order publish admits exactly ring capacity before dropping");
    Check(Telemetry_TestWeaponEventDrops() == 50,
        "Weapon-order overflow is counted, never silent");

    // Serialization: fixed JSONL vocabulary for the analyser.
    Telemetry_TestResetWeaponEvents();
    (void)Telemetry_PublishWeaponEvent(
        static_cast<uint8_t>(WeaponOrderEventKind::FpWeaponCommit),
        static_cast<uint8_t>(WeaponOrderEventStatus::Success),
        4, 21, 22, 0x12345678u, 0x9ABCDEF0u, 0, 3u);
    Check(Telemetry_TestPopWeaponEvent(popped), "Event available to serialize");
    char text[1024]{};
    size_t written = 0;
    Check(Telemetry_TestSerializeWeaponEvent(popped, text, sizeof(text),
                written) &&
            std::string(text, written).find("\"type\":\"weapon_event\"") !=
                std::string::npos &&
            std::string(text, written).find("\"kind\":\"fp_weapon_commit\"") !=
                std::string::npos &&
            std::string(text, written).find("\"status\":\"success\"") !=
                std::string::npos,
        "Weapon-order serialization uses the fixed analyser vocabulary");

    // T-2 shot round trip: the engine-aim source ordinal and a Reach-shaped
    // payload (unknown slot, known barrel, seated-compact source) survive the
    // fixed record, the pop and the production serializer.
    Telemetry_TestResetWeaponEvents();
    {
        TelemetryShotPayload reachShot{};
        reachShot.origin[0] = 5.0f;
        reachShot.origin[1] = 0.0f;
        reachShot.origin[2] = -1.0f;
        reachShot.direction[2] = -1.0f;
        reachShot.engineAim[2] = 1.0f;
        reachShot.slot = kTelemetryShotIndexUnknown;
        reachShot.barrel = 1;
        reachShot.flags = 0x0002u;
        reachShot.engineAimSource = 4; // Reach seated compact fallback
        (void)Telemetry_PublishShotEvent(
            static_cast<uint8_t>(WeaponOrderEventStatus::Success), 3, 21, 22,
            0x12345678u, UINT32_MAX, reachShot);
        Check(Telemetry_TestPopWeaponEvent(popped) &&
                popped.kind ==
                    static_cast<uint8_t>(WeaponOrderEventKind::Shot) &&
                popped.shotEngineAimSource == 4,
            "Shot payload round-trips through the event record");
        char shotText[1024]{};
        size_t shotWritten = 0;
        Check(Telemetry_TestSerializeWeaponEvent(popped, shotText,
                    sizeof(shotText), shotWritten) &&
                std::string(shotText, shotWritten).find("\"kind\":\"shot\"") !=
                    std::string::npos &&
                std::string(shotText, shotWritten).find(
                    "\"shot_engine_aim_source\":4") != std::string::npos &&
                std::string(shotText, shotWritten).find("\"shot_slot\":-1") !=
                    std::string::npos &&
                std::string(shotText, shotWritten).find("\"shot_barrel\":1") !=
                    std::string::npos,
            "Shot serialization carries the engine aim source ordinal");
    }

    // Worker drain parity: the production ordered-serialization core with an
    // in-memory sink. Session 0 is the forced-acceptance publish session here.
    Telemetry_TestResetWeaponEvents();
    (void)Telemetry_PublishWeaponEvent(5, 1, 1, 2, 3, 4, 10, 0, 0);
    (void)Telemetry_PublishWeaponEvent(6, 1, 1, 2, 3, 4, 11, 0, 0);
    (void)Telemetry_PublishWeaponEvent(5, 1, 1, 2, 3, 4, 12, 0, 0);
    {
        std::vector<std::string> lines;
        bool drained = Telemetry_TestDrainWeaponEvents(0, lines);
        Check(drained && lines.size() == 3,
            "Worker drain serializes every published weapon event");
        Check(lines.size() == 3 &&
            lines[0].find("\"seq\":1") != std::string::npos &&
            lines[1].find("\"seq\":2") != std::string::npos &&
            lines[2].find("\"seq\":3") != std::string::npos &&
            lines[0].find("\"type\":\"weapon_event\"") != std::string::npos &&
            lines[0].find("\"session\":0") != std::string::npos,
            "Worker drain preserves global sequence order and session");
        Check(Telemetry_GetWeaponEventCounters().written == 3 &&
            !Telemetry_TestPopWeaponEvent(popped),
            "Worker drain advances the shared drain pointer exactly once");
    }

    // Cross-session record: never silently discarded; becomes a gap marker.
    Telemetry_TestResetWeaponEvents();
    (void)Telemetry_PublishWeaponEvent(5, 1, 1, 2, 3, 4, 10, 0, 0);
    {
        std::vector<std::string> lines;
        bool drained = Telemetry_TestDrainWeaponEvents(7, lines);
        Check(drained && lines.size() == 1 &&
            lines[0].find("\"type\":\"event_gap\"") != std::string::npos &&
            lines[0].find("\"first_missing_seq\":1") != std::string::npos,
            "Cross-session weapon event drains as an explicit gap marker");
        Check(Telemetry_GetWeaponEventCounters().sessionSkipped == 1,
            "Cross-session weapon events stay counted, never silent");
    }

    // Publish-side drops: records plus explicit gap lines, no worker stall.
    Telemetry_TestResetWeaponEvents();
    for (uint32_t i = 0; i < kWeaponOrderEventQueueSlots + 3; ++i)
        (void)Telemetry_PublishWeaponEvent(2, 0, 1, 1, 1, 1, 1, 0, 0);
    {
        std::vector<std::string> lines;
        bool drained = Telemetry_TestDrainWeaponEvents(0, lines);
        Check(drained && lines.size() == kWeaponOrderEventQueueSlots + 3,
            "Dropped publishes drain as records plus explicit gap lines");
        size_t gapLines = 0;
        for (const std::string& text : lines)
            if (text.find("\"type\":\"event_gap\"") != std::string::npos)
                ++gapLines;
        Check(gapLines == 3,
            "Exactly the dropped sequences become explicit gap markers");
    }

    // Claimed-frontier gap: a large drop cascade behind the claim frontier is
    // summarized without stalling, and the full coverage invariant holds.
    Telemetry_TestResetWeaponEvents();
    {
        constexpr uint32_t kFrontierExtra = kWeaponOrderEventQueueSlots + 6;
        const uint32_t total = kWeaponOrderEventQueueSlots + kFrontierExtra;
        for (uint32_t i = 0; i < total; ++i)
            (void)Telemetry_PublishWeaponEvent(2, 0, 1, 1, 1, 1, 1, 0, 0);
        std::vector<std::string> lines;
        for (int pass = 0; pass < 64; ++pass)
        {
            std::vector<std::string> part;
            const bool drained = Telemetry_TestDrainWeaponEvents(0, part);
            Check(drained, "Claimed-frontier drain never fails");
            if (part.empty())
                break;
            lines.insert(lines.end(), part.begin(), part.end());
            if (lines.size() > total * 2)
                break;
        }
        const TelemetryWeaponEventCounters counters =
            Telemetry_GetWeaponEventCounters();
        const uint64_t claimed = counters.enqueued + counters.droppedQueueFull;
        std::string why;
        Check(claimed == total, "Every frontier publish is claimed once");
        Check(VerifyWeaponEventCoverage(claimed, lines, why),
            "Claimed-frontier drain covers every sequence exactly once");
        if (!why.empty())
            std::fprintf(stderr, "frontier coverage: %s\n", why.c_str());
    }

    // WriteLine failure: a failed sink must not advance the drain pointer, and
    // the retry must serialize the pending event exactly once.
    Telemetry_TestResetWeaponEvents();
    (void)Telemetry_PublishWeaponEvent(1, 1, 1, 2, 3, 4, 10, 0, 0);
    (void)Telemetry_PublishWeaponEvent(1, 1, 1, 2, 3, 4, 11, 0, 0);
    (void)Telemetry_PublishWeaponEvent(1, 1, 1, 2, 3, 4, 12, 0, 0);
    {
        std::vector<std::string> lines;
        Check(!Telemetry_TestDrainWeaponEventsLimited(0, 1, lines) &&
            lines.size() == 1 &&
            lines[0].find("\"seq\":1") != std::string::npos,
            "A failed drain write does not advance past the failed event");
        std::vector<std::string> retry;
        Check(Telemetry_TestDrainWeaponEvents(0, retry) && retry.size() == 2 &&
            retry[0].find("\"seq\":2") != std::string::npos &&
            retry[1].find("\"seq\":3") != std::string::npos,
            "The retry resumes at the first unwritten event exactly once");
    }

    // Concurrent producers with the actual drain running concurrently: the
    // queue wraps/reuses slots and every claim stays serialized or gapped.
    Telemetry_TestResetWeaponEvents();
    {
        constexpr int kPerProducer = 700;
        std::atomic<int> producersLeft{2};
        std::atomic<bool> producersDone{false};
        const auto produce = [&producersLeft, &producersDone]() {
            for (int i = 0; i < kPerProducer; ++i)
                (void)Telemetry_PublishWeaponEvent(2, 1, 3, 4, 5, 6, 7,
                    uint64_t(i), 0);
            if (producersLeft.fetch_sub(1, std::memory_order_acq_rel) == 1)
                producersDone.store(true, std::memory_order_release);
        };
        std::thread left(produce), right(produce);
        std::vector<std::string> lines;
        bool sawMalformed = false;
        const uint64_t deadline = GetTickCount64() + 15000;
        for (;;)
        {
            std::vector<std::string> part;
            const bool drained = Telemetry_TestDrainWeaponEvents(0, part);
            Check(drained, "Concurrent drain never fails");
            for (const std::string& text : part)
                if (text.find("\"type\":\"weapon_event\"") !=
                        std::string::npos &&
                    (text.find("\"title\":3") == std::string::npos ||
                     text.find("\"prepared_serial\":5") ==
                         std::string::npos))
                    sawMalformed = true;
            lines.insert(lines.end(), part.begin(), part.end());
            if (producersDone.load(std::memory_order_acquire) &&
                part.empty())
                break;
            if (GetTickCount64() > deadline)
            {
                Check(false, "Concurrent producer/drain test times out");
                break;
            }
            Sleep(0);
        }
        left.join();
        right.join();
        for (;;)
        {
            std::vector<std::string> part;
            (void)Telemetry_TestDrainWeaponEvents(0, part);
            if (part.empty())
                break;
            lines.insert(lines.end(), part.begin(), part.end());
        }
        const TelemetryWeaponEventCounters counters =
            Telemetry_GetWeaponEventCounters();
        const uint64_t claimed = counters.enqueued + counters.droppedQueueFull;
        Check(claimed == uint64_t(kPerProducer) * 2,
            "Concurrent producers claim exactly one sequence each");
        Check(!sawMalformed,
            "No torn payload is serialized under concurrent publication");
        std::string why;
        Check(VerifyWeaponEventCoverage(claimed, lines, why),
            "Concurrent producer/drain preserves exact sequence coverage");
        if (!why.empty())
            std::fprintf(stderr, "concurrent coverage: %s\n", why.c_str());
    }

    // Producer/session handshake: a publisher parked in flight when the
    // admission generation changes aborts before claiming a sequence; the
    // in-flight counter drains so session finalization can wait for it.
    Telemetry_TestResetWeaponEvents();
    Telemetry_TestForceWeaponEventsAccepting(true);
    Telemetry_TestPauseWeaponEventProducer(true);
    {
        std::thread victim([]() {
            (void)Telemetry_PublishWeaponEvent(1, 1, 1, 1, 1, 1, 1, 0, 0);
        });
        const uint64_t deadline = GetTickCount64() + 5000;
        while (!Telemetry_TestWeaponEventProducerReachedPause() &&
               GetTickCount64() < deadline)
            Sleep(0);
        Check(Telemetry_TestWeaponEventProducerReachedPause(),
            "In-flight publisher reaches the handshake pause");
        Check(Telemetry_TestWeaponEventProducersInFlight() == 1,
            "An admitted publisher is counted in flight");
        Telemetry_TestBumpAdmissionGeneration(); // simulate Stop/Start race
        Telemetry_TestPauseWeaponEventProducer(false);
        victim.join();
        Check(Telemetry_TestWeaponEventProducersInFlight() == 0,
            "The producer handshake drains on every exit path");
        const TelemetryWeaponEventCounters counters =
            Telemetry_GetWeaponEventCounters();
        Check(counters.enqueued == 0 && counters.droppedQueueFull == 0,
            "A stale-session publisher aborts before claiming a sequence");
        std::vector<std::string> lines;
        Check(Telemetry_TestDrainWeaponEvents(0, lines) && lines.empty(),
            "Nothing is left unaccounted after an aborted publish");
    }
    Telemetry_TestResetAdmissionToken(); // restore the token-less test session

    // Stalled claim: a claimed-but-uncommitted sequence must never be gapped
    // or advanced while its producer is in flight, its slot must not be
    // reused (later claims beyond capacity are dropped), and the resumed
    // producer must commit into exact coverage.
    Telemetry_TestResetWeaponEvents();
    Telemetry_TestForceWeaponEventsAccepting(true);
    Telemetry_TestPauseWeaponEventCommit(true);
    {
        std::atomic<uint64_t> stalledSequence{0};
        std::thread stalled([&stalledSequence]() {
            stalledSequence.store(Telemetry_PublishWeaponEvent(6, 1, 1, 2, 3,
                4, 10, 0, 0), std::memory_order_release);
        });
        const uint64_t deadline = GetTickCount64() + 5000;
        while (!Telemetry_TestWeaponEventCommitReachedPause() &&
               GetTickCount64() < deadline)
            Sleep(0);
        Check(Telemetry_TestWeaponEventCommitReachedPause(),
            "Stalled producer reaches the post-claim pause");
        Check(Telemetry_TestWeaponEventProducersInFlight() == 1 &&
            Telemetry_GetWeaponEventCounters().enqueued == 0,
            "The claim is outstanding and not yet committed");
        {
            std::vector<std::string> lines;
            Check(Telemetry_TestDrainWeaponEvents(0, lines) && lines.empty(),
                "Drain emits no gap and does not advance past an in-flight claim");
        }
        // Queue pressure while the claim is stalled. The sequence that would
        // collide with the stalled slot is beyond capacity and must be
        // dropped, never written beneath the stalled producer.
        for (uint32_t i = 0; i < kWeaponOrderEventQueueSlots + 10; ++i)
            (void)Telemetry_PublishWeaponEvent(2, 0, 1, 1, 1, 1, 1, 0, 0);
        {
            std::vector<std::string> lines;
            Check(Telemetry_TestDrainWeaponEvents(0, lines) && lines.empty(),
                "No gap and no slot reuse while the stalled claim is in flight");
        }
        Telemetry_TestPauseWeaponEventCommit(false);
        stalled.join();
        Check(Telemetry_TestWeaponEventProducersInFlight() == 0,
            "Stalled claim completes and its producer receipt drains");
        Check(stalledSequence.load(std::memory_order_acquire) == 1,
            "The stalled producer committed its original sequence");
        std::vector<std::string> lines;
        for (int pass = 0; pass < 64; ++pass)
        {
            std::vector<std::string> part;
            Check(Telemetry_TestDrainWeaponEvents(0, part),
                "Resumed drain never fails");
            if (part.empty())
                break;
            lines.insert(lines.end(), part.begin(), part.end());
        }
        const TelemetryWeaponEventCounters counters =
            Telemetry_GetWeaponEventCounters();
        const uint64_t claimed = counters.enqueued + counters.droppedQueueFull;
        Check(claimed == uint64_t(kWeaponOrderEventQueueSlots) + 11,
            "Stalled claim and pressure claims are accounted exactly once");
        std::string why;
        Check(VerifyWeaponEventCoverage(claimed, lines, why),
            "Stalled-claim drain preserves exact sequence coverage");
        if (!why.empty())
            std::fprintf(stderr, "stalled coverage: %s\n", why.c_str());
        Check(!lines.empty() &&
            lines[0].find("\"seq\":1") != std::string::npos &&
            lines[0].find("\"type\":\"weapon_event\"") != std::string::npos,
            "The stalled record commits as a record rather than a gap");
    }

    // Final-drain frontier: a session's final drain must consume every claimed
    // sequence even when the missing-sequence cascade exceeds the normal
    // per-poll gap budget, and no undrained tail may survive into a
    // subsequent session.
    Telemetry_TestResetWeaponEvents();
    {
        constexpr uint32_t kFinalCommitted = kWeaponOrderEventQueueSlots;
        constexpr uint32_t kFinalDropped = 400;
        for (uint32_t i = 0; i < kFinalCommitted + kFinalDropped; ++i)
            (void)Telemetry_PublishWeaponEvent(2, 0, 1, 1, 1, 1, 1, 0, 0);
        const TelemetryWeaponEventCounters before =
            Telemetry_GetWeaponEventCounters();
        const uint64_t claimed = before.enqueued + before.droppedQueueFull;
        Check(claimed == kFinalCommitted + kFinalDropped,
            "Final-drain regression claims the full frontier");
        std::vector<std::string> lines;
        Check(Telemetry_TestFinalDrainWeaponEvents(0, lines),
            "Final drain completes");
        Check(lines.size() == claimed,
            "Final drain represents every claimed sequence exactly once");
        std::string why;
        Check(VerifyWeaponEventCoverage(claimed, lines, why),
            "Final drain leaves no unrepresented tail");
        if (!why.empty())
            std::fprintf(stderr, "final-drain coverage: %s\n", why.c_str());
        std::vector<std::string> leftover;
        Check(Telemetry_TestDrainWeaponEvents(7, leftover) &&
                leftover.empty(),
            "No undrained weapon-event tail carries into a subsequent session");
    }
    Telemetry_TestForceWeaponEventsAccepting(false);
    Telemetry_TestResetWeaponEvents();
}

void TestShotsVsReticleSerialization()
{
    char serialized[32768]{};
    size_t serializedBytes = 0;
    const TelemetryFrame frame = EvidenceFrame(0);
    Check(Telemetry_TestSerializeFrame(
            frame, serialized, sizeof(serialized), serializedBytes),
        "Shots-vs-reticle frame serializes inside the bounded buffer");
    Check(std::strstr(serialized, "\"presented_aim_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"presented_aim_forward\":[0,0,-1]") != nullptr,
        "Presented aim validity and forward serialize explicitly");
    Check(std::strstr(serialized,
                "\"reticle_presented_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_serial\":41") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_sample_ms\":1234567") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_support_epoch\":5") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_support_trusted\":true") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_orientation\":[0,0,0,1]") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_position\":[0.5,-0.25,4]") != nullptr,
        "Consumer-visible reticle pose travels with its own lagging serial");
    Check(std::strstr(serialized, "\"engine_aim_valid\":true") != nullptr &&
            std::strstr(serialized, "\"engine_aim_source\":1") != nullptr &&
            std::strstr(serialized,
                "\"engine_aim_forward\":[1,0,0]") != nullptr &&
            std::strstr(serialized, "\"engine_aim_serial\":0") != nullptr &&
            std::strstr(serialized,
                "\"engine_aim_sample_ms\":0") != nullptr &&
            std::strstr(serialized,
                "\"engine_aim_pitch_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"engine_aim_pitch_deg\":0") != nullptr,
        "Engine aim feedback serializes source, latest-only serial and pitch coupling");
    Check(std::strstr(serialized,
                "\"engine_camera_base_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"engine_camera_base_position\":[10,20,30]") != nullptr &&
            std::strstr(serialized,
                "\"engine_camera_eye_valid\":true") != nullptr &&
            std::strstr(serialized,
                "\"engine_camera_eye_position\":[11,21,31]") != nullptr &&
            std::strstr(serialized,
                "\"engine_camera_source\":1") != nullptr &&
            std::strstr(serialized,
                "\"engine_camera_serial\":0") != nullptr &&
            std::strstr(serialized,
                "\"world_scale_valid\":true") != nullptr &&
            std::strstr(serialized, "\"world_scale\":") != nullptr &&
            std::strstr(serialized, "\"dual_active\":true") != nullptr,
        "Engine camera truth, authoritative scale and dual predicate serialize explicitly");
    Check(std::strstr(serialized,
                "\"aim_stabilization\":0.479999989") != nullptr &&
            std::strstr(serialized,
                "\"crosshair_distance_m\":41") != nullptr &&
            std::strstr(serialized,
                "\"crosshair_size_deg\":10.1000004") != nullptr &&
            std::strstr(serialized, "\"crosshair\":true") != nullptr &&
            std::strstr(serialized, "\"kill_reticle\":true") != nullptr,
        "Effective reticle/steering settings serialize explicitly");
    Check(std::strstr(serialized, "\"weapon_buttons\":17") != nullptr &&
            std::strstr(serialized,
                "\"weapon_pulse_until_ms\":7654321") != nullptr &&
            std::strstr(serialized, "\"weapon_generation\":3") != nullptr,
        "Weapon gesture pad state serializes explicitly");

    // Finite handling: a non-finite engine vector serializes as JSON null
    // (the worker never emits NaN/Infinity); validity still travels
    // separately so the null is interpretable, not silent.
    TelemetryFrame nonFinite = frame;
    nonFinite.engineAimForward = {NAN, 0.0f, 0.0f};
    Check(Telemetry_TestSerializeFrame(nonFinite, serialized,
            sizeof(serialized), serializedBytes) &&
            std::strstr(serialized,
                "\"engine_aim_forward\":[null,0,0]") != nullptr &&
            std::strstr(serialized,
                "\"engine_aim_valid\":true") != nullptr,
        "Non-finite engine vectors serialize as JSON null without losing validity");

    // Validity coupling: an invalid observation keeps zero payloads (never
    // stale, never non-finite), while the source still names the attributed
    // publication (Halo 2 absent here).
    TelemetryFrame absent = frame;
    absent.presentedAimValid = false;
    absent.presentedAimForward = {};
    absent.reticlePresentedValid = false;
    absent.reticlePresentedSerial = 0;
    absent.reticlePresentedSampleMs = 0;
    absent.reticlePresentedSupportEpoch = 0;
    absent.reticlePresentedSupportTrusted = false;
    // Note: TelemetryQuat{} defaults w to 1, so the zeroed invalid pose
    // needs explicit zeros (matching the recorder's invalid convention).
    absent.reticlePresentedOrientation =
        TelemetryQuat{0.0f, 0.0f, 0.0f, 0.0f};
    absent.reticlePresentedPosition = {};
    absent.engineAimValid = false;
    absent.engineAimSource = 8;
    absent.engineAimForward = {};
    absent.engineAimSerial = 0;
    absent.engineAimSampleMs = 0;
    absent.engineAimPitchValid = false;
    absent.engineAimPitchDeg = 0.0f;
    absent.engineCameraBaseValid = false;
    absent.engineCameraBasePosition = {};
    absent.engineCameraEyeValid = false;
    absent.engineCameraEyePosition = {};
    absent.engineCameraSource = 0;
    absent.engineCameraSerial = 0;
    absent.worldScaleValid = false;
    absent.worldScale = 0.0f;
    absent.dualActive = false;
    Check(Telemetry_TestSerializeFrame(absent, serialized, sizeof(serialized),
            serializedBytes) &&
            std::strstr(serialized,
                "\"presented_aim_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"presented_aim_forward\":[0,0,0]") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_serial\":0") != nullptr &&
            std::strstr(serialized,
                "\"reticle_presented_orientation\":[0,0,0,0]") != nullptr &&
            std::strstr(serialized,
                "\"engine_aim_valid\":false") != nullptr &&
            std::strstr(serialized, "\"engine_aim_source\":8") != nullptr &&
            std::strstr(serialized,
                "\"engine_aim_forward\":[0,0,0]") != nullptr &&
            std::strstr(serialized,
                "\"engine_camera_eye_valid\":false") != nullptr &&
            std::strstr(serialized,
                "\"world_scale_valid\":false") != nullptr &&
            std::strstr(serialized, "\"dual_active\":false") != nullptr,
        "Invalid shots-vs-reticle observations keep zero payloads with an attributed source");
}

void TestPersistentSupportGripSerialization()
{
    char serialized[32768]{};
    size_t serializedBytes = 0;
    const TelemetryFrame frame = EvidenceFrame(42);
    Check(Telemetry_TestSerializeFrame(
            frame, serialized, sizeof(serialized), serializedBytes),
        "Persistent-grip frame serializes inside the bounded buffer");
    Check(std::strstr(serialized,
                "\"persistent_support_grip_configured\":true") != nullptr &&
            std::strstr(serialized,
                "\"persistent_support_grip_applicable\":true") != nullptr &&
            std::strstr(serialized,
                "\"support_relationship_readable\":true") != nullptr &&
            std::strstr(serialized,
                "\"support_relationship_engaged\":true") != nullptr &&
            std::strstr(serialized,
                "\"support_solve_trusted\":true") != nullptr &&
            std::strstr(serialized,
                "\"support_force_one_hand\":false") != nullptr,
        "Persistent-grip provenance bits serialize explicitly");
    Check(std::strstr(serialized, "\"support_epoch\":5") != nullptr &&
            std::strstr(serialized,
                "\"support_solve_serial\":41") != nullptr,
        "Persistent-grip epoch and solve serial serialize as integers");
    // The stamped solve serial is the frame's own serial minus one at capture;
    // it travels as its own value and is never rewritten to prepared_serial.
    Check(std::strstr(serialized, "\"prepared_serial\":42") != nullptr,
        "Persistent-grip serial lag is not rewritten to the frame identity");

    // Epoch sentinel: all-ones means "the relationship could not be read" and
    // must survive the wire as a plain unsigned integer (never a float, never
    // a string, never truncated).
    TelemetryFrame unknownEpoch = frame;
    unknownEpoch.supportEpoch = UINT64_MAX;
    unknownEpoch.supportRelationshipReadable = false;
    unknownEpoch.supportRelationshipEngaged = false;
    unknownEpoch.supportSolveTrusted = false;
    unknownEpoch.supportForceOneHand = true;
    Check(Telemetry_TestSerializeFrame(unknownEpoch, serialized,
            sizeof(serialized), serializedBytes) &&
            std::strstr(serialized,
                "\"support_epoch\":18446744073709551615") != nullptr &&
            std::strstr(serialized,
                "\"support_force_one_hand\":true") != nullptr,
        "Unknown-epoch sentinel round-trips as an exact unsigned integer");

    // Absence/zero convention: with the feature off the qualification
    // short-circuits and every carried field reads its frozen zero/false
    // default, never a stale receipt.
    TelemetryFrame featureOff = frame;
    featureOff.persistentSupportGripConfigured = false;
    featureOff.persistentSupportGripApplicable = false;
    featureOff.supportRelationshipReadable = false;
    featureOff.supportRelationshipEngaged = false;
    featureOff.supportEpoch = 0;
    featureOff.supportSolveTrusted = false;
    featureOff.supportForceOneHand = false;
    featureOff.supportSolveSerial = 0;
    Check(Telemetry_TestSerializeFrame(featureOff, serialized,
            sizeof(serialized), serializedBytes) &&
            std::strstr(serialized,
                "\"persistent_support_grip_configured\":false") != nullptr &&
            std::strstr(serialized,
                "\"persistent_support_grip_applicable\":false") != nullptr &&
            std::strstr(serialized,
                "\"support_relationship_readable\":false") != nullptr &&
            std::strstr(serialized,
                "\"support_relationship_engaged\":false") != nullptr &&
            std::strstr(serialized, "\"support_epoch\":0") != nullptr &&
            std::strstr(serialized,
                "\"support_solve_trusted\":false") != nullptr &&
            std::strstr(serialized,
                "\"support_force_one_hand\":false") != nullptr &&
            std::strstr(serialized, "\"support_solve_serial\":0") != nullptr,
        "A feature-off frame reads the frozen zero/false provenance defaults");
}

void TestShotEvents()
{
    // Disabled gate: with recording off the shot producer returns on one
    // admission load without touching any other state and publishes nothing.
    Telemetry_TestForceWeaponEventsAccepting(false);
    Telemetry_TestResetWeaponEvents();
    TelemetryShotPayload blocked{};
    blocked.origin[0] = 1.0f;
    Check(Telemetry_PublishShotEvent(
              static_cast<uint8_t>(WeaponOrderEventStatus::Success), 3, 7,
              1234, 0xAABBCCDDu, 0x11223344u, blocked) == 0,
        "Shot publish is inert while recording is off");
    const TelemetryWeaponEventCounters disabled =
        Telemetry_GetWeaponEventCounters();
    Check(disabled.enqueued == 0 && disabled.droppedQueueFull == 0 &&
            disabled.written == 0,
        "No shot accounting moves while recording is off");

    // Field-preserving round trip through the fixed transport.
    Telemetry_TestForceWeaponEventsAccepting(true);
    Telemetry_TestResetWeaponEvents();
    TelemetryShotPayload payload{};
    payload.origin[0] = 1.5f;
    payload.origin[1] = -2.25f;
    payload.origin[2] = 3.0f;
    payload.direction[0] = 0.0f;
    payload.direction[1] = 0.5f;
    payload.direction[2] = -0.8660254f;
    payload.engineAim[0] = 4.0f;
    payload.engineAim[1] = 5.0f;
    payload.engineAim[2] = 6.0f;
    payload.reticleDirection[0] = -0.25f;
    payload.reticleDirection[1] = 0.0f;
    payload.reticleDirection[2] = 0.9682458f;
    payload.slot = 1;
    payload.barrel = 0;
    payload.flags = 0x00A5u;
    const uint64_t shotSequence = Telemetry_PublishShotEvent(
        static_cast<uint8_t>(WeaponOrderEventStatus::Success), 3, 7, 1234,
        0xAABBCCDDu, 0x11223344u, payload);
    Check(shotSequence == 1,
        "Shot events claim the same process-wide sequence as other events");
    TelemetryWeaponEvent popped{};
    Check(Telemetry_TestPopWeaponEvent(popped) &&
            popped.kind == static_cast<uint8_t>(WeaponOrderEventKind::Shot) &&
            popped.status ==
                static_cast<uint8_t>(WeaponOrderEventStatus::Success) &&
            popped.title == 3 && popped.titleGeneration == 7 &&
            popped.preparedSerial == 1234 &&
            popped.controlledUnit == 0xAABBCCDDu &&
            popped.primaryWeapon == 0x11223344u &&
            popped.aux0 == 0 && popped.aux1 == 0 &&
            popped.shotOrigin[0] == payload.origin[0] &&
            popped.shotOrigin[1] == payload.origin[1] &&
            popped.shotOrigin[2] == payload.origin[2] &&
            popped.shotDirection[0] == payload.direction[0] &&
            popped.shotDirection[1] == payload.direction[1] &&
            popped.shotDirection[2] == payload.direction[2] &&
            popped.shotEngineAim[0] == payload.engineAim[0] &&
            popped.shotEngineAim[1] == payload.engineAim[1] &&
            popped.shotEngineAim[2] == payload.engineAim[2] &&
            popped.shotReticleDirection[0] == payload.reticleDirection[0] &&
            popped.shotReticleDirection[2] == payload.reticleDirection[2] &&
            popped.shotSlot == 1 && popped.shotBarrel == 0 &&
            popped.shotFlags == 0x00A5u &&
            popped.tearGuard == popped.sequence &&
            popped.threadId == GetCurrentThreadId(),
        "Popped shot event preserves every fixed payload field");

    // Wire vocabulary: the shot keys exist only for kind shot, use the
    // documented names, and map the unknown sentinel to -1.
    char shotText[2048]{};
    size_t shotWritten = 0;
    Check(Telemetry_TestSerializeWeaponEvent(popped, shotText,
              sizeof(shotText), shotWritten),
        "A shot event serializes inside the bounded buffer");
    const std::string shotLine(shotText, shotWritten);
    Check(shotLine.find("\"type\":\"weapon_event\"") != std::string::npos &&
            shotLine.find("\"kind\":\"shot\"") != std::string::npos &&
            shotLine.find("\"status\":\"success\"") != std::string::npos &&
            shotLine.find("\"shot_origin\":[1.5,-2.25,3]") !=
                std::string::npos &&
            shotLine.find("\"shot_direction\":[0,0.5,-0.866025388]") !=
                std::string::npos &&
            shotLine.find("\"shot_engine_aim\":[4,5,6]") !=
                std::string::npos &&
            shotLine.find("\"shot_reticle_direction\":[-0.25,0,0.968245804]") !=
                std::string::npos &&
            shotLine.find("\"shot_slot\":1") != std::string::npos &&
            shotLine.find("\"shot_barrel\":0") != std::string::npos &&
            shotLine.find("\"shot_flags\":165") != std::string::npos,
        "Shot serialization uses the fixed analyser vocabulary");
    TelemetryWeaponEvent unknownIndex{};
    unknownIndex.kind = static_cast<uint8_t>(WeaponOrderEventKind::Shot);
    unknownIndex.shotSlot = kTelemetryShotIndexUnknown;
    unknownIndex.shotBarrel = kTelemetryShotIndexUnknown;
    Check(Telemetry_TestSerializeWeaponEvent(unknownIndex, shotText,
              sizeof(shotText), shotWritten) &&
            std::string(shotText, shotWritten).find("\"shot_slot\":-1") !=
                std::string::npos &&
            std::string(shotText, shotWritten).find("\"shot_barrel\":-1") !=
                std::string::npos,
        "An unknown shot slot/barrel serializes as -1, never as a fake identity");
    TelemetryWeaponEvent nonShot{};
    nonShot.kind = static_cast<uint8_t>(
        WeaponOrderEventKind::CaptureProbeResult);
    Check(Telemetry_TestSerializeWeaponEvent(nonShot, shotText,
              sizeof(shotText), shotWritten) &&
            std::string(shotText, shotWritten).find("\"shot_origin\"") ==
                std::string::npos &&
            std::string(shotText, shotWritten).find("\"shot_slot\"") ==
                std::string::npos &&
            std::string(shotText, shotWritten).find("\"shot_flags\"") ==
                std::string::npos,
        "Existing event kinds serialize without the shot payload keys");

    // Non-finite safety: the serializer never emits NaN/Infinity. Producers
    // suppress a non-finite final ray before publishing; this is the wire's
    // last-resort convention.
    TelemetryWeaponEvent nonFinite{};
    nonFinite.kind = static_cast<uint8_t>(WeaponOrderEventKind::Shot);
    nonFinite.shotOrigin[0] = std::numeric_limits<float>::quiet_NaN();
    nonFinite.shotOrigin[1] = std::numeric_limits<float>::infinity();
    nonFinite.shotOrigin[2] = 2.0f;
    nonFinite.shotDirection[2] = -std::numeric_limits<float>::infinity();
    Check(Telemetry_TestSerializeWeaponEvent(nonFinite, shotText,
              sizeof(shotText), shotWritten) &&
            std::string(shotText, shotWritten).find(
                "\"shot_origin\":[null,null,2]") != std::string::npos &&
            std::string(shotText, shotWritten).find(
                "\"shot_direction\":[0,0,null]") != std::string::npos,
        "Non-finite shot vectors serialize as JSON null, never NaN/Infinity");

    // Admission/gap semantics are unchanged: a cross-session shot claim is
    // counted and drained as an explicit gap marker.
    Telemetry_TestResetWeaponEvents();
    (void)Telemetry_PublishShotEvent(
        static_cast<uint8_t>(WeaponOrderEventStatus::Success), 3, 7, 1234,
        0xAABBCCDDu, 0x11223344u, payload);
    {
        std::vector<std::string> lines;
        Check(Telemetry_TestDrainWeaponEvents(7, lines) && lines.size() == 1 &&
                lines[0].find("\"type\":\"event_gap\"") !=
                    std::string::npos &&
                lines[0].find("\"first_missing_seq\":1") !=
                    std::string::npos,
            "A cross-session shot claim drains as an explicit gap marker");
        Check(Telemetry_GetWeaponEventCounters().sessionSkipped == 1,
            "Cross-session shot claims stay counted, never silent");
    }

    // Queue pressure: records plus explicit gaps, exact coverage.
    Telemetry_TestResetWeaponEvents();
    for (uint32_t i = 0; i < kWeaponOrderEventQueueSlots + 3; ++i)
        (void)Telemetry_PublishShotEvent(
            static_cast<uint8_t>(WeaponOrderEventStatus::Success), 3, 7, 1234,
            0xAABBCCDDu, 0x11223344u, payload);
    {
        std::vector<std::string> lines;
        Check(Telemetry_TestDrainWeaponEvents(0, lines) &&
                lines.size() == kWeaponOrderEventQueueSlots + 3,
            "Shot overflow drains as records plus explicit gap lines");
        size_t gapLines = 0, shotLines = 0;
        for (const std::string& line : lines)
        {
            if (line.find("\"type\":\"event_gap\"") != std::string::npos)
                ++gapLines;
            else if (line.find("\"kind\":\"shot\"") != std::string::npos)
                ++shotLines;
        }
        Check(gapLines == 3 && shotLines == kWeaponOrderEventQueueSlots,
            "Exactly the dropped shot sequences become explicit gap markers");
        std::string why;
        Check(VerifyWeaponEventCoverage(kWeaponOrderEventQueueSlots + 3, lines,
                  why),
            "Shot events preserve the transport's exact sequence coverage");
        if (!why.empty())
            std::fprintf(stderr, "shot coverage: %s\n", why.c_str());
    }

    Telemetry_TestForceWeaponEventsAccepting(false);
    Telemetry_TestResetWeaponEvents();
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 4 && std::wcscmp(argv[1], L"--analyser-smoke") == 0)
        return RunAnalyserSmoke(argv[2], argv[3]);
    Check(argc == 2, "Telemetry test receives a JSONL validation output path");
    TestRing();
    TestConcurrentRing();
    TestSerializerHelpers();
    TestTwoHandLabLiveSerialization();
    TestShotsVsReticleSerialization();
    TestPersistentSupportGripSerialization();
    TestWeaponOrderEvents();
    TestShotEvents();
    TestAnalyserLaunchHelpers();
    if (argc == 2)
        TestLifecycleAndAccounting(argv[1]);
    std::printf(
        "Telemetry recorder: %u checks, %u failures, TelemetryFrame %zu bytes, ring payload %zu bytes\n",
        checks, failures, sizeof(TelemetryFrame),
        sizeof(TelemetryFrame) * static_cast<size_t>(kTelemetryQueueSlots));
    return failures ? 1 : 0;
}
