# Halo 2 dual-wield shake reload: one Reload reloads both guns (2026-11-06)

Asked: in Halo 2, a left-hand shake should reload the left gun and a right-hand shake the right gun, as in Halo 3;
failing that, a shake of either hand should reload both.

Result: **per-hand reload is not possible through input in Halo 2.** The game has a single reload control, and it
starts both guns' reloads together. Either hand's shake now presses that Reload, so either shake reloads both guns,
exactly as the game's own Reload button does for a flat player.

## Why not per hand

The shake must stay shared input (co-op runs one simulation; `8b1d863b` removed the earlier direct reload call for
that reason). So a per-hand reload needs an input that reloads only the left gun. Halo 2 has none.

All RVAs are `halo2.dll` (Steam, SHA-256 `DE65B4F4...71946`, the binary pinned in
`tools/re/verify_dual_shake_reload.py`; the Store module is byte-identical). Found with the portable Ghidra
(`out/ghidra` project, copied read-only) and capstone.

1. **Input.** The control builder `+6C0E30` turns the native action states into the player action. Action `0x2E`
   (`reload` in H2EK's action-name table, `VR NativeAction(Halo2, Reload)`) held sets action bit `0x200000`
   (`+6C36C5`); `0x2F` (`exit_vehicle`) sets `0x400000`. The builder's `+0x24` dword is copied to the networked player
   action's `+0x20` (`+6C05A0`).
2. **Press edge.** `+69DB50` turns the rising edge of the action-group bit 1 into bit 4.
3. **Reload decision.** `+69EDA0` (`+69F0D0..+69F16E`): bit 4 set, a weapon in either of the unit's slots (`+0x226`
   or `+0x227`), and bit `0x200000` → unit control `|= 0x40000000` (bit 30).
4. **Both guns.** The unit update `+8FFC90` (`+8FFEA9..+8FFEC6`): unit control bit 30 → unit action `0` (primary
   reload) and unit action `0xA` (secondary reload), back to back. These are the same actions the weapon's own
   reload request `+8EADF0` selects per gun (`DUAL-WIELD-SHAKE-RELOAD-2026-10-23.md`).

Every caller of the unit-action starter `+93B750` was scanned for action `0xA`. Only three start it:
`+8FFEC6` (bit 30, both guns, above), `+8CE2EB` (also action 0 then `0xA`, a non-input caller), and `+8EAEEB` (the
weapon's own request, reached only from the weapon update `+8EDA30` when a trigger is pulled on an empty magazine,
`+8E4570`/`+8EA4D0`). No input reaches a left-only reload. MCC's "Reload left weapon" control has no Halo 2 action
(`EXTRA-ACTIONS-EVIDENCE-2026-11-05.md`), consistent with this.

Rejected: pressing the left fire button to trigger the empty-magazine reload. It fires the gun whenever the
magazine is not empty.

## Change

- `dual_wield_shake::ReloadButtons` (`src/common/dual_wield_shake_logic.h`) chooses the buttons a frame's shaken
  slots press. Halo 2: either slot presses Reload. Halo 3 unchanged: right = Reload, left = its own left-weapon
  reload binding, never Reload when that is unbound. Tested in `tests/dual_wield_shake_tests.cpp`.
- `vr.cpp` uses it; `leftReloadUnbound` no longer counts in Halo 2 unless Reload itself is unbound.
- F1 › Controls › Reload and holsters › Dual-wield shake reload: "Halo 3: each hand reloads its own gun. Halo 2:
  either hand presses Reload, which reloads both guns."
- `tools/re/verify_dual_shake_reload.py` checks steps 1, 3 and 4 at their instructions.

Not headset verified.
