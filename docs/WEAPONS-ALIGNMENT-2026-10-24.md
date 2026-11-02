# Weapons alignment candidate

User scope: branch weapons-alignment; all prior accepted modded-weapon and dual
shake fixes retained. Three changes authorized together; user explicitly
clarified visual alignment must not steer barrel aim in any supported game.

## H2 Classic and visual/aim separation

The visible packet builder applied visual trim, then the muzzle publisher read
that final model palette. Consequently the visible-barrel option also followed
the UI's nominally visual sliders. The new publisher re-expresses the authored
marker in the matching neutral presentation frame before storing its aiming
receipt. Native effects continue to use the drawn model. This changes the
option's semantics deliberately as requested: barrel aiming uses the authored
barrel without the user's visual rotations.

H2 records pre/post trim carriers in the packet context and preserves collision
translation. Classic alone adds -11.5 degrees pitch, as specified by the user;
Anniversary does not. The two Classic menu sliders are removed. Legacy parsed
Classic keys remain for configuration compatibility but are not applied by the
active packet builder. Global visual pitch/yaw/roll remain available and zero
means zero additional visual trim. Existing saved global trims are not reset.

CE reconstructs both mount frames from its frozen render context. H3/ODST obtain
the automatic barrel-aligned wrist frame and its pre-visual counterpart; only
primary visual trim is removed because secondary placement does not apply those
sliders. Reach retains both pre-IK controller frames in its prepared targets,
including the one-hand fallback. H4 freezes the paired normal and one-hand
carriers with and without visual trim. All preserve their existing marker
catalog, title/weapon/slot/generation/epoch checks and bounded publication.
No invented native offsets or copied cross-title tag layouts. Shared math only.

## H3 secondary SMG

The existing H3 model catalog identifies fp_smg by checksum171C0B10 and four
render nodes. Rotate that final slot1 weapon palette +30 degrees of the existing
visual-yaw convention around the left grip. This correction uses the user's
headset observation; it is not claimed to be a measured engine defect. It does
not rotate the hand palette, primary weapon, silenced SMG or other title. The
exception is limited to right-handed player mode, where slot1 is the physical
left weapon. Native marker aim follows this corrected model; global visual
sliders are independently neutralized as above.

## Bilateral offsets

All active CE/H2/H3/ODST/Reach/H4 controller paths use shared gun forward/right/up
on both grips. Left lateral sign is inverted; existing left_hand_forward_m is
an additional left-only forward trim. Gun-forward default thus also moves the
left grip, intentionally changing its old placement. The offsets keep existing
bounds and finite guards. Models with planted support grip still obey their
established weapon attachment policy rather than detaching the support hand.

## Validation and limits

Portable ASan/UBSan runs: new weapon_alignment_tests; weapon_muzzle_tests (615
checks including actual H2 publisher with rotated mesh/stable muzzle); complete
CE first-person suite, including opposite lateral offsets. Math tests use
combined pitch/yaw/roll and collision translations, and reject invalid frames.
Existing Linux-only isfinite shim/warning exemptions remain needed; no unrelated
production portability changes. Windows publisher fixtures updated for new
context fields and helper call; full Release build not executable on this Linux
workspace. Actions API is unavailable (Forbidden); source workflow is pinned
for the user's normal Windows compilation. No successful Windows build or
headset result is claimed.

Test in H2 Classic with Reset visual rotation: baseline -11.5 is built in. With
visible-barrel aim enabled, change visual pitch/yaw/roll and confirm crosshair
and impacts stay put while model turns; repeat Anniversary and other titles.
Test H3 dual SMG, another dual weapon and primary SMG. Test forward/right/up on
both free hands, with positive right moving outward on opposite sides. Test
reload, support grip and switching weapons; include H3 regression for shared
math changes. Left-handed/anatomical routing, collisions, scoped weapons and
long sessions require additional headset coverage; they are not accepted by
portable math tests. No installation, game launch, release or PR.
