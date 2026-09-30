# ODST controller-driven FP finger gestures

The requested player experience is tracked arms with responsive free-hand
fingers, while held guns and attached support hands keep their authored grip.
ODST already has its own accepted first-person shoulder/elbow/wrist IK path.
This addition runs under `experimental_body_ik` and adds free support-hand
finger posing at the **committed native FP palette**, after existing anatomical
routing and visual offsets. It adds no native hook and does not alter the
camera, weapons, first-person IK solve, collision geometry or game assets.

The retained official ODST evidence establishes the FP path; it does not yet
establish an ODST full-world local-body visibility/private-GPU transaction.
The new H3 avatar bindings are deliberately not reused. **ODST full-world
avatar, tracked torso/head/legs and a full-world lower-body mask remain
unfinished.** This is an explicit FP implementation of finger gestures rather
than a claim that all-title avatars are complete.

## Own-title evidence and geometry

`ODST-WEAPON-IK-EVIDENCE.md` documents the own H3ODSTEK FP model and the uniquely
resolved retail `0x2EDD10` final palette. `ODST-VEHICLE-EVIDENCE.md` establishes
this title's loaded render-model node block at `+0x30/+0x34`, node stride
`0x60`, and stored default inverse at `+0x28`. The existing production
`OdstResolveAuthoredNodeLocal` already consumes that same native layout.

Both official FP render models have 37 nodes:

| Model | Import checksum | XML SHA-256 |
|---|---:|---|
| `odst_recon/fp/fp` | 286525724 | `c7c6b3bca2a440843ed850b1fb1e5fd4548e5720adc03a54733b9607598c9e53` |
| `odst_oni_op/fp/fp` | 403178001 | `6126a5e692b8c309c8a2071cc50f5af8b4ca153b2835c534b313674960312fe6` |

Their independently exported parent/default-TR graphs are identical. Left
wrist 5 owns index `7->17->27`, middle `8->18->28`, pinky `9->19->29`, ring
`10->20->30`, thumb `11->21->31`. Right wrist 6 owns `12->22->32` through
`16->26->36`. No appended weapon or camera-control record is in these masks.
Own anatomical palm markers provide the bend direction; bone indices are not
copied from H3/H4 or inferred from a generic humanoid.

`tools/re/generate_odst_finger_fixture.py` verifies both complete XML hashes,
compares their graphs, and regenerates `tests/odst_finger_bind_fixture.h` from
the valid default translation/quaternion fields. The official XML inverse
labels are shifted; the fixture does not use those labels as native layout
evidence. Native inverse layout comes from the independent ODST consumer proof.

## Behavior and failure isolation

Free fingers reset from their own bind transforms relative to the already
committed wrist before applying curl. Repeated render calls cannot accumulate
the gesture. Trigger controls the index; grip controls the other fingers,
allowing open hand, pointing and fist. These are controller-driven poses,
not optical finger tracking or contact-reactive finger collision.

The real palette's `handAlignment` state chooses the displayed anatomical
support hand. Left-handed input alone does not swap mesh anatomy. Raw physical
controller values are taken from the frozen tracking snapshot before VR action
remapping, so an Unbound action does not disable finger posing.

Admission requires the current ODST title/generation and player-zero primary
FP context; current tracking serial/reference epoch; a known model checksum
and node count; the live full-salt local owner and native FP slot's current
weapon. Cinematics, vehicle ownership, secondary weapons, persistent support
attachment and active two-hand aim retain native finger animation. The holding
hand, wrists, arms, gun and camera records remain unchanged.

The complete candidate is validated before any write. An access fault rolls
back changed finger bytes immediately, including a write interrupted midway
through a record, and disables only this optional feature for that title
generation. Turning the option off clears the fault latch for a deliberate
retry. Existing ODST callback retirement protects the native dependencies.
Hot callbacks perform no logging, allocation or I/O; the cold worker reports
application/refusal/fault counters.

The focused fixture uses the official bind geometry and the actual production
palette transaction. It covers both own profiles and both anatomical sides,
open/point/fist, grip preservation, repeated-call stability, unknown models,
invalid late bind/input rejection, untouched appended weapon bytes, and actual
read/protected-page write faults. Release verification passes **810 checks**.
Headset acceptance is pending.
