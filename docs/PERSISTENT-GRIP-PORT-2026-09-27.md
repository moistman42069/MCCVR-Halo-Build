# Persistent Grip port (foundation tranche T4 + core wiring tranches T5a/T5b/T6a/T6b/T6c)

## September 28 candidate refinement

The current candidate changes only the default for **new configurations**:
`persistent_support_grip` is now on by default. There is no migration or
version-based rewrite; a saved explicit `persistent_support_grip = 0` or `= 1`
continues to be parsed and saved as the user's choice. The durable relationship,
title applicability, ownership proof, support geometry and every other PG rule
are unchanged by this default flip. This remains a candidate awaiting headset
acceptance; the default-off statements below describe the original port state.

The same candidate makes one narrow lifecycle refinement around the adjacent
product continuity feature: when a proven PG owner break releases an already
presented VS-OFF latch and the configured 200 ms transition is enabled, that
product release bridge is preserved to raw one-hand aim. Lab damping and the
controller-input filter are invalidated. Title/generation identity loss never
preserves continuity; other owner breaks retain the existing invalidation path.
This is not a change to PG ownership, acquisition or support-geometry policy.

## September 29 product-surface update

The free-two-hand surface as a whole is now documented in
`docs/FREE-TWO-HAND-PRODUCT-SURFACE-2026-09-29.md`. The points that touch this
port's UI/help text:

- The F1 checkbox is `Persistent support grip` (no "(experimental)" suffix).
  New configurations default it ON as recorded above; its help text keeps
  describing the durable hold and the `Switched weapons already two-handed`
  sub-option.
- `Reduce Support-Hand Rotation` is now explicitly **Virtual Stock only** in its
  help text: it selects the VS support endpoint (and the Two-Handed Lab
  Production anchor pass-through) and is provenance only. Free two-handed aim
  always uses the fixed primary-Grip -> support-Grip positional pair, so this
  checkbox is not a free-two-hand anchor selector. The neighbouring VS-OFF
  product controls are `Offhand influence` (0-100%, default 50%) and
  `Two-Hand Smoothing` (0..25, default 0).
- The retired `two_hand_transition_smoothing` toggle is gone from the product:
  the 200 ms VS-OFF acquire/release continuity is fixed-on internal behaviour
  with no user control.

Known follow-ups (unchanged by the productisation): two-handed projectile
landing/aim accuracy across Halo 3, ODST, Reach and Halo 4 still needs
investigation - the cause is not solved - and per-weapon/weapon-group grip
positions are not implemented yet.

Status: **IN PROGRESS - T4 foundation, T5a durable writer, T5b invocation
trust + first title slice (Halo 3), T6a transitional applicability gate +
Halo CE and Halo 2 producers/trust/fallbacks, T6b ODST and Reach slices +
the Reach presented-reticle consumer (F06/F07/F14 consumer side), T6c Halo 4
slice (F03/F04) completing all-six-title coverage, T7 corrections tranche
(T7-VER-01 F1-F5) landed: the A003 acquisition predicate is now gated, the
Reach presented-ray serial bound is honest, the aim-continuity guard message,
the evidence-publication comment and the config round-trip provenance are
fixed, and the new `halomccvr_persistent_support_grip_guard` source guard
pins the wiring.**
T4 added pure policy modules, their offline test targets and the config/menu
toggle. T5a wired the durable relationship into the shared input/aim layer
(`vr.cpp`/`vr.h`): publication shapes, the single-writer relationship step, the
pending acquisition intent, the handedness route and the N1/N2 invalidation.
T5b added the per-invocation owner-trust gate, the solve-time support receipt,
the presented-ray receipt and the first end-to-end title slice (Halo 3 producer
-> trust -> presentation/cache). T6a added the transitional per-title
applicability gate and wired Halo CE and Halo 2 (evidence producers,
current-invocation trust, one-hand fallbacks and presentation/detach seams).
T6b wired ODST and Reach (own readers/timing, per-pair frozen trust, stale-owner
one-hand fallback, presentation) and landed the Reach presented-reticle
consumer: the title-native invocation receipt, the two-half ray qualification
(F06), the smoothing discontinuity it drives (F14/section 10) and the closed
`SupportRayConsumable` epoch-0 rule. T6c wired Halo 4: the
production owner-evidence producer with the raw primary-absence path (F03), the
storm-hands decision-boundary trust freeze (F04: semantic-primary read, one
frozen decision per pair, held-record validation only), the owner-safe prepared
one-hand fallback and the presentation. Every gameplay title is now in
`PersistentSupportGripApplies`. The T7 corrections tranche (section 3F) closes
the T7-VER-01 findings against the landed tree without widening the feature:
PG ON now runs the archive's A003 acquisition predicate at its latch while
PG OFF keeps the base geometry verbatim, and a new source-consistency guard
(`tools/check_persistent_support_grip.py`, CTest
`halomccvr_persistent_support_grip_guard`) pins the config, gate, base-store,
reseed/owner-break and anti-donor invariants.
`Game_ComputeAimStick` remains a later tranche. The feature stays OFF by
default and **no behavioural claim here is a headset result**. See sections
3A-3E and 3F.

Base tree: PR16 (Virtual Stock aim continuity + retired head-turn-sway
correction) plus the uncommitted Two-Handed Lab patch
(`docs/TWO-HAND-LAB-2026-09-27.md`), both untouched except for the single
`src/dll/menu.cpp` checkbox insertion described below.

## 1. Mission contract

Port the persistent support-grip feature into this build as **one F1 toggle**
("Persistent support grip (experimental)", config key
`persistent_support_grip`, default **off**). When it is on, the two-hand support
grip becomes a durable, owner-bound relationship: acquisition stays spatial
(the existing grab zone), but once acquired the relationship is retained
logically until the grip is released or the weapon/title lifecycle proves the
owner changed, instead of following the physical support hand's continued
proximity. It must work with Virtual Stock on **and** off, must never migrate a
relationship from weapon A to weapon B while Grip is held, and must leave every
existing PR16 option (Virtual Stock Standard/Plus, the 0.35 acceptance floor,
the continuity layer, the Two-Handed Lab) behaviourally unchanged when the
toggle is off. The retired head-turn sway correction is never reintroduced as
part of this work.

## 2. Architecture summary

One durable relationship plus title-local current-invocation trust. The
policy is shared; the native evidence producers stay per-title.

```text
DURABLE RELATIONSHIP (single writer, epoch-disciplined)
    title + title generation + controlled unit + full weapon handle
    engaged / requiresRelease / epoch
    unknown evidence never durably releases; proven absence or a proven
    different owner invalidates and arms requiresRelease FIRST

PER-TITLE OWNER EVIDENCE (tri-state)
    KnownPresent(owner) / KnownAbsent / Unknown
    monotonic publication revision per publication
    KnownAbsent must not require the FP producer-agreement bit

PENDING ACQUISITION INTENT (not a second latch)
    records title/generation + spatial admission + evidence revision floor
    toggle intent survives the physical button-up until proof or cancel
    hold intent lives only while Grip is held
    cancelled by explicit second toggle/cancel and by lifecycle/title/
    generation/role/secondary invalidation

TITLE-LOCAL INVOCATION TRUST (read-only, never mutates the relationship)
    TrustedSameOwner / Untrusted
    mismatch, instability, producer disagreement, invocation Unknown => Untrusted
    untrusted => the existing ordinary one-hand/native fallback

PRESENTATION distinct from aim authority
    relationship valid AND owner trusted for this invocation => the visible
    support hand may stay attached even when support aim authority is off

RETICLE RECEIPTS
    presented ray carries epoch + trusted-owner proof + prepared serial
    acceptance requires producer receipt AND consuming-invocation
    compatibility AND a coherent relationship reading (epoch 0 alone is
    never permission); smoothing reseeds across an owner discontinuity

PG GATE
    off (default) = no consumer, base behaviour; the feature is its own
    transaction and degrades to stock only for itself, loudly
```

## 3. T4 deliverables (this tranche)

New files (all pure: `<cstdint>`/`<cmath>` only, `noexcept`, allocation-free,
no engine state, no I/O, no OpenXR types):

- `src/common/support_grip_logic.h` - durable relationship/writer policy,
  tri-state evidence, invocation trust, carrier detach matrices, presented-ray
  qualification, reticle continuity, plus the T4 repairs A1-A4.
- `src/common/support_grab_logic.h` - acquisition-zone predicate, ported
  as-is; the support wrist orientation is deliberately not an input.
- `src/common/halo4_owner_evidence.h` - H4 on-foot owner-evidence stage
  decision and per-pair finalization (A003 semantics), ported as-is.

New tests (standalone targets; `tests/core_tests.cpp` and
`tests/virtual_stock_tests.cpp` untouched):

- `tests/support_grip_logic_tests.cpp` - 141 checks: owner identity incl.
  title, durable step/writer matrices, `requiresRelease` ordering, invocation
  trust, carrier detach, ray consumption, stable evidence, CE mapping, pending
  intent + revision floor, epoch-0 coherence, serial/reticle continuity.
- `tests/support_grab_tests.cpp` - 69 checks (donor port).
- `tests/halo4_owner_evidence_tests.cpp` - 88 checks (donor port).

CMake targets: `halomccvr_support_grip_logic_tests`,
`halomccvr_support_grab_tests`, `halomccvr_halo4_owner_evidence_tests`.

Config/menu surface (inert):

- `Config::persistent_support_grip` (default `false`), parsed from
  `persistent_support_grip`, emitted by the writer with a generated comment,
  and covered by `Config{}`'s default member initializer (the
  `g_config = Config{}` reset path). Config version unchanged.
- One F1 checkbox in the Weapon & Aim page beside "Reduce Support-Hand
  Rotation"; the existing checkbox, its copy and the Virtual Stock menus are
  untouched.

### Repair notes carried by the port

- **A1 / F01 - title in the owner identity.** `OwnerTuple` now carries
  `GameTitle` (from `src/common/runtime_types.h`) as its first member, and
  `OwnerComplete()`/`SameOwner()` include it. A test proves
  `Halo3/gen1/unitX/weaponY` and `Reach/gen1/unitX/weaponY` are different
  owners and that a foreign-title invocation is never trusted.
- **A2 / F08 - evidence freshness.** `EvidenceRevisionFresh(publication,
  floor)` requires a strictly newer publication; `PendingAcquisitionIntent`
  records the floor observed when the intent began, and
  `PendingAcquisitionBindable()`/`PendingAcquisitionRequest()` require a
  compatible `KnownPresent` published strictly after that floor. Tests: a
  cached `KnownPresent(A)` at the floor never creates a relationship; the
  newer publication binds; absent/unknown publications never satisfy the
  intent.
- **A3 / F12 - pending acquisition intent.** A pure
  `PendingAcquisitionStep()` state machine: toggle intents survive the
  physical button-up, hold intents live only while Grip is held, and both are
  cancelled by an explicit second toggle/cancel, a title or generation
  replacement, a lifecycle/role/secondary invalidation, or an already-engaged
  relationship. Tests cover a click released before owner proof still binding
  later, and a hold cancelled on release.
- **A4 / F14 - epoch 0 is not automatically acceptable.**
  `PresentedRayOwnerCompatible()` gained a
  `relationshipDisengagedCoherent` parameter. An epoch-0 ray is accepted only
  when the caller asserts the relationship was coherently readable and
  disengaged when the pose was solved; the latch-engaged/publication-old race
  and an unavailable snapshot are both rejected, as is an epoch-0 ray while
  the relationship reads engaged.
- **Residual at T4, since closed:** `SupportRayConsumable()` originally kept
  its donor epoch-0 short-circuit ("an ordinary one-hand ray is always
  consumable"), because applying the same coherence requirement there needs the
  producing invocation, which only exists once the consumer wiring lands. T6b
  (section 3D) closed it: the epoch-0 branch now takes the same
  `relationshipDisengagedCoherent` assertion as `PresentedRayOwnerCompatible`.

## 3A. T5 deliverable: core lifecycle/ownership wiring (this tranche)

Everything below is wired but **unreachable in behaviour** until a title hook
publishes owner evidence (T6): a durable relationship can only ever bind from a
complete, current-generation `KnownPresent` published for the active title.

Publication shapes and APIs (`vr.h`, implemented in `vr.cpp`):

- `SupportGripOwnerEvidencePublication` - one record per title:
  `{title, generation, owner{title,generation,unit,weapon}, evidence,
  revision}`. `VR_PublishSupportGripOwnerEvidence(title, generation, owner,
  evidence)` is called by title producers only.
- **F13 repair:** one storage slot per title (`TitleRuntimeSlotIndex`), a real
  writer acquisition (`std::atomic<bool> busy` try-begin CAS) inside the slot,
  an odd/even sequence for readers, and a strictly monotonic per-slot
  `revision`. A losing concurrent producer drops its publication (the next
  producer update republishes) instead of tearing a record, a foreign/retiring
  title can only ever write its own slot, and a reader that sees a title or
  generation other than the one it asked for reports unavailable.
- `VR_ReadSupportGripOwnerEvidence(title, expectedGeneration, owner, evidence,
  revision)` - coherent read. False (=> the caller treats Unknown) for a
  never-published slot, a torn read, a zero expected generation, or another
  lifecycle's record. A `KnownPresent` claim whose tuple is incomplete or names
  another title is reported as **Unknown**, never as a proven different owner.
- `SupportGripRelationshipSnapshot {title, generation, unit, weapon, epoch,
  engaged}` + `VR_GetSupportGripRelationship(snapshot)` - coherent read of the
  durable relationship; **false is "unavailable", not "disengaged"** (F15
  shape: consumers must fail closed). A coherent disengaged reading carries
  None/0/0xFFFFFFFF and the current epoch.

Durable writer (`UpdateTwoHandLatch` -> `UpdatePersistentSupportGrip`, one call
per captured frame on the input thread, single writer):

- **PG OFF is the parity contract:** with `persistent_support_grip` false the
  original gate/stores/toggle/hold code runs unchanged (the acquisition-zone
  geometry was only extracted verbatim into `TwoHandGrabZoneHit`, which is now
  the base helper only). The only added call retires dormant durable state.
  PG ON uses the archive's A003 acquisition predicate
  (`PersistentSupportGrabZoneHit` -> `support_grab::EvaluateSupportGrabZone`)
  in the writer instead: see the T7 corrections below.
- **F02:** the active title, its generation and `TitleAdapter_GetRuntimeMode()`
  are read per update. Title change, an unavailable/unclassified title slot, a
  generation that becomes unavailable (0) or is replaced, and Loading / Dead /
  Shell terminate an engaged relationship; `Paused` is retained and
  `RuntimeMode::Unsupported` is deliberately treated as mode ambiguity (like
  Unknown evidence) rather than as proven absence. `DurableLifecycleIdentifiesOwner`
  / `DurableRelationshipIdentityLost` carry this policy purely.
- **Ordering:** the teardown reasons arm `SupportGripAdmission::Arm()` *before*
  `DurableWriterStep`/`DurableWriterRelease` clears anything, so a held Grip can
  never inherit the next owner across a lifecycle/role/gesture teardown
  (F02/F11/F16). `DurableWriterStep` still runs owner-break before acquisition.
  Amended by T18 (section 3I): a plain proven owner replacement no longer arms
  release-before-reacquire, so the switched weapon is re-acquirable while the
  Grip stays held, and the `two_hand_switch_inherit` option can rebind it in
  place.
- **Tracking outage (F11-analog):** `!rightValid || !leftValid` takes no step:
  the last owner is retained/suspended, nothing binds, nothing is erased, and
  no arming is added. The first recovered update still applies the owner-break
  rule.
- **F11:** `weaponGesture` arms release-before-reacquire and releases through
  the writer (an interaction gesture can coincide with a weapon replacement).
- **F12:** the acquisition request is a pending intent, not the latch.
  Toggle mode begins it on the rising in-zone edge with the observed evidence
  revision as its floor; it survives the button-up; a second toggle press, a
  title/generation/lifecycle/role/secondary/gesture invalidation, or an engaged
  relationship cancels it. Hold mode's intent lives while Grip is held. A pose
  outage neither cancels nor advances it.
- **F08 fail-closed floor:** an intent may begin only from a *coherent*
  evidence read. A click whose revision cannot be proven is dropped (once per
  episode, logged) instead of admitting a stale cached owner; the next click
  works. The bind itself still requires `KnownPresent` with
  `revision > floor`, a complete owner, the active title and the active
  generation.
- **F16:** the handedness swap no longer stores `g_twoHandLatched = false` out
  of band while PG is on; the same edge reaches the writer as `rolesChanged`, so
  owner, epoch and publication move together. PG off keeps the original direct
  clear.
- **N1/N2:** on an owner break (`Invalidated`) or a lifecycle/generation
  replacement, `InvalidateAimContinuityLayer()` and
  `InvalidateTwoHandLabTemporal()` also run, so a proven owner discontinuity
  cannot ease or re-present A-derived orientation into the first B invocation.
  An ordinary release (hold release, toggle off, config off, gate release)
  deliberately leaves both layers' existing release-edge behaviour alone.
- `g_twoHandLatched` is written as `relationship.engaged`, so every existing
  reader (aim solver gate, aim-continuity edge source, Lab latch-edge source,
  `pad.supportAimActive`) keeps working; `g_twoHandActive` keeps its aim-authority
  meaning. The relationship snapshot is published **before** the latch moves, so
  the confirmed F14 bind race (latch engaged while the publication still reads
  disengaged) cannot recur; the residual release-edge window is carried by the
  F14/T5b consumer rule (`PresentedRayOwnerCompatible` coherence flag).
- Logs (transitions only, never per frame): bind (epoch/title/generation/
  unit/weapon/revision), release/invalidate (reason + old owner, plus the new
  owner or proven absence), armed-for-release block, lifecycle replacement,
  acquisition dropped for lack of a coherent revision, feature disable.

Producer obligation carried into T6: a title producer must publish on every
relevant invocation (not only on a weapon change), or a pending intent's
revision floor can never be satisfied.

Not written by this tranche (T5b/T6/T7): every per-title producer and consumer,
the invocation-trust seams, the Reach presented-reticle consumer and smoothing
reseed, the H4 storm-hands ordering, `Game_ComputeAimStick` / H2 native aim
qualification, and the remaining F14 residual in `SupportRayConsumable`.

Coverage gap carried into T7: the slot/relationship publication protocol
itself (CAS writer acquisition, odd/even reader rejection, per-title slot
isolation, revision monotonicity) is single-writer engine state inside
`vr.cpp` and has no offline test in this tranche. An extract-a-fixture seam
test (the pattern already used for the aim/grip/d-pad fixtures) is the smallest
way to close it and is listed as T7 work.

## 3B. T5b deliverable: invocation trust + Halo 3 vertical slice (this tranche)

Scope: the per-invocation owner-trust mechanism, the presented-ray receipt and
the first complete title slice (Halo 3 producer -> trust -> presentation).
Reach's presented-ray consumer, the H2/CE/ODST/H4 producers and
`Game_ComputeAimStick` stay T6. **PG stays default off and PG-off parity stays
a hard contract**; no headset result is claimed here.

### Invocation-trust mechanism (`vr.h`/`vr.cpp`/`virtual_stock_aim.inl`)

- `VR_SetSupportInvocationUntrusted(bool)` / `VR_SupportInvocationUntrusted()`
  own one thread-local gate. It is fail-open (a stray set can only force that
  invocation to the ordinary one-hand path), it is never consulted with the
  feature off, and it never mutates the durable relationship. The setter is
  only ever called by a title that proved the currently executing invocation
  is not the relationship owner (Halo 3: `ControllerWorldPoseEx`), and it is
  cleared immediately after the guarded call.
- Solve-time receipt: `AimPoseInputs`/`AimPoseResult` carry plain-scalar
  receipt fields (`supportMayConsume`, `supportEpoch`,
  `supportRelationshipReadable`, `supportRelationshipEngaged`,
  `supportSolveSerial`) - plain scalars so the generated aim fixture keeps
  compiling the shipping solver from source. `supportTrusted` is narrowed
  inside the solver: it is claimed only when the accepted two-hand support
  geometry actually steered the produced pose AND the assembly qualified the
  invocation.
- `VR_GetAimPoseWithSupportProvenance(q, p, receipt)` is the extended getter.
  `VR_GetAimPose` keeps its exact signature, outputs and return contract and is
  now a thin wrapper, so every existing caller is unchanged.
- Assembly gate: `CurrentStockAimPoseInputs` calls the new pure
  `support_grip::QualifySolveSupport` before any support-capable raw aim is
  selected. With the feature off the qualification is the all-zero default and
  no durable state is read at all (PG-off parity). An unreadable relationship
  or a denied invocation forces `twoHandEnabled=false` for that assembly only.
- F14 coherence: the qualification resolves the durable snapshot once per
  assembly. A coherent DISENGAGED read is the only source of receipt epoch 0;
  an engaged relationship carries its epoch whether the invocation is trusted
  or denied; an unreadable relationship carries `kUnknownRelationshipEpoch`
  (all ones), so no consumer can mistake it for a coherent disengagement.

### Lab interaction (A3)

The Two-Handed Lab's temporal edges must never be fabricated by a
per-invocation gate. Implementation: the gate forces **only**
`twoHandEnabled`; `twoHandLatched` continues to be `g_twoHandLatched` - the
durable engaged bit written by the single writer in `vr.cpp` - exactly as
before. **No Lab file was changed.** The Lab's `Continuity200ms` edge source
(`labInputs.twoHandLatched` in `two_hand_lab_temporal_runtime.inl`), its
one-hand counterfactual and the latched
`g_fpStereoSolveScope.twoHandAimActive` are therefore unaffected by the gate.
This is a deliberate difference from the archived donor, which gated
`twoHandLatched && !untrusted` in the shared constructor and could have
fabricated a Lab release edge on a gated invocation.

### Presented-ray receipt (`vr.cpp`)

- `ReticleAimPosePublication` now carries `supportEpoch`, `supportTrusted` and
  `preparedSerial`, filled from the SOLVE-TIME receipt returned by
  `VR_GetAimPoseWithSupportProvenance` for that presented serial. The producer
  never re-reads live durable evidence (F07 producer repair).
- Smoothing keeps its discontinuity semantics:
  `ReticleSmoothingDiscontinuity(ownerSafe, epoch, lastEpoch)` with
  `ownerSafe = !feature || !engaged || trusted`. A denied invocation reseeds,
  an epoch change reseeds, and a coherently disengaged PG-on solve keeps the
  ordinary smoothing. With the feature off the receipt is all-zero/false, so
  the expression is the base expression (byte-identical smoothing).
- `VR_GetPresentedReticleAimPoseWithSupportProvenance` is the new reader; the
  existing `VR_GetPresentedReticleAimPose` is unchanged and delegates. No
  existing consumer changes; the Reach consumer and its first-B receipt
  compatibility check are T6b.

### Halo 3 vertical slice (producer + trust + presentation)

- **Producer.** `Game_ReadPrimaryWeaponEvidence` (`game.cpp`/`game.h`) reads
  the same guarded native state as `DiagnosticH3Primary`, through a shared
  `H3PrimaryWeaponEvidence` body (the diagnostic entry point is now a thin
  wrapper, so the read logic exists once). It is wrapped in the same SEH guard
  as the diagnostic path; a fault is Unknown, never absence. Halo 3 is the
  only title wired; every other title reports Unknown until its own T6
  producer lands. `Halo3ReadOwnedWeapons` gained an optional
  `primaryAbsentOut` filled from the native role byte 0xFF BEFORE the
  slot-validation guards, so an explicit empty primary slot resolves to
  `KnownAbsent` (F03/F17 source half). `Game_ResolveSupportInvocation`
  publishes every resolved invocation's evidence (including Unknown, so no
  stale `KnownPresent` survives) and uses the T4 `ResolveStableEvidence`,
  which requires the FP producer-agreement bit only for a KnownPresent claim
  (F03 repair: absent-without-agreement resolves as KnownAbsent).
- **Trust ordering (A015 section 16).** The before-read is taken before the
  native original; the after-read happens IMMEDIATELY after the native
  original interpolation and BEFORE MCCVR's context capture / marker
  transforms. The decision is frozen for the stereo pair in
  `g_fpStereoSolveScope` (`supportGripAttached`/`supportGripResolved`). The
  first slot-0 invocation of the armed pair resolves it; both eyes then reuse
  the same value. (Deliberate difference from the archived donor's
  `view == 0` requirement: with `right_eye_first` the engine interpolates
  view 1 first, and the donor rule would have left that eye on the old
  presentation path for the transition pair while the other eye used the
  frozen value.)
- **Aim fallback.** `ControllerWorldPoseEx` sets the thread-local gate around
  the weapon-hand `VR_GetAimPose` call when the pair's frozen trust is false
  (including a pair that never resolved), so that invocation resolves to the
  ordinary calibrated one-hand path. The durable relationship is never
  mutated.
- **Cache discipline (C3).** `FpStereoPaletteCache` now keys the frozen
  `supportGripAttached` in addition to `armIk`, and `armIkActive` is derived
  from the support attachment (`ShouldApplyArmIk(arm_ik,
  supportGripAttached)`), not from the aim-authority flag. A trust/owner
  change can therefore never reuse the other eye's solved geometry. Slot 1
  (dual-wield secondary), the explicit-target Reach path, ODST (which never
  resolves the H3 decision) and PG-off keep the existing `twoHandAimActive`
  semantics exactly.
- **Presentation (C4).** For the primary weapon the visible support
  attachment follows relationship trust for the invocation: a trusted
  same-owner pair keeps the support hand attached even when aim authority is
  false (no detach), and an untrusted/mismatched pair never inherits support
  presentation.
- **F17 (H3 residual, not closed).** The KnownAbsent producer is implemented
  and source-provable, but its liveness is NOT proven: absence is only
  observed while the engine still calls the FP interpolation hook with an
  empty primary slot. Whether the unarmed presentation calls that hook is not
  established by source here; the transition can therefore remain Unknown
  until the headset/instrumentation tranche answers it. This document does not
  claim the weapon->unarmed transition is closed.

### Not written by this tranche

- The Reach presented-ray consumer and smoothing reseed wiring (T6b): the
  publication now carries the receipt, nothing consumes it yet.
- The H3 FP-invocation trust does not reach the compositor's reticle solve.
  The per-pair decision is thread-local to the game render thread (that is
  where the FP hook and the palette run), while the presented ray is solved on
  the OpenXR frame thread, so the ray's producer receipt carries the solve-time
  relationship qualification (engaged + coherent + not denied) rather than the
  FP invocation's frozen trust. Closing that needs a serial-scoped publication
  of the frozen per-pair trust (T6/T7 seam); the consumer-side
  current-invocation compatibility (F06) is the other half and is what makes
  the presented ray safe for a weapon-sensitive consumer.
- `SupportRayConsumable`'s epoch-0 coherence qualification (F14 residual) -
  since closed by T6b (section 3D).
- `Game_ComputeAimStick` owner qualification (F10): the live servo path still
  consumes `VR_GetAimPose` outside any title gate.
- CE/H2/ODST/Reach/H4 producers and consumers, H4 storm-hands ordering (F04),
  the H2 native aim updater (F05), H2 absence liveness (F09), H2 snapshot
  semantics (F15).

(CE and H2 are no longer in that list: see section 3C.)

## 3C. T6a deliverable: transitional applicability gate + CE and Halo 2 slices

Scope: the per-title applicability gate, the Halo CE producer/trust/fallback
slice and the Halo 2 producer/trust/fallback/presentation slice. ODST, Reach
and Halo 4 stay unwired **by construction**. PG stays default off and PG-off
parity stays a hard contract; no headset result is claimed here.

### A0 transitional applicability gate (required safety)

The port is being landed one title at a time, so a title that has no producer,
no trust seam or no fallback must never be exposed to a half-wired
relationship. One pure predicate carries the rollout set:

```text
support_grip::PersistentSupportGripApplies(GameTitle)
    wired after T6a : Halo3, HaloCE, Halo2
    pending         : Halo3ODST, HaloReach, Halo4 (later tranches)
```

`VR_SupportGripWiredForTitle(title)` (= `persistent_support_grip && applies`)
is the single runtime gate every new path consults instead of the config alone.
When it is false - feature off **or** an unwired title:

- the durable writer does not own the frame: `UpdateTwoHandLatch` retires any
  dormant durable state on the transition (releasing an engaged relationship
  and republishing a coherent disengaged snapshot, so a later wired title
  cannot inherit it) and then runs the **exact base latch path** - the same
  gate/stores/toggle/hold code as PG off, with no durable-state read for any
  latch decision and no PG publication;
- the assembly qualification path (`ResolveAimSupportQualification`) returns
  the all-zero default before any durable read, so the aim assembly is the base
  assembly and the solve receipt stays zero (the reticle-smoothing expression
  is therefore the base expression);
- the F16 handedness teardown keeps its original direct latch clear instead of
  routing through the writer;
- every title-side consumer (CE's decision helper, H2's packet/live helpers and
  the H2 poll producer) short-circuits to the base decision without reading any
  durable state.

This is a deliberate rollout and failure-isolation gate, not a permanent
feature limit: each later tranche adds its title to the predicate once its
producer, trust seam and fallback are all present.

### Halo CE (wired)

- **Producer (B1).** The smallest raw-slot-preserving seam was extending
  `HaloCELocalPlayerState`: `hasFirstPersonUserRecord` is set only when the
  native FP user record was read at all, and `weaponSlotPresent` records the
  **raw** slot before object/owner validation. The tri-state mapping is the T4
  helper `support_grip::CeOwnerEvidence`: a validated local owner is
  KnownPresent, a valid user record with an empty raw slot is KnownAbsent, and
  everything else (unreadable record, non-null candidate that failed
  validation/ownership) is Unknown - never absence. `PrepareHook` publishes for
  the **outermost** prepare only (a nested prepare must not publish Unknown
  over the frame's proven record) via `VR_PublishSupportGripOwnerEvidence` from
  the CE first-person context, before the render context is read and even when
  that read fails, so an explicit absence still reaches the durable writer.
  There is no diagnostic-session dependency.
- **Trust seam (B2).** The decision is frozen per invocation in `PrepareHook`
  from the copied context (`CeEvaluateSupportInvocation`), which is also what
  the native palette invocation consumes: `RunPrepare` calls the native
  original synchronously while `scope` holds that frozen context and
  `PaletteHook` applies that same scope.
- **Consumers.** `ControllerShotDirection` is the single funnel for the modern
  ray, the legacy ray, the assist ray and the continuous target query. It
  consumes the frozen `primaryAim` only while this invocation proves the
  relationship owner **and** the frozen pose is actually support-derived; on a
  mismatch, Unknown/Absent evidence, an unreadable snapshot or a released
  relationship whose frozen pose is still support-derived it substitutes the
  existing `ControllerRig.independentPrimaryAim` (A015 section 4.1; no new
  solve) and a missing independent pose returns to stock. The unit-control and
  vehicle packet builders apply the same policy and additionally require
  two-read continuity: an owner or relationship-epoch change between the two
  owner reads, a release between them, or a failed read retains the stock
  native packet.
- **Presentation.** `ControllerRig.supportGripAttached` (seeded from
  `twoHandAimActive` at publish time, i.e. base parity) is frozen per
  invocation in `PrepareHook`: a trusted same-owner invocation keeps the
  visible support hand attached even when support aim authority is off, and an
  untrusted/detached invocation never inherits it. `primaryAimSupportDerived`
  records the frozen pose's provenance (derived only from the solve result,
  cleared when the carrier is substituted). The palette and arm-IK consumers
  now read `supportGripAttached`; with the feature off it is exactly
  `twoHandAimActive`, so the base palette is unchanged.

### Halo 2 (wired)

- **Producer (C1).** `Halo2DiagnosticReadPrimaryWeapon` gained the optional
  `primaryAbsentOut`, set from the **raw** native primary slot before the
  ownership validation, so an explicit empty slot is KnownAbsent while a
  non-null candidate that fails validation stays Unknown. `Game_ReadPrimaryWeaponEvidence`
  now handles Halo 2 through that guarded reader, and the shared
  `Game_PublishSupportGripOwnerEvidence(title)` publishes the tri-state record
  (including an explicit Unknown).
- **Absence liveness (F09).** The publication site is the level-live observer
  poll (`Halo2Observer6Dof_Poll`), not the packet builder: this poll runs on
  the title worker once per frame for as long as the level is live, with no
  dependency on a first-person weapon packet being admitted, so a weapon ->
  unarmed transition can no longer leave a stale `KnownPresent` cached. It
  reads the same guarded direct reader (A016: H2's reader is safe outside the
  TLS-restricted titles).
  **Deliberate difference from the archived donor:** the donor also published
  from the packet-build render hook. This port does not, because the landed
  writer-side slot protocol is documented as never being called from a render
  or palette hook; the poll seam covers the same frames plus the unarmed ones,
  and the packet transaction stays read-only (each publication carries a
  strictly newer revision, which is what the pending-intent floor consumes).
- **Trust seam (C2/F05).** The packet builder decides per invocation with
  `Halo2EvaluateSupportUse(publication, unitObject, weaponObject)`: the
  snapshot's frozen `twoHandAimActive` flag and the packet's own owner identity
  are compared with the durable relationship, and the shared
  `SupportInvocationMustDetach` matrix produces the detach decision.
  `BuildPublishedControllerAimDirection` - the seam that feeds the always-on
  `Halo2NativeAimUpdateDetour` and the aim-assist controller direction - and
  the scope camera now select the **existing** `independentRightAimOrientation`
  carrier whenever current H2 owner compatibility is not proven, including a
  failed relationship read and a release discontinuity. No new solver, no
  weapon prediction, and aim assist is not made a lifecycle oracle.
- **Fail closed (C3/F15).** `Halo2SupportUse` distinguishes coherently
  disengaged / coherently engaged / snapshot unavailable. Unavailable detaches
  (never "no relationship, use legacy two-hand"); a readable disengaged
  relationship detaches only when the frozen publication still carries
  support-derived geometry (the release discontinuity).
- **Presentation (C4).** `Halo2VisibleConsumerContext::supportGripAttached`
  carries the relationship-based presentation flag to the two first-person
  palette call sites, while `twoHandAimActive` keeps its aim-authority meaning
  (including for the contact-frame epoch). The dual-wield secondary override
  and every non-engaged case keep the existing semantics. No dormant/rejected
  controller-ownership or presented-reticle experiment is revived.

### T6a completion note

Halo CE and Halo 2 are wired end to end (producer -> current-invocation trust ->
one-hand fallback -> presentation/detach), the transitional applicability gate
`PersistentSupportGripApplies` ships with `{Halo3, HaloCE, Halo2}`, and the CE
donor test additions were ported into
`tests/haloce_first_person_runtime_tests.cpp`,
`tests/haloce_first_person_tests.cpp` and
`tests/haloce_unit_control_runtime_tests.cpp` (both CE fixtures are fail-open
stubs that include the shipping `.cpp`, so the ported matrices exercise the real
policy). The H2 consumer half was reconciled with the shared T4 matrices while
porting those tests, exactly as section 3C describes: a readable *disengaged*
relationship detaches only when the frozen publication still carries
support-derived geometry (`publicationSupportDerived`), and an *unavailable*
relationship read always detaches; the donor's H2 shape (which used only
`SupportCarrierMustDetachForInvocation`) was deliberately tightened to the unit
tested `SupportInvocationMustDetach` pair. The H2 defects and their evidence are
recorded in section 3C and the ledger rows; anything the tests prove is stated
there, not inferred here. `cmake --build --preset release` was green and
`ctest --preset release` passed **98/98** at the tranche boundary;
`halomccvr_support_grip_logic_tests` reported **212 checks / 0 failures**.
ODST, Reach and Halo 4 stayed unwired by construction. The final doc edits of
T6a were re-verified with the same build and suite at the start of T6b (Step 0,
section 7C).

### Verification for T6a

- `cmake --build --preset release`: full Release build green (`HaloMCCVR.dll`
  and every test executable link, 0 errors, 0 new warnings).
- `ctest --preset release`: **98/98** tests passed, including both guard suites
  (`halomccvr_aim_continuity_lifecycle_guard`,
  `halomccvr_two_hand_lab_patch_guard`) and the regenerated fixtures.
- `halomccvr_support_grip_logic_tests`: **212 checks / 0 failures** (was 204).
  New coverage: the applicability predicate (Halo3/HaloCE/Halo2 wired;
  ODST/Reach/H4/None/Unknown not).
- CE fixtures: `halomccvr_ce_first_person_runtime_tests` and
  `halomccvr_ce_unit_control_runtime_tests` (both fail-open/stub fixtures that
  include the shipping `.cpp`) exercise the ported matrices - unwired title
  stays on the base carrier, proven same owner keeps the support carrier,
  owner mismatch / Unknown / KnownAbsent / unreadable snapshot / release
  discontinuity detach to the independent one-hand carrier, an owner or epoch
  change (or a release) between the two reads retains the stock native packet,
  and the prepare producer publishes KnownPresent / Unknown / KnownAbsent /
  no-record correctly. `halomccvr_ce_first_person_tests` covers the palette's
  new presentation input including the same-owner-with-aim-authority-off case.
- **PG OFF parity, source-level audit.** Every new runtime path is behind
  `VR_SupportGripWiredForTitle`, which is false whenever
  `persistent_support_grip` is off: the qualification short-circuits before
  any durable read, the writer runs the base latch path (the only added call
  being the pre-existing dormant-state retire), the CE prepare publishes
  nothing and leaves the copied context exactly as published (where
  `supportGripAttached == twoHandAimActive` and
  `primaryAimSupportDerived == AimSupportDerived(solve)`), the CE consumer
  helpers return the constant base decision without reading durable state, and
  the H2 helpers return the base decisions (`detach=false`, presentation from
  `publication.snapshot.twoHandAimActive`). The palette/aim/contact consumers
  therefore see the base values.
- **Still runtime-only (no offline seam test in this tranche):** the CE
  `PrepareHook` publish/trust ordering inside the real native prepare, the CE
  two-read continuity against real engine reads, the H2 packet-builder and
  native-aim/aim-assist carriers against a live publication, the H2 poll
  producer's cadence, and every headset behaviour. No test target includes
  `halo2_observer_6dof.cpp` (the H2 policy it calls - `SupportInvocationMustDetach`,
  `SupportCarrierMustDetachForInvocation` - is the unit-tested T4 pair), so H2's
  wiring is a documented runtime-only claim until T7 and the headset gate.
- **Deliberately unchanged (recorded, not hidden):** the CE contact-frame
  settings hash keeps hashing `twoHandAimActive` (the donor did the same); a
  relationship-derived presentation change is not a new contact-settings
  input. Whether that matters for contact melee is a T7/headset question.

## 3D. T6b deliverable: ODST and Reach slices + the Reach presented-ray consumer

Scope: the ODST producer/trust/presentation slice, the Reach
producer/trust/presentation slice (including the stale-owner one-hand fallback)
and the Reach presented-reticle consumer qualification (F06) with its smoothing
discontinuity (F14/§10). Halo 4 stays unwired. PG stays default off and PG-off
parity stays a hard contract; no headset result is claimed here.

### ODST (wired)

- **Producer (A1).** ODST keeps its own reader and timing: `OdstReadMuzzleWeapons`
  gained the optional `primaryAbsentOut` filled from the raw primary role byte
  `unit+0x276` **before** the secondary-role/shared-handle validation guards, and
  the new `OdstPrimaryWeaponEvidence` body (the diagnostic probe is now a thin
  wrapper) carries it through the same SEH-guarded read. Nothing is copied from
  Halo 3: role bytes, the FP agreement record (`tls+0x598`) and the vehicle gate
  are ODST's own. `Game_ReadPrimaryWeaponEvidence(Halo3ODST)` maps a validated
  local owner to `KnownPresent`, a valid record with a raw empty primary slot
  (role `0xFF`) to `KnownAbsent`, and everything else to `Unknown`; absence is
  resolved without the FP producer-agreement bit (F03), while a present owner
  still requires agreement on both reads.
- **Trust ordering (A2).** The before-read is taken before the native original;
  the after-read lands immediately after it and **before** the ODST
  scope/context capture (`ReconstructVisiblePaletteSource` and the marker
  transforms), per A015 §16. The decision is frozen once per stereo pair in the
  ODST context (`g_fpStereoSolveScope.supportGripAttached/supportGripResolved`,
  first slot-0 invocation resolves, both eyes reuse it), and the shared
  `FpStereoPaletteCache` key already carries the frozen attachment, so a
  trust/owner change can never reuse the other eye's solved geometry.
  ODST's two observed transition forms both fail closed through the same rule:
  an unstable before/after pair resolves `Unknown`, and a stable
  B-before-Capture resolves a `KnownPresent` owner that is not the relationship
  owner - neither is trusted.
- **Presentation (A3).** ODST shares the H3 palette expression: for a resolved
  pair the visible support attachment is the frozen relationship trust
  (`SupportPresentationAttached`), never the aim-authority flag; a trusted
  same-owner invocation keeps the support hand attached even when aim authority
  is off, and an owner-untrusted invocation gets the ordinary one-hand aim
  (`ControllerWorldPoseEx` sets the thread-local gate around the weapon-hand
  `VR_GetAimPose` call) with no support presentation. Never
  `twoHandAimActive` for a resolved pair, and never a durable mutation.
- **A4.** `PersistentSupportGripApplies` now includes `Halo3ODST`, added in the
  same change that put the producer/trust seam in place.

### Reach (wired)

- **Producer (B1).** `ReachReadMuzzleWeapons` gained the same raw
  `primaryAbsentOut` (role byte `unit+0x34A`, before validation) and
  `ReachPrimaryWeaponEvidence` (thin diagnostic wrapper) carries it with Reach's
  own role bytes, `ReachMuzzleTargetStorage` on-foot gate and `tls+0x6A0`
  agreement record. The FP interpolate hook takes the before-read, lets the
  native original run, takes the after-read immediately after it and resolves
  the pair through the shared `ResolveStableEvidence` (present needs agreement,
  explicit absence does not), then freezes the decision in the Reach pair scope
  before `ReachCaptureFpInterpolation` copies the support-dependent targets.
- **Trust seam (B2).** The frozen decision travels into the per-invocation
  context (`context.targets.supportGripResolved/supportGripAttached`) and drives
  the presentation: the visible support attachment follows the relationship
  trust (`ReachShouldBindVisibleLeftHandToController` now consumes the frozen
  attachment for a resolved pair), never the aim-authority flag. **Stale-owner fallback:** the Reach adapter already
  computes the ordinary calibrated one-hand right-hand orientation for this
  exact prepared frame (`ReachVrRenderSnapshot::rightPhysicalOrientation`); with
  the feature on it also builds the ordinary one-hand weapon-wrist target from
  that pose (same position/scale contract as the attached target, collision
  correction applied separately so the published correction still belongs to the
  served target). A resolved-but-untrusted invocation re-seats the visible
  weapon hand - and the marker graph derived from the same target - on that
  one-hand target. No new solve, no new mode, and no stock-preserving fallback
  is invented; with the feature off the field is never built and the targets are
  byte-identical to the base.
- **Presented-reticle consumer (B3/F06).** `VR_PublishSupportInvocationReceipt`
  (new, `vr.cpp`) publishes the title-native invocation receipt: the evidence
  the Reach FP hook actually resolved, its owner tuple and agreement bit, the
  relationship reading taken at the same instant, the title generation and the
  prepared serial. `ReachBuildPresentedReticleWorldTarget` now reads the ray's
  receipt (`VR_GetPresentedReticleAimPoseWithSupportProvenance`), and requires
  **both** halves through the new pure
  `support_grip::PresentedRayConsumableForInvocation`: (a) the ray's producer
  receipt (owner epoch, trusted support, F14 coherence, serial staleness bound)
  and (b) the consuming frame's own receipt covering that generation/serial,
  still proving the same owner against the LIVE relationship. Epoch equality
  alone is never sufficient, and serial equality is deliberately not required
  beyond the same one-serial staleness bound. On failure the ray is refused and
  the caller keeps the existing native/stock behaviour. The same receipt is
  published with both completed presented points
  (`g_reachCompletedReticleTarget`, `g_reachOnFootShotRay`) and re-qualified by
  every live weapon-sensitive consumer that reads them later - the seated
  shot-origin redirect, the on-foot hand-shot redirect and the vehicle redirect
  sample - so a point can never be injected into a shot after its owner moved.
  **T7-F3 correction:** only the seated shot-origin site has a genuinely
  available consuming-frame serial: it passes the completed rendered eye's
  prepared serial - the same frame whose occupation/freshness proof
  (`ReachShotOriginSampleAdmitted` already proves it non-zero and key/freshness
  coherent) admits the shot. The shot origin itself is the presented sight ray
  (R-V27), not that eye position. The on-foot hand-shot and vehicle-redirect
  sites have no consuming prepared serial in scope inside the Reach weapon
  transaction, so they deliberately keep the ray's own serial and the actual
  conservative behaviour is documented in section 3F: the invocation receipt is
  latest-only, so a ray is consumable only while the newest title invocation is
  still within one serial of the ray - an old ray (a newer receipt exists) and
  an old receipt (nothing newer than the ray) are both refused.
- **Smoothing discontinuity (B4/§10).** The solve-time qualification
  (`ResolveAimSupportQualification`) is unchanged, but the reticle publisher now
  requires the title-native
  invocation receipt for the exact prepared serial before the ray may claim
  support trust (`VR_SupportInvocationReceiptTrustedForReticle`). A missing,
  mismatched or untrusted receipt makes the ray support-untrusted: the smoothing
  history is reseeded for that discontinuity (no blend across the owner
  transition), the published ray carries `supportTrusted=false`, and every
  consumer above refuses it, so nothing reaches a weapon path. Smoothing resumes
  on the next compatible serial; it is never disabled globally. With the feature
  off (or a title that publishes no receipt) the expression is the base one.

### F14 residual closed: `SupportRayConsumable` epoch 0

`SupportRayConsumable` no longer short-circuits an epoch-0 ray to consumable. It
now takes the same `relationshipDisengagedCoherent` assertion as
`PresentedRayOwnerCompatible`: an epoch-0 ray is accepted only when the caller
proves the relationship was coherently readable **and** disengaged when the
reading was taken. Missing/unreadable snapshots and an epoch-0 ray while the
relationship reads engaged are both refused. The pure suite covers both halves,
and the new consumer composition reuses this one owner rule.

### Verification for T6b

- `cmake --build --preset release`: full Release build green (`HaloMCCVR.dll`
  and every test executable link, 0 errors).
- `ctest --preset release`: see section 7C for the recorded numbers.
- New pure coverage (`halomccvr_support_grip_logic_tests`): the receipt coverage
  matrix (own/next serial accepted, two-serials-old, newer-than-consumer, foreign
  generation/title, unresolved, zero serials), the full F06 composition
  (healthy trusted same-owner accepted; the confirmed A->B counterexample
  refused even though the durable reading is still A/42; untrusted or unreadable
  invocation refused; ray and receipt serial bounds; replaced relationship
  epoch), the epoch-0 coherence matrix for both `SupportRayConsumable` and the
  composition, and the applicability/receipt-publisher predicates
  (ODST+Reach wired, H4 not; only Reach publishes a receipt).
- **PG OFF parity, source-level audit.** Every new path is behind
  `VR_SupportGripWiredForTitle` (which is false whenever the config is off):
  ODST/Reach take no before/after read and publish no evidence or receipt; the
  Reach one-hand fallback target is not built; the Reach pair scope stays
  unresolved so `twoHandAimActive` remains the only presentation input and the
  collision/target fields are byte-identical; `VR_SupportInvocationReceiptTrustedForReticle`
  returns true before any read for an unwired title, so the reticle smoothing
  expression and the published receipt are the base ones; and
  `ReachPresentedRayConsumable` returns true for an unwired title, so every
  shot consumer keeps its existing path. PG-off equality remains a code-path
  argument, not a headset result.
- **Still runtime-only (no offline seam test in this tranche):** the ODST pair
  freeze/cache ordering inside the real FP hook, the Reach pair-scope
  freeze/target substitution ordering, the receipt publication/read seqlock, the
  Reach reticle consumer ordering against the real composition/prepared-serial
  sequence, and every headset behaviour. No test target includes
  `reach_muzzle_ownership.inl`/`odst_muzzle_ownership.inl` or the Reach FP hook,
  so those remain documented runtime-only claims until T7 and the headset gate.
- **Residual (recorded, not hidden).** On a Reach owner-untrusted serial the
  compositor still publishes the frame-thread solve's pose; the title's
  per-invocation decision does not reach the frame-thread aim assembly until a
  serial-scoped publication of the frozen pair trust lands (the same T5b
  residual the presented-ray receipt was designed around). The ray is published
  as support-untrusted, refused by every consumer, and never used to seed the
  smoothing history of a later compatible serial, so no weapon path consumes it.
  Closing the pose itself is T7/T6b-remainder work.

## 3E. T6c deliverable: the Halo 4 slice (F03/F04), all six titles wired

Scope: the Halo 4 production owner-evidence producer with the raw
primary-absence path, the storm-hands decision-boundary trust freeze (F04), the
owner-safe prepared one-hand fallback and the presentation. This completes the
per-title rollout: `PersistentSupportGripApplies` is true for every gameplay
title. PG stays default off and PG-off parity stays a hard contract; no headset
result is claimed here.

### Producer (A1/A2)

- **Reader and tri-state mapping.** `Halo4ReadMuzzleWeapons` gained the optional
  `primaryAbsentOut`, filled from the raw primary role byte `unit+0x63A`
  **before** its validation guards, so an explicit empty primary slot
  (`0xFF`) is a real `KnownAbsent` and never an unexplained refusal (F03).
  `Halo4WeaponOwnerEvidence` is the shared production body the diagnostic probe
  used to be: it now also carries the A016 tranche-E decoupling (the vehicle
  input helper is an admission shortcut, not a prerequisite - when its runtime
  reader cannot establish the local output unit, the engine's own first-person
  record proves the on-foot owner through `halo4_owner_evidence::ResolveFromFpRecord`),
  the absence out-parameter and its one-shot per-generation logs.
  `DiagnosticHalo4WeaponOwner` is a thin wrapper, so the diagnostic entry keeps
  working and the native read logic exists once.
- **Publication.** `Halo4ReadSupportInvocation(candidate)` runs the SEH-guarded
  tri-state observation (`Halo4ObservePrimaryWeapon`; a native fault is Unknown,
  never absence) and calls the shared `Game_ResolveSupportInvocation`, which
  publishes the record (including an explicit Unknown) and returns this
  invocation's relation to the live relationship. There is no diagnostic-session
  dependency.
- **Safe context and cadence.** The producer runs inside the first-person
  model-skinning detour on the game render thread, gated by the same conditions
  the diagnostic commit uses (armed, no teardown, the proven FP return address)
  plus a live floating pair. It is deliberately **independent of the optional
  hands presentation**: a publication per pair is what lets the durable writer
  ever bind for Halo 4, so gating it on `halo4_hands` would silently kill Halo
  4's ordinary two-hand authority whenever that presentation toggle is off.
- **Single-read shape.** Halo 4 has no second read to bracket around a native
  original inside the skinning callback, so the observation is passed as both
  the before and after read; a `KnownPresent` claim still requires the FP
  producer/slot agreement bit (`+0x6C ==` the primary slot handle), and an
  explicit absence still resolves without it (F03). This mirrors the archived
  donor's H4 shape and differs from H3/ODST/Reach, which bracket their own
  native original.
- `Game_ReadPrimaryWeaponEvidence` gained the Halo 4 branch (through the same
  shared body), so the shared tri-state reader covers every title. The live
  producer path is `Halo4ReadSupportInvocation`; the shared API branch is what
  the per-title matrix and the shared publish helper see.

### F04 repair: the storm-hands decision boundary (B1-B5)

The established H4 record order per eye is storm hands (its `objectIndex` is the
local **unit** handle) -> held weapon (`objectIndex` is the weapon handle) ->
native body. The archived donor resolved trust for every flag-1 record and fed
the record's own `objectIndex` as a weapon candidate, so the storm record - the
one whose palette consumes the decision - resolved Unknown and the visible
support attachment was already false when the hand palette was built.

- **One decision per pair, semantic primary only.** The pair's first safe
  FP invocation (the storm-hands decision boundary by record order, before any
  palette can consume it) reads the semantic primary with
  `candidate = UINT32_MAX` through the pure
  `halo4_owner_evidence::PairRecordWeaponCandidate(PairRecordAction::StormHandsBoundary,
  objectIndex)`, which deliberately never yields the record's object index. The
  freeze is not gated on `view == 0`; whichever stereo slot the engine renders
  first decides, exactly as the T5b H3 seam does (with `right_eye_first` a
  `view == 0` rule would leave the first eye on the old path).
- **A003 pair policy.** `UpdatePairResolution`/`PairResolutionOf` finalize the
  pair: a provisional Unknown publishes its explicit Unknown record and writes
  nothing to the pair (later records, including the second eye's, may still
  freeze it), Present and Absent are terminal, and the one-shot per-generation
  pair logs name the outcome. `PairRecordMayResolve` states in the pure policy
  that only the storm-hands boundary may resolve; the detour's single freeze
  site - the pair's first safe FP invocation, before any palette - enforces that
  position structurally.
- **The held record only validates.** When the pair froze Present, the held
  record's exact `objectIndex` is validated against the frozen primary weapon
  (`PairRecordValidatesFrozenPrimary`); a mismatch is logged once per generation
  and the frozen decision is retained. The held record can never re-resolve a
  pair whose hand solve already ran.
- **Presentation (B4).** The visible rigid support lock consumes
  `g_halo4FloatingPair.supportGripAttached` - the frozen pair decision - and
  never `twoHandAimActive`, so a trusted same-owner invocation with aim
  authority off stays attached and an untrusted invocation gets the free hand.
  With the feature wired, pair begin no longer seeds the attachment from
  `targetFrame.twoHandAimActive`; it starts unresolved/detached and is frozen by
  the pair's own evidence. With the feature off the seeding is byte-identical to
  the base.

### Owner-safe prepared one-hand fallback (C1/C2)

- **C1.** `Halo4VrRenderSnapshot` gained the smallest immutable field pair,
  `oneHandRightAimValid` + `oneHandRightAimOrientation[4]`, computed by
  `PublishHalo4RenderSnapshot` from the **same** prepared controller sample as
  `rightAim` through `CurrentAimPoseInputs(...)` with `twoHandEnabled = false`
  and `ComputeAimPose` - the ordinary calibrated one-hand solve, no new solver
  and no new mode. The computation is gated on
  `VR_SupportGripWiredForTitle(GameTitle::Halo4)`, so PG off adds no solve and
  the field keeps its falsy default.
- **C2.** `Halo4BeginFloatingPair` freezes the one-hand carrier once per pair
  (`Halo4BuildFloatingWorldTarget(..., oneHandRight = true)`) from the same pair
  frame, and `Halo4BuildFloatingHandsPalette` substitutes it for the weapon
  carrier **only** when the feature is wired, the pair is detached/untrusted,
  the frozen ordinary carrier is valid, and the prepared aim is support-derived
  (`twoHandAimActive`). An untrusted invocation therefore never rides the
  two-hand solve of an owner it could not prove, while a coherently disengaged
  PG-on frame keeps the base (presented) carrier. **Deliberate differences from
  the archived donor:** the carrier is frozen at pair begin instead of rebuilt
  from mutable reference atomics inside the palette hook, and the substitution
  is feature-gated and support-derived-gated, so PG off cannot change the base
  behaviour. `Halo4PlaceFirstPersonHands` stays dormant and uncalled.

### Applicability (D)

`PersistentSupportGripApplies` now includes `Halo4`; the pure suite asserts all
six gameplay titles are wired and that `None`/`Unknown` are not.
`TitlePublishesSupportInvocationReceipt` deliberately stays Reach-only: Halo 4
has no presented-ray consumer in this port, so it publishes no invocation
receipt and its reticle path keeps the solve-time receipt semantics.

### Verification for T6c

See section 7D.

## 3F. T7 corrections tranche: verifier T7-VER-01 findings F1-F5

Scope: the bounded corrections resolving the T7-VER-01 verifier findings on the
landed T6c tree, plus the missing PG parity guard. PG stays default off and
PG-off parity stays a hard contract; no headset result and no new behavioural
claim about the feature is made here. The findings and the chosen resolutions:

| T7-VER-01 finding | Severity | Resolution in this tranche |
| --- | --- | --- |
| F1 - the archive's A003 acquisition predicate exists and is unit tested but no runtime path calls it | MEDIUM | FIX-1: `PersistentSupportGrabZoneHit` (`support_grab::EvaluateSupportGrabZone` on the primary acquisition axis) runs at the durable writer's acquisition edge, which is reachable only while `VR_SupportGripWiredForTitle()` is true; the base `TwoHandGrabZoneHit` keeps the pre-fix geometry verbatim for PG off / unwired titles. Header comment corrected; pure tests kept and a shipping-code seam test added. |
| F2 - no source-consistency evidence that the PG-off parity invariants are actually wired (evidence gap) | MEDIUM | FIX-5: new `tools/check_persistent_support_grip.py` + CTest `halomccvr_persistent_support_grip_guard` pinning config default/parse/save, the applicability gate on both required paths, the base latch stores, the single reseed + owner-break pair, the anti-donor/anti-sway invariants and the acquisition-gate wiring, with a self-test of 25 failing fixtures plus clean-tree assertions. |
| F3 - `ReachPresentedRayConsumable` receives the ray's own serial as the consuming serial at the three live sites, making the staleness bound tautological | LOW | FIX-2: the seated shot-origin site now passes the completed rendered eye's prepared serial (genuinely available, already proved non-zero); the on-foot hand-shot and vehicle-redirect sites keep the ray serial deliberately (no consuming serial exists in that transaction) and the document states the real conservative latest-only-receipt behaviour instead of claiming a consumer-relative bound. |
| F4 - `tools/check_aim_continuity_lifecycle.py` failure message says "definition plus 8 call sites" while `EXPECTED_REFERENCES = 10` (definition plus 9) | LOW | FIX-3: message text corrected to 9; count and behaviour unchanged. |
| F5 - the evidence-publication comment claims "Never called from a render/palette hook" while title producers publish from FP/prepare/interpolate seams | LOW | FIX-4: comment reworded to the actual invariant (lock-free/log-free body; never from a D3D palette-compose or hot compose hook; title-side FP/prepare/interpolate producers). Body unchanged: CAS writer acquisition plus relaxed stores. |

### FIX-1 (verifier F1, MEDIUM): the A003 acquisition-recovery predicate is wired under the PG gate

- **What was wrong.** `src/common/support_grab_logic.h` (the A003 acquisition
  rule from the donor's `SUPPORT_GRIP_ACQUISITION_RECOVERY_A003` evidence:
  palm depth expressed along the stable primary acquisition axis, so the
  support wrist orientation is not an input) existed and was unit tested, but no
  runtime path called it. The port's latch had extracted the pre-fix geometry
  into `TwoHandGrabZoneHit` and used it for both the base path and the durable
  writer, so the archive's acquisition repair was silently absent while PG was
  on.
- **Wiring.** `TwoHandGrabZoneHit` is now the BASE helper only and its body is
  the pre-fix geometry verbatim (palm-depth shift along the SUPPORT controller's
  forward, strict along/lateral thresholds, same zone-right nudge). The new
  `PersistentSupportGrabZoneHit` evaluates
  `support_grab::EvaluateSupportGrabZone(primary position, primary acquisition
  forward, primary right, two_hand_zone_right_m, raw support position,
  left_grip_forward_m)`; it derives the primary frame from `rpose` (the raw
  primary forward and right) and never reads `lpose.orientation`. The durable
  writer's acquisition edge (`UpdatePersistentSupportGrip`) calls that helper,
  and that function is reached only while `VR_SupportGripWiredForTitle()` is
  true. PG OFF (or an unwired title) therefore runs the exact base path and the
  exact base geometry.
- **Gate semantics (deliberate difference from the archived donor).** The donor
  had no PG-off mode, so its single latch ran the A003 rule unconditionally.
  This port gates it: **PG ON = the archive's full support-grip behaviour
  including the A003 acquisition predicate; PG OFF = the exact base behaviour.**
  `src/common/support_grab_logic.h`'s header comment now states that it runs
  when persistent support grip is enabled (it no longer claims all six titles
  run the rule unconditionally).
- **Tests.** The pure acquisition suite is kept. A seam test now compiles the
  shipping helpers themselves: `tools/generate_grip_pose_fixture.py` extracts
  both `TwoHandGrabZoneHit` and `PersistentSupportGrabZoneHit` from `vr.cpp`
  into the grip-pose fixture, and `tests/grip_pose_capture_tests.cpp` asserts on
  one frozen hand position (0.40 m along, 0.089 m lateral): the base helper
  equals an independent copy of the pre-fix arithmetic for every modelled wrist
  (neutral, both rolls, both pitches, both yaws) and its eligibility flips on
  rotation; the persistent helper equals the pure predicate for every wrist
  (rotation-invariant); both honour the shipped palm depth (0.75 m along is
  accepted at depth 0 and pushed past the 0.80 m bound at 0.097 m) and the
  shipped zone-right nudge (0.155 m lateral is outside at zero nudge, inside at
  0.075 m); both fail closed on non-finite primary geometry. The gate wiring
  itself (which helper each path calls) is asserted at source level by the new
  guard (FIX-5).

### FIX-2 (verifier F3, LOW): Reach presented-ray serial bound

- **What was wrong.** `ReachPresentedRayConsumable` received the RAY's own
  serial as the consuming serial at the three live re-qualification sites
  (seated shot-origin, on-foot hand shot, vehicle redirect), which made
  `PresentedRaySerialCompatible(raySerial, consumerSerial)` tautological.
- **Resolution (hybrid, stated with evidence).**
  - The **seated shot-origin** site now passes
    `sample.renderedEyePreparedSerial`: the prepared serial of the completed
    rendered eye - the same frame whose occupation/freshness proof admits the
    shot (`ReachShotOriginSampleAdmitted` already proves it non-zero and
    key/freshness coherent). The serial is genuinely available in scope with no
    new plumbing and comes from that admission/proof publication rather than
    from the ray, so the bound is no longer tautological there. The shot origin
    itself is the presented sight ray (R-V27), never that eye position.
  - The **on-foot hand-shot** and **vehicle-redirect** sites keep the ray's own
    serial **deliberately**. Inside `ReachUnitAdjustBody` (the Reach weapon
    transaction) there is no consuming prepared serial in scope: the
    publication snapshots carry the ray's serial, and `g_reachOwnerScope` is a
    thread-local owned by the render pipeline whose affinity with the weapon
    transaction is not established. Passing it would be speculative plumbing
    whose failure mode (a mismatched or zero serial) would refuse legitimate
    shots, so none was added.
  - **The actual conservative behaviour, stated precisely:** the title-native
    invocation receipt is a latest-only slot, so with the ray serial as the
    consumer serial a ray is consumable only while the NEWEST title invocation
    is still within one serial of that ray. An old ray (whose producing
    invocation has been superseded by a newer receipt) is refused, and an old
    receipt (nothing newer than the ray) is refused; the live-relationship
    re-qualification (`PresentedRayOwnerCompatible`) still refuses a moved
    owner independently. What the code does NOT have at those two sites is a
    bound relative to the consuming frame, and the document now says that
    instead of claiming a consumer-relative staleness bound.

### FIX-3 (verifier F4, LOW): aim-continuity guard failure message

`tools/check_aim_continuity_lifecycle.py` printed "definition plus 8 call
sites" while `EXPECTED_REFERENCES = 10` (definition plus 9). The message text
now says 9. The count, the rule and the self-test are unchanged.

### FIX-4 (verifier F5, LOW): evidence-publication comment

The comment above `PublishSupportGripOwnerEvidenceInternal` said the slot
protocol is "never called from a render/palette hook", which contradicted the
real producers (CE publishes from its prepare hook). It now says what the
invariant actually is: the body stays lock-free, allocation-free and log-free
(CAS writer acquisition plus relaxed stores) because it runs on title-side
publisher threads - the FP/prepare/interpolate observation seams (CE prepare,
H2 level-live poll, H3/ODST/Reach FP interpolate, H4 model-skinning) - and must
never be called from a D3D palette-compose or other hot compose hook. The body
itself was not changed; the publish path remains CAS + relaxed stores with no
lock, log or allocation.

### FIX-5 (verifier F2, MEDIUM evidence gap): the parity guard

New stdlib-only tool `tools/check_persistent_support_grip.py`, registered as
CTest `halomccvr_persistent_support_grip_guard` (mirroring
`tools/check_two_hand_lab_patch.py`; `--self-test` runs synthetic fixtures and
then the real tree, `--self-test-only` runs the fixtures alone, `--root` points
the real check at another tree). Its checks, each narrow:

- (a) the config key: `src/common/config.h` still declares
  `bool persistent_support_grip = false;`, and `src/common/config.cpp` still
  parses the key into that field and still emits the key plus its generated
  comment;
- (b) the applicability predicate
  (`support_grip::PersistentSupportGripApplies`) exists, `VR_SupportGripWiredForTitle`
  still ANDs the config flag with it, the gate is consulted from BOTH the
  writer path (`UpdateTwoHandLatch`) and the assembly-qualification path
  (`ResolveAimSupportQualification`), and the durable writer call itself is
  owned by the gate branch (a writer hoisted above the gate is rejected);
- (c) the PG-off latch branch (after `RetirePersistentSupportGripState`) still
  contains the base toggle on/off stores and the base `UpdateTwoHandHold` store
  verbatim, still uses the base `TwoHandGrabZoneHit` geometry, and contains no
  writer-only call (`UpdatePersistentSupportGrip`/`DurableWriterStep`);
- (d) `ReticleSmoothingDiscontinuity(` occurs exactly once in `vr.cpp`, the
  reseed keeps its `!supportDiscontinuity` guard and its raw-pose fallback, and
  the PG owner-break block still calls both `InvalidateAimContinuityLayer()` and
  `InvalidateTwoHandLabTemporal()`;
- (e) no donor leftovers or retired sway symbols in the new paths:
  `SupportGripEndpointUsesRawGripPose` and the donor's "Use grip point for
  support hand" menu copy are absent from `vr.cpp`, `menu.cpp` and the two new
  pure headers, and no head-turn sway-correction symbol appears in the pure
  modules or the `vr.cpp` persistent-grip region;
- (f) the acquisition gate wiring: the durable writer calls
  `PersistentSupportGrabZoneHit` (never the base helper) and the persistent
  helper body evaluates `support_grab::EvaluateSupportGrabZone` with the
  shipped `two_hand_zone_right_m`/`left_grip_forward_m` values while never
  reading the support wrist orientation, while the base helper keeps its
  `virtual_stock::SupportGrabPoint` shift along the support forward.

The self-test exercises every check group with a passing synthetic tree and
with failing fixtures per rule (default-true config, missing parse/save branch,
missing predicate, gate without the config flag, gate without the predicate,
gate not consulted by the writer or the assembly path, the writer hoisted above
the gate, missing retire, changed base hold store, missing base toggle store, the base path calling the
persistent helper, durable-writer code in the base path, a second reseed, a
missing reseed guard, a missing Lab invalidation, the donor symbol, the donor
menu copy, a sway symbol in the pure module, a sway symbol in the PG paths, the
writer falling back to the base geometry, a base helper that stops using the
support forward, the persistent helper reading the support wrist, and the
persistent helper dropping the shipped zone tuning).

### FIX-6 (doc accuracy): config round-trip harness provenance

The T4 verification section claimed the config round-trip harness lived
"outside the repo". It actually lives at `.scratch/pg-config-roundtrip` inside
this worktree, untracked. The line now says so.

### Verification for the T7 corrections

See section 7E.

## 3G. T13 corrective tranche: retained support steering past the legacy agreement floor

Corrective tranche after the user's headset result on the T12 candidate. The
runtime change is one product behaviour (VS-off support steering persists under
an engaged+trusted persistent-grip relationship), plus one diagnostics-truth
repair and the guards/tests that pin both.

### Headset finding (S1) and the telemetry that proves its mechanism

Reported result: with **Virtual Stock OFF** and the F1 persistent grip on, the
support hand stays visibly attached but the weapon **stops following the support
hand** — the aim snaps back to the trigger-hand calibration for stretches at a
time and returns just as suddenly, with the durable relationship never
releasing. The same session with VS ON is continuous.

The two read-only telemetry reports from that install are the evidence
(`out/telemetry-20260928/T12A-REPORT.md`, `.../T12B-REPORT.md`):

- **The drop is the shared VS-off legacy rule.** With VS off,
  `SelectTwoHandAimDirection` falls through to
  `TryBuildAcceptedSupportDirection(primary, supportEndpoint, rawForward)`,
  which rejects the ray when
  `dot(normalize(support - primary), primaryForward) < 0.35` (~69.5 deg). The
  solve then sets `exactAEndpointSelected`, leaves `twoHandActive = false` and
  returns the primary-only pose while `two_hand_latched` stays true/`engaged`
  (T12A sections 2.4 and 2.6: every one of the 42 latch-held drops in
  Halo 2 / Halo 3 / ODST had `b_attempted=true, b_accepted=false,
  b_extreme_rejected=true` at the flip, and the emitted aim was the ordinary
  one-hand pose; T12B section 2.2: 759 of H4's 1720 latch-held VS-off frames
  dropped this way, 0 by any other cause).
- **The boundary is exactly the code constant.** Max agreement on rejected
  frames / min on accepted frames: 0.349724 / 0.350674 (H2), 0.349896 /
  0.350850 (H3), 0.348815 / 0.350924 (ODST) — T12A section 0; 0.349610 /
  0.351085 on Reach with 26 authority flips inside one 1589-frame held grab —
  T12B section 3.1.
- **VS ON never consults the floor.** A valid stock ray wins outright in
  `SelectTwoHandAimDirection`, which is why VS-on already persisted (T12A
  section 2.1; T12B section 2.4 — the H4 window after the user switched VS on
  had zero rejections).
- **CE log labels were a second, independent finding.** Every CE release
  printed `proven owner replacement` (10/10) because `explicitRelease` was not
  a rung of the release-reason ladder (T12B sections 3a/3b).

### FIX-A (product): retained steering under an engaged+trusted relationship

One pure input, one source of truth:

- `AimPoseInputs::supportSteeringRetained` (default `false`) is set **only** in
  the aim assembly (`CurrentStockAimPoseInputs`,
  `src/dll/virtual_stock_aim.inl`) as a direct copy of the frozen T5b
  qualification bit `supportQualification.supportTrusted`. `QualifySolveSupport`
  only ever sets that bit for a readable, engaged relationship after its
  feature-off short circuit and its untrusted early return — so PG off, an
  unwired title and a disengaged/unreadable relationship leave retention false
  by construction, and an invocation denied through the live
  `g_supportInvocationUntrusted` channel cannot retain either. That channel is
  set only by a title trust seam (today the H3/ODST `ControllerWorldPoseEx`
  carrier around its weapon-hand solve), so it protects exactly the assemblies
  such a seam wraps; the frame-thread assemblies (the presented pose/reticle
  solve) carry the durable relationship qualification and rely on each title's
  consumer seam to refuse support-derived geometry. The per-title audit and the
  consumers that do not re-check are §3G.1. No second latch/relationship read
  is added.
- `TryBuildAcceptedSupportDirection` gained an optional
  `retainBeyondAgreementFloor` argument. When set, a finite agreement below
  0.35 no longer rejects the direction; **only** the floor is skipped —
  finiteness of every input, the minimum primary -> support segment length
  (`1e-4`), the normalization, the finite-output check and (newly explicit)
  the non-finite-agreement guard all still reject. The floor verdict is still
  written to `rejectedExtreme` / `rejectedAgreement`.
- The legacy fallback of `SelectTwoHandAimDirection` consumes it and reports
  the retained acceptance as `DirectionSelection::retainedBeyondAgreementFloor`
  (accepted, with the crossed floor agreement kept in `rejectedAgreement`).
  `rejectedExtreme` keeps its old meaning: it is set only when the invocation
  was actually rejected. `rejectedAgreement` is rejection-only on the rejected
  branch and, since the T14-VER-03 F1 store, carries the exact crossed floor
  agreement on a retained acceptance (and exactly zero for an ordinary in-cone
  acceptance). No consumer reads the field for an accepted selection: the
  solver forwards it to the aim result only on the rejected branch
  (`virtual_stock_aim.inl`), and that aim-result field is only written on the
  rejected paths and only read by telemetry/logging under `rejectedExtreme`.
- The solver forwards it at the VS-off legacy selection **and** at its trace
  re-derivation as `inputs.supportSteeringRetained && !inputs.virtualStockEnabled`.
  A VS-on invocation therefore has zero behavioural delta, including the
  degenerate-stock-ray fallback; the Hybrid offhand call and the target-aware
  (VS-on) selector are untouched, and no threshold or other maths changed.
- The retained selection is `valid = true`, so it runs the normal accepted-B
  authority path: `BuildTwoHandAimOrientation` steers the pose and
  `finishAimPose()` sets `twoHandActive = true` and
  `supportTrusted = twoHandActive && supportMayConsume`. That last line is why
  the fix reaches CE/Reach/H4 as well: `AimSupportDerived(valid, twoHandActive)`
  is true again for a retained frame, so the title consumers keep consuming a
  support-derived pose instead of the silently one-hand one.

Trace semantics (decided and documented rather than implied): `bAccepted`
reflects the retained acceptance, the new `AimPoseTrace::bSteeringRetained`
records that the 0.35 floor was crossed and retained, and `bExtremeRejected`
keeps its established meaning ("this attempt was rejected by the floor"), so
`bRejectedAgreement` stays non-zero only for a real rejection. `bAgreement`
always carries the true dot product, giving the unambiguous retained signature
`b_accepted=true + b_steering_retained=true + a_dot_b<0.35`. The flag is also
serialized additively as `aim_trace.b_steering_retained` so the next headset
recording shows whether retention fired; existing consumers read JSON by key,
so no schema version changes.

Two-Hand Lab coherence: the Lab reads the same selection, so a retained
invocation now arrives as an ordinary accepted B (`labValid`/`bValid` true, its
true crossed agreement, `twoHandActive = true`) instead of the rejected-B zero
authority path. Lab default (off) behaviour is unchanged and its suites stay
green.

### FIX-B (diagnostics truth): the release-reason ladder

`UpdatePersistentSupportGrip` now reports `explicitRelease` **first**; the
evidence rungs describe only transitions the writer itself decided, and the
`replacement owner evidence` / `proven weapon absence` detail lines stay scoped
to those (`!explicitRelease` guards unchanged, logging still transition-only).
The residual `else` rung is named for what it actually is
(`lifecycle identity unavailable (no owner proof)`); it replaced a default that
printed `explicit release` for coincidences, not causes.

### FIX-C (guard): the retained-steering wiring

`tools/check_persistent_support_grip.py` (CTest
`halomccvr_persistent_support_grip_guard`) gained a narrow eighth group, with
failing synthetic fixtures for each rule:

- `AimPoseInputs::supportSteeringRetained` is declared default-false in
  `vr.cpp` and written exactly once — inside `CurrentStockAimPoseInputs` — as
  the direct copy `inputs.supportSteeringRetained =
  supportQualification.supportTrusted` (a hardcoded `true` or a second writer is
  rejected);
- `QualifySolveSupport` keeps exactly one `supportTrusted = true` store, after
  its feature-off short circuit and its untrusted early return (so PG off / a
  denied invocation cannot retain);
- the legacy selection **and** its trace re-derivation both forward
  `inputs.supportSteeringRetained && !inputs.virtualStockEnabled` (exactly two
  occurrences);
- the Hybrid offhand call keeps its exact six-argument agreement-guarded shape,
  and `SelectTwoHandAimDirectionForTarget` never gains the retention parameter
  (VS-on protection);
- the acceptance helper keeps the 0.35 floor text, the non-finite-agreement
  guard, the minimum segment guard, and `return retainBeyondAgreementFloor`
  inside the floor branch.

### CE and Reach notes

- **Reach is confirmed on the same gate** (T12B section 3.3): with VS off the
  durable relationship held for 1589 consecutive frames while the 0.35 rule
  dropped the support aim 677 times, and the VS-on window in the same session
  had zero rejections. Retention therefore addresses exactly the mechanism the
  user reported on Reach.
- **CE remains unproven without a recording** (T12B sections 4.3 and 4.4). CE's
  consumers are downstream of the same solve (`primaryAimSupportDerived`), so a
  retained frame now publishes support-derived geometry for them, but no CE
  telemetry exists and the installed logs' release labels were unusable for
  classification (FIX-B). CE confirmation needs the single PG-on,
  VS-off recorder session requested in T12B section 4.4.

### 3G.1 T14-VER-03 F3 audit: who re-checks the current invocation/owner

Bounded read-only audit of every carrier/consumer that consumes a support-capable
frame-thread solve or the aim/published receipt it produces, for the titles the
T13 corrective reaches. "Re-checks" means the consumer proves **this**
invocation's owner/trust against the live durable relationship (or consumes a
decision frozen for that same invocation by its own title seam); it does not
mean the pose's `supportTrusted` flag.

| Title | Carrier / consumer (what it reads) | Site | Independent current-invocation/owner re-check |
| --- | --- | --- | --- |
| CE (reference) | support-carrier decision for the frozen CE primary aim | `src/dll/haloce_controls.h:47-82` (owner proof `:66-80`), consumed at `src/dll/haloce_unit_control.cpp:116-145` and `src/dll/haloce_first_person.cpp:744-818` | **Yes** — `CeOwnerEvidence` is built from the consuming invocation's own `HaloCELocalPlayerState` (the CE prepare decision owns a dedicated read at `src/dll/haloce_first_person.cpp:798-803`), and the unit-control seam additionally requires the before/after read continuity and a stable decision |
| Halo 4 (reference) | support carrier / floating controller pair | `src/dll/game.cpp:36198-36212` (also `:36009-36054`, `:36480-36507`, `:36696-36699`, `:47102`) | **Yes** — the pair's own frozen `supportGripAttached`/`oneHandRightTargetValid` decide, and an owner-untrusted support-derived pair falls back to the frozen ordinary one-hand carrier |
| Halo 2 | packet-builder presentation / two-hand authority | `src/dll/halo2_observer_6dof.cpp:2413-2442` (`Halo2EvaluateSupportUse`), consumed at `:2629` and `:2749-2761` | **Yes** — the consuming packet's own `unitObject`/`weaponObject` is proved against the live relationship (`InvocationOwnerTrusted`, `KnownPresent`) at `:2426-2434` |
| Halo 2 | native aim / aim-assist shot direction | `src/dll/halo2_observer_6dof.cpp:3368-3402` (`BuildPublishedControllerAimDirection`), decision at `:3389-3390` | **Yes** — `Halo2SupportCarrierMustDetach(publication)` reads the current owner through `Game_ReadPrimaryWeaponEvidence(Halo2)` (`:2466-2495`) |
| Halo 2 | scope camera | `src/dll/halo2_observer_6dof.cpp:6117-6126` (`Halo2Observer6Dof_BuildScopeCamera`) | **Yes** — same helper, evaluated against this publication |
| Halo 2 | direct weapon-aim bullet direction rebuilt from the presented reticle pose | `src/dll/halo2_observer_6dof.cpp:3649-3728` (`Halo2WeaponAimHelperDetour`; presented pose read `:3717-3728`) | **No** (row F3-a below) |
| Halo 2 | visible FP carriers from the live solve | `src/dll/halo2_observer_6dof.cpp:1261-1272` (`BuildFirstPersonCarrier`), used at `:2074` (`PrepareFinalPaletteContext`) and `:3197` (reanchor floaty path); interpolation-reset bypass at `:3342` | **No** (row F3-b below) — presentation-only carriers fed by `VR_GetAimPose`; they never consult `Halo2EvaluateSupportUse`/`Halo2SupportCarrierMustDetach` |
| Halo 3 | FP/palette pair freeze (producer, trust seam) | `src/dll/game.cpp:5108-5167` (`FpInterpolateHook`; freeze `:5158-5166`) | **Yes** (producer) — the root slot-0 before/after read resolves `InvocationOwnerTrusted` and freezes it once per stereo pair |
| Halo 3 | visible palette presentation, arm IK and palette-cache key | `src/dll/game.cpp:5378-5432` (`ReconstructVisiblePaletteSource`); cache key `:5446-5447`, `:5506-5507` | **Yes** — consumes the same invocation's frozen pair decision (`supportGripResolved`/`supportGripAttached`) instead of `twoHandAimActive`; an unresolved pair keeps base semantics |
| Halo 3 | visible weapon aim carrier (the denial channel itself) | `src/dll/game.cpp:6567-6588` (`ControllerWorldPoseEx`) | **Yes** — for an armed, owner-untrusted pair it sets the live `g_supportInvocationUntrusted` gate around the weapon-hand `VR_GetAimPose` (`:6579-6587`), so the assembly resolves the ordinary one-hand path for that invocation |
| Halo 3 | marker/flash root and dormant sim-bank hand path from the live solve | `src/dll/game.cpp:6697-6710` (`ApplyControllerToRoot`); `:4300-4303` and `:4538` (sim-bank, dead once the FP interpolator hook is live) | **No** (row F3-c below) — live `VR_GetAimPose` through `GetControllerFirstPersonTransform` (`:4017-4024`) with no pair-trust/owner proof |
| Halo 3 | reticle smoothing | non-Reach reticle row below | **No** |
| ODST | FP pair freeze (producer, trust seam) | `src/dll/game.cpp:12834-12907` (`OdstFpInterpolateWeaponBody`; freeze `:12890-12906`) | **Yes** (producer) — ODST's own role bytes/agreement reader, frozen once per stereo pair |
| ODST | visible palette presentation (shared with Halo 3) | `src/dll/game.cpp:5378-5432` | **Yes** — frozen pair decision when the pair is resolved; base `twoHandAimActive` semantics otherwise (PG-off parity) |
| ODST | weapon-hand aim carrier | `src/dll/game.cpp:6567-6588` (`ControllerWorldPoseEx`, same denial gate) | **Yes** |
| ODST | scope camera | `src/dll/odst_scope.inl:23` (through `ControllerWorldPoseEx`) | **Yes** — inherits the denial gate |
| ODST | marker/flash root (shared Halo 3 path) | `src/dll/game.cpp:6697-6710` | **No** (row F3-c below) |
| ODST | reticle smoothing | non-Reach reticle row below | **No** |
| Reach | FP pair freeze + title-native invocation receipt (producer) | `src/dll/game.cpp:24544-24636`; freeze `:24613-24618`, receipt publication `:24619-24634` | **Yes** (producer) |
| Reach | FP/palette presentation and stale-owner re-seat | `src/dll/game.cpp:24492-24508` (`ReachCaptureFpInterpolation`); left-hand bind `:24935-24939` (`ReachProcessFpPalette`) | **Yes** — a resolved-but-untrusted pair re-seats the visible weapon hand on the frozen ordinary one-hand target before capture, and the bind consumes the same frozen attachment |
| Reach | presented reticle world target / shot origin | `src/dll/game.cpp:22463-22494` (`ReachBuildPresentedReticleWorldTarget`) through `ReachPresentedRayConsumable` (`:22439-22457`) | **Yes** — ray producer receipt plus the consuming invocation's own receipt against the live relationship |
| Reach | seated shot-origin / on-foot hand-shot / vehicle-redirect re-qualification | `src/dll/game.cpp:18942-18944` (`ReachUnitCameraPositionBody`), `:19135-19137` and `:19206-19209` (`ReachUnitAdjustBody`) | **Yes** — each site re-proves the receipt; the serial argument of the last two is the documented conservative latest-only choice (T7-F3) |
| Reach | scope camera | `src/dll/reach_scope.inl:18` (through `ControllerWorldPoseEx`) | **No** (row F3-d below) — the H3/ODST `g_fpStereoSolveScope` gate `ControllerWorldPoseEx` consults is armed only by the H3/ODST render hooks (`src/dll/game.cpp:10003-10006`, `:13651-13654`), so on Reach no denial is applied |
| Non-Reach reticle smoothing path (CE/H2/H3/ODST/H4) | presented reticle pose and the solve-time receipt | `src/dll/vr.cpp:16025-16078`; `VR_SupportInvocationReceiptTrustedForReticle` `src/dll/vr.cpp:21461-21483` | **No** (row F3-e below) — for a title that publishes no invocation receipt (everything except Reach) the helper returns `true` before any read (`:21468-21470`), so the path trusts the solve-time `supportTrusted`/`relationshipEngaged` receipt and the receipt-relative `ReticleSmoothingDiscontinuity` (`:16062-16066`); there is no current-invocation owner proof to be had |

**Rows that do not re-check (fail-open paths).** Recorded, not changed, per the
T14-VER-03 instruction; none of them is introduced or modified by T13, but
retention widens the angular window in which the frame-thread solve is
support-steered, so the exposure of each row is wider than before the fix:

- **F3-a, Halo 2 direct weapon-aim bullet path** (`halo2_observer_6dof.cpp:3717-3728`):
  rebuilds the firing carrier from the presented reticle pose, which for a
  retained (or ordinary in-cone) frame is support-steered, without consulting
  `Halo2SupportCarrierMustDetach`. The aim/aim-assist/scope and packet-builder
  paths do re-check, so this is a local inconsistency inside Halo 2, not a
  missing Halo 2 mechanism.
- **F3-b, Halo 2 visible carriers** (`halo2_observer_6dof.cpp:1261-1272` used at
  `:2074`/`:3197`, and the interpolation-reset bypass at `:3342`): presentation
  only; the packet-builder presentation path is the one that detaches.
- **F3-c, Halo 3/ODST marker/flash root** (`game.cpp:6697-6710` through
  `GetControllerFirstPersonTransform`, `:4017-4024`): the visible weapon aim
  carrier does apply the denial gate; this marker/flash (and dormant sim-bank)
  consumer does not.
- **F3-d, Reach scope camera** (`reach_scope.inl:18`): the shared carrier's
  denial gate is H3/ODST-scoped, so a Reach scope frame can consume a
  support-steered live solve even when the Reach FP invocation proved a
  different owner. The Reach presented-ray consumers are unaffected (they carry
  their own receipt rule).
- **F3-e, non-Reach reticle smoothing** (`vr.cpp:16025-16078`): no
  invocation receipt exists for these titles, so the presented ray/reticle
  carries the solve-time qualification only. This is the residual already
  recorded for Reach in section 3D ("the compositor still publishes the
  frame-thread solve's pose"), generalised to every title without a receipt.

Consequence for the T13 claim: "a denied invocation leaves retention false" is
true only for the assemblies a title trust seam wraps (H3/ODST
`ControllerWorldPoseEx`). The frame-thread assemblies resolve the durable
relationship and carry that qualification into the presented pose; the
per-title consumer seams above are what refuse support-derived geometry for an
untrusted invocation, and rows F3-a..F3-e are the places without such a proof.

### T14-VER-03 follow-ups (FIX-1..FIX-3)

- **FIX-1 (store).** `SelectTwoHandAimDirection` now stores the crossed floor
  agreement on a retained acceptance (`rejectedAgreement = floorRejected ?
  floorAgreement : 0.0f`), matching the `DirectionSelection`/header contract;
  the solver trace keeps its rejection-only semantics and no consumer reads the
  field for an accepted selection (see the corrected FIX-A bullet above).
  `tests/virtual_stock_tests.cpp` now asserts the exact retained agreement
  (`fabs(... - 0.3499) <= 1e-5`) and that an ordinary in-cone acceptance keeps
  it at zero, so the previously vacuous `< 0.35` check can no longer pass on the
  default zero.
- **FIX-2 (guard fixtures).** `tools/check_persistent_support_grip.py` gained
  the rule that `ResolveAimSupportQualification` must still pass the live
  `g_supportInvocationUntrusted` input to `QualifySolveSupport` (a hardcoded
  argument is rejected), and two synthetic fixtures: a call site that passes
  `false`, and a deleted non-finite-agreement guard in
  `src/common/virtual_stock_logic.h`. Both are rejected by the check; the
  fixture count below is updated accordingly.
- **FIX-3 (audit + claim correction).** Section 3G.1 above; the T13 code
  comment at `src/dll/virtual_stock_aim.inl` and the FIX-A bullet now scope the
  denial claim to the trust-seam assemblies and cite the table.

### Verification for the T14-VER-03 follow-ups

- `cmake --build --preset release`: green, exit 0 (`HaloMCCVR.dll` and every
  test executable linked; no errors).
- `ctest --preset release --output-on-failure`: **99/99** tests passed
  (11.82 s total).
- `halomccvr_virtual_stock_tests`: **6660 checks / 0 failures** (was 6659: the
  strengthened retained-agreement assertion replaces the vacuous `< 0.35`
  check, plus the new in-cone zero assertion).
  `halomccvr_aim_pose_tests`: **166 checks / 0 failures** (unchanged: the F1
  store lives on `DirectionSelection` and no solver consumer reads it for an
  accepted selection).
- `tools/check_persistent_support_grip.py --self-test`: clean tree passes and
  all **37** synthetic failing fixtures (12 new) are rejected, including the two
  added here (a qualification call passing `false` instead of the live untrusted
  input, and a deleted non-finite-agreement guard); the real-tree check under
  this worktree is clean.
- Still runtime-only (unchanged by these follow-ups): every headset behaviour,
  the F3-a..F3-e rows' real-traffic exposure, and the CE recording.

### Verification for the T13 corrective

- `cmake --build --preset release`: full Release build green
  (`HaloMCCVR.dll` and every test executable, 0 errors after the changes).
- `ctest --preset release --output-on-failure`: **99/99** suites passed.
  `halomccvr_virtual_stock_tests` **6659 checks / 0 failures**,
  `halomccvr_aim_pose_tests` **166 checks / 0 failures** (20 new assertions
  across the two suites, all in the new coverage).
- New coverage: pure boundary tests (0.3501 accepted, 0.3499 rejected;
  retained accepts the same sub-floor ray with the exact normalized direction
  and still reports the floor verdict; non-finite forward, non-finite
  agreement, non-finite support and near-zero-length segments still reject;
  VS-on stock ray unaffected), and a solver-level test through the generated
  production solver: the same sub-floor VS-off fixture stays
  one-hand + `rejectedExtreme` unretained and becomes
  `valid + twoHandActive + supportTrusted` (exact support direction,
  position and receipt) when retained, with the retained trace flags; the
  retention input does not change a VS-on invocation.
- `tools/check_persistent_support_grip.py --self-test`: clean tree passes and
  all 37 synthetic failing fixtures (12 new) are rejected: default-true
  retention, hardcoded retention write, a second writer, dropped
  selection/trace forwarding, Hybrid/target-selector retention, an ignored
  retention parameter, a lowered floor, trust-before-untrusted, a qualification
  call that drops the live untrusted input and a deleted non-finite-agreement
  guard.
- **PG OFF parity and VS ON parity are code-path arguments, not headset
  results.** PG off: the qualification short-circuits before any durable read,
  so the retention input is false and the legacy rule, the Hybrid path and
  every threshold are byte-identical; the guard pins the single default-false
  declaration, the assembly-only write and the qualification ordering. VS on:
  the retention argument is gated off by `!inputs.virtualStockEnabled` at both
  consumption sites, and the pure helper's stock branch is untouched.
- **Still runtime-only:** the headset confirmation that VS-off steering now
  persists, the CE recording, and the retained frame's end-to-end effect on
  each title's consumers.

## 3H. T17 headset round 2: all-title persistent grip with the CE trust-source repair

### Headset results (round 2, PG ON)

The T13/T14 candidate was tested on all six titles across the full matrix
(PG ON with VS off and VS on). Five titles behave as intended: while a
relationship is engaged and the current invocation proves the same owner, the
support hand stays attached and the support-derived steering persists with
virtual stock off *and* on.

| Title | PG ON, VS off | PG ON, VS on | Recorded result |
| --- | --- | --- | --- |
| Halo 2 | pass | pass | full matrix (support attaches, steering persists) |
| Halo 3 | pass | pass | full matrix (reference title) |
| ODST | pass | pass | full matrix |
| Reach | pass | pass | full matrix |
| Halo 4 | pass | pass | full matrix |
| CE | **fail** | **fail** | "grip doesn't work" — no support influence in either VS state, while the durable writer's own bind/release edges still fire (the release is still visible as the 200 ms VS-continuity ease); repaired below, pending headset confirmation |

### CE root cause (confirmed in source)

`PrepareHook` resolved the invocation's support trust from the local-player
state that the **muzzle** read above it had filled:
`src/dll/haloce_first_person.cpp` gated that read on
`local.context.tracking.controllers.gunBarrelAim && …GetLocalPlayerState(state)
&& LocalOnFootShooter(state.unit)`. `gun_barrel_aim` ships off
(`src/common/config.h:595`), so on a default installation that read never ran
and the default-initialized state (`generation = 0`, `weapon = 0xffffffff`;
`src/dll/haloce_controls.h:12`) reached `CeEvaluateSupportInvocation`
(`src/dll/haloce_controls.h:45-82`) → `CeOwnerEvidence` → Unknown →
`ownerTrusted = false` → `SupportInvocationMustDetach` → **detach on every
invocation**. The CE prepare then substituted `independentPrimaryAim` for the
frozen support carrier and cleared `supportGripAttached`, so the visible support
hand never attached and the support-derived steering was never consumed.

The durable writer was healthy throughout (binds and explicit releases logged),
which is exactly why the release edge stayed visible in the headset while no
support influence ever appeared: the defect sits in the per-invocation trust
seam, not in the relationship.

### Fix (CE-local, `src/dll/haloce_first_person.cpp:798-803`)

The `if (supportOutermostPrepare)` decision block now owns a dedicated
`HaloCELocalPlayerState` read, and the trust decision consumes **that** state:

- the read is independent of `gun_barrel_aim` and of the muzzle read's
  `LocalOnFootShooter` validation;
- a failed read resets the local to the default state, so it stays generation 0
  → Unknown → detach (`fail-closed`); no evidence is inferred from a failed
  read;
- the muzzle gating (`local.muzzleUnit`/`local.muzzleWeapon`) and every other
  behaviour are unchanged;
- the audited comment block now records the real dependency chain (the trust
  decision reads its own state; the muzzle read stays gated).

Audit of the other `CeEvaluateSupportInvocation` callers (read-only): the four
CE unit-control sites (`src/dll/haloce_unit_control.cpp:117-145` and
`:166-205`) pass state read per call by `Owner` (`:48-58`, through
`HaloCEControls_GetLocomotionFrame`) or `VehicleOwner` (`:65-92`), and
`ControllerShotDirection` (`src/dll/haloce_first_person.cpp:178-180`) passes
state filled by its own `LocalOnFootShooter` read. None of them is gated on
`gun_barrel_aim`; all are unaffected and need no change.

### Verification for the T17 CE repair

- `tests/haloce_first_person_runtime_tests.cpp` now captures the frozen prepare
  scope from the native prepare fixture (`:179-183`) and asserts the decision on
  the exact prepared context (`:744-857`): with `gun_barrel_aim = false` and a
  matching relationship owner the support hand **attaches** and the frozen
  support-derived carrier is not substituted (while `muzzleUnit`/`muzzleWeapon`
  stay absent, pinning the unchanged muzzle gate); an owner mismatch, a failed
  read (the fixture still exposes a matching raw record) and an unreadable
  relationship all detach and substitute the independent carrier; PG off leaves
  the frozen context untouched. Reverting only the trust argument to the
  gated state reproduces the defect as a test failure at the attach assertion,
  so the coverage pins the actual contract rather than the new text.
- `tools/check_persistent_support_grip.py` gained pin 9: in
  `src/dll/haloce_first_person.cpp` the outermost-prepare decision must consume
  the state written by a `HaloCEControls_GetLocalPlayerState` read inside its
  own `supportOutermostPrepare` block, and the pre-fix gated-state form is
  rejected by that pin (39 synthetic failing fixtures now, 2 new).
- `cmake --build --preset release`: green, exit 0 (the whole candidate tree,
  including `halomccvr_ce_first_person_runtime_tests`).
- `ctest --preset release --output-on-failure`: **99/99** tests passed
  (11.60 s real), unchanged suite population.
- **Still runtime-only:** the CE headset confirmation (PG ON × VS off/on) and
  the CE recording. The five-title round-2 results above are headset evidence
  for the T13/T14 candidate, not for this repair.

## 3I. T18 user-requested weapon-switch policy (this tranche)

### Headset request and the product decision

The user tested the T17 candidate with PG ON and reported two weapon-switch
behaviours (Halo CE, one session; Halo 2 also observed):

1. **Default change:** with the Grip still held, switching weapons left the new
   weapon ungrippable until the button was released and pressed again. The user
   asked for the switched weapon to be re-grippable through the normal spatial
   admission (hand to the grip point) without a release cycle: *"if I am holding
   down the grip button ... and then switch the weapon and move the weapon to
   where it should be grippable - it does not grip until I let go ... I think
   that should be a default."*
2. **New option:** the pre-persistent-grip behaviour the user liked, where the
   switched weapon was already held two-handed while the Grip stayed held, is
   now the opt-in `two_hand_switch_inherit` (F1 checkbox "Switched weapons
   already two-handed", default off).

Both are PG-ON only: they live inside `UpdatePersistentSupportGrip`
(`src/dll/vr.cpp`), which only runs behind `VR_SupportGripWiredForTitle()`
(`persistent_support_grip && PersistentSupportGripApplies(title)`). PG OFF
keeps the untouched base latch path with no durable writer.

### Log evidence (user's own session, HaloMCCVR.log)

Installed-log evidence (Steam edition,
`...\MCC\Binaries\Win64\Halo_MCC_VR\HaloMCCVR.log`): every CE switch
(11:37:25, 11:37:29, 11:38:01, 11:38:13, 11:38:35) and every Halo 2 switch
(11:39:57, 11:40:00, 11:40:04) logged exactly:

```
relationship released (epoch N -> N+1): proven owner replacement; old owner ... weapon=0xE45901EA
replacement owner evidence ... weapon=0xE45A01EB revision=...
acquisition blocked until the Grip is released (release-before-reacquire armed)
```

followed by the bind 1-5 s later, after the user released and re-pressed the
Grip. The CE example the user quoted (11:38:01.482 -> 11:38:06.578) shows
`weapon=0xE45901EA -> 0xE45A01EB` and the re-bind only after the release.

**Absence transient checked, not assumed.** Across every preserved log (the
user's 11:29-11:48 session in the install folder - the build that produced the
11:37-11:38 evidence - plus the earlier 09:10-09:34 and 09:35-09:43 sessions
preserved under `out/telemetry-20260928/`) there is **no** `proven weapon
absence` line at all. In the 11:37-11:38 build every one of the 11 recorded
switch releases is a `proven owner replacement` followed by a `replacement
owner evidence` line naming a different, complete, same-generation weapon on
the same unit (CE 0xE45901EA <-> 0xE45A01EB; Halo 2 0xE3D00161 <-> 0xE2A20033),
and the only real switch in the earlier session (Halo 2, 09:19:55) does too.
The earlier pre-T13 build's identical-owner `proven owner replacement` lines
carry no replacement-detail line and are the old catch-all ladder mislabel that
T13 already repaired (section 3G FIX-B), not switches. No title switch observed
in any log passes through a transient `KnownAbsent`, so the changed default
covers the reported case directly; the absence arm is retained for the case
where a title's weapon->unarmed window is actually observed (F17 liveness
remains unproven for H3/ODST/Reach/H4).

### Behaviour 1 (default): a proven replacement no longer arms

`support_grip::DurableWriterStep` stage 2 used to treat a KnownPresent owner
replacement and a KnownAbsent slot identically: both set
`requiresRelease = true`, cleared the owner and advanced the epoch. A KnownPresent
replacement now only drops the relationship:

```
old:  if (evidence == KnownAbsent || (KnownPresent && !SameOwner)) -> requiresRelease = true; clear; Invalidated
new:  if (evidence == KnownAbsent)                                  -> requiresRelease = true; clear; Invalidated
      if (KnownPresent && !SameOwner)                               -> clear; Invalidated (no arm)
```

The switched weapon is therefore re-acquired by the ordinary path:

- **hold mode:** `acquisitionEdge = gripHeld && inZone` at the next update
  begins the normal pending intent; the bind still needs a `KnownPresent`
  whose revision is strictly newer than the intent's floor (F08 intact), so the
  weapon binds as soon as the producer's next publication arrives (one evidence
  cycle, ~1 frame at 90 Hz) with the Grip still held;
- **toggle mode:** the next deliberate click in the zone is the acquisition
  edge and binds with a fresh publication; the click that was already down
  before the switch cannot re-acquire (no rising edge), which is ordinary
  toggle semantics, not an arming block.

**Corrected by T19 (section 3J, same-day headset follow-up):** the level-triggered
hold-mode edge above re-bound the replacement on the next updates whenever the
support hand was already inside the NEW weapon's grab zone through the swap - the
installed log shows the bind 38-51 ms after the drop - and the user felt that as
the switched weapon arriving pre-held. The default path now requires a fresh zone
crossing before the held acquisition re-fires (no release/press cycle
inserted). The no-arm drop rule itself is unchanged.

Unchanged arms: the generation replacement (stage 0), proven absence (stage 2),
the lifecycle gate, and the caller's `weaponGesture` / `rolesChanged` /
`identityLost` teardowns (which arm the shared admission before the writer
clears anything). The `"acquisition blocked until the Grip is released"`
diagnostic and the `release-before-reacquire armed` reason rung therefore stay
truthful: they are reachable only in the retained armed cases. The N1/N2
invalidation (`InvalidateAimContinuityLayer` + `InvalidateTwoHandLabTemporal`)
still runs on the drop, because the relationship really did end.

### Behaviour 2 (option, default off): `two_hand_switch_inherit`

New `Config::two_hand_switch_inherit` (default `false`), parsed from
`two_hand_switch_inherit`, emitted by the config writer, and exposed as one F1
checkbox next to "Persistent support grip (experimental)". When a proven
replacement arrives for an engaged relationship and the option is on, the
policy rebinds in place instead of dropping:

- the successor must be `KnownPresent`, `OwnerComplete`, the same title and the
  active generation; then `owner = evidenceOwner`, `epoch++`, `engaged` stays
  true, the step returns `Bind`;
- an incomplete, foreign-title or stale-generation claim falls back to the
  default drop-without-arm, so a partial record can never inherit;
- absence, generation replacement, lifecycle/role/gesture teardowns and unknown
  evidence are untouched by the option;
- **no F08 floor** is applied: this is a proven replacement of the relationship
  owner rather than a fresh acquisition, so there is no risk of a cached
  prior-owner publication creating a relationship - the claim *is* the
  replacement owner, observed live;
- **no N1/N2 reset and no arming**: the relationship never ended, so the aim
  continuity and Two-Handed Lab layers keep easing/re-presenting exactly as
  the old liked pre-port behaviour did (the switched weapon is two-handed
  immediately, with no re-orientation);
- toggle mode inherits too while the relationship is engaged (the switch is
  observed while engaged; a subsequent click still releases normally);
- the runtime logs `Persistent support grip: switched-weapon inheritance
  rebound owner (epoch N) ...` so an inherited switch is distinguishable from a
  fresh bind.

Thresholds and filtering: the switch is only consumed when the caller's
`hardGate` is true (`two_handed_aim`, grab admission, no role change, no
weapon gesture, valid lifecycle identity, no identity loss, tracking valid), so
a switch coinciding with a gesture/role teardown keeps the teardown semantics.

### Verification for T18

- `halomccvr_support_grip_logic_tests`: **254 checks / 0 failures** (was 235;
  the new `TestWeaponSwitchPolicy` function adds 13 checks, and the replacement/
  absence/intent expectations were rewritten to the new contract, not deleted:
  the held-Grip A->B drop without arming, its F08-gated re-acquisition, the
  absence arm, the in-place inheritance, the three fallbacks, the untouched
  absence/generation/unknown cases, plus the unchanged lifecycle, role,
  gesture, pose-outage and retained-arm matrices).
- `tests/core_tests.cpp` config round trip: the default-off key is written by
  the config writer, and `two_hand_switch_inherit` survives
  `ConfigSave` -> `ConfigLoad`.
- `tools/check_persistent_support_grip.py` gained check group 10: the new key
  stays default-off with its parse/save branches, `DurableWriterStep` keeps
  exactly two `state.requiresRelease` arm sites (generation + proven absence)
  and its option-gated in-place rebind, and
  `UpdatePersistentSupportGrip` reads the option and passes it into the policy
  step. Nine new synthetic failing fixtures (default-true, missing parse,
  missing save, option not passed, hardcoded option, re-armed replacement,
  absence without arm, ungated inheritance, inheritance that drops engagement).
- `cmake --build --preset release` and `ctest --preset release
  --output-on-failure` results are recorded in section 7F.
- **Still runtime-only:** the headset confirmation of both behaviours (the new
  default re-acquire feel and the inherit option), and the H2/H3/ODST/Reach/H4
  switch behaviour beyond the two titles observed so far.

## 3J. T19 headset follow-up: the default switch must not pre-hold (this tranche)

### Headset evidence (user, build `a718e53`, installed `HaloMCCVR.log`)

The user tested the T18 default and the `two_hand_switch_inherit` option (their
cfg had `two_hand_switch_inherit = 1` for part of the session and they toggled
it, so both modes were exercised). The option-on path behaved as designed; the
DEFAULT (option off) path did not:

- every default switch logged `relationship released (epoch N -> N+1): proven
  owner replacement`, then `replacement owner evidence ...`, and then
  `bound owner (epoch N+2) ... evidence revision=+4` only **38-51 ms** later
  (e.g. `13:14:48.377 -> .415`, `13:14:52.320 -> .359`,
  `13:16:34.978 -> 35.029`, `13:19:01.063 -> 01.100`);
- cause: the durable writer's hold-mode acquisition is LEVEL-triggered
  (`gripHeld && inZone`) and the player's support hand is already inside the NEW
  weapon's grab zone through the swap, so the next updates re-bound the
  replacement immediately - felt in the headset as the switched weapon arriving
  "pre-held";
- the INHERIT (on) path logged `switched-weapon inheritance rebound owner`
  immediately; that path is correct and is unchanged by this tranche.

The user's ask, verbatim in substance: the default must NOT pre-hold; it should
engage when they bring the weapon/grab point to the hand (a fresh crossing),
WITHOUT a grip release/press cycle - *"not that we switch into the weapon and
it's already gripped"*.

### Corrected UX statement (default, option off)

With `persistent_support_grip` on and `two_hand_switch_inherit` off:

- a proven `KnownPresent` owner replacement still drops the durable relationship
  WITHOUT arming release-before-reacquire (section 3I unchanged), so the
  physical Grip stays live through the swap;
- hold mode then does **not** re-acquire the replacement until the grab zone has
  been observed FALSE at least once: the grab point must leave the zone (move
  the hand away, or bring the weapon's grab point away) and come back. That
  single crossing re-enables the ordinary acquisition edge; no Grip
  release/press cycle is required and none is inserted;
- toggle mode is unchanged: a fresh click is already a deliberate acquisition,
  so it is never gated by the crossing requirement;
- `two_hand_switch_inherit` (on) is unchanged: the replacement is rebound in
  place immediately (no crossing, no arming, no N1/N2 reset), and the option
  never arms the crossing requirement;
- proven absence, generation replacement and every lifecycle/role/gesture
  teardown are unchanged: they keep arming release-before-reacquire.

### State-machine change (old vs new)

`src/dll/vr.cpp` (`PersistentSupportGripState`, writer only) plus two pure
policy functions in `src/common/support_grip_logic.h`:

```
old:  hold-mode acquisition edge   = gripHeld && inZone
      toggle-mode acquisition edge = rising && inZone && !writer.engaged

new:  arm (root invocation both modes, after the writer step):
        DefaultSwitchReplacementDrop(engagedBefore, action,
            evidence == KnownPresent && !SameOwner(evidenceOwner, previousOwner),
            identityLost, switchInherit)
          -> g_persistentSupportGrip.awaitZoneReentry = true

      per update (vr.cpp, before the pending-intent step):
        StepSwitchZoneReentry(g_persistentSupportGrip.awaitZoneReentry,
            gripHeld, inZone)
          -> step.awaitZoneReentry      = awaitZoneReentry && inZone
             step.holdAcquisitionEdge  = gripHeld && inZone && !awaitZoneReentry
      hold-mode acquisition edge   = zoneReentry.holdAcquisitionEdge
      toggle-mode acquisition edge = rising && inZone && !writer.engaged  (unchanged)
```

`DefaultSwitchReplacementDrop` is the no-inherit stage-2 owner break only: a
proven absence and a generation replacement arm `release-before-reacquire`
inside `DurableWriterStep` instead, the inherit option rebinds in place
(`Bind`), a role/gesture/lifecycle teardown answers at the lifecycle gate
first, and the predicate is explicitly false with `switchInherit` set, so the
option's shipped semantics are untouched.

Flag matrix (`g_persistentSupportGrip.awaitZoneReentry`, default false):

| Site | Transition | Effect |
| --- | --- | --- |
| writer, after `DurableWriterStep` | default (no-inherit) proven replacement drop | SET |
| `StepSwitchZoneReentry` | zone observed FALSE (`!inZone`) | CLEAR (the crossing) |
| explicit-release branch | hold-mode Grip release / toggle off press | CLEAR |
| `forcedTeardown` (`identityLost` / `rolesChanged` / `weaponGesture`) | lifecycle, role, gesture teardown | CLEAR |
| `RetirePersistentSupportGripState` | feature off / unwired title | CLEAR |
| inherit rebind branch | option-on in-place rebind | CLEAR (defensively; never set) |
| pose-outage early return | tracking outage | retained (suspended, like the relationship) |

The F08 revision floor, the pending-intent lifetime, the absence/generation
arms, the no-arm default of section 3I, the N1/N2 invalidation and every other
arm are untouched.

## 4. Defect-repair ledger (A017 red-team A001 -> tranches)

Planning ledger, **not** a claim of completion. Status per row is updated as
tranches land; a row without a T5b note is unchanged since T5a.

| Defect | Repair (short) | Target tranche | T4/T5a/T5b status |
| --- | --- | --- | --- |
| F01 owner identity omits the title | title in `OwnerTuple`/`SameOwner` + per-title adapters | T4 (pure), T6 (adapters) | pure part done + tested; adapters outstanding |
| F02 "level generation" guard is only the module generation | terminate on title change / None / generation-unavailable / Loading / Dead / Shell, arm `requiresRelease` | T5 (core) | **wired** in `vr.cpp` (`DurableLifecycleIdentifiesOwner` / `DurableRelationshipIdentityLost`, arm-before-clear); `Paused` retained; `Unsupported` treated as ambiguity and retained (documented); no runtime result yet |
| F03 `KnownAbsent` unreachable through the real resolver | accept stable absent-without-agreement; direct resolver tests | T4 (pure), T5b (H3 resolver), T6a (CE/H2), T6b (ODST/Reach), T7 (tests) | pure half done + tested; **H3 real-resolver half landed (T5b)**: `Halo3ReadOwnedWeapons` reports the raw role-byte-0xFF absence before its validation guards, `H3PrimaryWeaponEvidence` carries it through the shared guarded read, and `Game_ResolveSupportInvocation` calls `ResolveStableEvidence`, which requires the agreement bit only for KnownPresent. **CE half landed (T6a)**: the raw FP slot is preserved separately from validation (`hasFirstPersonUserRecord`/`weaponSlotPresent`) and mapped through `CeOwnerEvidence`; H2 landed through the raw-slot `primaryAbsentOut` of its guarded reader. **ODST/Reach halves landed (T6b)**: each reader reports its own raw primary role byte (ODST `+0x276`, Reach `+0x34A`) before validation through its own `primaryAbsentOut`, and `Game_ReadPrimaryWeaponEvidence` maps it to KnownAbsent without the FP agreement bit. **H4 half landed (T6c)**: `Halo4ReadMuzzleWeapons` reports the raw role byte `+0x63A` before its validation guards (including the explicit-`0xFF` early exit), the shared `Halo4WeaponOwnerEvidence` body carries it and the shared `Halo4ObservePrimaryWeapon` maps it to KnownAbsent; `Game_ReadPrimaryWeaponEvidence` gained the Halo 4 branch, so the shared tri-state reader covers every title. The direct live resolver test remains |
| F04 H4 trust resolved from the wrong record order | freeze semantic-primary trust at the storm-hands boundary, `candidate=UINT32_MAX` | T6 (H4), T7 (regression) | **wired (T6c)**: the pair's one owner-trust decision is frozen at its first safe FP invocation (the storm-hands decision boundary, before any palette consumes it) from the SEMANTIC PRIMARY (`candidate = UINT32_MAX`), never from the record's own object index; the storm record's unit handle is never offered as a weapon candidate. One terminal decision per pair through `UpdatePairResolution` (provisional Unknown publishes and leaves the pair open, Present/Absent terminal); the later held-weapon record only validates its exact object index against the frozen primary and can never retroactively redefine the hand solve. The pure suite (`tests/halo4_owner_evidence_tests.cpp`) carries the Storm(unit) -> held(weapon) regression; the runtime seam remains T7/headset |
| F05 H2 native aim updater not owner-qualified | owner-qualified controller-aim carrier, fail closed to `independentRightAimOrientation` | T6 (H2), T7 (tests) | **wired (T6a)**: `BuildPublishedControllerAimDirection` (the ALWAYS-ON native aim detour and the aim-assist direction) and the scope camera evaluate the frozen publication with `Halo2SupportCarrierMustDetach` and select the existing `independentRightAimOrientation` carrier on a failed read, a release discontinuity or an owner mismatch; PG off / unwired title keeps the base carrier selection. Runtime-only until T7/headset |
| F06 Reach consumer discards `preparedSerial` | require producer receipt AND current-invocation compatibility | T6 (Reach), T7 (first-B test) | **consumer half landed (T6b)**: `ReachBuildPresentedReticleWorldTarget` reads the ray's receipt and requires BOTH halves through `support_grip::PresentedRayConsumableForInvocation` - the ray's producer receipt (owner epoch, trusted support, F14 coherence, serial staleness bound) AND the consuming frame's title-native invocation receipt covering that generation/serial and still proving the same owner against the live relationship. Epoch equality alone is not accepted, serial equality is not required beyond the one-serial bound, and a refusal keeps the existing stock shot. The same receipt is published with both completed presented points and re-qualified by the seated shot-origin, on-foot hand-shot and vehicle redirect consumers. **T7-VER-01 F3 correction:** the seated shot-origin consumer now passes the completed rendered eye's prepared serial (the same frame whose occupation/freshness proof admits the shot, already proved non-zero by `ReachShotOriginSampleAdmitted`; the shot origin itself is the presented sight ray, R-V27); the on-foot hand-shot and vehicle-redirect sites keep the ray's own serial because no consuming prepared serial exists in that transaction, and the document records the resulting conservative latest-only-receipt behaviour instead of claiming a consumer-relative staleness bound the code does not have (section 3F). The A->B counterexample is unit tested in the pure suite; the first-B runtime test remains T7 |
| F07 reticle trust taken from cached durable evidence | derive ray trust from a title-native invocation receipt | T5b (plumbing, done), T6 (Reach, T6b) | **producer half landed (T5b)**: the reticle publication is filled from the receipt frozen by the solve (`VR_GetAimPoseWithSupportProvenance`) and the producer never re-reads live durable evidence. **T6b**: for a title that publishes a title-native invocation receipt (Reach), the reticle publisher additionally requires that receipt for the exact prepared serial (`VR_SupportInvocationReceiptTrustedForReticle`) before the ray may claim support trust; a missing/mismatched receipt publishes `supportTrusted=false` and reseeds the smoothing history. The receipt itself is produced by the Reach FP hook (`Game_ResolveSupportInvocation` material), not by a durable re-read |
| F08 fresh acquisition can bind stale evidence | monotonic revision + floor + pending intent | T4 (pure), T5 (wiring) | pure contract done + tested; **wired**: per-title monotonic revision published by the slot writer, the intent records the observed floor, binds need a strictly newer `KnownPresent`, and an intent may only begin from a coherent read (fail closed, logged). T5b adds the H3 producer that satisfies the "publish on every relevant invocation" obligation (root invocation of each stereo pair) |
| F09 H2 can retain stale evidence when no packet is built | publish an H2 observation that also runs unarmed | T6 (H2) | **wired (T6a)**: the level-live observer poll publishes the tri-state H2 evidence every frame while the level is live, independent of any weapon packet, reading the guarded direct reader (raw-slot absence included). The packet-build render hook is deliberately not a publication site (writer-side slot discipline documented in T5a); runtime-only until T7/headset |
| F10 `Game_ComputeAimStick()` unqualified live servo | shared bounded owner-trust receipt, force one-hand when unproven | T6 (titles; shared receipt exists but is not consumed here) | not started: the servo path still calls `VR_GetAimPose` outside any title gate (the receipt API landed in T5b, the consumption did not) |
| F11 interaction teardown bypasses `requiresRelease` | weaponGesture teardown while Grip is held must arm it | T5 (core), T7 (holster/swap test) | **wired**: `weaponGesture` arms the shared admission and routes the release through the writer; a pose outage retains instead of erasing; T7 holster/swap seam test outstanding |
| F12 toggle deferred acquisition is hold semantics | pending intent with toggle lifetime | T4 (pure), T5 (wiring) | **wired**: the writer consumes the pending intent as the acquisition request, with the toggle/hold lifetimes and the cancel policy above |
| F13 multi-writer odd/even evidence "seqlock" | per-title publication slots + real writer acquisition | T5 (core) | **wired**: per-title slot array, try-begin CAS writer acquisition, odd/even sequence, monotonic per-slot revision, reader rejects foreign/torn records; a lost writer race drops that publication |
| F14 epoch-0 ray mislabelled as ordinary one-hand | coherence flag; producer/consumer receipt; `SupportRayConsumable` epoch-0 rule | T4 (pure), T5b (assembly + producer), T6b (consumer + residual) | `PresentedRayOwnerCompatible` done + tested; the bind-side race is removed at the writer (snapshot published before the latch); **T5b**: the assembly now resolves the relationship coherence BEFORE selecting support-capable raw aim (`QualifySolveSupport`), never reports epoch 0 for an engaged/unreadable read, marks a denied/unreadable invocation untrusted, and the reticle publisher reseeds smoothing on that discontinuity. **T6b**: the Reach consumer receipt check landed (F06 row), the reticle publisher reseeds on a missing/mismatched title invocation receipt, and the residual is closed - `SupportRayConsumable` no longer short-circuits epoch 0: it takes the same `relationshipDisengagedCoherent` assertion as `PresentedRayOwnerCompatible`, so an epoch-0 ray while the relationship reads engaged, or under an unreadable snapshot, is refused. Pure suite covers both halves and the composition |
| F15 H2 snapshots fail open on a failed relationship read | distinguish disengaged / engaged / unavailable; unavailable fails closed | T6 (H2), T5 (snapshot API) | snapshot API landed (`VR_GetSupportGripRelationship`; false = unavailable, never "no relationship"); **consumer half landed (T6a)**: `Halo2SupportUse` distinguishes coherently disengaged / coherently engaged / unavailable, `BuildPublishedControllerAimDirection` (the always-on native aim detour), the aim-assist direction and the scope camera send every unavailable read to the existing `independentRightAimOrientation` carrier, and a readable disengaged relationship still detaches when the frozen publication carries support-derived geometry (section 3C). Runtime-only until T7/headset |
| F16 handedness teardown bypasses the writer | route role teardown through the canonical writer | T5 (core) | **wired**: the out-of-band latch clear is gated off while PG is on; the same frame reaches the writer as `rolesChanged`, which arms first and releases owner+epoch+publication together (PG off unchanged) |
| F17 weapon->unarmed producer liveness unproven | source-prove or instrument an absence producer per title | T5b (H3 source half), T6a (H2 poll), T6b (ODST/Reach source halves) + evidence candidate | **H3 source half landed (T5b)**: an explicit empty primary slot (native role byte 0xFF) resolves to `KnownAbsent` through the real resolver. **ODST/Reach source halves landed (T6b)**: each reports its own raw primary role byte (ODST `+0x276`, Reach `+0x34A`) before validation, so an explicit empty slot resolves to KnownAbsent without the FP agreement bit. **H4 source half landed (T6c)**: `Halo4ReadMuzzleWeapons` reports the raw primary role byte `+0x63A` before its validation guards, and the shared H4 owner body maps it to KnownAbsent without the agreement bit. Liveness is NOT proven for H3/ODST/Reach/H4 - absence is only observed if the engine still submits their first-person hook (H4: the per-pair model-skinning invocation) while unarmed - so the transition can stay Unknown until instrumentation answers it; H2's level-live poll is the only producer whose absence path is independent of a weapon invocation. Remains the final sign-off blocker |
| F18 the green suite misses the failing seams | seam/live tests + a further red team before package | T4 (pure suites), T7 (seams) | T4 suites landed; T5a added the policy-seam rehearsal (see section 3A); T5b added coverage for `QualifySolveSupport` and the receipt/smoothing consequences the runtime derives from it; T6a added the applicability predicate and the ported CE matrices; T6b added the invocation-receipt coverage matrix, the F06 consumer composition (including the confirmed A->B counterexample), the closed epoch-0 rule and the Reach/ODST predicate rows; T6c added the F04 record-identity regression (Storm unit handle -> held weapon handle with a frozen pair), the all-six applicability rows, the H4 raw-absence reader rows and the prepared one-hand fallback field determinism. The title hook seams (ODST/Reach/H4 pair freeze and target substitution, the receipt publication/read seqlock, the Reach consumer ordering) and the publication protocol remain runtime-only until T7 |

**N1/N2 (novel-surface requirement) - handled in T5a.** An owner-break
invalidation or a lifecycle/generation replacement additionally calls the
existing `InvalidateAimContinuityLayer()` and `InvalidateTwoHandLabTemporal()`
in `vr.cpp`, so a proven owner discontinuity cannot leave either presentation
layer easing or re-presenting geometry derived from the previous owner. An
ordinary release deliberately does not invalidate them (the release edge keeps
its existing, headset-tuned behaviour). This is a runtime-only claim until the
headset gate; the writer integration is covered by
`tools/check_aim_continuity_lifecycle.py`, whose documented topology grew from
"definition plus eight call sites" to "definition plus nine" for exactly this
path (the rule was updated deliberately, not bypassed).

## 5. Tranche map

- **T4 - foundation.** Pure policy headers with repairs A1-A4, three standalone
  test targets, the inert config key and F1 checkbox. No runtime consumer.
- **T5a (this tranche) - core durable writer.** The single durable writer in
  `vr.cpp`: per-title evidence publication slots (F13), the lifecycle-identity
  termination (F02), owner-break-before-acquisition, the pending-intent
  consumption (F08/F12), weaponGesture and role teardown routing (F11/F16), the
  pose-outage retention, the relationship/latch publication order (F14 bind
  race) and the N1/N2 invalidation. Covers the runtime halves of F02, F08, F11,
  F12, F13, F16.
- **T5b (this tranche) - invocation trust + first title slice.** The
  thread-local invocation gate, the solve-time support receipt and the
  coherence-aware assembly qualification; the presented-ray receipt filled at
  the producer; the Halo 3 producer/trust/ordering/cache/presentation slice.
  Covers the producer-side halves of F03/F07/F14 and the H3 half of F17.
- **T5b remainder (moved to T6).** The residual `SupportRayConsumable`
  coherence qualification (F14) and shared receipt plumbing for
  `Game_ComputeAimStick` (F10) still need the consuming invocation.
- **T6a - applicability gate + CE/H2.** See section 3C. Covers the CE/H2 halves
  of F03 and F09, the H2 halves of F05/F15 and the A0 rollout gate.
- **T6b - ODST + Reach + the Reach presented-ray consumer.**
  ODST and Reach producers/trust/freeze/presentation with their own readers and
  timing; the Reach stale-owner one-hand fallback; the title-native invocation
  receipt; `PresentedRayConsumableForInvocation` (F06) and its re-qualification
  at every live weapon-sensitive consumer; the reticle smoothing discontinuity
  (F14/section 10) and the closed `SupportRayConsumable` epoch-0 rule. Covers
  the ODST/Reach halves of F03/F17 and the consumer halves of F06/F07/F14.
- **T6c - Halo 4 (F03/F04), all six titles wired.** See section
  3E. The H4 production owner-evidence producer (shared body, raw
  primary-absence), the storm-hands decision-boundary trust freeze with the A003
  pair policy and the held-record validation, the owner-safe prepared one-hand
  fallback and the presentation; `PersistentSupportGripApplies` now covers every
  gameplay title. Covers the H4 halves of F03/F17 and F04.
- **T7 corrections (this tranche) - T7-VER-01 findings F1-F5.** See section 3F.
  The A003 acquisition predicate wired behind the PG gate (F1), the Reach
  presented-ray serial-bound resolution (F3), the aim-continuity guard message
  (F4), the evidence-publication comment (F5), and the new
  `tools/check_persistent_support_grip.py` /
  `halomccvr_persistent_support_grip_guard` source guard that pins the config,
  gate, base-store, reseed/owner-break and anti-donor invariants (F2).
- **T7 - seam tests, red team and acceptance.** Live/seam regression suites
  for the actual failing seams (F18), a repeat red team, offline evidence
  packaging, then headset acceptance. Still open: the F10 servo, the F01
  adapter halves, the serial-scoped publication of the frozen pair trust into
  the frame-thread aim assembly, the H4/ODST/Reach absence liveness
  instrumentation (F17), and the H2 F15 runtime proof.

## 6. Guardrails

- **PG off equals base behaviour.** The toggle has no consumer in T4; when it
  is off the build must behave exactly like PR16 + the Two-Handed Lab patch.
  Any wiring tranche must preserve that equality and prove it. T5b evidence:
  every new path is behind `g_config.persistent_support_grip` (qualification
  short-circuits before any durable read, the H3 hook resolves nothing, the
  aim gate is never set, the palette keeps `twoHandAimActive`, the cache key
  is constant within a pair, and the reticle receipt is all-zero so the
  smoothing expression is the base one). T6a/T6b audits are in sections 3C and
  7C: the CE/H2 helpers short-circuit, and ODST/Reach publish nothing, freeze
  nothing, build no fallback target and leave every receipt/consumer expression
  at its base value. T7 evidence (section 3F): the acquisition geometry is
  gated - the base latch keeps the pre-fix `TwoHandGrabZoneHit` verbatim while
  only the wired writer runs the A003 predicate - and
  `halomccvr_persistent_support_grip_guard` pins the default-off config, the
  gate consultations, the base stores and the single reseed/owner-break pair at
  source level.
- **No new Virtual Stock mode, aim solver or inventory framework.** The
  existing solve, hold equation and admission state are extended, not
  replaced. `UpdateTwoHandHold` keeps its current semantics; no
  retained-distance release check is added.
- **No optional-hook dependencies.** Optional muzzle/dual-wield hooks and
  `camscan` are not correctness dependencies; absent hooks leave stock
  behaviour stock.
- **No dead-code revival.** Dormant/rejected paths (for example H2's old
  weapon-aim-helper and presented-reticle experiments, H4
  `Halo4PlaceFirstPersonHands`) stay dormant. Disabling a failed experiment is
  preferred to deleting understood code.
- **No sway reintroduction.** Nothing in this port may reactivate the retired
  head-turn sway correction / inverse-neck product path
  (`docs/VIRTUAL-STOCK-HEAD-TURN-SWAY-REMOVAL-2026-09-27.md`).
- **Fail-open isolation.** The feature is its own transaction: a failure
  degrades that feature to stock and logs loudly, naming the path. It never
  disarms the camera core, ends the OpenXR session, or gates arming.
- **Loud logs, real evidence.** Zero/multiple signature matches block that
  hook only. No theory is recorded as a finding; runtime-only claims wait for
  headset/targeted instrumentation.

## 7. Verification recorded for T4

- `cmake --preset release`, then `cmake --build --preset release`: full
  Release build green (`HaloMCCVR.dll` and every test executable link, 0
  errors).
- The three new executables: support grip logic 141 checks / 0 failures;
  support grab 69 checks / 0 failures; H4 owner evidence 88 checks / 0
  failures.
- `ctest --preset release`: 98/98 tests passed, including the existing
  `halomccvr_core_tests`, `halomccvr_virtual_stock_tests`, the Two-Handed Lab
  suites, the aim-continuity lifecycle guard and the Two-Handed Lab patch
  guard.
- Config round-trip evidence from a development-only harness inside the
  worktree (untracked, `.scratch/pg-config-roundtrip/`):
  default false; parse `= 1` enables; save emits the key and its generated
  comment; parse `= 0` disables; an out-of-range value keeps the previous
  behaviour; a file without the key keeps the default.

## 7A. Verification recorded for T5a

- `cmake --build --preset release`: full Release build green (`HaloMCCVR.dll`
  and every test executable link, 0 errors, no new warnings).
- `ctest --preset release`: 98/98 suites passed, including
  `halomccvr_aim_continuity_lifecycle_guard` (topology rule updated for the new
  N1/N2 call site, documented above) and `halomccvr_two_hand_lab_patch_guard`.
- `halomccvr_support_grip_logic_tests`: 192 checks / 0 failures (was 141). New
  coverage: F02 lifecycle identity (`Gameplay`/`Paused`/`Cutscene`/`Vehicle`/
  `Turret` valid, `Shell`/`Loading`/`Dead` terminate, `Unsupported` retained,
  unavailable title/generation rejected), the writer seam rehearsal (arm before
  clear on lifecycle/role/gesture teardowns, owner break before a bindable
  acquisition request, pose-outage suspension then recovery), the four pending
  -intent outcomes (toggle click surviving the button-up; hold-mode
  release-before-reacquire; the first click after a break binding B; a break
  while the click is still held behaving like hold), and the F08 revision-floor
  regression including the foreign-title mismatch.
- `halomccvr_two_hand_lab_patch_guard` and the regenerated aim-pose/grip-pose/
  d-pad fixtures rebuilt against the changed `vr.cpp`: the extraction tools take
  named functions, and the acquisition-zone geometry moved verbatim into
  `TwoHandGrabZoneHit` for the base latch path. (T5a note superseded by the T7
  corrections: the writer no longer shares that helper - it runs the A003
  predicate via `PersistentSupportGrabZoneHit`, both extracted into the
  grip-pose fixture and asserted by `halomccvr_grip_pose_capture_tests`.)
- PG OFF parity: the base gate/stores/toggle/hold path is unchanged apart from
  the verbatim geometry extraction and the dormant-state retire call; with the
  toggle off no durable state is consulted for any latch decision.
- Not yet verified anywhere: any end-to-end runtime effect of the toggle (with
  no title producer, PG ON can never bind an owner), the per-title seams, the
  consumer/receipt rules (T5b/T6), and headset behaviour. Headset acceptance
  remains the gate for every behavioural conclusion in this port.

## 7B. Verification recorded for T5b

- `cmake --build --preset release`: full Release build green (`HaloMCCVR.dll`
  and every test executable link, 0 errors, 0 new warnings).
- `ctest --preset release`: 98/98 suites passed, including
  `halomccvr_aim_continuity_lifecycle_guard`, `halomccvr_two_hand_lab_patch_guard`,
  the regenerated `halomccvr_aim_pose_tests`, `halomccvr_two_hand_lab_solver_tests`,
  `halomccvr_two_hand_lab_temporal_tests` and `halomccvr_virtual_stock_aim_*`.
- `halomccvr_support_grip_logic_tests`: 204 checks / 0 failures (was 192). New
  coverage: the solve-time qualification matrix (feature off = no read/forcing/
  receipt; coherently disengaged = ordinary one-hand with epoch 0; engaged +
  proven = may consume; engaged + denied = one-hand but epoch retained;
  unreadable = one-hand with the unknown-epoch marker; engaged-with-zero-epoch
  never reads as disengaged) and the receipt consequences the producer relies
  on (the coherence-aware ray acceptance refuses denied/unreadable receipts,
  and the smoothing rule reseeds for a denied invocation while a trusted
  same-epoch solve keeps smoothing).
- The aim-pose fixture regenerated from the changed shipping source (the new
  receipt fields are plain scalars, so the extracted solver still compiles
  without the support-grip header and the fixture tests exercise the shipping
  body).
- **PG OFF parity, source-level audit.** Every new runtime path is behind
  `g_config.persistent_support_grip`: `ResolveAimSupportQualification`
  returns the default qualification before any durable read; the H3 FP hook
  resolves and publishes nothing; the aim gate is never set; the palette keeps
  `twoHandAimActive` (and `ShouldApplyArmIk(arm_ik, twoHandAimActive)` is the
  old call); the added cache-key term equals the latched per-pair aim flag, so
  it cannot introduce a new miss; `Halo3ReadOwnedWeapons`' new out-parameter
  is defaulted and never dereferenced by existing callers; and the reticle
  receipt is all-zero/false, which makes the discontinuity expression the base
  one. PG-off equality is a code-path argument, not a headset result.
- **Still runtime-only (no offline seam test in this tranche):** the H3
  producer/trust ordering inside `FpInterpolateHook`, the per-pair frozen trust
  and the palette cache key, the reticle publication/receipt seqlock, and the
  `Game_ComputeAimStick`/Reach consumer halves. These are exactly the T7 seam
  tests plus the headset gate.

## 7C. Verification recorded for T6b

- **Step 0 (T6a continuation), before any T6b edit:** `cmake --build --preset
  release` green and `ctest --preset release --output-on-failure` **98/98**
  passed with the T6a tree including its final doc edits. The F15 ledger row was
  brought up to date with the landed T6a H2 wiring and the T6a completion note
  above was added; no source was changed in Step 0.
- `cmake --build --preset release` (final T6b source): full Release build
  green (`HaloMCCVR.dll` and every test executable link, 0 errors).
- `ctest --preset release --output-on-failure`: **98/98** suites passed,
  including both guard suites (`halomccvr_aim_continuity_lifecycle_guard`,
  `halomccvr_two_hand_lab_patch_guard`), the CE runtime/palette fixtures and
  `halomccvr_reach_muzzle_tests`/`halomccvr_odst_muzzle_tests` (the two readers
  whose signatures gained the optional absence out-parameter).
- `halomccvr_support_grip_logic_tests`: **234 checks / 0 failures** (was 212).
  New coverage: the invocation-receipt coverage matrix (own serial and the next
  one accepted; two serials old, a newer-than-consumer serial, a foreign
  generation or title, an unresolved receipt and any zero serial refused); the
  full F06 consumption composition (healthy trusted same-owner ray accepted; the
  confirmed stale-A/consuming-B counterexample refused while the durable reading
  is still A/42; an untrusted or unreadable invocation refused; ray and receipt
  serial staleness bounds; replaced relationship epoch; ordinary epoch-0 ray
  under a coherent disengagement accepted and refused while the relationship
  reads engaged or unreadable); the closed `SupportRayConsumable` epoch-0 rule
  (its own positive/negative rows); and the predicate rows (ODST+Reach wired,
  H4 not; only Reach publishes an invocation receipt).
- **PG OFF parity, source-level audit.** Every T6b path is behind
  `VR_SupportGripWiredForTitle` (false whenever `persistent_support_grip` is
  off) or its guarded call site:
  - ODST/Reach take no before/after owner read, publish no evidence and no
    invocation receipt (`supportRootInvocation` requires the config flag), so
    the durable writer, the assembly qualification and the reticle smoothing
    expression all see their base inputs;
  - the Reach one-hand fallback target is not built, `supportGripResolved` stays
    false, so `twoHandAimActive` remains the only presentation input, the
    palette's left-hand binding call is unchanged, the target/wrist/collision
    fields are byte-identical, and `ReachCaptureFpInterpolation` takes no
    substitution branch;
  - `VR_SupportInvocationReceiptTrustedForReticle` returns true before any read
    for an unwired title, so the reticle publisher's `supportTrusted` and the
    published receipt are exactly the base ones and the smoothing expression is
    the base expression;
  - `ReachPresentedRayConsumable` returns true for an unwired title, so
    `ReachBuildPresentedReticleWorldTarget` and the three shot consumers keep
    their existing paths, and the added snapshot fields read 0/false.
  PG-off equality is a code-path argument, not a headset result.
- **Still runtime-only (no offline seam test in this tranche):** the ODST pair
  freeze inside the real FP hook and its cache-key consequence, the Reach pair
  scope freeze and one-hand target substitution order, the invocation-receipt
  publication/read seqlock, and the Reach reticle consumer ordering against the
  real composition/prepared-serial sequence. No test target includes
  `odst_muzzle_ownership.inl`, `reach_muzzle_ownership.inl` or the Reach FP
  hook. These are the T7 seam tests plus the headset gate.
- **Deliberately not claimed:** the Reach residual above (the frame-thread
  assembly still solves from the durable relationship, so the compositor's pose
  on an owner-untrusted serial is support-steered; it is published untrusted,
  refused by every consumer and never bridged into a later smoothing history).
  The Halo 4 slice, `Game_ComputeAimStick` and the F01 adapter halves remain
  unwired. *(Historical: the Halo 4 slice landed in T6c - section 3E.)*

## 7D. Verification recorded for T6c

- **Step 0, before any T6c edit:** `cmake --build --preset release` green and
  `ctest --preset release --output-on-failure` **98/98** passed on the T6b tree.
- `cmake --build --preset release` (final T6c source): full Release build green
  (`HaloMCCVR.dll` and every test executable link, 0 errors, 0 new warnings).
- `ctest --preset release --output-on-failure`: **98/98** suites passed,
  including both guard suites (`halomccvr_aim_continuity_lifecycle_guard`,
  `halomccvr_two_hand_lab_patch_guard`), `halomccvr_core_tests` and the H4
  suites whose sources gained a parameter or a branch
  (`halomccvr_halo4_muzzle_tests`, `halomccvr_halo4_vehicle_input_tests`).
- `halomccvr_halo4_owner_evidence_tests`: **101 checks / 0 failures** (was 88).
  New coverage: the F04 record-identity regression (Storm objectIndex = unit
  handle -> held objectIndex = the relationship's weapon handle; the storm
  handle is proven not to map to equipped weapon slot 0; the boundary candidate
  is the semantic primary; only the boundary may resolve; a unit-handle
  candidate read would leave the pair open; semantic-primary trust freezes
  Present; the held record validates only and can never redefine Present or
  re-attach Absent; the second eye reuses the frozen decision).
- `halomccvr_support_grip_logic_tests`: **235 checks / 0 failures** (was 234).
  New coverage: Halo 4 is a wired title and all six gameplay titles are wired.
- `halomccvr_halo4_muzzle_tests`: **6012 checks / 0 failures** (5 new rows):
  the raw primary role byte `0xFF` reports primary-absent through the new
  out-parameter while an invalid role, a foreign-owned weapon, an unreadable
  unit and a complete inventory never do. The fixture includes the shipping
  `halo4_muzzle_ownership.inl`.
- `halomccvr_halo4_vehicle_input_tests`: **30 checks / 0 failures** (3 new
  rows): `Halo4ReadVehicleInputEx` admits the same owned state with no reason
  and names the exact rejection exit (`camera`, `title`); the shipping wrapper
  and its unwind metadata are unchanged (the fixture's
  `RtlLookupFunctionEntry` check still passes).
- `halomccvr_core_tests`: passed, including the new Halo 4 prepared-snapshot
  rows (an unfilled one-hand fallback is not a usable pose; a filled one
  survives the snapshot copy bit-exactly).
- **PG OFF parity, source-level audit.** Every T6c path is behind
  `VR_SupportGripWiredForTitle(GameTitle::Halo4)` (false whenever
  `persistent_support_grip` is off):
  - the detour's producer/pair-freeze block and the held-record validation take
    no read, publish no evidence, freeze no pair and add no log;
  - `Halo4BeginFloatingPair` keeps the exact base seeding of
    `supportGripAttached` (`twoHandAimActive`) and builds no one-hand carrier;
  - `Halo4BuildFloatingHandsPalette` keeps `rightTargetWorld` and the exact
    `twoHandAimActive` branch, so the solved geometry is byte-identical;
  - `PublishHalo4RenderSnapshot` runs no extra solve and leaves both new fields
    at their falsy defaults, and no consumer reads them;
  - `Halo4ReadMuzzleWeapons`' absence out-parameter is defaulted, and the
    reader's new raw-role check reuses the `roles[0]` byte it already loaded;
  - the vehicle-input split into `Halo4ReadVehicleInputEx` plus the shipping
    wrapper preserves the exact checks, order, counters and drainable unwind
    metadata of the original single function.
  PG-off equality is a code-path argument, not a headset result.
- **Still runtime-only (no offline seam test in this tranche):** the producer,
  the pair freeze and the palette substitution inside the real Halo 4
  model-skinning detour, the prepared one-hand field against a live sample, the
  durable slot publication from this seam, and every headset behaviour. No test
  target includes `Halo4ModelSkinningDetour`. These are the T7 seam tests plus
  the headset gate.
- **Deliberately not claimed / recorded residuals:**
  - H4 weapon->unarmed absence liveness is source-provable but NOT proven, the
    same class as H3/ODST/Reach (F17): absence is only observed if the engine
    still submits the per-pair first-person invocation while unarmed.
  - The optional world-collision transaction publishes its initial target at
    pair begin, before the per-record trust decision, so on an untrusted pair
    that initial target is still the two-hand carrier while the visible
    carriers use the frozen one-hand fallback; the authored hand/weapon volume
    publications follow the solved palettes. Recorded, not hidden.
  - `Game_ReadPrimaryWeaponEvidence(GameTitle::Halo4)` is the shared tri-state
    API's Halo 4 coverage; the live producer path is
    `Halo4ReadSupportInvocation` -> `Halo4ObservePrimaryWeapon` -> the shared
    `Halo4WeaponOwnerEvidence` body.
  - Halo 4 publishes no title-native invocation receipt (it has no presented-ray
    consumer in this port), so its reticle keeps the solve-time receipt
    semantics the T5b publisher provides.

## 7E. Verification recorded for the T7 corrections tranche

- **Step 0, before any edit:** `cmake --build --preset release` green and
  `ctest --preset release --output-on-failure` **98/98** passed on the
  T7-VER-01 tree.
- `cmake --build --preset release` (final tree): full Release build green
  (`HaloMCCVR.dll` and every test executable link, 0 errors, 0 new warnings).
  Two intermediate link errors during development (the fixture's `Rotate` and
  `Config` dependencies) were fixed in the candidate tree by ordering the
  fixture include after the stub rotate and by standing in only the two config
  members the extracted helpers read; the final tree builds from clean source.
- `ctest --preset release --output-on-failure`: **99/99** suites passed (was
  98), including `halomccvr_aim_continuity_lifecycle_guard`,
  `halomccvr_two_hand_lab_patch_guard`, the new
  `halomccvr_persistent_support_grip_guard` (test #32) and
  `halomccvr_grip_pose_capture_tests` (#44).
- `tools/check_persistent_support_grip.py --self-test`: the self-test passed
  (25 failing synthetic fixtures plus the clean-tree assertions, covering every
  check group) and the real tree is clean.
- `halomccvr_grip_pose_capture_tests`: **45 checks / 0 failures** (was 33; the
  seam test adds 12). It now compiles BOTH shipping acquisition helpers out of
  `vr.cpp` and proves the F1 seam on frozen geometry.
- `halomccvr_support_grab_tests`: **69 checks / 0 failures** (pure suite kept
  unchanged). `halomccvr_support_grip_logic_tests`: **235 checks / 0 failures**
  (unchanged).
- **PG OFF parity, source-level audit for this tranche.** The only runtime
  behaviour change is at the durable writer's acquisition edge, which is behind
  `VR_SupportGripWiredForTitle()`; PG off and unwired titles keep the exact
  base latch path and the exact base `TwoHandGrabZoneHit` geometry (verbatim
  body, with a seam test asserting it still equals the pre-fix arithmetic for
  every modelled wrist). The Reach consumer change only replaces one function
  argument at the seated site, inside the `!VR_SupportGripWiredForTitle(title)`
  early-return-guarded consumer. FIX-3/FIX-4/FIX-6 are comments/messages, and
  FIX-5 is an offline guard; none changes runtime behaviour. The guard asserts
  the base stores, the gate consultations and the single reseed/owner-break
  pair at source level. PG-off equality remains a code-path argument, not a
  headset result.
- **Still runtime-only (unchanged by this tranche):** the Reach seated
  shot-origin consumer's new consuming serial, the two documented conservative
  consumer sites, the A003 acquisition predicate inside the live writer, every
  title hook seam and every headset behaviour. These wait for the T7 seam
  instrumentation and the headset gate.

## 7F. Verification recorded for T18 (weapon-switch policy)

- **Step 0 (declared by the mission capsule):** HEAD `83c8428`, worktree clean,
  `ctest --preset release` 99/99.
- `cmake --build --preset release` (final tree): **green, exit 0** for the full
  cumulative Release build (`HaloMCCVR.dll` plus every test executable). The
  captured log (`out/build-release-switch-policy.log`) contains no compiler
  errors or warnings; an earlier pass of the same command was re-run after the
  last test-file edit so the final binaries are built from the final sources
  (the rebuilt `halomccvr_support_grip_logic_tests.exe` contains the final
  message text and not the replaced one).
- `ctest --preset release --output-on-failure`: **98/99 passed** on this run
  (`out/ctest-release-switch-policy.log`). The single failure,
  `halomccvr_installer_tests` (#16), is an **environment refusal, not a
  regression**: the user's MCC is still running
  (`MCC-Win64-Shipping.exe`, pid 24352, started 11:35:43 - the 11:37-11:38
  headset session), and `GameRunning()` (`src/launcher/installer.cpp`) refuses
  the synthetic install by design, so
  `Require(first.success, "first synthetic install")` fails. That test target
  compiles only `tests/installer_tests.cpp` + `src/launcher/installer.cpp`,
  neither of which this tranche touches; the game process is deliberately never
  killed by this tooling. Every other suite passed, including #21
  `halomccvr_core_tests`, #24 `halomccvr_support_grip_logic_tests`, #30
  `halomccvr_aim_continuity_lifecycle_guard`, #31
  `halomccvr_two_hand_lab_patch_guard`, #32
  `halomccvr_persistent_support_grip_guard` and #44
  `halomccvr_grip_pose_capture_tests`.
- `halomccvr_support_grip_logic_tests`: **254 checks / 0 failures** (was 235;
  the new `TestWeaponSwitchPolicy` function adds 13 checks, and the rewritten
  replacement/absence/intent expectations replace, not delete, the old arm
  assertions, for a net +19).
- `halomccvr_core_tests`: passed, including the new `two_hand_switch_inherit`
  config round trip (default written off, `ConfigSave` -> `ConfigLoad`
  persistence).
- `tools/check_persistent_support_grip.py --self-test`: passed; the real tree
  is clean and the synthetic fixtures now number **48** failing fixtures (39 +
  the 9 new switch-policy ones) plus the three clean-tree assertions.
- **PG OFF parity argument (unchanged in kind):** the switch policy lives
  entirely in `DurableWriterStep`, which the caller invokes only inside
  `UpdatePersistentSupportGrip`; that function is called only from
  `UpdateTwoHandLatch` inside the `VR_SupportGripWiredForTitle(...)` branch
  (`persistent_support_grip && PersistentSupportGripApplies(title)`), with the
  dormant-state retire and the verbatim base latch path after it. With PG off
  the new config field is read only by the menu and the config writer, the
  policy function is never reached, and the base toggle/hold stores and the
  base `TwoHandGrabZoneHit` geometry are untouched (the guard pins all of it).
  **VS-on unchanged:** the option only decides who owns the durable
  relationship; the assembly qualification, the VS-on target selection and the
  Hybrid offhand call are untouched, and `supportSteeringRetained` still comes
  from `supportQualification.supportTrusted` exactly as before (guard group 8).
- **Still runtime-only:** the headset confirmation of the new default
  re-acquire feel and of the inherit option, the remaining titles' switch
  behaviour, and every conclusion that depends on the live engine.

## 7G. Verification recorded for T19 (default-switch re-grip UX)

- HEAD at the start of this tranche: `a718e53` (the T18 candidate), worktree
  clean.
- `cmake --build --preset release`: **green, exit 0** for the whole cumulative
  Release build (`HaloMCCVR.dll` plus every test executable); the captured log
  (`out/build-release-zone-reentry.log`) contains no compiler errors or
  warnings.
- `ctest --preset release --output-on-failure`: **99/99 passed** (21.41 s real;
  `out/ctest-release-zone-reentry.log`). MCC was not running during this run, so
  `halomccvr_installer_tests` exercised its synthetic install normally - no
  ENVIRONMENTAL exclusion was needed.
- `halomccvr_support_grip_logic_tests`: **276 checks / 0 failures** (was 254).
  The new `TestDefaultSwitchZoneReentry` adds 22 checks: the drop that arms the
  requirement and the five transitions that do not (not engaged, `Bind`,
  `Released`, absence/no-different-owner, lifecycle identity lost, and the
  inherit option), the blocked held/in-zone update and its continuation while
  the zone stays true, the FALSE reading as the crossing (not itself an
  acquisition), the re-enabled edge afterwards, the unchanged "hold mode needs
  the physical Grip" rule, and a seam rehearsal that starts from a bound owner,
  drops it without arming, is refused while the hand stays in the zone, begins
  the ordinary hold-mode intent on the crossing, passes the F08 revision floor
  and binds the switched owner **with no release**.
- `tools/check_persistent_support_grip.py --self-test`: passed; the real tree is
  clean and the synthetic fixtures now number **62** failing fixtures (48 + the
  14 new crossing-requirement ones) plus four clean-tree assertions. The new
  check group 11 pins the default-false flag, the single arm site (exact
  predicate call + the set statement), the single-armed-transition count, the
  `StepSwitchZoneReentry` call and its state store, the hold-mode edge consuming
  the step result (with the old ungated `(gripHeld && inZone)` form rejected),
  the untouched toggle edge, the teardown/explicit-release/retire/inherit clear
  sites, and the pure survival/edge/arm-rule text.
- **PG OFF parity argument (unchanged in kind):** `awaitZoneReentry` is a member
  of `PersistentSupportGripState`, which only `UpdatePersistentSupportGrip`
  touches; that function is called only from `UpdateTwoHandLatch` inside the
  `VR_SupportGripWiredForTitle(...)` branch
  (`persistent_support_grip && PersistentSupportGripApplies(title)`), with the
  dormant-state retire and the verbatim base latch path after it (guard group 4
  still pins every base store and the base geometry). With PG off the flag is
  never read, the pure helpers are never called, and `StepSwitchZoneReentry`
  with the flag clear is exactly the old `gripHeld && inZone` rule, so PG-off
  and unwired-title behaviour is bit-for-bit the previous rule.
- **Inherit semantics unchanged:** `DurableWriterStep` is not modified by this
  tranche (guard group 10 still pins its two `requiresRelease` arm sites and the
  option-gated in-place rebind), `DefaultSwitchReplacementDrop` is false
  whenever `switchInherit` is set, and the only option-path edit is the
  defensive clear in the inheritance-rebind branch, which can only clear an
  already-clear flag (the option never sets it).
- **VS-on unchanged:** nothing in the assembly, the presentation or the
  virtual-stock selection paths was touched (guard group 8 still pins the
  single `supportSteeringRetained` writer, the VS-on target selector signature
  and the Hybrid offhand call).
- **Still runtime-only:** the headset confirmation of the corrected default
  switch feel (no pre-hold; a fresh zone crossing engages, with no release
  cycle), the option-on regression, and the same behaviour on the other five
  wired titles.
