# Physical crouch camera correction, September 23

This records the completed read-only camera integration prepared for local
validation, not headset acceptance. The reference behavior is Halo 3 positional
tracking: moving the physical head down should not add an unchecked second
native crouch-height drop. Input/config/menu work is owned by the parent task;
native hold-to-crouch remains the documented preference requirement.

`physical_crouch_camera.cpp` publishes a coherent title/generation/tracking-epoch,
input time and gesture request. A separate invalidation epoch prevents a racing
input publication from reviving a suspended gesture. The camera lease expires
after 100 ms, follows the native standing transition only after owning an actual
gesture, and retires on stale tracking, recenter, changed player or storage.

Every correction is capped at the current downward tracked-Y displacement in
game units. After positional tracking, the resulting eye cannot be raised above
the unmodified native camera by this correction. Shallow real crouches may
therefore retain some native lowering; this is deliberately bounded compensation,
not a claim that native collision/camera interpolation can be removed. Tilted
native up-vector modes and alternate on-foot camera selectors stay stock for
this optional correction. No body position, crouch fraction, physics field or
game file is written; no new hook is installed. Cold signature failure affects
only crouch-height correction and is logged.

## Halo 3 and ODST

H3EK `A61330` calls its biped evaluator `A9A230` in estimate mode zero. Pinned
retail `3555EC`, call `3556B7`, matches evaluator `37C394`. Normal camera selection
requires parent `+10 == NONE`, flags `+110` bit 2 clear and kind `+96 == 0`.
The evaluator reads crouch fraction `+384`, definition standing/crouched heights
`+2E8/+2EC`, and scale `+8C`. Its own special predicate `37E58C` treats byte
`+4DE == 6` plus `+4E4` bit 4 as standing. Physics modes 7/8, found through signed
component offset `+162`, use a different up vector and are refused.

The H3 reader computes `(standing - crouched) * fraction * scale`. Both the
current salted local-unit storage and its loaded definition are revalidated
before changing the copied camera. Five independently unique native witnesses
guard the layout and branch meanings. `verify_h3_physical_crouch_camera.py`
passes five witnesses, two call edges, and 162 actual native math executions in
Unicorn. The pinned H3 DLL SHA-256 is
`B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63`;
kit SHA-256 is
`59A78F2C96034D7CEB5D710505B2B36813AA141FC81A083E3F952973DBCE4602`.

ODST uses the parent's independently verified reader in
`physical_crouch_native_read.h`: retail evaluator `38A408`, native selector
`399DC4`, fraction `+398`, own definition heights `+310/+318`, scale `+8C`,
special state `+516/+51C`, and signed physics component `+15A`. The native
wrapper checks ODST's salted biped table and own loaded definition. It does not
borrow Halo 3's field offsets. Parent-owned H2/CE integrations and their tests
are recorded separately; this document does not claim their validation.

## Reach and Halo 4

The official kits explain a different mechanism. HREK evaluator `DC65F0`
consumes cached camera height `+A38`, model scale `+80`, and separately adds the
native bob terms. Producer `DCB7C0` approaches the target returned by `D88160`.
H4EK evaluator `F0EB90` consumes cache `+F74` and scale `+A0`; producer `F02A80`
approaches target `EB10D0`. Each target chooses its own animation-camera string
ID's standing/crouched height pair, falling back to its tag's base pair.

These constructs match their own pinned retail modules:

| Proof | Reach | Halo 4 |
| --- | --- | --- |
| Biped evaluator | `4A2D50` | `6320D0` |
| Normal unit-camera call | `48A284` | `5F8BF2` |
| Smoothed-height producer | `4A0DF0` | `62E184` |
| Current target selector | `4A0D14` | `62E0A4` |
| Animation component offset / camera SID | signed `+18E` / `+1FC` | unsigned `+1E2` / `+224` |
| Base standing / crouched tag heights | `+524/+52C` | `+618/+620` |
| Override block / record stride | `+54C`, 12 bytes | `+640`, 12 bytes |
| Physics component / alternate up | signed `+1B2`, modes 7/8 | unsigned `+47A`, modes 7/8 |
| Seat / camera flags / biped kind | `+336/+138/+8E` | `+2C/+16C/+B2` |
| Actual cache / scale | `+A38/+80` | `+F74/+A0` |

Both target selectors read native binary crouch state (Reach `+1C4` bit 20,
H4 `+490` bit 20). The integration deliberately uses the actual smoothed cache,
not that target bit, so activation/release do not jump ahead of the native
camera. It computes `(selected standing height - current cached height)*scale`.
The native bob additions stay intact. Camera tables have finite/count bounds,
camera-ID overrides are respected, changed/unloaded tag pages are refused,
and caches outside the selected native height range are refused. The camera
wrapper requires a fresh player-controlled cinematic receipt and current local
on-foot owner; its pointer/tag/generation checks repeat before application.

Paged tag globals are decoded from each title's uniquely matched target
selector. Packed addresses retain the full native encoded value in the address
arithmetic; the high nibble selects its title-owned page. No live memory probe,
native height function call or signature scan occurs in the camera callback.
Existing local-owner accessors are reused.

`verify_cached_physical_crouch_camera.py` checks the ten production patterns,
their unique text matches and unwind ranges, four call edges, each title's own
tag/page RIP references, and pinned retail/official-kit hashes. Preserved own-kit
and retail decompilations are under `out/coop-stability-20260923/crouch-*` and
`root-crouch-*`; no BSim score alone is treated as proof.

## Final offline validation

The combined Release build and all 83 CTest cases pass, including the expanded
Reach/H4 reader, H2/CE camera fixtures and vehicle identity fixtures. Native
witness scripts for H3, ODST/H2, CE and Reach/H4 pass. The exact Reach/H4 selector
functions are leaf functions without required unwind records; the verifier
checks their complete instructions for no calls, stack or nonvolatile changes
instead of incorrectly requiring unwind metadata. Nonleaf witnesses retain
their unwind-range checks. These results do not establish headset acceptance.

### Earlier integration handoff

The actual shared publication/lease and H3 raw-reader fixture passed, as did
the corrected core roomscale suite (2/2 selected CTest targets). The fixture was
then expanded with the actual Reach/H4 reader, native animation-camera override,
cached-blend, model-scale, paged-tag, stale-owner and invalid-data cases. The
parent owns the final compile/test pass for that expanded fixture and DLL.
No further build was launched by this agent after the final freeze instruction.
All new camera behavior still requires the user's headset test, including a
Halo 3 regression result. Accepted-build pointers remain unchanged.
