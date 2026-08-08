# Halo 4 first-person VRIK authoring evidence

## Purpose

`tools/build_halo4_vrik_scene.py` builds a Blender calibration scene for the
Halo 4 first-person player model. It follows the authored-landmark workflow
used by the Reach vehicle-camera kit, but exports two shoulder anchors, two
hand poses, and two elbow-pole directions for runtime two-bone IK.

This tooling does not alter Halo 4 tags and is not evidence that a retail skin
palette hook is installed. The runtime consumer remains a separate candidate.

## Official source

The scene is built from the official H4EK tag:

`objects\characters\storm_fp\storm_fp.render_model`

The XML was produced by H4EK `tool.exe` version `1.890.0.0` using
`export-tag-to-xml`. Its SHA-256 is embedded in every generated `.blend` and
exported JSON. The current source export hash is:

`047501A9C6811097FC8E6ABBB591EC5BC4610EE441976CAAFED9EEFF6F13591F`

The official tag contains 80 contiguous first-person body nodes. The arm
chains used by the authoring rig are:

- right: node 4 `b_r_upperarm` -> node 16 `b_r_forearm` -> node 29 `b_r_hand`
- left: node 5 `b_l_upperarm` -> node 8 `b_l_forearm` -> node 37 `b_l_hand`

The visible Chief scene uses raw meshes 3, 50, 97, and 98: techsuit, Chief
arms, left shoulder, and right shoulder. Raw positions are decoded through the
tag's authored position-compression bounds. Skin indices are resolved through
each mesh's official per-mesh node map.

Coordinates retain Blam's `+X forward, +Y left, +Z up` convention and convert
world units to metres with `1 wu = 3.048 m`.

## Controls and output

The scene contains exactly these movable empties:

- `vrik:right_shoulder`, `vrik:right_hand`, `vrik:right_pole`
- `vrik:left_shoulder`, `vrik:left_hand`, `vrik:left_pole`

`tools/halo4_vrik_authoring.py` provides the Blender sidebar. The hand controls
drive two-bone preview IK, shoulder controls move the chain roots, and pole
controls select elbow bend direction.

`tools/parse_halo4_vrik_points.py` refuses export when a control is missing,
renamed, non-finite, or not explicitly marked placed. Its JSON contains metre
and world-unit positions, hand quaternions, shoulder-to-hand vectors, normalized
pole directions, full audit matrices, and the embedded H4EK provenance.

## Validation

The generated kit was clean-loaded in Blender 5.1.2. Validation confirmed 80
bones, four official skinned meshes, six controls, successful draft JSON
export, and an evaluated forearm change when the right-hand target moved. The
builder solves each mirrored chain's Blender pole angle independently and
rejects the scene if enabled neutral IK moves any arm joint from the official
tag bind pose.
