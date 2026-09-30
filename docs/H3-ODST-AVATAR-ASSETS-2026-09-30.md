# H3 / ODST avatar asset audit, September 30

The separate experimental avatar must use actual character geometry. A full
node list alone does not prove that a first-person body contains visible arms.
This corrects the contrary July 19 archived roadmap claim. The H3 optional
runtime transaction described below now uses full-world geometry; ODST remains
asset research. Neither has new headset acceptance.

## Reproducible official evidence

Read the official H3EK/H3ODSTEK tags, exported by their own `tool.exe
export-tag-to-xml` commands. New exports and source tags are preserved under
`out/h3-odst-avatar-review/{h3,odst}`. Earlier untouched first-person exports
are under `out/left-hand-shared-evidence/{h3,odst}`. Run
`tools/re/summarize_legacy_body_xml.py XML TAG OUTPUT` to retain hashes, complete
node and region tables, mesh metadata, and positive raw skin-index counts.
The script repairs only the official malformed `value="<unavailable>"`
attribute in parser memory; source tags and exports remain unchanged. It does
not infer a runtime palette mapping from a raw vertex index.

| Official asset | Nodes | Import checksum | Geometry evidence |
|---|---:|---:|---|
| H3 masterchief/fp_body | 51 | 353376263 | `body` and `legs`; 300/2370 raw vertices; positive skin indices only 0..13, excluding 6/9/10/11 |
| H3 elite/fp_body | 55 | 319362069 | One mesh, 1638 vertices; positive indices 0,1,3,4,5,6,7,8,9,13,14 |
| H3 dervish/fp_body | 55 | 269747456 | One mesh, 1259 vertices; positive indices 0,1,3,5,6,8,9,13,14 |
| ODST odst_recon/fp_body | 55 | 286530845 | Seven meshes; all positive indices fall within 0..14 |
| H3 masterchief/masterchief | 51 | 0x17121B0D | Seven meshes; arms/body/helmet/leftshoulder/legs/rightshoulder regions and positive arm, hand and finger indices |
| H3 elite/elite | 55 | 0x10140D04 | Fifteen meshes; nine regions, including body/head/chest, with positive arm/hand/finger indices |
| H3 dervish/dervish | 55 | 0x17180910 | Two meshes, body/head regions, with positive arm/hand/finger indices |
| ODST odst/odst | 55 | 0x17181A1A | Thirty-seven meshes, sixteen regions including hands/head/body/gear, with positive arm/hand/finger indices |

All these exports have zero entries in the per-mesh node-map table. The raw
skin records and named skeletons distinguish the geometry coverage; a retail
skinning consumer still must prove the final index mapping before mutation.
The full ODST render-model reference is independently established by
`odst_recon.model`: it names `objects/characters/odst/odst`, while animation
comes from `objects/characters/odst_recon/odst_recon`.

The Chief skeleton's arm chains are 14->17->19 and 16->18->20; finger nodes
occupy 21..50. Elite/Dervish chains are 15->18->21 and 17->20->38. ODST uses
15->18->39 and 17->38->42. These are title/model-specific tag facts, not
interchangeable numeric bindings. The first-person meshes' skeletons contain
these names too; this was the misleading basis of the old roadmap.

## Required runtime continuation

Identify each title's local-object render submission and its private full-model
palette, retaining the full object handle so an identical remote/AI model is
never posed. Prove camera visibility, head hiding and any lower-body geometry
mask separately. Chief's authored `legs` region is usable; other characters
do not expose the same region contract. Do not collapse a shared pelvis to
hide legs. Keep existing controller arms/camera functional if the optional
avatar fails. The continuation below implements that boundary for H3 only.

## H3 world submission and skinning continuation

Read-only H3EK analysis on September 30 establishes a concrete full-object
route, separate from `fp_body`. Preserved decompilation exports are
`out/h3-avatar-kit-{visibility,skinning,frame,world}.txt`; source assertion
references are in `out/h3-avatar-render-asserts.json`. Addresses below are
**official `halo3_tag_test.exe` RVAs**, not runtime hook bindings.

* `798B80` allocates a 0x60 visibility object record, stores the full object
  handle at record+0x48, then calls `617890` at `798EA5`. It recursively handles
  object children separately. Model identity alone is therefore insufficient
  to select the local player's body.
* `617890` obtains the render-model tag and object matrices through `8D3B10`,
  selects region meshes, and allocates a frame skinning designator through
  `915980`. `915CD0` resolves that designator to the output buffer. Its call at
  `617BF7` passes the full object handle, render-model datum, source node
  matrices, selected region meshes and output buffer to `7D2A50`.
* `8D3B10` obtains interpolated object matrices through `4D7C20`, or the
  ordinary object matrices through `A431F0`. Its own assertion checks that
  object matrix count matches the selected render-model node count. The
  selected model can be a lower-detail alternate; a runtime adapter must
  verify the complete model identity, count and checksum before using an
  authored skeleton.
* `7D2A50` stores the palette count at skinning+0, region count at +2 and
  sixteen possible `{first_node,node_count}` pairs starting at +4. Palette
  matrices begin at +0x44 and have a 0x30 stride. Its no-node-map path writes
  direct node order. The alternate node-map path performs explicit remapping;
  it must not be treated as the direct path.
* `7D2DC0` converts each 0x34 object matrix using that render-model node's
  default inverse matrix (node record stride0x60, matrix at +0x28), then writes
  a 0x30 GPU matrix. `7D26E0` delegates the transform composition to `41F5E0`
  and emits the transposed/scaled 3x4 output. Thus the GPU palette cannot be
  edited as an array of the existing 0x34 `BoneMatrix` type.
* `798B80` first calls `8D3F20(user,object)` when its caller requests local
  visibility exclusion. That predicate compares a full biped handle against
  `3EA720(user)`; for a weapon object it first follows object+0x10 to its
  owner. The predicate also retains the user's mode condition through
  `3FEDB0`. An avatar exception must not make attached native weapons or
  another user's body visible accidentally.

`617890` may reuse a cached designator, so the implementation deliberately
leaves that native pool unchanged. It replaces only a proven mesh upload's
private mapped GPU destination. No game assets are modified.

## H3 implementation and retail witnesses

`src/dll/halo3_avatar.inl` and `halo3_avatar_backend.inl` implement a separate,
default-off `experimental_body_ik` transaction. Cold install checks all eight
unique hook signatures and exact call edges. No hook is installed while the
option remains off. Optional fault/partial cleanup never disarms the camera,
first-person arms, or OpenXR. Retirement disables hooks, checks entry and
trampoline quiescence, and waits for the complete stereo-pair lease.

| Own H3EK RVA | Pinned retail RVA | Proven use |
|---|---|---|
| 798B80 / 617890 | 252C24 / 20B3BC | Full salted object handle in 0x60 visibility row +48; successful row producer |
| 8D3F20 | 28CDC0 | Local-body exclusion; exception limited to exact 252C75 caller and user zero's full biped |
| 8D3B10 / 4D7C20 | 28B0C8 / 1846AC | Selected render-model identity and world-space animated/interpolated node matrices |
| 7D2A50 | 266838 | Own inverse-bind composition, transposed/scaled 3x4 GPU data at skinning+44 |
| 935840 | 2B32C0 | Native customization's final region selection |
| 9372F0 / 9377C0 | 2B37CC / 2B3CB0 | Outer region submission scopes; temporary head/FP masks restored in finally |
| 935E60 / 8ED7B0 | 2B5450 | Exact mesh upload carrying visibility info, region and mesh indices |
| 926CA0 / 928890 | 2AFB88 / 2AFC14 | Native Map / Unmap-and-bind; exact callers 2B550D / 2B5533 |

The mesh upload computes `{first,count}` from its selected region, allocates
`count*0x30` bytes, copies native skinning matrices, then commits that buffer.
Only this exact active local-body upload substitutes private solved matrices
before commit. Other draws and all native world/cache matrices stay stock.
Its bounded before-image restores a partially written destination on an
optional access fault. Region count/mesh writes are scoped through each full
submission, restored only if full unit/tag/designator and our changed bytes
still agree. Native exceptions propagate through finally cleanup.

The H3 FP preparation chain is `276E1C -> 28ADDC -> 2C0D20 -> 2C5A38 ->
2C561C`. The existing stereo path calls `g_prepareView` before
`g_origRenderView`, with the FP solve scope already armed. The observer reads
the **actual final committed anatomical hand palette**, after visible hand
offsets; it does not derive a second gun calibration. Its own FP checksum,
node count, full owner, title generation, tracking serial, reference epoch and
unique render-pair nonce
must match this pair. Own world/FP palm markers convert wrists between the
different authored rigs. A body lacking a matching current-pair receipt stays
hidden, while ordinary FP arms stay visible. Early FP admission prevents a
later body from doubling those hands. No preceding frame's wrist pose is
relabeled or reused. Atomic refusal counters are reported by the cold worker.

The head target is the exact saved compact center camera after
`ApplyHeadLook`, crouch compensation and physical lean, before eye IPD/cant.
The full body follows the remaining physical displacement from its own
animated head; native animated foot references stay pinned through bounded
leg IK. This does **not** claim that native roomscale collision follows the
body perfectly during simultaneous stick movement. The body solve is visual.

Chief, Elite and Dervish each use their own exported graph and palm/head
markers. Head geometry is hidden by own region masks. Chief's separate `legs`
region honors `body_ik_hide_lower`. Elite/Dervish have leg geometry mixed into
their body region: requesting that mask explicitly refuses their optional
avatar instead of hiding shared torso geometry or silently ignoring the
setting. Their normal FP presentation remains available.

Own world fingers receive physical trigger/grip curl for an unoccupied,
unattached support hand. The Chief has five three-joint finger chains;
Elite/Dervish have four two-joint chains. Held/support-attached fingers retain
their native authored shape. Free fingers reset from the already-validated own
inverse bind palette before curl, so release opens the hand and repeated
submissions cannot accumulate flex. Trigger independently controls the index
for pointing; grip controls the other fingers. Actual `handAlignment`, rather
than input handedness alone, selects the displayed support anatomy.
This is controller-driven finger posing, not
optical joint tracking or contact-reactive finger collision. Exact held-finger
matching between different world/FP meshes and headset appearance remain
validation items.

Offline verification: `halo3_avatar_logic_tests` passes **2867** checks;
`halo3_avatar_runtime_tests` passes **45** checks exercising actual
production hooks, both-eye restoration, exact owner/caller guards, native
exception propagation, ordering refusal, stale generation, and real access
faults including GPU and region-array writes crossing a read-only page. An
interrupted region write restores changed words immediately, even when a
partial write never equals the full intended after-image. Repeated render
pairs with the same tracking serial cannot revive an earlier pair's wrists;
the new pair must observe its own committed palette. The pinned verifier
`tools/verify-halo3-avatar-evidence.py` passes **62** witnesses against the
official kit SHA-256 `59A78F2C96034D7CEB5D710505B2B36813AA141FC81A083E3F952973DBCE4602`
and retail `B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63`.
These establish source behavior, not headset acceptance or support for an
unknown/custom model fingerprint. ODST and other titles require their own
runtime submission proof before receiving this adapter.

ODST now has a deliberately separate controller-finger transaction in its
existing FP arm/hand palette; see `ODST-FINGER-GESTURES-2026-09-30.md`. That
addition does not establish ODST full-world-body ownership or copy this H3
avatar adapter's bindings.
