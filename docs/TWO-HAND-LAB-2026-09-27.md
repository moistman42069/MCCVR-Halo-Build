# Two-Handed Lab (experimental rig)

## September 28 product-continuity and input-smoothing update

The fixed 200 ms latch-edge transition is now owned by the product continuity
layer for both Virtual Stock and VS-OFF. The Lab no longer owns or stacks a
`Continuity200ms` temporal mode: the retained historical ordinal normalizes to
`None`, and its radio is absent. Constant and adaptive Lab damping remain the
only Lab temporal experiments and engage only on frames the product seam does
not own (`TwoHandLabTemporalEngagedFor`: Lab enabled, resolved Virtual Stock
off, `two_handed_aim` off), so the two 200 ms corrections can never stack.
Their target is the product-presented two-hand orientation when a matching
product output exists for the serial, otherwise the frame's raw stateless Lab
orientation; on every frame the predicate engages, the product seam produced
no matching output, so the raw stateless orientation is the live target. The
separate optional `two_hand_smoothing_strength` setting
(0..25) filters controller-input copies for applicable two-hand solves (Virtual
Stock on or off), as a wet/dry mix over the unchanged fixed speed-25 filter; it
is not Lab damping, it never steers the Lab (the Lab stays VS-OFF only) and it
never changes the VS-OFF-only product Grip -> Grip geometry.

The Lab page mirrors the two shared product controls shown with Weapon & Aim:
`Persistent support grip` and the `Two-Hand Smoothing` slider. They write the
same real Config fields; Reset Lab resets only the runtime-only Lab settings and
leaves these product controls unchanged. The retired
`two_hand_transition_smoothing` toggle is not on the page (nor anywhere else):
the fixed 200 ms product latch continuity is fixed-on internal behaviour with no
user control. The candidate's headset acceptance is pending.

Status: experimental, runtime-only, disabled by default. This rig changes no
Virtual Stock Standard/Plus semantics, no latch/presentation/`twoHandActive`
semantics, no 0.35 acceptance floor, no `result.pose.position`, no
config/persistence, and no weapon heuristics. Tranche 1 (pure logic) and
tranche 2A (store, anchor/influence/agreement integration, single-source D/S,
diagnostics) are the baseline; this document covers tranche 2B: the temporal
experiment modes, their presentation to aim consumers, the telemetry family,
and the Lab UI page (a first-class F1 sidebar category).

## 1. What the Lab is

A VS-off experimental rig for two-hand anchor selection (the aim/grip pivot
matrix: Production/AA/AG/GA/GG), soft off-hand authority, and temporal
damping of the emitted aim orientation. It is inert until explicitly enabled
from its own Two-Handed Lab page, never persists, and resets to
`two_hand_lab::DefaultSettings()` (disabled) on process restart. The Lab only
ever steers solves whose resolved inputs have Virtual Stock off; Virtual
Stock paths are behaviorally unchanged (proven by the tranche-2A parity
suite).

Since 2026-09-29 the page is **hidden from normal navigation** exactly like
`Cat_VirtualStockLab`: no F1 sidebar entry, no Advanced doorway and no back
button. The page block, runtime store and diagnostics remain wired and build
as before — this is research/diagnostic infrastructure, not a player surface.
The `Production` anchor is the **pre-GG production shape**: the primary Aim
observation paired with the production-selected support endpoint (the
VS-eligible `semantic_support_endpoint`), passed through rather than
re-derived. It is not the current GG free two-hand product geometry, which
consumes the fixed primary-Grip -> support-Grip positional pair whenever the
Lab is not active.

## 2. Temporal modes (mutually exclusive)

One `two_hand_lab::TemporalMode` enum selects at most one mode; modes are
never stacked:

- `None`: identity. Presented == the frame's stateless Lab orientation
  (the calibrated `result.pose.orientation` of the Lab-active solve).
- Historical ordinal `Continuity200ms`: normalizes to `None` and has no UI
  selector. Product latch continuity is owned by
  `src/common/virtual_stock_aim_continuity.h` for both VS-on and VS-off paths;
  Lab has no duplicate continuity state.
- `ConstantDamping`: exponential approach `alpha = 1-exp(-dt/tau)` as a
  shortest-arc slerp from `previous` toward the frame's stateless target
  (tranche-1 `ConstantDampingStep`). `tau<=eps` answers the exact target.
  Seeds to the target on the first valid frame after any reset.
- `AdaptiveDamping`: same chain with the tranche-1 slow/fast/fullErrorDeg
  blend (`AdaptiveDampingStep`); `Normalize` enforces slow>=fast and finite.

Temporal engagement is the shared product predicate
`TwoHandLabTemporalEngagedFor` (`src/common/virtual_stock_settings.h`): Lab
stored-enabled AND the frame's own resolved inputs have Virtual Stock off AND
`two_handed_aim` is off. The product 200 ms continuity seam owns every frame
whose two-handed aim is enabled (VS-on and VS-off alike), so the Lab temporal
layer stands down and drops its history there, and the two 200 ms corrections
can never stack. When damping runs, its target is the product-presented
two-hand orientation when a matching product output exists for the serial,
otherwise the frame's raw stateless target; because the product seam does not
own an engaged frame, the effective live target is the raw stateless
orientation. The fragment-level product-target path remains exercised
directly by `tests/two_hand_lab_temporal_tests.cpp`.

All temporal state lives outside the pure solve, advances at most once per
prepared serial on the prepared-frame dt
(`g_preparedFrame.predictedDisplayDelta`, like the VS seam), never mutates
during telemetry counterfactual solves (those inputs are Lab-inert), and
never feeds back into tracking or B geometry. Raw stateless telemetry stays
raw; the presented orientation is reported separately.

## 3. Where it is wired

- Pure steps: `src/common/two_hand_lab_logic.h` (tranche 1, unchanged
  except the additive pivot diagnostics below).
- Production fragment: `src/dll/two_hand_lab_temporal_runtime.inl`. Follows
  the `virtual_stock_aim_continuity_runtime.inl` pattern: store-free, so
  `tests/two_hand_lab_temporal_tests.cpp` includes the shipping body against
  the generated aim fixture, and `vr.cpp` includes it too. Owns
  `TwoHandLabTemporalState`, `TwoHandLabTemporalFrameSolve`,
  `ResetTwoHandLabTemporal`, and `AdvanceTwoHandLabTemporalFrame`.
- Product latch continuity is advanced by the shared `vr.cpp` seam for both
  VS-on and VS-off; the Lab does not allocate or advance a second continuity
  state. VS Standard/Plus ownership-edge behavior remains, while VS-OFF product
  transitions are driven only by durable latch edges.
- Solver trace: `src/dll/virtual_stock_aim.inl` fills `trace.lab` on every
  Lab-active solve, including one-hand solves (`bValid=false`,
  resolved echoes requested, zero effective authority, no pivots) and the
  selected pivot pair (`pivotsValid` + the same D/S the selection used).

### Packet identity (same-serial settings-toggle guard)

The runtime store carries a generation counter incremented on every publish
(`TwoHandLabPublication::generation`, monotonic, never reset).
`CurrentStockAimPoseInputs` stamps the snapshot's generation into
`AimPoseInputs::twoHandLabGeneration` at assembly. The Lab temporal packet
(serial, contact-space epoch, generation, validity, stateless orientation,
correction) echoes the generation of the solve its correction belongs to.
Consumers fail open (no correction) on any serial/epoch/generation/own-solve
mismatch: the frame-thread publishers compare against their own solve's
generation, and `VR_GetAimPose` does the same through the lock-free packet.
The packet stays lock-free (seqlock; no blocking locks on the aim path).

### Presentation consumers

The Lab's presented orientation reaches the same consumers the VS continuity
reaches, called AFTER the existing VS calls at each site (at most one
correction is ever active; both inactive is identity; VS lines untouched):

- frame-thread publishers via `PresentTwoHandLabTemporal` /
  `PresentedTwoHandLabTemporalPose` (contact snapshot, Reach/Halo 2/Halo 4
  render snapshots, CE rig);
- cross-thread `VR_GetAimPose` via `PresentTwoHandLabTemporalFromPublication`
  (lock-free packet read).

The packet is additionally gated inactive while the VS continuity layer
itself applies, so VS active always implies Lab packet inactive.

### Reset sites

`InvalidateTwoHandLabTemporal()` mirrors every `InvalidateAimContinuityLayer`
site with the same semantics (the routine prepared-frame retire does NOT
invalidate, exactly like VS):

1. aborted prepared frame (`EndPreparedFrameWithoutLayers`);
2. fatal wait drain (`EnterFrameWaitFatalDrain`);
3. reference-space change pending;
4. session exiting / loss pending;
5. instance loss pending;
6. title/generation/epoch identity change (Lab seam's own tracking);
7. seam inapplicability (Lab disabled, resolved VS-on, or two-handed aim
   enabled so the product continuity seam owns the frame);
8. weapon-handedness swap.

Plus mode change, Lab enable/disable toggle, and applicability edges (VS
on/off, and two-handed aim on/off i.e. the product seam owning the frame;
detected per serial; history resets before the new serial is processed),
and Lab Reset (publishes disabled, so the next serial takes the inactive
path and drops history).

## 4. Diagnostics

The frame diagnostics publication carries the stateless Lab observation plus
temporal fields: temporal mode, `temporalActive`, and the damping error angle
between previous and target. The retired continuity ordinal normalizes to None
and therefore cannot report a Lab transition. Diagnostics publish for EVERY
Lab-applicable frame (stored-enabled and resolved VS-off), including
one-hand/exact-A solves, so the UI shows honest status. `Diagnostics` also
carries the selected pivots (`pivotsValid` + `primaryPivot` + `supportPivot`)
filled from the actual resolved pair the live selection used.

## 5. Telemetry

Additive schema-2 `two_hand_lab_*` family (see `docs/TELEMETRY-SYSTEM.md`
§8.1/§8.5): enabled, requested/resolved anchors, fallback, requested
offhand influence, agreement mode, agreement, agreement confidence,
effective influence, temporal mode, primary/support pivots (+valid),
stateless direction (+valid), presented direction (+valid, only while
temporal is active), temporal active, temporal error deg. Sources: the
frame's canonical solve trace Lab payload (stateless) + the Lab temporal
packet/state (presented); geometry is never recomputed in the recorder.
Validator cross-field invariants: confidence in [0,1], effective <=
requested + tolerance, enum ranges, presented requires temporal active,
pivots require valid flags, disabled reads inactive defaults.

## 6. UI

The shared product controls appear at the top of this page as well as under
Weapon & Aim: `Persistent support grip` and the `Two-Hand Smoothing` 0..25
slider. They bind directly to `g_config`; they are not Lab runtime settings.
Reset Lab calls only the Lab settings reset and cannot modify these Config
values. The retired `two_hand_transition_smoothing` toggle is neither on this
page nor on Weapon & Aim: the 200 ms product latch continuity is fixed-on.

Hidden `Cat_TwoHandLab` page (`src/dll/two_hand_lab_menu.inl`): since
2026-09-29 it has **no F1 sidebar entry**, exactly like `Cat_VirtualStockLab`.
The category, page block and runtime store stay wired for internal
research/diagnostic use, so the page is reachable only by editing the sidebar
skip (never by a player in normal navigation); there is no Advanced doorway and
no "< Back to Weapon & Aim" button. Status block (the two-hand reading uses
numerical steering vocabulary — "Numerical two-hand steering: Active|Inactive" —
because it derives from the numerical aim output, not a latch), explicit enable
checkbox (opening the page never enables), one-per-line positional anchor radios
("Current behaviour (Production)", "Aim pos → Aim pos (AA)",
"Aim pos → Grip pos (AG)", "Grip pos → Aim pos (GA)", "Grip pos → Grip pos (GG)";
each names two POSITIONS — primary/trigger-hand then support/front-hand — pivots
only, with the primary Aim orientation still supplying the orientation/roll
baseline), influence slider, agreement radios + soft full-authority slider (dot
value + equivalent A-B angle), temporal radios + per-mode parameters, quick
presets (Baseline / AA 100 / AG 100 / GG 100 / GG 75 / GG 50) as complete
deterministic states built by `two_hand_lab::QuickPresetSettings` — enabled,
anchor, and influence pinned while Agreement resets to Legacy hard and Temporal
to None (canonical dormant soft/damping seeds), so consecutive presets never
contaminate each other; no encoded winner), telemetry recorder state + Start/Stop
through the existing recorder API, and Reset Lab (defaults + disables; never
touches Virtual Stock). The global Reset ALL on the Advanced page additionally
calls `two_hand_lab_runtime::ResetSettings()`, while the Virtual Stock reset
never touches the Lab. Runtime-only: `SetSettings`/`ResetSettings` only; no
ConfigSave, no config keys, no persistence. The UI never recomputes solver
geometry (settings + last diagnostics only).

## 7. Tests

- `tests/two_hand_lab_logic_tests.cpp`: tranche-1 pure coverage plus the
  `QuickPresetSettings` pin — every preset asserts enabled, its exact
  anchor/influence, Legacy hard agreement, None temporal, and canonical
  dormant soft/damping seeds; a contaminated-settings scenario proves the
  builder is independent of prior state; Baseline asserts the all-clean
  enabled Production/1.00 state.
- `tools/check_two_hand_lab_patch.py` (`halomccvr_two_hand_lab_patch_guard`
  CTest, stdlib-only, `--self-test` runs synthetic passing/failing fixtures
  plus the real tree): both Lab categories stay hidden from the sidebar
  (`Cat_VirtualStockLab` and `Cat_TwoHandLab` skips present), no
  Advanced doorway, no Back button, numerical steering wording, preset
  region uses the builder with no live-state read, no mutation before the
  enable checkbox, global reset calls `ResetSettings` while the Virtual
  Stock reset touches no Lab runtime, and the logic header carries no stale
  tranche-1 claim.
- `tests/two_hand_lab_temporal_tests.cpp` (new): live temporal suite against
  shipping code — identity fail-open, grab/release no-jump seeds, 200 ms
  exact identity, duplicate-serial discipline, acceptance-without-edge never
  reseeds, VS-on inactivity, mode-switch/lifecycle/disable resets, constant
  first-frame/convergence/tau-eps/guards, adaptive fast-vs-slow/monotonic/
  guards, no NaN/Inf anywhere.
- `tests/telemetry_recorder_tests.cpp`: Lab family in the evidence frame,
  enabled/disabled serialization, and live-solve-to-serialized identity
  across the anchor/agreement/temporal matrix (resolved label, effective
  influence, D/S pivots, stateless vs presented).
- `tools/validate_telemetry_jsonl.py`: Lab family checks + fixture
  assertions; absence stays legacy-valid.

## 8. Experiment record and limits

- Seed damping values (150 ms, 400/50 ms, 8 deg) are PROVISIONAL
  experimental values for lab exploration, not product recommendations.
- Headset acceptance is still required before any behavioural conclusion;
  the Lab has no product semantics to accept — it is a measurement rig.
- Known runtime-only checks (not unit-covered): the cross-thread packet
  generation/own-solve fail-open under a real mid-frame settings toggle
  (code path mirrors the VS packet; exercised by inspection + headset),
  and the Lab page's rendering (menu builds; visual check in headset).
