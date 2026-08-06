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
- **The Reach script-table chain is REFUTED here; Halo 4 has its own,
  measured chain.** See "Retail derivation: the script registrar" below. The
  Reach form (script-name string → single qword xref → entry+0x18 =
  implementation) fails at its first premise: no Halo 4 script name has any
  on-disk qword reference, and +0x18 holds the documentation string.
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

## Candidate status

### C-H4-1 — adapter identity + controller input (headset-PENDING)

The first Halo 4 candidate and the first headset touch. It needs identity
evidence only, which E-H4-1's preflight already pins.

**What it changes.** `Halo4Adapter_GetStage()` returns `ControllerInputOnly`;
the registry row's `admissionCapabilities` gains
`TitleCapability_ControllerInput` (the identical staging shape ODST and Reach
used); the title-adapter poll replaces "adapter not implemented" with a
transport line plus a pinned-identity line.

**What it deliberately does not do.** `Halo4Adapter_RuntimeHooksPermitted()`
stays `false`, the row advertises `TitleCapability_None`, and
`TitleRegistry_HookPlan(Halo4)` stays `None` — so no hook is created, no
camera is owned, and no capability is published. The identity line is printed
from **compile-time constants only**: nothing in the loaded `halo4.dll` is
read, not even PE headers, because touching a title whose level is still
loading is what caused the recorded load bounce. Verifying the loaded image
belongs to C-H4-2's preflight, behind the level-load gate.

**Expected headset result.** Halo 4 plays entirely stock, with the VR
controllers working as a gamepad through the shared virtual-pad transport.
The log must carry the edition, the OpenXR runtime, the headset, the
transport line, and the pinned-identity line. Halo 3, ODST and Reach are
untouched by construction — the only shared-code edit is the additive
`else if` branch in the poll's unsupported-title reporting.

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

Every section records: kit evidence (binary, RVA, symbols, assert text), the
retail match (AOB, expected RVA, uniqueness count, executable range, rel32
edges), the ABI, the consumed layout fields, and the consequence of a miss.
Sections are added only with the proof in hand — theories are never written
here (`AGENTS.md`: ship a probe or record the negative result).

### E-H4-1: retail derivation — the script registrar (PROVEN 2026-08-06)

Full byte-level working, disassembly, and per-probe tables:
`out/h4ek-evidence/identity/script-table-bootstrap.md`. Measured against the
pinned Steam `halo4.dll` (preflight PASS, image base `0x180000000`) with
`tools/h4-probes/`; nothing inferred from Reach or Halo 3.

**Negative result first — Reach's chain does not exist in Halo 4.** Three
long, distinctive HaloScript names, each confirmed real two independent ways
(present in the H4EK strings dump *and* documented by
`tool.exe script-doc`): `game_difficulty_get_real`,
`player_action_test_grenade_trigger`, `device_group_set_immediate`. Each
occurs exactly once as a standalone NUL-terminated string in `.rdata`. A
whole-image scan for each string's preferred-base VA as a qword (any
alignment) and as a 4-byte RVA found **zero** references in every case. The
disk image does store preferred-base VAs in relocated data, so a static table
would have been found. **Halo 4 keeps no static script-function table on
disk**, and at entry+0x18 it stores the *documentation string* — carrying
Reach's constant over would have silently dereferenced help text.

**The Halo 4 chain, measured and three-way consistent.** Each name has
exactly one reference image-wide: a RIP-relative `lea rdx` inside a single
script-registrar function, `.pdata` bounds **`0x1466E4`–`0x17BA98`**
(`0x353B4` bytes; direct callers `0x605CB`, `0x13C4AF`). It contains **1,247**
`mov ecx,0x68` + `call 0x80F648` allocation sites — one per registered script
function. Every registration block has the same fixed shape:

```
mov  ecx, 0x68            ; entry size
call 0x80F648             ; allocator
lea  r9,  [rip+...]       ; -> documentation string
lea  r8,  [rip+...]       ; -> IMPLEMENTATION function
lea  rdx, [rip+...]       ; -> NAME string   (the only xref to the name)
mov  rcx, [entry]
call <per-signature ctor>
...
call 0x14646C             ; register(registrar, entry)
```

| probe name | `lea r8` site → implementation | `lea rdx` site → name | ctor |
| --- | --- | --- | --- |
| `game_difficulty_get_real` | `0x156952` → `0x9C050` | `0x156959` → `0xD068B8` | `0x189764` |
| `player_action_test_grenade_trigger` | `0x1571C4` → `0xAB2AC` | `0x1571CB` → `0xD07048` | `0x1931DC` |
| `device_group_set_immediate` | `0x15424C` → `0x67A140` | `0x154253` → `0xD044C0` | `0x18B574` |

**Runtime entry layout** (measured from all three per-signature constructors,
which agree): allocation size `0x68`; `+0x00` vtable, `+0x08` return-type
code (measured `0x38` game_difficulty / `5` boolean / `4` void, matching each
`script-doc` signature), `+0x0C` flags, **`+0x10` name pointer**, `+0x18`
docs pointer, `+0x20` per-signature helper, `+0x30` parameter count (word;
0/0/2), `+0x34..` parameter type codes, **`+0x60` implementation pointer**.
The registrar's append helper `0x14646C` shows the table is an array of
8-byte entry **pointers** at `registrar+0x260` with a dword count at
`registrar+0x6264` — so Halo 4 has no inline entry stride at all.

**Admissible recipe.** (1) Locate the name's single standalone `.rdata`
occurrence. (2) Find the single `48 8D 15 <rel32>` in `.text` resolving to it
and require the site to lie inside the registrar function. (3) The
`4C 8D 05 <rel32>` seven bytes earlier is the implementation RVA; the
`4C 8D 0D <rel32>` before that is the docs string, which must match
`tool.exe script-doc` verbatim — an independent identity cross-check for
every future anchor. (4) Validate the implementation RVA as an exact `.pdata`
function begin, **or** as a `.pdata`-exempt leaf thunk whose single
`jmp rel32` target is an exact `.pdata` begin.

That fourth clause is required, not cosmetic:
`player_action_test_grenade_trigger`'s implementation `0xAB2AC` is a 10-byte
leaf thunk (`mov ecx,5; jmp 0xAB128`) into a shared per-action bit-test
dispatcher at `0xAB128` (an exact `.pdata` function running `bt rax, r9`);
neighbouring thunks pass other action indices. The other two implementations
are exact `.pdata` begins with plausible bodies (`0x9C050` reads a dword from
a per-thread globals block via `gs:[0x58]`; `0x67A140` clamps a float with
`ecx` as the device-group handle).

**Scope of this proof.** It admits the derivation *method* and pins the
registrar's bounds and the entry layout for this exact module. It does not
admit any camera, render, HUD, or vehicle binding: each of those still needs
its own H4EK semantics plus its own retail match recorded here.

### E-H4-2: debug-variable name census (measured 2026-08-06)

Full tables, per-name counts, and menu line references:
`out/h4ek-evidence/debugvars/triage.md`. This is an **existence and
uniqueness census only** — it authorises no retail resolution. The debug-var
entry layout and type discipline are a separate gate, still open.

**`debug_menu_init.txt` format**, measured: a tab-indented pseudo-XML menu
tree of 1,830 items — 1,464 `type=command` and 366 `type=global` (338
distinct global names). On a global item, `inc`/`min`/`max` present implies a
numeric slider and their absence a boolean toggle; a command item's payload
is a console line whose first token is the command name.

**The Reach command-vs-global trap is live in Halo 4, on the same name.**
H4EK's own menu drives `render_atmosphere_fog 1` as a *command*, exactly the
name that burned the Reach work, where float-resolving a command name hands
back a pointer into `.text`. Every command-payload name inherits that
warning: typed (`FindDebugVarSlot`-style) resolution only.

**Retail scan method.** Exact ASCII bytes with a `0x00` terminator and a
`0x00` preceding byte — the same boundary condition the project's own name
matcher uses (`src/dll/game.cpp:1572` ff.). Every anchor below counted
exactly one match; longer names that merely contain them are rejected by that
boundary, as the matcher already does.

| Feature | Strongest anchor(s), all unique in retail |
| --- | --- |
| Brightness / gamma | `render_screen_gamma` (menu float 1.0–3.0); backup `render_buffer_gamma_curve` |
| Motion blur | `motion_blur_scale` + `motion_blur_max` |
| Draw distance | `render_far_clip_distance` |
| Cinematic FOV | `reduce_widescreen_fov_during_cinematics`; letterbox `cinematic_letterbox_style` |
| First-person FOV scale | `render_first_person_fov_scale` + `enable_first_person_fov` |
| Exposure hold | `render_exposure_lock` (menu bool) + `render_exposure_stops` (menu float −20..20) + `render_autoexposure_enable` |
| Tonemap | `render_tone_curve` / `render_tone_curve_white` (the entire tonemap surface — no string containing "tonemap" exists) |
| Rain / weather | `render_rain`, `rain_intensity`, `render_weather` |
| Fog | `render_patchy_fog`, `render_atmosphere_fog` — both command-shaped, see the trap above |
| Depth of field | `cinematic_depth_of_field_enable`, `render_first_person_dof` |

Halo 4 offers a **purpose-built first-person FOV scale pair** that no earlier
title had. Motion blur follows Reach's single-axis naming: the Halo 3-style
`motion_blur_*_x`/`_y` names are absent from the kit and from retail.

**Negative results, recorded so they are not re-hunted:**

- **SSAO has no debug global.** Only the HaloScript function
  `cinematic_set_ssao_mode` (retail help text: "Sets SSAO mode for
  cinematic.") and shader entry-point tokens.
- **Promethean vision has no debug control.** The substring `vision_mode`
  does not occur anywhere in retail `halo4.dll`; it is kit-only tag
  vocabulary. If VR comfort ever needs it, the lever is tag data, not a
  debug name.
- **No `camera_shake` debug name exists in retail** (tag-struct vocabulary
  only), so recoil/shake suppression must come from the observer camera path,
  not a named variable.
- **No engine "brightness" global exists** — only MCC UI option strings. Use
  the gamma pair.
- `render_force_mipmap_lodbias` is a **dead menu reference**: present in the
  kit's menu but absent from both the kit strings and retail. Do not use it.

Many of these names are in retail while absent from H4EK's own debug menu
(rain, far-clip, the FP-FOV pair), so the menu is a lead source, never a
completeness bound.
