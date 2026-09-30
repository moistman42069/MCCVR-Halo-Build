# Runtime finger-joint identity foundation

The user narrowed the contact-physics request to identifying individual finger
joints in all six games. This change supplies a callable, rig-scoped inventory
and uses it in the existing finger-pose consumers. It does not perform collision
queries, apply surface reactions, or claim optical finger tracking.

## Shared contract

`src/common/finger_joint_identity.h` defines an allocation-free inventory for
one anatomical hand. Its key contains title, a full 64-bit rig identity, palette
node count and `FirstPerson`/`WorldBody` kind. Each record retains that key,
left/right anatomy, proven named digit (or `Unknown`), native digit slot,
root-to-tip joint ordinal, palette index and immediate parent index. H2 can also
retain `sourcePaletteIndex` when its source graph is mapped into the output
palette; the default sentinel means no separate mapping was supplied.

Joint labels are `Root`, `Child1`, `Child2`, `Child3`. They deliberately do not
equate every first node with a proximal phalanx: H4 has four-node pinky chains,
and Elite rigs have different segment counts. `AnatomicalJoint` remains
`Unknown` unless independently proven. Named digits and these stable native
joint ordinals are enough to address each actual joint without inventing bones.

`Build` validates every parent edge, range and duplicate palette index and
publishes only a complete inventory. `FindNative(slot, ordinal)` addresses
unknown-semantic native chains; `Find(digit, joint)` addresses proven named
digits and never resolves `Digit::Unknown` as a guessed name. The generic pose
consumer validates each record against its admitted chain and uses the labeled
palette index for articulation. H3 and ODST's existing specialized solvers use
the same records to select their exact native joint nodes.

## Admitted rigs and consumers

Counts below are **joint records per anatomical hand**, not total model nodes.
Existing local-player, full-handle, ownership, generation and free-hand guards
remain in each title's runtime adapter.

| Title / palette | Own rig identity | Model nodes | Records/hand | Callable provider / runtime consumer |
| --- | --- | ---: | ---: | --- |
| CE FP | Existing complete 64-bit named-node graph fingerprint | Admitted native FP graph | 15 (5 named digits × 3 joints) | `BuildFingerBindings` / `DescribeFirstPersonFingerJoints` in `haloce_first_person_logic.h`; `ApplyFreeHandFingerPose` consumes the inventory |
| H2 FP Chief | Complete source graph identity plus validated packet map | Source/output counts checked separately | 15 (5 unknown native slots × 3 joints) | `Halo2BuildFirstPersonArmBinding` / `Halo2DescribeFirstPersonFingerJoints`; `Halo2MapFingerInventoryToPacket` retains source and output indices |
| H2 FP Elite | Complete source graph identity plus validated packet map | Source/output counts checked separately | 12 (4 unknown native slots × 3 joints) | Same provider and packet adapter; digit names remain `Unknown` |
| H3 world Chief | `0x17121B0D` | 51 | 15 | `halo3_avatar::DescribeFingers` / `PoseFingers` |
| H3 world Elite / Dervish | `0x10140D04` / `0x17180910` | 55 | 8 | Same own-profile provider; four two-node native digits |
| ODST recon / ONI FP | `286525724` / `403178001` | 37 | 15 | `odst_fingers::Describe` / `Solve` |
| Reach Spartan FP | `0x181E0B17` | 47 | 15 | `reach_fingers::Describe` / `Apply` |
| Reach Elite FP | `0x19021311` | 41 | 12 | Same provider; absent middle digit produces no record |
| H4 Storm FP | `0x150D0000` | 80 | 16 | `Halo4DescribeFpFingers` / `Halo4PoseFreeFpFingers` |
| H4 Chief world | `0x17010100` | 120 | 16 | `halo4_body_ik_detail::DescribeFingers` / `PoseControllerFingers` |

H3 inventories currently describe its implemented full-world avatar rigs, not
an additional H3 FP finger-pose subsystem. ODST/Reach/CE/H2 FP inventory support
does not imply those titles gained a full-world avatar. Unsupported/custom rig
identities retain existing behavior rather than borrowing another game's graph.

Named H3 world digits were checked against retained H3EK Chief/Elite/Dervish
exports in `out/h3-odst-avatar-review/h3/*-world-summary.json`. ODST names and
hierarchy are independently pinned by `ODST-FINGER-GESTURES-2026-09-30.md` and
its generated bind fixture. Reach and H4 use their already verified own-title
chain tables and rig identities. H2's verified native chain slots remain useful
without a speculative thumb/index assignment.

## Coordinate and validation limits

The inventory identifies where to read a joint in the **matching admitted
palette**. It does not relabel those coordinates as world space or transport
joint positions between threads. A future contact system must take a coherent
palette snapshot, apply its title's proven frame conversion, include pose and
ownership generations, and return bounded contact limits. See the contact-finger
audit in `PHYSICAL-CONTACT-MELEE-WORK.md` for the remaining physics contracts.

`finger_joint_identity_tests` covers own-profile counts, both anatomical hands,
named/native lookup, missing digits, H4's fourth joint, full-width identity,
cross-title rejection and atomic duplicate rejection. Existing production pose
fixtures exercise inventory-backed joint selection; CE/H2 tests additionally
cover their native binding and source/output map. Build/test results are recorded
with the candidate; none replace headset validation.
