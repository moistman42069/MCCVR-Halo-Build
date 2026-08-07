# Halo 4 signature evidence

Status: **C-H4-3 built, headset-PENDING — the first Halo 4 hooks exist.** Two
hooks on the per-window camera transaction (E-H4-6), behind an all-or-nothing
install proof; everything else in Halo 4 is still stock. This file
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

### C-H4-3 — the per-eye camera core (BUILT 2026-08-07, headset-PENDING)

**The candidate that puts Halo 4 in stereo.** It is the first Halo 4 hook of
any kind. Everything below is what the code does and what proves it; **no
headset has run it yet**, so nothing here is a result.

| Identity | Value |
| --- | --- |
| Source | `2987dc217b43094e49ce09c5bb32ed960bd96b81` (branch `feature/halo4-bringup`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON, Halo4 ON |
| Candidate package | `out/candidates/2987dc2-reach-fp-parity-20260807-144434557Z` |
| `halo3xr.dll` SHA-256 | `9AFE77E2A9BA13691A59EF520721ABFDA1D3D5DF875F21D99B161390BB9C4ED5` |
| `halo3xr_launcher.exe` SHA-256 | `930BEA232BFC3F8010BC2B385834DEBF796CD3DBEC02ECD0E8475E0DE8A72CE6` |
| Installed editions | Steam and Microsoft Store; both DLL hashes verified independently in each `Halo_MCC_VR` folder after install |
| Preserved priors | `out/deploy-backups/abcbe82-steam-before-2987dc2-...`, `...-store-before-2987dc2-...` |
| Headset result | **PENDING** |

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
SEH-guarded, the camera basis is validated for finiteness and unit length
before it is used, and a failed eye pair falls back to one stock render while
the core stays armed — a single bad frame never drops the player out of VR.

**FOV.** Halo 4 stores one tangent pair per camera, so the raster uses a
symmetric cover taken as the wider half of each axis across both eyes, and
`Game_GetRenderHalfFovs` reports that cover to the compositor through the
existing shared path.

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

**Expected headset result.** Halo 4 enters a level and turns stereo on about a
second after the level-load gate opens, with head tracking and 6DOF, the same
as the other three titles. Halo 3, ODST and Reach are untouched by
construction: every edit is inside `#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA`
or a Halo 4-only branch, and the Reach parity gate passes.

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
F3 0F 10 42 78 / F3 0F 11 43 28  [rdx+0x78] -> [rbx+0x28] tangent X
F3 0F 10 72 7C / F3 0F 11 73 2C  [rdx+0x7C] -> [rbx+0x2C] tangent Y
```

So the observer result carries **position `+0x00`, forward `+0x28`, up
`+0x34`, tangents `+0x78`/`+0x7C`** — five fields, 0x80 bytes covering all of
them plus the `+0x44..+0x5C` block setup copies onto the view immediately
after (`0x374D65`-`0x374D7A`). That block is the entire per-eye substitution
surface.

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
