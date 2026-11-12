// HALO CE: THE MOTION TRACKER FOLLOWS THE HEAD -- the runtime half. The sites, the pure logic and the reasoning
// are in src/common/haloce_head_relative_logic.h; the write-up is docs/HALOCE-HEAD-RELATIVE-2026-10-07.md.
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT HCEEK-VERIFIED. Every binding is read from retail halo1.dll alone
// (no editing-kit symbols), and the names are ours. The lane therefore
//   (a) binds nothing unless every static link holds on the loaded image (nine unique signatures that agree);
//   (b) at every use requires the float it would replace to be bit-identical to the control yaw plus pi/2, the
//       sum the game's store has just computed; and
//   (c) replaces nothing until that float has agreed, at run time, with the heading of the view frame this mod
//       maps the room through (ViewProof: provisional at rest, confirmed by the first stick turn).
// Until (c) every call is forwarded untouched and counted as "unproven".
//
// In short: CE's sensor turns its contacts by the player's control yaw, which only the turn stick moves, so in VR
// it follows neither the gun nor the head. The game stores that yaw (+ pi/2) in the sensor's own active datum on
// every draw and then calls the render, the first reader. This lane hooks the render and, for local player 0,
// rewrites that one float with the head's heading first. It writes nothing else, and reads player control only.
#include "haloce_head_relative.h"
#include "haloce_controls.h"
#include "hook_quiescence.h"
#include "native_forward.h"
#include "sigscan.h"
#include "title_adapter.h"
#include "../common/config.h"
#include "../common/haloce_head_relative_logic.h"
#include "../common/log.h"
#include "../common/minhook_lifecycle.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

uint64_t Sight_NowUs();   // sight_layer.h: the one sight clock the head sample is stamped on

namespace
{
using namespace haloce_head_relative;

// ---- THE HEAD, as the frame that rendered with it published it (the two-clocks rule) ---------------------------
// Written by the render path (PublishGameplayContext, where both renderers publish the frame's stock centre camera
// with its tracking sample and reference), read by the sensor's render. A sequence lock over atomics: an odd
// sequence is a write in progress, and a read that saw the sequence move is retried. `writing` keeps two
// publishers apart; the two renderers never publish together, and if they ever did one sample would be skipped.
struct HeadPublication
{
    std::atomic<bool> writing{false};
    std::atomic<uint32_t> sequence{0};
    std::atomic<float> forward[3]{}, up[3]{}, heading[3]{};
    std::atomic<uint64_t> stampUs{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint32_t> flags{0};
    std::atomic<uint32_t> thread{0};  // the publishing thread, for the dev report (not under the sequence)
} published;
// live: the head view was built for that frame; seat: its heading is a seat's control yaw; anniversary: the frame
// was prepared under Anniversary graphics.
constexpr uint32_t kHeadLive = 1u, kHeadSeat = 2u, kHeadAnniversary = 4u;

// Any thread. False = nothing coherent could be read (the writer was mid-write four times running).
bool ReadHead(head_relative::HeadSample& sample, float heading[3], uint32_t& flags) noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before = published.sequence.load();
        if (before & 1u)
            continue;
        for (int i = 0; i < 3; ++i)
        {
            sample.forward[i] = published.forward[i].load();
            sample.up[i] = published.up[i].load();
            heading[i] = published.heading[i].load();
        }
        sample.stampUs = published.stampUs.load();
        sample.generation = published.generation.load();
        const uint32_t read = published.flags.load();
        if (published.sequence.load() != before)
            continue;
        sample.valid = (read & kHeadLive) != 0;
        flags = read;
        return true;
    }
    return false;
}

// ---- THE TRACKER -----------------------------------------------------------------------------------------------
// Retail +0xBAE890: void(local player in cx, in-multiplayer in dl, the sensor's centre in r8). It is reached only
// by the tail jump of the draw entry (+0xBADFD5), whose one caller is the HUD's unit interface (+0xC514EB).
// tools/audit_caller_area.py BAE890:8:v is clean: it touches no caller slot past its home area, and that caller
// keeps no volatile register across the call.
using RenderFn = void (__fastcall*)(int16_t localPlayer, uint8_t inMultiplayer, const void* centre);
constexpr size_t kUseCount = static_cast<size_t>(TrackerUse::Count);
constexpr size_t kStepCount = static_cast<size_t>(ProofStep::Count);
struct Feature
{
    // Worker-owned; the detour reads `ready`, the original and what the install decoded.
    HMODULE retained = nullptr;
    uintptr_t base = 0;
    size_t size = 0;
    void* target = nullptr;
    void* original = nullptr;                 // RenderFn
    bool hookEnabled = false;
    TrackerSites sites;                       // decoded from the game's own instructions at install
    uint8_t* const* globalsSlot = nullptr;    // the image's own pointer to the sensor globals
    const uint8_t* const* controlSlot = nullptr;  // ... and to player control (read only)
    float halfPi = 0.0f;                      // the constant the store adds, as the image holds it
    std::atomic<uint32_t> generation{0}, callbacks{0};
    std::atomic<bool> active{false}, ready{false}, retiring{false};
    // The proof and the held heading belong to local player 0's draw. Which thread draws CE's HUD is not
    // established (Halo 4's tracker is built on the game's job threads), so "one at a time" is
    // made a fact here instead of an assumption: a draw of player 0 that arrives while another is inside is
    // forwarded untouched and counted (TrackerUse::Overlapped). The flag is tried, never waited for.
    std::atomic<bool> inside{false};
    ViewProof proof;                          // under `inside`
    head_relative::HeadingHold hold;          // under `inside`
    // The window the report drains: every call lands in exactly one use.
    std::atomic<uint32_t> uses[kUseCount]{};
    std::atomic<uint32_t> steps[kStepCount]{};
    std::atomic<uint32_t> faults{0}, mixedReads{0}, anniversaryDraws{0}, classicDraws{0};
    std::atomic<uint32_t> headReads{0};       // dev builds: draws in which the published head had a heading
    std::atomic<float> lastNativeDeg{0.0f}, lastFrameDeg{0.0f}, lastHeadDeg{0.0f};
    std::atomic<uint32_t> lastThread{0}, threadChanges{0};  // dev builds: the drawing thread; how often it changed
    // The proof's events: counted where they happen, said by the report.
    std::atomic<uint32_t> stage{0}, provisionalCount{0}, confirmedCount{0}, withdrawnCount{0};
    std::atomic<float> provisionalDeg{0.0f}, confirmedFromDeg{0.0f}, confirmedToDeg{0.0f}, confirmedArcDeg{0.0f};
    std::atomic<float> withdrawnAgreedGameDeg{0.0f}, withdrawnAgreedFrameDeg{0.0f}, withdrawnGameDeg{0.0f},
        withdrawnFrameDeg{0.0f};
    // The report's thread only.
    int reportedSwitch = -1;
    uint32_t reportedProvisional = 0, reportedConfirmed = 0, reportedWithdrawn = 0;
    bool reportedFirstUse = false, reportedFirstRefusal = false, cleanupReported = false;
    uint64_t lastReport = 0;
    uint32_t rejectedGeneration = 0;
    uintptr_t rejectedBase = 0;
} feature;

// A proof is a statement about one loaded image and the sites decoded from it. A reinstall on the same image and
// sites keeps a CONFIRMED proof (CE's optional lanes are removed and installed again every time the core disarms:
// a load, a long pause); anything else, and every new load of the title, takes the proof again. Worker only.
struct ProofIdentity
{
    uintptr_t base = 0;
    size_t size = 0;
    uint32_t generation = 0;
    TrackerSites sites;
    bool operator==(const ProofIdentity&) const = default;
};
struct KeptProof
{
    bool have = false;
    ProofIdentity identity;
    ViewProof proof;
} keptProof;

float Degrees(float radians) { return WrapPi(radians) * head_relative::kRadToDeg; }

// Local player 0's draw, holding `inside`: between the game's store of the heading and its first read. No
// allocation, no logging, no lock. Every refusal leaves the game's own heading. False = a read or the write
// faulted (counted; `use` then says nothing).
__declspec(noinline) bool DecideHeld(int32_t localPlayer, TrackerUse& use) noexcept
{
    auto& f = feature;
    use = TrackerUse::NoStore;
    __try
    {
        TrackerFrame frame;
        frame.localPlayer = localPlayer;
        float* field = nullptr;
        if (localPlayer == 0)
        {
            uint8_t* const globals = *f.globalsSlot;
            const uint8_t* const control = *f.controlSlot;
            if (globals && control)
            {
                const int32_t active = *reinterpret_cast<const int16_t*>(globals + f.sites.activeOffset);
                if (ActiveDatumValid(f.sites, active))
                {
                    field = reinterpret_cast<float*>(globals + HeadingOffset(f.sites, localPlayer, active));
                    frame.field = *field;
                    // READ ONLY: the yaw the game's store has just read, for the exact guard.
                    frame.controlYaw =
                        *reinterpret_cast<const float*>(control + ControlYawOffset(f.sites, localPlayer));
                    frame.halfPi = f.halfPi;
                    frame.fieldRead = true;
                }
            }
        }
        uint32_t headFlags = 0;
        if (frame.fieldRead)
        {
            frame.sampleRead = ReadHead(frame.sample, frame.frameHeading, headFlags);
            if (!frame.sampleRead)
                f.mixedReads.fetch_add(1, std::memory_order_relaxed);
            frame.seatFrame = (headFlags & kHeadSeat) != 0;
            frame.nowUs = Sight_NowUs();
            // This hook belongs to one load of the title; a sample from another is not this sensor's head.
            const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::HaloCE);
            frame.generation = generation == f.generation.load(std::memory_order_acquire) ? generation : 0;
            HaloCELocalPlayerState state{};
            frame.playerState = frame.generation && HaloCEControls_GetLocalPlayerState(state) &&
                                state.generation == frame.generation;
            frame.cinematic = state.nativeCinematicFlag;
            frame.perspective = state.nativePerspective;
            frame.hasUnit = state.hasControlledUnit;
            frame.onFoot = state.onFoot;
            frame.switchOn = g_config.tracker_follows_head;
            frame.sign = g_config.tracker_head_sign;
            frame.offsetDeg = g_config.tracker_head_offset_deg;
#if HALOMCCVR_DEV_DIAGNOSTICS
            // For the dev line: where the published head faces, whether or not this call may use it. A reading,
            // never a verdict: nothing compares the head (or the hand) with the game's heading.
            float devHeading[3], devYaw = 0.0f;
            if (frame.sampleRead && frame.sample.valid &&
                head_relative::Heading(frame.sample.forward, kWorldUp, devHeading) && HeadingYaw(devHeading, devYaw))
            {
                f.lastHeadDeg.store(Degrees(devYaw), std::memory_order_relaxed);
                f.headReads.fetch_add(1, std::memory_order_relaxed);
            }
#endif
        }
        const TrackerDecision decision = DecideTracker(frame, f.proof, f.hold);
        use = decision.use;
        if (decision.stepped)
        {
            f.steps[static_cast<size_t>(decision.step)].fetch_add(1, std::memory_order_relaxed);
            if (decision.step == ProofStep::Provisional)
            {
                f.provisionalDeg.store(Degrees(decision.frameYaw), std::memory_order_relaxed);
                f.provisionalCount.fetch_add(1, std::memory_order_release);
            }
            else if (decision.step == ProofStep::Confirmed)
            {
                f.confirmedFromDeg.store(Degrees(f.proof.anchorYaw), std::memory_order_relaxed);
                f.confirmedToDeg.store(Degrees(decision.frameYaw), std::memory_order_relaxed);
                f.confirmedArcDeg.store(f.proof.arc * head_relative::kRadToDeg, std::memory_order_relaxed);
                f.confirmedCount.fetch_add(1, std::memory_order_release);
            }
            else if (decision.step == ProofStep::Withdrawn)
            {
                f.withdrawnAgreedGameDeg.store(Degrees(f.proof.lastAgreedGame), std::memory_order_relaxed);
                f.withdrawnAgreedFrameDeg.store(Degrees(f.proof.lastAgreedReference), std::memory_order_relaxed);
                f.withdrawnGameDeg.store(Degrees(f.proof.firstDisagreedGame), std::memory_order_relaxed);
                f.withdrawnFrameDeg.store(Degrees(f.proof.firstDisagreedReference), std::memory_order_relaxed);
                f.withdrawnCount.fetch_add(1, std::memory_order_release);
            }
            f.stage.store(static_cast<uint32_t>(f.proof.stage), std::memory_order_relaxed);
            f.lastNativeDeg.store(Degrees(decision.gameYaw), std::memory_order_relaxed);
            if (decision.frameYawKnown)
                f.lastFrameDeg.store(Degrees(decision.frameYaw), std::memory_order_relaxed);
            if (frame.sampleRead && frame.sample.stampUs)
                ((headFlags & kHeadAnniversary) ? f.anniversaryDraws : f.classicDraws)
                    .fetch_add(1, std::memory_order_relaxed);
        }
        if (use == TrackerUse::Used && field)
        {
            *field = decision.field;   // THE ONE WRITE: the sensor's own copy of the heading, before its first read
            f.lastHeadDeg.store(Degrees(decision.headYaw), std::memory_order_relaxed);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        f.faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
// The sensor render's thread. Every call lands in exactly one reason, or in `faults`.
__declspec(noinline) void ApplyHead(int32_t localPlayer) noexcept
{
    auto& f = feature;
    // Another local player's sensor is not read at all. Player 0's is, by one draw at a time: the flag is tried,
    // never waited for.
    TrackerUse use = TrackerUse::NotLocalUser;
    bool decided = true;
    if (localPlayer == 0)
    {
        if (f.inside.exchange(true, std::memory_order_acquire))
            use = TrackerUse::Overlapped;
        else
        {
            decided = DecideHeld(localPlayer, use);
            f.inside.store(false, std::memory_order_release);
        }
    }
    if (decided)
        f.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    const uint32_t thread = GetCurrentThreadId();
    const uint32_t previous = f.lastThread.exchange(thread, std::memory_order_relaxed);
    if (previous && previous != thread)
        f.threadChanges.fetch_add(1, std::memory_order_relaxed);
#endif
}
__declspec(noinline) void __fastcall RenderDetour(int16_t localPlayer, uint8_t inMultiplayer, const void* centre)
{
    auto& f = feature;
    f.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (f.ready.load(std::memory_order_acquire))
            ApplyHead(localPlayer);
        if (const auto original = reinterpret_cast<RenderFn>(f.original))
            CallNativeWithSpareSlots(original, localPlayer, inMultiplayer, centre);
    }
    __finally
    {
        f.callbacks.fetch_sub(1, std::memory_order_release);
    }
}

// ---- proving the image -----------------------------------------------------------------------------------------
// The game's bytes at a fixed place, as they stood before any hook of ours. False = unreadable.
__declspec(noinline) bool Copy(uintptr_t address, void* out, size_t bytes) noexcept
{
    __try
    {
        (void)sig::CopyUnhooked(address, out, bytes);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
bool Inside(uintptr_t base, size_t size, uintptr_t address, size_t bytes)
{
    return address >= base && bytes <= size && address - base <= size - bytes;
}
// `bytes` of the image at `address` when it is inside the image and readable.
bool Read(uintptr_t base, size_t size, uintptr_t address, void* out, size_t bytes)
{
    return address && Inside(base, size, address, bytes) && Copy(address, out, bytes);
}
// A pattern's one match in the image, or 0 when it is missing or matches twice.
uintptr_t Unique(uintptr_t base, size_t size, const char* pattern)
{
    const uintptr_t match = sig::Find(base, size, pattern);
    if (!match || match + 1 >= base + size)
        return 0;
    return sig::Find(match + 1, base + size - match - 1, pattern) ? 0 : match;
}
// The beginning of the function `address` lies in, from the image's own function table, as an offset from
// `base`; -1 when the table has no entry for it or it belongs to another image.
int64_t FunctionRva(uintptr_t base, uintptr_t address)
{
    DWORD64 imageBase = 0;
    const auto* function = RtlLookupFunctionEntry(address, &imageBase, nullptr);
    return function && imageBase == base ? static_cast<int64_t>(function->BeginAddress) : -1;
}
// `function`(x) against the C runtime's own cosine or sine at two angles. Guarded: `function` is a pointer read
// from the title's import table.
__declspec(noinline) bool TrigAnswers(float (*function)(float), bool sine) noexcept
{
    const float angles[2]{0.5f, -2.25f};
    __try
    {
        for (int i = 0; i < 2; ++i)
        {
            const float expected = sine ? std::sin(angles[i]) : std::cos(angles[i]);
            if (!(std::fabs(function(angles[i]) - expected) <= 1.0e-5f))
                return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
// The placer's two trig calls are import thunks (jmp [slot]) whose slots hold the C runtime's cosf, then sinf.
// Null = proven, and `how` says by what; otherwise what did not prove.
const char* ProveTrig(uintptr_t base, size_t size, const TrackerSites& sites, const char*& how)
{
    float (*imported[2])(float) = {nullptr, nullptr};
    const int64_t thunks[2]{sites.cosThunkRva, sites.sinThunkRva};
    for (int i = 0; i < 2; ++i)
    {
        const uintptr_t thunk = base + static_cast<uintptr_t>(thunks[i]);
        uint8_t bytes[6]{};
        if (!Read(base, size, thunk, bytes, sizeof(bytes)) || bytes[0] != 0xFF || bytes[1] != 0x25)
            return "the placer's trig calls are not import thunks";
        const uintptr_t slot = thunk + 6 + static_cast<uintptr_t>(static_cast<intptr_t>(ReadI32(bytes + 2)));
        uintptr_t function = 0;
        if (!Read(base, size, slot, &function, sizeof(function)) || !function || Inside(base, size, function, 1))
            return "a trig import of the placer is not bound";
        imported[i] = reinterpret_cast<float (*)(float)>(function);
    }
    const HMODULE runtime = GetModuleHandleW(L"ucrtbase.dll");
    const void* runtimeCos = runtime ? reinterpret_cast<const void*>(GetProcAddress(runtime, "cosf")) : nullptr;
    const void* runtimeSin = runtime ? reinterpret_cast<const void*>(GetProcAddress(runtime, "sinf")) : nullptr;
    if (runtimeCos && runtimeSin && reinterpret_cast<const void*>(imported[0]) == runtimeCos &&
        reinterpret_cast<const void*>(imported[1]) == runtimeSin)
        how = "the imports are ucrtbase's cosf and sinf";
    else if (TrigAnswers(imported[0], false) && TrigAnswers(imported[1], true))
        how = "the imports answer as cosf and sinf";
    else
        return "the placer's trig calls are not cosf then sinf";
    return nullptr;
}
// Every site of the sensor's heading, from the game's own bytes. Null = proven; otherwise what did not prove,
// with which signatures matched in `matched` (one letter each, '-' = no unique match).
const char* Prove(uintptr_t base, size_t size, TrackerSites& sites, float& halfPi, const char*& trig, char matched[10])
{
    const struct
    {
        const char* pattern;
        size_t bytes;
        char letter;
    } wanted[9]{{kTrackerDrawPattern, kDrawBytes, 'd'},          {kTrackerUpdateHeadPattern, kUpdateHeadBytes, 'u'},
                {kTrackerStorePattern, kStoreBytes, 's'},        {kTrackerRenderHeadPattern, kRenderHeadBytes, 'r'},
                {kTrackerRenderDatumPattern, kRenderDatumBytes, 'h'}, {kTrackerUnitCallPattern, kUnitCallBytes, 'c'},
                {kTrackerPointCallPattern, kPointCallBytes, 'p'}, {kTrackerPlacerPattern, kPlacerBytes, 'b'},
                {kFacingReaderPattern, kFacingBytes, 'f'}};
    static uint8_t copies[9][kPlacerBytes];   // cold, worker only: the largest pattern is the placer's
    static_assert(kPlacerBytes >= kUpdateHeadBytes && kPlacerBytes >= kUnitCallBytes && kPlacerBytes >= kRenderHeadBytes);
    uintptr_t at[9]{};
    const uint8_t* bytes[9]{};
    for (int i = 0; i < 9; ++i)
    {
        at[i] = Unique(base, size, wanted[i].pattern);
        bytes[i] = at[i] && Read(base, size, at[i], copies[i], wanted[i].bytes) ? copies[i] : nullptr;
        matched[i] = bytes[i] ? wanted[i].letter : '-';
    }
    matched[9] = 0;
    const auto rva = [&](int i) { return static_cast<int64_t>(at[i] - base); };
    // A head must be the beginning of a function in the image's own table.
    const auto head = [&](int i) { return bytes[i] && FunctionRva(base, at[i]) == rva(i) ? bytes[i] : nullptr; };
    TrackerImage image;
    image.draw = head(0);
    image.drawRva = rva(0);
    image.update = head(1);
    image.updateRva = rva(1);
    image.store = bytes[2];
    image.storeRva = rva(2);
    image.storeFunctionRva = bytes[2] ? FunctionRva(base, at[2]) : -1;
    image.render = head(3);
    image.renderRva = rva(3);
    image.datum = bytes[4];
    image.datumRva = rva(4);
    image.datumFunctionRva = bytes[4] ? FunctionRva(base, at[4]) : -1;
    image.unitCall = bytes[5];
    image.unitCallRva = rva(5);
    image.unitCallFunctionRva = bytes[5] ? FunctionRva(base, at[5]) : -1;
    image.pointCall = bytes[6];
    image.pointCallRva = rva(6);
    image.pointCallFunctionRva = bytes[6] ? FunctionRva(base, at[6]) : -1;
    image.placer = head(7);
    image.placerRva = rva(7);
    image.facing = bytes[8];
    image.facingRva = rva(8);
    TrackerSites decoded;
    TrackerBind bind = DecodeTrackerSites(image, decoded);
    if (bind == TrackerBind::Bound)
    {
        uint8_t halfPiBytes[sizeof(float)]{}, maskBytes[sizeof(uint32_t)]{};
        const bool constants =
            Read(base, size, base + static_cast<uintptr_t>(decoded.halfPiRva), halfPiBytes, sizeof(halfPiBytes)) &&
            Read(base, size, base + static_cast<uintptr_t>(decoded.signMaskRva), maskBytes, sizeof(maskBytes));
        bind = CheckTrackerConstants(constants ? halfPiBytes : nullptr, constants ? maskBytes : nullptr);
        std::memcpy(&halfPi, halfPiBytes, sizeof(halfPi));
    }
    if (bind != TrackerBind::Bound)
        return TrackerBindName(bind);
    // The two pointers the detour reads through must be pointer-sized slots inside the image.
    if (!Inside(base, size, base + static_cast<uintptr_t>(decoded.globalsRva), sizeof(void*)) ||
        !Inside(base, size, base + static_cast<uintptr_t>(decoded.controlRva), sizeof(void*)) ||
        (decoded.globalsRva & 7) || (decoded.controlRva & 7))
        return "the sensor's or player control's pointer is not a slot of this image";
    if (const char* why = ProveTrig(base, size, decoded, trig))
        return why;
    sites = decoded;
    return nullptr;
}

bool Current() noexcept
{
    return feature.ready.load(std::memory_order_acquire) && feature.active.load(std::memory_order_acquire) &&
           !feature.retiring.load(std::memory_order_acquire) && TitleAdapter_GetActiveTitle() == GameTitle::HaloCE &&
           TitleAdapter_GetGeneration(GameTitle::HaloCE) == feature.generation.load(std::memory_order_acquire);
}
// Worker, every build: the proof passing at rest, being confirmed, being withdrawn. Each is said once, by the
// two-second report or, if the lane retires first, at the retirement.
void ReportProof() noexcept
{
    auto& f = feature;
    const uint32_t provisional = f.provisionalCount.load(std::memory_order_acquire);
    const uint32_t confirmed = f.confirmedCount.load(std::memory_order_acquire);
    const uint32_t withdrawn = f.withdrawnCount.load(std::memory_order_acquire);
    if (provisional != f.reportedProvisional)
    {
        f.reportedProvisional = provisional;
        LOG("TRACKER HEAD Halo CE: proven at rest (provisional): the control yaw agrees with the view frame at "
            "%.1f deg; the first stick turn confirms it",
            f.provisionalDeg.load(std::memory_order_relaxed));
    }
    if (confirmed != f.reportedConfirmed)
    {
        f.reportedConfirmed = confirmed;
        LOG("TRACKER HEAD Halo CE: CONFIRMED at run time: the sensor's heading agreed with the view frame on at "
            "least %u samples across a turn of %.1f degrees (the frame from %.1f to %.1f deg); a constant or a "
            "mirrored yaw cannot do that. Kept across reinstalls on this loaded image",
            kProofSamples, f.confirmedArcDeg.load(std::memory_order_relaxed),
            f.confirmedFromDeg.load(std::memory_order_relaxed), f.confirmedToDeg.load(std::memory_order_relaxed));
    }
    if (withdrawn != f.reportedWithdrawn)
    {
        f.reportedWithdrawn = withdrawn;
        LOG("TRACKER HEAD Halo CE: PROOF WITHDRAWN: %u disagreements in a row. Last agreement: the game's heading "
            "%.1f deg, the view frame's %.1f deg; first disagreement: the game's %.1f deg, the view frame's %.1f "
            "deg. The game's own tracker until the proof is CONFIRMED again by a stick turn (no provisional pass "
            "a second time on this image)",
            kProofWithdrawRun, f.withdrawnAgreedGameDeg.load(std::memory_order_relaxed),
            f.withdrawnAgreedFrameDeg.load(std::memory_order_relaxed),
            f.withdrawnGameDeg.load(std::memory_order_relaxed), f.withdrawnFrameDeg.load(std::memory_order_relaxed));
    }
}
// Worker: disable, prove nothing is inside, remove, release the module. Idempotent; false = try again.
bool Remove() noexcept
{
    auto& f = feature;
    f.active.store(false, std::memory_order_release);
    f.retiring.store(true, std::memory_order_release);
    f.ready.store(false, std::memory_order_release);
    if (f.target && f.hookEnabled)
    {
        const auto disabled = MCCVR_DisableHookForRetirement(f.target);
        if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
            return false;
        f.hookEnabled = false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&RenderDetour)};
    const void* trampolines[]{f.original};
    if (f.target && !WaitForNativeDetourQuiescence(functions, trampolines, 1, f.callbacks))
        return false;
    if (f.target)
    {
        const auto removed = MH_RemoveHook(f.target);
        if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
            return false;
        // Nothing runs in the detour now. Say what the proof did since the last report, and keep what a
        // reinstall on this image may keep of it.
        ReportProof();
        keptProof.have = true;
        keptProof.identity = {f.base, f.size, f.generation.load(std::memory_order_acquire), f.sites};
        keptProof.proof = ProofKeptAcrossReinstall(f.proof);
    }
    if (f.callbacks.load(std::memory_order_acquire))
        return false;
    f.target = nullptr;
    f.original = nullptr;
    f.globalsSlot = nullptr;
    f.controlSlot = nullptr;
    if (f.retained)
    {
        FreeLibrary(f.retained);
        f.retained = nullptr;
    }
    f.base = 0;
    f.size = 0;
    f.generation.store(0, std::memory_order_release);
    f.retiring.store(false, std::memory_order_release);
    return true;
}
// Worker, while CE is armed. Binds nothing unless every static link holds on this image; a refusal is the
// game's own tracker, said once.
bool Install(uintptr_t base, size_t size, uint32_t generation) noexcept
{
    auto& f = feature;
    if (f.target)
    {
        LOG("TRACKER HEAD Halo CE: StockFallback: the previous install was not retired");
        return false;
    }
    if (!base || !generation || size < 0x1000)
    {
        LOG("TRACKER HEAD Halo CE: StockFallback: no title image or generation");
        return false;
    }
    // Keep the module mapped from the first byte the proof reads to the last trampoline's removal.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(base), &f.retained))
    {
        f.retained = nullptr;
        LOG("TRACKER HEAD Halo CE: StockFallback: the title module could not be pinned (error %lu)", GetLastError());
        return false;
    }
    f.base = base;
    f.size = size;
    f.generation.store(generation, std::memory_order_release);
    f.retiring.store(false, std::memory_order_release);
    TrackerSites sites;
    float halfPi = 0.0f;
    const char* trig = "";
    char matched[10]{};
    if (const char* why = Prove(base, size, sites, halfPi, trig, matched))
    {
        LOG("TRACKER HEAD Halo CE: StockFallback: %s (unique signatures: %s of dusrhcpbf); the game's own tracker. "
            "Retail-derived lead, not HCEEK-verified",
            why, matched);
        (void)Remove();
        return false;
    }
    f.sites = sites;
    f.halfPi = halfPi;
    f.globalsSlot = reinterpret_cast<uint8_t* const*>(base + static_cast<uintptr_t>(sites.globalsRva));
    f.controlSlot = reinterpret_cast<const uint8_t* const*>(base + static_cast<uintptr_t>(sites.controlRva));
    // A reinstall on the same loaded image and the same decoded sites keeps a CONFIRMED proof (and the fact that
    // a provisional pass was spent); anything else starts from nothing.
    const ProofIdentity identity{base, size, generation, sites};
    f.proof = keptProof.have && keptProof.identity == identity ? keptProof.proof : ViewProof{};
    f.hold = head_relative::HeadingHold{};
    f.stage.store(static_cast<uint32_t>(f.proof.stage), std::memory_order_relaxed);
    f.provisionalCount.store(0, std::memory_order_relaxed);
    f.confirmedCount.store(0, std::memory_order_relaxed);
    f.withdrawnCount.store(0, std::memory_order_relaxed);
    f.reportedProvisional = f.reportedConfirmed = f.reportedWithdrawn = 0;
    f.reportedSwitch = -1;
    f.reportedFirstUse = f.reportedFirstRefusal = false;
    for (auto& count : f.uses)
        count.store(0, std::memory_order_relaxed);
    for (auto& count : f.steps)
        count.store(0, std::memory_order_relaxed);
    f.faults.store(0, std::memory_order_relaxed);
    f.mixedReads.store(0, std::memory_order_relaxed);
    f.anniversaryDraws.store(0, std::memory_order_relaxed);
    f.classicDraws.store(0, std::memory_order_relaxed);
    f.headReads.store(0, std::memory_order_relaxed);
    f.lastThread.store(0, std::memory_order_relaxed);
    f.threadChanges.store(0, std::memory_order_relaxed);
    f.inside.store(false, std::memory_order_release);
    void* const target = reinterpret_cast<void*>(base + static_cast<uintptr_t>(sites.renderRva));
    const auto created = MH_CreateHook(target, reinterpret_cast<void*>(&RenderDetour), &f.original);
    if (created != MH_OK)
    {
        f.original = nullptr;
        LOG("TRACKER HEAD Halo CE: StockFallback: hook creation failed (%d)", static_cast<int>(created));
        (void)Remove();
        return false;
    }
    f.target = target;
    const auto enabled = MH_EnableHook(target);
    if (enabled != MH_OK)
    {
        LOG("TRACKER HEAD Halo CE: StockFallback: hook enable failed (%d)", static_cast<int>(enabled));
        (void)Remove();
        return false;
    }
    f.hookEnabled = true;
    f.ready.store(true, std::memory_order_release);
    LOG("TRACKER HEAD Halo CE: Installed (RETAIL-DERIVED LEAD, NOT HCEEK-VERIFIED): the sensor's first heading "
        "reader halo1.dll+0x%llX is hooked (the draw entry +0x%llX calls the update +0x%llX and jumps to it). The "
        "heading is the float at +0x%X of local player 0's active datum (sensor globals through +0x%llX: 0x%X a "
        "player, 0x%X a datum, the active index at +0x%X); the game stores [player control through +0x%llX] + "
        "user * 0x%X + 0x%X plus pi/2 there at +0x%llX, and only the placer +0x%llX reads it (%s). Nothing is "
        "replaced until that heading has agreed with the view frame this mod maps the room through on %u samples "
        "(provisional); a stick turn across %.0f degrees confirms it. Proof now: %s; switch=%d sign=%d offset=%.1f deg",
        static_cast<unsigned long long>(sites.renderRva), static_cast<unsigned long long>(sites.drawRva),
        static_cast<unsigned long long>(sites.updateRva), sites.headingOffset,
        static_cast<unsigned long long>(sites.globalsRva), sites.playerStride, sites.datumStride, sites.activeOffset,
        static_cast<unsigned long long>(sites.controlRva), sites.controlStride, sites.yawOffset,
        static_cast<unsigned long long>(sites.storeRva), static_cast<unsigned long long>(sites.placerRva), trig,
        kProofSamples, kProofSpreadRad * head_relative::kRadToDeg,
        f.proof.stage == ProofStage::Confirmed
            ? "CONFIRMED earlier on this loaded image, kept"
            : f.proof.provisionalSpent ? "pending; one was withdrawn on this image, so only a CONFIRMED proof substitutes"
                                       : "pending",
        g_config.tracker_follows_head ? 1 : 0, g_config.tracker_head_sign < 0 ? -1 : 1,
        g_config.tracker_head_offset_deg);
    return true;
}

const char* UseName(size_t use) { return TrackerUseName(static_cast<TrackerUse>(use)); }
// The worker's report (every 2 s). Every build: the proof passing, being confirmed or withdrawn, the switch
// changing, the first use and the first refusal after an install. Dev builds: the window's counts.
void Report() noexcept
{
    auto& f = feature;
    const uint64_t now = GetTickCount64();
    if (now - f.lastReport < 2000)
        return;
    f.lastReport = now;
    if (!f.target)
        return;
    ReportProof();
    const int switchNow = g_config.tracker_follows_head ? 1 : 0;
    if (f.reportedSwitch != switchNow)
    {
        if (f.reportedSwitch >= 0)
            LOG("TRACKER HEAD Halo CE: switch %s: the tracker now turns with %s", switchNow ? "on" : "off",
                switchNow ? "your head (once proven)" : "the game's own heading (the turn stick only)");
        f.reportedSwitch = switchNow;
    }
    uint32_t uses[kUseCount]{}, steps[kStepCount]{}, refused = 0;
    for (size_t i = 0; i < kUseCount; ++i)
    {
        uses[i] = f.uses[i].exchange(0, std::memory_order_relaxed);
        if (i != static_cast<size_t>(TrackerUse::Used))
            refused += uses[i];
    }
    for (size_t i = 0; i < kStepCount; ++i)
        steps[i] = f.steps[i].exchange(0, std::memory_order_relaxed);
    const uint32_t used = uses[static_cast<size_t>(TrackerUse::Used)];
    const uint32_t faults = f.faults.exchange(0, std::memory_order_relaxed);
    const uint32_t mixed = f.mixedReads.exchange(0, std::memory_order_relaxed);
    const uint32_t anniversary = f.anniversaryDraws.exchange(0, std::memory_order_relaxed);
    const uint32_t classic = f.classicDraws.exchange(0, std::memory_order_relaxed);
    const uint32_t headReads = f.headReads.exchange(0, std::memory_order_relaxed);
    const uint32_t threadChanges = f.threadChanges.exchange(0, std::memory_order_relaxed);
    if (!used && !refused && !faults)
        return;
    char reasons[360];
    int length = 0;
    reasons[0] = 0;
    for (size_t i = 0; i < kUseCount; ++i)
    {
        if (i == static_cast<size_t>(TrackerUse::Used) || !uses[i] || length >= static_cast<int>(sizeof(reasons)) - 48)
            continue;
        const int wrote = _snprintf_s(reasons + length, sizeof(reasons) - length, _TRUNCATE, "%s%s=%u",
                                      length ? ", " : "", UseName(i), uses[i]);
        if (wrote > 0)
            length += wrote;
    }
    if (used && !f.reportedFirstUse)
    {
        f.reportedFirstUse = true;
        LOG("TRACKER HEAD Halo CE: following the head: first contacts placed by the head's heading (the game's "
            "heading was %.1f deg, the head's %.1f deg)",
            f.lastNativeDeg.load(std::memory_order_relaxed), f.lastHeadDeg.load(std::memory_order_relaxed));
    }
    if (!used && !f.reportedFirstRefusal)
    {
        f.reportedFirstRefusal = true;
        LOG("TRACKER HEAD Halo CE: the game's own tracker for now: %s%s", length ? reasons : "a fault",
            faults && length ? ", and faults" : "");
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    const float nativeDeg = f.lastNativeDeg.load(std::memory_order_relaxed);
    const float headDeg = f.lastHeadDeg.load(std::memory_order_relaxed);
    const uint32_t stage = f.stage.load(std::memory_order_relaxed);
    // "head minus game" is a diagnostic reading, never a verdict: under a simulated runtime whose poses are not
    // yet set it can be anything (half a turn included), and nothing here counts it.
    LOG("TRACKER HEAD Halo CE (dev): substituted %u; the game's heading %.1f deg, the view frame's %.1f deg, the "
        "head's %.1f deg, head minus game %+.1f deg (last; the head was read in %u draws); proof %s: agreed %u, "
        "lagging %u, disagreed %u, restarted %u, no reference %u; refused %u: %s; faults %u, mixed sample reads "
        "%u; draws while the frame was Anniversary's %u, Classic's %u; drawn on thread %u (the drawing thread "
        "changed %u times), the head published on thread %u; switch=%d sign=%d offset=%.1f deg",
        used, nativeDeg, f.lastFrameDeg.load(std::memory_order_relaxed), headReads ? headDeg : 0.0f,
        headReads ? Degrees((headDeg - nativeDeg) * head_relative::kDegToRad) : 0.0f, headReads,
        ProofStageName(stage < static_cast<uint32_t>(ProofStage::Count) ? static_cast<ProofStage>(stage)
                                                                        : ProofStage::Unproven),
        steps[static_cast<size_t>(ProofStep::Agreed)] + steps[static_cast<size_t>(ProofStep::Provisional)] +
            steps[static_cast<size_t>(ProofStep::Confirmed)],
        steps[static_cast<size_t>(ProofStep::Lagging)],
        steps[static_cast<size_t>(ProofStep::Disagreed)] + steps[static_cast<size_t>(ProofStep::Withdrawn)] +
            steps[static_cast<size_t>(ProofStep::Restarted)],
        steps[static_cast<size_t>(ProofStep::Restarted)], steps[static_cast<size_t>(ProofStep::NoReference)],
        refused, length ? reasons : "none", faults, mixed, anniversary, classic,
        f.lastThread.load(std::memory_order_relaxed), threadChanges,
        published.thread.load(std::memory_order_relaxed), switchNow,
        g_config.tracker_head_sign < 0 ? -1 : 1, g_config.tracker_head_offset_deg);
#else
    (void)mixed;
    (void)steps;
    (void)headReads;
    (void)threadChanges;
    (void)anniversary;
    (void)classic;
#endif
}
} // namespace

void HaloCEHeadRelative_PublishHead(const halo_ce::RenderContext& context, bool anniversary) noexcept
{
    if (published.writing.exchange(true, std::memory_order_acquire))
        return;
    HeadFrame frame;
    const bool live = context.tracking.generation && context.cameraStampUs && BuildHeadFrame(context, frame);
    const uint32_t flags =
        (live ? kHeadLive : 0u) | (live && frame.seat ? kHeadSeat : 0u) | (anniversary ? kHeadAnniversary : 0u);
    const uint32_t sequence = published.sequence.load();
    published.sequence.store(sequence + 1);
    for (int i = 0; i < 3; ++i)
    {
        published.forward[i].store(live ? frame.forward[i] : 0.0f);
        published.up[i].store(live ? frame.up[i] : 0.0f);
        published.heading[i].store(live ? frame.heading[i] : 0.0f);
    }
    published.stampUs.store(context.cameraStampUs);
    published.generation.store(context.tracking.generation);
    published.flags.store(flags);
    published.sequence.store(sequence + 2);
#if HALOMCCVR_DEV_DIAGNOSTICS
    published.thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
#endif
    published.writing.store(false, std::memory_order_release);
}

bool HaloCEHeadRelative_Poll(uintptr_t base, size_t size, uint32_t gen, bool isActive) noexcept
{
    auto& f = feature;
    f.active.store(isActive, std::memory_order_release);
    if (f.retained && (!isActive || base != f.base || gen != f.generation.load() || f.retiring.load()))
    {
        if (!Remove())
        {
            if (!f.cleanupReported)
            {
                LOG("TRACKER HEAD Halo CE: CleanupRequired: the hook is disabled for admission; the module and the "
                    "trampoline are kept until disable, quiescence and removal succeed");
                f.cleanupReported = true;
            }
            return false;
        }
        f.cleanupReported = false;
    }
    if (!isActive || !base || !gen)
        return false;
    f.active.store(true, std::memory_order_release);
    if (!f.ready.load() && !(gen == f.rejectedGeneration && base == f.rejectedBase))
        if (!Install(base, size, gen))
        {
            f.rejectedGeneration = gen;
            f.rejectedBase = base;
        }
    Report();
    return Current();
}
