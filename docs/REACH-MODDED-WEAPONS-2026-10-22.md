# Reach modded first-person weapons: Artifact comparison

Status: static asset comparison completed. The user subsequently resumed a
generic implementation; see [implementation status](REACH-MODDED-WEAPONS-IMPLEMENTATION-2026-10-22.md).
The paused-WIP discussion below records why the earlier approach was insufficient.
No DLL was built, no game assets changed, and no headset acceptance is claimed.

## Inputs and reproducibility

- Branch: `modded-weapons-fix`; baseline source:
  `6919a53a8c6a6ad12ccb5c98355ebaecadaddc2f`.
- User-supplied Prophet's Hand / Artifact `m20.map`, 1,377,579,008 bytes.
- Cache SHA-256:
  `75d7aa599da3aeab9ae54f4a49b00ddf5854657b8131075cc306cf840a0eea28`.
- Supplied log SHA-256:
  `ac335bee79c7097cad9eff5e05c5806b40a64b9d7d28e86977493e73ddd6521c`.
  The log reports the baseline source, Steam and SteamVR/OpenXR 2.18.4, 90 Hz.
- `tools/re/inspect_reach_modded_fp_map.py` reads the cache without writes,
  validates the pinned hash, metadata spans, tag groups/salts, unique node
  identities and parent bounds, and follows each weapon's first FP model and
  animation references. The second FP slot is not the subject of this study.
- Run: `python3 tools/re/inspect_reach_modded_fp_map.py /path/to/m20.map docs/REACH-MODDED-WEAPONS-M20-EVIDENCE.json`.
- JSON records full tag paths, graph parent indices, body/weapon mappings,
  extra nodes and English pickup-string references. Localization offsets and
  selected tag indices are specific to this cache, not engine hook bindings.

## Method and important limitation

The official Elite FP render model in the cache has 41 nodes and checksum
`0x19021311`. Match its node string IDs, and each weapon model's node string
IDs, to the referenced animation graph. All nine inspected graphs contain all
41 Elite body names and all of their weapon model's node names, without overlap.
These are asset-derived mappings, not a captured live palette transaction.
The player shown in the report uses Elite arms. A live diagnostic is still
needed to establish the exact selected graph/palette and early-return reason
for every weapon; the original log does not capture these for failing weapons.

## Results

Indices below are zero-based animation source indices. Weapon indices are
sorted for readability, while JSON retains model output order.

| Display name | Weapon tag | Graph nodes | Weapon sources | Sources outside Elite body and weapon | Baseline Elite-map resolver |
| --- | --- | ---: | --- | --- | --- |
| Covenant Carbine | `20CB` | 46 | 25, 34–36, 45 | None | Reject |
| Plasma Repeater (working control) | `21DB` | 48 | 41–47 | None | Accept |
| LMG | `24E3` | 51 | 47–50 | 6 middle-finger nodes | Reject |
| MA40 | `22E2` | 52 | 47–51 | 6 middle-finger nodes | Reject |
| Precision SMG (possible user's SMG; see below) | `2377` | 51 | 47–50 | 6 middle-finger nodes | Reject |
| Rapid-Fire SMG | `2629` | 51 | 47–50 | 6 middle-finger nodes | Reject |
| CE Fuel rod Gun | `3ECB` | 52 | 47–51 | 6 middle-finger nodes | Reject |
| Halo 3 Fuel rod Gun | `3EA8` | 52 | 47–51 | 6 middle-finger nodes | Reject |
| Spike Launcher | `2645` | 55 | 9, 14, 19–22 | 6 middle-finger nodes + 2 IK nodes | Reject |

The SMG identification must remain qualified: `objects\weapons\rifle\msmc\msmc`
references pickup strings saying **Precision SMG**. The map also contains an
English **SMG** pickup string (`0xCCD4`, text at `0x49428B75`), but no weapon
pickup field examined references it. This does not prove that the user's SMG
is the MSMC. Rapid-Fire SMG is positively identified through the `scorpion evo`
weapon's `we_pickup` string. Other table labels were also checked against their
referenced localized strings rather than inferred solely from tag filenames.

### Three different incompatibilities

1. **Carbine:** same 41 Elite body nodes, different ordering, and weapon nodes
   interleaved among hand nodes. Left/right wrists are 12/14 rather than 11/13.
   Camera control is parented to pedestal instead of base. The graph is not
   simply the Repeater graph with a differently named weapon.
2. **LMG, MA40 and both Fuel Rod variants:** identical body mapping to one
   another, with a 47-node body graph containing both middle fingers (three
   joints each). The Elite render body uses only 41 of those nodes. Weapon
   nodes are appended after 47, not 41. Their wrists remain 11/13, but their
   finger mapping differs from the accepted Elite fingerprint. Precision and
   Rapid-Fire SMG have the same six extra finger nodes and share another,
   reordered mapping: left/right wrists 14/12, camera 6, right upper arm 4.
3. **Spike Launcher:** reordered 47 body nodes plus `l_handik`/`r_handik` at 4/5,
   interleaved weapon nodes, wrists 16/17 and camera 7. Its gun root at 9 is
   parented to base (1), rather than the holding wrist. A wrist-subtree rule
   alone cannot identify and carry this weapon.

Thus the failing weapons share an incompatibility with fixed source-layout
assumptions, but do not all share the Carbine's exact anatomy/order/hierarchy.
Neither the total node count nor the presence of modded assets alone explains
failure. The working Repeater retains the exact accepted Elite layout.

## Implication for the paused correction

A Carbine-only remap is insufficient. In particular, treating every source
outside the 41 rendered Elite body nodes as a weapon would incorrectly classify
middle fingers and the Spike Launcher's IK nodes as held weapon parts. The
incomplete WIP's body-mask-complement approach needs reconsideration before
it can be used for this expanded scope. Do not deliver that WIP as a fix.

The eventual implementation must distinguish the rendered body mapping, the
actual held-model mapping and auxiliary animation nodes. Anatomical arm/hand
identities, descendants and gun ownership must be established separately,
including for a gun parented to base. Preserve feature-local fallback and all
bounds, finite-value and transaction/stereo guards. This analysis does not
establish new runtime bindings or authorize guessed ones.

The intended behavior remains the working Halo 3/Reach behavior: the holding
wrist and gun follow the weapon controller; the free support hand follows its
controller, with intentional two-hand support grip handled separately.

## Validation and log correlation

Compiled the **unchanged HEAD** `reach_render_logic.h` in an isolated C++20
probe with GCC. Submitted all nine asset-derived Elite mappings and their graph
counts: Repeater accepted; the other eight rejected, matching all nine expected
outcomes. This verifies baseline policy rejection of these mappings, not the
live runtime transaction or a corrected build. Probe artifacts are under
`out/modded-weapons-investigation/map_baseline_probe*`; the baseline header was
read with `git show HEAD:src/common/reach_render_logic.h` to avoid testing WIP.

The previous 40-check baseline harness also verified official layouts and
native passthrough for an unrecognized layout. No full Windows build, CTest
suite, headset test or Halo 3 regression test was performed here.

In the supplied log, the failing interval has mapped gun root -1 and no bounds
receipts while controller aim and shot re-aim continue. The functioning interval
commits `body=41 live=48`, identifies `plasma_repeater` checksum `0x1208171E`,
and publishes root 41/bounds. This supports the independent aim-versus-visual
path explanation. Optional finger-pose refusals occur during the functioning
interval and are not evidence for the frozen-arm cause.

Unlisted weapons have not been declared working: absence from the user's list
is not a headset result. Several other tags in this cache also have nonstandard
mappings; their presence does not establish accessibility or failure in this
mission. Runtime work was subsequently resumed; see the implementation status linked above.
