# Gab_dC virtual stock adaptation

Source: the September 20 DM contribution archived as
`out/community-audit-20260923/gabdc-virtual-stock-20260920.zip`, SHA-256
`D187C4ACB6E9B9BA663D4C92AE34095867CE9B62F6640713D7AD9B483EE5CF9F`.
The contributor's pure finite-checked geometry helper and geometry regression
suite are retained with attribution. This is adapted source, not execution of
the supplied binary and not a claim of all-title headset acceptance.

Halo 3 reference behavior being preserved: two-hand support is acquired by the
existing support-grip latch; orientation follows the semantic primary/support
controller pair, raw aim position remains the primary controller, and calibrated
gun yaw/pitch/roll is applied afterward. The optional feature changes only the
orientation of an already-supported main weapon. It defaults off and dispatches
to the original controller solver without altering that solver's body.

The opt-in stock provides Head, Shoulder, Chest and Adaptive references,
continuous strength, gravity-relative/shoulder/chest geometry controls, and
smooth proximity release. Advanced controls live in a foldout. Shoulder/chest
side follows the already captured semantic main hand. The adaptive surface is
computed entirely from tracking geometry and uses no engine bindings/offsets.

The OpenXR pose capture records HMD and controller locate timestamps under the
existing tracking critical section. Stock constructors admit the HMD reference
only when both timestamps match. The frame-thread publishers use their owned
pose samples; the public getter copies head and controller poses and timestamp
agreement under the same lock. A missing/mismatched head sample selects ordinary
controller aiming for that feature. Reach, H2, H4 and CE snapshots and the H3/ODST
public aim getter use this same deliberate VR-side geometry.

Physical-contact and independent dual-wield inputs remain stock-unaware. The CE
independent result disables two-hand processing before calculating its pose.
No grab-zone, raw primary/support position, physical melee, native muzzle origin,
per-gun calibration, palette-hook locking, or engine-owned seat data is changed.

One separate contribution experiment is not adopted: using a new OpenXR grip
pose as the support endpoint. It needs an additional input action/binding and
changes the released raw aim-pose endpoint, so it cannot be silently folded into
this geometry-only feature. Existing grab acquisition and raw endpoints remain
the reference; the head/shoulder/chest/adaptive stock modes do not need that
experiment.

Offline validation:

- The contributor's geometry suite passes 6,539 checks, including finite-value,
  degeneracy, adaptive triangle/diagonal continuity, bilateral sweeps and release.
- A fixture extracts the actual production aim structs, quaternion rotation,
  controller solver and stock solver. 640 mode/hand/pose combinations verify
  zero-strength parity, missing-head fallback, unchanged raw position, no new
  latch acquisition, calibration and independent/physical-path isolation.
- Existing core tests already exercise two stable weapon identities across all
  seven title/graphics profiles, save/load, swaps, disabling, title fallback,
  muzzle trims, HUD separation and rejection of unknown handles. No duplicate
  implementation-mirroring alignment tests were added.

Outstanding acceptance: headset tests for each title/mode, both handedness
choices, all four stock reference modes, tracking loss/recovery, physical stock
users leaving the option off, H2/H3 dual wield and Halo 3 regression. Geometry
tests do not establish network, vehicle or renderer acceptance.
