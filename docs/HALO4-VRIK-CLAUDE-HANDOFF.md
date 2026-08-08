# Halo 4 VRIK handoff — updated 2026-08-08 for C-H4-14

## Read this first

Read `AGENTS.md`, `CLAUDE.md`, `docs/CURRENT-STATE.md`, and the bottom of
`docs/HALO4-SIGNATURE-EVIDENCE.md` before changing code. Halo 4 discovery must
come from H4EK; retail is only for matching and verification. Do not advance
the accepted pointer without the user's explicit headset acceptance.

The user wants Halo 4's gun and hands off the face and following the tracked
controllers, with two-bone IK, the Blender-authored pole placements,
two-controller/two-handed attachment behavior, and `floating_hands`. Their
non-negotiable visual requirement is that the bones stay aligned to the mesh
exactly as in the authored Blender file. Empty scale is not relative and must
not affect runtime placement.

## Current state: C-H4-14 installed, headset-pending

Source HEAD is `27411fa`, tree clean, on `feature/halo4-bringup`.

```
27411fa tools(C-H4-14): name the package for the candidate it carries
cc3e038 feat(C-H4-14): admit Halo 4 palettes by Storm bind geometry
1a75742 docs(halo4): hand off VRIK failure evidence
b8f884d revert(C-H4-13): disable refused palette admission
50899d5 feat(C-H4-13): solve Halo 4 hands in final palette
```

- Package: `27411fa-halo4-c14-storm-bind-vrik-20260808-205302440Z`
- DLL SHA-256:
  `DB82A69E5BBBBF1EFDE24FD64B73065B29F1FFD0BFAD2D46F7EAB7158621E6D5`
- Steam and Store were both independently hash-verified after install.
- No config was changed. Steam carries `halo4_hands=1`, `arm_ik=1`,
  `floating_hands=1` with all Halo 4 trims zero; Store carries `arm_ik=1` and
  `floating_hands=1` with `halo4_hands` defaulting true in `config.h`.

Release build, core tests, the Reach consistency gate and `git diff --check`
all pass at this commit.

## What C-H4-13 got wrong, and how it was found

C-H4-13's hook installed, fired at the correct return site, and solved zero
palettes. The cause was one predicate, and it is visible in the pinned caller.
The window `halo4.dll+0x36F346..0x36F3D4` was re-disassembled offline from the
installed Steam image; every line of the previous handoff's disassembly
reproduced byte-for-byte:

```
36F346  mov  ecx, [rsi-4]          ; this record's render-model index
36F34D  call 0x33D6F0              ; -> the model's skinning count
36F352  mov  r15d, eax
36F35C  lea  ebp,[rax+rax*2]
36F35F  shl  ebp, 4                ; count * 0x30
36F365  add  ebp, 0xA8             ; + header  -> the output allocation size
36F39C  lea  r8, [rsi+0xAC]        ; arg 3: THIS record's input matrix bank
36F3B1  mov  [rsp+0x30], r15d      ; arg 7: the SAME count
36F3C4  call 0x33D8B8              ; return 0x36F3C9 is the admitted site
36F5DA  add  rsi, 0x1910           ; next render-model record
```

Argument 7 is the current render model's own skinning-output count. C-H4-13
required it to equal the 85-node composed *animation* count, so nothing was
ever admitted. Its ~5,800 refusals per two seconds is itself a measurement:
about twelve render-model records reach the first-person return site per
rendered eye.

## What C-H4-14 changes

- Argument 7 now only has to lie within `[80, 120]`. The scratch copy is
  bounded by argument 7 itself, never by a believed node total.
- The record is classified as `storm_fp` from matrix relationships alone:
  orthonormal finite in-range bases, the four H4EK bind link lengths, and Blam
  left-axis side ordering.
- The `Halo4ResolveFirstPerson` TLS dependency is gone from admission.
- Every refusal stage has its own counter.

The link envelopes and the side ordering are **unchanged from C-H4-13**. They
have never been measured against a live palette, so widening them would trade
one guess for another. The new telemetry exists to measure them.

## The four log lines

Grep `C-H4-14 VRIK` and read all four before asking the user anything:

1. solved / stock / refused, against the number of calls that reached the
   first-person return site.
2. `stages in 2s:` — `count`, `copy`, `basis`, `link`, `side`, `head-pose`,
   `right-pose`, `left-pose`, `right-ik`, `left-ik`, plus how many records
   carried nodes past the 80 body nodes.
3. `live arm links:` — the four distances the engine actually holds, next to
   the H4EK bind values and the admitted envelopes. Published before they are
   judged, so a window that admits nothing still reports them.
4. `argument-7 histogram:` — which per-render-model counts actually arrive.

If `solved` is still zero, line 2 names the stage and lines 3–4 give the live
numbers to fix it with. Do not weaken a predicate that line 2 does not blame.

## Still unidentified — do not guess it

**Which `0x1910` record is Halo 4's weapon render model.** The caller proves it
is a separate record. C-H4-14 writes to no other record: it only moves nodes
past index 80 *inside an already-classified Storm record* with the right hand's
rigid delta.

So if the hands follow the controllers but the gun stays on the face, that is
the known shape of this limitation, not a regression. The next step is to
identify the weapon record from H4EK and retail-verify it. A plausible
classifier is a record whose root coincides with the solved right-hand
position in the same frame — but that is a design, not evidence, and must be
measured before it is written.

## Authored Blender facts to preserve exactly

Authoritative assets:

- `out/halo4-vrik-kit/halo4_storm_fp_vrik_v4_authored.blend`
  - SHA-256 `37E5A6D0E4F35BF350929A1A18228E819481C2AFF1A7E119B4A664088B826251`
- `out/halo4-vrik-kit/halo4_vrik_points.v4-authored.json`
  - SHA-256 `A964969D47976EF5495F986E83B70164092915EA5F9E4B2A46003014DF2A519C`

Only the poles changed in v4. Exported pole positions in metres:

- Left: `(-0.309405237, 0.807897568, -0.150000006)`
- Right: `(-0.385492623, -0.585929811, -0.150000006)`

Runtime normalized pole directions:

- Left: `(-0.417066097, 0.881134331, -0.222840950)`
- Right: `(-0.665396988, -0.692102790, -0.279715300)`

Controller-parented attachment empties:

- `Gun placement left`, parented to the left-hand controller/hand, local
  position approximately `(0, +0.059896648, 0)` metres.
- `right hand, two handed lock in zone`, parented to the right-hand
  controller/hand, local position approximately `(0, +0.059896708, 0)` metres.
- `runtime_uses_scale=false`; ignore empty scale.

H4EK `storm_fp.render_model` facts:

- Body nodes: 80.
- Right arm: upperarm 4, forearm 16, hand 29.
- Left arm: upperarm 5, forearm 8, hand 37.
- Bind link lengths: 0.0915251 and 0.116662 world units.
- Full descendant arrays are already preserved in `src/dll/game.cpp`.

Rigid deltas must be applied to complete shoulder/elbow/hand descendant sets,
not isolated bones. That is how finger and armor geometry stays lined up with
the mesh. `floating_hands` hides only the arm bones outside either hand
subtree; it never destroys the hand/finger transforms.

## Rules that still bind

- Never write the animation producer again: C-H4-12's producer candidate caused
  feedback/face-lock regression and was rolled back. Work stays on private
  scratch matrices passed only for that one palette.
- One candidate, one headset claim. Package with `tools/package-candidate.ps1`;
  it installs the exact bytes to both editions.
- Headset acceptance requires `solved > 0`, visible controller-following hands,
  working two-bone bends using the authored poles, a visibly working
  floating-hands toggle, and no Halo 3 regression. Preserve and compare the
  pre-candidate log under `out/deploy-backups`.

## Code map and verification

- Stage enum and admission predicates: `src/common/halo4_render_logic.h:318`
- Storm counts/indices/authored offsets: `src/common/halo4_render_logic.h:295`
- Argument-7 histogram: `src/dll/game.cpp` `Halo4RecordSkinningCount`
- Storm classifier: `src/dll/game.cpp` `Halo4ClassifyStormArms`
- Solve: `src/dll/game.cpp` `Halo4BuildVrikPalette`
- Detour: `src/dll/game.cpp` `Halo4ModelSkinningDetour`
- Installer: `src/dll/game.cpp` `InstallHalo4Vrik`
- Capability row: `src/common/title_registry.cpp` `kHalo4Capabilities`
- Predicate tests: `tests/core_tests.cpp`, the E-H4-21c block
- Full evidence: `docs/HALO4-SIGNATURE-EVIDENCE.md`, section E-H4-21c

Verification commands:

```
cmake --build --preset release --target halo3xr halomccvr_core_tests
ctest --preset release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-reach-fp-parity.ps1
git diff --check
```

The user is frustrated by several hours of loops. Lead with the exact log
counter being changed and do not ask them to retest a candidate that cannot
report which admission stage succeeded.
