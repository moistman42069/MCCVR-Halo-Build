#include <array>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <windows.h>

#include "aim_servo_logic.h"
#include "config.h"
#include "coop_probe_logic.h"
#include "cutscene_theater_logic.h"
#include "frame_pacing_logic.h"
#include "halo3_theater_logic.h"
#include "halo3_vehicle_logic.h"
#include "hud_layout_logic.h"
#include "input_logic.h"
#include "level_load_gate_logic.h"
#include "odst_bringup_logic.h"
#include "sigscan.h"
#include "odst_vehicle_logic.h"
#include "halo4_adapter.h"
#include "halo4_cui_reticle_logic.h"
#include "halo4_hud_logic.h"
#include "halo4_parity_trace_logic.h"
#include "halo4_render_logic.h"
#include "reach_adapter.h"
#include "reach_chud_logic.h"
#include "reach_observer_logic.h"
#include "reach_render_candidate.h"
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
#include "reach_render_preflight.h"
#endif
#include "reach_render_logic.h"
#include "reach_vehicle_logic.h"
#include "scope_logic.h"
#include "title_registry.h"
#include "title_runtime_state.h"
#include "view_cache_logic.h"
#include "two_hand_ik_logic.h"

#include <authored_reticle_logic.h>

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    ReachRenderCandidateProof CompleteReachRenderCandidateProof()
    {
        ReachRenderCandidateProof proof{};
        proof.retailIdentity = true;
        proof.mainRenderViewMatchCount = 1;
        proof.mainRenderViewAtExpectedRva = true;
        proof.mainRenderViewBodyHash = true;
        proof.playerViewRenderMatchCount = 1;
        proof.playerViewRenderAtExpectedRva = true;
        proof.playerViewRenderBodyHash = true;
        proof.cameraStackCallbackMatchCount = 1;
        proof.cameraStackCallbackAtExpectedRva = true;
        proof.cameraStackCallbackBodyHash = true;
        proof.frustumHelperMatchCount = 1;
        proof.frustumHelperAtExpectedRva = true;
        proof.frustumHelperExecutableRange = true;
        proof.fpCameraRebuildMatchCount = 1;
        proof.fpCameraRebuildAtExpectedRva = true;
        proof.fpCameraRebuildBodyHash = true;
        proof.fpCameraUploadMatchCount = 1;
        proof.fpCameraUploadAtExpectedRva = true;
        proof.fpCameraUploadBodyHash = true;
        proof.fpCameraWrapperBodyHashes = true;
        proof.exactFpCameraFlowEdges = true;
        proof.exactOuterCallerEdges = true;
        proof.exactInnerCallerEdge = true;
        proof.fixedDataRanges = true;
        return proof;
    }

    ReachDisplaySurfaceProof CompleteReachDisplaySurfaceProof(
        const ReachModuleEpoch& epoch,
        const ReachPreflightToken& preflight,
        uint64_t resourceRevision = 1)
    {
        ReachDisplaySurfaceProof proof{};
        proof.continuity.epoch = epoch;
        proof.continuity.resourceRevision = resourceRevision;
        proof.continuity.lifecycleSerial = 7;
        proof.continuity.swapchainIdentity = 0x20000000;
        proof.continuity.buffer0Identity = 0x20001000;
        proof.continuity.surfaceArrayIdentity = 0x20002000;
        proof.continuity.record0RtvIdentity = 0x20003000;
        proof.continuity.record0SrvIdentity = 0x20004000;
        proof.continuity.selectedRtvIdentity = 0x20003000;
        proof.continuity.deviceIdentity = 0x20008000;
        proof.continuity.immediateContextIdentity = 0x20005000;
        proof.continuity.eyeResourceIdentities[0] = 0x20006000;
        proof.continuity.eyeResourceIdentities[1] = 0x20007000;
        proof.continuity.specializationCount =
            kReachDisplaySurfaceCount;
        proof.continuity.selectedSpecialization = 0;
        proof.preflight = preflight;
        proof.immediateContextIdentity = 0x20005000;
        proof.eyeResourceIdentities[0] = 0x20006000;
        proof.eyeResourceIdentities[1] = 0x20007000;
        proof.source = {1920, 1080, 1, 1,
                        kReachDisplayFormatR8G8B8A8Unorm, 1, 0};
        proof.eyes[0] = proof.source;
        proof.eyes[1] = proof.source;
        proof.readyEyeMask = 0x3u;
        proof.engineSwapchainMatchesPresent = true;
        proof.selectedRtvMatchesRecord0 = true;
        proof.swapchainContract = true;
        proof.sameDevice = true;
        proof.immediateContext = true;
        return proof;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    // The byte-by-byte scan sig::Find used before it was made memchr-anchored.
    // Kept verbatim as the reference an optimisation must agree with: a faster
    // scan that finds a DIFFERENT address would silently hook the wrong code.
    uintptr_t ReferenceFind(
        const uint8_t* data, size_t size,
        const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& wild)
    {
        const size_t n = bytes.size();
        if (!n || size < n)
            return 0;
        const size_t last = size - n;
        for (size_t i = 0; i <= last; i++)
        {
            size_t j = 0;
            for (; j < n; j++)
                if (!wild[j] && data[i + j] != bytes[j])
                    break;
            if (j == n)
                return reinterpret_cast<uintptr_t>(data) + i;
        }
        return 0;
    }

    size_t CountText(std::string_view text, std::string_view needle)
    {
        size_t count = 0;
        for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string_view::npos;
             pos += needle.size())
            ++count;
        return count;
    }
}

int main()
{
    Check(!ShouldApplyArmIk(false, false),
          "Arm IK remains off when disabled in config");
    Check(!ShouldApplyArmIk(false, true),
          "Two-hand state cannot enable disabled arm IK");
    Check(ShouldApplyArmIk(true, false),
          "Configured arm IK returns outside a two-hand grab");
    Check(!ShouldApplyArmIk(true, true),
          "Two-hand grab suppresses competing arm IK");

    {
        // sig::Find is memchr-anchored for speed (it is the dominant cost of
        // every hook install: ODST resolves four optional features, each
        // needing a find plus an ambiguity re-scan, ~945 ms of whole-module
        // passes measured 2026-08-06). Speed is worthless if it changes WHICH
        // address is returned, so every case here is checked against the
        // original byte-by-byte loop kept in ReferenceFind.
        //
        // Deterministic xorshift, not <random>: the corpus must be identical
        // on every machine so a disagreement is always reproducible.
        uint32_t rng = 0x13579BDFu;
        auto NextByte = [&rng]() -> uint8_t {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return static_cast<uint8_t>(rng >> 24);
        };

        // A tiny alphabet makes accidental partial matches - and therefore
        // candidate positions that must be rejected - overwhelmingly common.
        std::vector<uint8_t> corpus(64 * 1024);
        for (auto& b : corpus)
            b = static_cast<uint8_t>(NextByte() & 0x03);

        bool allAgree = true;
        bool sawHit = false;
        bool sawMiss = false;
        for (int trial = 0; trial < 400; ++trial)
        {
            const size_t n = 1 + (NextByte() % 12);
            std::vector<uint8_t> bytes(n);
            std::vector<uint8_t> wild(n);
            std::string pattern;
            // Half the trials copy a real run out of the corpus (guaranteeing
            // a hit somewhere); half are random (usually a miss).
            const bool fromCorpus = (NextByte() & 1) != 0;
            const size_t origin = NextByte() % (corpus.size() - 16);
            for (size_t j = 0; j < n; ++j)
            {
                // Wildcards at every position, including the first - that is
                // the case the anchor skip has to handle by moving the anchor.
                wild[j] = (NextByte() % 4 == 0) ? 1 : 0;
                bytes[j] = wild[j]
                    ? 0
                    : (fromCorpus ? corpus[origin + j]
                                  : static_cast<uint8_t>(NextByte() & 0x03));
                if (!pattern.empty())
                    pattern += ' ';
                if (wild[j])
                {
                    pattern += "??";
                }
                else
                {
                    static const char* kHex = "0123456789ABCDEF";
                    pattern += kHex[bytes[j] >> 4];
                    pattern += kHex[bytes[j] & 0x0F];
                }
            }
            const uintptr_t base = reinterpret_cast<uintptr_t>(corpus.data());
            const uintptr_t expected =
                ReferenceFind(corpus.data(), corpus.size(), bytes, wild);
            const uintptr_t actual =
                sig::Find(base, corpus.size(), pattern.c_str());
            if (expected != actual)
                allAgree = false;
            if (expected)
                sawHit = true;
            else
                sawMiss = true;

            // The ambiguity re-scan callers rely on to prove a signature is
            // unique starts one byte past a hit; it must agree there too.
            if (expected)
            {
                const uintptr_t offset = expected - base + 1;
                const uintptr_t expectedNext = ReferenceFind(
                    corpus.data() + offset, corpus.size() - offset,
                    bytes, wild);
                const uintptr_t actualNext = sig::Find(
                    base + offset, corpus.size() - offset, pattern.c_str());
                if (expectedNext != actualNext)
                    allAgree = false;
            }
        }
        Check(allAgree, "sig::Find matches the byte-by-byte reference exactly");
        Check(sawHit && sawMiss,
              "sig::Find equivalence corpus covered both hits and misses");

        // All-wildcard: matches at the first offset, as the original did.
        const std::vector<uint8_t> corpusSmall{0xAA, 0xBB, 0xCC};
        const uintptr_t smallBase =
            reinterpret_cast<uintptr_t>(corpusSmall.data());
        Check(sig::Find(smallBase, corpusSmall.size(), "?? ??") == smallBase,
              "sig::Find all-wildcard pattern matches at the first offset");
        // A pattern longer than the region never matches.
        Check(sig::Find(smallBase, corpusSmall.size(),
                        "AA BB CC DD") == 0,
              "sig::Find rejects a pattern longer than the region");
        // A trailing-wildcard pattern that would run off the end must not
        // match early, and an exact tail match must be found.
        Check(sig::Find(smallBase, corpusSmall.size(), "CC") ==
                  smallBase + 2,
              "sig::Find locates a match at the final offset");
    }
    {
        // Level-load gate decision core. Each Observe() is one 50 ms worker
        // sample; `true` means the engine's player-view fingerprint changed.
        // Scenarios replay the preserved captures in
        // docs/ODST-LEVEL-LOAD-LOCKOUT.md.
        using Gate = LevelLoadGateLogic;

        // The b70141d bounce, replayed: a Save & Quit leaves ~3 s of the
        // OUTGOING level's dying ticks running when observation begins. The
        // shipped 12-sample "already running" rule opened at 0.6 s and the
        // load bounced. The dying ticks must hold, the loading screen's
        // freeze must hold, and the NEW level's first tick must open.
        Gate bounced;
        bool heldThroughDyingTicks = true;
        for (int i = 0; i < 60; ++i) // 3 s of dying ticks
            heldThroughDyingTicks &=
                bounced.Observe(true) == Gate::Decision::Hold;
        Check(heldThroughDyingTicks,
              "gate holds through 3 s of the outgoing level's dying ticks");
        bool heldThroughLoadFreeze = true;
        for (int i = 0; i < 144; ++i) // 7.2 s loading-screen freeze
            heldThroughLoadFreeze &=
                bounced.Observe(false) == Gate::Decision::Hold;
        Check(heldThroughLoadFreeze,
              "gate holds through the loading screen freeze");
        Check(bounced.Observe(true) ==
                  Gate::Decision::OpenFrozenThenTicking,
              "gate opens on the new level's first tick");

        // The 11,766 ms successful retry: the OLD 12 s unconditional timeout
        // was within 300 ms of installing into this loading screen. There is
        // no timeout now - stillness holds indefinitely and only the tick
        // opens.
        Gate longLoad;
        bool heldThroughLongFreeze = true;
        for (int i = 0; i < 400; ++i) // 20 s of freeze, past any old timeout
            heldThroughLongFreeze &=
                longLoad.Observe(false) == Gate::Decision::Hold;
        Check(heldThroughLongFreeze,
              "gate holds a 20 s freeze with no timeout install");
        Check(longLoad.Observe(true) ==
                  Gate::Decision::OpenFrozenThenTicking,
              "gate still opens after an arbitrarily long freeze");

        // A menu idle is stillness forever; the gate must simply keep
        // holding (the flat screen layer still shows in the headset).
        Gate menuIdle;
        bool heldThroughMenu = true;
        for (int i = 0; i < 2400; ++i) // 2 minutes in a lobby
            heldThroughMenu &=
                menuIdle.Observe(false) == Gate::Decision::Hold;
        Check(heldThroughMenu && !menuIdle.IsOpen(),
              "gate holds a menu idle indefinitely");

        // A title genuinely mid-level when observation begins never
        // freezes: 6 s of uninterrupted change opens, one sample earlier
        // holds. Dying ticks measured <= ~4 s can never reach it.
        Gate running;
        bool heldBeforeThreshold = true;
        for (uint32_t i = 0; i + 1 < Gate::kAlreadyRunningSamples; ++i)
            heldBeforeThreshold &=
                running.Observe(true) == Gate::Decision::Hold;
        Check(heldBeforeThreshold,
              "gate holds one sample short of already-running");
        Check(running.Observe(true) == Gate::Decision::OpenAlreadyRunning,
              "gate opens for a title already running 6 s");

        // The frozen half is deliberately satisfied by a SINGLE still
        // sample. In every bounced capture the dying ticks ran with no
        // still sample at all (still=0 at the first gate log line), while
        // the captured pause-resume witnesses exactly one still before
        // gameplay's first tick - a higher minimum (300 ms in build
        // 19757f6) sent every resume down the 6 s already-running path and
        // was rejected as far too slow to enter 3D.
        Gate oneStill;
        for (int i = 0; i < 40; ++i)
            oneStill.Observe(true);
        Check(!oneStill.SawFrozen(),
              "uninterrupted dying ticks never satisfy the frozen half");
        oneStill.Observe(false);
        Check(oneStill.SawFrozen(),
              "a single witnessed still satisfies the frozen half");
        Check(oneStill.Observe(true) ==
                  Gate::Decision::OpenFrozenThenTicking,
              "the next tick after a witnessed still opens the gate");

        // Pause -> resume (the 07:14:13 capture): the pause fade's single
        // witnessed still is the frozen half, the resume tick opens - the
        // ~94 ms resume the accepted builds had.
        Gate resume;
        for (uint32_t i = 0; i < Gate::kFrozenMinSamples; ++i)
            resume.Observe(false);
        Check(resume.Observe(true) ==
                  Gate::Decision::OpenFrozenThenTicking,
              "pause-resume reopens on the first tick after stillness");

        // Rearm: once open, Reset() closes it and the proof must be
        // re-earned - the b934b61 one-shot defect, expressed as a test.
        Gate rearm;
        for (uint32_t i = 0; i < Gate::kFrozenMinSamples; ++i)
            rearm.Observe(false);
        rearm.Observe(true);
        Check(rearm.IsOpen(), "gate open before rearm");
        rearm.Reset();
        Check(!rearm.IsOpen(), "rearm closes the gate");
        bool heldAfterRearm = true;
        for (int i = 0; i < 60; ++i)
            heldAfterRearm &= rearm.Observe(true) == Gate::Decision::Hold;
        Check(heldAfterRearm,
              "after rearm the dying ticks hold again until a real freeze");
    }
    {
        const float neutral[4]{1.0f, 0.3f, 0.6f, 0.9f};
        const float neutralFaded[4]{0.5f, 0.15f, 0.3f, 0.45f};
        const float friendly[4]{1.0f, 0.8f, 0.2f, 0.4f};
        const float enemy[4]{1.0f, 0.2f, 0.8f, 0.4f};
        const uint32_t neutralState =
            ClassifyAuthoredReticleColorOrdering(neutral);
        Check(neutralState != 0 &&
                  neutralState ==
                      ClassifyAuthoredReticleColorOrdering(neutralFaded) &&
                  neutralState !=
                      ClassifyAuthoredReticleColorOrdering(friendly) &&
                  neutralState !=
                      ClassifyAuthoredReticleColorOrdering(enemy) &&
                  ClassifyAuthoredReticleColorOrdering(friendly) !=
                      ClassifyAuthoredReticleColorOrdering(enemy),
              "authored colour ordering and fade stability");
        const float invalid[4]{1.0f, 0.0f, INFINITY, 0.0f};
        Check(ClassifyAuthoredReticleColorOrdering(invalid) == 0,
              "authored invalid colour rejected");
    }
    {
        AuthoredReticleRefreshState state{};
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 7, 2, 0, 100, 1, state), "reticle settle starts");
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 7, 2, 0, 123, 1, state), "reticle still settling");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 7, 2, 0, 124, 1, state), "reticle settled publish");
        MarkAuthoredReticleUploaded(state, 7, 2, 0, 124);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 7, 2, 0, 130, 1, state), "same colour skipped");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 7, 4, 0, 130, 1, state), "colour edge publish");
        MarkAuthoredReticleUploaded(state, 7, 4, 0, 130);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 7, 8, 0, 133, 1, state), "colour edge throttled");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 7, 8, 0, 136, 1, state), "colour edge after gap");
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 8, 8, 0, 200, 1, state), "new identity settles");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 8, 8, 0, 224, 1, state), "new identity publishes");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 7, 0, 0, 230, 1, state), "bounded title cadence");
        // GitHub #70: Reach's crosshair vanished for a moment when the player
        // took damage. The preserved 74e1477 log shows the published art key
        // flipping to one weapon-independent value and back inside ~50 ms,
        // over and over during combat, while the quad stayed SUBMITTED with
        // held art - so a momentary alternate class-2 widget set was being
        // published straight over good art. A different identity must now
        // settle first; the identity already on the quad still republishes on
        // the bounded cadence, which is what keeps the animation live.
        AuthoredReticleRefreshState flickerState{};
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 0, 500, 1, flickerState),
              "bounded first art publishes immediately");
        MarkAuthoredReticleUploaded(flickerState, 0xABC, 0, 0, 500);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 0, 506, 1, flickerState),
              "bounded animation republishes the same identity");
        MarkAuthoredReticleUploaded(flickerState, 0xABC, 0, 0, 506);
        // The ~50 ms transient: six frames of a different identity, then back.
        for (uint64_t frame = 512; frame < 518; ++frame)
        {
            Check(!ShouldUploadAuthoredReticle(
                      AuthoredReticleRefreshPolicy::BoundedAnimation,
                      true, true, 0xDEF, 0, 0, frame, 1, flickerState),
                  "bounded transient identity never publishes");
        }
        Check(flickerState.lastPublishedKey == 0xABC,
              "bounded transient leaves the held art alone");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 0, 518, 1, flickerState),
              "bounded animation resumes after the transient");
        MarkAuthoredReticleUploaded(flickerState, 0xABC, 0, 0, 518);
        // A real weapon swap still gets through: the same identity held for
        // the settle window publishes.
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xF00, 0, 0, 600, 1, flickerState),
              "bounded new identity settles");
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xF00, 0, 0, 623, 1, flickerState),
              "bounded new identity still settling");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xF00, 0, 0, 624, 1, flickerState),
              "bounded new identity publishes once settled");
        // GitHub #70, the harder half: Reach's crosshair THINS OUT before it
        // stops. The preserved c2d9149 log shows a two-second window uploading
        // 19 where the steady rate was 30, so frames capture a fragment of the
        // reticle under the SAME identity key. Publishing that fragment is
        // what the player sees as the crosshair disappearing when hit.
        AuthoredReticleRefreshState thinState{};
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 5, 700, 1, thinState),
              "bounded full capture publishes");
        MarkAuthoredReticleUploaded(thinState, 0xABC, 0, 5, 700);
        Check(thinState.lastPublishedDraws == 5, "published piece count kept");
        for (uint64_t frame = 706; frame < 724; ++frame)
        {
            Check(!ShouldUploadAuthoredReticle(
                      AuthoredReticleRefreshPolicy::BoundedAnimation,
                      true, true, 0xABC, 0, 1, frame, 1, thinState),
                  "thinned capture never replaces good art");
        }
        Check(thinState.lastPublishedDraws == 5,
              "thinned capture leaves the held art alone");
        // The full crosshair coming back publishes at once - no settle debt.
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 5, 724, 1, thinState),
              "full capture returns immediately");
        MarkAuthoredReticleUploaded(thinState, 0xABC, 0, 5, 724);
        // A permanently simpler crosshair is not held hostage: once the lower
        // piece count persists past the settle window it publishes.
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 2, 800, 1, thinState),
              "sustained thinner capture starts settling");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 2, 824, 1, thinState),
              "sustained thinner capture eventually publishes");
        // A title that supplies no piece count (Halo 3, ODST) is unaffected.
        AuthoredReticleRefreshState noCountState{};
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 0, 900, 1, noCountState),
              "no piece count publishes");
        MarkAuthoredReticleUploaded(noCountState, 0xABC, 0, 0, 900);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation,
                  true, true, 0xABC, 0, 0, 906, 1, noCountState),
              "no piece count leaves the guard inert");
        AuthoredReticleRefreshState immediateState{};
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityImmediate,
                  true, true, 31, 0, 0, 240, 1, immediateState), __func__);
        MarkAuthoredReticleUploaded(immediateState, 31, 0, 0, 240);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityImmediate,
                  true, true, 31, 0, 0, 270, 1, immediateState), __func__);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityImmediate,
                  true, true, 32, 0, 0, 270, 1, immediateState), __func__);
        AuthoredReticleRefreshState odstState{};
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 21, 0, 0, 300, 1, odstState), __func__);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 21, 0, 0, 324, 1, odstState), __func__);
        MarkAuthoredReticleUploaded(odstState, 21, 0, 0, 324);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 21, 0, 0, 400, 1, odstState), __func__);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 22, 0, 0, 400, 1, odstState), __func__);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 22, 0, 0, 424, 1, odstState), __func__);
        Check(!AuthoredReticleLayerHasContent(true, false) &&
                  AuthoredReticleLayerHasContent(true, true) &&
                  AuthoredReticleLayerHasContent(false, false),
              "held authored layer gate");
        Check(AuthoredReticleCaptureOwnsSceneBind(true, true, true) &&
                  !AuthoredReticleCaptureOwnsSceneBind(false, true, true) &&
                  !AuthoredReticleCaptureOwnsSceneBind(true, false, true) &&
                  !AuthoredReticleCaptureOwnsSceneBind(true, true, false),
              "authored capture owns only the exact active scene bind");
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState,
                  true, true, 9, 2, 0, 400, 2, state) &&
                  state.ownerEpoch == 2 && state.lastPublishedKey == 0,
              "reticle owner reset");
    }
    {
        // Offscreen capture cadence. The first sample and every widget within
        // an already-sampled frame are always admitted, so a compound reticle
        // is never captured in pieces from different frames.
        Check(ShouldSampleAuthoredCapture(6, 0, 100), "first capture sampled");
        Check(ShouldSampleAuthoredCapture(6, 100, 100), "same frame re-admitted");
        Check(!ShouldSampleAuthoredCapture(6, 100, 101), "capture held inside gap");
        Check(!ShouldSampleAuthoredCapture(6, 100, 105), "capture held to the gap");
        Check(ShouldSampleAuthoredCapture(6, 100, 106), "capture sampled at the gap");
        Check(ShouldSampleAuthoredCapture(30, 100, 130), "slow cadence sampled");
        Check(!ShouldSampleAuthoredCapture(30, 100, 129), "slow cadence held");
        Check(ShouldSampleAuthoredCapture(6, 500, 10), "serial restart re-samples");
        Check(ShouldSampleAuthoredCapture(0, 100, 101), "gap 0 never throttles");

        // The capture gap and the publish floor are deliberately the same
        // number: a sampled frame must always be allowed to publish, or the
        // capture work is spent and thrown away.
        Check(ResolveAuthoredAnimationGapFrames(6) == kMinimumUploadGapFrames &&
                  ResolveAuthoredAnimationGapFrames(1) == kMinimumUploadGapFrames &&
                  ResolveAuthoredAnimationGapFrames(12) == 12 &&
                  ResolveAuthoredAnimationGapFrames(600) == 60 &&
                  ResolveAuthoredAnimationGapFrames(0) == 0 &&
                  ResolveAuthoredAnimationGapFrames(-4) == 0,
              "authored animation cadence clamp");

        // A Halo 3 sample publishes on the frame it was captured on, every
        // time, so the animation cadence the player sets is the cadence they
        // get. This is the property the frozen-snapshot bug violated.
        AuthoredReticleRefreshState animated{};
        uint64_t lastSample = 0;
        int published = 0;
        const uint64_t gap = ResolveAuthoredAnimationGapFrames(6);
        for (uint64_t serial = 1; serial <= 120; ++serial)
        {
            if (!ShouldSampleAuthoredCapture(gap, lastSample, serial))
                continue;
            lastSample = serial;
            // The identity key is deliberately constant: an animating Halo 3
            // crosshair does not change which widgets drew.
            if (ShouldUploadAuthoredReticle(
                    AuthoredReticleRefreshPolicy::BoundedAnimation,
                    true, true, 0xABCD, 0, 0, serial, 1, animated))
            {
                MarkAuthoredReticleUploaded(animated, 0xABCD, 0, 0, serial);
                ++published;
            }
        }
        Check(published == 20, "every animated sample publishes");

        // Halo 4 has no proven stable weapon-art identity, so animation=0
        // holds the fixed key between slow probes rather than freezing it for
        // the rest of the level. Every 30-frame sample must still publish.
        AuthoredReticleRefreshState halo4Held{};
        lastSample = 0;
        published = 0;
        for (uint64_t serial = 1; serial <= 120; ++serial)
        {
            if (!ShouldSampleAuthoredCapture(30, lastSample, serial))
                continue;
            lastSample = serial;
            if (ShouldUploadAuthoredReticle(
                    AuthoredReticleRefreshPolicy::BoundedAnimation,
                    true, true, 1, 0, 0, serial, 4, halo4Held))
            {
                MarkAuthoredReticleUploaded(
                    halo4Held, 1, 0, 0, serial);
                ++published;
            }
        }
        Check(published == 4,
              "Halo 4 held art republishes on each slow fixed-key probe");
    }
    {
        constexpr std::array<uint8_t, 6> repeatedPattern{
            0xAA, 0xBB, 0xCC, 0xAA, 0xBB, 0xCC
        };
        constexpr std::array<uint8_t, 3> twice{ 0xAA, 0xBB, 0xCC };
        constexpr std::array<uint8_t, 3> once{ 0xBB, 0xCC, 0xAA };
        constexpr std::array<uint8_t, 2> absent{ 0xCC, 0xBB };
        Check(CountReachExactPattern(
                  repeatedPattern.data(), repeatedPattern.size(),
                  twice.data(), twice.size()) == 2,
            "Reach loaded-image proof rejects a multiple-match exact pattern");
        Check(CountReachExactPattern(
                  repeatedPattern.data(), repeatedPattern.size(),
                  once.data(), once.size()) == 1,
            "Reach loaded-image proof accepts one exact pattern match");
        Check(CountReachExactPattern(
                  repeatedPattern.data(), repeatedPattern.size(),
                  absent.data(), absent.size()) == 0,
            "Reach loaded-image proof rejects an absent exact pattern");
        Check(CountReachExactPattern(
                  nullptr, repeatedPattern.size(), twice.data(), twice.size()) == 0 &&
                  CountReachExactPattern(
                      repeatedPattern.data(), repeatedPattern.size(),
                      nullptr, twice.size()) == 0 &&
                  CountReachExactPattern(
                      repeatedPattern.data(), repeatedPattern.size(),
                      twice.data(), 0) == 0,
            "Reach exact-pattern counting fails closed on invalid inputs");

        std::array<uint8_t, 5> call{ 0xE8, 0, 0, 0, 0 };
        uintptr_t callTarget = 0;
        int32_t displacement = 0x20;
        std::memcpy(call.data() + 1, &displacement, sizeof(displacement));
        Check(ResolveReachRel32Call(
                  0x1000, call.data(), call.size(), callTarget) &&
                  callTarget == 0x1025,
            "Reach rel32 proof resolves a forward call target exactly");
        displacement = -0x20;
        std::memcpy(call.data() + 1, &displacement, sizeof(displacement));
        Check(ResolveReachRel32Call(
                  0x1000, call.data(), call.size(), callTarget) &&
                  callTarget == 0x0FE5,
            "Reach rel32 proof resolves a backward call target exactly");
        call[0] = 0xE9;
        Check(!ResolveReachRel32Call(
                  0x1000, call.data(), call.size(), callTarget),
            "Reach rel32 proof rejects a non-call opcode");
        call[0] = 0xE8;
        Check(!ResolveReachRel32Call(
                  0x1000, call.data(), 4, callTarget),
            "Reach rel32 proof rejects a truncated instruction");
        displacement = -6;
        std::memcpy(call.data() + 1, &displacement, sizeof(displacement));
        Check(!ResolveReachRel32Call(
                  0, call.data(), call.size(), callTarget),
            "Reach rel32 proof rejects target-address underflow");
        displacement = 1;
        std::memcpy(call.data() + 1, &displacement, sizeof(displacement));
        Check(!ResolveReachRel32Call(
                  std::numeric_limits<uintptr_t>::max() - 5,
                  call.data(), call.size(), callTarget),
            "Reach rel32 proof rejects target-address overflow");

        constexpr uintptr_t playerArray = 0x100000;
        constexpr size_t playerStride = 0xA40;
        constexpr size_t playerCount = 4;
        for (uint32_t slot = 0; slot < playerCount; ++slot)
        {
            const ReachObservedView view = ClassifyReachObservedView(
                playerArray + slot * playerStride,
                playerArray, playerStride, playerCount);
            Check(view.kind == ReachObservedViewKind::NormalPlayerSlot &&
                      view.slot == slot,
                "Reach active-view proof recognizes each exact 0xA40 player slot");
        }
        Check(ClassifyReachObservedView(
                  0, playerArray, playerStride, playerCount).kind ==
                  ReachObservedViewKind::None,
            "Reach active-view proof treats a null pointer as no transaction");
        Check(ClassifyReachObservedView(
                  playerArray + 1, playerArray, playerStride, playerCount).kind ==
                  ReachObservedViewKind::OutsidePlayerArray,
            "Reach active-view proof rejects an interior non-slot pointer");
        Check(ClassifyReachObservedView(
                  playerArray - 1, playerArray, playerStride, playerCount).kind ==
                  ReachObservedViewKind::OutsidePlayerArray &&
                  ClassifyReachObservedView(
                      playerArray + playerStride * playerCount,
                      playerArray, playerStride, playerCount).kind ==
                      ReachObservedViewKind::OutsidePlayerArray,
            "Reach active-view proof rejects pointers outside the four-slot array");
        Check(ClassifyReachObservedView(
                  playerArray, 0, playerStride, playerCount).kind ==
                  ReachObservedViewKind::OutsidePlayerArray &&
                  ClassifyReachObservedView(
                      playerArray, playerArray, 0, playerCount).kind ==
                      ReachObservedViewKind::OutsidePlayerArray,
            "Reach active-view proof fails closed on an invalid array description");

        ReachObserverTransactionGate transactionGate;
        Check(!transactionGate.Observe(playerArray),
            "Reach observer ignores a mid-transaction value at session start");
        Check(!transactionGate.Observe(0) &&
                  transactionGate.Observe(playerArray),
            "Reach observer requires a witnessed clear before its first transaction");
        Check(!transactionGate.Observe(playerArray),
            "Reach observer counts a latched pointer only once");
        Check(transactionGate.Observe(playerArray + playerStride),
            "Reach observer recognizes a changed non-null transaction owner");
        transactionGate.RequireClear();
        Check(!transactionGate.Observe(playerArray + playerStride) &&
                  !transactionGate.HasLatchedValue() &&
                  !transactionGate.Observe(0) &&
                  transactionGate.Observe(playerArray + playerStride),
            "Reach continuity reset requires a new witnessed clear");
    }

    {
        // Independent HREK literals: the validator must accept both optimized
        // official chud_draw_widget bodies and reject any ownership-bearing
        // byte drift. The rel32 itself is link-layout data and is intentionally
        // allowed to differ.
        constexpr std::array<uint8_t, 33> officialEntry{
            0x48, 0x8B, 0xC4, 0x44, 0x89, 0x48, 0x20, 0x48, 0x89, 0x50, 0x10,
            0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8D, 0x68, 0xA9, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};
        constexpr std::array<uint8_t, 3> tagDescriptorMove{
            0x4C, 0x8B, 0xF2};
        constexpr std::array<uint8_t, 3> sapienDescriptorMove{
            0x4C, 0x8B, 0xFA};
        constexpr std::array<uint8_t, 4> fifthArgumentLoad{
            0x4C, 0x8B, 0x4D, 0x7F};
        constexpr std::array<uint8_t, 6> tagClassRead{
            0x41, 0x0F, 0xBE, 0x56, 0x04, 0xE8};
        constexpr std::array<uint8_t, 6> sapienClassRead{
            0x41, 0x0F, 0xBE, 0x57, 0x04, 0xE8};
        constexpr std::array<uint8_t, 3> postClassCall{
            0x48, 0x8B, 0xD0};
        Check(kReachHrekChudDrawWidgetEntryBytes == officialEntry,
            "Reach CHUD entry bytes remain the independently recorded official HREK signature");

        std::array<uint8_t, kReachTagPlayChudDrawWidgetBodySize> tagPlay{};
        std::memcpy(tagPlay.data(), officialEntry.data(), officialEntry.size());
        std::memcpy(
            tagPlay.data() + kReachTagPlayChudDescriptorMoveOffset,
            tagDescriptorMove.data(), tagDescriptorMove.size());
        std::memcpy(
            tagPlay.data() + kReachTagPlayChudFifthArgumentLoadOffset,
            fifthArgumentLoad.data(), fifthArgumentLoad.size());
        std::memcpy(
            tagPlay.data() + kReachTagPlayChudClassReadOffset,
            tagClassRead.data(), tagClassRead.size());
        tagPlay[kReachTagPlayChudClassReadOffset + 6] = 0x12;
        tagPlay[kReachTagPlayChudClassReadOffset + 7] = 0x34;
        tagPlay[kReachTagPlayChudClassReadOffset + 8] = 0x56;
        tagPlay[kReachTagPlayChudClassReadOffset + 9] = 0x78;
        std::memcpy(
            tagPlay.data() + kReachTagPlayChudClassReadOffset + 10,
            postClassCall.data(), postClassCall.size());
        Check(ReachHrekChudDrawWidgetLayoutMatches(tagPlay),
            "Reach CHUD validator accepts the exact official reach_tag_play layout");
        Check(!ReachHrekChudDrawWidgetLayoutMatches(
                  std::span<const uint8_t>(
                      tagPlay.data(), tagPlay.size() - 1)),
            "Reach CHUD validator rejects a non-official tag-play body size");
        tagPlay[0] = 0x49;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(tagPlay),
            "Reach CHUD validator rejects official-entry signature drift");
        tagPlay[0] = officialEntry[0];
        tagPlay[kReachTagPlayChudFifthArgumentLoadOffset + 3] = 0x7E;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(tagPlay),
            "Reach CHUD validator rejects fifth-argument ABI drift in tag-play");
        tagPlay[kReachTagPlayChudFifthArgumentLoadOffset + 3] = 0x7F;
        tagPlay[kReachTagPlayChudClassReadOffset + 4] = 0x05;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(tagPlay),
            "Reach CHUD validator rejects descriptor-class offset drift in tag-play");
        tagPlay[kReachTagPlayChudClassReadOffset + 4] = 0x04;

        std::array<uint8_t, kReachSapienPlayChudDrawWidgetBodySize> sapienPlay{};
        std::memcpy(
            sapienPlay.data(), officialEntry.data(), officialEntry.size());
        std::memcpy(
            sapienPlay.data() + kReachSapienPlayChudDescriptorMoveOffset,
            sapienDescriptorMove.data(), sapienDescriptorMove.size());
        std::memcpy(
            sapienPlay.data() + kReachSapienPlayChudFifthArgumentLoadOffset,
            fifthArgumentLoad.data(), fifthArgumentLoad.size());
        std::memcpy(
            sapienPlay.data() + kReachSapienPlayChudClassReadOffset,
            sapienClassRead.data(), sapienClassRead.size());
        sapienPlay[kReachSapienPlayChudClassReadOffset + 6] = 0xDE;
        sapienPlay[kReachSapienPlayChudClassReadOffset + 7] = 0xAD;
        sapienPlay[kReachSapienPlayChudClassReadOffset + 8] = 0xBE;
        sapienPlay[kReachSapienPlayChudClassReadOffset + 9] = 0xEF;
        std::memcpy(
            sapienPlay.data() + kReachSapienPlayChudClassReadOffset + 10,
            postClassCall.data(), postClassCall.size());
        Check(ReachHrekChudDrawWidgetLayoutMatches(sapienPlay),
            "Reach CHUD validator accepts the exact official sapien_play layout");
        sapienPlay[kReachSapienPlayChudDescriptorMoveOffset + 2] = 0xF2;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(sapienPlay),
            "Reach CHUD validator rejects descriptor-register drift in sapien-play");
        sapienPlay[kReachSapienPlayChudDescriptorMoveOffset + 2] = 0xFA;
        sapienPlay[kReachSapienPlayChudClassReadOffset + 10] = 0x49;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(sapienPlay),
            "Reach CHUD validator rejects post-class-call flow drift in sapien-play");

        using Action = ReachChudCrosshairAction;
        Check(ReachDecideChudCrosshairAction(
                  false, true, kReachChudCrosshairScriptingClass,
                  true, true, 0, false) == Action::DrawStock &&
              ReachDecideChudCrosshairAction(
                  false, false, kReachChudCrosshairScriptingClass,
                  true, true, -1, true) == Action::DrawStock,
            "Reach CHUD stays stock outside its owned stereo transaction");
        Check(ReachDecideChudCrosshairAction(
                  true, false, kReachChudCrosshairScriptingClass,
                  false, true, 0, false) == Action::RejectTransaction,
            "Owned Reach CHUD rejects the transaction when its scripting-class descriptor is unreadable");
        Check(ReachDecideChudCrosshairAction(
                  true, true, 1, false, true, 0, false) ==
                  Action::DrawStock,
            "Reach CHUD never suppresses a non-crosshair scripting class");
        Check(ReachDecideChudCrosshairAction(
                  true, true, kReachChudCrosshairScriptingClass,
                  false, true, 0, false) == Action::Suppress,
            "The universal crosshair switch suppresses only the native class-2 widget");
        Check(ReachDecideChudCrosshairAction(
                  true, true, kReachChudCrosshairScriptingClass,
                  true, false, 0, false) == Action::DrawStock,
            "The universal kill-reticle switch deliberately restores the stock widget");
        Check(ReachDecideChudCrosshairAction(
                  true, true, kReachChudCrosshairScriptingClass,
                  true, true, 0, false) == Action::CaptureAuthored &&
              ReachDecideChudCrosshairAction(
                  true, true, kReachChudCrosshairScriptingClass,
                  true, true, 1, false) == Action::Suppress,
            "Left-eye-first captures the authored widget once and suppresses its opposite-eye duplicate");
        Check(ReachDecideChudCrosshairAction(
                  true, true, kReachChudCrosshairScriptingClass,
                  true, true, 1, true) == Action::CaptureAuthored &&
              ReachDecideChudCrosshairAction(
                  true, true, kReachChudCrosshairScriptingClass,
                  true, true, 0, true) == Action::Suppress,
            "Right-eye-first captures the authored widget once and suppresses its opposite-eye duplicate");
        Check(ReachDecideChudCrosshairAction(
                  true, true, kReachChudCrosshairScriptingClass,
                  true, true, -1, true) == Action::RejectTransaction &&
              ReachDecideChudCrosshairAction(
                  true, true, kReachChudCrosshairScriptingClass,
                  true, true, 2, false) == Action::RejectTransaction,
            "Owned Reach CHUD rejects invalid eyes and never captures them as an authored fallback");

        Check(ReachAuthoredCrosshairPairComplete(false, true, false) &&
              ReachAuthoredCrosshairPairComplete(true, false, false) &&
              ReachAuthoredCrosshairPairComplete(true, true, true) &&
              !ReachAuthoredCrosshairPairComplete(true, true, false),
            "A required Reach authored crosshair rejects a pair only when class 2 was emitted without a completed capture");

        Check(ReachCanSubmitCompleteProjection(
                  false, false, true, false) &&
              ReachCanSubmitCompleteProjection(
                  false, true, false, false) &&
              ReachCanSubmitCompleteProjection(
                  true, true, false, true) &&
              !ReachCanSubmitCompleteProjection(
                  true, false, false, true) &&
              !ReachCanSubmitCompleteProjection(
                  true, true, true, true) &&
              !ReachCanSubmitCompleteProjection(
                  true, true, false, false),
            "Reach queues its world projection only after both eye uploads, authored upload success, and a live post-upload owner proof");
    }

    {
        static_assert(kReachSpartanFpBodyBoneMap.size() == 47);
        static_assert(kReachEliteFpBodyBoneMap.size() == 41);
        static_assert(kReachFpMaxSourceNodeCount == 120);

        // Independent evidence literals: do not derive these from the
        // production constants, so any fingerprint drift fails this test.
        constexpr std::array<int32_t, 47> expectedSpartan{{
            0, 3, 1, 2, 5, 4, 6, 9, 10, 8, 7, 13, 12, 14, 11, 22,
            26, 18, 21, 24, 19, 20, 15, 23, 16, 17, 25, 36, 35, 32,
            28, 33, 29, 27, 34, 31, 30, 46, 38, 42, 37, 40, 39, 43,
            45, 44, 41}};
        constexpr std::array<int32_t, 41> expectedElite{{
            0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 10, 14, 12, 11, 13, 22,
            20, 18, 21, 19, 16, 23, 24, 17, 15, 27, 28, 25, 31, 32,
            30, 26, 29, 35, 34, 39, 33, 40, 36, 38, 37}};
        Check(kReachSpartanFpBodyBoneMap == expectedSpartan &&
                  kReachEliteFpBodyBoneMap == expectedElite,
            "Reach production fingerprints match independent full evidence literals");
        Check(expectedSpartan[6] == 6 && expectedSpartan[7] == 9 &&
                  expectedSpartan[8] == 10 && expectedSpartan[11] == 13 &&
                  expectedSpartan[13] == 14 &&
                  expectedSpartan[4] == 5 && expectedSpartan[10] == 7 &&
                  expectedSpartan[14] == 11 && expectedSpartan[5] == 4 &&
                  expectedElite[6] == 6 && expectedElite[9] == 9 &&
                  expectedElite[10] == 10 && expectedElite[11] == 14 &&
                  expectedElite[14] == 13 &&
                  expectedElite[4] == 5 && expectedElite[7] == 7 &&
                  expectedElite[13] == 11 && expectedElite[5] == 4,
            "Reach render-output chains map exactly to the shared source chains");

        const std::span<const int32_t> spartanMap{
            kReachSpartanFpBodyBoneMap};
        ReachFpBodyLayout spartan{};
        Check(ResolveReachFpBodyLayout(spartanMap, 65, spartan) &&
                  spartan.Valid() &&
                  spartan.kind == ReachFpBodyKind::Spartan &&
                  spartan.paletteBodyNodeCount == 47 &&
                  spartan.liveSourceNodeCount == 65,
            "Reach Spartan keeps the 47-node body distinct from the 65-node source");
        Check(spartan.rightShoulderSource == 6 &&
                  spartan.rightElbowSource == 9 &&
                  spartan.rightWristSource == 13 &&
                  spartan.leftShoulderSource == 5 &&
                  spartan.leftElbowSource == 7 &&
                  spartan.leftWristSource == 11 &&
                  spartan.cameraControlSource == 4,
            "Reach Spartan resolves the exact source arm and camera anchors");
        Check(spartan.leftHandSourceDescendants ==
                       0x000003E0F81F8800ull &&
                   spartan.rightHandSourceDescendants ==
                       0x00007C1F07E02000ull,
            "Reach Spartan maps exact hand descendants into source order");
        Check(kReachLeftControllerOwnedAuxiliarySourceMask ==
                      0x00000000000011A0ull &&
                  kReachRightControllerOwnedAuxiliarySourceMask ==
                      0x0000000000004640ull &&
                  spartan.leftControllerOwnedSourceBranch ==
                      0x000003E0F81F99A0ull &&
                  spartan.rightControllerOwnedSourceBranch ==
                      0x00007C1F07E06640ull &&
                  (spartan.leftControllerOwnedSourceBranch ^
                   spartan.leftHandSourceDescendants) ==
                      kReachLeftControllerOwnedAuxiliarySourceMask &&
                  (spartan.rightControllerOwnedSourceBranch ^
                   spartan.rightHandSourceDescendants) ==
                      kReachRightControllerOwnedAuxiliarySourceMask &&
                  (spartan.leftControllerOwnedSourceBranch &
                   ~spartan.leftHandSourceDescendants) ==
                      kReachLeftControllerOwnedAuxiliarySourceMask &&
                  (spartan.rightControllerOwnedSourceBranch &
                   ~spartan.rightHandSourceDescendants) ==
                      kReachRightControllerOwnedAuxiliarySourceMask &&
                  (spartan.leftHandSourceDescendants &
                   kReachLeftControllerOwnedAuxiliarySourceMask) == 0 &&
                  (spartan.rightHandSourceDescendants &
                   kReachRightControllerOwnedAuxiliarySourceMask) == 0 &&
                  (spartan.leftControllerOwnedSourceBranch &
                   spartan.rightHandSourceDescendants) == 0 &&
                  (spartan.leftControllerOwnedSourceBranch &
                   spartan.rightControllerOwnedSourceBranch) == 0 &&
                  (spartan.leftControllerOwnedSourceBranch >>
                   spartan.paletteBodyNodeCount) == 0 &&
                  (spartan.rightControllerOwnedSourceBranch >>
                   spartan.paletteBodyNodeCount) == 0,
            "Reach Spartan controllers own the exact hidden skin-influence closures");
        auto brokenSpartanOwnership = spartan;
        brokenSpartanOwnership.leftControllerOwnedSourceBranch ^=
            uint64_t{1} << 12;
        Check(!brokenSpartanOwnership.Valid(),
            "Reach Spartan rejects an incomplete left controller influence branch");
        auto brokenSpartanRightOwnership = spartan;
        brokenSpartanRightOwnership.rightControllerOwnedSourceBranch ^=
            uint64_t{1} << 14;
        Check(!brokenSpartanRightOwnership.Valid(),
            "Reach Spartan rejects an incomplete right controller influence branch");
        Check((spartan.leftHandSourceDescendants &
                   (uint64_t{1} << spartan.leftWristSource)) != 0 &&
                  (spartan.rightHandSourceDescendants &
                   (uint64_t{1} << spartan.rightWristSource)) != 0 &&
                  (spartan.leftHandSourceDescendants &
                   spartan.rightHandSourceDescendants) == 0 &&
                  (spartan.leftHandSourceDescendants >>
                   spartan.paletteBodyNodeCount) == 0,
            "Reach Spartan floating hands are disjoint exact wrist subtrees and exclude held objects");
    }

    {
        const std::span<const int32_t> spartanMap{
            kReachSpartanFpBodyBoneMap};
        ReachFpBodyLayout spartan{};
        Check(ResolveReachFpBodyLayout(spartanMap, 65, spartan) &&
                  spartan.leftHandPaletteDescendants ==
                      kReachSpartanLeftHandPaletteMask &&
                  spartan.rightHandPaletteDescendants ==
                      kReachSpartanRightHandPaletteMask &&
                  !ReachFpSourceIndexIsHeldObject(spartan, -1) &&
                  !ReachFpSourceIndexIsHeldObject(spartan, 46) &&
                  ReachFpSourceIndexIsHeldObject(spartan, 47) &&
                  ReachFpSourceIndexIsHeldObject(spartan, 64) &&
                  !ReachFpSourceIndexIsHeldObject(spartan, 65),
            "Reach Spartan held-object range covers appended indices 47 through 64");
        ReachFpBodyLayout maximumSource{};
        Check(ResolveReachFpBodyLayout(
                  spartanMap, kReachFpMaxSourceNodeCount, maximumSource) &&
                  maximumSource.paletteBodyNodeCount == 47 &&
                  maximumSource.liveSourceNodeCount == 120 &&
                  ReachFpSourceIndexIsHeldObject(maximumSource, 119),
            "Reach body resolution accepts the bounded 120-node source span");

        const std::span<const int32_t> eliteMap{kReachEliteFpBodyBoneMap};
        ReachFpBodyLayout elite{};
        Check(ResolveReachFpBodyLayout(eliteMap, 59, elite) &&
                  elite.Valid() && elite.kind == ReachFpBodyKind::Elite &&
                  elite.paletteBodyNodeCount == 41 &&
                  elite.liveSourceNodeCount == 59 &&
                  elite.rightShoulderSource == 6 &&
                  elite.rightElbowSource == 9 &&
                  elite.rightWristSource == 13 &&
                  elite.leftShoulderSource == 5 &&
                  elite.leftElbowSource == 7 &&
                  elite.leftWristSource == 11 &&
                  elite.cameraControlSource == 4,
            "Reach Elite resolves its 41-node body over the shared arm prefix");
        Check(elite.leftHandPaletteDescendants ==
                      kReachEliteLeftHandPaletteMask &&
                  elite.rightHandPaletteDescendants ==
                      kReachEliteRightHandPaletteMask &&
                  elite.leftHandSourceDescendants ==
                      0x0000001E1E0F8800ull &&
                  elite.rightHandSourceDescendants ==
                      0x000001E1E1F02000ull &&
                  !ReachFpSourceIndexIsHeldObject(elite, 40) &&
                  ReachFpSourceIndexIsHeldObject(elite, 41) &&
                  ReachFpSourceIndexIsHeldObject(elite, 58) &&
                  !ReachFpSourceIndexIsHeldObject(elite, 59),
            "Reach Elite maps exact hand sets and appended held-object range");
        Check(elite.leftControllerOwnedSourceBranch ==
                      0x0000001E1E0F99A0ull &&
                  elite.rightControllerOwnedSourceBranch ==
                      0x000001E1E1F06640ull &&
                  (elite.leftControllerOwnedSourceBranch ^
                   elite.leftHandSourceDescendants) ==
                      kReachLeftControllerOwnedAuxiliarySourceMask &&
                  (elite.rightControllerOwnedSourceBranch ^
                   elite.rightHandSourceDescendants) ==
                      kReachRightControllerOwnedAuxiliarySourceMask &&
                  (elite.leftControllerOwnedSourceBranch &
                   ~elite.leftHandSourceDescendants) ==
                      kReachLeftControllerOwnedAuxiliarySourceMask &&
                  (elite.rightControllerOwnedSourceBranch &
                   ~elite.rightHandSourceDescendants) ==
                      kReachRightControllerOwnedAuxiliarySourceMask &&
                  (elite.leftHandSourceDescendants &
                   kReachLeftControllerOwnedAuxiliarySourceMask) == 0 &&
                  (elite.rightHandSourceDescendants &
                   kReachRightControllerOwnedAuxiliarySourceMask) == 0 &&
                  (elite.leftControllerOwnedSourceBranch &
                   elite.rightHandSourceDescendants) == 0 &&
                  (elite.leftControllerOwnedSourceBranch &
                   elite.rightControllerOwnedSourceBranch) == 0 &&
                  (elite.leftControllerOwnedSourceBranch >>
                   elite.paletteBodyNodeCount) == 0 &&
                  (elite.rightControllerOwnedSourceBranch >>
                   elite.paletteBodyNodeCount) == 0,
            "Reach Elite controllers own the exact shared hidden arm branches");
        Check((elite.leftHandSourceDescendants &
                   (uint64_t{1} << elite.leftWristSource)) != 0 &&
                  (elite.rightHandSourceDescendants &
                   (uint64_t{1} << elite.rightWristSource)) != 0 &&
                  (elite.leftHandSourceDescendants &
                   elite.rightHandSourceDescendants) == 0 &&
                  (elite.leftHandSourceDescendants >>
                   elite.paletteBodyNodeCount) == 0,
            "Reach Elite floating hands are disjoint exact wrist subtrees and exclude held objects");
    }

    {
        const std::span<const int32_t> spartanMap{
            kReachSpartanFpBodyBoneMap};
        auto wrongFingerprint = kReachSpartanFpBodyBoneMap;
        const int32_t first = wrongFingerprint[0];
        wrongFingerprint[0] = wrongFingerprint[1];
        wrongFingerprint[1] = first;
        ReachFpBodyLayout rejected{};
        Check(!ResolveReachFpBodyLayout(
                  std::span<const int32_t>{wrongFingerprint}, 65, rejected) &&
                  !rejected.Valid() &&
                  rejected.kind == ReachFpBodyKind::None &&
                  rejected.paletteBodyNodeCount == 0 &&
                  rejected.liveSourceNodeCount == 0,
            "Reach rejects a full permutation with the wrong exact fingerprint");

        auto duplicate = kReachSpartanFpBodyBoneMap;
        duplicate[1] = duplicate[0];
        auto negative = kReachSpartanFpBodyBoneMap;
        negative[0] = -1;
        auto appendedInBody = kReachSpartanFpBodyBoneMap;
        appendedInBody[0] = 47;
        Check(!ResolveReachFpBodyLayout(
                  std::span<const int32_t>{duplicate}, 65, rejected) &&
                  !ResolveReachFpBodyLayout(
                      std::span<const int32_t>{negative}, 65, rejected) &&
                  !ResolveReachFpBodyLayout(
                      std::span<const int32_t>{appendedInBody}, 65,
                      rejected),
            "Reach rejects duplicate, negative, and appended body-map indices");

        ReachFpBodyLayout accepted{};
        Check(ResolveReachFpBodyLayout(spartanMap, 65, accepted),
            "Reach prepares a valid layout before fail-closed reset testing");
        rejected = accepted;
        Check(!ResolveReachFpBodyLayout(
                  spartanMap.first(46), 65, rejected) &&
                  !rejected.Valid() &&
                  rejected.rightShoulderSource == -1 &&
                  rejected.rightHandSourceDescendants == 0,
            "Reach rejects a wrong palette span and clears stale output");
        Check(!ResolveReachFpBodyLayout(spartanMap, 46, rejected) &&
                  !ResolveReachFpBodyLayout(
                      spartanMap, kReachFpMaxSourceNodeCount + 1, rejected) &&
                  !ResolveReachFpBodyLayout(
                      std::span<const int32_t>{}, 65, rejected) &&
                  !ResolveReachFpBodyLayout(
                      std::span<const int32_t>{kReachEliteFpBodyBoneMap},
                      40, rejected),
            "Reach rejects unsafe live source counts and empty maps");
    }

    {
        using LegKind = ReachFpLegPaletteKind;
        Check(
            ClassifyReachFpLegPalette(
                kReachSpartanFpLegRuntimeImportChecksum,
                kReachSpartanFpLegNodeCount) == LegKind::Spartan &&
            ClassifyReachFpLegPalette(
                kReachEliteFpLegRuntimeImportChecksum,
                kReachEliteFpLegNodeCount) == LegKind::Elite,
            "Reach admits the exact HREK Spartan and Elite first-person leg identities");

        // Counts collide with the ordinary world render models, so their
        // distinct HREK checksums must remain on the collapse path.
        Check(
            ClassifyReachFpLegPalette(0x171B0502u, 82) == LegKind::None &&
            ClassifyReachFpLegPalette(0x171C1D0Fu, 67) == LegKind::None,
            "Reach rejects same-count world Spartan and Elite render models as legs");
        Check(
            ClassifyReachFpLegPalette(
                kReachSpartanFpLegRuntimeImportChecksum, 81) ==
                    LegKind::None &&
            ClassifyReachFpLegPalette(
                kReachEliteFpLegRuntimeImportChecksum, 68) ==
                    LegKind::None &&
            ClassifyReachFpLegPalette(
                kReachSpartanFpLegRuntimeImportChecksum + 1, 82) ==
                    LegKind::None &&
            ClassifyReachFpLegPalette(0, 0) == LegKind::None,
            "Reach leg identity rejects altered and empty observations");

        constexpr uint16_t kLegTag = 0x3902;
        constexpr uint16_t kArmsTag = 0x3901;
        Check(
            !ReachFpShouldCollapseVisiblePalette(
                true, LegKind::Spartan, kLegTag, kLegTag) &&
            ReachFpLegNodeCountMatchesKind(
                LegKind::Spartan,kReachSpartanFpLegNodeCount) &&
            ReachFpShouldCollapseVisiblePalette(
                true, LegKind::Spartan, kLegTag, kArmsTag) &&
            !ReachFpLegNodeCountMatchesKind(
                LegKind::Spartan,kReachEliteFpLegNodeCount),
            "Reach preserves only the exact leg palette while exact arms still collapse");
        Check(
            ReachFpShouldCollapseVisiblePalette(
                false, LegKind::Spartan, kLegTag, kLegTag) &&
            ReachFpShouldCollapseVisiblePalette(
                true, LegKind::None, kLegTag, kLegTag) &&
            ReachFpShouldCollapseVisiblePalette(
                true, LegKind::Elite, 0x4702, kLegTag),
            "Reach collapses when legs are disabled, unproven, or tag-mismatched");
        Check(
            !ReachFpLegObservationCanValidate(
                true, 9, 100, 9, 100) &&
            ReachFpLegObservationCanValidate(
                true, 9, 100, 9, 101) &&
            !ReachFpLegObservationCanValidate(
                true, 9, 100, 10, 101) &&
            !ReachFpLegObservationCanValidate(
                false, 9, 100, 9, 101) &&
            !ReachFpLegObservationCanValidate(
                true, 9, 0, 9, 101),
            "Reach verifies a leg observation only on a later pair in the same title generation");
    }

    {
        constexpr uint32_t kGeneration = 7;
        constexpr uint64_t kDiscoverySerial = 100;
        const auto decide = [](uint32_t learnedGeneration,
                                uint64_t learnedSerial,
                                uint32_t pairGeneration,
                                uint64_t pairSerial,
                                bool valid = true,
                                bool invalidated = false) {
            return DecideReachFpPairLayout(
                valid, invalidated, learnedGeneration, learnedSerial,
                pairGeneration, pairSerial);
        };

        Check(decide(
                  kGeneration, kDiscoverySerial, kGeneration,
                  kDiscoverySerial) == ReachFpPairLayoutDecision::Stock,
            "Reach keeps the discovery pair stock");
        Check(decide(
                  kGeneration, kDiscoverySerial, kGeneration,
                  kDiscoverySerial + 1) == ReachFpPairLayoutDecision::Active,
            "Reach activates a learned layout on a later prepared serial");
        Check(decide(
                  kGeneration, kDiscoverySerial, kGeneration,
                  kDiscoverySerial - 1) == ReachFpPairLayoutDecision::Stock &&
                  decide(
                      kGeneration, kDiscoverySerial, kGeneration,
                      kDiscoverySerial) == ReachFpPairLayoutDecision::Stock,
            "Reach rejects stale and same-serial layout activation");
        Check(decide(
                  kGeneration, kDiscoverySerial, kGeneration + 1,
                  kDiscoverySerial + 1) == ReachFpPairLayoutDecision::Stock &&
                  decide(
                      0, kDiscoverySerial, 0, kDiscoverySerial + 1) ==
                      ReachFpPairLayoutDecision::Stock,
            "Reach rejects generation mismatch and zero-generation replay");
        Check(decide(
                  kGeneration, kDiscoverySerial, kGeneration,
                  kDiscoverySerial + 1, true, true) ==
                  ReachFpPairLayoutDecision::Stock &&
                  decide(
                      kGeneration, kDiscoverySerial, kGeneration,
                      kDiscoverySerial + 1, false, false) ==
                      ReachFpPairLayoutDecision::Stock &&
                  decide(
                      kGeneration, 0, kGeneration, 1) ==
                      ReachFpPairLayoutDecision::Stock &&
                  decide(
                      kGeneration, kDiscoverySerial, kGeneration, 0) ==
                      ReachFpPairLayoutDecision::Stock,
            "Reach invalidation, missing learning, and zero serials remain stock");

        const auto evaluateEyeOrder = [&](const std::array<int, 2>& eyeOrder) {
            std::array<ReachFpPairLayoutDecision, 2> eyeDecisions{
                ReachFpPairLayoutDecision::Stock,
                ReachFpPairLayoutDecision::Stock};
            for (int eye : eyeOrder)
            {
                eyeDecisions[static_cast<size_t>(eye)] = decide(
                    kGeneration, kDiscoverySerial, kGeneration,
                    kDiscoverySerial + 1);
            }
            return eyeDecisions;
        };
        const auto leftFirst = evaluateEyeOrder({0, 1});
        const auto rightFirst = evaluateEyeOrder({1, 0});
        Check(leftFirst == rightFirst &&
                  leftFirst[0] == ReachFpPairLayoutDecision::Active &&
                  leftFirst[1] == ReachFpPairLayoutDecision::Active,
            "Reach pair activation is independent of stereo eye order");
    }

    {
        using Action = ReachFpPaletteAction;
        const auto decide = [](bool current, bool frozen, bool transformed,
                               bool exactBody, bool exactMatch,
                               bool learnedTag) {
            return DecideReachFpPaletteAction(
                current, frozen, transformed, exactBody, exactMatch,
                learnedTag);
        };
        Check(decide(true, false, false, true, false, false) ==
                  Action::LearnStockOnly,
            "Reach first exact body discovery is stock-only");
        const Action precedingWeapon =
            decide(true, true, true, false, false, false);
        const Action body = decide(true, true, true, true, true, true);
        const Action followingWeapon =
            decide(true, true, true, false, false, false);
        Check(precedingWeapon == Action::ArticulateKnownTransaction &&
                  body == Action::ArticulateKnownTransaction &&
                  followingWeapon == Action::ArticulateKnownTransaction,
            "Reach reconstructs every final palette in a known source transaction");
        Check(decide(true, true, true, true, false, true) ==
                  Action::RestoreStockAndInvalidate &&
                  decide(true, true, true, true, false, false) ==
                  Action::RestoreStockAndInvalidate &&
                  decide(true, true, false, false, false, true) ==
                  Action::RestoreStockAndInvalidate,
            "Reach changed maps, body-tag transitions, and invalid known-body maps restore stock");
        Check(decide(true, true, false, false, false, false) ==
                  Action::PassThroughLive &&
                  decide(false, true, true, true, true, true) ==
                  Action::PassThroughLive,
            "Reach unsolved and stale transactions remain stock");
    }

    {
        constexpr size_t kPlasmaLauncherNodes = 65;
        constexpr size_t kFloats =
            kPlasmaLauncherNodes * kReachFpBoneMatrixFloatCount;
        std::array<float, kFloats> stock{};
        for (size_t node = 0; node < kPlasmaLauncherNodes; ++node)
            stock[node * kReachFpBoneMatrixFloatCount] = 1.0f;
        auto candidate = stock;
        candidate[12] = 3.5f;
        candidate[kReachFpBoneMatrixFloatCount + 10] = -2.0f;
        auto live = stock;
        bool commitCalled = false;
        const bool committed = ReachFpCommitGraphIfFinite(
            std::span<const float>{candidate}, kPlasmaLauncherNodes, [&]() {
                commitCalled = true;
                live = candidate;
                return true;
            });
        Check(committed && commitCalled && live == candidate &&
                  ReachFpPackedGraphFinite(
                      std::span<const float>{live}, kPlasmaLauncherNodes),
            "Reach validates and atomically commits a real 65-node live graph");

        const auto committedLive = live;
        auto nanCandidate = candidate;
        nanCandidate[3 * kReachFpBoneMatrixFloatCount + 6] =
            std::numeric_limits<float>::quiet_NaN();
        commitCalled = false;
        Check(!ReachFpCommitGraphIfFinite(
                  std::span<const float>{nanCandidate},
                  kPlasmaLauncherNodes, [&]() {
                      commitCalled = true;
                      live = nanCandidate;
                      return true;
                  }) &&
                  !commitCalled && live == committedLive,
            "Reach NaN rejection leaves the stock/live graph byte-identical");

        auto invalidRoot = candidate;
        invalidRoot[0] = 0.0f;
        commitCalled = false;
        Check(!ReachFpCommitGraphIfFinite(
                  std::span<const float>{invalidRoot},
                  kPlasmaLauncherNodes, [&]() {
                      commitCalled = true;
                      return true;
                  }) &&
                  !commitCalled &&
                  !ReachFpPackedGraphFinite(
                      std::span<const float>{candidate}.first(kFloats - 1),
                      kPlasmaLauncherNodes) &&
                  !ReachFpPackedGraphFinite(
                      std::span<const float>{candidate},
                      kReachFpMaxSourceNodeCount + 1),
            "Reach invalid roots, malformed spans, and over-120 graphs never commit");
    }

    {
        static_assert(kReachMainRenderViewAob.size() == 32);
        static_assert(kReachPlayerViewRenderAob.size() == 69);
        static_assert(kReachCameraStackCallbackAob.size() == 28);
        static_assert(kReachFrustumHelperAob.size() == 25);
        Check(std::string_view(kReachMainRenderViewBodySha256) ==
                  "95DF3EFFF9AC6EE29887D1272CCA8D7BF3E58F87041BAD8032107825B733FE89" &&
              std::string_view(kReachPlayerViewRenderBodySha256) ==
                  "2628D1189621EACED7C95A1F295815D70E7783054F1C3CBA46799F838CC33C60" &&
              std::string_view(kReachCameraStackCallbackBodySha256) ==
                  "6E2A249710A53498ADE7AFB12EE7414099D16315B2F06D90D8EC01D185E6B0C4" &&
              kReachMainRenderViewBodySize == 515 &&
              kReachPlayerViewRenderBodySize == 2314 &&
              kReachCameraStackCallbackBodySize == 0x6C,
            "Reach render candidate pins the renderer and outer-camera callback identities");

        constexpr uint32_t vehicleGeneration = 37;
        constexpr uint64_t onFootSnapshot = ReachVehicleInputSnapshot(
            vehicleGeneration, ReachVehicleInputState::OnFoot);
        constexpr uint64_t vehicleSnapshot = ReachVehicleInputSnapshot(
            vehicleGeneration, ReachVehicleInputState::Vehicle);
        Check(kReachPlayerUnitByOutputUserRva == 0x00053EF8 &&
              kReachUnitInVehicleRva == 0x004F9368 &&
              kReachUnitInVehicleEvaluatorRva == 0x0019EF28 &&
              kReachUnitInVehicleEvaluatorCallRva == 0x0019EF5E &&
              kReachUnitInVehicleNameRva == 0x009FB710 &&
              kReachUnitInVehicleDescriptorRva == 0x00A22B60 &&
              kReachEngineTlsIndexRva == 0x00C17B18 &&
              !ReachVehicleInputSnapshotIsVehicle(
                  onFootSnapshot, vehicleGeneration) &&
              ReachVehicleInputSnapshotIsVehicle(
                  vehicleSnapshot, vehicleGeneration) &&
              !ReachVehicleInputSnapshotIsVehicle(
                  vehicleSnapshot, vehicleGeneration + 1) &&
              !ReachVehicleInputSnapshotIsVehicle(vehicleSnapshot, 0),
            "Reach vehicle input pins the HREK-matched retail identities and rejects stale generations");

        Check(kReachUnitGetCameraInfoRva == 0x0048A4B4 &&
              kReachUnitGetCameraInfoBodySize == 0x45A &&
              kReachObjectMarkerResolverRva == 0x0047044C &&
              kReachObjectMarkerResolverBodySize == 0x2DB &&
              kReachObjectUltimateParentRva == 0x00473DC0 &&
              kReachObjectUltimateParentBodySize == 0x3F &&
              kReachVehicleTypeAccessorRva == 0x004AC1E4 &&
              kReachVehicleTypeAccessorBodySize == 0x51 &&
              kReachTagGetRva == 0x00031AE8 &&
              kReachTagGetBodySize == 0x7B,
            "Reach native seat-camera, marker, carrier, type and tag bindings are pinned");
        // R-V25: the trim bank carries one row MORE than the identity list -
        // the unmatched row every unresolved vehicle keys instead of the
        // shared universal trim.
        // Halo 3 seat flags, from real values recorded during ODST O5: the
        // Warthog passenger's 0x1070 fires a personal weapon, the driver's
        // 0x40014 does not. That single bit is what decides whether a seated
        // shot gets re-origined onto the engine's own eye.
        Check(kHalo3SeatAllowsWeaponsBit == (1u << 5) &&
              kHalo3SeatThirdPersonCameraBit == (1u << 4) &&
              Halo3SeatFiresPersonalWeapon(0x1070u) &&
              !Halo3SeatFiresPersonalWeapon(0x40014u) &&
              !Halo3SeatFiresPersonalWeapon(0u) &&
              Halo3SeatFlagsLookLikePlayerSeat(0x1070u) &&
              Halo3FirstPersonSeatFlags(0x1070u) == 0x1060u &&
              Halo3SeatFiresPersonalWeapon(Halo3FirstPersonSeatFlags(0x1070u)),
            "Halo 3 allows-weapons is bit 5 and survives clearing the third-person bit");
        // Turret seats, both titles. A mounted gunner is a turret whatever it
        // hangs off; the walk-up emplacements and the shade are turrets on
        // their own identity. Nothing else may enter the servo.
        Check(Halo3SeatIsTurret(Halo3VehicleId::StationaryTurret, false) &&
              Halo3SeatIsTurret(Halo3VehicleId::Warthog, true) &&
              !Halo3SeatIsTurret(Halo3VehicleId::Warthog, false) &&
              !Halo3SeatIsTurret(Halo3VehicleId::Unknown, false) &&
              OdstSeatIsTurret(OdstVehicleId::StationaryTurret, false) &&
              OdstSeatIsTurret(OdstVehicleId::Shade, false) &&
              OdstSeatIsTurret(OdstVehicleId::Warthog, true) &&
              !OdstSeatIsTurret(OdstVehicleId::Warthog, false) &&
              !OdstSeatIsTurret(OdstVehicleId::Unknown, false),
            "only turret seats take the aim servo");
        {
            // The plant the loop actually drives, reproduced from measured
            // facts: MCC receives our stick through ToRawStick, whose 9000/32767
            // floor means the engine only ever hears "stop" or "at least 27.5%
            // deflection", and the aim then moves at the look rate, capped by
            // the seat's authored rate bounds (H3EK: warthog_g has NO pitch
            // cap, shade_d is 15 deg/s).
            const auto step = [](float aim, float command, float lookRateRad,
                                 float capRad, float dt) {
                float deflection = 0.0f;
                if (std::fabs(command) >= 1.0e-3f)
                {
                    deflection = std::fabs(command) < 0.2747f
                        ? 0.2747f : std::fabs(command);
                    if (deflection > 1.0f) deflection = 1.0f;
                    if (command < 0.0f) deflection = -deflection;
                }
                float rate = deflection * lookRateRad;
                if (capRad > 0.0f)
                {
                    if (rate > capRad) rate = capRad;
                    if (rate < -capRad) rate = -capRad;
                }
                return aim + rate * dt;
            };
            const float dt = 1.0f / 120.0f;
            const float target = 0.30f;
            // Uncapped pitch (warthog_g) at a mid look rate. The proportional
            // form cannot rest, so it hunts forever; the servo parks.
            // Peak-to-peak of the settled aim, and how far it settled from the
            // hand. Both matter: parking anywhere is easy, parking ON the
            // target is the requirement.
            const auto settle = [&](bool useServo, float lookRateRad,
                                    float capRad, float* outFinalError) {
                AimServoAxis axis{};
                float aim = 0.0f;
                float commanded = 0.0f;
                float lo = 1.0e9f, hi = -1.0e9f;
                for (int frame = 0; frame < 900; ++frame)
                {
                    const float error = target - aim;
                    AimServoObserve(axis, aim, commanded);
                    float command;
                    if (useServo)
                    {
                        command = AimServoCommand(
                            axis, error, 12.0f, kAimServoRestEnterRadians,
                            kAimServoRestExitRadians);
                    }
                    else
                    {
                        command = error * 12.0f;
                        if (command > 1.0f) command = 1.0f;
                        if (command < -1.0f) command = -1.0f;
                    }
                    commanded = std::fabs(command);
                    aim = step(aim, command, lookRateRad, capRad, dt);
                    if (frame >= 700)
                    {
                        if (aim < lo) lo = aim;
                        if (aim > hi) hi = aim;
                    }
                }
                if (outFinalError)
                    *outFinalError = std::fabs(target - aim);
                return hi - lo;
            };
            // 3 rad/s ~ 172 deg/s, a plain uncapped look rate. Measured: the
            // proportional loop never rests, cycling 0.0069 rad (0.39 deg)
            // peak to peak forever, while the servo parks dead still.
            const float stockUncapped = settle(false, 3.0f, 0.0f, nullptr);
            const float servoUncapped = settle(true, 3.0f, 0.0f, nullptr);
            Check(stockUncapped > 0.004f && servoUncapped < 0.001f,
                "uncapped pitch: the proportional loop limit-cycles, the servo parks");
            // The same actuator under the warthog turret's 60 deg/s YAW cap.
            // The cap binds only once the look rate would exceed it, and then
            // it shrinks the quantum and so the chatter -- which is exactly
            // why the user sees the wiggle up and down and not side to side.
            const float stockFast = settle(false, 6.0f, 0.0f, nullptr);
            const float stockFastCapped = settle(false, 6.0f, 1.047f, nullptr);
            Check(stockFastCapped < stockFast,
                "an authored rate cap shrinks the same chatter on yaw");
            // Robust across look sensitivity: the rest band is set by the
            // quantum the servo measures, so no sensitivity re-opens the
            // cycle. The parked offset is bounded by the actuator's OWN
            // resolution - a stick that can only move the aim in steps of S
            // cannot be parked closer than S/2 in the worst case, and no
            // controller can beat that. Worst case here is ~0.95 deg at the
            // top sensitivity, which the barrel-riding reticle then reports
            // honestly rather than hiding.
            bool servoStableEverywhere = true;
            for (float lookRate = 1.0f; lookRate <= 12.0f; lookRate += 0.5f)
            {
                const float quantum = kAimServoStickFloor * lookRate * dt;
                const float bound = 1.05f *
                    (kAimServoRestEnterRadians > 0.6f * quantum
                         ? kAimServoRestEnterRadians : 0.6f * quantum);
                float finalError = 0.0f;
                if (settle(true, lookRate, 0.0f, &finalError) >= 0.001f ||
                    finalError > bound)
                {
                    servoStableEverywhere = false;
                }
            }
            Check(servoStableEverywhere,
                "the servo parks within the actuator's own resolution at every sensitivity");
            // A real hand move must break the park and be followed.
            AimServoAxis axis{};
            float aim = 0.0f;
            float commanded = 0.0f;
            const float moved = target + 0.60f;
            bool retook = false;
            for (int frame = 0; frame < 1800; ++frame)
            {
                const float goal = frame < 900 ? target : moved;
                const float error = goal - aim;
                AimServoObserve(axis, aim, commanded);
                const float command = AimServoCommand(
                    axis, error, 12.0f, kAimServoRestEnterRadians,
                    kAimServoRestExitRadians);
                commanded = std::fabs(command);
                if (frame > 900 && commanded != 0.0f)
                    retook = true;
                aim = step(aim, command, 3.0f, 0.0f, dt);
            }
            Check(retook && std::fabs(moved - aim) < 0.012f,
                "a real hand move breaks the park and the gun follows it");
            // A 15 deg/s shade cannot be made to follow a hand flick by any
            // gain, which is why its reticle must ride the barrel instead.
            static_assert(kAimServoTurretReticleRidesBarrel,
                          "a turret's reticle reports the gun, not the hand");
        }
        Check(kReachVehicleIdentityCount == kReachVehicleIdentityTrimCount &&
              kReachVehicleTrimCount == kReachVehicleIdentityCount + 1 &&
              kReachUnmatchedVehicleTrimId == kReachVehicleTrimCount &&
              ReachVehicleSeatIsPlayer(ReachVehicleId::Falcon, 0) &&
              ReachVehicleSeatIsPlayer(ReachVehicleId::Falcon, 3) &&
              ReachVehicleSeatIsPlayer(ReachVehicleId::Falcon, 4) &&
              !ReachVehicleSeatIsPlayer(ReachVehicleId::Falcon, 1) &&
              ReachVehicleSeatIsPlayer(ReachVehicleId::Warthog, 1) &&
              !ReachVehicleSeatIsPlayer(ReachVehicleId::Scorpion, 1) &&
              ReachVehicleIsAircraft(ReachVehicleId::SpaceBanshee) &&
              ReachVehicleIsWalkUpTurret(ReachVehicleId::Machinegun) &&
              ReachVehicleIsAttachedWeapon(ReachVehicleId::WarthogGauss),
            "Reach HREK player-seat census and vehicle families stay explicit");
        // The 2026-08-04 full-census identities: the Pelican flies with four
        // passenger benches and no player driver, the Onager and corvette gun
        // are walk-up emplacements, the Scorpion's secondary MG is an
        // attached child gun, and the civilian line steers like the warthog.
        Check(ReachVehicleSeatIsPlayer(ReachVehicleId::Pelican, 4) &&
              ReachVehicleSeatIsPlayer(ReachVehicleId::Pelican, 10) &&
              !ReachVehicleSeatIsPlayer(ReachVehicleId::Pelican, 0) &&
              ReachVehicleSeatIsPlayer(ReachVehicleId::SquadDropPod, 3) &&
              ReachVehicleSeatIsPlayer(ReachVehicleId::CargoTruck, 1) &&
              ReachVehicleIsAircraft(ReachVehicleId::Seraph) &&
              ReachVehicleIsAircraft(ReachVehicleId::Pelican) &&
              ReachVehicleIsWalkUpTurret(ReachVehicleId::MacCannon) &&
              ReachVehicleIsWalkUpTurret(ReachVehicleId::CorvetteCannon) &&
              ReachVehicleIsAttachedWeapon(
                  ReachVehicleId::ScorpionAntiInfantry) &&
              ReachVehicleIsAttachedWeapon(
                  ReachVehicleId::SpacePhantomBeamTurret) &&
              ReachVehicleIsLookSteered(ReachVehicleId::OniVan) &&
              ReachVehicleUsesWheel(ReachVehicleId::Pickup) &&
              !ReachVehicleSeatIsDriver(ReachVehicleId::Pelican, 4) &&
              ReachVehicleExpectedPhysicsType(ReachVehicleId::Pelican) == 2 &&
              ReachVehicleExpectedPhysicsType(
                  ReachVehicleId::CorvetteCannon) == 6 &&
              ReachVehicleExpectedPhysicsType(ReachVehicleId::Seraph) == 13,
            "The census identities carry their HREK-derived policy families");
        bool reachFingerprintsUnique = true;
        for (const auto& entry : kReachVehicleFingerprints)
        {
            reachFingerprintsUnique = reachFingerprintsUnique &&
                ReachResolveVehicleFingerprint(entry.fingerprint) ==
                    entry.identity;
        }
        // The player-seat census counts each IDENTITY once; the fingerprint
        // table is larger than the identity list because retail maps carry
        // eight explicit full-tuple aliases for HREK import-rounding variants.
        int reachPlayerSeatCount = 0;
        for (int id = 1; id <= kReachVehicleIdentityCount; ++id)
            for (int seat = 0; seat < kReachVehicleSeatLimit; ++seat)
                if (ReachVehicleSeatIsPlayer(
                        static_cast<ReachVehicleId>(id), seat))
                    ++reachPlayerSeatCount;
        ReachVehicleFingerprint alteredReachFingerprint =
            kReachVehicleFingerprints[0].fingerprint;
        alteredReachFingerprint.offsetZ ^= 1u;
        Check(reachFingerprintsUnique && reachPlayerSeatCount == 50 &&
              ReachResolveVehicleFingerprint(alteredReachFingerprint) ==
                  ReachVehicleId::Unknown,
            "Reach HREK fingerprints resolve 34 identities/50 player seats and fail closed on mutation");
        // Retail aliases stay exact full tuples: six rows complete the
        // accepted-partial 619644c miss census beside R-V17's first two.
        Check(kReachVehicleFingerprintCount == kReachVehicleIdentityCount + 8 &&
              ReachResolveVehicleFingerprint(
                  {0x3F978071, 0xBD406A82, 0xBAD03632, 0x3EC25783}) ==
                  ReachVehicleId::Warthog &&
              ReachResolveVehicleFingerprint(
                  {0x3F2ABABB, 0x39815C02, 0x39CE6FFD, 0x3E7776E0}) ==
                  ReachVehicleId::Mongoose &&
              ReachResolveVehicleFingerprint(
                  {0x3F978072, 0xBD406A82, 0xBAD03632, 0x3EC25783}) ==
                  ReachVehicleId::Warthog &&
              ReachResolveVehicleFingerprint(
                  {0x3F73045A, 0x3DD8D24E, 0xBC93505D, 0x3F1EA4E9}) ==
                  ReachVehicleId::ShadeFlak &&
              ReachResolveVehicleFingerprint(
                  {0x3F7A9153, 0x3E1B9697, 0xBC935062, 0x3F1EF5FC}) ==
                  ReachVehicleId::ShadePlasma &&
              ReachResolveVehicleFingerprint(
                  {0x400D30E2, 0x3D70D852, 0x3D28430B, 0x3EBBF788}) ==
                  ReachVehicleId::Scorpion &&
              ReachResolveVehicleFingerprint(
                  {0x3EE99B18, 0x3D9811F2, 0xB991445F, 0x3E8589DB}) ==
                  ReachVehicleId::Machinegun &&
              ReachResolveVehicleFingerprint(
                  {0x40A69CFE, 0xBF19A963, 0xBB2A0198, 0x3F8AF1EF}) ==
                  ReachVehicleId::Sabre &&
              ReachResolveVehicleFingerprint(
                  {0x3F786DCE, 0x3C8C975D, 0xBC8111A1, 0x3EE5C245}) ==
                  ReachVehicleId::Cart,
            "Eight exact retail fingerprint aliases resolve to their HREK identities");
        const ReachVehicleFingerprint shadePlasmaHrek{
            0x3F7A9153, 0x3E1B9697, 0xBC935063, 0x3F1EF5FC};
        const ReachVehicleFingerprint shadePlasmaRetail{
            0x3F7A9153, 0x3E1B9697, 0xBC935062, 0x3F1EF5FC};
        const ReachVehicleFingerprint shadeFlakHrek{
            0x3F73045B, 0x3DD8D24F, 0xBC93505D, 0x3F1EA4E9};
        const ReachVehicleFingerprint shadeFlakRetail{
            0x3F73045A, 0x3DD8D24E, 0xBC93505D, 0x3F1EA4E9};
        const ReachVehicleFingerprint machinegunHrek{
            0x3EE99B18, 0x3D9811F2, 0xB9914460, 0x3E8589DB};
        const ReachVehicleFingerprint machinegunRetail{
            0x3EE99B18, 0x3D9811F2, 0xB991445F, 0x3E8589DB};
        const ReachVehicleFingerprint plasmaTurret{
            0x3F000000, 0x00000000, 0x00000000, 0x3DCCCCCD};
        const ReachVehicleFingerprint scorpionAntiInfantry{
            0x3ED28405, 0x3DB8AD5D, 0xB8A6B78A, 0x3D8A1BD8};
        const ReachVehicleFingerprint corvetteCannon{
            0x40400000, 0x00000000, 0x00000000, 0x00000000};
        const ReachVehicleFingerprint warthogChaingun{
            0x3F1CC397, 0x3E86AFD5, 0xB3A6AFD5, 0x3E9C5885};
        const ReachVehicleFingerprint scorpionHrek{
            0x400D30E2, 0x3D70D852, 0x3D28430C, 0x3EBBF788};
        const ReachVehicleFingerprint scorpionRetail{
            0x400D30E2, 0x3D70D852, 0x3D28430B, 0x3EBBF788};
        const ReachVehicleFingerprint warthogRetail{
            0x3F978071, 0xBD406A82, 0xBAD03632, 0x3EC25783};
        Check(ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadePlasma, shadePlasmaHrek, 16) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadePlasma, shadePlasmaRetail, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadePlasma, shadePlasmaHrek, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadePlasma, shadePlasmaRetail, 16) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadeFlak, shadeFlakHrek, 16) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadeFlak, shadeFlakRetail, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadeFlak, shadeFlakHrek, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadeFlak, shadeFlakRetail, 16) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Machinegun, machinegunHrek, 16) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Machinegun, machinegunRetail, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Machinegun, machinegunHrek, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Machinegun, machinegunRetail, 16) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::PlasmaTurret, plasmaTurret, 16) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::PlasmaTurret, plasmaTurret, 6) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ScorpionAntiInfantry,
                  scorpionAntiInfantry, 16) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ScorpionAntiInfantry,
                  scorpionAntiInfantry, 6) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::CorvetteCannon, corvetteCannon, 6) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Scorpion, scorpionHrek, 0) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Scorpion, scorpionRetail, 0) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Warthog, warthogRetail, 1) &&
              // R-V25: the chaingun Warthog is an HREK type-16 mounted weapon,
              // so retail's turret bucket (6) is now an accepted pairing for
              // its canonical tuple - measured on the rocket variant.
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::WarthogChaingun, warthogChaingun, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::WarthogChaingun, warthogChaingun, 8) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Scorpion, scorpionHrek, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::ShadeFlak, shadePlasmaRetail, 6) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Unknown, alteredReachFingerprint, -1),
            "Reach fingerprint/type proof accepts only canonical and evidenced retail pairs");
        Check(ReachVehicleSeatIsDriver(ReachVehicleId::Warthog, 0) &&
              !ReachVehicleSeatIsDriver(ReachVehicleId::Warthog, 1) &&
              !ReachVehicleSeatIsDriver(
                  ReachVehicleId::WarthogChaingun, 0) &&
              ReachVehicleIsLookSteered(ReachVehicleId::Revenant) &&
              !ReachVehicleIsLookSteered(ReachVehicleId::Wraith) &&
              ReachVehicleSeatFollowsHull(ReachVehicleId::Warthog, 1) &&
              // The census no longer gates the camera, so an out-of-census
              // seat on a proven identity rides its hull too; Unknown and
              // walk-up turrets still claim no hull frame.
              ReachVehicleSeatFollowsHull(ReachVehicleId::Warthog, 2) &&
              !ReachVehicleSeatFollowsHull(ReachVehicleId::Unknown, 0) &&
              !ReachVehicleSeatFollowsHull(ReachVehicleId::Machinegun, 0) &&
              !ReachVehicleSeatFollowsPitch(ReachVehicleId::Sabre, 0) &&
              ReachVehicleSeatAuthorsSteering(
                  ReachVehicleId::Ghost, 0, true) &&
              !ReachVehicleSeatAuthorsSteering(
                  ReachVehicleId::Ghost, 0, false) &&
              ReachVehicleUsesWheel(ReachVehicleId::Forklift) &&
              !ReachVehicleUsesWheel(ReachVehicleId::Banshee),
            "Reach view-follow and steering ownership remain seat-explicit");
        // R-V19 is intentionally refresh-rate independent: every supported
        // 72-144 Hz headset consumes that frame's render-matched carrier basis.
        Check(!kReachR_V15RawHullFollowEnabled,
            "Reach vehicle follow stays on the render-matched carrier basis");
        // Rejected R-V20 remains dormant evidence: its value-only lease still
        // preserves full salted identities, but may never arm again.
        {
            constexpr ReachSeatLeaseKey leaseKey{
                37, 0x12340007, 0x23450009, 0x00030021, 3};
            constexpr ReachSeatLeaseKey otherGeneration{
                38, 0x12340007, 0x23450009, 0x00030021, 3};
            constexpr ReachSeatLeaseKey otherUnit{
                37, 0x12350007, 0x23450009, 0x00030021, 3};
            constexpr ReachSeatLeaseKey otherParent{
                37, 0x12340007, 0x23460009, 0x00030021, 3};
            constexpr ReachSeatLeaseKey otherDefinition{
                37, 0x12340007, 0x23450009, 0x00040021, 3};
            constexpr ReachSeatLeaseKey otherSeat{
                37, 0x12340007, 0x23450009, 0x00030021, 4};
            constexpr uint32_t originalFlags = 0x04020014;
            constexpr uint32_t writtenFlags =
                originalFlags & ~kReachSeatThirdPersonCameraBit;
            Check(
                !kReachR_V20SeatBitLeaseEnabled &&
                sizeof(ReachSeatLeaseKey) == 5 * sizeof(uint32_t) &&
                sizeof(ReachSeatLeasePayload) == 7 * sizeof(uint32_t) &&
                ReachSeatLeaseKeyValid(leaseKey) &&
                ReachSeatLeaseKeyEqual(leaseKey, leaseKey) &&
                !ReachSeatLeaseKeyEqual(leaseKey, otherGeneration) &&
                !ReachSeatLeaseKeyEqual(leaseKey, otherUnit) &&
                !ReachSeatLeaseKeyEqual(leaseKey, otherParent) &&
                !ReachSeatLeaseKeyEqual(leaseKey, otherDefinition) &&
                !ReachSeatLeaseKeyEqual(leaseKey, otherSeat) &&
                !ReachSeatLeaseKeyValid(
                    {0, 0x12340007, 0x23450009, 0x00030021, 3}) &&
                !ReachSeatLeaseKeyValid(
                    {37, -1, 0x23450009, 0x00030021, 3}) &&
                !ReachSeatLeaseKeyValid(
                    {37, 0x12340007, -1, 0x00030021, 3}) &&
                !ReachSeatLeaseKeyValid(
                    {37, 0x12340007, 0x23450009, 0xFFFFFFFFu, 3}) &&
                !ReachSeatLeaseKeyValid(
                    {37, 0x12340007, 0x23450009, 0x00030021, 16}),
                "Reach body-hide lease key preserves generation, both salted handles, definition datum and raw seat");

            // R-V26: the seat-bit lease is armed again, but with Halo 3's
            // accepted C20 LIFETIME, not R-V20's. The two rules that make it
            // C20 rather than R-V20 are: the seat we already own is never
            // rewritten (so the word cannot toggle per frame - the rejected
            // shaking), and any other key is a new occupation that must be
            // restored first. R-V20's own flag stays permanently false.
            Check(
                kReachSeatFirstPersonPresentationEnabled &&
                !kReachR_V20SeatBitLeaseEnabled &&
                ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Active) &&
                ReachSeatLeaseOwnsMutation(
                    ReachSeatLeaseState::RestorePending) &&
                !ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Empty) &&
                !ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::External) &&
                // Same seat while Active: owned, equal, so the sampler returns
                // early and performs no write.
                (ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Active) &&
                 ReachSeatLeaseKeyEqual(leaseKey, leaseKey)) &&
                // Any other seat/vehicle/generation is a different occupation.
                !ReachSeatLeaseKeyEqual(leaseKey, otherSeat) &&
                !ReachSeatLeaseKeyEqual(leaseKey, otherParent) &&
                // A naturally first-person seat (the Workshop maps clear this
                // bit in the tags) is External for its own key and is never
                // rewritten or restored.
                ReachSeatLeaseBlocksKey(
                    ReachSeatLeaseState::External, leaseKey, leaseKey) &&
                writtenFlags ==
                    (originalFlags & ~kReachSeatThirdPersonCameraBit) &&
                (writtenFlags & kReachSeatAllowsWeaponsBit) ==
                    (originalFlags & kReachSeatAllowsWeaponsBit),
                "The Reach seat first-person lease keeps Halo 3 C20's lifetime and touches only bit 4");

            // A complete exact seat earns one re-centre regardless of headset
            // cadence or View Follow. Repeated frames carrying that same key
            // can never fire again after the successful application commits.
            bool cadenceInvariant = true;
            for (const uint32_t refreshRate : {72u, 90u, 120u, 144u})
            {
                for (const bool viewFollow : {false, true})
                {
                    (void)viewFollow; // deliberately absent from the policy
                    ReachSeatRecenterLatch latch;
                    uint32_t requests = 0;
                    for (uint32_t frame = 0; frame < refreshRate * 2; ++frame)
                    {
                        if (latch.NeedsApply(true, true, &leaseKey))
                        {
                            ++requests;
                            cadenceInvariant = cadenceInvariant &&
                                latch.Commit(leaseKey);
                        }
                    }
                    cadenceInvariant = cadenceInvariant && requests == 1;
                }
            }
            Check(cadenceInvariant,
                "Reach seat re-centre fires once for the same exact seat at 72-144 Hz with View Follow off or on");

            // Every member of the full key is an occupation boundary. Commit
            // remains explicit so a failed runtime application can retry.
            ReachSeatRecenterLatch changedKeyLatch;
            const bool exactChangesRearm =
                changedKeyLatch.NeedsApply(true, true, &leaseKey) &&
                changedKeyLatch.Commit(leaseKey) &&
                changedKeyLatch.NeedsApply(true, true, &otherSeat) &&
                changedKeyLatch.Commit(otherSeat) &&
                changedKeyLatch.NeedsApply(true, true, &otherParent) &&
                changedKeyLatch.Commit(otherParent) &&
                changedKeyLatch.NeedsApply(true, true, &otherUnit) &&
                changedKeyLatch.Commit(otherUnit) &&
                changedKeyLatch.NeedsApply(true, true, &otherDefinition) &&
                changedKeyLatch.Commit(otherDefinition) &&
                changedKeyLatch.NeedsApply(true, true, &otherGeneration) &&
                changedKeyLatch.Commit(otherGeneration) &&
                !changedKeyLatch.NeedsApply(
                    true, true, &otherGeneration);
            Check(exactChangesRearm,
                "Reach seat re-centre treats seat, vehicle, unit, definition and generation changes as exact new occupations");
            ReachSeatRecenterLatch stagedCommitLatch;
            const bool rejectedStageRetries =
                stagedCommitLatch.NeedsApply(true,true,&leaseKey) &&
                stagedCommitLatch.NeedsApply(true,true,&leaseKey) &&
                stagedCommitLatch.Commit(leaseKey) &&
                !stagedCommitLatch.NeedsApply(true,true,&leaseKey);
            Check(rejectedStageRetries,
                "Reach rejected outer-camera stages remain pending until an explicit committed render consumes the exact seat");

            // No complete key means there is nothing safe to apply, but it is
            // not an exit. Preserve the applied key so a torn frame followed by
            // the same seat cannot yank the play space a second time.
            constexpr ReachSeatLeaseKey invalidKey{
                37, -1, 0x23450009, 0x00030021, 3};
            ReachSeatRecenterLatch missingLatch;
            const bool missingPreserves =
                missingLatch.NeedsApply(true, true, &leaseKey) &&
                missingLatch.Commit(leaseKey) &&
                !missingLatch.NeedsApply(true, true, nullptr) &&
                !missingLatch.NeedsApply(true, true, &invalidKey) &&
                !missingLatch.NeedsApply(true, true, &leaseKey) &&
                missingLatch.NeedsApply(true, true, &otherSeat);
            Check(missingPreserves,
                "Reach seat re-centre preserves its exact key across missing, torn or invalid current samples");
            Check(
                ReachSeatOccupationObserved(
                    leaseKey.directParent,leaseKey.seatIndex,16) &&
                !ReachSeatOccupationObserved(-1,leaseKey.seatIndex,16) &&
                !ReachSeatOccupationObserved(
                    leaseKey.directParent,-1,16) &&
                !ReachSeatOccupationObserved(
                    leaseKey.directParent,16,16),
                "Reach parent plus raw seat proves occupation independently of a torn seat-camera pointer");

            // A settled exit and an explicit config disable are real
            // boundaries. Both clear the latch so the next valid entry earns
            // one new request. Invalid commits must not replace good state.
            ReachSeatRecenterLatch boundaryLatch;
            const bool boundariesRearm =
                boundaryLatch.NeedsApply(true, true, &leaseKey) &&
                boundaryLatch.Commit(leaseKey) &&
                !boundaryLatch.NeedsApply(true, false, nullptr) &&
                boundaryLatch.NeedsApply(true, true, &leaseKey) &&
                boundaryLatch.Commit(leaseKey) &&
                !boundaryLatch.NeedsApply(false, true, &leaseKey) &&
                boundaryLatch.NeedsApply(true, true, &leaseKey) &&
                boundaryLatch.Commit(leaseKey) &&
                !boundaryLatch.Commit(invalidKey) &&
                !boundaryLatch.NeedsApply(true, true, &leaseKey);
            Check(boundariesRearm,
                "Reach seat re-centre rearms only after settled exit/config disable and commits only valid exact keys");

            // Once a seat is admitted, later identity/marker/bounds proof can
            // miss without proving that the player left it. Feeding the
            // already-stable occupation into the shared debounce must hold the
            // latch indefinitely at every supported headset cadence. Only a
            // genuinely observed OnFoot run earns the exit boundary.
            bool proofMissesHoldOccupation = true;
            for (const uint32_t refreshRate : {72u, 90u, 120u, 144u})
            {
                Halo3VehicleDebounce occupation{};
                occupation.stable=Halo3VehicleState::Vehicle;
                occupation.candidate=Halo3VehicleState::Vehicle;
                ReachSeatRecenterLatch latch;
                proofMissesHoldOccupation =
                    proofMissesHoldOccupation &&
                    latch.NeedsApply(true,true,&leaseKey) &&
                    latch.Commit(leaseKey);
                for (uint32_t frame=0;frame<refreshRate*2;++frame)
                {
                    proofMissesHoldOccupation =
                        proofMissesHoldOccupation &&
                        !occupation.Update(
                            occupation.stable,
                            kHalo3VehicleDebounceFrames) &&
                        !latch.NeedsApply(
                            true,
                            occupation.stable==
                                Halo3VehicleState::Vehicle,
                            nullptr);
                }
                proofMissesHoldOccupation =
                    proofMissesHoldOccupation &&
                    !latch.NeedsApply(true,true,&leaseKey);
                for (uint32_t frame=0;
                     frame<kHalo3VehicleDebounceFrames;++frame)
                {
                    occupation.Update(
                        Halo3VehicleState::OnFoot,
                        kHalo3VehicleDebounceFrames);
                }
                proofMissesHoldOccupation =
                    proofMissesHoldOccupation &&
                    occupation.stable==Halo3VehicleState::OnFoot &&
                    !latch.NeedsApply(true,false,nullptr) &&
                    latch.NeedsApply(true,true,&leaseKey);
            }
            Check(proofMissesHoldOccupation,
                "Reach camera-proof misses preserve an occupied seat at 72-144 Hz while a true OnFoot sample rearms entry");

            float entryYaw = 0.0f;
            const float forward[3]{0.0f, 1.0f, 0.0f};
            const float vertical[3]{0.0f, 0.0f, 1.0f};
            const float nonFinite[3]{
                std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
            const bool headingGuarded =
                ReachVehicleEntryYaw(forward, entryYaw) &&
                std::fabs(entryYaw - 1.57079632679f) < 1.0e-5f &&
                !ReachVehicleEntryYaw(vertical, entryYaw) &&
                !ReachVehicleEntryYaw(nonFinite, entryYaw) &&
                !ReachVehicleEntryYaw(nullptr, entryYaw);
            Check(headingGuarded,
                "Reach seat re-centre consumes only a finite render-matched horizontal vehicle heading");
            constexpr float packedGameYaw = -2.375f;
            constexpr float packedHeadYaw = 1.125f;
            constexpr uint64_t packedYawReferences =
                ReachPackYawReferencePair(packedGameYaw, packedHeadYaw);
            constexpr ReachYawReferencePair unpackedYawReferences =
                ReachUnpackYawReferencePair(packedYawReferences);
            Check(
                unpackedYawReferences.gameYaw == packedGameYaw &&
                unpackedYawReferences.headYaw == packedHeadYaw &&
                ReachYawReferencePairValid(unpackedYawReferences) &&
                !ReachYawReferencePairValid(
                    ReachUnpackYawReferencePair(
                        kReachInvalidYawReferencePair)),
                "Reach publishes game/head yaw references as one bit-exact input snapshot");
            Check(
                ReachSeatLeaseRetainsHook(
                    ReachSeatLeaseState::Installing) &&
                ReachSeatLeaseRetainsHook(ReachSeatLeaseState::Active) &&
                ReachSeatLeaseRetainsHook(
                    ReachSeatLeaseState::RestorePending) &&
                !ReachSeatLeaseRetainsHook(
                    ReachSeatLeaseState::External) &&
                !ReachSeatLeaseRetainsHook(ReachSeatLeaseState::Empty) &&
                !ReachSeatLeaseRetainsHook(
                    ReachSeatLeaseState::CleanupLocked) &&
                ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Active) &&
                ReachSeatLeaseOwnsMutation(
                    ReachSeatLeaseState::RestorePending) &&
                !ReachSeatLeaseOwnsMutation(
                    ReachSeatLeaseState::Installing) &&
                ReachSeatLeaseBlocksKey(
                    ReachSeatLeaseState::External, leaseKey, leaseKey) &&
                !ReachSeatLeaseBlocksKey(
                    ReachSeatLeaseState::External, leaseKey, otherSeat) &&
                ReachSeatLeaseBlocksKey(
                    ReachSeatLeaseState::Active, leaseKey, otherSeat),
                "Reach body-hide lease retains teardown ownership only while installing/owned/pending and never retries an external exact key");
            Check(
                ReachSeatLeaseCanAcquireCleanupLock(
                    ReachSeatLeaseState::Empty) &&
                ReachSeatLeaseCanAcquireCleanupLock(
                    ReachSeatLeaseState::External) &&
                !ReachSeatLeaseCanAcquireCleanupLock(
                    ReachSeatLeaseState::Installing) &&
                !ReachSeatLeaseCanAcquireCleanupLock(
                    ReachSeatLeaseState::Active) &&
                !ReachSeatLeaseCanAcquireCleanupLock(
                    ReachSeatLeaseState::RestorePending) &&
                !ReachSeatLeaseCanAcquireCleanupLock(
                    ReachSeatLeaseState::CleanupLocked) &&
                ReachSeatLeaseCleanupLocked(
                    ReachSeatLeaseState::CleanupLocked) &&
                !ReachSeatLeaseCleanupLocked(
                    ReachSeatLeaseState::Empty) &&
                ReachSeatLeaseBlocksKey(
                    ReachSeatLeaseState::CleanupLocked,
                    leaseKey, otherSeat),
                "Reach body-hide cleanup lock races conclusively with Installing on the shared state and blocks every new key");
            Check(
                writtenFlags == 0x04020004 &&
                ReachClassifySeatLeaseRestore(
                    originalFlags, originalFlags, writtenFlags) ==
                    ReachSeatLeaseRestoreDisposition::AlreadyOriginal &&
                ReachClassifySeatLeaseRestore(
                    writtenFlags, originalFlags, writtenFlags) ==
                    ReachSeatLeaseRestoreDisposition::RestoreOriginal &&
                ReachClassifySeatLeaseRestore(
                    originalFlags ^ 0x40u, originalFlags, writtenFlags) ==
                    ReachSeatLeaseRestoreDisposition::ExternalWrite &&
                ReachClassifySeatLeaseRestore(
                    writtenFlags, writtenFlags, writtenFlags) ==
                    ReachSeatLeaseRestoreDisposition::AlreadyOriginal,
                "Reach body-hide restoration writes only exact owned flags and yields to engine/tag changes");
        }
        // Reach's actual body-visibility policy is unit-camera WORD bit 2.
        // It is scoped to one admitted outer render; no seat camera-mode bit is
        // changed, so the native first-person camera and both view-follow
        // settings remain independent.
        {
            constexpr uint16_t original = 0xA521;
            constexpr uint16_t written =
                ReachUnitCameraHidePlayerFlags(original);
            constexpr uint16_t alreadyHidden = 0xA525;
            constexpr uint16_t changedDuringRender =
                static_cast<uint16_t>(written ^ 0x0080u);
            Check(
                kReachSeatCameraOffset == 0x70 &&
                kReachUnitCameraFlagsOffset == 0x00 &&
                kReachUnitCameraHidePlayerBit == 0x0004 &&
                written == 0xA525 &&
                static_cast<uint16_t>(written ^ original) ==
                    kReachUnitCameraHidePlayerBit &&
                ReachUnitCameraHidePlayerFlags(alreadyHidden) ==
                    alreadyHidden &&
                ReachUnitCameraRestorePlayerFlags(
                    written, original) == original &&
                ReachUnitCameraRestorePlayerFlags(
                    changedDuringRender, original) ==
                    static_cast<uint16_t>(original ^ 0x0080u) &&
                ReachClassifySeatLeaseRestore(
                    written, original, written) ==
                    ReachSeatLeaseRestoreDisposition::RestoreOriginal &&
                ReachClassifySeatLeaseRestore(
                    static_cast<uint16_t>(written ^ 0x0080u),
                    original, written) ==
                    ReachSeatLeaseRestoreDisposition::ExternalWrite,
                "Reach body hide sets only unit-camera bit 2 and restores only that owned bit while preserving unrelated changes");
        }
        // R-V18: entering a Reach seat selects the same native aim feedback
        // under BOTH view-follow settings.
        {
            float nativeAim[3] = {0.3f, 0.4f, 0.0f};
            float normalizedAim[3] = {};
            const float invalidAim[3] = {
                std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
            Check(
                ReachNormalizeUnitAimingVector(nativeAim, normalizedAim) &&
                std::fabs(normalizedAim[0] - 0.6f) < 1.0e-6f &&
                std::fabs(normalizedAim[1] - 0.8f) < 1.0e-6f &&
                normalizedAim[2] == 0.0f &&
                !ReachNormalizeUnitAimingVector(
                    invalidAim, normalizedAim) &&
                ReachSelectAimFeedbackSource(true, true, false) ==
                    ReachAimFeedbackSource::SeatedUnitAim &&
                ReachSelectAimFeedbackSource(true, true, true) ==
                    ReachAimFeedbackSource::SeatedUnitAim &&
                ReachSelectAimFeedbackSource(true, false, false) ==
                    ReachAimFeedbackSource::SeatedCompactFallback &&
                ReachSelectAimFeedbackSource(false, true, true) ==
                    ReachAimFeedbackSource::OnFootCompact &&
                ReachAimFeedbackCanDriveReticle(
                    ReachAimFeedbackSource::SeatedUnitAim) &&
                !ReachAimFeedbackCanDriveReticle(
                    ReachAimFeedbackSource::SeatedCompactFallback) &&
                !ReachAimFeedbackCanDriveReticle(
                    ReachAimFeedbackSource::OnFootCompact) &&
                !ReachVehicleSeatAuthorsSteering(
                    ReachVehicleId::Warthog, 0, false) &&
                ReachVehicleSeatAuthorsSteering(
                    ReachVehicleId::Warthog, 0, true),
                "Reach unit-aim feedback stays active under both view-follow modes");
        }
        // Completed-pair reticle truth is expressed in the final Reach camera
        // basis. Identity here means Reach world +X is camera forward, world
        // -Y is camera right and world +Z is camera up.
        {
            const float forward[3] = {1.0f, 0.0f, 0.0f};
            const float up[3] = {0.0f, 0.0f, 1.0f};
            const float right[3] = {0.0f, -1.0f, 0.0f};
            float localForward[3]{};
            float localRight[3]{};
            float localUp[3]{};
            const float scaledForward[3] = {2.0f, 0.0f, 0.0f};
            float localScaledForward[3]{};
            Check(
                ReachWorldAimToCameraLocal(
                    forward, forward, up, localForward) &&
                ReachWorldAimToCameraLocal(
                    right, forward, up, localRight) &&
                ReachWorldAimToCameraLocal(
                    up, forward, up, localUp) &&
                ReachWorldAimToCameraLocal(
                    scaledForward, forward, up, localScaledForward) &&
                std::fabs(localForward[0]) < 1.0e-6f &&
                std::fabs(localForward[1]) < 1.0e-6f &&
                std::fabs(localForward[2] + 1.0f) < 1.0e-6f &&
                std::fabs(localRight[0] - 1.0f) < 1.0e-6f &&
                std::fabs(localRight[1]) < 1.0e-6f &&
                std::fabs(localRight[2]) < 1.0e-6f &&
                std::fabs(localUp[0]) < 1.0e-6f &&
                std::fabs(localUp[1] - 1.0f) < 1.0e-6f &&
                std::fabs(localUp[2]) < 1.0e-6f &&
                std::fabs(localScaledForward[2] + 1.0f) < 1.0e-6f,
                "Reach completed-pair aim maps forward/right/up to OpenXR -Z/+X/+Y and normalizes");

            // Non-trivial yaw, pitch and roll: world -> local -> world must
            // round-trip through the exact F/U/R basis and sign convention.
            constexpr float yaw = 0.71f;
            constexpr float pitch = -0.29f;
            constexpr float roll = 0.23f;
            const float cy = std::cos(yaw), sy = std::sin(yaw);
            const float cp = std::cos(pitch), sp = std::sin(pitch);
            const float cr = std::cos(roll), sr = std::sin(roll);
            const float rolledForward[3] = {cp * cy, cp * sy, sp};
            const float rolledUp[3] = {
                (-sp * cy) * cr + sy * sr,
                (-sp * sy) * cr - cy * sr,
                cp * cr};
            const float rolledRight[3] = {
                rolledForward[1] * rolledUp[2] -
                    rolledForward[2] * rolledUp[1],
                rolledForward[2] * rolledUp[0] -
                    rolledForward[0] * rolledUp[2],
                rolledForward[0] * rolledUp[1] -
                    rolledForward[1] * rolledUp[0]};
            const float worldAim[3] = {0.2f, -0.7f, 0.6f};
            float normalizedWorldAim[3]{};
            float rolledLocal[3]{};
            float roundTrip[3]{};
            const bool converted = ReachNormalizeUnitAimingVector(
                    worldAim, normalizedWorldAim) &&
                ReachWorldAimToCameraLocal(
                    worldAim, rolledForward, rolledUp, rolledLocal);
            if (converted)
            {
                for (int component = 0; component < 3; ++component)
                {
                    roundTrip[component] =
                        rolledRight[component] * rolledLocal[0] +
                        rolledUp[component] * rolledLocal[1] -
                        rolledForward[component] * rolledLocal[2];
                }
            }
            Check(
                converted &&
                std::fabs(roundTrip[0] - normalizedWorldAim[0]) < 1.0e-5f &&
                std::fabs(roundTrip[1] - normalizedWorldAim[1]) < 1.0e-5f &&
                std::fabs(roundTrip[2] - normalizedWorldAim[2]) < 1.0e-5f,
                "Reach camera-local aim round-trips through a rolled yaw/pitch basis");

            const float zero[3] = {};
            const float nonUnitForward[3] = {1.1f, 0.0f, 0.0f};
            const float nonOrthogonalUp[3] = {1.0f, 0.0f, 0.0f};
            const float invalidAim[3] = {
                std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
            const float invalidUp[3] = {
                0.0f, std::numeric_limits<float>::infinity(), 1.0f};
            float rejected[3]{};
            Check(
                !ReachWorldAimToCameraLocal(
                    zero, forward, up, rejected) &&
                !ReachWorldAimToCameraLocal(
                    invalidAim, forward, up, rejected) &&
                !ReachWorldAimToCameraLocal(
                    forward, nonUnitForward, up, rejected) &&
                !ReachWorldAimToCameraLocal(
                    forward, forward, nonOrthogonalUp, rejected) &&
                !ReachWorldAimToCameraLocal(
                    forward, forward, invalidUp, rejected),
                "Reach camera-local aim rejects zero/non-finite aim and invalid camera bases");
        }

        // Admission is exact to one completed prepared serial and one complete
        // salted seat occupation. View-follow never appears in this decision;
        // only native SeatedUnitAim may take reticle ownership.
        {
            constexpr uint32_t generation = 37;
            constexpr uint64_t serial = 0x1020304050607080ull;
            constexpr ReachSeatLeaseKey occupation{
                generation, 0x12340007, 0x23450009, 0x00030021, 3};
            ReachReticleAimSample sample{};
            sample.generation = generation;
            sample.preparedSerial = serial;
            sample.source = ReachAimFeedbackSource::SeatedUnitAim;
            sample.occupation = occupation;
            sample.cameraLocalDirection[0] = 0.6f;
            sample.cameraLocalDirection[1] = 0.0f;
            sample.cameraLocalDirection[2] = -0.8f;
            Check(
                ReachReticleAimSampleAdmitted(
                    sample, generation, serial) &&
                !ReachReticleAimSampleAdmitted(
                    sample, generation + 1, serial) &&
                !ReachReticleAimSampleAdmitted(
                    sample, generation, serial + 1),
                "Reach reticle sample admission requires current generation and exact prepared serial");

            ReachReticleAimSample fallback = sample;
            fallback.source =
                ReachAimFeedbackSource::SeatedCompactFallback;
            ReachReticleAimSample onFoot = sample;
            onFoot.source = ReachAimFeedbackSource::OnFootCompact;
            ReachReticleAimSample invalidKey = sample;
            invalidKey.occupation.directParent = -1;
            ReachReticleAimSample wrongKeyGeneration = sample;
            wrongKeyGeneration.occupation.generation = generation + 1;
            ReachReticleAimSample invalidVector = sample;
            invalidVector.cameraLocalDirection[0] =
                std::numeric_limits<float>::quiet_NaN();
            ReachReticleAimSample zeroVector = sample;
            zeroVector.cameraLocalDirection[0] = 0.0f;
            zeroVector.cameraLocalDirection[1] = 0.0f;
            zeroVector.cameraLocalDirection[2] = 0.0f;
            Check(
                !ReachReticleAimSampleAdmitted(
                    fallback, generation, serial) &&
                !ReachReticleAimSampleAdmitted(
                    onFoot, generation, serial) &&
                !ReachReticleAimSampleAdmitted(
                    invalidKey, generation, serial) &&
                !ReachReticleAimSampleAdmitted(
                    wrongKeyGeneration, generation, serial) &&
                !ReachReticleAimSampleAdmitted(
                    invalidVector, generation, serial) &&
                !ReachReticleAimSampleAdmitted(
                    zeroVector, generation, serial),
                "Reach reticle sample rejects fallback sources, incomplete full keys and invalid vectors");

            ReachReticleAimSample next = sample;
            next.occupation.seatIndex = 4;
            next.cameraLocalDirection[0] = -0.8f;
            next.cameraLocalDirection[2] = -0.6f;
            const ReachReticleAimSample afterFailed =
                ReachReticleAimSampleAfterAttempt(sample, next, false);
            const ReachReticleAimSample afterCompleted =
                ReachReticleAimSampleAfterAttempt(sample, next, true);
            ReachReticleAimSample revocation = next;
            revocation.source =
                ReachAimFeedbackSource::SeatedCompactFallback;
            const ReachReticleAimSample afterCompletedRevocation =
                ReachReticleAimSampleAfterAttempt(
                    sample, revocation, true);
            Check(
                afterFailed.occupation.seatIndex == 3 &&
                afterFailed.cameraLocalDirection[0] == 0.6f &&
                afterCompleted.preparedSerial == serial &&
                afterCompleted.occupation.seatIndex == 4 &&
                afterCompleted.cameraLocalDirection[0] == -0.8f &&
                ReachReticleAimSampleAdmitted(
                    afterCompleted, generation, serial) &&
                afterCompletedRevocation.occupation.seatIndex == 4 &&
                !ReachReticleAimSampleAdmitted(
                    afterCompletedRevocation, generation, serial),
                "Reach reticle publication preserves a prior pair across failure and replaces it on every later completed same-serial pair");
        }

        Check(
            ReachSeatAimCanDriveReticle(
                ReachAimFeedbackSource::SeatedUnitAim,
                kReachSeatAllowsWeaponsBit) &&
            !ReachSeatAimCanDriveReticle(
                ReachAimFeedbackSource::SeatedUnitAim, 0) &&
            !ReachSeatAimCanDriveReticle(
                ReachAimFeedbackSource::SeatedCompactFallback,
                kReachSeatAllowsWeaponsBit),
            "Reach unit aim drives only personal-weapon seat reticles, never vehicle barrels");

        Check(
            ReachPassengerNeedsFirstPersonRenderAdmission(
                true, true, true, true, kReachSeatAllowsWeaponsBit, 0) &&
            !ReachPassengerNeedsFirstPersonRenderAdmission(
                true, true, true, true, 0, 0) &&
            !ReachPassengerNeedsFirstPersonRenderAdmission(
                true, true, true, true, kReachSeatAllowsWeaponsBit,
                0xFFFFu) &&
            !ReachPassengerNeedsFirstPersonRenderAdmission(
                true, false, true, true, kReachSeatAllowsWeaponsBit, 0) &&
            !ReachPassengerNeedsFirstPersonRenderAdmission(
                true, true, true, false, kReachSeatAllowsWeaponsBit, 0),
            "Reach passenger first-person render admission is exact to an active VR-owned allows-weapons seat and never overrides stock admission");

        // R-V22 leaves the visible controller reticle in charge and admits a
        // native selected-barrel direction redirect only for one fresh exact
        // local occupation/target pair. Fifty milliseconds covers three
        // scheduled intervals at 72 Hz without allowing long-stale aim at
        // 144 Hz.
        {
            constexpr uint32_t generation = 43;
            constexpr uint64_t nowMs = 7000;
            constexpr ReachSeatLeaseKey occupation{
                generation, 0x12340007, 0x23450009, 0x00030021, 3};
            ReachShotDirectionSample sample{};
            sample.currentGeneration = generation;
            sample.nowMs = nowMs;
            sample.occupationSampleMs =
                nowMs - kReachShotDirectionFreshMs;
            sample.targetSampleMs =
                nowMs - kReachShotDirectionFreshMs;
            sample.active = true;
            sample.firingUnitIndex = occupation.unitHandle;
            sample.occupation = occupation;
            sample.targetKey = occupation;
            sample.origin[0] = 1.0f;
            sample.origin[1] = 2.0f;
            sample.origin[2] = 3.0f;
            sample.target[0] = 401.0f;
            sample.target[1] = 2.0f;
            sample.target[2] = 3.0f;
            Check(
                !kReachR_V22NativeVehicleReticleEnabled &&
                ReachShotDirectionSampleAdmitted(sample),
                "Reach vehicle shot direction admits the exact fresh full-key boundary while retaining the controller reticle");

            ReachShotDirectionSample staleOccupation = sample;
            staleOccupation.occupationSampleMs =
                nowMs - kReachShotDirectionFreshMs - 1;
            ReachShotDirectionSample staleTarget = sample;
            staleTarget.targetSampleMs =
                nowMs - kReachShotDirectionFreshMs - 1;
            ReachShotDirectionSample futureOccupation = sample;
            futureOccupation.occupationSampleMs = nowMs + 1;
            ReachShotDirectionSample futureTarget = sample;
            futureTarget.targetSampleMs = nowMs + 1;
            ReachShotDirectionSample wrongGeneration = sample;
            wrongGeneration.currentGeneration = generation + 1;
            ReachShotDirectionSample wrongSalt = sample;
            wrongSalt.targetKey.unitHandle ^= 0x00010000;
            ReachShotDirectionSample wrongUnit = sample;
            wrongUnit.firingUnitIndex ^= 0x00010000;
            ReachShotDirectionSample inactive = sample;
            inactive.active = false;
            ReachShotDirectionSample invalidOrigin = sample;
            invalidOrigin.origin[1] =
                std::numeric_limits<float>::quiet_NaN();
            ReachShotDirectionSample zeroLength = sample;
            memcpy(zeroLength.target, zeroLength.origin,
                   sizeof(zeroLength.target));
            ReachShotDirectionSample excessiveDistance = sample;
            excessiveDistance.target[0] =
                excessiveDistance.origin[0] + 2049.0f;
            Check(
                !ReachShotDirectionSampleAdmitted(staleOccupation) &&
                !ReachShotDirectionSampleAdmitted(staleTarget) &&
                !ReachShotDirectionSampleAdmitted(futureOccupation) &&
                !ReachShotDirectionSampleAdmitted(futureTarget) &&
                !ReachShotDirectionSampleAdmitted(wrongGeneration) &&
                !ReachShotDirectionSampleAdmitted(wrongSalt) &&
                !ReachShotDirectionSampleAdmitted(wrongUnit) &&
                !ReachShotDirectionSampleAdmitted(inactive) &&
                !ReachShotDirectionSampleAdmitted(invalidOrigin) &&
                !ReachShotDirectionSampleAdmitted(zeroLength) &&
                !ReachShotDirectionSampleAdmitted(excessiveDistance),
                "Reach vehicle shot direction rejects stale, future, mismatched and invalid samples");
        }

        // Personal-weapon origin substitution requires one fresh, matching
        // full-salt seat/eye pair. The 100 ms boundary is admitted exactly;
        // every stale, mismatched or vehicle-barrel case remains stock.
        {
            constexpr uint32_t generation = 41;
            constexpr uint64_t nowMs = 5000;
            constexpr ReachSeatLeaseKey occupation{
                generation, 0x12340007, 0x23450009, 0x00030021, 3};
            ReachShotOriginSample sample{};
            sample.currentGeneration = generation;
            sample.nowMs = nowMs;
            sample.occupationSampleMs = nowMs - kReachShotOriginFreshMs;
            sample.renderedEyeSampleMs = nowMs - kReachShotOriginFreshMs;
            sample.renderedEyePreparedSerial = 77;
            sample.active = true;
            sample.allowsWeapons = true;
            sample.firingUnitIndex = 7;
            sample.occupation = occupation;
            sample.renderedEye = occupation;
            sample.renderedEyePosition[0] = 1.0f;
            sample.renderedEyePosition[1] = 2.0f;
            sample.renderedEyePosition[2] = 3.0f;
            Check(
                ReachShotOriginSampleAdmitted(sample),
                "Reach personal-shot origin admits the exact fresh full-key boundary");
            ReachShotOriginSample saltedFiringArgument = sample;
            saltedFiringArgument.firingUnitIndex = 0x7ABC0007;
            Check(
                ReachShotOriginSampleAdmitted(saltedFiringArgument),
                "Reach personal-shot origin matches the engine firing argument by object index");

            ReachShotOriginSample staleOccupation = sample;
            staleOccupation.occupationSampleMs =
                nowMs - kReachShotOriginFreshMs - 1;
            ReachShotOriginSample staleEye = sample;
            staleEye.renderedEyeSampleMs =
                nowMs - kReachShotOriginFreshMs - 1;
            ReachShotOriginSample futureEye = sample;
            futureEye.renderedEyeSampleMs = nowMs + 1;
            ReachShotOriginSample wrongGeneration = sample;
            wrongGeneration.currentGeneration = generation + 1;
            ReachShotOriginSample wrongSalt = sample;
            wrongSalt.renderedEye.unitHandle ^= 0x00010000;
            ReachShotOriginSample wrongParent = sample;
            wrongParent.renderedEye.directParent ^= 0x00010000;
            ReachShotOriginSample wrongDefinition = sample;
            wrongDefinition.renderedEye.definitionDatum ^= 1;
            ReachShotOriginSample wrongSeat = sample;
            wrongSeat.renderedEye.seatIndex = 4;
            ReachShotOriginSample barrelSeat = sample;
            barrelSeat.allowsWeapons = false;
            ReachShotOriginSample wrongUnit = sample;
            wrongUnit.firingUnitIndex = 8;
            ReachShotOriginSample noCompletedPair = sample;
            noCompletedPair.renderedEyePreparedSerial = 0;
            ReachShotOriginSample invalidEye = sample;
            invalidEye.renderedEyePosition[1] =
                std::numeric_limits<float>::quiet_NaN();
            Check(
                !ReachShotOriginSampleAdmitted(staleOccupation) &&
                !ReachShotOriginSampleAdmitted(staleEye) &&
                !ReachShotOriginSampleAdmitted(futureEye) &&
                !ReachShotOriginSampleAdmitted(wrongGeneration) &&
                !ReachShotOriginSampleAdmitted(wrongSalt) &&
                !ReachShotOriginSampleAdmitted(wrongParent) &&
                !ReachShotOriginSampleAdmitted(wrongDefinition) &&
                !ReachShotOriginSampleAdmitted(wrongSeat) &&
                !ReachShotOriginSampleAdmitted(barrelSeat) &&
                !ReachShotOriginSampleAdmitted(wrongUnit) &&
                !ReachShotOriginSampleAdmitted(noCompletedPair) &&
                !ReachShotOriginSampleAdmitted(invalidEye),
                "Reach personal-shot origin rejects stale, mismatched, barrel and invalid samples");
        }

        // The same validated stereo centre supplies both native-aim rotation
        // and ray origin. q/-q sign equivalence and symmetric eye cant must not
        // create a false centre rotation.
        {
            const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const float negativeScaledIdentity[4] = {
                0.0f, 0.0f, 0.0f, -2.0f};
            const float leftPosition[3] = {-0.032f, 1.5f, -2.0f};
            const float rightPosition[3] = {0.032f, 1.5f, -2.0f};
            ReachStereoCenterPose signAligned{};
            Check(
                ReachBuildStereoCenterPose(
                    identity, leftPosition,
                    negativeScaledIdentity, rightPosition,
                    signAligned) &&
                std::fabs(signAligned.orientation[0]) < 1.0e-6f &&
                std::fabs(signAligned.orientation[1]) < 1.0e-6f &&
                std::fabs(signAligned.orientation[2]) < 1.0e-6f &&
                std::fabs(signAligned.orientation[3] - 1.0f) < 1.0e-6f &&
                std::fabs(signAligned.position[0]) < 1.0e-6f &&
                std::fabs(signAligned.position[1] - 1.5f) < 1.0e-6f &&
                std::fabs(signAligned.position[2] + 2.0f) < 1.0e-6f,
                "Reach stereo centre aligns q/-q and uses the exact eye-position midpoint");

            constexpr float halfCant = 0.10f;
            const float cantSin = std::sin(halfCant);
            const float cantCos = std::cos(halfCant);
            const float leftCant[4] = {
                0.0f, cantSin, 0.0f, cantCos};
            const float rightCant[4] = {
                0.0f, -cantSin, 0.0f, cantCos};
            ReachStereoCenterPose symmetricCant{};
            Check(
                ReachBuildStereoCenterPose(
                    leftCant, leftPosition, rightCant, rightPosition,
                    symmetricCant) &&
                std::fabs(symmetricCant.orientation[0]) < 1.0e-6f &&
                std::fabs(symmetricCant.orientation[1]) < 1.0e-6f &&
                std::fabs(symmetricCant.orientation[2]) < 1.0e-6f &&
                std::fabs(symmetricCant.orientation[3] - 1.0f) < 1.0e-6f,
                "Reach stereo centre averages symmetric canted-eye orientations without bias");

            const float zeroQuaternion[4] = {};
            const float quarterTurnX[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            const float invalidQuaternion[4] = {
                0.0f, 0.0f,
                std::numeric_limits<float>::quiet_NaN(), 1.0f};
            const float invalidPosition[3] = {
                0.0f, std::numeric_limits<float>::infinity(), 0.0f};
            ReachStereoCenterPose rejected{};
            Check(
                !ReachBuildStereoCenterPose(
                    zeroQuaternion, leftPosition,
                    identity, rightPosition, rejected) &&
                !ReachBuildStereoCenterPose(
                    identity, leftPosition,
                    quarterTurnX, rightPosition, rejected) &&
                !ReachBuildStereoCenterPose(
                    invalidQuaternion, leftPosition,
                    identity, rightPosition, rejected) &&
                !ReachBuildStereoCenterPose(
                    identity, invalidPosition,
                    identity, rightPosition, rejected),
                "Reach stereo centre rejects zero/non-finite and ambiguous eye poses");
        }
        ReachSeatCameraBasis reachBasis{};
        reachBasis.forward[0] = 1.0f;
        reachBasis.left[1] = 1.0f;
        reachBasis.up[2] = 1.0f;
        const float reachBase[3] = {10.0f, 20.0f, 30.0f};
        float reachPoint[3]{};
        Check(ReachComposeSeatCameraPoint(
                  reachBase, reachBasis, 2.0f, 3.0f, 4.0f, reachPoint) &&
              reachPoint[0] == 12.0f && reachPoint[1] == 17.0f &&
              reachPoint[2] == 34.0f,
            "Reach camera trims use Blender +forward/+right/+up axes");
        reachBasis.scale = 2.0f;
        Check(ReachComposeSeatCameraPoint(
                  reachBase, reachBasis, 2.0f, 3.0f, 4.0f, reachPoint) &&
              reachPoint[0] == 14.0f && reachPoint[1] == 14.0f &&
              reachPoint[2] == 38.0f,
            "Reach camera trims honor authored marker uniform scale");
        constexpr uint32_t reachTrimGeneration = 0x11223344;
        constexpr uint64_t reachTrimSnapshot = ReachVehicleTrimSnapshot(
            reachTrimGeneration, ReachVehicleId::Falcon, 4);
        Check(ReachVehicleTrimSnapshotSlot(
                  reachTrimSnapshot, reachTrimGeneration,
                  kReachVehicleSeatSlots) ==
                  ConfigReachSeatTrimSlot(
                      static_cast<int>(ReachVehicleId::Falcon), 4) &&
              ReachVehicleTrimSnapshotSlot(
                  reachTrimSnapshot, reachTrimGeneration + 1,
                  kReachVehicleSeatSlots) == -1,
            "Reach menu trim publication is generation-bound and slot-compatible");
        // An unmatched vehicle publishes the generic sentinel: the snapshot
        // stays generation-live (so FpActive, recenter and body hide keep
        // working) and, since R-V25, decodes to the dedicated unmatched trim
        // row. It must never decode to -1, which meant the shared universal
        // trim and let a seated F1 edit move every other seat in every title.
        constexpr uint64_t reachGenericSnapshot = ReachVehicleTrimSnapshot(
            reachTrimGeneration, ReachVehicleId::Unknown, 2);
        Check(reachGenericSnapshot != 0 &&
              static_cast<uint32_t>(reachGenericSnapshot >> 32) ==
                  reachTrimGeneration &&
              ((reachGenericSnapshot >> 8) & 0xFFu) ==
                  kReachVehicleGenericIdentityCode &&
              ReachVehicleTrimSnapshotSlot(
                  reachGenericSnapshot, reachTrimGeneration,
                  kReachVehicleSeatSlots) ==
                  ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 2) &&
              ReachVehicleTrimSnapshotSlot(
                  reachGenericSnapshot, reachTrimGeneration,
                  kReachVehicleSeatSlots) >= 0 &&
              ReachVehicleTrimSnapshot(0, ReachVehicleId::Unknown, 2) == 0 &&
              ReachVehicleTrimSnapshot(
                  reachTrimGeneration, ReachVehicleId::Unknown, 16) == 0,
            "An unmatched Reach seat stays live under the generic sentinel and keys its own trim row");

        constexpr float kReachTestIpdMeters = 0.064f;
        Check(std::isfinite(kReachWorldUnitsPerMeter) &&
              kReachWorldUnitsPerMeter > 0.0f &&
              std::fabs(kReachWorldUnitsPerMeter * 3.048f - 1.0f) < 1.0e-6f &&
              std::fabs(kReachTestIpdMeters * kReachWorldUnitsPerMeter -
                        0.020997375f) < 1.0e-7f,
            "Reach head translation and runtime IPD use the exact ten-foot world-unit conversion");

        Check(kReachSsaoCallRva == 0x0026E81D &&
              kReachRainGateAfterSsaoRva == 0x0026E822 &&
              kReachSsaoCallRva + 5 == kReachRainGateAfterSsaoRva &&
              kReachLightmapShadowsRenderCallRva < kReachSsaoCallRva &&
              kReachSsaoRva == 0x002A13A0 &&
              kReachSsaoEndRva == 0x002A1907 &&
              kReachSsaoBodySize == 0x567 &&
              kReachSsaoShadowMaskCallRvas[0] == 0x002A169A &&
              kReachSsaoShadowMaskCallRvas[1] == 0x002A1755 &&
              kReachSsaoShadowMaskCallRvas[0] >= kReachSsaoRva &&
              kReachSsaoShadowMaskCallRvas[0] <
                  kReachSsaoShadowMaskCallRvas[1] &&
              kReachSsaoShadowMaskCallRvas[1] < kReachSsaoEndRva &&
              kReachShadowMaskAcquireRva == 0x00252F08 &&
              kReachShadowMaskSurfaceIndex == 2 &&
              std::string_view(kReachSsaoBodySha256) ==
                  "760D2BEC3AA13ABFA0AB2002E2873C9C8A9F1FEA9EE63238585C2A6C92943EE7",
            "Reach SSAO candidate pins the exact retail caller, callee, range, and body identity");

        constexpr uintptr_t motionBlurTestBase = 0x10000000u;
        Check(ReachMotionBlurSlotsMatchPinnedImage(
                  motionBlurTestBase, kReachRetailImageSize,
                  motionBlurTestBase + kReachMotionBlurScaleValueRva,
                  motionBlurTestBase + kReachMotionBlurMaxValueRva) &&
              !ReachMotionBlurSlotsMatchPinnedImage(
                  motionBlurTestBase, kReachRetailImageSize,
                  motionBlurTestBase + kReachMotionBlurMaxValueRva,
                  motionBlurTestBase + kReachMotionBlurScaleValueRva) &&
              !ReachMotionBlurSlotsMatchPinnedImage(
                  motionBlurTestBase, kReachRetailImageSize - 1,
                  motionBlurTestBase + kReachMotionBlurScaleValueRva,
                  motionBlurTestBase + kReachMotionBlurMaxValueRva) &&
              kReachMotionBlurMaxOverScaleDivideRva == 0x00287561 &&
              kReachMotionBlurScaledMaxDivideRva == 0x002875AD &&
              ReachMotionBlurSuppressionValuesValid(0.35f, 0.08f) &&
              ReachMotionBlurSuppressionValuesValid(0.35f, 0.0f) &&
              !ReachMotionBlurSuppressionValuesValid(0.0f, 0.0f) &&
              !ReachMotionBlurSuppressionValuesValid(1.0e-6f, 0.08f) &&
              !ReachMotionBlurSuppressionValuesValid(0.35f, -0.01f) &&
              !ReachMotionBlurSuppressionValuesValid(
                  0.35f, std::numeric_limits<float>::infinity()) &&
              !ReachMotionBlurSuppressionValuesValid(
                  std::numeric_limits<float>::quiet_NaN(), 0.08f),
            "Reach blur suppression pins the exact slots/divisions and keeps a positive scale with zero max");

        bool patchyFogPolicyExact = true;
        for (unsigned original = 0; original <= 0xFFu; ++original)
        {
            const uint8_t originalByte = static_cast<uint8_t>(original);
            const uint8_t suppressed =
                ReachPatchyFogSuppressedFlags(originalByte);
            patchyFogPolicyExact = patchyFogPolicyExact &&
                (suppressed & kReachPatchyFogSkipMask) != 0 &&
                (suppressed & static_cast<uint8_t>(~kReachPatchyFogSkipMask)) ==
                    (originalByte &
                     static_cast<uint8_t>(~kReachPatchyFogSkipMask));
            for (unsigned current = 0; current <= 0xFFu; ++current)
            {
                const uint8_t currentByte = static_cast<uint8_t>(current);
                const uint8_t restored = ReachPatchyFogRestoredFlags(
                    currentByte, originalByte);
                patchyFogPolicyExact = patchyFogPolicyExact &&
                    (restored & kReachPatchyFogSkipMask) ==
                        (originalByte & kReachPatchyFogSkipMask) &&
                    (restored &
                     static_cast<uint8_t>(~kReachPatchyFogSkipMask)) ==
                        (currentByte &
                         static_cast<uint8_t>(~kReachPatchyFogSkipMask));
            }
        }
        Check(patchyFogPolicyExact &&
              kReachPatchyFogGateTestRva == 0x0026CC59 &&
              kReachPatchyFogSkipJumpRva == 0x0026CC60 &&
              kReachPatchyFogCallRva == 0x0026CC65 &&
              kReachPatchyFogTargetRva == 0x0026EFEC &&
              kReachPatchyFogFlagsRva == 0x00CA0240 &&
              kReachPatchyFogSkipMask == 0x08,
            "Reach VR patchy-fog policy sets and restores only the exact proven skip bit");

        // Atmospheric fog is the OPPOSITE polarity to patchy fog above: the bit
        // means "enabled", so suppression CLEARS it and restore SETS it. Both
        // must leave every other bit in the shared render flags byte alone.
        bool atmosphereFogPolicyExact = true;
        for (unsigned original = 0; original <= 0xFFu; ++original)
        {
            const uint8_t originalByte = static_cast<uint8_t>(original);
            const uint8_t suppressed =
                ReachAtmosphereFogSuppressedFlags(originalByte);
            const uint8_t restored =
                ReachAtmosphereFogRestoredFlags(originalByte);
            constexpr uint8_t kOther =
                static_cast<uint8_t>(~kReachAtmosphereFogEnableMask);
            atmosphereFogPolicyExact = atmosphereFogPolicyExact &&
                (suppressed & kReachAtmosphereFogEnableMask) == 0 &&
                (suppressed & kOther) == (originalByte & kOther) &&
                (restored & kReachAtmosphereFogEnableMask) ==
                    kReachAtmosphereFogEnableMask &&
                (restored & kOther) == (originalByte & kOther) &&
                // Suppress-then-restore returns the byte whenever the bit was
                // set to begin with, which is the only case that suppresses.
                (!(originalByte & kReachAtmosphereFogEnableMask) ||
                 ReachAtmosphereFogRestoredFlags(suppressed) == originalByte);
        }
        Check(atmosphereFogPolicyExact &&
              kReachAtmosphereFogHelperRva == 0x0026D5B4 &&
              kReachAtmosphereFogHelperCallRva == 0x0026CC54 &&
              kReachAtmosphereFogTlsIndexLoadRva == 0x0026D5DF &&
              kReachAtmosphereFogTlsIndexRva == 0x00C17B18 &&
              kReachAtmosphereFogSlotLoadRva == 0x0026D5E5 &&
              kReachAtmosphereFogGateTestRva == 0x0026D5F3 &&
              kReachAtmosphereFogSkipJumpRva == 0x0026D5F6 &&
              kReachAtmosphereFogFlagsSlotOffset == 0x168 &&
              kReachAtmosphereFogEnableMask == 0x04 &&
              kReachRenderTlsIndexLimit == 256 &&
              // The two fog systems are separate structures. Sharing a mask
              // between them would silently make one control the other.
              kReachAtmosphereFogEnableMask != kReachPatchyFogSkipMask,
            "Reach atmospheric-fog policy clears and restores only the exact proven enable bit");

        Check(kReachDebugVarTypeBoolean == 5 &&
              kReachRenderRainValueRva == 0x00B4444C &&
              kReachRenderRainGateRva == 0x0026CC92 &&
              kReachRainParticleRenderRva == 0x00288D60 &&
              ReachRenderRainSlotMatchesPinnedImage(
                  0x180000000u, kReachRetailImageSize,
                  0x180000000u + kReachRenderRainValueRva) &&
              // A slot one byte away is the NEIGHBOURING boolean, not rain.
              !ReachRenderRainSlotMatchesPinnedImage(
                  0x180000000u, kReachRetailImageSize,
                  0x180000000u + kReachRenderRainValueRva + 1) &&
              !ReachRenderRainSlotMatchesPinnedImage(
                  0x180000000u, kReachRetailImageSize - 1,
                  0x180000000u + kReachRenderRainValueRva),
            "Reach render_rain is a boolean debug var cross-checked against the pinned image");

        Check(kReachSkyParallaxSignatureRva == 0x0024B5C4 &&
              kReachSkyParallaxQuantizeRva == 0x0024B5DB &&
              kReachModelFlagsOffset == 0x015C &&
              kReachModelSkyParallaxOffset == 0x01B4 &&
              kReachObjectSkyParallaxByteOffset == 0x000B &&
              kReachModelAttachToCameraMask == 0x40 &&
              kReachSkyParallaxQuantizeOriginal ==
                  std::array<uint8_t, 4>{0xF3, 0x0F, 0x2C, 0xC0} &&
              kReachSkyParallaxQuantizeNeutral ==
                  std::array<uint8_t, 4>{0x31, 0xC0, 0x90, 0x90},
            "Reach camera-attached sky neutralization pins the generic model flag, parallax field, object property, and exact reversible quantizer");

        Check(kReachFpWeaponIkDecisionPreludeRva == 0x002B506E &&
              kReachFpWeaponIkDisableCompareRva == 0x002B507F &&
              kReachFpWeaponIkDisableBranchRva == 0x002B5085 &&
              kReachFpWeaponIkDisabledEpilogueRva == 0x002B52D1 &&
              kReachFpWeaponIkDisableNameRva == 0x009F2AD8 &&
              kReachFpWeaponIkDisableEntryRva == 0x00B3AEB8 &&
              kReachFpWeaponIkDisableValueRva == 0x04E38B61 &&
              kReachDebugBooleanType == 5,
            "Reach native weapon-IK bypass pins the exact named control and stock no-IK edge");

        std::array<uint8_t, kReachMainRenderViewAob.size()> exactMask{};
        exactMask.fill(0xFF);
        std::array<uint8_t, kReachMainRenderViewAob.size() * 2 + 1>
            repeatedMain{};
        std::memcpy(repeatedMain.data(), kReachMainRenderViewAob.data(),
                    kReachMainRenderViewAob.size());
        std::memcpy(repeatedMain.data() + kReachMainRenderViewAob.size() + 1,
                    kReachMainRenderViewAob.data(),
                    kReachMainRenderViewAob.size());
        Check(CountReachMaskedPattern(
                  repeatedMain.data(), repeatedMain.size(),
                  kReachMainRenderViewAob.data(), exactMask.data(),
                  kReachMainRenderViewAob.size()) == 2,
            "Reach render scanner counts every exact executable candidate");

        auto playerEntry = kReachPlayerViewRenderAob;
        playerEntry[49] = 0x12;
        playerEntry[50] = 0x34;
        playerEntry[51] = 0x56;
        playerEntry[52] = 0x78;
        Check(CountReachMaskedPattern(
                  playerEntry.data(), playerEntry.size(),
                  kReachPlayerViewRenderAob.data(),
                  kReachPlayerViewRenderAobMask.data(),
                  kReachPlayerViewRenderAob.size()) == 1,
            "Reach inner signature wildcards only the four cookie-displacement bytes");
        playerEntry[48] ^= 1;
        Check(CountReachMaskedPattern(
                  playerEntry.data(), playerEntry.size(),
                  kReachPlayerViewRenderAob.data(),
                  kReachPlayerViewRenderAobMask.data(),
                  kReachPlayerViewRenderAob.size()) == 0,
            "Reach inner signature rejects a changed non-cookie byte");

        auto outerCameraCallbackEntry = kReachCameraStackCallbackAob;
        for (size_t index = 9; index <= 12; ++index)
            outerCameraCallbackEntry[index] = static_cast<uint8_t>(index * 7);
        for (size_t index = 16; index <= 19; ++index)
            outerCameraCallbackEntry[index] = static_cast<uint8_t>(index * 11);
        Check(CountReachMaskedPattern(
                  outerCameraCallbackEntry.data(),
                  outerCameraCallbackEntry.size(),
                  kReachCameraStackCallbackAob.data(),
                  kReachCameraStackCallbackAobMask.data(),
                  kReachCameraStackCallbackAob.size()) == 1,
            "Reach outer-camera callback signature masks only its two RIP-relative globals");
        outerCameraCallbackEntry[20] ^= 1;
        Check(CountReachMaskedPattern(
                  outerCameraCallbackEntry.data(),
                  outerCameraCallbackEntry.size(),
                  kReachCameraStackCallbackAob.data(),
                  kReachCameraStackCallbackAobMask.data(),
                  kReachCameraStackCallbackAob.size()) == 0,
            "Reach outer-camera callback signature rejects a changed fixed byte");

        auto frustumEntry = kReachFrustumHelperAob;
        std::array<uint8_t, kReachFrustumHelperAob.size()> frustumMask{};
        frustumMask.fill(0xFF);
        Check(CountReachMaskedPattern(
                  frustumEntry.data(), frustumEntry.size(),
                  kReachFrustumHelperAob.data(), frustumMask.data(),
                  kReachFrustumHelperAob.size()) == 1,
            "Reach production pattern pins the canonical 25-byte frustum entry");
        frustumEntry.back() ^= 1;
        Check(CountReachMaskedPattern(
                  frustumEntry.data(), frustumEntry.size(),
                  kReachFrustumHelperAob.data(), frustumMask.data(),
                  kReachFrustumHelperAob.size()) == 0,
            "Reach production pattern checks the byte omitted by the historical observer");
        Check(CountReachMaskedPattern(
                  nullptr, 1, kReachMainRenderViewAob.data(), exactMask.data(),
                  kReachMainRenderViewAob.size()) == 0 &&
              CountReachMaskedPattern(
                  repeatedMain.data(), repeatedMain.size(), nullptr,
                  exactMask.data(), kReachMainRenderViewAob.size()) == 0,
            "Reach masked scanning fails closed on invalid buffers");

        auto fpCameraEntry = kReachFpCameraRebuildAob;
        fpCameraEntry[22] = 0x12;
        fpCameraEntry[23] = 0x34;
        fpCameraEntry[24] = 0x56;
        fpCameraEntry[25] = 0x78;
        Check(CountReachMaskedPattern(
                  fpCameraEntry.data(), fpCameraEntry.size(),
                  kReachFpCameraRebuildAob.data(),
                  kReachFpCameraRebuildAobMask.data(),
                  kReachFpCameraRebuildAob.size()) == 1,
            "Reach FP camera signature masks only the LEA displacement");
        fpCameraEntry.back() ^= 1;
        Check(CountReachMaskedPattern(
                  fpCameraEntry.data(), fpCameraEntry.size(),
                  kReachFpCameraRebuildAob.data(),
                  kReachFpCameraRebuildAobMask.data(),
                  kReachFpCameraRebuildAob.size()) == 0,
            "Reach FP camera signature rejects a changed fixed byte");
        auto fpUploadEntry = kReachFpCameraUploadAob;
        std::array<uint8_t, kReachFpCameraUploadAob.size()> fpUploadMask{};
        fpUploadMask.fill(0xFF);
        Check(CountReachMaskedPattern(
                  fpUploadEntry.data(), fpUploadEntry.size(),
                  kReachFpCameraUploadAob.data(), fpUploadMask.data(),
                  kReachFpCameraUploadAob.size()) == 1,
            "Reach FP camera uploader signature is exact");
        fpUploadEntry[19] ^= 1;
        Check(CountReachMaskedPattern(
                  fpUploadEntry.data(), fpUploadEntry.size(),
                  kReachFpCameraUploadAob.data(), fpUploadMask.data(),
                  kReachFpCameraUploadAob.size()) == 0,
            "Reach FP camera uploader rejects a changed fixed byte");

        struct ReachFpNestedSelectorInput
        {
            uintptr_t moduleBase;
            size_t moduleSize;
            uintptr_t stackTop;
            uintptr_t workspaceCallback;
            uintptr_t fpView;
        };
        const uintptr_t fpModuleBase = 0x10000000u;
        const ReachFpNestedSelectorInput validFpNested{
            fpModuleBase,
            kReachRetailImageSize,
            fpModuleBase + kReachFpCameraWorkspaceRva,
            fpModuleBase + kReachFpCameraWorkspaceCallbackRva,
            fpModuleBase + kReachFpCameraViewRva};
        const auto selectFpNested =
            [](const ReachFpNestedSelectorInput& input)
        {
            return SelectReachFpCameraNestedWorkspace(
                input.moduleBase, input.moduleSize, input.stackTop,
                input.workspaceCallback, input.fpView);
        };
        const auto fpNestedRejects =
            [&validFpNested, &selectFpNested](auto mutate)
        {
            auto candidate = validFpNested;
            mutate(candidate);
            return selectFpNested(candidate) == 0;
        };
        Check(selectFpNested(validFpNested) == validFpNested.stackTop,
            "Reach FP camera selector accepts the exact nested workspace, callback, and view");
        const uintptr_t selectedFpNested = selectFpNested(validFpNested);
        Check(selectedFpNested + kReachSecondaryDerivedOffset ==
                  fpModuleBase + kReachFpCameraWorkspaceRva + 0x1E4 &&
              kReachSecondaryDerivedOffset + kReachDerivedBlockSize <=
                  kReachFpCameraWorkspaceCallbackOffset,
            "Reach FP camera selector targets nested+0x1E4 without crossing the callback");
        Check(kReachFpCameraWorkspaceCallbackOffset + sizeof(uintptr_t) ==
                  kReachRenderScopeSnapshotSize &&
              kReachFpCameraWorkspaceRva <=
                  kReachRetailImageSize - kReachRenderScopeSnapshotSize,
            "Reach FP camera callback and full snapshot stay inside the fixed nested workspace range");
        Check(
            fpNestedRejects([](auto& v) {
                v.moduleBase = 0;
                v.stackTop = kReachFpCameraWorkspaceRva;
                v.workspaceCallback = kReachFpCameraWorkspaceCallbackRva;
                v.fpView = kReachFpCameraViewRva;
            }) &&
            fpNestedRejects([](auto& v) { v.moduleSize = 0; }) &&
            fpNestedRejects([](auto& v) {
                v.moduleSize = kReachRetailImageSize - 1;
            }) &&
            fpNestedRejects([](auto& v) {
                v.moduleSize = kReachRetailImageSize + 1;
            }) &&
            fpNestedRejects([](auto& v) { --v.stackTop; }) &&
            fpNestedRejects([](auto& v) { ++v.stackTop; }) &&
            fpNestedRejects([](auto& v) { v.workspaceCallback = 0; }) &&
            fpNestedRejects([](auto& v) { --v.workspaceCallback; }) &&
            fpNestedRejects([](auto& v) { ++v.workspaceCallback; }) &&
            fpNestedRejects([](auto& v) { v.fpView = 0; }) &&
            fpNestedRejects([](auto& v) { --v.fpView; }) &&
            fpNestedRejects([](auto& v) { ++v.fpView; }) &&
            fpNestedRejects([](auto& v) {
                v.stackTop =
                    v.moduleBase + kReachDefaultWorkspaceRva;
            }) &&
            fpNestedRejects([](auto& v) {
                v.moduleBase =
                    std::numeric_limits<uintptr_t>::max() -
                    kReachFpCameraWorkspaceRva + 2;
                v.stackTop = v.moduleBase + kReachFpCameraWorkspaceRva;
                v.workspaceCallback =
                    v.moduleBase + kReachFpCameraWorkspaceCallbackRva;
                v.fpView = v.moduleBase + kReachFpCameraViewRva;
            }),
            "Reach FP camera selector fails closed for every invalid identity component");
        const uintptr_t fpBoundaryBase =
            std::numeric_limits<uintptr_t>::max() -
            kReachFpCameraWorkspaceRva;
        const ReachFpNestedSelectorInput boundaryFpNested{
            fpBoundaryBase,
            kReachRetailImageSize,
            fpBoundaryBase + kReachFpCameraWorkspaceRva,
            fpBoundaryBase + kReachFpCameraWorkspaceCallbackRva,
            fpBoundaryBase + kReachFpCameraViewRva};
        Check(selectFpNested(boundaryFpNested) ==
                  std::numeric_limits<uintptr_t>::max(),
            "Reach FP camera selector accepts the last non-overflowing nested workspace");

        ReachRenderCandidateProof proof =
            CompleteReachRenderCandidateProof();
        Check(ReachRenderCandidateProofComplete(proof),
            "Reach render proof requires every exact static preflight gate");
        const auto proofRejects = [&proof](auto mutate)
        {
            auto candidate = proof;
            mutate(candidate);
            return !ReachRenderCandidateProofComplete(candidate);
        };
        Check(proofRejects([](auto& p) { p.retailIdentity = false; }) &&
              proofRejects([](auto& p) { p.mainRenderViewMatchCount = 0; }) &&
              proofRejects([](auto& p) { p.mainRenderViewMatchCount = 2; }) &&
              proofRejects([](auto& p) { p.mainRenderViewAtExpectedRva = false; }) &&
              proofRejects([](auto& p) { p.mainRenderViewBodyHash = false; }) &&
              proofRejects([](auto& p) { p.playerViewRenderMatchCount = 0; }) &&
              proofRejects([](auto& p) { p.playerViewRenderMatchCount = 2; }) &&
              proofRejects([](auto& p) { p.playerViewRenderAtExpectedRva = false; }) &&
              proofRejects([](auto& p) { p.playerViewRenderBodyHash = false; }) &&
              proofRejects([](auto& p) { p.cameraStackCallbackMatchCount = 0; }) &&
              proofRejects([](auto& p) { p.cameraStackCallbackMatchCount = 2; }) &&
              proofRejects([](auto& p) { p.cameraStackCallbackAtExpectedRva = false; }) &&
              proofRejects([](auto& p) { p.cameraStackCallbackBodyHash = false; }) &&
              proofRejects([](auto& p) { p.frustumHelperMatchCount = 0; }) &&
              proofRejects([](auto& p) { p.frustumHelperMatchCount = 2; }) &&
               proofRejects([](auto& p) { p.frustumHelperAtExpectedRva = false; }) &&
               proofRejects([](auto& p) { p.frustumHelperExecutableRange = false; }) &&
               proofRejects([](auto& p) { p.fpCameraRebuildMatchCount = 0; }) &&
               proofRejects([](auto& p) { p.fpCameraRebuildMatchCount = 2; }) &&
               proofRejects([](auto& p) { p.fpCameraRebuildAtExpectedRva = false; }) &&
               proofRejects([](auto& p) { p.fpCameraRebuildBodyHash = false; }) &&
               proofRejects([](auto& p) { p.fpCameraUploadMatchCount = 0; }) &&
               proofRejects([](auto& p) { p.fpCameraUploadMatchCount = 2; }) &&
               proofRejects([](auto& p) { p.fpCameraUploadAtExpectedRva = false; }) &&
               proofRejects([](auto& p) { p.fpCameraUploadBodyHash = false; }) &&
               proofRejects([](auto& p) { p.fpCameraWrapperBodyHashes = false; }) &&
               proofRejects([](auto& p) { p.exactFpCameraFlowEdges = false; }) &&
               proofRejects([](auto& p) { p.exactOuterCallerEdges = false; }) &&
              proofRejects([](auto& p) { p.exactInnerCallerEdge = false; }) &&
              proofRejects([](auto& p) { p.fixedDataRanges = false; }),
            "Reach render proof fails closed when any identity gate is absent");

        const ReachModuleEpoch proofEpoch{0x10000000u, 1};
        ReachPreflightPublication preflightPublication;
        Check(preflightPublication.Publish(proofEpoch, proof),
            "Reach preflight publication accepts one complete exact proof");
        const ReachPreflightToken preflight =
            preflightPublication.Get(proofEpoch);
        auto incompleteProof = proof;
        incompleteProof.frustumHelperMatchCount = 0;
        ReachPreflightPublication incompletePublication;
        Check(preflight.Complete() &&
              IsPreflightCurrent(preflight) &&
              ReachSameModuleEpoch(preflight.Epoch(), proofEpoch) &&
              !incompletePublication.Publish(
                  proofEpoch, incompleteProof) &&
              !incompletePublication.Get(proofEpoch).Complete(),
            "Reach preflight authorization is publication-bound and requires the full proof");
        ReachPreflightPublication replacementPublication;
        Check(replacementPublication.Publish(proofEpoch, proof),
            "Reach preflight replacement test begins with one current proof");
        const ReachPreflightToken replacedPreflight =
            replacementPublication.Get(proofEpoch);
        Check(replacedPreflight.Complete() &&
              replacementPublication.IsCurrent(replacedPreflight) &&
              !replacementPublication.Publish(proofEpoch, incompleteProof) &&
              !replacementPublication.HasCurrent() &&
              !replacementPublication.IsCurrent(replacedPreflight),
            "A failed preflight replacement revokes prior readiness and copied tokens");

        ReachDisplaySurfaceProof displayProof =
            CompleteReachDisplaySurfaceProof(proofEpoch, preflight);
        Check(ReachDisplaySurfaceProofComplete(displayProof),
            "Reach display proof accepts exact buffer0 shape and record0 structural continuity");
        const auto displayRejects = [&displayProof](auto mutate)
        {
            auto candidate = displayProof;
            mutate(candidate);
            return !ReachDisplaySurfaceProofComplete(candidate);
        };
        Check(displayRejects([](auto& p) { p.preflight = {}; }) &&
              displayRejects([](auto& p) {
                  ++p.continuity.epoch.generation;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.resourceRevision = 0;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.lifecycleSerial = 0;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.lifecycleSerial =
                      std::numeric_limits<uint64_t>::max();
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.swapchainIdentity = 0;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.buffer0Identity = 0;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.surfaceArrayIdentity = 0;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.record0RtvIdentity = 0;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.record0SrvIdentity = 0;
              }) &&
              displayRejects([](auto& p) {
                  ++p.continuity.selectedRtvIdentity;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.deviceIdentity = 0;
              }) &&
              displayRejects([](auto& p) {
                  ++p.continuity.immediateContextIdentity;
              }) &&
              displayRejects([](auto& p) {
                  ++p.continuity.eyeResourceIdentities[0];
              }) &&
              displayRejects([](auto& p) {
                  ++p.continuity.eyeResourceIdentities[1];
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.specializationCount = 3;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.selectedSpecialization = 1;
              }) &&
              displayRejects([](auto& p) {
                  p.continuity.teardownRequested = true;
              }) &&
              displayRejects([](auto& p) {
                  p.eyeResourceIdentities[1] =
                      p.eyeResourceIdentities[0];
              }) &&
              displayRejects([](auto& p) {
                  p.eyeResourceIdentities[0] =
                      p.continuity.buffer0Identity;
              }) &&
              displayRejects([](auto& p) {
                  p.immediateContextIdentity = 0;
              }) &&
              displayRejects([](auto& p) { p.source.width = 0; }) &&
              displayRejects([](auto& p) {
                  p.source.format = 0;
              }) &&
              displayRejects([](auto& p) { ++p.eyes[0].width; }) &&
              displayRejects([](auto& p) { ++p.eyes[1].sampleCount; }) &&
              displayRejects([](auto& p) { p.readyEyeMask = 1; }) &&
              displayRejects([](auto& p) {
                  p.engineSwapchainMatchesPresent = false;
              }) &&
              displayRejects([](auto& p) {
                  p.selectedRtvMatchesRecord0 = false;
              }) &&
              displayRejects([](auto& p) {
                  p.swapchainContract = false;
              }) &&
              displayRejects([](auto& p) { p.sameDevice = false; }) &&
              displayRejects([](auto& p) {
                  p.immediateContext = false;
              }),
            "Reach display proof rejects stale identity, aliasing, partial eyes, and every copy-shape/context mismatch");

        ReachDirectCopyGate displayGate;
        const ReachPreparedFrameToken displayFrame =
            ReachPreparedFrameToken::Create(proofEpoch, 11, true);
        Check(displayGate.AdvanceEpoch(proofEpoch) &&
              displayGate.Publish(displayProof),
            "Reach direct-copy gate admits one monotonic cold resource proof");
        const ReachDirectCopyToken firstCopy = displayGate.Prepare(
            displayFrame, displayProof.continuity);
        Check(firstCopy.Ready() && displayGate.IsCurrent(
                  firstCopy, displayProof.continuity),
            "A current display proof mints one live direct-copy token");
        const auto liveContinuityRejects =
            [&displayGate, &firstCopy, &displayFrame, &displayProof](
                auto mutate)
        {
            auto live = displayProof.continuity;
            mutate(live);
            return !displayGate.IsCurrent(firstCopy, live) &&
                !displayGate.Prepare(displayFrame, live).Ready();
        };
        Check(liveContinuityRejects([](auto& c) {
                  c.epoch.moduleBase += 0x10000;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.epoch.generation;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.resourceRevision;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.lifecycleSerial;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.swapchainIdentity;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.buffer0Identity;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.surfaceArrayIdentity;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.record0RtvIdentity;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.record0SrvIdentity;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.selectedRtvIdentity;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.deviceIdentity;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.immediateContextIdentity;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.eyeResourceIdentities[0];
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.eyeResourceIdentities[1];
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.specializationCount;
              }) &&
              liveContinuityRejects([](auto& c) {
                  ++c.selectedSpecialization;
              }) &&
              liveContinuityRejects([](auto& c) {
                  c.teardownRequested = true;
              }),
            "Every live Reach display-continuity field invalidates copied and newly prepared tokens when it drifts");
        ReachDisplaySurfaceProof invalidReplacement =
            CompleteReachDisplaySurfaceProof(
                proofEpoch, preflight, 2);
        invalidReplacement.sameDevice = false;
        Check(!displayGate.Publish(invalidReplacement) &&
              !displayGate.Ready() &&
              !displayGate.IsCurrent(
                  firstCopy, displayProof.continuity),
            "A failed display replacement revokes prior readiness and copied tokens");
        auto changedContinuity = displayProof.continuity;
        ++changedContinuity.record0RtvIdentity;
        Check(!displayGate.IsCurrent(firstCopy, changedContinuity) &&
              displayGate.Invalidate(proofEpoch) &&
              !displayGate.IsCurrent(
                  firstCopy, displayProof.continuity) &&
              !displayGate.Publish(displayProof),
            "Resize or live view drift invalidates copied tokens and forbids resource-revision replay");
        ReachDisplaySurfaceProof nextDisplayProof =
            CompleteReachDisplaySurfaceProof(
                proofEpoch, preflight, 2);
        const ReachPreparedFrameToken nextDisplayFrame =
            ReachPreparedFrameToken::Create(proofEpoch, 12, true);
        Check(displayGate.Publish(nextDisplayProof),
            "A higher same-epoch resource revision may re-arm after ResizeBuffers or resident title ambiguity");
        const ReachDirectCopyToken nextCopy = displayGate.Prepare(
            nextDisplayFrame, nextDisplayProof.continuity);
        Check(nextCopy.Ready() &&
              nextCopy.PreparedFrameSerial() == 12 &&
              nextCopy.ResourceRevision() == 2 &&
              !displayGate.IsCurrent(
                  firstCopy, nextDisplayProof.continuity),
            "A new resource publication invalidates its predecessor");

        ReachPreflightPublication differentPublication;
        Check(differentPublication.Publish(proofEpoch, proof) &&
              !differentPublication.IsCurrent(preflight),
            "A Reach preflight token cannot cross publication owners");
        const uint64_t firstPublicationNonce =
            preflight.PublicationNonce();
        Check(preflightPublication.Publish(proofEpoch, proof),
            "The same module epoch may receive a newer cold-proof publication");
        const ReachPreflightToken reissuedPreflight =
            preflightPublication.Get(proofEpoch);
        Check(reissuedPreflight.Complete() &&
              IsPreflightCurrent(reissuedPreflight) &&
              reissuedPreflight.PublicationNonce() >
                  firstPublicationNonce &&
              preflight.Complete() &&
              !IsPreflightCurrent(preflight) &&
              !ReachDisplaySurfaceProofComplete(nextDisplayProof) &&
              !displayGate.Ready() &&
              !displayGate.IsCurrent(
                  nextCopy, nextDisplayProof.continuity) &&
              !displayGate.Publish(nextDisplayProof),
            "A newer preflight nonce rejects copied-token replay and stale display publication");
        ReachDisplaySurfaceProof reissuedDisplayProof =
            CompleteReachDisplaySurfaceProof(
                proofEpoch, reissuedPreflight, 3);
        const ReachPreparedFrameToken reissuedDisplayFrame =
            ReachPreparedFrameToken::Create(proofEpoch, 13, true);
        Check(displayGate.Publish(reissuedDisplayProof),
            "A current preflight nonce can publish a newer display revision");
        const ReachDirectCopyToken reissuedCopy = displayGate.Prepare(
            reissuedDisplayFrame, reissuedDisplayProof.continuity);
        Check(reissuedCopy.Ready() &&
              displayGate.IsCurrent(
                  reissuedCopy, reissuedDisplayProof.continuity) &&
              displayGate.Teardown(proofEpoch) &&
              !displayGate.IsCurrent(
                  reissuedCopy, reissuedDisplayProof.continuity) &&
              !displayGate.AdvanceEpoch(proofEpoch) &&
              displayGate.AdvanceEpoch({proofEpoch.moduleBase, 2}) &&
              displayGate.Teardown({proofEpoch.moduleBase, 2}),
            "Reach direct-copy teardown rejects old resource and module-generation replay");

        ReachRenderFreshnessGate freshness;
        const ReachModuleEpoch freshnessEpoch{0x10000000u, 7};
        Check(freshness.AdvanceEpoch(freshnessEpoch) &&
              !freshness.Observe(
                  100, freshnessEpoch, 1, true, true).Stable() &&
              !freshness.Observe(
                  499, freshnessEpoch, 2, true, true).Stable() &&
              !freshness.Observe(
                  898, freshnessEpoch, 3, true, true).Stable() &&
              !freshness.Observe(
                  1100, freshnessEpoch, 4, true, true).Stable() &&
              freshness.CurrentSpanMs() == 1000,
            "Reach production freshness arms only on a new transaction after one second");
        const ReachFreshCameraToken stableFreshness =
            freshness.Observe(1101, freshnessEpoch, 5, true, true);
        Check(stableFreshness.Stable() &&
              freshness.IsCurrent(stableFreshness) &&
              stableFreshness.PreparedFrameSerial() == 5,
            "Reach stable freshness is bound to the exact observed frame serial");
        const ReachModuleEpoch staleFreshnessEpoch{0x10000000u, 6};
        const ReachFreshCameraToken nextFreshness =
            freshness.Observe(1200, freshnessEpoch, 6, true, true);
        Check(!freshness.IsCurrent(stableFreshness) &&
              nextFreshness.Stable() && freshness.IsCurrent(nextFreshness) &&
              !freshness.AdvanceEpoch(staleFreshnessEpoch) &&
              !freshness.Consume(
                  nextFreshness,
                  ReachPreparedFrameToken::Create(
                      freshnessEpoch, 6, true),
                  1700) &&
              !freshness.IsCurrent(nextFreshness) &&
              !freshness.Observe(
                  1700, freshnessEpoch, 7, true, true).Stable() &&
              freshness.TransactionCount() == 1 &&
              !freshness.Teardown(staleFreshnessEpoch),
            "Reach freshness invalidates saved tokens on a new observation or continuity gap");
        Check(freshness.Teardown(freshnessEpoch) &&
              !freshness.AdvanceEpoch(freshnessEpoch) &&
              freshness.AdvanceEpoch({0x10000000u, 8}) &&
              !freshness.Observe(
                  1800, {0x10000000u, 8}, 1, false, true).Stable() &&
              freshness.TransactionCount() == 0 &&
              freshness.Teardown({0x10000000u, 8}) &&
              !freshness.AdvanceEpoch({0x10000000u, 8}) &&
              freshness.AdvanceEpoch({0x10000000u, 9}),
            "Reach freshness rejects stale epochs, gaps, non-normal owners, and generation replay");
    }

    {
        constexpr uintptr_t moduleBase = 0x10000000;
        ReachOuterRenderInput outer{};
        outer.moduleBase = moduleBase;
        outer.moduleSize = kReachRetailImageSize;
        outer.returnAddress = moduleBase + kReachNormalOuterReturnRva;
        outer.workspace = moduleBase + kReachDefaultWorkspaceRva;
        outer.playerView = moduleBase + kReachPlayerViewArrayRva;
        outer.playerWindowIndex = 0;
        // Retail initializes the camera-stack depth to -1. The normal
        // top-level push increments that sentinel to slot/depth zero.
        outer.cameraStackDepthBefore = -1;
        outer.nowMs = 1101;
        const ReachModuleEpoch epoch{moduleBase, 9};
        const ReachRenderCandidateProof outerProof =
            CompleteReachRenderCandidateProof();
        ReachPreflightPublication outerPreflightPublication;
        Check(outerPreflightPublication.Publish(epoch, outerProof),
            "Reach outer preflight publishes for the selected module epoch");
        outer.preflight = outerPreflightPublication.Get(epoch);
        ReachRenderFreshnessGate ownerFreshness;
        Check(ownerFreshness.AdvanceEpoch(epoch) &&
              !ownerFreshness.Observe(
                  100, epoch, 38, true, true).Stable() &&
              !ownerFreshness.Observe(
                  400, epoch, 39, true, true).Stable() &&
              !ownerFreshness.Observe(
                  700, epoch, 40, true, true).Stable() &&
              !ownerFreshness.Observe(
                  1000, epoch, 41, true, true).Stable(),
            "Reach owner freshness starts from exact prepared-frame observations");
        outer.freshCamera = ownerFreshness.Observe(
            1101, epoch, 42, true, true);
        outer.preparedFrame = ReachPreparedFrameToken::Create(
            epoch, 42, true);
        const ReachModuleEpoch wrongPreflightEpoch{moduleBase, 10};
        ReachPreflightPublication wrongPreflightPublication;
        Check(wrongPreflightPublication.Publish(
                  wrongPreflightEpoch, outerProof),
            "Reach mismatch test publishes a distinct module generation");
        const ReachPreflightToken wrongPreflight =
            wrongPreflightPublication.Get(wrongPreflightEpoch);

        Check(ClassifyReachOuterRenderCaller(
                  moduleBase, kReachRetailImageSize,
                  moduleBase + kReachNormalOuterReturnRva) ==
                  ReachOuterRenderCaller::NormalPlayer &&
              ClassifyReachOuterRenderCaller(
                  moduleBase, kReachRetailImageSize,
                  moduleBase + kReachScreenshotOuterReturnRva) ==
                  ReachOuterRenderCaller::ScreenshotTileBloom &&
              ClassifyReachOuterRenderCaller(
                  moduleBase, kReachRetailImageSize, moduleBase + 1) ==
                  ReachOuterRenderCaller::Unknown &&
              ClassifyReachOuterRenderCaller(
                  moduleBase, kReachRetailImageSize - 1,
                  moduleBase + kReachNormalOuterReturnRva) ==
                  ReachOuterRenderCaller::Unknown,
            "Reach outer routing distinguishes normal, screenshot, and unknown callers exactly");

        const auto outerRejects = [&outer](auto mutate)
        {
            auto candidate = outer;
            mutate(candidate);
            return !ReachNormalOuterInputMatches(candidate);
        };
        Check(ReachNormalOuterInputMatches(outer) &&
              outerRejects([](auto& v) { v.preflight = {}; }) &&
              outerRejects([](auto& v) { v.freshCamera = {}; }) &&
              outerRejects([](auto& v) { v.preparedFrame = {}; }) &&
              outerRejects([&wrongPreflight](auto& v) {
                  v.preflight = wrongPreflight;
              }) &&
              outerRejects([](auto& v) {
                  v.preparedFrame = ReachPreparedFrameToken::Create(
                      v.preflight.Epoch(), 43, true);
              }) &&
              outerRejects([](auto& v) { v.teardownRequested = true; }) &&
              outerRejects([](auto& v) { v.nowMs = 0; }) &&
              outerRejects([](auto& v) { ++v.moduleBase; }) &&
              outerRejects([](auto& v) { ++v.playerWindowIndex; }) &&
              outerRejects([](auto& v) { v.cameraStackDepthBefore = -2; }) &&
              outerRejects([](auto& v) { v.cameraStackDepthBefore = 3; }) &&
              outerRejects([](auto& v) { ++v.workspace; }) &&
              outerRejects([](auto& v) { ++v.playerView; }) &&
              outerRejects([](auto& v) {
                  v.returnAddress = v.moduleBase +
                      kReachScreenshotOuterReturnRva;
              }),
            "Only the exact fresh slot-zero normal owner can mint a Reach token");

        ReachRenderOwnerGate owner;
        Check(!owner.TryBegin(outer, ownerFreshness) &&
              ownerFreshness.IsCurrent(outer.freshCamera) &&
              owner.AdvanceEpoch(epoch) &&
              !owner.AdvanceEpoch(epoch) &&
              !owner.AdvanceEpoch({moduleBase, 8}) &&
              owner.TryBegin(outer, ownerFreshness) &&
              !ownerFreshness.IsCurrent(outer.freshCamera) &&
              owner.Token().Active() &&
              owner.Token().PreparedFrameSerial() == 42 &&
              !owner.TryBegin(outer, ownerFreshness),
            "Reach owner gate requires an explicit monotonic epoch and rejects nesting");
        const ReachRenderOwnerToken ownerToken = owner.Token();
        Check(!owner.Finish(ownerToken, {}) && owner.IsCurrent(ownerToken),
            "A Reach owner cannot finish without a bound clean-completion token");

        ReachInnerRenderInput inner{};
        inner.returnAddress = moduleBase + kReachPlayerViewRenderReturnRva;
        inner.playerView = outer.playerView;
        inner.activeView = outer.playerView;
        inner.cameraStackDepth = 0;
        inner.topWorkspace = outer.workspace;
        inner.workspaceCallback = moduleBase + kReachCameraStackCallbackRva;
        inner.renderCameraOwner =
            outer.playerView + kReachPlayerViewCameraStateOffset;
        inner.selectedSpecialization = 0;
        inner.primaryCameraValid = true;
        inner.secondaryCameraValid = true;
        inner.preparedFrame = outer.preparedFrame;
        ReachDirectCopyGate directCopyGate;
        ReachDisplaySurfaceProof innerDisplayProof =
            CompleteReachDisplaySurfaceProof(
                epoch, outer.preflight);
        Check(directCopyGate.AdvanceEpoch(epoch) &&
              directCopyGate.Publish(innerDisplayProof),
            "Reach inner admission begins with a live cold display-resource owner");
        inner.displayContinuity = innerDisplayProof.continuity;
        inner.directCopy = directCopyGate.Prepare(
            inner.preparedFrame, inner.displayContinuity);

        const auto innerRejects =
            [&owner, &inner, &directCopyGate](auto mutate)
        {
            auto candidate = inner;
            mutate(candidate);
            return !ReachInnerScopeMatches(
                owner, owner.Token(), directCopyGate, candidate);
        };
        Check(ReachInnerScopeMatches(
                  owner, owner.Token(), directCopyGate, inner) &&
              innerRejects([](auto& v) { ++v.returnAddress; }) &&
              innerRejects([](auto& v) { v.preparedFrame = {}; }) &&
              innerRejects([](auto& v) {
                  v.preparedFrame = ReachPreparedFrameToken::Create(
                      v.directCopy.Epoch(), 43, true);
              }) &&
              innerRejects([](auto& v) { v.directCopy = {}; }) &&
              innerRejects([&directCopyGate](auto& v) {
                  v.directCopy = directCopyGate.Prepare(
                      ReachPreparedFrameToken::Create(
                          v.preparedFrame.Epoch(), 43, true),
                      v.displayContinuity);
              }) &&
              innerRejects([](auto& v) {
                  ++v.displayContinuity.surfaceArrayIdentity;
              }) &&
              innerRejects([](auto& v) { ++v.playerView; }) &&
              innerRejects([](auto& v) { ++v.activeView; }) &&
              innerRejects([](auto& v) { ++v.cameraStackDepth; }) &&
              innerRejects([](auto& v) { ++v.topWorkspace; }) &&
              innerRejects([](auto& v) { ++v.workspaceCallback; }) &&
              innerRejects([](auto& v) { ++v.renderCameraOwner; }) &&
              innerRejects([](auto& v) { ++v.selectedSpecialization; }) &&
              innerRejects([](auto& v) { v.primaryCameraValid = false; }) &&
              innerRejects([](auto& v) { v.secondaryCameraValid = false; }) &&
              innerRejects([](auto& v) { v.teardownRequested = true; }),
            "Reach inner admission checks the exact return edge, epoch, stack, camera, and target");

        const ReachPreflightToken incompletePreflight{};
        auto wrongInner = inner;
        ++wrongInner.returnAddress;
        Check(SelectReachRenderAction(
                  false, outer.preflight, owner, ownerToken,
                  directCopyGate, inner) ==
                  ReachRenderAction::StockOnce &&
              SelectReachRenderAction(
                  true, incompletePreflight, owner, ownerToken,
                  directCopyGate, inner) ==
                  ReachRenderAction::StockOnce &&
              SelectReachRenderAction(
                  true, outer.preflight, owner, ownerToken,
                  directCopyGate, wrongInner) ==
                  ReachRenderAction::StockOnce &&
              SelectReachRenderAction(
                  true, outer.preflight, owner, ownerToken,
                  directCopyGate, inner) ==
                  ReachRenderAction::StereoTransaction,
            "Reach action selection consumes bound proof, owner, and inner-scope inputs");
        Check(SelectReachRenderAction(
                  ReachAdapter_RuntimeHooksPermitted(), outer.preflight,
                  owner, ownerToken, directCopyGate, inner) ==
                  ReachRenderAction::StockOnce,
            "The adapter hard gate keeps an otherwise complete Reach scope stock-once");
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
        ReachLoadedImagePreflight invalidLoadedImage{};
        ReachLoadedImageModulePin invalidModulePin;
        ReachRenderCandidate_ColdPoll(0, 0, 0, false);
        Check(ReachRenderCandidate_Compiled() &&
              !ReachRenderCandidate_RuntimeHooksEnabled() &&
              !ReachRender_RunLoadedImagePreflight(
                  0, kReachRetailImageSize,
                  invalidLoadedImage, invalidModulePin) &&
              !invalidModulePin.Valid() &&
              invalidLoadedImage.failure ==
                  ReachLoadedImageFailure::InvalidInput &&
              !ReachRenderCandidate_GetPreflight(epoch).Complete() &&
              !ReachRenderCandidate_IsPreflightCurrent({}) &&
              ReachRenderCandidate_SelectAction(
                  outer.preflight, owner, ownerToken,
                  directCopyGate, inner) ==
                  ReachRenderAction::StockOnce,
            "The compiled DLL-facing Reach wrapper remains hard-disabled");
#endif

        ReachRollbackGate rollback;
        Check(rollback.Bind(owner, ownerToken) &&
              rollback.BeginFirstPass(ownerToken) &&
              rollback.NeedsRollback() &&
              !rollback.BeginFinalPass(ownerToken) &&
              rollback.MarkFirstPassRestored(ownerToken) &&
              !rollback.NeedsRollback() &&
              rollback.BeginFinalPass(ownerToken) &&
              rollback.NeedsRollback(),
            "Reach cleanup state tracks dirty first and final passes independently");
        const ReachCleanupToken completedCleanup =
            rollback.MarkFinalPassRestoredAndComplete(ownerToken);
        Check(completedCleanup.Valid() && rollback.Finished() &&
              owner.Finish(ownerToken, completedCleanup) &&
              !owner.Token().Active() &&
              owner.LastCompletedSerial() == 42 &&
              SelectReachRenderAction(
                  true, outer.preflight, owner, ownerToken,
                  directCopyGate, inner) ==
                  ReachRenderAction::StockOnce,
            "Owner completion requires clean rollback and invalidates every copied owner token");

        outer.freshCamera = ownerFreshness.Observe(
            1200, epoch, 43, true, true);
        outer.nowMs = 1200;
        outer.preparedFrame = ReachPreparedFrameToken::Create(
            epoch, 43, true);
        Check(owner.TryBegin(outer, ownerFreshness),
            "A fresh prepared-frame serial can mint the next Reach owner token");
        const ReachRenderOwnerToken abortedToken = owner.Token();
        ReachRollbackGate abortedRollback;
        Check(abortedRollback.Bind(owner, abortedToken) &&
              abortedRollback.BeginFirstPass(abortedToken) &&
              abortedRollback.NeedsRollback() &&
              !owner.Abort(abortedToken, {}) &&
              abortedRollback.MarkDirtyPassRestoredForAbort(abortedToken),
            "A dirty aborted pass must explicitly transition through restored state");
        const ReachCleanupToken abortedCleanup =
            abortedRollback.AbortClean(abortedToken);
        Check(abortedCleanup.Valid() && abortedRollback.Aborted() &&
              owner.Abort(abortedToken, abortedCleanup) &&
              !owner.TryBegin(outer, ownerFreshness),
            "A clean abort consumes its exact prepared-frame serial");
        Check(owner.Teardown(epoch) && ownerFreshness.Teardown(epoch) &&
              !owner.AdvanceEpoch(epoch) &&
              !owner.AdvanceEpoch({moduleBase, 8}),
            "Teardown retains the generation high-water mark against stale replay");
        const ReachModuleEpoch nextEpoch{moduleBase, 10};
        Check(outerPreflightPublication.Publish(nextEpoch, outerProof),
            "A newer module generation receives a newer preflight publication");
        outer.preflight = outerPreflightPublication.Get(nextEpoch);
        Check(owner.AdvanceEpoch(nextEpoch) &&
              ownerFreshness.AdvanceEpoch(nextEpoch) &&
              !ownerFreshness.Observe(
                  100, nextEpoch, 39, true, true).Stable() &&
              !ownerFreshness.Observe(
                  400, nextEpoch, 40, true, true).Stable() &&
              !ownerFreshness.Observe(
                  700, nextEpoch, 41, true, true).Stable() &&
              !ownerFreshness.Observe(
                  1000, nextEpoch, 42, true, true).Stable(),
            "A newer Reach generation starts fresh owner and freshness state");
        outer.freshCamera = ownerFreshness.Observe(
            1101, nextEpoch, 43, true, true);
        outer.nowMs = 1101;
        outer.preparedFrame = ReachPreparedFrameToken::Create(
            nextEpoch, 43, true);
        Check(owner.TryBegin(outer, ownerFreshness),
            "A newer Reach module generation starts a fresh serial namespace");
        const ReachRenderOwnerToken nextToken = owner.Token();
        ReachRollbackGate nextRollback;
        Check(nextRollback.Bind(owner, nextToken),
            "The next generation binds cleanup to its live owner");
        const ReachCleanupToken nextAbort =
            nextRollback.AbortClean(nextToken);
        Check(!owner.Abort(nextToken, abortedCleanup) &&
              owner.IsCurrent(nextToken) &&
              owner.Abort(nextToken, nextAbort) &&
              owner.Teardown(nextEpoch) &&
              ownerFreshness.Teardown(nextEpoch),
            "A same-serial cleanup from an old generation cannot close the new owner");

        Check(ReachEyeForPass(0, false) == 0 &&
              ReachEyeForPass(1, false) == 1 &&
              ReachEyeForPass(0, true) == 1 &&
              ReachEyeForPass(1, true) == 0 &&
              ReachEyeForPass(2, false) == -1,
            "Reach pass order matches Halo 3 right-eye-first behavior exactly");
        const ReachStereoPassPolicy firstPass =
            SelectReachStereoPassPolicy(0, false, 1);
        const ReachStereoPassPolicy finalPass =
            SelectReachStereoPassPolicy(1, false, 1);
        const ReachStereoPassPolicy invalidPass =
            SelectReachStereoPassPolicy(2, false, 1);
        Check(firstPass.valid && firstPass.eye == 0 &&
              firstPass.writeLastWindow && firstPass.lastWindowInput == 0 &&
              firstPass.restoreLastWindowAfterPass &&
              finalPass.valid && finalPass.eye == 1 &&
              finalPass.writeLastWindow && finalPass.lastWindowInput == 1 &&
              !finalPass.restoreLastWindowAfterPass &&
              !invalidPass.valid && invalidPass.eye == -1 &&
              !invalidPass.writeLastWindow &&
              invalidPass.restoreLastWindowAfterPass,
            "Reach last-window policy fails closed for invalid passes and preserves the final result");
        Check(kReachRollbackLayout.workspaceSize == 0x2B0 &&
              kReachCameraPairDataSize == 0x2A8 &&
              kReachSecondaryDerivedOffset + kReachDerivedBlockSize ==
                  kReachCameraPairDataSize &&
              kReachRollbackLayout.cameraStateOffset == 0x3B0 &&
              kReachRollbackLayout.cameraStateSize == 0xC8 &&
              kReachRollbackLayout.currentMatricesOffset == 0x490 &&
              kReachRollbackLayout.currentMatricesSize == 0x2D0 &&
              kReachRollbackLayout.previousMatricesOffset == 0x760 &&
              kReachRollbackLayout.previousMatricesSize == 0x2D0 &&
              kReachRollbackLayout.excludedLastWindowOffset == 0xA30 &&
              kReachRollbackLayout.workspaceSize < kReachPlayerViewStride,
            "Reach rollback policy snapshots bounded regions and excludes the whole player view");
        Check(kReachVisibilityClusterLookupCallRva == 0x000C3320 &&
              kReachVisibilityClusterLookupTargetRva == 0x00273458 &&
              kReachVisibilitySecondaryCompactLeaRva == 0x00273468 &&
              kReachVisibilityBuildCallRva == 0x000C335C &&
              kReachVisibilityBuildTargetRva == 0x0027F408 &&
              kReachVisibilitySecondaryDerivedLeaRva == 0x000C3339 &&
              kReachVisibilitySecondaryCompactAddressRva == 0x00C9FC34 &&
              kReachVisibilitySecondaryDerivedAddressRva == 0x00C9FCC4,
            "Reach visibility evidence identifies the exact secondary camera pair consumer");

        constexpr float reachRad = 3.14159265358979323846f / 180.0f;
        const ReachSymmetricFovCover fovCover = SelectReachSymmetricFovCover(
            -61.5f * reachRad, 43.4f * reachRad,
            53.0f * reachRad, -53.0f * reachRad, 2912, 2100);
        const float halfV = fovCover.verticalFov * 0.5f;
        const float halfH = std::atan(
            std::tan(halfV) * (2912.0f / 2100.0f));
        const ReachProjectionHalfFovs decodedFov =
            DecodeReachProjectionHalfFovs(
                1.0f / std::tan(halfH), 1.0f / std::tan(halfV));
        Check(fovCover.valid && decodedFov.valid &&
              std::fabs(decodedFov.horizontal - 61.5f * reachRad) < 0.0001f &&
              ReachProjectionCoversOpenXr(decodedFov, fovCover),
            "Reach raster and OpenXR FOVs cover the same headset view");
        Check(!SelectReachSymmetricFovCover(
                   0.1f, 0.2f, 0.3f, -0.3f, 2912, 2100).valid &&
              !SelectReachSymmetricFovCover(
                   -0.8f, 0.8f, 0.8f, -0.8f, 0, 2100).valid &&
              !DecodeReachProjectionHalfFovs(0.0f, 1.0f).valid &&
              !ReachProjectionCoversOpenXr(
                   DecodeReachProjectionHalfFovs(2.0f, 2.0f), fovCover),
            "Reach FOV proof rejects invalid and under-covering projections");

        const std::array<ReachEyeCullFrustum, 2> asymmetricEyes{{
            {-61.5f * reachRad, 43.4f * reachRad,
             53.0f * reachRad, -48.0f * reachRad,
             {0.0f, 0.0f, 0.0f, 1.0f}},
            {-43.4f * reachRad, 61.5f * reachRad,
             49.0f * reachRad, -53.0f * reachRad,
             {0.0f, 0.0f, 0.0f, 1.0f}},
        }};
        const ReachSymmetricFovCover stereoIdentityCover =
            SelectReachStereoCullFovCover(asymmetricEyes, 2912, 2100);
        const float stereoIdentityHalfV =
            stereoIdentityCover.verticalFov * 0.5f;
        const float stereoIdentityHalfH = std::atan(
            std::tan(stereoIdentityHalfV) * (2912.0f / 2100.0f));
        Check(stereoIdentityCover.valid &&
              std::fabs(stereoIdentityCover.requiredHalfHorizontal -
                        61.5f * reachRad) < 0.0001f &&
              std::fabs(stereoIdentityCover.requiredHalfVertical -
                        53.0f * reachRad) < 0.0001f &&
              stereoIdentityHalfH + 0.0001f >=
                  stereoIdentityCover.requiredHalfHorizontal &&
              stereoIdentityHalfV + 0.0001f >=
                  stereoIdentityCover.requiredHalfVertical,
            "Reach stereo cull FOV encloses all eight asymmetric identity-eye corners");

        const auto yawQuaternion = [](float yaw, float scale = 1.0f)
        {
            return std::array<float, 4>{
                0.0f, std::sin(yaw * 0.5f) * scale, 0.0f,
                std::cos(yaw * 0.5f) * scale};
        };

        // Adversarial headset geometry: each raw OpenXR eye is much wider on
        // its nasal side, while opposed cant rotates the originally narrow
        // temporal side outward. Reach rasterizes a symmetric per-eye FOV, so
        // the outer cull must contain those widened raster corners rather than
        // only the raw asymmetric angles.
        const std::array<ReachEyeCullFrustum, 2> adversarialRawEyes{{
            {-20.0f * reachRad, 70.0f * reachRad,
             45.0f * reachRad, -35.0f * reachRad,
             yawQuaternion(10.0f * reachRad)},
            {-70.0f * reachRad, 20.0f * reachRad,
             42.0f * reachRad, -38.0f * reachRad,
             yawQuaternion(-10.0f * reachRad)},
        }};
        const std::array<ReachSymmetricFovCover, 2> adversarialRasterCovers{{
            SelectReachSymmetricFovCover(
                adversarialRawEyes[0].angleLeft,
                adversarialRawEyes[0].angleRight,
                adversarialRawEyes[0].angleUp,
                adversarialRawEyes[0].angleDown, 2912, 2100),
            SelectReachSymmetricFovCover(
                adversarialRawEyes[1].angleLeft,
                adversarialRawEyes[1].angleRight,
                adversarialRawEyes[1].angleUp,
                adversarialRawEyes[1].angleDown, 2912, 2100),
        }};
        std::array<ReachEyeCullFrustum, 2> adversarialRasterEyes{};
        const bool builtAdversarialLeft =
            BuildReachSymmetricRasterCullFrustum(
                adversarialRasterCovers[0],
                adversarialRawEyes[0].relativeOrientation,
                2912, 2100, adversarialRasterEyes[0]);
        const bool builtAdversarialRight =
            BuildReachSymmetricRasterCullFrustum(
                adversarialRasterCovers[1],
                adversarialRawEyes[1].relativeOrientation,
                2912, 2100, adversarialRasterEyes[1]);
        const float adversarialRasterHalfV =
            adversarialRasterCovers[0].verticalFov * 0.5f;
        const float adversarialRasterHalfH = std::atan(
            std::tan(adversarialRasterHalfV) * (2912.0f / 2100.0f));
        Check(builtAdversarialLeft && builtAdversarialRight &&
              std::fabs(adversarialRasterEyes[0].angleLeft +
                        adversarialRasterHalfH) < 0.000001f &&
              std::fabs(adversarialRasterEyes[0].angleRight -
                        adversarialRasterHalfH) < 0.000001f &&
              std::fabs(adversarialRasterEyes[0].angleUp -
                        adversarialRasterHalfV) < 0.000001f &&
              std::fabs(adversarialRasterEyes[0].angleDown +
                        adversarialRasterHalfV) < 0.000001f &&
              adversarialRasterEyes[0].angleLeft <
                  adversarialRawEyes[0].angleLeft - 40.0f * reachRad &&
              adversarialRasterEyes[1].angleRight >
                  adversarialRawEyes[1].angleRight + 40.0f * reachRad &&
              adversarialRasterEyes[0].relativeOrientation ==
                  adversarialRawEyes[0].relativeOrientation &&
              adversarialRasterEyes[1].relativeOrientation ==
                  adversarialRawEyes[1].relativeOrientation,
            "Reach cull inputs describe the actual widened symmetric eye rasters");

        const ReachSymmetricFovCover adversarialRawCull =
            SelectReachStereoCullFovCover(
                adversarialRawEyes, 2912, 2100);
        const ReachSymmetricFovCover adversarialRasterCull =
            SelectReachStereoCullFovCover(
                adversarialRasterEyes, 2912, 2100);
        const float adversarialCullHalfV =
            adversarialRasterCull.verticalFov * 0.5f;
        const float adversarialCullHalfH = std::atan(
            std::tan(adversarialCullHalfV) * (2912.0f / 2100.0f));
        Check(adversarialRawCull.valid && adversarialRasterCull.valid &&
              std::fabs(adversarialRasterCull.requiredHalfHorizontal -
                        80.0f * reachRad) < 0.0001f &&
              adversarialRasterCull.requiredHalfHorizontal >
                  adversarialRawCull.requiredHalfHorizontal +
                      15.0f * reachRad &&
              adversarialRasterCull.requiredHalfVertical >
                  adversarialRawCull.requiredHalfVertical +
                      15.0f * reachRad &&
              adversarialCullHalfH + 0.0001f >=
                  adversarialRasterCull.requiredHalfHorizontal &&
              adversarialCullHalfV + 0.0001f >=
                  adversarialRasterCull.requiredHalfVertical,
            "Reach binocular cull encloses widened asymmetric-and-canted raster corners");

        ReachEyeCullFrustum rejectedRasterFrustum{
            -0.5f, 0.5f, 0.5f, -0.5f, {0.0f, 0.0f, 0.0f, 1.0f}};
        ReachSymmetricFovCover invalidRasterCover =
            adversarialRasterCovers[0];
        invalidRasterCover.valid = false;
        const bool rejectsInvalidRasterCover =
            !BuildReachSymmetricRasterCullFrustum(
                invalidRasterCover,
                adversarialRawEyes[0].relativeOrientation,
                2912, 2100, rejectedRasterFrustum);
        const bool clearsRejectedRasterFrustum =
            rejectedRasterFrustum.angleLeft == 0.0f &&
            rejectedRasterFrustum.angleRight == 0.0f &&
            rejectedRasterFrustum.angleUp == 0.0f &&
            rejectedRasterFrustum.angleDown == 0.0f;
        Check(rejectsInvalidRasterCover && clearsRejectedRasterFrustum &&
              !BuildReachSymmetricRasterCullFrustum(
                  adversarialRasterCovers[0],
                  {0.0f, 0.0f, 0.0f, 0.0f},
                  2912, 2100, rejectedRasterFrustum) &&
              !BuildReachSymmetricRasterCullFrustum(
                  adversarialRasterCovers[0],
                  adversarialRawEyes[0].relativeOrientation,
                  2912, 0, rejectedRasterFrustum),
            "Reach symmetric raster cull conversion fails closed on invalid input");

        const std::array<ReachEyeCullFrustum, 2> cantedEyes{{
            {-40.0f * reachRad, 40.0f * reachRad,
             35.0f * reachRad, -35.0f * reachRad,
             yawQuaternion(10.0f * reachRad)},
            {-40.0f * reachRad, 40.0f * reachRad,
             35.0f * reachRad, -35.0f * reachRad,
             yawQuaternion(-10.0f * reachRad)},
        }};
        const ReachSymmetricFovCover cantedCover =
            SelectReachStereoCullFovCover(cantedEyes, 2912, 2100);
        std::array<ReachEyeCullFrustum, 2> scaledCantedEyes = cantedEyes;
        for (ReachEyeCullFrustum& eye : scaledCantedEyes)
            for (float& component : eye.relativeOrientation)
                component *= -3.25f;
        const ReachSymmetricFovCover scaledCantedCover =
            SelectReachStereoCullFovCover(scaledCantedEyes, 2912, 2100);
        const std::array<ReachEyeCullFrustum, 2> swappedCantedEyes{{
            cantedEyes[1], cantedEyes[0]}};
        const ReachSymmetricFovCover swappedCantedCover =
            SelectReachStereoCullFovCover(swappedCantedEyes, 2912, 2100);
        Check(cantedCover.valid && scaledCantedCover.valid &&
              swappedCantedCover.valid &&
              std::fabs(cantedCover.requiredHalfHorizontal -
                        50.0f * reachRad) < 0.0001f &&
              cantedCover.requiredHalfVertical > 35.0f * reachRad &&
              std::fabs(cantedCover.verticalFov -
                        scaledCantedCover.verticalFov) < 0.000001f &&
              std::fabs(cantedCover.requiredHalfHorizontal -
                        swappedCantedCover.requiredHalfHorizontal) < 0.000001f &&
              std::fabs(cantedCover.requiredHalfVertical -
                        swappedCantedCover.requiredHalfVertical) < 0.000001f,
            "Reach stereo cull FOV normalizes quaternion scale and is invariant to eye order");

        bool yawEnvelopeProperty = true;
        constexpr std::array<float, 5> yawDegrees{
            0.0f, 2.5f, 6.0f, 10.0f, 15.0f};
        constexpr std::array<float, 4> quaternionScales{
            0.25f, 1.0f, 3.0f, -2.0f};
        for (float yawDegreesValue : yawDegrees)
        {
            const float yaw = yawDegreesValue * reachRad;
            ReachSymmetricFovCover reference{};
            for (size_t scaleIndex = 0;
                 scaleIndex < quaternionScales.size(); ++scaleIndex)
            {
                const float scale = quaternionScales[scaleIndex];
                const std::array<ReachEyeCullFrustum, 2> propertyEyes{{
                    {-30.0f * reachRad, 30.0f * reachRad,
                     25.0f * reachRad, -25.0f * reachRad,
                     yawQuaternion(yaw, scale)},
                    {-30.0f * reachRad, 30.0f * reachRad,
                     25.0f * reachRad, -25.0f * reachRad,
                     yawQuaternion(-yaw, scale)},
                }};
                const ReachSymmetricFovCover candidate =
                    SelectReachStereoCullFovCover(
                        propertyEyes, 2912, 2100);
                yawEnvelopeProperty = yawEnvelopeProperty && candidate.valid &&
                    std::fabs(candidate.requiredHalfHorizontal -
                              (30.0f + yawDegreesValue) * reachRad) < 0.0001f;
                if (scaleIndex == 0)
                    reference = candidate;
                else
                    yawEnvelopeProperty = yawEnvelopeProperty &&
                        std::fabs(candidate.verticalFov -
                                  reference.verticalFov) < 0.000001f &&
                        std::fabs(candidate.requiredHalfVertical -
                                  reference.requiredHalfVertical) < 0.000001f;
            }
        }
        Check(yawEnvelopeProperty,
            "Reach stereo cull FOV encloses deterministic opposed-yaw properties");

        std::array<ReachEyeCullFrustum, 2> invalidStereoEyes = asymmetricEyes;
        invalidStereoEyes[0].relativeOrientation = {0.0f, 0.0f, 0.0f, 0.0f};
        const bool rejectsZeroQuaternion =
            !SelectReachStereoCullFovCover(
                invalidStereoEyes, 2912, 2100).valid;
        invalidStereoEyes = asymmetricEyes;
        invalidStereoEyes[1].relativeOrientation[2] =
            std::numeric_limits<float>::quiet_NaN();
        const bool rejectsNonFiniteQuaternion =
            !SelectReachStereoCullFovCover(
                invalidStereoEyes, 2912, 2100).valid;
        invalidStereoEyes = asymmetricEyes;
        invalidStereoEyes[0].angleLeft =
            -std::numeric_limits<float>::infinity();
        const bool rejectsNonFiniteFov =
            !SelectReachStereoCullFovCover(
                invalidStereoEyes, 2912, 2100).valid;
        invalidStereoEyes = asymmetricEyes;
        invalidStereoEyes[0].angleRight = 1.57079632679489661923f;
        const bool rejectsRearHemisphereFov =
            !SelectReachStereoCullFovCover(
                invalidStereoEyes, 2912, 2100).valid;
        const std::array<ReachEyeCullFrustum, 2> behindCenterEyes{{
            {-20.0f * reachRad, 20.0f * reachRad,
             20.0f * reachRad, -20.0f * reachRad,
             yawQuaternion(80.0f * reachRad)},
            {-20.0f * reachRad, 20.0f * reachRad,
             20.0f * reachRad, -20.0f * reachRad,
             yawQuaternion(-80.0f * reachRad)},
        }};
        Check(rejectsZeroQuaternion && rejectsNonFiniteQuaternion &&
              rejectsNonFiniteFov && rejectsRearHemisphereFov &&
              !SelectReachStereoCullFovCover(
                  behindCenterEyes, 2912, 2100).valid &&
              !SelectReachStereoCullFovCover(
                  asymmetricEyes, 0, 2100).valid,
            "Reach stereo cull FOV rejects degenerate, non-finite, and behind-centre inputs");
    }

    {
        std::array<uint8_t, 0x90> compactCamera{};
        const auto store = [&compactCamera](
                               size_t offset, const auto& value)
        {
            std::memcpy(
                compactCamera.data() + offset, &value, sizeof(value));
        };
        const float position[3]{ 10.0f, -2.0f, 5.0f };
        const float forward[3]{ 1.0f, 0.0f, 0.0f };
        const float up[3]{ 0.0f, 0.0f, 1.0f };
        const float verticalFov = 1.0f;
        const ReachObservedRect windowBounds{ 0, 0, 1080, 1920 };
        const ReachObservedRect renderBounds{ 0, 0, 720, 1280 };
        const ReachObservedRect clientBounds{ 0, 0, 720, 1280 };
        store(0x00, position);
        store(0x0C, forward);
        store(0x18, up);
        store(0x28, verticalFov);
        store(0x38, windowBounds);
        store(0x4C, renderBounds);
        store(0x5C, clientBounds);

        ReachCompactCameraObservation observedCamera{};
        Check(ValidateReachCompactCamera(
                  compactCamera.data(), compactCamera.size(), observedCamera) &&
                  observedCamera.position[0] == position[0] &&
                  observedCamera.position[1] == position[1] &&
                  observedCamera.position[2] == position[2] &&
                  observedCamera.verticalFov == verticalFov &&
                  observedCamera.clientBounds.x1 == clientBounds.x1,
            "Reach compact-camera proof accepts and decodes its exact validated layout");
        Check(!ValidateReachCompactCamera(
                  nullptr, compactCamera.size(), observedCamera) &&
                  !ValidateReachCompactCamera(
                      compactCamera.data(), compactCamera.size() - 1,
                      observedCamera),
            "Reach compact-camera proof rejects null or truncated snapshots");

        const auto validateWithFloat =
            [&compactCamera, &observedCamera](size_t offset, float value)
        {
            auto candidate = compactCamera;
            std::memcpy(candidate.data() + offset, &value, sizeof(value));
            return ValidateReachCompactCamera(
                candidate.data(), candidate.size(), observedCamera);
        };
        Check(!validateWithFloat(
                  0x00, std::numeric_limits<float>::quiet_NaN()) &&
                  !validateWithFloat(
                      0x0C, std::numeric_limits<float>::infinity()),
            "Reach compact-camera proof rejects NaN and infinite vectors");
        Check(validateWithFloat(0x0C, std::sqrt(1.0005f)) &&
                  !validateWithFloat(0x0C, std::sqrt(1.0015f)),
            "Reach compact-camera proof enforces the HREK axis-length tolerance");

        {
            auto candidate = compactCamera;
            const float nearOrthogonalUp[3]{
                0.0005f, 0.0f, std::sqrt(1.0f - 0.0005f * 0.0005f)
            };
            std::memcpy(
                candidate.data() + 0x18, nearOrthogonalUp,
                sizeof(nearOrthogonalUp));
            Check(ValidateReachCompactCamera(
                      candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof accepts dot products inside tolerance");
        }
        {
            auto candidate = compactCamera;
            const float nonOrthogonalUp[3]{
                0.0015f, 0.0f, std::sqrt(1.0f - 0.0015f * 0.0015f)
            };
            std::memcpy(
                candidate.data() + 0x18, nonOrthogonalUp,
                sizeof(nonOrthogonalUp));
            Check(!ValidateReachCompactCamera(
                      candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof rejects non-orthogonal axes");
        }

        Check(!validateWithFloat(0x28, kReachCameraFovMin) &&
                  validateWithFloat(
                      0x28, std::nextafter(
                                kReachCameraFovMin,
                                std::numeric_limits<float>::infinity())) &&
                  validateWithFloat(
                      0x28, std::nextafter(kReachCameraFovMax, 0.0f)) &&
                  !validateWithFloat(0x28, kReachCameraFovMax),
            "Reach compact-camera proof enforces strict finite FOV bounds");
        {
            auto candidate = compactCamera;
            const ReachObservedRect unorderedWindow{ 1, 0, 1, 1920 };
            std::memcpy(
                candidate.data() + 0x38, &unorderedWindow,
                sizeof(unorderedWindow));
            Check(!ValidateReachCompactCamera(
                      candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof rejects unordered viewport bounds");
        }
        {
            auto candidate = compactCamera;
            const ReachObservedRect nonzeroClientOrigin{ 1, 0, 720, 1280 };
            std::memcpy(
                candidate.data() + 0x5C, &nonzeroClientOrigin,
                sizeof(nonzeroClientOrigin));
            Check(!ValidateReachCompactCamera(
                      candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof requires the proven zero client origin");
        }
        {
            auto candidate = compactCamera;
            const ReachObservedRect undersizedClient{ 0, 0, 7, 1280 };
            std::memcpy(
                candidate.data() + 0x5C, &undersizedClient,
                sizeof(undersizedClient));
            Check(!ValidateReachCompactCamera(
                      candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof enforces the producer's minimum extent");
        }

        ReachObserverFreshnessWindow freshness;
        Check(!freshness.ObserveTransaction(10),
            "One Reach transaction cannot satisfy freshness stability");
        Check(!freshness.ObserveTransaction(400),
            "A sub-500 ms Reach transaction preserves but cannot yet arm freshness");
        Check(!freshness.ObserveTransaction(800),
            "Repeated fresh Reach transactions stay below the safety interval");
        Check(!freshness.ObserveTransaction(1010),
            "Reach freshness remains unarmed through exactly one continuous second");
        Check(freshness.IsFresh(1010) &&
                  freshness.TransactionCount() == 4 &&
                  freshness.CurrentSpanMs() == 1000,
            "Reach freshness counts only sub-500 ms transaction intervals");
        Check(freshness.ObserveTransaction(1011) &&
                  freshness.IsStable(1011),
            "Reach freshness becomes observationally stable only after one second");
        Check(freshness.Tick(1510),
            "A 499 ms gap keeps the Reach transaction window fresh");
        Check(!freshness.Tick(1511) && freshness.TransactionCount() == 0,
            "A 500 ms gap resets the Reach freshness window");
        Check(!freshness.ObserveTransaction(2000),
            "A new Reach transaction starts a fresh observational window");
        Check(!freshness.ObserveTransaction(2500) &&
                  freshness.TransactionCount() == 1 &&
                  freshness.CurrentSpanMs() == 0,
            "A missed Reach pulse is inconclusive and begins a new window");
        Check(!freshness.ObserveTransaction(2400) &&
                  freshness.TransactionCount() == 1,
            "A non-monotonic Reach observation also fails closed");
        freshness.Reset();
        Check(!freshness.IsFresh(3000) &&
                  !freshness.IsStable(3000) &&
                  freshness.LastTransactionMs() == 0,
            "Reach freshness reset clears all observational state");

        Check(!freshness.ObserveTransaction(4000) &&
                  !freshness.ObserveTransaction(4400) &&
                  !freshness.ObserveTransaction(4800) &&
                  !freshness.Tick(5001) &&
                  freshness.CurrentSpanMs() == 800,
            "Elapsed wall time cannot satisfy a Reach gate without a new transaction");
        Check(freshness.ObserveTransaction(5101) &&
                  freshness.CurrentSpanMs() == 1101,
            "A new Reach transaction may satisfy the strict one-second span");
        Check(!freshness.Tick(5601) &&
                  freshness.TransactionCount() == 0,
            "A post-gate 500 ms pause expires the Reach freshness window");
        Check(!freshness.ObserveTransaction(6000) &&
                  !freshness.ObserveTransaction(6400) &&
                  !freshness.ObserveTransaction(6800) &&
                  freshness.ObserveTransaction(7001),
            "Reach freshness can establish a new stable window after pause recovery");

        std::array<ReachObserverFreshnessWindow, 4> slotFreshness{};
        Check(ReachObserverUniqueFreshOwner(
                  slotFreshness.data(), slotFreshness.size(), 10) ==
                  kReachNoFreshOwner,
            "Reach observer reports no owner before a slot heartbeat");
        slotFreshness[0].ObserveTransaction(10);
        Check(ReachObserverUniqueFreshOwner(
                  slotFreshness.data(), slotFreshness.size(), 10) == 0,
            "Reach observer identifies one fresh player-view owner");
        slotFreshness[1].ObserveTransaction(20);
        Check(ReachObserverUniqueFreshOwner(
                  slotFreshness.data(), slotFreshness.size(), 20) ==
                  kReachMultipleFreshOwners,
            "Reach observer fails closed when two player slots are fresh");
        Check(ReachObserverUniqueFreshOwner(
                  slotFreshness.data(), slotFreshness.size(), 510) == 1,
            "Reach observer expires a stale slot independently");
    }

    {
        Check(kTitleRuntimeSlotCount == 6 &&
                  TitleRuntimeSlotIndex(GameTitle::Halo3) == 0 &&
                  TitleRuntimeSlotIndex(GameTitle::Halo3ODST) == 1 &&
                  TitleRuntimeSlotIndex(GameTitle::HaloReach) == 2 &&
                  TitleRuntimeSlotIndex(GameTitle::Halo4) == 3 &&
                  TitleRuntimeSlotIndex(GameTitle::HaloCE) == 4 &&
                  TitleRuntimeSlotIndex(GameTitle::Halo2) == 5 &&
                  TitleRuntimeSlotIndex(GameTitle::None) ==
                      kInvalidTitleRuntimeSlot &&
                  TitleRuntimeSlotIndex(GameTitle::Unknown) ==
                      kInvalidTitleRuntimeSlot,
            "title-runtime slots cover only concrete MCC game modules");
        Check(TitleRuntimeSlotTitle(0) == GameTitle::Halo3 &&
                  TitleRuntimeSlotTitle(1) == GameTitle::Halo3ODST &&
                  TitleRuntimeSlotTitle(2) == GameTitle::HaloReach &&
                  TitleRuntimeSlotTitle(3) == GameTitle::Halo4 &&
                  TitleRuntimeSlotTitle(4) == GameTitle::HaloCE &&
                  TitleRuntimeSlotTitle(5) == GameTitle::Halo2 &&
                  TitleRuntimeSlotTitle(6) == GameTitle::None,
            "title-runtime slot mapping is reversible and bounds checked");
        Check(TitleRuntimeAvailabilityBit(GameTitle::Halo3) == 0x01u &&
                  TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST) == 0x02u &&
                  TitleRuntimeAvailabilityBit(GameTitle::HaloReach) == 0x04u &&
                  TitleRuntimeAvailabilityBit(GameTitle::Halo4) == 0x08u &&
                  TitleRuntimeAvailabilityBit(GameTitle::HaloCE) == 0x10u &&
                  TitleRuntimeAvailabilityBit(GameTitle::Halo2) == 0x20u &&
                  TitleRuntimeAvailabilityBit(GameTitle::Unknown) == 0 &&
                  kTitleRuntimeAvailabilityMask == 0x3Fu,
            "title-runtime availability bits are stable and non-overlapping");
        Check(static_cast<uint32_t>(TitleCapability_ControllerInput) == 0x40u &&
                  static_cast<uint32_t>(TitleCapability_Haptics) == 0x80u &&
                  static_cast<uint32_t>(TitleCapability_CutsceneTheater) == 0x100u &&
                  kTitleRuntimeKnownCapabilities == 0x1FFu,
            "controller input, haptics, and cutscene theatre extend the known capability mask exactly");
        Check(TitleRuntimeHeartbeatWindowMs(GameTitle::Halo3) == 500 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::Halo3ODST) == 5001 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::HaloReach) == 500 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::Halo4) == 500 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::HaloCE) == 0 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::Halo2) == 0 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::Unknown) == 0,
            "heartbeat windows pin H3/ODST/Reach byte-for-byte and every runtime title is nonzero because a zero window silently disqualifies ownership");
        const TitleRuntimeHeartbeatPolicy windowPolicy =
            MakeTitleRuntimeHeartbeatPolicy();
        bool heartbeatWindowsMirrorTable = true;
        for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
            heartbeatWindowsMirrorTable = heartbeatWindowsMirrorTable &&
                windowPolicy.freshForMs[slot] ==
                    TitleRuntimeHeartbeatWindowMs(TitleRuntimeSlotTitle(slot));
        Check(heartbeatWindowsMirrorTable,
            "the built heartbeat policy mirrors the window table slot for slot");
        bool retainedTruthTable = true;
        for (uint32_t mask = 0; mask < (1u << kTitleRuntimeSlotCount); ++mask)
        {
            std::array<uint32_t, kTitleRuntimeSlotCount> generations{};
            size_t nonzeroCount = 0;
            size_t nonzeroSlot = 0;
            for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
            {
                if ((mask & (uint32_t{1} << slot)) == 0)
                    continue;
                generations[slot] = static_cast<uint32_t>(slot) + 7u;
                ++nonzeroCount;
                nonzeroSlot = slot;
            }
            const GameTitle expected = nonzeroCount == 1
                ? TitleRuntimeSlotTitle(nonzeroSlot) : GameTitle::None;
            retainedTruthTable = retainedTruthTable &&
                RetainedRuntimeTitleFromGenerations(generations) == expected;
        }
        Check(retainedTruthTable,
            "over all 64 generation combinations the retained runtime title is the unique nonzero-generation title, else None (H3-only stays Halo3, H3+ODST stays None)");
    }

    {
        const CinematicControlPublication locked{
            GameTitle::HaloCE, 77, CinematicControlState::AuthoredLocked, 1000};
        const uint32_t capability = TitleCapability_CutsceneTheater;
        Check(ResolveCinematicControl(
                  locked, GameTitle::HaloCE, 77, capability, 1100) ==
                  CinematicControlState::AuthoredLocked,
            "a mock future title receives the generic theatre contract without a title check");
        Check(CutsceneTheaterRequested(
                  true, CinematicControlState::AuthoredLocked) &&
              !CutsceneTheaterRequested(
                  false, CinematicControlState::AuthoredLocked) &&
              !CutsceneTheaterRequested(
                  true, CinematicControlState::PlayerControlled) &&
              !CutsceneTheaterRequested(
                  true, CinematicControlState::Unknown),
            "only enabled AuthoredLocked state requests theatre presentation");
        Check(ClassifyHalo3FamilyCinematicControl(true, true, true) ==
                  CinematicControlState::AuthoredLocked &&
              ClassifyHalo3FamilyCinematicControl(true, false, false) ==
                  CinematicControlState::PlayerControlled &&
              ClassifyHalo3FamilyCinematicControl(false, false, false) ==
                  CinematicControlState::Unknown &&
              ClassifyHalo3FamilyCinematicControl(true, true, false) ==
                  CinematicControlState::Unknown,
            "Halo 3 and ODST require both their cinematic flag and shot-state proof");
        const float odstLockedLook[4] = {};
        const float odstNoLookRate[4] = {};
        const float odstFreeLook[4] = {0.0f, -0.1f, 0.0f, 0.2f};
        const float odstOpeningLookRate[4] = {0.0f, -0.01f, 0.0f, 0.01f};
        const float odstInvalidLook[4] = {
            0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
        Check(ClassifyOdstCinematicControl(
                  CinematicControlState::AuthoredLocked, true,
                  odstLockedLook, odstNoLookRate, 0) ==
                  CinematicControlState::AuthoredLocked &&
              ClassifyOdstCinematicControl(
                  CinematicControlState::AuthoredLocked, true,
                  odstFreeLook, odstNoLookRate, 0) ==
                  CinematicControlState::PlayerControlled &&
              ClassifyOdstCinematicControl(
                  CinematicControlState::AuthoredLocked, true,
                  odstLockedLook, odstOpeningLookRate, 60) ==
                  CinematicControlState::PlayerControlled &&
              ClassifyOdstCinematicControl(
                  CinematicControlState::AuthoredLocked, true,
                  odstLockedLook, odstOpeningLookRate, 0) ==
                  CinematicControlState::AuthoredLocked,
            "ODST qualifies only zero live look freedom with no active opening interpolation");
        Check(ClassifyOdstCinematicControl(
                  CinematicControlState::AuthoredLocked, false,
                  odstLockedLook, odstNoLookRate, 0) ==
                  CinematicControlState::Unknown &&
              ClassifyOdstCinematicControl(
                  CinematicControlState::AuthoredLocked, true,
                  odstInvalidLook, odstNoLookRate, 0) ==
                  CinematicControlState::Unknown &&
              ClassifyOdstCinematicControl(
                  CinematicControlState::AuthoredLocked, true,
                  odstLockedLook, odstNoLookRate, -1) ==
                  CinematicControlState::Unknown &&
              ClassifyOdstCinematicControl(
                  CinematicControlState::PlayerControlled, false,
                  nullptr, nullptr, -1) ==
                  CinematicControlState::PlayerControlled,
            "ODST missing or invalid look proof fails closed without weakening known gameplay state");
        Check(ClassifyReachCinematicControl(true, 1) ==
                  CinematicControlState::AuthoredLocked &&
              ClassifyReachCinematicControl(true, 0) ==
                  CinematicControlState::PlayerControlled &&
              ClassifyReachCinematicControl(false, 1) ==
                  CinematicControlState::Unknown,
            "Reach qualifies only its proven cinematic-globals +0x24 state");
        Check(ShouldDisableWidescreenCinematicFov(true, true, false) &&
                  !ShouldDisableWidescreenCinematicFov(true, true, true) &&
                  !ShouldDisableWidescreenCinematicFov(true, false, false) &&
                  !ShouldDisableWidescreenCinematicFov(false, true, false),
            "the stock cinematic FOV is restored only in theatre while immersive VR keeps its existing widening policy");
        Check(UseAuthoredCutsceneProjection(true, 4.0f / 3.0f) &&
                  !UseAuthoredCutsceneProjection(false, 4.0f / 3.0f) &&
                  !UseAuthoredCutsceneProjection(true, 0.0f),
            "authored projection replacement is isolated to active theatre with proven metadata");
        const Halo3CutsceneTheaterFrustum immersiveHalo3 =
            SelectHalo3CutsceneTheaterFrustum(
                false, 1.20f, 0.90f, 1.84f, 1.33f);
        Check(immersiveHalo3.valid &&
                  immersiveHalo3.projectionTangentX == 1.84f &&
                  immersiveHalo3.projectionTangentY == 1.33f &&
                  immersiveHalo3.projectionCenterYAdjustment == 0.0f &&
                  immersiveHalo3.cullingTangentX == 1.84f &&
                  immersiveHalo3.cullingTangentY == 1.33f,
            "Halo 3 immersive projection and culling remain byte-for-byte on the existing OpenXR cover");
        const Halo3CutsceneTheaterFrustum theaterHalo3 =
            SelectHalo3CutsceneTheaterFrustum(
                true, 1.20f, 0.90f, 1.84f, 1.33f);
        Check(theaterHalo3.valid &&
                  std::fabs(theaterHalo3.projectionTangentX - 1.02f) < 0.00001f &&
                  std::fabs(theaterHalo3.projectionTangentY - 0.765f) < 0.00001f &&
                  std::fabs(theaterHalo3.projectionCenterYAdjustment +
                            0.10f) < 0.00001f &&
                  theaterHalo3.cullingTangentX == 1.84f &&
                  theaterHalo3.cullingTangentY == 1.33f &&
                  std::fabs(
                      theaterHalo3.projectionTangentX /
                          theaterHalo3.projectionTangentY -
                      4.0f / 3.0f) < 0.00001f,
            "Halo 3 theatre tightens and lifts framing without changing aspect or the wider visibility cover");
        Check(!SelectHalo3CutsceneTheaterFrustum(
                   true, 0.0f, 0.90f, 1.84f, 1.33f).valid &&
                  !SelectHalo3CutsceneTheaterFrustum(
                      false, 1.20f, 0.90f, 0.0f, 1.33f).valid,
            "Halo 3 theatre rejects invalid authored or immersive frustum evidence");
        const float tangentAspect =
            CutsceneTheaterAspectFromTangents(4.0f / 3.0f, 1.0f);
        const float projectionAspect =
            CutsceneTheaterAspectFromProjectionScales(0.75f, 1.0f);
        const float dimensionAspect =
            CutsceneTheaterAspectFromDimensions(2912, 2100);
        Check(std::fabs(tangentAspect - 4.0f / 3.0f) < 0.00001f &&
                  std::fabs(projectionAspect - 4.0f / 3.0f) < 0.00001f &&
                  std::fabs(dimensionAspect - 2912.0f / 2100.0f) < 0.00001f &&
                  CutsceneTheaterAspectFromTangents(1.0f, 0.0f) == 0.0f &&
                  CutsceneTheaterAspectFromProjectionScales(0.0f, 1.0f) == 0.0f &&
                  CutsceneTheaterAspectFromDimensions(0, 2100) == 0.0f,
            "authored aspect decodes safely from camera tangents, projection scales, and image dimensions");
        Check(CutsceneTheaterProjectionMatchesAspect(
                  4.0f / 3.0f, 0.75f, 1.0f) &&
              !CutsceneTheaterProjectionMatchesAspect(
                  16.0f / 9.0f, 0.75f, 1.0f),
            "a rebuilt theatre projection must still match the pristine authored aspect");
        const CutsceneTheaterProjectionPublication authoredProjection{
            GameTitle::HaloCE, 77, 4.0f / 3.0f, 1000};
        float resolvedAspect = 0.0f;
        Check(ResolveCutsceneTheaterProjection(
                  authoredProjection, GameTitle::HaloCE, 77,
                  capability, 1100, resolvedAspect) &&
                  std::fabs(resolvedAspect - 4.0f / 3.0f) < 0.00001f,
            "a future title's fresh generation-tagged authored aspect resolves generically");
        Check(!ResolveCutsceneTheaterProjection(
                  authoredProjection, GameTitle::HaloCE, 78,
                  capability, 1100, resolvedAspect) &&
              !ResolveCutsceneTheaterProjection(
                  authoredProjection, GameTitle::Halo3, 77,
                  capability, 1100, resolvedAspect) &&
              !ResolveCutsceneTheaterProjection(
                  authoredProjection, GameTitle::HaloCE, 77,
                  TitleCapability_None, 1100, resolvedAspect) &&
              !ResolveCutsceneTheaterProjection(
                  authoredProjection, GameTitle::HaloCE, 77,
                  capability, 1501, resolvedAspect),
            "authored aspect rejects title, generation, capability, and freshness mismatches");
        CinematicControlPublication publication = locked;
        publication.state = CinematicControlState::PlayerControlled;
        Check(ResolveCinematicControl(
                  publication, GameTitle::HaloCE, 77, capability, 1100) ==
                  CinematicControlState::PlayerControlled,
            "player-controlled sequences remain immersive");
        publication.state = CinematicControlState::Unknown;
        Check(ResolveCinematicControl(
                  publication, GameTitle::HaloCE, 77, capability, 1100) ==
                  CinematicControlState::Unknown,
            "unproven sequences remain immersive");
        Check(ResolveCinematicControl(
                  locked, GameTitle::HaloCE, 78, capability, 1100) ==
                  CinematicControlState::Unknown &&
              ResolveCinematicControl(
                  locked, GameTitle::Halo3, 77, capability, 1100) ==
                  CinematicControlState::Unknown &&
              ResolveCinematicControl(
                  locked, GameTitle::HaloCE, 77, TitleCapability_None, 1100) ==
                  CinematicControlState::Unknown &&
              ResolveCinematicControl(
                  locked, GameTitle::HaloCE, 77, capability, 1501) ==
                  CinematicControlState::Unknown,
            "title transitions, generation changes, capability loss, and stale state exit theatre");

        float position[3]{0.032f, -0.004f, 0.002f};
        float orientation[4]{0.1f, 0.2f, 0.3f, 0.9f};
        ApplyCutsceneTheaterEyeTransform(true, 0.0f, position, orientation);
        Check(position[0] == 0.0f && position[1] == 0.0f &&
                  position[2] == 0.0f && orientation[0] == 0.0f &&
                  orientation[1] == 0.0f && orientation[2] == 0.0f &&
                  orientation[3] == 1.0f,
            "zero theatre depth is flat and uses parallel eye cameras");
        float doublePosition[3]{0.032f, -0.004f, 0.002f};
        float doubleOrientation[4]{0.1f, 0.2f, 0.3f, 0.9f};
        ApplyCutsceneTheaterEyeTransform(
            true, 2.0f, doublePosition, doubleOrientation);
        Check(std::fabs(doublePosition[0] - 0.064f) < 0.00001f &&
                  std::fabs(doublePosition[1] + 0.008f) < 0.00001f &&
                  std::fabs(doublePosition[2] - 0.004f) < 0.00001f,
            "two hundred percent theatre depth doubles natural eye separation");
        float unchangedPosition[3]{1.0f, 2.0f, 3.0f};
        float unchangedOrientation[4]{0.1f, 0.2f, 0.3f, 0.9f};
        ApplyCutsceneTheaterEyeTransform(
            false, 0.0f, unchangedPosition, unchangedOrientation);
        Check(unchangedPosition[0] == 1.0f && unchangedOrientation[0] == 0.1f,
            "inactive theatre leaves immersive eye transforms untouched");
        Check(CutsceneTheaterImageIndex(0, false) == 0 &&
                  CutsceneTheaterImageIndex(1, false) == 1 &&
                  CutsceneTheaterImageIndex(0, true) == 1 &&
                  CutsceneTheaterImageIndex(1, true) == 0,
            "Flip Depth swaps only the per-eye image slices");
        const float projectionEye[3]{0.0f, 0.0f, 0.0f};
        const float projectionOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        const float projectionCenter[3]{0.0f, 0.0f, -4.0f};
        const float projectionFov[4]{
            -0.785398163f, 0.785398163f,
             0.785398163f, -0.785398163f};
        CutsceneTheaterClipVertex projectionVertices[4]{};
        Check(BuildCutsceneTheaterProjectionQuad(
                  projectionEye, projectionOrientation,
                  projectionCenter, projectionOrientation,
                  6.0f, 3.375f, projectionFov, projectionVertices) &&
              std::fabs(projectionVertices[0].x + 3.0f) < 0.00001f &&
              std::fabs(projectionVertices[0].y - 1.6875f) < 0.00001f &&
              std::fabs(projectionVertices[0].w - 4.0f) < 0.00001f &&
              std::fabs(projectionVertices[3].x - 3.0f) < 0.00001f &&
              std::fabs(projectionVertices[3].y + 1.6875f) < 0.00001f,
            "theatre screen projects into an ordinary stereo projection view");
        const float rightEye[3]{0.032f, 0.0f, 0.0f};
        CutsceneTheaterClipVertex rightEyeVertices[4]{};
        Check(BuildCutsceneTheaterProjectionQuad(
                  rightEye, projectionOrientation,
                  projectionCenter, projectionOrientation,
                  6.0f, 3.375f, projectionFov, rightEyeVertices) &&
              rightEyeVertices[0].x < projectionVertices[0].x &&
              rightEyeVertices[3].x < projectionVertices[3].x,
            "room-fixed theatre projection preserves physical-eye parallax");
        constexpr float questRadians = 0.01745329251994329577f;
        const float questEyes[2][3]{
            {-0.03295f, 0.0f, 0.0f}, {0.03295f, 0.0f, 0.0f}};
        const float questFovs[2][4]{
            {-54.0f * questRadians, 40.0f * questRadians,
              44.0f * questRadians, -55.0f * questRadians},
            {-40.0f * questRadians, 54.0f * questRadians,
              44.0f * questRadians, -55.0f * questRadians}};
        const float questScreenCenter[3]{0.0f, 0.0f, -5.3f};
        bool questNativeViewsContainScreen = true;
        for (int eye = 0; eye < 2; ++eye)
        {
            CutsceneTheaterClipVertex questVertices[4]{};
            questNativeViewsContainScreen &= BuildCutsceneTheaterProjectionQuad(
                questEyes[eye], projectionOrientation, questScreenCenter,
                projectionOrientation, 6.0f, 3.375f, questFovs[eye],
                questVertices);
            for (const auto& vertex : questVertices)
            {
                questNativeViewsContainScreen &= vertex.w > 0.0f &&
                    std::fabs(vertex.x) <= vertex.w * 1.001f &&
                    std::fabs(vertex.y) <= vertex.w * 1.001f;
            }
        }
        Check(questNativeViewsContainScreen,
            "runtime-native asymmetric Quest views contain the configured room screen");
        const float behindCenter[3]{0.0f, 0.0f, 1.0f};
        Check(!BuildCutsceneTheaterProjectionQuad(
                  projectionEye, projectionOrientation,
                  behindCenter, projectionOrientation,
                  6.0f, 3.375f, projectionFov, projectionVertices),
            "theatre projection rejects a screen behind the viewer");
        Check(std::fabs(CutsceneTheaterHeight(6.0f, 2912, 2100) -
                    6.0f * 2100.0f / 2912.0f) < 0.00001f &&
                  CutsceneTheaterHeight(6.0f, 0, 2100) == 0.0f,
            "theatre height preserves the native render-image aspect ratio");
        Check(std::fabs(CutsceneTheaterHeightFromAspect(
                    6.0f, 4.0f / 3.0f) - 4.5f) < 0.00001f &&
                  CutsceneTheaterHeightFromAspect(6.0f, 0.0f) == 0.0f,
            "theatre quad height follows the authored cinematic projection aspect");

        // Halo 3 rasterizes its cutscene into the headset render shape, which
        // this machine's accepted run reports as 3262x2352 (1.387:1). Hiding
        // everything outside the 16:9 slice a monitor shows retains 78% of that
        // height, split evenly into two bars.
        const CutsceneTheaterMatte headsetShapeMatte =
            ComputeCutsceneTheaterMatte(3262.0f / 2352.0f, 16.0f / 9.0f, 0.0f);
        Check(headsetShapeMatte.active &&
                  std::fabs((headsetShapeMatte.vMax - headsetShapeMatte.vMin) -
                            (3262.0f / 2352.0f) / (16.0f / 9.0f)) < 0.00001f &&
                  std::fabs((headsetShapeMatte.vMin + headsetShapeMatte.vMax) -
                            1.0f) < 0.00001f,
            "cine bars keep the centred 16:9 slice of the taller authored frame");
        const CutsceneTheaterMatte liftedMatte =
            ComputeCutsceneTheaterMatte(1.387f, 16.0f / 9.0f, 0.05f);
        Check(liftedMatte.active &&
                  std::fabs((liftedMatte.vMax - liftedMatte.vMin) -
                            (headsetShapeMatte.vMax -
                             headsetShapeMatte.vMin)) < 0.001f &&
                  liftedMatte.vMin < headsetShapeMatte.vMin,
            "a positive matte offset lifts the retained window without resizing it");
        const CutsceneTheaterMatte pinnedMatte =
            ComputeCutsceneTheaterMatte(1.387f, 16.0f / 9.0f, 0.5f);
        Check(pinnedMatte.active && pinnedMatte.vMin == 0.0f &&
                  std::fabs(pinnedMatte.vMax - (1.387f / (16.0f / 9.0f))) <
                      0.00001f,
            "an offset past the edge stops there at the full window size");
        Check(!ComputeCutsceneTheaterMatte(2.39f, 16.0f / 9.0f, 0.0f).active &&
                  !ComputeCutsceneTheaterMatte(1.387f, 0.0f, 0.0f).active &&
                  !ComputeCutsceneTheaterMatte(0.0f, 16.0f / 9.0f, 0.0f).active,
            "cine bars never rescale a frame already at or wider than the target");

        CutsceneTheaterTransition transition;
        Check(!transition.Update(1000, true).active,
            "theatre entry starts by fading the immersive view");
        const auto entryHalf = transition.Update(1050, true);
        const auto entered = transition.Update(1100, true);
        const auto entryFadeIn = transition.Update(1150, true);
        const auto entryDone = transition.Update(1200, true);
        Check(std::fabs(entryHalf.fadeAlpha - 0.5f) < 0.001f &&
                  entered.active && entered.switched && entered.fadeAlpha == 1.0f &&
                  entryFadeIn.active &&
                  std::fabs(entryFadeIn.fadeAlpha - 0.5f) < 0.001f &&
                  entryDone.active && entryDone.fadeAlpha == 0.0f,
            "theatre entry uses a 100 ms fade out and 100 ms fade in");
        const auto exitHalf = transition.Update(1250, false);
        const auto exited = transition.Update(1300, false);
        Check(exitHalf.active && std::fabs(exitHalf.fadeAlpha - 0.5f) < 0.001f &&
                  !exited.active && exited.switched && exited.fadeAlpha == 1.0f,
            "capability loss begins a comfort-faded theatre exit");
    }

    {
        constexpr uint64_t kEpoch = 1000;
        constexpr uint64_t kFreshFor = 500;
        constexpr uint32_t kUnknownCapability = 0x80000000u;
        constexpr uint32_t kPublishedCapabilities =
            TitleCapability_Stereo |
            TitleCapability_ControllerAim |
            TitleCapability_RuntimeModes |
            TitleCapability_ControllerInput |
            TitleCapability_Haptics |
            TitleCapability_CutsceneTheater;
        constexpr uint32_t kArmRequiredCapabilities =
            TitleCapability_Stereo |
            TitleCapability_ControllerAim |
            TitleCapability_Hud |
            TitleCapability_ArmIk |
            TitleCapability_RoomScale |
            TitleCapability_Haptics |
            TitleCapability_CutsceneTheater;
        constexpr uint32_t kUnarmedCapabilities =
            TitleCapability_RuntimeModes |
            TitleCapability_ControllerInput;

        const auto isCanonicalFailure = [](
            const TitleRuntimeSnapshot& snapshot, uint32_t ownerCount)
        {
            return snapshot.owner == GameTitle::None &&
                snapshot.generation == 0 &&
                !snapshot.installed && !snapshot.armed &&
                !snapshot.teardownRequested &&
                snapshot.mode == RuntimeMode::Shell &&
                snapshot.heartbeatMs == 0 &&
                snapshot.enabledCapabilities == TitleCapability_None &&
                snapshot.qualifyingOwnerCount == ownerCount;
        };

        TitleRuntimeResolveInput single{};
        single.availabilityMask =
            TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        single.availabilitySetEpochMs = kEpoch;
        single.nowMs = kEpoch + 1;
        single.titles[TitleRuntimeSlotIndex(GameTitle::Halo3)] = {
            GameTitle::Halo3,
            11,
            900,
            true,
            false,
            false,
            RuntimeMode::Vehicle,
            kEpoch + 1,
            kFreshFor,
            kPublishedCapabilities | kUnknownCapability,
        };

        const TitleRuntimeSnapshot owner = ResolveTitleRuntime(single);
        Check(owner.owner == GameTitle::Halo3 &&
                  owner.generation == 11 && owner.installed &&
                  !owner.armed && !owner.teardownRequested &&
                  owner.mode == RuntimeMode::Vehicle &&
                  owner.heartbeatMs == kEpoch + 1 &&
                  owner.enabledCapabilities == kPublishedCapabilities &&
                  owner.qualifyingOwnerCount == 1,
            "one fresh installed title owns an exact generation-tagged snapshot");
        Check(TitleRuntimeMaskUnarmedCapabilities(
                  owner, kArmRequiredCapabilities) == kUnarmedCapabilities,
            "an unarmed owner retains only caller-designated non-render capabilities");
        TitleRuntimeSnapshot armedOwner = owner;
        armedOwner.armed = true;
        armedOwner.enabledCapabilities |= kUnknownCapability;
        Check(TitleRuntimeMaskUnarmedCapabilities(
                  armedOwner, kArmRequiredCapabilities) ==
                  kPublishedCapabilities,
            "an armed owner keeps all known capabilities and strips unknown bits");

        TitleRuntimeResolveInput zero{};
        zero.availabilityMask =
            TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        zero.availabilitySetEpochMs = kEpoch;
        zero.nowMs = kEpoch + 1;
        Check(isCanonicalFailure(ResolveTitleRuntime(zero), 0),
            "zero fresh owners produces the canonical fail-open snapshot");

        auto epochBoundary = single;
        epochBoundary.nowMs = kEpoch;
        epochBoundary.titles[0].heartbeatMs = kEpoch;
        auto generationBoundary = single;
        generationBoundary.availabilitySetEpochMs = 800;
        generationBoundary.titles[0].generationStartMs = kEpoch + 1;
        generationBoundary.titles[0].heartbeatMs = kEpoch + 1;
        auto future = single;
        future.nowMs = kEpoch;
        auto zeroWindow = single;
        zeroWindow.titles[0].heartbeatFreshForMs = 0;
        Check(isCanonicalFailure(ResolveTitleRuntime(epochBoundary), 0) &&
                  isCanonicalFailure(
                      ResolveTitleRuntime(generationBoundary), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(future), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(zeroWindow), 0),
            "epoch-equal, generation-equal, future, and zero-window heartbeats fail closed");

        auto lastFresh = single;
        lastFresh.nowMs = kEpoch + 1 + kFreshFor - 1;
        auto expired = lastFresh;
        expired.nowMs += 1;
        Check(ResolveTitleRuntime(lastFresh).owner == GameTitle::Halo3 &&
                  isCanonicalFailure(ResolveTitleRuntime(expired), 0),
            "heartbeat freshness accepts age window-minus-one and rejects exact expiry");

        auto uninstalled = single;
        uninstalled.titles[0].installed = false;
        auto teardown = single;
        teardown.titles[0].teardownRequested = true;
        auto zeroGeneration = single;
        zeroGeneration.titles[0].generation = 0;
        auto unavailable = single;
        unavailable.availabilityMask = 0;
        auto foreign = single;
        foreign.titles[0].title = GameTitle::Halo3ODST;
        Check(isCanonicalFailure(ResolveTitleRuntime(uninstalled), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(teardown), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(zeroGeneration), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(unavailable), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(foreign), 0),
            "uninstalled, teardown, zero-generation, unavailable, and foreign claims fail closed");

        auto multiple = single;
        multiple.availabilityMask |=
            TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        multiple.nowMs = kEpoch + 2;
        multiple.titles[TitleRuntimeSlotIndex(GameTitle::Halo3ODST)] = {
            GameTitle::Halo3ODST,
            22,
            900,
            true,
            true,
            false,
            RuntimeMode::Paused,
            kEpoch + 2,
            kFreshFor,
            TitleCapability_Stereo,
        };
        Check(isCanonicalFailure(ResolveTitleRuntime(multiple), 2),
            "two fresh title owners discard both complete snapshots");
        multiple.nowMs = kEpoch + 1 + kFreshFor;
        const TitleRuntimeSnapshot soleRemainder = ResolveTitleRuntime(multiple);
        Check(soleRemainder.owner == GameTitle::Halo3ODST &&
                  soleRemainder.generation == 22 &&
                  soleRemainder.mode == RuntimeMode::Paused &&
                  soleRemainder.qualifyingOwnerCount == 1,
            "one title wins only after the other expires at the exact freshness boundary");

        TitleRuntimeResolveInput pending{};
        pending.availabilityMask =
            TitleRuntimeAvailabilityBit(GameTitle::Halo3) |
            TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        pending.availabilitySetEpochMs = kEpoch;
        pending.nowMs = kEpoch;
        pending.titles[TitleRuntimeSlotIndex(GameTitle::Halo3)] = {
            GameTitle::Halo3,
            11,
            900,
            true,
            true,
            false,
            RuntimeMode::Gameplay,
            kEpoch,
            kFreshFor,
            kPublishedCapabilities,
        };
        Check(TitleRuntimeOwnershipMayBePending(
                  pending, GameTitle::Halo3, 100),
            "a safe retained owner gets a short zero-owner post-transition grace");
        pending.nowMs = kEpoch + 99;
        Check(TitleRuntimeOwnershipMayBePending(
                  pending, GameTitle::Halo3, 100),
            "pending ownership remains valid through grace-minus-one");
        pending.nowMs = kEpoch + 100;
        Check(!TitleRuntimeOwnershipMayBePending(
                  pending, GameTitle::Halo3, 100),
            "pending ownership expires at the exact grace boundary");
        pending.nowMs = kEpoch - 1;
        Check(!TitleRuntimeOwnershipMayBePending(
                  pending, GameTitle::Halo3, 100),
            "a future availability-set epoch cannot grant pending ownership");

        auto soleModulePending = pending;
        soleModulePending.nowMs = kEpoch;
        soleModulePending.availabilityMask =
            TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        auto teardownPending = pending;
        teardownPending.nowMs = kEpoch;
        teardownPending.titles[0].teardownRequested = true;
        auto uninstalledPending = pending;
        uninstalledPending.nowMs = kEpoch;
        uninstalledPending.titles[0].installed = false;
        auto duplicatePending = pending;
        duplicatePending.nowMs = kEpoch;
        duplicatePending.titles[TitleRuntimeSlotIndex(GameTitle::HaloReach)] =
            duplicatePending.titles[0];
        auto multiplePending = multiple;
        multiplePending.nowMs = kEpoch + 2;
        Check(!TitleRuntimeOwnershipMayBePending(
                  soleModulePending, GameTitle::Halo3, 100) &&
                  !TitleRuntimeOwnershipMayBePending(
                      teardownPending, GameTitle::Halo3, 100) &&
                  !TitleRuntimeOwnershipMayBePending(
                      uninstalledPending, GameTitle::Halo3, 100) &&
                  !TitleRuntimeOwnershipMayBePending(
                      duplicatePending, GameTitle::Halo3, 100),
            "sole-title, teardown, uninstalled, and duplicate retained claims get no grace");
        Check(!TitleRuntimeOwnershipMayBePending(
                  multiplePending, GameTitle::Halo3, 100),
            "multiple fresh owners fail closed immediately without pending grace");
    }

    {
        constexpr size_t kHalo3Slot =
            TitleRuntimeSlotIndex(GameTitle::Halo3);
        constexpr size_t kOdstSlot =
            TitleRuntimeSlotIndex(GameTitle::Halo3ODST);
        constexpr size_t kReachSlot =
            TitleRuntimeSlotIndex(GameTitle::HaloReach);
        constexpr uintptr_t kHalo3Base = 0x100000;
        constexpr uintptr_t kHalo3ReboundBase = 0x110000;
        constexpr uintptr_t kOdstBase = 0x200000;

        TitleRuntimeState state;
        const TitleRuntimeAvailabilitySnapshot initial =
            state.LoadAvailability();
        Check(initial.stable && initial.availabilityMask == 0 &&
                  initial.availabilitySetEpochMs == 0 &&
                  state.Generation(GameTitle::Halo3) == 0 &&
                  state.Generation(GameTitle::None) == 0,
            "title-runtime atomic state starts stable, unavailable, and generation zero");

        TitleRuntimeModuleSet modules{};
        modules.availabilityMask =
            TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        modules.moduleBases[kHalo3Slot] = kHalo3Base;
        Check(state.PublishModuleSet(modules, 100) &&
                  state.Generation(GameTitle::Halo3) == 1,
            "the first Halo 3 module load creates generation one");
        TitleRuntimeAvailabilitySnapshot availability =
            state.LoadAvailability();
        Check(availability.stable &&
                  availability.availabilityMask == modules.availabilityMask &&
                  availability.availabilitySetEpochMs == 100 &&
                  availability.moduleBases[kHalo3Slot] == kHalo3Base,
            "the first available set publishes its exact base and epoch");

        Check(state.PublishModuleSet(modules, 150) &&
                  state.Generation(GameTitle::Halo3) == 1 &&
                  state.LoadAvailability().availabilitySetEpochMs == 100,
            "an unchanged module poll preserves generation and set epoch");

        modules.availabilityMask |=
            TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = kOdstBase;
        Check(state.PublishModuleSet(modules, 200) &&
                  state.Generation(GameTitle::Halo3) == 1 &&
                  state.Generation(GameTitle::Halo3ODST) == 1 &&
                  state.LoadAvailability().availabilitySetEpochMs == 200,
            "adding ODST advances the set epoch without changing resident Halo 3 generation");

        const TitleRuntimeLifecycle odstLifecycle{
            true, false, false, TitleCapability_ControllerInput
        };
        const uint32_t odstGeneration =
            state.Generation(GameTitle::Halo3ODST);
        Check(state.PublishLifecycle(
                  GameTitle::Halo3ODST, odstGeneration, odstLifecycle) &&
                  state.PublishHeartbeat(
                      GameTitle::Halo3ODST, odstGeneration, 201),
            "resident ODST establishes a pre-rebind heartbeat");

        modules.moduleBases[kHalo3Slot] = kHalo3ReboundBase;
        Check(state.PublishModuleSet(modules, 250) &&
                  state.Generation(GameTitle::Halo3) == 2 &&
                  state.Generation(GameTitle::Halo3ODST) == 1 &&
                  state.LoadAvailability().availabilitySetEpochMs == 250,
            "a Halo 3 base change advances its generation and the complete-set epoch");
        TitleRuntimeHeartbeatPolicy baseChangePolicy{};
        baseChangePolicy.freshForMs[kOdstSlot] = 500;
        Check(state.Resolve(250, baseChangePolicy).owner == GameTitle::None,
            "a pre-rebind heartbeat from another resident title cannot survive the complete-set epoch");

        modules.availabilityMask &=
            ~TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = 0;
        Check(state.PublishModuleSet(modules, 300) &&
                  state.Generation(GameTitle::Halo3ODST) == 2 &&
                  state.LoadAvailability().availabilitySetEpochMs == 300,
            "ODST removal invalidates its load generation and advances the set epoch");
        modules.availabilityMask |=
            TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = kOdstBase;
        Check(state.PublishModuleSet(modules, 400) &&
                  state.Generation(GameTitle::Halo3ODST) == 3 &&
                  state.LoadAvailability().availabilitySetEpochMs == 400,
            "ODST reload receives generation three rather than reusing stale state");

        const uint32_t h3GenerationBeforeInvalid =
            state.Generation(GameTitle::Halo3);
        const uint32_t odstGenerationBeforeInvalid =
            state.Generation(GameTitle::Halo3ODST);
        auto unknownBitSet = modules;
        unknownBitSet.availabilityMask |= 0x80000000u;
        auto inconsistentSet = modules;
        inconsistentSet.moduleBases[kReachSlot] = 0x300000;
        Check(!state.PublishModuleSet(unknownBitSet, 450) &&
                  !state.PublishModuleSet(inconsistentSet, 450) &&
                  state.Generation(GameTitle::Halo3) ==
                      h3GenerationBeforeInvalid &&
                  state.Generation(GameTitle::Halo3ODST) ==
                      odstGenerationBeforeInvalid,
            "unknown availability bits and mask/base disagreement are rejected atomically");

        modules.availabilityMask &=
            ~TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = 0;
        Check(state.PublishModuleSet(modules, 350) &&
                  state.Generation(GameTitle::Halo3ODST) == 4 &&
                  state.LoadAvailability().availabilitySetEpochMs == 400,
            "a non-monotonic module observation cannot move the set epoch backward");
    }

    {
        constexpr size_t kHalo3Slot =
            TitleRuntimeSlotIndex(GameTitle::Halo3);
        constexpr size_t kOdstSlot =
            TitleRuntimeSlotIndex(GameTitle::Halo3ODST);
        constexpr uint64_t kFreshFor = 500;
        constexpr uint32_t kUnknownCapability = 0x80000000u;
        constexpr uint32_t kCapabilities =
            TitleCapability_Stereo |
            TitleCapability_ControllerAim |
            TitleCapability_ControllerInput |
            TitleCapability_Haptics;

        TitleRuntimeState state;
        TitleRuntimeModuleSet modules{};
        modules.availabilityMask =
            TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        modules.moduleBases[kHalo3Slot] = 0x100000;
        Check(state.PublishModuleSet(modules, 100),
            "atomic publication test establishes a Halo 3 module generation");
        const uint32_t generation1 = state.Generation(GameTitle::Halo3);

        Check(!state.PublishMode(
                  GameTitle::Halo3, generation1, RuntimeMode::Gameplay),
            "mode publication requires a current installed lifecycle");
        const TitleRuntimeLifecycle unarmed{
            true, false, false, kCapabilities | kUnknownCapability
        };
        Check(state.PublishLifecycle(
                  GameTitle::Halo3, generation1, unarmed) &&
                  state.PublishMode(
                      GameTitle::Halo3, generation1, RuntimeMode::Vehicle) &&
                  !state.PublishMode(
                      GameTitle::Halo3, generation1,
                      static_cast<RuntimeMode>(255)),
            "current lifecycle tags a valid mode and rejects invalid mode values");
        Check(!state.PublishHeartbeat(GameTitle::Halo3, generation1, 100) &&
                  state.PublishHeartbeat(
                      GameTitle::Halo3, generation1, 101) &&
                  !state.PublishHeartbeat(
                      GameTitle::Halo3, generation1, 101) &&
                  !state.PublishHeartbeat(
                      GameTitle::Halo3, generation1, 100),
            "heartbeats must be post-epoch and strictly monotonic within a generation");

        TitleRuntimeCandidate candidate{};
        Check(state.LoadCandidate(
                  GameTitle::Halo3, kFreshFor, candidate) &&
                  candidate.title == GameTitle::Halo3 &&
                  candidate.generation == generation1 &&
                  candidate.generationStartMs == 100 &&
                  candidate.installed && !candidate.armed &&
                  !candidate.teardownRequested &&
                  candidate.mode == RuntimeMode::Vehicle &&
                  candidate.heartbeatMs == 101 &&
                  candidate.heartbeatFreshForMs == kFreshFor &&
                  candidate.enabledCapabilities == kCapabilities,
            "atomic candidate loads one coherent lifecycle, mode, heartbeat, and sanitized mask");

        TitleRuntimeHeartbeatPolicy policy{};
        policy.freshForMs[kHalo3Slot] = kFreshFor;
        TitleRuntimeSnapshot snapshot = state.Resolve(101, policy);
        Check(snapshot.owner == GameTitle::Halo3 &&
                  snapshot.generation == generation1 &&
                  !snapshot.armed &&
                  snapshot.mode == RuntimeMode::Vehicle &&
                  snapshot.enabledCapabilities == kCapabilities,
            "the production atomic resolver preserves an unarmed unique owner");

        modules.availabilityMask |=
            TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = 0x200000;
        Check(state.PublishModuleSet(modules, 200) &&
                  state.Resolve(200, policy).owner == GameTitle::None &&
                  !state.PublishHeartbeat(
                      GameTitle::Halo3, generation1, 200) &&
                  state.PublishHeartbeat(
                      GameTitle::Halo3, generation1, 201) &&
                  state.Resolve(201, policy).owner == GameTitle::Halo3,
            "a set transition requires a strictly newer heartbeat from a resident generation");

        modules.moduleBases[kHalo3Slot] = 0x110000;
        Check(state.PublishModuleSet(modules, 250),
            "a rebound Halo 3 base starts a new atomic generation");
        const uint32_t generation2 = state.Generation(GameTitle::Halo3);
        Check(generation2 == 2 &&
                  !state.PublishLifecycle(
                      GameTitle::Halo3, generation1, unarmed) &&
                  !state.PublishMode(
                      GameTitle::Halo3, generation1, RuntimeMode::Paused) &&
                  !state.PublishHeartbeat(
                      GameTitle::Halo3, generation1, 251) &&
                  !state.ClearHeartbeat(GameTitle::Halo3, generation1),
            "stale lifecycle, mode, heartbeat, and clear publications are rejected");
        Check(!state.PublishLifecycle(
                  GameTitle::Halo3ODST, generation2, unarmed) &&
                  !state.PublishMode(
                      GameTitle::Halo3ODST, generation2,
                      RuntimeMode::Gameplay) &&
                  !state.PublishHeartbeat(
                      GameTitle::Halo3ODST, generation2, 251) &&
                  !state.ClearHeartbeat(
                      GameTitle::Halo3ODST, generation2),
            "a generation token from another title cannot publish foreign state");

        Check(state.LoadCandidate(
                  GameTitle::Halo3, kFreshFor, candidate) &&
                  candidate.generation == generation2 &&
                  candidate.generationStartMs == 250 &&
                  !candidate.installed && !candidate.armed &&
                  !candidate.teardownRequested &&
                  candidate.mode == RuntimeMode::Shell &&
                  candidate.heartbeatMs == 0 &&
                  candidate.enabledCapabilities == TitleCapability_None,
            "a new module generation cannot inherit prior lifecycle, mode, heartbeat, or capabilities");
        Check(!state.PublishMode(
                  GameTitle::Halo3, generation2, RuntimeMode::Gameplay) &&
                  state.PublishLifecycle(
                      GameTitle::Halo3, generation2, unarmed) &&
                  state.PublishMode(
                      GameTitle::Halo3, generation2, RuntimeMode::Vehicle) &&
                  !state.PublishHeartbeat(
                      GameTitle::Halo3, generation2, 250) &&
                  state.PublishHeartbeat(
                      GameTitle::Halo3, generation2, 251),
            "the new generation admits mode and heartbeat only after its lifecycle and boundary");
        snapshot = state.Resolve(251, policy);
        Check(snapshot.owner == GameTitle::Halo3 &&
                  snapshot.generation == generation2 &&
                  snapshot.mode == RuntimeMode::Vehicle,
            "current generation publications restore one coherent owner");
        Check(!state.ClearHeartbeat(
                  GameTitle::Halo3ODST, generation2) &&
                  state.Resolve(251, policy).owner == GameTitle::Halo3 &&
                  state.ClearHeartbeat(GameTitle::Halo3, generation2) &&
                  state.Resolve(251, policy).owner == GameTitle::None &&
                  state.LoadCandidate(
                      GameTitle::Halo3, kFreshFor, candidate) &&
                  candidate.heartbeatMs == 0,
            "only the exact title generation can clear a resident heartbeat");

        Check(state.PublishHeartbeat(
                  GameTitle::Halo3, generation2, 252),
            "a cleared current generation can publish a fresh heartbeat again");
        const TitleRuntimeLifecycle teardown{
            true, true, true, kCapabilities
        };
        Check(state.PublishLifecycle(
                  GameTitle::Halo3, generation2, teardown) &&
                  !state.PublishMode(
                      GameTitle::Halo3, generation2, RuntimeMode::Paused) &&
                  state.Resolve(252, policy).owner == GameTitle::None,
            "teardown vetoes ownership and further runtime-mode publication");
        const TitleRuntimeLifecycle notInstalled{
            false, false, false, kCapabilities
        };
        Check(state.PublishLifecycle(
                  GameTitle::Halo3, generation2, notInstalled) &&
                  state.Resolve(252, policy).owner == GameTitle::None,
            "an uninstalled title cannot own through a retained fresh heartbeat");
        Check(state.PublishLifecycle(
                  GameTitle::Halo3, generation2, unarmed) &&
                  state.PublishMode(
                      GameTitle::Halo3, generation2,
                      RuntimeMode::Gameplay) &&
                  state.Resolve(252, policy).owner == GameTitle::Halo3 &&
                  state.Resolve(252, policy).mode == RuntimeMode::Gameplay,
            "a safe unarmed lifecycle restores its exact generation-tagged mode");

        Check(!state.PublishLifecycle(
                  GameTitle::None, generation2, unarmed) &&
                  !state.PublishMode(
                      GameTitle::Unknown, generation2,
                      RuntimeMode::Gameplay) &&
                  !state.PublishHeartbeat(
                      GameTitle::None, generation2, 253) &&
                  !state.ClearHeartbeat(GameTitle::Unknown, generation2) &&
                  !state.LoadCandidate(
                      GameTitle::None, kFreshFor, candidate),
            "non-module titles cannot enter the atomic runtime state API");
    }

    {
        constexpr size_t kReachSlot =
            TitleRuntimeSlotIndex(GameTitle::HaloReach);
        TitleRuntimeState reachState;
        TitleRuntimeModuleSet modules{};
        modules.availabilityMask =
            TitleRuntimeAvailabilityBit(GameTitle::HaloReach);
        modules.moduleBases[kReachSlot] = 0x300000;
        Check(reachState.PublishModuleSet(modules, 100),
            "the generic runtime state can record Reach as available");
        const uint32_t generation =
            reachState.Generation(GameTitle::HaloReach);
        const TitleRuntimeLifecycle evidenceOnly{
            true, false, false, TitleCapability_None
        };
        Check(reachState.PublishLifecycle(
                  GameTitle::HaloReach, generation, evidenceOnly) &&
                  reachState.PublishMode(
                      GameTitle::HaloReach, generation,
                      RuntimeMode::Unsupported) &&
                  reachState.PublishHeartbeat(
                      GameTitle::HaloReach, generation, 101),
            "a synthetic Reach observation is generation tagged without enabling behavior");
        TitleRuntimeHeartbeatPolicy policy{};
        policy.freshForMs[kReachSlot] = 500;
        const TitleRuntimeSnapshot reachSnapshot =
            reachState.Resolve(101, policy);
        Check(reachSnapshot.owner == GameTitle::HaloReach &&
                  reachSnapshot.generation == generation &&
                  !reachSnapshot.armed &&
                  reachSnapshot.mode == RuntimeMode::Unsupported &&
                  reachSnapshot.enabledCapabilities == TitleCapability_None &&
                  TitleRuntimeMaskUnarmedCapabilities(
                      reachSnapshot, kTitleRuntimeKnownCapabilities) ==
                      TitleCapability_None,
            "the shared foundation manufactures zero Reach capabilities");
    }

    {
        Check(!OdstCameraOnlyScopeRequired(false, true, false),
            "a public build never claims the private ODST camera core");
        Check(OdstCameraOnlyScopeRequired(true, true, false),
            "the adapter transition remains camera-only after module polling");
        Check(OdstCameraOnlyScopeRequired(false, false, true),
            "owned teardown state remains isolated until presentation detaches");
        Check(OdstManualArmEligible(true, true, true, false),
            "stable camera plus explicit head/stereo toggles permits manual arm");
        Check(!OdstManualArmEligible(false, true, true, false),
            "manual ODST arm still requires the fresh-camera debounce");
        Check(!OdstManualArmEligible(true, true, true, true),
            "teardown always vetoes manual ODST arm");
        OdstHalo3LookAngles look{};
        Check(ComputeOdstHalo3LookAngles(
                  1.0f, 0.25f, 0.5f, 0.2f, 0.4f, -1.0f, 1.0f, 0.1f, look),
            "ODST Halo 3 look ownership accepts finite tracked angles");
        Check(std::fabs(look.yaw - 0.75f) < 1e-5f &&
                  std::fabs(look.pitch - 0.3f) < 1e-5f &&
                  std::fabs(look.roll - 0.4f) < 1e-5f,
            "ODST look matches Halo 3 recentered-yaw and absolute pitch/roll");
        Check(ComputeOdstHalo3LookAngles(
                  0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 1.0f, 1.0f, 0.0f, look) &&
                  std::fabs(look.pitch - 1.5f) < 1e-5f,
            "ODST Halo 3 look retains the proven pitch safety clamp");
        Check(OdstVrOwnsLookStick(true, true),
            "tracked private ODST consumes the stock look-stick axes");
        Check(!OdstVrOwnsLookStick(true, false),
            "ODST menus retain ordinary look-stick input before head tracking");
        Check(!OdstVrOwnsLookStick(false, true),
            "the ODST input rule cannot affect Halo 3 or public title paths");
        Check(OdstMotionAimEligible(true, true, true, false),
            "owned+armed+tracked private ODST permits the narrow motion-aim gate");
        Check(!OdstMotionAimEligible(false, true, true, false),
            "ODST motion aim requires the camera-only context (never a public title)");
        Check(!OdstMotionAimEligible(true, false, true, false),
            "ODST motion aim stays closed until the camera hooks are armed");
        Check(!OdstMotionAimEligible(true, true, false, false),
            "ODST motion aim follows head tracking off");
        Check(!OdstMotionAimEligible(true, true, true, true),
            "teardown always vetoes ODST motion aim");
        Check(OdstFirstPersonControlBlend(1.0f) &&
                  OdstFirstPersonControlBlend(0.998f),
            "on-foot and near-1 camera transitions retain the FP render classification");
        Check(!OdstFirstPersonControlBlend(0.0f),
            "blend-0 cameras stay outside the FP-only render classification");
        Check(OdstShouldStereoRedirect(true, true, true, true),
            "a proven slot-0 camera is stereo-redirected");
        Check(!OdstShouldStereoRedirect(true, true, true, false),
            "a non-redirectable custom camera renders stock");
        Check(!OdstShouldStereoRedirect(false, true, true, true),
            "a foreign camera slot is never stereo-redirected");
        Check(!OdstShouldStereoRedirect(true, false, true, true),
            "a broken single-user tail is never stereo-redirected");
        Check(!OdstShouldStereoRedirect(true, true, false, true),
            "a mismatched nested FP source is never stereo-redirected");
        Check(OdstCamCopyRequestsTeardown(true, true, false),
            "a broken slot-0 single-user tail tears down (level unload/transition)");
        Check(!OdstCamCopyRequestsTeardown(true, true, true),
            "an active non-FP camera with a valid tail never tears down (3D recovers)");
        Check(!OdstCamCopyRequestsTeardown(false, true, false),
            "camera-copy teardown requires the core to be armed");
        Check(!OdstCamCopyRequestsTeardown(true, false, false),
            "camera-copy teardown only fires for our own primary slot");
        Check(PausePresentationInputAllowed(true),
            "proven Halo 3 gameplay may control pause presentation");
        Check(!PausePresentationInputAllowed(false),
            "MCC shell and private ODST Start edges cannot head-lock presentation");
        Check(ReachShouldSwapLeftHandActions(true, false) &&
              !ReachShouldSwapLeftHandActions(true, true) &&
              !ReachShouldSwapLeftHandActions(false, false) &&
              !ReachShouldSwapLeftHandActions(false, true),
            "Reach swaps LT/X only on foot and restores the native layout in every proven vehicle");
        Check(PauseToggleInputAllowed(true, false),
            "Halo 3 ownership admits the Y+B pause fallback");
        Check(PauseToggleInputAllowed(false, true),
            "ODST, Reach, or Halo 4 ownership admits the Y+B pause fallback");
        Check(!PauseToggleInputAllowed(false, false),
            "Y+B cannot inject Start without supported title ownership");
        Check(TitleSpecificPauseToggleOwner(GameTitle::Unknown, true) &&
                  TitleSpecificPauseToggleOwner(GameTitle::HaloReach, false) &&
                  TitleSpecificPauseToggleOwner(GameTitle::Halo4, false) &&
                  !TitleSpecificPauseToggleOwner(GameTitle::Halo3, false) &&
                  !TitleSpecificPauseToggleOwner(GameTitle::Unknown, false),
            "the title-specific Y+B admission includes Halo 4 without broadening unsupported ownership");
        Check(OdstMustClearForeignPause(true, true, false) &&
                  OdstMustClearForeignPause(true, false, true),
            "private ODST entry clears either pending or active foreign pause state");
        Check(!OdstMustClearForeignPause(false, true, true),
            "foreign pause cleanup cannot affect non-ODST title ownership");
        Check(OdstNestedSourceIsCompatible(0, 0x1234),
            "ODST installation may precede the first nested FP source publish");
        Check(OdstNestedSourceIsCompatible(0x1234, 0x1234),
            "ODST accepts the proven nested FP source pointer");
        Check(!OdstNestedSourceIsCompatible(0x5678, 0x1234),
            "ODST rejects a nested FP source owned by another camera");
        Check(OdstInactiveCameraSlotsAreSafe(false, false, false),
            "constructed but inactive ODST split-screen slots are safe");
        Check(!OdstInactiveCameraSlotsAreSafe(false, true, false),
            "another active ODST camera blocks the single-user bring-up");
        Check(EvaluateOdstStereoFrame(false) ==
                  OdstStereoFrameAction::RenderStockWithoutCapture,
            "OpenXR no-render frames preserve ODST hooks without eye validation");
        Check(EvaluateOdstStereoFrame(true) ==
                  OdstStereoFrameAction::RenderStereoAndValidate,
            "active OpenXR frames still require validated stereo eye redirects");

        OdstHalo3FovMatch matchedFov{};
        Check(ComputeOdstHalo3FovMatch(
                  std::atan(1.8418f), std::atan(1.3290f), matchedFov),
            "ODST accepts the live Halo 3 headset FOV pair");
        Check(std::fabs(matchedFov.compactVerticalInput - 1.8418f) < 0.0001f &&
                  std::fabs(matchedFov.compactReferenceInput - 1.3290f) < 0.0001f,
            "ODST feeds both compact FOV inputs from Halo 3's matched pair");
        Check(std::fabs(matchedFov.projectionX - 0.54295f) < 0.0001f &&
                  std::fabs(matchedFov.projectionY - 0.75244f) < 0.0001f,
            "ODST reproduces the live Halo 3 projection scales");
        Check(!ComputeOdstHalo3FovMatch(0.0f, 0.9f, matchedFov),
            "ODST rejects an invalid headset FOV pair");

        for (int count : {41, 42, 43, 45, 46})
        {
            OdstFpSkeletonLayout fp{};
            Check(ComputeOdstFpSkeletonLayout(count, fp),
                "official ODST FP combined-skeleton counts are accepted");
            Check(fp.rightShoulder == 2 && fp.rightElbow == 4 &&
                      fp.rightWrist == 6 && fp.leftShoulder == 1 &&
                      fp.leftElbow == 3 && fp.leftWrist == 5,
                "ODST uses its editing-kit-proven arm chains");
            Check(fp.cameraControl == count - 1,
                "ODST camera_control is the final combined node");
            Check((fp.leftHandDescendants & (uint64_t{1} << 5)) != 0 &&
                      (fp.leftHandDescendants & (uint64_t{1} << 6)) == 0,
                "ODST left-hand mask owns only the authored left subtree");
            Check((fp.rightHandAndWeaponDescendants &
                       (uint64_t{1} << 6)) != 0 &&
                      (fp.rightHandAndWeaponDescendants &
                       (uint64_t{1} << 37)) != 0 &&
                      (fp.rightHandAndWeaponDescendants &
                       (uint64_t{1} << (fp.cameraControl - 1))) != 0 &&
                      (fp.rightHandAndWeaponDescendants &
                       (uint64_t{1} << fp.cameraControl)) == 0,
                "ODST right carrier owns weapon nodes but excludes camera_control");
        }
        OdstFpSkeletonLayout invalidFp{};
        Check(!ComputeOdstFpSkeletonLayout(38, invalidFp) &&
                  !ComputeOdstFpSkeletonLayout(65, invalidFp),
            "ODST rejects unsafe or structurally incomplete FP skeletons");

        Check(TitleRegistry_AllowsSharedGameplayFeatures(
                  GameTitle::None, false, false),
            "the MCC shell retains shared controller behavior");
        Check(TitleRegistry_AllowsSharedGameplayFeatures(
                  GameTitle::Halo3, false, false),
            "an explicitly detected Halo 3 session retains shared features");
        Check(TitleRegistry_AllowsSharedGameplayFeatures(
                  GameTitle::Unknown, true, false),
            "a fresh Halo 3 camera heartbeat resolves resident-module ambiguity");
        Check(!TitleRegistry_AllowsSharedGameplayFeatures(
                  GameTitle::Halo3ODST, false, false),
            "public ODST XInput and presentation remain pass-through");
        Check(!TitleRegistry_AllowsSharedGameplayFeatures(
                  GameTitle::Halo3ODST, true, false),
            "an explicit unsupported title beats a stale Halo 3 heartbeat");
        Check(!TitleRegistry_AllowsSharedGameplayFeatures(
                  GameTitle::Unknown, false, false),
            "an ambiguous title without camera ownership fails closed");
        Check(TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Unknown, false, false, true, false),
            "the private frontend exception retains controller input");
        Check(TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Unknown, true, false, false, false),
            "a resolved owner's controller capability admits ambiguous input");
        Check(!TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Unknown, false, false, false, false),
            "the normal build keeps ambiguous title input fail-closed");
        Check(TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::None, false, false, false, false),
            "the unambiguous MCC shell retains ordinary controller input");
        Check(TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Halo3, false, false, false, true),
            "explicit Halo 3 retains ordinary controller input");
        Check(TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Halo3ODST, false, true, false, true),
            "private ODST camera ownership permits ordinary gamepad input");
        Check(TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Halo3ODST, false, false, false, true),
            "private ODST admits controller input before camera ownership");
        Check(!TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Halo3ODST, false, true, false, false),
            "public ODST camera ownership keeps controller input stock");
        Check(!TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::HaloCE, false, false, true, false),
            "an explicit title without controller admission keeps stock input");
        Check(!TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Unknown, false, true, true, true),
            "owned ODST teardown beats resident-module ambiguity");
        Check(TitleRegistry_AllowsSharedControllerInput(
                  GameTitle::Unknown, true, true, true, true),
            "a unique ambiguous owner may retain ordinary controller input");
        Check(!TitleRegistry_AllowsSharedGameplayFeatures(
                  GameTitle::Halo3, true, true),
            "private camera ownership overrides a stale Halo 3 signal");
        Check(!TitleRegistry_Halo3CameraOwnsAmbiguousState(
                  1050, 999, 1000),
            "an ambiguous title cannot inherit a pre-transition heartbeat");
        Check(!TitleRegistry_Halo3CameraOwnsAmbiguousState(
                  1050, 1000, 1000),
            "a heartbeat at the transition boundary is not new ownership");
        Check(TitleRegistry_Halo3CameraOwnsAmbiguousState(
                  1099, 1001, 1000),
            "a post-transition Halo 3 heartbeat owns a short ambiguous window");
        Check(!TitleRegistry_Halo3CameraOwnsAmbiguousState(
                  1101, 1001, 1000),
            "ambiguous Halo 3 ownership expires at the dedicated 100 ms limit");
        Check(!TitleRegistry_Halo3CameraOwnsAmbiguousState(
                  999, 1001, 1000),
            "a non-monotonic camera timestamp fails closed");

        OdstCameraRearmGate gate;
        Check(gate.CanAttemptInstall(), "ODST camera install begins unblocked");
        gate.BlockUntilReload(true);
        Check(!gate.CanAttemptInstall(),
            "stale active camera memory cannot immediately rearm ODST hooks");
        gate.Observe(true, true);
        Check(!gate.CanAttemptInstall(),
            "an active-to-active observation is not a reload edge");
        gate.Observe(true, false);
        Check(!gate.CanAttemptInstall(),
            "an inactive camera waits for the next active level");
        gate.Observe(true, true);
        Check(gate.CanAttemptInstall(),
            "inactive-to-active camera transition rearms ODST hooks");
        gate.BlockUntilReload(true);
        gate.Observe(false, false);
        Check(gate.CanAttemptInstall(),
            "a genuine title exit clears the ODST rearm gate");

        gate.BlockUntilTitleExit();
        gate.Observe(true, false);
        gate.Observe(true, true);
        Check(!gate.CanAttemptInstall(),
            "unsupported/menu camera transitions cannot rearm in-session");
        gate.Observe(false, false);
        Check(gate.CanAttemptInstall(),
            "title exit clears the unsupported-camera session latch");

        OdstPauseRearmGate pauseGate;
        const uint64_t pauseStable = kOdstPauseRearmStableMs;
        pauseGate.Block();
        pauseGate.Observe(100, true, true, true);
        Check(!pauseGate.CanAttemptInstall(),
            "native ODST pause blocks camera-hook reinstallation");
        pauseGate.Observe(200, true, false, true);
        pauseGate.Observe(200 + pauseStable, true, false, true);
        Check(!pauseGate.CanAttemptInstall(),
            "pause exit requires more than the pause-rearm stable interval");
        pauseGate.Observe(201 + pauseStable, true, false, true);
        Check(pauseGate.CanAttemptInstall(),
            "stable gameplay after pause exit permits camera-hook reinstall");
        // Flicker tolerance: with the copy hook removed the live camera array
        // reads ready/not-ready frame-to-frame. A momentary not-ready sample
        // must NOT restart the settle window (a continuous-ready reset stalled
        // the post-pause rearm for tens of seconds in the headset log).
        pauseGate.Block();
        pauseGate.Observe(5000, true, false, true);   // pause cleared, camera seen live
        pauseGate.Observe(5100, true, false, false);  // momentary not-ready flicker
        pauseGate.Observe(5000 + pauseStable, true, false, false);
        Check(!pauseGate.CanAttemptInstall(),
            "a not-ready flicker does not release the gate before the window");
        pauseGate.Observe(5001 + pauseStable, true, false, true);
        Check(pauseGate.CanAttemptInstall(),
            "a momentary flicker no longer restarts the pause-exit window");
        // A genuine re-pause, however, does restart the window from its exit.
        pauseGate.Block();
        pauseGate.Observe(6000, true, false, true);   // window starts at 6000
        pauseGate.Observe(6100, true, true, true);    // re-pause before it elapses
        Check(!pauseGate.CanAttemptInstall(),
            "a re-pause during the settle window blocks reinstall again");
        pauseGate.Observe(6200, true, false, true);   // window restarts at 6200
        pauseGate.Observe(6200 + pauseStable, true, false, true);
        Check(!pauseGate.CanAttemptInstall(),
            "the re-pause restarted the settle window from its exit");
        pauseGate.Observe(6201 + pauseStable, true, false, true);
        Check(pauseGate.CanAttemptInstall(),
            "a full settle window after the last pause exit clears the gate");
        pauseGate.Block();
        pauseGate.Observe(9000, false, false, false);
        Check(pauseGate.CanAttemptInstall(),
            "title exit clears the native-pause reinstall gate");

        const HudLayoutAdapter* halo3Hud =
            HudLayoutAdapterFor(HudLayoutProfile::Halo3);
        const HudLayoutAdapter* odstHud =
            HudLayoutAdapterFor(HudLayoutProfile::Halo3ODST);
        Check(halo3Hud && halo3Hud->expectedBlocks == 3,
            "Halo 3 HUD layout retains its three proven skin blocks");
        Check(odstHud && odstHud->expectedBlocks == 1,
            "ODST HUD layout accepts only its uniquely proven globals block");
        const HudLayoutAdapter* reachHud =
            HudLayoutAdapterFor(HudLayoutProfile::HaloReach);
        constexpr uint8_t expectedHalo3HudAnchor[24] = {
            0x00, 0x05, 0x00, 0x00, 0xD0, 0x02, 0x00, 0x00,
            0x00, 0x00, 0x5C, 0x42, 0x00, 0x40, 0x25, 0x44,
            0x00, 0x00, 0x68, 0x42, 0x00, 0x00, 0x80, 0x40,
        };
        constexpr uint8_t expectedOdstHudAnchor[24] = {
            0x00, 0x05, 0x00, 0x00, 0xD0, 0x02, 0x00, 0x00,
            0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0xFA, 0x44,
            0x00, 0x00, 0x68, 0x42, 0x00, 0x00, 0x80, 0x40,
        };
        const auto anchorPrefixEquals = [](
            const HudLayoutAdapter* adapter, const uint8_t* expected, int n)
        {
            if (!adapter || adapter->anchorLength != n)
                return false;
            for (int i = 0; i < n; ++i)
            {
                if (adapter->anchor[i] != expected[i] ||
                    adapter->mask[i] != 0xFF)
                    return false;
            }
            return true;
        };
        Check(anchorPrefixEquals(halo3Hud, expectedHalo3HudAnchor, 24),
            "Halo 3 HUD adapter retains its evidence-exact anchor");
        Check(anchorPrefixEquals(odstHud, expectedOdstHudAnchor, 24),
            "ODST HUD adapter retains its evidence-exact 2000/2000 anchor");
        Check(halo3Hud && odstHud && halo3Hud->anchor != odstHud->anchor,
            "ODST cannot reuse Halo 3's title-specific safe-frame anchor");
        // Reach's record is a different shape, proven from HREK's own
        // chud_curvature_info_block postprocess and tag export. Copying Halo 3's
        // offsets would land 36 bytes short, inside the minimap points.
        Check(halo3Hud && odstHud && halo3Hud->safeFrameOffset == 24 &&
                  odstHud->safeFrameOffset == 24 &&
                  halo3Hud->depthFromSlot == -28 &&
                  odstHud->depthFromSlot == -28,
            "Halo 3 and ODST keep the shared s_chud_curvature_info offsets");
        Check(reachHud && reachHud->expectedBlocks == 3 &&
                  reachHud->safeFrameOffset == 60 &&
                  reachHud->anchorLength == 76,
            "Reach HUD adapter uses its own record shape, not Halo 3's");
        Check(reachHud && !HudLayoutHasDepthField(*reachHud),
            "Reach declares no HUD depth field: its curvature is baked at "
            "tag-block load, so writing it would be inert");
        Check(halo3Hud && odstHud && HudLayoutHasDepthField(*halo3Hud) &&
                  HudLayoutHasDepthField(*odstHud),
            "Halo 3 and ODST keep their live dest-offset-z depth control");
        Check(reachHud && reachHud->anchor[0] == 0x00 &&
                  reachHud->anchor[1] == 0x05 && reachHud->anchor[20] == 0x00 &&
                  reachHud->anchor[21] == 0x00 &&
                  reachHud->anchor[22] == 0xA0 &&
                  reachHud->anchor[23] == 0x42,
            "Reach compares its own vehicle-3d-sensor-radius field, which "
            "Halo 3's record does not contain at all");
        Check(reachHud && reachHud->mask[12] == 0x00 &&
                  reachHud->mask[16] == 0x00 && reachHud->mask[60] == 0x00 &&
                  reachHud->mask[64] == 0x00,
            "Reach wildcards its per-skin sensor values and the safe-frame "
            "pair this feature writes");
        Check(HudLayoutAdapterWellFormed(*halo3Hud) &&
                  HudLayoutAdapterWellFormed(*odstHud) &&
                  HudLayoutAdapterWellFormed(*reachHud),
            "every HUD layout adapter keeps a fully compared scan prefix");
        // Halo 3 and ODST keep their proven exact cardinality and their proven
        // private-read-write-only search. Only Reach may look wider.
        Check(HudLayoutAcceptedCountOk(*halo3Hud, 3) &&
                  !HudLayoutAcceptedCountOk(*halo3Hud, 2) &&
                  !HudLayoutAcceptedCountOk(*halo3Hud, 4),
            "Halo 3 still accepts exactly three layout blocks");
        Check(HudLayoutAcceptedCountOk(*odstHud, 1) &&
                  !HudLayoutAcceptedCountOk(*odstHud, 0) &&
                  !HudLayoutAcceptedCountOk(*odstHud, 2),
            "ODST still accepts exactly its one proven globals block");
        Check(HudLayoutAcceptedCountOk(*reachHud, 3) &&
                  HudLayoutAcceptedCountOk(*reachHud, 6) &&
                  !HudLayoutAcceptedCountOk(*reachHud, 2) &&
                  !HudLayoutAcceptedCountOk(*reachHud, 17),
            "Reach accepts a second identity-verified copy of its record but "
            "never fewer than its three skins");
        Check(!halo3Hud->scanMappedRegions && !odstHud->scanMappedRegions &&
                  reachHud->scanMappedRegions,
            "only Reach widens the search beyond private read-write memory");
        Check(!halo3Hud->forceWriteEveryPass && !odstHud->forceWriteEveryPass &&
                  reachHud->forceWriteEveryPass,
            "only Reach reasserts every pass, because it overwrites its own "
            "record live and a skip-when-matching write loses that race");
        Check(HudLayoutCanReacquireFromRemembered(1, 1) &&
                  HudLayoutCanReacquireFromRemembered(3, 3),
            "exact per-title remembered cardinality permits stock restoration");
        Check(!HudLayoutCanReacquireFromRemembered(0, 1) &&
                  !HudLayoutCanReacquireFromRemembered(1, 3) &&
                  !HudLayoutCanReacquireFromRemembered(0, 0),
            "missing, partial, and unproven remembered HUD sets fail closed");
        Check(!HudLayoutAdapterFor(HudLayoutProfile::None),
            "an unowned title has no writable HUD layout adapter");
        Check(!HudLayoutPublicationMatches(
                  HudLayoutProfile::None, 9,
                  HudLayoutProfile::None, 9),
            "matching generations cannot grant HUD ownership to no title");
        Check(HudLayoutPublicationMatches(
                  HudLayoutProfile::Halo3ODST, 9,
                  HudLayoutProfile::Halo3ODST, 9),
            "HUD scan results publish only to their exact title generation");
        Check(!HudLayoutPublicationMatches(
                  HudLayoutProfile::Halo3ODST, 9,
                  HudLayoutProfile::Halo3, 9) &&
                  !HudLayoutPublicationMatches(
                      HudLayoutProfile::Halo3ODST, 9,
                      HudLayoutProfile::Halo3ODST, 8),
            "foreign-title and stale-generation HUD results are rejected");

        Check(OdstHudLayoutEligible(true, true, true, true, false, false),
            "ODST starts shared HUD layout work on its first eligible camera heartbeat");
        Check(!OdstHudLayoutEligible(false, true, true, true, false, false) &&
                  !OdstHudLayoutEligible(true, false, true, true, false, false) &&
                  !OdstHudLayoutEligible(true, true, false, true, false, false) &&
                  !OdstHudLayoutEligible(true, true, true, false, false, false),
            "ODST HUD layout writes require private title ownership and a fresh camera");
        Check(!OdstHudLayoutEligible(true, true, true, true, true, false) &&
                  !OdstHudLayoutEligible(true, true, true, true, false, true),
            "teardown and native pause veto ODST HUD layout writes");

        OdstFreshCameraDebounce debounce;
        Check(!debounce.Update(100, true),
            "ODST camera does not arm on its first fresh frame");
        Check(!debounce.Update(1100, true),
            "ODST camera remains flat at the exact Halo 3 one-second boundary");
        Check(debounce.Update(1101, true),
            "ODST camera arms on the first millisecond after Halo 3 stability");
        debounce.Reset();
        Check(!debounce.Update(5000, true),
            "ODST session re-entry resets the stability debounce");

        OdstFreshCameraDebounce interruptedDebounce;
        Check(!interruptedDebounce.Update(2000, true) &&
                  !interruptedDebounce.Update(2500, false) &&
                  !interruptedDebounce.Update(3000, true),
            "loss of camera freshness restarts the ODST stability interval");
        Check(!interruptedDebounce.Update(4000, true),
            "the restarted interval remains flat at its exact one-second boundary");
        Check(interruptedDebounce.Update(4001, true),
            "only a continuous fresh-camera interval arms ODST after reset");

        // ODST's camera tail boolean toggles about ten times a second during
        // ordinary play. Restarting the interval on each toggle meant the
        // camera armed only when it caught a lucky quiet gap, which is why
        // arming was slow and a quick pause/unpause often never re-armed.
        OdstFreshCameraDebounce flickerDebounce;
        Check(!flickerDebounce.Update(1000, true),
            "the flicker-tolerant debounce still requires a stability interval");
        Check(!flickerDebounce.Update(1100, false) &&
                  !flickerDebounce.Update(1200, true) &&
                  !flickerDebounce.Update(1300, false) &&
                  !flickerDebounce.Update(1400, true),
            "a 100ms tail toggle does not arm ODST early");
        Check(flickerDebounce.Update(2001, true),
            "a flickering tail no longer restarts the interval, so ODST arms "
            "one second after the camera first became fresh");

        OdstFreshCameraDebounce sustainedLossDebounce;
        Check(!sustainedLossDebounce.Update(1000, true) &&
                  !sustainedLossDebounce.Update(1400, false),
            "a gap beyond the tolerance is a genuine camera loss");
        Check(!sustainedLossDebounce.Update(2001, true),
            "a genuine loss still restarts the full stability interval");

        Check(EvaluateOdstHeartbeat(1700, 1000, 0, false, false) ==
                  OdstHeartbeatAction::None,
            "ODST tolerates a short delay before its first heartbeat");
        Check(EvaluateOdstHeartbeat(1800, 1000, 0, false, false) ==
                  OdstHeartbeatAction::LevelUnloaded,
            "an inactive camera after install is treated as an unload");
        Check(EvaluateOdstHeartbeat(6100, 1000, 0, false, true) ==
                  OdstHeartbeatAction::NoFirstHeartbeat,
            "active-looking memory cannot retain a hook without a heartbeat");
        Check(EvaluateOdstHeartbeat(7000, 1000, 6200, true, true) ==
                  OdstHeartbeatAction::None,
            "a short heartbeat gap with a ready camera is tolerated");
        Check(EvaluateOdstHeartbeat(6701, 1000, 6000, true, false) ==
                  OdstHeartbeatAction::None,
            "a transient heartbeat gap does not detach presentation even when camera readiness flickers");
        Check(EvaluateOdstHeartbeat(6751, 1000, 6000, true, false) ==
                  OdstHeartbeatAction::LevelUnloaded,
            "an unready camera must exceed the soft timeout before presentation detaches");
        Check(EvaluateOdstHeartbeat(12001, 1000, 7000, true, true) ==
                  OdstHeartbeatAction::LevelUnloaded,
            "a hard heartbeat timeout falls back even with stale ready bytes");
    }

    const TitleDescriptor* halo3 = TitleRegistry_FromModuleName(L"halo3.dll");
    Check(halo3 != nullptr, "Halo 3 module is recognized");
    Check(halo3 && halo3->runtimeSupported, "Halo 3 is the supported baseline adapter");
    Check(halo3 && halo3->admissionCapabilities ==
              TitleCapability_ControllerInput,
        "Halo 3 retains ordinary shared-controller admission");
    Check(halo3 && (halo3->capabilities & TitleCapability_CutsceneTheater) != 0,
        "Halo 3 advertises the proven cutscene-theatre capability");

    const TitleDescriptor* odst =
        TitleRegistry_FromModuleName(L"N:/MCC/HALO3ODST.DLL");
    Check(odst && odst->title == GameTitle::Halo3ODST,
        "ODST paths are matched case-insensitively");
    Check(odst && !odst->runtimeSupported,
        "ODST stays disabled until its adapter passes the title gate");
    Check(odst && odst->capabilities == TitleCapability_None,
        "ODST advertises no public capabilities during private bring-up");
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    Check(odst && odst->admissionCapabilities ==
              TitleCapability_ControllerInput,
        "The private preset grants ODST controller admission");
#else
    Check(odst && odst->admissionCapabilities == TitleCapability_None,
        "The normal preset grants ODST no controller admission");
#endif

    Check(TitleRegistry_HookPlan(GameTitle::Halo3) == TitleHookPlan::Halo3Full,
        "Halo 3 keeps the full headset-confirmed hook plan");
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    Check(TitleRegistry_HookPlan(GameTitle::Halo3ODST) ==
              TitleHookPlan::OdstExperimentalCameraCore,
        "The explicit private build enables only the ODST camera core");
#else
    Check(TitleRegistry_HookPlan(GameTitle::Halo3ODST) == TitleHookPlan::None,
        "A normal build leaves ODST completely stock");
#endif

    const TitleDescriptor* reach = TitleRegistry_FromModuleName(L"haloreach.dll");
    Check(reach && reach->title == GameTitle::HaloReach, "Reach module is recognized");
    Check(reach && reach->runtimeSupported,
        "Reach is a permanent runtime-supported title");
    Check(reach && reach->capabilities ==
              (TitleCapability_Stereo | TitleCapability_ControllerAim |
               TitleCapability_ArmIk | TitleCapability_Hud |
               TitleCapability_RuntimeModes |
               TitleCapability_RoomScale |
               TitleCapability_ControllerInput | TitleCapability_Haptics |
               TitleCapability_CutsceneTheater),
        "Reach advertises controller aim, arm IK, and proven cutscene theatre");
    Check(reach && (reach->capabilities &
              TitleCapability_Hud) != 0u,
        "Reach advertises native HUD capability now that its layout adapter "
        "locates Reach's own curvature record");
    Check(TitleRegistry_HookPlan(GameTitle::HaloReach) ==
              TitleHookPlan::ReachCameraCore,
        "Reach receives its permanent camera-core hook plan");
#if HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP
    Check(reach && reach->admissionCapabilities ==
              TitleCapability_ControllerInput,
        "The private preset grants Reach only controller admission");
    Check(ReachAdapter_GetStage() == ReachAdapterStage::ControllerInputOnly,
        "The private preset compiles only Reach controller transport");
#else
    Check(reach && reach->admissionCapabilities == TitleCapability_None,
        "The normal preset grants Reach no controller admission");
    Check(ReachAdapter_GetStage() == ReachAdapterStage::Disabled,
        "The normal Release preset keeps the Reach adapter disabled");
#endif
    Check(!ReachAdapter_RuntimeHooksPermitted(),
        "Neither controller admission nor the hard-off render foundation can install Reach runtime hooks");
#if HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE
    Check(HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP == 1,
        "The hard-off Reach render foundation compiles only with controller admission retained");
#else
    Check(true,
        "The normal and controller-only presets omit the Reach render foundation");
#endif
    const ReachEvidenceIdentity& reachIdentity =
        ReachAdapter_GetEvidenceIdentity();
    Check(std::wstring_view(reachIdentity.moduleName) == L"haloreach.dll" &&
          std::string_view(reachIdentity.moduleSha256) ==
              "738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894" &&
          reachIdentity.peTimestamp == 0x68A0EFE1u &&
          reachIdentity.sizeOfImage == 0x04EDA000u &&
          std::string_view(reachIdentity.hrekBuild) ==
              "2023.07.17.176677.1-QFE1",
        "The Reach adapter pins the independently verified retail and HREK identities");

    ReachHookProof proof{ true, 1, true, true, true, true, true, true };
    Check(ReachAdapter_HookProofComplete(proof),
        "A synthetic proof is complete only when every evidence gate is present");
    proof.loadedImageMatchCount = 0;
    Check(!ReachAdapter_HookProofComplete(proof),
        "A zero-match loaded-image signature fails the Reach proof closed");
    proof.loadedImageMatchCount = 2;
    Check(!ReachAdapter_HookProofComplete(proof),
        "A multiple-match loaded-image signature fails the Reach proof closed");
    proof.loadedImageMatchCount = 1;
    proof.executableRange = false;
    Check(!ReachAdapter_HookProofComplete(proof),
        "A non-executable candidate range fails the Reach proof closed");
    proof.executableRange = true;
    proof.abi = false;
    Check(!ReachAdapter_HookProofComplete(proof),
        "An unproven ABI fails the Reach proof closed");
    Check(!TitleRegistry_AllowsSharedGameplayFeatures(
              GameTitle::HaloReach, true, false),
        "Stale Halo 3 ownership cannot admit Reach gameplay features");
    const bool reachControllerAdmission = reach &&
        (reach->admissionCapabilities & TitleCapability_ControllerInput) != 0;
    Check(TitleRegistry_AllowsSharedControllerInput(
              GameTitle::HaloReach, true, false, true,
              reachControllerAdmission) == reachControllerAdmission,
        "Explicit Reach obeys its immutable controller admission policy");
    Check(!TitleRegistry_AllowsSharedControllerInput(
              GameTitle::HaloReach, false, true, false,
              reachControllerAdmission),
        "Camera-only ownership cannot leak controller input into Reach");
    const Halo4EvidenceIdentity& halo4Identity =
        Halo4Adapter_GetEvidenceIdentity();
    Check(Halo4ParityCommandFitsBucket(0x00) &&
              Halo4ParityCommandFitsBucket(0x28) &&
              Halo4ParityCommandFitsBucket(0xFF) &&
              !Halo4ParityCommandFitsBucket(-1) &&
              !Halo4ParityCommandFitsBucket(0x100) &&
              Halo4ParityCommandBucket(0x28) == 0x28,
        "The Halo 4 parity trace bins only complete byte-range CUI command IDs");
    {
        int32_t ids[kHalo4ParityTransformSlotCount]{};
        for (int32_t& id : ids)
            id = kHalo4ParityEmptyTransformId;
        ids[0] = 17;
        ids[1] = 42;
        Check(Halo4ParityFindTransformSlot(ids, 42) == 1 &&
                  Halo4ParityFindTransformSlot(ids, 99) == 2,
            "The Halo 4 parity trace reuses an exact transform ID and admits a new ID only into an empty bounded slot");
        for (size_t i = 0; i < kHalo4ParityTransformSlotCount; ++i)
            ids[i] = static_cast<int32_t>(i);
        Check(Halo4ParityFindTransformSlot(ids, 7) == 7 &&
                  Halo4ParityFindTransformSlot(ids, 99) == -1,
            "The Halo 4 parity trace reports full-table overflow without merging identities");
    }
    const TitleDescriptor* halo4Row = TitleRegistry_Find(GameTitle::Halo4);
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    Check(Halo4Adapter_GetStage() ==
              Halo4AdapterStage::ControllerInputAndStereoCamera,
        "C-H4-3 stages Halo 4 at controller transport plus the camera core");
    Check(Halo4Adapter_RuntimeHooksPermitted(),
        "C-H4-3 permits the camera core's hooks; the install proof, not this "
        "flag, is what actually admits them");
#else
    Check(Halo4Adapter_GetStage() ==
              Halo4AdapterStage::ControllerInputAndColdObservation,
        "C-H4-2 stages Halo 4 at controller transport plus cold observation");
    Check(!Halo4Adapter_RuntimeHooksPermitted(),
        "No Halo 4 runtime hook is permitted before a proven camera core");
#endif

    // C-H4-2 anchor table: every entry must be well-formed offline, because
    // the runtime scan trusts its shape (docs/HALO4-SIGNATURE-EVIDENCE.md
    // E-H4-4 records each pattern's on-disk uniqueness proof; the offline
    // grammar here is deliberately STRICTER than sig::Find's - uppercase hex
    // and '??' only - so a house-style violation fails before it ships).
    Check(kHalo4RetailAnchorCount == 4,
        "C-H4-2 pins exactly the four E-H4-4 anchors");
    for (const Halo4RetailAnchor& anchor : kHalo4RetailAnchors)
    {
        Check(anchor.name && anchor.name[0] && anchor.pattern &&
                  anchor.pattern[0] != '?' &&
                  anchor.rva != 0 && anchor.rva < kHalo4RetailImageSize,
            "Every Halo 4 anchor names itself, starts concrete, and pins an "
            "in-image RVA");
        size_t tokenBytes = 0;
        bool tokensValid = true;
        const char* p = anchor.pattern;
        while (*p)
        {
            if (*p == ' ') { ++p; continue; }
            const char a = p[0];
            const char b = p[1];
            const bool wild = a == '?' && b == '?';
            const bool hex =
                ((a >= '0' && a <= '9') || (a >= 'A' && a <= 'F')) &&
                ((b >= '0' && b <= '9') || (b >= 'A' && b <= 'F'));
            if ((!wild && !hex) || (p[2] != ' ' && p[2] != '\0'))
            {
                tokensValid = false;
                break;
            }
            ++tokenBytes;
            p += 2;
        }
        Check(tokensValid && tokenBytes >= 12,
            "Every Halo 4 anchor pattern parses and is long enough to be "
            "meaningfully unique");
        // A rip decode must index bytes the pattern actually matched, and
        // must aim at a real in-image data anchor.
        Check(anchor.ripDispOffset == 0 ||
                  (static_cast<size_t>(anchor.ripDispOffset) + 4 <=
                       tokenBytes &&
                   anchor.ripTargetRva != 0 &&
                   anchor.ripTargetRva < kHalo4RetailImageSize),
            "Every Halo 4 rip decode stays inside its own match and lands "
            "in-image");
    }
    Check(kHalo4PlayerViewArrayRva == 0x30AD1C0u &&
              kHalo4PlayerViewStride == 0xAD0u &&
              kHalo4ViewStackTopRva == 0xE84634u,
        "The Halo 4 data anchors pin E-H4-4's array, stride, and stack top");
    Check(kHalo4PlayerViewArrayRva +
                  4ull * kHalo4PlayerViewStride <= kHalo4RetailImageSize &&
              kHalo4ViewStackTopRva + 4u <= kHalo4RetailImageSize,
        "The Halo 4 array span and stack top fit inside the pinned image");
    // The named indices are part of the contract: the level-load gate reuses
    // the ctor pattern by index, and push/pop must cross-check on ONE stack
    // top.
    Check(kHalo4RetailAnchors[kHalo4AnchorCtor].ripDispOffset == 0x0D &&
              kHalo4RetailAnchors[kHalo4AnchorCtor].ripTargetRva ==
                  kHalo4PlayerViewArrayRva &&
              kHalo4RetailAnchors[kHalo4AnchorPush].ripTargetRva ==
                  kHalo4ViewStackTopRva &&
              kHalo4RetailAnchors[kHalo4AnchorPop].ripTargetRva ==
                  kHalo4ViewStackTopRva &&
              kHalo4RetailAnchors[kHalo4AnchorClamp].ripDispOffset == 0,
        "The Halo 4 anchor indices bind ctor->array, push/pop->stack top, "
        "clamp->no decode");
    Check(kHalo4RetailAnchorRipTargets == 3,
        "Exactly three Halo 4 anchors carry rip decodes");

    // C-H4-2 verdict truth table: complete observations pass, and each
    // degraded field fails closed on its own.
    Halo4ColdObservationResult halo4Cold{};
    halo4Cold.moduleRangeValid = true;
    halo4Cold.peIdentity = true;
    halo4Cold.anchorsMatchedOnce =
        static_cast<uint32_t>(kHalo4RetailAnchorCount);
    halo4Cold.anchorsAtPinnedRva =
        static_cast<uint32_t>(kHalo4RetailAnchorCount);
    halo4Cold.ripTargetsAtPinnedRva = kHalo4RetailAnchorRipTargets;
    halo4Cold.mappingStable = true;
    Check(Halo4ColdObservationPass(halo4Cold),
        "A complete Halo 4 cold observation passes");
    {
        Halo4ColdObservationResult broken = halo4Cold;
        broken.moduleRangeValid = false;
        Check(!Halo4ColdObservationPass(broken),
            "A wrong Halo 4 module size fails the cold observation closed");
        broken = halo4Cold;
        broken.peIdentity = false;
        Check(!Halo4ColdObservationPass(broken),
            "A wrong Halo 4 PE identity fails the cold observation closed");
        broken = halo4Cold;
        broken.anchorsMatchedOnce -= 1;
        Check(!Halo4ColdObservationPass(broken),
            "A missing or duplicated Halo 4 anchor fails the cold "
            "observation closed");
        broken = halo4Cold;
        broken.anchorsAtPinnedRva -= 1;
        Check(!Halo4ColdObservationPass(broken),
            "A moved Halo 4 anchor fails the cold observation closed");
        broken = halo4Cold;
        broken.ripTargetsAtPinnedRva -= 1;
        Check(!Halo4ColdObservationPass(broken),
            "A Halo 4 rip decode off its pinned target fails the cold "
            "observation closed");
        broken = halo4Cold;
        broken.mappingStable = false;
        Check(!Halo4ColdObservationPass(broken),
            "A remapped halo4.dll fails the cold observation closed");
    }

    // ---- C-H4-3: the camera anchors and the per-eye camera math ----
    // C-H4-11 adds the first-person node accessor, which is anchored only
    // to prove the engine TLS index global - it is never hooked.
    Check(kHalo4CameraAnchorCount == 5,
        "The four E-H4-6 camera anchors plus C-H4-11's first-person "
        "accessor");
    for (const Halo4RetailAnchor& anchor : kHalo4CameraAnchors)
    {
        size_t tokenBytes = 0;
        bool tokensValid = true;
        const char* p = anchor.pattern;
        while (*p)
        {
            if (*p == ' ') { ++p; continue; }
            const char a = p[0];
            const char b = p[1];
            const bool wild = a == '?' && b == '?';
            const bool hex =
                ((a >= '0' && a <= '9') || (a >= 'A' && a <= 'F')) &&
                ((b >= '0' && b <= '9') || (b >= 'A' && b <= 'F'));
            if ((!wild && !hex) || (p[2] != ' ' && p[2] != '\0'))
            {
                tokensValid = false;
                break;
            }
            ++tokenBytes;
            p += 2;
        }
        Check(anchor.name && anchor.name[0] && anchor.pattern &&
                  anchor.pattern[0] != '?' && anchor.rva != 0 &&
                  anchor.rva < kHalo4RetailImageSize && tokensValid &&
                  tokenBytes >= 12,
            "Every Halo 4 camera anchor names itself, parses, starts concrete "
            "and pins an in-image RVA");
        Check(anchor.ripDispOffset == 0 ||
                  (static_cast<size_t>(anchor.ripDispOffset) + 4 <=
                       tokenBytes &&
                   anchor.ripTargetRva != 0 &&
                   anchor.ripTargetRva < kHalo4RetailImageSize),
            "Every Halo 4 camera rip decode stays inside its own match and "
            "lands in-image");
    }
    // The loop anchor's three displacements must all index bytes it matched -
    // a lengthened prefix that did not move these offsets would decode
    // garbage into a hook address.
    Check(kHalo4CameraAnchors[kHalo4CameraAnchorLoop].rva ==
              kHalo4PerWindowLoopRva &&
          kHalo4CameraAnchors[kHalo4CameraAnchorLoop].ripDispOffset ==
              kHalo4LoopElementRipOffset &&
          kHalo4LoopSetupRel32Offset < kHalo4LoopElementRipOffset &&
          kHalo4LoopElementRipOffset < kHalo4LoopWrapperRel32Offset,
        "The Halo 4 loop anchor's three displacements are in program order");
    Check(kHalo4CameraAnchors[kHalo4CameraAnchorSetup].rva ==
              kHalo4SetupRva &&
          kHalo4CameraAnchors[kHalo4CameraAnchorSetup].ripTargetRva ==
              kHalo4StackElementRva &&
          kHalo4CameraAnchors[kHalo4CameraAnchorLoop].ripTargetRva ==
              kHalo4StackElementRva &&
          kHalo4CameraAnchors[kHalo4CameraAnchorWrapper].rva ==
              kHalo4WrapperRva &&
          kHalo4CameraAnchors[kHalo4CameraAnchorWrapper].ripTargetRva ==
              kHalo4ActiveViewRva,
        "The loop and setup anchors derive the SAME stack element, and the "
        "wrapper anchor derives the active-view global");
    Check(kHalo4CameraAnchorRipTargets == 4,
        "Four Halo 4 anchors carry rip decodes: three camera targets plus "
        "the engine TLS index the first-person blocks are reached through");
    Check(kHalo4CameraAnchors[kHalo4CameraAnchorFirstPerson].ripTargetRva ==
              kHalo4EngineTlsIndexRva,
        "The first-person anchor derives the engine TLS index global, which "
        "is what keeps that address out of the shipped constants");
    Check(Halo4CameraLoopTargetsAgree(kHalo4SetupRva, kHalo4WrapperRva) &&
          !Halo4CameraLoopTargetsAgree(kHalo4SetupRva, kHalo4SetupRva) &&
          !Halo4CameraLoopTargetsAgree(0, kHalo4WrapperRva),
        "The loop's call-edge check accepts only the two pinned functions");
    Check(Halo4PreparedPairMatches(17, 17, 17) &&
              !Halo4PreparedPairMatches(0, 0, 0) &&
              !Halo4PreparedPairMatches(18, 17, 18) &&
              !Halo4PreparedPairMatches(18, 18, 17),
        "Halo 4 pair admission rejects zero, partial, and stale eye/FOV "
        "serials");
    Check(Halo4EyeCaptureIsCurrent(1, 1, true, true) &&
              !Halo4EyeCaptureIsCurrent(1, -1, true, true) &&
              !Halo4EyeCaptureIsCurrent(1, 0, true, true) &&
              !Halo4EyeCaptureIsCurrent(1, 1, false, true) &&
              !Halo4EyeCaptureIsCurrent(1, 1, true, false),
        "Halo 4 stamps an eye only while that exact raster scope is active, "
        "redirected, and backed by a cache");
    Check(Halo4XrPairUploadComplete(true, true, true, true) &&
              !Halo4XrPairUploadComplete(false, true, true, true) &&
              !Halo4XrPairUploadComplete(true, false, true, true) &&
              !Halo4XrPairUploadComplete(true, true, false, true) &&
              !Halo4XrPairUploadComplete(true, true, true, false),
        "Halo 4 submits an XR pair only after exact acquire, wait, both-eye "
        "upload, and release completion");
    Check(Halo4XrPairSubmissionAccepted(true, true) &&
              !Halo4XrPairSubmissionAccepted(false, true) &&
              !Halo4XrPairSubmissionAccepted(true, false),
        "Halo 4 reports a headset pair only when its projection was queued "
        "and xrEndFrame returned exact success");
    Check(kHalo4ObserverFovRatioOffset + 4 <= kHalo4ObserverSnapshotBytes &&
          kHalo4ObserverUpOffset + 12 <= kHalo4ObserverSnapshotBytes &&
          kHalo4ObserverForwardOffset + 12 <= kHalo4ObserverUpOffset &&
          kHalo4ObserverPositionOffset + 12 <= kHalo4ObserverForwardOffset &&
          kHalo4ElementProjectionMatrixOffset == 0x100,
        "Every substituted observer field fits inside the saved snapshot and "
        "the proven finished projection begins at element+0x100");

    // The install proof is all-or-nothing: each field fails closed alone.
    Halo4CameraInstallProof halo4Install{};
    halo4Install.coldObservationPassed = true;
    halo4Install.anchorsMatchedOnce =
        static_cast<uint32_t>(kHalo4CameraAnchorCount);
    halo4Install.anchorsAtPinnedRva =
        static_cast<uint32_t>(kHalo4CameraAnchorCount);
    halo4Install.ripTargetsAtPinnedRva = kHalo4CameraAnchorRipTargets;
    halo4Install.loopCallTargetsAgree = true;
    halo4Install.executableRange = true;
    halo4Install.mappingStable = true;
    Check(Halo4CameraInstallComplete(halo4Install),
        "A complete Halo 4 camera install proof admits the hooks");
    {
        Halo4CameraInstallProof broken = halo4Install;
        broken.coldObservationPassed = false;
        Check(!Halo4CameraInstallComplete(broken),
            "Halo 4 refuses to hook without C-H4-2's PASS for this module");
        broken = halo4Install;
        broken.anchorsMatchedOnce -= 1;
        Check(!Halo4CameraInstallComplete(broken),
            "A missing or duplicated Halo 4 camera anchor refuses the hook");
        broken = halo4Install;
        broken.anchorsAtPinnedRva -= 1;
        Check(!Halo4CameraInstallComplete(broken),
            "A moved Halo 4 camera anchor refuses the hook");
        broken = halo4Install;
        broken.ripTargetsAtPinnedRva -= 1;
        Check(!Halo4CameraInstallComplete(broken),
            "A Halo 4 camera rip decode off target refuses the hook");
        broken = halo4Install;
        broken.loopCallTargetsAgree = false;
        Check(!Halo4CameraInstallComplete(broken),
            "A loop that does not call the two pinned functions refuses the "
            "hook");
        broken = halo4Install;
        broken.executableRange = false;
        Check(!Halo4CameraInstallComplete(broken),
            "An out-of-image Halo 4 hook site refuses the hook");
        broken = halo4Install;
        broken.mappingStable = false;
        Check(!Halo4CameraInstallComplete(broken),
            "A remapped halo4.dll refuses the Halo 4 camera hook");
    }

    // Halo 4's native reticle transform is an optional CUI feature. Its independent
    // proof fails closed, while every runtime miss fails open to stock without
    // affecting camera ownership or OpenXR.
    Check(kHalo4CuiReticleDispatcherRva == 0x003F0EA4 &&
              kHalo4CuiReticleDispatcherEntryBytes.size() == 24 &&
              std::strcmp(kHalo4CuiReticleDispatcherEntryAob,
                  "48 8B C4 55 56 57 41 56 41 57 48 8D A8 B8 FC FF FF "
                  "48 81 EC 50 04 00 00") == 0,
        "The Halo 4 CUI dispatcher is pinned by its unique 24-byte entry");
    Check(kHalo4CuiReticleCallerRva == 0x003F4B6B &&
              kHalo4CuiReticleCallerFixedBytes.size() == 18 &&
              std::strcmp(kHalo4CuiReticleCallerAob,
                  "49 8B 8F 10 04 00 00 4D 8D 8F 20 04 00 00 49 8B D6 "
                  "E8 ?? ?? ?? ??") == 0 &&
              kHalo4CuiReticleCallerCallOpcodeOffset == 17 &&
              kHalo4CuiReticleCallerCallDisplacementOffset == 18 &&
              kHalo4CuiReticleCallerCallNextOffset == 22,
        "The Halo 4 CUI caller edge pins its context and rel32 geometry");
    Check(kHalo4CuiGameplayRenderRva == 0x003ACD60 &&
              kHalo4CuiGameplayRenderEntryBytes.size() == 31 &&
              std::strcmp(kHalo4CuiGameplayRenderEntryAob,
                  "48 8B C4 55 53 56 57 41 56 41 57 48 8D 68 B1 48 81 "
                  "EC A8 00 00 00 0F 29 78 B8 44 0F 29 40 A8") == 0,
        "Halo 4's CUI front end is pinned independently of its dispatcher");
    Check(kHalo4CuiGameplayCallerRva == 0x00375C51 &&
              std::strcmp(kHalo4CuiGameplayCallerAob,
                  "8B 8E 8C 03 00 00 4C 8D 45 A0 45 33 C9 44 88 6C 24 28 "
                  "33 D2 89 7C 24 20 E8 ?? ?? ?? ?? 83 FB 03") == 0 &&
              kHalo4CuiGameplayCallerCallOpcodeOffset == 24 &&
              kHalo4CuiGameplayCallerCallDisplacementOffset == 25 &&
              kHalo4CuiGameplayCallerCallNextOffset == 29 &&
              kHalo4CuiGameplayCallerReturnRva == 0x00375C6E &&
              Halo4CuiGameplayCallerTargetsRender(
                  kHalo4CuiGameplayRenderRva) &&
              !Halo4CuiGameplayCallerTargetsRender(
                  kHalo4CuiReticleDispatcherRva),
        "Only the exact full-size gameplay CUI call can open reticle ownership");
    Check(kHalo4CuiCommandBegin == 0x28 && kHalo4CuiCommandEnd == 0x29 &&
              kHalo4CuiCommandBeginPayloadSize == 0x0C &&
              Halo4CuiReticleCallerTargetsDispatcher(
                  kHalo4CuiReticleDispatcherRva) &&
              !Halo4CuiReticleCallerTargetsDispatcher(
                  kHalo4CuiReticleCallerRva),
        "The Halo 4 CUI command pair and decoded dispatcher edge stay pinned");

    {
        std::array<uint8_t, 20 + kHalo4HudAnchorSpanFromBasis> tagBytes{};
        uint8_t* const basisBytes = tagBytes.data() + 20;
        std::memcpy(
            basisBytes + kHalo4HudDamagePrefixFromBasis,
            kHalo4HudDamagePrefix.data(), sizeof(kHalo4HudDamagePrefix));
        std::memcpy(
            basisBytes, kHalo4HudAuthoredBasis.data(), kHalo4HudBasisBytes);
        std::memcpy(
            basisBytes + kHalo4HudSpreadFromBasis,
            &kHalo4HudDamagePrefix.back(), sizeof(float));
        std::memset(
            basisBytes + kHalo4HudSpreadFromBasis + sizeof(float),
            0xA5, 16); // relocated tag reference is deliberately wildcarded
        const uint32_t zeroFlags = 0;
        std::memcpy(
            basisBytes + kHalo4HudFlagsFromBasis,
            &zeroFlags, sizeof(zeroFlags));
        std::memcpy(
            basisBytes + kHalo4HudContrastFromBasis,
            kHalo4HudContrastTail.data(), sizeof(kHalo4HudContrastTail));
        Check(Halo4HudAuthoredBasisMatches(basisBytes) &&
                  Halo4HudImmutableSurroundMatches(basisBytes),
            "Halo 4 HUD locator accepts the official basis and neighbors");
        basisBytes[kHalo4HudContrastFromBasis] ^= 1;
        Check(!Halo4HudImmutableSurroundMatches(basisBytes),
            "Halo 4 HUD locator rejects a changed immutable contrast tail");
        basisBytes[kHalo4HudContrastFromBasis] ^= 1;

        std::array<Halo4HudBasisPoint, 9> flat{};
        std::array<Halo4HudBasisPoint, 9> authored{};
        std::array<Halo4HudBasisPoint, 9> curved{};
        Check(Halo4ComputeHudBasis(
                  1.0f, 1.0f, 0.0f, 0.0f, flat.data()) &&
                  std::memcmp(flat.data(), kHalo4HudFlatBasis.data(),
                      sizeof(flat)) == 0,
            "Halo 4 hud_curvature=0 produces the identity grid");
        Check(Halo4ComputeHudBasis(
                  1.0f, 1.0f, 0.5f, 0.0f, authored.data()) &&
                  std::fabs(authored[1].x - (-0.98f)) < 0.0001f &&
                  std::fabs(authored[3].y - (-0.92f)) < 0.0001f &&
                  std::fabs(authored[7].x - 0.98f) < 0.0001f,
            "Halo 4 hud_curvature=0.5 retains the authored basis");
        Check(Halo4ComputeHudBasis(
                  0.5f, 0.25f, 1.0f, 36.0f, curved.data()) &&
                  std::fabs(curved[1].x - (-0.48f)) < 0.0001f &&
                  std::fabs(curved[3].y - (-0.31f)) < 0.0001f &&
                  std::fabs(curved[4].y - (-0.10f)) < 0.0001f,
            "Halo 4 HUD basis combines size, curvature, and positive-up height");
        Check(!Halo4ComputeHudBasis(
                  0.1f, 1.0f, 0.5f, 0.0f, authored.data()) &&
                  !Halo4ComputeHudBasis(
                      1.0f, 1.0f, 1.1f, 0.0f, authored.data()),
            "Halo 4 HUD layout rejects out-of-range config values");
    }

    Halo4CuiReticleInstallProof halo4CuiInstall{};
    halo4CuiInstall.transformLayoutProven = true;
    halo4CuiInstall.anchorsMatchedOnce = kHalo4CuiReticleAnchorCount;
    halo4CuiInstall.anchorsAtPinnedRva = kHalo4CuiReticleAnchorCount;
    halo4CuiInstall.callerDecodesDispatcher = true;
    halo4CuiInstall.gameplayCallerDecodesRender = true;
    halo4CuiInstall.executableRange = true;
    halo4CuiInstall.mappingStable = true;
    Check(Halo4CuiReticleInstallComplete(halo4CuiInstall),
        "Complete transform, signatures, edge decode, and mapping admit the "
        "optional Halo 4 CUI reticle transaction");
    {
        Halo4CuiReticleInstallProof broken = halo4CuiInstall;
        broken.transformLayoutProven = false;
        Check(!Halo4CuiReticleInstallComplete(broken),
            "Halo 4 CUI movement stays stock without the proven stack layout");
        broken = halo4CuiInstall;
        broken.anchorsMatchedOnce -= 1;
        Check(!Halo4CuiReticleInstallComplete(broken),
            "A missing or duplicated Halo 4 CUI signature refuses both hooks");
        broken = halo4CuiInstall;
        broken.anchorsAtPinnedRva -= 1;
        Check(!Halo4CuiReticleInstallComplete(broken),
            "A moved Halo 4 CUI signature refuses both hooks");
        broken = halo4CuiInstall;
        broken.callerDecodesDispatcher = false;
        Check(!Halo4CuiReticleInstallComplete(broken),
            "A Halo 4 CUI caller that misses the dispatcher refuses the hook");
        broken = halo4CuiInstall;
        broken.gameplayCallerDecodesRender = false;
        Check(!Halo4CuiReticleInstallComplete(broken),
            "A Halo 4 gameplay caller that misses the CUI front end refuses "
            "both optional hooks");
        broken = halo4CuiInstall;
        broken.executableRange = false;
        Check(!Halo4CuiReticleInstallComplete(broken),
            "A Halo 4 CUI site outside executable image memory refuses the hook");
        broken = halo4CuiInstall;
        broken.mappingStable = false;
        Check(!Halo4CuiReticleInstallComplete(broken),
            "A remapped halo4.dll refuses only the CUI reticle transaction");
    }

    const auto cuiInstalled = Halo4CuiReticleLifecycleFor(
        Halo4CuiReticleOptionalInstallState::Installed, halo4CuiInstall);
    Check(cuiInstalled.nativeTransformLive && !cuiInstalled.cleanupFeature,
        "Native Halo 4 CUI movement becomes live after a complete install");
    {
        Halo4CuiReticleInstallProof incomplete = halo4CuiInstall;
        incomplete.mappingStable = false;
        Check(!Halo4CuiReticleLifecycleFor(
                   Halo4CuiReticleOptionalInstallState::Installed, incomplete)
                   .nativeTransformLive &&
                  !Halo4CuiReticleLifecycleFor(
                      Halo4CuiReticleOptionalInstallState::StockFallback,
                      halo4CuiInstall)
                       .nativeTransformLive &&
                  !Halo4CuiReticleLifecycleFor(
                      Halo4CuiReticleOptionalInstallState::CleanupRequired,
                      halo4CuiInstall)
                       .nativeTransformLive,
            "Native Halo 4 CUI movement is never live before complete optional "
            "installation");
    }
    Check(Halo4CuiReticleLifecycleFor(
              Halo4CuiReticleOptionalInstallState::CleanupRequired,
              halo4CuiInstall)
              .cleanupFeature &&
              !Halo4CuiReticleLifecycleFor(
                  Halo4CuiReticleOptionalInstallState::StockFallback,
                  halo4CuiInstall)
                   .cleanupFeature,
        "A partial Halo 4 CUI install requests feature-local cleanup only");
    for (const Halo4CuiReticleOptionalInstallState state : {
             Halo4CuiReticleOptionalInstallState::StockFallback,
             Halo4CuiReticleOptionalInstallState::CleanupRequired,
             Halo4CuiReticleOptionalInstallState::Installed})
    {
        const auto lifecycle =
            Halo4CuiReticleLifecycleFor(state, halo4CuiInstall);
        Check(!lifecycle.disarmCameraCore && !lifecycle.endOpenXrSession,
            "No optional Halo 4 CUI install state disarms camera or OpenXR");
    }
    Check(Halo4CuiReticleNeedsProceduralBootstrap(
              true, false, true, true) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(
                  true, true, true, true) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(
                  false, false, true, true) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(
                  true, false, true, false) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(
                  true, false, false, true),
        "Halo 4 keeps procedural gun-ray pixels only while a live authored "
        "hook is waiting for its first validated image");

    using CuiAction = Halo4CuiReticleAction;
    Check(Halo4DecideCuiReticleAction(
              false, true, true, kHalo4CuiCommandBegin,
              true, true, 0, false) == CuiAction::DrawStock,
        "Halo 4 CUI work outside VR ownership remains stock");
    Check(Halo4DecideCuiReticleAction(
              true, false, true, kHalo4CuiCommandBegin,
              true, true, 0, false) == CuiAction::DrawStock,
        "An uninstalled Halo 4 CUI transform path remains stock");
    Check(Halo4DecideCuiReticleAction(
              true, true, true, kHalo4CuiCommandEnd,
              true, true, 0, false) == CuiAction::DrawStock &&
              Halo4DecideCuiReticleAction(
                  true, true, true, 0x2A,
                  true, true, 0, false) == CuiAction::DrawStock,
        "Only Halo 4's CUI begin command can move the reticle transform");
    Check(Halo4DecideCuiReticleAction(
              true, true, false, kHalo4CuiCommandBegin,
              true, true, 0, false) == CuiAction::DrawStock,
        "An unreadable Halo 4 CUI command fails open to stock");
    Check(Halo4DecideCuiReticleAction(
              true, true, true, kHalo4CuiCommandBegin,
              true, true, -1, false) == CuiAction::DrawStock &&
              Halo4DecideCuiReticleAction(
                  true, true, true, kHalo4CuiCommandBegin,
                  true, true, 2, false) == CuiAction::DrawStock,
        "An invalid Halo 4 stereo eye fails open to stock");
    Check(Halo4DecideCuiReticleAction(
              true, true, true, kHalo4CuiCommandBegin,
              false, true, 0, false) == CuiAction::HideNative,
        "crosshair=0 suppresses Halo 4's native CUI reticle");
    Check(Halo4DecideCuiReticleAction(
              true, true, true, kHalo4CuiCommandBegin,
              true, false, 0, false) == CuiAction::DrawStock,
        "kill_reticle=0 preserves Halo 4's native CUI reticle");
    Check(Halo4DecideCuiReticleAction(
              true, true, true, kHalo4CuiCommandBegin,
              true, true, 0, false) == CuiAction::HideNative &&
              Halo4DecideCuiReticleAction(
                  true, true, true, kHalo4CuiCommandBegin,
                  true, true, 1, false) == CuiAction::HideNative &&
              Halo4DecideCuiReticleAction(
                  true, true, true, kHalo4CuiCommandBegin,
                  true, true, 1, true) == CuiAction::HideNative &&
              Halo4DecideCuiReticleAction(
                  true, true, true, kHalo4CuiCommandBegin,
                  true, true, 0, true) == CuiAction::HideNative,
        "Both Halo 4 eyes hide the native flat copy while authored pixels ride "
        "the exact OpenXR reticle quad; eye order cannot create a duplicate");

    {
        const float forward[3] = {0.0f, 1.0f, 0.0f};
        const float up[3] = {0.0f, 0.0f, 1.0f};
        const float centerAim[3] = {0.0f, 1.0f, 0.0f};
        const Halo4CuiAimOffset center = Halo4ProjectAimToCuiOffset(
            forward, up, centerAim, 0.78539816f, 0.78539816f);
        Check(center.valid && fabsf(center.x) < 1.0e-6f &&
                  fabsf(center.y) < 1.0e-6f,
            "A camera-forward Halo 4 gun ray leaves the native reticle centred");
        const float rightAim[3] = {0.70710678f, 0.70710678f, 0.0f};
        const Halo4CuiAimOffset right = Halo4ProjectAimToCuiOffset(
            forward, up, rightAim, 0.78539816f, 0.78539816f);
        Check(right.valid && fabsf(right.x - 1.0f) < 1.0e-5f &&
                  fabsf(right.y) < 1.0e-5f,
            "The native Halo 4 reticle projection reaches the right frustum edge");
        const float upAim[3] = {0.0f, 0.70710678f, 0.70710678f};
        const Halo4CuiAimOffset upOffset = Halo4ProjectAimToCuiOffset(
            forward, up, upAim, 0.78539816f, 0.78539816f);
        Check(upOffset.valid && fabsf(upOffset.x) < 1.0e-5f &&
                  fabsf(upOffset.y - 1.0f) < 1.0e-5f,
            "Camera-up maps to positive Halo 4 reticle-matrix translation Y");
        const float behind[3] = {0.0f, -1.0f, 0.0f};
        Check(!Halo4ProjectAimToCuiOffset(
                   forward, up, behind, 0.78539816f, 0.78539816f).valid,
            "A behind-camera Halo 4 gun ray never writes a reticle matrix");

        const Halo4CuiAimOffset rightPixels =
            Halo4MapAimToCuiTranslation(right, -1920.0f, 1080.0f, false);
        const Halo4CuiAimOffset upPixels =
            Halo4MapAimToCuiTranslation(upOffset, -1920.0f, 1080.0f, false);
        const Halo4CuiAimOffset hidden =
            Halo4MapAimToCuiTranslation(center, -1920.0f, 1080.0f, true);
        const Halo4CuiAimOffset directHidden =
            Halo4BuildHiddenCuiTranslation(-1920.0f, 1080.0f);
        Check(rightPixels.valid && fabsf(rightPixels.x - 1920.0f) < 1.0e-4f &&
                  fabsf(rightPixels.y) < 1.0e-4f && upPixels.valid &&
                  fabsf(upPixels.x) < 1.0e-4f &&
                   fabsf(upPixels.y - 1080.0f) < 1.0e-4f,
            "Halo 4 converts normalized gun-ray projection through the live "
            "pixel-space CUI half extents");
        Check(hidden.valid && fabsf(hidden.x - 7680.0f) < 1.0e-4f &&
                  fabsf(hidden.y) < 1.0e-4f,
            "crosshair=0 moves Halo 4's native reticle fully offscreen in its "
            "own CUI coordinate system");
        Check(directHidden.valid &&
                  fabsf(directHidden.x - 7680.0f) < 1.0e-4f &&
                  fabsf(directHidden.y) < 1.0e-4f &&
                  !Halo4BuildHiddenCuiTranslation(NAN, 1080.0f).valid,
            "Halo 4 native-copy hiding is independent of every aim coordinate");
        Check(!Halo4MapAimToCuiTranslation(
                   right, 0.0f, 1080.0f, false).valid &&
                  !Halo4MapAimToCuiTranslation(
                      right, -1920.0f, NAN, false).valid,
            "Invalid Halo 4 CUI extents fail open without a transform write");

        float angularScale = 0.0f;
        Check(Halo4MapAngularSizeToCuiScale(
                  1080.0f, 0.78539816f, 7.2f, angularScale),
            "Halo 4 maps the shared crosshair angular-size knob into its "
            "title-native CUI scale");
        const float reconstructedRadians = 2.0f * atanf(
            (kHalo4CuiNominalReticleHeight * angularScale * 0.5f / 1080.0f) *
            tanf(0.78539816f));
        Check(fabsf(reconstructedRadians - 7.2f * 0.01745329252f) < 1.0e-5f,
            "Halo 4's nominal authored reticle height subtends exactly the "
            "configured angle");
        Check(!Halo4MapAngularSizeToCuiScale(
                  0.0f, 0.78539816f, 7.2f, angularScale) &&
                  !Halo4MapAngularSizeToCuiScale(
                      1080.0f, 0.0f, 7.2f, angularScale) &&
                  !Halo4MapAngularSizeToCuiScale(
                      1080.0f, 0.78539816f, NAN, angularScale),
            "Invalid Halo 4 size inputs fail feature-locally");

        const float centerPosition[3] = {0.0f, 0.0f, 0.0f};
        const float leftEyePosition[3] = {-0.032f, 0.0f, 0.0f};
        const float engineForward[3] = {0.0f, 2.0f, 0.0f};
        float leftEyeRay[3]{};
        Check(Halo4BuildReticleEyeRay(
                  centerPosition, leftEyePosition, engineForward, 2.0f,
                  leftEyeRay) &&
                  fabsf(leftEyeRay[0] - 0.032f) < 1.0e-6f &&
                  fabsf(leftEyeRay[1] - 2.0f) < 1.0e-6f &&
                  fabsf(leftEyeRay[2]) < 1.0e-6f,
            "Halo 4 projects each eye to one finite gun-ray target so the "
            "native CUI reticle retains the VR crosshair's stereo depth");
        Check(!Halo4BuildReticleEyeRay(
                  centerPosition, leftEyePosition, engineForward, 0.0f,
                  leftEyeRay),
            "An invalid Halo 4 reticle range cannot write an eye projection");
        Check(Halo4CuiReticlePairPositionsNative(77, 77, 77, 77) &&
                  !Halo4CuiReticlePairPositionsNative(0, 77, 77, 77) &&
                  !Halo4CuiReticlePairPositionsNative(76, 77, 77, 77) &&
                  !Halo4CuiReticlePairPositionsNative(77, 77, 78, 78),
            "The procedural VR fallback is suppressed only after both Halo 4 "
            "eyes moved native reticle art for the exact rendered serial");
    }

    // Generic cover math remains available for a later FOV milestone, but it
    // is not an observer-field layout claim: +0x78/+0x7C are full-vFOV and a
    // reference-FOV ratio, not tangents.
    {
        float tanX = 0.0f;
        float tanY = 0.0f;
        const float asymmetric[4] = {-0.9f, 0.7f, 0.8f, -1.0f};
        Check(Halo4SymmetricCoverFromFov(asymmetric, tanX, tanY) &&
                  fabsf(tanX - tanf(0.9f)) < 1e-5f &&
                  fabsf(tanY - tanf(1.0f)) < 1e-5f,
            "The dormant OpenXR cover helper takes the wider side of each axis");
        const float degenerate[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        Check(!Halo4SymmetricCoverFromFov(degenerate, tanX, tanY),
            "A zero OpenXR FOV is refused by the generic cover helper");
        const float absurd[4] = {-1.6f, 1.6f, 1.6f, -1.6f};
        Check(!Halo4SymmetricCoverFromFov(absurd, tanX, tanY),
            "A past-90-degree half angle is refused");
    }

    // Per-eye displacement changes pose only. Halo 4's stock full vertical FOV
    // and FOV-ratio fields are carried through byte-for-byte.
    {
        Halo4CameraBasis mono{};
        mono.forward[0] = 1.0f;   // +X forward
        mono.up[2] = 1.0f;        // +Z up  => right = forward x up = -Y
        mono.position[0] = 10.0f;
        mono.verticalFov = 1.4f;
        mono.fovRatio = 1.125f;
        Check(Halo4ValidateCameraBasis(mono),
            "A unit Halo 4 camera basis validates");

        const float eyePosition[3] = {0.032f, 0.0f, 0.0f}; // 32 mm right
        Halo4CameraBasis eye{};
        Check(Halo4BuildEyeCamera(mono, eyePosition, nullptr, 0.33f, eye),
            "Halo 4 builds an eye camera from a valid basis");
        Check(fabsf(eye.position[0] - 10.0f) < 1e-6f &&
                  fabsf(eye.position[1] - (-0.032f * 0.33f)) < 1e-6f &&
                  fabsf(eye.position[2]) < 1e-6f,
            "The Halo 4 eye offset lands on the camera's own right axis, "
            "scaled to world units");
        Check(memcmp(&eye.verticalFov, &mono.verticalFov, sizeof(float)) == 0 &&
                  memcmp(&eye.fovRatio, &mono.fovRatio, sizeof(float)) == 0,
            "Eye separation preserves Halo 4's stock FOV fields bit-for-bit");
        Check(fabsf(eye.forward[0] - 1.0f) < 1e-6f &&
                  fabsf(eye.up[2] - 1.0f) < 1e-6f,
            "A null Halo 4 eye orientation leaves the basis uncanted");

        // A +90 degree yaw about the eye's own up axis turns the view LEFT.
        // The camera's right axis here is -Y, so left is +Y: forward must land
        // on +Y and up must be unchanged. This is what proves the cant is
        // applied in the camera's basis rather than in world axes.
        const float quarterTurnAboutUp[4] = {
            0.0f, 0.70710678f, 0.0f, 0.70710678f};
        Halo4CameraBasis canted{};
        Check(Halo4BuildEyeCamera(mono, eyePosition, quarterTurnAboutUp, 0.33f,
                                  canted) &&
                  fabsf(canted.forward[1] - 1.0f) < 1e-5f &&
                  fabsf(canted.forward[0]) < 1e-5f &&
                  fabsf(canted.up[2] - 1.0f) < 1e-5f,
            "A Halo 4 eye cant rotates forward about the camera's own up axis");

        // Fail-closed inputs.
        Halo4CameraBasis rejected{};
        Halo4CameraBasis degenerate = mono;
        degenerate.forward[0] = 0.0f;
        Check(!Halo4ValidateCameraBasis(degenerate) &&
                  !Halo4BuildEyeCamera(
                      degenerate, eyePosition, nullptr, 0.33f, rejected),
            "A zero-length Halo 4 forward vector is refused");
        Halo4CameraBasis infinite = mono;
        infinite.position[1] = std::numeric_limits<float>::infinity();
        Check(!Halo4ValidateCameraBasis(infinite),
            "A non-finite Halo 4 camera position is refused");
        Halo4CameraBasis parallel = mono;
        memcpy(parallel.up, parallel.forward, sizeof(parallel.up));
        Check(!Halo4ValidateCameraBasis(parallel),
            "Parallel Halo 4 forward/up vectors are refused before eye "
            "separation can collapse");
        const float invalidOrientation[4] = {
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f};
        Check(!Halo4BuildEyeCamera(
                  mono, eyePosition, nullptr, 0.0f, rejected) &&
                  !Halo4BuildEyeCamera(
                      mono, eyePosition, invalidOrientation, 0.33f, rejected),
            "A zero world scale or invalid eye orientation is refused");

        Halo4CameraBasis extremeRatio = mono;
        extremeRatio.fovRatio = 100.0f;
        Check(Halo4ValidateCameraBasis(extremeRatio),
            "The evidence-backed camera gate does not reject a legal wide "
            "reference-FOV ratio");
        extremeRatio.fovRatio = -1.0f;
        Check(!Halo4ValidateCameraBasis(extremeRatio),
            "A negative Halo 4 reference-FOV ratio is refused");
    }

    // The engine-held camera result and finished projection are the acceptance
    // proof. A material center term is rejected until the compositor can carry
    // an off-axis H4 raster exactly.
    {
        Halo4CameraBasis requested{};
        requested.position[0] = 10.0f;
        requested.forward[0] = 1.0f;
        requested.up[2] = 1.0f;
        requested.verticalFov = 1.4f;
        requested.fovRatio = 1.125f;
        Check(Halo4CameraOutputMatches(
                  requested, requested.position, requested.forward,
                  requested.up),
            "An exact Halo 4 setup readback proves the requested camera took");
        float movedPosition[3] = {10.0f, 0.0f, 0.0f};
        movedPosition[0] = std::nextafter(movedPosition[0], 11.0f);
        Check(!Halo4CameraOutputMatches(
                  requested, movedPosition, requested.forward, requested.up),
            "Even a one-ULP engine-held camera difference fails the exact "
            "claim proof");

        float projection[16]{};
        projection[0] = 1.0f / tanf(0.7f);
        projection[5] = 1.0f / tanf(0.6f);
        projection[11] = -1.0f;
        float halfX = 0.0f, halfY = 0.0f, centerX = 0.0f, centerY = 0.0f;
        Check(Halo4DecodeSymmetricProjectionHalfFovs(
                  projection, halfX, halfY, centerX, centerY) &&
                  fabsf(halfX - 0.7f) < 1e-5f &&
                  fabsf(halfY - 0.6f) < 1e-5f && centerX == 0.0f &&
                  centerY == 0.0f,
            "Halo 4's finished symmetric row-vector matrix decodes exactly");
        projection[0] = 0.0f;
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(
                  projection, halfX, halfY, centerX, centerY),
            "A zero Halo 4 projection scale is refused");
        projection[0] = -1.0f / tanf(0.7f);
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(
                  projection, halfX, halfY, centerX, centerY),
            "A negative scale is refused because H4's proven normal projection "
            "writes positive X/Y scales");
        projection[0] = 1.0f / tanf(0.7f);
        projection[11] = 0.0f;
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(
                  projection, halfX, halfY, centerX, centerY),
            "A matrix without Halo 4's proven row-vector -1 is refused");
        projection[11] = -1.0f;
        projection[8] = std::numeric_limits<float>::epsilon();
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(
                  projection, halfX, halfY, centerX, centerY),
            "Any nonzero off-axis center is refused by the symmetric API");
        projection[8] = 0.0f;
        projection[5] = std::numeric_limits<float>::quiet_NaN();
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(
                  projection, halfX, halfY, centerX, centerY),
            "A non-finite Halo 4 projection scale is refused");
    }

    // Model the observer substitution itself: every byte except the three
    // proven pose vectors must survive unchanged, especially +0x78/+0x7C.
    {
        unsigned char stockObserver[kHalo4ObserverSnapshotBytes];
        unsigned char eyeObserver[kHalo4ObserverSnapshotBytes];
        for (size_t i = 0; i < sizeof(stockObserver); ++i)
            stockObserver[i] = static_cast<unsigned char>(i ^ 0xA5u);
        memcpy(eyeObserver, stockObserver, sizeof(eyeObserver));
        const float replacement[3] = {1.0f, 2.0f, 3.0f};
        memcpy(eyeObserver + kHalo4ObserverPositionOffset, replacement,
               sizeof(replacement));
        memcpy(eyeObserver + kHalo4ObserverForwardOffset, replacement,
               sizeof(replacement));
        memcpy(eyeObserver + kHalo4ObserverUpOffset, replacement,
               sizeof(replacement));
        bool untouchedBytesMatch = true;
        for (size_t i = 0; i < sizeof(stockObserver); ++i)
        {
            const bool mutableByte =
                (i >= kHalo4ObserverPositionOffset &&
                 i < kHalo4ObserverPositionOffset + 12) ||
                (i >= kHalo4ObserverForwardOffset &&
                 i < kHalo4ObserverForwardOffset + 12) ||
                (i >= kHalo4ObserverUpOffset &&
                 i < kHalo4ObserverUpOffset + 12);
            if (!mutableByte && eyeObserver[i] != stockObserver[i])
                untouchedBytesMatch = false;
        }
        Check(untouchedBytesMatch &&
                  memcmp(eyeObserver + kHalo4ObserverVerticalFovOffset,
                         stockObserver + kHalo4ObserverVerticalFovOffset,
                         sizeof(float)) == 0 &&
                  memcmp(eyeObserver + kHalo4ObserverFovRatioOffset,
                         stockObserver + kHalo4ObserverFovRatioOffset,
                         sizeof(float)) == 0,
            "C-H4-7 changes only position/forward/up and preserves every FOV "
            "input byte");

        // C-H4-8 additionally writes the vertical FOV. The FOV RATIO at +0x7C
        // must still survive: retail's converter scales both fields by one
        // shared factor (halo4.dll 0x38F0A8/0x38F0AC), so writing both would
        // double-apply, and the field's true meaning is still unresolved.
        const float widerVerticalFov = 2.2f;
        memcpy(eyeObserver + kHalo4ObserverVerticalFovOffset, &widerVerticalFov,
               sizeof(widerVerticalFov));
        Check(memcmp(eyeObserver + kHalo4ObserverFovRatioOffset,
                     stockObserver + kHalo4ObserverFovRatioOffset,
                     sizeof(float)) == 0,
            "C-H4-8 widens the vertical FOV input and still leaves the FOV "
            "ratio at +0x7C bit-identical");
    }

    // C-H4-8: the native headset-FOV cover. Nothing here may be
    // headset-specific - every number is derived from a supplied XrFovf.
    {
        // The user's PSVR2 as SteamVR/OpenXR reports it: asymmetric on X.
        const float psvr2[4] = {-1.07338f, 0.75747f, 0.92502f, -0.92502f};
        float tangentX = 0.0f;
        float tangentY = 0.0f;
        Check(Halo4RequiredCoverTangents(psvr2, tangentX, tangentY) &&
                  fabsf(tangentX - tanf(1.07338f)) < 1.0e-4f &&
                  fabsf(tangentY - tanf(0.92502f)) < 1.0e-4f,
            "The required cover takes the WIDER side of an asymmetric frustum, "
            "so a canted-panel headset is fully covered");

        const float badFov[4] = {0.1f, 0.75747f, 0.92502f, -0.92502f};
        Check(!Halo4RequiredCoverTangents(badFov, tangentX, tangentY),
            "A frustum with a non-negative left angle is refused rather than "
            "silently producing a cover");

        // Halo 4's measured stock cover does NOT contain that frustum - this is
        // exactly the C-H4-7 headset fault, reproduced offline.
        Check(!Halo4CoverContainsFov(0.88072f, 0.71805f, psvr2),
            "Halo 4's stock 50.46/41.14 deg cover fails to contain the PSVR2 "
            "frustum, which is why the whole slice was submitted at the wrong "
            "FOV");

        // Learn the mapping from the engine's own measured response. The retail
        // converter scales observer +0x78 by 0.785 and the builder halves it,
        // so writing 1.8295 must build a 0.71805 rad half-Y.
        Halo4FovCalibration calibration{};
        Check(!calibration.learned,
            "The FOV calibration starts unlearned, so the first Halo 4 stereo "
            "frame of a generation renders at the engine's own stock FOV");
        float solved = 0.0f;
        float expectedHalfY = 0.0f;
        Check(!Halo4SolveCoverVerticalFov(
                  psvr2, calibration, 1.01f, solved, expectedHalfY),
            "No cover may be solved before the engine's response has been "
            "measured");

        Check(Halo4LearnFovCalibration(1.8295f, 0.88072f, 0.71805f, calibration) &&
                  calibration.learned &&
                  fabsf(calibration.gain - 0.3925f) < 1.0e-3f,
            "The learned gain reproduces retail's proven 0.785/2 mapping from "
            "the engine's own finished projection");

        Check(Halo4SolveCoverVerticalFov(
                  psvr2, calibration, 1.01f, solved, expectedHalfY),
            "A learned calibration solves the vertical FOV write");
        // The solved write, put back through the engine's proven mapping,
        // must produce a cover that contains the frustum on all four edges.
        const float rebuiltHalfY = solved * 0.785f / 2.0f;
        const float rebuiltHalfX =
            atanf(tanf(rebuiltHalfY) * calibration.ratio);
        Check(Halo4CoverContainsFov(rebuiltHalfX, rebuiltHalfY, psvr2),
            "The solved cover, replayed through retail's own 0.785 mapping, "
            "contains the headset frustum - the containment vr.cpp requires "
            "before it can crop to native FOV");

        // Headset-agnostic: a different, wider, symmetric headset must also be
        // covered without touching any constant.
        const float wideFov[4] = {-1.3f, 1.3f, 1.2f, -1.2f};
        float wideSolved = 0.0f;
        float wideExpected = 0.0f;
        Check(Halo4SolveCoverVerticalFov(
                  wideFov, calibration, 1.01f, wideSolved, wideExpected) &&
                  wideSolved > solved,
            "A wider headset solves a wider cover from the same calibration, "
            "with no headset named anywhere in the code");
        const float wideHalfY = wideSolved * 0.785f / 2.0f;
        Check(Halo4CoverContainsFov(
                  atanf(tanf(wideHalfY) * calibration.ratio), wideHalfY,
                  wideFov),
            "The wider headset's solved cover also contains its own frustum");

        // A flipped engine scale branch must not distort the world: the gain is
        // re-learned from the readback rather than assumed.
        Halo4FovCalibration flipped{};
        Check(Halo4LearnFovCalibration(1.8295f, 0.30f, 0.1538f, flipped) &&
                  fabsf(flipped.gain - 0.0841f) < 1.0e-3f,
            "A changed engine FOV scale is absorbed by re-learning the gain, "
            "instead of silently rendering the wrong field of view");

        // End-to-end lock on the exact retail numbers, derived independently
        // from halo4.dll and the preserved logs: the engine's stock pair is
        // observer +0x78 = 1.8295 rad -> element +0x28 = 1.4361 rad (K = 0.785)
        // -> built half 50.46/41.14 deg, and the projection builder halves
        // element +0x28 before tan (0x38F4F7-0x38F501). Feeding the PSVR2
        // frustum through that chain must ask the engine for 2.3701 rad and
        // build 61.74/53.30 deg, covering the native 61.5/53.0 on both axes.
        Halo4FovCalibration retail{};
        Check(Halo4LearnFovCalibration(1.8295f, 0.88072f, 0.71805f, retail),
            "The retail stock pair teaches the calibration");
        float retailWrite = 0.0f;
        float retailHalfY = 0.0f;
        Check(Halo4SolveCoverVerticalFov(
                  psvr2, retail, 1.01f, retailWrite, retailHalfY),
            "The retail calibration solves a PSVR2 cover");
        Check(fabsf(retailWrite - 2.3702f) < 5.0e-3f,
            "The solved observer +0x78 write matches the independently derived "
            "2.3702 rad for this headset and this engine mapping");
        const float builtHalfY = retailWrite * 0.785f / 2.0f;
        const float builtHalfX = atanf(tanf(builtHalfY) * retail.ratio);
        Check(fabsf(builtHalfY * 57.2958f - 53.30f) < 0.05f &&
                  fabsf(builtHalfX * 57.2958f - 61.74f) < 0.10f,
            "Replayed through retail's own 0.785 scale and half-then-tan "
            "builder, the write produces 61.74/53.30 deg");
        Check(builtHalfX > 1.07338f && builtHalfY > 0.92502f,
            "Both built half-angles exceed PSVR2's native 61.5/53.0 deg, so "
            "the containment test vr.cpp applies can finally pass");
    }

    // C-H4-8: head pose and 6DOF on Halo 4's PROVEN right-handed Z-up basis.
    {
        Halo4CameraBasis mono{};
        mono.position[0] = 48.73f;
        mono.position[1] = -4.92f;
        mono.position[2] = 25.23f;
        // The live C-H4-6 engine dump, which proves the basis this math assumes.
        mono.forward[0] = 0.222f;
        mono.forward[1] = -0.975f;
        mono.forward[2] = 0.0f;
        mono.up[0] = 0.0f;
        mono.up[1] = 0.0f;
        mono.up[2] = 1.0f;
        mono.verticalFov = 1.8295f;
        mono.fovRatio = 1.5385f;
        // Normalise forward exactly so the basis passes validation.
        const float forwardLength = sqrtf(
            mono.forward[0] * mono.forward[0] +
            mono.forward[1] * mono.forward[1]);
        mono.forward[0] /= forwardLength;
        mono.forward[1] /= forwardLength;
        Check(Halo4ValidateCameraBasis(mono),
            "The live Halo 4 engine camera basis is a valid orthonormal Z-up "
            "basis");

        Halo4HeadPoseInput level{};
        level.quaternion[3] = 1.0f;
        level.positional = false;
        Halo4CameraBasis unchanged = mono;
        Check(Halo4ApplyHeadPose(unchanged, level) &&
                  fabsf(unchanged.forward[0] - mono.forward[0]) < 1.0e-4f &&
                  fabsf(unchanged.forward[1] - mono.forward[1]) < 1.0e-4f &&
                  fabsf(unchanged.forward[2] - mono.forward[2]) < 1.0e-4f,
            "A level head at the recenter reference leaves Halo 4's own camera "
            "exactly where the engine aimed it, so the gamepad still aims");

        // Yaw the head 30 degrees. The camera must yaw with it and stay level.
        Halo4HeadPoseInput yawed{};
        const float halfYaw = 0.5f * 0.5235988f; // 30 deg
        yawed.quaternion[1] = sinf(halfYaw); // OpenXR yaw is about +Y
        yawed.quaternion[3] = cosf(halfYaw);
        yawed.positional = false;
        yawed.yawSign = 1.0f;
        Halo4CameraBasis turned = mono;
        const float baseYaw = atan2f(mono.forward[1], mono.forward[0]);
        Check(Halo4ApplyHeadPose(turned, yawed),
            "A yawed head produces a valid Halo 4 camera basis");
        const float turnedYaw = atan2f(turned.forward[1], turned.forward[0]);
        Check(fabsf(Halo4WrapPi(turnedYaw - baseYaw) - (-0.5235988f)) < 1.0e-3f,
            "Turning your head yaws Halo 4's camera by the same angle about "
            "world up, which is what makes the world stay put");
        Check(fabsf(turned.up[2] - 1.0f) < 1.0e-3f,
            "A pure yaw introduces no roll or pitch");
        Check(Halo4ValidateCameraBasis(turned),
            "The yawed basis stays orthonormal, so the IPD split remains exact");

        // Pitch is absolute and needs no reference: it composes onto the
        // engine's own pitch. This is the case that broke C-H4-6, because it
        // left `up` at the engine's value and failed the orthogonality band.
        Halo4HeadPoseInput pitched{};
        const float halfPitch = 0.5f * 0.5235988f;
        pitched.quaternion[0] = sinf(halfPitch); // about +X
        pitched.quaternion[3] = cosf(halfPitch);
        pitched.positional = false;
        Halo4CameraBasis looked = mono;
        Check(Halo4ApplyHeadPose(looked, pitched) &&
                  Halo4ValidateCameraBasis(looked),
            "A pitched head keeps forward and up orthonormal - the C-H4-6 "
            "defect that rejected every frame past 2.87 degrees of pitch");
        Check(looked.forward[2] > 0.4f && looked.forward[2] < 0.6f,
            "Looking up pitches Halo 4's camera up, in its Z-up basis");

        // 6DOF: stepping right in room space must move the camera along the
        // camera's own right axis, which for Halo is forward x up.
        Halo4HeadPoseInput leaned{};
        leaned.quaternion[3] = 1.0f;
        leaned.position[0] = 1.0f; // one metre to the right
        leaned.positional = true;
        leaned.worldScale = 0.33f;
        Halo4CameraBasis moved = mono;
        Check(Halo4ApplyHeadPose(moved, leaned),
            "A leaning head produces a valid Halo 4 camera");
        const float right[3] = {
            mono.forward[1] * mono.up[2] - mono.forward[2] * mono.up[1],
            mono.forward[2] * mono.up[0] - mono.forward[0] * mono.up[2],
            mono.forward[0] * mono.up[1] - mono.forward[1] * mono.up[0]};
        float alongRight = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            alongRight += (moved.position[axis] - mono.position[axis]) * right[axis];
        Check(fabsf(alongRight - 0.33f) < 1.0e-3f,
            "One metre of physical lean moves Halo 4's camera one metre along "
            "its own right axis, scaled by world_scale");

        // The clamp is in world units and applies after the scale.
        Halo4HeadPoseInput absurd{};
        absurd.quaternion[3] = 1.0f;
        absurd.position[1] = 100.0f;
        absurd.positional = true;
        absurd.worldScale = 0.33f;
        Halo4CameraBasis clamped = mono;
        Check(Halo4ApplyHeadPose(clamped, absurd) &&
                  fabsf(clamped.position[2] - (mono.position[2] + 1.5f)) < 1.0e-4f,
            "An absurd tracked translation is clamped to 1.5 world units "
            "instead of throwing the camera out of the level");

        // Failure is feature-local: a broken pose must leave the basis alone.
        Halo4HeadPoseInput broken{};
        broken.quaternion[0] = std::numeric_limits<float>::quiet_NaN();
        broken.quaternion[3] = 1.0f;
        Halo4CameraBasis untouched = mono;
        Check(!Halo4ApplyHeadPose(untouched, broken),
            "A non-finite head pose is refused, so the eyes fall back to the "
            "engine's own camera instead of rendering garbage");

        // ---- C-H4-9: the headset owns pitch --------------------------------
        //
        // The reported defect: with the head level, the engine's own camera
        // pitch went straight into the view, so the look stick tilted the world
        // away from the player's real horizon.
        Halo4CameraBasis enginePitched = mono;
        {
            const float p = 0.4f; // engine looking ~23 deg up, e.g. after stick
            const float y = atan2f(mono.forward[1], mono.forward[0]);
            Halo4ComposeHeadOwnedBasis(
                y, p, 0.0f, enginePitched.forward, enginePitched.up);
            Check(Halo4ValidateCameraBasis(enginePitched),
                "Halo4ComposeHeadOwnedBasis builds an orthonormal Z-up basis");
            Check(fabsf(asinf(enginePitched.forward[2]) - p) < 1.0e-4f &&
                      fabsf(Halo4WrapPi(
                          atan2f(enginePitched.forward[1],
                                 enginePitched.forward[0]) - y)) < 1.0e-4f,
                "It reproduces exactly the yaw and pitch it was asked for");
        }

        Halo4HeadPoseInput levelOwned{};
        levelOwned.quaternion[3] = 1.0f;
        levelOwned.positional = false;
        levelOwned.headOwnsPitch = true;
        Halo4CameraBasis levelled = enginePitched;
        Check(Halo4ApplyHeadPose(levelled, levelOwned) &&
                  fabsf(levelled.forward[2]) < 1.0e-4f,
            "C-H4-9: a LEVEL head produces a LEVEL horizon even though the "
            "engine's own camera is pitched 23 degrees up - the reported "
            "'the up and down stick is breaking my orientation on my head'");
        Check(fabsf(Halo4WrapPi(
                  atan2f(levelled.forward[1], levelled.forward[0]) -
                  atan2f(mono.forward[1], mono.forward[0]))) < 1.0e-4f,
            "Taking pitch does not disturb the engine's HEADING, so the stick "
            "still turns the player, the aim and the view together");

        Halo4CameraBasis added = enginePitched;
        Check(Halo4ApplyHeadPose(added, level) &&
                  fabsf(asinf(added.forward[2]) - 0.4f) < 1.0e-3f,
            "With headOwnsPitch off the C-H4-8 behaviour is bit-for-bit "
            "unchanged: the engine's own pitch survives into the view");

        Halo4HeadPoseInput pitchedOwned = pitched;
        pitchedOwned.headOwnsPitch = true;
        Halo4CameraBasis ownedLook = enginePitched;
        Check(Halo4ApplyHeadPose(ownedLook, pitchedOwned) &&
                  fabsf(asinf(ownedLook.forward[2]) - 0.5235988f) < 1.0e-3f,
            "A 30 degree head pitch puts the view at 30 degrees, not at 30 "
            "PLUS whatever the engine was already holding");
        Check(Halo4ValidateCameraBasis(ownedLook),
            "The head-owned basis stays orthonormal, so the IPD split and the "
            "|fwd.up| validator that rejected C-H4-6 both stay satisfied");

        // 6DOF is shared by both paths and must be identical in each.
        Halo4HeadPoseInput leanOwned{};
        leanOwned.quaternion[3] = 1.0f;
        leanOwned.position[0] = 1.0f;
        leanOwned.positional = true;
        leanOwned.worldScale = 0.33f;
        leanOwned.headOwnsPitch = true;
        Halo4CameraBasis movedOwned = mono;
        Halo4CameraBasis movedDelta = mono;
        Halo4HeadPoseInput leanDelta = leanOwned;
        leanDelta.headOwnsPitch = false;
        Check(Halo4ApplyHeadPose(movedOwned, leanOwned) &&
                  Halo4ApplyHeadPose(movedDelta, leanDelta),
            "Both composition paths accept the same leaning head");
        Check(fabsf(movedOwned.position[0] - movedDelta.position[0]) < 1.0e-5f &&
                  fabsf(movedOwned.position[1] - movedDelta.position[1]) <
                      1.0e-5f &&
                  fabsf(movedOwned.position[2] - movedDelta.position[2]) <
                      1.0e-5f,
            "6DOF is the SAME shared code on both paths, so taking pitch "
            "cannot quietly change how much the world leans");

        // A pitch far past the poles must clamp rather than degenerate.
        Halo4HeadPoseInput extreme{};
        const float halfExtreme = 0.5f * 3.0f;
        extreme.quaternion[0] = sinf(halfExtreme);
        extreme.quaternion[3] = cosf(halfExtreme);
        extreme.positional = false;
        extreme.headOwnsPitch = true;
        extreme.pitchTrim = 1.4f;
        Halo4CameraBasis extremeCamera = mono;
        Check(Halo4ApplyHeadPose(extremeCamera, extreme) &&
                  Halo4ValidateCameraBasis(extremeCamera) &&
                  fabsf(asinf(extremeCamera.forward[2])) <= 1.5f + 1.0e-3f,
            "An extreme head pitch plus trim clamps at 1.5 rad instead of "
            "degenerating the basis at the pole");

        // ---- C-H4-10: yaw ownership ----------------------------------------
        //
        // The doubling hazard, pinned. Once the aim loop steers the engine's
        // heading toward the reference, a view that still reads the engine's
        // LIVE heading applies the head's yaw twice.
        {
            const float reference = 0.7f;
            const float headYaw = 0.4f;   // 23 deg of head turn
            Halo4HeadPoseInput owned{};
            // OpenXR yaw is about +Y and Halo4DecodeHeadOrientation reports
            // atan2(hfx, -hfz), which negates it - so a +headYaw decoded yaw
            // comes from a -headYaw quaternion. The C-H4-8 yaw test above
            // pins the same relationship from the other side.
            const float half = -0.5f * headYaw;
            owned.quaternion[1] = sinf(half);
            owned.quaternion[3] = cosf(half);
            owned.yawSign = 1.0f;
            owned.positional = false;
            owned.headOwnsPitch = true;
            owned.headOwnsYaw = true;
            owned.gameYawReference = reference;

            // The engine has already been steered to (reference + headYaw),
            // which is what a converged loop leaves behind.
            Halo4CameraBasis steered = mono;
            Halo4ComposeHeadOwnedBasis(
                reference + headYaw, 0.0f, 0.0f, steered.forward, steered.up);
            Halo4CameraBasis viewed = steered;
            Check(Halo4ApplyHeadPose(viewed, owned),
                "The yaw-owned path accepts a converged engine heading");
            Check(fabsf(Halo4WrapPi(
                      atan2f(viewed.forward[1], viewed.forward[0]) -
                      (reference + headYaw))) < 1.0e-3f,
                "C-H4-10: the view composes from the REFERENCE, so a converged "
                "aim loop leaves the view exactly where the head points");

            // The same input against the live-heading path is the bug: it
            // lands at reference + 2 * headYaw.
            Halo4HeadPoseInput live = owned;
            live.headOwnsYaw = false;
            Halo4CameraBasis doubled = steered;
            Check(Halo4ApplyHeadPose(doubled, live) &&
                      fabsf(Halo4WrapPi(
                          atan2f(doubled.forward[1], doubled.forward[0]) -
                          (reference + 2.0f * headYaw))) < 1.0e-3f,
                "Reading the engine's live heading instead would double the "
                "head's yaw - the exact reason yaw ownership is not optional "
                "once the aim loop runs");

            // And with no turn and a level head, the reference IS the view.
            Halo4HeadPoseInput still{};
            still.quaternion[3] = 1.0f;
            still.positional = false;
            still.headOwnsPitch = true;
            still.headOwnsYaw = true;
            still.gameYawReference = reference;
            Halo4CameraBasis parked = mono;
            Check(Halo4ApplyHeadPose(parked, still) &&
                      fabsf(Halo4WrapPi(
                          atan2f(parked.forward[1], parked.forward[0]) -
                          reference)) < 1.0e-3f,
                "A level head at the reference looks exactly along it, "
                "whatever the engine's own camera was doing");
        }
    }

    // ---- C-H4-11: first-person hands ----------------------------------
    {
        // The dimensions must reproduce the kit's own allocations, or the
        // layout being written into is not the one E-H4-15/16 proved.
        Check(kHalo4FirstPersonOrientationStride * kHalo4FirstPersonMaxWeapons *
                  kHalo4FirstPersonMaxUsers == 0xF000u,
            "The orientation dimensions reproduce H4EK's 0xF000 allocation");
        Check(kHalo4FirstPersonWeaponsUserStride * kHalo4FirstPersonMaxUsers ==
                  0x17D20u,
            "The fp-weapons dimensions reproduce H4EK's 0x17D20 allocation");
        Check(kHalo4FirstPersonMaxNodes == 120,
            "A first-person node bank holds 120 transforms of 0x20 bytes");
        // E-H4-17, corrected by the C-H4-11 headset probe: the LIVE bank is
        // the first one. Reading the +0xF00 copy is what returned zeros.
        Check(kHalo4FirstPersonNodeArrayOffset == 0 &&
                  kHalo4FirstPersonPreviousNodeArrayOffset == 0xF00,
            "The live node bank is the record's FIRST half; +0xF00 is the "
            "per-frame copy the engine interpolates against");
        Check(kHalo4FirstPersonPreviousNodeArrayOffset +
                  kHalo4FirstPersonMaxNodes * kHalo4FirstPersonNodeStride ==
                      kHalo4FirstPersonOrientationStride,
            "The two banks exactly fill the 0x1E00 orientation record");
        Check(kHalo4FirstPersonRootNode == 0,
            "The assembly is written at its root node");

        // E-H4-21b / C-H4-13: these are title-specific H4EK/tag facts.  If
        // they drift, the runtime must not silently solve a different rig.
        Check(kHalo4ModelSkinningRva == 0x33D8B8u &&
                  kHalo4FirstPersonSkinningReturnRva == 0x36F3C9u,
            "Halo 4 VRIK pins the final palette consumer and its sole FP return");
        Check(kHalo4StormFpBodyNodeCount == 80 &&
                  kHalo4StormFpComposedNodeCount == 85 &&
                  kHalo4RightShoulderNode == 4 &&
                  kHalo4RightElbowNode == 16 &&
                  kHalo4RightHandNode == 29 &&
                  kHalo4LeftShoulderNode == 5 &&
                  kHalo4LeftElbowNode == 8 &&
                  kHalo4LeftHandNode == 37,
            "The Storm body/composed counts and both arm chains match the H4EK tag");
        const auto lengthSquared = [](const float value[3]) {
            return value[0] * value[0] + value[1] * value[1] +
                value[2] * value[2];
        };
        Check(fabsf(lengthSquared(kHalo4RightPoleDirection) - 1.0f) < 1.0e-3f &&
                  fabsf(lengthSquared(kHalo4LeftPoleDirection) - 1.0f) < 1.0e-3f,
            "The Blender-authored pole directions remain normalized");
        Check(fabsf(kHalo4RightAttachmentMetres[1] - 0.059896708f) < 1.0e-6f &&
                  fabsf(kHalo4LeftAttachmentMetres[1] - 0.059896648f) < 1.0e-6f,
            "The two controller-parented attachment offsets preserve authored metres");

        // E-H4-21d / C-H4-15.  Two headset sittings were lost to gating on
        // argument 7.  It is a skinning PALETTE SIZE, not a node count: the
        // live records measured 96, 5 and 33 while storm_fp genuinely has 80
        // nodes, and 80 is not even a reachable palette size for it.  The copy
        // is bounded by the record's fixed bank instead, which is a structural
        // fact rather than a belief about any model.
        Check(kHalo4FirstPersonBankTransforms == 120 &&
                  kHalo4FirstPersonBankTransforms >=
                      kHalo4StormFpBodyNodeCount,
            "The record bank bound covers every Storm body node without "
            "consulting argument 7");
        Check(0xB0u + static_cast<uint32_t>(kHalo4FirstPersonBankTransforms) *
                  0x34u <= 0x1910u,
            "The whole copied bank fits inside one 0x1910 first-person record");
        Check(kHalo4FirstPersonRecordBankOffset == 0xB0u &&
                  kHalo4FirstPersonRecordFillFlagOffset == 0x08u &&
                  kHalo4FirstPersonBodyFillFlag == 0,
            "The record header offsets match the engine's own bank filler");

        // The Storm classifier is what identifies the record now, so it must
        // keep rejecting a rig that merely has enough nodes.
        Check(Halo4StormLinkLengthsMatch(kHalo4StormUpperArmBind,
                  kHalo4StormForearmBind, kHalo4StormUpperArmBind,
                  kHalo4StormForearmBind),
            "The H4EK storm_fp bind lengths sit inside the admitted envelope");
        Check(!Halo4StormLinkLengthsMatch(0.5f, kHalo4StormForearmBind,
                  kHalo4StormUpperArmBind, kHalo4StormForearmBind) &&
                  !Halo4StormLinkLengthsMatch(kHalo4StormUpperArmBind, 0.0f,
                      kHalo4StormUpperArmBind, kHalo4StormForearmBind),
            "A single out-of-envelope link refuses the whole record");
        const float notANumber = std::numeric_limits<float>::quiet_NaN();
        Check(!Halo4StormLinkLengthsMatch(notANumber, kHalo4StormForearmBind,
                  kHalo4StormUpperArmBind, kHalo4StormForearmBind),
            "A NaN link length is refused by the comparisons themselves");

        // This helper is deliberately only a broad sanity envelope. These
        // values came from the old path's misrouted flag-0/120 native body,
        // not storm_fp; admitting them proves the helper is not being reused
        // as C-H4-35's record-identity predicate.
        Check(Halo4StormLinkLengthsMatch(0.2113f, 0.2341f, 0.2135f, 0.3144f),
            "The broad link sanity envelope remains non-identifying (window A)");
        Check(Halo4StormLinkLengthsMatch(0.2100f, 0.2209f, 0.2027f, 0.3192f),
            "The broad link sanity envelope remains non-identifying (window B)");
        // Mirror symmetry on the upper arm is the discriminator that survives
        // the widened absolute range: a real pair of arms agreed to within
        // 3.5% in every sample, and an unrelated rig will not.
        Check(!Halo4StormLinkLengthsMatch(0.2100f, 0.2300f, 0.0800f, 0.2000f),
            "Mismatched left/right upper arms are still refused");

        // C-H4-28: the official H4EK meshes contain 82 upper/forearm
        // cross-weighted vertices per side and 222 right / 235 left
        // forearm/hand cross-weighted vertices. An over-reach must therefore
        // extend both links proportionally; assigning all excess translation
        // to the hand subtree creates the reported torn, badly weighted mesh.
        Halo4ArmReachPlan naturalReach{};
        Check(Halo4PlanArmReach(0.20f, 0.30f, 0.45f, naturalReach) &&
                  fabsf(naturalReach.stretch - 1.0f) < 1.0e-6f &&
                  fabsf(naturalReach.upperExtension) < 1.0e-6f &&
                  fabsf(naturalReach.lowerExtension) < 1.0e-6f,
            "Halo 4 leaves an in-reach authored arm at its stock link lengths");
        Halo4ArmReachPlan extendedReach{};
        Check(Halo4PlanArmReach(0.20f, 0.30f, 0.75f, extendedReach) &&
                  fabsf(extendedReach.stretch - 1.5f) < 1.0e-6f &&
                  fabsf(extendedReach.upperLength - 0.30f) < 1.0e-6f &&
                  fabsf(extendedReach.lowerLength - 0.45f) < 1.0e-6f &&
                  fabsf(extendedReach.upperExtension - 0.10f) < 1.0e-6f &&
                  fabsf(extendedReach.lowerExtension - 0.15f) < 1.0e-6f,
            "Halo 4 distributes tracked over-reach across both weighted arm links");
        Halo4ArmReachPlan boundedReach{};
        Check(Halo4PlanArmReach(0.20f, 0.30f, 2.0f, boundedReach) &&
                  fabsf(boundedReach.stretch - 1.8f) < 1.0e-6f,
            "Halo 4 retains the finite 1.8x arm-stretch safety bound");
        Halo4ArmReachPlan invalidReach{};
        Check(!Halo4PlanArmReach(0.0f, 0.30f, 0.4f, invalidReach) &&
                  !Halo4PlanArmReach(0.20f, 0.30f,
                      std::numeric_limits<float>::quiet_NaN(), invalidReach),
            "Halo 4 refuses invalid arm-link or tracked-span inputs");

        // The 28 helper/fixup/armour bones between the joints. The shared
        // solver writes only the shoulder, the elbow and the hand mask, so
        // these are carried separately; if the band tables ever drift out of
        // the subtree tables the arm tears open between shoulder and hand.
        {
            const int rightShoulder[] = {
                4,11,12,14,15,16,17,18,22,26,27,29,30,31,34,36,38,40,41,42,
                44,45,49,50,52,53,55,56,58,63,65,66,67,68,71,72,76,77,78};
            const int rightElbow[] = {
                16,22,26,27,29,30,31,34,36,38,40,41,42,44,45,49,50,52,53,
                55,56,58,63,65,66,67,68,71,72,76,77,78};
            const int rightHand[] = {
                29,40,41,42,44,45,49,50,52,53,55,56,58,63,65,66,67,68,71,
                72,76,77,78};
            const int rightUpperBand[] = {11,12,14,15,17,18};
            const int rightForearmBand[] = {22,26,27,30,31,34,36,38};
            auto has = [](const int* set, size_t n, int v) {
                for (size_t i = 0; i < n; ++i) if (set[i] == v) return true;
                return false;
            };
            bool upperExact = true;
            for (int i = 0; i < 80; ++i)
            {
                const bool expected =
                    has(rightShoulder, std::size(rightShoulder), i) &&
                    !has(rightElbow, std::size(rightElbow), i) && i != 4;
                if (expected !=
                    has(rightUpperBand, std::size(rightUpperBand), i))
                    upperExact = false;
            }
            bool forearmExact = true;
            for (int i = 0; i < 80; ++i)
            {
                const bool expected =
                    has(rightElbow, std::size(rightElbow), i) &&
                    !has(rightHand, std::size(rightHand), i) && i != 16;
                if (expected !=
                    has(rightForearmBand, std::size(rightForearmBand), i))
                    forearmExact = false;
            }
            Check(upperExact,
                "The right upper-arm band is exactly shoulder minus elbow minus the joint");
            Check(forearmExact,
                "The right forearm band is exactly elbow minus hand minus the joint");
            Check(!has(rightUpperBand, std::size(rightUpperBand), 4) &&
                      !has(rightForearmBand, std::size(rightForearmBand), 16),
                "No joint appears inside the band it drives");
        }
        Check(Halo4StormSideOrderMatches(-0.1f, 0.1f) &&
                  !Halo4StormSideOrderMatches(0.1f, -0.1f) &&
                  !Halo4StormSideOrderMatches(0.1f, 0.1f),
            "Blam's left axis orders the right shoulder below the left one");

        Halo4FirstPersonNode stock{};
        stock.rotation[3] = 1.0f;
        stock.translation[0] = 0.2f;
        stock.scale = 1.0f;
        Check(Halo4FirstPersonNodeLooksValid(stock),
            "A unit quaternion with a sane scale reads as a valid node");

        // The whole point of the live probe: shapes that are NOT
        // {quat,translation,scale} must be refused, not written over.
        Halo4FirstPersonNode notANode = stock;
        notANode.rotation[3] = 37.0f; // a matrix row, or a float that is not a quaternion
        Check(!Halo4FirstPersonNodeLooksValid(notANode),
            "A non-unit rotation is refused, so a wrong layout can never be "
            "written into an engine bone array");
        Halo4FirstPersonNode absurd = stock;
        absurd.translation[1] = 5000.0f;
        Check(!Halo4FirstPersonNodeLooksValid(absurd),
            "A translation far outside the first-person envelope is refused");
        Halo4FirstPersonNode broken = stock;
        broken.scale = std::numeric_limits<float>::quiet_NaN();
        Check(!Halo4FirstPersonNodeLooksValid(broken),
            "A non-finite field is refused");

        // Placement: a controller held 30 cm forward of the head must put the
        // node 30 cm forward in the engine's frame, scaled to world units.
        Halo4HandPlacementInput input{};
        input.controllerOffset[2] = -0.30f; // OpenXR forward is -Z
        input.controllerOrientation[3] = 1.0f;
        input.worldScale = 0.33f;
        Halo4FirstPersonNode placed{};
        Check(Halo4BuildHandNode(input, stock, placed),
            "A tracked controller produces a valid first-person node");
        Check(fabsf(placed.translation[0] - 0.30f * 0.33f) < 1.0e-5f,
            "Reaching forward moves the gun forward along Blam's +X, scaled by "
            "world_scale");
        Check(fabsf(placed.translation[1]) < 1.0e-5f &&
                  fabsf(placed.translation[2]) < 1.0e-5f,
            "A purely forward reach introduces no lateral or vertical shift");
        Check(fabsf(placed.scale - stock.scale) < 1.0e-6f,
            "The engine's own node scale is preserved, never invented");

        // Right and up map onto Blam's -Y (left is +Y) and +Z.
        Halo4HandPlacementInput right{};
        right.controllerOffset[0] = 0.25f; // OpenXR +X is right
        right.controllerOrientation[3] = 1.0f;
        right.worldScale = 1.0f;
        Halo4FirstPersonNode placedRight{};
        Check(Halo4BuildHandNode(right, stock, placedRight) &&
                  fabsf(placedRight.translation[1] + 0.25f) < 1.0e-5f,
            "Moving your hand right moves the gun along Blam's -Y");
        Halo4HandPlacementInput up{};
        up.controllerOffset[1] = 0.25f;
        up.controllerOrientation[3] = 1.0f;
        up.worldScale = 1.0f;
        Halo4FirstPersonNode placedUp{};
        Check(Halo4BuildHandNode(up, stock, placedUp) &&
                  fabsf(placedUp.translation[2] - 0.25f) < 1.0e-5f,
            "Raising your hand raises the gun along Blam's +Z");

        // Mirroring is a left-handed reflection across the forward axis.
        Halo4HandPlacementInput mirrored = right;
        mirrored.mirrored = true;
        Halo4FirstPersonNode placedMirror{};
        Check(Halo4BuildHandNode(mirrored, stock, placedMirror) &&
                  fabsf(placedMirror.translation[1] - 0.25f) < 1.0e-5f,
            "Left-handed mirrors the lateral placement");

        // Trims are applied in the controller's frame, before scaling.
        Halo4HandPlacementInput trimmed{};
        trimmed.controllerOrientation[3] = 1.0f;
        trimmed.worldScale = 1.0f;
        trimmed.forwardTrim = 0.1f;
        Halo4FirstPersonNode placedTrim{};
        Check(Halo4BuildHandNode(trimmed, stock, placedTrim) &&
                  fabsf(placedTrim.translation[0] - 0.1f) < 1.0e-5f,
            "The forward trim moves the gun even with the controller at the "
            "head, so the config can tune an authored placement");

        // The output quaternion must stay unit-length, or the engine gets a
        // bone it cannot use.
        const float length = sqrtf(
            placed.rotation[0] * placed.rotation[0] +
            placed.rotation[1] * placed.rotation[1] +
            placed.rotation[2] * placed.rotation[2] +
            placed.rotation[3] * placed.rotation[3]);
        Check(fabsf(length - 1.0f) < 1.0e-4f,
            "The placed rotation is renormalised before it reaches the engine");

        // Refusal is total: a bad stock node yields no write at all.
        Halo4FirstPersonNode fromBad{};
        Check(!Halo4BuildHandNode(input, notANode, fromBad),
            "A stock node that failed the format proof produces no placement");

        // Final-palette ownership is a world pose built from the controller
        // and the stable recenter/body pair. Neither the current HMD pose nor
        // Halo 4's live aim-camera basis is an input.
        Halo4ControllerWorldPoseInput controllerWorld{};
        controllerWorld.controllerOrientation[3] = 1.0f;
        controllerWorld.controllerPosition[2] = -0.30f;
        controllerWorld.bodyOrigin[0] = 10.0f;
        controllerWorld.bodyOrigin[1] = 20.0f;
        controllerWorld.bodyOrigin[2] = 30.0f;
        controllerWorld.worldScale = 0.33f;
        Halo4ControllerWorldPose neutralWorld{};
        Check(Halo4BuildControllerWorldPose(
                  controllerWorld, neutralWorld),
            "Halo 4 builds a controller world pose without a head-camera parent");
        Check(fabsf(neutralWorld.position[0] - 10.099f) < 1.0e-5f &&
                  fabsf(neutralWorld.position[1] - 20.0f) < 1.0e-5f &&
                  fabsf(neutralWorld.position[2] - 30.0f) < 1.0e-5f,
            "A neutral forward reach is anchored to the stable pre-HMD body origin");
        Check(fabsf(neutralWorld.basis[0] - 1.0f) < 1.0e-5f &&
                  fabsf(neutralWorld.basis[4] - 1.0f) < 1.0e-5f &&
                  fabsf(neutralWorld.basis[8] - 1.0f) < 1.0e-5f,
            "A neutral controller produces Halo 4's native forward-left-up basis");

        // A VR turn rotates position and orientation together about the same
        // body anchor. It does not need the engine camera that motion aim is
        // simultaneously steering toward the hand.
        Halo4ControllerWorldPoseInput snapTurn = controllerWorld;
        snapTurn.gameYawReference = 1.57079632679f;
        Halo4ControllerWorldPose turnedWorld{};
        Check(Halo4BuildControllerWorldPose(snapTurn, turnedWorld),
            "A Halo 4 VR turn produces a valid controller world pose");
        Check(fabsf(turnedWorld.position[0] - 10.0f) < 1.0e-5f &&
                  fabsf(turnedWorld.position[1] - 20.099f) < 1.0e-5f &&
                  fabsf(turnedWorld.basis[0]) < 1.0e-5f &&
                  fabsf(turnedWorld.basis[1] - 1.0f) < 1.0e-5f,
            "Snap turn rotates the hand and its reach once in the game frame");

        // A 30-degree controller yaw must appear exactly once. The rejected
        // C-H4-25/C-H4-26 construction also multiplied by Halo 4's live aim
        // camera, which that same controller had already turned by 30 degrees,
        // and therefore emitted roughly 60 degrees.
        constexpr float controllerYaw = 0.52359877559f;
        Halo4ControllerWorldPoseInput aimed = controllerWorld;
        aimed.yawSign = 1.0f;
        aimed.controllerOrientation[1] = -sinf(controllerYaw * 0.5f);
        aimed.controllerOrientation[3] = cosf(controllerYaw * 0.5f);
        Halo4ControllerWorldPose aimedWorld{};
        Check(Halo4BuildControllerWorldPose(aimed, aimedWorld),
            "A yawed controller produces a valid Halo 4 world basis");
        Check(fabsf(aimedWorld.basis[0] - cosf(controllerYaw)) < 1.0e-5f &&
                  fabsf(aimedWorld.basis[1] - sinf(controllerYaw)) < 1.0e-5f,
            "Controller yaw is applied once, never again through the aim camera");
        Check(fabsf(aimedWorld.position[0] - neutralWorld.position[0]) < 1.0e-5f &&
                  fabsf(aimedWorld.position[1] - neutralWorld.position[1]) < 1.0e-5f,
            "Turning the controller does not orbit its tracked position around the head");

        // Recenter removes the physical facing from both orientation and
        // position. The controller remains 30 cm forward when neutral facing
        // happens to be 30 degrees around the room.
        Halo4ControllerWorldPoseInput recentered = controllerWorld;
        recentered.headYawReference = controllerYaw;
        recentered.controllerOrientation[1] = -sinf(controllerYaw * 0.5f);
        recentered.controllerOrientation[3] = cosf(controllerYaw * 0.5f);
        recentered.controllerPosition[0] = 0.30f * sinf(controllerYaw);
        recentered.controllerPosition[2] = -0.30f * cosf(controllerYaw);
        Halo4ControllerWorldPose recenteredWorld{};
        Check(Halo4BuildControllerWorldPose(recentered, recenteredWorld) &&
                  fabsf(recenteredWorld.position[0] - 10.099f) < 1.0e-5f &&
                  fabsf(recenteredWorld.position[1] - 20.0f) < 1.0e-5f &&
                  fabsf(recenteredWorld.basis[0] - 1.0f) < 1.0e-5f &&
                  fabsf(recenteredWorld.basis[1]) < 1.0e-5f,
            "The recenter pair removes room facing without consulting current head pose");

        Halo4ControllerWorldPoseInput controllerTrim = aimed;
        controllerTrim.controllerPosition[2] = 0.0f;
        controllerTrim.forwardTrim = 0.10f;
        Halo4ControllerWorldPose trimmedWorld{};
        Check(Halo4BuildControllerWorldPose(controllerTrim, trimmedWorld) &&
                  fabsf(trimmedWorld.position[0] -
                      (10.0f + 0.10f * 0.33f * cosf(controllerYaw))) < 1.0e-5f &&
                  fabsf(trimmedWorld.position[1] -
                      (20.0f + 0.10f * 0.33f * sinf(controllerYaw))) < 1.0e-5f,
            "Halo 4 hand trims are applied in the controller's own frame");

        Halo4ControllerWorldPoseInput badControllerWorld = controllerWorld;
        badControllerWorld.gameYawReference =
            std::numeric_limits<float>::quiet_NaN();
        Halo4ControllerWorldPose refusedWorld{};
        Check(!Halo4BuildControllerWorldPose(
                  badControllerWorld, refusedWorld),
            "A non-finite controller reference fails closed for the rig only");

        // --- C-H4-29: the model-frame hand, for the bank that has no camera --
        //
        // E-H4-22: the body fill's root is NULL (or a pure translation), so the
        // arms bank is the model's own frame and the hand must be expressed as
        // the headset sees the controller. The invariants below are the ones
        // that distinguish that conversion from the world-space one above.
        Check(fabsf(Halo4MeasureRigScale(0.2113f, 0.0915251f) - 2.3086f) < 1.0e-3f,
            "The rig scale is measured from the bank against the authored bind");
        Check(Halo4MeasureRigScale(0.0f, 0.0915251f, 1.0f) == 1.0f &&
                  Halo4MeasureRigScale(
                      std::numeric_limits<float>::quiet_NaN(),
                      0.0915251f, 1.0f) == 1.0f,
            "An unmeasurable arm falls back rather than scaling by garbage");
        Check(Halo4MeasureRigScale(1000.0f, 0.0915251f) == kHalo4RigScaleMax &&
                  Halo4MeasureRigScale(1.0e-4f, 10.0f) == kHalo4RigScaleMin,
            "A corrupt bank cannot produce an unbounded tracked reach");

        Halo4ModelHandInput modelHand{};
        modelHand.controllerOrientation[3] = 1.0f;
        modelHand.headOrientation[3] = 1.0f;
        modelHand.controllerPosition[2] = -0.30f;   // 30 cm in front
        modelHand.worldScale = 0.33f;
        modelHand.rigScale = 2.0f;
        Halo4ModelHandPose neutralModel{};
        Check(Halo4BuildModelHandPose(modelHand, neutralModel),
            "Halo 4 builds a model-frame hand from the live head and controller");
        Check(fabsf(neutralModel.position[0] - 0.30f * 0.33f * 2.0f) < 1.0e-5f &&
                  fabsf(neutralModel.position[1]) < 1.0e-5f &&
                  fabsf(neutralModel.position[2]) < 1.0e-5f,
            "A hand held in front of the headset lands ahead of the model origin");
        Check(fabsf(neutralModel.basis[0] - 1.0f) < 1.0e-5f &&
                  fabsf(neutralModel.basis[4] - 1.0f) < 1.0e-5f &&
                  fabsf(neutralModel.basis[8] - 1.0f) < 1.0e-5f,
            "A neutral controller produces Halo 4's native forward-left-up basis");

        // The measured reach scales with the rig, which is the whole reason the
        // factor is read from the bank: the same physical reach must drive a
        // 2.3x rig 2.3x as far or the hand can never catch up with it.
        Halo4ModelHandInput unscaled = modelHand;
        unscaled.rigScale = 1.0f;
        Halo4ModelHandPose unscaledModel{};
        Check(Halo4BuildModelHandPose(unscaled, unscaledModel) &&
                  fabsf(neutralModel.position[0] -
                        2.0f * unscaledModel.position[0]) < 1.0e-5f,
            "Tracked reach is expressed in the units the bank actually uses");

        // THE INVARIANT THAT DEFINES THIS FRAME. Turning your whole body -
        // head and controller together - moves nothing in the model's frame,
        // because that frame is the camera's. No recenter reference and no
        // gameplay yaw is consulted to achieve it, which is precisely why the
        // world-space conversion above is the wrong one for this bank.
        const float bodyYaw = 0.7f;
        const float sy = sinf(bodyYaw * 0.5f), cy = cosf(bodyYaw * 0.5f);
        Halo4ModelHandInput turnedBody = modelHand;
        turnedBody.headOrientation[0] = 0.0f;
        turnedBody.headOrientation[1] = sy;      // yaw about OpenXR +Y
        turnedBody.headOrientation[2] = 0.0f;
        turnedBody.headOrientation[3] = cy;
        turnedBody.controllerOrientation[1] = sy;
        turnedBody.controllerOrientation[3] = cy;
        // The controller stays 30 cm in front of the head after the turn.
        turnedBody.controllerPosition[0] = -0.30f * sinf(bodyYaw);
        turnedBody.controllerPosition[2] = -0.30f * cosf(bodyYaw);
        Halo4ModelHandPose turnedModel{};
        Check(Halo4BuildModelHandPose(turnedBody, turnedModel),
            "A turned player still produces a valid model-frame hand");
        for (int i = 0; i < 3; ++i)
            Check(fabsf(turnedModel.position[i] - neutralModel.position[i]) <
                      1.0e-4f,
                "Turning head and hand together does not move the hand in the "
                "model frame");
        Check(fabsf(turnedModel.basis[0] - 1.0f) < 1.0e-4f &&
                  fabsf(turnedModel.basis[1]) < 1.0e-4f,
            "Turning head and hand together does not rotate the hand either");

        // Moving the head alone must move the hand in the model frame, or the
        // arms would be welded to the face - the C-H4-11 symptom.
        Halo4ModelHandInput leanedHead = modelHand;
        leanedHead.headPosition[0] = 0.10f;   // lean 10 cm right
        Halo4ModelHandPose leanedModel{};
        Check(Halo4BuildModelHandPose(leanedHead, leanedModel) &&
                  fabsf(leanedModel.position[1] -
                        (neutralModel.position[1] +
                         0.10f * 0.33f * 2.0f)) < 1.0e-5f,
            "Leaning the head alone moves the hand within the model frame");

        // Controller yaw appears exactly once, with no camera to double it,
        // and in Halo's axes rather than OpenXR's. A left turn of the wrist is
        // a positive rotation about OpenXR +Y and must swing Blam's forward
        // column toward Blam +Y, which is left.
        Halo4ModelHandInput yawedHand = modelHand;
        yawedHand.controllerOrientation[1] = sinf(controllerYaw * 0.5f);
        yawedHand.controllerOrientation[3] = cosf(controllerYaw * 0.5f);
        Halo4ModelHandPose yawedModel{};
        Check(Halo4BuildModelHandPose(yawedHand, yawedModel) &&
                  fabsf(yawedModel.basis[0] - cosf(controllerYaw)) < 1.0e-4f &&
                  fabsf(yawedModel.basis[1] - sinf(controllerYaw)) < 1.0e-4f,
            "Controller yaw reaches the model-frame hand exactly once");

        Halo4ModelHandInput mirroredHand = modelHand;
        mirroredHand.controllerPosition[0] = 0.20f;
        mirroredHand.controllerOrientation[1] = sinf(controllerYaw * 0.5f);
        mirroredHand.controllerOrientation[3] = cosf(controllerYaw * 0.5f);
        mirroredHand.mirrored = true;
        Halo4ModelHandInput handedHand = mirroredHand;
        handedHand.mirrored = false;
        Halo4ModelHandPose mirroredModel{}, handedModel{};
        Check(Halo4BuildModelHandPose(mirroredHand, mirroredModel) &&
                  Halo4BuildModelHandPose(handedHand, handedModel) &&
                  fabsf(mirroredModel.position[1] +
                        handedModel.position[1]) < 1.0e-5f,
            "Left-handed mirroring reflects the hand across the model's midline");
        // A reflection is not a rotation. Negating one basis column would send
        // the determinant to -1 and render the hand inside out, and nothing
        // downstream would catch it: NormalizedBasis checks column lengths and
        // orthogonality, and InvertBoneMatrix transposes, which a reflection
        // survives.
        {
            const float* m = mirroredModel.basis;
            const float determinant =
                m[0] * (m[4] * m[8] - m[5] * m[7]) -
                m[3] * (m[1] * m[8] - m[2] * m[7]) +
                m[6] * (m[1] * m[5] - m[2] * m[4]);
            Check(fabsf(determinant - 1.0f) < 1.0e-4f,
                "Mirrored hands stay a proper rotation, never an inside-out "
                "reflection");
        }

        // The per-eye stereo offset and the hand share one frame conversion, so
        // a change to either cannot silently desynchronise them. With a neutral
        // head the conversion is the bare axis permutation.
        {
            const float identityHead[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const float delta[3] = {0.032f, 0.0f, 0.0f};  // half an IPD, right
            float blam[3] = {};
            Check(Halo4HeadLocalToBlam(identityHead, delta, blam) &&
                      fabsf(blam[0]) < 1.0e-6f &&
                      fabsf(blam[1] + 0.032f) < 1.0e-6f &&
                      fabsf(blam[2]) < 1.0e-6f,
                "A right-eye offset becomes a negative-left offset in Halo's "
                "axes");
            // Turning the head must carry the offset with it: the right eye is
            // still to the model's right whichever way the player faces.
            const float quarter = 0.5f * 1.5707963f;
            const float turned[4] = {0.0f, sinf(quarter), 0.0f, cosf(quarter)};
            const float worldDelta[3] = {0.0f, 0.0f, -0.032f};
            float turnedBlam[3] = {};
            Check(Halo4HeadLocalToBlam(turned, worldDelta, turnedBlam) &&
                      fabsf(turnedBlam[1] + 0.032f) < 1.0e-5f,
                "The eye offset is expressed in the model frame, not the room");
        }

        Halo4ModelHandInput badModelHand = modelHand;
        badModelHand.headPosition[1] = std::numeric_limits<float>::quiet_NaN();
        Halo4ModelHandPose refusedModel{};
        Check(!Halo4BuildModelHandPose(badModelHand, refusedModel),
            "A non-finite head pose refuses the hand instead of guessing one");

        // The rigid assembly transform: applying one rotation+translation to
        // every node must move the whole rig without depending on which node
        // is the root, and must PRESERVE the shape between nodes.
        Halo4FirstPersonNode a{}, b{};
        a.rotation[3] = 1.0f; a.translation[0] = 0.10f; a.scale = 1.0f;
        b.rotation[3] = 1.0f; b.translation[0] = 0.40f; b.scale = 1.0f;
        const float halfTurn = 0.5f * 1.5707963f; // 90 deg about Blam +Z (up)
        const float yaw[4] = {0.0f, 0.0f, sinf(halfTurn), cosf(halfTurn)};
        const float shift[3] = {1.0f, 2.0f, 3.0f};
        Halo4FirstPersonNode movedA{}, movedB{};
        Check(Halo4TransformAssemblyNode(a, yaw, shift, movedA) &&
                  Halo4TransformAssemblyNode(b, yaw, shift, movedB),
            "Every node accepts the rigid assembly transform");
        const float beforeGap = b.translation[0] - a.translation[0];
        float afterGap = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float d = movedB.translation[axis] - movedA.translation[axis];
            afterGap += d * d;
        }
        Check(fabsf(sqrtf(afterGap) - fabsf(beforeGap)) < 1.0e-4f,
            "The distance between two nodes is unchanged, so the gun and arms "
            "move as ONE RIGID BODY rather than being distorted");
        Check(fabsf(movedA.translation[1] - (0.10f + 2.0f)) < 1.0e-4f &&
                  fabsf(movedA.translation[0] - 1.0f) < 1.0e-4f,
            "A 90 degree yaw carries a node's own offset around with it before "
            "the translation is added");
        Check(fabsf(movedA.scale - a.scale) < 1.0e-6f,
            "The rigid transform never touches a node's scale");
        Halo4FirstPersonNode fromBadNode{};
        Check(!Halo4TransformAssemblyNode(notANode, yaw, shift, fromBadNode),
            "A node that fails the format check is left alone");

        // C-H4-35: pin the live per-eye record ownership that C-H4-34 routed
        // backwards. Argument 7 is deliberately absent from this decision.
        Halo4FloatingRecordPhase recordPhase=
            Halo4FloatingRecordPhase::AwaitStormHands;
        auto recordDecision=Halo4SelectFloatingRecord(recordPhase,1,80);
        Check(recordDecision.action==
                  Halo4FloatingRecordAction::BuildStormHands &&
              recordDecision.next==
                  Halo4FloatingRecordPhase::AwaitHeldModel,
            "The first flag-1/80 record owns the exact Storm hands transaction");
        recordPhase=recordDecision.next;
        recordDecision=Halo4SelectFloatingRecord(recordPhase,1,5);
        Check(recordDecision.action==
                  Halo4FloatingRecordAction::CarryHeldModel &&
              recordDecision.next==
                  Halo4FloatingRecordPhase::AwaitSequenceBoundary,
            "The immediately following flag-1 record consumes the same-eye hand delta as the held model");
        recordPhase=recordDecision.next;
        recordDecision=Halo4SelectFloatingRecord(recordPhase,0,120);
        Check(recordDecision.action==Halo4FloatingRecordAction::Stock &&
              recordDecision.next==
                  Halo4FloatingRecordPhase::AwaitStormHands,
            "The flag-0/120 native body stays stock and closes the record sequence");
        Check(Halo4SelectFloatingRecord(
                  Halo4FloatingRecordPhase::AwaitStormHands,1,5).action==
                  Halo4FloatingRecordAction::Stock &&
              Halo4SelectFloatingRecord(
                  Halo4FloatingRecordPhase::AwaitSequenceBoundary,1,80).action==
                  Halo4FloatingRecordAction::Stock &&
              Halo4SelectFloatingRecord(
                  Halo4FloatingRecordPhase::AwaitStormHands,-1,80).action==
                  Halo4FloatingRecordAction::Stock,
            "A reversed, late, unreadable, or count-mismatched record can never inherit Storm ownership");
        Check(kHalo4StormFpRuntimeImportChecksum==0x150D0000u &&
              kHalo4FirstPersonSkinningRecordStride==0x1910u,
            "The official Storm checksum is telemetry and exact record adjacency is the held-object ownership key");
        Check(Halo4ExpectedHeldRecordSource(0x100000u)==0x101910u &&
              Halo4ExpectedHeldRecordSource(0)==0 &&
              Halo4ExpectedHeldRecordSource(~uintptr_t{0})==0,
            "Held-model ownership requires exact forward 0x1910 adjacency and refuses null or overflowing sources");

        // Floating hands are a tested final-palette ownership map, not a
        // scale-everything guess inside the render detour.
        int rightHands = 0;
        int leftHands = 0;
        int collapseRight = 0;
        int collapseLeft = 0;
        int hidden = 0;
        for (int node = 0; node < kHalo4StormFpBodyNodeCount; ++node)
        {
            switch (Halo4ClassifyFloatingNode(node))
            {
            case Halo4FloatingNodeRole::RightHand: ++rightHands; break;
            case Halo4FloatingNodeRole::LeftHand: ++leftHands; break;
            case Halo4FloatingNodeRole::CollapseAtRightWrist:
                ++collapseRight;
                break;
            case Halo4FloatingNodeRole::CollapseAtLeftWrist:
                ++collapseLeft;
                break;
            case Halo4FloatingNodeRole::Hidden: ++hidden; break;
            case Halo4FloatingNodeRole::OutsideBody:
                Check(false, "Every Storm body node has a floating-hands role");
                break;
            }
        }
        Check(rightHands == 23 && leftHands == 20 &&
                  collapseRight == 16 && collapseLeft == 16 && hidden == 5,
            "The H4EK Storm mask keeps 43 hand nodes, wrist-collapses 32 arm "
            "influences, and hides the five unrelated body nodes");
        Check(Halo4ClassifyFloatingNode(kHalo4RightHandNode) ==
                  Halo4FloatingNodeRole::RightHand &&
              Halo4ClassifyFloatingNode(kHalo4LeftHandNode) ==
                  Halo4FloatingNodeRole::LeftHand &&
              kHalo4LeftThumbBaseNode == 46 &&
              Halo4StormNodeInSet(
                  kHalo4LeftHandSubtree,kHalo4LeftThumbBaseNode) &&
              Halo4ClassifyFloatingNode(kHalo4RightElbowNode) ==
                  Halo4FloatingNodeRole::CollapseAtRightWrist &&
              Halo4ClassifyFloatingNode(kHalo4LeftElbowNode) ==
                  Halo4FloatingNodeRole::CollapseAtLeftWrist,
            "Both wrists stay visible while each cross-weighted forearm "
            "collapses at its own solved wrist");
        Check(Halo4ClassifyFloatingNode(-1) ==
                  Halo4FloatingNodeRole::OutsideBody &&
              Halo4ClassifyFloatingNode(kHalo4StormFpBodyNodeCount) ==
                  Halo4FloatingNodeRole::OutsideBody,
            "The floating-hands map never claims a node outside storm_fp");
        bool rightClosureExact = true;
        for (int node : kHalo4RightShoulderSubtree)
        {
            const auto role = Halo4ClassifyFloatingNode(node);
            rightClosureExact = rightClosureExact &&
                (role == Halo4FloatingNodeRole::RightHand ||
                 role == Halo4FloatingNodeRole::CollapseAtRightWrist);
        }
        bool leftClosureExact = true;
        for (int node : kHalo4LeftShoulderSubtree)
        {
            const auto role = Halo4ClassifyFloatingNode(node);
            leftClosureExact = leftClosureExact &&
                (role == Halo4FloatingNodeRole::LeftHand ||
                 role == Halo4FloatingNodeRole::CollapseAtLeftWrist);
        }
        Check(rightClosureExact && leftClosureExact,
            "Every H4EK arm descendant is either kept as a hand or co-located "
            "with the matching wrist; no ribbon influence is left behind");

        // Dormant C-H4-34 helpers remain pinned so history cannot silently
        // change, but C-H4-35 never calls this previous-pair cache path.
        Check(Halo4FloatingHandCacheIsCurrent(7,100,7,100) &&
                  Halo4FloatingHandCacheIsCurrent(7,100,7,99),
            "The gun accepts only this pair's or the immediately prior body's "
            "untouched local wrist relation");
        Check(!Halo4FloatingHandCacheIsCurrent(7,100,7,98) &&
                  !Halo4FloatingHandCacheIsCurrent(7,100,7,101) &&
                  !Halo4FloatingHandCacheIsCurrent(7,100,8,100) &&
                  !Halo4FloatingHandCacheIsCurrent(0,100,0,100) &&
                  !Halo4FloatingHandCacheIsCurrent(7,0,7,0),
            "A stale, future, cross-generation, or unkeyed body relation can "
            "never move a Halo 4 weapon record");
        Check(Halo4FloatingRelationPairIsCurrent(
                  3,7,100,true,3,7,99,true,3,7,99) &&
              !Halo4FloatingRelationPairIsCurrent(
                  3,7,100,true,3,7,99,false,3,7,99) &&
              !Halo4FloatingRelationPairIsCurrent(
                  3,7,100,true,3,7,99,true,3,7,98) &&
              !Halo4FloatingRelationPairIsCurrent(
                  3,7,100,true,3,7,99,true,4,7,99),
            "Stereo gun admission accepts only a complete, same-serial, "
            "same-epoch relation pair");

        // Dormant C-H4-34 invariant: BODY wrists are world-rooted. Cache
        // inverse(eye)*stock, reconstruct through each eye, and derive both
        // body and gun deltas against one shared physical target.
        Halo4FloatingTransform eye0{}, eye1{}, localWrist{}, targetWorld{};
        eye0.translation[0] = -0.032f;
        eye1.translation[0] = 0.032f;
        // A non-yaw camera root does not commute with the wrist yaw.
        // Keeping both eyes at identity here would let an order/transpose bug
        // hide behind yaw-only algebra.
        const float eyePitch = 0.42f;
        const float ce = cosf(eyePitch);
        const float se = sinf(eyePitch);
        eye0.rotation[4] = ce;
        eye0.rotation[5] = se;
        eye0.rotation[7] = -se;
        eye0.rotation[8] = ce;
        const float eye1Pitch = -0.23f;
        const float ce1 = cosf(eye1Pitch);
        const float se1 = sinf(eye1Pitch);
        eye1.rotation[4] = ce1;
        eye1.rotation[5] = se1;
        eye1.rotation[7] = -se1;
        eye1.rotation[8] = ce1;
        localWrist.translation[0] = 0.45f;
        localWrist.translation[1] = -0.18f;
        localWrist.translation[2] = 0.08f;
        localWrist.scale = 1.25f;
        const float relationYaw = 0.35f;
        localWrist.rotation[0] = cosf(relationYaw);
        localWrist.rotation[1] = sinf(relationYaw);
        localWrist.rotation[3] = -sinf(relationYaw);
        localWrist.rotation[4] = cosf(relationYaw);
        targetWorld.translation[0] = 4.25f;
        targetWorld.translation[1] = -1.50f;
        targetWorld.translation[2] = 2.10f;
        targetWorld.scale = localWrist.scale;
        const float targetPitch = -0.6f;
        const float ct = cosf(targetPitch);
        const float st = sinf(targetPitch);
        targetWorld.rotation[0] = ct;
        targetWorld.rotation[2] = -st;
        targetWorld.rotation[6] = st;
        targetWorld.rotation[8] = ct;

        Halo4FloatingTransform stockEye0{}, stockEye1{};
        Check(Halo4ComposeFloatingTransforms(
                  eye0, localWrist, stockEye0) &&
              Halo4ComposeFloatingTransforms(
                  eye1, localWrist, stockEye1),
            "Each stock BODY wrist is currentEye times one eye-local relation");
        Check(fabsf(stockEye0.translation[0] - (-0.032f + 0.45f)) <
                  1.0e-5f &&
              fabsf(stockEye1.translation[0] - (0.032f + 0.45f)) <
                  1.0e-5f &&
              fabsf(stockEye0.translation[1] -
                    (ce * -0.18f - se * 0.08f)) < 1.0e-5f &&
              fabsf(stockEye0.translation[2] -
                    (se * -0.18f + ce * 0.08f)) < 1.0e-5f &&
              fabsf(stockEye0.rotation[0] - cosf(relationYaw)) < 1.0e-5f &&
              fabsf(stockEye0.rotation[1] -
                    ce * sinf(relationYaw)) < 1.0e-5f &&
              fabsf(stockEye0.rotation[2] -
                    se * sinf(relationYaw)) < 1.0e-5f &&
              fabsf(stockEye0.rotation[7] + se) < 1.0e-5f &&
              fabsf(stockEye0.scale - 1.25f) < 1.0e-5f,
            "The eye-times-wrist result matches the manually expanded "
            "noncommuting column-basis transform and non-unit scale");
        Halo4FloatingTransform cached0{}, cached1{};
        Check(Halo4BuildFloatingEyeLocalWrist(
                  eye0, stockEye0, cached0) &&
              Halo4BuildFloatingEyeLocalWrist(
                  eye1, stockEye1, cached1),
            "The body transaction strips only the current eye from stock");
        for (int i = 0; i < 9; ++i)
            Check(fabsf(cached0.rotation[i] - cached1.rotation[i]) < 1.0e-5f &&
                      fabsf(cached0.rotation[i] - localWrist.rotation[i]) <
                          1.0e-5f,
                "Both eyes cache the same stock-wrist orientation relation");
        for (int i = 0; i < 3; ++i)
            Check(fabsf(cached0.translation[i] - cached1.translation[i]) <
                      1.0e-5f &&
                      fabsf(cached0.translation[i] -
                            localWrist.translation[i]) < 1.0e-5f,
                "Both eyes cache the same stock-wrist position relation");
        Check(fabsf(cached0.scale-localWrist.scale)<1.0e-5f &&
                  fabsf(cached1.scale-localWrist.scale)<1.0e-5f,
            "Eye-local reconstruction preserves the authored wrist scale");

        const auto landsOnTarget = [&](const Halo4FloatingTransform& eyeRoot,
                                       const Halo4FloatingTransform& stock,
                                       const Halo4FloatingTransform& cached)
        {
            Halo4FloatingTransform bodyDelta{}, bodyResult{};
            Halo4FloatingTransform rebuiltStock{}, gunDelta{}, gunResult{};
            return Halo4BuildFloatingWorldDelta(
                       targetWorld, stock, bodyDelta) &&
                Halo4ComposeFloatingTransforms(
                       bodyDelta, stock, bodyResult) &&
                Halo4ComposeFloatingTransforms(
                       eyeRoot, cached, rebuiltStock) &&
                Halo4BuildFloatingWorldDelta(
                       targetWorld, rebuiltStock, gunDelta) &&
                Halo4ComposeFloatingTransforms(
                       gunDelta, rebuiltStock, gunResult) &&
                [&]()
                {
                    for (int i = 0; i < 9; ++i)
                        if (fabsf(bodyResult.rotation[i] -
                                  targetWorld.rotation[i]) >= 1.0e-5f ||
                            fabsf(gunResult.rotation[i] -
                                  targetWorld.rotation[i]) >= 1.0e-5f)
                            return false;
                    for (int i = 0; i < 3; ++i)
                        if (fabsf(bodyResult.translation[i] -
                                  targetWorld.translation[i]) >= 1.0e-5f ||
                            fabsf(gunResult.translation[i] -
                                  targetWorld.translation[i]) >= 1.0e-5f)
                            return false;
                    return fabsf(bodyResult.scale-targetWorld.scale)<1.0e-5f &&
                        fabsf(gunResult.scale-targetWorld.scale)<1.0e-5f;
                }();
        };
        Check(landsOnTarget(eye0,stockEye0,cached0) &&
                  landsOnTarget(eye1,stockEye1,cached1),
            "The dormant C-H4-34 direct BODY and reconstructed-gun algebra remains internally exact");
        Halo4FloatingTransform heldLocal{},heldWorld{},sameFrameDelta{};
        Halo4FloatingTransform movedHeld{},expectedHeld{};
        heldLocal.translation[0]=0.21f;
        heldLocal.translation[1]=-0.04f;
        heldLocal.translation[2]=0.07f;
        Check(Halo4ComposeFloatingTransforms(
                  stockEye0,heldLocal,heldWorld) &&
              Halo4BuildFloatingWorldDelta(
                  targetWorld,stockEye0,sameFrameDelta) &&
              Halo4ComposeFloatingTransforms(
                  sameFrameDelta,heldWorld,movedHeld) &&
              Halo4ComposeFloatingTransforms(
                  targetWorld,heldLocal,expectedHeld),
            "The current Storm wrist delta composes over the immediately adjacent held graph");
        bool heldMatches=true;
        for (int i=0;i<9;++i)
            heldMatches=heldMatches &&
                fabsf(movedHeld.rotation[i]-expectedHeld.rotation[i])<1.0e-5f;
        for (int i=0;i<3;++i)
            heldMatches=heldMatches &&
                fabsf(movedHeld.translation[i]-expectedHeld.translation[i])<
                    1.0e-5f;
        Check(heldMatches,
            "Hands and gun share one same-eye rigid motion and preserve the authored grip relation");

        Halo4FloatingTransform rigidSupportTarget{};
        Check(Halo4BuildFloatingRigidSupportTarget(
                  targetWorld,stockEye0,heldWorld,rigidSupportTarget),
            "Halo 4 two-hand support accepts the right-hand rigid world motion");
        bool rigidSupportMatchesHeld=true;
        for (int i=0;i<9;++i)
            rigidSupportMatchesHeld=rigidSupportMatchesHeld &&
                fabsf(rigidSupportTarget.rotation[i]-
                      expectedHeld.rotation[i])<1.0e-5f;
        for (int i=0;i<3;++i)
            rigidSupportMatchesHeld=rigidSupportMatchesHeld &&
                fabsf(rigidSupportTarget.translation[i]-
                      expectedHeld.translation[i])<1.0e-5f;
        Check(rigidSupportMatchesHeld &&
                  fabsf(rigidSupportTarget.scale-expectedHeld.scale)<1.0e-5f,
            "Halo 4 two-hand support preserves the authored hand-to-weapon relation instead of following left-controller translation");
        Halo4FloatingTransform invalidRigidSupport=stockEye0;
        invalidRigidSupport.translation[1]=
            std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedRigidSupport{};
        untouchedRigidSupport.translation[0]=71.0f;
        Check(!Halo4BuildFloatingRigidSupportTarget(
                  targetWorld,invalidRigidSupport,heldWorld,
                  untouchedRigidSupport) &&
                  untouchedRigidSupport.translation[0]==71.0f,
            "an invalid Halo 4 rigid support relation publishes no partial target");

        // C-H4-36: replace only the current eye's orientation parent.  The
        // live eye-local wrist relation is the title's own authored mount; a
        // Blender bind/control quaternion is not.  Pitch(E), yaw(L), and
        // pitch(C) deliberately do not all commute.
        const float physicalTarget[3] = {
            targetWorld.translation[0],targetWorld.translation[1],
            targetWorld.translation[2]};
        Halo4FloatingTransform controllerCarrier{};
        Check(Halo4BuildFloatingControllerCarrier(
                  targetWorld.rotation,physicalTarget,false,
                  31.0f,-17.0f,23.0f,controllerCarrier),
            "The production Halo 4 right carrier accepts a calibrated aim basis");
        bool rightCarrierUnchanged=true;
        for (int i=0;i<9;++i)
            rightCarrierUnchanged=rightCarrierUnchanged &&
                fabsf(controllerCarrier.rotation[i]-targetWorld.rotation[i])<
                    1.0e-6f;
        Check(rightCarrierUnchanged,
            "The right carrier is exactly the aim basis: gun angles are never applied twice and no Blender seed survives");

        constexpr float carrierYawDeg=31.0f;
        constexpr float carrierPitchDeg=-17.0f;
        constexpr float carrierRollDeg=23.0f;
        Halo4FloatingTransform leftCarrier{};
        Check(Halo4BuildFloatingControllerCarrier(
                  targetWorld.rotation,physicalTarget,true,
                  carrierYawDeg,carrierPitchDeg,carrierRollDeg,leftCarrier),
            "The dormant C-H4-36 left-carrier branch retains its mirrored presentation-trim algebra");
        constexpr float testDegreesToRadians=0.01745329252f;
        const float leftYaw=-carrierYawDeg*testDegreesToRadians;
        const float leftPitch=carrierPitchDeg*testDegreesToRadians;
        const float leftRoll=-carrierRollDeg*testDegreesToRadians;
        const float lcp=cosf(leftPitch),lsp=sinf(leftPitch);
        const float lcy=cosf(leftYaw),lsy=sinf(leftYaw);
        const float lcr=cosf(leftRoll),lsr=sinf(leftRoll);
        const float leftForward[3]={lcp*lcy,lcp*lsy,lsp};
        const float leftUp[3]={
            (-lsp*lcy)*lcr+lsy*lsr,
            (-lsp*lsy)*lcr-lcy*lsr,lcp*lcr};
        Halo4FloatingTransform expectedLeftMount{};
        memcpy(expectedLeftMount.rotation,leftForward,sizeof(leftForward));
        expectedLeftMount.rotation[3]=
            leftUp[1]*leftForward[2]-leftUp[2]*leftForward[1];
        expectedLeftMount.rotation[4]=
            leftUp[2]*leftForward[0]-leftUp[0]*leftForward[2];
        expectedLeftMount.rotation[5]=
            leftUp[0]*leftForward[1]-leftUp[1]*leftForward[0];
        memcpy(expectedLeftMount.rotation+6,leftUp,sizeof(leftUp));
        Halo4FloatingTransform rawController{};
        memcpy(rawController.rotation,targetWorld.rotation,
               sizeof(rawController.rotation));
        memcpy(rawController.translation,physicalTarget,
               sizeof(rawController.translation));
        Halo4FloatingTransform expectedLeftCarrier{};
        Check(Halo4ComposeFloatingTransforms(
                  rawController,expectedLeftMount,expectedLeftCarrier),
            "The independent left-trim reference is a proper noncommuting basis");
        bool leftCarrierMatches=true;
        for (int i=0;i<9;++i)
            leftCarrierMatches=leftCarrierMatches &&
                fabsf(leftCarrier.rotation[i]-
                    expectedLeftCarrier.rotation[i])<1.0e-5f;
        Check(leftCarrierMatches,
            "The left carrier postmultiplies exactly (-yaw, +pitch, -roll), matching Halo 3, ODST and Reach");

        // C-H4-38 replaces that unaccepted Halo 4 left mount with one
        // prepared-frame parent policy. Free uses the raw left controller;
        // support copies only the frozen right-aim rotation while retaining
        // the already-working left physical position and scale.
        const float leftPhysicalTarget[3]={
            physicalTarget[0]+0.37f,physicalTarget[1]-0.21f,
            physicalTarget[2]+0.12f};
        Halo4FloatingTransform rawLeftStateCarrier{};
        Check(Halo4BuildFloatingControllerCarrier(
                  eye1.rotation,leftPhysicalTarget,false,
                  carrierYawDeg,carrierPitchDeg,carrierRollDeg,
                  rawLeftStateCarrier),
            "The C-H4-38 free-left carrier accepts the raw prepared controller with no borrowed gun trim");
        bool rawLeftUntrimmed=true;
        for (int i=0;i<9;++i)
            rawLeftUntrimmed=rawLeftUntrimmed &&
                fabsf(rawLeftStateCarrier.rotation[i]-eye1.rotation[i])<1.0e-6f;
        Check(rawLeftUntrimmed,
            "Nonzero universal gun angles do not cant the independent Halo 4 hand");

        Halo4FloatingTransform selectedFreeCarrier{};
        Halo4FloatingTransform selectedSupportCarrier{};
        Check(Halo4BuildFloatingLeftCarrierForState(
                  false,rawLeftStateCarrier,controllerCarrier,
                  selectedFreeCarrier) &&
              memcmp(&selectedFreeCarrier,&rawLeftStateCarrier,
                     sizeof(selectedFreeCarrier))==0,
            "Free mode preserves the raw left-controller carrier byte-for-byte");
        Check(Halo4BuildFloatingLeftCarrierForState(
                  true,rawLeftStateCarrier,controllerCarrier,
                  selectedSupportCarrier),
            "Support mode accepts the exact frozen right-aim carrier");
        Halo4FloatingTransform closestFreeCarrier{},closestSupportCarrier{};
        Halo4FloatingTransform expectedClosestFreeCarrier{};
        Check(Halo4BuildFloatingClosestLeftCarrierForState(
                  false,rawLeftStateCarrier,controllerCarrier,
                  carrierYawDeg,carrierPitchDeg,carrierRollDeg,
                  closestFreeCarrier) &&
              Halo4BuildFloatingControllerCarrier(
                  rawLeftStateCarrier.rotation,
                  rawLeftStateCarrier.translation,true,
                  carrierYawDeg,carrierPitchDeg,carrierRollDeg,
                  expectedClosestFreeCarrier) &&
              memcmp(&closestFreeCarrier,&expectedClosestFreeCarrier,
                     sizeof(closestFreeCarrier))==0,
            "C-H4-42 free mode restores C-H4-37's mirrored left-controller carrier exactly");
        Check(Halo4BuildFloatingClosestLeftCarrierForState(
                  true,rawLeftStateCarrier,controllerCarrier,
                  carrierYawDeg,carrierPitchDeg,carrierRollDeg,
                  closestSupportCarrier) &&
              memcmp(&closestSupportCarrier,&selectedSupportCarrier,
                     sizeof(closestSupportCarrier))==0,
            "C-H4-42 support remains byte-identical to the accepted C-H4-38 right-aim parent");
        bool supportUsesRightAim=true;
        for (int i=0;i<9;++i)
            supportUsesRightAim=supportUsesRightAim &&
                fabsf(selectedSupportCarrier.rotation[i]-
                       controllerCarrier.rotation[i])<1.0e-6f;
        Check(supportUsesRightAim &&
                  selectedSupportCarrier.translation[0]==
                      rawLeftStateCarrier.translation[0] &&
                  selectedSupportCarrier.translation[1]==
                      rawLeftStateCarrier.translation[1] &&
                  selectedSupportCarrier.translation[2]==
                      rawLeftStateCarrier.translation[2] &&
                  selectedSupportCarrier.scale==rawLeftStateCarrier.scale,
            "Support copies only the gun parent rotation and cannot move the left hand");
        Halo4FloatingTransform invalidStateRight=controllerCarrier;
        invalidStateRight.rotation[0]=
            std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedStateCarrier{};
        untouchedStateCarrier.translation[0]=123.0f;
        Check(!Halo4BuildFloatingLeftCarrierForState(
                  true,rawLeftStateCarrier,invalidStateRight,
                  untouchedStateCarrier) &&
                  untouchedStateCarrier.translation[0]==123.0f,
            "An invalid support parent publishes no partial left carrier");
        Check(Halo4BuildFloatingLeftCarrierForState(
                  false,rawLeftStateCarrier,invalidStateRight,
                  untouchedStateCarrier) &&
                  memcmp(&untouchedStateCarrier,&rawLeftStateCarrier,
                         sizeof(untouchedStateCarrier))==0,
            "Free mode has no dependency on an unused support orientation");

        Halo4FloatingTransform rerooted0{},rerooted1{};
        Check(Halo4BuildFloatingControllerRerootTarget(
                  controllerCarrier,eye0,stockEye0,rerooted0) &&
              Halo4BuildFloatingControllerRerootTarget(
                  controllerCarrier,eye1,stockEye1,rerooted1),
            "Each current eye builds a controller-facing Halo 4 wrist from its live stock relation");
        Halo4FloatingTransform expectedReroot{};
        Check(Halo4ComposeFloatingTransforms(
                  controllerCarrier,localWrist,expectedReroot),
            "The controller-times-live-wrist reference is valid");
        bool rerootRotationMatches=true;
        for (int i=0;i<9;++i)
            rerootRotationMatches=rerootRotationMatches &&
                fabsf(rerooted0.rotation[i]-expectedReroot.rotation[i])<
                    1.0e-5f &&
                fabsf(rerooted1.rotation[i]-expectedReroot.rotation[i])<
                    1.0e-5f;
        Check(rerootRotationMatches,
            "The final wrist faces as controller times the live authored wrist, with no fixed seed rotation");
        Check(rerooted0.translation[0]==controllerCarrier.translation[0] &&
                  rerooted0.translation[1]==controllerCarrier.translation[1] &&
                  rerooted0.translation[2]==controllerCarrier.translation[2] &&
                  fabsf(rerooted0.scale-stockEye0.scale)<1.0e-6f,
            "Orientation rerooting preserves C-H4-35's physical target position and the stock wrist scale");

        Halo4FloatingTransform inverseEye0{},expectedRerootDelta{};
        Halo4FloatingTransform rerootDelta{};
        Check(Halo4InvertFloatingTransform(eye0,inverseEye0) &&
              Halo4ComposeFloatingTransforms(
                  controllerCarrier,inverseEye0,expectedRerootDelta) &&
              Halo4BuildFloatingWorldDelta(
                  rerooted0,stockEye0,rerootDelta),
            "The final target produces one valid current-eye controller delta");
        bool rerootDeltaMatches=true;
        for (int i=0;i<9;++i)
            rerootDeltaMatches=rerootDeltaMatches &&
                fabsf(rerootDelta.rotation[i]-
                    expectedRerootDelta.rotation[i])<1.0e-5f;
        Check(rerootDeltaMatches,
            "The wrist delta cancels to controller times inverse current-eye orientation");

        Halo4FloatingTransform freeStateReroot{};
        Check(Halo4BuildFloatingControllerRerootTarget(
                  selectedFreeCarrier,eye0,stockEye0,freeStateReroot),
            "The C-H4-38 free hand reroots from the raw left-controller parent");
        Halo4FloatingTransform closestFreeReroot{};
        Check(Halo4BuildFloatingControllerRerootTarget(
                  closestFreeCarrier,eye0,stockEye0,closestFreeReroot),
            "C-H4-42 restores the exact C-H4-37 mirrored-carrier current-eye reroot");
        Halo4FloatingTransform parityFreeTarget{},expectedParityCarrier{};
        Check(Halo4BuildFloatingFreeLeftParityTarget(
                  selectedFreeCarrier,carrierYawDeg,carrierPitchDeg,
                  carrierRollDeg,freeStateReroot,parityFreeTarget) &&
              Halo4BuildFloatingControllerCarrier(
                  selectedFreeCarrier.rotation,
                  selectedFreeCarrier.translation,true,
                  carrierYawDeg,carrierPitchDeg,carrierRollDeg,
                  expectedParityCarrier),
            "C-H4-40 builds the free wrist with the shared H3/ODST/Reach left-controller mount");
        bool parityFreeRotationMatches=true;
        for (int i=0;i<9;++i)
            parityFreeRotationMatches=parityFreeRotationMatches &&
                fabsf(parityFreeTarget.rotation[i]-
                       expectedParityCarrier.rotation[i])<1.0e-6f;
        Check(parityFreeRotationMatches,
            "The free wrist equals controller times mirrored trim directly, without a camera-local wrist or finger-to-aim mapping");
        Check(parityFreeTarget.translation[0]==freeStateReroot.translation[0] &&
                  parityFreeTarget.translation[1]==freeStateReroot.translation[1] &&
                  parityFreeTarget.translation[2]==freeStateReroot.translation[2] &&
                  parityFreeTarget.scale==freeStateReroot.scale,
            "The C-H4-40 parity mount preserves C-H4-38 free-hand placement and stock scale exactly");
        bool parityDoesNotRetainEyeWrist=false;
        for (int i=0;i<9;++i)
            parityDoesNotRetainEyeWrist=parityDoesNotRetainEyeWrist ||
                fabsf(parityFreeTarget.rotation[i]-
                       freeStateReroot.rotation[i])>1.0e-3f;
        Check(parityDoesNotRetainEyeWrist,
            "The noncommuting fixture proves C-H4-40 removed the rejected live camera-local wrist relation");
        // C-H4-43 uses the named cross-title attachment frame, not wrist-bone
        // axis equality. Reach's official left_hand marker is identity on its
        // wrist. This is Halo 4's official left_hand marker quaternion on
        // identity child b_l_hand_marker_offset, converted to a Blam basis.
        const float halo4LeftHandMarkerBasis[9]={
            0.0143071814f,-0.996801702f,-0.0786236117f,
            -0.983843685f,-0.0000000752f,-0.179029617f,
            0.178457021f,0.0799147531f,-0.980697060f};
        Halo4FloatingTransform markerParityTarget{};
        Check(Halo4BuildFloatingFreeLeftMarkerParityTarget(
                  expectedParityCarrier,halo4LeftHandMarkerBasis,
                  freeStateReroot,markerParityTarget),
            "C-H4-43 solves Halo 4's wrist from its official left_hand marker and the Reach-style controller mount");
        Halo4FloatingTransform markerLocal{},movedMarker{};
        memcpy(markerLocal.rotation,halo4LeftHandMarkerBasis,
               sizeof(markerLocal.rotation));
        Check(Halo4ComposeFloatingTransforms(
                  markerParityTarget,markerLocal,movedMarker),
            "The solved Halo 4 wrist carries its named left_hand marker into world space");
        bool markerMatchesOtherTitles=true;
        for (int i=0;i<9;++i)
            markerMatchesOtherTitles=markerMatchesOtherTitles &&
                fabsf(movedMarker.rotation[i]-
                       expectedParityCarrier.rotation[i])<1.0e-5f;
        Check(markerMatchesOtherTitles,
            "Halo 4's left_hand marker—not its incompatible wrist bone axes—equals the H3/ODST/Reach controller target");
        Check(markerParityTarget.translation[0]==freeStateReroot.translation[0] &&
                  markerParityTarget.translation[1]==freeStateReroot.translation[1] &&
                  markerParityTarget.translation[2]==freeStateReroot.translation[2] &&
                  markerParityTarget.scale==freeStateReroot.scale,
            "Cross-title marker parity changes free-hand orientation only");
        float invalidMarkerBasis[9];
        memcpy(invalidMarkerBasis,halo4LeftHandMarkerBasis,
               sizeof(invalidMarkerBasis));
        invalidMarkerBasis[0]=std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedMarkerParity{};
        untouchedMarkerParity.translation[0]=22.0f;
        Check(!Halo4BuildFloatingFreeLeftMarkerParityTarget(
                  expectedParityCarrier,invalidMarkerBasis,freeStateReroot,
                  untouchedMarkerParity) &&
                  untouchedMarkerParity.translation[0]==22.0f,
            "Invalid optional marker calibration publishes nothing over the C-H4-38 fallback");
        Halo4FloatingTransform untouchedParity{};
        untouchedParity.translation[0]=44.0f;
        Check(!Halo4BuildFloatingFreeLeftParityTarget(
                  selectedFreeCarrier,
                  std::numeric_limits<float>::quiet_NaN(),carrierPitchDeg,
                  carrierRollDeg,freeStateReroot,untouchedParity) &&
                  untouchedParity.translation[0]==44.0f,
            "Invalid optional C-H4-40 mount input publishes nothing over the C-H4-38 fallback");
        Halo4FloatingTransform supportLocalWrist{};
        supportLocalWrist.translation[0]=-0.16f;
        supportLocalWrist.translation[1]=0.29f;
        supportLocalWrist.translation[2]=-0.05f;
        supportLocalWrist.scale=0.92f;
        const float supportLocalRoll=-0.31f;
        supportLocalWrist.rotation[0]=cosf(supportLocalRoll);
        supportLocalWrist.rotation[1]=sinf(supportLocalRoll);
        supportLocalWrist.rotation[3]=-sinf(supportLocalRoll);
        supportLocalWrist.rotation[4]=cosf(supportLocalRoll);
        Halo4FloatingTransform stockSupportLeft{},supportStateReroot{};
        Halo4FloatingTransform supportLeftDelta{};
        Check(Halo4ComposeFloatingTransforms(
                  eye0,supportLocalWrist,stockSupportLeft) &&
              Halo4BuildFloatingControllerRerootTarget(
                  selectedSupportCarrier,eye0,stockSupportLeft,
                  supportStateReroot) &&
              Halo4BuildFloatingWorldDelta(
                  supportStateReroot,stockSupportLeft,supportLeftDelta),
            "The support hand consumes its own live wrist relation under the shared right-aim parent");
        bool supportDeltaMatchesGun=true;
        for (int i=0;i<9;++i)
            supportDeltaMatchesGun=supportDeltaMatchesGun &&
                fabsf(supportLeftDelta.rotation[i]-
                       rerootDelta.rotation[i])<1.0e-5f;
        Check(supportDeltaMatchesGun,
            "Right wrist, held gun, and support hand receive one identical rotational parent change");
        Halo4FloatingTransform expectedSupportOrientation{};
        Check(Halo4ComposeFloatingTransforms(
                  controllerCarrier,supportLocalWrist,
                  expectedSupportOrientation),
            "The support reference composes right aim with the live Halo 4 left-wrist relation");
        bool supportRelationPreserved=true;
        for (int i=0;i<9;++i)
            supportRelationPreserved=supportRelationPreserved &&
                fabsf(supportStateReroot.rotation[i]-
                       expectedSupportOrientation.rotation[i])<1.0e-5f;
        Check(supportRelationPreserved,
            "Two-hand orientation preserves Halo 4's authored support relation instead of physical controller twist");

        // A held model has its own eye-local authored orientation, independent
        // of the Storm wrist bone axes.  The same delta must carry that exact
        // relation onto the controller frame.
        Halo4FloatingTransform heldEyeLocal{};
        const float heldRoll=-0.27f;
        heldEyeLocal.rotation[3]=0.0f;
        heldEyeLocal.rotation[4]=cosf(heldRoll);
        heldEyeLocal.rotation[5]=sinf(heldRoll);
        heldEyeLocal.rotation[6]=0.0f;
        heldEyeLocal.rotation[7]=-sinf(heldRoll);
        heldEyeLocal.rotation[8]=cosf(heldRoll);
        Halo4FloatingTransform heldFromEye{},movedFromEye{};
        Halo4FloatingTransform expectedFromController{};
        Check(Halo4ComposeFloatingTransforms(
                  eye0,heldEyeLocal,heldFromEye) &&
              Halo4ComposeFloatingTransforms(
                  rerootDelta,heldFromEye,movedFromEye) &&
              Halo4ComposeFloatingTransforms(
                  controllerCarrier,heldEyeLocal,expectedFromController),
            "The adjacent held graph composes through the production orientation reroot");
        bool heldFacesController=true;
        for (int i=0;i<9;++i)
            heldFacesController=heldFacesController &&
                fabsf(movedFromEye.rotation[i]-
                    expectedFromController.rotation[i])<1.0e-5f;
        Check(heldFacesController,
            "The Halo 4 gun keeps its authored orientation while its eye-facing orientation parent becomes the aim controller");

        const float identityBasis[9]={
            1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 0.0f,0.0f,1.0f};
        const float identityPosition[3]={0.0f,0.0f,0.0f};
        Halo4FloatingTransform identityTarget{},identityEye{},identityWrist{};
        Check(Halo4BuildFloatingControllerCarrier(
                  identityBasis,identityPosition,true,0.0f,0.0f,0.0f,
                  identityTarget),
            "The dormant C-H4-36 left-carrier branch accepts a neutral controller and zero trim");
        Halo4FloatingTransform identityReroot{};
        Check(Halo4BuildFloatingControllerRerootTarget(
                  identityTarget,identityEye,identityWrist,identityReroot),
            "A neutral left-hand relation produces a valid neutral target");
        bool neutralReroot=true;
        for (int i=0;i<9;++i)
            neutralReroot=neutralReroot &&
                fabsf(identityReroot.rotation[i]-identityTarget.rotation[i])<
                    1.0e-6f;
        Check(neutralReroot,
            "No 49-degree Blender left-hand seed survives in the final neutral orientation");

        // C-H4-37: the free palm turns over around Halo 4's live thumb ray,
        // never around a guessed controller axis. H4EK node 46 b_l_thumb1 is
        // a direct child of the wrist; its exact authored translation supplies
        // a stable non-axis-aligned thumb-outward ray that cannot wobble with
        // thumb2/thumb3 curl.
        const float thumbBaseOffset[3]={
            0.0112261f,-0.00861943f,-0.01287f};
        float thumbAxis[3]={
            thumbBaseOffset[0],thumbBaseOffset[1],thumbBaseOffset[2]};
        float thumbAxisLength=sqrtf(
            thumbAxis[0]*thumbAxis[0]+thumbAxis[1]*thumbAxis[1]+
            thumbAxis[2]*thumbAxis[2]);
        for (float& value : thumbAxis) value/=thumbAxisLength;
        Halo4FloatingTransform thumbLocal{};
        for (int axis=0;axis<3;++axis)
            thumbLocal.translation[axis]=thumbBaseOffset[axis];
        Halo4FloatingTransform stockThumbBase{};
        Check(Halo4ComposeFloatingTransforms(
                  stockEye0,thumbLocal,stockThumbBase),
            "The official direct-child thumb base can be represented in the live Storm wrist frame");
        Halo4FloatingTransform freeGripTarget{},expectedFreeGrip{};
        Check(Halo4BuildFloatingFreeLeftGripTarget(
                  selectedFreeCarrier,carrierYawDeg,carrierPitchDeg,
                  carrierRollDeg,stockEye0,stockThumbBase,
                  freeStateReroot,freeGripTarget) &&
              Halo4BuildFloatingLeftPresentationTarget(
                  false,stockEye0,stockThumbBase,parityFreeTarget,
                  expectedFreeGrip),
            "C-H4-41 composes the parity controller grip with the proven live-thumb turnover");
        Check(memcmp(&freeGripTarget,&expectedFreeGrip,
                     sizeof(freeGripTarget))==0,
            "The production free grip is exactly the C-H4-40 heading followed by the C-H4-37 back-facing turnover");
        Check(freeGripTarget.translation[0]==freeStateReroot.translation[0] &&
                  freeGripTarget.translation[1]==freeStateReroot.translation[1] &&
                  freeGripTarget.translation[2]==freeStateReroot.translation[2] &&
                  freeGripTarget.scale==freeStateReroot.scale,
            "C-H4-41 changes only free-hand orientation and preserves the accepted placement");
        Halo4FloatingTransform restoredClosestFree{};
        Check(Halo4BuildFloatingLeftPresentationTarget(
                  false,stockEye0,stockThumbBase,closestFreeReroot,
                  restoredClosestFree),
            "C-H4-42 completes the restored C-H4-37 free pose with its original live-thumb turnover");
        Check(!kEnableHalo4C39FreeAnatomy &&
                  !kEnableHalo4C40UnflippedParity &&
                  !kEnableHalo4C41BackFacingGrip,
            "The restored free target contains no C-H4-39, C-H4-40, or C-H4-41 orientation layer");
        Halo4FloatingTransform freePalmTarget{};
        Check(Halo4BuildFloatingLeftPresentationTarget(
                  false,stockEye0,stockThumbBase,freeStateReroot,
                  freePalmTarget),
            "The production free-left policy accepts a live non-axis-aligned thumb ray");
        Check(freePalmTarget.translation[0]==freeStateReroot.translation[0] &&
                  freePalmTarget.translation[1]==freeStateReroot.translation[1] &&
                  freePalmTarget.translation[2]==freeStateReroot.translation[2] &&
                  freePalmTarget.scale==freeStateReroot.scale,
            "The free-palm correction changes orientation only");
        const auto rotateDirection=[](const float basis[9],
                                      const float local[3],float world[3])
        {
            for (int row=0;row<3;++row)
            {
                world[row]=0.0f;
                for (int column=0;column<3;++column)
                    world[row]+=basis[column*3+row]*local[column];
            }
        };
        float thumbBefore[3]{},thumbAfter[3]{};
        rotateDirection(freeStateReroot.rotation,thumbAxis,thumbBefore);
        rotateDirection(freePalmTarget.rotation,thumbAxis,thumbAfter);
        Check(fabsf(thumbBefore[0]-thumbAfter[0])<1.0e-5f &&
                  fabsf(thumbBefore[1]-thumbAfter[1])<1.0e-5f &&
                  fabsf(thumbBefore[2]-thumbAfter[2])<1.0e-5f,
            "Turning the free palm preserves the stable thumb-base outward direction exactly");
        float parityThumb[3]{},gripThumb[3]{};
        rotateDirection(parityFreeTarget.rotation,thumbAxis,parityThumb);
        rotateDirection(freeGripTarget.rotation,thumbAxis,gripThumb);
        Check(fabsf(parityThumb[0]-gripThumb[0])<1.0e-5f &&
                  fabsf(parityThumb[1]-gripThumb[1])<1.0e-5f &&
                  fabsf(parityThumb[2]-gripThumb[2])<1.0e-5f,
            "The corrected grip turns the glove without swapping the reference thumb inward");

        // H4EK node 43 b_l_middle1 is another direct child of b_l_hand.
        // Its exact authored ray and the thumb-base ray define the title's
        // stable palm plane rather than an arbitrary algebra-only vector.
        const float middleBaseOffset[3]={-0.00297f,-0.03872f,-0.00605f};
        Halo4FloatingTransform middleLocal{};
        for (int axis=0;axis<3;++axis)
            middleLocal.translation[axis]=middleBaseOffset[axis];
        Halo4FloatingTransform stockMiddleBase{};
        Check(Halo4ComposeFloatingTransforms(
                  stockEye0,middleLocal,stockMiddleBase),
            "The official direct-child middle base can be represented in the live Storm wrist frame");

        // C-H4-39 replaces only the rejected free-hand heading. The live
        // direct-child middle and thumb rays form an anatomical basis whose
        // transpose maps fingers to controller-forward, thumb outward to
        // controller-left, and the back of the hand to controller-up.
        float fingerForward[3]={
            middleBaseOffset[0],middleBaseOffset[1],middleBaseOffset[2]};
        float fingerLength=sqrtf(
            fingerForward[0]*fingerForward[0]+fingerForward[1]*fingerForward[1]+
            fingerForward[2]*fingerForward[2]);
        for (float& value : fingerForward) value/=fingerLength;
        float thumbAlongFinger=0.0f;
        for (int axis=0;axis<3;++axis)
            thumbAlongFinger+=thumbAxis[axis]*fingerForward[axis];
        float thumbOutward[3]={
            thumbAxis[0]-thumbAlongFinger*fingerForward[0],
            thumbAxis[1]-thumbAlongFinger*fingerForward[1],
            thumbAxis[2]-thumbAlongFinger*fingerForward[2]};
        float outwardLength=sqrtf(
            thumbOutward[0]*thumbOutward[0]+thumbOutward[1]*thumbOutward[1]+
            thumbOutward[2]*thumbOutward[2]);
        for (float& value : thumbOutward) value/=outwardLength;
        const float anatomyUp[3]={
            fingerForward[1]*thumbOutward[2]-fingerForward[2]*thumbOutward[1],
            fingerForward[2]*thumbOutward[0]-fingerForward[0]*thumbOutward[2],
            fingerForward[0]*thumbOutward[1]-fingerForward[1]*thumbOutward[0]};
        Halo4FloatingTransform freeAnatomicalTarget{};
        Check(!kEnableHalo4C39FreeAnatomy,
            "The headset-rejected C-H4-39 finger-to-aim experiment remains disabled");
        Check(Halo4BuildFloatingFreeLeftAnatomicalTarget(
                  selectedFreeCarrier,stockEye0,stockThumbBase,stockMiddleBase,
                  freePalmTarget,freeAnatomicalTarget),
            "C-H4-39 builds the free wrist from Halo 4's live middle/thumb anatomy");
        Check(freeAnatomicalTarget.translation[0]==freePalmTarget.translation[0] &&
                  freeAnatomicalTarget.translation[1]==freePalmTarget.translation[1] &&
                  freeAnatomicalTarget.translation[2]==freePalmTarget.translation[2] &&
                  freeAnatomicalTarget.scale==freePalmTarget.scale,
            "The C-H4-39 anatomical correction changes free-hand orientation only");
        float mappedFinger[3]{},mappedOutward[3]{},mappedUp[3]{};
        rotateDirection(freeAnatomicalTarget.rotation,fingerForward,mappedFinger);
        rotateDirection(freeAnatomicalTarget.rotation,thumbOutward,mappedOutward);
        rotateDirection(freeAnatomicalTarget.rotation,anatomyUp,mappedUp);
        bool anatomyMapsToController=true;
        for (int row=0;row<3;++row)
            anatomyMapsToController=anatomyMapsToController &&
                fabsf(mappedFinger[row]-selectedFreeCarrier.rotation[row])<1.0e-5f &&
                fabsf(mappedOutward[row]-selectedFreeCarrier.rotation[3+row])<1.0e-5f &&
                fabsf(mappedUp[row]-selectedFreeCarrier.rotation[6+row])<1.0e-5f;
        Check(anatomyMapsToController,
            "Free fingers face controller-forward, thumb faces outward, and the back of the hand faces up");
        float anatomyBasis[9]{};
        Check(Halo4NormalizeFloatingBasis(
                  freeAnatomicalTarget.rotation,anatomyBasis),
            "The free anatomical target remains a finite proper basis");
        const float anatomyDeterminant=
            anatomyBasis[0]*(anatomyBasis[4]*anatomyBasis[8]-anatomyBasis[7]*anatomyBasis[5])-
            anatomyBasis[3]*(anatomyBasis[1]*anatomyBasis[8]-anatomyBasis[7]*anatomyBasis[2])+
            anatomyBasis[6]*(anatomyBasis[1]*anatomyBasis[5]-anatomyBasis[4]*anatomyBasis[2]);
        Check(fabsf(anatomyDeterminant-1.0f)<1.0e-4f,
            "The free anatomical mount is a rotation, never a reflected hand");
        float palmNormal[3]={
            thumbAxis[1]*middleBaseOffset[2]-
                thumbAxis[2]*middleBaseOffset[1],
            thumbAxis[2]*middleBaseOffset[0]-
                thumbAxis[0]*middleBaseOffset[2],
            thumbAxis[0]*middleBaseOffset[1]-
                thumbAxis[1]*middleBaseOffset[0]};
        const float palmLength=sqrtf(
            palmNormal[0]*palmNormal[0]+palmNormal[1]*palmNormal[1]+
            palmNormal[2]*palmNormal[2]);
        for (float& value : palmNormal) value/=palmLength;
        float parityPalm[3]{},gripPalm[3]{};
        rotateDirection(parityFreeTarget.rotation,palmNormal,parityPalm);
        rotateDirection(freeGripTarget.rotation,palmNormal,gripPalm);
        Check(fabsf(parityPalm[0]+gripPalm[0])<1.0e-5f &&
                  fabsf(parityPalm[1]+gripPalm[1])<1.0e-5f &&
                  fabsf(parityPalm[2]+gripPalm[2])<1.0e-5f,
            "C-H4-41 reverses C-H4-40's player-facing palm so the back of the glove faces the player");
        float anatomicalPalmNormal[3]{};
        rotateDirection(
            freeAnatomicalTarget.rotation,palmNormal,anatomicalPalmNormal);
        Check(fabsf(anatomicalPalmNormal[0]+selectedFreeCarrier.rotation[6])<1.0e-5f &&
                  fabsf(anatomicalPalmNormal[1]+selectedFreeCarrier.rotation[7])<1.0e-5f &&
                  fabsf(anatomicalPalmNormal[2]+selectedFreeCarrier.rotation[8])<1.0e-5f,
            "The palm normal faces controller-down, placing the back of the free hand upward");
        float palmBefore[3]{},palmAfter[3]{};
        rotateDirection(freeStateReroot.rotation,palmNormal,palmBefore);
        rotateDirection(freePalmTarget.rotation,palmNormal,palmAfter);
        Check(fabsf(palmBefore[0]+palmAfter[0])<1.0e-5f &&
                  fabsf(palmBefore[1]+palmAfter[1])<1.0e-5f &&
                  fabsf(palmBefore[2]+palmAfter[2])<1.0e-5f,
            "The same pi rotation reverses a palm normal perpendicular to the thumb");
        float freeBasis[9]{};
        Check(Halo4NormalizeFloatingBasis(
                  freePalmTarget.rotation,freeBasis),
            "The free-palm result remains a proper finite rotation");
        const float freeDeterminant=
            freeBasis[0]*(freeBasis[4]*freeBasis[8]-
                freeBasis[7]*freeBasis[5])-
            freeBasis[3]*(freeBasis[1]*freeBasis[8]-
                freeBasis[7]*freeBasis[2])+
            freeBasis[6]*(freeBasis[1]*freeBasis[5]-
                freeBasis[4]*freeBasis[2]);
        Check(fabsf(freeDeterminant-1.0f)<1.0e-4f,
            "The palm flip is a 180-degree rotation, never a mesh-reflecting mirror");
        Halo4FloatingTransform baseThumbDelta{},freeThumbDelta{};
        Halo4FloatingTransform baseMovedThumb{},freeMovedThumb{};
        Check(Halo4BuildFloatingWorldDelta(
                  freeStateReroot,stockEye0,baseThumbDelta) &&
              Halo4BuildFloatingWorldDelta(
                  freePalmTarget,stockEye0,freeThumbDelta) &&
              Halo4ComposeFloatingTransforms(
                  baseThumbDelta,stockThumbBase,baseMovedThumb) &&
              Halo4ComposeFloatingTransforms(
                  freeThumbDelta,stockThumbBase,freeMovedThumb),
            "Both the C-H4-38 raw free parent and its palm-flipped delta carry the official thumb-base node");
        Check(fabsf(baseMovedThumb.translation[0]-
                    freeMovedThumb.translation[0])<1.0e-5f &&
                  fabsf(baseMovedThumb.translation[1]-
                    freeMovedThumb.translation[1])<1.0e-5f &&
                  fabsf(baseMovedThumb.translation[2]-
                    freeMovedThumb.translation[2])<1.0e-5f,
            "The end-to-end free-palm subtree carry leaves the thumb-base origin exactly where C-H4-36 placed it");

        Halo4FloatingTransform anatomyDelta{},movedMiddle{};
        Check(Halo4BuildFloatingWorldDelta(
                  freeAnatomicalTarget,stockEye0,anatomyDelta) &&
              Halo4ComposeFloatingTransforms(
                  anatomyDelta,stockMiddleBase,movedMiddle),
            "The C-H4-39 production delta carries the middle-base node with the anatomical wrist");
        float carriedMiddleRay[3]={
            movedMiddle.translation[0]-freeAnatomicalTarget.translation[0],
            movedMiddle.translation[1]-freeAnatomicalTarget.translation[1],
            movedMiddle.translation[2]-freeAnatomicalTarget.translation[2]};
        const float carriedMiddleLength=sqrtf(
            carriedMiddleRay[0]*carriedMiddleRay[0]+
            carriedMiddleRay[1]*carriedMiddleRay[1]+
            carriedMiddleRay[2]*carriedMiddleRay[2]);
        for (float& value : carriedMiddleRay) value/=carriedMiddleLength;
        Check(fabsf(carriedMiddleRay[0]-selectedFreeCarrier.rotation[0])<1.0e-5f &&
                  fabsf(carriedMiddleRay[1]-selectedFreeCarrier.rotation[1])<1.0e-5f &&
                  fabsf(carriedMiddleRay[2]-selectedFreeCarrier.rotation[2])<1.0e-5f,
            "End-to-end subtree carry leaves the free fingers facing controller-forward");

        Halo4FloatingTransform untouchedAnatomy{};
        untouchedAnatomy.translation[0]=66.0f;
        Check(!Halo4BuildFloatingFreeLeftAnatomicalTarget(
                  selectedFreeCarrier,stockEye0,stockThumbBase,stockThumbBase,
                  freePalmTarget,untouchedAnatomy) &&
                  untouchedAnatomy.translation[0]==66.0f,
            "Collinear free anatomy refuses write-last and leaves C-H4-38 available as fallback");

        Halo4FloatingTransform invalidThumb=stockThumbBase;
        invalidThumb.translation[0]=
            std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedGrip{};
        untouchedGrip.translation[0]=33.0f;
        Check(!Halo4BuildFloatingFreeLeftGripTarget(
                  selectedFreeCarrier,carrierYawDeg,carrierPitchDeg,
                  carrierRollDeg,stockEye0,invalidThumb,freeStateReroot,
                  untouchedGrip) && untouchedGrip.translation[0]==33.0f,
            "Invalid optional C-H4-41 thumb input publishes nothing over the C-H4-38 fallback");
        Halo4FloatingTransform supportTarget{};
        supportTarget.translation[0]=99.0f;
        Check(Halo4BuildFloatingLeftPresentationTarget(
                  true,stockSupportLeft,invalidThumb,supportStateReroot,
                  supportTarget) &&
                  memcmp(&supportTarget,&supportStateReroot,
                         sizeof(supportTarget))==0,
            "The exact prepared-frame two-hand state preserves the C-H4-38 shared-right-aim-rotation target byte-for-byte and ignores the free-palm dependency");
        Halo4FloatingTransform untouchedInvalidAnatomy{};
        untouchedInvalidAnatomy.translation[0]=55.0f;
        Check(!Halo4BuildFloatingFreeLeftAnatomicalTarget(
                  selectedFreeCarrier,stockEye0,invalidThumb,stockMiddleBase,
                  freePalmTarget,untouchedInvalidAnatomy) &&
                  untouchedInvalidAnatomy.translation[0]==55.0f,
            "Non-finite optional anatomy cannot publish over the exact C-H4-38 fallback target");
        Halo4FloatingTransform zeroThumb=stockEye0;
        Halo4FloatingTransform untouchedPalm{};
        untouchedPalm.translation[0]=88.0f;
        Check(!Halo4BuildFloatingLeftPresentationTarget(
                  false,stockEye0,zeroThumb,freeStateReroot,untouchedPalm) &&
                  untouchedPalm.translation[0]==88.0f,
            "A zero free-thumb ray refuses without publishing a partial correction");
        Halo4FloatingTransform untouchedNonfinitePalm{};
        untouchedNonfinitePalm.translation[0]=77.0f;
        Check(!Halo4BuildFloatingLeftPresentationTarget(
                  false,stockEye0,invalidThumb,freeStateReroot,
                  untouchedNonfinitePalm) &&
                  untouchedNonfinitePalm.translation[0]==77.0f,
            "A non-finite free-thumb ray leaves the C-H4-38 raw-controller target available for feature-local fallback");

        Halo4FloatingTransform invalidEye=eye0;
        invalidEye.rotation[0]=std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedReroot{};
        untouchedReroot.translation[0]=77.0f;
        Check(!Halo4BuildFloatingControllerRerootTarget(
                  controllerCarrier,invalidEye,stockEye0,untouchedReroot) &&
              untouchedReroot.translation[0]==77.0f,
            "A non-finite current-eye relation refuses without publishing a partial target");
        Check(Halo4FloatingWristDeltaPlausible(0.7f,0.33f) &&
                  !Halo4FloatingWristDeltaPlausible(300.0f,0.33f) &&
                  !Halo4FloatingWristDeltaPlausible(
                      std::numeric_limits<float>::quiet_NaN(),0.33f),
            "The hand transaction admits physical movement but refuses the "
            "hundreds-of-units frame mix measured in the rejected line");
        Halo4FloatingTransform collapsedTransform{};
        collapsedTransform.scale=0.0001f;
        Halo4FloatingTransform composedCollapsed{};
        Check(Halo4FloatingTransformValid(collapsedTransform) &&
                  !Halo4ComposeFloatingTransforms(
                      collapsedTransform,eye0,composedCollapsed),
            "A final hidden wrist copy remains finite but can never re-enter "
            "the composable rigid-transform path");
    }

    // C-H4-9: the closed loop that keeps Halo 4's own look pitch - and so its
    // shot line - under the headset.
    {
        Halo4PitchServo servo;
        // A simulated engine: pitch integrates the stick through a mapping
        // whose SIGN the servo is not told. -1 is the inverted-look player.
        for (float mapping : {1.0f, -1.0f})
        {
            Halo4ResetPitchServo(servo);
            float enginePitch = 0.0f;
            const float target = 0.35f; // ~20 deg up
            int frames = 0;
            for (; frames < 400; ++frames)
            {
                const float stick = Halo4PitchServoStep(
                    servo, enginePitch, target, kHalo4PitchServoGain);
                // ToRawStick floors every non-zero command at 27.5%, which is
                // the quantised actuator the rest hysteresis exists for.
                float applied = 0.0f;
                if (fabsf(stick) >= 1.0e-3f)
                {
                    applied = kAimServoStickFloor +
                        fabsf(stick) * (1.0f - kAimServoStickFloor);
                    if (stick < 0.0f)
                        applied = -applied;
                }
                enginePitch += applied * mapping * 0.02f; // 0.02 rad/frame max
                if (fabsf(enginePitch - target) < 0.01f && stick == 0.0f)
                    break;
            }
            Check(frames < 400 && fabsf(enginePitch - target) < 0.01f,
                mapping > 0.0f
                    ? "The loop drives the engine's own pitch onto the head's "
                      "and parks there, so shots leave along the view"
                    : "It learns an INVERTED look mapping from what the engine "
                      "actually did, instead of encoding a belief about it");
            Check(servo.direction == mapping,
                mapping > 0.0f
                    ? "The learned direction matches a normal look mapping"
                    : "The learned direction matches an inverted look mapping");
        }

        // Parked means EXACTLY zero. A proportional command cannot express a
        // small correction through a 27.5% floor, so anything else is the
        // turret limit cycle all over again.
        Halo4ResetPitchServo(servo);
        float parkedStick = 1.0f;
        for (int i = 0; i < 8; ++i)
            parkedStick = Halo4PitchServoStep(servo, 0.2f, 0.2f, 12.0f);
        Check(parkedStick == 0.0f,
            "With the gun already on the head's pitch the loop commands "
            "exactly zero rather than chattering against the stick floor");

        // Feature-local failure: a non-finite input must command nothing.
        Halo4ResetPitchServo(servo);
        Check(Halo4PitchServoStep(
                  servo, std::numeric_limits<float>::quiet_NaN(), 0.1f,
                  12.0f) == 0.0f,
            "A non-finite engine pitch parks the stick instead of steering the "
            "camera with garbage");
    }
    Check(halo4Row && halo4Row->admissionCapabilities ==
              TitleCapability_ControllerInput,
        "Halo 4 admits shared controller input and nothing else");
    // Each exclusion below is a deliberate withholding, not an oversight, and
    // each cost a title a real defect when it was granted early.
    Check(halo4Row &&
              (halo4Row->capabilities &
               (TitleCapability_Stereo | TitleCapability_ControllerAim |
                 TitleCapability_Haptics | TitleCapability_RuntimeModes |
                TitleCapability_RoomScale | TitleCapability_ControllerInput)) ==
                  (TitleCapability_Stereo | TitleCapability_ControllerAim |
                    TitleCapability_Haptics | TitleCapability_RuntimeModes |
                   TitleCapability_RoomScale |
                   TitleCapability_ControllerInput),
        "Halo 4 advertises stereo, controller aim, haptics, runtime modes, "
        "room scale and controller input");
    Check(halo4Row && !(halo4Row->capabilities & TitleCapability_ArmIk),
        "Halo 4 withholds ArmIk: C-H4-43 has one rigid no-IK floating-hands "
        "transaction on the proven first-person return site");
    Check(halo4Row && !(halo4Row->capabilities & TitleCapability_Hud),
        "Halo 4 withholds Hud while C-H4-44's rejected basis writer is dormant");
    Check(halo4Row && !(halo4Row->capabilities & TitleCapability_CutsceneTheater),
        "Halo 4 withholds CutsceneTheater: it has no cinematic evidence yet");
    Check(TitleRegistry_AllowsSharedControllerInput(
              GameTitle::Halo4, false, false, false, true),
        "Explicit Halo 4 receives the virtual pad through its own admission");
    Check(!TitleRegistry_AllowsSharedControllerInput(
              GameTitle::Halo4, false, true, false, true),
        "Camera-only ownership cannot leak controller input into Halo 4");
    Check(!TitleRegistry_AllowsSharedGameplayFeatures(
              GameTitle::Halo4, true, false),
        "Stale Halo 3 ownership cannot admit Halo 4 gameplay features");
    Check(halo4Row && std::wstring_view(halo4Identity.moduleName) ==
              std::wstring_view(halo4Row->moduleName),
        "The Halo 4 adapter identity names the registry's own module");
    Check(std::string_view(halo4Identity.moduleSha256Steam) ==
              "7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84" &&
          std::string_view(halo4Identity.moduleSha256Store) ==
              "5767CD564C1E8E8D012D002A8DE8E92960A3DE46442399ED054E3C4EF44AA496" &&
          halo4Identity.peTimestamp == 0x68A0E7BFu &&
          halo4Identity.sizeOfImage == 0x04A3F000u &&
          kHalo4RetailFileSize == 17829336u &&
          std::string_view(halo4Identity.h4ekBuild) ==
              "2023.06.27.176405.1-Release",
        "The Halo 4 adapter pins the independently verified retail and H4EK identities");
    Halo4HookProof halo4Proof{ true, 1, true, true, true, true, true, true };
    Check(Halo4Adapter_HookProofComplete(halo4Proof),
        "A synthetic Halo 4 proof is complete only with every evidence gate present");
    halo4Proof.loadedImageMatchCount = 2;
    Check(!Halo4Adapter_HookProofComplete(halo4Proof),
        "A multiple-match loaded-image signature fails the Halo 4 proof closed");
    halo4Proof.loadedImageMatchCount = 1;
    halo4Proof.h4ekSemantics = false;
    Check(!Halo4Adapter_HookProofComplete(halo4Proof),
        "Missing H4EK semantics fail the Halo 4 proof closed");
    // Halo 4 stays here until C-H4-3 installs its camera core: the
    // controller-only stage grants admission, never a hook plan.
    const GameTitle unsupportedTitles[] = {
        GameTitle::Halo4, GameTitle::HaloCE,
        GameTitle::Halo2, GameTitle::Unknown, GameTitle::None,
    };
    for (GameTitle title : unsupportedTitles)
        Check(TitleRegistry_HookPlan(title) == TitleHookPlan::None,
            "Unsupported titles never receive game hooks");
    const GameTitle stockControllerTitles[] = {
        GameTitle::HaloCE, GameTitle::Halo2,
    };
    for (GameTitle title : stockControllerTitles)
    {
        const TitleDescriptor* descriptor = TitleRegistry_Find(title);
        const bool admitted = descriptor &&
            (descriptor->admissionCapabilities &
                TitleCapability_ControllerInput) != 0;
        Check(!admitted && !TitleRegistry_AllowsSharedControllerInput(
                  title, false, false, true, admitted),
            "CE and H2 remain stock despite private title flags");
    }
    Check(TitleRegistry_FromModuleName(L"MCC-Win64-Shipping.exe") == nullptr,
        "The MCC host is not mistaken for a game title");
    Check(std::string_view(RuntimeModeName(RuntimeMode::Vehicle)) == "vehicle",
        "Runtime modes have stable diagnostic names");

    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    const std::filesystem::path configDir = std::filesystem::path(tempPath) /
        (L"halomccvr-config-tests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(configDir);
    const std::filesystem::path primary = configDir / L"halomccvr.cfg";
    const std::filesystem::path legacy = configDir / L"halo3xr.cfg";
    {
        std::ofstream file(legacy);
        file << "screen_width_m = 6.25\n";
        file << "haptic_intensity = malformed\n";
    }
    ConfigLoadMigrating(primary.c_str(), legacy.c_str());
    Check(std::filesystem::exists(primary), "Legacy config migration creates halomccvr.cfg");
    Check(std::filesystem::exists(legacy), "Legacy config migration retains halo3xr.cfg");
    Check(g_config.screen_width_m == 6.25f, "Legacy values survive migration");
    Check(g_config.haptic_intensity == 0.86f,
        "Malformed new values retain their individual default");
    const std::string organizedConfig = ReadTextFile(primary);
    const size_t openXrSection = organizedConfig.find("#  OPENXR & COMFORT");
    const size_t controlsSection = organizedConfig.find("#  CONTROLS & TURNING");
    const size_t aimingSection = organizedConfig.find("#  RETICLE & AIMING");
    const size_t weaponSection = organizedConfig.find("#  WEAPON CALIBRATION");
    const size_t scopeSection = organizedConfig.find("#  EXPERIMENTAL SCOPE");
    const size_t displaySection = organizedConfig.find("#  HUD, PRESENTATION & PERFORMANCE");
    const size_t handsSection = organizedConfig.find("#  GAMEPLAY, HANDS & IK");
    const size_t diagnosticsSection = organizedConfig.find("#  DEVELOPMENT DIAGNOSTICS");
    Check(openXrSection < controlsSection && controlsSection < aimingSection &&
          aimingSection < weaponSection && weaponSection < scopeSection &&
          scopeSection < displaySection && displaySection < handsSection &&
          handsSection < diagnosticsSection,
        "The generated universal config has stable, readable section ordering");
    Check(organizedConfig.find("This ONE file is shared by every supported MCC game") !=
              std::string::npos,
        "The generated config explains that preferences are shared across titles");
    Check(organizedConfig.find("\nreach_") == std::string::npos &&
          organizedConfig.find("\nhalo3_") == std::string::npos &&
          organizedConfig.find("\nodst_") == std::string::npos,
        "The universal config contains no title-prefixed assignments");
    constexpr const char* universalKeys[] = {
        "config_version", "haptic_intensity", "headset_smoothing",
        "aim_stabilization", "screen_width_m", "screen_distance_m",
        "cutscene_theater_enabled", "cutscene_theater_depth",
        "cutscene_theater_flip_depth", "cutscene_theater_width_m",
        "cutscene_theater_distance_m",
        "turn_smooth", "turn_snap_deg", "turn_smooth_deg_s", "y_b_start_chord", "dpad_hand",
        "vehicle_first_person", "vehicle_cam_forward_m", "vehicle_cam_up_m",
        "vehicle_cam_right_m",
        "vehicle_view_follow", "vehicle_cam_smoothing",
        "vehicle_motion", "vehicle_wheel_max_deg",
        "vehicle_wheel_deadzone_deg",
        "crosshair", "crosshair_distance_m", "crosshair_size_deg",
        "reticle_r", "reticle_g", "reticle_b", "kill_reticle",
        "gun_scale", "left_hand_scale", "gun_pitch_deg", "gun_yaw_deg",
        "gun_roll_deg", "gun_forward_m", "muzzle_height_m",
        "scope_enabled", "scope_zoom",
        "scope_screen_width_m", "scope_screen_right_m", "scope_screen_up_m",
        "scope_screen_forward_m", "scope_refresh_divisor", "game_brightness",
        "resolution_scale", "upscale_filter", "sharpness", "aa_mode",
        "rain", "atmospheric_fog",
        "hud_size", "hud_aspect", "hud_curvature",
        "hud_vertical_offset", "motion_blur", "auto_vr", "two_handed_aim",
        "two_hand_toggle", "left_hand_forward_m", "two_hand_zone_right_m",
        "left_grip_forward_m", "arm_ik", "floating_hands",
        "right_shoulder_drop", "shoulder_level", "body_wip", "weapon_probe",
        "hud_probe", "fsr_probe", "bullet_probe", "right_eye_first"
    };
    for (const char* key : universalKeys)
    {
        const std::string assignment = std::string("\n") + key + " = ";
        const std::string message = std::string("Generated config writes exactly one '") +
            key + "' assignment";
        Check(CountText(organizedConfig, assignment) == 1, message.c_str());
    }
    {
        std::ofstream file(primary);
        file << "config_version = 1\n";
        file << "haptic_intensity = 2.0\n";
        file << "headset_smoothing = 1.0\n";
        file << "aim_stabilization = -1.0\n";
        file << "vehicle_cam_forward_m = -9.0\n";
        file << "vehicle_cam_up_m = 9.0\n";
        file << "vehicle_cam_right_m = 9.0\n";
        file << "aa_mode = 4\n";
    }
    ConfigLoad(primary.c_str());
    Check(g_config.haptic_intensity == 1.0f, "Haptic intensity is safely clamped");
    Check(g_config.headset_smoothing == 0.10f,
        "Headset smoothing is capped at the low-latency maximum");
    Check(g_config.aim_stabilization == 0.0f, "Aim stabilization is safely clamped");
    Check(g_config.vehicle_cam_forward_m == kVehicleCamForwardMin &&
              g_config.vehicle_cam_up_m == kVehicleCamUpMax &&
              g_config.vehicle_cam_right_m == kVehicleCamRightMax,
        "Vehicle forward, height and lateral trims use the expanded safe ranges");
    Check(g_config.aa_mode == 4,
        "SMAA 1x plus FXAA Strong survives config loading");
    Check(g_config.cutscene_theater_enabled &&
              g_config.cutscene_theater_depth == 1.0f &&
              !g_config.cutscene_theater_flip_depth &&
              g_config.cutscene_theater_width_m == 6.0f &&
              g_config.cutscene_theater_distance_m == 4.0f,
        "legacy configs inherit the enabled cutscene-theatre defaults");
    Check(g_config.y_b_start_chord,
        "legacy configs inherit the enabled Y+B Start chord default");

    {
        std::ofstream file(primary);
        file << "y_b_start_chord = 0\n";
    }
    ConfigLoad(primary.c_str());
    Check(!g_config.y_b_start_chord,
        "The Y+B Start chord can be disabled in the config");
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(!g_config.y_b_start_chord,
        "The Y+B Start chord setting survives a save/load round trip");

    // C13 per-SEAT trim. An override exists only for the axis and SEAT
    // actually written; every other axis, seat, vehicle and on-foot read
    // follows the universal pair live. A driver adjustment must never move
    // the passenger or the gunner of the same vehicle. Malformed values and
    // unknown names never invent an override, and the retired
    // vehicle_cam_lead knob is accepted quietly and never rewritten.
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "vehicle_cam_forward_m = 0.20\n";
        file << "vehicle_cam_up_m = -0.10\n";
        file << "vehicle_cam_right_m = 0.12\n";
        file << "vehicle_cam_forward_m_warthog_driver = 0.44\n";
        file << "vehicle_cam_up_m_warthog_gunner = 0.31\n";
        file << "vehicle_cam_right_m_warthog_passenger = -0.27\n";
        file << "vehicle_cam_up_m_hornet_passenger2 = 0.33\n";
        file << "vehicle_cam_forward_m_gondola_driver = 9.0\n";
        file << "vehicle_cam_forward_m_ghost_navigator = 9.0\n";
        file << "vehicle_cam_up_m_ghost_driver = broken\n";
        file << "vehicle_view_follow = 0.82\n";
        file << "vehicle_cam_lead = 0.50\n";
    }
    ConfigLoad(primary.c_str());
    // Warthog is id 2, Ghost 4, Hornet 8, mirroring Halo3VehicleId.
    const int hogDriver = ConfigSeatTrimSlot(2, 0, false);
    const int hogPassenger = ConfigSeatTrimSlot(2, 1, false);
    const int hogGunner = ConfigSeatTrimSlot(2, 0, true);
    const int hornetSeat2 = ConfigSeatTrimSlot(8, 2, false);
    const int ghostDriver = ConfigSeatTrimSlot(4, 0, false);
    Check(hogDriver >= 0 && hogGunner >= 0 && hogDriver != hogGunner &&
              hogPassenger != hogDriver &&
              ConfigSeatTrimSlot(0, 0, false) == -1 &&
              ConfigSeatTrimSlot(99, 0, false) == -1 &&
              ConfigSeatTrimSlot(2, 7, false) == -1 &&
              // The gunner slot is reserved: a rider seat may never land on
              // it, or a fourth passenger would share the turret's trim.
              ConfigSeatTrimSlot(2, kVehicleGunnerSlot, false) == -1 &&
              ConfigSeatTrimSlot(2, 0, true) ==
                  ConfigSeatTrimSlot(2, 99, true),
        "Every seat of a vehicle keys a distinct trim slot, an unidentified "
        "vehicle or unauthored seat keys none, and the gunner slot is "
        "reserved for mounted turrets whatever seat index they report");
    // The mongoose is the control group: no shipped seat trim touches it, so
    // it still shows the bare universal fallback. Seats the maintainer tuned
    // are asserted separately below, against the shipped table itself.
    const int mongooseDriver = ConfigSeatTrimSlot(3, 0, false);
    Check(ConfigSeatCamForward(g_config, hogDriver) == 0.44f &&
              ConfigSeatCamForward(g_config, mongooseDriver) == 0.20f &&
              ConfigSeatCamForward(g_config, hogGunner) == 0.20f &&
              ConfigSeatCamUp(g_config, hogGunner) == 0.31f &&
              ConfigSeatCamUp(g_config, hogDriver) == -0.10f &&
              ConfigSeatCamUp(g_config, hornetSeat2) == 0.33f &&
              ConfigSeatCamUp(g_config, ghostDriver) == -0.10f &&
              ConfigSeatCamForward(g_config, -1) == 0.20f &&
              // The file wins over a shipped seat default: the warthog
              // passenger's lateral trim ships at -0.10 and this config sets
              // it to -0.27.
              ConfigSeatCamRight(g_config, hogPassenger) == -0.27f &&
              ConfigSeatCamRight(g_config, hogDriver) == 0.12f &&
              ConfigSeatCamRight(g_config, -1) == 0.12f &&
              g_config.vehicle_view_follow,
        "A seat overrides exactly the axis written for it; an untuned "
        "vehicle's seats and every unset axis follow the universal trim; a "
        "config value beats the shipped seat default; a legacy non-zero "
        "follow fraction migrates to ON");

    // The maintainer's headset-tuned seats ARE the product default, so a file
    // that predates a key must still land on their placement rather than on a
    // bare universal trim. Asserted on a fresh Config so it reflects the
    // built-ins, not whatever the file above happened to write.
    {
        Config fresh;
        const int shipScorpionDriver = ConfigSeatTrimSlot(1, 0, false);
        const int shipHogPassenger = ConfigSeatTrimSlot(2, 1, false);
        const int shipProwlerGunner = ConfigSeatTrimSlot(6, 0, true);
        const int shipChopperDriver = ConfigSeatTrimSlot(9, 0, false);
        const int untunedMongoose = ConfigSeatTrimSlot(3, 0, false);
        const bool applied =
            ConfigSeatCamForward(fresh, shipScorpionDriver) == 0.29f &&
            ConfigSeatCamUp(fresh, shipScorpionDriver) == 1.50f &&
            ConfigSeatCamRight(fresh, shipScorpionDriver) == -0.03f &&
            ConfigSeatCamForward(fresh, shipHogPassenger) == -0.28f &&
            ConfigSeatCamUp(fresh, shipProwlerGunner) == 0.30f &&
            ConfigSeatCamForward(fresh, shipChopperDriver) == 0.12f;
        // Only the axes actually moved are set. The chopper driver's lateral
        // and the prowler gunner's forward were never touched, so they must
        // still follow the universal trim and move with it.
        const bool untouchedAxesFollow =
            !fresh.vehicle_cam_right_set[shipChopperDriver] &&
            !fresh.vehicle_cam_forward_set[shipProwlerGunner] &&
            ConfigSeatCamRight(fresh, shipChopperDriver) ==
                fresh.vehicle_cam_right_m &&
            ConfigSeatCamForward(fresh, shipProwlerGunner) ==
                fresh.vehicle_cam_forward_m;
        // A vehicle nobody tuned keeps a completely bare slot.
        const bool untunedStaysBare =
            !fresh.vehicle_cam_forward_set[untunedMongoose] &&
            !fresh.vehicle_cam_up_set[untunedMongoose] &&
            !fresh.vehicle_cam_right_set[untunedMongoose];
        // The follow is off out of the box; the vehicles ship world-locked.
        const bool followShipsOff = !fresh.vehicle_view_follow;
        Check(applied && untouchedAxesFollow && untunedStaysBare &&
                  followShipsOff,
            "The maintainer's tuned seat placements are the built-in "
            "defaults, only on the axes they moved, and the view follow "
            "ships off");
    }
    ConfigSave();
    const std::string perSeatConfig = ReadTextFile(primary);
    Check(CountText(perSeatConfig, "\nvehicle_cam_forward_m_warthog_driver = ") == 1 &&
              CountText(perSeatConfig, "\nvehicle_cam_up_m_warthog_gunner = ") == 1 &&
              CountText(perSeatConfig, "\nvehicle_cam_up_m_hornet_passenger2 = ") == 1 &&
              CountText(perSeatConfig, "\nvehicle_cam_right_m_warthog_passenger = ") == 1 &&
              CountText(perSeatConfig, "vehicle_cam_up_m_warthog_driver") == 0 &&
              CountText(perSeatConfig, "vehicle_cam_right_m_warthog_driver") == 0 &&
              CountText(perSeatConfig, "vehicle_cam_forward_m_warthog_gunner") == 0 &&
              CountText(perSeatConfig, "\nvehicle_cam_up_m_ghost_driver") == 0 &&
              CountText(perSeatConfig, "gondola") == 0 &&
              CountText(perSeatConfig, "navigator") == 0 &&
              CountText(perSeatConfig, "vehicle_cam_lead") == 0 &&
              CountText(perSeatConfig, "\nvehicle_view_follow = 1") == 1,
        "Saving writes a per-seat line only for overrides that exist, drops "
        "unknown vehicle and seat names and malformed values, and retires "
        "vehicle_cam_lead");
    ConfigLoad(primary.c_str());
    Check(ConfigSeatCamForward(g_config, hogDriver) == 0.44f &&
              ConfigSeatCamUp(g_config, hogGunner) == 0.31f &&
              ConfigSeatCamRight(g_config, hogPassenger) == -0.27f &&
              ConfigSeatCamUp(g_config, hornetSeat2) == 0.33f &&
              !g_config.vehicle_cam_up_set[hogDriver] &&
              !g_config.vehicle_cam_forward_set[hogGunner] &&
              !g_config.vehicle_cam_right_set[hogDriver] &&
              g_config.vehicle_view_follow,
        "Per-seat trim overrides survive a save/load round trip");

    // Reach has its own role-neutral bank. These endpoints pin the runtime
    // ReachVehicleId order and prove all sixteen stable seat indices are
    // collision-free without claiming title-specific driver/gunner roles.
    const int reachBanshee0 = ConfigReachSeatTrimSlot(1, 0);
    const int reachFalcon10 = ConfigReachSeatTrimSlot(12, 10);
    const int reachMachinegun15 = ConfigReachSeatTrimSlot(20, 15);
    const int reachDropPod15 = ConfigReachSeatTrimSlot(34, 15);
    const int reachUnmatched0 =
        ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 0);
    const int reachUnmatched15 =
        ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 15);
    Check(reachBanshee0 == 0 &&
              reachDropPod15 == 34 * kReachVehicleSeatSlots - 1 &&
              reachUnmatched0 == 34 * kReachVehicleSeatSlots &&
              reachUnmatched15 == kReachVehicleTrimSlots - 1 &&
              reachMachinegun15 == 20 * kReachVehicleSeatSlots - 1 &&
              reachFalcon10 != reachBanshee0 &&
              !strcmp(kReachVehicleTrimNames[0], "banshee") &&
              !strcmp(kReachVehicleTrimNames[19], "machinegun") &&
              !strcmp(kReachVehicleTrimNames[20], "mac_cannon") &&
              !strcmp(kReachVehicleTrimNames[33], "squad_drop_pod") &&
              !strcmp(kReachVehicleTrimNames[34], "unmatched") &&
              ConfigReachSeatTrimSlot(0, 0) == -1 &&
              ConfigReachSeatTrimSlot(36, 0) == -1 &&
              ConfigReachSeatTrimSlot(12, -1) == -1 &&
              ConfigReachSeatTrimSlot(12, 16) == -1,
        "Reach vehicle names and raw seat indices map to a bounded independent bank");
    // R-V25: an unmatched vehicle's seat must key its own row, so a seated F1
    // adjustment can never reach the universal trim shared by all three
    // titles. This is the exact defect the 2026-08-06 session hit.
    {
        const uint64_t unmatchedSnapshot = ReachVehicleTrimSnapshot(
            7u, ReachVehicleId::Unknown, 3);
        const uint64_t knownSnapshot = ReachVehicleTrimSnapshot(
            7u, ReachVehicleId::Warthog, 1);
        Check(ReachVehicleTrimSnapshotSlot(
                  unmatchedSnapshot, 7u, kReachVehicleSeatSlots) ==
                  ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 3) &&
              ReachVehicleTrimSnapshotSlot(
                  knownSnapshot, 7u, kReachVehicleSeatSlots) ==
                  ConfigReachSeatTrimSlot(
                      static_cast<int>(ReachVehicleId::Warthog), 1) &&
              ReachVehicleTrimSnapshotSlot(
                  unmatchedSnapshot, 8u, kReachVehicleSeatSlots) == -1,
            "An unmatched Reach seat keys its own trim row, never the universal trim");
    }
    // The retail maps report the official type-turret physics block for HREK's
    // type-16 mounted weapons. Measured 2026-08-06: the rocket Warthog turret
    // arrived as the canonical HREK tuple with observed type 6 and was
    // rejected, which is what dropped it onto the universal trim.
    {
        const ReachVehicleFingerprint rocketHog{
            0x3F12852A, 0x3DC5E04F, 0x3BE1BCFB, 0x3EA9708D};
        const ReachVehicleFingerprint warthog{
            0x3F978072, 0xBD406A82, 0xBAD03632, 0x3EC25783};
        Check(ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::WarthogRocket, rocketHog, 6) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::WarthogRocket, rocketHog, 16) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::WarthogRocket, rocketHog, 8) &&
              ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Warthog, warthog, 1) &&
              !ReachVehiclePhysicsTypeMatches(
                  ReachVehicleId::Warthog, warthog, 6),
            "HREK type-16 mounted weapons also resolve at retail's turret physics type");
    }
    // The completed Blender scene is the standalone product default, not a
    // Steam-only side effect of whichever config happened to be edited.
    {
        const Config fresh;
        bool allReachShippedRowsApplied =
            kConfigReachShippedSeatTrimCount == 25;
        for (const ConfigReachShippedSeatTrim& trim :
             kConfigReachShippedSeatTrims)
        {
            const int slot =
                ConfigReachSeatTrimSlot(trim.vehicleId, trim.seatIndex);
            allReachShippedRowsApplied =
                allReachShippedRowsApplied && slot >= 0 &&
                fresh.reach_vehicle_cam_forward_set[slot] &&
                fresh.reach_vehicle_cam_up_set[slot] &&
                fresh.reach_vehicle_cam_right_set[slot] &&
                ConfigReachSeatCamForward(fresh, slot) == trim.forward &&
                ConfigReachSeatCamUp(fresh, slot) == trim.up &&
                ConfigReachSeatCamRight(fresh, slot) == trim.right;
        }
        const int scorpion0 = ConfigReachSeatTrimSlot(14, 0);
        const int shadePlasma0 = ConfigReachSeatTrimSlot(17, 0);
        const int shadeFlak0 = ConfigReachSeatTrimSlot(18, 0);
        const int plasmaTurret0 = ConfigReachSeatTrimSlot(19, 0);
        const int sabre0 = ConfigReachSeatTrimSlot(13, 0);
        Check(allReachShippedRowsApplied &&
                  ConfigReachSeatCamUp(fresh, scorpion0) == 2.69f &&
                  ConfigReachSeatCamUp(fresh, shadePlasma0) == 2.41f &&
                  ConfigReachSeatCamForward(fresh, shadeFlak0) == 0.11f &&
                  ConfigReachSeatCamUp(fresh, shadeFlak0) == 2.27f &&
                  ConfigReachSeatCamUp(fresh, plasmaTurret0) == 0.24f &&
                  ConfigReachSeatCamForward(fresh, sabre0) == 42.43f &&
                  ConfigReachSeatCamUp(fresh, sabre0) == -8.18f,
            "All 25 user-authored Reach Blender placements ship as built-in defaults");

        {
            std::ofstream file(primary);
            file << "config_version = 5\n";
        }
        ConfigLoad(primary.c_str());
        ConfigSave();
        const std::string shippedReachConfig = ReadTextFile(primary);
        Check(CountText(shippedReachConfig,
                  "\nvehicle_cam_up_m_reach_scorpion_seat0 = 2.69") == 1 &&
                  CountText(shippedReachConfig,
                  "\nvehicle_cam_up_m_reach_shade_plasma_seat0 = 2.41") == 1 &&
                  CountText(shippedReachConfig,
                  "\nvehicle_cam_forward_m_reach_shade_flak_seat0 = 0.11") == 1 &&
                  CountText(shippedReachConfig,
                  "\nvehicle_cam_up_m_reach_plasma_turret_seat0 = 0.24") == 1 &&
                  CountText(shippedReachConfig,
                  "\nvehicle_cam_forward_m_reach_sabre_seat0 = 42.43") == 1,
            "Saving a bare config materializes the Reach Blender camera defaults");
        ConfigLoad(primary.c_str());
        Check(ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamUp(g_config, shadePlasma0) == 2.41f &&
                  ConfigReachSeatCamForward(g_config, shadeFlak0) == 0.11f &&
                  ConfigReachSeatCamUp(g_config, shadeFlak0) == 2.27f &&
                  ConfigReachSeatCamUp(g_config, plasmaTurret0) == 0.24f &&
                  ConfigReachSeatCamForward(g_config, sabre0) == 42.43f &&
                  ConfigReachSeatCamUp(g_config, sabre0) == -8.18f,
            "Reach shipped camera defaults survive a save/load round trip");

        // F1's seat reset is not representable by deleting numeric keys now
        // that absence selects the shipped Blender row. The explicit
        // tombstone must survive save/load and suppress all three numeric axes.
        // R-V25: the tombstone returns the seat to its AUTHORED row, not to
        // the shared universal trim. The 2026-08-06 session proved why - a
        // polluted universal reached the Warthog driver through its tombstone
        // and moved the seat by nearly a metre. Only a seat with no authored
        // row (the unmatched row) still follows the universal.
        g_config.vehicle_cam_forward_m = 0.34f;
        g_config.vehicle_cam_up_m = -0.27f;
        g_config.vehicle_cam_right_m = 0.19f;
        ConfigReachSeatUseUniversalTrim(g_config, scorpion0);
        const int reachUnmatchedSeat0 =
            ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 0);
        Check(g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  !g_config.reach_vehicle_cam_forward_set[scorpion0] &&
                  !g_config.reach_vehicle_cam_up_set[scorpion0] &&
                  !g_config.reach_vehicle_cam_right_set[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.00f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f &&
                  ConfigReachSeatCamForward(
                      g_config, reachUnmatchedSeat0) == 0.34f &&
                  ConfigReachSeatCamUp(
                      g_config, reachUnmatchedSeat0) == -0.27f &&
                  ConfigReachSeatCamRight(
                      g_config, reachUnmatchedSeat0) == 0.19f,
            "Reach Back to authored point restores the Blender row, never the universal trim");
        ConfigSave();
        const std::string universalReachConfig = ReadTextFile(primary);
        Check(CountText(universalReachConfig,
                  "\nvehicle_cam_use_universal_reach_scorpion_seat0 = 1") == 1 &&
                  CountText(universalReachConfig,
                  "\nvehicle_cam_forward_m_reach_scorpion_seat0") == 0 &&
                  CountText(universalReachConfig,
                  "\nvehicle_cam_up_m_reach_scorpion_seat0") == 0 &&
                  CountText(universalReachConfig,
                  "\nvehicle_cam_right_m_reach_scorpion_seat0") == 0,
            "Saving a Reach universal reset writes one tombstone and no numeric seat rows");
        ConfigLoad(primary.c_str());
        Check(g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  !g_config.reach_vehicle_cam_forward_set[scorpion0] &&
                  !g_config.reach_vehicle_cam_up_set[scorpion0] &&
                  !g_config.reach_vehicle_cam_right_set[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.00f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f,
            "Reach Back to authored point survives a config reload");

        // This is the pure state transition used immediately before any of the
        // three F1 sliders writes its changed axis. It must capture exactly
        // what the sliders were displaying - the authored row.
        ConfigReachSeatBeginTrimEdit(g_config, scorpion0);
        g_config.reach_vehicle_cam_forward_v[scorpion0] = 0.88f;
        Check(!g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  g_config.reach_vehicle_cam_forward_set[scorpion0] &&
                  g_config.reach_vehicle_cam_up_set[scorpion0] &&
                  g_config.reach_vehicle_cam_right_set[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.88f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f,
            "The first F1 move after reset preserves the other displayed authored axes");
        ConfigSave();
        ConfigLoad(primary.c_str());
        Check(!g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.88f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f,
            "The first post-reset Reach slider triplet survives save/load");

        // A hand-edited or stale file can contain both shapes. Any valid
        // numeric row is the explicit override regardless of line ordering.
        {
            std::ofstream file(primary);
            file << "config_version = 5\n";
            file << "vehicle_cam_forward_m_reach_scorpion_seat0 = 0.44\n";
            file << "vehicle_cam_use_universal_reach_scorpion_seat0 = 1\n";
        }
        ConfigLoad(primary.c_str());
        Check(!g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.44f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f,
            "A valid Reach numeric seat key overrides a tombstone independent of file order");
    }
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "vehicle_cam_forward_m = 0.20\n";
        file << "vehicle_cam_forward_m_reach_falcon_seat10 = 0.42\n";
        file << "vehicle_cam_up_m_reach_space_banshee_seat0 = -0.23\n";
        file << "vehicle_cam_right_m_reach_machinegun_seat15 = 0.31\n";
        file << "vehicle_cam_up_m_reach_wraith_gunner_seat0 = 0.17\n";
        file << "vehicle_cam_right_m_reach_warthog_chaingun_seat0 = -0.19\n";
        file << "vehicle_cam_forward_m_reach_cart_seat15 = 99.0\n";
        file << "vehicle_cam_up_m_reach_cart_seat15 = -99.0\n";
        file << "vehicle_cam_right_m_reach_cart_seat15 = 99.0\n";
        file << "vehicle_cam_forward_m_reach_falcon_seat16 = 0.99\n";
        file << "vehicle_cam_forward_m_reach_prowler_seat0 = 0.99\n";
        file << "vehicle_cam_up_m_reach_ghost_seat0 = broken\n";
    }
    ConfigLoad(primary.c_str());
    const int reachSpaceBanshee0 = ConfigReachSeatTrimSlot(2, 0);
    const int reachGhost0 = ConfigReachSeatTrimSlot(3, 0);
    const int reachWraithGunner0 = ConfigReachSeatTrimSlot(6, 0);
    const int reachHogChaingun0 = ConfigReachSeatTrimSlot(9, 0);
    const int reachMacCannon0 = ConfigReachSeatTrimSlot(21, 0);
    const int reachCart15 = ConfigReachSeatTrimSlot(16, 15);
    Check(ConfigReachSeatCamForward(g_config, reachFalcon10) == 0.42f &&
              ConfigReachSeatCamUp(g_config, reachSpaceBanshee0) == -0.23f &&
              ConfigReachSeatCamRight(g_config, reachMachinegun15) == 0.31f &&
              ConfigReachSeatCamUp(g_config, reachWraithGunner0) == 0.17f &&
              ConfigReachSeatCamRight(g_config, reachHogChaingun0) == -0.19f &&
              ConfigReachSeatCamForward(g_config, reachCart15) ==
                  kReachVehicleCamForwardMax &&
              ConfigReachSeatCamUp(g_config, reachCart15) ==
                  kReachVehicleCamUpMin &&
              ConfigReachSeatCamRight(g_config, reachCart15) ==
                  kReachVehicleCamRightMax &&
              ConfigReachSeatCamForward(g_config, reachMacCannon0) == 0.20f &&
              g_config.reach_vehicle_cam_up_set[reachGhost0] &&
              ConfigReachSeatCamUp(g_config, reachGhost0) == 1.13f &&
              ConfigSeatCamForward(g_config, hogDriver) != 0.42f &&
              ConfigOdstSeatCamForward(g_config,
                  ConfigOdstSeatTrimSlot(2, 0, false)) != 0.42f,
        "Reach trim keys load per axis and never modify Halo 3 or ODST storage");
    ConfigSave();
    const std::string reachSeatConfig = ReadTextFile(primary);
    Check(CountText(reachSeatConfig,
              "\nvehicle_cam_forward_m_reach_falcon_seat10 = 0.42") == 1 &&
              CountText(reachSeatConfig,
              "\nvehicle_cam_up_m_reach_space_banshee_seat0 = -0.23") == 1 &&
              CountText(reachSeatConfig,
              "\nvehicle_cam_right_m_reach_machinegun_seat15 = 0.31") == 1 &&
              CountText(reachSeatConfig,
              "\nvehicle_cam_up_m_reach_wraith_gunner_seat0 = 0.17") == 1 &&
              CountText(reachSeatConfig,
              "\nvehicle_cam_right_m_reach_warthog_chaingun_seat0 = -0.19") == 1 &&
              CountText(reachSeatConfig, "reach_falcon_seat16") == 0 &&
              CountText(reachSeatConfig, "reach_prowler") == 0,
        "Saving writes only valid Reach seat overrides with stable Reach prefixes");
    ConfigLoad(primary.c_str());
    Check(ConfigReachSeatCamForward(g_config, reachFalcon10) == 0.42f &&
              ConfigReachSeatCamUp(g_config, reachSpaceBanshee0) == -0.23f &&
              ConfigReachSeatCamRight(g_config, reachMachinegun15) == 0.31f &&
              ConfigReachSeatCamUp(g_config, reachWraithGunner0) == 0.17f &&
              ConfigReachSeatCamRight(g_config, reachHogChaingun0) == -0.19f,
        "Reach per-seat trims survive a save/load round trip");
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "vehicle_view_follow = 0\n";
    }
    ConfigLoad(primary.c_str());
    Check(!g_config.vehicle_view_follow,
        "The vehicle-follow toggle can still select the world-locked view");
    // A C12-era whole-vehicle key means every seat of that vehicle. These are
    // the user's OWN tuned lines, copied from their live halomccvr.cfg on
    // 2026-07-31 — six vehicles they had already trimmed by hand before the
    // per-seat rework. Losing any of them would be a real regression, so the
    // exact file is pinned here.
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "vehicle_cam_forward_m = -0.03\n";
        file << "vehicle_cam_up_m = 0.10\n";
        file << "vehicle_cam_forward_m_scorpion = 0.21\n";
        file << "vehicle_cam_up_m_scorpion = 1.00\n";
        file << "vehicle_cam_forward_m_warthog = 0.02\n";
        file << "vehicle_cam_up_m_warthog = 0.51\n";
        file << "vehicle_cam_forward_m_mongoose = 0.38\n";
        file << "vehicle_cam_up_m_mongoose = 0.04\n";
        file << "vehicle_cam_forward_m_prowler = 0.18\n";
        file << "vehicle_cam_up_m_prowler = -0.02\n";
        file << "vehicle_cam_up_m_banshee = -0.06\n";
        file << "vehicle_cam_forward_m_hornet = 0.06\n";
        file << "vehicle_cam_up_m_hornet = -0.07\n";
    }
    ConfigLoad(primary.c_str());
    bool migrated = true;
    // scorpion=1, warthog=2, mongoose=3, prowler=6, banshee=7, hornet=8.
    const struct { int id; float fwd; float up; bool fwdSet; } kTuned[] = {
        {1, 0.21f, 1.00f, true},  {2, 0.02f, 0.51f, true},
        {3, 0.38f, 0.04f, true},  {6, 0.18f, -0.02f, true},
        {7, -0.03f, -0.06f, false}, {8, 0.06f, -0.07f, true},
    };
    for (const auto& t : kTuned)
        for (int s = 0; s < kVehicleSeatSlots; ++s)
        {
            const int slot = ConfigSeatTrimSlot(
                t.id, s == kVehicleGunnerSlot ? 0 : s,
                s == kVehicleGunnerSlot);
            if (ConfigSeatCamForward(g_config, slot) != t.fwd ||
                ConfigSeatCamUp(g_config, slot) != t.up)
                migrated = false;
            // The banshee line set only the height; its forward must still
            // follow the universal trim rather than being invented.
            if (!t.fwdSet && g_config.vehicle_cam_forward_set[slot])
                migrated = false;
        }
    // A vehicle the user never touched still follows the universal pair.
    const int ghostSlot = ConfigSeatTrimSlot(4, 0, false);
    Check(migrated && ConfigSeatCamForward(g_config, ghostSlot) == -0.03f &&
              ConfigSeatCamUp(g_config, ghostSlot) == 0.10f,
        "Every per-vehicle trim the user had already tuned migrates onto all "
        "four seats of that vehicle, sets no axis they never set, and leaves "
        "untouched vehicles on the universal trim");

    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "cutscene_theater_enabled = 0\n";
        file << "cutscene_theater_depth = 9\n";
        file << "cutscene_theater_flip_depth = 1\n";
        file << "cutscene_theater_width_m = 0.1\n";
        file << "cutscene_theater_distance_m = 99\n";
    }
    ConfigLoad(primary.c_str());
    Check(!g_config.cutscene_theater_enabled &&
              g_config.cutscene_theater_depth == 2.0f &&
              g_config.cutscene_theater_flip_depth &&
              g_config.cutscene_theater_width_m == 0.5f &&
              g_config.cutscene_theater_distance_m == 20.0f,
        "cutscene-theatre settings load and clamp independently");
    g_config.cutscene_theater_enabled = true;
    g_config.cutscene_theater_depth = 1.25f;
    g_config.cutscene_theater_flip_depth = false;
    g_config.cutscene_theater_width_m = 6.5f;
    g_config.cutscene_theater_distance_m = 4.5f;
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.cutscene_theater_enabled &&
              g_config.cutscene_theater_depth == 1.25f &&
              !g_config.cutscene_theater_flip_depth &&
              g_config.cutscene_theater_width_m == 6.5f &&
              g_config.cutscene_theater_distance_m == 4.5f,
        "cutscene-theatre settings survive a save/reload round trip");

    // resolution_scale is free-form: a hand-typed value must survive exactly,
    // not snap to one of the six installer tiers (the pre-2026-07-20 behavior).
    {
        std::ofstream file(primary);
        file << "resolution_scale = 0.90\n";
    }
    ConfigLoad(primary.c_str());
    Check(g_config.resolution_scale == 0.90f,
        "A custom resolution scale is honored exactly, not snapped to a preset");
    ConfigSave();
    const std::string savedResolutionConfig = ReadTextFile(primary);
    Check(CountText(savedResolutionConfig, "resolution_scale = 0.90") == 1,
        "Organized config keeps the launcher's resolution_scale line compatible");
    ConfigLoad(primary.c_str());
    Check(g_config.resolution_scale == 0.90f,
        "A custom resolution scale survives a save/reload round trip");
    {
        std::ofstream file(primary);
        file << "resolution_scale = 0.05\n";
    }
    ConfigLoad(primary.c_str());
    Check(g_config.resolution_scale == kResolutionScaleMin,
        "A too-small resolution scale is pulled up to the minimum");
    {
        std::ofstream file(primary);
        file << "resolution_scale = 5.0\n";
    }
    ConfigLoad(primary.c_str());
    Check(!UpdateTwoHandHold(false, true, false) &&
          UpdateTwoHandHold(false, true, true) &&
          UpdateTwoHandHold(true, true, false) &&
          !UpdateTwoHandHold(true, false, true), __func__);

    Check(g_config.resolution_scale == kResolutionScaleMax,
        "A too-large resolution scale is pulled down to the maximum");

    {
        std::ofstream file(primary);
        file << "hud_height = 1.25\n"; // compatibility alias from the first test build
        file << "hud_aspect = 1.35\n";
        file << "hud_vertical_offset = -125\n";
    }
    ConfigLoad(primary.c_str());
    const float migratedCurvature = (0.30f - 1.25f * 0.1f) / 0.60f;
    Check(std::abs(g_config.hud_curvature - migratedCurvature) < 0.0001f &&
          g_config.hud_aspect == 1.35f &&
          g_config.hud_vertical_offset == -125.0f,
        "Legacy HUD curvature, aspect trim, and height migrate exactly");
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(std::abs(g_config.hud_curvature - migratedCurvature) < 0.01f &&
          g_config.hud_aspect == 1.35f &&
          g_config.hud_vertical_offset == -125.0f,
        "Normalized HUD curvature, aspect, and height survive save/reload");
    {
        std::ofstream file(primary);
        file << "config_version = 2\n";
        file << "hud_curvature = 99\n";
        file << "hud_aspect = 99\n";
        file << "hud_vertical_offset = -999\n";
    }
    ConfigLoad(primary.c_str());
    Check(g_config.hud_curvature == kHudCurvatureMax &&
          g_config.hud_aspect == kHudAspectMax &&
          g_config.hud_vertical_offset == kHudHeightMin,
        "Excessive HUD curvature, aspect, and height values are safely clamped");

    {
        std::ofstream file(primary);
        file << "config_version = 2\n";
        file << "scope_zoom = 3.39\n";
    }
    ConfigLoad(primary.c_str());
    Check(std::fabs(g_config.scope_zoom - 11.865f) < 1e-4f,
        "Version 2 scope zoom migrates into the tighter gameplay-origin lens");
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(std::fabs(g_config.scope_zoom - 11.87f) < 1e-4f,
        "Migrated scope zoom is not strengthened again after saving version 5");

    {
        std::ofstream file(primary);
        file << "scope_enabled = 1\n";
        file << "scope_zoom = 99\n";
        file << "scope_screen_width_m = 1\n";
        file << "scope_screen_right_m = -1\n";
        file << "scope_screen_up_m = 1\n";
        file << "scope_screen_forward_m = 0\n";
        file << "scope_refresh_divisor = 99\n";
    }
    ConfigLoad(primary.c_str());
    Check(g_config.scope_enabled && g_config.scope_zoom == 24.0f &&
          g_config.scope_screen_width_m == 0.25f &&
          g_config.scope_screen_right_m == -0.30f &&
          g_config.scope_screen_up_m == 0.30f &&
          g_config.scope_screen_forward_m == 0.05f &&
          g_config.scope_refresh_divisor == 4,
        "Universal scope settings are safely clamped");
    g_config.scope_zoom = 8.25f;
    g_config.scope_screen_width_m = 0.12f;
    g_config.scope_screen_right_m = 0.03f;
    g_config.scope_screen_up_m = 0.09f;
    g_config.scope_screen_forward_m = 0.35f;
    g_config.scope_refresh_divisor = 3;
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.scope_zoom == 8.25f &&
          g_config.scope_screen_width_m == 0.12f &&
          g_config.scope_screen_right_m == 0.03f &&
          g_config.scope_screen_up_m == 0.09f &&
          g_config.scope_screen_forward_m == 0.35f &&
          g_config.scope_refresh_divisor == 3,
        "Universal scope settings survive a save/reload round trip");

    // Deleting the file is the documented "put everything back" escape hatch.
    {
        std::ofstream file(primary);
        file << "gun_scale = 2.5\n";
    }
    ConfigLoad(primary.c_str());
    Check(g_config.gun_scale == 2.5f, "A tuned value loads before the reset test");
    std::filesystem::remove(primary);
    ConfigLoad(primary.c_str());
    Check(std::filesystem::exists(primary),
        "Deleting the config file recreates it on the next load");
    const Config defaults{};
    Check(g_config.gun_scale == defaults.gun_scale &&
          g_config.resolution_scale == defaults.resolution_scale &&
          g_config.hud_size == defaults.hud_size &&
          g_config.hud_aspect == defaults.hud_aspect &&
          g_config.hud_curvature == defaults.hud_curvature &&
          g_config.hud_vertical_offset == defaults.hud_vertical_offset &&
          g_config.scope_enabled &&
          g_config.scope_zoom == defaults.scope_zoom &&
          g_config.scope_screen_width_m == defaults.scope_screen_width_m &&
          g_config.scope_screen_right_m == defaults.scope_screen_right_m &&
          g_config.scope_screen_up_m == defaults.scope_screen_up_m &&
          g_config.scope_screen_forward_m == defaults.scope_screen_forward_m &&
          g_config.scope_refresh_divisor == defaults.scope_refresh_divisor &&
          g_config.cutscene_theater_enabled == defaults.cutscene_theater_enabled &&
          g_config.cutscene_theater_depth == defaults.cutscene_theater_depth &&
          g_config.cutscene_theater_flip_depth == defaults.cutscene_theater_flip_depth &&
          g_config.cutscene_theater_width_m == defaults.cutscene_theater_width_m &&
          g_config.cutscene_theater_distance_m == defaults.cutscene_theater_distance_m,
        "The recreated config file carries the struct defaults");
    std::filesystem::remove_all(configDir);

    MenuChordDetector chord;
    MenuChordResult chordResult = chord.Update(1000, true, false);
    Check(!chordResult.toggled, "One stick click does not toggle the menu");
    chordResult = chord.Update(1249, true, true);
    Check(chordResult.toggled && chordResult.consumeClicks,
        "L3+R3 emits one shared menu-toggle/recenter edge and consumes both clicks");
    Check(!chord.Update(1300, true, true).toggled,
        "A held chord cannot repeat the shared menu-toggle/recenter action");
    Check(chord.Update(1350, false, true).consumeClicks,
        "Chord clicks stay consumed until both are released");
    chord.Update(1400, false, false);
    chord.Update(2000, true, false);
    Check(!chord.Update(2251, true, true).toggled,
        "A chord outside the 250 ms window does not toggle");
    chord.Update(2300, false, false);
    Check(chord.Update(2400, true, true).toggled,
        "A simultaneous chord works after release");

    ScopeToggleDetector scopeToggle;
    Check(!scopeToggle.Update(true, true, false).changed,
        "R3 press arms the scope without toggling early");
    ScopeToggleUpdate scopeResult = scopeToggle.Update(true, false, false);
    Check(scopeResult.changed && scopeResult.active,
        "R3 release opens the universal scope");
    scopeToggle.Update(true, true, false);
    scopeResult = scopeToggle.Update(true, false, false);
    Check(scopeResult.changed && !scopeResult.active,
        "A second R3 click closes the universal scope");

    scopeToggle.Reset();
    scopeToggle.Update(true, true, false);      // R3 begins first
    scopeToggle.Update(true, true, true);       // L3 joins; menu consumes chord
    scopeResult = scopeToggle.Update(true, false, true);
    Check(!scopeResult.changed && !scopeResult.active,
        "A staggered L3+R3 menu chord cancels the pending scope toggle");
    scopeToggle.Reset();
    scopeToggle.Update(true, true, true);       // simultaneous menu chord
    scopeResult = scopeToggle.Update(true, false, true);
    Check(!scopeResult.changed && !scopeResult.active,
        "A simultaneous L3+R3 menu chord never toggles the scope");

    scopeToggle.Update(true, true, false);
    scopeToggle.Update(true, false, false);
    scopeResult = scopeToggle.Update(false, false, false);
    Check(scopeResult.changed && !scopeResult.active,
        "Losing gameplay or disabling the feature resets an active scope");
    scopeToggle.Update(false, true, false);
    scopeToggle.Update(true, true, false);
    scopeResult = scopeToggle.Update(true, false, false);
    Check(!scopeResult.changed && !scopeResult.active,
        "R3 held across gameplay entry cannot cause a surprise toggle");

    const float identity[4] = {0, 0, 0, 1};
    const float scopeOrigin[3] = {1, 2, 3};
    const ScopeQuadTransform quad = ComputeScopeQuadTransform(
        identity, scopeOrigin, 0.04f, 0.08f, 0.30f, 0.10f);
    Check(std::fabs(quad.position[0] - 1.04f) < 1e-5f &&
          std::fabs(quad.position[1] - 2.08f) < 1e-5f &&
          std::fabs(quad.position[2] - 2.70f) < 1e-5f,
        "Scope offsets follow the gun's local right/up/forward axes");
    Check(std::fabs(quad.width - 0.10f) < 1e-5f &&
          std::fabs(quad.height - 0.075f) < 1e-5f,
        "Scope is fixed-size 4:3 geometry independent of headset distance");

    const float gameBasis[9] = {
        1, 0, 0,  // forward
        0, 1, 0,  // left
        0, 0, 1}; // up
    const float safeCameraOrigin[3] = {4.0f, 5.0f, 6.0f};
    const float bulletForward[3] = {1.0f, 0.02f, -0.01f};
    ScopeCameraPose scopeCamera{};
    Check(ComputeScopeCameraPose(gameBasis, safeCameraOrigin,
                                 bulletForward, scopeCamera),
        "Scope camera accepts valid gameplay-camera and bullet inputs");
    Check(std::fabs(scopeCamera.position[0] - 4.0f) < 1e-5f &&
          std::fabs(scopeCamera.position[1] - 5.0f) < 1e-5f &&
          std::fabs(scopeCamera.position[2] - 6.0f) < 1e-5f,
        "Scope camera keeps Halo's collision-safe gameplay origin");
    const float bulletLength = std::sqrt(1.0f + 0.02f * 0.02f + 0.01f * 0.01f);
    Check(std::fabs(scopeCamera.forward[0] - bulletForward[0] / bulletLength) < 1e-5f &&
          std::fabs(scopeCamera.forward[1] - bulletForward[1] / bulletLength) < 1e-5f &&
          std::fabs(scopeCamera.forward[2] - bulletForward[2] / bulletLength) < 1e-5f,
        "Remote scope center continues along Halo's actual bullet direction");

    const ScopeProjectionTangents scopeLens =
        ComputeScopeProjectionTangents(2.5f, 16.0f / 9.0f);
    Check(std::fabs(scopeLens.horizontal / scopeLens.vertical - 16.0f / 9.0f) < 1e-5f,
        "Scope render projection matches its source surface before cropping");
    const float croppedHorizontal = scopeLens.horizontal * (4.0f / 3.0f) / (16.0f / 9.0f);
    Check(std::fabs(croppedHorizontal / scopeLens.vertical - 4.0f / 3.0f) < 1e-5f &&
          std::fabs(croppedHorizontal - 0.70020754f / 2.5f) < 1e-5f,
        "Center-cropped scope image is an undistorted 4:3 2.5x lens");

    ScopeRefreshScheduler refresh;
    Check(!refresh.Advance(true, 2) && refresh.Advance(true, 2),
        "Scope refresh divisor 2 renders every second active frame");
    Check(!refresh.Advance(false, 2) && !refresh.Advance(true, 2) &&
          refresh.Advance(true, 2),
        "Closing the scope resets its image refresh schedule");
    Check(refresh.Advance(true, 0),
        "Scope refresh divisor clamps safely to one");

    ScopeZoomController runtimeZoom;
    Check(std::fabs(runtimeZoom.Update(true, 1.0f, 0.5f, 8.0f) - 8.0f) < 1e-5f,
        "Opening the scope restores configured zoom before applying stick input");
    Check(std::fabs(runtimeZoom.Update(true, 1.0f, 0.10f, 8.0f) - 9.0f) < 1e-5f,
        "Right-stick up increases runtime scope zoom");
    Check(std::fabs(runtimeZoom.Update(true, -1.0f, 0.10f, 8.0f) - 8.0f) < 1e-5f,
        "Right-stick down decreases runtime scope zoom");
    Check(std::fabs(runtimeZoom.Update(true, 0.15f, 0.10f, 8.0f) - 8.0f) < 1e-5f,
        "Scope zoom ignores right-stick deadzone noise");
    runtimeZoom.Update(false, 0.0f, 0.0f, 8.0f);
    Check(std::fabs(runtimeZoom.Update(true, 0.0f, 0.0f, 12.0f) - 12.0f) < 1e-5f,
        "Reopening the scope resets temporary zoom to the configured default");

    ScopeZoomResolver zoomResolver;
    zoomResolver.RequestToggle();
    Check(!zoomResolver.Update(true, false) && zoomResolver.Update(true, false),
        "A weapon with no native zoom opens the fallback scope after detection");
    zoomResolver.RequestToggle();
    Check(zoomResolver.Update(true, false) && !zoomResolver.Update(true, false),
        "A second non-zoom weapon click closes the fallback scope");
    zoomResolver.Reset();
    Check(zoomResolver.Update(true, true),
        "Halo native zoom immediately owns scope visibility");
    zoomResolver.RequestToggle();
    Check(zoomResolver.Update(true, true),
        "A second authored zoom stage keeps the scope visible");
    zoomResolver.RequestToggle();
    Check(!zoomResolver.Update(true, false) && !zoomResolver.Update(true, false),
        "Leaving Halo native zoom closes without enabling fallback");
    zoomResolver.Reset();
    zoomResolver.Update(true, true);
    Check(!zoomResolver.Update(true, false),
        "Native zoom can close before its R3 release is observed");
    zoomResolver.RequestToggle();
    Check(!zoomResolver.Update(true, false) && !zoomResolver.Update(true, false),
        "The late release from native zoom-off is not mistaken for fallback");

    PauseLevelRecovery pauseRecovery;
    Check(!pauseRecovery.Update(true, false, false),
        "Pause recovery stays armed during an ordinary pause");
    Check(!pauseRecovery.Update(true, true, false),
        "Pause recovery records loading without resuming early");
    Check(pauseRecovery.Update(true, false, true),
        "Pause recovery restores 3D when restarted level becomes stable");
    Check(!pauseRecovery.Update(true, false, true),
        "Pause recovery fires only once per loading gap");
    pauseRecovery.Update(true, true, false);
    Check(!pauseRecovery.Update(false, false, true),
        "Leaving pause resets an incomplete restart recovery");

    const float rayOrigin[3] = { 0.0f, 0.0f, 0.0f };
    const float rayForward[3] = { 0.0f, 0.0f, -1.0f };
    MenuPointerHit hit = IntersectMenuQuad(rayOrigin, rayForward,
        1.2f, 1.1f, 0.825f, -0.08f);
    Check(hit.hit && hit.u == 0.5f, "Forward controller ray hits the menu center column");
    const float rayAway[3] = { 0.0f, 0.0f, 1.0f };
    Check(!IntersectMenuQuad(rayOrigin, rayAway, 1.2f, 1.1f, 0.825f, -0.08f).hit,
        "Controller rays pointing away from the menu miss");

    // The grab handle can slide the panel sideways, so the pointer must follow
    // it. A forward ray that centred the old fixed panel has to miss a panel
    // pushed a full width to the right, and hit its left edge exactly.
    Check(!IntersectMenuQuad(rayOrigin, rayForward, 1.2f, 1.1f, 0.825f, -0.08f, 1.1f).hit,
        "A panel moved a full width aside is no longer under a straight-ahead ray");
    const MenuPointerHit offsetHit =
        IntersectMenuQuad(rayOrigin, rayForward, 1.2f, 1.1f, 0.825f, -0.08f, 0.55f);
    Check(offsetHit.hit && offsetHit.u == 0.0f,
        "A panel moved half a width right puts its left edge on the forward ray");
    // Passing centerX explicitly must match the defaulted call exactly, and the
    // vertical offset still applies: the panel hangs below the eye line, so a
    // level ray lands above its middle.
    const MenuPointerHit offsetCenter =
        IntersectMenuQuad(rayOrigin, rayForward, 1.2f, 1.1f, 0.825f, -0.08f, 0.0f);
    Check(offsetCenter.hit && offsetCenter.u == hit.u && offsetCenter.v == hit.v,
        "An explicit zero side offset matches the defaulted panel exactly");
    Check(offsetCenter.v > 0.0f && offsetCenter.v < 0.5f,
        "A level ray lands above the middle of a panel hung below the eye line");
    Check(BlendXInputMotors(0, 0) == 0.0f, "Zero XInput rumble stops haptics");
    Check(BlendXInputMotors(65535, 65535) == 1.0f,
        "Both full XInput motors produce full portable haptics");
    const float lowOnly = BlendXInputMotors(65535, 0);
    const float highOnly = BlendXInputMotors(0, 65535);
    Check(lowOnly > highOnly && highOnly > 0.0f,
        "Both motor bands contribute to the blended haptic amplitude");

    Check(NormalizeVirtualXInputSetStateResult(
              ERROR_DEVICE_NOT_CONNECTED, 0, true) == ERROR_SUCCESS,
        "Virtual slot 0 stays connected when title policy suppresses haptics");
    Check(NormalizeVirtualXInputSetStateResult(
              ERROR_DEVICE_NOT_CONNECTED, 1, true) ==
              ERROR_DEVICE_NOT_CONNECTED,
        "Foreign XInput slots preserve the underlying SetState result");

    // Peak-hold haptics: a gunfire pulse that rose and fell between two VR
    // frame samples must still fire once, a sustained rumble must persist,
    // and out-of-range inputs must clamp.
    const HapticPeakSample onePulse = SampleHapticPeak(0.8f, 0.0f);
    Check(onePulse.apply == 0.8f,
        "A rumble pulse that already returned to zero still applies its peak");
    Check(onePulse.carry == 0.0f,
        "A one-shot rumble pulse does not persist after it is applied");
    const HapticPeakSample sustained = SampleHapticPeak(0.3f, 0.6f);
    Check(sustained.apply == 0.6f && sustained.carry == 0.6f,
        "A sustained rumble above the stale peak applies and carries forward");
    const HapticPeakSample clamped = SampleHapticPeak(1.5f, -0.5f);
    Check(clamped.apply == 1.0f && clamped.carry == 0.0f,
        "Peak-hold haptic samples clamp to the [0,1] amplitude range");
    Check(NormalizeVirtualXInputSetStateResult(
              ERROR_DEVICE_NOT_CONNECTED, 0, false) ==
              ERROR_DEVICE_NOT_CONNECTED,
        "An invalid vibration request preserves the underlying SetState result");

    Check(!IsMaterialFramePeriodTransition(8333333, 8333334),
        "OpenXR nanosecond period jitter does not trigger a pacing capture");
    Check(IsMaterialFramePeriodTransition(8333333, 16666667),
        "A 120-to-60 runtime cadence change triggers a pacing capture");
    Check(IsMaterialFramePeriodTransition(11111111, 22222222),
        "A 90-to-45 runtime cadence change triggers a pacing capture");
    Check(IsMaterialFramePeriodTransition(6944444, 13888889),
        "A 144-to-72 runtime cadence change triggers a pacing capture");
    Check(!IsMaterialFramePeriodTransition(8333333, 8403361),
        "A small runtime-period adjustment does not masquerade as a cadence flip");
    Check(ShouldReleaseFrameWaitWorkerBeforeBegin(true, true),
        "A claimed worker packet releases the next wait before xrBeginFrame");
    Check(!ShouldReleaseFrameWaitWorkerBeforeBegin(true, false),
        "A missing worker packet never releases another wait before xrBeginFrame");
    Check(!ShouldReleaseFrameWaitWorkerBeforeBegin(false, true),
        "No wait-worker release occurs when the worker is unavailable");
    Check(ClassifyFrameWaitPermit(7, 6) == FrameWaitPermit::Park,
        "The worker remains parked until its exact packet sequence is released");
    Check(ClassifyFrameWaitPermit(7, 7) == FrameWaitPermit::StartNextWait,
        "An exact sequence acknowledgement permits one subsequent wait");
    Check(ClassifyFrameWaitPermit(7, 8) == FrameWaitPermit::Fault &&
          ClassifyFrameWaitPermit(0, 0) == FrameWaitPermit::Fault,
        "Skipped or invalid wait generations fault instead of issuing a wait");
    Check(IsExpectedNextFrameWaitDispatch(7, 8),
        "Begin admission observes the exact subsequent worker dispatch");
    Check(!IsExpectedNextFrameWaitDispatch(7, 7) &&
          !IsExpectedNextFrameWaitDispatch(7, 9) &&
          !IsExpectedNextFrameWaitDispatch(0, 1) &&
          !IsExpectedNextFrameWaitDispatch(UINT64_MAX, 0),
        "Begin admission rejects stale, skipped, invalid, or wrapped dispatches");

    // Reach observer-camera head-lock: the call-site classifier decides which
    // consumers get taken off the hand. Getting the world-render index wrong
    // would double-apply head-look to the accepted Reach 3D path.
    Check(ReachClassifyObserverCameraReturn(0x0026C2DE) ==
              kReachObserverCameraWorldSite,
        "The world render camera return address maps to the world site");
    Check(ReachClassifyObserverCameraReturn(0x002E1525) ==
              kReachObserverCameraChudSite,
        "The CHUD projection return address maps to the CHUD site");
    Check(kReachObserverCameraWorldSite != kReachObserverCameraChudSite,
        "The world and CHUD sites are distinct");
    Check(ReachClassifyObserverCameraReturn(0x0025B404) == 0 &&
          ReachClassifyObserverCameraReturn(0x0025D406) == 1 &&
          ReachClassifyObserverCameraReturn(0x0026FA47) == 3 &&
          ReachClassifyObserverCameraReturn(0x0026FB0E + 5) == 4,
        "Every measured observer-camera call site maps to its own index");
    Check(ReachClassifyObserverCameraReturn(0x0026C2DD) < 0 &&
          ReachClassifyObserverCameraReturn(0x0026C2DF) < 0 &&
          ReachClassifyObserverCameraReturn(0) < 0,
        "An off-by-one or unknown return address is never classified as a site");
    {
        bool collision = false;
        for (int a = 0; a < 6; ++a)
            for (int b = a + 1; b < 6; ++b)
                if (kReachObserverCameraReturnRvas[a] ==
                    kReachObserverCameraReturnRvas[b])
                    collision = true;
        Check(!collision,
            "The six observer-camera return addresses are all distinct");
    }

    // Reach second muzzle flash: only a WORLD location on an effect that
    // declares a first-person weapon user is redirected onto the gun. Getting
    // this wrong would relocate damage or object-spawn points.
    {
        // fp mask 1, output user 0 -> a real first-person weapon effect.
        const ReachEffectFpDecision world =
            ReachDecideEffectLocation(0x01, 0x0007);
        Check(world.redirect && world.userIndex == 0 &&
              world.markerIndex == 0x0007,
            "A world location on a first-person weapon effect is redirected");
        const ReachEffectFpDecision alreadyFp =
            ReachDecideEffectLocation(0x01, 0x8007);
        Check(!alreadyFp.redirect,
            "A location already flagged first-person is left to the engine");
        Check(!ReachDecideEffectLocation(0x00, 0x0007).redirect,
            "An effect with no first-person weapon user is never redirected");
        Check(!ReachDecideEffectLocation(0xF1, 0x0007).redirect,
            "An effect whose first-person output user is 'none' is not "
            "redirected");
        Check(!ReachDecideEffectLocation(0x01, 0xFFFE).redirect &&
              !ReachDecideEffectLocation(0x01, 0xFFFF).redirect,
            "The engine's own -2/-1 designator cases are left untouched");
        const ReachEffectFpDecision user2 =
            ReachDecideEffectLocation(0x21, 0x0123);
        Check(user2.redirect && user2.userIndex == 2 &&
              user2.markerIndex == 0x0123,
            "The first-person output user index is decoded from the high nibble");
    }

    // Reach muzzle retarget: every first-person system off the majority
    // marker is moved onto it; ties break toward the lower location index
    // (primary_trigger is index 0 in all six affected weapons).
    {
        // AR: round@0, smoke@0, long_brake@2, glow@0, all mode 1 -> one move.
        const unsigned short arM[] = {1, 1, 1, 1};
        const unsigned short arL[] = {0, 0, 2, 0};
        const ReachMuzzleRetargetPlan ar = ReachDecideMuzzleRetarget(arM, arL, 4);
        Check(ar.count == 1 && ar.elements[0] == 2 && ar.newLocation == 0,
            "The AR's odd first-person system is retargeted onto its siblings'");
        // DMR: two odd systems at location 2 -> both move.
        const unsigned short dmrM[] = {1, 1, 1, 1, 1};
        const unsigned short dmrL[] = {2, 2, 0, 0, 0};
        const ReachMuzzleRetargetPlan dmr =
            ReachDecideMuzzleRetarget(dmrM, dmrL, 5);
        Check(dmr.count == 2 && dmr.newLocation == 0 &&
              dmr.elements[0] == 0 && dmr.elements[1] == 1,
            "Both DMR odd systems are retargeted");
        // Sniper: 4 at 0 vs 4 at 1 - tie breaks to the lower index.
        const unsigned short snM[] = {1, 1, 1, 1, 1, 1, 1, 1};
        const unsigned short snL[] = {0, 0, 1, 0, 1, 0, 1, 1};
        const ReachMuzzleRetargetPlan sn = ReachDecideMuzzleRetarget(snM, snL, 8);
        Check(sn.count == 4 && sn.newLocation == 0,
            "A tie between locations resolves to the lower index");
        // Uniform event: nothing to do.
        const unsigned short uniM[] = {1, 1, 1, 1, 1};
        const unsigned short uniL[] = {2, 2, 2, 2, 2};
        Check(ReachDecideMuzzleRetarget(uniM, uniL, 5).count == 0,
            "An event whose systems share one location is never touched");
        // Mode-0 systems at other markers never make a weapon eligible.
        const unsigned short othM[] = {0, 0, 1, 1, 1};
        const unsigned short othL[] = {1, 5, 0, 0, 0};
        Check(ReachDecideMuzzleRetarget(othM, othL, 5).count == 0,
            "Mode-0 systems at other markers never make a weapon eligible");
        // No agreeing pair -> ambiguous -> untouched.
        const unsigned short thinM[] = {1, 1, 0};
        const unsigned short thinL[] = {0, 2, 0};
        Check(ReachDecideMuzzleRetarget(thinM, thinL, 3).count == 0,
            "A single sibling is not a majority; nothing is moved");
    }

    {
        // Halo 3 vehicle snapshot: generation keying must reject stale and
        // zero generations exactly like the Reach precedent, and the optional
        // type/seat refinements must read as unproven (-1) whenever absent,
        // out of range, or stale.
        constexpr uint32_t gen = 41;
        constexpr uint64_t onFoot = Halo3VehicleSnapshot(
            gen, Halo3VehicleState::OnFoot);
        constexpr uint64_t warthogDriver = Halo3VehicleSnapshot(
            gen, Halo3VehicleState::Vehicle, 1, 0);
        constexpr uint64_t turretSeat = Halo3VehicleSnapshot(
            gen, Halo3VehicleState::Vehicle, 5, 2);
        constexpr uint64_t typeUnproven = Halo3VehicleSnapshot(
            gen, Halo3VehicleState::Vehicle);
        Check(Halo3VehicleSnapshotState(onFoot, gen) ==
                  Halo3VehicleState::OnFoot &&
              Halo3VehicleSnapshotState(warthogDriver, gen) ==
                  Halo3VehicleState::Vehicle &&
              Halo3VehicleSnapshotState(warthogDriver, gen + 1) ==
                  Halo3VehicleState::Unknown &&
              Halo3VehicleSnapshotState(warthogDriver, 0) ==
                  Halo3VehicleState::Unknown,
            "Halo 3 vehicle snapshot state honours the generation key");
        Check(Halo3VehicleSnapshotType(warthogDriver, gen) == 1 &&
              Halo3VehicleSnapshotSeat(warthogDriver, gen) == 0 &&
              Halo3VehicleSnapshotType(turretSeat, gen) == 5 &&
              Halo3VehicleSnapshotSeat(turretSeat, gen) == 2 &&
              Halo3VehicleSnapshotType(typeUnproven, gen) == -1 &&
              Halo3VehicleSnapshotSeat(typeUnproven, gen) == -1 &&
              Halo3VehicleSnapshotType(warthogDriver, gen + 1) == -1 &&
              Halo3VehicleSnapshotSeat(warthogDriver, 0) == -1,
            "Halo 3 vehicle snapshot type/seat unpack and degrade to unproven");
        Check(Halo3VehicleSnapshotType(Halo3VehicleSnapshot(
                  gen, Halo3VehicleState::Vehicle,
                  kHalo3VehicleTypeMax + 1, kHalo3VehicleSeatMax + 1),
                  gen) == -1 &&
              Halo3VehicleSnapshotSeat(Halo3VehicleSnapshot(
                  gen, Halo3VehicleState::Vehicle,
                  kHalo3VehicleTypeMax + 1, kHalo3VehicleSeatMax + 1),
                  gen) == -1 &&
              Halo3VehicleSnapshotType(Halo3VehicleSnapshot(
                  gen, Halo3VehicleState::Vehicle,
                  kHalo3VehicleTypeMax, kHalo3VehicleSeatMax),
                  gen) == kHalo3VehicleTypeMax &&
              Halo3VehicleSnapshotSeat(Halo3VehicleSnapshot(
                  gen, Halo3VehicleState::Vehicle,
                  kHalo3VehicleTypeMax, kHalo3VehicleSeatMax),
                  gen) == kHalo3VehicleSeatMax,
            "Halo 3 vehicle snapshot rejects out-of-range refinements only");

        // Debounce: the stable state flips only after `threshold` consecutive
        // differing samples, the edge fires exactly once, and a flapping
        // sample restarts the count without ever surfacing.
        Halo3VehicleDebounce debounce;
        bool edged = false;
        for (uint32_t i = 0; i < kHalo3VehicleDebounceFrames - 1; ++i)
            edged = debounce.Update(
                Halo3VehicleState::Vehicle, kHalo3VehicleDebounceFrames);
        Check(!edged && debounce.stable == Halo3VehicleState::Unknown,
            "Vehicle entry does not settle before the debounce threshold");
        Check(debounce.Update(
                  Halo3VehicleState::Vehicle, kHalo3VehicleDebounceFrames) &&
              debounce.stable == Halo3VehicleState::Vehicle,
            "Vehicle entry settles exactly at the debounce threshold");
        Check(!debounce.Update(
                  Halo3VehicleState::Vehicle, kHalo3VehicleDebounceFrames),
            "A settled state does not re-fire the edge");
        for (uint32_t i = 0; i < kHalo3VehicleDebounceFrames - 2; ++i)
            debounce.Update(
                Halo3VehicleState::OnFoot, kHalo3VehicleDebounceFrames);
        debounce.Update(
            Halo3VehicleState::Vehicle, kHalo3VehicleDebounceFrames);
        Check(debounce.stable == Halo3VehicleState::Vehicle,
            "A flap back to Vehicle restarts the exit count unseen");
        for (uint32_t i = 0; i < kHalo3VehicleDebounceFrames - 1; ++i)
            edged = debounce.Update(
                Halo3VehicleState::OnFoot, kHalo3VehicleDebounceFrames);
        Check(!edged &&
              debounce.Update(
                  Halo3VehicleState::OnFoot, kHalo3VehicleDebounceFrames) &&
              debounce.stable == Halo3VehicleState::OnFoot,
            "Vehicle exit settles only after a full fresh debounce run");

        // Pin the byte-proven Halo 3 identities exactly like the Reach
        // vehicle test pins its HREK-matched RVAs. These expected values are
        // cross-checks; the runtime binding is always the unique AOB match.
        Check(kHalo3UnitInVehicleNativeRva == 0x3A36F4 &&
              kHalo3PlayerUnitGetterRva == 0xEE48C &&
              kHalo3VehicleTypeAccessorRva == 0x396D84 &&
              kHalo3EngineTlsIndexRva == 0xA39F9C &&
              kHalo3TlsObjectTableOffset == 0x38 &&
              kHalo3ObjectTableEntriesOffset == 0x48 &&
              kHalo3ObjectEntryStride == 0x18 &&
              kHalo3ObjectEntryDataOffset == 0x10 &&
              kHalo3ObjectKindVehicle == 1 &&
              kHalo3ObjectParentOffset == 0x10 &&
              kHalo3UnitSeatWordOffset == 0x24E &&
              kHalo3TlsObserverOffset == 0x578 &&
              kHalo3ObserverStride == 0x3D0 &&
              kHalo3ObserverPosOffset == 0x11C &&
              kHalo3ObserverFwdOffset == 0x144 &&
              kHalo3ObserverUpOffset == 0x150 &&
              kHalo3VehicleTypeTurret == 5 &&
              kHalo3VehicleTypeNoneAuthored == 0xB &&
              kHalo3VehicleFlyingTypeMask == 0x94u,
            "Halo 3 vehicle identities pin the evidence-doc values");

        // Focus-candidate scan: a planted focus = pos + fwd*d must be found
        // exactly, and a degenerate window must return no candidate.
        float observerWindow[kHalo3ObserverCaptureFloats];
        for (size_t i = 0; i < kHalo3ObserverCaptureFloats; ++i)
            observerWindow[i] = 1000.0f + static_cast<float>(i) * 7.0f;
        constexpr size_t posIndex =
            (kHalo3ObserverPosOffset - kHalo3ObserverCaptureBase) / 4;
        constexpr size_t fwdIndex =
            (kHalo3ObserverFwdOffset - kHalo3ObserverCaptureBase) / 4;
        observerWindow[posIndex] = 10.0f;
        observerWindow[posIndex + 1] = 20.0f;
        observerWindow[posIndex + 2] = 30.0f;
        observerWindow[fwdIndex] = 0.0f;
        observerWindow[fwdIndex + 1] = 1.0f;
        observerWindow[fwdIndex + 2] = 0.0f;
        observerWindow[0] = 10.0f;    // planted focus = pos + fwd * 2.5
        observerWindow[1] = 22.5f;
        observerWindow[2] = 30.0f;
        observerWindow[30] = 2.5f;    // planted focus distance
        const Halo3FocusCandidate found = Halo3FindFocusCandidate(
            observerWindow, kHalo3ObserverCaptureFloats, posIndex, fwdIndex);
        Check(found.tripletIndex == 0 && found.distanceIndex == 30 &&
              found.residual < 1.0e-4f && found.distance == 2.5f,
            "Observer focus scan recovers a planted focus/distance pair");
        Check(Halo3FindFocusCandidate(
                  nullptr, kHalo3ObserverCaptureFloats, posIndex,
                  fwdIndex).tripletIndex == -1 &&
              Halo3FindFocusCandidate(
                  observerWindow, 2, 0, 1).tripletIndex == -1,
            "Observer focus scan rejects degenerate windows");

        // C4 transform-probe analysis: unit-length triplets (orientation
        // basis signature) and the triplet nearest a reference point
        // (position signature) must be found exactly and never invented.
        float xformWindow[kHalo3ParentCaptureFloats];
        for (size_t i = 0; i < kHalo3ParentCaptureFloats; ++i)
            xformWindow[i] = 500.0f + static_cast<float>(i);
        xformWindow[8] = 0.0f;  xformWindow[9] = 0.6f;   // unit triplet A
        xformWindow[10] = 0.8f;
        xformWindow[20] = 1.0f; xformWindow[21] = 0.0f;  // unit triplet B
        xformWindow[22] = 0.0f;
        xformWindow[40] = 101.0f; xformWindow[41] = 202.0f; // position
        xformWindow[42] = 303.5f;
        const float xformRef[3] = {100.0f, 202.0f, 303.0f};
        const Halo3UnitTripletScan unitScan = Halo3FindUnitTriplets(
            xformWindow, kHalo3ParentCaptureFloats, 0.01f);
        bool sawA = false, sawB = false;
        for (int i = 0; i < unitScan.count && i < 12; ++i)
        {
            if (unitScan.indices[i] == 8) sawA = true;
            if (unitScan.indices[i] == 20) sawB = true;
        }
        const Halo3NearestTriplet nearest = Halo3FindNearestTriplet(
            xformWindow, kHalo3ParentCaptureFloats, xformRef);
        Check(sawA && sawB && nearest.index == 40 &&
              nearest.distance < 1.5f &&
              Halo3FindUnitTriplets(nullptr, 0, 0.01f).count == 0 &&
              Halo3FindNearestTriplet(nullptr, 0, xformRef).index == -1,
            "Transform probe scans find planted basis and position triplets");

        // C8 identity resolution: physics type alone is a CLASS, so the
        // definition fields the C7 log confirmed in process must separate the
        // cousins, and anything unrecognised must resolve to Unknown.
        {
            Halo3DefinitionFields hogDef;
            hogDef.physicsType = 1; hogDef.jeepValid = true;
            hogDef.engineMoment = 2000.0f;              // C7 log, def=05F9
            Halo3DefinitionFields gooseDef;
            gooseDef.physicsType = 1; gooseDef.jeepValid = true;
            gooseDef.engineMoment = 650.0f;             // C7 log, def=0517
            Halo3DefinitionFields oddJeep;
            oddJeep.physicsType = 1; oddJeep.jeepValid = true;
            oddJeep.engineMoment = 1300.0f;             // neither -> stock
            Halo3DefinitionFields blindJeep;
            blindJeep.physicsType = 1;                  // block unreadable
            Halo3DefinitionFields ghostDef;
            ghostDef.physicsType = 3; ghostDef.scoutValid = true;
            ghostDef.specificType = 1;
            Halo3DefinitionFields wraithDef;
            wraithDef.physicsType = 3; wraithDef.scoutValid = true;
            wraithDef.specificType = 3;
            Halo3DefinitionFields maulerDef;
            maulerDef.physicsType = 3; maulerDef.scoutValid = true;
            maulerDef.specificType = 4;
            Halo3DefinitionFields scoutOdd;
            scoutOdd.physicsType = 3; scoutOdd.scoutValid = true;
            scoutOdd.specificType = 7;
            Halo3DefinitionFields turretDef;
            turretDef.physicsType = 5;                  // never a root identity
            Halo3DefinitionFields tankDef;  tankDef.physicsType = 0;
            Halo3DefinitionFields choppDef; choppDef.physicsType = 8;

            const bool cousinsSplit =
                Halo3ResolveVehicleId(hogDef) == Halo3VehicleId::Warthog &&
                Halo3ResolveVehicleId(gooseDef) == Halo3VehicleId::Mongoose &&
                Halo3ResolveVehicleId(ghostDef) == Halo3VehicleId::Ghost &&
                Halo3ResolveVehicleId(wraithDef) == Halo3VehicleId::Wraith &&
                Halo3ResolveVehicleId(maulerDef) == Halo3VehicleId::Mauler &&
                Halo3ResolveVehicleId(tankDef) == Halo3VehicleId::Scorpion &&
                Halo3ResolveVehicleId(choppDef) == Halo3VehicleId::Chopper;
            const bool unknownsStayUnknown =
                Halo3ResolveVehicleId(oddJeep) == Halo3VehicleId::Unknown &&
                Halo3ResolveVehicleId(blindJeep) == Halo3VehicleId::Unknown &&
                Halo3ResolveVehicleId(scoutOdd) == Halo3VehicleId::Unknown &&
                Halo3ResolveVehicleId(turretDef) == Halo3VehicleId::Unknown &&
                Halo3ResolveVehicleId(Halo3DefinitionFields{}) ==
                    Halo3VehicleId::Unknown;

            // The two authored jeep constants must not be within tolerance of
            // each other, or the resolver would be a coin toss.
            const bool constantsSeparate =
                std::fabs(kHalo3WarthogEngineMoment -
                          kHalo3MongooseEngineMoment) >
                2.0f * kHalo3EngineMomentTolerance;

            // Seat lookup keys on identity, not type: a mongoose must never
            // receive the warthog point, mounted gunners come from the
            // carrier's identity, and Unknown never resolves to anything.
            const Halo3SeatPoint* hogDriver = Halo3FindSeatPoint(
                Halo3VehicleId::Warthog, 0, false);
            const Halo3SeatPoint* hogGunner = Halo3FindSeatPoint(
                Halo3VehicleId::Warthog, 0, true);
            const Halo3SeatPoint* gooseDriver = Halo3FindSeatPoint(
                Halo3VehicleId::Mongoose, 0, false);
            const Halo3SeatPoint* hogPassenger = Halo3FindSeatPoint(
                Halo3VehicleId::Warthog, 1, false);
            const bool lookupOk =
                hogDriver && hogDriver->y > 0.1f && !hogDriver->carrierFrame &&
                hogPassenger && hogPassenger->y < -0.1f &&
                gooseDriver && gooseDriver != hogDriver &&
                std::fabs(gooseDriver->z - hogDriver->z) > 0.1f &&
                hogGunner && hogGunner->carrierFrame &&
                hogGunner != hogDriver &&
                // the gunner point is the same key but a different frame
                Halo3FindSeatPoint(Halo3VehicleId::Mongoose, 0, true) ==
                    nullptr &&
                Halo3FindSeatPoint(Halo3VehicleId::Unknown, 0, false) ==
                    nullptr &&
                Halo3FindSeatPoint(Halo3VehicleId::Ghost, 1, false) ==
                    nullptr &&
                Halo3FindSeatPoint(Halo3VehicleId::Hornet, 2, false) !=
                    nullptr &&
                Halo3FindSeatPoint(Halo3VehicleId::Banshee, 3, false) ==
                    nullptr;

            Check(cousinsSplit && unknownsStayUnknown && constantsSeparate &&
                  lookupOk,
                "Vehicle identity separates same-type cousins and an "
                "unidentified vehicle resolves to no point at all");
        }

        // O1: ODST's own identity rules. ODST ships the same authored vehicles
        // as Halo 3, so the shared discriminators must behave identically —
        // but the shade is new, and it collides with the walk-up turrets
        // because neither authors a physics block. Only the seat's own driver
        // bit plus the engine's in-vehicle answer separate them.
        {
            OdstDefinitionFields hog;
            hog.physicsType = 1; hog.jeepValid = true;
            hog.engineMoment = kOdstWarthogEngineMoment;
            OdstDefinitionFields goose;
            goose.physicsType = 1; goose.jeepValid = true;
            goose.engineMoment = kOdstMongooseEngineMoment;
            OdstDefinitionFields ghost;
            ghost.physicsType = 3; ghost.scoutValid = true;
            ghost.specificType = 1;
            OdstDefinitionFields wraith;
            wraith.physicsType = 3; wraith.scoutValid = true;
            wraith.specificType = 3;
            const bool sharedVehiclesResolve =
                OdstResolveVehicleId(hog) == OdstVehicleId::Warthog &&
                OdstResolveVehicleId(goose) == OdstVehicleId::Mongoose &&
                OdstResolveVehicleId(ghost) == OdstVehicleId::Ghost &&
                OdstResolveVehicleId(wraith) == OdstVehicleId::Wraith;

            // A shade: no physics block authored, seat 0 is a driver seat, and
            // the engine reports the occupant is in a vehicle.
            OdstDefinitionFields shade;
            shade.physicsType = kOdstPhysicsTypeNoneAuthored;
            shade.seatFlagsValid = true;
            shade.seat0Flags = kOdstSeatDriverBit | kOdstSeatGunnerBit |
                kOdstSeatThirdPersonCameraBit;   // the tag's authored 0x1C
            shade.inVehicle = true;
            // A walk-up machinegun turret: gunner seat, no driver bit.
            OdstDefinitionFields turret;
            turret.physicsType = kOdstPhysicsTypeNoneAuthored;
            turret.seatFlagsValid = true;
            turret.seat0Flags = kOdstSeatGunnerBit |
                kOdstSeatThirdPersonCameraBit;
            turret.inVehicle = false;
            // O8: same shape as the shade but the engine says it is not a
            // vehicle. This was asserted as "refuse to guess" -> Unknown, and
            // the 2026-08-06 probe proved it is real shipped content: the
            // Covenant walk-up gun, `native=0 type=5 seats=1
            // seat0Flags=0x800C`. Refusing left it with no authored seat point
            // and put the camera at the vehicle root - in the ground. A turret
            // the engine does not call a vehicle is a walk-up emplacement.
            OdstDefinitionFields ambiguous = shade;
            ambiguous.inVehicle = false;
            // No seat evidence at all: refuse to guess.
            OdstDefinitionFields blind = shade;
            blind.seatFlagsValid = false;
            // O8: every real turret line from the 2026-08-06 ODST probe logs,
            // so a change to this switch can never again move a family that
            // was not checked. O7 was rejected for exactly that.
            //
            // The Warthog's own mounted gun: in a vehicle, two seats, seat 0 a
            // GUNNER seat. It must keep resolving as it always has - O7 swept
            // this into Shade and the user reported the Warthog turrets off.
            OdstDefinitionFields warthogMountedGun;
            warthogMountedGun.physicsType = kOdstPhysicsTypeTurret;
            warthogMountedGun.seatFlagsValid = true;
            warthogMountedGun.seat0Flags = 0x101188u;   // driver=0 gunner=1
            warthogMountedGun.inVehicle = true;
            // The Covenant walk-up gun: NOT a vehicle, one seat, and seat 0
            // carries the driver bit - which used to reject it into Unknown,
            // leaving it with no authored seat point at all.
            OdstDefinitionFields covenantWalkUp;
            covenantWalkUp.physicsType = kOdstPhysicsTypeTurret;
            covenantWalkUp.seatFlagsValid = true;
            covenantWalkUp.seat0Flags = 0x800Cu;        // driver=1 gunner=1
            covenantWalkUp.inVehicle = false;
            const bool probedTurretsResolve =
                OdstResolveVehicleId(warthogMountedGun) ==
                    OdstVehicleId::StationaryTurret &&
                OdstResolveVehicleId(covenantWalkUp) ==
                    OdstVehicleId::StationaryTurret &&
                OdstFindSeatPoint(
                    OdstVehicleId::StationaryTurret, 0, false) != nullptr;
            const bool shadeSplitsFromTurret =
                probedTurretsResolve &&
                OdstResolveVehicleId(shade) == OdstVehicleId::Shade &&
                OdstResolveVehicleId(turret) ==
                    OdstVehicleId::StationaryTurret &&
                OdstResolveVehicleId(ambiguous) ==
                    OdstVehicleId::StationaryTurret &&
                OdstResolveVehicleId(blind) == OdstVehicleId::Unknown &&
                OdstResolveVehicleId(OdstDefinitionFields{}) ==
                    OdstVehicleId::Unknown;

            // ODST's engine offsets must never coincide with Halo 3's, or a
            // mis-bound title would read plausible garbage instead of failing.
            const bool offsetsAreTitleSpecific =
                kOdstUnitSeatWordOffset != kHalo3UnitSeatWordOffset &&
                kOdstTlsObjectTableOffset != kHalo3TlsObjectTableOffset &&
                // Halo 3's node-bank cluster is 0x136/0x138 (game.cpp, not
                // exported here); ODST's is the same cluster shifted -0xC.
                kOdstObjectNodeBankSizeOffset == 0x12A &&
                kOdstObjectNodeBankRelOffset == 0x12C &&
                // ...while the parts that genuinely are shared stay shared.
                kOdstObjectTableEntriesOffset ==
                    kHalo3ObjectTableEntriesOffset &&
                kOdstObjectEntryStride == kHalo3ObjectEntryStride &&
                kOdstNodeMatrixStride == sizeof(Halo3Matrix4x3);

            // The node bank is only trusted when its byte size is a whole
            // number of matrices AND that count matches the render model.
            OdstProbeVehicleRecord good{};
            good.nodeBankByteSize = 16 * 0x34;
            good.tagNodeCount = 16;
            OdstProbeVehicleRecord ragged = good;
            ragged.nodeBankByteSize = 16 * 0x34 + 3;
            OdstProbeVehicleRecord mismatched = good;
            mismatched.tagNodeCount = 12;
            OdstProbeVehicleRecord empty{};
            const bool coherenceGateHolds =
                OdstNodeBankIsCoherent(good) &&
                !OdstNodeBankIsCoherent(ragged) &&
                !OdstNodeBankIsCoherent(mismatched) &&
                !OdstNodeBankIsCoherent(empty);

            const bool seatControlHolds =
                OdstSeatWordMeansUnseated(-1) &&
                !OdstSeatWordMeansUnseated(0) &&
                !OdstSeatWordMeansUnseated(3);

            // The object-table walk must refuse anything that is not provably
            // this build's "object" data array, because a wrong TLS slot would
            // otherwise hand it an arbitrary pointer to iterate.
            OdstDataArrayHeaderView table{};
            table.nameIsObject = true;
            table.signature = kOdstDataArraySignature;
            table.maximumCount = kOdstObjectTableCapacity;
            table.elementSize = kOdstObjectEntryStride;
            table.firstUnallocated = 400;
            table.valid = 1;
            OdstDataArrayHeaderView wrongName = table;
            wrongName.nameIsObject = false;
            OdstDataArrayHeaderView wrongMagic = table;
            wrongMagic.signature = 0x64407441;
            OdstDataArrayHeaderView wrongStride = table;
            wrongStride.elementSize = 0x1C;
            OdstDataArrayHeaderView notConnected = table;
            notConnected.valid = 0;
            OdstDataArrayHeaderView overrun = table;
            overrun.firstUnallocated = kOdstObjectTableCapacity + 1;
            const bool tableGateHolds =
                OdstObjectTableIsWalkable(table) &&
                !OdstObjectTableIsWalkable(wrongName) &&
                !OdstObjectTableIsWalkable(wrongMagic) &&
                !OdstObjectTableIsWalkable(wrongStride) &&
                !OdstObjectTableIsWalkable(notConnected) &&
                !OdstObjectTableIsWalkable(overrun) &&
                !OdstObjectTableIsWalkable(OdstDataArrayHeaderView{}) &&
                // Only a zero identifier marks a free slot; 0xFFFF is a live
                // identifier in this engine, not a sentinel.
                !OdstObjectEntryIsLive(0) &&
                OdstObjectEntryIsLive(0x8000) &&
                OdstObjectEntryIsLive(0xFFFF);

            // ODST's config bank must be able to key every seat its tags
            // actually expose, which is exactly why it is wider than Halo 3's:
            // the Scorpion's four riders are player seats in ODST, and the
            // Shade exists at all. A slot collision here would put one seat's
            // trim on another.
            const int shadeSlot = ConfigOdstSeatTrimSlot(
                static_cast<int>(OdstVehicleId::Shade), 0, false);
            const int scorpionRider4 = ConfigOdstSeatTrimSlot(
                static_cast<int>(OdstVehicleId::Scorpion), 4, false);
            const int scorpionGunner = ConfigOdstSeatTrimSlot(
                static_cast<int>(OdstVehicleId::Scorpion), 0, true);
            const int warthogDriver = ConfigOdstSeatTrimSlot(
                static_cast<int>(OdstVehicleId::Warthog), 0, false);
            bool slotsUnique = shadeSlot >= 0 && scorpionRider4 >= 0 &&
                scorpionGunner >= 0 && warthogDriver >= 0 &&
                shadeSlot < kOdstVehicleTrimSlots &&
                scorpionRider4 != scorpionGunner &&
                scorpionRider4 != warthogDriver &&
                shadeSlot != scorpionGunner;
            // Every seat ODST actually authors a point for must be keyable.
            for (const OdstSeatPoint& p : kOdstSeatPoints)
            {
                const int slot = ConfigOdstSeatTrimSlot(
                    static_cast<int>(p.vehicle), p.seatIndex, p.carrierFrame);
                slotsUnique = slotsUnique && slot >= 0 &&
                    slot < kOdstVehicleTrimSlots;
            }
            // Halo 3's narrower table genuinely cannot key the ODST-only
            // seats -- which is the whole reason ODST has its own bank.
            const bool halo3CannotKeyOdstSeats =
                ConfigSeatTrimSlot(
                    static_cast<int>(OdstVehicleId::Scorpion), 4, false) < 0 &&
                ConfigSeatTrimSlot(
                    static_cast<int>(OdstVehicleId::Shade), 0, false) < 0;

            // An unset ODST seat follows the universal trim; a set one wins.
            Config c;
            const bool odstFallbackHolds =
                ConfigOdstSeatCamForward(c, shadeSlot) ==
                    c.vehicle_cam_forward_m &&
                ConfigOdstSeatCamUp(c, scorpionRider4) == c.vehicle_cam_up_m;
            c.odst_vehicle_cam_forward_v[shadeSlot] = 0.42f;
            c.odst_vehicle_cam_forward_set[shadeSlot] = true;
            const bool odstOverrideWins =
                ConfigOdstSeatCamForward(c, shadeSlot) == 0.42f &&
                // ...and it must not have disturbed the Halo 3 bank.
                ConfigSeatCamForward(c, ConfigSeatTrimSlot(
                    static_cast<int>(Halo3VehicleId::Warthog), 0, false)) !=
                    0.42f;
            // The shipped ODST defaults must land on ODST slots, seeded from
            // the accepted Halo 3 tuning for the seats that diffed identical.
            const bool odstShippedSeeded =
                c.odst_vehicle_cam_forward_set[warthogDriver] &&
                c.odst_vehicle_cam_forward_v[warthogDriver] ==
                    c.vehicle_cam_forward_v[ConfigSeatTrimSlot(
                        static_cast<int>(Halo3VehicleId::Warthog), 0, false)];

            Check(slotsUnique && halo3CannotKeyOdstSeats &&
                  odstFallbackHolds && odstOverrideWins && odstShippedSeeded,
                "The ODST trim bank keys every seat ODST authors, including "
                "the seats Halo 3's table cannot express, and stays "
                "independent of the Halo 3 bank");

            // O3 follow eligibility. The Shade is the ODST-specific rule: it
            // has a seat point, but its own body is what its aim turns, so
            // following it would cancel the closed loop's feedback exactly the
            // way a walk-up turret would.
            const bool shadeNeverFollows =
                OdstFindSeatPoint(OdstVehicleId::Shade, 0, false) != nullptr &&
                !OdstSeatFollowsHull(OdstVehicleId::Shade, 0, false) &&
                !OdstSeatFollowsPitch(OdstVehicleId::Shade, 0, false) &&
                !OdstSeatIsDriver(OdstVehicleId::Shade, 0, false) &&
                !OdstSeatAuthorsSteering(OdstVehicleId::Shade, 0, false, true);
            // Aircraft follow yaw but never pitch; ground vehicles do both.
            const bool aircraftStayYawOnly =
                OdstSeatFollowsHull(OdstVehicleId::Banshee, 0, false) &&
                !OdstSeatFollowsPitch(OdstVehicleId::Banshee, 0, false) &&
                OdstSeatFollowsHull(OdstVehicleId::Hornet, 0, false) &&
                !OdstSeatFollowsPitch(OdstVehicleId::Hornet, 0, false) &&
                OdstSeatFollowsPitch(OdstVehicleId::Warthog, 0, false) &&
                OdstSeatFollowsPitch(OdstVehicleId::Scorpion, 0, false);
            // The Scorpion and Wraith aim a turret independently of the hull,
            // so nothing may author their steering; aircraft are look-steered
            // but take the flight stick, not a wheel.
            const bool steeringFamiliesHold =
                !OdstVehicleIsLookSteered(OdstVehicleId::Scorpion) &&
                !OdstVehicleIsLookSteered(OdstVehicleId::Wraith) &&
                OdstVehicleIsLookSteered(OdstVehicleId::Warthog) &&
                OdstVehicleUsesWheel(OdstVehicleId::Warthog) &&
                !OdstVehicleUsesWheel(OdstVehicleId::Banshee) &&
                !OdstVehicleUsesWheel(OdstVehicleId::Hornet) &&
                // The follow toggle is the master switch for steering author.
                OdstSeatAuthorsSteering(OdstVehicleId::Warthog, 0, false,
                                        true) &&
                !OdstSeatAuthorsSteering(OdstVehicleId::Warthog, 0, false,
                                         false) &&
                // A passenger never authors steering, only the driver does.
                !OdstSeatAuthorsSteering(OdstVehicleId::Warthog, 1, false,
                                         true) &&
                // Walk-up turrets are excluded everywhere.
                !OdstSeatFollowsHull(OdstVehicleId::StationaryTurret, 0,
                                     false);

            // The seat flag that decides whose eye the shots leave. Only a
            // seat that lets the occupant use their own weapon may have its
            // aim re-origined onto the engine's eye; a driver or mounted
            // gunner fires a vehicle barrel.
            const bool personalWeaponSeatSplit =
                // ODST warthog passenger, flags 0x1070 (from its own tag)
                (0x1070u & kOdstSeatAllowsWeaponsBit) != 0 &&
                // ODST warthog driver, flags 0x40014
                (0x40014u & kOdstSeatAllowsWeaponsBit) == 0 &&
                // the machinegun turret gunner, flags 0x5500018
                (0x5500018u & kOdstSeatAllowsWeaponsBit) == 0 &&
                // and the bit must not collide with the ones already keyed
                kOdstSeatAllowsWeaponsBit != kOdstSeatDriverBit &&
                kOdstSeatAllowsWeaponsBit != kOdstSeatGunnerBit &&
                kOdstSeatAllowsWeaponsBit !=
                    kOdstSeatThirdPersonCameraBit;

            Check(personalWeaponSeatSplit,
                "The allows-weapons seat flag separates a seat whose shots "
                "leave the occupant's own eye from a driver or turret gunner "
                "firing the vehicle's weapon");

            Check(shadeNeverFollows && aircraftStayYawOnly &&
                  steeringFamiliesHold,
                "ODST view-follow keeps the Shade and walk-up turrets out, "
                "holds aircraft to yaw-only, and authors steering for exactly "
                "the look-steered drivers");

            Check(sharedVehiclesResolve && shadeSplitsFromTurret &&
                  offsetsAreTitleSpecific && coherenceGateHolds &&
                  seatControlHolds && tableGateHolds,
                "ODST vehicle identity separates the shade from a walk-up "
                "turret, refuses to guess without seat evidence, keeps every "
                "engine offset title-specific, and only walks an object table "
                "that proves its own name and signature");
        }

        // C7 frame composition: a mounted turret's parent-relative frame must
        // compose through the carrier. Carrier at (10, 20, -5) yawed 90° left
        // (fwd = +Y world), turret mounted 0.5 behind carrier origin, 0.02 up,
        // facing carrier-forward: world origin = carrier + fwd*(-0.5) + up*0.02.
        {
            Halo3Frame carrier{};
            carrier.pos[0] = 10.0f; carrier.pos[1] = 20.0f;
            carrier.pos[2] = -5.0f;
            carrier.fwd[1] = 1.0f;          // facing world +Y
            carrier.up[2] = 1.0f;
            Halo3Frame local{};
            local.pos[0] = -0.5f; local.pos[2] = 0.02f;
            local.fwd[0] = 1.0f;
            local.up[2] = 1.0f;
            const Halo3Frame world = Halo3ComposeFrame(carrier, local);
            // carrier left = up × fwd = (-1, 0, 0); local x rides carrier fwd.
            const bool posOk =
                std::fabs(world.pos[0] - 10.0f) < 1e-4f &&
                std::fabs(world.pos[1] - 19.5f) < 1e-4f &&
                std::fabs(world.pos[2] - (-4.98f)) < 1e-4f;
            const bool axesOk =
                std::fabs(world.fwd[1] - 1.0f) < 1e-4f &&
                std::fabs(world.up[2] - 1.0f) < 1e-4f &&
                Halo3FrameOrthonormal(world);
            // Identity carrier: composition must be exact pass-through.
            Halo3Frame ident{};
            ident.fwd[0] = 1.0f; ident.up[2] = 1.0f;
            const Halo3Frame same = Halo3ComposeFrame(ident, local);
            const bool identOk =
                std::fabs(same.pos[0] - local.pos[0]) < 1e-6f &&
                std::fabs(same.pos[2] - local.pos[2]) < 1e-6f;
            // Sanity gate: the C6 sky failure (frame near the map origin,
            // bounds at the real vehicle) must be rejected; a frame origin
            // inside the bounding sphere must pass; junk radius rejects.
            const float boundsCenter[3] = {9.9f, 12.1f, -21.4f};
            const float skyPos[3] = {-0.5f, 0.0f, 0.019f};
            Halo3Frame degenerate = local;
            degenerate.up[2] = 0.0f;        // zero-length up
            Check(posOk && axesOk && identOk &&
                  !Halo3FrameOrthonormal(degenerate) &&
                  !Halo3AnchorWithinBounds(skyPos, boundsCenter, 0.8f, 2.0f) &&
                  Halo3AnchorWithinBounds(world.pos, world.pos, 0.8f, 2.0f) &&
                  !Halo3AnchorWithinBounds(skyPos, boundsCenter, 1e9f, 2.0f),
                "Frame composition carries mounted turrets and the sanity "
                "gate rejects wrong-frame anchors");
        }

        // C17 native parenting: calibration must preserve the exact Blender
        // point, while every later world-space movement comes from the native
        // camera even when the sampled object frame does not move at all.
        {
            Halo3Frame frame{};
            frame.pos[0] = 10.0f; frame.pos[1] = -3.0f; frame.pos[2] = 2.0f;
            frame.fwd[1] = 1.0f; frame.up[2] = 1.0f;
            const float nativeLocal[3] = {0.40f, -0.10f, 0.50f};
            const float authoredLocal[3] = {0.62f, 0.18f, 0.81f};
            float nativeOffset[3];
            Halo3FrameRotate(frame, nativeLocal, nativeOffset);
            float nativeWorld[3] = {
                frame.pos[0] + nativeOffset[0],
                frame.pos[1] + nativeOffset[1],
                frame.pos[2] + nativeOffset[2]};
            float recoveredLocal[3];
            const float relative[3] = {
                nativeWorld[0] - frame.pos[0],
                nativeWorld[1] - frame.pos[1],
                nativeWorld[2] - frame.pos[2]};
            Halo3FrameUnrotate(frame, relative, recoveredLocal);
            float anchor[3];
            Halo3NativeParentedAnchor(frame, nativeWorld, recoveredLocal,
                                      authoredLocal, anchor);
            float authoredOffset[3];
            Halo3FrameRotate(frame, authoredLocal, authoredOffset);
            const bool placementExact =
                std::fabs(anchor[0] - (frame.pos[0] + authoredOffset[0])) < 1e-5f &&
                std::fabs(anchor[1] - (frame.pos[1] + authoredOffset[1])) < 1e-5f &&
                std::fabs(anchor[2] - (frame.pos[2] + authoredOffset[2])) < 1e-5f;

            const float nativeDelta[3] = {1.25f, -0.75f, 0.33f};
            const float movedNative[3] = {
                nativeWorld[0] + nativeDelta[0],
                nativeWorld[1] + nativeDelta[1],
                nativeWorld[2] + nativeDelta[2]};
            float movedAnchor[3];
            Halo3NativeParentedAnchor(frame, movedNative, recoveredLocal,
                                      authoredLocal, movedAnchor);
            const bool nativeOwnsMotion =
                std::fabs((movedAnchor[0] - anchor[0]) - nativeDelta[0]) < 1e-5f &&
                std::fabs((movedAnchor[1] - anchor[1]) - nativeDelta[1]) < 1e-5f &&
                std::fabs((movedAnchor[2] - anchor[2]) - nativeDelta[2]) < 1e-5f;
            Check(placementExact && nativeOwnsMotion,
                "Native seat motion passes through exactly while the Blender "
                "point remains the calibrated camera placement");
        }

        // C8 fingerprint: the same physics type covers several vehicles, so
        // the authored point is keyed on the tag's node-0 offset recovered
        // from data already sampled. It must be independent of where the
        // vehicle is and which way it faces, and ambiguity must never pick.
        {
            // Warthog-like: node 0 sits 0.30 up and 0.05 forward of the origin.
            const float node0[3] = {0.05f, 0.0f, 0.30f};
            // Same vehicle parked at the origin facing +X...
            Halo3Frame a{};
            a.fwd[0] = 1.0f; a.up[2] = 1.0f;
            float centerA[3] = {0.05f, 0.0f, 0.30f};
            float fpA[3] = {0.0f, 0.0f, 0.0f};
            const bool okA = Halo3ComputeFingerprint(a, centerA, fpA);
            // ...and the same vehicle far away, yawed 90 deg left (fwd = +Y).
            Halo3Frame b{};
            b.pos[0] = 120.5f; b.pos[1] = -44.25f; b.pos[2] = 8.0f;
            b.fwd[1] = 1.0f; b.up[2] = 1.0f;
            // world center = pos + fwd*0.05 + up*0.30 (left is unused here)
            float centerB[3] = {120.5f, -44.25f + 0.05f, 8.0f + 0.30f};
            float fpB[3] = {0.0f, 0.0f, 0.0f};
            const bool okB = Halo3ComputeFingerprint(b, centerB, fpB);
            const bool invariant = okA && okB &&
                Halo3FingerprintDistance(fpA, node0) < 1e-4f &&
                Halo3FingerprintDistance(fpB, node0) < 1e-4f;

            // A degenerate frame must refuse rather than emit junk.
            Halo3Frame bad = a;
            bad.up[2] = 0.0f;
            float ignored[3];
            const bool refuses = !Halo3ComputeFingerprint(bad, centerA, ignored);

            // Uniqueness: close winner with a distant runner-up identifies;
            // two close candidates are ambiguous and must produce no point;
            // a lone candidate that is simply too far also fails.
            const bool picks = Halo3FingerprintIsUnique(0.02f, 0.55f);
            const bool ambiguous = !Halo3FingerprintIsUnique(0.02f, 0.08f);
            const bool tooFar = !Halo3FingerprintIsUnique(0.40f, 9.0f);
            const bool loneOk = Halo3FingerprintIsUnique(
                0.01f, std::numeric_limits<float>::infinity());

            Check(invariant && refuses && picks && ambiguous && tooFar &&
                  loneOk,
                "Bounding-center fingerprint is pose-invariant and refuses to "
                "identify a vehicle when two candidates are both plausible");
        }

        // C18 interpolated node anchoring. The default inverse converts the
        // Blender/tag-space camera point to node-local once; the live node
        // returns it to world space. Rest pose must therefore preserve the
        // authored point exactly, while animation belongs entirely to the node.
        {
            Halo3Matrix4x3 defaultNode{};
            defaultNode.scale = 2.0f;
            defaultNode.forward[1] = 1.0f;
            defaultNode.left[0] = -1.0f;
            defaultNode.up[2] = 1.0f;
            defaultNode.position[0] = 10.0f;
            defaultNode.position[1] = -3.0f;
            defaultNode.position[2] = 2.0f;

            // Exact affine inverse of defaultNode, in the same 0x34 layout.
            Halo3Matrix4x3 defaultInverse{};
            defaultInverse.scale = 0.5f;
            defaultInverse.forward[1] = -1.0f;
            defaultInverse.left[0] = 1.0f;
            defaultInverse.up[2] = 1.0f;
            defaultInverse.position[0] = 1.5f;
            defaultInverse.position[1] = 5.0f;
            defaultInverse.position[2] = -1.0f;

            const float authored[3] = {9.64f, -1.76f, 3.62f};
            const float expectedNodeLocal[3] = {0.62f, 0.18f, 0.81f};
            float recoveredLocal[3] = {};
            float restAnchor[3] = {};
            const bool restOk =
                Halo3MatrixInverseTransformPoint(
                    defaultNode, authored, recoveredLocal) &&
                Halo3ComputeNodeAnchoredPoint(
                    defaultInverse, defaultNode, authored, restAnchor);
            const bool restExact =
                std::fabs(recoveredLocal[0] - expectedNodeLocal[0]) < 1e-5f &&
                std::fabs(recoveredLocal[1] - expectedNodeLocal[1]) < 1e-5f &&
                std::fabs(recoveredLocal[2] - expectedNodeLocal[2]) < 1e-5f &&
                std::fabs(restAnchor[0] - authored[0]) < 1e-5f &&
                std::fabs(restAnchor[1] - authored[1]) < 1e-5f &&
                std::fabs(restAnchor[2] - authored[2]) < 1e-5f;

            // The live node translates and rotates 180 degrees relative to the
            // rest node; no object-root interpolation participates.
            Halo3Matrix4x3 liveNode{};
            liveNode.scale = 2.0f;
            liveNode.forward[1] = -1.0f;
            liveNode.left[0] = 1.0f;
            liveNode.up[2] = 1.0f;
            liveNode.position[0] = -4.0f;
            liveNode.position[1] = 12.0f;
            liveNode.position[2] = 1.0f;
            float animatedAnchor[3] = {};
            const bool animatedOk = Halo3ComputeNodeAnchoredPoint(
                defaultInverse, liveNode, authored, animatedAnchor);
            const bool animationFollowed =
                std::fabs(animatedAnchor[0] - (-3.64f)) < 1e-5f &&
                std::fabs(animatedAnchor[1] - 10.76f) < 1e-5f &&
                std::fabs(animatedAnchor[2] - 2.62f) < 1e-5f;

            // Head translation is latched in this same node space. On latch it
            // contributes zero; subsequent head-local delta passes through
            // exactly while the authored baseline remains unchanged.
            const float headReference[3] = {0.10f, -0.05f, 0.20f};
            float latchHeadWorld[3] = {};
            const bool latchHeadOk = Halo3MatrixTransformPoint(
                liveNode, headReference, latchHeadWorld);
            float latchCamera[3] = {};
            const bool latchCameraOk = Halo3ComputeHeadParentedPoint(
                liveNode, latchHeadWorld, headReference, expectedNodeLocal, 1.0f,
                latchCamera);
            const bool latchExact =
                std::fabs(latchCamera[0] - animatedAnchor[0]) < 1e-5f &&
                std::fabs(latchCamera[1] - animatedAnchor[1]) < 1e-5f &&
                std::fabs(latchCamera[2] - animatedAnchor[2]) < 1e-5f;

            const float movedHeadLocal[3] = {0.16f, -0.07f, 0.24f};
            float movedHeadWorld[3] = {};
            const bool movedHeadOk = Halo3MatrixTransformPoint(
                liveNode, movedHeadLocal, movedHeadWorld);
            float movedCamera[3] = {};
            const bool movedCameraOk = Halo3ComputeHeadParentedPoint(
                liveNode, movedHeadWorld, headReference, expectedNodeLocal, 1.0f,
                movedCamera);
            const bool headMotionFollowed =
                std::fabs(movedCamera[0] - (-3.68f)) < 1e-5f &&
                std::fabs(movedCamera[1] - 10.64f) < 1e-5f &&
                std::fabs(movedCamera[2] - 2.70f) < 1e-5f;

            // Changing the live rendered node after H0 is frozen must not
            // reintroduce an object/root parent or preserve any old world-space
            // offset. Both the authored point and head delta move exactly once
            // through the new node.
            Halo3Matrix4x3 laterNode{};
            laterNode.scale = 1.5f;
            laterNode.forward[1] = 1.0f;
            laterNode.left[0] = -1.0f;
            laterNode.up[2] = 1.0f;
            laterNode.position[0] = 5.0f;
            laterNode.position[1] = -2.0f;
            laterNode.position[2] = 7.0f;
            float laterHeadWorld[3] = {};
            float laterCamera[3] = {};
            const bool laterNodeOk =
                Halo3MatrixTransformPoint(
                    laterNode, movedHeadLocal, laterHeadWorld) &&
                Halo3ComputeHeadParentedPoint(
                    laterNode, laterHeadWorld, headReference,
                    expectedNodeLocal, 1.0f, laterCamera) &&
                std::fabs(laterCamera[0] - 4.76f) < 1e-5f &&
                std::fabs(laterCamera[1] - (-0.98f)) < 1e-5f &&
                std::fabs(laterCamera[2] - 8.275f) < 1e-5f;

            // A frame-count debounce froze H0 during the boarding animation.
            // The C18 latch instead needs both concrete-seat age and a
            // continuous node-local quiet window. A reset (the action taken on
            // a concrete seat switch) must start both clocks again.
            Halo3HeadSettleLatch settle;
            const float enteringHead[3] = {0.10f, -0.05f, 0.70f};
            const float seatedHead[3] = {0.10f, -0.05f, 0.20f};
            const bool settleHeldInitial =
                !settle.Update(1000, enteringHead) &&
                !settle.Update(2200, seatedHead) &&
                !settle.Update(2699, seatedHead);
            const bool settleLatched =
                settle.Update(2700, seatedHead) && settle.valid &&
                std::fabs(settle.reference[0] - seatedHead[0]) < 1e-6f &&
                std::fabs(settle.reference[1] - seatedHead[1]) < 1e-6f &&
                std::fabs(settle.reference[2] - seatedHead[2]) < 1e-6f;
            const float safeBounce[3] = {
                seatedHead[0] + 0.079f, seatedHead[1], seatedHead[2]};
            const float animationJump[3] = {
                seatedHead[0] + 0.081f, seatedHead[1], seatedHead[2]};
            const bool headDeltaBounded =
                Halo3HeadLocalDeltaWithinLimit(
                    safeBounce, settle.reference) &&
                !Halo3HeadLocalDeltaWithinLimit(
                    animationJump, settle.reference);

            // C22: the same boundary, but the contribution SATURATES there
            // rather than dropping to zero. Inside the limit the clamp must be
            // the identity, so it agrees exactly with the old gate; outside it
            // must land exactly ON the limit, which makes the function
            // continuous and the camera unable to step.
            float insideDelta[3] = {}, outsideDelta[3] = {}, edgeDelta[3] = {};
            const float farAnimation[3] = {
                seatedHead[0] + 3.0f, seatedHead[1] - 4.0f, seatedHead[2]};
            const float atEdge[3] = {
                seatedHead[0] + kHalo3HeadMaximumLocalDelta, seatedHead[1],
                seatedHead[2]};
            const bool clampIdentityInside =
                Halo3ClampedHeadLocalDelta(
                    safeBounce, settle.reference, 1.0f, insideDelta) &&
                std::fabs(insideDelta[0] - 0.079f) < 1e-6f &&
                std::fabs(insideDelta[1]) < 1e-6f &&
                std::fabs(insideDelta[2]) < 1e-6f;
            // 3/-4/0 has length 5; clamped it must be the same direction with
            // length exactly kHalo3HeadMaximumLocalDelta.
            const bool clampSaturates =
                Halo3ClampedHeadLocalDelta(
                    farAnimation, settle.reference, 1.0f, outsideDelta) &&
                std::fabs(outsideDelta[0] -
                          kHalo3HeadMaximumLocalDelta * 0.6f) < 1e-6f &&
                std::fabs(outsideDelta[1] +
                          kHalo3HeadMaximumLocalDelta * 0.8f) < 1e-6f &&
                std::fabs(outsideDelta[2]) < 1e-6f;
            // Continuity at the boundary is the whole point: approaching the
            // limit from inside and sitting exactly on it must agree, so no
            // amount of head movement can produce a step.
            const float justInside[3] = {
                seatedHead[0] + kHalo3HeadMaximumLocalDelta - 1e-5f,
                seatedHead[1], seatedHead[2]};
            float justInsideDelta[3] = {};
            const bool clampContinuous =
                Halo3ClampedHeadLocalDelta(
                    atEdge, settle.reference, 1.0f, edgeDelta) &&
                Halo3ClampedHeadLocalDelta(
                    justInside, settle.reference, 1.0f, justInsideDelta) &&
                std::fabs(edgeDelta[0] - kHalo3HeadMaximumLocalDelta) < 1e-6f &&
                std::fabs(edgeDelta[0] - justInsideDelta[0]) < 1e-4f;
            // A non-finite sample must refuse rather than emit a partial or
            // stale delta, so the caller keeps the exact authored point.
            const float brokenHead[3] = {
                std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
            float brokenDelta[3] = {};
            const bool clampRefusesBroken =
                !Halo3ClampedHeadLocalDelta(
                    brokenHead, settle.reference, 1.0f, brokenDelta) &&
                !Halo3ClampedHeadLocalDelta(
                    safeBounce, settle.reference, 1.0f, nullptr);
            // C23: gain is a plain strength scale on the clamped result. Half
            // gain must be exactly half the travel, zero must bolt the view to
            // the seat, and an out-of-range or non-finite gain must not be able
            // to amplify the bounce past its full setting.
            float halfDelta[3] = {}, zeroDelta[3] = {}, overDelta[3] = {};
            float nanGainDelta[3] = {};
            const bool gainScales =
                Halo3ClampedHeadLocalDelta(
                    safeBounce, settle.reference, 0.5f, halfDelta) &&
                std::fabs(halfDelta[0] - 0.0395f) < 1e-6f &&
                Halo3ClampedHeadLocalDelta(
                    safeBounce, settle.reference, 0.0f, zeroDelta) &&
                std::fabs(zeroDelta[0]) < 1e-9f &&
                std::fabs(zeroDelta[1]) < 1e-9f &&
                std::fabs(zeroDelta[2]) < 1e-9f;
            const bool gainBounded =
                Halo3ClampedHeadLocalDelta(
                    farAnimation, settle.reference, 5.0f, overDelta) &&
                std::fabs(overDelta[0] -
                          kHalo3HeadMaximumLocalDelta * 0.6f) < 1e-6f &&
                !Halo3ClampedHeadLocalDelta(
                    safeBounce, settle.reference,
                    std::numeric_limits<float>::quiet_NaN(), nanGainDelta);
            Check(clampIdentityInside && clampSaturates && clampContinuous &&
                  clampRefusesBroken && gainScales && gainBounded,
                "The occupant-head contribution is the identity inside its "
                "limit, saturates exactly on it, is continuous across it, "
                "scales linearly with the bounce strength without exceeding "
                "full travel, and refuses a non-finite sample or gain");
            settle = {};
            const bool seatSwitchRestarts =
                !settle.Update(2701, seatedHead) && !settle.valid &&
                settle.observedSinceMs == 2701;

            Halo3SeatPositionKey positionKey;
            const bool positionKeyStartsEmpty =
                !positionKey.Matches(
                    7, 0x12340002, 0,
                    static_cast<uint32_t>(Halo3VehicleId::Warthog), false);
            positionKey.Set(
                7, 0x12340002, 0,
                static_cast<uint32_t>(Halo3VehicleId::Warthog), false);
            const bool positionKeyConcrete =
                positionKey.Matches(
                    7, 0x12340002, 0,
                    static_cast<uint32_t>(Halo3VehicleId::Warthog), false) &&
                !positionKey.Matches(
                    8, 0x12340002, 0,
                    static_cast<uint32_t>(Halo3VehicleId::Warthog), false) &&
                !positionKey.Matches(
                    7, 0x12340003, 0,
                    static_cast<uint32_t>(Halo3VehicleId::Warthog), false) &&
                !positionKey.Matches(
                    7, 0x12340002, 1,
                    static_cast<uint32_t>(Halo3VehicleId::Warthog), false) &&
                !positionKey.Matches(
                    7, 0x12340002, 0,
                    static_cast<uint32_t>(Halo3VehicleId::Mongoose), false) &&
                !positionKey.Matches(
                    7, 0x12340002, 0,
                    static_cast<uint32_t>(Halo3VehicleId::Warthog), true);

            // Actual Warthog hull node fixture from the official exported
            // render-model tag. This pins the scale-first +0x28 interpretation
            // and proves a configured vehicle-local placement trim survives
            // D^-1 -> D without changing axes or being applied twice.
            Halo3Matrix4x3 hogDefault{};
            hogDefault.scale = 1.0f;
            hogDefault.forward[0] = 1.0f;
            hogDefault.left[1] = 1.0f;
            hogDefault.up[2] = 1.0f;
            hogDefault.position[0] = -0.0250015f;
            hogDefault.position[1] = -7.03e-10f;
            hogDefault.position[2] = 0.42f;
            Halo3Matrix4x3 hogInverse{};
            hogInverse.scale = 1.0f;
            hogInverse.forward[0] = 1.0f;
            hogInverse.left[1] = 1.0f;
            hogInverse.up[2] = 1.0f;
            hogInverse.position[0] = 0.0250015f;
            hogInverse.position[1] = 7.03e-10f;
            hogInverse.position[2] = -0.42f;
            const float hogAdjustedPoint[3] = {
                0.0299f + 0.0328084f, 0.1683f - 0.0492126f,
                0.6663f + 0.0164042f};
            float hogRestAnchor[3] = {};
            const bool actualTagFixture = Halo3ComputeNodeAnchoredPoint(
                hogInverse, hogDefault, hogAdjustedPoint, hogRestAnchor) &&
                std::fabs(hogRestAnchor[0] - hogAdjustedPoint[0]) < 1e-6f &&
                std::fabs(hogRestAnchor[1] - hogAdjustedPoint[1]) < 1e-6f &&
                std::fabs(hogRestAnchor[2] - hogAdjustedPoint[2]) < 1e-6f;

            // Every public helper refuses torn/non-finite matrices and points.
            Halo3Matrix4x3 nonfinite = liveNode;
            nonfinite.position[1] = std::numeric_limits<float>::quiet_NaN();
            Halo3Matrix4x3 zeroScale = liveNode;
            zeroScale.scale = 0.0f;
            Halo3Matrix4x3 badBasis = liveNode;
            badBasis.left[0] = 0.0f;
            badBasis.left[1] = -1.0f; // duplicates forward
            const float nonfinitePoint[3] = {
                0.0f, std::numeric_limits<float>::infinity(), 0.0f};
            float rejected[3] = {91.0f, 92.0f, 93.0f};
            const bool invalidRejected =
                Halo3MatrixCountFromByteSize(0x34) == 1 &&
                Halo3MatrixCountFromByteSize(0x68) == 2 &&
                Halo3MatrixCountFromByteSize(0x35) == 0 &&
                Halo3MatrixCountFromByteSize(0) == 0 &&
                !Halo3MatrixValid(nonfinite) &&
                !Halo3MatrixValid(zeroScale) &&
                !Halo3MatrixValid(badBasis) &&
                !Halo3MatrixTransformPoint(nonfinite, authored, rejected) &&
                !Halo3MatrixInverseTransformPoint(
                    zeroScale, authored, rejected) &&
                !Halo3ComputeNodeAnchoredPoint(
                    defaultInverse, badBasis, authored, rejected) &&
                !Halo3ComputeHeadParentedPoint(
                    liveNode, nonfinitePoint, headReference,
                    expectedNodeLocal, 1.0f, rejected) &&
                rejected[0] == 91.0f && rejected[1] == 92.0f &&
                rejected[2] == 93.0f;

            Check(restOk && restExact && animatedOk && animationFollowed &&
                    latchHeadOk && latchCameraOk && latchExact && movedHeadOk &&
                    movedCameraOk && headMotionFollowed && laterNodeOk &&
                    settleHeldInitial && settleLatched && headDeltaBounded &&
                    seatSwitchRestarts && positionKeyStartsEmpty &&
                    positionKeyConcrete && actualTagFixture && invalidRejected,
                "Halo 3 live node anchoring preserves the exact authored point, "
                "waits out entry motion, inherits mesh/head-local motion once, "
                "and rejects invalid data");
        }

        // C10 frame smoothing. MEASURED: the camera samples at 240/sec while
        // the hull only moves at 60, so three of every four published frames
        // must be walked toward the next simulation state instead of repeating
        // the last one. The output has to be continuous across a tick boundary
        // (that discontinuity IS the reported shaking), must never overshoot,
        // and must snap rather than slide on a teleport.
        {
            auto frameAt = [](float x, float yawDeg) {
                Halo3Frame f{};
                f.pos[0] = x; f.pos[1] = 0.0f; f.pos[2] = 0.0f;
                const float r = yawDeg / 57.2957795f;
                f.fwd[0] = std::cos(r); f.fwd[1] = std::sin(r); f.fwd[2] = 0.0f;
                f.up[0] = 0.0f; f.up[1] = 0.0f; f.up[2] = 1.0f;
                return f;
            };
            // 60 Hz simulation, 240 Hz camera: the hull advances 0.1 wu and 2
            // degrees per tick and is sampled four times per tick.
            Halo3FrameInterp interp;
            const double dt = 1.0 / 240.0;
            double now = 0.0;
            bool smooth = true;
            bool ordered = true;
            float lastX = 0.0f;
            float biggestStep = 0.0f;
            for (int tick = 0; tick < 30; ++tick)
            {
                const Halo3Frame raw = frameAt(tick * 0.1f, tick * 2.0f);
                for (int sub = 0; sub < 4; ++sub)
                {
                    const Halo3Frame out =
                        Halo3InterpolateFrame(interp, raw, now, true);
                    now += dt;
                    if (tick < 3)   // the tick length is still being learned
                    {
                        lastX = out.pos[0];
                        continue;
                    }
                    const float step = out.pos[0] - lastX;
                    if (step < -1e-4f)
                        ordered = false;               // never runs backwards
                    if (step > biggestStep)
                        biggestStep = step;
                    lastX = out.pos[0];
                    if (!Halo3FrameOrthonormal(out))
                        smooth = false;                // blended basis is real
                }
            }
            // A raw feed would stand still for three frames then jump the whole
            // 0.1 wu. Smoothed, no single frame may move much more than the
            // even quarter-tick share of it.
            const bool evened = biggestStep < 0.045f && ordered;
            // At rest the output settles exactly on the newest state. C15
            // makes that settling gradual on purpose — when ticks stop, the
            // extrapolation authority glides away over ~40 ms rather than
            // snapping the seat back — so allow it that time.
            Halo3Frame settled{};
            for (int i = 0; i < 200; ++i)
            {
                settled = Halo3InterpolateFrame(
                    interp, frameAt(29 * 0.1f, 29 * 2.0f), now, true);
                now += dt;
            }
            const bool settles = std::fabs(settled.pos[0] - 2.9f) < 1e-4f;
            // A teleport snaps. Sliding a camera across a level would put the
            // player's head inside the geometry between the two ends.
            const Halo3Frame teleported = frameAt(400.0f, 58.0f);
            const Halo3Frame jumped =
                Halo3InterpolateFrame(interp, teleported, now + dt, true);
            const bool snaps = std::fabs(jumped.pos[0] - 400.0f) < 1e-3f;
            // Switched off, the published frame is exactly the raw one.
            Halo3FrameInterp off;
            const Halo3Frame passthrough = Halo3InterpolateFrame(
                off, frameAt(7.0f, 10.0f), now, false);
            const bool bypasses = passthrough.pos[0] == 7.0f && !off.valid;
            // Blending two orientations component-wise is not a rotation; the
            // result must still be a usable basis.
            const Halo3Frame mid =
                Halo3LerpFrame(frameAt(0.0f, 0.0f), frameAt(1.0f, 40.0f), 0.5f);
            const bool basisOk = Halo3FrameOrthonormal(mid) &&
                std::fabs(std::atan2(mid.fwd[1], mid.fwd[0]) * 57.2957795f -
                          20.0f) < 1.0f;

            // The residual the C10 video exposed. A tick is only NOTICED on
            // the first camera call after it happens, which is up to a whole
            // camera interval late and a different amount late every time. If
            // the blend restarts at zero on each detection, that jitter lands
            // straight on the camera: measured in the headset as the vehicle
            // still moving on 49% of frames whose world was stationary. With
            // the hull travelling at a constant speed, every published frame
            // must advance by the same amount no matter when the tick was
            // noticed.
            {
                Halo3FrameInterp jit;
                const double camDt = 1.0 / 240.0;
                const double tickLen = 1.0 / 60.0;
                double t = 0.0;
                double simTime = 0.0;
                int simTicks = 0;
                Halo3Frame held = frameAt(0.0f, 0.0f);
                float previousX = 0.0f;
                bool first = true;
                double worst = 0.0, total = 0.0;
                int counted = 0;
                // Camera and simulation on independent clocks: the camera runs
                // a hair off 240 Hz, so the detection lag walks through the
                // whole range instead of sitting still.
                for (int i = 0; i < 2000; ++i)
                {
                    t += camDt * 1.017;
                    while (simTime + tickLen <= t)
                    {
                        simTime += tickLen;
                        ++simTicks;
                        held = frameAt(simTicks * 0.1f, 0.0f);
                    }
                    const Halo3Frame out =
                        Halo3InterpolateFrame(jit, held, t, true);
                    if (i > 600)   // let the phase settle
                    {
                        if (!first)
                        {
                            const double d = out.pos[0] - previousX;
                            total += d;
                            ++counted;
                            const double ideal = 0.1 * (camDt * 1.017) / tickLen;
                            const double err = std::fabs(d - ideal);
                            if (err > worst) worst = err;
                        }
                        first = false;
                    }
                    previousX = out.pos[0];
                }
                const double ideal = 0.1 * (camDt * 1.017) / tickLen;
                // Worst single-frame deviation under a quarter of the even
                // share. Restarting the blend at each detection puts it at a
                // full share or more.
                const bool locked = counted > 1000 && worst < ideal * 0.25 &&
                    std::fabs(total / counted - ideal) < ideal * 0.05;
                Check(locked,
                    "Seat frame advances evenly even though each simulation "
                    "tick is noticed a different amount late");
            }

            Check(smooth && evened && settles && snaps && bypasses && basisOk,
                "Seat frame is walked across the 60Hz simulation tick, settles "
                "on the newest state, snaps on a teleport, and stays a real "
                "orthonormal basis");
        }

        // C14 photon-time prediction — the real content of "my headset is 120
        // and 60hz is not enough" (user, 2026-07-31, after their log showed
        // the C12 bounding-centre witness reporting "bounds not render-smooth,
        // C11 pacing, render lead +0.00 ticks" on real hardware: that
        // mechanism never measured anything, so it is now diagnostic-only).
        //
        // Every frame is DISPLAYED one refresh period after it is built, and
        // OpenXR already predicts the head that far ahead. The seat must be
        // predicted by the same interval or it trails the head, worse the
        // faster the vehicle moves.
        {
            auto hullAt = [](float x) {
                Halo3Frame f{};
                f.pos[0] = x;
                f.fwd[0] = 1.0f;
                f.up[2] = 1.0f;
                return f;
            };
            const double tickLen = 1.0 / 60.0;
            const float stepWu = 0.1f;       // wu per tick = wu per 16.7 ms


            // C16: the seat is PARENTED to the car. With smoothing off — the
            // default — every published frame is EXACTLY the simulation's own
            // hull state, so the seat is rigidly bolted to the car and you
            // feel its physics unaltered.
            {
                Halo3FrameInterp rigid;
                const double camDt = 1.0 / 251.0;
                double t = 0.0, simTime = 0.0;
                int ticks = 0;
                Halo3Frame held = hullAt(0.0f);
                bool exact = true;
                for (int i = 0; i < 2000; ++i)
                {
                    while (simTime + tickLen <= t)
                    {
                        simTime += tickLen; ++ticks;
                        held = hullAt(static_cast<float>(ticks * 0.30));
                    }
                    const Halo3Frame out =
                        Halo3InterpolateFrame(rigid, held, t, false);
                    if (out.pos[0] != held.pos[0])
                        exact = false;
                    t += camDt;
                }
                Check(exact && !rigid.valid,
                    "With smoothing off the seat is bolted to the car: every "
                    "published frame is exactly the simulation's own hull "
                    "state, physics and all");
            }

            // With smoothing on, the blend must stay strictly BETWEEN the two
            // most recent simulation states. Everything that used to live past
            // the newest state was removed: a bounded blend that is held at a
            // ceiling while the hull keeps turning loses that rotation for
            // good, which is what let the car rotate out from under the
            // camera ("i was able to drift the camera completely around").
            {
                Halo3FrameInterp interp;
                const double camDt = 1.0 / 251.0;
                const double boostStep = 0.30;
                double t = 0.0, simTime = 0.0;
                int ticks = 0;
                Halo3Frame held = hullAt(0.0f);
                bool bounded = true, ordered = true;
                float last = 0.0f;
                bool have = false;
                for (int i = 0; i < 4000; ++i)
                {
                    while (simTime + tickLen <= t)
                    {
                        simTime += tickLen; ++ticks;
                        held = hullAt(static_cast<float>(ticks * boostStep));
                    }
                    const Halo3Frame out =
                        Halo3InterpolateFrame(interp, held, t, true);
                    if (i > 600)
                    {
                        // Never past the newest state, never behind the one
                        // before it.
                        const double newest = ticks * boostStep;
                        if (out.pos[0] > newest + 1e-4 ||
                            out.pos[0] < newest - boostStep - 1e-4)
                            bounded = false;
                        if (have && out.pos[0] < last - 1e-4f)
                            ordered = false;
                    }
                    last = out.pos[0];
                    have = true;
                    t += camDt;
                }
                Check(bounded && ordered && interp.lastAlpha <= 1.0f,
                    "Smoothed, the seat stays strictly between the two most "
                    "recent simulation states and never runs backwards - "
                    "nothing extrapolates past the car any more");
            }

            // The view-follow integrates the heading step it is handed, so it
            // must be fed the RAW hull heading. Driving a full circle must
            // fold in exactly one full turn: any rotation the source fails to
            // deliver is lost for good and reads as the car slowly rotating
            // out from under the camera.
            {
                Halo3SeatYaw yaw;
                float fwd[3] = {1.0f, 0.0f, 0.0f};
                double total = 0.0;
                const int steps = 720;
                for (int i = 0; i <= steps; ++i)
                {
                    const double a = 6.283185307 * i / steps;
                    fwd[0] = static_cast<float>(std::cos(a));
                    fwd[1] = static_cast<float>(std::sin(a));
                    total += Halo3SeatYawDelta(yaw, fwd, true);
                }
                Check(std::fabs(total - 6.283185307) < 1e-3,
                    "A full circle folds exactly one full turn into the view "
                    "reference, so the car can never rotate out from under "
                    "the camera");
            }
            // The C11 guarantee: an evenly advancing hull must produce evenly
            // advancing frames even though each tick is noticed a different
            // amount late.
            {
                Halo3FrameInterp jit;
                const double camDt = 1.0 / 240.0;
                double t = 0.0, simTime = 0.0;
                int simTicks = 0;
                Halo3Frame held = hullAt(0.0f);
                float previousX = 0.0f;
                bool first = true;
                double worst = 0.0, total = 0.0;
                int counted = 0;
                for (int i = 0; i < 2000; ++i)
                {
                    t += camDt * 1.017;
                    while (simTime + tickLen <= t)
                    {
                        simTime += tickLen;
                        ++simTicks;
                        held = hullAt(simTicks * 0.1f);
                    }
                    const Halo3Frame out =
                        Halo3InterpolateFrame(jit, held, t, true);
                    if (i > 600)
                    {
                        if (!first)
                        {
                            const double d = out.pos[0] - previousX;
                            total += d;
                            ++counted;
                            const double ideal =
                                0.1 * (camDt * 1.017) / tickLen;
                            const double err = std::fabs(d - ideal);
                            if (err > worst) worst = err;
                        }
                        first = false;
                    }
                    previousX = out.pos[0];
                }
                const double ideal = 0.1 * (camDt * 1.017) / tickLen;
                Check(counted > 1000 && worst < ideal * 0.25 &&
                          std::fabs(total / counted - ideal) < ideal * 0.05,
                    "Seat frame advances evenly even though each simulation "
                    "tick is noticed a different amount late");
            }
        }

        // C9 seat yaw: the view must turn with the hull. The function returns
        // the yaw STEP to fold into the shared reference, so what matters is
        // that the accumulated steps equal the hull's rotation, that entry and
        // exit re-reference instead of snapping, and that no single frame can
        // whip the view.
        {
            auto hullFwd = [](float yaw, float out[3]) {
                out[0] = std::cos(yaw); out[1] = std::sin(yaw); out[2] = 0.0f;
            };
            float fwd[3];
            Halo3SeatYaw seat;
            hullFwd(1.0f, fwd);
            const bool idleZero =
                Halo3SeatYawDelta(seat, fwd, false) == 0.0f &&
                !seat.armed;
            // Sitting down at heading 1.0 rad: this heading is straight ahead.
            const bool entryZero =
                Halo3SeatYawDelta(seat, fwd, true) == 0.0f && seat.armed;
            // Hull swings 20 deg left -> the view follows by exactly that.
            hullFwd(1.0f + 0.34906585f, fwd);
            const bool followsHull = std::fabs(
                Halo3SeatYawDelta(seat, fwd, true) - 0.34906585f) < 1e-4f;
            // Getting out re-references: sitting in a car facing the other way
            // must read straight ahead again, not 180 degrees out.
            Halo3SeatYawDelta(seat, fwd, false);
            hullFwd(-2.5f, fwd);
            const bool reentryZero = !seat.armed &&
                Halo3SeatYawDelta(seat, fwd, true) == 0.0f;

            // Two full left turns at full weight, three degrees at a time: the
            // steps sum to the hull's whole rotation and none of them is a
            // jump. This is the wrap test — a wrapped TOTAL scaled by weight
            // would snap by 2*pi*weight every time the hull passed +/-pi.
            Halo3SeatYaw spin;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(spin, fwd, true);
            bool fullOk = true;
            double sum = 0.0;
            for (int i = 1; i <= 240; ++i)
            {
                hullFwd(Halo3WrapPi(static_cast<float>(i) * 0.05236f), fwd);
                const float step = Halo3SeatYawDelta(spin, fwd, true);
                if (std::fabs(step) > 0.1f)
                    fullOk = false;
                sum += step;
            }
            fullOk = fullOk && std::fabs(sum - 240.0 * 0.05236) < 1e-2;
            // Toggle-off is represented by the caller making the transaction
            // inactive. It clears the old reference, so turning it back on in
            // the current seat establishes a fresh zero without a snap.
            Halo3SeatYaw off;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(off, fwd, true);
            hullFwd(0.3f, fwd);
            const bool toggleOff =
                Halo3SeatYawDelta(off, fwd, false) == 0.0f && !off.armed &&
                Halo3SeatYawDelta(off, fwd, true) == 0.0f && off.armed;

            // This is the user-reported collision/spin regression. A legitimate
            // same-car 2.6-rad step must be consumed in full; the old 0.60-rad
            // cap discarded it and left the car permanently rotated under the
            // view. A concrete seat switch is reset explicitly instead.
            Halo3SeatYaw collision;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(collision, fwd, true);
            hullFwd(2.6f, fwd);
            const bool collisionFollowed = std::fabs(
                Halo3SeatYawDelta(collision, fwd, true) - 2.6f) < 1e-4f;
            hullFwd(2.7f, fwd);
            const bool continuesAfterCollision = std::fabs(
                Halo3SeatYawDelta(collision, fwd, true) - 0.1f) < 1e-4f;
            Halo3SeatYaw switched;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(switched, fwd, true);
            Halo3SeatYawDelta(switched, fwd, false);
            hullFwd(2.6f, fwd);
            const bool seatSwitchRearmed =
                Halo3SeatYawDelta(switched, fwd, true) == 0.0f;

            // A Banshee pulled through the vertical has no heading at all;
            // hold the last valid yaw, then catch up when heading exists again.
            Halo3SeatYaw vertical;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(vertical, fwd, true);
            float nose[3] = {0.02f, 0.0f, 0.9998f};
            const bool verticalHeld =
                Halo3SeatYawDelta(vertical, nose, true) == 0.0f &&
                vertical.armed;
            hullFwd(0.9f, fwd);
            const bool verticalCaughtUp = std::fabs(
                Halo3SeatYawDelta(vertical, fwd, true) - 0.9f) < 1e-4f;

            Halo3SeatYaw junk;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(junk, fwd, true);
            float bad[3] = {std::nanf(""), 0.0f, 0.0f};
            const bool junkSafe =
                Halo3SeatYawDelta(junk, bad, true) == 0.0f && junk.armed;
            hullFwd(0.5f, fwd);
            const bool junkCaughtUp = std::fabs(
                Halo3SeatYawDelta(junk, fwd, true) - 0.5f) < 1e-4f;

            Check(idleZero && entryZero && followsHull && reentryZero &&
                  fullOk && toggleOff && collisionFollowed &&
                  continuesAfterCollision && seatSwitchRearmed &&
                  verticalHeld && verticalCaughtUp && junkSafe &&
                  junkCaughtUp,
                "Seat yaw follows every same-car collision/spin step, resets "
                "only at an explicit ownership boundary, and catches up after "
                "invalid or degenerate samples");
        }

        // C25: tracked poses compose inside roll-free vehicle yaw+pitch.
        {
            const auto approx = [](float a, float b) {
                return std::fabs(a - b) <= 1.0e-4f;
            };
            const float hill = 20.0f / 57.2957795f;
            float nose[3] = {std::cos(hill), 0.0f, std::sin(hill)};
            float pitch = 0.0f;
            float badNose[3] = {
                std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
            const bool pitchSafe =
                Halo3RollStablePitchFromForward(nose, pitch) &&
                approx(pitch, hill) &&
                !Halo3RollStablePitchFromForward(badNose, pitch);
            float hillBasis[9], sideBasis[9];
            const bool hillSafe =
                Halo3ComposeRollStableFollowBasis(
                    0.0f, hill, 0.0f, 0.0f, 0.0f, 0.0f, hillBasis) &&
                approx(hillBasis[0], std::cos(hill)) &&
                approx(hillBasis[2], std::sin(hill)) &&
                approx(hillBasis[6], -std::sin(hill)) &&
                approx(hillBasis[8], std::cos(hill));
            // A 90-degree local look points across the hill, not uphill.
            const bool sideSafe =
                Halo3ComposeRollStableFollowBasis(
                    0.0f, hill, 0.0f, 1.5707963268f, 0.0f, 0.0f,
                    sideBasis) &&
                approx(sideBasis[0], 0.0f) &&
                approx(sideBasis[1], 1.0f) &&
                approx(sideBasis[2], 0.0f);
            float composedAim[9], recoveredYaw = 0.0f,
                recoveredPitch = 0.0f;
            constexpr float aimHullYaw = 0.7f;
            constexpr float aimHullPitch = 0.25f;
            constexpr float aimGameYawRef = 1.1f;
            constexpr float aimTrackedYaw = -0.4f;
            constexpr float aimTrackedPitch = 0.2f;
            const bool inverseFollowSafe =
                Halo3ComposeRollStableFollowBasis(
                    aimHullYaw, aimHullPitch, aimGameYawRef,
                    aimTrackedYaw, aimTrackedPitch, 0.0f, composedAim) &&
                Halo3InverseRollStableFollowForward(
                    composedAim, aimHullYaw, aimHullPitch, aimGameYawRef,
                    recoveredYaw, recoveredPitch) &&
                approx(recoveredYaw, aimTrackedYaw) &&
                approx(recoveredPitch, aimTrackedPitch);
            float flatAim[9], flatYaw = 0.0f, flatPitch = 0.0f;
            constexpr float flatHullYaw = -0.3f;
            constexpr float flatGameYawRef = 0.4f;
            constexpr float flatTrackedYaw = 0.55f;
            constexpr float flatTrackedPitch = -0.25f;
            const bool inverseFlatSafe =
                Halo3ComposeRollStableFollowBasis(
                    flatHullYaw, 0.0f, flatGameYawRef, flatTrackedYaw,
                    flatTrackedPitch, 0.0f, flatAim) &&
                Halo3InverseRollStableFollowForward(
                    flatAim, flatHullYaw, 0.0f, flatGameYawRef,
                    flatYaw, flatPitch) &&
                approx(flatYaw, flatTrackedYaw) &&
                approx(flatPitch, flatTrackedPitch);
            const float zeroAim[3] = {};
            const bool inverseRejectsZero =
                !Halo3InverseRollStableFollowForward(
                    zeroAim, 0.0f, 0.0f, 0.0f,
                    recoveredYaw, recoveredPitch);
            const bool inverseRejectsNonfinite =
                !Halo3InverseRollStableFollowForward(
                    badNose, 0.0f, 0.0f, 0.0f,
                    recoveredYaw, recoveredPitch);
            const bool pitchSeatPolicy =
                Halo3SeatFollowsPitch(Halo3VehicleId::Warthog, 0, false) &&
                Halo3SeatFollowsPitch(Halo3VehicleId::Warthog, 1, false) &&
                Halo3SeatFollowsPitch(Halo3VehicleId::Scorpion, 0, false) &&
                !Halo3SeatFollowsPitch(Halo3VehicleId::Hornet, 1, false) &&
                !Halo3SeatFollowsPitch(Halo3VehicleId::Banshee, 0, false) &&
                !Halo3SeatFollowsPitch(Halo3VehicleId::Hornet, 0, false) &&
                !Halo3SeatFollowsPitch(Halo3VehicleId::Unknown, 0, false);
            Check(pitchSafe && hillSafe && sideSafe && pitchSeatPolicy,
                  "Vehicle pitch follow is roll-stable and seat-safe");
            const bool inverseSafe = inverseFollowSafe &&
                inverseFlatSafe && inverseRejectsZero &&
                inverseRejectsNonfinite;
            Check(inverseSafe,
                  "World-space vehicle aim inverts to tracked yaw/pitch");
        }

        // C9 steering ownership. The seat that authors steering must be
        // exactly the seat where the hull-follow reference destroys the closed
        // loop's feedback: a look-steered vehicle's driver, and only while the
        // view actually follows.
        {
            const bool drivers =
                Halo3SeatIsDriver(Halo3VehicleId::Warthog, 0, false) &&
                !Halo3SeatIsDriver(Halo3VehicleId::Warthog, 1, false) &&
                !Halo3SeatIsDriver(Halo3VehicleId::Warthog, 0, true) &&
                !Halo3SeatIsDriver(Halo3VehicleId::StationaryTurret, 0,
                                   false) &&
                !Halo3SeatIsDriver(Halo3VehicleId::Unknown, 0, false);
            // The Scorpion and Wraith steer from the left stick and aim a
            // turret with the right: their closed loop keeps real feedback and
            // must never be taken over.
            const bool families =
                Halo3VehicleIsLookSteered(Halo3VehicleId::Warthog) &&
                Halo3VehicleIsLookSteered(Halo3VehicleId::Chopper) &&
                Halo3VehicleIsLookSteered(Halo3VehicleId::Banshee) &&
                !Halo3VehicleIsLookSteered(Halo3VehicleId::Scorpion) &&
                !Halo3VehicleIsLookSteered(Halo3VehicleId::Wraith) &&
                !Halo3VehicleIsLookSteered(Halo3VehicleId::StationaryTurret) &&
                !Halo3VehicleIsLookSteered(Halo3VehicleId::Unknown);
            // Aircraft are look-steered but take the plain flight stick.
            const bool wheelSeats =
                Halo3VehicleUsesWheel(Halo3VehicleId::Warthog) &&
                Halo3VehicleUsesWheel(Halo3VehicleId::Mongoose) &&
                Halo3VehicleUsesWheel(Halo3VehicleId::Ghost) &&
                Halo3VehicleUsesWheel(Halo3VehicleId::Mauler) &&
                !Halo3VehicleUsesWheel(Halo3VehicleId::Banshee) &&
                !Halo3VehicleUsesWheel(Halo3VehicleId::Hornet) &&
                !Halo3VehicleUsesWheel(Halo3VehicleId::Scorpion);
            const bool gate =
                Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 0, false,
                                         true) &&
                Halo3SeatAuthorsSteering(Halo3VehicleId::Banshee, 0, false,
                                         true) &&
                // Follow off -> the closed loop still has its feedback, so
                // every control stays exactly as it shipped.
                !Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 0, false,
                                           false) &&
                !Halo3SeatAuthorsSteering(Halo3VehicleId::Scorpion, 0, false,
                                           true) &&
                !Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 1, false,
                                           true) &&
                !Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 0, true,
                                           true);
            // The follow itself must never reach a seat whose aim channel
            // turns the very frame being followed while nothing replaces that
            // loop. A walk-up turret is published in its OWN object frame, so
            // it is excluded; a mounted gunner is published in the carrier's,
            // so it is fine.
            const bool follows =
                Halo3SeatFollowsHull(Halo3VehicleId::Warthog, 0, false) &&
                Halo3SeatFollowsHull(Halo3VehicleId::Warthog, 1, false) &&
                Halo3SeatFollowsHull(Halo3VehicleId::Warthog, 0, true) &&
                Halo3SeatFollowsHull(Halo3VehicleId::Scorpion, 0, false) &&
                !Halo3SeatFollowsHull(Halo3VehicleId::StationaryTurret, 0,
                                      false) &&
                !Halo3SeatFollowsHull(Halo3VehicleId::Unknown, 0, false) &&
                // A seat with no authored point keeps the stock chase camera,
                // so there is nothing there for the follow to fix.
                !Halo3SeatFollowsHull(Halo3VehicleId::Mongoose, 4, false);
            // Anything that authors steering must also be following, or the
            // two would disagree about which seat changed behavior.
            const bool consistent =
                Halo3SeatFollowsHull(Halo3VehicleId::Warthog, 0, false) &&
                Halo3SeatFollowsHull(Halo3VehicleId::Banshee, 0, false) &&
                Halo3SeatFollowsHull(Halo3VehicleId::Chopper, 0, false);
            const bool turnOwnership =
                Halo3VrTurnOwnsStick(false, false) &&
                Halo3VrTurnOwnsStick(false, true) &&
                !Halo3VrTurnOwnsStick(true, false) &&
                Halo3VrTurnOwnsStick(true, true);
            bool snapLatched = false;
            const bool heldTakeoverSafe =
                !Halo3ConsumeSnapTurn(false, 0.9f, snapLatched) &&
                snapLatched &&
                !Halo3ConsumeSnapTurn(true, 0.9f, snapLatched);
            const bool deliberateSnap =
                !Halo3ConsumeSnapTurn(false, 0.0f, snapLatched) &&
                !snapLatched &&
                Halo3ConsumeSnapTurn(true, 0.9f, snapLatched) &&
                !Halo3ConsumeSnapTurn(true, 0.9f, snapLatched);
            Check(drivers && families && wheelSeats && gate && follows &&
                  consistent && turnOwnership && heldTakeoverSafe &&
                  deliberateSnap,
                "Only a look-steered vehicle's driver authors steering, only "
                "while the view follows the hull; the wheel frees the turn "
                "stick, and unsafe pitch-follow seats remain excluded");
        }

        // C20 first-person seat flag. The exact stock flag words come from the
        // E1 official H3EK exports recorded in docs/HALO3-VEHICLE-EVIDENCE.md.
        {
            constexpr uint32_t kWarthogDriver = 0x40014u;   // bits {2,4,18}
            constexpr uint32_t kWarthogPassenger = 0x1070u; // bits {4,5,6,12}
            constexpr uint32_t kMachinegunTurret = 0x5500018u;
            // Every stock player seat is third person, which is why Halo has no
            // first-person vehicle view of its own — and is the live proof that
            // the seat walk landed on a real player seat.
            const bool recognised =
                Halo3SeatFlagsLookLikePlayerSeat(kWarthogDriver) &&
                Halo3SeatFlagsLookLikePlayerSeat(kWarthogPassenger) &&
                Halo3SeatFlagsLookLikePlayerSeat(kMachinegunTurret) &&
                !Halo3SeatFlagsLookLikePlayerSeat(0u) &&
                // A record that already reads first person is refused, so a
                // mis-walked seat can never be "restored" to a value we
                // invented.
                !Halo3SeatFlagsLookLikePlayerSeat(
                    Halo3FirstPersonSeatFlags(kWarthogDriver));
            // Exactly one bit changes, and only that bit. The passenger keeps
            // `allows weapons` (5) and `third person on enter` (6); the gunner
            // keeps `first person camera slaved to gun` (7) — the reference mod
            // clears more, and this candidate deliberately does not.
            const bool onlyBit4 =
                Halo3FirstPersonSeatFlags(kWarthogDriver) == 0x40004u &&
                Halo3FirstPersonSeatFlags(kWarthogPassenger) == 0x1060u &&
                Halo3FirstPersonSeatFlags(kMachinegunTurret) == 0x5500008u;
            // Restoring is the exact inverse for every seat in the table, so
            // seat exit cannot leave a vehicle permanently altered.
            bool reversible = true;
            for (uint32_t flags : {kWarthogDriver, kWarthogPassenger,
                                   kMachinegunTurret})
                reversible = reversible &&
                    (Halo3FirstPersonSeatFlags(flags) |
                     kHalo3SeatThirdPersonCameraBit) == flags;
            Check(recognised && onlyBit4 && reversible,
                "The first-person seat flag clears only `third person camera` "
                "on a seat proven to have it, and restores exactly");
        }

        // C21 hand origin: the seat carries the arms, the head never does.
        {
            const float camera[3] = {10.0f, 20.0f, 30.0f};
            const float body[3] = {1.0f, 2.0f, 3.0f};
            float origin[3] = {camera[0], camera[1], camera[2]};
            const bool takesBody =
                Halo3SelectHandOrigin(true, true, body, origin) &&
                origin[0] == body[0] && origin[1] == body[1] &&
                origin[2] == body[2];

            // Every refusal must leave the caller's origin byte-for-byte alone,
            // so the fallback is exactly the head-anchored behavior this
            // replaces and never an invented position.
            bool untouched = true;
            const float nan3[3] = {1.0f, std::numeric_limits<float>::quiet_NaN(),
                                   3.0f};
            const float inf3[3] = {std::numeric_limits<float>::infinity(),
                                   2.0f, 3.0f};
            struct Refusal { bool follow; bool valid; const float* src; };
            const Refusal refusals[] = {
                {false, true, body},   // feature off
                {true, false, body},   // on foot / no seat sample this frame
                {false, false, body},  // both
                {true, true, nan3},    // torn or unfinished sample
                {true, true, inf3},
                {true, true, nullptr}, // no publication at all
            };
            for (const Refusal& r : refusals)
            {
                float kept[3] = {camera[0], camera[1], camera[2]};
                if (Halo3SelectHandOrigin(r.follow, r.valid, r.src, kept))
                    untouched = false;
                if (kept[0] != camera[0] || kept[1] != camera[1] ||
                    kept[2] != camera[2])
                    untouched = false;
            }
            Check(takesBody && untouched,
                "The vehicle hand origin takes the rigid seat placement when "
                "it is published and finite, and otherwise leaves the camera "
                "origin exactly as it was");
        }

        // C9 virtual wheel: two gripped hands, the tilt of the line between
        // them, read in the head's own heading plane.
        {
            Halo3Wheel wheel;
            const float level[2][3] = {{-0.20f, 1.00f, -0.40f},
                                       {0.20f, 1.00f, -0.40f}};
            auto click = [&](uint64_t at) {
                Halo3UpdateWheelToggle(wheel, true, 0.9f, 0.9f, at);
                return Halo3UpdateWheelToggle(wheel, true, 0.0f, 0.0f, at + 5);
            };
            // A single click is not a toggle, and neither is a slow pair.
            const bool singleIgnored = !click(1000) && !wheel.engaged;
            const bool slowPairIgnored = !click(3000) && !wheel.engaged;
            // Two clicks inside the window take the wheel; two more let go.
            const bool takes = click(3100) && wheel.engaged;
            const bool releases = !click(3300) && click(3400) &&
                !wheel.engaged && wheel.steer == 0.0f;
            // Squeezing ONE grip is never the gesture, so it is never
            // swallowed — that is the dismount.
            Halo3Wheel lone;
            Halo3UpdateWheelToggle(lone, true, 0.0f, 0.9f, 10);
            const bool loneGripFree = !Halo3WheelSwallowsGrips(lone) &&
                !lone.engaged;
            // Both grips down IS the gesture, so those buttons are withheld
            // for exactly that window — including the very first click, before
            // anything has toggled.
            Halo3Wheel gesture;
            Halo3UpdateWheelToggle(gesture, true, 0.9f, 0.9f, 20);
            const bool gestureSwallows = Halo3WheelSwallowsGrips(gesture);
            Halo3UpdateWheelToggle(gesture, true, 0.0f, 0.0f, 25);
            const bool freedOnRelease = !Halo3WheelSwallowsGrips(gesture);
            // Leaving the seat drops the wheel and frees the grips.
            Halo3UpdateWheelToggle(gesture, false, 0.9f, 0.9f, 30);
            const bool dropsWhenUnavailable = !gesture.engaged &&
                !Halo3WheelSwallowsGrips(gesture);

            // Steering. Take the wheel with a real double-click, then read
            // hand poses only — nothing is squeezed while driving.
            Halo3Wheel driving;
            Halo3UpdateWheelToggle(driving, true, 0.9f, 0.9f, 100);
            Halo3UpdateWheelToggle(driving, true, 0.0f, 0.0f, 105);
            Halo3UpdateWheelToggle(driving, true, 0.9f, 0.9f, 200);
            const bool tookIt =
                Halo3UpdateWheelToggle(driving, true, 0.0f, 0.0f, 205) &&
                driving.engaged;
            const bool centred = Halo3ComputeWheelSteer(
                driving, level[0], level[1], 0.0f, 75.0f, 6.0f) &&
                driving.steer == 0.0f;
            // Right hand 45 deg up = counter-clockwise = steer LEFT.
            const float up45[3] = {-0.20f + 0.28284f, 1.0f + 0.28284f, -0.40f};
            Halo3ComputeWheelSteer(driving, level[0], up45, 0.0f, 75.0f, 6.0f);
            const float expected = -(45.0f - 6.0f) / (75.0f - 6.0f);
            const bool steersLeft = std::fabs(driving.steer - expected) < 2e-3f;
            // Mirror image steers right by the same amount.
            const float down45[3] = {-0.20f + 0.28284f, 1.0f - 0.28284f,
                                     -0.40f};
            Halo3ComputeWheelSteer(driving, level[0], down45, 0.0f, 75.0f,
                                   6.0f);
            const bool steersRight =
                std::fabs(driving.steer + expected) < 2e-3f;
            // Past full lock saturates instead of wrapping.
            const float over[3] = {-0.20f + 0.02f, 1.0f + 0.40f, -0.40f};
            Halo3ComputeWheelSteer(driving, level[0], over, 0.0f, 75.0f, 6.0f);
            const bool saturates = std::fabs(driving.steer + 1.0f) < 1e-4f;
            // The same physical wheel, with the player turned 90 degrees in
            // the room, reads exactly the same.
            const float leftTurned[3] = {0.0f, 1.0f, -0.20f};
            const float rightTurned[3] = {0.0f, 1.0f + 0.28284f,
                                          -0.20f + 0.28284f};
            Halo3ComputeWheelSteer(driving, leftTurned, rightTurned,
                                   1.5707963f, 75.0f, 6.0f);
            const bool headRelative =
                std::fabs(driving.steer - expected) < 2e-3f;
            // Hands dropped into a lap must read as straight ahead. Holding
            // the last steer would keep the vehicle turning with nobody
            // driving — a hold could get away with it, a toggle cannot.
            const float together[3] = {0.0f, 1.05f, -0.15f};
            const bool straightOnCollapse =
                Halo3ComputeWheelSteer(driving, leftTurned, together,
                                       1.5707963f, 75.0f, 6.0f) &&
                driving.steer == 0.0f;
            // Not taken, or non-finite input: the stick steers instead.
            Halo3Wheel idleWheel;
            const float nanHand[3] = {std::nanf(""), 1.0f, 0.0f};
            const bool failsOpen =
                !Halo3ComputeWheelSteer(idleWheel, level[0], level[1], 0.0f,
                                        75.0f, 6.0f) &&
                !Halo3ComputeWheelSteer(driving, level[0], nanHand, 0.0f,
                                        75.0f, 6.0f) &&
                driving.steer == 0.0f;

            Check(singleIgnored && slowPairIgnored && takes && releases &&
                  loneGripFree && gestureSwallows && freedOnRelease &&
                  dropsWhenUnavailable && tookIt && centred && steersLeft &&
                  steersRight && saturates && headRelative &&
                  straightOnCollapse && failsOpen,
                "Double-clicking both grips toggles the wheel, a lone grip is "
                "never swallowed so the dismount survives, and the wheel "
                "steers from the hand line in the head's own plane");
        }

        // C2 mode refinement: only a proven seated state upgrades Gameplay;
        // Turret requires the measured physics type 5 exactly; an unproven
        // type stays a plain Vehicle; Unknown and OnFoot never upgrade.
        Check(Halo3ClassifyGameplayUpgrade(
                  Halo3VehicleState::Unknown, 5) == 0 &&
              Halo3ClassifyGameplayUpgrade(
                  Halo3VehicleState::OnFoot, 1) == 0 &&
              Halo3ClassifyGameplayUpgrade(
                  Halo3VehicleState::Vehicle, 1) == 1 &&
              Halo3ClassifyGameplayUpgrade(
                  Halo3VehicleState::Vehicle, 3) == 1 &&
              Halo3ClassifyGameplayUpgrade(
                  Halo3VehicleState::Vehicle,
                  kHalo3VehicleTypeTurret) == 2 &&
              Halo3ClassifyGameplayUpgrade(
                  Halo3VehicleState::Vehicle, -1) == 1,
            "Gameplay upgrades to Vehicle/Turret only on proven seated state");
    }

    // Co-op drop probe: the reduction that turns a per-frame ring into one line
    // per second must age samples correctly, average and peak them per second,
    // count the frames where the mod held MCC's render thread long enough to
    // matter, and survive a GetTickCount64 wrap mid-capture.
    {
        constexpr size_t kBuckets = 8;
        CoopProbeBucket buckets[kBuckets]{};

        // Two seconds of frames. The older second is quiet; the second right
        // before the dump has one 9 ms hold and one 5 ms hold.
        const uint32_t dumpTick = 100000;
        CoopProbeSample samples[6]{};
        samples[0] = { dumpTick - 1500, 0.5f, 0.05f, 8.3f };
        samples[1] = { dumpTick - 1200, 0.7f, 0.05f, 8.4f };
        samples[2] = { dumpTick - 1100, 0.6f, 0.05f, 8.3f };
        samples[3] = { dumpTick -  800, 9.0f, 0.10f, 20.0f };
        samples[4] = { dumpTick -  400, 5.0f, 0.06f, 9.0f };
        samples[5] = { dumpTick -  100, 1.0f, 0.04f, 8.3f };

        const size_t populated =
            CoopProbeSummarise(samples, 6, dumpTick, buckets, kBuckets);

        // Buckets are indexed by age: [0] is the second before the dump.
        const bool aged = populated == 2 &&
            buckets[0].frames == 3 && buckets[1].frames == 3;
        const bool peaks = buckets[0].holdMaxMs == 9.0f &&
            buckets[1].holdMaxMs == 0.7f &&
            buckets[0].intervalMaxMs == 20.0f;
        const bool thresholds = buckets[0].holdOver4Ms == 2 &&
            buckets[0].holdOver8Ms == 1 &&
            buckets[1].holdOver4Ms == 0 && buckets[1].holdOver8Ms == 0;
        // (9 + 5 + 1) / 3 == 5, and the quiet second averages (0.5+0.7+0.6)/3.
        const bool averaged = buckets[0].holdAvgMs == 5.0f &&
            buckets[1].holdAvgMs > 0.59f && buckets[1].holdAvgMs < 0.61f;

        // Samples older than the bucket window are dropped, not misfiled.
        CoopProbeSample stale[1]{};
        stale[0] = { dumpTick - (uint32_t)(kBuckets * 1000 + 5), 3.0f, 0.0f, 8.3f };
        const bool dropsStale =
            CoopProbeSummarise(stale, 1, dumpTick, buckets, kBuckets) == 0;

        // A capture spanning the uint32 tick wrap still ages correctly, because
        // the subtraction is modular.
        CoopProbeBucket wrapped[kBuckets]{};
        const uint32_t afterWrap = 1500;
        CoopProbeSample across[2]{};
        // 1756 ms before the dump, i.e. the bucket before last, recorded 256 ms
        // before the counter wrapped through zero.
        across[0] = { 0xFFFFFF00u, 2.0f, 0.0f, 8.3f };
        across[1] = { afterWrap - 100, 4.0f, 0.0f, 8.3f };
        const size_t wrapPopulated =
            CoopProbeSummarise(across, 2, afterWrap, wrapped, kBuckets);
        const bool wrapSafe = wrapPopulated == 2 &&
            wrapped[0].frames == 1 && wrapped[1].frames == 1;

        // Degenerate inputs must not write anything.
        const bool guards =
            CoopProbeSummarise(nullptr, 4, dumpTick, buckets, kBuckets) == 0 &&
            CoopProbeSummarise(samples, 6, dumpTick, buckets, 0) == 0;

        Check(aged && peaks && thresholds && averaged && dropsStale &&
              wrapSafe && guards,
            "Co-op probe reduces per-frame holds into per-second buckets, keeps "
            "peaks and over-threshold counts, and is tick-wrap safe");

        // Only a real level teardown carries a run-up worth dumping; menu
        // traffic into Loading does not.
        Check(CoopProbeIsInLevelMode(RuntimeMode::Gameplay) &&
              CoopProbeIsInLevelMode(RuntimeMode::Vehicle) &&
              CoopProbeIsInLevelMode(RuntimeMode::Turret) &&
              CoopProbeIsInLevelMode(RuntimeMode::Paused) &&
              !CoopProbeIsInLevelMode(RuntimeMode::Shell) &&
              !CoopProbeIsInLevelMode(RuntimeMode::Loading) &&
              !CoopProbeIsInLevelMode(RuntimeMode::Unsupported),
            "Co-op probe dumps only when a live level falls to Loading");
    }

    {
        // The upload path's source-view cache. The defect this replaces was a
        // ONE-entry cache keyed on the source texture pointer: a frame that
        // publishes two eyes from two different textures missed on every
        // single call and asked the device for a new view each time.

        // Stand-ins for the resources and the views made from them. Only the
        // addresses matter; the cache never dereferences either.
        int eyeLeft = 0, eyeRight = 0, menu = 0, screen = 0;
        int viewLeft = 0, viewRight = 0, viewMenu = 0, viewScreen = 0;

        ViewCacheTable<32> cache;

        // A cold frame misses once per distinct source, which is unavoidable.
        const bool coldMisses =
            !cache.Find(&eyeLeft).hit && !cache.Find(&eyeRight).hit;
        cache.Insert(&eyeLeft, &viewLeft);
        cache.Insert(&eyeRight, &viewRight);

        // THE REGRESSION THIS LOCKS DOWN: alternating between the two eyes
        // must hit every time. The old single slot returned a miss on all
        // four of these lookups.
        bool alternatingHits = true;
        for (int frame = 0; frame < 4; ++frame)
        {
            const ViewCacheLookup left = cache.Find(&eyeLeft);
            const ViewCacheLookup right = cache.Find(&eyeRight);
            alternatingHits = alternatingHits &&
                left.hit && left.view == &viewLeft &&
                right.hit && right.view == &viewRight;
        }

        // Adding the menu and the screen quad to the same frame does not
        // evict the eyes, which is what a one-slot cache could not manage.
        cache.Insert(&menu, &viewMenu);
        cache.Insert(&screen, &viewScreen);
        const bool busyFrameHolds =
            cache.Contains(&eyeLeft) && cache.Contains(&eyeRight) &&
            cache.Contains(&menu) && cache.Contains(&screen) &&
            cache.Size() == 4;

        Check(coldMisses && alternatingHits && busyFrameHolds,
            "Source-view cache holds every source a frame touches, so "
            "alternating eyes stop recreating a view per eye per frame");

        // Nothing is displaced while capacity remains, so steady-state play
        // performs no releases at all.
        ViewCacheTable<32> steady;
        int sources[6]{};
        int views[6]{};
        bool noDisplacement = true;
        for (int i = 0; i < 6; ++i)
            noDisplacement = noDisplacement &&
                steady.Insert(&sources[i], &views[i]) == nullptr;
        // Replay ten frames over that working set: all hits, no misses.
        steady.ResetStats();
        for (int frame = 0; frame < 10; ++frame)
            for (int i = 0; i < 6; ++i)
                (void)steady.Find(&sources[i]);
        const bool steadyStateIsFree =
            steady.Stats().hits == 60 && steady.Stats().misses == 0 &&
            steady.Stats().evictions == 0;
        Check(noDisplacement && steadyStateIsFree,
            "A working set inside capacity reaches a steady state with zero "
            "view creation and zero eviction");

        // Eviction, exercised at a small capacity: the least recently USED
        // entry goes, not the least recently inserted. Touching a stale entry
        // must save it.
        ViewCacheTable<3> tight;
        int a = 0, b = 0, c = 0, d = 0;
        int va = 0, vb = 0, vc = 0, vd = 0;
        tight.Insert(&a, &va);
        tight.Insert(&b, &vb);
        tight.Insert(&c, &vc);
        (void)tight.Find(&a);            // `a` is now the freshest, `b` oldest
        void* evicted = tight.Insert(&d, &vd);
        const bool lru = evicted == &vb && !tight.Contains(&b) &&
            tight.Contains(&a) && tight.Contains(&c) && tight.Contains(&d) &&
            tight.Size() == 3 && tight.Stats().evictions == 1;
        Check(lru, "The cache evicts the least recently used view and keeps "
                   "the resources the current frame is still touching");

        // Re-inserting the same key hands back the previous view to release,
        // and never grows the table - otherwise a resource that legitimately
        // gets a fresh view would leak one every time.
        ViewCacheTable<4> replace;
        int key = 0, first = 0, second = 0;
        replace.Insert(&key, &first);
        void* superseded = replace.Insert(&key, &second);
        const bool replaced = superseded == &first && replace.Size() == 1 &&
            replace.Find(&key).view == &second;

        // Forget() is how the owner drops a resource it is about to destroy,
        // which is what stops the table pinning a dead texture alive.
        void* forgotten = replace.Forget(&key);
        const bool forgets = forgotten == &second && replace.Size() == 0 &&
            !replace.Find(&key).hit && replace.Forget(&key) == nullptr;
        Check(replaced && forgets,
            "Re-inserting a key returns the superseded view, and Forget drops "
            "a resource the owner is recreating");

        // Guards. A failed view creation must not be stored: a poisoned slot
        // would report a hit and hand back nothing, which is worse than the
        // miss it replaced.
        ViewCacheTable<4> guarded;
        int live = 0, view = 0;
        const bool rejectsNulls =
            guarded.Insert(&live, nullptr) == nullptr &&
            !guarded.Find(&live).hit && guarded.Size() == 0 &&
            guarded.Insert(nullptr, &view) == &view &&
            !guarded.Find(nullptr).hit && !guarded.Contains(nullptr) &&
            guarded.Forget(nullptr) == nullptr;

        // Teardown drains every entry exactly once, so nothing leaks and
        // nothing is released twice.
        guarded.Insert(&live, &view);
        int extra = 0, extraView = 0;
        guarded.Insert(&extra, &extraView);
        int drained = 0;
        while (guarded.TakeAny() != nullptr)
            ++drained;
        const bool drains = drained == 2 && guarded.Size() == 0;
        Check(rejectsNulls && drains,
            "A failed creation is never cached, and teardown drains each held "
            "view exactly once");
    }

    {
        // The intermediate-copy pool. The defect this replaces kept ONE
        // intermediate and rebuilt it whenever the requested shape differed,
        // so two different-sized slow-path sources in one frame destroyed and
        // recreated a full-resolution texture every frame.
        IntermediatePoolTable<8> pool;

        const IntermediateShape eye{2064, 2208, 28};
        const IntermediateShape reticle{64, 64, 28};

        const IntermediatePoolSlot firstEye = pool.Acquire(eye);
        const bool coldCreate = firstEye.valid && firstEye.needsCreate &&
            !firstEye.evicted.Valid();
        pool.Commit(firstEye.index, eye);

        const IntermediatePoolSlot firstReticle = pool.Acquire(reticle);
        const bool secondShapeGetsItsOwnSlot = firstReticle.valid &&
            firstReticle.needsCreate && !firstReticle.evicted.Valid() &&
            firstReticle.index != firstEye.index;
        pool.Commit(firstReticle.index, reticle);

        // THE REGRESSION THIS LOCKS DOWN: alternating shapes must both be
        // resident. The old single intermediate reported needsCreate on every
        // one of these, at full render size.
        bool alternatingReuse = true;
        for (int frame = 0; frame < 4; ++frame)
        {
            const IntermediatePoolSlot e = pool.Acquire(eye);
            const IntermediatePoolSlot r = pool.Acquire(reticle);
            alternatingReuse = alternatingReuse &&
                e.valid && !e.needsCreate && e.index == firstEye.index &&
                r.valid && !r.needsCreate && r.index == firstReticle.index;
        }
        Check(coldCreate && secondShapeGetsItsOwnSlot && alternatingReuse,
            "The intermediate pool keeps one copy per shape, so alternating "
            "sources stop rebuilding a full-resolution texture every frame");

        // THE PROPERTY THAT MAKES A CAPACITY OF 32 SAFE: the pool is a ceiling,
        // not a preallocation. A big cap must hold only the shapes actually
        // asked for, so raising it cannot cost VRAM that is never requested.
        IntermediatePoolTable<32> ceiling;
        const bool emptyUntilAsked = ceiling.Size() == 0;
        const IntermediateShape onlyShape{2064, 2208, 28};
        for (int frame = 0; frame < 50; ++frame)
        {
            const IntermediatePoolSlot s = ceiling.Acquire(onlyShape);
            if (s.needsCreate)
                ceiling.Commit(s.index, onlyShape);
        }
        // Fifty frames of one shape occupy exactly one slot out of thirty-two,
        // and only the first frame created anything.
        const bool onlyWhatWasAskedFor = ceiling.Size() == 1 &&
            ceiling.Stats().misses == 1 && ceiling.Stats().hits == 49 &&
            ceiling.Stats().evictions == 0;
        Check(emptyUntilAsked && onlyWhatWasAskedFor,
            "The intermediate pool's capacity is a ceiling, not a "
            "preallocation: 32 slots hold only the shapes actually requested");

        // A shape differing only in format is a different shape - reusing a
        // copy across formats would sample the wrong bits.
        const IntermediateShape sameSizeOtherFormat{2064, 2208, 87};
        const IntermediatePoolSlot recoloured = pool.Acquire(sameSizeOtherFormat);
        const bool formatIsPartOfIdentity =
            recoloured.needsCreate && recoloured.index != firstEye.index;
        Check(formatIsPartOfIdentity,
            "Format is part of an intermediate's identity, so a copy is never "
            "reused across formats");

        // Eviction at a small capacity reports the shape whose resource the
        // caller must release, so a slot is never overwritten while occupied.
        IntermediatePoolTable<2> tight;
        const IntermediateShape s1{100, 100, 1};
        const IntermediateShape s2{200, 200, 1};
        const IntermediateShape s3{300, 300, 1};
        const IntermediatePoolSlot t1 = tight.Acquire(s1);
        tight.Commit(t1.index, s1);
        const IntermediatePoolSlot t2 = tight.Acquire(s2);
        tight.Commit(t2.index, s2);
        (void)tight.Acquire(s1);  // `s1` freshest, so `s2` is the victim
        const IntermediatePoolSlot t3 = tight.Acquire(s3);
        const bool evictsLru = t3.needsCreate && t3.evicted == s2 &&
            t3.index == t2.index && tight.Stats().evictions == 1;
        tight.Commit(t3.index, s3);

        // A creation that fails must abandon its slot rather than leave a
        // shape claimed by a resource that does not exist.
        const IntermediatePoolSlot failing = tight.Acquire(IntermediateShape{400, 400, 1});
        tight.Abandon(failing.index);
        const bool abandonClears = !tight.Live(failing.index) &&
            tight.Acquire(IntermediateShape{400, 400, 1}).needsCreate;

        // Degenerate shapes are refused outright rather than claiming a slot.
        IntermediatePoolTable<2> guards;
        const bool rejectsDegenerate =
            !guards.Acquire(IntermediateShape{0, 100, 1}).valid &&
            !guards.Acquire(IntermediateShape{100, 0, 1}).valid &&
            guards.Size() == 0;

        Check(evictsLru && abandonClears && rejectsDegenerate,
            "The intermediate pool evicts least-recently-used, clears an "
            "abandoned slot, and refuses a degenerate shape");
    }

    if (g_failures == 0)
        std::cout << "HaloMCCVR core tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
