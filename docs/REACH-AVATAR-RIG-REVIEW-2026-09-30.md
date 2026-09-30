# Reach experimental avatar rig review — 2026-09-30

This review addresses the requested separate experimental full-body IK mode.
It does not treat existing floating first-person arms or seated lower-body
visibility as a full avatar implementation. No runtime world-palette mutation
is enabled by this research.

## Official assets and identity

Discovery used only the official HREK archive and editing tool. HREK build is
`2023.07.17.176677.1-QFE1`; `reach_tag_test.exe` SHA-256 is
`CBDD8448A87A433B0DFFC0DE47D06DB7A18B4BF868B96B057135DAA86790ABA8`, and
`tool.exe` SHA-256 is
`7C6B868B5F5B9303AA9210F31A7CC3221A6FA419A6AB3F323C8E0831DFD0AFCD`.
The archive SHA-256 is
`B093CC7B657EAC7C209880682752F72E0F5FB88591AADFD0B527B5C0111D8254`.
The selected official tags were extracted read-only from `HREK.7z` into the
ignored `out/reach-avatar-review/tags/` tree and exported with
`tool.exe export-tag-to-xml <absolute-tag-path> <absolute-output-path>` from an
isolated working directory containing `tags`, `data`, and `project.root`.

The biped tags reference these model tags, which in turn reference the exact
full character render model and animation tag:

| Species | HREK model / render model | Render model import checksum | Nodes | Export SHA-256 |
|---|---|---:|---:|---|
| Spartan | `objects\characters\spartans\spartans` | `0x171B0502` | 82 | `D1F3E6E41F402E66002AAB2C69642CD93B7B6CC79AD69D28B5487A894FC44F18` |
| Elite | `objects\characters\elite\elite` | `0x171C1D0F` | 67 | `BF23AC53847ABB3DDFB7AC8D634B71FD099781FE15260CB30BB557C78377F488` |

These are not interchangeable with the lower-body `fp_body` assets. The
Spartan/Elite `fp_body` models have 82/67 nodes too, but their identity tuples
are `0x10041201` / `0x1404030E` and their only regions are `decals` and
`flair_knees`. Those assets do not provide a torso or a complete full-player
avatar. The existing 47/41-node first-person `fp` assets are separate arms
palettes and likewise do not prove a full avatar.

## Rig and finger evidence

The full Spartan render-model skeleton has the following named chains:

- Pelvis 1 → spine 1/2/3 at 7/8/18 → neck 0/1 at 27/30 → head 38.
- Left clavicle 22 → upper arm 28 → forearm 41 → hand 44; right clavicle 26
  → upper arm 31 → forearm 40 → hand 47.
- Left pelvis branch: thigh 4 → calf 10 → foot 19; right: thigh 6 → calf 9
  → foot 20.
- Thirty authored finger joints occupy node indices 48–81.

The Elite rig has a different hierarchy and indices:

- Pelvis 2 → spine 1/2/3 at 5/9/12 → neck 0/1/2 at 15/19/25 → head 31.
- Left clavicle 14 → upper arm 20 → forearm 24 → hand 33; right clavicle 13
  → upper arm 23 → forearm 30 → hand 32.
- Left leg: thigh 4 → calf 8 → `b_l_horselink` 11 → foot 17; right: thigh 6
  → calf 7 → `b_r_horselink` 10 → foot 18.
- Twenty-four authored finger joints occupy node indices 34–66.

The fingers have three named joints per digit and side in the world-model rig.
This makes controller-driven curl posing structurally possible for these two
Reach skeletons; it does not establish that the retail local-player draw
palette can be safely isolated or that authored hand/weapon grip ownership may
be replaced. Hold finger mutation until that ownership is proven.

## Visibility and implementation boundary

Official HREK `unit_camera_flags_definition` names bit 2 (`0x0004`) “hides
player-unit from camera.” Its retail consumer is already documented in
`REACH-SIGNATURE-EVIDENCE.md`; this is an existing camera visibility policy,
not an avatar palette API. Clearing it could expose a full local unit model,
but it does not identify the exact local render submission among other
Spartans/Elites, align that pose to the HMD/controllers, or prevent head
clipping. Do not clear the bit as a substitute for body IK.

Neither full model provides a proven dedicated lower-body region. Spartan's
15 regions include `arms`, `body`, `head`, and cosmetic `flair_knees`; Elite's
four are `armor_fx`, `armor_pieces`, `body`, and `head`. `flair_knees` is not
evidence that the actual legs can be hidden independently. The pelvis is shared
by torso and leg branches, so collapsing it would damage upper-body motion.
The requested Hide lower body control must remain unavailable for Reach until
the exact mesh/region consumer and skinning boundary are proven.

The remaining blocker for an implementation is the exact retail path from the
local unit's render record to its full-world-model node palette. A generic
render-model palette hook cannot identify local ownership from the model tag:
other actors can use the same Spartan/Elite model. No hook, transform write,
or per-region override should be added until HREK explains this ownership and
the retail match proves a unique local-only consumer/row. The first viable
prototype should then transform a private copy of that exact 82/67-node palette
with the HMD, tracked wrists, authored finger chains, and roomscale root, and
restore stock for only the avatar when any identity, target, or solve check
fails.

### HREK camera and model-render trace

The existing camera-hide route was rechecked in official HREK. `following_camera`
virtual slot 13 at `0x516BD0` resolves the controlled unit for the active camera
and returns that unit only when the camera's `hides player-unit` bit is clear.
This proves the correct local-unit identity for camera policy, but that function
does not produce or own the full-body node palette. Its retail counterpart is
already independently matched in `REACH-SIGNATURE-EVIDENCE.md` at `0x1DD360`.

The generic HREK model queue drains through `0x2B7800`, calls
`0x2B0CF0`, then builds render-model node/region data in `0x2B49D0`.
`0x2B7800` passes values read from the queued model record (including the
render-model datum) into that general builder. The builder's visible-palette
interface has no explicit controlled-unit datum, so observing its model
checksum cannot distinguish the local Spartan/Elite from remote actors. The
trace does not establish that the queue row is the first-person/local unit or
that the row has an owner handle suitable for a per-instance transaction.

More specifically, `FUN_1402B7800` scans active `0x38`-byte TLS work entries
at `TLS+0xE30`; each entry's `+0xE30` value is resolved through
`FUN_140083E20(TLS+0x5F0, entryValue)`. The returned model/render record
supplies model type (`+0x466`), model index (`+0x464`), asset handle (`+0x458`),
and transform descriptor (`+0x430`) to `FUN_1402B0CF0`. The drain clears the
entry active byte at `+0xE34`. This queue item is a keyed model/resource work
item; the observed drain passes no unit datum to the generic model builder.
The `FUN_14041D120`/`FUN_14041FF70` producer family also resolves model
definitions and emits per-model submissions, so it does not yet establish
local ownership. The remaining proof must start at the higher-level object or
unit producer, before it reduces the request to a model-only queue entry.

The separate HREK `render_objects` visibility walk was traced one stage earlier.
`FUN_140806020` invokes `FUN_140881AB0` for the world-object rendering pass.
That routine walks visibility records under the render-object table, validates
each record's render-model datum at record offset `+0x24`, resolves its model
definition through the handle at `+0xC0`, then calls `FUN_140882B90` with the
geometry subrecord (`record +0x20`) and render flags. The called geometry
submission routine receives no parent visibility-record pointer or controlled
unit datum. This proves that the consumer-facing submission has already
discarded object ownership by the time it reaches the geometry/palette helper.
The visibility-record index itself is an index into the current render-object
table; no evidence yet equates it with a unit datum. The enqueue/record producer
must be traced further before any per-instance palette or region mutation can
be considered. Supporting read-only HREK decompilation is retained in ignored
artifacts `out/reach-avatar-review/hrek-object-vis-constructors.txt`,
`hrek-object-submit-trace.txt`, and `hrek-render-vis-caller-context.txt`.
These HREK traces are preserved in ignored artifacts
`out/reach-avatar-review/hrek-palette-camera.txt`,
`hrek-render-owner-candidates.txt`, `hrek-model-render-wrapper.txt`, and
`hrek-submit-producer.txt`; no retail hook has been inferred from those kit
addresses.

The review therefore closes the available rig and camera-policy evidence, but
not the required avatar consumer proof. Reach experimental full-body IK,
finger shaping, and its lower-body hide option remain unimplemented and must
stay off/unavailable until a retail-matched render submission carries an exact
local-unit owner identity. Do not clear the camera hide flag to simulate an
avatar: that would only reveal the native unit and would not provide tracked
pose alignment or head-clipping protection.

### Follow-up on the existing world-object producer path

The retained HREK producer artifacts also cover `FUN_140883900`, the second
render-object visibility branch. It walks the active object-visibility table
and passes each entry's geometry subrecord (`entry + 0x20`) to the same
`FUN_140882B90` geometry submission used by `FUN_140881AB0`. The `+0xC0`
definition datum is used for model/name resolution in the diagnostic path; it
is not forwarded to the geometry submission as a controlled-unit owner. The
entry's `render_model_index` is the visibility/model index named by HREK's
`render_objects.cpp` assertion, so it is not evidence of a salted unit datum.
This closes the readily available alternate world-object producer branch but
does not supply the missing exact local-player ownership handoff. It does not
change the no-hook/no-palette-write conclusion above.

## Retained artifacts

The ignored `out/reach-avatar-review/` folder contains the official biped and
render-model tags, all eight XML exports, extracted node/parent JSON, and the
HREK tool outputs. Full render-model XML exports include an HREK exporter
`api resource` field containing the literal `<unavailable>` marker; a sanitized
copy was used only for offline inspection. Source tags and installed HREK files
were not modified. Existing pinned first-person and `fp_body` export hashes
remain in `REACH-SIGNATURE-EVIDENCE.md`.
