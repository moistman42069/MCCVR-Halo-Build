# Halo 4 signature evidence

Status: **C-H4-3, C-H4-4, C-H4-5, and C-H4-6 headset-FAILED; C-H4-1 remains
the only accepted Halo 4 line.** C-H4-6 widened the eye scope to
`main_render_game`; its exact run completed zero pairs, stalled the visible
game, exposed a `void` detour on a return value the caller consumes from `AL`,
and exposed an invalid FOV diagnostic built on misidentified fields. Commit
`7d58a68` reverted that failed behavior before C-H4-7. C-H4-7 is a deliberately
narrow stock-projection stereo-geometry candidate; it does not claim head
tracking, 6DOF, or HUD. This file is the
proof ledger for every Halo 4 signature, RVA, layout, and hook the runtime will
consume. Nothing may be hooked, scanned for, or shipped for Halo 4 unless its
proof is recorded here first. The machine-readable identity set lives in
`docs/HALO4-EVIDENCE-MANIFEST.json`.

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

### E-H4-3: player-view / render-view transaction, kit survey (2026-08-06)

Full assert quotes, disassembly and per-binary tables:
`out/h4ek-evidence/camera/player-view-survey.md`. H4EK binaries only — no
retail module was opened, per the kit-first policy. **Every RVA below is a kit
RVA in `halo4_tag_test.exe`, never retail.**

**The plan's open structural question is settled: it is BOTH, and they are
different objects.** Halo 4 has a fixed per-player view array *and* a
render-view stack; the stack symbols that prompted the question are the
active-view scope mechanism, not the per-player storage. Reach has both too.
**The Reach-shaped M1 hook architecture therefore carries over.**

**The count bound is 4**, proven three independent ways rather than assumed:
the `MAXIMUM_PLAYER_WINDOWS` assert guard is `cmp ecx,3; jbe`, the
`MAX_SPLIT_SCREEN_VIEWS` and `m_window_count` guards are `cmp esi,4; jle`, and
`main_render_game` *computes* `m_window_count = clamp(n,1,4)` in registers.

**(a) The per-player array — the direct homolog of Reach's 4 × 0xA40.**
Constructor loop `mov edi,4` / `call <element ctor>` / `add rbx,0xAD0`, base
`0x5570970`; `main_render_game` walks the same base with the same stride.
**4 slots, stride `0xAD0`.** Byte-evidenced element fields so far: `+0x389` a
first-window flag written per window, `+0x39C` and `+0x3A4` dwords read during
the transaction.

**(b) The render-view stack — the homolog of Reach's camera stack.**
`g_view_stack_top` at `0x24733D8` (static initialiser `0xFFFFFFFF`, i.e. −1 =
empty); four 8-byte pointer slots at `0x5536330`; push `0x873F10` refuses at
`top >= 3` and emits `view overflowed!!!`; pop `0x874000`; top `0x8741E0`.
Every view object carries a **re-entry callback at `+0x298`** which push and
pop invoke for the new top. Reach's equivalents: depth global, four slots,
callback at workspace `+0x2A8`, push skips at depth ≥ 3 — the identical
architecture with one field of drift.

**(c) The publication pair.** `g_player_view_stack_element`, a single global
c_player_view-shaped object at `0x55605A0` (proven by its one-instruction
accessor `0x8B6240` plus NaN-check asserts reading its render camera at
`+0x14C`); its render camera is position `+0x14C`, forward `+0x158`, up
`+0x164`. A second camera block passed as `element+0x1D4` is **inferred, not
proven**, to be the rasterizer camera. The active player-view pointer lives at
`0x5573F28`, written by set-current `0x8B9530` (NULL allowed = clear) — the
homolog of Reach's active-view global and setter/clearer.

**The transaction, statically ordered.** Dispatcher `0xB8DB0` → `main_render`
`0x1F6C60` → `main_render_game` `0x1F6FF0` → per-window loop: setup
`0x8B9990` → inner wrapper **`0x1F7C00`** → per-view post `0x8B93C0`. The
inner wrapper is the exact Reach-shaped scope:

```
call 0x8B9530        ; SET current player view    -> [0x5573F28]
call 0x873F10        ; PUSH g_player_view_stack_element, callback 0x8B8890
call 0x8B5930        ; RENDER the player view
call 0x874000        ; POP
jmp  0x8B9530        ; CLEAR current (tail-call set(NULL))
```

Its ABI is `rcx` = view element to publish, `rdx` = `c_player_view*` (array
slot), `r8d` = player window index — the same three-argument shape as Reach's
`main_render_view`.

**Cross-checked in both optimized builds**, as the plan required:
`halo4_tag_play.exe` (push `0x754868`, top `0x1D483D0`, slots `0x2414F80`,
array `0x4D97EE0`) and `sapien_play.exe` (push `0xAA8C5C`, top `0x2002010`,
slots `0x26EC890`, array `0x51760A0`) both carry the same `cmp ?,3` refusal,
the same `mov [rcx+0x298],rdx` callback store, and the same `mov e?i,4` +
`add r??,0xAD0` constructor loop. The storage shape is build-invariant, not a
debug artifact — which makes all three shapes strong retail AOB candidates.

**What is explicitly incomplete:** the `element+0x1D4` rasterizer-camera
identity, the meaning of `+0x389`/`+0x39C`/`+0x3A4`, the internals of setup
`0x8B9990` and render body `0x8B5930` (where the M1 camera-write point lives),
and the identity of callbacks `0x8B8890` vs `0x8BAE30`. None of those affect
the storage-shape verdict, and all are named as the next measurements.

**Numbers that change from Reach, to be carried carefully:** stride
`0xA40` → `0xAD0`, callback offset `+0x2A8` → `+0x298`, and the pushed
workspace is a *named single global* rather than an anonymous one.

## Candidate status

### C-H4-1 — adapter identity + controller input (headset-ACCEPTED 2026-08-06)

The first Halo 4 candidate and the first headset touch. It needs identity
evidence only, which E-H4-1's preflight already pins.

| Identity | Value |
| --- | --- |
| Runtime source | `954359b7f786b78c76824b662ead3c1fc8cd7917` (branch `feature/halo4-bringup`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/954359b-reach-fp-parity-20260806-212516151Z` |
| `halo3xr.dll` SHA-256 | `8B327A0B2FFC20135ECBEB71BEA698C78908EC1AA7C09C810CA329482ADE74AD` |
| `halo3xr_launcher.exe` SHA-256 | `930BEA232BFC3F8010BC2B385834DEBF796CD3DBEC02ECD0E8475E0DE8A72CE6` |
| Installed editions | Steam and Microsoft Store; both DLL hashes verified independently in each `Halo_MCC_VR` folder after install |
| Preserved priors | `out/deploy-backups/1c6101f-steam-before-954359b-...`, `...-store-before-954359b-...` |
| Accepted run | Steam edition, VirtualDesktopXR 1.0.10, Meta Quest 3, 120 Hz panel; Halo 4 window `17:20:46`–`17:21:54` on 2026-08-06 |
| Headset result | **ACCEPTED** — user: "itested halo 4 i think the controls work" |
| Preserved evidence | `out/test-runs/954359b-halo4-c1-controller-steam-pass-20260806-172046` |
| Preserved log SHA-256 | `07B3030B41662411D1C1235348D61EB62F8AD80624E8873095FF4568806BBBE6` |

**What the accepted log proves, line by line.** The run is in the Steam
install's `halo3xr.log.prev` (the live `halo3xr.log` had already rolled to a
later Halo 3/Reach session), preserved above before the next launch could
overwrite it.

- Header: `source 954359b7f786b78c76824b662ead3c1fc8cd7917 ... compiled
  Aug  6 2026 16:25:08` — the exact installed bytes, matching the installed
  DLL's own timestamp.
- `MCC edition: Steam`, `OpenXR runtime: VirtualDesktopXR 1.0.10`,
  `headset: 'Meta Quest 3' (vendor 0xFFFFD23E)`, `panel is running at 120.0Hz`.
- `17:20:46.465 Title adapter: detected Halo 4 (halo4.dll); shared
  virtual-controller transport is enabled; Halo 4 camera, render, aim/movement
  transforms, HUD, haptics, lifecycle, and runtime hooks remain disabled` —
  the transport line, exactly as designed.
- The pinned-identity line printed `PE timestamp 0x68A0E7BF, SizeOfImage
  0x04A3F000, H4EK build 2023.06.27.176405.1-Release`, matching the pinned
  identities section of this document.
- `controller edge: A / Y / B / Menu/Start` recur through the whole Halo 4
  window, so the shared virtual-pad transport was live in Halo 4 specifically.
- **`fps ... (stereo off)` for the entire Halo 4 window.** No stereo, no camera
  ownership, no hook — the negative half of the claim, which is the half that
  actually mattered.
- Zero warnings and zero errors between `17:20:46` and `17:21:54`;
  `stalls=0 worstStall=0ms orderFailures=0`, and no load bounce or kick to
  menu on either the entry or the exit transition.

**Scope of the acceptance, stated so it is not overread.** What is proven is
that adding Halo 4 to the registry changes nothing else and that the gamepad
transport reaches Halo 4. The transport itself is a process-wide XInput hook
installed at DLL load and shared with the other titles, so this result is a
weak test *of the transport* and a strong test *of the inertness*. The Halo 4
window is ~68 s and menu-heavy; no long level-load/exit cycle was exercised in
Halo 4, so the load-bounce gate remains unexercised for this title. That is
C-H4-2's business, behind the level-load gate.

**Incidental cross-title regression evidence, on these exact bytes.** The same
session went on to Reach (`17:22:09 Reach camera core armed`), and the
follow-on ~1 hour session on the same DLL (`17:47`–`18:50`) detected Halo 3 and
Reach as supported titles across nine transitions with **303** `stereo on`
windows. The additive `else if` branch broke neither shipped title.

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

### C-H4-2 — level-load gate + cold observation (RAN 2026-08-07, log-verified; explicit acceptance pending)

| Identity | Value |
| --- | --- |
| Source | `3656da999a581a4b5acdbeade22a4c743925eb9a` |
| `halo3xr.dll` SHA-256 | `ABCBE8232031D9019949611A3B842CB2A74C399148E43F241AB256FB61371EAC` |
| Candidate package | `out/candidates/3656da9-reach-fp-parity-20260807-135318906Z` |
| Ran on | Steam edition, VirtualDesktopXR 1.0.10, Meta Quest 3, 120 Hz panel |
| Halo 4 window | `09:03:47`-`09:04:59` |
| Preserved evidence | `out/test-runs/3656da9-halo4-c2-steam-20260807` |
| Preserved log SHA-256 | `3D1F18F390450F7ABDC1EA6536EB6C1407A8CA24981765C2E92F8B0AFEC85037` |

**The log's first line names this exact source**, so the result is against
the intended bytes.

**Both halves of the candidate did exactly what they were built to do, and
this is the first Halo 4 level load ever exercised with the mod running.**

1. **The gate held through the loading screen.** Halo 4 was detected at
   `09:03:47.382`; the gate then reported `holding install` at 47 ms, 2062 ms
   and 4078 ms with `frozen seen=1, still run=1 → 41 → 81, change run=0` —
   the engine's player-view memory sat still for the whole load, exactly as
   the frozen half of the proof requires.
2. **It opened on the real transition, once.** At `09:03:53.326`:
   `the engine's camera was frozen and has now started ticking (5906 ms); the
   level is running, so installing here instead of during its loading
   screen`. One open event in the session, via the frozen-then-ticking path —
   not the 6 s already-running fallback.
3. **The cold observation PASSED against the loaded image** 48 ms later at
   `09:03:53.374`: pinned PE identity matched, **all 4 E-H4-4 anchors unique
   at their pinned RVAs**, and the player-view array `0x30AD1C0` (stride
   `0xAD0`) plus `g_view_stack_top` `0xE84634` **decoded correctly from the
   loaded bytes**. Zero `FAIL`, zero `WITHHELD`. This is the first time any
   Halo 4 RVA in this repository has been verified against the running game
   rather than the file on disk.
4. **No load bounce, no kick to menu.** The level loaded and ran to the
   player quitting from in-level at `09:04:54.778` (Alt+F4). The gate then
   re-armed on teardown at `09:04:57.861`, so the next install must re-earn
   its proof.
5. **Frame behavior is clean and unchanged in character.** `fps 120 (stereo
   off)` for the whole gameplay stretch — flat by design — and `missed`
   froze at **2139** from `09:04:02` through `09:04:42`, i.e. essentially
   zero missed frames across ~50 s of play; every accumulated miss predates
   the level, from the 61-62 fps menu period. `stalls=0 orderFailures=0`
   throughout. The one `STALL` line is at `09:04:58.827`, after Alt+F4, and
   is the game shutting down.

**What this run does NOT cover.** The player quit from inside the level
rather than exiting to the menu and loading again, so the **repeat-load
cycle is still unexercised** — and that is precisely the shape of the
recorded load-bounce bug (the *first* load back into a title whose module we
previously touched). Halo 3, ODST and Reach never became the active title in
this session, so it carries no cross-title regression evidence for them.

### C-H4-2 — as designed

**One behavior, stated plainly: Halo 4 stops being a title nothing checks.**
It joins the level-load gate the other three titles already use, and once
that gate proves a level is actually running, the mod verifies — exactly once
per module instance, read-only — that the `halo4.dll` in memory is the build
this repository's evidence describes. **No hook is created, no camera is
owned, nothing is written into the game.**
`Halo4Adapter_RuntimeHooksPermitted()` stays `false`.

**Why the gate half is not cosmetic.** Before this candidate, the worker's
gate `switch` had no `Halo4` case, so `activeLevelRunning` kept its
initialiser of `true` for the whole Halo 4 session. Two things then ran
against a module that might still be loading a level: the draw-distance
reassert, which resolves `render_far_clip_distance` by a whole-module name
scan and **writes** it, and the process-wide safe-frame publication. That is
precisely the "touch nothing while a level loads" invariant that
`docs/ODST-LEVEL-LOAD-LOCKOUT.md` records eight bounced loads for. Halo 4 was
outside it by omission. (Halo CE and Halo 2 still take the `default` branch
and keep their pre-existing behavior; they have no adapter work and are out
of scope here.)

**What the cold observation measures**, all against the loaded image and all
from the 50 ms title worker — never from Present or a render callback:

1. the module size equals the pinned `SizeOfImage`;
2. the loaded PE headers carry the pinned machine, timestamp and image size;
3. each of the four E-H4-4 anchors matches **exactly once** in the loaded
   image (a second match anywhere fails the anchor, as it should);
4. each anchor sits at its pinned RVA;
5. the three anchors carrying a RIP displacement **decode** to their pinned
   data anchors — the constructor's `lea` to the player-view array
   `0x30AD1C0`, and push and pop independently to the same
   `g_view_stack_top` `0xE84634`;
6. the module is still mapped at the same base after the scan.

Anything short of all six logs a named FAIL and changes nothing. The verdict
function is pure data (`Halo4ColdObservationPass`) so `core_tests` can prove
each field fails closed on its own, and the anchor table is validated
offline: every pattern parses, every RIP offset indexes bytes inside its own
match, and the named indices bind the constructor to the array and push/pop
to one stack top.

**Ordering that is load-bearing, not incidental.** The preflight takes a
refcount pin and scans the whole image. Both are touches. It therefore runs
only on a tick where the gate was actually sampled *and* reported the level
running, and it is **withheld entirely when the gate failed open** — a gate
that could not resolve the player-view array cannot see the module's loading
state, and that is exactly the stale-evidence case where scanning is least
justified. The withholding is logged. A failed pin does **not** consume the
one attempt; it retries, because a permanently-consumed attempt on a
transient loader race is how the level-load gate's own one-shot defect hid
for two builds.

**Expected headset result.** Halo 4 still plays entirely stock and still
renders flat — that is the design, not a shortfall. What is new is in the
log: a `Halo 4 level-load gate:` line, and one `Halo 4 cold observation
PASS`/`FAIL`/`WITHHELD` line. **This is the candidate that finally exercises
a Halo 4 level load and level exit**, which C-H4-1's ~68 s menu-heavy window
never did. Halo 3, ODST and Reach are untouched by construction: the gate
change is additive per-title, and the observation is Halo 4-only.

### C-H4-3 — the per-eye camera core (HEADSET-FAILED 2026-08-07)

**The first Halo 4 hook candidate.** Its headset run proved sustained wrapper
replay but produced a black headset because no eye image was captured. It did
not prove head tracking, projection correctness, or camera parity.

| Identity | Value |
| --- | --- |
| Source | `2987dc217b43094e49ce09c5bb32ed960bd96b81` (branch `feature/halo4-bringup`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON, Halo4 ON |
| Candidate package | `out/candidates/2987dc2-reach-fp-parity-20260807-144434557Z` |
| `halo3xr.dll` SHA-256 | `9AFE77E2A9BA13691A59EF520721ABFDA1D3D5DF875F21D99B161390BB9C4ED5` |
| `halo3xr_launcher.exe` SHA-256 | `930BEA232BFC3F8010BC2B385834DEBF796CD3DBEC02ECD0E8475E0DE8A72CE6` |
| Installed editions | Steam and Microsoft Store; both DLL hashes verified independently in each `Halo_MCC_VR` folder after install |
| Preserved priors | `out/deploy-backups/abcbe82-steam-before-2987dc2-...`, `...-store-before-2987dc2-...` |
| Headset result | **FAILED** — black headset, no head tracking. Root cause measured, see below. |
| Preserved evidence | `out/test-runs/2987dc2-halo4-c3-steam-blackscreen-20260807-094854` |
| Preserved log SHA-256 | `1C10257061F2ECD0CF610575113BF1A7D20502A86003EC74A4987E18A4CD8943` |

**RESULT: the setup+wrapper replay ran; the capture half did not. Risk 1 fired
exactly as written.** Steam edition, VirtualDesktopXR 1.0.10, Meta Quest 3,
120 Hz panel; Halo 4 window `09:48:20`-`09:48:47`. The log's first line names
`2987dc2`, so this is the intended bytes.

What the run proves, in order:

1. **Every install proof passed and the hooks went in.** The gate held the
   loading screen (`holding install` at 47/2062/4094 ms), opened on
   frozen→ticking at 5656 ms, the cold observation PASSED, and
   `Halo 4 camera core installed (generation 1)` names both pinned RVAs and
   records that the loop's own call targets agreed. The core armed ~0.9 s
   later and emitted the historical `head tracking, stereo, and 6DOF ON`
   label. Source audit later proved that label false: this candidate never read
   HMD head pose.
2. **The double wrapper replay ran.** `Halo 4 stereo: 243 owned pairs, 0 stock
   windows` sustained for ~20 s — ~121 claimed transactions per second at
   `fps 120 (stereo on)`, with **zero** rejections of any kind. That proves code
   execution and transaction liveness, not that the observer projection or
   camera geometry was correct. No crash, no load bounce, no kick to menu.
3. **Not one eye was ever captured.** `M2 RASTER: no internal scene-color RTV
   redirect occurred; refusing fake eye copy` at `09:48:27.531`, then
   **486 uncaptured eyes against 243 owned pairs** — exactly two per pair,
   i.e. 100%.
4. **That is the black screen, and it completely explains layers=0.** With no eye
   image there is no projection layer, and because stereo was on there was no
   flat screen layer either: `status: session=focused shouldRender=1
   **layers=0**` for the entire Halo 4 window. Zero submitted layers is a black
   headset with nothing to assess — which is precisely what the user reported.
   Later source audit proved C-H4-3 through C-H4-5 never read the head pose, so
   this run cannot be cited as head-tracking evidence.

**The capture defect fully explains this run's black output.** It is in
`VR_RedirectRenderTargets`' scene-target discovery, which identifies the eye
image by **Halo 3's exact resource signature**: full-backbuffer-size
`R8G8B8A8_TYPELESS` at slot 0 carrying `RENDER_TARGET|SHADER_RESOURCE|
UNORDERED_ACCESS`. Halo 4 never binds that shape. This was named as risk 1
before the run and the log was instrumented to separate it from "not rendering
twice", which is what made it a five-minute diagnosis rather than a hunt. It
does not establish that the camera/projection geometry was correct.

**Two smaller facts worth keeping.** `renderWindow p95` was 5.93 ms while
rendering the scene twice, against 16.22 ms in the menu beforehand — the
double render is not obviously expensive, though nothing was being captured so
this is not yet a fair cost measurement. The FOV log ran, but it was based on
fields later proven not to be tangents; it is not evidence that the FOV path
worked.

### C-H4-4 — identify Halo 4's scene target, and never present a black headset (HEADSET-FAILED 2026-08-07)

| Identity | Value |
| --- | --- |
| Source | `68daa2730be1ff7c4ff37221d143f10b9425396d` (branch `feature/halo4-bringup`) |
| Candidate package | `out/candidates/68daa27-reach-fp-parity-20260807-145817666Z` |
| `halo3xr.dll` SHA-256 | `FD976175D1B2BC9899CEBBE7866561056AD0ECEA2819DCA86D406C4C596306BE` |
| `halo3xr_launcher.exe` SHA-256 | `930BEA232BFC3F8010BC2B385834DEBF796CD3DBEC02ECD0E8475E0DE8A72CE6` |
| Installed editions | Steam and Microsoft Store; both DLL hashes verified independently after install |
| Preserved priors | `out/deploy-backups/9afe77e-steam-before-68daa27-...`, `...-store-before-68daa27-...` |
| Headset result | **FAILED** — captured an early deferred target; unlit meshes, no lighting, shadows, post-processing, or HUD |
| Preserved evidence | `out/test-runs/68daa27-halo4-c4-steamvr-unlit-20260807` |
| Preserved log SHA-256 | `BFC239487725A2B706D0F1514F706D8C384B0CF9C33C828061BA333EEAC65A9C` |

Three changes, no camera-core change: C-H4-3 proved that the hooks and wrapper
replay execute, not that camera geometry or tracking is correct.

1. **A self-arming scene-target census.** The existing `fsr_probe` diagnostic
   already logs each distinct scene-scale render target once per eye context.
   It now **self-arms whenever a per-eye redirect scope is active and no scene
   colour target has ever been learned** — that combination *is* the discovery
   failure — and while self-armed it walks **every bound slot**, not slot 0
   alone, since assuming slot 0 is the assumption that just failed. It also
   logs the RTV's own view format and sample count. No config flag: handing
   this user a config experiment to prove a mod bug is forbidden here, so the
   answer has to arrive from a normal run. Bounded to 96 distinct shapes and
   to the failing case only. Lines are tagged `SCENEPROBE:`.
2. **A relaxed Halo 4 discovery rule**, the smallest relaxation that can still
   only name a final scene image: Halo 4 only, slot 0, backbuffer-sized,
   single-sampled, `RENDER_TARGET|SHADER_RESOURCE` (dropping Halo 3's UAV
   requirement), and an 8-bit RGBA/BGRA format — MCC's backbuffer here is
   `R8G8B8A8_UNORM` (fmt 28), so the final composited scene image is in that
   family, while anything wider is an HDR intermediate with tonemapping still
   ahead of it. Whichever rule matches now logs **which rule and the full
   shape it matched**, so a wrong latch is as diagnosable as no latch.
3. **A loud flat-screen fallback.** After 240 consecutive uncaptured eyes
   (~1 s at 120 Hz) the core stops claiming transactions, logs
   `Halo 4 stereo DISABLED: … that is a black headset, which is worse than
   flat`, and hands the flat screen back. The latch clears with the module
   generation. This is not a silent degrade — it is the loudest line in the
   log — and it means a future capture failure costs the player a flat screen
   instead of a black void.

**Expected headset result.** Either Halo 4 is in stereo, or it is flat with a
named reason plus a `SCENEPROBE:` census that identifies the real target.
Black is no longer a possible outcome.

**RESULT: capture now happens, but it captured the WRONG target.** Steam
edition, **SteamVR/OpenXR 2.17.6, PSVR2** (a runtime and headset change from
every previous Halo 4 result), Halo 4 window `10:22:04`-`10:22:45`; preserved
at `out/test-runs/68daa27-halo4-c4-steamvr-unlit-20260807`. The headset showed
**unlit meshes with no lighting, shadows, post-processing or HUD**.

- `Halo 4 stereo: 244 owned pairs, 0 stock windows, **0 uncaptured eyes**` —
  capture is fixed, and `layers=1` throughout, so the black-screen defect is
  gone and the fallback never had to fire.
- `M2 RASTER: learned scene-color RTV ... via the Halo 4 relaxed rule
  (2912x2100 fmt=29 viewfmt=29 bind=0x28)` — `fmt 29` is
  `R8G8B8A8_UNORM_SRGB`, `bind 0x28` is `RENDER_TARGET|SHADER_RESOURCE`, full
  backbuffer size, `rtCount=1`.
- **The census emitted exactly ONE line**, and it is the target we then
  latched.

**Two separate mistakes, both mine, both in the discovery and neither in the
camera core.** First, the rule latched on the FIRST qualifying target bound in
the eye. In a deferred pipeline the first full-size colour target is an input
to the composite, not the composite — everything after it (lighting, post,
HUD) writes to other targets we never redirected, which is exactly "unlit
meshes". Second, the census was armed only while nothing had been learned, so
**latching switched the census off**: one line was logged, describing the one
target that was already wrong, and the rest of the pipeline was never
described. The diagnostic could not contradict the decision it was meant to
check.

### C-H4-5 — pick Halo 4's scene target by watching a whole eye (HEADSET-FAILED 2026-08-07)

| Identity | Value |
| --- | --- |
| Source | `89b89efbed581f9b303f513ba88c0d489ec4681d` (branch `feature/halo4-bringup`) |
| Candidate package | `out/candidates/89b89ef-reach-fp-parity-20260807-153209741Z` |
| `halo3xr.dll` SHA-256 | `72CE654FEAA1B8D23F0F68D9C0E506D15AD7FD9CE893975506EB57A3CD71B49E` |
| Installed editions | Steam and Microsoft Store; both hashes verified independently after install |
| Preserved priors | `out/deploy-backups/fd97617-steam-before-89b89ef-...`, `...-store-before-89b89ef-...` |
| Headset result | **FAILED** — lit, captured stereo sustained, but the 3D/FOV was malformed; no head pose, 6DOF, or HUD |
| Ran on | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2, 120 Hz; Halo 4 window `10:42:33`-`10:43:10` |
| Preserved evidence | `out/deploy-backups/72ce654-steam-before-4fc3c84-20260807-155606716Z/halo3xr.log` |
| Preserved log SHA-256 | `775066A161B277528D337869A074677185CBAE7975E974145F6AA52EF7574E06` |

No camera-core change again. The setup+wrapper transaction and scene capture
now sustained on two runtimes/headsets; camera geometry and tracking still did
not pass.

1. **Halo 4 no longer decides at bind time.** It observes an entire eye,
   remembers the **LAST** qualifying full-size colour target bound in it, and
   latches that only once **two consecutive eyes name the same one**. The last
   target written inside the eye scope is the composited result; the first is
   an input to it. Qualification is unchanged and still narrow (backbuffer
   size, single-sampled, `RENDER_TARGET|SHADER_RESOURCE`, 8-bit RGBA/BGRA).
   Nothing is redirected during a learning eye, which costs two or three
   uncaptured eyes at level start - far under the 240-eye fallback threshold.
2. **The census outlives the decision.** It now keeps logging for six eyes
   after Halo 4 latches, across every bound slot, so the full ordered pipeline
   is in the log whether or not the pick was right. A wrong pick is now
   selectable from the census instead of requiring another guess.
3. **Discovery resets at every backbuffer resize and presentation detach**, so
   a new level re-learns rather than inheriting a dead pointer.

**RESULT.** The last-target rule solved the deferred-capture defect: after two
learning eyes, the run sustained `layers=1`, zero steady uncaptured eyes, and
roughly 108-120 fps with lit/post-processed scene images. The user nevertheless
rejected the result as weird/malformed 3D with awful FOV, no 6DOF ("the ground
follows my head"), and no HUD. Source audit then found zero Halo 4 head-pose
reads, while the FOV fields were being used with the wrong representation.

**If it is still wrong**, the `SCENEPROBE:` census now lists every scene-scale
target Halo 4 binds during an owned eye, in bind order, with resource format,
RTV view format, bind flags, sample count and slot. Pin the right one from that
list. Do NOT re-theorise.

**Known limitation, now measured rather than inferred.** The per-eye
scope is the render wrapper `0x1222F4`, and E-H4-5 places Halo 4's UI bracket
**after** the per-window loop, outside that scope. C-H4-6 proved that replaying
the enclosing `main_render_game` as an eye scope is unsafe. HUD must instead
use its own title-native, H4EK-proven CUI boundary; it must not widen the camera
transaction again merely because the outer function contains UI.

**Not a change, but worth recording because it was asked:** the 32-slot source
view cache and intermediate texture pool from the `f4c641f` baseline are
title-agnostic (`src/common/view_cache_logic.h` contains no title reference at
all) and were already live in every Halo 4 run - `upload reuse: views 0/32
resident ... intermediates 0/32 live = 0 KB`. Both read zero because the eye
caches are directly samplable and skip the intermediate pool entirely, which is
the same thing the baseline measured. Per
`docs/CURRENT-STATE.md` this is a correctness/allocation-churn fix worth about
0.15% of a frame, **not** a frame-rate fix, and 32-deep on the display path
would cost 267-355 ms of latency - do not extend it there.

**One behavior: Halo 4 renders the scene twice per frame, once per eye, from
two cameras the engine derives itself.**

**The design, and why it is smaller than Reach's.** E-H4-5 proved that Halo 4
produces every camera artifact — rasterizer camera, projection, render pair
and constant bank — inside ONE straight-line setup call per window, so writing
the published element afterwards is stale by construction. Rather than rebuild
those four artifacts per eye the way Reach must, this core substitutes setup's
**input** and re-runs the engine's own unmodified producer:

1. hook setup `0x374C84` purely to capture its six arguments at the one proven
   call site;
2. hook the render wrapper `0x1222F4`; inside it, per eye — write that eye's
   camera into the observer result, call the engine's own setup through the
   trampoline, call the engine's own render transaction through the
   trampoline, capture the eye;
3. restore the observer bytes and run setup once more, so the UI bracket that
   follows the loop in `main_render_game` sees the mono camera it expects.

Nothing on our side computes a projection, a render pair or a constant bank.
Halo 4 does, twice.

**What must be true before a single hook is created** (all of it, or Halo 4
stays stock and flat, loudly):

- C-H4-2's cold observation **PASSED for this exact module generation** — a
  new base or generation re-earns it;
- all four E-H4-6 camera anchors match **exactly once** in the loaded image
  and at their pinned RVAs;
- their three rip decodes land on their pinned data anchors, with the loop and
  setup independently deriving the **same** element `0x10DAFE0`;
- the per-window loop's own two `call` instructions target the two functions
  being hooked — the edge that makes this a proven caller relationship rather
  than two addresses that matched a pattern;
- both hook sites are in-image and `halo4.dll` is still mapped at the observed
  base;
- the install is all-or-nothing: a failed second `MH_CreateHook` or a failed
  `MH_EnableHook` backs the first one out.

**Per-transaction gates, every frame.** The wrapper detour renders stock
unless the core is armed, the caller's return address is exactly `0x122CE7`,
the element argument is exactly the pinned `0x10DAFE0`, the window index is 0
(a split-screen guest keeps its flat render), and the immediately preceding
setup call was for this same view and window. The observer read/write is
SEH-guarded, and the camera basis is validated before it is used. C-H4-5's
historical implementation rerendered stock after a partial eye attempt. C-H4-7
supersedes that unsafe mixing rule: a failure before mutation runs stock once;
after a claimed transaction starts, `__finally` restores mono state, both eye
and FOV serials are invalidated, that frame is dropped, the core stays armed,
and the next prepared frame retries.

**FOV CORRECTION (2026-08-07).** C-H4-3 through C-H4-6 got the observer layout
wrong. H4EK's `s_observer_result` finisher proves `+0x40` is horizontal FOV,
`+0x60` is aspect, `+0x78` is full vertical FOV
`2*atan(tan(horizontal/2)/aspect)`, and `+0x7C` is the dimensionless
horizontal/reference-FOV ratio (default reference 78 degrees), not a pair of
tangents. The converter copies `+0x78/+0x7C` to camera `+0x28/+0x2C`; the
projection consumes the vertical-FOV field. Writing OpenXR half-angle tangents
into those two observer fields directly explains the malformed C-H4-5 result.

The finished row-vector projection matrix begins at raster projection `+0x78`,
therefore element `+0x100`. Let `Sx=p[0]`, `Sy=p[5]`, `Cx=p[8]`, and `Cy=p[9]`
with `p[11]=-1`; exact raster-edge tangents are
`L=(Cx-1)/Sx`, `R=(Cx+1)/Sx`, `D=(Cy-1)/Sy`, and `U=(Cy+1)/Sy`. The normal
retail setup passes an exact zero center and produces positive scales. C-H4-7
therefore admits only finite `Sx/Sy>0`, `Cx=Cy=0`, and publishes
`halfX=atan(1/Sx)`, `halfY=atan(1/Sy)`. A custom/off-axis or unrecognized
matrix drops that frame because the current symmetric compositor API cannot
represent it honestly. C-H4-7 leaves `+0x78/+0x7C` and every other non-pose
observer byte stock.

**Capabilities published: `Stereo` and `ControllerInput` only.** Aim, HUD,
haptics, room-scale locomotion, IK and the cutscene theatre are deliberately
NOT published — none has Halo 4 evidence, and publishing one would switch on
shared code that has never run in this title.

**The three things most likely to be wrong, named in advance.**

1. **Eye capture may not find Halo 4's scene target.** `VR_RedirectRenderTargets`
   learns the scene-colour RTV by Halo 3's shape (the unique full-resolution
   `R8G8B8A8_TYPELESS` target at slot 0 carrying RTV+SRV+UAV). Whether Halo 4's
   renderer presents that shape is **unverified**. If it does not, the game
   renders twice and neither eye is captured. The log separates that case
   explicitly: `Halo 4 stereo:` reports owned pairs, stock windows **and
   uncaptured eyes** every two seconds, so "not rendering twice" and "rendering
   twice, capturing nothing" cannot be confused.
2. **Temporal passes may cross-contaminate the eyes.** Running setup twice per
   frame rebuilds the constant bank twice; any previous-frame bank or history
   buffer Halo 4 keeps will now see two cameras per frame. This is the same
   hazard that produced Reach's effects eye-desync. The bank builder's
   prev-bank copy internals are recorded as undissected in E-H4-5 and were not
   opened for this candidate.
3. **Cost.** The scene is rendered twice at `resolution_scale`, which is the
   inherent cost of stereo in every title here and was measured as the
   dominant frame cost on the `f4c641f` baseline.

**Historical C-H4-5 outcome.** Halo 4 entered stereo, but head tracking/6DOF
were absent and projection geometry was invalid. No future candidate may call
wrapper execution or pair count alone a camera-parity pass.

### E-H4-7: main_render_game identity/extent proven; eye scope refuted (return ABI corrected 2026-08-07)

The per-window render wrapper `0x1222F4` cannot contain Halo 4's later UI
bracket: the UI runs after the per-window loop. Static evidence proves that the
enclosing `main_render_game` contains both regions. It does **not** make that
stateful outer function a legal per-eye boundary. C-H4-6 treated containment as
re-callability, missed the live return register, and failed in the headset.
The wrapper remains the only runtime-sustained camera/scene boundary; HUD needs
its own later CUI transaction.

| Retail fact | Value |
| --- | --- |
| `main_render_game` | `0x12259C`-`0x123115` (contains the window loop AND the UI bracket) |
| Arguments | **NONE** - its call site marshals nothing |
| Callers | **exactly one**, `call 0x12259C` at `0x122076` |
| Return address | `0x12207B` |
| Return ABI | A status value is returned in `AL`; the caller executes `test al, al` immediately after the call. C-H4-6's `void` typedef/detour was wrong. |
| Entry signature | `48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 48 F9 FF FF 48 81 EC 90 07 00 00 48 C7 45 C8 FE FF FF FF` - **UNIQUE** |
| Call-site signature | `E8 ?? ?? ?? ?? 84 C0 75 07 E8 ?? ?? ?? ?? EB 0A B9 01 00 00 00` - **UNIQUE** at `0x12206D`, its rel32 at +0x0A decodes to `0x12259C` |

Both were measured over `.text` of the pinned image. The single caller is what
lets the detour additionally require its exact return address, exactly as the
setup detour does.

The call-site signature itself continues `84 C0 75 07`: `test al, al` followed
by a conditional branch. That is direct proof that the return register is live,
even though C-H4-6 declared the function pointer and detour `void`. No future
hook may consume this boundary until it preserves that status exactly. Identity,
extent, no-argument ABI and the UI placement remain proven; re-callability was
never proven.

**Refuted C-H4-6 design inference.** Scoping the eye here looked simpler because
setup would run naturally inside each call. Runtime proved that exact design
unsafe; its invalid FOV readback did not establish whether the substitution
survived. Do not reuse the outer scope merely because it contains more drawing
work.

### C-H4-6 — head tracking, 6DOF and a HUD-inclusive eye (HEADSET-FAILED 2026-08-07)

| Identity | Value |
| --- | --- |
| Source | `4fc3c84834162c8154f9ac5e34771b4971c0dc4b` (branch `feature/halo4-bringup`) |
| Candidate package | `out/candidates/4fc3c84-reach-fp-parity-20260807-155605774Z` |
| `halo3xr.dll` SHA-256 | `A6488B4DC15323372BB1D7F93FD55F2323D3A08C5F09E580500A2C0E9915FA90` |
| Installed editions | Steam and Microsoft Store; both hashes verified independently after install |
| Preserved priors | `out/deploy-backups/72ce654-steam-before-4fc3c84-...`, `...-store-before-4fc3c84-...` |
| Headset result | **FAILED** — zero completed stereo pairs and a visible game stall after the first eye; its `NOT TAKING` diagnostic was itself invalid |
| Preserved evidence | `out/test-runs/4fc3c84-halo4-c6-steamvr-failed-20260807-112043` |
| Preserved log SHA-256 | `4BF4992E18A92ACE266AF26D4A4115642348D7C0E6B9B8F2D945175FB5955D4A` |

**Historical bundled hypothesis, not a validated fix.** The user
reported *"its not even 6dof the ground follows my head"*, *"the fov is
awful"* and *"the hud has to be in there"*. C-H4-6 attempted all three at
once even though they are separate behaviors with separate evidence:

> **C-H4-3 through C-H4-5 never read the head pose. A search for
> `VR_GetHeadPose` across the entire Halo 4 core returned zero.**

Those builds took the engine's own camera and applied only the per-eye IPD
split. There was stereo separation but no head tracking and no 6DOF, so the
rendered image never responded to the headset and the world read as
head-locked. And the two-second line reported `243 owned pairs, 0 rejections`
throughout, which counted that **our code ran** and never what **the engine
held** - the precise failure mode `docs/CURRENT-STATE.md` and the "clean
diagnostic = wrong mechanism" rule exist to prevent.

**What C-H4-6 attempted.** None of these claims passed the headset:

1. **`Halo4ApplyHeadLook`**, intended to match Halo 3's `ApplyHeadLook`:
   yaw relative to a recentre reference (the stick still turns the player
   underneath), pitch absolute plus `pitch_trim`, roll measured against a
   horizon-level up so tilting your head leaves the world fixed, and 6DOF that
   decomposes the headset's room-space movement in the head's horizontal frame,
   re-applies it in the game's frame, scales by `world_scale` and clamps. It
   runs once per frame on the mono camera, before the eyes split off it. Halo 4
   turn-stick ownership was not wired, so the old text's turn claim was false.
2. **The eye scope moves to `main_render_game`** (E-H4-7 above), so each eye
   renders the window loop *and* the UI bracket - the HUD is inside the eye by
   construction rather than excluded by it.
3. **A camera-claim diagnostic was added.** The setup detour reads the element's
   forward and tangent pair back **after the engine's own converter has run**,
   and the two-second line reports `tangents requested X/Y, engine holds X/Y ->
   TOOK / NOT TAKING` plus the engine's `fwd.z`. A substitution that does not
   land can no longer hide behind a healthy pair count. The engine's own camera
   basis is also logged once per generation, so the Blam Z-up assumption the
   head-look depends on is confirmed against real values rather than inherited
   from the other titles.

**Historical expected result.** Halo 4 with real head tracking and 6DOF - looking
around moves the view and the world stays put - stereo depth, the headset's
FOV, and the HUD present. The `Halo 4 stereo:` line should read `TOOK`.

**RESULT: FAILED. Commit `7d58a68` reverted the whole C-H4-6 behavior before
the next candidate.** Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz; the
Halo 4 window began at `11:20:19`, and the first owned attempt began at
`11:20:26`. The first log line names source `4fc3c84`, so these are the intended
bytes.

1. The level-load gate, loaded-image proof and six camera anchors all passed.
   The engine's live camera also confirmed the expected Z-up basis, and the
   headset pose was available: the core logged its `-77.2` degree recenter.
2. The widened transaction never completed one pair. It reached the first
   learning eye and only the beginning of the second, then the runtime mode
   bounced `unsupported -> shell`; one second later the stall watchdog reported
   that the visible headset was holding the last submitted frame. Every later
   interval reported **0 owned pairs**.
3. The new `NOT TAKING` readback is **not valid evidence**. The requested values
   were OpenXR half-angle tangents written into fields that actually hold full
   vertical FOV and FOV ratio. The engine values were read after every natural
   setup, including stock windows, with no matching eye/frame serial, and the
   converter applies its own native FOV processing. Comparing
   `1.8418/1.3290` directly with `1.4361/1.2077` therefore compares different
   representations and potentially different transactions. It proves only
   that the diagnostic was wrong, not that the observer write was ignored.
4. The pinned call site exposes an independent ABI defect: it executes
   `test al, al` immediately after `main_render_game`, but C-H4-6 declared the
   original function, its body and the detour `void`. The detour therefore did
   not preserve a live return status. That independently invalidates the run
   and is a plausible contributor to the title-state bounce/stall; the log
   cannot isolate it as the sole cause.
5. E-H4-7 still proves that `main_render_game` contains the UI bracket. It does
   **not** prove that the function is re-callable twice. Because the return-ABI
   defect independently invalidates the run, the stall cannot honestly prove
   that the engine function itself is never re-callable; it proves only that
   C-H4-6's exact outer transaction is unsafe.

The recovery point is C-H4-5's sustained per-window wrapper transaction, not a
new tuning guess. C-H4-7 first repairs and proves stock projection geometry on
that boundary. Only after its headset result may C-H4-8 add head pose/6DOF in
the same wrapper transaction. HUD remains a separate later feature and must not
widen the render scope again without its own title-native CUI boundary and
runtime proof.

### C-H4-7 — stock-projection exact-serial stereo geometry (OFFLINE-PASS 2026-08-07; headset-PENDING)

| Identity | Value |
| --- | --- |
| Source | `dbf1382219907c514dcd80650e43d6829821c8b3` (branch `feature/halo4-bringup`) |
| Build | Clean Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON, Halo4 ON |
| Candidate package | `out/candidates/dbf1382-halo4-c7-stock-geometry-20260807-173743014Z` |
| `halo3xr.dll` SHA-256 | `7A7E1448BC38405943C5F20F3C7E4E6340B01AE58B54A6C0C0623FBADD2C0C0E` |
| `halo3xr_launcher.exe` SHA-256 | `81BD9A7BECEA92EDA586D1A82A2D570F7728846CEDCF8BB25849EA0E50F6C021` |
| Installed editions | Steam and Microsoft Store; package and both installed DLL/launcher hashes independently matched |
| Preserved failed C-H4-6 installs | `out/deploy-backups/a6488b4-steam-before-dbf1382-20260807-173743858Z`, `...-store-before-dbf1382-20260807-173743858Z` |
| Headset result | **PENDING** — package manifest is explicitly unaccepted; C-H4-1 remains the accepted pointer |

**One player-visible claim:** the C-H4-5 lit scene pair has sane, mutually
consistent stereo geometry when Halo 4's own FOV inputs and finished projection
are left authoritative. This candidate does not apply the HMD midpoint's
rotation or translation. Head tracking, 6DOF, HUD/CUI, turn/look ownership,
aim, reticle, hands, and weapons are explicitly absent.

The runtime keeps C-H4-5's sustained setup+wrapper boundary and last-target
capture, with these measured invariants:

1. The OpenXR frame path publishes an H4-only, lock-free snapshot containing
   the exact prepared serial and the two eyes' midpoint-relative position/cant.
2. The transaction mutates only observer position `+0x00`, forward `+0x28`,
   and up `+0x34`. Every other observer byte, especially full-vFOV `+0x78` and
   FOV ratio `+0x7C`, is restored from the stock snapshot unchanged.
3. After each stock setup call, element position/forward/up at
   `+0x00/+0x0C/+0x18` must match the requested bytes exactly. The finished
   projection at element `+0x100` must pass the normal zero-center H4 matrix
   contract above before that eye renders.
4. Both eye images and both half-FOV publications carry the same nonzero
   prepared serial. The compositor admits the pair only when all four stamps
   match the frame being submitted; a prior redirected cache cannot be stamped
   unless that exact raster-eye scope is active now.
5. Headset publication additionally requires exact swapchain acquire, wait,
   both eye uploads, release, and an `xrEndFrame == XR_SUCCESS` that actually
   queued the H4 projection. A failed acquire or completed-release eye/projection
   miss drops only that frame; a non-completing wait/release is an OpenXR
   ownership failure and enters the existing named runtime-recovery path rather
   than pretending the image is reusable.
6. All mono restoration runs in `__finally`. A failure before mutation renders
   stock once. A failure after mutation begins invalidates both eye/FOV stamps,
   drops only that frame, leaves the camera core armed, and retries next frame.
   Only repeated, actual eye-capture misses may trip C-H4-5's loud flat fallback.

Offline verification passed: Release configure/build, `core_tests`, and the
Reach consistency gate. The clean package step repeated all three before
creating and installing the exact identity recorded above.

**Geometry-only headset pass.** Test at 90 Hz first so the separately open
120-Hz pacing tail cannot confound the result. With the head held near center,
the scene must be lit/post-processed, visibly distinct in depth, free of the
grotesque stretch/eye mismatch from C-H4-5, and free of stalls/title bounce.
`layers=1`; steady two-second telemetry must show completed pairs > 0,
`geometry TAKING`, two camera and two projection readbacks per pair, exact-zero
camera errors, center `0/0`, zero drops/uncaptured eyes, and `Halo 4 C-H4-7 XR
publish` reporting submitted pairs > 0 with recoverable drops 0. A narrower stock
H4 raster is allowed here and belongs to the later coverage milestone. The
world following physical head motion, absent 6DOF, and absent HUD are expected
in C-H4-7 and cannot be used to accept or reject its geometry claim.

### C-H4-7 — RESULT: stereo geometry PASSED (headset-run 2026-08-08)

The user ran the installed `dbf1382` bytes on Steam / SteamVR-OpenXR 2.17.6 /
PSVR2 at 120 Hz; the Halo 4 window ran `05:51:26`-`05:53:08`. Preserved at
`out/test-runs/dbf1382-halo4-c7-stock-geometry-20260808-0553`.

**The geometry claim passed on its own terms.** Steady two-second telemetry read
226-243 completed pairs, `geometry TAKING`, `0 dropped frames`, `0 uncaptured
eyes`, exact-zero camera readback error (`pos 0.000000 fwd 0.000000 up
0.000000`), `center 0.000000/0.000000`, and `Halo 4 ... XR publish` 240-244
pairs submitted with zero recoverable drops, at `fps 120 (stereo on)`. Two
genuinely distinct eye images were measured: `M2 VALIDATION: distinct eye pixels
mean RGB delta=3.925, changed samples=27.1%`.

**The user rejected the experience, for the two reasons the candidate itself
declared out of scope:** "6dof is not working so idk if the stereo 3d is
implemented correctly", and a request for "proper fov like the other halos".
The log confirms both were absent by construction, not broken:
`Halo 4 C-H4-7 stereo geometry ON; head tracking, 6DOF, and HUD remain
intentionally pending`.

**One NEW defect the run exposed, which C-H4-7 did not predict.** The FOV was
not merely stock, it was geometrically wrong at the compositor:

```
[05:50:49.205] M2: eye 0 pose(...) fov L-61.5 R43.4 U53.0 D-53.0 deg
[05:51:33.755] M2 WARNING: the symmetric raster cover does not contain the
               headset's native per-eye frustum, so the whole slice is
               submitted at the cover FOV. Compositors that ignore a custom
               layer FOV (ALVR) will show a doubled image.
               last stock projection half 50.46/41.14 deg
```

Halo 4's stock cover (50.46/41.14 deg) does not contain PSVR2's frustum
(61.5/53.0 deg) on either axis, so the native-FOV crop in `vr.cpp` could not run
and the whole slice was submitted at the wrong FOV. Preserved logs show the
working titles on the SAME headset reaching `cover 61.5/53.0 deg` - an exact
match - because they drive the engine FOV to the runtime's own. That is the gap
C-H4-8 closes.

### E-H4-8: the observer FOV path, measured end to end (PROVEN 2026-08-08)

Disassembled from the pinned retail image (SHA-256 `7C53E7D5...`), and
corroborated against live logged values.

| Retail fact | Value |
| --- | --- |
| Converter | `0x38F014`-`0x38F175`; the copy map at `0x38F074` is inside it |
| Pose copy | position/forward/up copied verbatim, **no scale, no axis permutation** (`0x38F066`-`0x38F091`) |
| FOV scale | **both** FOV fields multiplied by one shared factor (`0x38F0A8`/`0x38F0AC`), stored at `0x38F13E`/`0x38F143` |
| Scale constant | literal float `0.785` at RVA `0xD9560C`; alternative `0.168214291` at `0xD9543C` |
| Scale selector | branch at `0x38F01A`-`0x38F05E` on a global at RVA `0x4969640` (deg->rad, fallback 78.000 deg) compared against `0.0` |
| Net mapping | `element[+0x28] = observer[+0x78] * K`, and the builder treats `element[+0x28]` as a FULL vertical FOV, so `builtHalfY = observer[+0x78] * K / 2` |
| Basis | right-handed, `right = forward x up`, Z-up; projection builder `0x38F658` writes `(right, up, -forward)` |

**The mapping is confirmed live to five figures on two independent values.** The
C-H4-6 log records the engine camera as `tan(1.8295 1.5385)` (those are the raw
observer `+0x78`/`+0x7C` bytes, despite the misleading `tan` label) and the
element as `1.4361/1.2077`. `1.8295 * 0.785 = 1.43616` and
`1.5385 * 0.785 = 1.20772`. Independently, C-H4-7 measured the built half-Y as
`41.14 deg = 0.71805 rad`, and `0.71805 / 1.8295 = 0.39249 = 0.785 / 2`.

**Two things this makes explicit, and one it does not.**

- C-H4-6's `1.8418/1.3290` write was OpenXR half-angle **tangents** placed in
  fields holding a full vertical FOV in radians and an unresolved ratio. Even
  with the right representation it would have landed at `0.785x` its intended
  value. Both errors are now accounted for.
- The `+0x7C` "FOV ratio" field remains **UNRESOLVED**. Its stock `1.5385` does
  not reconcile with the raster aspect `2912/2100 = 1.3867` nor with
  `tan(50.46 deg) = 1.2110`, and retail scales it by the same `K` that a
  dimensionless ratio would not need. **C-H4-8 therefore does not write it.**
- `K` is selected at runtime by a global with no static initializer, so it is
  **not safe to hardcode**. C-H4-8 measures it instead, and is the first build
  to log the raw `observer +0x78 -> element +0x28` pair.

### C-H4-8 — head tracking, 6DOF and native headset-FOV coverage (OFFLINE-PASS 2026-08-08; headset-PENDING)

**Two player-visible claims, reported on separate log lines so either can be
accepted or rejected alone:**

1. **You are inside the world.** The headset's orientation and its room-space
   translation drive Halo 4's camera, so looking and leaning move the view while
   the world stays put.
2. **The image fills the headset correctly, on any headset.** The raster cover
   is solved from whatever per-eye frustum the OpenXR runtime reports, so the
   native-FOV crop can run instead of submitting the whole slice.

**Built on C-H4-7's proven boundary, which is unchanged.** Same setup+wrapper
scope, same exact-serial pairing, same `__finally` mono restore, same
last-target capture, same publication gates.

**Design decision: the head pose is a DELTA, not a replacement.** Halo 3's
`ApplyHeadLook` overwrites forward/up outright, which it can do because
`ApplyVrTurn` also owns the turn stick and feeds `g_gameYawRef`. Halo 4
turn/look ownership is a separate later rung, so replacing the basis would leave
the player unable to turn at all and would discard the accepted C-H4-1 gamepad
behaviour. C-H4-8 instead composes the headset on top of Halo 4's own camera:
yaw about world up relative to a recentre reference, then pitch about the
resulting right axis, then roll about the resulting forward. Pitch and roll need
no reference because a level head is zero. `AGENTS.md` permits a different
implementation reaching the same player experience; this is recorded as that
difference.

**Defect inherited from C-H4-6 and deliberately not repeated.** C-H4-6 honoured
Halo 3's `g_writeUp` (F7) toggle and rewrote `forward` while leaving `up` at the
engine's value. `Halo4ValidateCameraBasis` rejects `|forward . up| >= 0.05`, so
every frame past ~2.87 degrees of head pitch would have failed validation. Halo
3 has no such validator and never showed the fault. C-H4-8 rotates forward and
up together at every step, so orthonormality holds by construction and
`g_writeUp` is intentionally not consulted.

**The FOV cover is measured, not assumed.** The first Halo 4 stereo frame of a
generation renders at the engine's own stock FOV and reads back the finished
projection; that teaches both the gain (`builtHalfY / writtenVerticalFov`) and
the ratio (`tan(builtHalfX) / tan(builtHalfY)`). Every later frame solves
`targetTanY = max(requiredTanY, requiredTanX / ratio)` - the same construction
Reach's proven `SelectReachSymmetricFovCover` uses - applies a 1% margin, and
writes only `observer +0x78`. Because the published half-angles are always the
ones **decoded from the projection the engine actually built**, a wrong write
can never be reported as correct geometry; it shows up as
`contains headset frustum: NO`.

**Failure isolation, per AGENTS.md.** A refused head pose leaves the engine's
own camera and still renders the pair. An unavailable per-eye FOV, or an
unlearned calibration, renders at stock FOV. Neither disarms the core, ends the
session, or drops a frame.

**Lifecycle.** The recentre reference and the FOV calibration are both dropped
in `Halo4ResetTelemetry`, so a level load never inherits a heading chosen during
the previous level nor a mapping learned from a different window layout.

**What is NOT in this candidate:** HUD/CUI, turn/look ownership and
configuration parity, controller aim and reticle, first-person weapons and
hands, vehicles.

**Defect found and fixed during review: the head pose was one frame stale.**
`PublishHalo4RenderSnapshot` was originally placed ABOVE `CaptureHeadPose` in
`vr.cpp`'s prepare block. `CaptureHeadPose` is the only writer of the pose that
snapshot carries, so Halo 4 would have received the PREVIOUS frame's head while
its eye offsets, its solved FOV and the layer pose submitted later all described
the current frame - a full 8.33 ms of extra head latency at 120 Hz plus a
render/layer pose mismatch the compositor reprojects against, which reads as the
world swimming when you turn and is invisible in a clean log. Reach's publish
sits below `CaptureHeadPose` for exactly this reason. The Halo 4 publish was
moved below it, and deliberately NOT gated on `upcomingHeadValid` the way Reach
is: for Halo 4 the head pose is optional, so a tracking dropout costs head
tracking rather than stereo.

**Known risks to watch in the headset, neither of them mitigated in code.**

1. **Cross-eye history contamination (motion blur).** The transaction calls the
   engine's own `setup` twice per game frame, and H4EK
   (`out/h4ek-evidence/camera/camera-producer-chain.md:122-130`) proves setup
   saves current->previous constant bank and computes a bank-position delta. With
   two setups per frame the "previous" bank for the second eye is the first
   eye's, so that delta becomes the IPD. This is the same family as the Reach
   temporal-AA cross-eye desync. It is NOT mitigated here on purpose:
   `out/h4ek-evidence/debugvars/triage.md` records `motion_blur_scale` /
   `motion_blur_max` as present in Halo 4 but with **kind unproven for this
   title**, and Reach proved that zeroing that exact pair naively creates 0/0
   NaNs in `apply_distortions`. Binding them without Halo 4 evidence is
   precisely what `AGENTS.md` forbids. Halo 4 exposes its own
   `motion_blur on/off` command in MCC's Graphics > Screen Effects menu, so the
   zero-risk check is to turn motion blur off there if ghosting or smearing
   appears. C-H4-7 already ran two setups per frame and no ghosting was
   reported, but head motion enlarges the per-eye delta, so this may surface
   now. If it does, it earns its own evidence-backed candidate.
2. **First-person weapon scale.** Halo 3 additionally matches its first-person
   gun/HUD overlay camera to the widened world tangents, or the weapon
   magnifies. **This paragraph originally claimed Halo 4 draws no HUD; that was
   an assumption carried forward from C-H4-5's failure notes and the user
   REFUTED it in the headset on 2026-08-08 ("i can see the hud").** Halo 4's
   CUI arrives inside the captured scene target, so no separate HUD capture or
   redirect is needed the way Halo 3, ODST and Reach each needed one. If the
   first-person weapon appears at the wrong scale against the widened world
   tangents, this is still the first place to look.

**Unproven and carried forward:** one Halo 4 world unit in metres has no
title-native derivation. Halo 4 inherits Halo 3's shared `g_worldScale` default
of `0.33` game units per metre, adjustable live with PageUp/PageDown. Reach and
ODST each carry an independently derived `1/3.048 = 0.32808`; if Halo 4 shares
Blam's ten-foot world unit, `0.33` over-scales head motion and IPD by 0.58%.

**What the log must show for a pass.** Beside C-H4-7's existing geometry line:

- `Halo 4 C-H4-8 head tracking:` tracked frames > 0, `reference captured`, and
  yaw/pitch deltas that move as the head moves.
- `Halo 4 C-H4-8 FOV cover:` `calibration learned`, widened eyes > 0, and
  **`contains headset frustum: YES`**.
- `Halo 4 C-H4-8 FOV converter:` the raw `observer +0x78 -> element +0x28` pair
  and measured `K`. This settles E-H4-8's one open value.
- The `M2 WARNING` about the cover not containing the native frustum must be
  **absent**, replaced by `M2: submitting native per-eye FOV; ... cover
  61.5/53.0 deg` on this headset.

### C-H4-9 — the headset owns Halo 4's look pitch (PITCH PASSED, shot line MISSED 2026-08-08)

Source `0e450d504ef2f37971281fc756f67ae55676e498`, `halo3xr.dll` SHA-256
`33FC9E41612D8AC1A92F4CC1A92E26DFA9BB5B3E4AB5DAA67F33F5C5A31D3579`, package
`out/candidates/0e450d5-halo4-c9-headset-owns-pitch-20260808-121246432Z`,
installed and hash-verified in both editions.

**Result: Steam, SteamVR/OpenXR 2.17.6, PSVR2, 120 Hz.** *"shots don't follow my
view but that doesn't matter, 6dof is working and it looks and runs great."*
Evidence preserved at
`out/test-runs/0e450d5-halo4-c9-look-pitch-steam-psvr2-20260808-0741`, log
SHA-256 `688B06CE1CA05552763FAFEE5669BE4DF4235C9FA526898EC97C5DC15B27862A`.

Parts 1 and 2 PASSED: `head pitch ... (ABSOLUTE, headset owns pitch)`, 242
tracked frames per 2 s, `lean 0.027 world units = +0.020/+0.017/-0.006 xyz`.

**Part 3 (the closed loop) MISSED, with the mechanism measured.** The loop is
alive and converging - `learned direction +1`, mean |error| 1.79 deg across 64
reported windows - but `min step` latches at **2.758 deg** in 39 of them, which
sets the rest band to enter 1.65 deg / exit 4.14 deg. The gun parks up to ~1.7
deg off the view and re-engages only past 4.1 deg; at 20 m that is 0.6-1.4 m,
invisible without a crosshair. Max window error 20.8 deg.

**E-H4-9: the sampling rate mismatch, from the log's own counters.** One window
reports `1354 commanded / 96 parked polls` in 2 s = **~725 XInput polls per
second against a 120 Hz publication**, i.e. MCC polls the pad about six times
per rendered frame while `Halo4StereoTransaction` republishes the engine pitch
once. `AimServoObserve` consequently sees five zero-steps and one whole-frame
step where it expects one step per command, and its deliberate
rise-immediately/decay-slowly rule (`step > minStep ? step : minStep*0.99 +
step*0.01`) latches that lump and holds it. **The fix is to drive the observer
from the publication serial rather than from the poll** - observe once per new
serial, and hold the previous command across the polls that share one frame.
The same hazard applies to any future Halo 4 loop actuated through XInput.

Deferred by explicit user choice to C-H4-12, where a drawn reticle makes the
residual error visible; correcting the sampling with nothing on screen to
measure against would be tuning a number nobody can see.

**C-H4-8 PASSED both of its own log claims and was rejected on one experience
defect.** Its preserved run reads `geometry TAKING`, 137 completed pairs/2s,
`138 tracked frames`, `reference captured`, `lean 0.006 world units (6DOF ON)`,
`276 widened eyes`, `calibration learned (gain 0.3925, ratio 1.3866)`, engine
built `61.75/53.31 deg` and **`contains headset frustum: YES`** — the `M2
WARNING` C-H4-7 exposed is gone. Stereo, head tracking, 6DOF and native FOV all
work. The user's report was narrower: *"the up and down stick is breaking my
orientation on my head — have it working like the other halo games."*

**The defect, stated exactly.** C-H4-8 applies the headset as a DELTA on Halo
4's own camera, so the view pitch is `enginePitch + headPitch`. That is correct
only while `enginePitch` is zero, and it is not: the look stick's vertical axis
drives it, and so does weapon kick. Every degree of engine pitch tilts the whole
world away from the player's real horizon. Artificial pitch fights the inner ear
in a way artificial yaw does not, which is why the same stick's horizontal axis
was not reported.

**Suppressing the stick alone does not fix it, and would break the game.**
Engine pitch that is already non-zero simply stays there with nothing to return
it to level; and because Halo spawns first-person shots along the ENGINE's
camera ray, a frozen engine pitch means every shot leaves level however far up
or down the player looks. The fix therefore has three inseparable parts.

1. **The view takes pitch and roll outright.** `Halo4ApplyHeadPose` keeps only
   the engine's HEADING (`atan2(fwd.y, fwd.x)`) and rebuilds the basis through
   `Halo4ComposeHeadOwnedBasis`, which is Halo 3's `ApplyHeadLook` composition
   term for term — `forward = (cos p cos y, cos p sin y, sin p)`, `up =` level
   up `* cos(roll) +` right `* sin(roll)`, with Halo 3's own ±1.5 rad clamp.
   Orthonormal by construction, so it cannot fail `Halo4ValidateCameraBasis` the
   way C-H4-6's partial rewrite did. Yaw is unchanged from C-H4-8.
2. **The stick's vertical axis never reaches the game again.** A new
   `Game_Halo4OwnsLookPitch()` branch in the XInput hook holds RY. It is
   deliberately narrower than `Game_VrOwnsLookStick`, which zeroes BOTH axes:
   Halo 3 can do that because `ApplyVrTurn` owns yaw and a controller aim loop
   keeps the gun on the VR sight, and Halo 4 has neither yet. Zeroing yaw too
   would leave the player unable to turn and unable to shoot where they turned,
   so the horizontal axis stays with the engine and keeps turning body, aim and
   view together. This is a stated implementation difference, which `AGENTS.md`
   permits, not a degradation.
3. **A closed loop puts the engine's own pitch back under the head**, so shots
   follow the view. Halo 4 has exactly one proven aim anchor at this stage — the
   observer camera the C-H4-7 transaction already reads every frame, which IS
   the ray shots leave along — and exactly one proven actuator, the virtual
   right stick C-H4-1 accepted. `Halo4PitchServoStep` closes that loop on the
   pitch axis alone, reusing the shared `AimServoAxis` rest hysteresis.

**Two quantities are MEASURED, not assumed.**

- **The sign of the engine's stick→pitch mapping.** `direction` estimates it
  from what the engine's pitch actually did after our last command:
  `sign(observed * issued)` is the mapping's own sign, not "was our guess
  right", so the estimate is stable once correct instead of oscillating with the
  value it estimates. A player with inverted look is followed rather than
  fought; a wrong starting value costs a handful of frames, bounded by a ±6
  saturating counter, and is printed in the log.
- **The actuator's resolution.** `ToRawStick` floors every non-zero command at
  `9000/32767 = 27.5%` to clear MCC's inner deadzone, so the engine only ever
  hears "stop" or "at least 27.5%" — the quantised actuator that produced the
  Halo 3/ODST turret wiggle. The shared `AimServoObserve` samples only frames
  whose command was inside that floor region and widens the rest band by the
  measured step, which is the only thing that stops a limit cycle on an axis
  this coarse.

**Fail-closed, on the render thread's own evidence.** The engine pitch is
published once per OWNED frame (window 0, armed, correct caller) with a serial.
The input thread steers only while that serial is advancing; 250 ms without a
new one parks the stick and resets the loop, because commanding against a stale
error is exactly how a runaway starts. `Halo4ResetTelemetry` drops the
publication on every install and removal, so a level change cannot inherit the
previous level's error. A declined poll holds the axis at zero and never falls
back to the raw stick — that would silently restore the artificial pitch.

**One predicate decides all three parts.** `Halo4LookPitchOwned()` is sampled by
the render camera, the XInput hook and the loop, so there is no state where the
view has taken pitch and the stick has not. F2 turns the whole behaviour off
together, returning exactly C-H4-8's additive head pose.

**Known limitation, stated rather than guessed.** Halo 4 has no cinematic
detection with evidence behind it, so an authored cutscene camera's pitch is
flattened to the head's, exactly as C-H4-8 already added head pitch on top of
it. The loop is inert there (Halo ignores look input in a cutscene, so the
command saturates harmlessly and the direction estimate is guarded by a motion
threshold). Cinematic ownership is its own rung.

**What the log must show for a pass**, beside the C-H4-8 lines, now relabelled
`C-H4-9`:

- `Halo 4 C-H4-9 head tracking:` `head pitch ... (ABSOLUTE, headset owns
  pitch)`, plus the per-axis `lean ... = +x/+y/+z xyz` that makes 6DOF provable
  on each axis instead of as one magnitude.
- `Halo 4 C-H4-9 look pitch:` `the headset owns the vertical axis`, a small
  `error`, a `learned direction` of `+1` or `-1`, and `commanded` polls falling
  away to `parked` ones as the gun settles. A large steady error with the stick
  pinned means the engine refused to be steered — a different fault, and it must
  not be reported as head tracking.

### C-H4-10 — motion aim, VR turn and rumble (OFFLINE-PASS 2026-08-08; headset-PENDING)

Source `140e15dcdba983b02bc99444f707f1ef61492c56` (behavior `8395c97`),
`halo3xr.dll` SHA-256
`765D3D7844F863A6755029991EAD22614BE83ECD14DA683EB99D9B787B990A47`, package
`out/candidates/140e15d-halo4-c10-motion-aim-turn-rumble-20260808-130741925Z`,
installed and hash-verified in both editions.

**Two premises the headset corrected first.** The user reported *"i can see the
hud"*, refuting the assumption carried from C-H4-5 that Halo 4 draws no HUD -
its CUI arrives inside the captured scene target, so Halo 4 needs **no HUD
redirect at all**, unlike Halo 3, ODST and Reach which each needed one. And
C-H4-9's shot line missed, which is what this candidate replaces.

**The three shared systems Halo 4 had never been wired into.** Halo 4's registry
row advertised `TitleCapability_None` and nothing ever published a `RuntimeMode`
for it. That silently disabled more than aim: `ApplyControllerHaptics` requires
`Gameplay`/`Vehicle`/`Turret`, and `Game_MoveStickIsLocomotion` decides on the
same mode whether the left stick walks head-relative. This is the identical
fault that cost Reach its rumble until `PublishReachLifecycle` existed.

Halo 4 now publishes `Stereo | ControllerInput | ControllerAim | Haptics |
RuntimeModes | RoomScale`, plus `RuntimeMode::Gameplay` while its core is armed.
`Hud` stays out deliberately - there is no Halo 4 HUD redirect to gate, so
granting it would advertise a path that does not exist. `ArmIk` stays out
because granting it to Reach before its arm solve was proven attached the left
hand to the player's face. `CutsceneTheater` stays out for want of evidence.

**Aim closes on the observer camera.** `Halo4ReadAimReferences` publishes the
yaw reference pair and the engine's whole forward vector from one observer read,
so the input thread can never pair a yaw from one frame with a pitch from the
next - the incoherence Reach's own feedback publication was rebuilt to remove.
That forward IS the ray Halo 4 spawns first-person shots along, so the shared
loop steering it puts the shots on the hand ray.

**Yaw ownership is not optional once the loop runs.** `Halo4ApplyVrTurn` moves
Halo 4's own `gameYawReference` (snap or smooth, from the shared config keys),
and the view now composes from that reference rather than from the engine's live
heading. Reading the live heading while the loop steers it toward the same
reference applies the head's yaw **twice**; `core_tests` pins both the correct
result and the doubled one so the hazard cannot be reintroduced silently.

**E-H4-9 fixed.** The pitch-only fallback (VR aim off) now steps once per new
publication serial and holds its command across the polls that share a frame,
so one observation corresponds to one issued command as the shared servo
assumes.

**Halo 3 state is fenced off.** MCC keeps every title's module loaded and
reloads them all on each menu return, so the shared aim loop's roll-stable
follow, occupied-seat re-origin, turret handling and stall timer are all
explicitly skipped for Halo 4 - it has no vehicle work, and that state would be
another title's. `g_aimSeen` is likewise cleared with the Halo 4 core so it
cannot tell the next title that its camera hook is already running.

**Two things to watch, stated rather than hidden.**

- **Halo 4 has no native-pause detection**, so its runtime mode stays `Gameplay`
  in a pause menu and the left stick keeps the locomotion mapping there. The
  rotation is `(gaze - aim)`, which converges to zero while the loop is
  tracking, so this should be near-identity - but GitHub #9 was exactly this
  class of bug in Halo 3's menus.
- **The floating reticle is the shared PROCEDURAL one.**
  `Game_TitleCapturesAuthoredCrosshair()` is false for Halo 4 by construction,
  so it takes the fail-open procedural path the ODST camera core established.
  Halo 4's own centred reticle keeps drawing inside the captured scene, and it
  reports the middle of the view rather than where the gun points, so expect two
  marks until a Halo 4 crosshair hider earns its own evidence.

### E-H4-11 — the Halo 4 level-re-entry crash, root-caused 2026-08-08

**Symptom.** Exit a Halo 4 level to the menu, then load another Halo 4 level:
the loading screen never finishes and MCC dies. Reported by the user and
reproduced in **two consecutive sessions on two different builds** (C-H4-9
`0e450d5` and C-H4-10 `140e15d`). Evidence preserved at
`out/test-runs/140e15d-halo4-c10-crash-on-relentry-steam-psvr2-20260808-0819`
(both logs plus the WER report).

**WER, identical in both crashes, same bucket `f4cde9f6adbfed8cf2ea8484b541ca79`:**

    Fault Module      halo4.dll 1.3528.0.0, timestamp 68a0e7bf  (our pinned image)
    Exception Code    c0000005
    Exception Offset  0x3b7ddd

**The faulting instruction, disassembled from the pinned image:**

    0x3B7DA5  imul rbx, r14, 0x5F48              ; rbx = index * 0x5F48
    0x3B7DAC  mov  rsi, [rax + r9*8]             ; rsi = this thread's TLS block (gs:[0x58])
    0x3B7DB9  add  rbx, [rcx + rsi]              ; rbx += *(TLS + 0x6A0)
    0x3B7DDD  mov  eax, [rbx + 4]                ; <-- FAULTS

The minidump records the access violation as a **READ of address
`0x0000000000000004`**, so `rbx` was exactly 0: `*(TLS + 0x6A0)` - the engine's
per-thread globals block - is **NULL** on that thread. The surrounding code
calls `object_get`-shaped `0x5DA400` with a type mask and stores a handle at
`[rbx+4]`, i.e. this is Halo 4's own player/unit bookkeeping running on a thread
with no game-thread globals.

**Our code is not in it.** Walking the minidump's faulting thread (id 37052,
9,624-byte stack) gives 73 `halo4.dll` frames, 54 `MCC-Win64-Shipping.exe`, the
usual ntdll/KERNELBASE exception machinery - and **zero `halo3xr.dll`
addresses anywhere on that stack**. At the moment of the crash we additionally
had:

- **no hooks in halo4.dll.** `Halo 4 camera core removed (generation 3)` at
  `08:29:56.806`, 26 s before the fault: both detours disabled AND removed, the
  module reference freed.
- **nothing installed for the new load.** The level-load gate held from
  `08:30:22.735` to the end, reporting `frozen seen=1, still run=304, change
  run=0` - the level's player-view fingerprint never ticked even once, so the
  gate correctly refused to touch the module and the level never started
  rendering. The `STALL: the game has not presented for 1000ms` line at
  `08:30:24.491` is the same fact from the display side.
- **no lasting refcount pin.** `Halo4ModulePin` is a function-local RAII object
  released on every exit path, and the camera core's `FreeLibrary` runs in
  `RemoveHalo4CameraCore` before it clears its state. Verified by reading both.

**The sharpest correlation in the logs.** The crash tracks whether the Halo 4
module generation ADVANCES between entries:

| Session | Entry | Generation | Result |
| --- | --- | --- | --- |
| C-H4-9 | Halo 4 #1 | 1 | played fine |
| C-H4-9 | Halo 4 #2, straight back from the menu | **1, unchanged** | **crash** |
| C-H4-10 | Halo 4 #1 | 1 | played fine |
| C-H4-10 | ODST, then Halo 4 #2 | 3, advanced | played fine |
| C-H4-10 | Halo 4 #3, straight back from the menu | **3, unchanged** | **crash** |

Halo 4 -> another title -> Halo 4 reloads the module and works. Halo 4 -> menu
-> Halo 4 reuses the same module instance and dies. That is consistent with the
NULL per-thread globals the fault shows: the engine tears its thread-local game
state down on exit and the second entry on the same module instance re-enters
without it.

**MECHANISM NOW PROVEN - see E-H4-15.** `0x5F48` is the `fp weapons`
per-user stride and `0x6A0` is that block's TLS offset, both cross-checked
against H4EK. The faulting code indexes `first_person_weapons[user]` while
the block pointer is NULL, and reads the record's `+4` unit field. Halo 4's
first-person weapons globals are simply not present on a level re-entry that
reuses the same module instance.

**What is still NOT proven.** That the fault also occurs with the mod absent. The
decisive test is a no-mod control run of the same exit/re-enter sequence, and it
is the one thing this evidence cannot supply. Everything above establishes that
no frame of ours is executing, that we hold no hooks and no pin at fault time,
and that we never touched the module during the dead load - not that the mod is
causally irrelevant.

### E-H4-12 — the first-person "weird layer": Halo 4 owns a separate FP FOV

Recorded because it is the lead for the hands/gun work and it is already
evidenced, not theorised. Two facts from the kit census above:

- Halo 4 retains `first_person_camera` / `first_person_skeleton` /
  `first_person_models` / `first_person_fov` / `first_person_hide_*` symbols.
- Halo 4 exposes a **purpose-built first-person FOV pair no earlier title had**:
  `render_first_person_fov_scale` and `enable_first_person_fov`.

C-H4-8 widened the WORLD raster cover from Halo 4's stock 50.46/41.14 deg to the
runtime's own 61.75/53.31 deg. Nothing widened the first-person overlay to
match, and this document already warned about exactly that under C-H4-8's open
items: *"Halo 3 additionally matches its first-person gun/HUD overlay camera to
the widened world tangents, or the weapon magnifies."* A first-person layer
drawn at the stock FOV inside a world drawn at the headset's FOV is precisely a
gun and hands sitting in their own wrongly-scaled space.

The write path already exists and is proven: `Game_ApplyDrawDistance` resolves a
Halo 4 debug global **by name** and writes it, gated behind the level-load gate
so it never touches a loading module. The scale to write is derivable from
values the camera core already measures per frame - the stock half-angles and
the solved cover half-angles are both in the C-H4-9/C-H4-10 telemetry - so this
needs no new signature and no new address.

### E-H4-13 — Halo 4's first-person camera/FOV builder, LOCATED 2026-08-08

**This is the "weird layer" the gun and hands live in.** Halo 3's accepted fix
(game.cpp `FpCameraRebuildHook`, the 2026-07-18 "flat-gun fix") describes the
construct exactly: the engine renders the first-person layer - gun, arms, HUD -
through the view's **second camera pair**, rebuilt immediately before each
first-person draw pass, and that rebuild *"forces the tangents to a fixed
viewmodel FOV (publishing `render_first_person_fov_scale`)"*. Without the fix
the layer is drawn identically in both eyes: **a flat mono layer at the wrong
FOV over a stereo world**. Halo 4 has the same construct, and it is now located.

**Chain, derived offline from the pinned image, no running game needed:**

1. `render_first_person_fov_scale` is a debug-var record at **.data RVA
   `0xE81210`**, type `6` (float), whose value slot is **RVA `0xE84678`**.
   Resolvable at runtime by the proven `FindDebugVarFloat` name path - no
   hardcoded address needs to ship. (`enable_first_person_squish` sits directly
   beside it at `0xE8467C`, and `kHalo4ViewStackTopRva` `0xE84634` is in the
   same render-globals block, which corroborates the neighbourhood.)
2. That slot has **exactly three** code references in `.text`, all inside one
   function:

       0x34ED15  movss [rip+0xB3595B], xmm0   ; WRITE  -> 0xE84678
       0x34ED1D  call 0x34F1A8                ; returns a factor in xmm0
       0x34ED22  movss xmm5, [rip+0xB3594E]   ; READ   <- 0xE84678
       0x34ED2A  mulss xmm5, xmm0
       0x34ED2E  movss [rip+0xB35942], xmm5   ; WRITE  -> 0xE84678

   The value it publishes is `constant / clamp(...)` scaled by `0x34F1A8`'s
   return - i.e. the function *computes and owns* the first-person FOV, which is
   precisely the role Halo 3's rebuild plays.
3. Immediately after, `0x34ED36`-`0x34ED6A` sign-extends two words, subtracts
   them and divides - building an aspect ratio from a viewport rect, the rest of
   a first-person camera/projection build.
4. **Function entry: RVA `0x34EC44`** (`mov rax,rsp; mov [rax+8],rbx; ... push
   rdi; push r14; push r15; sub rsp,0x40`, preceded by `int3` padding at
   `0x34EC42`).
5. **Nine call sites**, all in the render driver region: `0x34F0EE`, `0x360C19`,
   `0x360DAB`, `0x3704EF`, `0x37058F`, `0x376ACE`, `0x376B22`, `0x377D41`,
   `0x377D87`. Halo 3's homolog has six, in the same shape - one per
   first-person draw pass.

**Signature caveat, recorded before it wastes a candidate.** The 24-byte
prologue at `0x34EC44` occurs **17 times** in the image - it is a stock MSVC
prologue. Any AOB for this function must extend into its distinctive body (the
`render_first_person_fov_scale` rip-relative stores are the natural
discriminator) and be measured to match exactly once, exactly as the E-H4-4 and
E-H4-6 tables were.

**Still to derive before the hands candidate can be written:** where this
function deposits the first-person camera and its derived/projection block
(Halo 3: `{view+0x08, view+0x1E8}`), and the shader-constant uploader it feeds
(Halo 3: `0x2770F0`). Both are inside `0x34EC44`'s body and its callees; neither
may be guessed. Halo 3's fix is then a direct port: after the engine's own
rebuild, overwrite the pair with the CURRENT EYE's world camera and derived
block and re-run the uploader, so the gun and hands render in true world
perspective with real stereo disparity instead of a crushed mono slab.

### E-H4-14 — H4EK is the discovery tool for the first-person layer (KIT-FIRST)

**Process correction, recorded because it cost real time.** E-H4-13 was derived
by disassembling stripped retail. `AGENTS.md` already says the opposite is
required - *"Reach facts come from HREK. Retail is not a discovery tool ...
reading it to discover behavior produces plausible-looking wrong answers"* - and
the same applies to Halo 4 with H4EK. The user's words: *"my god can't you use
halo 4 mod tools"*. They were right. Retail verifies; the kit explains.

**Two false negatives are worth recording so they are not repeated:** `strings`
is NOT installed on this machine, so `strings <kit exe> | grep ...` returns
nothing and looks like "the kit has no symbols". It has plenty. Extract ASCII
runs with a script instead.

**The kit binaries carry full source paths and assert text.**
`N:\SteamLibrary\steamapps\common\H4EK\halo4_tag_test.exe` (and `sapien.exe`,
`tool.exe`) embed `c:\mcc\release\h4\shared\engine\source\...` paths beside the
assert expressions for each file. The three that own the layer the gun and
hands live in:

    blofeld\camera\first_person_camera.cpp
    blofeld\interface\first_person_weapons.cpp
    blofeld\interface\first_person_animation.cpp

plus `blofeld\dx9\render\views\render_view.cpp` and `render_view_stack.cpp`,
which is independent confirmation of the render_view STACK that E-H4-4's open
structural question asked about.

**From `first_person_camera.cpp` (asserts, verbatim):**

    camera: first person camera #%d attached to object 0x%08X != user object 0x%08X, this should never happen
    object_index==NONE || TEST_BIT(_object_mask_unit, object_get_type(object_index))
    valid_real_vector3d_axes2(&result->forward, &result->up)

So the first-person camera is **per-user**, is attached to a unit object, and
produces a `result` carrying `forward` and `up` - the same orthonormal pair
shape the observer result uses (E-H4-6), which is why the same validation and
the same basis convention apply.

**From `first_person_weapons.cpp` (asserts, verbatim):**

    VALID_INDEX(weapon_slot, k_first_person_max_weapons)
    first_person_weapons                     <- the globals allocation
    fp weapons                               <- named sub-allocation
    fp orientations                          <- named sub-allocation, SEPARATE
    node_matrices_count == weapon_data->node_matrices_count
    (pBodyModel->render_model.index != NONE)
    model_count<=maximum_model_count
    1st person body model nodes do not match 3rd person model in count or attachment. Legs will not render.
    first person: Too many child-objects for unit-index %x, at child %x
    node_index>=0 && node_index<MAXIMUM_NODES_PER_FIRST_PERSON_MODEL
    node_count_interpolated == node_count

This names the whole structure without a single guessed offset:

- a **`first_person_weapons` globals block**, split into a **`fp weapons`**
  array indexed by `weapon_slot` (bounded by `k_first_person_max_weapons`) and a
  **separate `fp orientations`** array;
- each weapon entry carries **`node_matrices`** with a `node_matrices_count`
  (the gun-and-arms bones), bounded by `MAXIMUM_NODES_PER_FIRST_PERSON_MODEL`,
  and an interpolated variant (`node_count_interpolated == node_count`);
- Halo 4 has a **first-person BODY model** with legs that must match the
  third-person model's node count - which is the construct any future VRIK work
  needs, and which Halo 3 does not have in this form.

**Why this matters for the hands candidate.** E-H4-13's remaining unknowns were
"where does `0x34EC44` deposit the first-person camera pair, and which uploader
does it feed". The kit answers the *shape* of both, so the retail search is now
a targeted match rather than a hunt: the camera result is a `{forward, up}`
pair validated by `valid_real_vector3d_axes2`, and the placement of the visible
gun and arms goes through `fp orientations` + per-weapon `node_matrices` rather
than through the camera at all. Those are two separable levers - camera for
stereo/depth, orientations for where the gun sits in the hand - and they should
not be conflated the way a camera-only fix would.

**Next discovery step, and it is kit-first:** locate the same functions inside
`halo4_tag_test.exe` by their assert call sites, read the field offsets they
use, then match the homologous code in retail `halo4.dll` to confirm. Per
`AGENTS.md`, byte-matching kit prologues to retail fails - transfer semantics
and layouts, never addresses.

### E-H4-15 — the first-person weapons globals, KIT-EXPLAINED and RETAIL-VERIFIED

This is the structure the gun and hands live in, **and it is the same block the
E-H4-11 crash dereferences as NULL.** Both open asks turn out to be one
subsystem.

**Kit (`halo4_tag_test.exe`), the two named allocations at `0x931A90`:**

    lea  rdx, [rip+...]     ; "fp weapons"
    mov  r9d, 0x17D20       ; total size
    lea  ecx, [rsi+4]       ; count = 4
    call <named allocator>
    ...
    mov  edi, 0x1960        ; TLS block offset (KIT layout)
    mov  [rdi+rbx], rax     ; store the block pointer into this thread's TLS
    ...
    lea  rdx, [rip+...]     ; "fp orientations"
    mov  r9d, 0xF000        ; total size
    mov  dword [rsp+0x20], 4

**Retail (`halo4.dll`), the homologous function at `0x3C647C`** - found by
searching for the kit's own constants, exactly the "kit explains, retail
verifies" flow `AGENTS.md` requires:

    003C6495  lea  rdx, [rip+0x99BA6C]   ; -> RVA 0xD61F08 = "fp weapons"
    003C64A9  mov  r9d, 0x17D20          ; SAME total size
    003C64C0  lea  ecx, [r8+4]           ; SAME count = 4
    003C64C4  call 0x113BB0              ; named allocator
    003C64C9  mov  rcx, gs:[0x58]        ; TLS array
    003C64E8  mov  r9d, 0xF000           ; SAME orientations size
    003C6510  mov  edx, 0x6A0            ; fp weapons  -> TLS + 0x6A0
    003C6515  mov  ebp, 0x6E0            ; fp orients  -> TLS + 0x6E0

**The resulting map, every number cross-checked in both images:**

| Quantity | Kit | Retail |
| --- | --- | --- |
| `fp weapons` total bytes | `0x17D20` | `0x17D20` |
| user count | 4 | 4 |
| **`fp weapons` per-user stride** | `0x5F48` (0x17D20/4) | `0x5F48` |
| `fp weapons` TLS offset | `0x1960` | **`0x6A0`** |
| `fp orientations` total bytes | `0xF000` | `0xF000` |
| **`fp orientations` per-user stride** | `0x3C00` (0xF000/4) | `0x3C00` |
| `fp orientations` TLS offset | - | **`0x6E0`** |

The TLS offsets differ between kit and retail, as expected; the sizes, count and
strides are identical. Layouts transfer, addresses never do.

**This closes E-H4-11's mechanism.** The crash instruction was

    imul rbx, r14, 0x5F48        ; user_index * 0x5F48
    add  rbx, [rcx + rsi]        ; + *(TLS + 0x6A0)
    mov  eax, [rbx + 4]          ; FAULT, read of address 0x4

`0x5F48` is the `fp weapons` per-user stride and `0x6A0` is its TLS offset, both
now proven. So the faulting code is indexing **`first_person_weapons[user]`**
while the whole block pointer is NULL, and it reads field `+4` - which the
surrounding retail code compares against a unit handle and writes back, i.e. the
record's current unit. On a Halo 4 level re-entry that reuses the same module
instance, the first-person weapons block is not there.

**Two consequences that must shape the hands candidate:**

1. **Null-check the block, always.** The engine itself does not, and that is the
   crash. Any hook of ours that reads `first_person_weapons` or
   `fp orientations` must prove the TLS slot, the block pointer and the user
   index before dereferencing, and degrade to stock rather than fault - the
   `AGENTS.md` failure-isolation rule, with a live example of what happens
   without it.
2. **`fp orientations` is the placement lever.** It is a separate 0x3C00-per-user
   array from the weapon records themselves, which is what E-H4-14 predicted
   from the kit's assert names. Where the gun and arms SIT is written there, not
   through the first-person camera - so the camera fix (stereo/depth) and the
   placement fix (gun in your hand) remain two distinct changes.

**Still to derive, kit-first:** the field layout inside one `0x5F48` weapon
record (`node_matrices`, `node_matrices_count`) and inside one `0x3C00`
orientation record. The kit's `first_person_weapons.cpp` asserts name both; the
next step is to locate those assert call sites through the kit's assert pointer
table (they are referenced indirectly, not by a direct `lea`, so the rip-relative
scan finds zero - use a qword pointer scan for the string VA instead).

### E-H4-16 — the first-person weapon/orientation record layout

Kit-explained, retail-verified, continuing E-H4-15. This is the structure the
hands candidate writes into.

**The kit's accessor (`halo4_tag_test.exe` `0x928290`)** carries both bound
checks in its own asserts, which is what makes the dimensions certain rather
than inferred:

    movsxd rbx, edx            ; arg2 = weapon_slot
    movsxd rdi, ecx            ; arg1 = user_index
    cmp    edi, 3 / jbe        ; user_index <= 3          -> 4 users
    cmp    ebx, 1 / jbe        ; weapon_slot <= 1         -> k_first_person_max_weapons = 2
    lea    rcx, [rbx + rdi*2]  ; index = weapon_slot + user_index * 2
    imul   rax, rcx, 0x1E00    ; element size 0x1E00
    mov    r8d, 0x1968         ; KIT TLS offset of the orientations block
    add    rax, [rcx + r8]

`0x1E00 * 2 * 4 = 0xF000`, which is exactly the `fp orientations` allocation
size from E-H4-15 - the dimensions close on themselves.

**The retail homolog (`halo4.dll` `0x3B5380`-`0x3B53CF`)**, in the same region
of the module as the E-H4-11 crash:

    imul rbx, r8, 0x5F48                    ; user_index * fp-weapons stride
    mov  eax, 0x6A0                         ; fp weapons TLS offset
    imul rdi, rcx, 0x2EC8                   ; weapon_slot * per-weapon stride
    add  rbx, [rax + r9]                    ; rbx = fp_weapons[user]
    mov  eax, [rbx]                         ; record +0x00 = flags dword
    shr  eax, 1 / test al, 1 / je bail      ; gated on flags bit 1
    lea  rax, [rcx + r8*2]                  ; index = weapon_slot + user*2
    movsxd r8, [rdi + rbx + 0x15D4]         ; per-weapon node index
    imul rdx, rax, 0x1E00                   ; orientations element
    mov  eax, 0x678                         ; a THIRD related TLS block
    shl  r8, 5                              ; node index * 0x20
    add  rdx, [rax + r9]
    lea  rcx, [rdx + 0xF00]                 ; node array at +0xF00

**What that establishes:**

| Field | Value |
| --- | --- |
| users | 4 |
| `k_first_person_max_weapons` | **2** |
| `fp weapons` per-user record | `0x5F48` at TLS `+0x6A0` |
| per-weapon sub-record stride | **`0x2EC8`** (2 x 0x2EC8 = 0x5D90, leaving a 0x1B8 header) |
| orientations element | **`0x1E00`**, indexed `weapon_slot + user*2`, base TLS `+0x678` |
| node transform stride | **`0x20`** (`shl r8, 5`) |
| node array inside an orientation | at **`+0xF00`** |

`0x1E00 - 0xF00 = 0xF00`, and `0xF00 / 0x20 = 120` nodes - so an orientation
record holds **two 120-node arrays of 32-byte transforms**, which matches the
kit's `node_count_interpolated == node_count` assert (a current and an
interpolated bank) and bounds `MAXIMUM_NODES_PER_FIRST_PERSON_MODEL` at 120.
A 32-byte Blam node transform is the standard rotation quaternion + translation
+ scale (4+3+1 floats); **this must be confirmed by reading live values before
anything is written, not assumed from the size.**

Further per-weapon fields observed in the same function, all relative to
`fp_weapons[user] + weapon_slot*0x2EC8`: `+0xBC` dword compared to NONE,
`+0xC6` word compared >= 0, `+0xDA` byte flag, `+0x1DC` a substructure address,
`+0x208` dword compared to NONE, `+0x240` dword flags (bit 2), `+0x15D4` the
node index used above.

**The safety rule this evidence forces, restated because it is the crash.**
E-H4-11/E-H4-15 proved `*(TLS + 0x6A0)` is NULL on a Halo 4 level re-entry and
the engine dereferences it anyway. Every access above chains through that same
block plus `+0x678`. The hands candidate must prove the engine TLS index, the
TLS slot, each block pointer, the user index and the weapon slot before touching
a byte, and degrade to stock on any failure.

### E-H4-17 — C-H4-11's probe corrected two reads (headset, 2026-08-08)

**Result: "no floaty hands, gun still stuck to my face."** The candidate wrote
NOTHING - it refused, exactly as designed - and its probe line is what corrects
the layout.

    Halo 4 C-H4-11 hands: REFUSED - the 0x20 node is NOT {quat,translation,scale},
    nothing was written; 0 placed / 243 refused frames in 2s, 2 weapon slot(s),
    root node 85; engine's stock node: |quat| 0.0000 scale 0.000
    translation 0.000/0.000/0.000

**What it PROVED (the whole addressing chain is right).** 2 weapon slots
resolved and a field value of 85 came back, which means the TLS index, the slot,
`*(TLS+0x6A0)`, the `0x5F48` user stride, the active flag, the unit handle, the
`0x2EC8` weapon stride and `*(TLS+0x678)` are all correct against the running
game. E-H4-15/16 stand.

**What it DISPROVED, and the arithmetic that settles it.** The read came back
all zeros, which is not a different layout - it is unwritten memory. Re-reading
retail `0x3B53B3`-`0x3B53D6`:

    movsxd r8, [rdi + rbx + 0x15D4]   ; the field
    shl    r8, 5                      ; << 5 = a BYTE LENGTH, not an element index
    lea    rcx, [rdx + 0xF00]         ; dst
    call   0xA62FB0                   ; an IMPORT THUNK (jmp [rip+...]), i.e. a CRT copy

with `rdx` = the orientation record base. So the call is

    memcpy(record + 0xF00, record + 0x00, node_count * 0x20)

Therefore **`+0x15D4` is the node COUNT, not a node index**, and **the LIVE node
bank is at `+0x00`** while `+0xF00` is the previous-frame copy the engine
interpolates against (the kit's `node_count_interpolated == node_count`).

The zeros confirm it exactly: the probe read `+0xF00 + 85*0x20`, and 85 nodes
copied to `+0xF00` occupy `0xF00..0x19A0` - so index 85 lands precisely one byte
past the end of the valid data. Two independent facts (the count's meaning and
which bank is live) fall out of one measured value.

    node bank A  record + 0x000 .. 0xF00   LIVE, 120 x 0x20
    node bank B  record + 0xF00 .. 0x1E00  previous frame, copied each frame

**Corrected in C-H4-11a:** read and write bank A, treat `+0x15D4` as a count
(reject 0 or > 120), and write the assembly's root at node 0.

**Process note.** The candidate refusing to write on an unproven layout is the
reason this cost one headset run and no damage. Had it written a guessed
transform into a live bone array on a NULL-prone block, the outcome would have
been a crash rather than a log line that hands over the answer.

### Forward milestone ladder — one visible claim per candidate

1. **C-H4-7:** stock-projection/exact-serial stereo geometry only.
2. **C-H4-8:** head rotation, 6DOF/recenter, AND native headset-FOV coverage,
   on the accepted wrapper transaction. Rungs 2 and 3 were merged after the
   C-H4-7 headset run showed they are one player-visible defect ("put me inside
   with proper fov"), and after E-H4-8 proved the converter scale that rung 3
   was waiting on. `+0x2C` remains unresolved and unwritten; exact four-edge
   off-axis geometry remains future work and is unnecessary while a solved
   symmetric cover contains the frustum.
3. **C-H4-9:** headset-owned look pitch only — the view takes pitch and roll,
   the stick's vertical axis is held, and a closed loop keeps the engine's own
   pitch (and so the shot line) under the head. Yaw ownership deliberately
   stays with the engine until there is a VR turn and an aim loop to replace it.
4. **C-H4-10:** motion aim, VR turn and rumble - the yaw half of look
   ownership, the shared closed aim loop, and the three capabilities Halo 4 had
   never published. **The old rung 4 (CUI HUD presence) is CANCELLED**: the user
   confirmed in the headset that Halo 4's HUD already arrives inside the
   captured scene target, so there is nothing to bring up.
5. **C-H4-11:** first-person hands and weapon placement. **Needs its own H4EK
   evidence pass before any code** - Halo 4 has no first-person palette or
   model-placement evidence at all, and Reach's passenger hands are still
   unsolved after several candidates, so this is discovery first.
6. **C-H4-12:** a Halo 4 crosshair hider, if the doubled reticle proves
   distracting; and arm IK once the hands exist.
7. Lifecycle, cinematics and vehicles remain separate candidates after that.

Every rung requires H4EK evidence, offline gates, a unique commit and artifact
hash installed to both editions, a log naming edition/runtime/headset, an
explicit Halo 4 headset result, and a Halo 3 regression whenever shared or
lifecycle code changes. A failed experiment gets its own behavior-revert commit
before the next rung. `docs/CURRENT-STATE.md` advances only on explicit headset
acceptance, never on a build or clean log alone.

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

### E-H4-4: retail anchoring of the player-view transaction (PROVEN 2026-08-07)

The first retail camera measurement for Halo 4, taken under the H4EK-first
rule: E-H4-3 explained the system from the kit, and this entry only **matches
and verifies** those shapes in retail. Module verified before any read —
`halo4.dll` SHA-256 `7C53E7D5...0C34FA84`, the pinned Steam identity above,
unchanged.

**Method note, and the first negative result.** E-H4-3's discovery handles
were assert strings (`view overflowed!!!`, `MAXIMUM_PLAYER_WINDOWS`,
`m_window_count`). **All of them are compiled out of retail** — measured, not
assumed: zero occurrences of `view overflowed`, `render_view`,
`MAXIMUM_PLAYER_WINDOWS`, `m_window_count` or `main_render_game` in the whole
17,829,336-byte image (`player_view` occurs 3x in unrelated data). The string
route is dead for Halo 4 camera work exactly as it was for Reach. What *did*
transfer is E-H4-3's structural constants — the stride, the refusal bound and
the callback offset — which is precisely why that entry cross-checked them in
two optimized kit builds first.

**Anchor 1 — the per-player view array. PROVEN.** Only **three**
`add r64, 0xAD0` instructions exist in the entire module, and one of them is
the constructor loop, byte-for-byte the kit's `mov edi,4` / `call <ctor>` /
`add rbx,0xAD0` shape:

```
00022A50  mov  [rsp+8], rbx
00022A55  push rdi
00022A56  sub  rsp, 0x20
00022A5A  lea  rbx, [rip+0x308A75F]   ; array base -> 0x30AD1C0 (.data)
00022A61  mov  edi, 4                 ; 4 slots
00022A66  mov  rcx, rbx
00022A69  call 0x356BC4               ; element constructor (size 0xC0)
00022A6E  add  rbx, 0xAD0             ; stride 0xAD0
00022A75  sub  rdi, 1
00022A79  jne  0x22A66
```

| Retail fact | Value |
| --- | --- |
| Player-view array base | RVA `0x30AD1C0` (`.data`, zero-init tail) |
| Slots x stride | **4 x `0xAD0`** — matches E-H4-3 exactly |
| Array extent | `0x30AD1C0` .. `0x30AFD00` |
| Element constructor | `0x356BC4`, size `0xC0`, bodySHA256 `BB4EB691...D79626` |

Two independent confirmations, neither assumed:

1. **A second, unrelated walk agrees.** The loop at `0x2998EC` iterates the
   same array with the same stride from a biased pointer
   (`lea rdi,[rip+...] -> 0x30ADC68` = base + `0xAA8`) and then *reconstructs
   the element base* with `lea rax,[rdi-0xAA8]` before calling `0x32CF6C`.
   The bias arithmetic closes on `element+0`, so this is the same object.
2. **The reference set is tiny and auditable.** Exactly **five**
   RIP-relative operands in the whole module land inside
   `[0x30AD1C0, 0x30AFD00)`: the constructor `0x22A5A`, the walk `0x299993`,
   and three consumers at `0x122951` (fn `0x12259C`), `0x287DB6`, `0x4CCF93`.

**Anchor 2 — element field `+0x39C` is a per-window selector.** E-H4-3 listed
`+0x389`/`+0x39C`/`+0x3A4` as byte-evidenced but unexplained. The `0x2998EC`
walk resolves one of them: with `rdi = element + 0xAA8`, its loop head reads

```
002999A3  cmp  dword ptr [rdi-0x70C], r12d    ; = element+0x39C
002999AA  jne  <next element>                 ; skip this window
```

so **`+0x39C` is a dword compared against an index and used to skip
non-matching elements** — a selector/filter, not payload. `+0x389` and
`+0x3A4` remain open.

**Anchor 3 — the render-view stack. PROVEN, and self-corroborating.** The
kit's `mov [rcx+0x298],rdx` callback store appears in retail with its exact
encoding `48 89 91 98 02 00 00` at `0x341774`, inside a 0x47-byte function
whose shape is the kit's push verbatim, with the pop immediately after it:

```
PUSH  0x341760 - 0x3417A7   bodySHA256 5581D218...8ECB4FC4
  sub  rsp,0x28
  mov  r8d,[rip+...]         ; g_view_stack_top -> 0xE84634
  cmp  r8d,3 / jge <refuse>  ; REFUSES AT top >= 3
  inc  r8d
  mov  [rcx+0x298], rdx      ; store re-entry callback
  lea  r9,[rip+...]          ; slot array -> 0x10BEE08
  mov  [rip+...], r8d        ; commit new top
  mov  [r9+rax*8], rcx       ; store view pointer
  call qword ptr [rax+0x298] ; invoke the NEW TOP's callback

POP   0x3417A8 - 0x3417DC   bodySHA256 CC97D2C6...E35A477B
  mov  eax,[rip+...]         ; same top   -> 0xE84634
  sub  eax,1 / mov back / js <empty>      ; underflow guard
  lea  rcx,[rip+...]         ; same slots -> 0x10BEE08
  mov  rcx,[rax+0x298] / test / call rcx  ; new top's callback
```

| Retail fact | Value | Corroboration |
| --- | --- | --- |
| `g_view_stack_top` | RVA `0xE84634` (`.data`) | derived independently from push and pop — **they agree** |
| Static initialiser | **`-1`** (empty) | read from the file, matches the kit's `0xFFFFFFFF` |
| Slot array | RVA `0x10BEE08`, 4 x 8 bytes | derived independently from push and pop — **they agree** |
| Capacity | **4** | refusal at `top>=3` + post-increment indexing |
| Re-entry callback offset | **`+0x298`** | in both push and pop (Reach's is `+0x2A8`) |

Each has **16 direct callers**, confirming E-H4-3's reading that this stack is
a *generic* render-view scope mechanism, not player-view-specific.

**Anchor 4 — the window count. PROVEN.** E-H4-3's third independent proof of
the bound 4 was `main_render_game` *computing* `clamp(n,1,4)` in registers.
Retail `0x122188` (size `0x66`, bodySHA256 `A8903B11...BC88BF4C`) is that
computation, and it is the `0x2998EC` walk's own loop count:

```
001221C9  call 0x95D0C          ; raw count
001221CE  mov  ecx,1 / cmp eax,ecx / cmovg ecx,eax   ; max(n,1)
001221D8  mov  eax,4 / cmp ecx,eax / cmovl eax,ecx   ; min(...,4)
001221E4  mov  eax,1            ; every early-out returns 1
```

**Candidate retail signatures, uniqueness measured over `.text`** (`??` =
wildcarded RIP displacement or rel32). Four of five are unique on the first
try; the fifth is recorded as unusable alone:

| Signature | Matches | Anchors |
| --- | --- | --- |
| `48 8D 1D ?? ?? ?? ?? BF 04 00 00 00 48 8B CB E8 ?? ?? ?? ?? 48 81 C3 D0 0A 00 00 48 83 EF 01 75` | **UNIQUE** `0x22A5A` | array base, stride, count, element ctor |
| `48 83 EC 28 44 8B 05 ?? ?? ?? ?? 41 83 F8 03 7D ?? 41 FF C0 48 89 91 98 02 00 00` | **UNIQUE** `0x341760` | push, top global, refusal, `+0x298` |
| `48 83 EC 28 8B 05 ?? ?? ?? ?? 83 E8 01 89 05 ?? ?? ?? ?? 78 ?? 48 8D 0D` | **UNIQUE** `0x3417A8` | pop, top global, slot array |
| `B9 01 00 00 00 3B C1 0F 4F C8 B8 04 00 00 00 3B C8 0F 4C C1` | **UNIQUE** `0x1221CE` | window count `clamp(n,1,4)` |
| `FF 90 98 02 00 00` (callback invoke alone) | 3 matches | **NOT usable alone** — `0xCB5E9`, `0x34179C`, `0x9A3084` |

**What is still OPEN, and one kit shape that did NOT transfer.** E-H4-3's
inner-wrapper signature was `call set-current -> push -> render -> pop ->
**tail-jmp** set-current(NULL)`. **That tail-call does not exist in retail:**
all 16 push call sites were enumerated and disassembled, and not one enclosing
function contains a `jmp` to a target it also `call`s. The retail compiler
emitted a plain call/ret, so the wrapper must be found another way. Still
unanchored in retail, in the order they are needed:

1. the set-current setter and the active player-view pointer global
   (kit `0x8B9530` / `0x5573F28`);
2. the inner wrapper itself (kit `0x1F7C00`) — **leading candidate is fn
   `0x12259C`-`0x123115`**, the only function that both references the
   player-view array (at `0x122951`) and pushes a view;
3. the render body (kit `0x8B5930`), which is where the M1 camera-write point
   lives, and setup (kit `0x8B9990`);
4. the `element+0x1D4` rasterizer-camera identity, still INFERRED;
5. element fields `+0x389` and `+0x3A4`;
6. callbacks `0x8B8890` vs `0x8BAE30`.

**Scope of this proof.** It pins storage and scope — where the per-player
views live, how the render-view stack admits and releases them, and how many
windows exist — for this exact module hash. It admits **no hook**: the camera
write point is item 3 above and is not yet located. Nothing here may be
shipped until the wrapper and render body carry their own retail proof.

**Item 3's premise was wrong, and E-H4-5 corrects it:** the camera-write
point is not in the render body at all. See the next section.

### E-H4-5: the camera producer chain and the M1 camera-write point (PROVEN 2026-08-07)

Closes every item of E-H4-4's OPEN list. Method: an eleven-agent evidence
workflow — three kit agents first (H4EK-first rule), two retail matchers
working from structural idioms, three retail anchor agents, then three
adversarial audits that independently re-measured every signature count with
freshly written scanners, attacked every kit→retail correspondence, and
spot-verified disputed byte sites on disk. Module identity verified by
preflight before any retail read (`halo4.dll` SHA-256 `7C53E7D5...0C34FA84`,
unchanged). Full quoted disassembly is in
`out/h4ek-evidence/camera/camera-producer-chain.md` (kit) and
`out/h4ek-evidence/camera/retail-camera-transaction.md` (retail); this entry
records the verdicts and the anchors.

**Kit half (halo4_tag_test.exe, cross-checked in halo4_tag_play.exe).** A
module-wide rip-relative write index proves the per-window SETUP (kit
`0x8B9990`, in `render_player_view.cpp` by its own assert record) is the ONLY
writer of `g_player_view_stack_element`. The camera source is the observer
result — TLS gamestate slot `+0x4A0` → observer[user] (stride `0x428`, base
`+8`) → result at `+0x154` — produced by `observer_update` (kit `0x168710`).
Inside one setup call, in order: **(A)** the observer→camera converter (kit
`0x8AB580`) writes position/forward/up/fov into the element's rasterizer
camera `+0x00..+0x2C`; **(B)** the projection builder derives basis and
position into `+0x88`; **(C)** the raster pair is copied to the render pair
`+0x14C`/`+0x1D4`; **(D)** the constant bank `+0x480` is rebuilt, rows
`+0xC0..+0xF0` = right/up/backward/position — exactly what the re-entry
callback uploads. Three prior kit claims are corrected in place: the "render
body" `0x8B5930` is the auxiliary-texture/UI pass, NOT the scene renderer and
NOT the camera-write point (it never dereferences its `c_player_view`
argument); its `0x8B5C6A` push callback is `0x89CBB0`, not `0x8A2BB0`; and
the kit wrapper has three callers, not one.

**Retail anchors. All PROVEN with quoted bytes; the audit reproduced every
signature count below.**

| Retail item | RVA | Kit homolog |
| --- | --- | --- |
| active `c_player_view*` global | `0x4969AA0` (`.data` zero-init) | `0x5573F28` |
| inner wrapper (whole transaction) | `0x1222F4`-`0x122599` | `0x1F7C00` |
| set-current | **INLINED**: store `0x122307`, clear `0x122580` | setter `0x8B9530` |
| `g_player_view_stack_element` | `0x10DAFE0` (+0x30 rect at `0x10DB010`) | `0x55605A0` |
| player-view re-entry callback | `0x374A60`-`0x374ADF` | `0x8B8890` |
| menu re-entry callback | `0x382B8C`-`0x382BB6` (object `0x10EDEC0`) | `0x8BAE30` |
| **per-window SETUP (the producer)** | **`0x374C84`-`0x3750C2`** | `0x8B9990` |
| **camera converter (write point A)** | **`0x38F014`, called at `0x374D5B`** | `0x8AB580` |
| projection builder | `0x38F658`, called at `0x374DA2` | `0x8ACBB0` |
| raster→render pair copy | inline `0x374DA7`-`0x374E77`, UNCONDITIONAL | `0x8B9DE7` |
| constant-bank builder | `0x395A7C`, called at `0x37502B` | `0x8FFBE0` |
| render body (aux-texture/UI pass) | `0x378D50`-`0x379118` | `0x8B5930` |
| viewport+scissor commit | `0x340148` (setters `0x34EAA0`/`0x34E618`) | `0x857040` |
| camera-const uploaders | `0x3737F4` / `0x3735A8`, writer `0x383CF8` | `0x8D9F90` / `0x8DA310` |
| split-screen layout table | `0xE84CC0`, stride `0x14`, initialized `.data` | `0x24A1200` |
| published layout-mode global | `0x4969950` (written by post-2 `0x3751D0`) | `0x5560308` |

E-H4-4's inlining question is answered: the wrapper is a real function and
the kit's tail-jmp negative is explained by **set-current** being inlined to
one store and one `and qword ..., 0`. Its census is complete and closed: 41
references, zero `lea`, zero data-section pointers, so the only durable
writers are those two instructions, plus two scoped save/set/restore pairs.

Element fields settled: `+0x389` = first-window flag, one module-wide write
(`0x122CCD`), and **no reader found under displacement or absolute
addressing in either binary** — recorded as a census-bounded negative, not as
deadness. `+0x38C/+0x390/+0x394/+0x39C` = window_index / window_count / mode
/ **output_user_index**, written only by setup; E-H4-4's `+0x39C` selector is
therefore an output-user filter. `+0x3A4` = split-screen layout mode
(0=full, 3=half, 2=quarter), published per window to `0x4969950`. The two
remaining E-H4-4 array consumers are a **dynamic-resolution controller**
(`0x287DB0`, which clears the rescale gate byte `0xE84CA0` that callback
`0x374A60` tests) and a smoothed **world→screen projector** (`0x4CCEDC`),
which independently re-proves the retail observer geometry as
`TLS[+0x680] + 0x15C + user*0x428` — the exact composite of the kit's
`+0x4A0 → +8 + user*0x428 + 0x154`.

**THE M1 CAMERA-WRITE POINT.** All four camera artifacts are produced inside
ONE setup invocation per window as straight-line code; A is the master write
and B/C/D derive from it in the same call. Writing the element after setup
returns is therefore stale by construction. The per-eye substitution boundary
for the future camera hook is:

- **β1 (preferred): before the setup call at `0x122CC3`** — substitute the
  content of the observer result the window record's `+0x08` points at, and
  let setup derive projection, render pair and bank per eye inside
  unmodified engine code;
- **β2: around the converter call at `0x374D5B`** — after A and before B at
  `0x374DA2` nothing has yet derived from the camera.

Retail simplifies the kit here, which helps: the kit's copy-skip argument and
its alternate object-attached render-camera path are both absent from retail
setup, so there is ONE write point, not two. The confirmed trap is the
opposite of Reach's: the re-entry callback takes **no arguments** and
re-publishes from singletons on every push and every pop; the render body's
nested pushes hand-commit from the element's rasterizer pair and swap the
active-view global mid-transaction. Per-eye state must therefore live in the
element and bank via β1/β2 and must never be written to the active global
mid-render.

**What the adversarial audits changed, recorded because it is the reason to
trust the rest.** The correspondence audit forced two identifications that
had been positional inferences — the camera-constant uploaders are proven
internally, and CB `0x17` registers 4-7 (the camera basis and position) are
written nowhere else in the module — and it defended the sole-writer claim by
resolving `rcx` at all five converter call sites, only one of which targets
the element. It **refuted** a subsidiary claim that `0x340148` is the only
viewport writer: `0x346668`, `0x12F0A0` and `0x395404` call both setters
directly and `0x34D664` issues `RSSetViewports` itself, so a second live
viewport path exists and is uncharacterized. The uniqueness audit reproduced
every signature and census number except four, all now corrected here and in
the evidence documents: a "contiguous six-sub" setup signature matches **zero
times** (retail interleaves `4C 8B F6`; the interleaved form is the correct
one — re-verified by hand this session), a weak projection-basis pattern is
**11 hits, not 1**, the render body's two Bink gate bytes are **`0x2F4EAD2`
and `0x2F4F0FC`** (re-verified by hand), and the kit `imul 0x428` shape has
**1** tag_play hit rather than zero (it survives optimization; it is simply
useless in retail at 66 hits).

**Homology labels that stay INFERRED — never promote these to findings.**
"fn `0x12259C` = main_render_game" (structure only; its own callers are
untraced); "record+0x08 = `s_observer_result`" (layout retail-proven, the
NAME rests on kit asserts compiled out of retail); post-1 `0x3750C4` has no
kit identity at all; "post-2 `0x3751D0` = kit `0x8B63C0`" rests on a
four-instruction opening plus call position, and "post-2 contains the scene
walk" is a candidate, not a finding; `0x38F178`, `0x12F738` and `0x357014`
are positional labels; the minor view-object name map is inferred except
`0x10BFA20`'s layout; the COM vtable slot names (`+0x160`/`+0x168`/`+0x190`)
are documented-interface-order inference; every subsystem name in this entry
(dynamic-resolution controller, world→screen projector) labels a proven
mechanism with an inferred purpose.

**Still OPEN, ordered by how much each blocks the M1 hook.** (1) The retail
scene-geometry submission point — post-2 `0x3751D0` versus the wrapper callee
`0x3532B4`; settle by diffing post-2 against kit `0x8B63C0`. (2) The last
writer-census gap: `0x3A0FA0` (receives the render pair) and `0x341658`
(receives bank+0x80) are undissected, so the β1/β2 boundary inherits that
gap. (3) `0x12F738`, the callback's conditional rect rescaler, and its
interaction with the dynamic-resolution controller. (4) fn `0x12259C`'s own
callers. (5) The retail force-window-count global (kit `0x46F5248`) via
`0x95D0C` — the deterministic single-window lever for VR. (6) A full census
of camera-constant re-publishers (`0x3443DC`, `0x378210`, and the bank
re-uploaders). (7) The NULL-observer default camera, needed only if the hook
must behave in menus.

**Scope.** This entry admits **no hook**. It pins where the camera is
written, what derives from it, and where a per-eye substitution must land.
The hook itself is C-H4-3's business and needs its own candidate with its own
proofs.

### E-H4-6: the two hook sites, their ABI, and setup's re-callability (PROVEN 2026-08-07)

The evidence C-H4-3 consumes. E-H4-5 said *where* a per-eye camera must be
substituted; this entry pins *what to hook*, *how to call it*, and the one
property that makes the β1 design legal at all — that setup can be invoked
more than once in a frame. Measured against the pinned Steam `halo4.dll`
(SHA-256 `7C53E7D5…0C34FA84`, preflight PASS before any read) with
`tools/h4-probes/rdis.py`; static file analysis only, no process touched.

**The whole per-window loop body, byte-quoted at `0x122CA6`.** This is the
single anchor the candidate resolves; everything else follows from its own
displacements.

```
00122CA6  48 8B 47 08              mov  rax,[rdi+8]        ; observer result
00122CAA  48 89 44 24 28           mov  [rsp+0x28],rax     ; arg6 = observer*
00122CAF  8B 07                    mov  eax,[rdi]          ; output_user_index
00122CB1  89 44 24 20              mov  [rsp+0x20],eax     ; arg5 = user
00122CB5  44 8B 4C 24 50           mov  r9d,[rsp+0x50]     ; arg4 = mode
00122CBA  45 8B C7                 mov  r8d,r15d           ; arg3 = count
00122CBD  8B 57 10                 mov  edx,[rdi+0x10]     ; arg2 = window
00122CC0  49 8B CD                 mov  rcx,r13            ; arg1 = view
00122CC3  E8 BC 1F 25 00           call 0x374C84           ; SETUP
00122CC8  85 F6 / 0F 94 C0         test esi,esi / sete al  ; window == 0
00122CCA
00122CCD  41 88 85 89 03 00 00     mov  [r13+0x389],al      ; first-window flag
00122CD4  44 8B 47 10              mov  r8d,[rdi+0x10]     ; window
00122CD8  49 8B D5                 mov  rdx,r13            ; view
00122CDB  48 8D 0D FE 82 FB 00     lea  rcx,[rip+0xFB82FE] ; -> 0x10DAFE0
00122CE2  E8 0D F6 FF FF           call 0x1222F4           ; WRAPPER
```

`rdi` is the 0x20-byte window record E-H4-5 described (`+0x00` user, `+0x08`
observer result, `+0x10` window index, `+0x18` view) and `r13` is that
record's `view`, loaded at `0x122C44`. So:

| Callee | Retail ABI, measured at the call site |
| --- | --- |
| setup `0x374C84` | `(rcx=view, edx=window, r8d=count, r9d=mode, [rsp+0x20]=user, [rsp+0x28]=observer*)` |
| wrapper `0x1222F4` | `(rcx=element `0x10DAFE0`, rdx=view, r8d=window)` |

Confirmed inside each callee rather than only at the call site: setup's
prologue moves `rcx` into `rbx` and writes `[rbx+0x3A4]`/`[rbx+0x3A8]`/
`[rbx+0x398]`, loads its own `r13 = lea [rip+0xD66339] = 0x10DAFE0`, and reads
its sixth argument as `mov rdi,[rsp+0xB8]` — which is exactly `entry_rsp+0x30`
after seven pushes and `sub rsp,0x50`. The wrapper's prologue moves `rdx` into
`rdi` and stores it to `0x4969AA0`, and keeps `r8d` in `ebx`.

**Both hook targets have EXACTLY ONE caller, and it is this loop.** A
whole-image xref scan returns one hit for each (`0x122CC3` → setup,
`0x122CE2` → wrapper). That is what lets each detour additionally require its
exact retail return address (`0x122CC8` and `0x122CE7`) before it may claim a
transaction, and it means our own re-invocations through the MinHook
trampolines can never re-enter a detour.

**The observer-result layout, from the converter's own instructions**
(`0x38F066`–`0x38F0A7`, unique in `.text`):

```
F2 0F 10 02 / F2 0F 11 03    [rdx+0x00] -> [rbx+0x00]   position.xy
8B 42 08    / 89 43 08       [rdx+0x08] -> [rbx+0x08]   position.z
F2 0F 10 42 28 / F2 0F 11 43 0C  [rdx+0x28] -> [rbx+0x0C] forward.xy
8B 42 30    / 89 43 14       [rdx+0x30] -> [rbx+0x14]   forward.z
F2 0F 10 42 34 / F2 0F 11 43 18  [rdx+0x34] -> [rbx+0x18] up.xy
8B 42 3C    / 89 43 20       [rdx+0x3C] -> [rbx+0x20]   up.z
F3 0F 10 42 78 / F3 0F 11 43 28  [rdx+0x78] -> [rbx+0x28] vertical FOV
F3 0F 10 72 7C / F3 0F 11 73 2C  [rdx+0x7C] -> [rbx+0x2C] FOV ratio
```

So the observer result carries **position `+0x00`, forward `+0x28`, up
`+0x34`, vertical FOV `+0x78`, and FOV ratio `+0x7C`** — five fields, 0x80
bytes covering all of them plus the `+0x44..+0x5C` block setup copies onto the
view immediately after (`0x374D65`-`0x374D7A`). H4EK independently proves the
field meanings: its observer finisher computes full vertical FOV at `+0x78`,
and its converter asserts camera `+0x28` as `vertical_field_of_view`. That
block is the entire per-eye substitution surface.

**Setup is re-callable within one frame — measured, not assumed.** This is the
load-bearing property of the β1 design and it was the one real hazard: setup
contains six in-place `sub`s on the element rect
(`0x374CE5`-`0x374D0D`, targets `0x10DB014`/`0x10DB016`/`0x10DB018`/
`0x10DB01A`/`0x10DB01C`/`0x10DB01E`, i.e. element `+0x34`..`+0x3E`), and
accumulating arithmetic run twice per frame would drift the viewport every
frame. It does not, because setup's **first** callee `0x38EF78` rewrites that
rect from scratch on every call:

```
0038EF86  mov  [rcx+0x44], eax        ; fresh from the frame-dimension globals
0038EF92  mov  [rcx+0x4A], ax
...
0038EFFB  mov  rax,[rbx+0x44] / mov [rbx+0x30], rax    ; +0x44.. -> +0x30..
0038F003  mov  rax,[rbx+0x4C] / mov [rbx+0x38], rax
```

The subtrahends `ax`/`r8w` are then re-read from the freshly written
`+0x30`/`+0x32` (`0x374CCF`, `0x374CDA`). A disassembly-wide scan of setup for
read-modify-write instructions on rip-relative memory returns **only** those
six subs, so no other accumulating state exists in the function.

**Signature uniqueness, measured over `.text` of the pinned image.** Four
patterns, four single matches, and three independent derivations that agree on
the same two functions:

| Signature | Matches | Derives |
| --- | --- | --- |
| the per-window loop body above | **UNIQUE** `0x122CA6` | rel32 → setup `0x374C84`; rip → element `0x10DAFE0`; rel32 → wrapper `0x1222F4` |
| setup entry `48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B D9 0F 29 74 24 40 4C 8D 2D ?? ?? ?? ?? 49 63 E8` | **UNIQUE** `0x374C84` | rip → element `0x10DAFE0` (agrees with the loop) |
| wrapper entry `48 89 5C 24 08 48 89 7C 24 10 41 56 48 83 EC 20 48 8B FA 48 89 15 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 41 8B D8 E8` | **UNIQUE** `0x1222F4` | rip → active view `0x4969AA0` |
| converter copy map (quoted above) | **UNIQUE** `0x38F074` | the observer offsets themselves |

**Scope.** This admits the two hooks C-H4-3 creates and nothing else. It says
nothing about Halo 4's render-target shape, its HUD, its aim, or its temporal
passes.
