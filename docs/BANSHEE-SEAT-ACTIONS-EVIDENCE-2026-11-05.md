# Banshee bomb and trick: native seat actions (2026-11-05)

`vr_mapping::NativeExtraAction` names, for each game, the native action the VR Mappings seat rows read from the
player's binding table (`gesture_binding_reader.inl`, the same table and reader as every action row). Each index
below is one a game's own control builder reads while the player is seated, through the reader the mod already
signature-matches (`readerRva`). Every index is below that title's `actionCount` (checked in
`tests/gesture_binding_reader_tests.cpp`).

| Title | Bomb | Trick | Source |
|---|---|---|---|
| Halo 3 | 0x0A | 0x1E | H3EK kit glyphs and builder; retail matches |
| ODST | 0x0A | 0x1F | retail |
| Halo 2 | 0x15 | 0x01 | retail (halo2.dll carries its own glyph names) |
| Reach | 0x0A (secondary trigger) | 0x0D | retail, structural; corrected 2026-11-06 |
| Halo 4 | 0x0B (secondary trigger) | 0x0E | retail; corrected in the headset 2026-11-06 |
| Halo CE | none | none | no Banshee bomb or trick action |

Binaries (SHA-256):

- H3EK `halo3_tag_test.exe` `59A78F2C96034D7CEB5D710505B2B36813AA141FC81A083E3F952973DBCE4602`
- `halo3.dll` `B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63`
- `halo3odst.dll` `5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A`
- `halo2.dll` `DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946`
- `haloreach.dll` `738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894`
- `halo4.dll` `7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84`

## Chains

- **Halo 3 (kit).** The glyph table names `button_banshee_bomb` (glyph 0xE46B) and `button_e_brake` (0xE465). The
  glyph callback (kit +0x98D390, retail +0x2E9C94) answers 0xE46B with action 10 and 0xE465 with 9. The control
  builder (kit +0x448A70) reads, only while seated, action 0x0A into control bit 32, and reads 0x1E instead of jump
  (0) for the jump bit. Retail does the same through +0x187028.
- **ODST.** The same glyph codes and callback answers (+0x323DCC). The builder (+0x10FBFC, reader +0x1B8420) reads
  0x0A while seated and 0x1F instead of 0 for the jump bit.
- **Halo 2.** halo2.dll's own glyph table names `button_trick` (0xE458) and `button_banshee_bomb` (0xE45E); its
  callback (+0x8255A0) answers them with 1 and 0x15 and hands them to the reader +0x6D9A00.
- **Reach.** No glyph names survive in the retail DLL. Its glyph jump table answers E44F/E450 with 0x0A and E451/E452
  with 0x0C/0x0D. The builder (+0x5DF80, reader +0xD1070) drives the analogue secondary trigger from 0x0A and, while
  seated, sets control bit 2 from 0x0C and bit 0x14 from 0x0D. 0x0C is chosen as the trick because it drives the bit
  Halo 4's trick drives. Medium confidence.
- **Halo 4.** The retail DLL holds the training line "Use <U+E453> and <U+E462> to perform Banshee tricks"; its glyph
  jump table answers E453 with 0x0D. The builder (+0xA2670, reader +0x13F5CC), seated and not in the Mantis, sets
  control bit 2 from 0x0D and bit 0x15 from 0x0E. The analogue secondary trigger is 0x0B.

**Correction, 2026-11-06.** In the headset, Halo 4's trick and brake rows were the wrong way round: the row on 0x0D
braked and the row on 0x0E tricked (MCC's `ControllerBansheeTrickButtonsSwapped[3]` was 0, so not the invert setting).
Seated control bit 2 is the Banshee's brake and bit 0x15 its trick. Reach's trick had been chosen as the action that
drives Halo 4's "trick" bit, so it moves with it: Reach's brake is 0x0C (bit 2) and its trick 0x0D (bit 0x14).

## Limits

- Halo 4's "Invert Banshee Actions" makes the game read 0x0D and 0x0E the other way round for one vehicle type. The
  seat row always presses 0x0D's button, so with that setting on, the row brakes and the brake tricks.
- Reach and Halo 4 read 0x0E/0x0F and 0x10/0x11 instead for one other vehicle type's driver. That vehicle is not the
  Banshee.
- The seat rows press whatever button the player's layout gives the action, so a button shared with another action
  presses that one too.
- Not headset verified. The log line `VR Mappings <game>, extra rows:` names each extra row's controls and the button
  the game has for it.
