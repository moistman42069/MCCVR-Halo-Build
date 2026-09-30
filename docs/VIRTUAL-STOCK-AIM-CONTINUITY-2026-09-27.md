# Virtual Stock grab/release aim continuity (product)

Status: **product behaviour. The continuity law below was accepted in a headset
A/B on 2026-09-27 (T1, fixed 200 ms smoothstep). T0 (hard switch), T2 (speed-5),
T3 (motion gate) and T4 (persistent calibration) were rejected and removed
entirely: there is no selector, no config key and no experiment surface left.
Nothing here changes the accepted-build pointer in `docs/CURRENT-STATE.md`;
headset acceptance of the packaged candidate is still the gate.**

## September 28 extension: VS-OFF latch transition and the shared Two-Hand Smoothing filter

The VS Standard/Plus law above remains unchanged. The same fixed 200 ms
latch-edge law applies to VS-OFF two-hand presentation as **fixed-on internal
product behaviour**: the former `two_hand_transition_smoothing` toggle was
retired on 2026-09-29 and no longer resolves (a historical `halomccvr.cfg`
containing the key still loads quietly, but nothing reads the field and the
generated config no longer writes it). With VS off, only durable latch
false-to-true / true-to-false edges seed the transition; stock-solve ownership,
B acceptance and persistent grip geometry do not start or restart it. Grab
seeds from the same-frame one-hand counterfactual, release holds the prior
presentation, and aim position is never changed. A
lifecycle/title/generation/reference-space reset still fails open and cannot
carry a transition into a new owner/session.

The independent `two_hand_smoothing_strength` product control defaults to 0
(off). For an applicable latched two-hand solve - Virtual Stock on or off -
controller-input copies first pass through the fixed Pavlov-inspired response
`alpha = clamp(25 * dt, 0, 1)`: orientation is shortest-arc quaternion SLERP and
the specified position copies use linear interpolation. The user strength is
then a wet/dry mix over that unchanged full-filter output: 0 emits raw input
untouched, 25 emits the full-filtered result untouched, and every value between
blends positions linearly and orientations along the shortest arc. From
2026-09-29 it also feeds the Virtual Stock Standard/Plus solve: that solve
consumes the same smoothed directional copies (primary orientation,
primary/support aim positions, primary/support grip positions), so `0` is the
pre-change raw Virtual Stock solve bit-for-bit and any higher value moves it.
The filter never alters raw acquisition/latch samples, one-hand solves, the
weapon/base position, the VS-OFF-only Grip -> Grip product geometry, the
offhand-influence authority or the Two-Handed Lab. It is a controller-input
filter with a mix amount, never a user-visible interpolation speed and not a
second Lab continuity mode. The Lab's constant/adaptive damping remains
separately selectable and engages only on frames the product seam does not own
(Lab enabled, resolved Virtual Stock off, `two_handed_aim` off:
`TwoHandLabTemporalEngagedFor`), so it never stacks a second 200 ms correction.
Headset acceptance of these candidate additions is pending.

One accepted consequence of the wider scope: the smoothing packet carries no
stock-mode identity, so a Standard <-> Plus toggle mid-hold keeps the filter's
history instead of reseeding. Telemetry reports the consumption it actually
made (`two_hand_smoothing_applied`, `two_hand_smoothing_strength`), not the
mode that produced the copies.

Scope: Virtual Stock **Standard (rear reference 0/1/2) and Plus (rear reference
3)** with `two_handed_aim` on. Ordinary non-Virtual-Stock two-hand aim is
untouched by the 200 ms continuity law, and the Two-Hand Smoothing control spans
both stock modes as described above. The solver, stock strength, the W/seat
thresholds, horizontal release, the rear-reference construction, the head-turn
sway correction (retired from the product 2026-09-27; see
`docs/VIRTUAL-STOCK-HEAD-TURN-SWAY-REMOVAL-2026-09-27.md`), support endpoint
selection, grab admission, controller smoothing, weapon tuning, handedness and
per-weapon tuning are untouched by this layer.

Explicitly separate (not part of this feature): the grip-zone authoring
rework and the ODST freeze investigation.

## 1. The problem

When the support grip latches, the live solve replaces the one-hand calibrated
aim orientation with the stocked/two-hand calibrated orientation in a single
frame. Measured on the bench, the two orientations differ by roughly 3-60
degrees depending on mode and how the player is holding the weapon. Releasing
the grip produced the mirror-image jump. Halo 3's headset-confirmed experience
is the reference for every title: the aim the player is already using must not
visibly snap, in either direction.

## 2. What the layer does

A presentation-only orientation correction is layered **after** the live
fully-calibrated solve:

```text
presented = live(that frame) (x) correction
correction = inverse(live) (x) anchor        [seeded on the edge serial]
```

with the same Hamilton product and local frame `finishAimPose` uses. The
composition is outside the solver, so:

- on the latch serial `presented == the same-frame one-hand counterfactual`
  exactly (up to float rounding), because the anchor is computed from the
  one-hand solve of the *same* inputs, so the latch itself adds ~zero
  orientation change;
- on the un-latch serial `presented == the orientation this layer last
  presented`, so the release does not jump back either;
- the target is always the **live orientation of the frame being presented**,
  never a frozen seed: `presented = live(that frame) (x) correction`;
- the correction is never fed back into solver inputs (no double calibration);
- the aim **position** is never modified: only the emitted orientation changes.

### Continuity law

The accepted law is one fixed 200 ms smoothstep for BOTH acquisition and
release:

```text
t = clamp(elapsed / 0.200, 0, 1)
u = t * t * (3 - 2 * t)
correction = shortestArcSlerp(initial_correction, identity, u)
```

The transition completes to exact identity at `elapsed >= 0.200 s`, so
presented == live exactly from then on. `dt` is the prepared-frame display
delta; a non-positive or unavailable delta is a no-op and the prepared serial
stays unconsumed.

### Contract details

- **Acquisition seed.** On the grab serial the correction is seeded from the
  same-frame one-hand counterfactual (identical inputs with
  `twoHandLatched=false`, so per-gun calibration is identical).
- **Release seed.** On the un-latch serial the layer anchors to the orientation
  it actually presented on the previous valid serial (`lastPresented`) against
  the live one-hand solve, then runs the same 200 ms smoothstep back to live.
- **Rapid reversals.** A grab while a release is still easing (or a release
  while an acquisition is still easing) re-anchors to what the player is
  currently seeing — either the caller-provided last presented orientation or,
  when that is unavailable, the module's own running correction. The ease is
  then re-seeded from that running correction, so the edge serial itself never
  snaps.
- **Edges.** A grab edge is a latch false->true OR stock ownership becoming
  true; either leaving is a release edge. When both happen on one serial the
  release edge wins. The first observation after initialization or Reset is
  always a baseline, never an edge.
- **Invalidation.** `ResetAimContinuity` plus a published identity are applied
  on: session stop/exit/loss and instance loss, the fatal frame-wait drain,
  aborted prepared frames (`EndPreparedFrameWithoutLayers` exceptional paths),
  LOCAL reference-space change (epoch bump), active-title/generation change,
  handedness change, and whenever Virtual Stock or two-handed aim stops
  applying. No stale history is ever replayed.
  The **routine** prepared-frame retire
  (`SubmitPreparedFrame` -> `ResetPreparedFrame`) deliberately does **not**
  invalidate: it runs between every pair of advances, so invalidating there
  would make every frame a first observation and no grab/release edge could
  ever seed. The layer intentionally persists across prepared frames; only the
  genuine abort/session/incoherence paths above reset it.
- **Plus<->Standard switch mid-ease.** Deliberately does NOT invalidate. It is
  a menu-driven geometry change, not a latch edge, so the running ease keeps
  decaying against the live solve of whichever mode is now selected and the
  presentation stays continuous.
- **Invalid inputs are per-observation, not a reset.** An active transition
  keeps stepping through a frame whose live solve is invalid, so the correction
  continues along its own 200 ms timeline rather than freezing or jumping. The
  edge handling for that serial changes:
  - a **grab edge** without a valid live solve fails open to identity (no jump
    is fabricated). The failed edge is never replayed, but stock ownership
    going false->true on a later valid serial is a grab edge too, so the
    correction re-seeds there;
  - a **release edge** without a valid anchor keeps the running correction
    (restarting the release ease) or fails open to identity when no transition
    was running.
  Consumers present the raw live solve whenever the solve itself is invalid
  (the seam only marks a presented orientation valid when the live solve is
  valid, and the cross-thread packet is published invalid in that case).
- **Fail open.** Invalid or non-finite poses, a torn or non-fresh publication, a
  changed contact-space epoch, a reader whose live solve was assembled
  differently (head coherence, support endpoint source), or a publication from
  another prepared serial all produce the raw live orientation: identity, never
  a fabricated jump. A failing layer can never disarm the camera core, end the
  session or gate arming.
- **Timing.** The advance runs once per prepared serial on the OpenXR frame
  thread with the prepared-frame display delta. A duplicate serial advances
  nothing.

## 3. Where it is wired

- Advance (frame thread, once per prepared serial, after input capture incl.
  `UpdateTwoHandLatch` and after `CaptureHeadPose`, before every publication):
  `AdvanceAimContinuityForPreparedFrame`. It is deliberately outside
  `CapturePreparedFrameTelemetry`, so the feature works with recording off.
- Live inputs: the same `CurrentFrameStockAimPoseInputs(padFresh && valid, ...)`
  assembly the per-title snapshots use. `VR_GetAimPose` assembles identical
  values under `g_headCs` from the same globals.
- Correction applied to the presented aim orientation:
  - `VR_GetAimPose` (all game/render thread aim consumers: FP gun/hands/muzzle
    steering, Halo 2 observer, reticle);
  - the per-title render snapshots Reach, Halo 2, Halo 4 and the Halo CE
    tracking rig (`primaryAim`);
  - the contact-tracking snapshot `primaryAimOrientation`, which feeds the
    anatomical carriers (`left_hand_presentation.inl`) and Reach/Halo 2 dual
    aiming, so it is a presented-aim surface.
- Intentionally raw (never corrected): the physical/one-hand solves
  (`twoHandEnabled=false`), the independent dual-wield solves, and all recorded
  live aim evidence (`aim_trace.final_direction`, `canonical_aim`,
  `semantic_primary_*`).
- Cross-thread publication: a lock-free seqlock packet (serial, contact-space
  epoch, validity, assembly identity, correction quaternion) that mirrors
  `ReticleAimPosePublication`. No locks, no allocation; invalid/stale/epoch
  mismatch/different assembly fails open to identity.
- `VR_GetAimPose` serial-guards that packet at read time: it samples
  `g_preparedSerialPublished` immediately before the `g_headCs` pose read and
  accepts the correction only when the packet's serial equals that sample *and*
  the published serial re-read immediately after the packet read still equals
  it. Both are strict; any mismatch returns the raw live orientation.
  One micro-window remains, and it is not a cross-serial mismatch: the poses of
  serial N+1 can already be captured while the packet still carries serial N
  (the packet is published mid-block, the pose globals are replaced earlier in
  the same block). A reader landing in that few-microsecond stretch can compose
  the previous serial's correction with a one-frame-newer pose. That is
  presentation-only and bounded by one frame of correction decay and pose
  motion in the live pose's own local frame, never the snap-sized cross-serial
  mismatch this guard exists to remove; closing it literally would require
  versioning the pose globals themselves, which this feature deliberately does
  not do.

## 4. Config and UI

For Virtual Stock Standard/Plus, the feature remains always on when
`two_handed_aim` is on and needs no user control. The same holds for the VS-OFF
latch continuity: since 2026-09-29 it is fixed-on internal product behaviour
with **no user toggle**, and the retired `two_hand_transition_smoothing` config
key no longer resolves (a historical file containing it still loads quietly;
nothing reads the field and the generated config no longer writes it). The one
separate VS-OFF product control is `two_hand_smoothing_strength` (0..25,
default 0 = off); it keeps the fixed speed-25 response and exposes only the
wet/dry mix amount (0 = raw, 25 = the previous full-strength behaviour). The
previous candidate's boolean `two_hand_smoothing` key is not written any more;
when it is present without the numeric key, `false` loads as strength 0 and
`true` as strength 25, so an existing configuration keeps its behaviour. The
rejected experiment's `virtual_stock_grab_transition` config key and its F1
"Grab Transition (Experimental)" selector were removed; an existing
`halomccvr.cfg` containing that key still loads (unknown keys are ignored) and
the generated config no longer writes it.

## 5. Telemetry

`frame` records carry the transition family (additive within schema 2; see
`docs/TELEMETRY-SYSTEM.md`). Field names keep the `transition_*` prefix for raw
schema stability:

```text
transition_stock_mode, transition_stock_mode_valid,
transition_active, transition_phase,
transition_edge_kind, transition_anchor_source,
transition_live_calibrated_forward_valid / _forward,
transition_presented_forward_valid / _forward,
transition_initial_correction_deg, transition_remaining_correction_deg,
transition_elapsed_ms,
transition_one_hand_anchor_valid / _forward,
transition_advance_count, transition_last_prepared_serial,
transition_applied_serial
```

The former `transition_mode` and `transition_motion_gate_*` fields were removed
with the rejected modes. `transition_stock_mode` is the product mode this frame
(1 = Plus, i.e. rear reference 3; 0 = Standard) and is only meaningful while
`transition_stock_mode_valid` is true (Virtual Stock enabled); when invalid it
reads 0.

`aim_trace.final_direction` and `canonical_aim` keep their meaning: the raw live
solve. The transition family describes the presentation layer only.

Offline check for the headline contract: at the first frame where
`transition_edge_kind == 1` (grab), `transition_anchor_source == 1`
(SameFrameOneHand) and `transition_active` is true, the angle between
`transition_presented_forward` and `transition_one_hand_anchor_forward` must be
~0, and `transition_initial_correction_deg` must equal the pre-latch swing
angle. That equality is only valid for a FRESH grab: on a re-entrant grab
(`transition_anchor_source == 2`, LastPresented) the seam deliberately anchors
to what the player was already seeing, so `transition_presented_forward` must
instead match the previously presented orientation (the last presented
`transition_presented_forward` before the edge), and
`transition_one_hand_anchor_forward` names that serial's one-hand
counterfactual rather than the anchor. At the first `transition_edge_kind == 2`
(release) frame, `transition_presented_forward` must match the last presented
orientation and `transition_elapsed_ms` must climb to ~200 ms before
`transition_active` clears. `transition_applied_serial` names the prepared
serial the layer ran for (0 when it did not run), and
`transition_advance_count` is a monotonic count of consumed serials.

## 6. Headset protocol (product verification)

There is no mode selection any more: the 200 ms law is the only behaviour, so
verify grab/release smoothness directly. Keep **Reduce Support-Hand Rotation
ON** and use one test profile before and after. Priority titles: Halo 2, Halo 4,
ODST, plus a Halo 3 regression run (shared-code change).

1. Grab and release the support grip ten times at normal speed. Watch the latch
   frame and the un-latch frame specifically: neither should jump, and the aim
   should settle onto the stocked/one-hand line within about a fifth of a
   second.
2. Repeat in Standard mode (rear reference Centre and Shoulder) and in Plus
   mode. Both modes must feel the same in the headset, because they run the
   same law.
3. Track a slowly moving target with the stock engaged, then release and re-grab
   mid-track (at least ten rapid cycles). Nothing should drift, lag or stack.
4. Lower the weapon, aim around a corner, turn the head hard while stocked.
5. Record a telemetry capture of (1) so the presented/one-hand angle and the
   ~200 ms settle can be checked offline (section 5).

Questions to answer in the headset:

- On the latch frame, does the weapon keep the aim you already had instead of
  snapping to the stocked line?
- On release, does the aim return smoothly to your hand, or is there a second
  jump?
- During rapid grab/release cycles, does anything drift, lag, or stack up?
- While stocked, does the reticle stay where the weapon points?
- Any interaction with aiming down sights or lower-the-weapon reloads?
  (Head-turn sway correction was retired from the product on 2026-09-27; see
  `docs/VIRTUAL-STOCK-HEAD-TURN-SWAY-REMOVAL-2026-09-27.md`.)
- Frame rate: any change from the previous build?

Report the MCC edition, OpenXR runtime and headset with the result, as usual.

## 7. Experiment record and limits

- Accepted (T1): fixed 200 ms smoothstep, both directions, both modes.
- Rejected: T0 hard switch (still perceptible), T2 speed-5 (no advantage over
  T1), T3 motion gate (no perceptible benefit, extra state), T4 persistent
  calibration (held a stale aim while stocked). All removed.
- The 200 ms constant is the accepted value; it is not further tunable and has
  no config surface.
- The layer affects the emitted aim orientation only. It does not change
  whether or when the grip latches, the stocked aim itself, or the aim position.
- Grip-zone authoring and the ODST freeze remain separate work items and are
  not addressed here.
- Standard-mode behaviour was verified by the offline suites (isolation, ease
  law, release continuity) before this candidate; the headset result for
  Standard specifically is still collected by section 6.
