## Subsequent runtime integration

The historical read-only review below supplied the world-frame proof for the
now-integrated opt-in producer transaction. See HALO4-AVATAR-RIG-2026-09-30.md.
Follow-up consumer tracing found that retail `0x36EF20` selects the region mask
before `0x33D8B8` skinning; the avatar mask therefore changes in the producer,
not after skinning. The skin allocator `0x3402F4` permits zero-node allocations
when the native buffer has capacity, so a hidden duplicate hands record can
still traverse the existing skin consumer. Native allocation failure drops
that record independently. Read-only evidence: `out/h4-avatar-submit-kit-root.txt`,
`out/h4-avatar-submit-retail-root.txt`, `out/h4-avatar-zero-skin-allocation.txt`.
This does not establish headset visibility or GPU correctness.

# Halo 4 body palette frame review — 2026-09-30

## Result

The official H4EK evidence proves that the flag-0 body fill is not rooted on
the active render camera. The input is an object-indexed animation-node palette;
the optional root supplied at the body fill is either null or a translation-only
matrix. This rules out applying an eye-camera inverse to the body palette.

The H4EK evidence establishes that the flag-0 body palette is not rooted on the
active render camera, and a separate native consumer establishes that the
object-node matrix getter returns positions in the same world frame as the
native render camera. Therefore the body palette can be treated as world-space
for controller-relative posing. A canonical meter value is not required: the
existing adjustable Halo 4 `g_worldScale` maps VR controller offsets into
engine units.

## Official H4EK chain

Source image: `halo4_tag_test.exe`, SHA-256
`B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6`.

- First-person producer `0x92A1F0` calls palette filler `0x929E60` at
  `0x92A5FB`, `0x92A727`, and `0x92ABCF`. The final call is the flag-0 body
  record; the earlier two records are weapon paths.
- The flag-0 call supplies the player/object index, the body's render-model
  index, `R8D=0`, and `R9=R15`. `R15` is initially null. Only a gated branch
  constructs a translation-only matrix with `0x1EC4F0`; that helper writes
  identity basis/scale and the supplied XYZ translation. The producer's
  camera matrix is separately built by `0x8740D0` and is not passed to this
  body call.
- In `0x929E60`, the flag-0 path iterates the node count and composes each
  input 0x34-byte matrix through `param_4` only when that optional root is
  non-null. The body record therefore receives no camera-root transform.
- The input pointer `R13` comes from `0xE5C090(objectIndex, &nodeCount)` at
  `0x92A861`. `0xE5C090` first asks `0x213D70` for the object's current
  animation-node palette. If unavailable, it obtains the unit object and
  returns the 0x34-byte transform at object offset `+0x1BC` through
  `0xE46310`.
- `0x213D70` validates that the current and previous frame records refer to
  the requested object, then returns the per-object node-matrix array at the
  frame record's `+0x50` with its node count. It copies each 0x34-byte node
  entry or interpolates current/previous entries through `0x214A00`.

This traces ownership to per-object animation data. The coordinate-space check
comes from a separate consumer: H4EK `FUN_1408ED4C0` obtains node transforms
through `FUN_140E5C120`, copies their position into its light-volume entries,
subtracts those coordinates directly from the active rasterizer camera
position (`FUN_1408B6240()+0x14C/+0x150/+0x154`), computes Euclidean distance,
and passes that distance to the light-volume distance evaluator
`FUN_1403AC8A0`. There is no intervening object-root composition or camera
inverse. Since this is native spatial attenuation, those node translations and
the camera position share world coordinates. `FUN_140E5C120` uses the same
per-object interpolated node array as the body producer and falls back to that
object's transform at `+0x1BC`; this closes the animated-node frame question.

The read-only decompilation and call evidence is retained in
`out/h4-object-node-world-consumers-20260930.txt`. This proves the mathematical
frame contract, not that a retail runtime hook or body-pose matrix writer is
safe; those still require exact retail matching, per-instance draw selection,
and a tested all-or-stock matrix transaction.

## Retail match

Pinned retail `halo4.dll`, SHA-256
`7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84`, has the
corresponding producer at `0x3B1B4C`, body call at `0x3B23AD`, filler at
`0x3B9564`, and skinning consumer at `0x33D8B8`. The existing evidence in
`HALO4-SIGNATURE-EVIDENCE.md` records the same flag order and the distinct
camera-root weapon versus non-camera-root body paths. This retail match verifies
the H4EK finding; it is not used to discover the contract.

## Unit and runtime limits

The official `storm_masterchief` export has 120 nodes. Its authored arm
parent-child distances are useful rig-space references. Historical `.21–.23`
runtime readings from the old 80-node indices are explicitly invalid: later
evidence established those indices refer to different full-body nodes, so they
are not a scale measurement and must not be used to infer a conversion ratio.
Halo 4 already exposes adjustable `g_worldScale` in game units per metre; a
canonical absolute unit conversion is not needed for controller-relative IK.
The proven frame does not itself authorize a retail write: runtime mutation
still needs exact model admission, a per-instance region/draw filter, and a
tested full-palette transaction that restores stock on every failure.

## Evidence artifacts

Read-only Ghidra outputs are in ignored `out/` files:

- `out/h4-body-flag0-kit-functions-20260930.txt`
- `out/h4-body-flag0-kit-more-ins-20260930.txt`
- `out/h4-body-flag0-kit-precall-20260930.txt`
- `out/h4-body-transform-kit-decomp-20260930.txt`
- `out/h4-body-objectmatrix-kit-decomp-20260930.txt`
- `out/h4-body-frame-helpers-20260930.txt`
- `out/h4-body-retail-fns-20260930.txt`
- `out/h4-body-flag0-retail-ins-20260930.txt`

No hooks, offsets, or runtime behavior were changed by this review.
