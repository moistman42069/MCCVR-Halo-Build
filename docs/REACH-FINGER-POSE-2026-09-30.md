# Reach first-person finger pose evidence and scope

The optional Reach controller gesture is limited to the visible first-person
Spartan/Elite hand palettes. It uses the render-model output-node indices from
the retained official HREK `fp.render_model` XML exports; it does not use world
avatar palettes or animation-source indices. Spartan is 47 nodes with runtime
import checksum `0x181E0B17`; Elite is 41 nodes with checksum `0x19021311`.
The source export hashes are recorded below so the generated parent and chain
tables can be checked against the same assets.

`out/reach-avatar-review/spartan-fp.render_model.xml` has SHA-256
`380FAC1B9136FBA38A41D4D82A90869344D38F887C1FD3BFC1F3BDFEFE03EE40`.
`out/reach-avatar-review/elite-fp.render_model.xml` has SHA-256
`EA0CEAA07C486395E25B707CFBCEE53437DB940DFD938B3E253A6A1870459EE1`.
The selected identities, parents, chains, and support-side policy are encoded
in `src/common/reach_finger_pose_logic.h`.

Only the free support hand may flex. The prepared controller sample must match
the exact display time, tracking epoch, handedness, and hand-alignment mode;
the native primary weapon owner and weapon must still match; and any secondary
weapon, two-hand aim, or admitted support grip leaves the authored hand alone.
Unknown models, stale samples, failed reads, and failed writes keep the stock
palette. Trigger flexes the index finger; grip flexes the remaining digits.
This is additive articulation of the palette produced by the engine, not
optical finger tracking. It does not synthesize an open bind pose; a zero input
leaves the native palette unchanged. The separately implemented H4 bind-pose
path should not be presented as Reach behavior.

The helper unit test checks both exact model identities, trigger/grip
independence, repeatability from the same native source palette, invalid-input
rejection, and the free-support admission gates. The native game integration
still requires the candidate DLL build and headset verification before this
behavior can be considered accepted.
