# Halo 4 VRIK handoff for Claude — 2026-08-08

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

## Current state: failed candidate disabled in source

Source HEAD at handoff is `b8f884d`:

```
b8f884d revert(C-H4-13): disable refused palette admission
50899d5 feat(C-H4-13): solve Halo 4 hands in final palette
5e719ea tools(halo4): retain authored poles and hand attachments
61dab4e fix(halo4): decode storm fp bounds and align bones
8d848a0 fix(halo4): preserve bind pose in VRIK scene
```

`b8f884d` is the required standalone disable point after the user's failed
headset result. It preserves the C-H4-13 implementation inert in
`src/dll/game.cpp`, does not install its optional palette hook, removes Halo 4
`TitleCapability_ArmIk`, and deliberately makes the package manifest
`HEADSET_FAILED_DISABLED`. Release build, core tests, Reach consistency gate,
and `git diff --check` all pass at this commit.

Important deployment distinction: the two MCC mod directories still contain
the failed-but-fail-open `50899d5` candidate because the disable commit was not
packaged. That installed DLL does nothing to hands but leaves the working Halo
4 camera intact.

- Installed candidate: `50899d5-halo4-c13-final-palette-vrik-20260808-200324093Z`
- Installed DLL SHA-256: `7251C1B3F59D3350AAA5374A9593ADF322B2912893B8A2A117729DF752B66015`
- Steam and Store were both independently hash-verified.
- Existing configs were not changed. Steam has `halo4_hands=1`, `arm_ik=1`,
  `floating_hands=1`, all Halo 4 trim values zero. Store has `arm_ik=1` and
  `floating_hands=1`; `halo4_hands` defaults true in `config.h`.

Do not reinstall or resurrect `50899d5` as a proposed fix.

## Headset result and preserved log

User report: **"nothing happened even when toggling the f1 menu options"**.

Repro identity: Steam edition, SteamVR/OpenXR 2.17.6, PSVR2, 120 Hz. The exact
runtime log was copied to:

`out/test-runs/50899d5-halo4-c13-zero-solve-steam-psvr2-20260808/halo3xr.log`

Log SHA-256:
`6B1ED981FEB044AC960F568FA5000A1D373E21F4E73B1D16985F19D75CADEA39`.

The decisive repeated line is:

```
Halo 4 C-H4-13 VRIK: palette hooked; 0 solved / 16991 stock /
5832 alignment-or-pose refused in 2s; ... arm_ik=1 floating_hands=1
```

The exact numbers vary by window, but solved is always zero and refused is
about 5,800 every two seconds. This proves the F1/config toggles were reaching
runtime, the unique hook installed, the exact FP return site fired, and an
admission/solve predicate refused every attempt. The camera did not regress:
roughly 243 stereo pairs per two seconds, 0 frame drops, geometry `TAKING`, and
6DOF/headset FOV healthy.

The detailed result is recorded at `docs/HALO4-SIGNATURE-EVIDENCE.md:2598`.

## The disproven C-H4-13 predicate

The final palette consumer is still the correct no-feedback boundary:

- Official H4EK `halo4_tag_test.exe+0x793D80`, identified from
  `model_skinning.cpp` symbols/asserts.
- Pinned retail homolog `halo4.dll+0x33D8B8`.
- Unique entry signature and three-caller census are documented in E-H4-21b.
- The only first-person caller is `0x36F3C4`, exact return `0x36F3C9`.
- ABI input matrix elements are 0x34-byte `BoneMatrix`; output final palette
  matrices are 0x30 bytes at `skinning+0xA8`.

The mistake is C-H4-13's `exact` block at approximately
`src/dll/game.cpp:29659-29685`. It requires both a separate animation TLS
record and argument 7 to report the composed count 85.

Retail caller disassembly proves argument 7 is not that count:

```
36F346  mov ecx,[rsi-4]
36F349  lea rdx,[r13+0xE]
36F34D  call 0x33D6F0
36F352  mov r15d,eax
...
36F39C  lea r8,[rsi+0xAC]       ; fixed per-model 120-matrix bank
...
36F3B1  mov [rsp+0x30],r15d     ; arg 7 / output skinning count
36F3C4  call 0x33D8B8
...
36F5DA  add rsi,0x1910          ; next render-model record
```

So `r15d` is the current render-model's skinning output count, used to allocate
`count * 0x30 + 0xA8`. It is not the 85-node composed animation count. Each
0x1910 record has its own fixed matrix bank at `+0xAC` large enough for 120
0x34-byte matrices. Never restore `totalNodeMatrixCount == 85`.

The separate `Halo4ResolveFirstPerson()` TLS dependency is also not evidence
that the current render-model record is Storm, and it may be unavailable on
this render callback's thread. Remove it from admission or measure it as its
own diagnostic; do not combine its failure with matrix alignment again.

What is still unknown: C-H4-13 counted all failures together, so no live result
exists yet for `Halo4StormAlignmentMatches`, controller-pose construction, or
the right/left solves. The count gate ran before them. Do not weaken those
guards based on the zero-solve log; split their counters first.

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
the mesh. `floating_hands` may hide only the non-hand arm subtrees; do not
destroy the hand/finger transforms.

## Recommended next candidate shape

1. Start from `b8f884d`. Keep the camera core and OpenXR failure-isolated.
2. Create a new candidate ID; do not amend the failed C-H4-13 history.
3. Re-enable the optional final-palette hook without either 85-count gate.
4. Before applying matrices, add separate hot-path atomics (worker logs them):
   exact-return hits; last/histogram of argument 7; safe-copy failures; basis
   failures; four measured link distances; side-order failure; right/left pose
   failures; right/left IK failures; successful Storm-arm records; and rigid
   weapon records. No logging, allocation, locks, scans, or file I/O in hook.
5. Classify the Storm arm record from the proven matrix relationships, not
   from argument 7. Validate the live predicate before changing its tolerance.
6. Do not assume composed animation nodes 80..84 are appended weapon nodes in
   each per-render-model `+0xAC` bank. The caller proves these are separate
   0x1910 records. Identify the weapon record from H4EK, then retail-verify it.
   A cached right-hand rigid delta applied to a proven weapon record is a
   plausible design, but it is not yet evidence and must not be guessed.
7. Keep work on private scratch matrices and pass the scratch pointer only for
   that palette. Never write the animation producer again: the earlier
   C-H4-12 producer candidate caused feedback/face-lock regression and was
   rolled back.
8. One candidate, one headset claim. Package with
   `tools/package-candidate.ps1`; it installs the exact bytes to both editions.
   Restore the package manifest from `HEADSET_FAILED_DISABLED` only when the
   replacement candidate and validator accurately describe each other.
9. Headset acceptance requires `solved > 0`, visible controller-following
   hands/gun, working two-bone bends using the authored poles, floating-hands
   toggle visibly working, and no Halo 3 regression. Preserve and compare the
   pre-candidate log under `out/deploy-backups`.

## Code map and verification

- Evidence constants: `src/common/halo4_render_logic.h:284`
- Storm counts/indices/authored offsets: `src/common/halo4_render_logic.h:295`
- Inert solve implementation: `src/dll/game.cpp:29406-29658`
- Failed combined admission/detour: `src/dll/game.cpp:29659`
- Optional installer (currently not called): `src/dll/game.cpp:30875`
- Explicit disable point: `src/dll/game.cpp:31155`
- Capability withholding test: `tests/core_tests.cpp:6841`
- Full evidence/result: `docs/HALO4-SIGNATURE-EVIDENCE.md:2540-2625`

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
