# VR Mappings extra rows: each game's native action (2026-11-05)

`vr_mapping::kExtraNative` names, for each game, the native action an extra row reads from the player's binding table
(`gesture_binding_reader.inl`, the same table and reader as every action row). Every index is below that title's
`actionCount` (checked in `tests/gesture_binding_reader_tests.cpp`). The Banshee bomb and trick rows are covered by
`BANSHEE-SEAT-ACTIONS-EVIDENCE-2026-11-05.md`.

## Method

MCC lists one set of controls for every game (`GameAbstractButton_*`, the actions its binding screen and
`GameUserSettings.ini` `ActionMapping[n].AbstractButton` use). `MCC-Win64-Shipping.exe` names all 66 in a value-to-name
switch at +0x20854FC. Each game DLL's host-binding function asks MCC for the player's layout (host vfunc +0x3A0, one byte
per control), loops over controls 0-0x41, and converts each to its own native index:

| Title | Converter |
|---|---|
| Halo CE | `halo1.dll` +0xB5FF20 |
| Halo 2 | `halo2.dll` +0x6D2A80 |
| Halo 3 | `halo3.dll` +0xD8A98 |
| ODST | `halo3odst.dll` +0xF08A4 |
| Reach | `haloreach.dll` +0x3C724 |
| Halo 4 | `halo4.dll` +0x7634C |

Every converter was run for all 66 controls. Every index `NativeAction` already used for an action row comes out of
them for the matching MCC control, which cross-checks the method. Where a row is read was taken from the retail control
builders (Halo 3 +0xF42D8, ODST +0x10FBFC, Reach +0x5DF80, Halo 4 +0xA2670) and from every call to each title's binding
reader. Halo CE's and Halo 2's read sites were not traced; their rows rest on the converter and the action names.

## Rows

| Row (MCC control) | Halo 3 | ODST | Reach | Halo 4 | Halo CE | Halo 2 | Context |
|---|---|---|---|---|---|---|---|
| Fire left weapon (DualWield) | 0x41 | - | - | - | - | 0x17 | on foot |
| Reload left weapon (ReloadSecondary) | 0x42 | - | - | - | - | - | on foot |
| Dual wield (SwapWeapon) | 0x1F | - | - | - | - | - | on foot |
| Previous grenade (PreviousGrenade) | 0x20 | 0x21 | 0x48 | 0x09 | 0x17 | 0x03 | on foot |
| Activate waypoint (ActivateWaypoint) | - | 0x25 | - | - | - | - | on foot |
| Vehicle secondary fire (SecondaryFire) | 0x09 | 0x09 | 0x0A | 0x0B | 0x12 | 0x19 | seat |
| Second vehicle trick / brake | 0x27 | 0x2B | 0x0C | 0x0D | - | 0x02 | seat |
| Fly up (RaiseHornet) | 0x22 | 0x23 | 0x0E | 0x10 | - | - | seat |
| Fly down (LowerHornet) | 0x23 | 0x24 | 0x0F | 0x11 | - | - | seat |
| Scoreboard (ShowScores) | 0x1D | 0x0C | 0x3B | 0x25 | - | - | anywhere |
| Special action (SpecialAction) | - | - | - | 0x54 | - | - | anywhere |

- **Builder reads.** Halo 3: while dual wielding the left trigger comes from 0x41 instead of 9; 0x42 (or 4) held sets
  the left-reload bit; a long hold of 0x1F (or 4) sets the pickup bit; 0x20 is the previous grenade; 0x27 sets bit
  0x40000 only while seated; 0x22/0x23 are analogue amounts, read only while the Hornet-joystick setting is off. ODST:
  0x21 previous grenade; 0x2B seated bit 0x40000; 0x25 or 0x26 (ActivateWaypointAlt) sets bit 0x800; 0x0C is read by
  its UI. Reach: 0x48 previous grenade; 0x0C seated bit 2 (the Banshee's brake; 0x0D, bit 0x14, is its trick);
  0x0E/0x0F drive bits 4/0x14 for another flying vehicle's driver; 0x3B read at +0x7F45C. Halo 4: 0x09 previous
  grenade; 0x0D seated bit 2 (the Banshee's brake; 0x0E, bit 0x15, is its trick, swapped by "Invert Banshee Actions";
  corrected in the headset 2026-11-06); 0x10/0x11 for vehicle type 2's driver; 0x25 read at +0xCBC2C; 0x54
  sets action-test bit 34, which the mission-script test `player_action_test_special_action` checks.
- **Second trick.** MCC's two vehicle-trick controls are each game's trick and brake, not in the same order: Halo 2's
  Banshee trick is MCC's SecondaryVehicleTrick (0x01) and its PrimaryVehicleTrick is 0x02, `button_brake` in halo2.dll's
  glyph names. The row takes whichever one the Banshee trick row does not.
- **Same action twice.** Reach's and Halo 4's vehicle secondary fire is their Banshee bomb (0x0A, 0x0B), and Halo 2's
  previous grenade is its Switch grenade (0x03). The menu says so under the row.

## Left out

- ODST's dual-wield actions (0x45, 0x46, 0x20): ODST keeps Halo 3's bindings but has no dual wielding.
- Halo 2's dual-wield pickup (0x2A) is stored as a held binding (hold 0xE9), which the binding reader leaves alone, so it
  would resolve to no button. Holding Switch weapon still dual wields there.
- Halo 4's SwapWeapon (0x12): meaning not established.
- Scoreboard in Halo 2 and Halo CE: Back is their Classic/Anniversary graphics switch.
- Loadouts (Reach 0x3A, Halo 4 0x27) and ping (PingNavpoints): no reader call found, or meaning unclear.
- Zoom in/out copy Zoom's button; push to talk is MCC's own, not a game action; no game has an ordnance control.

## Defaults

Every row here starts Automatic: it presses nothing of its own and the action stays on the game's own button, pressed
whenever another row sends that button. Default gamepad buttons were not established: MCC sends the player's layout, and
where it sends none the DLL keeps its engine default.

Binaries (SHA-256): `MCC-Win64-Shipping.exe` `BE70D6DCD1A884F10CEB342A7A2DCB35EE0FA43181B66A1D19C3D830E9834691`; the
six game DLLs as listed in `BANSHEE-SEAT-ACTIONS-EVIDENCE-2026-11-05.md`.

Not headset verified. The log line `VR Mappings <game>, extra rows:` names each row the game has, its controls and the
button the game has for it.
