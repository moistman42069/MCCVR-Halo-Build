// The Halo 3 seat view's PRODUCTION install / setting sync / teardown (src/dll/halo3_seat_view.inl), compiled
// here against a fixture: a VirtualAlloc'd executable "module" holding the seat's third-person test and the three
// director call sites at the retail RVAs, each site a callable thunk, so a redirected site is RUN and its answer
// read. Covers install -> redirect -> restore, the setting, a missing thread freeze, foreign bytes at a site
// (at install and after it), a thread inside the relay, a teardown that cannot finish in its time, a changed
// title generation with the module still mapped and with it gone, and an install that has to wait for another
// generation's relay.
//
// The thread freeze (ReachThreadFreeze, game.cpp) is stubbed: Capture() can be made to fail (the Busy path).
// Built twice by CMake: the player flavour and the dev flavour (HALOMCCVR_DEV_DIAGNOSTICS=1, the A/B key).
#include <Windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../src/common/seat_view_logic.h"

#ifndef HALOMCCVR_DEV_DIAGNOSTICS
#define HALOMCCVR_DEV_DIAGNOSTICS 0
#endif

// ---- the game.cpp surface the .inl uses ---------------------------------------------------------------------
enum class GameTitle { None, Halo3 };
static uint32_t g_generation = 7;
uint32_t TitleAdapter_GetGeneration(GameTitle) { return g_generation; }

static struct
{
    bool vehicle_first_person = true;
    bool vehicle_hide_body = true;
    bool vehicle_cameras = false;
    int seat_view_tag_write = 0;
} g_config;

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
static unsigned g_freezes = 0;   // successful captures: one is one whole-process freeze
static std::vector<ReachFrozenThread> g_fakeThreads;
class ReachThreadFreeze
{
public:
    bool Capture() { if (g_freezeFails) return false; ++g_freezes; return true; }
    bool Release() { return true; }
    const std::vector<ReachFrozenThread>& Threads() const { return g_fakeThreads; }
};

// The two helpers the seat view borrows from seat_aim_world_frame.inl. The executable test is the shipped one;
// the signature test is the fixture's switch.
static bool g_signaturesMatch = true;
bool SeatAimFrameUniqueAt(uintptr_t, size_t, uint32_t, const char*) { return g_signaturesMatch; }
bool SeatAimFrameExecutable(const void* address, size_t bytes)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
        return false;
    const auto end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (reinterpret_cast<uintptr_t>(address) + bytes > end)
        return false;
    const DWORD protection = info.Protect & 0xFFu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool Halo3SeatViewInstall(uintptr_t base, size_t size, uint32_t generation);
bool Halo3SeatViewRemove();
void Halo3SeatViewSync();
bool Halo3SeatViewAvailable();
bool Halo3SeatViewWritesTag();
void Halo3SeatViewArm(int32_t unit);
void Halo3SeatViewDisarm();
void Halo3SeatViewArmChase(int32_t unit);

#include "../src/dll/halo3_hook_watch_mark.h"
#include "../src/dll/halo3_seat_view.inl"

// ---- the fixture ---------------------------------------------------------------------------------------------
static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n--- log ---\n%s", message, g_log.c_str());
        std::exit(1);
    }
}
static bool Logged(const char* text) { return g_log.find(text) != std::string::npos; }

constexpr size_t kModuleSize = 0x214000;
constexpr int32_t kUnit = 0x00E30178, kOtherUnit = 0x00E30199;
using SiteFn = int (*)(int, int*);

struct Module
{
    uint8_t* base = nullptr;

    uint8_t* Call(size_t i) const
    {
        return base + seat_view::kHalo3DirectorSites[i].anchorRva + seat_view::kHalo3DirectorSites[i].callOffset;
    }
    // A site as something that can be called: `sub rsp, 0x28; call <the third-person test>; add rsp, 0x28; ret`,
    // the call at the site's own RVA.
    void WriteSite(size_t i, uint32_t targetRva) const
    {
        uint8_t* call = Call(i);
        const uint8_t before[]{0x48, 0x83, 0xEC, 0x28};
        const uint8_t after[]{0x48, 0x83, 0xC4, 0x28, 0xC3};
        std::memcpy(call - sizeof(before), before, sizeof(before));
        Check(seat_view::BuildCall(reinterpret_cast<uintptr_t>(call), reinterpret_cast<uintptr_t>(base) + targetRva,
                                   call), "a site's own call is in reach");
        std::memcpy(call + seat_view::kCallBytes, after, sizeof(after));
    }
    void Build()
    {
        base = static_cast<uint8_t*>(VirtualAlloc(nullptr, kModuleSize, MEM_RESERVE | MEM_COMMIT,
                                                  PAGE_EXECUTE_READWRITE));
        Check(base != nullptr, "fixture module");
        // The seat's third-person test: settled (state 2), third person (1).
        const uint8_t test[]{0xC7, 0x02, 0x02, 0x00, 0x00, 0x00, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};
        std::memcpy(base + seat_view::kHalo3ThirdPersonRva, test, sizeof(test));
        // Another function a site must never be taken to call: first person (0).
        const uint8_t other[]{0xC7, 0x02, 0x02, 0x00, 0x00, 0x00, 0x31, 0xC0, 0xC3};
        std::memcpy(base + 0x1000, other, sizeof(other));
        for (size_t i = 0; i < seat_view::kDirectorSiteCount; ++i)
            WriteSite(i, seat_view::kHalo3ThirdPersonRva);
        FlushInstructionCache(GetCurrentProcess(), base, kModuleSize);
    }
    void Free()
    {
        Check(VirtualFree(base, 0, MEM_RELEASE) != 0, "fixture module freed");
        base = nullptr;
    }
    uintptr_t Base() const { return reinterpret_cast<uintptr_t>(base); }
    // What a director call at site `i` is told for `unit`.
    int Ask(size_t i, int unit) const
    {
        int state = -1;
        return reinterpret_cast<SiteFn>(Call(i) - 4)(unit, &state);
    }
    bool Stock(size_t i) const
    {
        uintptr_t target = 0;
        return seat_view::ReadCall(Call(i), reinterpret_cast<uintptr_t>(Call(i)), target) &&
            target == Base() + seat_view::kHalo3ThirdPersonRva;
    }
    bool AllStock() const { return Stock(0) && Stock(1) && Stock(2); }
    bool NoneStock() const { return !Stock(0) && !Stock(1) && !Stock(2); }
    // Every site answers first person for the armed unit and the game's own answer for another.
    bool AnswersFirstPerson() const
    {
        for (size_t i = 0; i < seat_view::kDirectorSiteCount; ++i)
            if (Ask(i, kUnit) != seat_view::kFirstPerson || Ask(i, kOtherUnit) != seat_view::kThirdPerson)
                return false;
        return true;
    }
    bool AnswersAll(int unit, int answer) const
    {
        for (size_t i = 0; i < seat_view::kDirectorSiteCount; ++i)
            if (Ask(i, unit) != answer)
                return false;
        return true;
    }
    bool AnswersStock() const
    {
        for (size_t i = 0; i < seat_view::kDirectorSiteCount; ++i)
            if (Ask(i, kUnit) != seat_view::kThirdPerson)
                return false;
        return true;
    }
};

static void ResetWorld()
{
    g_config.vehicle_first_person = g_config.vehicle_hide_body = true;
    g_config.vehicle_cameras = false;
    g_config.seat_view_tag_write = 0;
    g_generation = 7;
    g_freezeFails = false;
    g_signaturesMatch = true;
    g_log.clear();
    InterlockedExchange(&g_halo3SeatViewInside, 0);
    Check(!g_halo3SeatView.relay, "the last test left no relay behind");
}

static void TestInstallRedirectRestore()
{
    ResetWorld();
    Module m;
    m.Build();
    Check(m.AnswersStock(), "the fixture's own sites call the third-person test");
    g_freezes = 0;
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && Halo3SeatViewAvailable() && m.NoneStock(),
          "install redirects the three sites");
    Check(g_freezes == 1, "...under one freeze of the other threads, not one a site");
    Check(Logged("answered first person"), "...and says so");
    Check(m.AnswersStock(), "nobody armed: the game's own answer through the relay");
    Halo3SeatViewArm(kUnit);
    Check(m.AnswersFirstPerson(), "the armed occupant is answered first person at every site, nobody else");
    Halo3SeatViewDisarm();
    Check(m.AnswersStock(), "disarmed: the game's own answer again");
    // A seat that is first person in its own data: the occupant shown from a chase camera is told third person.
    m.base[seat_view::kHalo3ThirdPersonRva + 7] = 0;   // the fixture's test now answers first person (0)
    FlushInstructionCache(GetCurrentProcess(), m.base, kModuleSize);
    Check(m.AnswersAll(kUnit, seat_view::kFirstPerson), "a first-person seat: the game's own answer through the relay");
    Halo3SeatViewArmChase(kUnit);
    Halo3SeatViewSync();
    Check(!Logged("told third person for unit"), "armed but nothing asked: the log does not claim it");
    Check(m.AnswersAll(kUnit, seat_view::kThirdPerson) && m.AnswersAll(kOtherUnit, seat_view::kFirstPerson),
          "the chase camera's occupant is answered third person at every site, nobody else");
    Halo3SeatViewSync();
    Check(Logged("told third person for unit"), "...and the log says so once");
    Halo3SeatViewDisarm();
    Check(m.AnswersAll(kUnit, seat_view::kFirstPerson), "disarmed: the game's first-person answer again");
    Halo3SeatViewSync();
    Check(Logged("the game's own answer again"), "...and the log says so");
    m.base[seat_view::kHalo3ThirdPersonRva + 7] = 1;
    FlushInstructionCache(GetCurrentProcess(), m.base, kModuleSize);
    Check(g_halo3SeatViewInside == 0, "no thread is counted inside the relay after its calls");

    // The setting.
    g_log.clear();
    g_config.vehicle_hide_body = false;
    Halo3SeatViewSync();
    Check(!Halo3SeatViewAvailable() && m.AllStock() && Logged("back to the game's own"),
          "the setting off puts the game's own calls back");
    g_config.vehicle_hide_body = true;
    g_freezeFails = true;
    g_log.clear();
    Halo3SeatViewSync();
    Check(!Halo3SeatViewAvailable() && m.AllStock() && Logged("pending"), "no thread freeze: nothing is written");
    g_freezeFails = false;
    Halo3SeatViewSync();
    Check(Halo3SeatViewAvailable() && m.NoneStock(), "...and the next poll redirects");

#if HALOMCCVR_DEV_DIAGNOSTICS
    g_config.seat_view_tag_write = 1;
    Halo3SeatViewSync();
    Check(Halo3SeatViewWritesTag() && !Halo3SeatViewAvailable() && m.AllStock(),
          "the dev key hands the seat back to the tag write: the sites are stock");
    g_config.seat_view_tag_write = 0;
    Halo3SeatViewSync();
    Check(Halo3SeatViewAvailable(), "...and back");
#else
    g_config.seat_view_tag_write = 1;
    Halo3SeatViewSync();
    Check(!Halo3SeatViewWritesTag() && Halo3SeatViewAvailable(), "a player build never writes the tag");
    g_config.seat_view_tag_write = 0;
#endif

    // Teardown.
    Halo3SeatViewArm(kUnit);
    g_log.clear();
    Check(Halo3SeatViewRemove() && m.AllStock() && !Halo3SeatViewAvailable() && !g_halo3SeatView.relay &&
              Logged("released at teardown"),
          "teardown restores the sites and releases the relay");
    Check(g_halo3SeatViewUnit.load() == -1 && m.AnswersStock(), "...and disarms");
    Check(Halo3SeatViewRemove(), "a second teardown has nothing to do");
    m.Free();
}

static void TestInstallRefusals()
{
    ResetWorld();
    Module m;
    m.Build();
    g_signaturesMatch = false;
    Check(!Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && m.AllStock() && !g_halo3SeatView.relay &&
              Logged("Nothing was written"),
          "code around a site that does not match: nothing is written");
    g_signaturesMatch = true;
    m.WriteSite(1, 0x1000);
    g_log.clear();
    Check(!Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && !g_halo3SeatView.relay &&
              Logged("no longer calls the seat's third-person test") && m.Stock(0) && m.Stock(2),
          "a site that calls another function: nothing is written");
    m.WriteSite(1, seat_view::kHalo3ThirdPersonRva);
    Check(!Halo3SeatViewInstall(m.Base(), kModuleSize, 0), "no generation: refused");
    Check(!Halo3SeatViewInstall(m.Base(), 0x1000, 7), "a module too small to hold the sites: refused");
    g_config.vehicle_first_person = false;
    g_log.clear();
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && !Halo3SeatViewAvailable() && m.AllStock() &&
              Logged("verified but not redirected"),
          "the setting off at install: verified, not redirected");
    g_config.vehicle_first_person = true;
    Halo3SeatViewSync();
    Check(Halo3SeatViewAvailable(), "...and redirected when it comes on");
    Check(Halo3SeatViewRemove(), "teardown");
    m.Free();
}

static void TestForeignSite()
{
    ResetWorld();
    Module m;
    m.Build();
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && Halo3SeatViewAvailable(), "installed");
    // Someone else rewrites a redirected site.
    m.WriteSite(1, 0x1000);
    g_config.vehicle_hide_body = false;
    g_log.clear();
    Halo3SeatViewSync();
    uintptr_t target = 0;
    Check(m.Stock(0) && m.Stock(2) && seat_view::ReadCall(m.Call(1), reinterpret_cast<uintptr_t>(m.Call(1)), target) &&
              target == m.Base() + 0x1000 && Logged("no longer the redirect this mod wrote"),
          "the two sites that are still ours go back; the foreign one is left as it is");
    g_config.vehicle_hide_body = true;
    Halo3SeatViewSync();
    Check(!Halo3SeatViewAvailable() && m.Stock(0) && m.Stock(2), "nothing more is redirected in this module");
    Check(Halo3SeatViewRemove() && !g_halo3SeatView.relay, "teardown releases the relay");
    m.Free();
}

static void TestTeardownThatCannotFinish()
{
    ResetWorld();
    Module m;
    m.Build();
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && Halo3SeatViewAvailable(), "installed");
    Halo3SeatViewArm(kUnit);
    // No thread freeze for the whole of the teardown's time: the sites cannot be put back.
    g_freezeFails = true;
    for (uint32_t i = 1; i < kHalo3SeatViewRemoveTries; ++i)
        Check(!Halo3SeatViewRemove(), "not yet: the teardown is asked to try again");
    g_log.clear();
    Check(Halo3SeatViewRemove() && Logged("could not be retired"), "its time is up: the teardown goes on");
    Check(g_halo3SeatView.relay && g_halo3SeatView.retired && m.NoneStock() && !Halo3SeatViewAvailable(),
          "the relay and the record of its sites are KEPT while the sites still call it");
    Check(m.AnswersStock(), "the relay still runs, and answers the game's own answer");
    Check(Halo3SeatViewRemove(), "a later teardown call does not start the count again");
    Halo3SeatViewSync();
    Check(m.NoneStock() && g_halo3SeatView.relay && !Halo3SeatViewAvailable(),
          "a poll that still cannot write leaves it so, and never redirects a retired module");
    // The freeze comes back: one poll puts the sites back, and the relay goes.
    g_freezeFails = false;
    g_log.clear();
    Halo3SeatViewSync();
    Check(m.AllStock(), "the poll puts the sites back");
    Halo3SeatViewSync();
    Check(!g_halo3SeatView.relay && Logged("relay released"), "...and releases the relay");
    Check(m.AnswersStock(), "the game's own calls run");

    // The same, but the module is installed again before the poll could finish: the relay is taken up.
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && Halo3SeatViewAvailable(), "installed again");
    const uint8_t* const relay = g_halo3SeatView.relay;
    g_freezeFails = true;
    for (uint32_t i = 0; i < kHalo3SeatViewRemoveTries; ++i)
        (void)Halo3SeatViewRemove();
    Check(g_halo3SeatView.retired && m.NoneStock(), "retired with its sites still redirected");
    g_freezeFails = false;
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && Halo3SeatViewAvailable() &&
              g_halo3SeatView.relay == relay && !g_halo3SeatView.retired,
          "a new install of the same module takes the same relay up");
    Halo3SeatViewArm(kUnit);
    Check(m.AnswersFirstPerson(), "...and it answers again");
    Check(Halo3SeatViewRemove() && m.AllStock() && !g_halo3SeatView.relay, "teardown");
    m.Free();
}

static void TestThreadInsideTheRelay()
{
    ResetWorld();
    Module m;
    m.Build();
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7), "installed");
    InterlockedExchange(&g_halo3SeatViewInside, 1);
    Check(!Halo3SeatViewRemove() && m.AllStock() && g_halo3SeatView.relay,
          "a thread inside the relay: the sites go back, the relay is not released");
    for (uint32_t i = 2; i < kHalo3SeatViewRemoveTries; ++i)
        Check(!Halo3SeatViewRemove(), "not yet");
    Check(Halo3SeatViewRemove() && g_halo3SeatView.relay && g_halo3SeatView.retired,
          "its time is up: the relay is left in place, and still known");
    Halo3SeatViewSync();
    Check(g_halo3SeatView.relay != nullptr, "a poll with the thread still inside releases nothing");
    InterlockedExchange(&g_halo3SeatViewInside, 0);
    Halo3SeatViewSync();
    Check(!g_halo3SeatView.relay, "the thread has left: the poll releases the relay");
    m.Free();
}

static void TestChangedGeneration()
{
    // The module still mapped and running under another generation: its sites are put back before the relay goes.
    ResetWorld();
    Module m;
    m.Build();
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && m.NoneStock(), "installed");
    g_generation = 8;
    g_freezeFails = true;
    g_log.clear();
    Halo3SeatViewSync();
    Check(g_halo3SeatView.relay && m.NoneStock() && !Halo3SeatViewAvailable() && Logged("pending"),
          "another generation and no thread freeze: the relay stays while its sites call it");
    Check(m.AnswersStock(), "...and those sites still run");
    g_freezeFails = false;
    g_log.clear();
    Halo3SeatViewSync();
    Check(m.AllStock() && !g_halo3SeatView.relay && Logged("it is released"),
          "the sites are put back, and only then is the relay released");
    Check(m.AnswersStock(), "the game's own calls run");

    // An install for the new generation that arrives while the old relay is still held waits, and is taken up.
    g_generation = 7;
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && Halo3SeatViewAvailable(), "installed at 7");
    g_generation = 8;
    g_freezeFails = true;
    Check(!Halo3SeatViewInstall(m.Base(), kModuleSize, 8) && g_halo3SeatView.generation == 7 && m.NoneStock(),
          "an install for 8 while 7's sites cannot be put back: refused for now");
    g_freezeFails = false;
    Halo3SeatViewSync();
    Check(g_halo3SeatView.generation == 8 && Halo3SeatViewAvailable() && m.NoneStock(),
          "the poll retires 7 and takes the install for 8 up");
    Halo3SeatViewArm(kUnit);
    Check(m.AnswersFirstPerson(), "...and it answers");
    Check(Halo3SeatViewRemove() && m.AllStock(), "teardown");

    // The module gone: nothing is written, nothing is read past a guard, and the relay goes.
    g_generation = 7;
    Check(Halo3SeatViewInstall(m.Base(), kModuleSize, 7) && m.NoneStock(), "installed");
    m.Free();
    g_generation = 9;
    g_log.clear();
    Halo3SeatViewSync();
    Check(!g_halo3SeatView.relay && !Halo3SeatViewAvailable() && Logged("it is released"),
          "the module unmapped under another generation: forgotten without a write");
    g_generation = 7;
}

int main()
{
    TestInstallRedirectRestore();
    TestInstallRefusals();
    TestForeignSite();
    TestTeardownThatCannotFinish();
    TestThreadInsideTheRelay();
    TestChangedGeneration();
    std::printf("PASS: %u Halo 3 seat view checks (%s flavour)\n", g_checks,
                HALOMCCVR_DEV_DIAGNOSTICS ? "dev" : "player");
    return 0;
}
