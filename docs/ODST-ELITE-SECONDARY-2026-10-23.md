# New Covenant secondary Elite palette

User confirms e3b5924f fixes the tested New Covenant guns, except dual-wield
left-hand plasma pistol stays head-locked. Preserve primary/New Covenant and
Mythic acceptance. Other secondary guns were not headset-tested by the user.
Log source e3b5924fd776bf30fba8fd66e4b29906d86ae10c; SHA-256:
8b82d319ced993c17b2578525f9353f1400e9576cb04ea90b76938821d9d4019.

At 18:18:50 the native probe records slot 1, count 35, Elite body checksum
10000E0B. Only outputs 1,3,5,7..10,15..18,23..26 map to their corresponding
sources; every other body output is -1. Gun tag 3509 maps to 31,32,33. The
existing full-map check rejects this partial body, leaving its short graph in
the observation-only/native-pose path. This is distinct from the fixed primary.

Add a secondary-only resolver for that exact checksum and partial mask. The
full underlying graph topology remains unchanged; missing render outputs stay
missing. Reuse the evidenced Elite source roles and existing dual reconstruction
which carries the gun with the solved LEFT wrist. Primary slot cannot use this
partial-map exception. Human/Mythic and full Elite admission are unchanged.
No shared IK edits, new native bindings, menu changes or arbitrary -1 acceptance.

25,596 checks across 202 map combinations pass GCC C++20 warnings-as-errors,
ASan/UBSan. Production-helper tests now cover secondary partial admission,
left-finger ownership, all held sources, second-interpolation receipt reuse,
primary-slot rejection and missing-left-wrist rejection. Engine boundaries are
stubbed; Windows compilation and headset acceptance remain pending.

Test left plasma pistol, then another available dual gun; move both controls,
reload, drop/reacquire the secondary, and regress the right-hand gun. Preserve
Mythic and normal ODST behavior. No install, release, PR or release-pointer update.
