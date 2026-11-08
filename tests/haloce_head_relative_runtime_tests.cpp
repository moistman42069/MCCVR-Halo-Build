// Halo CE, the motion tracker follows the head: the PRODUCTION lane (src/dll/haloce_head_relative.cpp) run against
// a synthetic image built from the header's own patterns at addresses that are NOT retail's. What runs here as
// shipped: the install proof (sig::Find over the image, the function table, the import thunks), the detour and its
// one write, the report (every line really formatted, so a LOG whose conversions do not match its arguments shows),
// the retirement, and what a reinstall keeps. RETAIL-DERIVED LEAD, NOT HCEEK-VERIFIED: nothing here is evidence
// about the game; tools/verify-ce-head-relative.py is what reads the retail file.
// It never maps game code, patches executable memory or opens MCC. The platform calls are test doubles.
// Built twice by CMake: the player flavour and the dev flavour (HALOMCCVR_DEV_DIAGNOSTICS=1, the 2-second line).
// `--print` prints every line the lane logs, as formatted, in the order this file provokes them.
#include <windows.h>
#include <MinHook.h>
MH_STATUS WINAPI TestCreate(LPVOID, LPVOID, LPVOID*);
MH_STATUS WINAPI TestEnable(LPVOID);
extern "C" MH_STATUS WINAPI TestDisable(LPVOID);
MH_STATUS WINAPI TestRemove(LPVOID);
BOOL WINAPI TestPin(DWORD, LPCWSTR, HMODULE*);
BOOL WINAPI TestFree(HMODULE);
ULONGLONG WINAPI TestTicks();
#define MH_CreateHook TestCreate
#define MH_EnableHook TestEnable
#define MCCVR_DisableHookForRetirement TestDisable
#define MH_RemoveHook TestRemove
#define GetModuleHandleExW TestPin
#define FreeLibrary TestFree
#define GetTickCount64 TestTicks
#include "../src/dll/haloce_head_relative.cpp"
#undef MH_CreateHook
#undef MH_EnableHook
#undef MCCVR_DisableHookForRetirement
#undef MH_RemoveHook
#undef GetModuleHandleExW
#undef FreeLibrary
#undef GetTickCount64
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool Near(float a, float b, float tol = 1.0e-4f) { return std::fabs(a - b) <= tol; }
using head_relative::kDegToRad;

// ---- the platform doubles ---------------------------------------------------------------------------------------
unsigned createCalls{}, enableCalls{}, disableCalls{}, removeCalls{}, pinCalls{}, freeCalls{}, unwindFailures{};
bool createFailure{}, enableFailure{}, disableFailure{}, quiescenceFailure{}, pinFailure{};
void* createdTarget{};
void* createdDetour{};
ULONGLONG ticks = 100000;
uint64_t nowUs = 50000000;
GameTitle title = GameTitle::HaloCE;
uint32_t currentGeneration = 7;
bool stateReadable = true;
HaloCELocalPlayerState localState{};
std::vector<std::string> lines;
bool printLines{};   // --print: every line the lane logs, as it is formatted

// ---- the synthetic image ----------------------------------------------------------------------------------------
// The nine patterns, two constants, two import thunks and two pointer slots, anywhere but where retail has them.
constexpr size_t kImageBytes = 0x10000;
constexpr int64_t kDrawAt = 0x1000, kUpdateAt = 0x2000, kStoreAt = 0x2210, kRenderAt = 0x3000, kDatumAt = 0x3100,
                  kUnitCallAt = 0x3180, kPointCallAt = 0x3220, kPlacerAt = 0x4000, kFacingAt = 0x500C,
                  kCosThunkAt = 0x6000, kSinThunkAt = 0x6010, kCosSlotAt = 0x8000, kSinSlotAt = 0x8008,
                  kHalfPiAt = 0x8100, kMaskAt = 0x8110, kGlobalsSlotAt = 0x9000, kControlSlotAt = 0x9008,
                  kUnwindAt = 0xA000;
// The layout the patterns' operands are given in this image.
constexpr uint32_t kPlayerBytes = 0x568, kDatumBytes = 0x84, kActiveAt = 0x15A4, kHeadingAt = 0x7C,
                   kControlBytes = 0x38, kYawAt = 0x17C;
uint8_t* image{};
uintptr_t base{};
RUNTIME_FUNCTION functionTable[4]{};
bool tableAdded{};
// The game's memory the lane reads, and the one float it writes: the sensor globals and player control.
uint8_t sensor[kActiveAt + 4]{};
uint8_t control[0x400]{};
float TestCos(float x) { return std::cos(x); }
float TestSin(float x) { return std::sin(x); }

template <typename T> void PutValue(int64_t rva, T value) { std::memcpy(image + rva, &value, sizeof(value)); }
void Place(int64_t rva, const char* pattern)
{
    size_t count = 0;
    for (const char* at = pattern; *at;)
    {
        while (*at == ' ')
            ++at;
        if (!*at)
            break;
        if (*at == '?')
        {
            image[rva + count++] = 0;
            while (*at == '?')
                ++at;
            continue;
        }
        char* end = nullptr;
        image[rva + count++] = static_cast<uint8_t>(std::strtoul(at, &end, 16));
        at = end;
    }
}
void PutBranch(int64_t site, size_t opcode, int64_t to)
{
    PutValue<int32_t>(site + static_cast<int64_t>(opcode) + 1,
                      static_cast<int32_t>(to - (site + static_cast<int64_t>(opcode) + 5)));
}
void PutRip(int64_t site, size_t disp, size_t next, int64_t to)
{
    PutValue<int32_t>(site + static_cast<int64_t>(disp), static_cast<int32_t>(to - (site + static_cast<int64_t>(next))));
}
// The image's function table: the draw entry, the update, the render and the placer begin functions.
void RegisterFunctions(bool withRender = true)
{
    if (tableAdded)
        RtlDeleteFunctionTable(functionTable);
    DWORD count = 0;
    const auto add = [&](int64_t begin, int64_t bytes) {
        functionTable[count].BeginAddress = static_cast<DWORD>(begin);
        functionTable[count].EndAddress = static_cast<DWORD>(begin + bytes);
        functionTable[count].UnwindData = static_cast<DWORD>(kUnwindAt);
        ++count;
    };
    add(kDrawAt, static_cast<int64_t>(kDrawBytes));
    add(kUpdateAt, 0x400);
    if (withRender)
        add(kRenderAt, 0x400);
    add(kPlacerAt, 0x200);
    tableAdded = RtlAddFunctionTable(functionTable, count, base) != FALSE;
}
void BuildImage()
{
    if (!image)
    {
        image = static_cast<uint8_t*>(VirtualAlloc(nullptr, kImageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        base = reinterpret_cast<uintptr_t>(image);
    }
    std::memset(image, 0, kImageBytes);
    Place(kDrawAt, kTrackerDrawPattern);
    PutBranch(kDrawAt, kDrawCallUpdate, kUpdateAt);
    PutBranch(kDrawAt, kDrawJumpRender, kRenderAt);
    Place(kUpdateAt, kTrackerUpdateHeadPattern);
    PutValue<uint32_t>(kUpdateAt + kUpdatePlayerStride, kPlayerBytes);
    image[kUpdateAt + kUpdateLocalSlot] = 0x28;
    PutRip(kUpdateAt, kUpdateGlobalsDisp, kUpdateGlobalsNext, kGlobalsSlotAt);
    PutValue<uint32_t>(kUpdateAt + kUpdateFlagDisp, kActiveAt + 2);
    PutValue<uint32_t>(kUpdateAt + kUpdateActiveDisp, kActiveAt);
    PutValue<uint32_t>(kUpdateAt + kUpdateDatumStride, kDatumBytes);
    Place(kStoreAt, kTrackerStorePattern);
    image[kStoreAt + kStoreLocalSlot] = 0x28;
    image[kStoreAt + kStoreControlStride] = static_cast<uint8_t>(kControlBytes);
    PutRip(kStoreAt, kStoreControlDisp, kStoreControlNext, kControlSlotAt);
    PutValue<uint32_t>(kStoreAt + kStoreYawDisp, kYawAt);
    PutRip(kStoreAt, kStoreHalfPiDisp, kStoreHalfPiNext, kHalfPiAt);
    image[kStoreAt + kStoreHeadingDisp] = static_cast<uint8_t>(kHeadingAt);
    Place(kRenderAt, kTrackerRenderHeadPattern);
    PutValue<uint32_t>(kRenderAt + kRenderPlayerStride, kPlayerBytes);
    PutRip(kRenderAt, kRenderGlobalsDisp, kRenderGlobalsNext, kGlobalsSlotAt);
    Place(kDatumAt, kTrackerRenderDatumPattern);
    PutRip(kDatumAt, kDatumGlobalsDisp, kDatumGlobalsNext, kGlobalsSlotAt);
    PutValue<uint32_t>(kDatumAt + kDatumActiveDisp, kActiveAt);
    PutValue<uint32_t>(kDatumAt + kDatumStride, kDatumBytes);
    Place(kUnitCallAt, kTrackerUnitCallPattern);
    PutBranch(kUnitCallAt, kUnitCall, kPlacerAt);
    Place(kPointCallAt, kTrackerPointCallPattern);
    PutBranch(kPointCallAt, kPointCall, kPlacerAt);
    Place(kPlacerAt, kTrackerPlacerPattern);
    image[kPlacerAt + kPlacerHeadingDisp] = static_cast<uint8_t>(kHeadingAt);
    PutRip(kPlacerAt, kPlacerMaskDisp, kPlacerMaskNext, kMaskAt);
    PutBranch(kPlacerAt, kPlacerCallCos, kCosThunkAt);
    PutBranch(kPlacerAt, kPlacerCallSin, kSinThunkAt);
    Place(kFacingAt, kFacingReaderPattern);
    PutRip(kFacingAt, kFacingControlDisp, kFacingControlNext, kControlSlotAt);
    PutValue<uint32_t>(kFacingAt + kFacingYaw, kYawAt);
    image[kFacingAt + kFacingStride] = static_cast<uint8_t>(kControlBytes);
    // The two import thunks: jmp [rip + slot].
    image[kCosThunkAt] = 0xFF;
    image[kCosThunkAt + 1] = 0x25;
    PutValue<int32_t>(kCosThunkAt + 2, static_cast<int32_t>(kCosSlotAt - (kCosThunkAt + 6)));
    image[kSinThunkAt] = 0xFF;
    image[kSinThunkAt + 1] = 0x25;
    PutValue<int32_t>(kSinThunkAt + 2, static_cast<int32_t>(kSinSlotAt - (kSinThunkAt + 6)));
    PutValue<uintptr_t>(kCosSlotAt, reinterpret_cast<uintptr_t>(&TestCos));
    PutValue<uintptr_t>(kSinSlotAt, reinterpret_cast<uintptr_t>(&TestSin));
    PutValue<float>(kHalfPiAt, kHalfPi);
    PutValue<uint32_t>(kMaskAt, 0x80000000u);
    PutValue<uintptr_t>(kGlobalsSlotAt, reinterpret_cast<uintptr_t>(sensor));
    PutValue<uintptr_t>(kControlSlotAt, reinterpret_cast<uintptr_t>(control));
    image[kUnwindAt] = 1;   // UNWIND_INFO: version 1, no flags, no codes
    RegisterFunctions();
}

// ---- the game, as far as the lane can see it ----------------------------------------------------------------------
int16_t& Active() { return *reinterpret_cast<int16_t*>(sensor + kActiveAt); }
float& SensorHeading(int local, int datum)
{
    return *reinterpret_cast<float*>(sensor + local * kPlayerBytes + datum * kDatumBytes + kHeadingAt);
}
float& ControlYaw(int local) { return *reinterpret_cast<float*>(control + local * kControlBytes + kYawAt); }
// What the game's store computes for a yaw: one single-precision add of the yaw and pi/2.
float GameField(float yawDeg)
{
    const volatile float yaw = WrapYaw(yawDeg * kDegToRad);
    const volatile float stored = yaw + kHalfPi;
    return stored;
}
// The update: the active datum's heading = the facing yaw + pi/2.
void GameUpdate(int local, float yawDeg)
{
    ControlYaw(local) = WrapYaw(yawDeg * kDegToRad);
    SensorHeading(local, Active()) = GameField(yawDeg);
}
// The tick: the next datum becomes the active one, as a copy of the previous one (local player 0's).
void GameTick()
{
    const int previous = Active();
    Active() = static_cast<int16_t>((previous + 1) % 10);
    std::memcpy(sensor + Active() * kDatumBytes, sensor + previous * kDatumBytes, kDatumBytes);
}
// The render (the "original" the detour forwards to): what the placer would read, and what it was called with.
unsigned renderCalls{};
int renderLocal{};
unsigned renderMultiplayer{};
const void* renderCentre{};
float renderSaw{};
void __fastcall NativeRender(int16_t localPlayer, uint8_t inMultiplayer, const void* centre)
{
    ++renderCalls;
    renderLocal = localPlayer;
    renderMultiplayer = inMultiplayer;
    renderCentre = centre;
    const bool readable = localPlayer >= 0 && localPlayer < 2 && Active() >= 0 && Active() < 10;
    renderSaw = readable ? SensorHeading(localPlayer, Active()) : 0.0f;
}
// The render path's publication for one frame: the stock centre camera along `frameYawDeg`, the head turned
// `headLeftDeg` to the left of the room's forward.
void Publish(float frameYawDeg, float headLeftDeg, bool anniversary = false)
{
    halo_ce::RenderContext context;
    const float yaw = frameYawDeg * kDegToRad, half = headLeftDeg * kDegToRad * 0.5f;
    const halo_ce::Quat head{0, std::sin(half), 0, std::cos(half)};   // OpenXR: about +Y, positive turns left
    context.camera.position = {10, 20, 30};
    context.camera.forward = {std::cos(yaw), std::sin(yaw), 0};
    context.camera.up = {0, 0, 1};
    context.camera.verticalFov = 1.0f;
    context.camera.viewport = context.camera.window = {0, 0, 32, 32};
    context.camera.nearPlane = 0.01f;
    context.camera.farPlane = 1000.0f;
    context.tracking.serial = 9;
    context.tracking.generation = currentGeneration;
    context.tracking.spaceEpoch = 7;
    context.tracking.headOrientation = head;
    for (auto& eye : context.tracking.eyes)
    {
        eye.orientation = head;
        eye.fov[0] = -0.8f;
        eye.fov[1] = 0.8f;
        eye.fov[2] = 0.8f;
        eye.fov[3] = -0.8f;
    }
    context.reference.generation = currentGeneration;
    context.reference.spaceEpoch = 7;
    context.referenceRevision = 1;
    context.rendererEpoch = 1;
    context.unitsPerMeter = 1.0f / 3.048f;
    context.cameraStampUs = nowUs;
    HaloCEHeadRelative_PublishHead(context, anniversary);
}
const short centre[2]{320, 400};
// One draw of the sensor: the update (when the player has a unit), the frame's publication, then the hooked
// render 8 ms later. Returns the heading the placer would read.
float Draw(float controlYawDeg, float headLeftDeg, bool hasUnit = true)
{
    if (hasUnit)
        GameUpdate(0, controlYawDeg);
    Publish(controlYawDeg, headLeftDeg);
    nowUs += 8000;
    RenderDetour(0, 1, centre);
    return renderSaw;
}
// The same with the view frame somewhere else than the control yaw.
float DrawApart(float controlYawDeg, float frameYawDeg, float headLeftDeg)
{
    GameUpdate(0, controlYawDeg);
    Publish(frameYawDeg, headLeftDeg);
    nowUs += 8000;
    RenderDetour(0, 1, centre);
    return renderSaw;
}
bool Poll(bool active = true) { return HaloCEHeadRelative_Poll(base, kImageBytes, currentGeneration, active); }
// Lets the worker's two-second report run.
void RunReport()
{
    ticks += 2100;
    (void)Poll();
}
bool Logged(const char* text, size_t from = 0)
{
    for (size_t i = from; i < lines.size(); ++i)
        if (lines[i].find(text) != std::string::npos)
            return true;
    return false;
}
unsigned Lines(const char* text)
{
    unsigned count = 0;
    for (const auto& line : lines)
        count += line.find(text) != std::string::npos ? 1u : 0u;
    return count;
}
uint32_t Uses(TrackerUse use) { return feature.uses[static_cast<size_t>(use)].load(); }
uint32_t Steps(ProofStep step) { return feature.steps[static_cast<size_t>(step)].load(); }
void ResetGame()
{
    std::memset(sensor, 0, sizeof(sensor));
    std::memset(control, 0, sizeof(control));
    sensor[kActiveAt + 2] = 1;
    localState = HaloCELocalPlayerState{};
    localState.generation = currentGeneration;
    localState.nativePerspective = 0;
    localState.onFoot = localState.hasControlledUnit = true;
    localState.nativeCinematicFlag = false;
    stateReadable = true;
    g_config.tracker_follows_head = true;
    g_config.tracker_head_sign = 1;
    g_config.tracker_head_offset_deg = 0.0f;
}
// A fresh load of the title: every earlier install retired, a new generation, a whole image.
void NewLoad()
{
    (void)HaloCEHeadRelative_Poll(base, kImageBytes, currentGeneration, false);
    ++currentGeneration;
    BuildImage();
    ResetGame();
    lines.clear();
}
// Thirty agreeing draws at rest: the proof becomes provisional on the last.
void DrawToProvisional(float yawDeg, float headLeftDeg)
{
    for (uint32_t i = 0; i < kProofSamples; ++i)
        (void)Draw(yawDeg, headLeftDeg);
}
// One refused install on the image as it now stands: nothing hooked, the module released, the reason said once,
// and the same load not tried again. `creates`: hooks the attempt created before it gave up. `proof`: the refusal
// came from the proof (its line then names the evidence class).
void Fallback(const char* reason, const char* what, unsigned creates = 0, bool proof = true)
{
    const unsigned createsBefore = createCalls, pins = pinCalls, frees = freeCalls;
    const size_t from = lines.size();
    const bool refused = !Poll() && !feature.ready.load() && !feature.target && !feature.retained &&
                         createCalls == createsBefore + creates && pinCalls - pins == freeCalls - frees;
    const std::string expected = std::string("TRACKER HEAD Halo CE: StockFallback: ") + reason;
    const bool said = Logged(expected.c_str(), from) &&
                      (!proof || Logged("the game's own tracker. Retail-derived lead, not HCEEK-verified", from));
    const size_t once = lines.size();
    (void)Poll();
    Check(refused && said && lines.size() == once, what);
}
} // namespace

MH_STATUS WINAPI TestCreate(LPVOID target, LPVOID detour, LPVOID* original)
{
    ++createCalls;
    if (createFailure)
        return MH_ERROR_MEMORY_ALLOC;
    createdTarget = target;
    createdDetour = detour;
    *original = reinterpret_cast<void*>(&NativeRender);
    return MH_OK;
}
MH_STATUS WINAPI TestEnable(LPVOID)
{
    ++enableCalls;
    return enableFailure ? MH_ERROR_MEMORY_PROTECT : MH_OK;
}
MH_STATUS WINAPI TestDisable(LPVOID)
{
    ++disableCalls;
    return disableFailure ? MH_ERROR_MEMORY_PROTECT : MH_OK;
}
MH_STATUS WINAPI TestRemove(LPVOID)
{
    ++removeCalls;
    return MH_OK;
}
BOOL WINAPI TestPin(DWORD, LPCWSTR address, HMODULE* module)
{
    if (pinFailure || reinterpret_cast<uintptr_t>(address) != base)
        return FALSE;
    ++pinCalls;
    *module = reinterpret_cast<HMODULE>(image);
    return TRUE;
}
BOOL WINAPI TestFree(HMODULE)
{
    ++freeCalls;
    return TRUE;
}
ULONGLONG WINAPI TestTicks() { return ticks; }
GameTitle TitleAdapter_GetActiveTitle() { return title; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return currentGeneration; }
bool HaloCEControls_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept
{
    if (!stateReadable)
        return false;
    state = localState;
    return true;
}
uint64_t Sight_NowUs() { return nowUs; }
void Logf(const char* format, ...)
{
    char text[4096];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    lines.emplace_back(text);
    if (printLines)
        std::puts(text);
}
bool WaitForNativeDetourQuiescence(const void* const* entries, const void* const*, size_t count,
                                   const std::atomic<uint32_t>& pending)
{
    for (size_t i = 0; i < count; ++i)
    {
        DWORD64 imageBase{};
        const auto* range = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entries[i]), &imageBase, nullptr);
        if (!range || range->EndAddress <= range->BeginAddress)
            ++unwindFailures;
    }
    return !quiescenceFailure && !unwindFailures && !pending.load();
}

int main(int argc, char** argv)
{
    printLines = argc > 1 && !std::strcmp(argv[1], "--print");
    BuildImage();
    ResetGame();

    // ---- install: the whole proof on the synthetic image --------------------------------------------------------
    Check(Poll() && feature.ready.load() && createCalls == 1 && enableCalls == 1 && pinCalls == 1,
          "every link holds: the lane pins the module, creates one hook and enables it");
    Check(createdTarget == image + kRenderAt && createdDetour == reinterpret_cast<void*>(&RenderDetour),
          "the hook is on the render the draw entry jumps to, wherever the image has it");
    Check(feature.sites.renderRva == kRenderAt && feature.sites.updateRva == kUpdateAt && feature.sites.drawRva == kDrawAt &&
              feature.sites.placerRva == kPlacerAt && feature.sites.globalsRva == kGlobalsSlotAt &&
              feature.sites.controlRva == kControlSlotAt && feature.sites.headingOffset == kHeadingAt &&
              feature.sites.yawOffset == kYawAt && feature.sites.playerStride == kPlayerBytes &&
              feature.sites.datumStride == kDatumBytes && feature.sites.activeOffset == kActiveAt &&
              feature.sites.controlStride == kControlBytes &&
              feature.sites.storeRva == kStoreAt + static_cast<int64_t>(kStoreInstruction) && feature.halfPi == kHalfPi,
          "nothing came from a constant: every address and operand is the synthetic image's");
    Check(Lines("TRACKER HEAD Halo CE: Installed (RETAIL-DERIVED LEAD, NOT HCEEK-VERIFIED)") == 1 &&
              Logged("reader halo1.dll+0x3000 is hooked (the draw entry +0x1000 calls the update +0x2000 and jumps to it)") &&
              Logged("the float at +0x7C of local player 0's active datum (sensor globals through +0x9000: 0x568 a "
                     "player, 0x84 a datum, the active index at +0x15A4)") &&
              Logged("[player control through +0x9008] + user * 0x38 + 0x17C plus pi/2 there at +0x2237, and only the "
                     "placer +0x4000 reads it (the imports answer as cosf and sinf)") &&
              Logged("on 30 samples (provisional); a stick turn across 10 degrees confirms it. Proof now: pending; "
                     "switch=1 sign=1 offset=0.0 deg"),
          "the install line names the evidence class and every decoded place");
    {
        DWORD64 imageBase{};
        const auto* range = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&RenderDetour), &imageBase, nullptr);
        Check(range && range->EndAddress > range->BeginAddress, "the compiled detour has unwind metadata for retirement");
    }

    // ---- before the proof: the game's own tracker, said once -------------------------------------------------------
    bool untouched = true;
    for (int i = 0; i < 5; ++i)
        untouched = untouched && Draw(20.0f, 50.0f) == GameField(20.0f);
    const size_t afterFive = lines.size();
    RunReport();
    Check(untouched && Logged("TRACKER HEAD Halo CE: the game's own tracker for now: unproven=5", afterFive),
          "the first report with draws and no substitution says why, by reason");

    // ---- at rest: 29 draws untouched, the 30th by the head ----------------------------------------------------------
    for (uint32_t i = 5; i + 1 < kProofSamples; ++i)
        untouched = untouched && Draw(20.0f, 50.0f) == GameField(20.0f);
    Check(untouched && renderCalls == kProofSamples - 1 && Uses(TrackerUse::Unproven) == kProofSamples - 1 - 5,
          "unproven: every draw is forwarded once with the game's own heading, counted as unproven");
    static uint8_t sensorBefore[sizeof(sensor)], controlBefore[sizeof(control)];
    GameUpdate(0, 20.0f);
    Publish(20.0f, 50.0f, true);
    nowUs += 8000;
    std::memcpy(sensorBefore, sensor, sizeof(sensor));
    std::memcpy(controlBefore, control, sizeof(control));
    RenderDetour(0, 1, centre);
    Check(Near(renderSaw, FieldFromYaw(70.0f * kDegToRad)) && Near(SensorHeading(0, Active()), FieldFromYaw(70.0f * kDegToRad)),
          "the 30th agreeing draw: the placer reads the head's heading (the room's 20 deg plus 50 deg to the left)");
    std::memcpy(sensorBefore + Active() * kDatumBytes + kHeadingAt, sensor + Active() * kDatumBytes + kHeadingAt,
                sizeof(float));
    Check(!std::memcmp(sensorBefore, sensor, sizeof(sensor)) && !std::memcmp(controlBefore, control, sizeof(control)),
          "THE ONE WRITE: four bytes of the active datum changed; player control and everything else did not");
    Check(renderLocal == 0 && renderMultiplayer == 1 && renderCentre == centre && renderCalls == kProofSamples,
          "the original is called exactly once per draw with its own three arguments");
    const size_t afterRest = lines.size();
    RunReport();
    Check(Logged("TRACKER HEAD Halo CE: proven at rest (provisional): the control yaw agrees with the view frame at "
                 "20.0 deg; the first stick turn confirms it", afterRest) &&
              Logged("TRACKER HEAD Halo CE: following the head: first contacts placed by the head's heading (the "
                     "game's heading was 20.0 deg, the head's 70.0 deg)", afterRest) &&
              Lines("the game's own tracker for now") == 1,
          "the report says it once: proven at rest (provisional), then following the head");
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        char threads[128];
        std::snprintf(threads, sizeof(threads),
                      "drawn on thread %lu (the drawing thread changed 0 times), the head published on thread %lu; "
                      "switch=1 sign=1 offset=0.0 deg",
                      GetCurrentThreadId(), GetCurrentThreadId());
        Check(Logged("TRACKER HEAD Halo CE (dev): substituted 1; the game's heading 20.0 deg, the view frame's 20.0 "
                     "deg, the head's 70.0 deg, head minus game +50.0 deg (last; the head was read in 25 draws); "
                     "proof PROVISIONAL: agreed 25, lagging 0, disagreed 0, restarted 0, no reference 0; refused 24: "
                     "unproven=24; faults 0, mixed sample reads 0; draws while the frame was Anniversary's 1, "
                     "Classic's 24; ", afterRest) &&
                  Logged(threads, afterRest),
              "the dev line (the acceptance check): the three headings, their difference, the proof's "
              "counts, the refusals, the graphics mode of each draw and both threads");
        // The reading is there before anything is substituted: the first window, all of it unproven.
        Check(Logged("TRACKER HEAD Halo CE (dev): substituted 0; the game's heading 20.0 deg, the view frame's 20.0 "
                     "deg, the head's 70.0 deg, head minus game +50.0 deg (last; the head was read in 5 draws); "
                     "proof pending: agreed 5, lagging 0, disagreed 0, restarted 0, no reference 0; refused 5: "
                     "unproven=5;", afterFive),
              "the dev line reads the head's heading while the proof is still pending");
    }
#else
    Check(!Logged("TRACKER HEAD Halo CE (dev)"), "a player build prints no dev line");
#endif

    // ---- the head alone: the stock tracker would not move, this one does --------------------------------------------
    Check(Near(Draw(20.0f, -40.0f), FieldFromYaw(340.0f * kDegToRad)) && ControlYaw(0) == WrapYaw(20.0f * kDegToRad),
          "the head 40 deg to the right, no stick: the placer's heading follows the head; the control yaw is untouched");

    // ---- the first stick turn confirms it ---------------------------------------------------------------------------
    Check(Near(Draw(50.0f, 50.0f), FieldFromYaw(100.0f * kDegToRad)) && feature.proof.stage == ProofStage::Confirmed,
          "a 30 deg snap turn: the head's heading turns with the stick, and the proof is confirmed");
    const size_t afterTurn = lines.size();
    RunReport();
    Check(Logged("TRACKER HEAD Halo CE: CONFIRMED at run time: the sensor's heading agreed with the view frame on at "
                 "least 30 samples across a turn of 30.0 degrees (the frame from 20.0 to 50.0 deg)", afterTurn),
          "the report says CONFIRMED once, with the turn it was confirmed across");

    // ---- the switch, live ------------------------------------------------------------------------------------------
    g_config.tracker_follows_head = false;
    const size_t afterOff = lines.size();
    const bool ownWhileOff = Draw(50.0f, 80.0f) == GameField(50.0f) && Uses(TrackerUse::SwitchOff) == 1;
    RunReport();
    g_config.tracker_follows_head = true;
    const bool headAgain = Near(Draw(50.0f, 80.0f), FieldFromYaw(130.0f * kDegToRad));
    RunReport();
    Check(ownWhileOff && headAgain &&
              Logged("TRACKER HEAD Halo CE: switch off: the tracker now turns with the game's own heading (the turn "
                     "stick only)", afterOff) &&
              Logged("TRACKER HEAD Halo CE: switch on: the tracker now turns with your head (once proven)", afterOff),
          "the switch is read at every use, and each change is said once");

    // ---- the trail: each history datum keeps the heading it had ------------------------------------------------------
    const int before = Active();
    GameTick();
    Check(Near(SensorHeading(0, Active()), FieldFromYaw(130.0f * kDegToRad)) && Active() != before,
          "the tick copies the datum this lane wrote into the next active one");
    Check(Near(Draw(50.0f, -30.0f), FieldFromYaw(20.0f * kDegToRad)) &&
              Near(SensorHeading(0, before), FieldFromYaw(130.0f * kDegToRad)),
          "the next draw rewrites the new active datum (head now 30 deg to the right); the older one keeps its own");

    // ---- refusals: the game's own heading, counted by reason ----------------------------------------------------------
    // No unit: the game stores nothing, and the active datum still holds what this lane wrote.
    GameTick();
    const float ours = SensorHeading(0, Active());
    const ViewProof proofBefore = feature.proof;
    const uint32_t noStore = Uses(TrackerUse::NoStore);
    Check(Draw(50.0f, 80.0f, false) == ours && SensorHeading(0, Active()) == ours &&
              Uses(TrackerUse::NoStore) == noStore + 1 && feature.proof.agreed == proofBefore.agreed &&
              feature.proof.disagreed == proofBefore.disagreed && feature.proof.stage == proofBefore.stage,
          "no store in this call: the field this lane wrote earlier is neither replaced nor held against the proof");
    localState.nativeCinematicFlag = true;
    Check(Draw(50.0f, 80.0f) == GameField(50.0f) && Uses(TrackerUse::NotGameplay) == 1, "a cinematic: the game's own heading");
    localState.nativeCinematicFlag = false;
    localState.nativePerspective = 3;
    Check(Draw(50.0f, 80.0f) == GameField(50.0f) && Uses(TrackerUse::NotGameplay) == 2,
          "a perspective the game draws no sensor in: the game's own heading");
    localState.nativePerspective = 0;
    stateReadable = false;
    Check(Draw(50.0f, 80.0f) == GameField(50.0f) && Uses(TrackerUse::NoPlayerState) == 1,
          "the player's state unreadable: the game's own heading");
    stateReadable = true;
    localState.generation = currentGeneration + 1;
    Check(Draw(50.0f, 80.0f) == GameField(50.0f) && Uses(TrackerUse::NoPlayerState) == 2,
          "a player state from another load: the game's own heading");
    localState.generation = currentGeneration;
    GameUpdate(0, 50.0f);
    Publish(50.0f, 80.0f);
    nowUs += head_relative::kMaxSampleAgeUs + 1000;
    RenderDetour(0, 1, centre);
    Check(renderSaw == GameField(50.0f) && Uses(TrackerUse::Stale) == 1, "a head sample older than 100 ms: the game's own heading");
    GameUpdate(0, 50.0f);
    ++currentGeneration;   // the title's generation moved on under a hook that belongs to the earlier load
    RenderDetour(0, 1, centre);
    --currentGeneration;
    Check(renderSaw == GameField(50.0f) && Uses(TrackerUse::OtherGeneration) == 1,
          "a hook from another load of the title: the game's own heading");
    // A seat: no reference, and the proof taken on foot carries.
    localState.onFoot = false;
    localState.nativePerspective = 1;
    Check(Near(Draw(50.0f, 80.0f), FieldFromYaw(130.0f * kDegToRad)) && feature.proof.stage == ProofStage::Confirmed,
          "in a seat, seen from outside: the head is used on the proof taken on foot");
    localState.onFoot = true;
    localState.nativePerspective = 0;
    // Another local player's sensor is not read, written or counted against anything.
    GameUpdate(1, 200.0f);
    std::memcpy(sensorBefore, sensor, sizeof(sensor));
    const unsigned callsBefore = renderCalls;
    RenderDetour(1, 0, centre);
    Check(!std::memcmp(sensorBefore, sensor, sizeof(sensor)) && renderCalls == callsBefore + 1 && renderLocal == 1 &&
              renderMultiplayer == 0 && Uses(TrackerUse::NotLocalUser) == 1,
          "local player 1's sensor is forwarded untouched");
    // A draw that arrives while another draw of player 0 is inside the hook (which thread draws CE's HUD is not
    // known without a run): forwarded untouched, counted, and neither the proof nor the held heading is touched.
    {
        GameUpdate(0, 50.0f);
        Publish(50.0f, 80.0f);
        nowUs += 8000;
        const ViewProof proofHeld = feature.proof;
        const uint32_t stepsBefore = Steps(ProofStep::Agreed) + Steps(ProofStep::NoReference);
        feature.inside.store(true);
        RenderDetour(0, 1, centre);
        const bool untouchedInside = renderSaw == GameField(50.0f) && Uses(TrackerUse::Overlapped) == 1 &&
                                     feature.proof.agreed == proofHeld.agreed && feature.proof.stage == proofHeld.stage &&
                                     Steps(ProofStep::Agreed) + Steps(ProofStep::NoReference) == stepsBefore &&
                                     feature.inside.load();
        feature.inside.store(false);
        Check(untouchedInside && Near(Draw(50.0f, 80.0f), FieldFromYaw(130.0f * kDegToRad)) && !feature.inside.load(),
              "an overlapping draw is forwarded untouched and counted; the next one is by the head, and the flag is free");
        std::memcpy(sensorBefore, sensor, sizeof(sensor));
    }
    // An active index that names no datum, and no globals yet: nothing is read through them.
    const int16_t savedActive = Active();
    Active() = 10;
    RenderDetour(0, 1, centre);
    Active() = -1;
    RenderDetour(0, 1, centre);
    Active() = savedActive;
    Check(feature.faults.load() == 0 && !std::memcmp(sensorBefore, sensor, sizeof(sensor)),
          "an active index that names no datum: forwarded untouched, no read outside the record");
    PutValue<uintptr_t>(kGlobalsSlotAt, 0);
    RenderDetour(0, 1, centre);
    Check(feature.faults.load() == 0, "no sensor globals yet: forwarded untouched");
    // A pointer that faults: caught, counted, and the game's render still runs once.
    PutValue<uintptr_t>(kGlobalsSlotAt, 0x10);
    const unsigned beforeFault = renderCalls;
    RenderDetour(0, 1, centre);
    Check(feature.faults.load() == 1 && renderCalls == beforeFault + 1 && !feature.callbacks.load() && !feature.inside.load(),
          "a read that faults is caught and counted, the original still runs exactly once, and the flag is free");
    PutValue<uintptr_t>(kGlobalsSlotAt, reinterpret_cast<uintptr_t>(sensor));
    Check(Near(Draw(50.0f, 10.0f), FieldFromYaw(60.0f * kDegToRad)), "and the next draw is by the head again");

    // ---- retirement, and what a reinstall on the same loaded image keeps ---------------------------------------------
    disableFailure = true;
    const size_t beforeCleanup = lines.size();
    Check(!Poll(false) && feature.retained && feature.target && !feature.ready.load() &&
              Logged("TRACKER HEAD Halo CE: CleanupRequired", beforeCleanup),
          "a disable that fails keeps the module and the trampoline, closes admission and says so");
    (void)Poll(false);
    Check(Lines("TRACKER HEAD Halo CE: CleanupRequired") == 1, "... once");
    disableFailure = false;
    quiescenceFailure = true;
    Check(!Poll(false) && feature.retained && feature.target, "a detour that is not quiet keeps them too");
    quiescenceFailure = false;
    const unsigned freesBefore = freeCalls, removesBefore = removeCalls;
    Check(!Poll(false) && !feature.retained && !feature.target && freeCalls == freesBefore + 1 &&
              removeCalls == removesBefore + 1 && !unwindFailures,
          "the next poll retires it: hook removed, module released");
    const unsigned lateBefore = renderCalls;
    RenderDetour(0, 1, centre);
    Check(feature.faults.load() == 1 && renderCalls == lateBefore, "a late call into the retired detour reads nothing");
    const size_t beforeReinstall = lines.size();
    Check(Poll() && createCalls == 2 && Logged("Proof now: CONFIRMED earlier on this loaded image, kept", beforeReinstall),
          "a reinstall on the same loaded image keeps the CONFIRMED proof and says so");
    Check(Near(Draw(50.0f, 25.0f), FieldFromYaw(75.0f * kDegToRad)), "... so the very first draw is by the head");

    // ---- a provisional proof is not kept ---------------------------------------------------------------------------
    NewLoad();
    Check(Poll() && Logged("Proof now: pending;"), "a new load of the title starts from nothing");
    DrawToProvisional(140.0f, -20.0f);
    Check(feature.proof.stage == ProofStage::Provisional && Near(renderSaw, FieldFromYaw(120.0f * kDegToRad)),
          "thirty draws at rest: provisional again on the new load");
    (void)Poll(false);
    const size_t beforeSecond = lines.size();
    Check(Logged("proven at rest (provisional): the control yaw agrees with the view frame at 140.0 deg") && Poll() &&
              Logged("Proof now: pending;", beforeSecond) && feature.proof.stage == ProofStage::Unproven,
          "retired before the report ran: the proof's line is still said, and the reinstall does NOT keep a provisional proof");
    Check(Draw(140.0f, -20.0f) == GameField(140.0f), "... so the first draw after it is the game's own again");

    // ---- a runtime whose poses are not set yet: the head half a turn from everything ------------------------------
    NewLoad();
    (void)Poll();
    DrawToProvisional(20.0f, 180.0f);
    Check(feature.proof.stage == ProofStage::Provisional && feature.proof.disagreed == 0 &&
              Steps(ProofStep::Disagreed) == 0 && Steps(ProofStep::Restarted) == 0 && Uses(TrackerUse::Used) == 1 &&
              Near(renderSaw, FieldFromYaw(200.0f * kDegToRad)),
          "a head half a turn from the control yaw is no fault: proven at rest all the same, nothing counted against "
          "it, and the contacts are placed by where that head faces");
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        const size_t from = lines.size();
        RunReport();
        Check((Logged("the head's -160.0 deg, head minus game +180.0 deg (last; the head was read in 30 draws)", from) ||
               Logged("the head's -160.0 deg, head minus game -180.0 deg (last; the head was read in 30 draws)", from)) &&
                  Logged("proof PROVISIONAL: agreed 30, lagging 0, disagreed 0, restarted 0, no reference 0; refused "
                         "29: unproven=29; faults 0", from),
              "the dev line reads that half turn and counts nothing against it");
    }
#endif

    // ---- withdrawal, and no second provisional pass ---------------------------------------------------------------
    NewLoad();
    (void)Poll();
    DrawToProvisional(10.0f, 40.0f);
    bool withdrawnAtSixty = true;
    for (uint32_t i = 0; i < kProofWithdrawRun; ++i)
    {
        // The view frame 90 deg away from the control yaw: this field is not the frame's heading after all.
        (void)DrawApart(10.0f, 100.0f, 0.0f);
        withdrawnAtSixty = withdrawnAtSixty && (i + 1 < kProofWithdrawRun ? feature.proof.stage == ProofStage::Provisional
                                                                           : feature.proof.stage == ProofStage::Unproven);
    }
    Check(withdrawnAtSixty && renderSaw == GameField(10.0f) && feature.proof.provisionalSpent,
          "sixty disagreeing draws withdraw the proof on the sixtieth, and that draw is the game's own");
    const size_t afterWithdrawal = lines.size();
    RunReport();
    Check(Logged("TRACKER HEAD Halo CE: PROOF WITHDRAWN: 60 disagreements in a row. Last agreement: the game's heading "
                 "10.0 deg, the view frame's 10.0 deg; first disagreement: the game's 10.0 deg, the view frame's "
                 "100.0 deg.", afterWithdrawal),
          "the withdrawal is said once, with the last agreeing and the first disagreeing values");
    bool stillOwn = true;
    for (int i = 0; i < 200; ++i)
        stillOwn = stillOwn && Draw(10.0f, 40.0f) == GameField(10.0f);
    Check(stillOwn && feature.proof.stage == ProofStage::Unproven,
          "back at rest, 200 agreeing draws: no second provisional pass");
    (void)Poll(false);
    const size_t afterSpent = lines.size();
    bool spentKept = Poll() && Logged("Proof now: pending; one was withdrawn on this image, so only a CONFIRMED proof "
                                      "substitutes", afterSpent);
    for (uint32_t i = 0; i < kProofSamples; ++i)
        spentKept = spentKept && Draw(10.0f, 40.0f) == GameField(10.0f);
    Check(spentKept, "a reinstall on the same image remembers that: thirty agreeing draws at rest still change nothing");
    Check(Near(Draw(55.0f, 40.0f), FieldFromYaw(95.0f * kDegToRad)) && feature.proof.stage == ProofStage::Confirmed,
          "a stick turn that agrees confirms it, and the head is used from that draw");

    // ---- StockFallback: each broken link leaves nothing hooked --------------------------------------------------------
    NewLoad();
    PutValue<float>(kHalfPiAt, 3.14159265f);
    Fallback("the constants are not pi/2 and the sign mask (unique signatures: dusrhcpbf of dusrhcpbf)",
             "a constant that is not pi/2: nothing is hooked, the reason is said once, the same load is not retried");
    NewLoad();
    PutValue<uint32_t>(kMaskAt, 0x7FFFFFFFu);
    Fallback("the constants are not pi/2 and the sign mask", "a mask that is not the sign bit: StockFallback");
    NewLoad();
    Place(0x7000, kTrackerStorePattern);   // a second store: the signature is no longer unique
    Fallback("the heading's store is missing or ambiguous (unique signatures: du-rhcpbf of dusrhcpbf)",
             "an ambiguous signature: StockFallback, and the line names which one");
    NewLoad();
    image[kRenderAt + 3] ^= 0xFF;   // the render's head does not match
    Fallback("the sensor render's head is missing, ambiguous or not a function (unique signatures: dus-hcpbf of dusrhcpbf)",
             "a render head that does not match: StockFallback");
    NewLoad();
    RegisterFunctions(false);   // the function table does not know the render
    Fallback("the sensor render's head is missing, ambiguous or not a function (unique signatures: dusrhcpbf of dusrhcpbf)",
             "a render head the function table does not begin a function at: StockFallback");
    NewLoad();
    PutBranch(kDrawAt, kDrawJumpRender, kRenderAt + 0x40);
    Fallback("the draw entry does not call the matched update and jump to the matched render",
             "a draw entry that jumps elsewhere: StockFallback");
    NewLoad();
    PutValue<uint32_t>(kFacingAt + kFacingYaw, kYawAt + 4);
    Fallback("the sensor's yaw is not the output user's facing", "a yaw that is not the facing reader's: StockFallback");
    NewLoad();
    image[kPlacerAt + kPlacerHeadingDisp] = static_cast<uint8_t>(kHeadingAt - 4);
    Fallback("the placer does not read the heading the store writes", "a placer on the neighbouring field: StockFallback");
    NewLoad();
    PutValue<uint32_t>(kDatumAt + kDatumStride, kDatumBytes + 4);
    Fallback("the update and the render disagree on the sensor's layout", "two strides for one datum: StockFallback");
    NewLoad();
    PutValue<uintptr_t>(kCosSlotAt, reinterpret_cast<uintptr_t>(&TestSin));
    PutValue<uintptr_t>(kSinSlotAt, reinterpret_cast<uintptr_t>(&TestCos));
    Fallback("the placer's trig calls are not cosf then sinf", "sine before cosine: StockFallback");
    NewLoad();
    image[kCosThunkAt + 1] = 0x15;
    Fallback("the placer's trig calls are not import thunks", "a trig call that is not a thunk: StockFallback");
    NewLoad();
    PutValue<uintptr_t>(kSinSlotAt, 0);
    Fallback("a trig import of the placer is not bound", "an unbound import: StockFallback");
    NewLoad();
    PutRip(kStoreAt, kStoreControlDisp, kStoreControlNext, kControlSlotAt + 4);
    PutRip(kFacingAt, kFacingControlDisp, kFacingControlNext, kControlSlotAt + 4);
    Fallback("the sensor's or player control's pointer is not a slot of this image",
             "a pointer slot that is not pointer-aligned: StockFallback");
    NewLoad();
    createFailure = true;
    Fallback("hook creation failed", "a hook that cannot be created: StockFallback, the module released", 1, false);
    createFailure = false;
    NewLoad();
    pinFailure = true;
    Fallback("the title module could not be pinned", "a module that cannot be pinned: StockFallback before any read", 0,
             false);
    pinFailure = false;
    NewLoad();
    enableFailure = true;
    const unsigned removesBeforeEnable = removeCalls;
    Fallback("hook enable failed", "a hook that cannot be enabled: StockFallback", 1, false);
    Check(removeCalls == removesBeforeEnable + 1, "... and the hook that was created is removed again");
    enableFailure = false;

    // ---- a new load recovers; another title closes admission ------------------------------------------------------
    NewLoad();
    Check(Poll() && feature.ready.load() && Logged("TRACKER HEAD Halo CE: Installed"), "the next load installs again");
    DrawToProvisional(300.0f, 15.0f);
    Check(Near(renderSaw, FieldFromYaw(315.0f * kDegToRad)), "and follows the head");
    title = GameTitle::Halo3;
    Check(!Poll(), "another active title: the lane is not current");
    title = GameTitle::HaloCE;
    (void)Poll(false);
    Check(!feature.target && !feature.retained && !feature.callbacks.load() && pinCalls == freeCalls && !unwindFailures,
          "at the end nothing is installed and every pin was released");

    std::printf("halo ce head relative runtime tests%s: %u checks, %u failures\n",
#if HALOMCCVR_DEV_DIAGNOSTICS
                " (dev)",
#else
                "",
#endif
                checks, failures);
    return failures ? 1 : 0;
}
