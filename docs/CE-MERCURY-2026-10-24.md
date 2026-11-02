# Mercury Rising sibling gun assembly

The user accepted both XAM launchers after eceed9ed, and explicitly deferred
MP5/MP7 orientation. Preserve that behavior and the accepted ODST secondary.

New log file_0000000097ac820eb5c469d64c350117 loads eceed9ed, Steam. Graphs
EA2B08B7 (41 nodes) and EE3C0CC8 (39 nodes) reject binding at stage11. Unlike
the XAM hidden-left case, these never reach palette matrix checks. The log
samples only these two graph datums; the excavator diagnosis additionally uses
the supplied map and the user's report, not a claimed third live sample.

Supplied map SHA256: 36548be9980796275f24e3a1ba7032bec6736dd30128e8878eb04ef3228c8eba.
The extracted graph fixtures in CE-MERCURY-GRAPHS.json record tag indices
2231 (elefant/innie/weapons/hose/hose), 3272 (sniper) and 631 (excavator).
All have frame gun parent0, beside the named left/right upper-arm chains.
Existing binding requires a named gun directly beneath the right wrist.
Thus these complete connected graphs are rejected despite valid hand topology.

Candidate accepts a unique frame gun directly beneath graph root0. It requires
no asset hash, weapon name, tag index or new native offset. Existing wrist-child
roots remain supported. Multiple gun roots, left-wrist ownership, overlapping
hand/gun/arm subtrees and the existing malformed graph cases still reject.
Gun ownership remains separate from the right hand mask. The production tracked
palette already carries gunMask via the authored right-wrist grip transform;
the standalone relocation helper now also carries detached gunMask explicitly.

Tests use all three actual cache graphs and cover independent wrists, gun axis
and seating, moving components, floating arms and support grip. Invalid left-hand
parent and duplicate root cases reject. Existing XAM and CE tests pass GCC
ASan/UBSan using the established Linux math shim; the existing misleading-
indentation warning remains exempt from Werror. Windows build and headset
acceptance pending. This is a topology-based extension, not a universal solution
to every mod's model axes, node naming or malformed animation data.

No ODST, Reach, Halo 3, dual shake, native binding or muzzle-axis changes.
No install, release pointer advance or PR. Next test SMG/excavator/sniper,
reload, support grip and one previously working gun with the new DLL/log.
