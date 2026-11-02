// Included in game.cpp at file scope, after legacy_direct_drive.inl (whose publication says where the player is).
//
// "INSTALLED" IS NOT "RUNNING", FOR HALO 3'S OWN HOOKS (drive_cascade.h: HookWatch, HookWatchBoard). MinHook installs
// on any address and reports success; the only proof that a signature found the right function is the game calling
// it. Each hook below is watched as the direct drive's control update is: its detour marks a flag on entry
// (Halo3HookCalled, halo3_hook_watch_mark.h), and the cold worker counts the time in which the game must have
// called it.
//
// WHAT MAY BE WATCHED. A watch counts only time in which the call was POSSIBLE at every pass, from state that the
// watched hook does not produce, and a false "never called" is worse than none. So a hook is watched only where
// the game calls it every frame in a state the worker can see:
//   * IN PLAY: Halo 3 is the active title in gameplay, its camera hook's cinematic publication says the player has
//     control (DriveWatchPlayerInControl: not a cutscene, not a load), the game's own pause flag reads "not
//     paused", and the direct drive's aim publication (made on the input thread from the game's own camera and
//     aim) is valid and at most 250 ms old: the player controls a unit (not a film, not a free camera). The
//     camera-copy hook behind the first is the reference clock and is not itself watched: when it is not called,
//     the title never leaves its load and nothing counts. With the direct drive off there is no publication and
//     nothing counts either.
//   * ON FOOT / SEATED: in play, and that publication says on foot, or in a seat.
//   * THE FIRST-PERSON VIEW / THE HUD IS BEING DRAWN: on foot, and the first-person render driver (resp. the HUD
//     widget draw) was called since the last pass. The hooks the game runs inside those -- the first-person camera
//     rebuild and palette, the HUD's anchor, placement and crosshair predicate -- are watched from the driver's own
//     calls, so a skull or a mode that hides the whole first-person view or HUD counts against none of them. The
//     driver and the widget draw are each watched from the other: one of them called on foot is the game drawing
//     the frame they share, and both silent (Blind) counts against neither.
// A hook is called once at least in every such state, or its watch fires once per install; one call since the
// install settles it for good, so a level that starts in a state the watch does not know (zoomed, in a seat) only
// delays the verdict.
//
// NOT WATCHED, and why (each would need a "possible" this worker cannot show):
//   * called per event -- a round fired (dual aim's firing scope, aim helper, acquisition query and camera; the
//     muzzle markers; the weapon-fire, damage-effect, player-effect, effect-worker and haptic-evaluator hooks of the
//     haptics lane), damage dealt (contact melee's damage hook), a melee strike (melee selection);
//   * called per thing on screen -- the tracker's contact builders and its blip draw (no contact, or the tracker
//     hidden, means no call), the avatar's eight render-model hooks and the part draw's bind and uploads (which
//     objects and materials are drawn);
//   * cadence not established -- the brightness publisher (it may run only when the screen's colour constants are
//     rebuilt) and the special-bones composer (not shown to run for every first-person frame);
//   * not a detour of ours that the game calls -- the camera copy (the reference above), the first-person root and
//     sway call-site redirects (a pass-through kept stable, and a hand-assembled shim that may clobber only rax),
//     and the byte patches (native weapon IK, the CHUD class gate, the seat aim frame);
//   * the direct drive's control update, which has its own watch (legacy_direct_drive.inl), and the dev-only
//     witnesses, which do not ship.
//
// Log-only. Nothing stands down on a verdict: every feature here already degrades to stock when its hook does
// nothing. Cost: the detours' one relaxed load (and a store once a pass); the worker reads and clears 18 bytes and
// one publication a loop. The first-person and HUD flags are cleared every pass, so they mean "called since the
// last pass" for the watches that depend on them, and "called" for the hook's own.

namespace
{
enum class Halo3HookWhen : uint8_t
{
    InPlay,
    OnFoot,
    Seated,
    FirstPersonDrawn,   // on foot, and the first-person render driver called since the last pass
    HudDrawn,           // on foot, and the HUD widget draw called since the last pass
};

struct Halo3HookWatchSpec
{
    Halo3Hook hook;
    Halo3HookWhen when;
    const char* name;          // in the summary and at the head of its own line
    const char* what;          // "the hook on <what>"
    const char* consequence;   // what the player is left with while it does not run
};

constexpr Halo3HookWatchSpec kHalo3HookWatches[]{
    {Halo3Hook::RenderView, Halo3HookWhen::InPlay, "per-eye render", "the inner per-view render",
     "Each eye is not rendered on its own: the headset shows no stereo picture of the game"},
    {Halo3Hook::ObserverEffect, Halo3HookWhen::InPlay, "camera effects", "the observer camera's effects",
     "The game's camera shake and recoil stay in the headset view"},
    {Halo3Hook::SoundListener, Halo3HookWhen::InPlay, "sound listener", "the sound listener update",
     "The sound keeps turning with the aim, not the head"},
    {Halo3Hook::WorldCollision, Halo3HookWhen::InPlay, "world collision", "the collision scheduler",
     "The hands and gun do not stop at walls, and the crosshair query is not re-aimed"},
    {Halo3Hook::HapticUpdate, Halo3HookWhen::InPlay, "haptic update", "the game's haptic update",
     "The weapons' authored haptics are not felt"},
    {Halo3Hook::PartDraw, Halo3HookWhen::InPlay, "part draw", "the part draw",
     "A vehicle camera's hideParts and fadeParts are not applied"},
    {Halo3Hook::ContactUpdate, Halo3HookWhen::OnFoot, "contact update", "the unit update",
     "Contact melee and the on-foot aim trace do not run"},
    {Halo3Hook::WeaponTriggerUpdate, Halo3HookWhen::OnFoot, "trigger update", "the weapon trigger update",
     "The weapons' authored haptics are not felt"},
    {Halo3Hook::SeatView, Halo3HookWhen::Seated, "seat view", "the camera director's seat calls (through a relay)",
     "A seat's first-person view is the game's own choice of camera"},
    {Halo3Hook::FpDriver, Halo3HookWhen::HudDrawn, "first-person render driver", "the first-person render driver",
     "The first-person gun and HUD are not placed for each eye"},
    {Halo3Hook::FpCameraRebuild, Halo3HookWhen::FirstPersonDrawn, "first-person camera",
     "the first-person camera rebuild", "The gun and HUD layer is drawn the same in both eyes"},
    {Halo3Hook::FpInterpolate, Halo3HookWhen::FirstPersonDrawn, "first-person interpolation",
     "the first-person interpolation", "The first-person arms and gun do not follow the controllers"},
    {Halo3Hook::FpVisiblePalette, Halo3HookWhen::FirstPersonDrawn, "first-person palette",
     "the first-person visible palette", "The first-person arms and gun do not follow the controllers"},
    {Halo3Hook::ComposeBones, Halo3HookWhen::FirstPersonDrawn, "bone composer", "the first-person bone composer",
     "The muzzle flash and effect markers are not moved onto the gun"},
    {Halo3Hook::HudDrawWidget, Halo3HookWhen::FirstPersonDrawn, "HUD widget draw", "the HUD widget draw",
     "The game's own crosshair is not drawn at the hand's aim"},
    {Halo3Hook::HudAnchorBasis, Halo3HookWhen::HudDrawn, "HUD height", "the HUD anchor basis",
     "The HUD height setting does nothing"},
    {Halo3Hook::HudPlace, Halo3HookWhen::HudDrawn, "reticle colour", "the HUD widget placement",
     "The hand reticle keeps its colour when the game's crosshair changes it"},
    {Halo3Hook::HudCrosshairVisible, Halo3HookWhen::HudDrawn, "crosshair predicate", "the crosshair predicate",
     "The game's centre crosshair is neither hidden nor moved to the hand"},
};
constexpr size_t kHalo3HookWatchCount = sizeof(kHalo3HookWatches) / sizeof(kHalo3HookWatches[0]);
static_assert(kHalo3HookWatchCount <= drive_cascade::HookWatchBoard::kMaxWatches, "the board holds every watch");

const char* Halo3HookWhenSays(Halo3HookWhen when)
{
    switch (when)
    {
    case Halo3HookWhen::InPlay:
        return "the player controlled a unit in Halo 3, not paused and not in a cutscene, where the game runs it "
               "every frame";
    case Halo3HookWhen::OnFoot:
        return "the player was on foot and in control, where the game runs it every frame";
    case Halo3HookWhen::Seated:
        return "the player sat in a vehicle seat and was in control, where the camera director runs it every frame";
    case Halo3HookWhen::FirstPersonDrawn:
        return "the player was on foot and in control and the game was drawing the first-person view (its "
               "first-person render driver was called), where it runs this every frame";
    case Halo3HookWhen::HudDrawn:
        return "the player was on foot and in control and the game was drawing the HUD (its HUD widget draw was "
               "called), where it runs this every frame";
    }
    return "?";
}

drive_cascade::HookWatchBoard g_halo3HookBoard;   // WaitThread only
uint32_t g_halo3HookWatchSeen = 0;               // the install the flags were last cleared for

// Where the watched hook is now (null: not installed now) and its RVA in halo3.dll. The install's record, and for the
// hooks that come and go within an install, their own state too.
const void* Halo3HookWatchTarget(Halo3Hook hook, uint32_t& rva)
{
    const void* target = g_halo3HookTarget[static_cast<size_t>(hook)];
    const void* at = target;
    switch (hook)
    {
    case Halo3Hook::WeaponTriggerUpdate:
    case Halo3Hook::HapticUpdate:
        // The lane takes its hooks out when it faults (ReportHalo3WeaponHaptics).
        if (!g_halo3WeaponHaptics.triggerUpdateTarget || !g_halo3WeaponHaptics.updateTarget)
            target = nullptr;
        break;
    case Halo3Hook::SoundListener:
        if (!g_halo3HeadAudio.target)
            target = nullptr;
        break;
    case Halo3Hook::SeatView:
        // Redirected only while the seat settings ask for it (Halo3SeatViewSync); the relay is outside the module,
        // so the line names the director's first call site.
        target = g_halo3SeatViewRedirected.load(std::memory_order_acquire) ? g_halo3SeatView.relay : nullptr;
        at = g_halo3SeatView.sites[0].call;
        break;
    case Halo3Hook::PartDraw:
        // In only while the camera file asks for hideParts or fadeParts.
        target = at = halo3_part_draw::runtime.draw.target;
        break;
    default:
        break;
    }
    const uintptr_t address = reinterpret_cast<uintptr_t>(at);
    rva = address >= g_halo3HookWatchBase && g_halo3HookWatchBase
              ? static_cast<uint32_t>(address - g_halo3HookWatchBase)
              : 0;
    return target;
}

// In play (the header). `seated`: the unit the player controls is in a seat.
bool Halo3HookWatchInPlay(uint64_t nowMs, bool& seated)
{
    seated = false;
    if (!DriveWatchPlayerInControl(GameTitle::Halo3, nowMs))
        return false;
    bool paused = true;
    if (!ReadEnginePaused(paused) || paused)
        return false;
    const int slot = LegacyDriveSlot(GameTitle::Halo3);
    if (slot < 0)
        return false;
    const LegacyDrivePublication p = LegacyDriveReadPublication(g_legacyDrive[slot]);
    if (!p.coherent || !p.valid || !p.setMs || nowMs < p.setMs || nowMs - p.setMs > 250)
        return false;
    seated = p.seated;
    return true;
}
} // namespace

// A HOOK THAT IS NEVER CALLED: THE SEARCHES ARE NOT TRUSTED AGAIN. The address may be the wrong one for what the game
// runs now though its bytes passed the compare in place (a match in data the build's identity does not cover), so
// what the last install took without searching is forgotten, here and in the file, and the next install searches
// for all of it and saves what it finds. Hooks are not put in again in play: that takes the game's threads and
// retires hooks under a running level, so it waits for the install at the next level. A false alarm costs one
// install's searches. An install that searched for everything has nothing to forget, so this cannot repeat within
// a load. Cold worker.
std::vector<sig::ImageMemo::Entry> g_halo3AnswersForgotten;

void Halo3HookWatchForgetSearches()
{
    sig::ImageMemo& memo = g_halo3InstallSearchMemo;
    if (!memo.lastKnown)
        return;
    const uint32_t known = memo.lastKnown;
    g_halo3AnswersForgotten.assign(memo.entries, memo.entries + memo.count);
    memo.lastKnown = 0;
    memo.count = 0;
    memo.changes = 0;
    const char* why = nullptr;
    const bool forgotten = !g_halo3SavedInstallSearches.identified ||
        install_search_cache::Forget(g_halo3SavedInstallSearches.identity, why);
    LOG("Halo 3 hook watch: a hook is never called, and the last install took %u answers without searching: the "
        "next install searches for all of them%s%s", known, forgotten ? "" : "; the saved ones stay in the file: ",
        forgotten ? "" : why);
}

// Cold worker, every loop, until the install's watches have all settled. A watch that has seen its hook called (or
// has fired) is done for the install: its flag is no longer cleared, so its detour goes back to one relaxed load,
// and once none is still waiting and the summary is said, the loop does nothing until the next install.
void Halo3HookWatchReport(uint64_t nowMs)
{
    const uint32_t install = g_halo3InstallReady.load(std::memory_order_acquire) ? g_halo3HookWatchInstall : 0;
    static bool s_summarised = false;   // the cold worker
    const bool newInstall = install != g_halo3HookWatchSeen;
    if (!newInstall && install && s_summarised)
    {
        bool waiting = false;
        for (size_t i = 0; i < kHalo3HookWatchCount && !waiting; ++i)
            waiting = g_halo3HookBoard.VerdictOf(i) == drive_cascade::HookWatchBoard::Verdict::Waiting;
        if (!waiting)
            return;
    }
    // The hooks whose flag is still read: a watch still waiting on its own hook, and the first-person driver and
    // the HUD widget draw while a watch still waiting rests on them.
    bool read[kHalo3HookCount]{};
    if (newInstall)
    {
        for (bool& flag : read)
            flag = true;
    }
    else
    {
        for (size_t i = 0; i < kHalo3HookWatchCount; ++i)
        {
            const auto verdict = g_halo3HookBoard.VerdictOf(i);
            if (verdict == drive_cascade::HookWatchBoard::Verdict::Called ||
                verdict == drive_cascade::HookWatchBoard::Verdict::NeverCalled)
                continue;
            const Halo3HookWatchSpec& spec = kHalo3HookWatches[i];
            read[static_cast<size_t>(spec.hook)] = true;
            if (spec.when == Halo3HookWhen::FirstPersonDrawn)
                read[static_cast<size_t>(Halo3Hook::FpDriver)] = true;
            if (spec.when == Halo3HookWhen::HudDrawn)
                read[static_cast<size_t>(Halo3Hook::HudDrawWidget)] = true;
        }
    }
    bool called[kHalo3HookCount]{};
    for (size_t i = 0; i < kHalo3HookCount; ++i)
        if (read[i])
            called[i] = g_halo3HookCalled[i].exchange(0, std::memory_order_relaxed) != 0;
    // A call counted before this install was made by the hooks before it.
    if (newInstall)
    {
        g_halo3HookWatchSeen = install;
        s_summarised = false;
        for (bool& flag : called)
            flag = false;
    }
    g_halo3HookBoard.Begin(install);
    if (!install)
        return;
    bool inSeat = false;
    const bool inPlay = Halo3HookWatchInPlay(nowMs, inSeat);
    const bool onFoot = inPlay && !inSeat;
    const bool seated = inPlay && inSeat;
    const bool firstPersonDrawn = onFoot && called[static_cast<size_t>(Halo3Hook::FpDriver)];
    const bool hudDrawn = onFoot && called[static_cast<size_t>(Halo3Hook::HudDrawWidget)];
    for (size_t i = 0; i < kHalo3HookWatchCount; ++i)
    {
        const Halo3HookWatchSpec& spec = kHalo3HookWatches[i];
        bool possible = false;
        switch (spec.when)
        {
        case Halo3HookWhen::InPlay: possible = inPlay; break;
        case Halo3HookWhen::OnFoot: possible = onFoot; break;
        case Halo3HookWhen::Seated: possible = seated; break;
        case Halo3HookWhen::FirstPersonDrawn: possible = firstPersonDrawn; break;
        case Halo3HookWhen::HudDrawn: possible = hudDrawn; break;
        }
        uint32_t rva = 0;
        const void* target = Halo3HookWatchTarget(spec.hook, rva);
        if (g_halo3HookBoard.Pass(i, target, possible, called[static_cast<size_t>(spec.hook)], nowMs))
        {
            char feature[96];
            snprintf(feature, sizeof(feature), "Halo 3 hook watch, %s", spec.name);
            drive_cascade::SayHookNeverCalled(feature, spec.what, rva, Halo3HookWhenSays(spec.when),
                                              spec.consequence);
            Halo3HookWatchForgetSearches();
        }
    }
    if (g_halo3HookBoard.SummaryDue(inPlay, nowMs))
    {
        s_summarised = true;
        const char* names[kHalo3HookWatchCount];
        for (size_t i = 0; i < kHalo3HookWatchCount; ++i)
            names[i] = kHalo3HookWatches[i].name;
        char summary[768];
        g_halo3HookBoard.Summary("Halo 3", names, kHalo3HookWatchCount, summary, sizeof(summary));
        LOG("%s (once per install; a hook that is never called has its own line)", summary);
    }
}

// Whether searching again changed anything: the answer to whether the searches were at fault at all.
void Halo3HookWatchSearchedAgain()
{
    if (g_halo3AnswersForgotten.empty())
        return;
    const sig::ImageMemo& memo = g_halo3InstallSearchMemo;
    uint32_t compared = 0, differ = 0;
    for (const sig::ImageMemo::Entry& old : g_halo3AnswersForgotten)
        for (uint32_t i = 0; i < memo.count; ++i)
            if (memo.entries[i].pattern == old.pattern && memo.entries[i].start == old.start &&
                memo.entries[i].length == old.length)
            {
                ++compared;
                differ += memo.entries[i].match != old.match ? 1u : 0u;
                break;
            }
    LOG("Halo 3 hook watch: searched again after a hook was never called: %u of %u answers differ%s", differ,
        compared, differ ? "" : "; the searches were not what kept that hook from running");
    g_halo3AnswersForgotten.clear();
    g_halo3AnswersForgotten.shrink_to_fit();
}
