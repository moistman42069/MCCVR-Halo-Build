# Free two-hand product surface (Virtual Stock OFF, plus the shared Two-Hand Smoothing filter)

Date: 2026-09-29. Status: **candidate product surface; the user's headset result
is still the gate, so nothing here changes the accepted-build pointer in
`docs/CURRENT-STATE.md`.**

This is the single statement of the free two-hand (Virtual Stock off, VS OFF)
player surface as the current candidate ships it. Field-level mechanics stay in
the feature documents linked from section 7; if this summary and a feature
document disagree, the feature document's mechanics win and this file must be
corrected. One control in this surface is wider than VS OFF: `Two-Hand
Smoothing` applies to two-handed aiming with Virtual Stock on as well (section
1); every other claim in this document stays VS-OFF scope, and Offhand
influence remains VS-OFF only.

## 1. Player surface

| Control (config key) | Default | Range | Applies to |
| --- | --- | --- | --- |
| Persistent support grip (`persistent_support_grip`) | **ON for new configurations** | on/off | two-handed aim, Virtual Stock on and off |
| Reduce Support-Hand Rotation (`two_hand_support_grip_pose`) | **ON** | on/off | Virtual Stock endpoint selection only (section 3) |
| Offhand influence (`two_hand_offhand_influence`) | **50%** | 0-100% | VS-OFF free two-hand aim only |
| Two-Hand Smoothing (`two_hand_smoothing_strength`) | **0 (off)** | 0-25 | two-handed aim, Virtual Stock on and off |

`Persistent support grip` keeps the support hand attached after a valid grab
even when it leaves the original grab area. It ends on an explicit release, or
when the weapon, title or life state changes; hold/toggle acquisition follows
the existing `two_hand_toggle` mode. New configurations default it on, with no
migration or version-based rewrite: a saved explicit
`persistent_support_grip = 0` (or `= 1`) keeps the user's own choice. The
dependent `Switched weapons already two-handed` (`two_hand_switch_inherit`)
sub-option stays off by default.

`Offhand influence` is the free two-hand offhand directional authority: 0% =
the primary controller's own aim is authoritative (exact primary
orientation, no positional rebuild), 100% = the accepted support direction
owns presentation entirely, and intermediate values blend the primary direction
towards the accepted support direction. It is directional authority only: at
every influence, acceptance and `two_hand_active` stay the existing solved
semantics, so zero authority never decides whether the weapon is logically
held.

`Two-Hand Smoothing` is the independent two-hand controller-input filter
amount: 0 = raw/off, 25 = the full fixed speed-25 response, and intermediate
values wet/dry mix that response over raw input copies. It applies to
two-handed aiming with Virtual Stock on or off: the VS-ON solve consumes the
same smoothed directional copies (primary orientation, primary/support aim
positions, primary/support grip positions) the VS-OFF free two-hand solve does,
so Virtual Stock output does move with this slider above 0. It is a
controller-input filter, never a user-visible interpolation speed, and it does
not alter raw acquisition/latch samples, one-hand aim, the weapon/base position,
the VS-OFF-only Grip -> Grip product geometry, Offhand influence or the
Two-Handed Lab. At 0 it publishes no packet and every path is raw exactly.

`Offhand influence` remains VS-OFF only: Virtual Stock keeps its own hybrid
offhand influence and never reads that value. `Two-Hand Smoothing` is the one
control in this surface that spans both stock modes.

## 2. The VS-OFF geometry is fixed: primary Grip -> support Grip

With Virtual Stock off and the two-hand hold latched, the two-hand positional
line is the **fixed primary-Grip -> support-Grip pair**
(`virtual_stock::SelectTwoHandGripEndpoints`): both grip positions when both are
committed and finite, otherwise the pre-GG aim-pair fallback. This is fixed
product geometry, not a player-selectable anchor - no setting chooses
AA/AG/GA/GG, and the VS-OFF solve ignores the "Reduce Support-Hand Rotation"
selection entirely. The orientation/roll baseline stays the primary Aim
observation and the weapon/base position stays the raw primary position; the
grip positions are positional pivots only.

Telemetry records the same truth for each prepared serial:
`two_hand_offhand_influence` is the frozen authority the solve consumed (never
a live config re-read at capture), and `two_hand_smoothing_strength` /
`two_hand_smoothing_mix` are the frozen filter amount, with
`two_hand_smoothing_applied` reporting a real consumption on either stock mode.
See `docs/TELEMETRY-SYSTEM.md`.

## 3. Reduce Support-Hand Rotation is a separate Virtual Stock feature

`Reduce Support-Hand Rotation` (`two_hand_support_grip_pose`) is **ON by
default** and is a distinct feature from the free two-hand geometry. As shipped
its scope is:

- it selects the **Virtual Stock** support endpoint from the support aim
  position or from the position-only grip-action endpoint, and that selection is
  reported (`support_endpoint_used_grip`, `semantic_support_endpoint`);
- the Two-Handed Lab's Production anchor passes the same production-selected
  endpoint through;
- it is provenance for those VS/Lab paths only. It is **not** grip sampling:
  both hand grip positions are sampled for every captured frame regardless of
  this checkbox. It is **not** the VS-OFF product geometry (section 2), so with
  Virtual Stock off in normal play the checkbox changes nothing about the
  two-hand solve.

The menu help text states this directly: "Virtual Stock only: anchors the stock
aim line to your support hand's grip position instead of its aim point. Free
two-handed aim always uses both grip positions."

## 4. Acquire/release continuity: fixed 200 ms, no toggle

VS-OFF latch continuity is the same fixed 200 ms smoothstep law the Virtual
Stock Standard/Plus paths use, and it is **fixed-on internal product behaviour
with no user control**. The former `two_hand_transition_smoothing` toggle was
retired on 2026-09-29: a historical `halomccvr.cfg` containing the key still
loads quietly, but nothing reads the field and the generated config no longer
writes it. Telemetry keeps reporting the effective per-frame truth
(`two_hand_transition_smoothing_configured` / `two_hand_transition_active`); the
continuity law itself is documented in
`docs/VIRTUAL-STOCK-AIM-CONTINUITY-2026-09-27.md`.

## 5. The Two-Handed Lab is internal research machinery

The Two-Handed Lab (anchor matrix Production/AA/AG/GA/GG, soft off-hand
authority, temporal damping, quick presets) remains runtime-only, disabled by
default and **hidden from normal navigation**, exactly like the Virtual Stock
Lab: no F1 sidebar entry, no Advanced doorway and no back button. It is
research/diagnostic infrastructure, never a player-selectable product anchor,
and it never steers a solve whose resolved inputs have Virtual Stock on. Its
`Production` anchor is the pre-GG production shape (primary Aim observation
plus the production-selected support endpoint), not the shipped GG geometry. See
`docs/TWO-HAND-LAB-2026-09-27.md`.

## 6. Known follow-ups (not solved here)

1. **Two-handed projectile landing / aim accuracy** in Halo 3, ODST, Reach and
   Halo 4 still requires investigation. Nothing in this productisation claims
   the cause is solved, and no field or behaviour described here should be read
   as a conclusion about it.
2. **Per-weapon / weapon-group grip positions are not implemented yet.** The
   grip-pair geometry consumes the tracked per-controller grip positions as
   they are; a per-weapon calibration/refinement layer is future work.

## 7. Where the mechanics live

- `docs/VIRTUAL-STOCK-AIM-CONTINUITY-2026-09-27.md` - the 200 ms continuity
  law, the shared Two-Hand Smoothing filter, config history.
- `docs/TWO-HAND-LAB-2026-09-27.md` - Lab anchors, temporal modes, UI, tests.
- `docs/PERSISTENT-GRIP-PORT-2026-09-27.md` - durable support-grip
  relationship, ownership trust, acquisition, per-title wiring.
- `docs/TELEMETRY-SYSTEM.md` - recorded fields and their provenance wording.
- `src/dll/menu.cpp` (Weapon & Aim page) and `src/dll/virtual_stock_aim.inl`
  (solver) - the shipped wording and geometry.
