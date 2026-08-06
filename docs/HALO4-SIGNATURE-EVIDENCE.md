# Halo 4 signature evidence

Status: **Phase 0 — identities pinned, no runtime bindings exist.** This file
is the proof ledger for every Halo 4 signature, RVA, layout, and hook the
runtime will consume. Nothing may be hooked, scanned for, or shipped for
Halo 4 unless its proof is recorded here first. The machine-readable identity
set lives in `docs/HALO4-EVIDENCE-MANIFEST.json`.

The template for this file is `docs/REACH-SIGNATURE-EVIDENCE.md` — Reach is
the only other proven new-engine-branch port. Halo 4 is a third distinct
engine branch: Halo 3/ODST facts, Reach facts, and their offsets are **not**
Halo 4 evidence.

## Evidence rules for this title

- **H4EK-first.** Discover what a system does from the official H4EK
  binaries (`halo4_tag_test.exe` carries symbols, assert text, and 2,264
  retained `.cpp` source names), tag schemas (ManagedBlam/Corinth — H4EK has
  no guerilla.exe; Foundation.exe is the tag editor), and `tool.exe` exports.
  Retail `halo4.dll` is used only to match and verify something the kit has
  already explained.
- **The Reach script-table chain is UNPROVEN here.** The proven Reach retail
  derivation (script-name string → single qword xref → entry+0x18 =
  implementation) must be re-proved on two harmless Halo 4 script functions —
  and H4's actual entry stride/offset recorded in the manifest — before any
  retail anchoring may rely on it.
- **Methods that failed for Reach are never retried:** byte-matching kit
  prologues against retail (484 matches / 0 matches), assert-validator shapes
  (compiled out of retail), brute memory scans.
- **Both storefront hashes gate admission.** The Steam and Store images are
  byte-identical apart from Authenticode, so the loaded-module check accepts
  either pinned SHA-256 (Reach precedent: `kReachRetailModuleSha256[]`).
- Zero or multiple signature matches block that hook; a mandatory-hook miss
  leaves Halo 4 100% stock, loudly. Never a guessed address, never a copied
  cross-title offset.

## Pinned identities (measured 2026-08-06)

| Artifact | Identity |
| --- | --- |
| `halo4.dll` (both editions) | 17,829,336 B; PE timestamp `0x68A0E7BF` (2025-08-16); SizeOfImage `0x04A3F000`; build 2025.08.16.178512.1 |
| `halo4.dll` SHA-256 (Steam) | `7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84` |
| `halo4.dll` SHA-256 (Store) | `5767CD564C1E8E8D012D002A8DE8E92960A3DE46442399ED054E3C4EF44AA496` |
| H4EK build | `2023.06.27.176405.1-Release` at `N:\SteamLibrary\steamapps\common\H4EK` |
| `H4EK.7z` (hashed before extraction) | 9,598,520,866 B; SHA-256 `C7214D90C37557ECAF4215E35EF6C3A2F578E83D35EAE5C15F7BCEFFACBF941F` |
| `halo4_tag_test.exe` | SHA-256 `B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6`; ts `0x649B096A`; SizeOfImage `0x068F2000` |
| `halo4_tag_play.exe` | SHA-256 `B796A1249004AD1C1A7B1E482A4A92C57F4C6E342E1D769DBB25F69EBC6709A8`; ts `0x649B0D4A`; SizeOfImage `0x061BA000` |
| `sapien_play.exe` | SHA-256 `F305674C1BA7417818F23D31C6161CA38E643AA3401B9F98FD6A1FF92656081A`; ts `0x649B0810`; SizeOfImage `0x065FE000` |
| `tool.exe` | SHA-256 `5E0AD8D03EC4B1C7F4C0C2A18C92CEC9F92F3EB7E7F0CBB7376BA9D866E3A758`; ts `0x649B075D`; SizeOfImage `0x06E66000` |

The same kit-vs-retail build drift Reach had applies: kit 2023.06.27 vs
retail 2025.08.16. A kit RVA is never a retail RVA; only semantics, layouts,
and names transfer, each with its own retail verification.

**Standing risk:** any MCC update replaces `halo4.dll` and invalidates every
retail identity above. The pinned-identity preflight makes that a loud
refusal, never a wrong hook.

## Tooling facts (negative result, measured 2026-08-06)

The H4EK `tool.exe` verb table (captured to
`out/h4ek-evidence/identity/tool-verbs.txt`, 264 lines) does **not** contain
`export-enum-tables`, `export-string-tables`, `export-script`, or
`export-node-object-function`. The bring-up plan carried those verbs over
from HREK; that assumption is refuted for H4EK. What the H4 verb table does
provide: `export-tag-to-xml <tag-file> <output-file>`,
`extract-unicode-strings <multilingual_unicode_string_list>`,
`script-doc <function-or-global-name>` (useful for the script-table
bootstrap), and `dump-cinematics-script`. Enum and schema recovery therefore
goes through ManagedBlam/Corinth reflection, not tool.exe.

## Confirmed engine-structure facts

- **No CHUD.** `bin\!public_tags.txt` (85,634 lines, census re-run
  2026-08-06): zero tags of any `chud_*` class. The HUD is the CUI system —
  409 `cui_screen`, 9 `cui_static_data`, 2 `cui_logic`, 1
  `user_interface_hud_globals_definition`; `cui_render_view.cpp` is among the
  retained source names. All CUI/HUD research is tracked in
  `docs/HALO4-CUI-EVIDENCE.md`, not here.
- **Camera/vehicle constructs survive:** `player_view`, `render_view`
  (+ stack symbols), `first_person_camera/skeleton/models/fov/hide_*`,
  `unit_seat_*`, 152 `camera_fx_settings`, 134 `camera_shake`, 79 `vehicle`,
  140 `weapon` tags.
- **Open structural question (settle before any player-view AOB):** H4
  symbols suggest a render_view STACK, possibly not Reach's fixed 4×0xA40
  array. `player_view_count` evidence decides; cross-check
  `halo4_tag_play.exe` and `sapien_play.exe`.

## Deliberate decision: groundhog.dll stays out of the registry (D-H4-5)

Recorded 2026-08-06, per the plan's skip option. `groundhog.dll` (Halo 2
Anniversary MP) appears nowhere in `src/` and has no row in `kTitles[]`
(`title_registry.cpp:58-71`). Two facts make the considered fix wrong for a
desk commit:

1. A registry row alone is provably inert: `TitleAdapter_PollLoaded`
   (`title_adapter.cpp:484-486`) skips any module whose title has no runtime
   slot **before** `detected`/`detectedCount++`, so a slotless groundhog row
   would change no observable behavior at all.
2. Making slotless modules count into `detectedCount` would change live
   ambiguity semantics in states users actually occupy (an H2A MP session
   would flip from "no MCC game module is loaded"/Shell to a counted
   unknown, and every menu transition's ambiguity accounting would shift) -
   a behavioral change to shipped titles with no headset gate.

The bias this was meant to address only materializes if `groundhog.dll` is
ever resident simultaneously with exactly one supported title DLL during
gameplay; no such state has been recorded. If one ever is, the fix belongs
in `TitleAdapter_PollLoaded`'s counting policy as its own headset-gated
candidate, not in the registry.

## Proof ledger

Empty. Every future section records: kit evidence (binary, RVA, symbols,
assert text), the retail match (AOB, expected RVA, uniqueness count,
executable range, rel32 edges), the ABI, the consumed layout fields, and the
consequence of a miss. Sections are added only with the proof in hand —
theories are never written here (`AGENTS.md`: ship a probe or record the
negative result).
