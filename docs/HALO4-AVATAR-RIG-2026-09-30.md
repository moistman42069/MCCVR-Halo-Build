# Halo 4 experimental avatar rig evidence

This records the experimental H4 runtime avatar candidate, grounded in its own
120-node world rig. The opt-in producer transaction now stages head/body follow,
controller-driven arms, bounded native-foot retention and free-hand finger poses.
It preserves the ordinary hands/gun path on refusal. This is implemented source,
not headset acceptance or all-title avatar completion.

Official H4EK source:
`objects/characters/storm_masterchief/storm_masterchief.render_model`,
80,667,072 bytes, SHA-256
`C2699114B2FF80031F00AA4CBE6ABB0FEB70D86A29F9D05BB7EA086211563DAB`.
`tool.exe export-tag-to-xml` produced the ignored 1,653,474,661-byte XML under
`out/h4-body-identity-review`. `tools/re/summarize_model_xml.py` streams it into
`storm_masterchief.summary.json` without retaining geometry. The export has
120 nodes, import checksum `0x17010100`, 883 region permutations, 935 render
meshes/node maps and 416 raw geometry meshes. Later meshes include clones;
their absent raw vertices do not mean they render nothing. The official export
contains a malformed `<unavailable>` attribute after all required mesh blocks;
the parser stops after validating the complete earlier tables.

This is a different graph from the 80-node `storm_fp` first-person hands model.
No node index may be transferred between them.

| Chain | Exact 120-node indices |
| --- | --- |
| Root/spine | pedestal 0, pelvis 3, spine1 4, spine2 13, spine3 19 |
| Neck/head | neck 24 -> 33 -> head 54 -> helmet 61 |
| Left arm | clavicle 27 -> upperarm 34 -> forearm 58 -> hand 73 |
| Right arm | clavicle 28 -> upperarm 38 -> forearm 50 -> hand 68 |
| Left leg | thigh 5 -> calf 10 -> foot 18 -> toe 29 |
| Right leg | thigh 6 -> calf 9 -> foot 22 -> toe 23 |
| Left fingers | thumb 84/100/110; index 94/99/115; middle 89/104/114; ring 93/102/113; pinky 90/103/109/118 |
| Right fingers | thumb 85/101/111; index 81/107/116; middle 87/106/112; ring 96/98/117; pinky 80/105/108/119 |

The arm links are approximately 0.0915251 / 0.116662 authored units; leg links
approximately 0.16764 / 0.185722. These are direct authored graph distances,
not a measurement of a running game's palette or a new fixed metric scale.

There are separate regions `armor_arms`, `armor_fx`, `armor_head`, `armor_legs`,
`armor_shoulder_left`, `armor_shoulder_right`, `armor_torso`, `body_arms`,
`body_head`, `body_legs`, `body_torso`, `visor` and `waist_cap`. The native render
consumer must resolve the selected permutation and clone before a local-only
region suppression can be implemented. For example, meshes 416/417/418 clone
techsuit meshes 18/19/20 for arms/legs/torso. A single hardcoded mesh skip is
insufficient.

All weighted vertices in the 416 raw meshes descend from pelvis. Hiding that
hierarchy would also hide the upper body. Some weighted vertices cross leg and
pelvis/spine or head and neck boundaries, so zeroing selected joint scales is
not a proven region-hiding operation either. Preserve upper-body transforms;
find the per-instance region/draw consumer for Hide lower body and head clipping.
Rigid meshes and clones also require their native selection path.

Palette ownership, coordinate frame and draw selection are independent proofs.
See `HALO4-BODY-PALETTE-FRAME-REVIEW-2026-09-30.md` for the current producer trace.
The existing producer hook now admits only a matching local unit and its exact
world-body and FP-hands rows in the current render pair. It verifies each model's
own count/checksum before staging any change; unknown rigs retain native behavior.

`src/common/halo4_body_ik_logic.h` now implements a pure full-transform solve
against this exact 120-node graph. It checks runtime-import checksum and node
count, derives limb lengths from the current pose, and can compose a roomscale
body-root delta across all nodes, align the head to the HMD, solve controller
wrist targets on both arms, and solve optional foot targets through the
official thigh/calf/foot chains. Limb rotations are applied to their exact
descendant trees; one complete candidate palette is published only if every
requested operation is finite and reachable.

Controller trigger/grip can drive independent synthetic finger flex on hands
whose caller allows finger posing. The synthetic flex axis derives from each live finger segment and the own
semantic palm-down direction; no authored per-joint flex-axis claim is made. The caller must disable this option when an authored held-weapon grip is
needed. Contact-reactive finger posing remains separate future work. Tests
cover both arms, a foot chain, root/head follow, controller wrist orientation,
finger descendant rigidity, held-grip preservation, wrong graph size,
nonfinite inputs, unreachable targets, and output rollback. The helper is now called by the guarded H4 producer path. The runtime constructs a
private preview with the same hand solver, support ownership, handedness and
visual offsets as the ordinary FP path, then transports its wrist targets through
each rig's own palm markers. Preview does not publish a weapon relation or contact
sample. Local-unit admission now happens before body staging, avoiding dependence
on the later skinning hook's first-eye receipt.

Arms permit bounded proportional extension (at most 1.75 times native reach),
while foot targets retain native animation positions whenever reachable. Larger
head shifts move the targets only enough to keep a valid leg solve; this is not
floor collision, foot tracking or a new walking animation. Hide lower body uses
separate native body/armor leg regions, and the transaction hides the duplicate
FP hands only after a complete avatar candidate is ready. Palette and mask writes
roll back together on failure.

Free fingers reset from their own authored default local transforms before flex;
trigger controls the index independently, grip the other digits. Released input
opens, grip alone points, and both inputs form a fist. Held primary or latched
support keeps its authored grip. These are controller-derived gestures, not
optical hand tracking or contact-reactive fingers. The generated bind table is
`src/common/halo4_body_finger_bind.generated.h`; its default translation/rotation
fields avoid the known shifted inverse-field labels in the XML export.


### Final avatar refinements

The HMD target is transported through the inverse of the world rig's own `head`
marker (node54), instead of placing the bone origin at the eyes. Its local
translation is `(-1.47467e-6,.0336001,-.0322376)` and quaternion is
`(-.492315,.507569,.507569,.492315)`. Marker display scale `.1` does not shrink
the skeleton. Head and palm composition tests verify semantic target coincidence.
The straight-arm singularity now selects a perpendicular from that native
upperarm's own basis, avoiding unnecessary avatar fallback at collinear reach.

The exact 80-node `storm_fp` first-person path also poses free support fingers
under the same opt-in setting, so they remain available when the world-avatar
transaction falls back. Its separate own default locals/parent tree are generated
by `tools/re/generate_h4_fp_finger_bind.py`, from XML SHA-256
`047501A9C6811097FC8E6ABBB591EC5BC4610EE441976CAAFED9EEFF6F13591F`.
Only this rig's own finger descendants change; wrists, arms, gun and authored
held-hand grip are preserved. Runtime tests check both anatomical sides, opening,
pointing, fist, repeat stability and rejected foreign-graph/nonfinite input.
