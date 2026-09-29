"""Guard the persistent support grip (PG) port's wiring and its PG-off parity.

The feature spans a config key, a pure applicability predicate, the durable
writer in ``src/dll/vr.cpp`` and the assembly qualification path in the same
file. Its correctness is carried by wiring between files rather than by any
single state machine, so the offline suites cannot catch a silent break: a
default that flips to true, a missing parse/save branch, a writer or assembly
path that stops consulting the applicability gate, a base latch path that
loses its pre-fix stores, a second reticle reseed, a lost owner-break
invalidation, or the re-introduction of a donor leftover (the raw-grip-pose
endpoint selector, its menu copy, or the retired head-turn sway correction)
all build and pass while changing the feature.

The check is deliberately narrow. With ``--root`` it asserts, for that tree:

  1. ``src/common/config.h`` still defaults ``persistent_support_grip`` to
     ``true`` (saved explicit values remain authoritative);
  2. ``src/common/config.cpp`` still parses that key into the field and still
     emits the key and its generated comment from the writer;
  3. the pure applicability predicate
     (``support_grip::PersistentSupportGripApplies``) exists, the runtime gate
     ``VR_SupportGripWiredForTitle`` still ANDs the config flag with it, the
     gate is consulted from BOTH the durable writer path
     (``UpdateTwoHandLatch``) and the assembly-qualification path
     (``ResolveAimSupportQualification``), and the durable writer call sits
     inside the gate branch, before the dormant-state retire (a writer hoisted
     above the gate is rejected);
  4. the PG-off latch branch (after ``RetirePersistentSupportGripState``)
     still contains the base toggle on/off stores and the base
     ``UpdateTwoHandHold`` store verbatim (matched with whitespace collapsed,
     so re-indentation cannot hide a change and re-wrapping cannot fake one),
     still uses the base ``TwoHandGrabZoneHit`` geometry, and contains no
     writer-only call;
  5. the reticle smoothing reseed exists exactly once, is driven by
     ``ReticleSmoothingDiscontinuity`` and the ``!supportDiscontinuity``
     guard; the PG owner-break site invalidates Lab/filter state and normally
     invalidates continuity, while an already-presented VS-OFF latch release
     retains its required product release bridge;
  6. the PG paths carry no donor leftovers: no
     ``SupportGripEndpointUsesRawGripPose`` helper, no "Use grip point for
     support hand" menu copy, and no retired head-turn sway-correction symbol
     in the new/persistent paths;
  7. the acquisition gate wiring: the durable writer calls the A003
     ``PersistentSupportGrabZoneHit`` helper (evaluating
     ``support_grab::EvaluateSupportGrabZone`` with the primary acquisition
     axis and the raw support position, never the support wrist orientation),
     while the base ``TwoHandGrabZoneHit`` helper keeps the pre-fix
     ``virtual_stock::SupportGrabPoint`` shift along the SUPPORT forward;
  8. the T13 retained-steering corrective is wired to one source of truth: the
     solve input ``AimPoseInputs::supportSteeringRetained`` is declared
     default-false in ``vr.cpp`` and is written exactly once inside the
     stock-aware aim assembly. In the original shape that is
     ``CurrentStockAimPoseInputs``; in the explicit-input shape it is
     ``CurrentStockAimPoseInputsWithNeutralCapture``, while the live-capture
     ``CurrentStockAimPoseInputs`` wrapper only delegates. The write is a
     direct copy of the frozen T5b qualification bit
     (``supportQualification.supportTrusted``) - which
     ``QualifySolveSupport`` only ever sets after the feature-off short
     circuit and the untrusted early return, so PG off / an unwired title / a
     disengaged or unreadable relationship / an invocation the title denied
     through the live ``g_supportInvocationUntrusted`` channel all leave it
     false; the qualification call itself must still pass that live untrusted
     input (a hardcoded argument is rejected); the VS-off legacy selector call
     forwards it together with the
     VS-off gate (``inputs.supportSteeringRetained && !inputs.virtualStockEnabled``);
     the pure selection helper offers the retention parameter and skips ONLY
     the 0.35 agreement floor (non-finite agreement, the minimum segment
     length and the normalization still reject); and neither the Hybrid
     offhand call nor the target-aware (VS-on) selector gains the retention.
     The telemetry-only forcing carry (``AimPoseInputs::supportForceOneHand``
     plus its ``AimPoseResult`` receipt) stays data-only: both default false,
     exactly one accepted assembly body is its only writer (the delegating
     wrapper owns no writes), and it is a direct copy of the SAME frozen
     qualification bit that drives the one-hand application, so telemetry can
     never become a second source of truth for forcing. An
     occurrence allowlist over the token in ``vr.cpp`` and
     ``virtual_stock_aim.inl`` admits only the two declarations, the single
     writer, the receipt copy and the prepared-frame payload copy that
     publishes the receipt; any other occurrence - a read, a branch or an
     extra serializer - is rejected.
  9. ``src/dll/haloce_first_person.cpp`` resolves the CE outermost-prepare
     support decision from its OWN validated local-player read: the read that
     feeds the decision sits inside the ``supportOutermostPrepare`` block and
     the state argument passed to ``CeEvaluateSupportInvocation`` is the one
     that read wrote. Consuming the muzzle read above it instead would read
     generation 0 -> Unknown -> detach on every invocation on a
     ``gun_barrel_aim``-off installation, which is the headset round-2 CE
     defect; the sibling CE consumers (``haloce_unit_control.cpp``,
     ``ControllerShotDirection``) already own a per-invocation read.
 10. the 2026-09-28 weapon-switch policy: the new ``two_hand_switch_inherit``
     key stays default-off with its parse/save branches, ``DurableWriterStep``
     keeps exactly the generation-replacement and proven-absence arm sites (a
     proven owner replacement drop never re-arms release-before-reacquire), its
     in-place inheritance is option-gated on a complete current-generation
     owner, and ``UpdatePersistentSupportGrip`` reads the config option and
     passes it into the policy step.
 11. the 2026-09-28 headset follow-up on that policy: the default switch must
     not pre-hold the replacement weapon. ``PersistentSupportGripState`` carries
     a default-false ``awaitZoneReentry`` crossing requirement, the
     ``DefaultSwitchReplacementDrop`` predicate keeps it the no-inherit stage-2
     owner break only, ``StepSwitchZoneReentry`` keeps its survival-on-TRUE-zone
     and gated-hold-edge rules, the writer arms it exactly once (the toggle-mode
     rising edge stays ungated) and clears it on the zone leaving, an explicit
     release, a role/gesture/lifecycle teardown, a feature retire and the
     inherit rebind.

It deliberately does not freeze surrounding implementation text: comments,
labels and unrelated geometry stay with human review. ``--self-test``
exercises this checking logic against synthetic sources and then checks the
real tree; ``--self-test-only`` runs the synthetic fixtures alone. ``--root``
points the real-source check at another tree (used to demonstrate a failing
tree without touching the real sources).
"""

import argparse
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_VR = REPO_ROOT / "src" / "dll" / "vr.cpp"
DEFAULT_CONFIG_H = REPO_ROOT / "src" / "common" / "config.h"
DEFAULT_CONFIG_CPP = REPO_ROOT / "src" / "common" / "config.cpp"
DEFAULT_MENU = REPO_ROOT / "src" / "dll" / "menu.cpp"
DEFAULT_GRIP_LOGIC = REPO_ROOT / "src" / "common" / "support_grip_logic.h"
DEFAULT_GRAB_LOGIC = REPO_ROOT / "src" / "common" / "support_grab_logic.h"
DEFAULT_STOCK = REPO_ROOT / "src" / "dll" / "virtual_stock_aim.inl"
DEFAULT_STOCK_LOGIC = REPO_ROOT / "src" / "common" / "virtual_stock_logic.h"

CONFIG_FIELD = "bool persistent_support_grip = true;"
CONFIG_TRUE = re.compile(r"bool\s+persistent_support_grip\s*=\s*true\s*;")
CONFIG_PARSE_KEY = 'strcmp(key,"persistent_support_grip")'
CONFIG_PARSE_CALL = "ParseBoolSetting(key, val, g_config.persistent_support_grip);"
CONFIG_SAVE = (
    'fprintf(f, "persistent_support_grip = %d\\n\\n", '
    "g_config.persistent_support_grip ? 1 : 0);"
)

APPLIES_DEF = (
    "constexpr bool PersistentSupportGripApplies(GameTitle title) noexcept"
)
WIRED_DEF = "bool VR_SupportGripWiredForTitle(GameTitle title) noexcept"
WIRED_CONFIG = "g_config.persistent_support_grip"
WIRED_PREDICATE = "support_grip::PersistentSupportGripApplies(title)"
GATE_CALL = "VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle())"
LATCH_DEF = "void UpdateTwoHandLatch(bool rightValid"
WRITER_DEF = "void UpdatePersistentSupportGrip(SupportGripAdmission& admission"
QUALIFY_DEF = "ResolveAimSupportQualification()"
RETIRE_CALL = "RetirePersistentSupportGripState();"
WRITER_CALL = "UpdatePersistentSupportGrip("

BASE_HOLD_STORE = (
    "g_twoHandLatched.store(UpdateTwoHandHold( g_twoHandLatched.load(), "
    "gripHeld, inZone));"
)
BASE_TOGGLE_ON = "else if (inZone) g_twoHandLatched.store(true);"
BASE_TOGGLE_OFF = "if (g_twoHandLatched.load()) g_twoHandLatched.store(false);"
BASE_GEOMETRY_CALL = "TwoHandGrabZoneHit(rpose, lpose)"
PG_GEOMETRY_CALL = "PersistentSupportGrabZoneHit(rpose, lpose)"
WRITER_ONLY_CALLS = ("UpdatePersistentSupportGrip(", "DurableWriterStep(")

DISC_CALL = "ReticleSmoothingDiscontinuity("
DISC_GUARD = "!supportDiscontinuity"
DISC_RESEED = "? SmoothTrackedPose(rawAim, g_reticleAimPose, smoothing)"
OWNER_BREAK = (
    "if (action == support_grip::DurableWriterAction::Invalidated ||"
)
INVALIDATE_AIM = "InvalidateAimContinuityLayer();"
INVALIDATE_LAB = "InvalidateTwoHandLabTemporal();"

DONOR_SYMBOL = "SupportGripEndpointUsesRawGripPose"
DONOR_MENU_TEXT = "Use grip point for support hand"
SWAY_TOKENS = (
    "virtual_stock_head_turn_sway_correction",
    "HeadTurnSwayCorrection",
    "EvaluateHybridInverseNeck",
    "HybridInverseNeck",
)

PG_WRITER_START = (
    "// Persistent support grip (`persistent_support_grip`, default on)"
)
PG_HELPER_DEF = "bool PersistentSupportGrabZoneHit(const XrPosef& rpose"
BASE_HELPER_DEF = "bool TwoHandGrabZoneHit(const XrPosef& rpose"
PG_PREDICATE_CALL = "support_grab::EvaluateSupportGrabZone("
BASE_PREDICATE_CALL = "virtual_stock::SupportGrabPoint("
BASE_SUPPORT_FORWARD = "Rotate(lpose.orientation, {0,0,-1})"

# T13 retained-steering corrective (all matched with whitespace collapsed).
RETENTION_FIELD = "bool supportSteeringRetained = false;"
RETENTION_TRUE = re.compile(r"bool\s+supportSteeringRetained\s*=\s*true\s*;")
RETENTION_ASSIGN = (
    "inputs.supportSteeringRetained = supportQualification.supportTrusted;"
)
RETENTION_WRITE = re.compile(r"inputs\.supportSteeringRetained\s*=(?!=)")
RETENTION_CONSUME = (
    "inputs.supportSteeringRetained && !inputs.virtualStockEnabled"
)
# Telemetry-only carry of the frozen forcing flag (PG frame provenance). Data
# only: the same frozen qualification that drives the one-hand application is
# its only source, and no solve path reads the copy.
FORCE_CARRY_FIELD = "bool supportForceOneHand = false;"
FORCE_CARRY_TRUE = re.compile(r"bool\s+supportForceOneHand\s*=\s*true\s*;")
FORCE_CARRY_ASSIGN = (
    "inputs.supportForceOneHand = supportQualification.forceOneHand;"
)
FORCE_CARRY_WRITE = re.compile(r"inputs\.supportForceOneHand\s*=(?!=)")
FORCE_CARRY_RESULT = (
    "result.supportForceOneHand = inputs.supportForceOneHand;"
)
# The receipt's only consumer: the prepared-frame population publishes the
# copied flag into the telemetry payload. Every other occurrence of the token
# in the two files is rejected, so no solve path can start reading the carry.
FORCE_CARRY_TELEMETRY = (
    "frame.supportForceOneHand = inputs.supportForceOneHand;"
)
FORCE_CARRY_TOKEN = "supportForceOneHand"
FORCE_CARRY_ALLOWLIST = {
    "vr": {FORCE_CARRY_FIELD: 2, FORCE_CARRY_TELEMETRY: 1},
    "stock": {FORCE_CARRY_ASSIGN: 1, FORCE_CARRY_RESULT: 1},
}
FORCE_CARRY_SOURCE_LABELS = {"vr": "vr.cpp", "stock": "virtual_stock_aim.inl"}
ASSEMBLY_DEFS = (
    (
        "explicit-input constructor",
        "AimPoseInputs CurrentStockAimPoseInputsWithNeutralCapture(",
    ),
    (
        "live-capture constructor",
        "AimPoseInputs CurrentStockAimPoseInputs(",
    ),
)
HELPER_PARAM = "bool retainBeyondAgreementFloor = false"
HELPER_RETURN = "return retainBeyondAgreementFloor;"
FLOOR_TEST = "agreement < 0.35f"
NON_FINITE_AGREEMENT = "if (!std::isfinite(agreement))"
MIN_SEGMENT_GUARD = "length < 1.0e-4f"
LEGACY_SELECTOR_DEF = "DirectionSelection SelectTwoHandAimDirection("
TARGET_SELECTOR_DEF = (
    "DirectionSelection SelectTwoHandAimDirectionForTarget("
)
HYBRID_OFFHAND_CALL = (
    "virtual_stock::TryBuildAcceptedSupportDirection( primary, support, "
    "primaryForward, offhandDirection, rejectedExtreme, rejectedAgreement)"
)
QUALIFY_FEATURE_OFF = "if (!featureEnabled)"
QUALIFY_UNTRUSTED = "if (invocationUntrusted)"
QUALIFY_TRUST_STORE = "qualification.supportTrusted = true;"
QUALIFY_CALL = "support_grip::QualifySolveSupport("
QUALIFY_LIVE_TRUST = "g_supportInvocationUntrusted"
SUPPORT_PREPARE_BLOCK = "if (supportOutermostPrepare)"
SUPPORT_DECISION_CALL = "CeEvaluateSupportInvocation("
SUPPORT_TRUST_READ = re.compile(
    r"HaloCEControls_GetLocalPlayerState\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")

# 2026-09-28 user-requested weapon-switch policy (PG ON only). The default drop
# of a proven replacement must never re-arm release-before-reacquire (the held
# Grip re-acquires the new owner through ordinary spatial admission), the
# proven-absence arm and the generation-replacement arm must stay, and the
# optional in-place inheritance must be gated by the config option.
SWITCH_FIELD = "bool two_hand_switch_inherit = false;"
SWITCH_TRUE = re.compile(r"bool\s+two_hand_switch_inherit\s*=\s*true\s*;")
SWITCH_PARSE_KEY = 'strcmp(key,"two_hand_switch_inherit")'
SWITCH_PARSE_CALL = (
    "ParseBoolSetting(key, val, g_config.two_hand_switch_inherit);"
)
SWITCH_SAVE = (
    'fprintf(f, "two_hand_switch_inherit = %d\\n\\n", '
    "g_config.two_hand_switch_inherit ? 1 : 0);"
)
WRITER_STEP_DEF = (
    "inline DurableWriterAction DurableWriterStep(DurableWriterState& state,"
)
POLICY_ABSENCE_ARM = (
    "if (evidence == OwnerEvidence::KnownAbsent) "
    "{ state.requiresRelease = true; state.owner = {}; state.epoch++; "
    "state.engaged = false; return DurableWriterAction::Invalidated; }"
)
POLICY_ARM_SITES = "state.requiresRelease = true;"
POLICY_INHERIT_GUARD = (
    "if (inheritReplacement && OwnerComplete(evidenceOwner) && "
    "evidenceOwner.title == state.owner.title && "
    "evidenceOwner.generation == activeGeneration)"
)
POLICY_INHERIT_BRANCH = (
    "if (inheritReplacement && OwnerComplete(evidenceOwner) && "
    "evidenceOwner.title == state.owner.title && "
    "evidenceOwner.generation == activeGeneration) "
    "{ state.owner = evidenceOwner; state.epoch++; state.engaged = true; "
    "return DurableWriterAction::Bind; }"
)
SWITCH_OPTION_FIELD = "const bool switchInherit = g_config.two_hand_switch_inherit;"
SWITCH_OPTION_ARG = ", switchInherit);"

# 2026-09-28 headset follow-up: the default weapon-switch re-grip UX. A proven
# replacement no longer arms release-before-reacquire, but the support hand is
# usually still inside the NEW weapon's grab zone through the swap, so the
# level-triggered hold-mode acquisition re-bound it 38-51 ms later: the switched
# weapon arrived pre-held. The crossing requirement must stay a default-false
# per-frame flag that ONLY the no-inherit proven-replacement drop sets, whose
# hold-mode acquisition edge is the pure StepSwitchZoneReentry result (the
# toggle edge stays untouched), and which is cleared by the zone leaving, an
# explicit release, a role/gesture/lifecycle teardown, a feature retire and the
# inherit rebind (all matched with whitespace collapsed).
ZONE_FLAG_FIELD = "bool awaitZoneReentry = false;"
ZONE_FLAG_TRUE = re.compile(r"bool\s+awaitZoneReentry\s*=\s*true\s*;")
ZONE_FLAG_SET = "g_persistentSupportGrip.awaitZoneReentry = true;"
ZONE_FLAG_CLEAR = "g_persistentSupportGrip.awaitZoneReentry = false;"
ZONE_SET_SITE = (
    "if (support_grip::DefaultSwitchReplacementDrop(engagedBefore, action, "
    "evidence == support_grip::OwnerEvidence::KnownPresent && "
    "!support_grip::SameOwner(evidenceOwner, previousOwner), identityLost, "
    "switchInherit)) g_persistentSupportGrip.awaitZoneReentry = true;"
)
ZONE_STEP_SITE = (
    "const support_grip::SwitchZoneReentryStep zoneReentry = "
    "support_grip::StepSwitchZoneReentry( "
    "g_persistentSupportGrip.awaitZoneReentry, gripHeld, inZone); "
    "g_persistentSupportGrip.awaitZoneReentry = "
    "zoneReentry.awaitZoneReentry;"
)
ZONE_HOLD_EDGE = "zoneReentry.holdAcquisitionEdge"
ZONE_TOGGLE_EDGE = "? (rising && inZone && !writer.engaged)"
ZONE_UNTETHERED_EDGE = ": (gripHeld && inZone);"
ZONE_TEARDOWN_CLEAR = (
    "if (forcedTeardown) g_persistentSupportGrip.awaitZoneReentry = false;"
)
ZONE_INHERIT_CLEAR = (
    "g_persistentSupportGrip.awaitZoneReentry = false; "
    'LOG("Persistent support grip: switched-weapon inheritance '
)
ZONE_EXPLICIT_RELEASE = "if (explicitRelease)"
ZONE_STEP_DEF = "constexpr SwitchZoneReentryStep StepSwitchZoneReentry("
ZONE_STEP_SURVIVE = "step.awaitZoneReentry = awaitZoneReentry && inZone;"
ZONE_STEP_ACQUIRE = (
    "step.holdAcquisitionEdge = gripHeld && inZone && !awaitZoneReentry;"
)
ZONE_DROP_DEF = "constexpr bool DefaultSwitchReplacementDrop(bool engagedBefore,"
ZONE_DROP_RETURN = (
    "return engagedBefore && action == DurableWriterAction::Invalidated && "
    "evidenceIsDifferentOwner && !lifecycleIdentityLost && !switchInherit;"
)
RETIRE_DEF = "void RetirePersistentSupportGripState() noexcept"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def normalize(text: str) -> str:
    """Collapse whitespace so a check survives re-indentation and re-wrapping."""
    return re.sub(r"\s+", " ", text)


def brace_span(text: str, marker: str) -> tuple[int, int] | None:
    """Return the (start, end) offsets between the braces opened after marker.

    The block is located at the first ``{`` following the marker and closed by
    brace matching that ignores braces inside comments and string/character
    literals, so the span cannot drift on prose.
    """
    start = text.find(marker)
    if start == -1:
        return None
    open_index = text.find("{", start + len(marker))
    if open_index == -1:
        return None
    depth = 0
    index = open_index
    length = len(text)
    while index < length:
        character = text[index]
        following = text[index + 1] if index + 1 < length else ""
        if character == "/" and following == "/":
            newline = text.find("\n", index)
            index = length if newline == -1 else newline + 1
            continue
        if character == "/" and following == "*":
            closing = text.find("*/", index + 2)
            index = length if closing == -1 else closing + 2
            continue
        if character == '"' or character == "'":
            quote = character
            index += 1
            while index < length:
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return (open_index + 1, index)
        index += 1
    return None


def region(text: str, marker: str) -> str:
    """The brace-matched block after marker, or the empty string when absent."""
    span = brace_span(text, marker)
    return "" if span is None else text[span[0]:span[1]]


def accepted_assembly_bodies(text: str) -> list[tuple[str, int, str]]:
    """Return every brace-matched body for an accepted stock-aim constructor.

    The original constructor assembles the inputs directly. The explicit-input
    architecture moves that body to ``CurrentStockAimPoseInputsWithNeutralCapture``
    and leaves ``CurrentStockAimPoseInputs`` as a live-capture delegating
    wrapper. Retaining all matches lets the caller prove the frozen writes are
    owned by one body rather than accidentally accepting only the wrapper (or
    the first of multiple definitions).
    """
    bodies: list[tuple[str, int, str]] = []
    for label, marker in ASSEMBLY_DEFS:
        search_from = 0
        while True:
            marker_offset = text.find(marker, search_from)
            if marker_offset == -1:
                break
            span = brace_span(text[marker_offset:], marker)
            if span is None:
                bodies.append((label, marker_offset, ""))
            else:
                body_start = marker_offset + span[0]
                body_end = marker_offset + span[1]
                bodies.append((label, marker_offset, text[body_start:body_end]))
            search_from = marker_offset + len(marker)
    return bodies


def call_arguments(text: str, open_index: int) -> list[str] | None:
    """Top-level comma-separated arguments of a call's parenthesis.

    ``open_index`` points at the call's ``(``. Nesting and comments are
    skipped, so a comma in an argument expression or in prose cannot split the
    argument list; whitespace is collapsed. ``None`` when the parenthesis is
    never closed.
    """
    if open_index >= len(text) or text[open_index] != "(":
        return None
    depth = 0
    index = open_index
    length = len(text)
    argument_start = open_index + 1
    arguments: list[str] = []
    while index < length:
        character = text[index]
        if character == "/" and index + 1 < length:
            following = text[index + 1]
            if following == "/":
                newline = text.find("\n", index)
                index = length if newline == -1 else newline + 1
                continue
            if following == "*":
                closing = text.find("*/", index + 2)
                index = length if closing == -1 else closing + 2
                continue
        if character == '"' or character == "'":
            quote = character
            index += 1
            while index < length:
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == quote:
                    break
                index += 1
            index += 1
            continue
        if character in "([{":
            depth += 1
        elif character in ")]}":
            depth -= 1
            if depth == 0:
                arguments.append(normalize(text[argument_start:index]).strip())
                return arguments
        elif character == "," and depth == 1:
            arguments.append(normalize(text[argument_start:index]).strip())
            argument_start = index + 1
        index += 1
    return None


def check_tree(sources: dict[str, str]) -> list[str]:
    """Return the violation messages for the sources (empty when clean)."""
    violations: list[str] = []

    config_h = sources["config_h"]
    config_cpp = sources["config_cpp"]
    vr = sources["vr"]
    menu = sources["menu"]
    grip_logic = sources["grip_logic"]
    grab_logic = sources["grab_logic"]

    # 1. New configs enable PG; explicit saved values remain user-owned.
    if not CONFIG_TRUE.search(config_h):
        violations.append(
            "config.h no longer defaults persistent_support_grip to true"
        )
    if CONFIG_FIELD not in config_h:
        violations.append(
            f"config.h no longer declares {CONFIG_FIELD!r}: the feature's "
            "default-on field cannot be located"
        )

    # 2. Parse and save branches stay present.
    if CONFIG_PARSE_KEY not in config_cpp or CONFIG_PARSE_CALL not in config_cpp:
        violations.append(
            "config.cpp no longer parses the persistent_support_grip key into "
            "g_config.persistent_support_grip"
        )
    if CONFIG_SAVE not in config_cpp:
        violations.append(
            "config.cpp no longer writes the persistent_support_grip key with "
            "its generated comment"
        )

    # 3. Applicability predicate + the two gate consultations.
    if APPLIES_DEF not in grip_logic:
        violations.append(
            "support_grip_logic.h no longer defines "
            "PersistentSupportGripApplies: the transitional applicability gate "
            "cannot be located"
        )
    wired_body = region(vr, WIRED_DEF)
    if not wired_body:
        violations.append(
            "vr.cpp no longer defines VR_SupportGripWiredForTitle: the single "
            "runtime gate cannot be located"
        )
    else:
        if WIRED_CONFIG not in wired_body:
            violations.append(
                "VR_SupportGripWiredForTitle no longer ANDs "
                "g_config.persistent_support_grip: the feature-off short "
                "circuit is gone"
            )
        if WIRED_PREDICATE not in wired_body:
            violations.append(
                "VR_SupportGripWiredForTitle no longer consults "
                "PersistentSupportGripApplies: an unwired title would be "
                "exposed to a half-wired relationship"
            )
    latch_body = region(vr, LATCH_DEF)
    if not latch_body:
        violations.append(
            "vr.cpp no longer defines UpdateTwoHandLatch: the latch path "
            "cannot be located"
        )
    elif GATE_CALL not in latch_body:
        violations.append(
            "UpdateTwoHandLatch no longer consults "
            "VR_SupportGripWiredForTitle: the durable writer gate is gone"
        )

    # 3b. The durable writer call must sit inside the gate branch: the gate
    # call opens the branch, the writer call and its early return are the only
    # statements in it, and the dormant-state retire follows it. A writer
    # hoisted above the gate would run with the feature off or on an unwired
    # title, which the surrounding latch checks cannot see.
    if latch_body and GATE_CALL in latch_body:
        gate_span = brace_span(latch_body, GATE_CALL)
        writer_offsets = [
            match.start()
            for match in re.finditer(re.escape(WRITER_CALL), latch_body)
        ]
        if gate_span is None:
            violations.append(
                "could not brace-match the VR_SupportGripWiredForTitle branch "
                "of UpdateTwoHandLatch: the durable writer call cannot be proved "
                "to be owned by the gate"
            )
        else:
            outside = [
                offset for offset in writer_offsets
                if not (gate_span[0] <= offset < gate_span[1])
            ]
            if not writer_offsets or outside:
                violations.append(
                    f"{WRITER_CALL} is not called inside the "
                    "VR_SupportGripWiredForTitle branch of UpdateTwoHandLatch: a "
                    "writer hoisted above the gate would run with the feature "
                    "off"
                )
            retire_offset = latch_body.find(RETIRE_CALL)
            if retire_offset != -1 and retire_offset < gate_span[1]:
                violations.append(
                    f"{RETIRE_CALL} sits inside the durable-writer gate branch "
                    "of UpdateTwoHandLatch: the dormant-state retire must open "
                    "the base path the gate refuses"
                )
    qualify_body = region(vr, QUALIFY_DEF)
    if not qualify_body:
        violations.append(
            "vr.cpp no longer defines ResolveAimSupportQualification: the "
            "assembly-qualification path cannot be located"
        )
    elif GATE_CALL not in qualify_body:
        violations.append(
            "ResolveAimSupportQualification no longer consults "
            "VR_SupportGripWiredForTitle: the assembly would read durable state "
            "with the feature off or on an unwired title"
        )
    # The live invocation-trust input must reach QualifySolveSupport: the
    # engaged+trusted store inside that helper is only reachable after its
    # untrusted early return, so a hardcoded argument at the call site would
    # silently drop that protection for every denial channel.
    if qualify_body:
        qualify_call = re.search(
            re.escape(QUALIFY_CALL) + r"(?P<args>[^;]*)\);",
            normalize(qualify_body))
        if qualify_call is None:
            violations.append(
                f"ResolveAimSupportQualification no longer calls {QUALIFY_CALL}: "
                "the assembly would stop resolving the relationship"
            )
        else:
            qualify_arguments = [
                part.strip() for part in qualify_call.group("args").split(",")
            ]
            if not qualify_arguments or qualify_arguments[-1] != QUALIFY_LIVE_TRUST:
                violations.append(
                    "ResolveAimSupportQualification no longer passes the live "
                    f"{QUALIFY_LIVE_TRUST!r} input to QualifySolveSupport: a "
                    "hardcoded argument would let a denied invocation retain"
                )

    # 4. The PG-off latch branch keeps the base stores verbatim. The branch is
    # everything after the dormant-state retire inside UpdateTwoHandLatch: the
    # writer gate above it is the PG-on path and is checked separately.
    if latch_body:
        base_path = ""
        if RETIRE_CALL in latch_body:
            base_path = latch_body[latch_body.find(RETIRE_CALL):]
        else:
            violations.append(
                "the PG-off latch branch no longer retires dormant durable "
                "state before running the base path"
            )
        flat_latch = normalize(base_path)
        if BASE_HOLD_STORE not in flat_latch:
            violations.append(
                "the base UpdateTwoHandHold latch store is no longer present "
                "verbatim: PG-off hold parity is broken"
            )
        if BASE_TOGGLE_ON not in flat_latch or BASE_TOGGLE_OFF not in flat_latch:
            violations.append(
                "the base toggle on/off latch stores are no longer present "
                "verbatim: PG-off toggle parity is broken"
            )
        if BASE_GEOMETRY_CALL not in flat_latch:
            violations.append(
                "the PG-off latch branch no longer uses the base "
                "TwoHandGrabZoneHit geometry"
            )
        if PG_GEOMETRY_CALL in flat_latch:
            violations.append(
                "the PG-off latch branch calls the persistent-grip acquisition "
                "helper: the A003 rule must stay behind the applicability gate"
            )
        for call in WRITER_ONLY_CALLS:
            if call in flat_latch:
                violations.append(
                    f"the PG-off latch branch calls {call}: the base latch path "
                    "must not run durable-writer code"
                )

    # 5. One smoothing reseed and the owner-break invalidation pair.
    disc_count = vr.count(DISC_CALL)
    if disc_count != 1:
        violations.append(
            f"{DISC_CALL} occurs {disc_count} times in vr.cpp, expected "
            "exactly one: the reticle reseed must stay a single owner/epoch "
            "discontinuity rule"
        )
    if DISC_GUARD not in vr:
        violations.append(
            f"vr.cpp no longer guards the smoothing with {DISC_GUARD!r}: a "
            "reseed without the discontinuity term is unreachable"
        )
    if DISC_RESEED not in normalize(vr):
        violations.append(
            "the smoothing reseed no longer falls back to the raw pose: the "
            "discontinuity would blend instead of reseeding"
        )
    owner_break_body = region(vr, OWNER_BREAK)
    if not owner_break_body:
        violations.append(
            "vr.cpp has no PG owner-break block "
            "(DurableWriterAction::Invalidated): the N1/N2 invalidation cannot "
            "be located"
        )
    else:
        for call, label in ((INVALIDATE_AIM, "InvalidateAimContinuityLayer"),
                            (INVALIDATE_LAB, "InvalidateTwoHandLabTemporal")):
            if call not in owner_break_body:
                violations.append(
                    f"the PG owner-break block no longer calls {label}(): an "
                    "owner discontinuity could ease or re-present the previous "
                    "owner's geometry"
                )
        for required, label in (
            ("productReleasePending", "VS-OFF release-bridge qualification"),
            ("transition.previousLatched", "prior durable latch edge"),
            ("!identityLost", "lifecycle replacement exclusion"),
            ("InvalidateTwoHandInputSmoothingLayer();",
             "input-filter invalidation"),
        ):
            if required not in owner_break_body:
                violations.append(
                    f"the PG owner-break block lost {label}: owner replacement "
                    "must not retain unrelated filter state or suppress a "
                    "required VS-OFF release bridge"
                )

    # 6. No donor leftovers and no retired sway correction in the new paths.
    donor_scope = {
        "vr.cpp": vr,
        "menu.cpp": menu,
        "support_grip_logic.h": grip_logic,
        "support_grab_logic.h": grab_logic,
    }
    for name, text in donor_scope.items():
        if DONOR_SYMBOL in text:
            violations.append(
                f"{name} reintroduces {DONOR_SYMBOL}: the donor's raw-grip-pose "
                "endpoint selector never belonged to this port"
            )
        if DONOR_MENU_TEXT in text:
            violations.append(
                f"{name} reintroduces the donor endpoint menu copy "
                f"{DONOR_MENU_TEXT!r}: the endpoint-polarity experiment stays "
                "retired"
            )
    sway_scope = {
        "support_grip_logic.h": grip_logic,
        "support_grab_logic.h": grab_logic,
    }
    # The PG paths run from the section header to the end of the latch
    # function, i.e. every persistent-grip helper plus the gate and the base
    # branch it protects.
    pg_region = ""
    pg_start = vr.find(PG_WRITER_START)
    if pg_start != -1:
        latch_span = brace_span(vr, LATCH_DEF)
        pg_end = latch_span[1] if latch_span else len(vr)
        pg_region = vr[pg_start:pg_end]
        sway_scope["vr.cpp persistent-grip paths"] = pg_region
    else:
        violations.append(
            "vr.cpp no longer carries the persistent support grip section "
            "header: the PG paths cannot be located for the sway check"
        )
    for name, text in sway_scope.items():
        for token in SWAY_TOKENS:
            if token in text:
                violations.append(
                    f"{name} references {token}: the retired head-turn sway "
                    "correction must never return through this port"
                )

    # 7. Acquisition gate wiring (A003 predicate under the PG gate).
    writer_body = region(vr, WRITER_DEF)
    helper_body = region(vr, PG_HELPER_DEF)
    base_helper_body = region(vr, BASE_HELPER_DEF)
    if not writer_body:
        violations.append(
            "vr.cpp no longer defines UpdatePersistentSupportGrip: the durable "
            "writer cannot be located"
        )
    else:
        flat_writer = normalize(writer_body)
        if PG_GEOMETRY_CALL not in flat_writer:
            violations.append(
                "the durable writer no longer uses "
                "PersistentSupportGrabZoneHit: the A003 acquisition predicate is "
                "unwired"
            )
        if BASE_GEOMETRY_CALL in flat_writer:
            violations.append(
                "the durable writer uses the base TwoHandGrabZoneHit geometry: "
                "PG on would not run the archive's A003 acquisition rule"
            )
    if not helper_body:
        violations.append(
            "vr.cpp no longer defines PersistentSupportGrabZoneHit: the A003 "
            "acquisition rule cannot be located"
        )
    else:
        flat_helper = normalize(helper_body)
        if PG_PREDICATE_CALL not in flat_helper:
            violations.append(
                "PersistentSupportGrabZoneHit no longer evaluates "
                "support_grab::EvaluateSupportGrabZone"
            )
        if BASE_SUPPORT_FORWARD in flat_helper:
            violations.append(
                "PersistentSupportGrabZoneHit reads the support controller's "
                "own orientation: the A003 rule must not let wrist rotation "
                "orbit the acquisition sample"
            )
        for setting in ("g_config.two_hand_zone_right_m",
                        "g_config.left_grip_forward_m"):
            if setting not in flat_helper:
                violations.append(
                    f"PersistentSupportGrabZoneHit no longer passes {setting}: "
                    "the acquisition tuning must stay the shipped config values"
                )
    if not base_helper_body:
        violations.append(
            "vr.cpp no longer defines TwoHandGrabZoneHit: the base acquisition "
            "helper cannot be located"
        )
    else:
        flat_base = normalize(base_helper_body)
        if BASE_PREDICATE_CALL not in flat_base:
            violations.append(
                "TwoHandGrabZoneHit no longer samples through "
                "virtual_stock::SupportGrabPoint: the pre-fix base geometry is "
                "gone"
            )
        if BASE_SUPPORT_FORWARD not in flat_base:
            violations.append(
                "TwoHandGrabZoneHit no longer derives the palm-depth axis from "
                "the support controller's orientation: PG-off parity depends on "
                "that exact geometry"
            )

    # 8. T13 retained steering: one source of truth, one writer, one gate.
    stock = sources["stock"]
    stock_logic = sources["stock_logic"]
    if RETENTION_TRUE.search(vr):
        violations.append(
            "vr.cpp initialises supportSteeringRetained to true: the retention "
            "input must default false so PG off cannot retain"
        )
    if RETENTION_FIELD not in vr:
        violations.append(
            f"vr.cpp no longer declares {RETENTION_FIELD!r}: the T13 retention "
            "input cannot be located"
        )
    flat_stock = normalize(stock)
    flat_both = normalize(stock + "\n" + vr)
    writes = RETENTION_WRITE.findall(stock) + RETENTION_WRITE.findall(vr)
    if len(writes) != 1:
        violations.append(
            "supportSteeringRetained is written "
            f"{len(writes)} times across virtual_stock_aim.inl and vr.cpp, "
            "expected exactly one write inside the aim assembly"
        )
    else:
        if normalize(RETENTION_ASSIGN) not in flat_both:
            violations.append(
                "the retention input is not copied straight from the frozen "
                "qualification: it must be "
                f"{RETENTION_ASSIGN!r} so the engaged+trusted T5b result stays "
                "its only source"
            )
    # 8b. Telemetry-only forcing carry (PG frame provenance). Data only: the
    # frozen qualification is its single source, one accepted assembly body is
    # the only writer, and the solve result must pass it through verbatim so
    # consumers can name explicit forcing separately from an ordinary
    # two-hand-off frame.
    if FORCE_CARRY_TRUE.search(vr):
        violations.append(
            "vr.cpp initialises supportForceOneHand to true: the telemetry "
            "forcing carry must default false"
        )
    carry_declarations = vr.count(FORCE_CARRY_FIELD)
    if carry_declarations < 2:
        violations.append(
            f"vr.cpp declares {FORCE_CARRY_FIELD!r} {carry_declarations} "
            "time(s), expected both the AimPoseInputs input and the "
            "AimPoseResult receipt"
        )
    carry_writes = FORCE_CARRY_WRITE.findall(stock) + \
        FORCE_CARRY_WRITE.findall(vr)
    if len(carry_writes) != 1:
        violations.append(
            "supportForceOneHand is written "
            f"{len(carry_writes)} times across virtual_stock_aim.inl and "
            "vr.cpp, expected exactly one write inside the aim assembly"
        )
    else:
        if normalize(FORCE_CARRY_ASSIGN) not in flat_stock:
            violations.append(
                "the telemetry forcing carry is not copied straight from the "
                f"frozen qualification: it must be {FORCE_CARRY_ASSIGN!r} so "
                "the one-hand forcing decision keeps a single source of truth"
            )
    if normalize(FORCE_CARRY_RESULT) not in flat_stock:
        violations.append(
            "the solve result no longer copies the frozen forcing flag into "
            "its AimPoseResult receipt"
        )
    # The original single-function implementation and the explicit-input split
    # are both supported. Whichever accepted constructor owns assembly, both
    # frozen-qualification writes must be in that same single body; the wrapper
    # in the split architecture must not become a second writer.
    accepted_bodies = accepted_assembly_bodies(stock)
    retention_owners = [
        (label, offset)
        for label, offset, body in accepted_bodies
        if normalize(RETENTION_ASSIGN) in normalize(body)
    ]
    force_owners = [
        (label, offset)
        for label, offset, body in accepted_bodies
        if normalize(FORCE_CARRY_ASSIGN) in normalize(body)
    ]
    same_single_owner = (
        len(retention_owners) == 1
        and len(force_owners) == 1
        and retention_owners[0][1] == force_owners[0][1]
    )
    if not same_single_owner:
        violations.append(
            "the frozen-qualification writes are not both present inside "
            "exactly one accepted stock-aware aim assembly body (the original "
            "CurrentStockAimPoseInputs or the explicit-input "
            "CurrentStockAimPoseInputsWithNeutralCapture); found retention "
            f"owner(s) {retention_owners!r} and forcing owner(s) "
            f"{force_owners!r}"
        )
    # 8c. The carry must stay data-only: an occurrence allowlist for the token
    # across both files. Every line that names it must be one of the permitted
    # statements - the two default-false declarations, the single assembly
    # writer, the receipt copy and the prepared-frame payload copy that
    # publishes the receipt. An added read (e.g. an `if` on the field) or an
    # extra serializer is rejected here even though it would compile and leave
    # every write/declaration check above intact.
    for source_name, source_text in (("vr", vr), ("stock", stock)):
        allowed = FORCE_CARRY_ALLOWLIST[source_name]
        label = FORCE_CARRY_SOURCE_LABELS[source_name]
        seen: dict[str, int] = {}
        for index, line in enumerate(source_text.splitlines(), start=1):
            if FORCE_CARRY_TOKEN not in line:
                continue
            form = normalize(line).strip()
            if form not in allowed:
                violations.append(
                    f"{label}:{index} names {FORCE_CARRY_TOKEN} outside its "
                    f"allowlist ({form!r}): the carry must stay data-only: no "
                    "consumer may read it beyond the prepared-frame payload copy"
                )
            seen[form] = seen.get(form, 0) + 1
        for form, expected in allowed.items():
            if seen.get(form, 0) != expected:
                violations.append(
                    f"{label} carries {seen.get(form, 0)} occurrence(s) of "
                    f"{form!r}, expected {expected}: the carry must stay "
                    "data-only: no consumer may read it beyond the "
                    "prepared-frame payload copy"
                )
    if flat_stock.count(RETENTION_CONSUME) != 2:
        violations.append(
            "the retention input must be forwarded with its VS-off gate "
            f"({RETENTION_CONSUME!r}) by BOTH the legacy selection and its "
            f"trace re-derivation; found "
            f"{flat_stock.count(RETENTION_CONSUME)} use(s): retained steering "
            "and the recorded trace would describe different rules"
        )
    if HYBRID_OFFHAND_CALL not in flat_stock:
        violations.append(
            "the Hybrid offhand call no longer matches "
            f"{HYBRID_OFFHAND_CALL!r}: the VS-on hybrid path must keep its "
            "exact six-argument agreement-guarded call"
        )
    legacy_span = brace_span(stock_logic, LEGACY_SELECTOR_DEF)
    target_span = brace_span(stock_logic, TARGET_SELECTOR_DEF)
    if legacy_span is None:
        violations.append(
            "virtual_stock_logic.h no longer defines SelectTwoHandAimDirection: "
            "the legacy selection cannot be located"
        )
    else:
        legacy_signature = stock_logic[
            stock_logic.find(LEGACY_SELECTOR_DEF):legacy_span[0]]
        legacy_body = stock_logic[legacy_span[0]:legacy_span[1]]
        if HELPER_PARAM not in normalize(legacy_signature):
            violations.append(
                "SelectTwoHandAimDirection no longer offers the retention "
                f"parameter ({HELPER_PARAM!r})"
            )
        if "retainBeyondAgreementFloor" not in legacy_body:
            violations.append(
                "SelectTwoHandAimDirection no longer consumes its retention "
                "parameter: the legacy fallback would keep rejecting"
            )
    if target_span is None:
        violations.append(
            "virtual_stock_logic.h no longer defines "
            "SelectTwoHandAimDirectionForTarget: the VS-on target selection "
            "cannot be located"
        )
    else:
        target_signature = stock_logic[
            stock_logic.find(TARGET_SELECTOR_DEF):target_span[0]]
        target_body = stock_logic[target_span[0]:target_span[1]]
        if ("retainBeyondAgreementFloor" in target_signature or
                "retainBeyondAgreementFloor" in target_body):
            violations.append(
                "SelectTwoHandAimDirectionForTarget gained the retention "
                "parameter: the VS-on stock/target paths must stay unchanged"
            )
    if FLOOR_TEST not in stock_logic:
        violations.append(
            f"virtual_stock_logic.h no longer keeps the {FLOOR_TEST!r} "
            "agreement floor: the legacy threshold moved"
        )
    if NON_FINITE_AGREEMENT not in stock_logic:
        violations.append(
            "the acceptance helper no longer separates the non-finite "
            "agreement guard: retention must never accept an unusable forward"
        )
    if MIN_SEGMENT_GUARD not in stock_logic:
        violations.append(
            "the acceptance helper no longer keeps its minimum primary -> "
            "support segment guard"
        )
    if HELPER_RETURN not in stock_logic:
        violations.append(
            f"the acceptance helper no longer returns "
            f"{HELPER_RETURN!r}: retention would be unconditional"
        )
    elif not (stock_logic.find(NON_FINITE_AGREEMENT) <
              stock_logic.find(FLOOR_TEST) <
              stock_logic.find(HELPER_RETURN)):
        violations.append(
            "the retention return no longer sits inside the floor branch after "
            "the non-finite agreement guard: finiteness and minimum-length "
            "guards must run first"
        )
    if QUALIFY_FEATURE_OFF not in grip_logic or QUALIFY_UNTRUSTED not in grip_logic:
        violations.append(
            "QualifySolveSupport lost its feature-off short circuit or its "
            "untrusted early return: PG off or a denied invocation could retain"
        )
    elif grip_logic.count(QUALIFY_TRUST_STORE) != 1:
        violations.append(
            f"{QUALIFY_TRUST_STORE!r} occurs "
            f"{grip_logic.count(QUALIFY_TRUST_STORE)} times in "
            "support_grip_logic.h, expected exactly one engaged+trusted store"
        )
    elif not (grip_logic.find(QUALIFY_FEATURE_OFF) <
              grip_logic.find(QUALIFY_TRUST_STORE) and
              grip_logic.find(QUALIFY_UNTRUSTED) <
              grip_logic.find(QUALIFY_TRUST_STORE)):
        violations.append(
            "QualifySolveSupport stores supportTrusted before the feature-off "
            "short circuit or the untrusted early return: PG off or a denied "
            "invocation could retain"
        )

    # 9. The CE outermost-prepare support decision reads its own validated
    # local-player state (headset round 2). The muzzle read above it is gated
    # on the optional gun_barrel_aim feature, so consuming that state would
    # read generation 0 -> Unknown -> detach on every invocation with the
    # feature off. Only the prepare support block is scoped here: the sibling
    # CE consumers already read their own state per invocation.
    first_person = sources["first_person"]
    decision_index = -1
    search = 0
    while True:
        candidate = first_person.find(SUPPORT_DECISION_CALL, search)
        if candidate == -1:
            break
        if first_person.rfind(SUPPORT_PREPARE_BLOCK, 0, candidate) != -1:
            decision_index = candidate
        search = candidate + 1
    if decision_index == -1:
        violations.append(
            "haloce_first_person.cpp no longer resolves the CE "
            "outermost-prepare support decision through "
            "CeEvaluateSupportInvocation: the trust seam cannot be located"
        )
    else:
        block_start = first_person.rfind(
            SUPPORT_PREPARE_BLOCK, 0, decision_index)
        block = first_person[block_start:decision_index]
        reads = set(SUPPORT_TRUST_READ.findall(block))
        arguments = call_arguments(
            first_person, decision_index + len(SUPPORT_DECISION_CALL) - 1)
        state_argument = arguments[1] if arguments and len(arguments) > 1 else ""
        if not reads:
            violations.append(
                "the CE outermost-prepare support decision has no "
                "HaloCEControls_GetLocalPlayerState read of its own: the "
                "gun_barrel_aim-gated muzzle read must not be its owner source"
            )
        elif state_argument not in reads:
            violations.append(
                "the CE outermost-prepare support decision consumes "
                f"{state_argument!r} instead of the state returned by its own "
                f"HaloCEControls_GetLocalPlayerState read ({sorted(reads)!r}): "
                "a gun_barrel_aim-off installation would detach every "
                "invocation"
            )

    # 10. The 2026-09-28 weapon-switch policy. The default replacement drop
    # must not re-arm release-before-reacquire, the absence/generation arms
    # must stay, and the optional in-place inheritance must be gated by the
    # config option end to end.
    if SWITCH_TRUE.search(config_h):
        violations.append(
            "config.h sets two_hand_switch_inherit = true: the option must "
            "ship default off"
        )
    if SWITCH_FIELD not in config_h:
        violations.append(
            f"config.h no longer declares {SWITCH_FIELD!r}: the switch option's "
            "default-off field cannot be located"
        )
    if SWITCH_PARSE_KEY not in config_cpp or SWITCH_PARSE_CALL not in config_cpp:
        violations.append(
            "config.cpp no longer parses the two_hand_switch_inherit key into "
            "g_config.two_hand_switch_inherit"
        )
    if SWITCH_SAVE not in config_cpp:
        violations.append(
            "config.cpp no longer writes the two_hand_switch_inherit key"
        )
    step_body = region(grip_logic, WRITER_STEP_DEF)
    if not step_body:
        violations.append(
            "support_grip_logic.h no longer defines DurableWriterStep: the "
            "durable switch policy cannot be located"
        )
    else:
        flat_step = normalize(step_body)
        if POLICY_ABSENCE_ARM not in flat_step:
            violations.append(
                "DurableWriterStep no longer arms release-before-reacquire on a "
                "proven absence: a held Grip could bind whatever owner follows "
                "an empty weapon slot"
            )
        arm_sites = flat_step.count(POLICY_ARM_SITES)
        if arm_sites != 2:
            violations.append(
                f"DurableWriterStep carries {arm_sites} "
                f"{POLICY_ARM_SITES!r} arm sites, expected exactly two "
                "(generation replacement and proven absence): a proven owner "
                "replacement must never re-arm release-before-reacquire"
            )
        if POLICY_INHERIT_GUARD not in flat_step:
            violations.append(
                "DurableWriterStep no longer gates the in-place replacement "
                "rebind on the switch option plus a complete, current-"
                "generation, same-title owner"
            )
        if POLICY_INHERIT_BRANCH not in flat_step:
            violations.append(
                "DurableWriterStep's inherited rebind no longer keeps the "
                "relationship engaged at the next epoch"
            )
    if writer_body:
        flat_writer = normalize(writer_body)
        if SWITCH_OPTION_FIELD not in flat_writer:
            violations.append(
                "UpdatePersistentSupportGrip no longer reads "
                "g_config.two_hand_switch_inherit into its writer step"
            )
        if SWITCH_OPTION_ARG not in flat_writer:
            violations.append(
                "the switch option is not passed to DurableWriterStep: the "
                "inherit path would never run"
            )

    # 11. The 2026-09-28 headset follow-up: the default weapon switch must not
    # pre-hold the replacement. One arm site, one gated hold-mode edge, the
    # toggle edge untouched, and every clear site present.
    if ZONE_FLAG_TRUE.search(vr):
        violations.append(
            "vr.cpp initialises awaitZoneReentry to true: the default-switch "
            "crossing requirement must start clear"
        )
    if ZONE_FLAG_FIELD not in vr:
        violations.append(
            f"vr.cpp no longer declares {ZONE_FLAG_FIELD!r}: the default-switch "
            "crossing requirement cannot be located"
        )
    if writer_body:
        flat_writer = normalize(writer_body)
        if normalize(ZONE_SET_SITE) not in flat_writer:
            violations.append(
                "UpdatePersistentSupportGrip no longer arms the crossing "
                "requirement exactly at the no-inherit stage-2 replacement drop: "
                "the switched weapon would be pre-held by the next held/in-zone "
                "update"
            )
        arm_sites = flat_writer.count(ZONE_FLAG_SET)
        if arm_sites != 1:
            violations.append(
                f"UpdatePersistentSupportGrip sets awaitZoneReentry {arm_sites} "
                "times, expected exactly one (the default replacement drop: an "
                "absence, a generation replacement, a teardown and the inherit "
                "path must never arm it)"
            )
        if normalize(ZONE_STEP_SITE) not in flat_writer:
            violations.append(
                "UpdatePersistentSupportGrip no longer applies "
                "StepSwitchZoneReentry to its hold-mode acquisition edge and to "
                "the next update's requirement: the crossing rule is unwired"
            )
        if ZONE_HOLD_EDGE not in flat_writer:
            violations.append(
                "the hold-mode acquisition edge no longer consumes the "
                "StepSwitchZoneReentry result: a held Grip that never left the "
                "replacement's grab zone would re-bind it (pre-held switch)"
            )
        if ZONE_TOGGLE_EDGE not in flat_writer:
            violations.append(
                "the toggle-mode acquisition edge changed: a fresh press is "
                "already a deliberate acquisition and stays ungated"
            )
        if ZONE_UNTETHERED_EDGE in flat_writer:
            violations.append(
                "the hold-mode acquisition edge is back to the ungated "
                "level-triggered rule: the switched weapon would be pre-held"
            )
        if normalize(ZONE_TEARDOWN_CLEAR) not in flat_writer:
            violations.append(
                "a role/gesture/lifecycle teardown no longer clears "
                "awaitZoneReentry: a stale requirement would block the player's "
                "next ordinary hold"
            )
        if normalize(ZONE_INHERIT_CLEAR) not in flat_writer:
            violations.append(
                "the inherit rebind no longer clears awaitZoneReentry: the "
                "option's semantics must stay exactly as shipped"
            )
        explicit_release = region(writer_body, ZONE_EXPLICIT_RELEASE)
        if not explicit_release or ZONE_FLAG_CLEAR not in normalize(explicit_release):
            violations.append(
                "the explicit-release branch no longer clears awaitZoneReentry: "
                "the player's own release must end the crossing requirement"
            )
    retire_body = region(vr, RETIRE_DEF)
    if not retire_body or ZONE_FLAG_CLEAR not in normalize(retire_body):
        violations.append(
            "RetirePersistentSupportGripState no longer clears "
            "awaitZoneReentry: a feature disable must not hand the requirement "
            "to a later re-enable"
        )
    step_span = brace_span(grip_logic, ZONE_STEP_DEF)
    if step_span is None:
        violations.append(
            "support_grip_logic.h no longer defines StepSwitchZoneReentry: the "
            "crossing requirement's per-update policy is gone"
        )
    else:
        flat_step = normalize(grip_logic[step_span[0]:step_span[1]])
        if normalize(ZONE_STEP_SURVIVE) not in flat_step:
            violations.append(
                "StepSwitchZoneReentry no longer keeps the requirement only "
                "while the zone reading stays TRUE: a held Grip that never left "
                "the zone could re-acquire"
            )
        if normalize(ZONE_STEP_ACQUIRE) not in flat_step:
            violations.append(
                "StepSwitchZoneReentry no longer gates the hold-mode "
                "acquisition edge on the requirement"
            )
    drop_span = brace_span(grip_logic, ZONE_DROP_DEF)
    if drop_span is None:
        violations.append(
            "support_grip_logic.h no longer defines "
            "DefaultSwitchReplacementDrop: the transition that arms the "
            "crossing requirement cannot be located"
        )
    else:
        flat_drop = normalize(grip_logic[drop_span[0]:drop_span[1]])
        if normalize(ZONE_DROP_RETURN) not in flat_drop:
            violations.append(
                "DefaultSwitchReplacementDrop changed: it must stay the "
                "no-inherit stage-2 owner break only (engaged before, "
                "Invalidated, a different-owner claim, no lifecycle identity "
                "loss, no switch inherit)"
            )

    return violations


# ---------------------------------------------------------------------------
# Self-test: the checking logic is exercised on synthetic sources only.
# ---------------------------------------------------------------------------

GOOD_CONFIG_H = """\
// Synthetic config.h for the persistent-support-grip guard self-test.
struct Config
{
    bool persistent_support_grip = true;
    bool two_hand_switch_inherit = false;
};
extern Config g_config;
"""

GOOD_CONFIG_CPP = """\
// Synthetic config.cpp for the persistent-support-grip guard self-test.
void ConfigParse(const char* key, const char* val)
{
    if(!strcmp(key,"persistent_support_grip"))
    {
        ParseBoolSetting(key, val, g_config.persistent_support_grip);
    }
    if(!strcmp(key,"two_hand_switch_inherit"))
    {
        ParseBoolSetting(key, val, g_config.two_hand_switch_inherit);
    }
}

void ConfigSave(FILE* f)
{
    const Config d{};
    fprintf(f, "# (default %d)\\n", d.persistent_support_grip ? 1 : 0);
    fprintf(f, "persistent_support_grip = %d\\n\\n", g_config.persistent_support_grip ? 1 : 0);
    fprintf(f, "# (default %d)\\n", d.two_hand_switch_inherit ? 1 : 0);
    fprintf(f, "two_hand_switch_inherit = %d\\n\\n", g_config.two_hand_switch_inherit ? 1 : 0);
}
"""

GOOD_GRIP_LOGIC = """\
// Synthetic support_grip_logic.h for the persistent-support-grip guard.
namespace support_grip
{
constexpr bool PersistentSupportGripApplies(GameTitle title) noexcept
{
    return title != GameTitle::None;
}

inline DurableWriterAction DurableWriterStep(DurableWriterState& state,
    bool lifecycleGateOk, bool acquisitionRequested, OwnerEvidence evidence,
    const OwnerTuple& evidenceOwner, uint32_t activeGeneration,
    bool inheritReplacement = false) noexcept
{
    if (state.engaged && activeGeneration != 0 && state.owner.generation != 0 &&
        state.owner.generation != activeGeneration)
    {
        state.requiresRelease = true;
        state.owner = {};
        state.epoch++;
        state.engaged = false;
        return DurableWriterAction::Invalidated;
    }
    if (!lifecycleGateOk)
    {
        if (state.engaged)
        {
            state.owner = {};
            state.epoch++;
            state.engaged = false;
        }
        return DurableWriterAction::Released;
    }
    if (state.engaged)
    {
        if (evidence == OwnerEvidence::KnownAbsent)
        {
            state.requiresRelease = true;
            state.owner = {};
            state.epoch++;
            state.engaged = false;
            return DurableWriterAction::Invalidated;
        }
        if (evidence == OwnerEvidence::KnownPresent &&
            !SameOwner(evidenceOwner, state.owner))
        {
            if (inheritReplacement && OwnerComplete(evidenceOwner) &&
                evidenceOwner.title == state.owner.title &&
                evidenceOwner.generation == activeGeneration)
            {
                state.owner = evidenceOwner;
                state.epoch++;
                state.engaged = true;
                return DurableWriterAction::Bind;
            }
            state.owner = {};
            state.epoch++;
            state.engaged = false;
            return DurableWriterAction::Invalidated;
        }
        return DurableWriterAction::None;
    }
    if (acquisitionRequested && evidence == OwnerEvidence::KnownPresent &&
        OwnerComplete(evidenceOwner) &&
        evidenceOwner.generation == activeGeneration)
    {
        state.owner = evidenceOwner;
        state.epoch++;
        state.engaged = true;
        return DurableWriterAction::Bind;
    }
    return DurableWriterAction::None;
}

constexpr bool DefaultSwitchReplacementDrop(bool engagedBefore,
    DurableWriterAction action, bool evidenceIsDifferentOwner,
    bool lifecycleIdentityLost, bool switchInherit) noexcept
{
    return engagedBefore && action == DurableWriterAction::Invalidated &&
        evidenceIsDifferentOwner && !lifecycleIdentityLost && !switchInherit;
}

struct SwitchZoneReentryStep
{
    bool awaitZoneReentry = false;
    bool holdAcquisitionEdge = false;
};

constexpr SwitchZoneReentryStep StepSwitchZoneReentry(bool awaitZoneReentry,
    bool gripHeld, bool inZone) noexcept
{
    SwitchZoneReentryStep step{};
    step.awaitZoneReentry = awaitZoneReentry && inZone;
    step.holdAcquisitionEdge = gripHeld && inZone && !awaitZoneReentry;
    return step;
}

inline SolveSupportQualification QualifySolveSupport(bool featureEnabled,
    bool relationshipReadable, bool relationshipEngaged,
    uint64_t relationshipEpoch, bool invocationUntrusted) noexcept
{
    SolveSupportQualification qualification{};
    if (!featureEnabled)
        return qualification; // feature off: no read, no forcing, no receipt
    qualification.relationshipReadable = relationshipReadable;
    qualification.relationshipEngaged = relationshipEngaged;
    if (!relationshipReadable)
        return qualification;
    if (!relationshipEngaged)
        return qualification; // coherently disengaged: ordinary one-hand
    qualification.supportEpoch = relationshipEpoch;
    if (invocationUntrusted)
    {
        qualification.forceOneHand = true;
        return qualification; // engaged epoch retained; trusted stays false
    }
    qualification.supportTrusted = true;
    return qualification;
}
}
"""

GOOD_STOCK_LOGIC = """\
// Synthetic virtual_stock_logic.h for the persistent-support-grip guard.
namespace virtual_stock
{
struct DirectionSelection
{
    bool valid = false;
    bool usedVirtualStock = false;
    bool rejectedExtreme = false;
    float rejectedAgreement = 0.0f;
    bool retainedBeyondAgreementFloor = false;
    Point3 direction{0.0f, 0.0f, 0.0f};
};

inline bool TryBuildAcceptedSupportDirection(
    Point3 primary, Point3 support, Point3 primaryForward,
    Point3& out, bool& rejectedExtreme, float& rejectedAgreement,
    bool retainBeyondAgreementFloor = false) noexcept
{
    const float length = 1.0f;
    if (!std::isfinite(length) || length < 1.0e-4f)
        return false;
    const float agreement = 1.0f;
    if (!std::isfinite(agreement))
    {
        rejectedExtreme = true;
        rejectedAgreement = agreement;
        return false;
    }
    if (agreement < 0.35f)
    {
        rejectedExtreme = true;
        rejectedAgreement = agreement;
        return retainBeyondAgreementFloor;
    }
    return true;
}

inline DirectionSelection SelectTwoHandAimDirection(
    bool virtualStockEnabled, float stockStrength, float rearHeightM,
    bool headValid, Point3 head, Point3 stockSupport, Point3 primary,
    Point3 legacySupport, Point3 primaryForward,
    bool retainBeyondAgreementFloor = false) noexcept
{
    DirectionSelection result{};
    Point3 candidate{};
    bool floorRejected = false;
    float floorAgreement = 0.0f;
    const bool accepted = TryBuildAcceptedSupportDirection(
        primary, legacySupport, primaryForward, candidate,
        floorRejected, floorAgreement, retainBeyondAgreementFloor);
    result.valid = accepted;
    if (accepted)
        result.retainedBeyondAgreementFloor = floorRejected;
    return result;
}

inline DirectionSelection SelectTwoHandAimDirectionForTarget(
    bool virtualStockEnabled, float stockStrength, bool rearTargetValid,
    Point3 rearTarget, Point3 stockSupport, Point3 primary,
    Point3 legacySupport, Point3 primaryForward) noexcept
{
    DirectionSelection result{};
    return result;
}
}
"""

GOOD_STOCK = """\
// Synthetic original single-function virtual_stock_aim.inl for the guard.
        AimPoseInputs CurrentStockAimPoseInputs(bool rightValid) noexcept
        {
            AimPoseInputs inputs{};
            const support_grip::SolveSupportQualification supportQualification =
                ResolveAimSupportQualification();
            inputs.supportMayConsume = supportQualification.supportTrusted;
            inputs.supportSteeringRetained = supportQualification.supportTrusted;
            inputs.supportForceOneHand = supportQualification.forceOneHand;
            AimPoseResult result{};
            result.supportForceOneHand = inputs.supportForceOneHand;
            return inputs;
        }

        void Solve(const AimPoseInputs& inputs, DirectionSelection& selection)
        {
            selection = virtual_stock::SelectTwoHandAimDirection(
                inputs.virtualStockEnabled, stockStrength,
                inputs.virtualStockRearHeightM, inputs.headValid,
                fixedHeadBase, toPoint(supportEndpoint),
                legacyPrimary, legacySupport, toPoint(rawForward),
                inputs.supportSteeringRetained && !inputs.virtualStockEnabled);
            trace->bAccepted =
                virtual_stock::TryBuildAcceptedSupportDirection(
                    legacyPrimary, legacySupport, toPoint(rawForward),
                    bDirection, rejectedExtreme, rejectedAgreement,
                    inputs.supportSteeringRetained &&
                        !inputs.virtualStockEnabled);
            const bool offhandAccepted =
                virtual_stock::TryBuildAcceptedSupportDirection(
                    primary, support, primaryForward, offhandDirection,
                    rejectedExtreme, rejectedAgreement);
        }
"""

# The explicit-input architecture keeps assembly writes in the variant and
# makes the original live-capture constructor a thin delegating wrapper.
GOOD_STOCK_SPLIT = GOOD_STOCK.replace(
    "AimPoseInputs CurrentStockAimPoseInputs(bool rightValid) noexcept",
    "AimPoseInputs CurrentStockAimPoseInputsWithNeutralCapture(\n"
    "            bool rightValid, bool neutralCaptureValid) noexcept",
    1,
).replace(
    "        void Solve(",
    "        AimPoseInputs CurrentStockAimPoseInputs(bool rightValid) noexcept\n"
    "        {\n"
    "            return CurrentStockAimPoseInputsWithNeutralCapture(\n"
    "                rightValid, true);\n"
    "        }\n\n"
    "        void Solve(",
    1,
)

GOOD_GRAB_LOGIC = """\
// Synthetic support_grab_logic.h for the persistent-support-grip guard.
namespace support_grab
{
inline SupportGrabZone EvaluateSupportGrabZone(
    virtual_stock::Point3 primaryPosition,
    virtual_stock::Point3 acquisitionForward,
    virtual_stock::Point3 primaryRight,
    float zoneRightM,
    virtual_stock::Point3 rawSupportPosition,
    float gripForwardM) noexcept
{
    return SupportGrabZone{};
}
}
"""

GOOD_MENU = """\
// Synthetic menu.cpp for the persistent-support-grip guard self-test.
        changed |= ImGui::Checkbox("Persistent support grip (experimental)",
            &g_config.persistent_support_grip);
"""

GOOD_VR = """\
// Synthetic vr.cpp for the persistent-support-grip guard self-test.
    struct AimPoseInputs
    {
        bool supportMayConsume = false;
        bool supportSteeringRetained = false;
        bool supportForceOneHand = false;
        bool virtualStockEnabled = false;
    };

    struct AimPoseResult
    {
        bool supportForceOneHand = false;
    };

    thread_local bool g_supportInvocationUntrusted = false;

bool VR_SupportGripWiredForTitle(GameTitle title) noexcept
{
    return g_config.persistent_support_grip &&
        support_grip::PersistentSupportGripApplies(title);
}

support_grip::SolveSupportQualification ResolveAimSupportQualification() noexcept
{
    support_grip::SolveSupportQualification qualification{};
    if (!VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))
        return qualification;
    SupportGripRelationshipSnapshot snapshot{};
    const bool readable = ReadSupportGripRelationshipInternal(snapshot);
    return support_grip::QualifySolveSupport(true, readable,
        snapshot.engaged, snapshot.epoch, g_supportInvocationUntrusted);
}

// Persistent support grip (`persistent_support_grip`, default on)
    bool TwoHandGrabZoneHit(const XrPosef& rpose,
        const XrPosef& lpose) noexcept
    {
        const XrVector3f supportForward = Rotate(lpose.orientation, {0,0,-1});
        const virtual_stock::Point3 grabPoint =
            virtual_stock::SupportGrabPoint(
                virtual_stock::Point3{
                    lpose.position.x, lpose.position.y, lpose.position.z},
                virtual_stock::Point3{
                    supportForward.x, supportForward.y, supportForward.z},
                g_config.left_grip_forward_m);
        return grabPoint.x != 0.0f;
    }

    bool PersistentSupportGrabZoneHit(const XrPosef& rpose,
        const XrPosef& lpose) noexcept
    {
        const XrVector3f rfwd = Rotate(rpose.orientation, {0,0,-1});
        const XrVector3f rright = Rotate(rpose.orientation, {1,0,0});
        const support_grab::SupportGrabZone zone =
            support_grab::EvaluateSupportGrabZone(
                virtual_stock::Point3{
                    rpose.position.x, rpose.position.y, rpose.position.z},
                virtual_stock::Point3{rfwd.x, rfwd.y, rfwd.z},
                virtual_stock::Point3{rright.x, rright.y, rright.z},
                g_config.two_hand_zone_right_m,
                virtual_stock::Point3{
                    lpose.position.x, lpose.position.y, lpose.position.z},
                g_config.left_grip_forward_m);
        return zone.inZone;
    }

    struct PersistentSupportGripState
    {
        bool awaitZoneReentry = false;
    };
    PersistentSupportGripState g_persistentSupportGrip;

    void RetirePersistentSupportGripState() noexcept
    {
        g_persistentSupportGrip.awaitZoneReentry = false;
    }

    void UpdatePersistentSupportGrip(SupportGripAdmission& admission,
        bool rightValid, const XrPosef& rpose, bool leftValid,
        const XrPosef& lpose, bool gripHeld, bool rising, bool grabAllowed,
        bool rolesChanged, bool weaponGesture)
    {
        const bool switchInherit = g_config.two_hand_switch_inherit;
        const bool explicitRelease = writer.engaged && !gripHeld;
        const bool forcedTeardown = rolesChanged || weaponGesture;
        if (forcedTeardown)
            g_persistentSupportGrip.awaitZoneReentry = false;
        const bool inZone = trackingValid &&
            PersistentSupportGrabZoneHit(rpose, lpose);
        const support_grip::SwitchZoneReentryStep zoneReentry =
            support_grip::StepSwitchZoneReentry(
                g_persistentSupportGrip.awaitZoneReentry, gripHeld, inZone);
        g_persistentSupportGrip.awaitZoneReentry =
            zoneReentry.awaitZoneReentry;
        const bool acquisitionEdge = toggleMode
            ? (rising && inZone && !writer.engaged)
            : zoneReentry.holdAcquisitionEdge;
        (void)inZone;
        if (explicitRelease)
        {
            g_persistentSupportGrip.awaitZoneReentry = false;
            (void)support_grip::DurableWriterRelease(writer);
        }
        (void)support_grip::DurableWriterStep(writer, true, gripHeld,
            OwnerEvidence::Unknown, {}, 1, switchInherit);
        if (support_grip::DefaultSwitchReplacementDrop(engagedBefore, action,
                evidence == support_grip::OwnerEvidence::KnownPresent &&
                    !support_grip::SameOwner(evidenceOwner, previousOwner),
                identityLost, switchInherit))
            g_persistentSupportGrip.awaitZoneReentry = true;
        if (acquisitionEdge)
        {
            if (engagedBefore &&
                !support_grip::SameOwner(previousOwner, writer.owner))
            {
                g_persistentSupportGrip.awaitZoneReentry = false;
                LOG("Persistent support grip: switched-weapon inheritance "
                    "rebound owner (epoch %llu) title=%u",
                    static_cast<unsigned long long>(writer.epoch),
                    static_cast<unsigned>(writer.owner.title));
            }
        }
    }

    void UpdateTwoHandLatch(bool rightValid, const XrPosef& rpose,
                            bool leftValid, const XrPosef& lpose, float gripL,
                            bool rolesChanged, bool weaponGesture = false)
    {
        if (VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))
        {
            UpdatePersistentSupportGrip(admission, rightValid, rpose, leftValid,
                lpose, gripHeld, rising, grabAllowed, rolesChanged,
                weaponGesture);
            return;
        }
        RetirePersistentSupportGripState();
        if (!grabAllowed || !g_config.two_handed_aim || !rightValid || !leftValid || weaponGesture)
        {
            g_twoHandLatched.store(false);
            g_twoHandActive.store(false);
            return;
        }
        const bool inZone = TwoHandGrabZoneHit(rpose, lpose);

        if (g_config.two_hand_toggle)
        {
            if (rising)
            {
                if (g_twoHandLatched.load()) g_twoHandLatched.store(false);      // toggle off
                else if (inZone)             g_twoHandLatched.store(true);       // toggle on
            }
        }
        else // hold mode
        {
            g_twoHandLatched.store(UpdateTwoHandHold(
                g_twoHandLatched.load(), gripHeld, inZone));
        }
    }

        if (action == support_grip::DurableWriterAction::Invalidated ||
            identityLost)
        {
            const bool productReleasePending = !writer.engaged && !identityLost &&
                ((g_aimContinuityLayer.transition.initialized &&
                    g_aimContinuityLayer.transition.previousLatched) ||
                 (g_aimContinuityLayer.transition.active &&
                    g_aimContinuityLayer.transition.phase == Release));
            if (productReleasePending)
                InvalidateTwoHandInputSmoothingLayer();
            else
                InvalidateAimContinuityLayer();
            InvalidateTwoHandLabTemporal();
        }

                            const bool supportDiscontinuity =
                                support_grip::ReticleSmoothingDiscontinuity(
                                    supportOwnerSafe,
                                    aimSupportReceipt.supportEpoch,
                                    g_reticleSupportEpoch);
                            g_reticleAimPose = g_reticleAimPoseValid &&
                                    smoothing > 0.0f && !supportDiscontinuity
                                ? SmoothTrackedPose(rawAim, g_reticleAimPose, smoothing)
                                : rawAim;

    void FillPersistentSupportGripTelemetry(TelemetryFrame& frame,
        const AimPoseInputs& inputs) noexcept
    {
        frame.supportForceOneHand = inputs.supportForceOneHand;
    }
"""


GOOD_FIRST_PERSON = """\
// Synthetic haloce_first_person.cpp fragment for the persistent-support-grip
// guard self-test: the CE outermost-prepare support trust seam. The muzzle read
// is gated on the optional gun_barrel_aim feature; the trust decision owns a
// separate validated read in its own block.
void __fastcall PrepareHook(int16_t user)
{
    const bool supportOutermostPrepare = supportWired && (user == 0 && !previous);
    if (supportOutermostPrepare)
    {
        HaloCELocalPlayerState supportState{};
        const bool supportReadOk = HaloCEControls_GetLocalPlayerState(supportState);
        VR_PublishSupportGripOwnerEvidence(GameTitle::HaloCE, 0, {}, {});
    }
    if (user == 0 && !previous)
    {
        HaloCELocalPlayerState state{};
        if (local.context.tracking.controllers.gunBarrelAim &&
            HaloCEControls_GetLocalPlayerState(state) && LocalOnFootShooter(state.unit))
        { local.muzzleUnit = state.unit; local.muzzleWeapon = state.weapon; }
        if (supportOutermostPrepare)
        {
            HaloCELocalPlayerState supportTrustState{};
            if (!HaloCEControls_GetLocalPlayerState(supportTrustState))
                supportTrustState = HaloCELocalPlayerState{};
            supportDecision = CeEvaluateSupportInvocation(supportWired,
                supportTrustState,
                local.context.tracking.controllers.primaryAimSupportDerived);
        }
    }
}
"""


def good_texts() -> dict[str, str]:
    return {
        "config_h": GOOD_CONFIG_H,
        "config_cpp": GOOD_CONFIG_CPP,
        "vr": GOOD_VR,
        "menu": GOOD_MENU,
        "grip_logic": GOOD_GRIP_LOGIC,
        "grab_logic": GOOD_GRAB_LOGIC,
        "stock": GOOD_STOCK,
        "stock_logic": GOOD_STOCK_LOGIC,
        "first_person": GOOD_FIRST_PERSON,
    }


def with_source(sources: dict[str, str], name: str, fragment: str) -> dict[str, str]:
    mutated = dict(sources)
    mutated[name] = fragment
    return mutated


def require_violation(sources: dict[str, str], needle: str, message: str) -> None:
    violations = check_tree(sources)
    require(bool(violations), f"{message}: expected a violation, got none")
    require(
        any(needle in violation for violation in violations),
        f"{message}: expected a violation mentioning {needle!r}, got "
        f"{violations!r}",
    )


def run_self_test() -> None:
    good = good_texts()
    clean = check_tree(good)
    require(
        clean == [],
        f"the original single-function assembly shape was rejected: {clean!r}")

    original_shape = check_tree(with_source(good, "stock", GOOD_STOCK))
    require(
        original_shape == [],
        "the original CurrentStockAimPoseInputs assembly shape was rejected: "
        f"{original_shape!r}")

    split_shape = check_tree(with_source(good, "stock", GOOD_STOCK_SPLIT))
    require(
        split_shape == [],
        "the explicit-input assembly plus delegating wrapper shape was "
        f"rejected: {split_shape!r}")

    outside_assembly_writes = GOOD_STOCK_SPLIT.replace(
        RETENTION_ASSIGN, "").replace(FORCE_CARRY_ASSIGN, "")
    outside_writer = (
        "        void CopyFrozenQualificationOutsideAssembly("
        "AimPoseInputs& inputs)\n"
        "        {\n"
        "            const support_grip::SolveSupportQualification "
        "supportQualification{};\n"
        f"            {RETENTION_ASSIGN}\n"
        f"            {FORCE_CARRY_ASSIGN}\n"
        "        }\n\n"
    )
    outside_assembly_writes = outside_assembly_writes.replace(
        "        void Solve(", outside_writer + "        void Solve(", 1)
    require_violation(
        with_source(good, "stock", outside_assembly_writes),
        "exactly one accepted stock-aware aim assembly body",
        "frozen-qualification writes outside every accepted assembly must be "
        "rejected")

    duplicate_accepted_writer = GOOD_STOCK_SPLIT.replace(
        "            return CurrentStockAimPoseInputsWithNeutralCapture(\n"
        "                rightValid, true);\n",
        "            const support_grip::SolveSupportQualification "
        "supportQualification{};\n"
        f"            {RETENTION_ASSIGN}\n"
        f"            {FORCE_CARRY_ASSIGN}\n"
        "            return CurrentStockAimPoseInputsWithNeutralCapture(\n"
        "                rightValid, true);\n",
        1,
    )
    require_violation(
        with_source(good, "stock", duplicate_accepted_writer),
        "expected exactly one write",
        "duplicate writers in the explicit-input function and its wrapper must "
        "be rejected")
    duplicate_assembly_violations = check_tree(
        with_source(good, "stock", duplicate_accepted_writer))
    require(
        any("exactly one accepted stock-aware aim assembly body" in violation
            for violation in duplicate_assembly_violations),
        "a duplicate assembly writer was not rejected by the one-owner check: "
        f"{duplicate_assembly_violations!r}")

    flipped_default = GOOD_CONFIG_H.replace(
        "persistent_support_grip = true;", "persistent_support_grip = false;")
    require_violation(
        with_source(good, "config_h", flipped_default),
        "defaults persistent_support_grip to true",
        "a default-false config field must be rejected")

    missing_parse = GOOD_CONFIG_CPP.replace(
        "        ParseBoolSetting(key, val, g_config.persistent_support_grip);\n",
        "")
    require_violation(
        with_source(good, "config_cpp", missing_parse), "no longer parses",
        "a missing parse branch must be rejected")

    missing_save = GOOD_CONFIG_CPP.replace(
        '    fprintf(f, "persistent_support_grip = %d\\n\\n", '
        "g_config.persistent_support_grip ? 1 : 0);\n",
        "")
    require_violation(
        with_source(good, "config_cpp", missing_save), "no longer writes",
        "a missing save branch must be rejected")

    missing_predicate = GOOD_GRIP_LOGIC.replace(
        "PersistentSupportGripApplies", "SomeOtherPredicate")
    require_violation(
        with_source(good, "grip_logic", missing_predicate),
        "PersistentSupportGripApplies",
        "a missing applicability predicate must be rejected")

    gate_without_flag = GOOD_VR.replace(
        "return g_config.persistent_support_grip &&\n        "
        "support_grip::PersistentSupportGripApplies(title);",
        "return support_grip::PersistentSupportGripApplies(title);")
    require_violation(
        with_source(good, "vr", gate_without_flag), "feature-off short circuit",
        "a gate that forgets the config flag must be rejected")

    gate_without_predicate = GOOD_VR.replace(
        "g_config.persistent_support_grip &&\n        "
        "support_grip::PersistentSupportGripApplies(title);",
        "g_config.persistent_support_grip;")
    require_violation(
        with_source(good, "vr", gate_without_predicate),
        "PersistentSupportGripApplies",
        "a gate that forgets the applicability predicate must be rejected")

    latch_without_gate = GOOD_VR.replace(
        "        if (VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))\n"
        "        {\n"
        "            UpdatePersistentSupportGrip(admission, rightValid, rpose, leftValid,\n",
        "        if (true)\n"
        "        {\n"
        "            UpdatePersistentSupportGrip(admission, rightValid, rpose, leftValid,\n")
    require_violation(
        with_source(good, "vr", latch_without_gate), "writer gate is gone",
        "a writer path that stops consulting the gate must be rejected")

    writer_above_gate = GOOD_VR.replace(
        "        if (VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))\n"
        "        {\n"
        "            UpdatePersistentSupportGrip(admission, rightValid, rpose, leftValid,\n"
        "                lpose, gripHeld, rising, grabAllowed, rolesChanged,\n"
        "                weaponGesture);\n"
        "            return;\n"
        "        }\n",
        "        UpdatePersistentSupportGrip(admission, rightValid, rpose, leftValid,\n"
        "            lpose, gripHeld, rising, grabAllowed, rolesChanged,\n"
        "            weaponGesture);\n"
        "        if (VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))\n"
        "        {\n"
        "            return;\n"
        "        }\n")
    require_violation(
        with_source(good, "vr", writer_above_gate), "hoisted above the gate",
        "a durable writer hoisted above the feature gate must be rejected")

    qualify_without_gate = GOOD_VR.replace(
        "    if (!VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))\n"
        "        return qualification;\n",
        "")
    require_violation(
        with_source(good, "vr", qualify_without_gate),
        "ResolveAimSupportQualification no longer consults",
        "an assembly path that stops short-circuiting must be rejected")

    latch_without_retire = GOOD_VR.replace(
        "        RetirePersistentSupportGripState();\n", "")
    require_violation(
        with_source(good, "vr", latch_without_retire), "retires dormant",
        "a latch path that forgets to retire durable state must be rejected")

    latch_without_hold_store = GOOD_VR.replace(
        "            g_twoHandLatched.store(UpdateTwoHandHold(\n"
        "                g_twoHandLatched.load(), gripHeld, inZone));\n",
        "            g_twoHandLatched.store(inZone);\n")
    require_violation(
        with_source(good, "vr", latch_without_hold_store), "UpdateTwoHandHold",
        "a changed base hold store must be rejected")

    latch_without_toggle_store = GOOD_VR.replace(
        "                else if (inZone)             g_twoHandLatched.store(true);       // toggle on\n",
        "")
    require_violation(
        with_source(good, "vr", latch_without_toggle_store), "toggle on/off",
        "a missing base toggle store must be rejected")

    latch_with_pg_geometry = GOOD_VR.replace(
        "        const bool inZone = TwoHandGrabZoneHit(rpose, lpose);",
        "        const bool inZone = PersistentSupportGrabZoneHit(rpose, lpose);")
    require_violation(
        with_source(good, "vr", latch_with_pg_geometry),
        "persistent-grip acquisition helper",
        "the base latch path calling the A003 helper must be rejected")

    latch_with_writer_call = GOOD_VR.replace(
        "        const bool inZone = TwoHandGrabZoneHit(rpose, lpose);",
        "        const bool inZone = TwoHandGrabZoneHit(rpose, lpose);\n"
        "        (void)support_grip::DurableWriterStep(writer);")
    require_violation(
        with_source(good, "vr", latch_with_writer_call), "DurableWriterStep",
        "durable-writer code in the PG-off latch path must be rejected")

    second_reseed = GOOD_VR.replace(
        "                                : rawAim;",
        "                                : rawAim;\n"
        "                            const bool again =\n"
        "                                support_grip::ReticleSmoothingDiscontinuity(\n"
        "                                    ownerSafe, epoch, lastEpoch);")
    require_violation(
        with_source(good, "vr", second_reseed), "expected exactly one",
        "a second reticle reseed must be rejected")

    missing_reseed_guard = GOOD_VR.replace("!supportDiscontinuity", "true")
    require_violation(
        with_source(good, "vr", missing_reseed_guard), "!supportDiscontinuity",
        "a smoothing path without the discontinuity guard must be rejected")

    missing_lab_invalidate = GOOD_VR.replace(
        "            InvalidateTwoHandLabTemporal();\n", "")
    require_violation(
        with_source(good, "vr", missing_lab_invalidate),
        "InvalidateTwoHandLabTemporal",
        "an owner break without the Lab invalidation must be rejected")

    owner_break_without_lifecycle_guard = GOOD_VR.replace(
        "!writer.engaged && !identityLost",
        "!writer.engaged")
    require_violation(
        with_source(good, "vr", owner_break_without_lifecycle_guard),
        "lifecycle replacement exclusion",
        "a title/generation replacement must not retain a VS-OFF release bridge")

    donor_symbol = GOOD_VR + \
        "inline bool SupportGripEndpointUsesRawGripPose(const Config& c);\n"
    require_violation(
        with_source(good, "vr", donor_symbol), DONOR_SYMBOL,
        "the donor raw-grip-pose selector must be rejected")

    donor_menu = GOOD_MENU + \
        '        bool useGripPoint = SupportGripEndpointUsesRawGripPose(g_config);\n' \
        '        ImGui::Checkbox("Use grip point for support hand", &useGripPoint);\n'
    require_violation(
        with_source(good, "menu", donor_menu), DONOR_MENU_TEXT,
        "the donor endpoint-polarity menu copy must be rejected")

    sway_in_logic = GOOD_GRIP_LOGIC.replace(
        "return title != GameTitle::None;",
        "return g_config.virtual_stock_head_turn_sway_correction;")
    require_violation(
        with_source(good, "grip_logic", sway_in_logic),
        "virtual_stock_head_turn_sway_correction",
        "a sway-correction symbol in the pure module must be rejected")

    sway_in_writer = GOOD_CONFIG_CPP.replace(
        '    if(!strcmp(key,"persistent_support_grip"))',
        '    if (g_config.virtual_stock_head_turn_sway_correction) return;\n'
        '    if(!strcmp(key,"persistent_support_grip"))')
    # The sway check only covers the PG paths, not config.cpp: prove that a
    # sway token placed inside the vr.cpp PG region is rejected instead.
    sway_in_pg_region = GOOD_VR.replace(
        "    void RetirePersistentSupportGripState() noexcept",
        "    float swayCorrection =\n"
        "        g_config.virtual_stock_head_turn_sway_correction;\n"
        "    void RetirePersistentSupportGripState() noexcept")
    require_violation(
        with_source(good, "vr", sway_in_pg_region),
        "virtual_stock_head_turn_sway_correction",
        "a sway-correction symbol in the PG writer paths must be rejected")
    clean_config = check_tree(with_source(good, "config_cpp", sway_in_writer))
    require(clean_config == [],
            "the sway check must stay scoped to the PG paths: config.cpp text "
            f"was rejected: {clean_config!r}")

    unwired_predicate = GOOD_VR.replace(
        "            PersistentSupportGrabZoneHit(rpose, lpose);",
        "            TwoHandGrabZoneHit(rpose, lpose);")
    require_violation(
        with_source(good, "vr", unwired_predicate), "predicate is unwired",
        "the durable writer falling back to the base geometry must be rejected")

    base_loses_support_forward = GOOD_VR.replace(
        "        const XrVector3f supportForward = Rotate(lpose.orientation, {0,0,-1});",
        "        const XrVector3f supportForward = Rotate(rpose.orientation, {0,0,-1});")
    require_violation(
        with_source(good, "vr", base_loses_support_forward),
        "support controller's orientation",
        "a base helper that stops using the support forward must be rejected")

    helper_reads_support_wrist = GOOD_VR.replace(
        "        const XrVector3f rfwd = Rotate(rpose.orientation, {0,0,-1});",
        "        const XrVector3f rfwd = Rotate(rpose.orientation, {0,0,-1});\n"
        "        const XrVector3f leak = Rotate(lpose.orientation, {0,0,-1});\n"
        "        (void)leak;")
    require_violation(
        with_source(good, "vr", helper_reads_support_wrist),
        "support controller's own orientation",
        "the A003 helper reading the support wrist must be rejected")

    missing_helper_geometry = GOOD_VR.replace(
        "                g_config.two_hand_zone_right_m,",
        "                0.0f,")
    require_violation(
        with_source(good, "vr", missing_helper_geometry),
        "g_config.two_hand_zone_right_m",
        "the A003 helper dropping the shipped zone tuning must be rejected")

    # Telemetry-only forcing carry.
    force_carry_default_true = GOOD_VR.replace(
        "bool supportForceOneHand = false;",
        "bool supportForceOneHand = true;")
    require_violation(
        with_source(good, "vr", force_carry_default_true), "default false",
        "a default-true telemetry forcing carry must be rejected")

    force_carry_hardcoded = GOOD_STOCK.replace(
        FORCE_CARRY_ASSIGN, "inputs.supportForceOneHand = true;")
    require_violation(
        with_source(good, "stock", force_carry_hardcoded),
        "supportQualification.forceOneHand",
        "a telemetry forcing carry not copied from the frozen qualification "
        "must be rejected")

    force_carry_second_writer = GOOD_STOCK.replace(
        "        void Solve(",
        "        void ForceAnyway(AimPoseInputs& inputs)\n"
        "        {\n"
        "            inputs.supportForceOneHand = true;\n"
        "        }\n\n"
        "        void Solve(")
    require_violation(
        with_source(good, "stock", force_carry_second_writer),
        "exactly one write",
        "a second forcing-carry writer must be rejected")

    force_carry_without_receipt = GOOD_STOCK.replace(
        FORCE_CARRY_RESULT + "\n", "")
    require_violation(
        with_source(good, "stock", force_carry_without_receipt),
        "AimPoseResult receipt",
        "a solve result that drops the forcing carry must be rejected")

    force_carry_missing_declaration = GOOD_VR.replace(
        "        bool supportForceOneHand = false;\n", "", 1)
    require_violation(
        with_source(good, "vr", force_carry_missing_declaration),
        "AimPoseInputs input",
        "a tree without the input declaration must be rejected")

    # The carry is data-only: a read of the copy is a new consumer and must be
    # rejected even though it compiles and leaves every write/declaration check
    # above intact.
    force_carry_read = GOOD_STOCK.replace(
        "        void Solve(",
        "        bool ForceForControl(const AimPoseInputs& inputs)\n"
        "        {\n"
        "            if (inputs.supportForceOneHand)\n"
        "                return true;\n"
        "            return false;\n"
        "        }\n\n"
        "        void Solve(")
    require_violation(
        with_source(good, "stock", force_carry_read),
        "the carry must stay data-only: no consumer may read it",
        "a read of the telemetry forcing carry must be rejected")

    force_carry_payload_removed = GOOD_VR.replace(
        "        frame.supportForceOneHand = inputs.supportForceOneHand;\n", "")
    require_violation(
        with_source(good, "vr", force_carry_payload_removed),
        "expected 1: the carry must stay data-only",
        "a tree without the prepared-frame payload copy must be rejected")

    # T13 retained-steering corrective.
    retention_default_true = GOOD_VR.replace(
        "bool supportSteeringRetained = false;",
        "bool supportSteeringRetained = true;")
    require_violation(
        with_source(good, "vr", retention_default_true), "default false",
        "a default-true retention input must be rejected")

    retention_hardcoded = GOOD_STOCK.replace(
        "inputs.supportSteeringRetained = "
        "supportQualification.supportTrusted;",
        "inputs.supportSteeringRetained = true;")
    require_violation(
        with_source(good, "stock", retention_hardcoded),
        "supportQualification.supportTrusted",
        "a retention input not copied from the frozen qualification must be "
        "rejected")

    retention_second_writer = GOOD_STOCK.replace(
        "        void Solve(",
        "        void RetainAnyway(AimPoseInputs& inputs)\n"
        "        {\n"
        "            inputs.supportSteeringRetained = true;\n"
        "        }\n\n"
        "        void Solve(")
    require_violation(
        with_source(good, "stock", retention_second_writer),
        "exactly one write",
        "a second retention writer must be rejected")

    retention_not_consumed = GOOD_STOCK.replace(RETENTION_CONSUME, "false")
    require_violation(
        with_source(good, "stock", retention_not_consumed),
        "trace re-derivation",
        "a legacy selection (or its trace re-derivation) that drops the "
        "retention input must be rejected")

    retention_only_selection = GOOD_STOCK.replace(
        "                    inputs.supportSteeringRetained &&\n"
        "                        !inputs.virtualStockEnabled);",
        "                    false);")
    require_violation(
        with_source(good, "stock", retention_only_selection),
        "trace re-derivation",
        "a trace re-derivation that drops the retention input must be rejected")

    hybrid_gains_retention = GOOD_STOCK.replace(
        "                    rejectedExtreme, rejectedAgreement);",
        "                    rejectedExtreme, rejectedAgreement,\n"
        "                    inputs.supportSteeringRetained);")
    require_violation(
        with_source(good, "stock", hybrid_gains_retention),
        "Hybrid offhand call",
        "the Hybrid offhand path gaining retention must be rejected")

    target_gains_retention = GOOD_STOCK_LOGIC.replace(
        "    Point3 legacySupport, Point3 primaryForward) noexcept\n"
        "{\n"
        "    DirectionSelection result{};\n"
        "    return result;\n"
        "}",
        "    Point3 legacySupport, Point3 primaryForward,\n"
        "    bool retainBeyondAgreementFloor = false) noexcept\n"
        "{\n"
        "    DirectionSelection result{};\n"
        "    return result;\n"
        "}")
    require_violation(
        with_source(good, "stock_logic", target_gains_retention), "VS-on",
        "the target-aware (VS-on) selector gaining retention must be rejected")

    retention_unconditional = GOOD_STOCK_LOGIC.replace(
        "        return retainBeyondAgreementFloor;",
        "        return true;")
    require_violation(
        with_source(good, "stock_logic", retention_unconditional),
        "return retainBeyondAgreementFloor",
        "an acceptance helper that ignores its retention parameter must be "
        "rejected")

    floor_lowered = GOOD_STOCK_LOGIC.replace(
        "    if (agreement < 0.35f)", "    if (agreement < 0.10f)")
    require_violation(
        with_source(good, "stock_logic", floor_lowered), "agreement floor",
        "a lowered legacy agreement floor must be rejected")

    qualify_trust_unconditional = GOOD_GRIP_LOGIC.replace(
        "    if (invocationUntrusted)\n"
        "    {\n"
        "        qualification.forceOneHand = true;\n"
        "        return qualification; // engaged epoch retained; trusted stays false\n"
        "    }\n"
        "    qualification.supportTrusted = true;\n",
        "    qualification.supportTrusted = true;\n"
        "    if (invocationUntrusted)\n"
        "    {\n"
        "        qualification.forceOneHand = true;\n"
        "        return qualification; // engaged epoch retained; trusted stays false\n"
        "    }\n")
    require_violation(
        with_source(good, "grip_logic", qualify_trust_unconditional),
        "untrusted early return",
        "a qualification that trusts before the untrusted early return must be "
        "rejected")

    # T14-VER-03 F2: the live invocation-trust input must reach the call, and
    # the non-finite-agreement guard must stay in the acceptance helper.
    qualification_ignores_live_trust = GOOD_VR.replace(
        "        snapshot.engaged, snapshot.epoch, "
        "g_supportInvocationUntrusted);",
        "        snapshot.engaged, snapshot.epoch, false);")
    require_violation(
        with_source(good, "vr", qualification_ignores_live_trust),
        "g_supportInvocationUntrusted",
        "a qualification call that stops passing the live untrusted input must "
        "be rejected")

    missing_non_finite_agreement_guard = GOOD_STOCK_LOGIC.replace(
        "    if (!std::isfinite(agreement))\n"
        "    {\n"
        "        rejectedExtreme = true;\n"
        "        rejectedAgreement = agreement;\n"
        "        return false;\n"
        "    }\n",
        "")
    require_violation(
        with_source(good, "stock_logic", missing_non_finite_agreement_guard),
        "non-finite agreement",
        "a deleted non-finite-agreement guard must be rejected")

    # Headset round 2, CE: the outermost-prepare support decision must consume
    # the state written by its own validated read, never the gun_barrel_aim
    # -gated muzzle state.
    gated_state_decision = GOOD_FIRST_PERSON.replace(
        "            supportDecision = CeEvaluateSupportInvocation(supportWired,\n"
        "                supportTrustState,\n",
        "            supportDecision = CeEvaluateSupportInvocation(supportWired,\n"
        "                state,\n")
    require_violation(
        with_source(good, "first_person", gated_state_decision),
        "instead of the state returned by its own",
        "a CE prepare support decision consuming the gated muzzle state must be "
        "rejected")

    missing_trust_read = GOOD_FIRST_PERSON.replace(
        "            HaloCELocalPlayerState supportTrustState{};\n"
        "            if (!HaloCEControls_GetLocalPlayerState(supportTrustState))\n"
        "                supportTrustState = HaloCELocalPlayerState{};\n"
        "            supportDecision = CeEvaluateSupportInvocation(supportWired,\n"
        "                supportTrustState,\n"
        "                local.context.tracking.controllers.primaryAimSupportDerived);\n",
        "            supportDecision = CeEvaluateSupportInvocation(supportWired,\n"
        "                state,\n"
        "                local.context.tracking.controllers.primaryAimSupportDerived);\n")
    require_violation(
        with_source(good, "first_person", missing_trust_read),
        "no HaloCEControls_GetLocalPlayerState read of its own",
        "a CE prepare support decision without its own local-player read must be "
        "rejected")

    # 2026-09-28 weapon-switch policy: the option, the arm sites and the
    # option-gated in-place inheritance.
    clean_switch = check_tree(good)
    require(clean_switch == [],
            f"the clean switch-policy synthetic tree was rejected: "
            f"{clean_switch!r}")

    switch_default_true = GOOD_CONFIG_H.replace(
        "two_hand_switch_inherit = false;",
        "two_hand_switch_inherit = true;")
    require_violation(
        with_source(good, "config_h", switch_default_true), "default off",
        "a default-true switch-inherit field must be rejected")

    switch_missing_parse = GOOD_CONFIG_CPP.replace(
        "        ParseBoolSetting(key, val, g_config.two_hand_switch_inherit);\n",
        "")
    require_violation(
        with_source(good, "config_cpp", switch_missing_parse),
        "no longer parses the two_hand_switch_inherit key",
        "a missing switch-inherit parse branch must be rejected")

    switch_missing_save = GOOD_CONFIG_CPP.replace(
        '    fprintf(f, "two_hand_switch_inherit = %d\\n\\n", '
        "g_config.two_hand_switch_inherit ? 1 : 0);\n",
        "")
    require_violation(
        with_source(good, "config_cpp", switch_missing_save),
        "no longer writes the two_hand_switch_inherit key",
        "a missing switch-inherit save branch must be rejected")

    switch_option_not_passed = GOOD_VR.replace(", switchInherit);", ");")
    require_violation(
        with_source(good, "vr", switch_option_not_passed),
        "switch option is not passed",
        "the durable writer dropping the switch option must be rejected")

    switch_option_hardcoded = GOOD_VR.replace(
        "const bool switchInherit = g_config.two_hand_switch_inherit;",
        "const bool switchInherit = true;")
    require_violation(
        with_source(good, "vr", switch_option_hardcoded),
        "no longer reads g_config.two_hand_switch_inherit",
        "a hardcoded switch option must be rejected")

    replacement_rearms = GOOD_GRIP_LOGIC.replace(
        "            if (inheritReplacement && OwnerComplete(evidenceOwner) &&",
        "            state.requiresRelease = true;\n"
        "            if (inheritReplacement && OwnerComplete(evidenceOwner) &&")
    require_violation(
        with_source(good, "grip_logic", replacement_rearms),
        "expected exactly two",
        "a re-armed owner replacement must be rejected")

    absence_without_arm = GOOD_GRIP_LOGIC.replace(
        "        if (evidence == OwnerEvidence::KnownAbsent)\n"
        "        {\n"
        "            state.requiresRelease = true;\n",
        "        if (evidence == OwnerEvidence::KnownAbsent)\n"
        "        {\n")
    require_violation(
        with_source(good, "grip_logic", absence_without_arm),
        "proven absence",
        "a proven absence that stops arming must be rejected")

    inherit_ungated = GOOD_GRIP_LOGIC.replace(
        "            if (inheritReplacement && OwnerComplete(evidenceOwner) &&",
        "            if (OwnerComplete(evidenceOwner) &&")
    require_violation(
        with_source(good, "grip_logic", inherit_ungated),
        "gates the in-place replacement rebind",
        "an ungated in-place inheritance must be rejected")

    inherit_drops_engagement = GOOD_GRIP_LOGIC.replace(
        "                state.owner = evidenceOwner;\n"
        "                state.epoch++;\n"
        "                state.engaged = true;\n",
        "                state.owner = evidenceOwner;\n"
        "                state.epoch++;\n")
    require_violation(
        with_source(good, "grip_logic", inherit_drops_engagement),
        "inherited rebind",
        "an inheritance that stops keeping the relationship engaged must be "
        "rejected")

    # 2026-09-28 default-switch re-grip UX (headset follow-up): the crossing
    # requirement's single arm site, its gated hold edge, its clear sites and the
    # pure rules it depends on.
    clean_zone_reentry = check_tree(good)
    require(clean_zone_reentry == [],
            f"the clean crossing-requirement synthetic tree was rejected: "
            f"{clean_zone_reentry!r}")

    zone_flag_default_true = GOOD_VR.replace(
        "bool awaitZoneReentry = false;", "bool awaitZoneReentry = true;")
    require_violation(
        with_source(good, "vr", zone_flag_default_true), "must start clear",
        "a default-true crossing requirement must be rejected")

    zone_missing_field = GOOD_VR.replace(
        "        bool awaitZoneReentry = false;\n", "")
    require_violation(
        with_source(good, "vr", zone_missing_field),
        "crossing requirement cannot be located",
        "a missing crossing-requirement flag must be rejected")

    zone_not_armed = GOOD_VR.replace(
        "            g_persistentSupportGrip.awaitZoneReentry = true;\n", "")
    require_violation(
        with_source(good, "vr", zone_not_armed),
        "no longer arms the crossing requirement",
        "a writer that stops arming the requirement at the replacement drop "
        "must be rejected")

    zone_double_armed = GOOD_VR.replace(
        "        (void)inZone;",
        "        g_persistentSupportGrip.awaitZoneReentry = true;\n"
        "        (void)inZone;")
    require_violation(
        with_source(good, "vr", zone_double_armed), "expected exactly one",
        "a second arm site for the requirement must be rejected")

    zone_edge_ungated = GOOD_VR.replace(
        "            : zoneReentry.holdAcquisitionEdge;",
        "            : (gripHeld && inZone);")
    require_violation(
        with_source(good, "vr", zone_edge_ungated),
        "ungated level-triggered rule",
        "the hold-mode edge falling back to the ungated level rule must be "
        "rejected")

    zone_toggle_gated = GOOD_VR.replace(
        "            ? (rising && inZone && !writer.engaged)",
        "            ? (rising && inZone && !writer.engaged &&\n"
        "                !g_persistentSupportGrip.awaitZoneReentry)")
    require_violation(
        with_source(good, "vr", zone_toggle_gated),
        "toggle-mode acquisition edge changed",
        "a toggle-mode edge gated by the crossing requirement must be rejected")

    zone_step_ignored = GOOD_VR.replace(
        "        g_persistentSupportGrip.awaitZoneReentry =\n"
        "            zoneReentry.awaitZoneReentry;\n", "")
    require_violation(
        with_source(good, "vr", zone_step_ignored), "crossing rule is unwired",
        "a writer that drops the requirement's next-update state must be "
        "rejected")

    zone_teardown_keeps = GOOD_VR.replace(
        "        if (forcedTeardown)\n"
        "            g_persistentSupportGrip.awaitZoneReentry = false;\n", "")
    require_violation(
        with_source(good, "vr", zone_teardown_keeps), "teardown no longer clears",
        "a teardown that keeps the requirement must be rejected")

    zone_retire_keeps = GOOD_VR.replace(
        "        g_persistentSupportGrip.awaitZoneReentry = false;\n"
        "    }\n\n    void UpdatePersistentSupportGrip",
        "    }\n\n    void UpdatePersistentSupportGrip")
    require_violation(
        with_source(good, "vr", zone_retire_keeps),
        "RetirePersistentSupportGripState no longer clears",
        "a feature retire that keeps the requirement must be rejected")

    zone_release_keeps = GOOD_VR.replace(
        "        if (explicitRelease)\n"
        "        {\n"
        "            g_persistentSupportGrip.awaitZoneReentry = false;\n"
        "            (void)support_grip::DurableWriterRelease(writer);\n"
        "        }\n",
        "        if (explicitRelease)\n"
        "        {\n"
        "            (void)support_grip::DurableWriterRelease(writer);\n"
        "        }\n")
    require_violation(
        with_source(good, "vr", zone_release_keeps),
        "explicit-release branch no longer clears",
        "an explicit release that keeps the requirement must be rejected")

    zone_inherit_keeps = GOOD_VR.replace(
        "                g_persistentSupportGrip.awaitZoneReentry = false;\n"
        '                LOG("Persistent support grip: switched-weapon inheritance "',
        '                LOG("Persistent support grip: switched-weapon inheritance "')
    require_violation(
        with_source(good, "vr", zone_inherit_keeps),
        "inherit rebind no longer clears",
        "the inherit rebind keeping the requirement must be rejected")

    zone_survives_clear = GOOD_GRIP_LOGIC.replace(
        "    step.awaitZoneReentry = awaitZoneReentry && inZone;",
        "    step.awaitZoneReentry = awaitZoneReentry;")
    require_violation(
        with_source(good, "grip_logic", zone_survives_clear),
        "only while the zone reading stays TRUE",
        "a requirement that survives a FALSE zone reading must be rejected")

    zone_step_ungated = GOOD_GRIP_LOGIC.replace(
        "    step.holdAcquisitionEdge = gripHeld && inZone && !awaitZoneReentry;",
        "    step.holdAcquisitionEdge = gripHeld && inZone;")
    require_violation(
        with_source(good, "grip_logic", zone_step_ungated),
        "no longer gates the hold-mode",
        "an ungated hold-mode acquisition edge must be rejected")

    zone_drop_widened = GOOD_GRIP_LOGIC.replace(
        "        evidenceIsDifferentOwner && !lifecycleIdentityLost && !switchInherit;",
        "        evidenceIsDifferentOwner && !lifecycleIdentityLost;")
    require_violation(
        with_source(good, "grip_logic", zone_drop_widened),
        "DefaultSwitchReplacementDrop changed",
        "a widened replacement-drop predicate must be rejected")


def read_tree(root: Path) -> dict[str, str]:
    paths = {
        "config_h": root / "src" / "common" / "config.h",
        "config_cpp": root / "src" / "common" / "config.cpp",
        "vr": root / "src" / "dll" / "vr.cpp",
        "menu": root / "src" / "dll" / "menu.cpp",
        "grip_logic": root / "src" / "common" / "support_grip_logic.h",
        "grab_logic": root / "src" / "common" / "support_grab_logic.h",
        "stock": root / "src" / "dll" / "virtual_stock_aim.inl",
        "stock_logic": root / "src" / "common" / "virtual_stock_logic.h",
        "first_person": root / "src" / "dll" / "haloce_first_person.cpp",
    }
    sources: dict[str, str] = {}
    for name, path in paths.items():
        require(path.is_file(), f"expected source file {path.as_posix()}")
        sources[name] = path.read_text(encoding="utf-8-sig")
    return sources


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=REPO_ROOT,
        help="repository root holding the checked sources",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="also exercise the checking logic on synthetic sources",
    )
    parser.add_argument(
        "--self-test-only",
        action="store_true",
        help="run only the synthetic self-test",
    )
    args = parser.parse_args()

    if args.self_test or args.self_test_only:
        run_self_test()
        if args.self_test_only:
            print("persistent support grip guard self-test passed")
            return 0

    violations = check_tree(read_tree(args.root))
    if violations:
        for violation in violations:
            print(f"FAIL: {violation}", file=sys.stderr)
        return 1
    print(
        "persistent support grip guard passed: config default/parse/save, "
        "applicability gate, base latch stores, reseed/owner-break pair, "
        "donor-leftover, acquisition-gate, retained-steering, forcing-carry "
        "allowlist, CE prepare trust-source, weapon-switch-policy and "
        "default-switch crossing-requirement wiring clean "
        f"under {args.root.as_posix()}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
