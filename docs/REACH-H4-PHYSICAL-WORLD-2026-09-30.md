# Reach and Halo 4 native physical world contacts

The player behavior being matched is the Halo 3 native material/breakable path:
either tracked hand or held weapon can strike a real collision surface, and the
game decides whether its authored material produces an effect or breaks. A BSP
surface must never be represented by a fabricated object handle.

## Reach implementation

The previous runtime admitted only native collision type 4 (objects). HREK's
own melee builder also accepts types 1 and 3 and already preserves their exact
material, position, plane normal and breakable tuple. The adapter now retains
those hits through its existing native builder/consumer/damage hooks.

- HREK `D6C1B0` / retail `4919D4` makes its 25-ray native fan. Only the
  physical center segment executes in the owned scope, as before. Type 1/3
  world hits retain `unit=UINT32_MAX`, a separate world identity, native
  fraction and finite point/normal.
- Its collision result has material at `+28`, normal at `+2C`, section at
  `+4C`, and the breakable flag at `+5D` bit 3. When set, it copies bytes
  `+62/+5E` and DWORDs `+3C/+54` to the native melee parameter DWORDs
  `6/7/10/11`. Retail world-copy block `492234..492282` confirms the layout.
  No breakable IDs are invented; ordinary surfaces keep the native -1 tuple.
- Native parameters are checked against the exact ray result before caching.
  A retargeted head/aim-assist result is rejected. Selected surface identity,
  point index, fraction, point and normal must still match at Apply.
- HREK `D6CD60` / retail `492D68` accepts a world damage target (`+1C=-1`).
  With its valid breakable BSP short it dispatches HREK `2B16C0` / retail
  `15AF28` at `D6D231` / `493200`. This function retains material thresholds,
  native surface validity, prediction restrictions and breakable aftermath.
  The owned damage detour checks the complete impact tuple and physical
  point/normal before substituting the hand's physical impulse direction.
- The native consumer retains authored full damage, stock material effects,
  host/client routing and ownership. Reaching this consumer is not proof that
  a material is destructible, or that a predicted request was accepted by host.

No new hook was added. Existing physical-melee opt-in, hand motion thresholds,
per-swing deduplication, simulation/cinematic/vehicle guards, bounded queues,
callback quiescence and feature-only fallback remain. Query and Apply scopes
restore through native exceptions without swallowing those exceptions.

Offline proof: `tools/verify-contact-world-reach-h4.py` currently verifies 69
combined Reach/H4 kit/retail checks, including unique installed proof signatures and the
native breakable call edge. `tests/reach_contact_backend_tests.cpp` compiles
the actual `reach_contact_melee_backend.inl` against mocked native services;
Release build passed and the fixture reports 312 checks, zero failures. The
images' full SHA-256 pins are in the verifier.
Raw primary evidence is preserved under ignored
`out/contact-world-reach-kit.txt` plus the earlier HREK damage-authority logs.

## Halo 4 material and native BSP glass implementation

H4EK differs. Its `E71F40` native melee builder accepts world types 1/3 but
copies only material/point/normal and section. It does not fill the legacy
breakable tuple that its `E730F0` damage constructor still understands.
Removing object guards alone would restore material effects but would not
establish working glass damage.

H4EK `4E8F40` converts a Havok environment ray to native type 3, records the
owning BSP at output `+20`, section at `+24`, material at `+38`, environment
shape at `+60` and shape key at `+68`. Optional input byte `+128` requests the
material, already enabled in the physical query. The adapter now preserves
this exact world identity through query, center-ray replay, native parameters
and the damage callback, retaining the native material-effects path. Shape
pointer/key, BSP, instance, material, position and normal must still agree;
an unrelated replacement surface is rejected. Padding byte `+35` is excluded
from identity. No object lookup is made for a world target of -1.

The production backend fixture `tests/halo4_contact_backend_tests.cpp` passed
621 checks in Release, including world/object routing, changed-surface
rejection, host prediction and native exception restoration. Runtime world-copy
proof `602580` and constructor proof `603740` are unique in the pinned image.
H4EK `E730F0` calls the authored breakable consumer `3D4D20` at `E7362E`;
the retail edge is `603768 -> 21E908`.

The native Havok melee builder leaves its breakable tuple at -1. A separate,
optional supplement now resolves a genuine collision BSP surface along the
same physical segment after the exact Havok replay succeeds. It does not add
a hook or call damage directly. The optional Havok ray list at `+50/+58` was
rejected: H4EK debug consumer `1485C0` resolves render geometry and uses those
values as render triangle indices, which are not collision BSP surface IDs.

The supplement follows H4EK's own vector path `4EDF90 -> 4EE150 -> 4DBFF0 ->
7ADE90`. Collision flags bits 12..17 become BSP flags, with the low two bits
set to 3 when absent, exactly as `4EDF90` does. Retail `24F53C` matches the
`4DBFF0` caller and inlines the outer wrapper. Its initializer is `2DE9FC`;
standard/supernode traversal paths are `2DF698/2DF400` and the alternate BSP
representation `2DEF10/2DEC78`. The initializer returns a 16-byte span in
**XMM0**; the decompiler's integer return guess was rejected against the
actual caller's `movaps [rsp+50],xmm0`. The implementation preserves the SIMD
return, ten-argument ABI and aligned 0x120-byte traversal context.

The BSP resource/view helper `21F7B4 -> DEA60` is called by the native
breakable damage path and matches H4EK `9F640 + 10AD00`. Both representations
return the actual surface index at result `+14`, plane at `+8`, orientation at
`+1C`, flags at `+1D`, surface/set bytes at `+1E/+1F`, and material at `+20`.
The supplement requires the breakable bit, no invalid bit, valid surface/set,
finite fraction and plane, point within 0.005 native units, fraction within
0.01, and aligned normal against the already confirmed physical hit. Native
`21DFF8` (H4EK `3D5B10`) must still confirm the authored surface is live.

Only then are native parameter DWORDs `6/7/10/11` filled with real set,
surface, instance=-1 and collision surface index. The existing consumer and
damage constructor retain full authored damage, material thresholds and
host/client ownership. This covers a matching native BSP surface; it does not
invent an instanced surface when the BSP query cannot resolve one. Such hits
retain their ordinary material path. Missing/ambiguous signatures and a native
BSP boundary fault disable only this supplement; stock material contacts and
object damage continue, with the supplement state reported outside callbacks.

The production fixture also exercises both representations and both tree
forms, SIMD span preservation, changed-surface rejection, broken-surface
rejection and optional-boundary fault isolation. The expanded Release fixture and focused CTest pass. The injected SEH case
also verifies that subsequent contacts bypass the faulted supplement while
ordinary material contacts continue. The guarded native wrapper uses explicit
volatile result/fault locals and publishes its atomic fault after the SEH
scope, avoiding the optimized early-return form that failed this regression. Raw primary
proof includes `out/contact-world-h4-bsp-vector-api.txt`,
`out/contact-world-h4-vector-retail.txt`, and the `bsp-retail-proof/alt` logs.

## Acceptance limits

No game was launched or installed. A target-title headset test, host/client
co-op test, both hands and the Halo 3 regression remain required. Ordinary
solid walls must remain indestructible. Breakable glass should follow its own
authored threshold; vehicles/object attachments must retain their original
object behavior. A successful offline fixture is not headset acceptance.
