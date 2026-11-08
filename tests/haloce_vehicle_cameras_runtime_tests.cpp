// The CE vehicle cameras' engine side (src/dll/haloce_vehicle_cameras.cpp) against owned fixtures: the following
// camera's hook forwards the game's own camera, then moves its command to the chosen place and hands the placement what
// the game's camera was; the root-frame query; retirement waits for every callback and resolves both compiled ranges.
// Native services are declared fixtures in an owned private image; no game code runs and no hook is installed.
#include "../src/dll/haloce_vehicle_cameras.cpp"
#include <cstdio>

namespace
{
unsigned failures{}, nativeCalls{}, placeCalls{}, markerCalls{}, unwindChecks{}, unwindFailures{};
bool chaseAny{}, chaseHead{}, seatReadable = true, placeAnswers = true, raiseMarker{};
uint32_t testGeneration = 5, seatGeneration = 5, commandFlags = 1;
GameTitle testTitle = GameTitle::HaloCE;
constexpr uint32_t kUnit = 0x11110001, kParent = 0x22220002, kRoot = 0x22220002;
HaloCEChaseFrame lastFrame{};
const float kPlaceAt[3] = {1.0f, 2.0f, 3.0f};

void Check(bool value, const char* message)
{
    if (!value)
    {
        ++failures;
        std::fprintf(stderr, "CE vehicle cameras: %s\n", message);
    }
}
bool Near(float a, float b)
{
    return std::fabs(a - b) < 1.0e-4f;
}
template <class Function> bool InstallService(uint32_t rva, Function function)
{
    // mov rax, imm64; jmp rax: the native x64 argument registers pass through.
    uint8_t code[]{0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    const uintptr_t target = reinterpret_cast<uintptr_t>(function);
    std::memcpy(code + 2, &target, 8);
    DWORD before{};
    auto* at = reinterpret_cast<void*>(moduleBase + rva);
    if (!VirtualProtect(at, sizeof(code), PAGE_EXECUTE_READWRITE, &before))
        return false;
    std::memcpy(at, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), at, sizeof(code));
    return true;
}

// The game's following camera: a command with a pivot, an offset along the look's heading [forward, right, up], a
// distance back along the look, and the look.
void __fastcall TestFollowing(void*, const void*, void* result)
{
    ++nativeCalls;
    auto* command = static_cast<uint8_t*>(result);
    std::memset(command, 0, 0x60);
    const float pivot[3]{10.0f, 20.0f, 30.0f}, offset[3]{1.0f, 0.5f, 2.0f}, distance = 3.0f, looks[3]{1.0f, 0.0f, 0.0f};
    std::memcpy(command, &commandFlags, 4);
    std::memcpy(command + 4, pivot, 12);
    std::memcpy(command + 0x10, offset, 12);
    std::memcpy(command + 0x1c, &distance, 4);
    std::memcpy(command + 0x24, looks, 12);
}

// The marker query: a frame for any object's root node (empty name), the head 5 world units up for "head".
int16_t __fastcall TestMarker(uint32_t object, const char* name, void* record, int16_t)
{
    ++markerCalls;
    if (raiseMarker)
        RaiseException(0xe0425151, 0, 0, nullptr);
    auto* r = static_cast<uint8_t*>(record);
    const float forward[3]{1.0f, 0.0f, 0.0f}, up[3]{0.0f, 0.0f, 1.0f};
    float position[3]{object == kRoot ? 100.0f : 101.0f, 0.0f, name && name[0] ? 5.0f : 0.0f};
    std::memcpy(r + 0x3c, forward, 12);
    std::memcpy(r + 0x54, up, 12);
    std::memcpy(r + 0x60, position, 12);
    return 1;
}

// One following-camera call through the production hook; the command as it comes back.
void RunHook(uint8_t (&command)[0x60])
{
    std::memset(command, 0xcd, sizeof(command));
    uint8_t camera[0x40]{}, controls[0x40]{};
    FollowingHook(camera, controls, command);
}
bool CommandIsTheGames(const uint8_t (&command)[0x60])
{
    float pivot[3], offset[3], distance;
    std::memcpy(pivot, command + 4, 12);
    std::memcpy(offset, command + 0x10, 12);
    std::memcpy(&distance, command + 0x1c, 4);
    return Near(pivot[0], 10) && Near(pivot[1], 20) && Near(pivot[2], 30) && Near(offset[0], 1) && Near(offset[2], 2) &&
           Near(distance, 3);
}
bool RootFrameFaults()
{
    float p[3], f[3], u[3];
    return !HaloCEVehicleCameras_RootFrame(kRoot, p, f, u);
}

// A stand-in for the game's following camera to hook for real: the same signature, a body MinHook can relocate.
unsigned standInCalls{};
__declspec(noinline) void __fastcall StandInFollowing(void* camera, const void* controls, void* result)
{
    ++standInCalls;
    TestFollowing(camera, controls, result);
}
} // namespace

Config::Config() {}
Config g_config;
GameTitle TitleAdapter_GetActiveTitle()
{
    return testTitle;
}
uint32_t TitleAdapter_GetGeneration(GameTitle)
{
    return testGeneration;
}
void Logf(const char*, ...) {}
bool Game_HaloCEChaseAny() noexcept
{
    return chaseAny;
}
bool Game_HaloCEChaseHead() noexcept
{
    return chaseHead;
}
bool Game_HaloCEChasePlace(const HaloCEChaseFrame& frame, float out[3]) noexcept
{
    ++placeCalls;
    lastFrame = frame;
    if (!placeAnswers)
        return false;
    std::memcpy(out, kPlaceAt, sizeof(kPlaceAt));
    return true;
}
bool HaloCEControls_ReadVehicleSeatNow(HaloCEVehicleSeatNow& seat) noexcept
{
    if (!seatReadable)
        return false;
    seat = HaloCEVehicleSeatNow{};
    seat.generation = seatGeneration;
    seat.unit = kUnit;
    seat.parent = kParent;
    seat.root = kRoot;
    seat.definition = 0x33330003;
    seat.seat = 0;
    seat.boundsRadius = 1.1f;
    return true;
}
bool WaitForNativeDetourQuiescence(const void* const* functions, const void* const* trampolines, size_t count,
                                   const std::atomic<uint32_t>& pending)
{
    // Production's real unwind admission for every range; this fixture runs no concurrent callers, so it replaces
    // only the drain that follows.
    if (!functions || !trampolines || !count)
        return false;
    for (size_t i = 0; i < count; ++i)
    {
        ++unwindChecks;
        DWORD64 base{};
        const auto* entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(functions[i]), &base, nullptr);
        if (!entry || entry->EndAddress <= entry->BeginAddress)
            ++unwindFailures;
    }
    return pending.load() == 0;
}

int main()
{
    auto* image = VirtualAlloc(nullptr, halo_ce::contract::imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!image)
        return 2;
    moduleBase = reinterpret_cast<uintptr_t>(image);
    if (!InstallService(halo_ce::vehicle_camera_contract::marker, &TestMarker) || MH_Initialize() != MH_OK)
        return 2;
    generation = testGeneration;
    active = ready = true;
    original = reinterpret_cast<void*>(&TestFollowing);
    uint8_t command[0x60];

    RunHook(command);
    Check(nativeCalls == 1 && placeCalls == 0 && markerCalls == 0 && CommandIsTheGames(command) && !callbacks.load(),
          "no chase camera chosen: the game's own camera, untouched, at one atomic load");

    chaseAny = true;
    RunHook(command);
    float pivot[3], offset[3], distance;
    std::memcpy(pivot, command + 4, 12);
    std::memcpy(offset, command + 0x10, 12);
    std::memcpy(&distance, command + 0x1c, 4);
    Check(placeCalls == 1 && Near(pivot[0], 1) && Near(pivot[1], 2) && Near(pivot[2], 3) && offset[0] == 0.0f &&
              offset[1] == 0.0f && offset[2] == 0.0f && distance == 0.0f && !callbacks.load(),
          "a chase camera: the command stands the camera exactly at the place, with no swing and no pull-in");
    Check(lastFrame.generation == testGeneration && lastFrame.unit == kUnit && lastFrame.parent == kParent &&
              lastFrame.root == kRoot && lastFrame.seat == 0 && Near(lastFrame.boundsRadius, 1.1f) &&
              Near(lastFrame.vehiclePos[0], 100) && lastFrame.seatValid && Near(lastFrame.seatPos[0], 101) &&
              !lastFrame.headValid,
          "the placement gets the vehicle's and the seat's root nodes as drawn, and no head unless one is asked for");
    // The game's camera from its pivot: offset along the heading (+x), its second axis the heading's right (-y),
    // less the look times the distance.
    Check(lastFrame.gameValid && Near(lastFrame.pivot[0], 10) && Near(lastFrame.pivot[2], 30) &&
              Near(lastFrame.gameFromPivot[0], 1 - 3) && Near(lastFrame.gameFromPivot[1], -0.5f) &&
              Near(lastFrame.gameFromPivot[2], 2) && Near(lastFrame.looks[0], 1),
          "and the game's own camera as its command had it");

    chaseHead = true;
    RunHook(command);
    Check(lastFrame.headValid && Near(lastFrame.headPos[2], 5), "a playerhead camera gets the head marker");
    chaseHead = false;

    const unsigned placedBefore = placeCalls;
    placeAnswers = false;
    RunHook(command);
    Check(placeCalls == placedBefore + 1 && CommandIsTheGames(command),
          "a place not known yet leaves the game's camera as it is");
    placeAnswers = true;

    commandFlags = 0;
    RunHook(command);
    Check(placeCalls == placedBefore + 1 && CommandIsTheGames(command), "a command the game marked unused is left alone");
    commandFlags = 1;

    seatReadable = false;
    RunHook(command);
    Check(placeCalls == placedBefore + 1 && CommandIsTheGames(command), "a seat that cannot be read leaves the game's camera");
    seatReadable = true;

    seatGeneration = testGeneration + 1;
    RunHook(command);
    Check(placeCalls == placedBefore + 1 && CommandIsTheGames(command), "another generation's seat leaves the game's camera");
    seatGeneration = testGeneration;

    const uint64_t faultedBefore = faults.load();
    raiseMarker = true;
    RunHook(command);
    Check(placeCalls == placedBefore + 1 && CommandIsTheGames(command) && faults.load() == faultedBefore + 1 &&
              !callbacks.load(),
          "a fault in the marker query is caught, counted, and leaves the game's camera");
    Check(RootFrameFaults() && faults.load() == faultedBefore + 2 && !callbacks.load(),
          "the root-frame query catches the same fault and leaves no callback counted");
    raiseMarker = false;

    float p[3], f[3], u[3];
    Check(HaloCEVehicleCameras_RootFrame(kRoot, p, f, u) && Near(p[0], 100) && Near(u[2], 1) && !callbacks.load(),
          "the root-frame query answers the root node as drawn");
    testTitle = GameTitle::Halo3;
    Check(!HaloCEVehicleCameras_RootFrame(kRoot, p, f, u) && !HaloCEVehicleCameras_FollowingLive(),
          "another title's frame is never answered");
    RunHook(command);
    Check(CommandIsTheGames(command), "nor is another title's camera moved");
    testTitle = GameTitle::HaloCE;

    // Retirement, on a real hook (a stand-in with the following camera's signature): it waits for every callback,
    // disables first, removes only once drained, and both compiled ranges resolve.
    void* standIn = reinterpret_cast<void*>(&StandInFollowing);
    Check(MH_CreateHook(standIn, reinterpret_cast<void*>(&FollowingHook), &original) == MH_OK &&
              MH_EnableHook(standIn) == MH_OK,
          "fixture: the stand-in is hooked");
    target = standIn;
    enabled = true;
    // Called through a pointer the compiler cannot see through: the hook changes what the call does behind its back.
    void(__fastcall* volatile callStandIn)(void*, const void*, void*) = &StandInFollowing;
    const unsigned placedHooked = placeCalls;
    callStandIn(nullptr, nullptr, command);
    Check(standInCalls == 1 && placeCalls == placedHooked + 1 && Near(reinterpret_cast<const float*>(command + 4)[0], 1),
          "through the real hook the game's camera runs, then the chase camera is placed");
    callbacks = 1;
    Check(!Remove() && retiring.load() && moduleBase != 0 && target == standIn && !enabled,
          "a callback still counted: the hook is disabled, but the module and the hook are kept");
    callStandIn(nullptr, nullptr, command);
    Check(standInCalls == 2 && placeCalls == placedHooked + 1 && CommandIsTheGames(command),
          "a disabled hook is the game's own camera");
    callbacks = 0;
    Check(Remove() && !moduleBase && !generation.load() && !retiring.load() && !target && !original,
          "with every callback drained the hook is removed and the module is let go");
    Check(MH_EnableHook(standIn) == MH_ERROR_NOT_CREATED, "and nothing of the hook is left behind");
    Check(unwindChecks == 4 && !unwindFailures, "both retirements resolve the hook's and the root-frame query's ranges");

    MH_Uninitialize();
    VirtualFree(image, 0, MEM_RELEASE);
    if (failures)
        return 1;
    std::printf("CE vehicle cameras runtime: all checks passed\n");
    return 0;
}
