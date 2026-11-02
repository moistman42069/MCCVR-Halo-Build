# Halo 3 Ultimate Firefight: detached weapons hidden by floating hands

Status update: the user compiled the candidate and confirms all previously
invisible weapons now appear, with hands and animations working correctly.
They declined an additional log because everything works. This is scoped user
acceptance; no installed-DLL hash or new exhaustive regression coverage was
provided. Preserve this fix while investigating ODST. The original candidate
and offline evidence below are retained; they did not change ODST.

## Report and inputs

The user reports normal tracked hands and animations but invisible Bulldog and
Ravager models in Ultimate Firefight. Skewer is suspected from memory, not a
confirmed test in this session. This differs from Reach's head-relative arms.

User log source: `f70f3b93aed3aff794b93084e5e9f37b79cee94a`, Steam,
SteamVR/OpenXR 2.18.4, 90 Hz, generic `holographic` headset description.
Log SHA-256: `50bcf74d76403d6f1293481d01d4860250c2d1c12ef702f75ff79f409a01ac32`.
The log does not provide an installed-DLL hash or an unambiguous per-weapon
selection timeline. It reports active arm IK and dual-slot left-controller IK.
Native held-model bounds observations repeatedly report mapped gun root 37.

Downloaded cache: internal name `firefight_shrine`, MCC Halo 3 v13, 2,211,516,416
bytes. SHA-256:
`55382adaef39d0e1508256135d0ba028147fc1af1d561389a6fd8be0547a827c`.
Read-only inspection: `tools/re/inspect_halo3_modded_fp_map.py` and
`HALO3-MODDED-WEAPONS-MAP-EVIDENCE.json`. The inspector is pinned to this hash,
checks tag salts/groups, spans, unique node IDs, parent bounds and model/graph
node correspondence. It establishes asset structure, not native hook bindings.
Halo 3's cache index signature (+0x44), packed addresses (direct shift by 2),
animation nodes (+0x14, stride 32), and FP reference block (+0x3FC) were inspected
in this file; no Reach cache offsets were assumed as runtime evidence.

## Asset findings

Indices below refer to the first FP animation reference in each weapon tag.

| Weapon | Weapon tag | Graph count | Gun model sources | Gun parent | Camera |
| --- | --- | ---: | --- | --- | ---: |
| Bulldog (`autoshotgun`, CQS48 model) | `40E1` | 42 | 37–40 | base (0) | 41 |
| Ravager | `4279` | 52 | 37–50 | base (0) | 51 |
| Skewer | `4225` | 44 | 37–42 | base (0) | 43 |
| Reach Shotgun (structural control) | `1EF1` | 43 | 38–42 | right hand (7) | 1 |

Bulldog's name is verified by weapon pickup string `autoshotgun_pickup`
(`0x6C3D`), whose English value is `Hold ... to pick up|nBulldog`, rather than
confusing it with the separate Bulldog vehicle in the same map. The control
is an asset comparison, not an independently user-confirmed working weapon.

The first three graphs have 37 body nodes, with left/right wrists 5/6. Every
weapon model node lies outside both wrist descendant masks. The control's gun
and all its pieces are right-wrist descendants.

## Defect and change

`FpVisiblePaletteHook` originally collapsed every source outside the two wrist
subtrees to scale 0.0001 when floating hands were enabled. All weapon outputs
of the three reported/suspected models satisfy that collapse condition. This
reproduces their disappearance in an offline test of the production rule.
The supplied log does not explicitly record the floating_hands configuration;
headset verification remains necessary for the full observed transaction.

Merely keeping the gun visible would be incomplete: the arm-IK solve also carries
wrist descendants only by default, so a detached gun could retain its native
placement. The candidate selects the current held model's actual native remap
before reconstruction. That selection reaches the existing shared arm-IK carry,
scale trim and stereo cache key, and the H3 visibility filter now preserves it.

Admission uses Halo 3's existing `LegacyClassifyRuntimeWeapon` body/held-model
observations and native render-model count reader. It does not use a weapon-name
allowlist, fixed weapon checksum, new hook, invented parent link, or the Reach
anatomical mapping. The helper validates generation, slot, count, source,
readability and model indices. Optional -1 outputs stay unmapped; camera,
out-of-range and duplicate sources are refused. Failed admission clears stale
selection and retains existing descendant behavior. Only private palette
scratch is affected; marker/aim/native animation code is unchanged.

This supports detached held models admitted by the existing H3 remap classifier.
It is not a promise for every custom graph: that classifier still expects a
recent contiguous body prefix and an appended model before camera_control.
Unknown/interleaved graph families may need additional H3-specific evidence.
The known early-camera control keeps its existing wrist-subtree behavior.

## Validation and handoff

- **828 checks pass** in `halo3_modded_fp_tests`, C++20 with GCC 14.2,
  `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer.
- The fixture extracts the actual production classifier and H3 selection
  wrapper. Topology comes from the supplied cache, not production constants.
  It covers all model outputs, reproduction of the old collapse, preserved
  hands/hidden arms, actual selections used by IK carry/scale, both slots with
  distinct source banks, generation/read failures, expired observations,
  optional/invalid maps and the highest mask bit.
- **2,793 Reach regression checks pass** after rebuilding their production
  fixture against this source. `git diff --check` and Python syntax checks pass.
- Tests are registered in CMake as `halo3_modded_fp`. They do not execute the full
  Windows matrix solver or native detours, and are not a headset acceptance.
- Local full Windows build/full CTest/PowerShell gate remain unavailable in this
  Linux environment. The user previously elected to compile on Windows.
- The existing GitHub workflow pins source `f70f3b93` and only runs when its own
  YAML changes. It will not automatically build this source-only commit. Compile
  the new source revision explicitly; do not retest the old workflow DLL.

After compiling the new revision, test Bulldog and Ravager, then Skewer; move
head and both controllers independently, toggle floating hands and arm IK,
check weapon scaling and reload, and compare a working gun. Also check dual
wield if the mod permits it. Supply the new log and exact result. No game files
were modified, no map was repacked, and the accepted release pointer is unchanged.
