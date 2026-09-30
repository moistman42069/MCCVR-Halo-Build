# CE experimental avatar: own-title evidence

The target is the same player experience requested for Halo 3: a local full
body that follows roomscale, with controller-driven arms/hands, finger poses
and an independent lower-body hide option. This file records proven inputs,
not an enabled avatar or headset acceptance. Both CE graphics modes remain
required.

## Official geometry

`tools/re/verify_ce_avatar_assets.py` reuses the existing official HCEEK tag
descriptor checks and parser. Its output is
`out/ce-avatar-model-20260930.json`. The extracted official world model
`characters/cyborg/cyborg.gbxmodel` has SHA-256
`92842DA6B94E0A4E172209EE0D858640FD6B22147AB805A94A18056838735BF7`.
It contains 19 nodes, 17 parts and 6,025 uncompressed vertices across its five
geometry LOD records. There is one region, so an H3-style named legs-region
mask cannot simply be copied here.

Its own arm chains are left `13 -> 15 -> 17`, right `14 -> 16 -> 18`.
Nodes 17/18 are terminal hands: **the world rig has no finger joints**.
The separately verified 37-node first-person hand model has five finger
roots beneath each wrist. Finger articulation therefore requires a deliberate
hybrid or replacement hand presentation; posing the world rig alone does not
satisfy that request. This is a fact about this official model, not a claim
that CE cannot support finger posing.

The existing first-person palette now has a separate experimental free-hand
gesture path. It binds the five three-joint chains by exact official node names
and parent links, then applies same-frame physical input to the free support
hand: trigger flexes only the verified index chain, and grip flexes the other
four. It leaves the gun and primary hand authored, skips posing while a
persistent support grip is attached, and preserves the tracked-hand result on
unknown graphs or invalid input. The per-frame palette is freshly rebuilt from
native output, so flex does not accumulate; this is not a verified bind-pose
reset and zero input does not claim to force an anatomically open hand. This is
first-person finger articulation only;
the world-rig/full-body route below remains unimplemented and unaccepted.

## Full-object render route

The preserved HCEEK trace in
`out/coop-stability-20260923/ce-render-objects-model-trace.txt` establishes
VA `00830260` as the recursive world-object draw path. It retains the full
object datum, chooses interpolated matrices through `005A2BF0` or ordinary
matrices through `00803940`, and passes them to model submission `007DC9B0`.
Its model argument comes from the object's definition at `+0x34`; the full
object datum is argument 11. Attached children are traversed separately.

The pinned retail homolog is `halo1.dll+B48F60`: the corresponding matrix
providers are `BA8B10` and `B36E64`, and model submission is `C6350C` at
calls `B4922E` and `B492AD`. The object/model path, two matrix-source branches,
shadow flag, modifier effect and child recursion all match the own-kit
semantics. `out/ce-avatar-world-retail-20260930.txt` preserves the dump.
Direct calls were checked against executable sections and PE unwind function
boundaries. These addresses are evidence, not installed hook bindings.

The retail exclusion helper `B48B30` matches the kit's inline local-object
tests in `00830260`: compare the full object datum against user mapping
`00591EC0`, examine native camera mode `00501150`, and retain the separate
`004FFF00` exclusion. Own-kit `00591EC0` asserts the four-user bound and reads
the user-to-object table. Retail `B48B30` reads that same kind of table and
calls `B14EA4` for the mode. Dumps:
`out/ce-avatar-exclusion-{kit,retail}-20260930.txt`.

## Next implementation requirements

An optional local-body exception must be scoped to the actual local full
datum and the intended render pass. It must not change the user mapping or
native object matrices, expose attached weapons, or affect another player.
The model submission can accept a private pose, but its exact ABI, complete
retail rig identity, consuming lifetime and all visibility gates still need
verification. Head, hand and lower-body geometry masking needs CE-specific
evidence because this model has a single region. Anniversary's separate world
skin/visibility bridge remains to be traced; the proven first-person bridge
does not establish world-body coverage. No CE avatar hooks were added by this
evidence pass.
