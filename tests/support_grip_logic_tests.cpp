// Persistent support grip: pure policy suite (port tranche T4).
//
// Ports the donor core_tests.cpp policy matrices for the durable relationship,
// the durable writer, per-invocation owner trust, carrier detach decisions,
// ray consumption, stable evidence resolution and the CE tri-state mapping
// into one standalone suite, and adds the A017 red-team repairs the port
// carries:
//
//   F01  the durable owner tuple includes the game title;
//   F02  lifecycle identity (title/generation/mode) terminates an engaged
//        relationship and arms release-before-reacquire; Paused and pose
//        outages are retained;
//   F08  fresh acquisition records an evidence revision floor and may bind
//        only from a publication strictly newer than that floor;
//   F11  an interaction teardown that can coincide with a weapon replacement
//        arms release-before-reacquire before the owner is cleared;
//   F12  a pending acquisition intent survives a toggle button-up and ends
//        only on an explicit cancel or a lifecycle/title/generation/role
//        invalidation (hold mode lives while Grip is held);
//   F14  an epoch-0 presented ray is accepted only when the caller asserts a
//        coherent disengagement;
//   F16  a handedness/role teardown runs through the canonical writer.
//
// Everything here is pure header policy plus the existing admission/hold
// helpers; no engine state, no publication, no I/O.
//
// The single-writer vr.cpp seam itself (UpdatePersistentSupportGrip: the
// per-frame ordering of the lifecycle/role/gesture teardowns, the pose-outage
// retention branch, the pending-intent inputs and the snapshot/latch
// publication) is not unit testable here - it is called once per captured
// frame and owns engine state. TestPersistentGripWriterSeam() rehearses that
// exact ordering against the same pure helpers so a policy regression cannot
// hide behind it; the runtime ordering is covered by the T7 seam tests and the
// headset gate.
#include "support_grip_logic.h"
#include "support_diagnostics.h"
#include <thread>
#include "input_logic.h"
#include "weapon_hand_logic.h"
#include "first_person_view_logic.h"

#include <cstdint>
#include <cstdio>

namespace
{
using namespace support_grip;

struct TestContext
{
    unsigned checks{};
    unsigned failures{};

    void Check(bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }

    int Finish() const
    {
        std::printf("support grip logic: %u checks, %u failures\n", checks,
            failures);
        return failures ? 1 : 0;
    }
};

constexpr uint32_t kGeneration = 7;
const OwnerTuple kOwnerA{GameTitle::Halo3, kGeneration, 0x11110001u,
    0x22220001u};
const OwnerTuple kOwnerB{GameTitle::Halo3, kGeneration, 0x11110001u,
    0x33330001u};

void TestOwnerIdentity(TestContext& test)
{
    // F01: title + title generation + controlled unit + full weapon handle.
    test.Check(!OwnerComplete(OwnerTuple{}) && OwnerComplete(kOwnerA),
        "an owner tuple requires title, generation, unit and weapon");
    test.Check(!OwnerComplete(OwnerTuple{GameTitle::None, kGeneration,
             kOwnerA.unit, kOwnerA.weapon}) &&
            !OwnerComplete(OwnerTuple{GameTitle::Unknown, kGeneration,
             kOwnerA.unit, kOwnerA.weapon}),
        "a missing or unclassified title cannot identify an owner");
    test.Check(!OwnerComplete(OwnerTuple{GameTitle::Halo3, 0, kOwnerA.unit,
             kOwnerA.weapon}),
        "a zero generation cannot identify an owner");
    test.Check(!OwnerComplete(OwnerTuple{GameTitle::Halo3, kGeneration,
             0xFFFFFFFFu, kOwnerA.weapon}) &&
            !OwnerComplete(OwnerTuple{GameTitle::Halo3, kGeneration,
             kOwnerA.unit, 0xFFFFFFFFu}),
        "a null unit or weapon handle cannot identify an owner");
    test.Check(SameOwner(kOwnerA, kOwnerA) && !SameOwner(kOwnerA, kOwnerB),
        "owner equality covers every tuple member");
    // F01 regression: per-title generation slots make an identical numeric
    // tuple legal in two titles, so the title must decide identity.
    const OwnerTuple halo3{GameTitle::Halo3, 1, 0x11110001u, 0x22220001u};
    const OwnerTuple reach{GameTitle::HaloReach, 1, 0x11110001u, 0x22220001u};
    test.Check(OwnerComplete(halo3) && OwnerComplete(reach),
        "each title's numeric tuple is complete on its own");
    test.Check(!SameOwner(halo3, reach),
        "Halo3/gen1/unitX/weaponY and Reach/gen1/unitX/weaponY are different owners");
    test.Check(!InvocationOwnerTrusted(true, halo3, OwnerEvidence::KnownPresent,
             reach),
        "a foreign-title invocation can never be trusted as the same owner");
    test.Check(TitleIdentifiesOwner(GameTitle::Halo3) &&
            TitleIdentifiesOwner(GameTitle::HaloReach) &&
            !TitleIdentifiesOwner(GameTitle::None) &&
            !TitleIdentifiesOwner(GameTitle::Unknown),
        "only real gameplay titles identify an owner");
}

void TestDurableRelationshipStep(TestContext& test)
{
    OwnerTuple owner{};
    // Fresh acquisition waits for a proven owner; a stale/unknown publication
    // can never bind a fresh relationship.
    test.Check(DurableRelationshipStep(false, true, OwnerEvidence::Unknown,
              OwnerTuple{}, owner) == DurableEvent::None,
        "unknown owner evidence never binds a fresh relationship");
    test.Check(DurableRelationshipStep(false, false,
              OwnerEvidence::KnownPresent, kOwnerA, owner) ==
              DurableEvent::None,
        "an unrequested acquisition never binds");
    test.Check(DurableRelationshipStep(false, true, OwnerEvidence::KnownAbsent,
              OwnerTuple{}, owner) == DurableEvent::None,
        "proven absence cannot bind a fresh relationship");
    test.Check(DurableRelationshipStep(false, true,
              OwnerEvidence::KnownPresent,
              OwnerTuple{GameTitle::Halo3, kGeneration, 0x11110001u,
                  0xFFFFFFFFu}, owner) == DurableEvent::None,
        "an incomplete owner tuple never binds");
    test.Check(DurableRelationshipStep(false, true,
              OwnerEvidence::KnownPresent, kOwnerA, owner) ==
              DurableEvent::Bind && SameOwner(owner, kOwnerA),
        "a proven current owner binds the fresh relationship");
    // Durable lifetime policy: keep / invalidate / keep.
    test.Check(DurableRelationshipStep(true, false, OwnerEvidence::KnownPresent,
              kOwnerA, owner) == DurableEvent::None,
        "the same proven owner keeps the relationship");
    test.Check(DurableRelationshipStep(true, false, OwnerEvidence::KnownPresent,
              kOwnerB, owner) == DurableEvent::Invalidated,
        "a proven owner replacement invalidates the relationship");
    test.Check(DurableRelationshipStep(true, false, OwnerEvidence::KnownAbsent,
              OwnerTuple{}, owner) == DurableEvent::Invalidated,
        "proven absence invalidates the relationship");
    test.Check(DurableRelationshipStep(true, false, OwnerEvidence::Unknown,
              OwnerTuple{}, owner) == DurableEvent::None,
        "unknown evidence never durably releases the relationship");
    // Same title, same generation, same unit, different weapon handle: a
    // same-model replacement is still a new incarnation.
    test.Check(DurableRelationshipStep(true, false, OwnerEvidence::KnownPresent,
              OwnerTuple{GameTitle::Halo3, kGeneration, kOwnerA.unit,
                  kOwnerB.weapon}, owner) == DurableEvent::Invalidated,
        "a same-model full-handle replacement invalidates the relationship");
}

void TestInvocationTrust(TestContext& test)
{
    test.Check(InvocationOwnerTrusted(true, kOwnerA,
              OwnerEvidence::KnownPresent, kOwnerA),
        "same-owner invocation is trusted");
    test.Check(!InvocationOwnerTrusted(true, kOwnerA,
              OwnerEvidence::KnownPresent, kOwnerB),
        "owner-mismatched invocation is untrusted");
    test.Check(!InvocationOwnerTrusted(true, kOwnerA, OwnerEvidence::Unknown,
              kOwnerA),
        "invocation-local unknown is untrusted");
    test.Check(!InvocationOwnerTrusted(true, kOwnerA, OwnerEvidence::KnownAbsent,
              OwnerTuple{}),
        "proven absence is untrusted");
    test.Check(!InvocationOwnerTrusted(false, kOwnerA,
              OwnerEvidence::KnownPresent, kOwnerA),
        "a released relationship trusts no invocation");
    test.Check(!InvocationOwnerTrusted(true, kOwnerA,
              OwnerEvidence::KnownPresent,
              OwnerTuple{GameTitle::Halo3, kGeneration, kOwnerA.unit,
                  0xFFFFFFFFu}),
        "an incomplete invocation owner tuple is untrusted");
    // Agreement-aware trust for titles whose reader exposes the FP
    // producer/slot agreement bit: a single-read KnownPresent is not enough
    // while an owner swap can leave the inventory read naming the previous
    // unit.
    test.Check(InvocationOwnerTrusted(true, kOwnerA, OwnerEvidence::KnownPresent,
              true, kOwnerA),
        "same-owner invocation with producer agreement is trusted");
    test.Check(!InvocationOwnerTrusted(true, kOwnerA,
              OwnerEvidence::KnownPresent, false, kOwnerA),
        "same-owner invocation without producer agreement is untrusted");
    test.Check(!InvocationOwnerTrusted(true, kOwnerA,
              OwnerEvidence::KnownPresent, true, kOwnerB),
        "agreed invocation naming a different owner is untrusted");
    test.Check(!InvocationOwnerTrusted(true, kOwnerA,
              OwnerEvidence::KnownPresent, true,
              OwnerTuple{GameTitle::Halo3, kGeneration, kOwnerA.unit,
                  0xFFFFFFFFu}),
        "agreed invocation with an incomplete owner tuple is untrusted");
    test.Check(!InvocationOwnerTrusted(true, kOwnerA, OwnerEvidence::Unknown,
              true, kOwnerA) &&
            !InvocationOwnerTrusted(true, kOwnerA, OwnerEvidence::KnownAbsent,
              true, OwnerTuple{}),
        "agreed invocation without a proven present owner is untrusted");
    test.Check(!InvocationOwnerTrusted(false, kOwnerA,
              OwnerEvidence::KnownPresent, true, kOwnerA),
        "a released relationship trusts no agreed invocation");
}

void TestCarrierDecisions(TestContext& test)
{
    test.Check(!SupportCarrierMustDetachForInvocation(false, kOwnerA,
              OwnerEvidence::Unknown, OwnerTuple{}) &&
            !SupportCarrierMustDetachForInvocation(false, kOwnerA,
              OwnerEvidence::KnownPresent, kOwnerB),
        "no engaged relationship always allows the ordinary carrier");
    test.Check(!SupportCarrierMustDetachForInvocation(true, kOwnerA,
              OwnerEvidence::KnownPresent, kOwnerA),
        "a current invocation proving the relationship owner keeps the support carrier");
    test.Check(SupportCarrierMustDetachForInvocation(true, kOwnerA,
              OwnerEvidence::KnownPresent, kOwnerB),
        "engaged relationship A with current owner B detaches");
    test.Check(SupportCarrierMustDetachForInvocation(true, kOwnerA,
              OwnerEvidence::Unknown, kOwnerA) &&
            SupportCarrierMustDetachForInvocation(true, kOwnerA,
              OwnerEvidence::KnownAbsent, OwnerTuple{}),
        "unknown evidence and proven absence both detach");
    test.Check(SupportCarrierMustDetachForInvocation(true, kOwnerA,
              OwnerEvidence::KnownPresent,
              OwnerTuple{GameTitle::Halo3, kGeneration, kOwnerA.unit,
                  0xFFFFFFFFu}),
        "an incomplete current owner tuple detaches");
    test.Check(SupportCarrierMustDetachForInvocation(true, kOwnerA,
              OwnerEvidence::KnownPresent,
              OwnerTuple{GameTitle::HaloReach, kGeneration, kOwnerA.unit,
                  kOwnerA.weapon}),
        "an identical numeric tuple from another title detaches");

    // Exact-invocation carrier decision shared by the CE and Halo 2 consumers.
    test.Check(SupportInvocationMustDetach(false, false, false, false) &&
            SupportInvocationMustDetach(false, false, true, true) &&
            SupportInvocationMustDetach(false, true, true, false) &&
            SupportInvocationMustDetach(false, true, false, true),
        "an unreadable relationship detaches regardless of relationship or publication state");
    test.Check(!SupportInvocationMustDetach(true, false, false, false),
        "a readable disengaged relationship with an ordinary one-hand publication stays valid");
    test.Check(SupportInvocationMustDetach(true, false, false, true),
        "a readable disengaged relationship with a support-derived publication detaches");
    test.Check(!SupportInvocationMustDetach(true, true, true, true) &&
            !SupportInvocationMustDetach(true, true, true, false),
        "an engaged trusted relationship consumes the support carrier");
    test.Check(SupportInvocationMustDetach(true, true, false, true) &&
            SupportInvocationMustDetach(true, true, false, false),
        "an engaged untrusted relationship detaches");
}

void TestRayAndPresentation(TestContext& test)
{
    // F14: an epoch-0 (ordinary one-hand) ray is consumable only under an
    // explicitly coherent DISENGAGED relationship reading. Epoch 0 alone is
    // never permission, so the latch-engaged/publication-old race and an
    // unavailable snapshot both fail.
    test.Check(SupportRayConsumable(0, false, false, 0, true,
              OwnerEvidence::Unknown, false, OwnerTuple{}, OwnerTuple{}),
        "a coherently disengaged ordinary one-hand ray is consumable");
    test.Check(!SupportRayConsumable(0, false, true, 0, true,
              OwnerEvidence::KnownPresent, true, kOwnerA, kOwnerA),
        "an epoch-0 ray while the relationship reads engaged is never consumable");
    test.Check(!SupportRayConsumable(0, false, false, 0, false,
              OwnerEvidence::Unknown, false, OwnerTuple{}, OwnerTuple{}),
        "an epoch-0 ray without a proven coherent disengagement is never consumable");
    test.Check(!SupportRayConsumable(9, false, true, 9, true,
              OwnerEvidence::KnownPresent, true, kOwnerA, kOwnerA),
        "an untrusted support ray is never consumable");
    test.Check(SupportRayConsumable(9, true, true, 9, false,
              OwnerEvidence::KnownPresent, true, kOwnerA, kOwnerA),
        "an engaged trusted ray is consumable regardless of the disengagement flag");
    test.Check(SupportRayConsumable(9, true, true, 9, true,
              OwnerEvidence::KnownPresent, true, kOwnerA, kOwnerA),
        "trusted same-epoch support with an agreed same-owner invocation is consumable");
    test.Check(!SupportRayConsumable(9, true, true, 9, true,
              OwnerEvidence::KnownPresent, false, kOwnerA, kOwnerA),
        "a support ray whose consuming invocation lacks producer agreement is not consumable");
    test.Check(!SupportRayConsumable(9, true, true, 9, true,
              OwnerEvidence::KnownPresent, true, kOwnerB, kOwnerA),
        "a support ray whose consuming invocation names a different owner is not consumable");
    test.Check(!SupportRayConsumable(9, true, true, 9, true,
              OwnerEvidence::KnownPresent, true,
              OwnerTuple{GameTitle::Halo3, kGeneration, kOwnerA.unit,
                  0xFFFFFFFFu}, kOwnerA),
        "a support ray whose consuming invocation has an incomplete owner is not consumable");
    test.Check(!SupportRayConsumable(9, true, true, 9, true, OwnerEvidence::Unknown,
              true, kOwnerA, kOwnerA),
        "a support ray whose consuming invocation cannot prove a present owner is not consumable");
    test.Check(!SupportRayConsumable(9, true, true, 10, true,
              OwnerEvidence::KnownPresent, true, kOwnerA, kOwnerA),
        "a support ray from a replaced relationship epoch is not consumable");
    test.Check(!SupportRayConsumable(9, true, false, 9, true,
              OwnerEvidence::KnownPresent, true, kOwnerA, kOwnerA),
        "a support ray is not consumable after the relationship is released");

    test.Check(SupportPresentationAttached(true, true) &&
            !SupportPresentationAttached(true, false) &&
            !SupportPresentationAttached(false, true),
        "presentation follows the valid relationship and owner trust only");
    test.Check(!AimSupportDerived(false, false) && !AimSupportDerived(false, true),
        "an invalid solve is never support-derived");
    test.Check(!AimSupportDerived(true, false),
        "a valid one-hand solve is not support-derived");
    test.Check(AimSupportDerived(true, true),
        "a valid two-hand solve is support-derived");
}

void TestStableEvidence(TestContext& test)
{
    const uint8_t present = uint8_t(OwnerEvidence::KnownPresent);
    const uint8_t absent = uint8_t(OwnerEvidence::KnownAbsent);
    const uint8_t unknown = uint8_t(OwnerEvidence::Unknown);
    test.Check(ResolveStableEvidence(true, present, true, present, true) ==
            OwnerEvidence::KnownPresent,
        "both reads agree on a producer-confirmed present owner");
    test.Check(ResolveStableEvidence(true, present, true, present, false) ==
            OwnerEvidence::Unknown,
        "a present owner without agreement on both reads stays unknown");
    test.Check(ResolveStableEvidence(false, present, false, present, false) ==
            OwnerEvidence::KnownPresent,
        "a caller that does not require agreement accepts both-read present");
    // F03 half: a proven absence does NOT require the FP agreement bit, which
    // the native readers cannot set on an empty primary slot.
    test.Check(ResolveStableEvidence(true, absent, false, absent, false) ==
            OwnerEvidence::KnownAbsent,
        "both reads prove absence without requiring agreement");
    test.Check(ResolveStableEvidence(true, absent, true, absent, true) ==
            OwnerEvidence::KnownAbsent,
        "agreement never downgrades a stable absence");
    test.Check(ResolveStableEvidence(true, present, true, absent, false) ==
            OwnerEvidence::Unknown &&
            ResolveStableEvidence(true, absent, false, present, false) ==
            OwnerEvidence::Unknown,
        "mixed present/absent reads are unknown in both orders");
    test.Check(ResolveStableEvidence(true, absent, false, unknown, false) ==
            OwnerEvidence::Unknown,
        "an inconclusive read never collapses a proven absence");
    test.Check(ResolveStableEvidence(true, unknown, false, unknown, false) ==
            OwnerEvidence::Unknown,
        "two unknown reads prove nothing");
}

void TestCeEvidence(TestContext& test)
{
    test.Check(CeOwnerEvidence(true, true, true, 0x22220001u) ==
            OwnerEvidence::KnownPresent,
        "CE validated full handle is a proven present owner");
    test.Check(CeOwnerEvidence(true, true, false, 0xFFFFFFFFu) ==
            OwnerEvidence::KnownAbsent,
        "CE empty raw slot is a proven absent owner");
    test.Check(CeOwnerEvidence(true, true, true, 0xFFFFFFFFu) ==
            OwnerEvidence::Unknown,
        "CE non-null candidate that failed validation is unknown");
    test.Check(CeOwnerEvidence(true, false, false, 0xFFFFFFFFu) ==
            OwnerEvidence::Unknown,
        "CE missing FP user record is unknown");
    test.Check(CeOwnerEvidence(false, false, false, 0xFFFFFFFFu) ==
            OwnerEvidence::Unknown,
        "CE failed reader is unknown");
    test.Check(CeOwnerEvidence(false, true, false, 0xFFFFFFFFu) ==
            OwnerEvidence::Unknown,
        "CE failed reader never reports absence");
}

void TestDurableWriterBasics(TestContext& test)
{
    DurableWriterState state;
    test.Check(!state.engaged && state.epoch == 1 &&
            state.owner.generation == 0 && !state.requiresRelease,
        "the durable writer starts disengaged, empty and unarmed");
    test.Check(DurableWriterStep(state, true, false, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::None &&
            !state.engaged && state.epoch == 1,
        "an unrequested acquisition never binds");
    test.Check(DurableWriterStep(state, true, true, OwnerEvidence::Unknown,
              OwnerTuple{}, kGeneration) == DurableWriterAction::None &&
            !state.engaged && state.epoch == 1,
        "unknown owner evidence never binds and keeps the epoch");
    test.Check(DurableWriterStep(state, true, true, OwnerEvidence::KnownAbsent,
              OwnerTuple{}, kGeneration) == DurableWriterAction::None &&
            !state.engaged && state.epoch == 1,
        "proven absence cannot bind either");
    test.Check(DurableWriterStep(state, true, true, OwnerEvidence::KnownPresent,
              OwnerTuple{}, kGeneration) == DurableWriterAction::None &&
            !state.engaged && state.epoch == 1,
        "an incomplete owner tuple never binds");
    test.Check(DurableWriterStep(state, true, true, OwnerEvidence::KnownPresent,
              OwnerTuple{kOwnerA.title, kGeneration - 1, kOwnerA.unit,
                  kOwnerA.weapon}, kGeneration) == DurableWriterAction::None &&
            !state.engaged && state.epoch == 1,
        "stale-generation evidence cannot bind the current generation");
    test.Check(DurableWriterStep(state, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind &&
            state.engaged && SameOwner(state.owner, kOwnerA) &&
            state.epoch == 2 && !state.requiresRelease,
        "a proven current owner binds and advances the epoch");
    // Engaged lifetime: unknown keeps, same owner keeps, and a proven owner
    // replacement drops the relationship WITHOUT arming release-before-
    // reacquire (the held/in-zone path or the next toggle click re-acquires
    // it); proven absence still arms.
    test.Check(DurableWriterStep(state, true, false, OwnerEvidence::Unknown,
              OwnerTuple{}, kGeneration) == DurableWriterAction::None &&
            state.engaged && SameOwner(state.owner, kOwnerA) &&
            state.epoch == 2 && !state.requiresRelease,
        "unknown evidence keeps the relationship and never arms requiresRelease");
    test.Check(DurableWriterStep(state, true, false, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::None &&
            state.engaged && state.epoch == 2,
        "the same proven owner keeps the relationship and its epoch");
    test.Check(DurableWriterStep(state, true, false, OwnerEvidence::KnownPresent,
              kOwnerB, kGeneration) == DurableWriterAction::Invalidated &&
            !state.engaged && state.epoch == 3 && !state.requiresRelease &&
            state.owner.generation == 0,
        "a proven owner replacement invalidates and clears WITHOUT arming "
        "release-before-reacquire");

    DurableWriterState absentState;
    absentState.engaged = true;
    absentState.owner = kOwnerA;
    absentState.epoch = 4;
    test.Check(DurableWriterStep(absentState, true, false,
              OwnerEvidence::KnownAbsent, OwnerTuple{}, kGeneration) ==
              DurableWriterAction::Invalidated &&
            !absentState.engaged && absentState.epoch == 5 &&
            absentState.requiresRelease,
        "proven absence invalidates and arms requiresRelease");

    DurableWriterState replacedGeneration;
    replacedGeneration.engaged = true;
    replacedGeneration.owner = kOwnerA;
    replacedGeneration.epoch = 9;
    test.Check(DurableWriterStep(replacedGeneration, true, false,
              OwnerEvidence::Unknown, OwnerTuple{}, kGeneration + 1) ==
              DurableWriterAction::Invalidated &&
            !replacedGeneration.engaged &&
            replacedGeneration.epoch == 10 &&
            replacedGeneration.requiresRelease,
        "a title generation replacement invalidates and arms requiresRelease");

    DurableWriterState sameGeneration = replacedGeneration;
    sameGeneration.engaged = true;
    sameGeneration.owner = kOwnerA;
    sameGeneration.epoch = 9;
    test.Check(DurableWriterStep(sameGeneration, true, false,
              OwnerEvidence::Unknown, OwnerTuple{}, 0) ==
              DurableWriterAction::None &&
            sameGeneration.engaged && sameGeneration.epoch == 9,
        "an unreadable active generation never fakes a replacement");

    // Lifecycle gates release without arming requiresRelease.
    DurableWriterState gateRelease;
    gateRelease.engaged = true;
    gateRelease.owner = kOwnerA;
    gateRelease.epoch = 6;
    test.Check(DurableWriterStep(gateRelease, false, false,
              OwnerEvidence::Unknown, OwnerTuple{}, kGeneration) ==
              DurableWriterAction::Released &&
            !gateRelease.engaged && gateRelease.epoch == 7 &&
            !gateRelease.requiresRelease && gateRelease.owner.generation == 0,
        "a lifecycle gate releases, clears and does not arm requiresRelease");

    DurableWriterState alreadyReleased;
    alreadyReleased.epoch = 3;
    test.Check(DurableWriterStep(alreadyReleased, false, false,
              OwnerEvidence::Unknown, OwnerTuple{}, kGeneration) ==
              DurableWriterAction::Released &&
            !alreadyReleased.engaged && alreadyReleased.epoch == 3,
        "a lifecycle gate on a released relationship changes nothing");

    // Explicit input release: clear owner, epoch++, requiresRelease intact.
    DurableWriterState inputRelease;
    inputRelease.engaged = true;
    inputRelease.owner = kOwnerA;
    inputRelease.epoch = 2;
    inputRelease.requiresRelease = true;
    test.Check(DurableWriterRelease(inputRelease) ==
              DurableWriterAction::Released &&
            !inputRelease.engaged && inputRelease.epoch == 3 &&
            inputRelease.owner.generation == 0 &&
            inputRelease.requiresRelease,
        "input release clears the owner and advances the epoch but never touches requiresRelease");
}

void TestRequiresReleaseBeforeAcquisition(TestContext& test)
{
    // Default weapon-switch policy (2026-09-28): a proven owner replacement
    // while the Grip is held drops the relationship WITHOUT arming
    // release-before-reacquire, and the still-held Grip re-acquires the new
    // owner through the ordinary spatial admission + pending-intent + F08
    // freshness path. No release-and-press cycle is required.
    SupportGripAdmission admission;
    DurableWriterState held;
    const auto observeGate = [&](bool gripHeld)
    {
        const bool allowed = admission.Observe(gripHeld, false, false);
        held.requiresRelease = admission.requiresRelease;
        return allowed;
    };
    const auto stepHeld = [&](bool gateOk, bool request,
        OwnerEvidence evidence, const OwnerTuple& evidenceOwner)
    {
        const DurableWriterAction action = DurableWriterStep(held, gateOk,
            request, evidence, evidenceOwner, kGeneration);
        admission.requiresRelease = held.requiresRelease;
        return action;
    };
    test.Check(stepHeld(observeGate(true), true, OwnerEvidence::KnownPresent,
              kOwnerA) == DurableWriterAction::Bind &&
            held.engaged && SameOwner(held.owner, kOwnerA),
        "the held request binds a proven current owner");
    test.Check(stepHeld(observeGate(true), false, OwnerEvidence::KnownPresent,
              kOwnerB) == DurableWriterAction::Invalidated &&
            !held.engaged && !held.requiresRelease,
        "the held Grip across A->B drops A without arming release-before-reacquire");
    test.Check(observeGate(true) && !admission.requiresRelease,
        "the still-held Grip is not blocked by the replacement");
    // The re-acquisition is an ordinary pending intent, so the F08 floor still
    // gates it: the replacement owner's own revision cannot satisfy it, and
    // the next publication binds the still-held Grip.
    PendingAcquisitionIntent heldIntent;
    PendingAcquisitionInputs heldOn{};
    heldOn.acquisitionEdge = true;
    heldOn.gripHeld = true;
    heldOn.holdMode = true;
    heldOn.title = GameTitle::Halo3;
    heldOn.generation = kGeneration;
    heldOn.publicationRevision = 10;
    test.Check(PendingAcquisitionStep(heldIntent, heldOn) ==
              PendingAcquisitionAction::Begun && heldIntent.pending,
        "the held replacement begins an ordinary hold-mode intent");
    const bool cachedBreakRequest = PendingAcquisitionRequest(heldIntent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 10);
    test.Check(!cachedBreakRequest &&
            DurableWriterStep(held, true, cachedBreakRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::None && !held.engaged,
        "the replacement owner's own revision cannot satisfy the F08 floor");
    const bool freshBreakRequest = PendingAcquisitionRequest(heldIntent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 12);
    test.Check(freshBreakRequest &&
            DurableWriterStep(held, true, freshBreakRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind &&
            held.engaged && SameOwner(held.owner, kOwnerB),
        "the next publication binds the still-held Grip to B without a release");

    // Proven ABSENCE keeps the arm: a held Grip must not silently bind
    // whatever owner the engine reaches while the weapon slot is empty.
    SupportGripAdmission absentAdmission;
    DurableWriterState absentWriter;
    absentWriter.requiresRelease = absentAdmission.requiresRelease;
    test.Check(DurableWriterStep(absentWriter, true, true,
              OwnerEvidence::KnownPresent, kOwnerA, kGeneration) ==
              DurableWriterAction::Bind,
        "the absence case starts from a bound owner A");
    test.Check(DurableWriterStep(absentWriter, true, false,
              OwnerEvidence::KnownAbsent, OwnerTuple{}, kGeneration) ==
              DurableWriterAction::Invalidated &&
            !absentWriter.engaged && absentWriter.requiresRelease,
        "proven absence still invalidates and arms release-before-reacquire");
    absentAdmission.requiresRelease = absentWriter.requiresRelease;
    const bool heldAfterAbsence = absentAdmission.Observe(true, false, false);
    test.Check(!heldAfterAbsence &&
            DurableWriterStep(absentWriter, heldAfterAbsence, true,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Released && !absentWriter.engaged,
        "the still-held Grip cannot bind B after a proven absence");
    const bool releasedAfterAbsence = absentAdmission.Observe(false, false, false);
    absentWriter.requiresRelease = absentAdmission.requiresRelease;
    test.Check(releasedAfterAbsence && !absentAdmission.requiresRelease &&
            DurableWriterStep(absentWriter, true, true,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind &&
            SameOwner(absentWriter.owner, kOwnerB),
        "release + fresh proof binds the successor after a proven absence");

    // Handedness/role discontinuity (F16 shape): the role change reaches the
    // canonical writer instead of clearing the latch out of band. A held grip
    // makes admission arm requiresRelease and deny the grab, and the writer's
    // lifecycle-gate step tears the still-engaged relationship down: owner
    // cleared, epoch advanced exactly once, requiresRelease retained.
    SupportGripAdmission roleAdmission;
    DurableWriterState writer;
    writer.requiresRelease = roleAdmission.requiresRelease;
    test.Check(DurableWriterStep(writer, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind &&
            writer.engaged && SameOwner(writer.owner, kOwnerA),
        "the handedness case starts from a bound relationship owner A");
    const uint64_t bindEpoch = writer.epoch;
    const bool rolesGate = roleAdmission.Observe(true, false, true);
    test.Check(!rolesGate && roleAdmission.requiresRelease,
        "a held grip across a handedness/role change is denied and arms requiresRelease");
    writer.requiresRelease = roleAdmission.requiresRelease;
    test.Check(DurableWriterStep(writer, false, false, OwnerEvidence::Unknown,
              OwnerTuple{}, kGeneration) == DurableWriterAction::Released &&
            !writer.engaged && !OwnerComplete(writer.owner) &&
            writer.epoch == bindEpoch + 1 && writer.requiresRelease,
        "the canonical writer tears the handedness change down: owner cleared, epoch advanced exactly once, requiresRelease retained");
    roleAdmission.requiresRelease = writer.requiresRelease;
    const bool heldAgain = roleAdmission.Observe(true, false, false);
    writer.requiresRelease = roleAdmission.requiresRelease;
    test.Check(!heldAgain &&
            DurableWriterStep(writer, false, true, OwnerEvidence::KnownPresent,
                kOwnerB, kGeneration) == DurableWriterAction::Released &&
            !writer.engaged,
        "a held re-request cannot bind while the handedness gate stays closed");
    const bool releasedRoleGate = roleAdmission.Observe(false, false, false);
    writer.requiresRelease = roleAdmission.requiresRelease;
    test.Check(releasedRoleGate && !roleAdmission.requiresRelease,
        "releasing the grip after the role change clears requiresRelease");
    test.Check(DurableWriterStep(writer, true, true, OwnerEvidence::KnownPresent,
              kOwnerB, kGeneration) == DurableWriterAction::Bind &&
            writer.engaged && SameOwner(writer.owner, kOwnerB) &&
            writer.epoch == bindEpoch + 2,
        "the next in-zone request with fresh owner B binds at the next epoch");
}

// 2026-09-28 switch policy: the default drop-without-arm (re-acquire through
// ordinary spatial admission) and the optional `two_hand_switch_inherit`
// in-place rebind for a proven, complete, current-generation replacement.
void TestWeaponSwitchPolicy(TestContext& test)
{
    // Default: the replacement drops the relationship without arming, and
    // nothing re-binds it until the ordinary acquisition request arrives.
    DurableWriterState drop;
    test.Check(DurableWriterStep(drop, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind,
        "switch policy: the relationship starts bound to A");
    const uint64_t dropEpoch = drop.epoch;
    test.Check(DurableWriterStep(drop, true, false, OwnerEvidence::KnownPresent,
              kOwnerB, kGeneration) == DurableWriterAction::Invalidated &&
            !drop.engaged && drop.epoch == dropEpoch + 1 &&
            !drop.requiresRelease && !OwnerComplete(drop.owner),
        "switch policy: the default replacement invalidates without arming");
    test.Check(DurableWriterStep(drop, true, false, OwnerEvidence::KnownPresent,
              kOwnerB, kGeneration) == DurableWriterAction::None && !drop.engaged,
        "switch policy: fresh evidence alone never re-binds an unrequested relationship");
    test.Check(DurableWriterStep(drop, true, true, OwnerEvidence::KnownPresent,
              kOwnerB, kGeneration) == DurableWriterAction::Bind &&
            SameOwner(drop.owner, kOwnerB) && !drop.requiresRelease,
        "switch policy: the spatial acquisition request re-binds the new owner");

    // Inherit on: a complete, current-generation replacement rebinds in place.
    DurableWriterState inherit;
    test.Check(DurableWriterStep(inherit, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind,
        "switch policy: inheritance starts from a bound owner A");
    const uint64_t inheritEpoch = inherit.epoch;
    test.Check(DurableWriterStep(inherit, true, false,
              OwnerEvidence::KnownPresent, kOwnerB, kGeneration, true) ==
              DurableWriterAction::Bind &&
            inherit.engaged && SameOwner(inherit.owner, kOwnerB) &&
            inherit.epoch == inheritEpoch + 1 && !inherit.requiresRelease,
        "switch policy: inheritance rebinds the replacement owner in place without arming");
    test.Check(DurableWriterStep(inherit, true, false,
              OwnerEvidence::KnownPresent, kOwnerB, kGeneration, true) ==
              DurableWriterAction::None && inherit.engaged &&
            inherit.epoch == inheritEpoch + 1,
        "switch policy: the inherited owner keeps the relationship and its epoch");

    // Fallbacks: incomplete, foreign-title and stale-generation replacements
    // never inherit; they take the default drop instead.
    DurableWriterState incomplete;
    incomplete.engaged = true;
    incomplete.owner = kOwnerA;
    incomplete.epoch = 4;
    test.Check(DurableWriterStep(incomplete, true, false,
              OwnerEvidence::KnownPresent,
              OwnerTuple{GameTitle::Halo3, kGeneration, kOwnerA.unit, 0xFFFFFFFFu},
              kGeneration, true) == DurableWriterAction::Invalidated &&
            !incomplete.engaged && incomplete.epoch == 5 &&
            !incomplete.requiresRelease,
        "switch policy: an incomplete replacement falls back to the default drop");
    DurableWriterState foreign;
    foreign.engaged = true;
    foreign.owner = kOwnerA;
    foreign.epoch = 4;
    test.Check(DurableWriterStep(foreign, true, false, OwnerEvidence::KnownPresent,
              OwnerTuple{GameTitle::HaloReach, kGeneration, kOwnerA.unit,
                  kOwnerB.weapon}, kGeneration, true) ==
              DurableWriterAction::Invalidated &&
            !foreign.engaged && !foreign.requiresRelease,
        "switch policy: a foreign-title replacement falls back to the default drop");
    DurableWriterState stale;
    stale.engaged = true;
    stale.owner = kOwnerA;
    stale.epoch = 4;
    test.Check(DurableWriterStep(stale, true, false, OwnerEvidence::KnownPresent,
              OwnerTuple{GameTitle::Halo3, kGeneration - 1, kOwnerA.unit,
                  kOwnerB.weapon}, kGeneration, true) ==
              DurableWriterAction::Invalidated &&
            !stale.engaged && !stale.requiresRelease,
        "switch policy: a stale-generation replacement falls back to the default drop");
    // Absence, generation replacement and unknown evidence are untouched by
    // the option.
    DurableWriterState absent;
    absent.engaged = true;
    absent.owner = kOwnerA;
    absent.epoch = 4;
    test.Check(DurableWriterStep(absent, true, false, OwnerEvidence::KnownAbsent,
              OwnerTuple{}, kGeneration, true) ==
              DurableWriterAction::Invalidated &&
            !absent.engaged && absent.requiresRelease,
        "switch policy: proven absence still arms with inheritance on");
    DurableWriterState generation;
    generation.engaged = true;
    generation.owner = kOwnerA;
    generation.epoch = 4;
    test.Check(DurableWriterStep(generation, true, false,
              OwnerEvidence::KnownPresent, kOwnerB, kGeneration + 1, true) ==
              DurableWriterAction::Invalidated &&
            !generation.engaged && generation.requiresRelease &&
            generation.epoch == 5,
        "switch policy: a generation replacement still arms, inheritance or not");
    DurableWriterState unknown;
    unknown.engaged = true;
    unknown.owner = kOwnerA;
    unknown.epoch = 4;
    test.Check(DurableWriterStep(unknown, true, false, OwnerEvidence::Unknown,
              OwnerTuple{}, kGeneration, true) == DurableWriterAction::None &&
            unknown.engaged && unknown.epoch == 4,
        "switch policy: unknown evidence still retains the relationship");
}

// 2026-09-28 headset follow-up: the default (no-inherit) weapon switch must not
// pre-hold the replacement. The proven-replacement drop arms a crossing
// requirement; the level-triggered hold-mode acquisition is gated by it until
// the zone has been observed FALSE at least once (the fresh crossing, which the
// player makes by bringing the weapon/grab point to the hand), while the inherit
// option never arms it and an explicit release or a teardown clears it.
void TestDefaultSwitchZoneReentry(TestContext& test)
{
    // The drop that arms the requirement: engaged A -> proven different owner
    // B, dropped without arming.
    DurableWriterState drop;
    test.Check(DurableWriterStep(drop, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind,
        "zone re-entry: the relationship is bound before the switch");
    test.Check(DurableWriterStep(drop, true, true, OwnerEvidence::KnownPresent,
              kOwnerB, kGeneration) == DurableWriterAction::Invalidated &&
            !drop.engaged && !drop.requiresRelease,
        "zone re-entry: the default replacement drop ends the relationship without arming");
    constexpr bool differentOwnerClaim = true;
    test.Check(DefaultSwitchReplacementDrop(true,
              DurableWriterAction::Invalidated, differentOwnerClaim,
              /*lifecycleIdentityLost=*/false, /*switchInherit=*/false),
        "zone re-entry: the no-inherit replacement drop arms the crossing requirement");
    // Only that transition arms it.
    test.Check(!DefaultSwitchReplacementDrop(/*engagedBefore=*/false,
              DurableWriterAction::Invalidated, differentOwnerClaim, false, false),
        "zone re-entry: an invalidation of a relationship that was not engaged arms nothing");
    test.Check(!DefaultSwitchReplacementDrop(true, DurableWriterAction::Bind,
              differentOwnerClaim, false, false),
        "zone re-entry: an in-place inheritance rebind is not a drop");
    test.Check(!DefaultSwitchReplacementDrop(true,
              DurableWriterAction::Released, differentOwnerClaim, false, false),
        "zone re-entry: a lifecycle-gate release is not a drop");
    test.Check(!DefaultSwitchReplacementDrop(true,
              DurableWriterAction::Invalidated, /*evidenceIsDifferentOwner=*/false,
              false, false),
        "zone re-entry: a proven absence is not a drop of a different owner");
    test.Check(!DefaultSwitchReplacementDrop(true,
              DurableWriterAction::Invalidated, differentOwnerClaim,
              /*lifecycleIdentityLost=*/true, false),
        "zone re-entry: a generation/lifecycle replacement arms nothing");
    test.Check(!DefaultSwitchReplacementDrop(true,
              DurableWriterAction::Invalidated, differentOwnerClaim, false,
              /*switchInherit=*/true),
        "zone re-entry: the inherit option never arms the crossing requirement");

    // A holster draw with the inherit option: the teardown arms nothing, the
    // hold waits out the gesture and comes back once owner evidence can bind.
    {
        auto step = StepGestureInherit(false, true, true, true, false, true, true, false, true);
        test.Check(step.keepsHold && step.pending && !step.resume,
            "gesture inherit: the gesture's teardown keeps the hold and arms nothing");
        step = StepGestureInherit(step.pending, true, false, true, false, true, true, false, true);
        test.Check(!step.keepsHold && step.pending && !step.resume,
            "gesture inherit: it waits while the gesture lasts");
        step = StepGestureInherit(step.pending, true, false, false, false, true, true, false, false);
        test.Check(step.resume && step.pending,
            "gesture inherit: unreadable evidence keeps the wait for a later update");
        step = StepGestureInherit(step.pending, true, false, false, false, true, true, false, true);
        test.Check(step.resume && !step.pending,
            "gesture inherit: the hold comes back after the gesture, once");
        test.Check(!StepGestureInherit(false, true, true, false, false, true, true, false, true).pending,
            "gesture inherit: no gesture, nothing to do");
        test.Check(!StepGestureInherit(false, false, true, true, false, true, true, false, true).keepsHold,
            "gesture inherit: option off keeps release-before-reacquire");
        test.Check(!StepGestureInherit(true, true, false, true, false, true, false, false, true).pending,
            "gesture inherit: letting go of the grip ends the wait");
        test.Check(!StepGestureInherit(true, true, false, true, false, false, false, true, true).pending,
            "gesture inherit: a toggle press ends the wait");
        test.Check(!StepGestureInherit(true, true, false, false, true, true, true, false, true).resume,
            "gesture inherit: a broken lifecycle ends the wait");
        const auto toggled = StepGestureInherit(true, true, false, false, false, false, false, false, true);
        test.Check(toggled.resume && !toggled.pending,
            "gesture inherit: toggle mode comes back with no grip held");
    }

    // The requirement survives a zone reading that stays TRUE, so the held Grip
    // that never left the replacement's grab zone cannot acquire - the installed
    // defect re-bound 38-51 ms after the drop, i.e. within the next updates.
    const SwitchZoneReentryStep stuck =
        StepSwitchZoneReentry(/*awaitZoneReentry=*/true, /*gripHeld=*/true,
            /*inZone=*/true);
    test.Check(stuck.awaitZoneReentry && !stuck.holdAcquisitionEdge,
        "zone re-entry: a held Grip that stayed inside the zone may not re-acquire");
    const SwitchZoneReentryStep stillStuck =
        StepSwitchZoneReentry(stuck.awaitZoneReentry, true, true);
    test.Check(stillStuck.awaitZoneReentry && !stillStuck.holdAcquisitionEdge,
        "zone re-entry: the block holds for as long as the zone stays true");
    // The first FALSE reading is the fresh crossing: it satisfies the
    // requirement (without itself acquiring) and the next held/in-zone update is
    // an ordinary acquisition edge again.
    const SwitchZoneReentryStep left =
        StepSwitchZoneReentry(stillStuck.awaitZoneReentry, true, /*inZone=*/false);
    test.Check(!left.awaitZoneReentry && !left.holdAcquisitionEdge,
        "zone re-entry: the zone leaving the hand is the crossing and is not itself an acquisition");
    const SwitchZoneReentryStep returned =
        StepSwitchZoneReentry(left.awaitZoneReentry, true, true);
    test.Check(returned.holdAcquisitionEdge,
        "zone re-entry: the next held/in-zone update after the crossing acquires again");
    // Hold mode still requires the physical Grip, crossing or not; with the
    // requirement clear the edge is exactly the pre-existing level-triggered
    // rule.
    test.Check(!StepSwitchZoneReentry(false, /*gripHeld=*/false, true).holdAcquisitionEdge &&
            !StepSwitchZoneReentry(true, false, true).holdAcquisitionEdge,
        "zone re-entry: hold mode still requires the physical Grip");
    test.Check(StepSwitchZoneReentry(false, true, true).holdAcquisitionEdge,
        "zone re-entry: with the requirement clear the held/in-zone edge is the pre-existing rule");

    // Seam rehearsal: the drop wakes no release, the next held/in-zone updates
    // are refused, the crossing begins the ordinary hold-mode intent, and the
    // bind still passes the F08 revision floor (no release/press cycle).
    SupportGripAdmission admission;
    DurableWriterState writer;
    writer.requiresRelease = admission.requiresRelease;
    test.Check(DurableWriterStep(writer, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind,
        "zone re-entry seam: the relationship is bound to A");
    test.Check(DurableWriterStep(writer, true, true, OwnerEvidence::KnownPresent,
              kOwnerB, kGeneration) == DurableWriterAction::Invalidated &&
            !writer.requiresRelease,
        "zone re-entry seam: the switch drops A and stays unarmed");
    bool awaitZoneReentry = DefaultSwitchReplacementDrop(true,
        DurableWriterAction::Invalidated, differentOwnerClaim, false, false);
    SwitchZoneReentryStep step =
        StepSwitchZoneReentry(awaitZoneReentry, true, true);
    awaitZoneReentry = step.awaitZoneReentry;
    test.Check(awaitZoneReentry && !step.holdAcquisitionEdge,
        "zone re-entry seam: the still-held Grip in the new zone does not re-acquire");
    step = StepSwitchZoneReentry(awaitZoneReentry, true, false);
    awaitZoneReentry = step.awaitZoneReentry;
    test.Check(!awaitZoneReentry,
        "zone re-entry seam: the grab point leaving the zone satisfies the requirement");
    step = StepSwitchZoneReentry(awaitZoneReentry, true, true);
    awaitZoneReentry = step.awaitZoneReentry;
    PendingAcquisitionIntent intent;
    PendingAcquisitionInputs held{};
    held.acquisitionEdge = step.holdAcquisitionEdge;
    held.gripHeld = true;
    held.holdMode = true;
    held.title = GameTitle::Halo3;
    held.generation = kGeneration;
    held.publicationRevision = 30;
    test.Check(PendingAcquisitionStep(intent, held) ==
              PendingAcquisitionAction::Begun,
        "zone re-entry seam: the crossing begins the ordinary hold-mode intent");
    test.Check(!PendingAcquisitionRequest(intent, GameTitle::Halo3, kGeneration,
              OwnerEvidence::KnownPresent, 30) &&
            PendingAcquisitionRequest(intent, GameTitle::Halo3, kGeneration,
                OwnerEvidence::KnownPresent, 31),
        "zone re-entry seam: the F08 revision floor still gates the re-acquisition");
    const bool request = PendingAcquisitionRequest(intent, GameTitle::Halo3,
        kGeneration, OwnerEvidence::KnownPresent, 31);
    test.Check(request &&
            DurableWriterStep(writer, true, request, OwnerEvidence::KnownPresent,
                kOwnerB, kGeneration) == DurableWriterAction::Bind &&
            writer.engaged && SameOwner(writer.owner, kOwnerB) &&
            !writer.requiresRelease,
        "zone re-entry seam: the fresh crossing binds the switched owner with no release");
}

void TestAcquisitionOrder(TestContext& test)
{
    // H4-E (A003): the acquisition sequence the pair fix unblocks. The first
    // readable FP record of a pair is normally the storm-hands record, so its
    // owner read is provisional Unknown; the later held-weapon record
    // publishes KnownPresent. This steps the production order
    // (UpdateTwoHandHold -> DurableWriterStep).
    constexpr uint32_t generation = 9;
    const OwnerTuple owner{GameTitle::Halo4, generation, 0x0BAD0001u,
        0x0C0FFEE1u};
    SupportGripAdmission admission;
    DurableWriterState writer;
    writer.requiresRelease = admission.requiresRelease;
    test.Check(DurableWriterStep(writer, true, false, OwnerEvidence::Unknown,
              OwnerTuple{}, generation) == DurableWriterAction::None &&
            !writer.engaged && writer.epoch == 1,
        "H4-E: a provisional unknown invocation binds nothing");
    const bool holdDesired = UpdateTwoHandHold(
        writer.engaged, /*gripHeld=*/true, /*inGrabZone=*/true);
    test.Check(holdDesired,
        "H4-E: a held grip inside the zone requests acquisition");
    test.Check(DurableWriterStep(writer, true, holdDesired,
              OwnerEvidence::Unknown, OwnerTuple{}, generation) ==
              DurableWriterAction::None &&
            !writer.engaged,
        "H4-E: the held/in-zone request waits while the owner is unproven");
    test.Check(DurableWriterStep(writer, true, holdDesired,
              OwnerEvidence::KnownPresent, OwnerTuple{}, generation) ==
              DurableWriterAction::None &&
            !writer.engaged,
        "H4-E: present evidence without a complete owner waits too");
    test.Check(DurableWriterStep(writer, true, holdDesired,
              OwnerEvidence::KnownPresent, owner, generation) ==
              DurableWriterAction::Bind &&
            writer.engaged && SameOwner(writer.owner, owner),
        "H4-E: the proven current owner binds the held/in-zone request");
    test.Check(InvocationOwnerTrusted(writer.engaged, writer.owner,
              OwnerEvidence::KnownPresent, owner),
        "H4-E: the bound owner makes the pair's invocation trusted");
    test.Check(!UpdateTwoHandHold(writer.engaged, false, true) &&
            UpdateTwoHandHold(writer.engaged, true, false),
        "UpdateTwoHandHold keeps admission-only proximity and logical retention");
}

void TestPendingAcquisitionIntent(TestContext& test)
{
    // F12/A3: toggle mode. A click released before the owner proof still binds
    // on a later update.
    PendingAcquisitionIntent toggle;
    PendingAcquisitionInputs click;
    click.acquisitionEdge = true;
    click.gripHeld = true;
    click.holdMode = false;
    click.title = GameTitle::Halo3;
    click.generation = kGeneration;
    click.publicationRevision = 10;
    test.Check(PendingAcquisitionStep(toggle, click) ==
              PendingAcquisitionAction::Begun &&
            toggle.pending && toggle.evidenceFloor == 10 &&
            toggle.title == GameTitle::Halo3 &&
            toggle.generation == kGeneration && !toggle.holdMode,
        "an acquisition edge begins a pending intent and records the evidence floor");
    PendingAcquisitionInputs buttonUp = click;
    buttonUp.acquisitionEdge = false;
    buttonUp.gripHeld = false;
    test.Check(PendingAcquisitionStep(toggle, buttonUp) ==
              PendingAcquisitionAction::None &&
            toggle.pending,
        "a toggle intent survives the physical button-up");
    test.Check(!PendingAcquisitionRequest(toggle, GameTitle::Halo3, kGeneration,
              OwnerEvidence::KnownPresent, 10),
        "the cached KnownPresent from the intent's own revision cannot bind");
    test.Check(PendingAcquisitionRequest(toggle, GameTitle::Halo3, kGeneration,
              OwnerEvidence::KnownPresent, 12) &&
            toggle.pending,
        "a KnownPresent published after the intent binds (click released before owner proof)");
    test.Check(!PendingAcquisitionRequest(toggle, GameTitle::Halo3, kGeneration,
              OwnerEvidence::KnownAbsent, 12) &&
            !PendingAcquisitionRequest(toggle, GameTitle::Halo3, kGeneration,
              OwnerEvidence::Unknown, 12),
        "absence and unknown publications never satisfy the pending intent");
    test.Check(!PendingAcquisitionRequest(toggle, GameTitle::HaloReach,
              kGeneration, OwnerEvidence::KnownPresent, 12) &&
            !PendingAcquisitionRequest(toggle, GameTitle::Halo3, kGeneration + 1,
              OwnerEvidence::KnownPresent, 12),
        "another title or generation cannot satisfy the pending intent");

    // F12/A3: hold mode. The intent lives only while the Grip is held.
    PendingAcquisitionIntent hold;
    PendingAcquisitionInputs holdClick = click;
    holdClick.holdMode = true;
    test.Check(PendingAcquisitionStep(hold, holdClick) ==
              PendingAcquisitionAction::Begun && hold.pending && hold.holdMode,
        "a hold-mode request begins a pending intent");
    PendingAcquisitionInputs stillHeld = holdClick;
    stillHeld.acquisitionEdge = false;
    stillHeld.publicationRevision = 11;
    test.Check(PendingAcquisitionStep(hold, stillHeld) ==
              PendingAcquisitionAction::None &&
            hold.pending,
        "a hold-mode intent persists while the Grip stays held");
    PendingAcquisitionInputs released = stillHeld;
    released.gripHeld = false;
    test.Check(PendingAcquisitionStep(hold, released) ==
              PendingAcquisitionAction::Elapsed &&
            !hold.pending,
        "a hold-mode intent is cancelled by releasing the Grip");
    test.Check(!PendingAcquisitionRequest(hold, GameTitle::Halo3, kGeneration,
              OwnerEvidence::KnownPresent, 12),
        "an elapsed hold intent can never bind later");

    // Cancel policy: explicit second toggle, title/generation replacement,
    // lifecycle/role/secondary invalidation, and an already-engaged
    // relationship.
    const auto freshToggleIntent = [&]()
    {
        PendingAcquisitionIntent intent;
        PendingAcquisitionStep(intent, click);
        return intent;
    };
    PendingAcquisitionIntent cancelled = freshToggleIntent();
    PendingAcquisitionInputs secondToggle = buttonUp;
    secondToggle.cancelRequested = true;
    test.Check(PendingAcquisitionStep(cancelled, secondToggle) ==
              PendingAcquisitionAction::Cancelled &&
            !cancelled.pending,
        "an explicit second toggle cancels a pending intent");
    cancelled = freshToggleIntent();
    PendingAcquisitionInputs newTitle = buttonUp;
    newTitle.title = GameTitle::HaloReach;
    test.Check(PendingAcquisitionStep(cancelled, newTitle) ==
              PendingAcquisitionAction::Cancelled &&
            !cancelled.pending,
        "a title replacement cancels a pending intent");
    cancelled = freshToggleIntent();
    PendingAcquisitionInputs newGeneration = buttonUp;
    newGeneration.generation = kGeneration + 1;
    test.Check(PendingAcquisitionStep(cancelled, newGeneration) ==
              PendingAcquisitionAction::Cancelled &&
            !cancelled.pending,
        "a generation replacement cancels a pending intent");
    cancelled = freshToggleIntent();
    PendingAcquisitionInputs invalidLifecycle = buttonUp;
    invalidLifecycle.lifecycleValid = false;
    test.Check(PendingAcquisitionStep(cancelled, invalidLifecycle) ==
              PendingAcquisitionAction::Cancelled &&
            !cancelled.pending,
        "a role/secondary/lifecycle invalidation cancels a pending intent");
    cancelled = freshToggleIntent();
    PendingAcquisitionInputs engagedInputs = buttonUp;
    engagedInputs.engaged = true;
    test.Check(PendingAcquisitionStep(cancelled, engagedInputs) ==
              PendingAcquisitionAction::Cancelled &&
            !cancelled.pending,
        "an engaged relationship ends a pending intent");
    PendingAcquisitionIntent never;
    PendingAcquisitionInputs noEdge = buttonUp;
    test.Check(PendingAcquisitionStep(never, noEdge) ==
              PendingAcquisitionAction::None &&
            !never.pending,
        "no acquisition edge never begins an intent");
    PendingAcquisitionInputs cancelledEdge = click;
    cancelledEdge.cancelRequested = true;
    test.Check(PendingAcquisitionStep(never, cancelledEdge) ==
              PendingAcquisitionAction::None &&
            !never.pending,
        "a cancel request never begins an intent");

    // F08/A2 end-to-end: a cached previous-owner record cannot create the
    // relationship, and the new owner binds once its own publication arrives.
    PendingAcquisitionIntent intent;
    PendingAcquisitionStep(intent, click);
    DurableWriterState writer;
    const bool staleRequest = PendingAcquisitionRequest(intent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 10);
    test.Check(!staleRequest &&
            DurableWriterStep(writer, true, staleRequest,
                OwnerEvidence::KnownPresent, kOwnerA, kGeneration) ==
                DurableWriterAction::None &&
            !writer.engaged,
        "a KnownPresent published before the intent never creates a relationship");
    const bool freshRequest = PendingAcquisitionRequest(intent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 12);
    test.Check(freshRequest &&
            DurableWriterStep(writer, true, freshRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind &&
            writer.engaged && SameOwner(writer.owner, kOwnerB),
        "the new owner's newer publication binds the pending request");
    PendingAcquisitionInputs afterBind = buttonUp;
    afterBind.engaged = writer.engaged;
    afterBind.publicationRevision = 12;
    test.Check(PendingAcquisitionStep(intent, afterBind) ==
              PendingAcquisitionAction::Cancelled &&
            !intent.pending,
        "the pending intent is consumed once the relationship is engaged");
}

void TestLifecycleIdentity(TestContext& test)
{
    // F02: what the durable relationship may survive. Paused is retained
    // (product policy), a pose outage is not a lifecycle change at all, and
    // mode ambiguity is treated like Unknown evidence.
    const OwnerTuple owner{GameTitle::Halo3, kGeneration, 0x11110001u,
        0x22220001u};
    const auto valid = [&](RuntimeMode mode)
    {
        return DurableLifecycleIdentifiesOwner(GameTitle::Halo3, kGeneration,
            mode);
    };
    test.Check(valid(RuntimeMode::Gameplay) && valid(RuntimeMode::Paused) &&
            valid(RuntimeMode::Cutscene) && valid(RuntimeMode::Vehicle) &&
            valid(RuntimeMode::Turret),
        "gameplay, pause, cutscene, vehicle and turret lifecycles can hold the owner");
    test.Check(!valid(RuntimeMode::Shell) && !valid(RuntimeMode::Loading) &&
            !valid(RuntimeMode::Dead),
        "shell, loading and dead lifecycles cannot hold the owner");
    test.Check(valid(RuntimeMode::Unsupported),
        "mode ambiguity is retained like Unknown evidence, not read as absence");
    test.Check(!DurableLifecycleIdentifiesOwner(GameTitle::None, kGeneration,
              RuntimeMode::Gameplay) &&
            !DurableLifecycleIdentifiesOwner(GameTitle::Unknown, kGeneration,
              RuntimeMode::Gameplay) &&
            !DurableLifecycleIdentifiesOwner(GameTitle::Halo3, 0,
              RuntimeMode::Gameplay),
        "an unavailable title or generation never identifies an owner");
    test.Check(!DurableRelationshipIdentityLost(GameTitle::Halo3, kGeneration,
              RuntimeMode::Paused, owner) &&
            !DurableRelationshipIdentityLost(GameTitle::Halo3, kGeneration,
              RuntimeMode::Unsupported, owner),
        "the same title/generation survives pause and mode ambiguity");
    test.Check(DurableRelationshipIdentityLost(GameTitle::HaloReach,
              kGeneration, RuntimeMode::Gameplay, owner),
        "another title owning the frame loses the relationship identity");
    test.Check(DurableRelationshipIdentityLost(GameTitle::Halo3,
              kGeneration + 1, RuntimeMode::Gameplay, owner) &&
            DurableRelationshipIdentityLost(GameTitle::Halo3, 0,
              RuntimeMode::Gameplay, owner),
        "a replaced or unavailable generation loses the relationship identity");
    test.Check(DurableRelationshipIdentityLost(GameTitle::Halo3, kGeneration,
              RuntimeMode::Loading, owner) &&
            DurableRelationshipIdentityLost(GameTitle::None, 0,
              RuntimeMode::Shell, owner),
        "a loading or shell lifecycle terminates the engaged relationship");
}

void TestPersistentGripWriterSeam(TestContext& test)
{
    // The vr.cpp seam's exact ordering, rehearsed purely. Every teardown that
    // is not an ordinary release arms the shared admission BEFORE the policy
    // step clears the owner, and a pose outage takes no step at all.
    SupportGripAdmission admission;
    DurableWriterState writer;
    writer.requiresRelease = admission.requiresRelease;
    test.Check(DurableWriterStep(writer, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind &&
            writer.engaged && SameOwner(writer.owner, kOwnerA) &&
            writer.epoch == 2,
        "seam: the writer binds a proven current owner");

    // F02: a title change while the Grip is held. admission.Arm() runs first
    // (the writer's forcedTeardown arming), then the lifecycle gate closes, so
    // the still-held Grip cannot silently bind the next title's owner.
    admission.Arm();
    writer.requiresRelease = admission.requiresRelease;
    test.Check(DurableWriterStep(writer, false, true, OwnerEvidence::KnownPresent,
              kOwnerB, kGeneration) == DurableWriterAction::Released &&
            !writer.engaged && writer.epoch == 3 && writer.requiresRelease &&
            writer.owner.generation == 0,
        "seam: a lifecycle teardown clears the owner, advances the epoch once and keeps the arming");
    const bool heldAfterLifecycleTeardown = admission.Observe(true, false, false);
    test.Check(!heldAfterLifecycleTeardown && admission.requiresRelease,
        "seam: the still-held Grip stays blocked after the lifecycle teardown");
    const bool releasedAfterLifecycle = admission.Observe(false, false, false);
    writer.requiresRelease = admission.requiresRelease;
    test.Check(releasedAfterLifecycle && !writer.requiresRelease,
        "seam: the physical release clears the shared arming state");

    // F16: the handedness/role teardown. vr.cpp arms and routes it through
    // DurableWriterRelease so the owner and the epoch move together instead of
    // leaving a stale owner in the publication.
    SupportGripAdmission roleAdmission;
    DurableWriterState roleWriter;
    roleWriter.requiresRelease = roleAdmission.requiresRelease;
    test.Check(DurableWriterStep(roleWriter, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind,
        "seam: the role teardown starts from a bound owner");
    const uint64_t boundEpoch = roleWriter.epoch;
    const bool roleGate = roleAdmission.Observe(true, false, true);
    roleWriter.requiresRelease = roleAdmission.requiresRelease;
    test.Check(!roleGate && roleWriter.requiresRelease &&
            DurableWriterRelease(roleWriter) == DurableWriterAction::Released &&
            !roleWriter.engaged && roleWriter.owner.generation == 0 &&
            roleWriter.epoch == boundEpoch + 1 && roleWriter.requiresRelease,
        "seam: the role teardown releases through the writer (owner cleared, epoch once, arming kept)");

    // F11: an interaction gesture that consumes a held Grip. Arming first, then
    // the release, then release + a fresh owner proof may bind the new owner.
    SupportGripAdmission gestureAdmission;
    DurableWriterState gestureWriter;
    gestureWriter.requiresRelease = gestureAdmission.requiresRelease;
    test.Check(DurableWriterStep(gestureWriter, true, true,
              OwnerEvidence::KnownPresent, kOwnerA, kGeneration) ==
              DurableWriterAction::Bind,
        "seam: the gesture teardown starts from a bound owner");
    gestureAdmission.Arm();
    gestureWriter.requiresRelease = gestureAdmission.requiresRelease;
    test.Check(DurableWriterRelease(gestureWriter) ==
              DurableWriterAction::Released &&
            !gestureWriter.engaged && gestureWriter.requiresRelease &&
            !gestureAdmission.Observe(true, false, false),
        "seam: the held Grip is blocked after the gesture teardown");
    const bool gestureRelease = gestureAdmission.Observe(false, false, false);
    gestureWriter.requiresRelease = gestureAdmission.requiresRelease;
    test.Check(gestureRelease &&
            DurableWriterStep(gestureWriter, true, true,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind &&
            SameOwner(gestureWriter.owner, kOwnerB),
        "seam: release + fresh proof binds the replacement owner");

    // Owner break BEFORE acquisition (B2 step 2 before step 3): a proven
    // different owner drops A in the very update that carries a bindable,
    // fresh-floor acquisition request, WITHOUT arming, so the still-pending
    // request binds B on the next update and no release is needed.
    SupportGripAdmission breakAdmission;
    DurableWriterState breakWriter;
    breakWriter.requiresRelease = breakAdmission.requiresRelease;
    test.Check(DurableWriterStep(breakWriter, true, true, OwnerEvidence::KnownPresent,
              kOwnerA, kGeneration) == DurableWriterAction::Bind,
        "seam: owner-break ordering starts from a bound owner");
    PendingAcquisitionIntent breakIntent;
    PendingAcquisitionInputs breakClick{};
    breakClick.acquisitionEdge = true;
    breakClick.gripHeld = true;
    breakClick.title = GameTitle::Halo3;
    breakClick.generation = kGeneration;
    breakClick.publicationRevision = 10;
    test.Check(PendingAcquisitionStep(breakIntent, breakClick) ==
              PendingAcquisitionAction::Begun,
        "seam: the acquisition intent begins from the click");
    const bool breakRequest = PendingAcquisitionRequest(breakIntent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 12);
    test.Check(breakRequest &&
            DurableWriterStep(breakWriter, true, breakRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Invalidated &&
            !breakWriter.engaged && !breakWriter.requiresRelease,
        "seam: the owner break beats a bindable acquisition request in the same update and does not arm");
    test.Check(DurableWriterStep(breakWriter, true, breakRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind &&
            breakWriter.engaged && SameOwner(breakWriter.owner, kOwnerB),
        "seam: the still-pending request binds B on the next update without a release");

    // B2 step 1: a pose outage takes no step, so the relationship and its epoch
    // survive suspended, and the first recovered update still applies the
    // owner-break rule instead of hiding a weapon change behind the outage.
    DurableWriterState suspended;
    suspended.engaged = true;
    suspended.owner = kOwnerA;
    suspended.epoch = 5;
    test.Check(DurableWriterStep(suspended, true, false,
              OwnerEvidence::KnownPresent, kOwnerA, kGeneration) ==
              DurableWriterAction::None &&
            suspended.engaged && suspended.epoch == 5 && !suspended.requiresRelease,
        "the recovered same-owner update keeps the suspended relationship untouched");
    test.Check(DurableWriterStep(suspended, true, false,
              OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
              DurableWriterAction::Invalidated &&
            suspended.epoch == 6 && !suspended.requiresRelease,
        "the first update after a pose outage still applies the owner-break rule without arming");
}

void TestPendingIntentOutcomes(TestContext& test)
{
    // The four required outcomes of the B3 pending-intent policy, driven the
    // way the writer drives them (the physical Grip edge and the existing
    // admission gate are the outer inputs).
    const auto bindA = [&](DurableWriterState& writer, SupportGripAdmission& adm)
    {
        adm.requiresRelease = writer.requiresRelease;
        return DurableWriterStep(writer, true, true, OwnerEvidence::KnownPresent,
            kOwnerA, kGeneration);
    };
    // (i) A toggle click released before the owner proof still binds later.
    PendingAcquisitionIntent toggle;
    PendingAcquisitionInputs click{};
    click.acquisitionEdge = true;
    click.gripHeld = true;
    click.title = GameTitle::Halo3;
    click.generation = kGeneration;
    click.publicationRevision = 4;
    test.Check(PendingAcquisitionStep(toggle, click) ==
              PendingAcquisitionAction::Begun,
        "(i) the toggle click begins a pending intent");
    PendingAcquisitionInputs buttonUp = click;
    buttonUp.acquisitionEdge = false;
    buttonUp.gripHeld = false;
    test.Check(PendingAcquisitionStep(toggle, buttonUp) ==
              PendingAcquisitionAction::None && toggle.pending,
        "(i) the physical button-up does not cancel the toggle intent");
    SupportGripAdmission toggleAdmission;
    DurableWriterState toggleWriter;
    const bool clickRequest = PendingAcquisitionRequest(toggle, GameTitle::Halo3,
        kGeneration, OwnerEvidence::KnownPresent, 5);
    test.Check(clickRequest &&
            DurableWriterStep(toggleWriter, true, clickRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind &&
            SameOwner(toggleWriter.owner, kOwnerB),
        "(i) the later owner proof binds the released click without a second press");

    // (ii) Hold mode: after an owner break while the Grip is held there is no
    // release-before-reacquire block. The held Grip begins an ordinary
    // hold-mode intent and binds B once B's own publication is fresh.
    SupportGripAdmission holdAdmission;
    DurableWriterState holdWriter;
    test.Check(bindA(holdWriter, holdAdmission) == DurableWriterAction::Bind,
        "(ii) hold mode starts from a bound owner A");
    test.Check(DurableWriterStep(holdWriter, true, false,
              OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
              DurableWriterAction::Invalidated && !holdWriter.requiresRelease,
        "(ii) the owner break drops A without arming while the Grip is held");
    holdAdmission.requiresRelease = holdWriter.requiresRelease;
    const bool holdGate = holdAdmission.Observe(true, false, false);
    holdWriter.requiresRelease = holdAdmission.requiresRelease;
    test.Check(holdGate && !holdAdmission.requiresRelease,
        "(ii) the still-held Grip stays admissible after the replacement");
    PendingAcquisitionIntent holdIntent;
    PendingAcquisitionInputs heldOn = click;
    heldOn.holdMode = true;
    heldOn.publicationRevision = 9;
    heldOn.lifecycleValid = holdGate;
    test.Check(PendingAcquisitionStep(holdIntent, heldOn) ==
              PendingAcquisitionAction::Begun && holdIntent.holdMode,
        "(ii) the still-held replacement begins an ordinary hold-mode intent");
    const bool cachedHoldRequest = PendingAcquisitionRequest(holdIntent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 9);
    test.Check(!cachedHoldRequest &&
            DurableWriterStep(holdWriter, holdGate, cachedHoldRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::None && !holdWriter.engaged,
        "(ii) the intent's own revision cannot bind (F08 floor)");
    PendingAcquisitionInputs stillHeld = heldOn;
    stillHeld.acquisitionEdge = false;
    stillHeld.publicationRevision = 10;
    test.Check(PendingAcquisitionStep(holdIntent, stillHeld) ==
              PendingAcquisitionAction::None && holdIntent.pending,
        "(ii) the hold-mode intent lives on while the Grip stays held");
    const bool holdRequest = PendingAcquisitionRequest(holdIntent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 11);
    test.Check(holdRequest &&
            DurableWriterStep(holdWriter, holdGate, holdRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind &&
            SameOwner(holdWriter.owner, kOwnerB),
        "(ii) a fresh B publication binds the still-held Grip with no release");

    // (iii) Toggle mode: the owner break drops A without arming, and the next
    // deliberate click in the zone may bind B with fresh proof; no release
    // cycle is imposed by the writer.
    SupportGripAdmission nextAdmission;
    DurableWriterState nextWriter;
    test.Check(bindA(nextWriter, nextAdmission) == DurableWriterAction::Bind &&
            DurableWriterStep(nextWriter, true, false,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Invalidated && !nextWriter.requiresRelease,
        "(iii) the owner break ends A without arming release-before-reacquire");
    nextAdmission.requiresRelease = nextWriter.requiresRelease;
    const bool admissibleHold = nextAdmission.Observe(true, false, false);
    const bool nextReleased = nextAdmission.Observe(false, false, false);
    nextWriter.requiresRelease = nextAdmission.requiresRelease;
    test.Check(admissibleHold && nextReleased && !nextWriter.requiresRelease,
        "(iii) the still-held click stays admissible and the release changes nothing");
    PendingAcquisitionIntent nextIntent;
    PendingAcquisitionInputs nextClick{};
    nextClick.acquisitionEdge = true;
    nextClick.gripHeld = true;
    nextClick.title = GameTitle::Halo3;
    nextClick.generation = kGeneration;
    nextClick.publicationRevision = 20;
    test.Check(PendingAcquisitionStep(nextIntent, nextClick) ==
              PendingAcquisitionAction::Begun &&
            nextIntent.evidenceFloor == 20,
        "(iii) the first click after the switch begins a fresh intent");
    PendingAcquisitionInputs nextUp = nextClick;
    nextUp.acquisitionEdge = false;
    nextUp.gripHeld = false;
    test.Check(PendingAcquisitionStep(nextIntent, nextUp) ==
              PendingAcquisitionAction::None && nextIntent.pending,
        "(iii) the toggle click survives its own button-up");
    const bool nextRequest = PendingAcquisitionRequest(nextIntent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 21);
    test.Check(nextRequest &&
            DurableWriterStep(nextWriter, true, nextRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind &&
            SameOwner(nextWriter.owner, kOwnerB),
        "(iii) the first deliberate click binds B with fresh proof");

    // (iv) Toggle mode with the pre-switch click still physically held: there
    // is no rising edge, so nothing begins an acquisition and nothing binds;
    // the release plus a new click binds B. This is ordinary toggle semantics,
    // not the removed release-before-reacquire block.
    SupportGripAdmission heldAdmission;
    DurableWriterState heldWriter;
    test.Check(bindA(heldWriter, heldAdmission) == DurableWriterAction::Bind,
        "(iv) the stale click bound A while still physically held");
    test.Check(DurableWriterStep(heldWriter, true, false,
              OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
              DurableWriterAction::Invalidated && !heldWriter.requiresRelease,
        "(iv) the owner break arrives while the click is still held and does not arm");
    heldAdmission.requiresRelease = heldWriter.requiresRelease;
    const bool heldGate = heldAdmission.Observe(true, false, false);
    PendingAcquisitionIntent heldIntent;
    PendingAcquisitionInputs heldStill{};
    heldStill.acquisitionEdge = false; // no rising edge: the click predates the switch
    heldStill.gripHeld = true;
    heldStill.lifecycleValid = heldGate;
    heldStill.title = GameTitle::Halo3;
    heldStill.generation = kGeneration;
    heldStill.publicationRevision = 30;
    test.Check(heldGate &&
            PendingAcquisitionStep(heldIntent, heldStill) ==
                PendingAcquisitionAction::None && !heldIntent.pending,
        "(iv) the still-held click begins no acquisition intent");
    test.Check(DurableWriterStep(heldWriter, heldGate, false,
              OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
              DurableWriterAction::None && !heldWriter.engaged,
        "(iv) no bind while the pre-switch click is still held");
    const bool heldReleased = heldAdmission.Observe(false, false, false);
    heldWriter.requiresRelease = heldAdmission.requiresRelease;
    PendingAcquisitionInputs newClick = heldStill;
    newClick.acquisitionEdge = true; // release + a new click in the zone
    newClick.publicationRevision = 31;
    test.Check(heldReleased && !heldAdmission.requiresRelease &&
            PendingAcquisitionStep(heldIntent, newClick) ==
                PendingAcquisitionAction::Begun,
        "(iv) the new click after the release begins a fresh intent");
    const bool heldRequest = PendingAcquisitionRequest(heldIntent,
        GameTitle::Halo3, kGeneration, OwnerEvidence::KnownPresent, 32);
    test.Check(heldRequest &&
            DurableWriterStep(heldWriter, true, heldRequest,
                OwnerEvidence::KnownPresent, kOwnerB, kGeneration) ==
                DurableWriterAction::Bind,
        "(iv) the new click with fresh proof binds B");
}

void TestEvidenceRevisionFloor(TestContext& test)
{
    // F08 exact regression: cached KnownPresent(A) at revision 10, the
    // acquisition intent starts, revision 10 must NOT bind, and the producer's
    // later KnownPresent(B) at revision 12 binds only through the intent.
    const OwnerTuple ownerA{GameTitle::Halo3, kGeneration, 0x11110001u,
        0x22220001u};
    const OwnerTuple ownerB{GameTitle::Halo3, kGeneration, 0x11110001u,
        0x33330001u};
    PendingAcquisitionIntent intent;
    PendingAcquisitionInputs intentStart{};
    intentStart.acquisitionEdge = true;
    intentStart.gripHeld = true;
    intentStart.title = GameTitle::Halo3;
    intentStart.generation = kGeneration;
    intentStart.publicationRevision = 10;
    test.Check(PendingAcquisitionStep(intent, intentStart) ==
              PendingAcquisitionAction::Begun && intent.evidenceFloor == 10,
        "the acquisition intent records the revision it began from");
    test.Check(!EvidenceRevisionFresh(10, intent.evidenceFloor) &&
            EvidenceRevisionFresh(11, intent.evidenceFloor),
        "only a strictly newer publication satisfies the revision floor");
    DurableWriterState writer;
    const bool cachedRequest = PendingAcquisitionRequest(intent, GameTitle::Halo3,
        kGeneration, OwnerEvidence::KnownPresent, 10);
    test.Check(!cachedRequest &&
            DurableWriterStep(writer, true, cachedRequest,
                OwnerEvidence::KnownPresent, ownerA, kGeneration) ==
                DurableWriterAction::None && !writer.engaged,
        "the cached KnownPresent at the intent's own revision never creates a relationship");
    const bool freshRequest = PendingAcquisitionRequest(intent, GameTitle::Halo3,
        kGeneration, OwnerEvidence::KnownPresent, 12);
    test.Check(freshRequest &&
            DurableWriterStep(writer, true, freshRequest,
                OwnerEvidence::KnownPresent, ownerB, kGeneration) ==
                DurableWriterAction::Bind &&
            SameOwner(writer.owner, ownerB) && writer.epoch == 2,
        "the later publication binds the proven new owner, never the cached one");
    test.Check(!PendingAcquisitionRequest(intent, GameTitle::Halo3,
              kGeneration, OwnerEvidence::KnownAbsent, 13) &&
            !PendingAcquisitionRequest(intent, GameTitle::Halo3,
              kGeneration, OwnerEvidence::Unknown, 14),
        "a newer absence or unknown publication never satisfies the intent");

    // Title-aware owner identity at the durable boundary. vr.cpp's reader maps
    // a present claim naming another title to Unknown before the policy step;
    // this is the same rule one layer down, and it also holds if a caller ever
    // passes the raw record through.
    const OwnerTuple foreignOwner{GameTitle::HaloReach, kGeneration,
        ownerA.unit, ownerA.weapon};
    test.Check(OwnerComplete(foreignOwner) &&
            !SameOwner(foreignOwner, ownerA) &&
            !InvocationOwnerTrusted(true, ownerA,
                OwnerEvidence::KnownPresent, foreignOwner),
        "a numerically identical foreign-title owner is a different owner");
    DurableWriterState heldWriter;
    test.Check(DurableWriterStep(heldWriter, true, true,
              OwnerEvidence::KnownPresent, ownerA, kGeneration) ==
              DurableWriterAction::Bind,
        "the durable boundary test starts from a bound owner");
    test.Check(DurableWriterStep(heldWriter, true, false,
              OwnerEvidence::KnownPresent, foreignOwner, kGeneration) ==
              DurableWriterAction::Invalidated &&
            !heldWriter.requiresRelease,
        "a foreign-title present claim is a proven owner mismatch that drops the relationship without arming");
}

void TestPresentedRayQualification(TestContext& test)
{
    test.Check(PresentedRayOwnerCompatible(false, 0, 0, false, true),
        "an ordinary one-hand ray is accepted with a coherent disengagement");
    test.Check(PresentedRayOwnerCompatible(true, 9, 9, true, false),
        "a trusted same-epoch support ray is accepted");
    test.Check(!PresentedRayOwnerCompatible(true, 9, 9, false, true),
        "a same-epoch ray without trusted-owner proof is rejected");
    test.Check(!PresentedRayOwnerCompatible(true, 10, 9, true, true),
        "a ray from a replaced relationship epoch is rejected");
    test.Check(!PresentedRayOwnerCompatible(false, 0, 9, true, true),
        "a support ray whose relationship has ended is rejected");
    // F14/A4: epoch 0 is not automatically acceptable. The race case is a
    // latch-engaged relationship whose publication still reads disengaged
    // while the pose was already solved support-derived, so the caller cannot
    // assert a coherent disengagement.
    test.Check(!PresentedRayOwnerCompatible(false, 0, 0, false, false),
        "an epoch-0 ray is rejected when a coherent disengagement is not asserted");
    test.Check(!PresentedRayOwnerCompatible(false, 7, 0, false, false),
        "a released relationship with unproven coherence still rejects the epoch-0 ray");
    test.Check(!PresentedRayOwnerCompatible(true, 0, 0, false, true) &&
            !PresentedRayOwnerCompatible(true, 0, 0, false, false),
        "an epoch-0 ray while the relationship reads engaged is rejected, coherent or not");
    test.Check(!PresentedRayOwnerCompatible(true, 9, 0, true, true),
        "a trusted support invocation can never publish an epoch-0 (ordinary) ray");
}

void TestSerialAndReticleContinuity(TestContext& test)
{
    test.Check(PresentedRaySerialCompatible(5, 5) &&
            PresentedRaySerialCompatible(4, 5),
        "the current and immediately previous presented serials are fresh");
    test.Check(!PresentedRaySerialCompatible(3, 5),
        "a presented ray two prepared frames old is stale");
    test.Check(!PresentedRaySerialCompatible(0, 5) &&
            !PresentedRaySerialCompatible(5, 0),
        "a missing presented or current serial is never fresh");
    test.Check(!PresentedRaySerialCompatible(6, 5),
        "a presented ray from a future serial is rejected");

    test.Check(ReticleSmoothingDiscontinuity(false, 4, 4),
        "an owner-unsafe invocation forces a reticle smoothing reseed");
    test.Check(ReticleSmoothingDiscontinuity(true, 5, 4),
        "a support epoch change forces a reticle smoothing reseed");
    test.Check(!ReticleSmoothingDiscontinuity(true, 4, 4),
        "a safe owner on the same support epoch keeps reticle smoothing");
    test.Check(!ReticleSmoothingDiscontinuity(true, 0, 0) &&
            !ReticleSmoothingDiscontinuity(true, UINT64_MAX, UINT64_MAX),
        "reticle smoothing continuity reads owner safety and epoch only");
    // The owner discontinuity is the boundary between an A-owned history and a
    // first-B invocation, so a mismatched owner never blends.
    test.Check(ReticleSmoothingDiscontinuity(false, 9, 9),
        "an owner discontinuity reseeds even on an unchanged support epoch");
}

// T5b: the solve-time qualification every aim assembly runs before it may
// select support-capable raw aim, and the receipt/smoothing consequences the
// runtime derives from it.
void TestSolveSupportQualification(TestContext& test)
{
    // Feature off: no durable read, no forcing, all-zero receipt, so a PG-off
    // solve can never report support provenance.
    const SolveSupportQualification off =
        QualifySolveSupport(false, true, true, 7, true);
    test.Check(!off.forceOneHand && !off.supportTrusted &&
            off.supportEpoch == 0 && !off.relationshipReadable &&
            !off.relationshipEngaged,
        "the feature off never qualifies, forces or reports support provenance");
    // Coherently disengaged: the ordinary one-hand case, epoch 0, no forcing.
    const SolveSupportQualification disengaged =
        QualifySolveSupport(true, true, false, 3, true);
    test.Check(!disengaged.forceOneHand && !disengaged.supportTrusted &&
            disengaged.relationshipReadable &&
            !disengaged.relationshipEngaged && disengaged.supportEpoch == 0,
        "a coherently disengaged relationship is the ordinary one-hand case");
    // Engaged + proven same-owner invocation: support may be consumed.
    const SolveSupportQualification trusted =
        QualifySolveSupport(true, true, true, 3, false);
    test.Check(!trusted.forceOneHand && trusted.supportTrusted &&
            trusted.relationshipReadable && trusted.relationshipEngaged &&
            trusted.supportEpoch == 3,
        "an engaged relationship with a proven invocation may consume support");
    // Engaged + denied: force one-hand, keep the engaged epoch, never 0.
    const SolveSupportQualification denied =
        QualifySolveSupport(true, true, true, 3, true);
    test.Check(denied.forceOneHand && !denied.supportTrusted &&
            denied.relationshipEngaged && denied.supportEpoch == 3 &&
            denied.supportEpoch != 0,
        "a denied invocation forces one-hand but never reports epoch 0");
    // F14: an unreadable relationship is not a disengaged read.
    const SolveSupportQualification unreadable =
        QualifySolveSupport(true, false, false, 0, false);
    test.Check(unreadable.forceOneHand && !unreadable.supportTrusted &&
            !unreadable.relationshipReadable &&
            !unreadable.relationshipEngaged &&
            unreadable.supportEpoch == kUnknownRelationshipEpoch &&
            unreadable.supportEpoch != 0,
        "an unreadable relationship forces one-hand and marks the epoch unknown");
    // Defensive: an engaged read carrying an impossible zero epoch is never
    // allowed to masquerade as a coherent disengagement.
    const SolveSupportQualification impossible =
        QualifySolveSupport(true, true, true, 0, false);
    test.Check(impossible.forceOneHand && !impossible.supportTrusted &&
            impossible.supportEpoch == kUnknownRelationshipEpoch,
        "an engaged read with a zero epoch can never be reported as disengaged");
    // The receipt the producer publishes obeys the same coherence-aware
    // consumer rule: trusted at its own epoch is accepted, a denied or
    // unreadable receipt is refused.
    test.Check(PresentedRayOwnerCompatible(true, trusted.supportEpoch,
              trusted.supportEpoch, trusted.supportTrusted, false),
        "a trusted qualified solve is accepted at its own epoch");
    test.Check(!PresentedRayOwnerCompatible(true, 3, denied.supportEpoch,
              denied.supportTrusted, false),
        "a denied invocation's receipt is refused even at the engaged epoch");
    test.Check(!PresentedRayOwnerCompatible(false, 0, unreadable.supportEpoch,
              unreadable.supportTrusted, true),
        "an unreadable relationship's receipt can never read as an epoch-0 ray");
    // Publisher-side smoothing, using the exact owner-safety expression the
    // producer applies (an engaged relationship is safe only when this
    // invocation is trusted): a denied invocation reseeds, a trusted one on
    // the same epoch keeps the history.
    test.Check(ReticleSmoothingDiscontinuity(
              !denied.relationshipEngaged || denied.supportTrusted,
              denied.supportEpoch, 3),
        "a denied invocation reseeds the reticle history");
    test.Check(!ReticleSmoothingDiscontinuity(
              !trusted.relationshipEngaged || trusted.supportTrusted,
              trusted.supportEpoch, 3),
        "a trusted invocation on the same epoch keeps reticle smoothing");
    test.Check(!ReticleSmoothingDiscontinuity(
              !disengaged.relationshipEngaged || disengaged.supportTrusted,
              disengaged.supportEpoch, 0),
        "a coherently disengaged solve keeps the ordinary one-hand smoothing");
}

// F06/F07 presented-ray consumption: the full consumer rule. The ray's producer
// receipt must be compatible with the LIVE relationship (owner epoch, trusted
// support, F14 epoch-0 coherence, serial staleness bound) AND the consuming
// frame must have its own title-native invocation receipt that still proves the
// same owner. The confirmed counterexample is a ray produced under trusted
// A/epoch 42 that is still presented while the current invocation already
// resolves owner B and the durable relationship has not been invalidated yet.
void TestPresentedRayConsumption(TestContext& test)
{
    const OwnerTuple relationshipA{
        GameTitle::HaloReach, kGeneration, kOwnerA.unit, kOwnerA.weapon};
    const OwnerTuple relationshipB{
        GameTitle::HaloReach, kGeneration, kOwnerB.unit, kOwnerB.weapon};

    auto receipt = [&](OwnerTuple owner, bool trusted, bool readable,
                       bool engaged, uint64_t epoch, uint64_t serial,
                       uint32_t generation = kGeneration,
                       GameTitle title = GameTitle::HaloReach,
                       OwnerEvidence evidence = OwnerEvidence::KnownPresent)
    {
        SupportInvocationReceipt value{};
        value.resolved = true;
        value.title = title;
        value.generation = generation;
        value.serial = serial;
        value.evidence = evidence;
        value.producerAgreement = trusted;
        value.owner = owner;
        value.relationshipReadable = readable;
        value.relationshipEngaged = engaged;
        value.relationshipEpoch = epoch;
        return value;
    };

    // Receipt coverage: title, generation and the serial staleness bound. A
    // completed ray is legitimately one serial old; two is stale.
    const SupportInvocationReceipt covering = receipt(
        relationshipA, true, true, true, 42, 1000);
    test.Check(InvocationReceiptCoversConsumer(GameTitle::HaloReach, covering,
              kGeneration, 1000) &&
            InvocationReceiptCoversConsumer(GameTitle::HaloReach, covering,
              kGeneration, 1001),
        "a receipt covers its own prepared serial and the immediately next one");
    test.Check(!InvocationReceiptCoversConsumer(GameTitle::HaloReach, covering,
              kGeneration, 1002),
        "a receipt two serials old does not cover the consuming frame");
    test.Check(!InvocationReceiptCoversConsumer(GameTitle::HaloReach, covering,
              kGeneration, 999),
        "a receipt from a newer serial than the consumer does not cover it");
    test.Check(!InvocationReceiptCoversConsumer(GameTitle::HaloReach, covering,
              kGeneration + 1, 1000),
        "a receipt from another generation does not cover the consuming frame");
    test.Check(!InvocationReceiptCoversConsumer(GameTitle::Halo3, covering,
              kGeneration, 1000),
        "a receipt from another title does not cover the consuming frame");
    SupportInvocationReceipt unresolved = covering;
    unresolved.resolved = false;
    test.Check(!InvocationReceiptCoversConsumer(GameTitle::HaloReach, unresolved,
              kGeneration, 1000),
        "an unresolved receipt never covers a consuming frame");
    test.Check(!InvocationReceiptCoversConsumer(GameTitle::HaloReach, covering,
              kGeneration, 0) &&
            !InvocationReceiptCoversConsumer(GameTitle::HaloReach, covering,
              0, 1000),
        "a zero serial on either side is never a proven relation");

    // Healthy path: ray produced under trusted A/epoch 42, relationship still
    // A/42, consuming invocation proves A.
    test.Check(PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, true, true, true, 42, 1000),
              kGeneration, 1001, true, true, 42, relationshipA,
              42, true, 1000),
        "trusted same-owner support ray with a covering invocation receipt is consumable");

    // F06 counterexample: the ray is still the trusted A ray and the durable
    // relationship still reads A/42, but the CONSUMING invocation now proves B.
    test.Check(!PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipB, false, true, true, 42, 1000),
              kGeneration, 1001, true, true, 42, relationshipA,
              42, true, 1000),
        "a stale trusted-A ray is refused while the consuming invocation proves B");
    test.Check(!PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, false, true, true, 42, 1000),
              kGeneration, 1001, true, true, 42, relationshipA,
              42, true, 1000),
        "an untrusted invocation receipt never authorises a support ray");
    test.Check(!PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, true, false, false, 0, 1000),
              kGeneration, 1001, false, false, 0, {},
              0, false, 1000),
        "an unreadable relationship reading is never permission");
    test.Check(!PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, true, true, true, 42, 995),
              kGeneration, 1001, true, true, 42, relationshipA,
              42, true, 1000),
        "a receipt outside the serial staleness bound is refused");
    test.Check(!PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, true, true, true, 42, 1000),
              kGeneration, 1001, true, true, 42, relationshipA,
              42, true, 990),
        "a ray outside the serial staleness bound is refused");

    // The relationship epoch moving on refuses the old ray even when the
    // invocation receipt still carries the old epoch.
    test.Check(!PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, true, true, true, 42, 1000),
              kGeneration, 1001, true, true, 43, relationshipA,
              42, true, 1000),
        "a ray from a replaced relationship epoch is refused");

    // Ordinary one-hand ray: epoch 0 with a coherent disengaged reading and a
    // covering invocation receipt is consumable; the same ray while the
    // relationship reads engaged is not.
    test.Check(PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, false, true, false, 7, 1000,
                  kGeneration, GameTitle::HaloReach, OwnerEvidence::Unknown),
              kGeneration, 1001, true, false, 7, {},
              0, false, 1000),
        "a coherently disengaged ordinary one-hand ray is consumable");
    test.Check(!PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, false, true, false, 7, 1000,
                  kGeneration, GameTitle::HaloReach, OwnerEvidence::Unknown),
              kGeneration, 1001, true, true, 7, relationshipA,
              0, false, 1000),
        "an epoch-0 ray is refused while the durable relationship reads engaged");
    test.Check(!PresentedRayConsumableForInvocation(GameTitle::HaloReach,
              receipt(relationshipA, false, false, false, 0, 1000,
                  kGeneration, GameTitle::HaloReach, OwnerEvidence::Unknown),
              kGeneration, 1001, false, false, 0, {},
              0, false, 1000),
        "an epoch-0 ray under an unreadable relationship is refused");
}

// A0 transitional applicability gate: only a title whose producer/trust/
// fallback slice is wired may consume the feature. This is the pure half of
// the gate; game code adds the config flag.
void TestPersistentSupportGripApplicability(TestContext& test)
{
    test.Check(PersistentSupportGripApplies(GameTitle::Halo3),
        "Halo 3 is a wired persistent-grip title");
    test.Check(PersistentSupportGripApplies(GameTitle::HaloCE),
        "Halo CE is a wired persistent-grip title");
    test.Check(PersistentSupportGripApplies(GameTitle::Halo2),
        "Halo 2 is a wired persistent-grip title");
    test.Check(PersistentSupportGripApplies(GameTitle::Halo3ODST),
        "ODST is a wired persistent-grip title (T6b)");
    test.Check(PersistentSupportGripApplies(GameTitle::HaloReach),
        "Reach is a wired persistent-grip title (T6b)");
    // T6c: Halo 4 completes the set; every gameplay title is wired and the
    // rollout gate no longer excludes one.
    test.Check(PersistentSupportGripApplies(GameTitle::Halo4),
        "Halo 4 is a wired persistent-grip title (T6c)");
    test.Check(PersistentSupportGripApplies(GameTitle::Halo3) &&
            PersistentSupportGripApplies(GameTitle::HaloCE) &&
            PersistentSupportGripApplies(GameTitle::Halo2) &&
            PersistentSupportGripApplies(GameTitle::Halo3ODST) &&
            PersistentSupportGripApplies(GameTitle::HaloReach) &&
            PersistentSupportGripApplies(GameTitle::Halo4),
        "all six gameplay titles are wired");
    // Only a title whose FP hook publishes a title-native invocation receipt is
    // held to the consuming-invocation half of the presented-ray rule.
    test.Check(TitlePublishesSupportInvocationReceipt(GameTitle::HaloReach),
        "Reach publishes a title-native invocation receipt");
    test.Check(!TitlePublishesSupportInvocationReceipt(GameTitle::Halo3) &&
            !TitlePublishesSupportInvocationReceipt(GameTitle::Halo3ODST) &&
            !TitlePublishesSupportInvocationReceipt(GameTitle::HaloCE) &&
            !TitlePublishesSupportInvocationReceipt(GameTitle::Halo2) &&
            !TitlePublishesSupportInvocationReceipt(GameTitle::Halo4),
        "only Reach publishes an invocation receipt in this tranche");
    test.Check(!PersistentSupportGripApplies(GameTitle::None),
        "no active title is never a wired persistent-grip title");
    test.Check(!PersistentSupportGripApplies(GameTitle::Unknown),
        "an unclassified title is never a wired persistent-grip title");
}

// Two-handed aim is off in every vehicle seat and walk-up turret. Rehearses
// vr.cpp's UpdateTwoHandLatch seam: SeatedSupportGate feeds the seat into the
// shared SupportGripAdmission as a blocker (beside dual wield), the base latch
// or the durable writer steps on that admission, and DropPresentationHistory
// decides when the release blend is skipped.
struct SeatedLatchRig
{
    SeatedSupportGate gate;
    SupportGripAdmission admission;
    bool latched = false;
    bool prevGrip = false;
    bool toggleMode = false;
    bool lastDrop = false;
    SeatedSupportGate::Step lastStep{};

    bool Frame(bool seated, bool gripHeld, bool inZone, bool dualWield = false)
    {
        const bool rising = gripHeld && !prevGrip;
        prevGrip = gripHeld;
        lastStep = gate.Observe(seated);
        const bool before = latched;
        const bool allowed = admission.Observe(gripHeld,
            dualWield || lastStep.blocksSupport, false);
        if (!allowed)
            latched = false;
        else if (toggleMode)
        {
            if (rising) latched = latched ? false : inZone;
        }
        else
            latched = UpdateTwoHandHold(latched, gripHeld, inZone);
        lastDrop = SeatedSupportGate::DropPresentationHistory(
            lastStep, before, latched);
        return latched;
    }
};

void TestSeatedTwoHandGate(TestContext& test)
{
    // Hold mode: sprinting two-handed, then boarding.
    SeatedLatchRig hold;
    test.Check(hold.Frame(false, true, true) && !hold.lastDrop,
        "seat: on foot, a grip in the zone engages two-handed aim");
    test.Check(!hold.Frame(true, true, true) && hold.lastStep.enteredSeat &&
            hold.lastDrop,
        "seat: boarding with the hold engaged drops it and skips the release blend");
    test.Check(!hold.Frame(true, true, true) && !hold.lastStep.enteredSeat &&
            !hold.lastDrop,
        "seat: a held grip in the zone never engages while seated; nothing is re-dropped");
    test.Check(!hold.Frame(true, false, true) && !hold.Frame(true, true, true),
        "seat: a fresh grip while seated still cannot engage");
    test.Check(!hold.Frame(false, true, true) && !hold.lastDrop,
        "seat: leaving the seat with the grip still held does not re-engage");
    test.Check(!hold.Frame(false, true, true),
        "seat: the still-held grip stays one-handed on foot");
    test.Check(!hold.Frame(false, false, true) && hold.Frame(false, true, true),
        "seat: release and grip again on foot re-engages");

    // Boarding without a hold: the entry edge still drops presentation
    // history once (a release blend begun just before boarding).
    SeatedLatchRig idle;
    (void)idle.Frame(false, false, false);
    test.Check(!idle.Frame(true, false, false) && idle.lastDrop,
        "seat: the entry edge drops presentation history even without a hold");
    test.Check(!idle.Frame(true, false, false) && !idle.lastDrop,
        "seat: a steady seat drops nothing further");
    test.Check(idle.Frame(false, true, true),
        "seat: a grip squeezed as or after the player leaves is a new grip and engages");

    // An ordinary on-foot release keeps its blend: only seated drops skip it.
    SeatedLatchRig onFoot;
    (void)onFoot.Frame(false, true, true);
    test.Check(!onFoot.Frame(false, false, true) && !onFoot.lastDrop,
        "seat: an on-foot release keeps the release blend");

    // A pose outage defers the durable drop until tracking returns; that
    // later, still-seated drop skips the blend too.
    const SeatedSupportGate::Step seatedSteady{true, false};
    test.Check(SeatedSupportGate::DropPresentationHistory(seatedSteady, true, false),
        "seat: a hold that drops later while seated still skips the blend");
    test.Check(!SeatedSupportGate::DropPresentationHistory(seatedSteady, true, true) &&
            !SeatedSupportGate::DropPresentationHistory(seatedSteady, false, false),
        "seat: no drop without a released hold or an entry edge");
    const SeatedSupportGate::Step onFootStep{false, false};
    test.Check(!SeatedSupportGate::DropPresentationHistory(onFootStep, true, false),
        "seat: an on-foot release is never a seated drop");

    // Toggle mode: boarding releases; a press made in the seat and still held
    // on exit is not a new grab; the next press on foot toggles on.
    SeatedLatchRig toggle;
    toggle.toggleMode = true;
    (void)toggle.Frame(false, true, true);
    test.Check(toggle.latched, "seat: toggle engages on foot");
    (void)toggle.Frame(false, false, true);
    test.Check(!toggle.Frame(true, false, true) && toggle.lastDrop,
        "seat: boarding releases a toggled hold");
    test.Check(!toggle.Frame(true, true, true),
        "seat: a toggle press in the seat does nothing");
    test.Check(!toggle.Frame(false, true, true),
        "seat: that press still held on exit does not engage");
    (void)toggle.Frame(false, false, true);
    test.Check(toggle.Frame(false, true, true),
        "seat: the next toggle press on foot engages");

    // On-foot parity: with no seat ever seen the latch is the pre-change
    // admission exactly, dual wield included.
    SeatedLatchRig gated;
    SupportGripAdmission reference;
    bool referenceLatched = false;
    bool parity = true;
    for (unsigned i = 0; i < 64; ++i)
    {
        const bool grip = (i * 7u + 3u) % 5u < 3u;
        const bool zone = (i * 11u + 1u) % 3u != 0u;
        const bool dual = (i % 13u) == 6u;
        const bool allowed = reference.Observe(grip, dual, false);
        referenceLatched = allowed &&
            UpdateTwoHandHold(referenceLatched, grip, zone);
        parity = parity && gated.Frame(false, grip, zone, dual) ==
            referenceLatched && !gated.lastDrop &&
            gated.admission.requiresRelease == reference.requiresRelease;
    }
    test.Check(parity, "seat: on foot the latch is exactly the pre-change one");

    // Persistent support grip: the seat closes the durable writer's hard gate,
    // which releases an engaged relationship; held across the exit, the armed
    // admission keeps the gate closed until the Grip is released.
    SupportGripAdmission pgAdmission;
    SeatedSupportGate pgGate;
    DurableWriterState writer;
    const auto pgFrame = [&](bool seated, bool grip)
    {
        const SeatedSupportGate::Step step = pgGate.Observe(seated);
        const bool allowed = pgAdmission.Observe(grip, step.blocksSupport, false);
        writer.requiresRelease = pgAdmission.requiresRelease;
        return DurableWriterStep(writer, allowed, allowed && grip,
            OwnerEvidence::KnownPresent, kOwnerA, kGeneration);
    };
    test.Check(pgFrame(false, true) == DurableWriterAction::Bind && writer.engaged,
        "seat: the durable writer binds on foot");
    test.Check(pgFrame(true, true) == DurableWriterAction::Released && !writer.engaged,
        "seat: boarding releases the durable relationship");
    test.Check(pgFrame(false, true) != DurableWriterAction::Bind && !writer.engaged,
        "seat: a grip held across the exit does not rebind");
    (void)pgFrame(false, false);
    test.Check(pgFrame(false, true) == DurableWriterAction::Bind && writer.engaged,
        "seat: a fresh grip on foot rebinds");
}

// Two-handed aim's exit condition: the game is not showing the player's own
// first-person view (first_person_view_logic.h). Classify is each title's
// evidence ladder; Gate turns verdicts into the admission blocker
// SeatedSupportGate already applies.
void TestFirstPersonViewClassify(TestContext& test)
{
    using fp_view::Classify;
    using fp_view::Evidence;
    using fp_view::Why;
    constexpr auto U = fp_view::Signal::Unavailable;
    constexpr auto R = fp_view::Signal::Unread;
    constexpr auto Y = fp_view::Signal::Yes;
    constexpr auto N = fp_view::Signal::No;
    // `monitor` defaults to No: the game said "not Forge's Monitor", or the title has no Forge (CE, ODST, Halo 2).
    const auto ev = [](fp_view::Signal cinematic, fp_view::Signal unit, fp_view::Signal seated,
                       fp_view::Signal camera, fp_view::Signal viewModel,
                       fp_view::Signal monitor = fp_view::Signal::No) {
        Evidence e;
        e.cinematic = cinematic;
        e.unit = unit;
        e.seated = seated;
        e.camera = camera;
        e.viewModel = viewModel;
        e.monitor = monitor;
        return e;
    };

    // Halo 3 / ODST: the director's first-person camera on foot.
    test.Check(Classify(ev(U, Y, N, Y, Y)) == Why::FirstPerson, "view: director first person on foot");
    // The camera outranks the view-model witness: a native zoom or an unarmed
    // player may stop the view model while the camera is still first person.
    test.Check(Classify(ev(U, Y, N, Y, N)) == Why::FirstPerson, "view: the camera rung outranks a quiet witness");
    // A turret or seat: the unit's own parent link -- even when the mod has
    // told Halo 3's seat to be first person (the director then says FP).
    test.Check(Classify(ev(U, Y, Y, Y, U)) == Why::Seated, "view: a seat leaves even with a first-person camera");
    test.Check(Classify(ev(U, Y, Y, U, Y)) == Why::Seated, "view: a seat leaves even with the view model drawn");
    // A carried turret / chase / orbit / Monitor camera on foot.
    test.Check(Classify(ev(U, Y, N, N, Y)) == Why::Camera, "view: a third-person camera on foot leaves");
    // Death: no unit, whatever the camera says.
    test.Check(Classify(ev(U, N, U, Y, U)) == Why::NoUnit, "view: no unit (death) leaves");
    // A cinematic vetoes everything.
    test.Check(Classify(ev(Y, Y, N, Y, Y)) == Why::Cinematic, "view: a cinematic leaves");
    // Fail toward one hand.
    test.Check(Classify(ev(U, Y, N, R, Y)) == Why::Unread, "view: an unreadable camera is no answer");
    test.Check(Classify(ev(U, Y, R, Y, Y)) == Why::Unread,
        "view: a first-person camera beside an unreadable seat is no answer");
    test.Check(Classify(ev(U, U, U, U, U)) == Why::Unread, "view: no evidence at all is no answer");
    test.Check(Classify(ev(U, U, U, U, R)) == Why::Unread, "view: an unreadable witness is no answer");
    // Reach / Halo 4 (no camera rung): the view-model witness decides.
    test.Check(Classify(ev(U, U, N, U, Y)) == Why::FirstPerson, "view: witness drawn on foot is first person");
    test.Check(Classify(ev(U, U, U, U, Y)) == Why::FirstPerson,
        "view: witness drawn, seat unknown (Reach's fallback sampler) is first person");
    test.Check(Classify(ev(U, U, N, U, N)) == Why::NoFirstPersonView, "view: witness quiet on foot leaves");
    test.Check(Classify(ev(U, U, R, U, Y)) == Why::Unread, "view: witness drawn beside an unreadable seat");
    // A title with only its parent link.
    test.Check(Classify(ev(U, U, N, U, U)) == Why::FirstPerson, "view: only the parent link, on foot");
    test.Check(Classify(ev(U, U, R, U, U)) == Why::Unread, "view: only the parent link, unreadable");
    // CE: the native perspective is the camera rung.
    test.Check(Classify(ev(N, Y, N, N, U)) == Why::Camera, "view: CE third-person perspective leaves");
    test.Check(Classify(ev(N, Y, N, Y, U)) == Why::FirstPerson, "view: CE first-person perspective on foot");

    // Forge's Monitor (Halo 3, Reach, Halo 4): an idle Monitor has the game's first-person camera, so the
    // camera rung alone read it as first person. The game's own editor answer is a veto.
    test.Check(Classify(ev(U, Y, N, Y, N, Y)) == Why::Monitor, "view: an idle Monitor (first-person camera) leaves");
    test.Check(Classify(ev(U, Y, N, Y, Y, Y)) == Why::Monitor, "view: the Monitor leaves whatever is drawn");
    test.Check(Classify(ev(U, Y, N, N, N, Y)) == Why::Monitor, "view: the Monitor holding an object leaves");
    test.Check(fp_view::HardReason(Why::Monitor), "view: the Monitor is positive evidence: no grace");
    test.Check(Classify(ev(Y, Y, N, Y, N, Y)) == Why::Cinematic && Classify(ev(U, N, U, Y, U, Y)) == Why::NoUnit &&
            Classify(ev(U, Y, Y, Y, U, Y)) == Why::Seated,
        "view: a cinematic, death and a seat are still named first");
    // Back to a Spartan: the game says no again, and a zoom or an empty hand needs no view model.
    test.Check(Classify(ev(U, Y, N, Y, N, N)) == Why::FirstPerson, "view: not the Monitor, unarmed or zoomed: first person");
    // The Monitor answer unknown (not bound, unread, faulted): only the view model can vouch for the camera.
    test.Check(Classify(ev(U, Y, N, Y, N, U)) == Why::NoFirstPersonView,
        "view: first-person camera, no view model, Monitor check unbound: leaves (after the grace)");
    test.Check(Classify(ev(U, Y, N, Y, N, R)) == Why::NoFirstPersonView, "view: ... or unread");
    test.Check(!fp_view::HardReason(Why::NoFirstPersonView), "view: that is a soft reason (a single long frame)");
    test.Check(Classify(ev(U, Y, N, Y, Y, U)) == Why::FirstPerson, "view: Monitor check unknown, view model drawn: first person");
    test.Check(Classify(ev(U, Y, N, Y, U, U)) == Why::FirstPerson,
        "view: Monitor check unknown and no witness on this build: first person, as before the witnesses");
    test.Check(Classify(ev(U, Y, N, Y, R, U)) == Why::Unread, "view: Monitor check unknown, witness unreadable: no answer");
    // Reach / Halo 4 with their camera rung: Armor Lock / Hardlight Shield / an assassination is the following
    // camera; a zoom is still the first-person camera.
    test.Check(Classify(ev(U, U, N, N, Y, N)) == Why::Camera, "view: Reach in Armor Lock (following camera) leaves at once");
    test.Check(Classify(ev(U, U, N, Y, N, N)) == Why::FirstPerson, "view: Reach zoomed or unarmed: first person by the camera");
    test.Check(Classify(ev(U, U, U, Y, N, N)) == Why::FirstPerson,
        "view: Reach's fallback seat sampler (no seat answer) with a first-person camera: first person");
    // Halo 2: the director's first-person update; the witness is its on-foot unit sample.
    test.Check(Classify(ev(U, U, N, N, Y, N)) == Why::Camera, "view: Halo 2's following / dead / scripted camera leaves");
    test.Check(Classify(ev(U, U, N, Y, Y, N)) == Why::FirstPerson, "view: Halo 2 first person on foot");
    // The rung unbound or condemned: the titles' evidence is what it was (the witness decides).
    test.Check(Classify(ev(U, U, N, U, N, U)) == Why::NoFirstPersonView, "view: no camera rung, witness quiet: leaves");
    test.Check(Classify(ev(U, U, N, U, Y, U)) == Why::FirstPerson, "view: no camera rung, witness drawn: first person");

    // The director record as the gate reads it.
    test.Check(fp_view::DirectorCameraSignal(true, true, 3, 0) == Y, "director: mode 3 perspective 0 is first person");
    test.Check(fp_view::DirectorCameraSignal(true, true, 3, 1) == N, "director: first-person camera, third perspective");
    test.Check(fp_view::DirectorCameraSignal(true, true, 0, 1) == N, "director: following camera");
    test.Check(fp_view::DirectorCameraSignal(true, true, 4, 1) == N, "director: dead camera");
    test.Check(fp_view::DirectorCameraSignal(true, true, 8, 0) == N,
        "director: the default (type-8) camera is not first person, though out of the record's range");
    test.Check(fp_view::DirectorCameraSignal(true, true, -1, -1) == N, "director: garbage is not first person");
    test.Check(fp_view::DirectorCameraSignal(true, false, 3, 0) == R, "director: bound but unread");
    test.Check(fp_view::DirectorCameraSignal(false, false, 3, 0) == U, "director: not bound");

    for (size_t i = 0; i < static_cast<size_t>(Why::Count); ++i)
        test.Check(std::strcmp(fp_view::WhyName(static_cast<Why>(i)), "?") != 0, "view: every reason has a name");
}

void TestDirectorGateSiteDecode(TestContext& test)
{
    // ODST's two sites, as the pinned module lays them out (offline:
    // tools/verify-odst-director-camera.py): the first-person gate
    // (+0x2A7150) and set_camera_mode's store (+0x152A90).
    const uint8_t gate[fp_view::kDirectorGateSiteBytes] = {
        0x4C, 0x63, 0xCA, 0x41, 0x83, 0xF9, 0xFF, 0x74, 0x41, 0x8B, 0x0D, 0xBD, 0x89, 0x7E, 0x00,
        0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0xBA, 0x90, 0x01, 0x00, 0x00,
        0x4C, 0x8B, 0x04, 0xC8, 0x4B, 0x8D, 0x0C, 0x49, 0x49, 0x8B, 0x04, 0x10,
        0x83, 0xBC, 0x88, 0x04, 0x06, 0x00, 0x00, 0x00, 0x75, 0x17, 0x41, 0x8B, 0xC9, 0xE8};
    uint8_t setMode[shot_lag::kDirectorSetModeSiteBytes] = {
        0x65, 0x48, 0x8B, 0x0C, 0x25, 0x58, 0x00, 0x00, 0x00, 0x41, 0xB8, 0x90, 0x01, 0x00, 0x00,
        0x4A, 0x8B, 0x0C, 0xF9, 0x49, 0x8B, 0x14, 0x08, 0x48, 0x63, 0x8E, 0x58, 0x01, 0x00, 0x00,
        0x4C, 0x8D, 0x04, 0x49, 0x42, 0x39, 0x84, 0x82, 0x04, 0x06, 0x00, 0x00, 0x75, 0x0A,
        0x42, 0x39, 0xAC, 0x82, 0x08, 0x06, 0x00, 0x00, 0x74, 0x32,
        0x42, 0x89, 0x84, 0x82, 0x04, 0x06, 0x00, 0x00};
    shot_lag::DirectorCameraSites sites{};
    test.Check(fp_view::DecodeDirectorGateSites(gate, setMode, sites) && sites.blockOffset == 0x190 &&
            sites.perspectiveOffset == 0x604 && sites.modeOffset == 0x608 &&
            0x2A7150 + fp_view::kDirectorGateTlsNext + sites.tlsIndexDisp == 0xA8FB1C,
        "director gate: ODST's two sites decode to its record and engine TLS index");
    setMode[shot_lag::kDirectorSetModeBlock] = 0x88;
    test.Check(!fp_view::DecodeDirectorGateSites(gate, setMode, sites),
        "director gate: sites that disagree on the block bind nothing");
    setMode[shot_lag::kDirectorSetModeBlock] = 0x90;
    setMode[shot_lag::kDirectorSetModeMode] = 0x0C;
    test.Check(!fp_view::DecodeDirectorGateSites(gate, setMode, sites),
        "director gate: a mode field not right after the perspective binds nothing");
    test.Check(!fp_view::DecodeDirectorGateSites(nullptr, setMode, sites), "director gate: no site, no binding");
}

// The seam vr.cpp runs on the frame thread: verdict -> fp_view::Gate ->
// SeatedSupportGate -> SupportGripAdmission -> the latch.
struct ViewLatchRig
{
    fp_view::Gate view;
    SeatedSupportGate edge;
    SupportGripAdmission admission;
    bool latched = false;
    bool prevGrip = false;
    bool toggleMode = false;
    bool lastDrop = false;
    fp_view::Gate::Step lastView{};
    uint64_t now = 1000;

    bool Frame(fp_view::Why why, bool gripHeld, bool inZone, bool hold = false, uint64_t stepMs = 11)
    {
        now += stepMs;
        const bool rising = gripHeld && !prevGrip;
        prevGrip = gripHeld;
        lastView = view.Observe(why, hold, now);
        const SeatedSupportGate::Step block = edge.Observe(lastView.blocksSupport);
        const bool before = latched;
        const bool allowed = admission.Observe(gripHeld, block.blocksSupport, false);
        if (!allowed)
            latched = false;
        else if (toggleMode)
        {
            if (rising) latched = latched ? false : inZone;
        }
        else
            latched = UpdateTwoHandHold(latched, gripHeld, inZone);
        lastDrop = SeatedSupportGate::DropPresentationHistory(block, before, latched);
        return latched;
    }
};

void TestFirstPersonViewGate(TestContext& test)
{
    using fp_view::Why;
    // Hold mode: two-handed on foot, then a turret that puts the player in
    // third person.
    ViewLatchRig hold;
    test.Check(hold.Frame(Why::FirstPerson, true, true) && !hold.lastDrop,
        "view gate: first person engages two-handed aim");
    test.Check(!hold.Frame(Why::Seated, true, true) && hold.lastView.entered && hold.lastDrop &&
            hold.lastView.reason == Why::Seated,
        "view gate: a turret leaves at once, skips the release blend and names the seat");
    test.Check(!hold.Frame(Why::Camera, true, true) && !hold.lastDrop && hold.lastView.reason == Why::Seated,
        "view gate: still out; the reason that closed it stands");
    test.Check(!hold.Frame(Why::FirstPerson, true, true),
        "view gate: back in first person, the grip held through it does not re-engage");
    test.Check(!hold.Frame(Why::FirstPerson, false, true) && hold.Frame(Why::FirstPerson, true, true),
        "view gate: release and grip again in first person re-engages");

    // A third-person camera on foot (Halo 3's carried turret, Forge's
    // Monitor): hard, at once.
    ViewLatchRig camera;
    (void)camera.Frame(Why::FirstPerson, true, true);
    test.Check(!camera.Frame(Why::Camera, true, true) && camera.lastDrop,
        "view gate: a third-person camera leaves at once");

    // Death: no unit.
    ViewLatchRig death;
    (void)death.Frame(Why::FirstPerson, true, true);
    test.Check(!death.Frame(Why::NoUnit, true, true) && death.lastView.reason == Why::NoUnit,
        "view gate: death leaves at once");

    // Soft reasons wait out a long frame, then leave.
    ViewLatchRig soft;
    (void)soft.Frame(Why::FirstPerson, true, true);
    test.Check(soft.Frame(Why::NoFirstPersonView, true, true, false, 100),
        "view gate: a quiet witness for one long frame is not yet an exit");
    test.Check(soft.Frame(Why::FirstPerson, true, true, false, 100),
        "view gate: the witness back within the grace keeps two-hand");
    (void)soft.Frame(Why::Unread, true, true, false, 100);   // grace starts
    (void)soft.Frame(Why::Unread, true, true, false, 100);   // 100 ms
    test.Check(soft.Frame(Why::Unread, true, true, false, 100),   // 200 ms
        "view gate: an unreadable view inside the grace keeps two-hand");
    test.Check(!soft.Frame(Why::Unread, true, true, false, 100) && soft.lastView.entered &&   // 300 ms
            soft.lastView.reason == Why::Unread,
        "view gate: an unreadable view for the grace leaves (fail toward one hand)");
    // A soft run that turns hard leaves at once.
    ViewLatchRig mixed;
    (void)mixed.Frame(Why::FirstPerson, true, true);
    (void)mixed.Frame(Why::NoFirstPersonView, true, true);
    test.Check(!mixed.Frame(Why::Camera, true, true) && mixed.lastView.reason == Why::Camera,
        "view gate: positive third-person evidence ends the grace");

    // A pause freezes the gate both ways, and its stale samples are no evidence.
    ViewLatchRig paused;
    (void)paused.Frame(Why::FirstPerson, true, true);
    test.Check(paused.Frame(Why::Unread, true, true, true, 2000),
        "view gate: a pause keeps two-hand engaged however long it is");
    test.Check(paused.Frame(Why::FirstPerson, true, true),
        "view gate: resuming in first person keeps it");
    (void)paused.Frame(Why::Seated, true, true);
    test.Check(!paused.Frame(Why::FirstPerson, true, true, true) && paused.lastView.blocksSupport,
        "view gate: a pause does not reopen a closed gate");
    // Grace restarts after a pause: a soft run split by a pause is two runs.
    ViewLatchRig split;
    (void)split.Frame(Why::FirstPerson, true, true);
    (void)split.Frame(Why::Unread, true, true, false, 200);
    (void)split.Frame(Why::Unread, true, true, true, 200);
    test.Check(split.Frame(Why::Unread, true, true, false, 100),
        "view gate: the grace restarts after a pause");

    // Toggle mode: the third-person exit releases a toggled hold; a press in
    // third person does nothing; the next press in first person engages.
    ViewLatchRig toggle;
    toggle.toggleMode = true;
    (void)toggle.Frame(Why::FirstPerson, true, true);
    (void)toggle.Frame(Why::FirstPerson, false, true);
    test.Check(toggle.latched, "view gate: toggle engages in first person");
    test.Check(!toggle.Frame(Why::Cinematic, false, true) && toggle.lastDrop,
        "view gate: a cinematic releases a toggled hold");
    test.Check(!toggle.Frame(Why::Cinematic, true, true), "view gate: a toggle press in a cinematic does nothing");
    test.Check(!toggle.Frame(Why::FirstPerson, true, true), "view gate: that press held back into first person");
    (void)toggle.Frame(Why::FirstPerson, false, true);
    test.Check(toggle.Frame(Why::FirstPerson, true, true), "view gate: the next press in first person engages");

    // First-person parity: with first person throughout, the latch is the
    // pre-change admission exactly, dual wield included.
    ViewLatchRig gated;
    SupportGripAdmission reference;
    bool referenceLatched = false;
    bool parity = true;
    for (unsigned i = 0; i < 64; ++i)
    {
        const bool grip = (i * 7u + 3u) % 5u < 3u;
        const bool zone = (i * 11u + 1u) % 3u != 0u;
        const bool allowed = reference.Observe(grip, false, false);
        referenceLatched = allowed && UpdateTwoHandHold(referenceLatched, grip, zone);
        parity = parity && gated.Frame(Why::FirstPerson, grip, zone) == referenceLatched && !gated.lastDrop &&
            gated.admission.requiresRelease == reference.requiresRelease;
    }
    test.Check(parity, "view gate: in first person the latch is exactly the pre-change one");
}

} // namespace

int main()
{
    TestContext test;
    // Producers only count; the worker coalesces without losing concurrent events.
    const auto publish = [] { for (int i = 0; i < 1000; ++i)
        support_diagnostics::Record(support_diagnostics::Event::Bound); };
    std::thread producerA(publish), producerB(publish);
    producerA.join(); producerB.join();
    unsigned diagnosticCount = 0, diagnosticMessages = 0;
    support_diagnostics::Drain([&](const char*, uint32_t count) {
        diagnosticCount += count; ++diagnosticMessages;
    });
    test.Check(diagnosticCount == 2000 && diagnosticMessages == 1,
        "deferred diagnostic counters preserve and coalesce concurrent events");
    support_diagnostics::Drain([&](const char*, uint32_t) { ++diagnosticMessages; });
    test.Check(diagnosticMessages == 1, "drained events are not emitted twice");
    TestOwnerIdentity(test);
    TestDurableRelationshipStep(test);
    TestInvocationTrust(test);
    TestCarrierDecisions(test);
    TestRayAndPresentation(test);
    TestStableEvidence(test);
    TestCeEvidence(test);
    TestDurableWriterBasics(test);
    TestRequiresReleaseBeforeAcquisition(test);
    TestWeaponSwitchPolicy(test);
    TestDefaultSwitchZoneReentry(test);
    TestAcquisitionOrder(test);
    TestPendingAcquisitionIntent(test);
    TestLifecycleIdentity(test);
    TestPersistentGripWriterSeam(test);
    TestPendingIntentOutcomes(test);
    TestEvidenceRevisionFloor(test);
    TestPresentedRayQualification(test);
    TestSerialAndReticleContinuity(test);
    TestSolveSupportQualification(test);
    TestPresentedRayConsumption(test);
    TestPersistentSupportGripApplicability(test);
    TestSeatedTwoHandGate(test);
    TestFirstPersonViewClassify(test);
    TestDirectorGateSiteDecode(test);
    TestFirstPersonViewGate(test);
    return test.Finish();
}
