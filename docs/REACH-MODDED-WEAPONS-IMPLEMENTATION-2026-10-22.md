# Reach generic modded-weapon mapping candidate

Status: source prepared for user-authorized delivery on `modded-weapons-fix`, offline checks passed;
Windows DLL build and headset validation are outstanding. Not an accepted or
installable candidate. Baseline HEAD is `6919a53a8c6a6ad12ccb5c98355ebaecadaddc2f`,
which descends from the accepted source pointer. The accepted pointer is unchanged.

The user resumed implementation after the Artifact comparison and requested a
generic correction. Intended behavior matches working Reach/Halo 3: the holding
hand and visible weapon follow their controller; the support hand remains
independent unless intentionally coupled by support grip.

## Implementation

- `ResolveReachFpBodyLayout` retains the original exact-map path. For verified
  official Elite/Spartan FP model identities (`0x19021311`/`0x181E0B17`), it also
  accepts a bounded injective native palette-to-source mapping. These model
  identities and anatomical output orders were already established in the
  repository's HREK evidence and finger/palm implementation.
- Translate the established anatomical anchors, hand descendants and auxiliary
  arm skinning influences through that map. No weapon-name list, custom weapon
  checksum list, map-specific offsets or downloaded assets enter the runtime.
- `ResolveReachFpHeldSources` selects exactly the current non-body model's native
  mapped outputs, disjoint from the rendered body. A body palette has an explicit
  empty held selection. This replaces the unfinished body-complement approach:
  unused middle-finger and IK graph nodes are not implicitly classified as guns.
- The selection is used for arm-IK carry, size trim and visibility, and is part
  of the stereo solve cache key. The held model can be interleaved or parented
  to base; its placement does not rely on a right-wrist descendant relation.
- The native held-model observation/classification uses the same bounded remap
  policy. Other titles retain their previous appended-range/default-disabled
  semantics. No new engine hook, address or ABI binding was added.
- Full body-map equality invalidates a changed finger mapping even if source
  membership/anchors remain identical. Existing generation/source-pointer and
  next-prepared-pair discovery/restoration rules remain in place.
- An unrelated/malformed non-body palette gets native presentation without
  invalidating an otherwise valid arms layout. Status publication is atomic;
  message formatting remains in the existing worker logger. Camera/aim are
  independent. No allocations, file I/O or new logging calls enter hot hooks.

This uses the game's existing render-model projection, not new animation-graph
name parsing in a render hook. Auxiliary source nodes absent from the current
model do not affect its projected output. A separate palette with a disjoint
mapping is treated as a held-model candidate, consistent with this existing
first-person transaction; this is not proof of arbitrary third-party semantics.

## Scope and limits

This is generic for supported official arms used with differently ordered
animation/weapon graphs; it does not promise every conceivable modded rig.
Rendered bodies must remain the recognized 41-node Elite or 47-node Spartan
anatomy (or match the original exact-map behavior). Body source indices must
fit the existing 64-bit anatomical masks. Total graph/model source capacity is
120; explicit held membership covers that capacity. Missing, duplicate or
out-of-range mappings are refused.

Entirely replaced arm models, additional visible anatomical joints, malformed
maps and unfamiliar FP transactions require further evidence/support. Optional
finger animation, manual reload placement and collision catalogs retain their
existing coverage; this change does not make those features universal.

The eight failing-model cases in the fixture include Precision SMG, whose
identity as the user's unnamed SMG remains unconfirmed. The analysis report
retains that distinction. Offline compatibility does not confirm a fix in game.

## Validation

- New C++20 fixture: **2,793 checks passed**, GCC 14.2, `-Wall -Wextra -Werror`,
  AddressSanitizer and UndefinedBehaviorSanitizer enabled; no sanitizer report.
- Nine asset-derived cases: Carbine, Repeater, LMG, MA40, Precision SMG,
  Rapid-Fire SMG, both Fuel Rod variants, Spike Launcher. Expected wrist/camera
  identities and hand ancestry come from the graph evidence, not production
  masks. Checks exclude extra fingers/IK nodes from held membership.
- Production `ReachLayoutsEqual` and `LegacyClassifyRuntimeWeapon` are extracted
  into the fixture, including stale-generation/source-pointer, read failures,
  invalid model maps and Halo 3/ODST classification regression checks.
- Invalid body identity/maps, bit 63, held indices 64/100/119, source capacity,
  changed full mappings, empty selections and stereo discovery/invalidation
  policy are covered. These tests do not execute the full shared matrix solver
  or native rendering detours.
- Previous focused layout/pair/palette harness: **40 checks passed** against
  the changed header. `git diff --check` passed.
- New test registered with the main CMake/CTest project as
  `reach_modded_fp_layout`.
- `cmake --preset release` was attempted and failed at configuration because
  **Visual Studio 17 2022 is unavailable in this Linux environment**. No full
  DLL build, full CTest suite or PowerShell Reach consistency gate was run.
  No DLL, installable build ZIP, game-file changes, installation or launch.

Portable focused reproduction:

```sh
python3 tools/generate_reach_modded_fp_fixture.py --source-root . --output out/modded-weapons-investigation/reach_modded_fp_fixture.inl
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iout/modded-weapons-investigation tests/reach_modded_fp_layout_tests.cpp -o out/modded-weapons-investigation/reach_modded_fp_layout_tests
out/modded-weapons-investigation/reach_modded_fp_layout_tests
```

## Remaining acceptance work

The user explicitly requested committing/pushing this source and will compile
on Windows. Run the cumulative Release configure/build/tests and Reach gate.
Resolve any failure before packaging with `tools/package-candidate.ps1` without
`-Install`. Deliver matching build/source ZIPs only after that succeeds.

In headset, use Artifact and compare Carbine, all seven reported weapons and
Repeater. Check each hand independently, head movement, reload/fire, switching
between same-node-count weapons, support grip, hand scale, floating hands/arm
IK and left-handed mode. Compare the new log to the preserved baseline.
A shared solver change also needs Halo 3 regression testing. Both MCC editions
remain supported; no new edition/headset acceptance is claimed.
