# New Covenant Elite: scoped ODST body mapping

User explicitly accepts bdad3ab1 for all tested Mythic guns, including placement
and deformation. Preserve that human path and dual shake. New Covenant remains
broken: pistol/sword/carbine head-locked; rifle/needler stretched and split across
hands. This is scoped Mythic acceptance, not an accepted-release pointer update.

New Covenant log source: bdad3ab16c37fde0d65cc005bc7a068592a05c9b.
Log SHA-256: f94184fa4d3c9c695468cfe4427d0e44035975c3988f2234a5934e2931c22164.
The native probe identifies body tag 10345, checksum 10000E0B, 31 nodes mapping
[0..30]. Observed gun outputs start at 31 and end at count-2; camera is last.
Combined counts include 34, 35, 37, 39, 47. Thus the old minimum of 39 blocks
the short graphs, and the human five-digit masks consume gun sources 31..36
in the longer graphs. The photos show exactly the split/stretch consequence.
The supplied Carbine photo URL duplicates the Needler URL; no Carbine photo
claim. Re-ran the read-only map inspector on retained New Covenant user.map;
output exactly matches ODST-MODDED-WEAPONS-MAP-EVIDENCE.json.

## Candidate boundary

A separate Elite resolver requires the exact 31-node checksum AND complete
identity body remap. It uses the evidenced four-digit body layout, carries gun
nodes 31..count-2 with the weapon hand and excludes the camera. Mythic's human
resolver is unchanged. Both may reuse the existing identity/generation/epoch
receipt; its already-mapped check now compares both hand masks and actual camera,
not a hardcoded Mythic camera index. No changes to shared IK or native bindings.

Short 33..38-node interpolations save an untouched graph with wrist=-1. They
remain in native pose, without barrel calibration or marker transformations,
until the actual body palette positively identifies the Elite. Known receipts
then apply before the next interpolation's calibration, exactly as in the
accepted Mythic lifecycle. No count alone selects an Elite; unknown checksums,
partial body maps and ordinary human bodies do not enter this resolver.

Validation: 25,452 checks across 202 map-derived combinations pass GCC C++20,
warnings-as-errors and ASan/UBSan. Includes pending short graphs, four-digit
ownership, disjoint hand masks, full gun carry, camera exclusion, partial-map
rejection, human identity isolation and existing Mythic lifecycle regressions.
Dual shake regressions remain 1,350 + 125 passing. Production helper compiled
with stubbed engine boundaries; Windows DLL/headset results pending.

Test New Covenant pistol, sword, carbine, rifle and needler, swap repeatedly,
then regress Mythic and an original ODST weapon. Send the new log and any
remaining visual problem. No all-Elite/all-mod claim, installation, launch,
release, PR or accepted-release pointer update.
