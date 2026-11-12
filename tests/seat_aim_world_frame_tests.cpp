// Seats aim in world angles: the PRODUCTION install / setting sync / teardown of the rebase skip
// (src/dll/seat_aim_world_frame.inl), compiled here against a fixture: a VirtualAlloc'd executable "module"
// holding the seat-flag anchor and its jne at the retail RVAs. Covers install -> patch -> remove, the setting
// and seat gates, adopting a jump an earlier teardown left in place, refusal of foreign bytes (at install and
// after it), a thread sitting inside the bytes, a missing thread freeze, and a replaced module generation.
//
// The thread freeze (ReachThreadFreeze, game.cpp) is stubbed: Capture() can be made to fail (the Busy path), and
// Threads() reports REAL suspended threads whose RIP the test sets, so the production RIP check runs as shipped.
// Built twice by CMake: the player flavour and the dev flavour (HALOMCCVR_DEV_DIAGNOSTICS=1, the A/B key).
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "../src/common/seat_aim_frame_logic.h"
#include "../src/dll/sigscan.h"

#ifndef HALOMCCVR_DEV_DIAGNOSTICS
#define HALOMCCVR_DEV_DIAGNOSTICS 0
#endif
#define HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP 1

// ---- the game.cpp surface the .inl uses ---------------------------------------------------------------------
enum class GameTitle { None, Halo3, Halo3ODST, HaloReach };
static uint32_t g_generation[4]{0, 7, 9, 11};
uint32_t TitleAdapter_GetGeneration(GameTitle title) { return g_generation[static_cast<int>(title)]; }

static struct
{
    bool vehicle_first_person = true;
    bool vehicle_hide_body = true;
    int seat_aim_world_frame = 1;
} g_config;

static std::atomic<uint32_t> g_halo3SeatNativeNoThirdPerson{0};
static std::atomic<uint32_t> g_odstSeatNativeNoThirdPerson{0};
static std::atomic<uint32_t> g_reachSeatNativeNoThirdPerson{0};
// Reach's gate is its vehicle camera binding (R-V27's own): 1 Installed, 2 StockFallback, as the fixture flips it.
static std::atomic<uint8_t> g_reachBinding{1};
static bool ReachSeatAimFrameBindingInstalled() { return g_reachBinding.load() == 1; }
enum class Halo3VehicleBindingState : uint8_t { NotInstalled = 0, Installed, StockFallback };
enum class OdstVehicleBindingState : uint8_t { NotInstalled = 0, Installed, StockFallback };
static std::atomic<uint8_t> g_halo3VehicleBinding{static_cast<uint8_t>(Halo3VehicleBindingState::Installed)};
static std::atomic<uint8_t> g_odstVehicleBinding{static_cast<uint8_t>(OdstVehicleBindingState::Installed)};
static uint32_t g_tlsSlot = 3;
static uint32_t* g_engineTlsIndex = &g_tlsSlot;
static uint32_t* g_odstEngineTlsIndex = &g_tlsSlot;

static std::string g_log;
static void TestLog(const char* format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log += line;
    g_log += '\n';
}
#define LOG(...) TestLog(__VA_ARGS__)

struct ReachFrozenThread
{
    HANDLE handle = nullptr;
    DWORD threadId = 0;
    bool suspended = false;
};
static bool g_freezeFails = false;
static unsigned g_freezeCaptures = 0;
static std::vector<ReachFrozenThread> g_fakeThreads;
class ReachThreadFreeze
{
public:
    bool Capture()
    {
        ++g_freezeCaptures;
        return !g_freezeFails;
    }
    bool Release() { return true; }
    const std::vector<ReachFrozenThread>& Threads() const { return g_fakeThreads; }
};


// Halo 3's seat view (halo3_seat_view.inl in the mod): whether the seat's tag is written (the dev A/B key). The
// checks that predate it run with the tag written, the case the skip exists for.
static bool g_seatViewWritesTag = true;
bool Halo3SeatViewWritesTag() { return g_seatViewWritesTag; }

// The sight layer's rule (sight_layer.h): false while the simulation is shared and one-machine changes are not
// allowed. The checks that predate it run alone in the game.
static bool g_fireTimeChangesAllowed = true;
bool Sight_FireTimeChangesAllowed() { return g_fireTimeChangesAllowed; }

#include "../src/dll/seat_aim_world_frame.inl"

// ---- the fixture ---------------------------------------------------------------------------------------------
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

bool Logged(const char* text) { return g_log.find(text) != std::string::npos; }

constexpr size_t kModuleSize = 0x500000;   // covers every title's RVAs (Reach's skip label is 0x48AF72)

// The site's anchor bytes (its pattern up to the jne), and the strict form that also requires the jne's `0F 85`
// (a form the production anchor must not take: it cannot find a site our own jump already patched).
std::vector<uint8_t> AnchorBytes(const seat_aim_frame::Site& site)
{
    std::vector<uint8_t> out;
    const char* p = site.anchorPattern;
    for (uint32_t i = 0; i < site.anchorToJcc; ++i, p += 3)
        out.push_back(static_cast<uint8_t>(strtoul(std::string(p, 2).c_str(), nullptr, 16)));
    return out;
}

std::string StrictAnchor(const seat_aim_frame::Site& site)
{
    return std::string(site.anchorPattern, site.anchorToJcc * 3) + "0F 85";
}

struct Fixture
{
    const seat_aim_frame::Site& site;
    GameTitle title;
    int slot;
    uint8_t* base = nullptr;
    uint8_t stock[6]{};
    uint8_t skip[6]{};

    std::vector<uint8_t> anchor;

    Fixture(const seat_aim_frame::Site& s, GameTitle t, int sl) : site(s), title(t), slot(sl), anchor(AnchorBytes(s))
    {
    }
    bool Reach() const { return slot == 2; }

    uintptr_t Base() const { return reinterpret_cast<uintptr_t>(base); }
    uint8_t* Jcc() const { return base + site.jccRva; }

    void Write(uint32_t rva, const uint8_t* bytes, size_t n)
    {
        DWORD previous = 0;
        VirtualProtect(base + rva, n, PAGE_EXECUTE_READWRITE, &previous);
        memcpy(base + rva, bytes, n);
        VirtualProtect(base + rva, n, previous, &previous);
    }

    // A fresh module: int3 filler, the anchor at its RVA and the stock jne to the skip label.
    void Build()
    {
        if (!base)
            base = static_cast<uint8_t*>(VirtualAlloc(nullptr, kModuleSize, MEM_RESERVE | MEM_COMMIT,
                                                      PAGE_EXECUTE_READWRITE));
        DWORD previous = 0;
        VirtualProtect(base, kModuleSize, PAGE_EXECUTE_READWRITE, &previous);
        memset(base, 0xCC, kModuleSize);
        memcpy(base + site.anchorRva, anchor.data(), anchor.size());
        const int32_t rel = static_cast<int32_t>(site.skipRva - (site.jccRva + 6));
        stock[0] = 0x0F;
        stock[1] = 0x85;
        seat_aim_frame::WriteI32(stock + 2, rel);
        skip[0] = 0xE9;
        seat_aim_frame::WriteI32(skip + 1, rel + 1);
        skip[5] = 0x90;
        memcpy(Jcc(), stock, 6);
        VirtualProtect(base, kModuleSize, PAGE_EXECUTE_READ, &previous);
    }

    bool Is(const uint8_t bytes[6]) const { return !memcmp(Jcc(), bytes, 6); }
    bool Patched() const { return Is(skip); }
    bool Stock() const { return Is(stock); }

    bool Install() { return SeatAimFrameInstall(title, Base(), kModuleSize, g_generation[static_cast<int>(title)]); }

    bool ProtectionRestored() const
    {
        MEMORY_BASIC_INFORMATION info{};
        return VirtualQuery(Jcc(), &info, sizeof(info)) && (info.Protect & 0xFF) == PAGE_EXECUTE_READ;
    }

    const SeatAimFrameState& State() const { return g_seatAimFrame[slot]; }
};

void ResetWorld()
{
    g_config.vehicle_first_person = true;
    g_config.vehicle_hide_body = true;
    g_config.seat_aim_world_frame = 1;
    g_seatViewWritesTag = true;
    g_fireTimeChangesAllowed = true;
    g_halo3SeatNativeNoThirdPerson = 0;
    g_odstSeatNativeNoThirdPerson = 0;
    g_reachSeatNativeNoThirdPerson = 0;
    g_reachBinding = 1;
    g_halo3VehicleBinding = static_cast<uint8_t>(Halo3VehicleBindingState::Installed);
    g_odstVehicleBinding = static_cast<uint8_t>(OdstVehicleBindingState::Installed);
    g_freezeFails = false;
    g_fakeThreads.clear();
    g_log.clear();
}

std::atomic<uint32_t>& NativeFlag(const Fixture& f)
{
    return f.slot == 0 ? g_halo3SeatNativeNoThirdPerson
         : f.slot == 1 ? g_odstSeatNativeNoThirdPerson : g_reachSeatNativeNoThirdPerson;
}

std::atomic<uint8_t>& Binding(const Fixture& f)
{
    return f.slot == 0 ? g_halo3VehicleBinding : f.slot == 1 ? g_odstVehicleBinding : g_reachBinding;
}

// The setting that switches this title's seat patch (and so the skip) off: C20 reads both first-person seat keys;
// Reach's R-V27 reads vehicle_first_person alone.
void SetPatchOff(const Fixture& f, bool off)
{
    if (f.Reach())
        g_config.vehicle_first_person = !off;
    else
        g_config.vehicle_hide_body = !off;
}

void TestInstallPatchRemove(Fixture& f)
{
    ResetWorld();
    f.Build();
    Check(sig::Find(f.Base(), kModuleSize, f.site.anchorPattern) == f.Base() + f.site.anchorRva,
          "fixture: the production anchor finds the stock site");
    Check(f.Install(), "install verifies the stock site");
    Check(f.Patched() && f.State().patched, "install applies the skip: E9 rel32+1 90");
    Check(Logged("vehicle-tilt rebase is skipped"), "...and logs it");
    Check(Logged("NOTE (co-op host)"), "...with the co-op limit of the global skip (once per title)");
    Check(f.ProtectionRestored(), "...and puts the page protection back");
    Check(f.Install() && f.Patched(), "a repeat install of the same mapping keeps the patch");

    g_log.clear();
    g_config.vehicle_hide_body = false;
    SeatAimFrameSync();
    if (f.Reach())
        Check(f.Patched() && f.State().patched, "Reach: vehicle_hide_body is not R-V27's key: still skipped");
    else
    {
        Check(f.Stock() && !f.State().patched, "vehicle_hide_body off: the stock jne is back");
        Check(Logged("back in place") && Logged("vehicle_hide_body off"), "...logged with the reason");
    }
    g_config.vehicle_hide_body = true;
    SeatAimFrameSync();
    Check(f.Patched(), "vehicle_hide_body on again: skipped again");
    g_log.clear();
    g_config.vehicle_first_person = false;
    SeatAimFrameSync();
    Check(f.Stock(), "vehicle_first_person off: the stock jne");
    Check(Logged("vehicle_first_person"), "...logged with the reason");
    g_config.vehicle_first_person = true;
    SeatAimFrameSync();
    Check(f.Patched(), "...and back");

    // Halo 3 with the seat's tag left as loaded (the mod's default): the game does not rebase such a seat, so
    // there is nothing to skip and the stock jne stays. ODST and Reach still write their tags.
    g_log.clear();
    g_seatViewWritesTag = false;
    SeatAimFrameSync();
    if (f.slot == 0)
    {
        Check(f.Stock() && !f.State().patched, "Halo 3, tag as loaded: the stock jne is back");
        Check(Logged("tag flag as loaded"), "...logged with the reason");
    }
    else
        Check(f.Patched(), "ODST / Reach: Halo 3's seat view does not concern them");
    g_seatViewWritesTag = true;
    SeatAimFrameSync();
    Check(f.Patched(), "...and with the tag written again, skipped again");

    g_log.clear();
    NativeFlag(f) = 0x80000000u | 0x12u << 8 | 1u;
    SeatAimFrameSync();
    Check(f.Stock(), "a seat natively lacking bit 4: the game's rebase, as stock");
    Check(Logged("natively lacks bit 4"), "...and the seat is logged");
    NativeFlag(f) = 0;
    SeatAimFrameSync();
    Check(f.Patched(), "the seat is left: skipped again");

    g_log.clear();
    Binding(f) = static_cast<uint8_t>(2);   // StockFallback: the sampler faulted and was disarmed
    SeatAimFrameSync();
    Check(f.Stock(), "the vehicle sampler disarmed: the skip stands down");
    Check(Logged(f.Reach() ? "vehicle camera binding is not installed" : "vehicle sampler is not bound"),
          "...logged with the reason");
    Binding(f) = static_cast<uint8_t>(1);
    SeatAimFrameSync();
    Check(f.Patched(), "(fixture) the binding back: skipped again");

    // A shared simulation: no seat patch clears a seat's camera flag there, so the skip has nothing to undo, and
    // the game's own rebase is what every machine runs -- whatever seat is occupied.
    g_log.clear();
    g_fireTimeChangesAllowed = false;
    SeatAimFrameSync();
    Check(f.Stock() && !f.State().patched, "a shared simulation: the stock jne is back");
    Check(Logged("back in place") && Logged("Co-op safe is on"), "...logged with the reason");
    NativeFlag(f) = 0x80000000u | 0x12u << 8 | 1u;
    SeatAimFrameSync();
    Check(f.Stock(), "...and it stays with a natively bit-4-less seat occupied");
    NativeFlag(f) = 0;
    SeatAimFrameSync();
    Check(f.Stock(), "...and with it left again");
    g_fireTimeChangesAllowed = true;
    SeatAimFrameSync();
    Check(f.Patched(), "alone in the game again: skipped again");

#if HALOMCCVR_DEV_DIAGNOSTICS
    g_log.clear();
    g_config.seat_aim_world_frame = 0;
    SeatAimFrameSync();
    Check(f.Stock() && Logged("dev A/B"), "dev: seat_aim_world_frame = 0 leaves the game's rebase");
    g_config.seat_aim_world_frame = 1;
    SeatAimFrameSync();
    Check(f.Patched(), "dev: the A/B key back to 1 skips again");
#endif

    g_log.clear();
    Check(SeatAimFrameRemove(f.title), "teardown restores");
    Check(f.Stock() && !f.State().jcc, "...the stock jne, and forgets the site");
    Check(Logged("restored at teardown"), "...and logs it");
    Check(f.ProtectionRestored(), "...and the page protection");
    Check(SeatAimFrameRemove(f.title) && f.Stock(), "a second teardown is a no-op");
}

void TestNotWantedAtInstall(Fixture& f)
{
    ResetWorld();
    f.Build();
    g_config.vehicle_first_person = false;
    Check(f.Install() && f.Stock() && !f.State().patched, "installed with the seat patch off: verified, not applied");
    Check(Logged("verified at") && Logged("not applied"), "...and says so");
    g_config.vehicle_first_person = true;
    SeatAimFrameSync();
    Check(f.Patched(), "the seat patch switched on: applied by the poll");
    Check(SeatAimFrameRemove(f.title) && f.Stock(), "teardown");

    // Installed into a shared simulation: verified, and the game's bytes are never written.
    ResetWorld();
    f.Build();
    g_fireTimeChangesAllowed = false;
    Check(f.Install() && f.Stock() && !f.State().patched, "installed in a shared simulation: verified, not applied");
    SeatAimFrameSync();
    Check(f.Stock(), "...and the poll leaves it");
    g_fireTimeChangesAllowed = true;
    Check(SeatAimFrameRemove(f.title) && f.Stock(), "teardown");
}

void TestBusy(Fixture& f)
{
    ResetWorld();
    f.Build();
    Check(f.Install() && f.Patched(), "busy: installed");
    g_log.clear();
    g_freezeFails = true;
    SetPatchOff(f, true);
    SeatAimFrameSync();
    Check(f.Patched() && f.State().patched, "no thread freeze: nothing is written");
    Check(Logged("restore is pending"), "...the pending restore is logged");
    const size_t logged = g_log.size();
    SeatAimFrameSync();
    Check(g_log.size() == logged, "...once");
    g_freezeFails = false;
    SeatAimFrameSync();
    Check(f.Stock() && !f.State().patched, "the freeze available again: the restore lands");
    Check(SeatAimFrameRemove(f.title), "teardown");
}

DWORD WINAPI NeverRuns(void*) { return 0; }

void TestThreadInsideBytes(Fixture& f)
{
    ResetWorld();
    f.Build();
    Check(f.Install() && f.Patched(), "thread check: installed");
    DWORD id = 0;
    HANDLE thread = CreateThread(nullptr, 0, &NeverRuns, nullptr, CREATE_SUSPENDED, &id);
    Check(thread != nullptr, "a suspended thread for the RIP check");
    if (!thread)
        return;
    auto parkAt = [&](uint8_t* rip) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        GetThreadContext(thread, &context);
        context.Rip = reinterpret_cast<DWORD64>(rip);
        return SetThreadContext(thread, &context) != 0;
    };
    g_fakeThreads = {{thread, id, true}};

    Check(parkAt(f.Jcc() + 2), "park the thread inside the jne");
    SetPatchOff(f, true);
    SeatAimFrameSync();
    Check(f.Patched() && f.State().patched, "a thread inside the six bytes: nothing is written (retried)");
    Check(parkAt(f.Jcc() + 5), "park the thread on the nop");
    SeatAimFrameSync();
    Check(f.Patched(), "...still inside: still nothing written");
    Check(parkAt(f.Jcc()), "park the thread AT the instruction");
    SeatAimFrameSync();
    Check(f.Stock(), "a thread at the instruction start just runs the new bytes: written");
    Check(parkAt(f.Jcc() + 6), "park the thread just past it");
    SetPatchOff(f, false);
    SeatAimFrameSync();
    Check(f.Patched(), "a thread past the bytes: written");

    g_fakeThreads.clear();
    TerminateThread(thread, 0);
    CloseHandle(thread);
    Check(SeatAimFrameRemove(f.title) && f.Stock(), "teardown");
}

// THE FINDING-1 CASE: a teardown gave up (no thread freeze for 20 tries) and left our jump in place. The next
// install of the same mapping must find the site, recognise the jump as ours and manage it again.
void TestAdoptLeftover(Fixture& f)
{
    ResetWorld();
    f.Build();
    Check(f.Install() && f.Patched(), "adopt: installed and patched");
    g_freezeFails = true;
    unsigned refused = 0;
    while (!SeatAimFrameRemove(f.title) && refused < 100)
        ++refused;
    Check(refused == 19, "teardown retries 19 times without a thread freeze, then gives up");
    Check(f.Patched() && !f.State().jcc, "...leaving our jump in place, unmanaged");
    Check(Logged("could not be restored at teardown"), "...and says so");
    g_freezeFails = false;

    Check(sig::Find(f.Base(), kModuleSize, StrictAnchor(f.site).c_str()) == 0,
          "witness: a strict anchor (`... 0F 85`) cannot find the patched site");
    Check(sig::Find(f.Base(), kModuleSize, f.site.anchorPattern) == f.Base() + f.site.anchorRva,
          "the production anchor still finds it");

    g_log.clear();
    Check(f.Install(), "the next install of the same mapping verifies the site");
    Check(!Logged("NOT skipped"), "...without refusing it");
    Check(f.State().jcc && f.State().patched && f.Patched(), "...adopts our jump as patched");
    Check(!memcmp(f.State().stock, f.stock, 6), "...and rebuilds the exact stock jne from it");
    SetPatchOff(f, true);
    SeatAimFrameSync();
    Check(f.Stock(), "the adopted jump is managed: the setting puts the stock jne back");
    SetPatchOff(f, false);
    SeatAimFrameSync();
    Check(f.Patched(), "...and skips again");
    Check(SeatAimFrameRemove(f.title) && f.Stock(), "...and teardown restores it");

    // A leftover adopted while the skip is NOT wanted is restored at once.
    ResetWorld();
    f.Write(f.site.jccRva, f.skip, 6);
    g_config.vehicle_first_person = false;
    Check(f.Install() && !Logged("NOT skipped"), "a leftover jump with the seat patch off: adopted");
    Check(f.Stock() && !f.State().patched, "...and the stock jne goes straight back");
    Check(SeatAimFrameRemove(f.title), "teardown");
}

void TestRefusals(Fixture& f)
{
    struct Foreign
    {
        const char* what;
        uint8_t bytes[6];
    };
    const int32_t rel = static_cast<int32_t>(f.site.skipRva - (f.site.jccRva + 6));
    Foreign cases[5]{};
    cases[0] = {"refuses a je (0F 84) at the site", {0x0F, 0x84}};
    seat_aim_frame::WriteI32(cases[0].bytes + 2, rel);
    cases[1] = {"refuses a jne to another target", {0x0F, 0x85}};
    seat_aim_frame::WriteI32(cases[1].bytes + 2, rel + 4);
    cases[2] = {"refuses a jmp to another target (someone else's hook)", {0xE9}};
    seat_aim_frame::WriteI32(cases[2].bytes + 1, rel + 0x40);
    cases[2].bytes[5] = 0x90;
    cases[3] = {"refuses our jmp without our nop", {0xE9}};
    seat_aim_frame::WriteI32(cases[3].bytes + 1, rel + 1);
    cases[3].bytes[5] = 0xCC;
    cases[4] = {"refuses int3 filler", {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}};
    for (const Foreign& c : cases)
    {
        ResetWorld();
        f.Build();
        f.Write(f.site.jccRva, c.bytes, 6);
        const bool installed = f.Install();
        Check(!installed && f.Is(c.bytes) && !f.State().jcc, c.what);
        Check(Logged("NOT skipped") && Logged("neither the stock"), "...with one NOT skipped line naming why");
    }

    ResetWorld();
    f.Build();
    f.Write(f.site.anchorRva + 0x30, f.anchor.data(), f.anchor.size());
    Check(!f.Install() && f.Stock() && Logged("ambiguous"), "refuses a second copy of the anchor");
    ResetWorld();
    f.Build();
    const uint8_t filler[1]{0xCC};
    f.Write(f.site.anchorRva, filler, 1);
    f.Write(f.site.anchorRva + 0x40, f.anchor.data(), f.anchor.size());
    Check(!f.Install() && Logged("ambiguous or moved"), "refuses an anchor that moved");
    ResetWorld();
    Check(!SeatAimFrameInstall(f.title, 0, kModuleSize, 7) && Logged("not loaded"), "refuses a module not loaded");
    ResetWorld();
    Check(!SeatAimFrameInstall(f.title, f.Base(), f.site.jccRva + 3, g_generation[static_cast<int>(f.title)]) &&
              Logged("not loaded"),
          "refuses a module too small for the site");
}

void TestForeignAfterInstall(Fixture& f)
{
    ResetWorld();
    f.Build();
    Check(f.Install() && f.Patched(), "foreign later: installed");
    uint8_t theirs[6]{0xE9, 0x10, 0x20, 0x30, 0x00, 0xCC};
    f.Write(f.site.jccRva, theirs, 6);
    g_log.clear();
    SetPatchOff(f, true);
    SeatAimFrameSync();
    Check(f.Is(theirs), "bytes changed under us: left alone");
    Check(!f.State().jcc && Logged("no longer the ones verified"), "...the site is forgotten, and logged");
    Check(SeatAimFrameRemove(f.title) && f.Is(theirs), "teardown then touches nothing");
}

void TestGenerationChange(Fixture& f)
{
    ResetWorld();
    f.Build();
    Check(f.Install() && f.Patched(), "generation: installed");
    uint32_t& generation = g_generation[static_cast<int>(f.title)];
    ++generation;
    SetPatchOff(f, true);
    SeatAimFrameSync();
    Check(f.Patched() && !f.State().jcc && Logged("generation changed"),
          "a replaced module generation: forgotten without writing");
    Check(SeatAimFrameRemove(f.title) && f.Patched(), "teardown then touches nothing");
    --generation;
}

} // namespace

int main()
{
    Fixture halo3(seat_aim_frame::kHalo3Site, GameTitle::Halo3, 0);
    Fixture odst(seat_aim_frame::kOdstSite, GameTitle::Halo3ODST, 1);
    Fixture reach(seat_aim_frame::kReachSite, GameTitle::HaloReach, 2);
    for (Fixture* f : {&halo3, &odst, &reach})
    {
        TestInstallPatchRemove(*f);
        TestNotWantedAtInstall(*f);
        TestBusy(*f);
        TestThreadInsideBytes(*f);
        TestAdoptLeftover(*f);
        TestRefusals(*f);
        TestForeignAfterInstall(*f);
        TestGenerationChange(*f);
    }
    Check(g_freezeCaptures > 0, "the freeze was taken for every write");
    VirtualFree(halo3.base, 0, MEM_RELEASE);
    VirtualFree(odst.base, 0, MEM_RELEASE);
    VirtualFree(reach.base, 0, MEM_RELEASE);
    if (failures)
    {
        std::printf("%u of %u checks FAILED\n", failures, checks);
        return 1;
    }
    std::printf("PASS: %u seat aim world-frame install checks (%s flavour)\n", checks,
                HALOMCCVR_DEV_DIAGNOSTICS ? "dev" : "player");
    return 0;
}
