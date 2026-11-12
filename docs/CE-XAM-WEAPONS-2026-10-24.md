# Halo CE XAM: graph admission correction and Uzi orientation probe

User prioritizes CE before testing the ODST left-hand fix. Preserve that pending
candidate and accepted Mythic/New Covenant primary/dual-reload behavior.
Supplied CE log source e3b5924f, Steam/OpenXR 2.18.4, Original renderer. FP counters
show long intervals of stock fallback with zero exceptions. The Uzi photo shows
a near-vertical gun. It does not identify the exact failing render stage.

Downloaded supplied a30.map from Drive (the attached file exceeds tool 32 MiB
limit). CE v13 cache, 72,246,332 bytes. SHA-256 and actual graph names/parents:
CE-XAM-MAP-EVIDENCE.json. Reproduce with tools/re/inspect_ce_xam_map.py. Header
meta index at +0x10, tag table at meta+40, 32-byte records and virtual base
0x50000000; all resolved pointers stay in this metadata. These are offline cache
facts, not new native bindings. Existing HCEEK-proven antr node fields are used.

## Confirmed graph defects

RPG and Stinger graphs have a complete support-arm subtree whose upper arm is
parent -1. The old binder demanded every nonzero node descend from node zero,
rejecting both. Now only the exact named left-upperarm root may head a separate
tree, with the existing count, link, chain, mask and cycle guards retained.
HL1 MP5's direct right-wrist weapon root is named mp5_master, absent from the
four stock spellings. Add this evidenced spelling under the same unique-root
and direct-wrist-parent constraints. Unrecognized/disconnected guns remain
rejected. No changes to native camera, aim, model writes or other titles.

## Uzi remains unresolved

Uzi graph has accepted stock-style names and parentage (42 nodes; gun 7,
wrists 5/6). Its referenced FP model has frame gun as root, identity rest rotation;
primary-trigger marker also uses identity local rotation. The cached vertex
buffer contains 3,188 vertices, 2,983 assigned to root and 205 to magazine;
positions span X [-0.05456,0.19823], Y [-0.01622,0.01801], Z [-0.07361,0.05860].
This is not evidence for a fixed 90-degree mesh correction. Do not invent one.

Add a bounded snapshot of actual graph identity, node roles, pre/post gun forward
basis, post up basis and mount-adjusted controller forward/up. Cold 2-second poll
logs it; hot callbacks do not format/log/allocate. This separates a VR palette
orientation error from a later native consumption problem. No Uzi pose fix is
claimed in this candidate. Remain on Uzi for several seconds in the next test.

## Validation and delivery

CE first-person suite passes with ASan/UBSan, including four real graph fixtures,
detached-root/missing-gun/cycle rejection, authored grip, independent wrists,
reload parts, recenter and generation guards. Linux requires an external
using-std::isfinite shim for existing MSVC-oriented ik.cpp and exemption of an
existing misleading-indentation warning from -Werror. No production portability
cleanup. Windows DLL build/headset testing remain pending.

User compiles source via pinned workflow. Test both launchers and MP5 for tracked
weapon/hands, then Uzi and a normal gun for several seconds each. Send the new log
and visual result. No installation, launch, release, PR or accepted-pointer update.
