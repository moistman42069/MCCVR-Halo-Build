# XAM imported-gun axes and unresolved launcher fallback

User tested 2935040d: MP5 now tracks but points sideways/up; Uzi/MP7 points up;
both launchers remain head-relative. Last weapon is a normal M4-like carbine.
Log SHA-256: 9ead37ba2ee34b8932993a04a31732a4af1905b10f7f83225c432a0bee2c775d.

Actual live graph datums identify M4A1 0xEBF00A7C (7560F22650441950), HL1 MP5
0xED620BEE (0698859159826A29), HL2 SMG 0xEDB50C41 (D822330265B09099).
The prior investigation of the asset named weapons/uzi was the wrong model for
the user's UI-named Uzi. Correct that attribution: the photographed/imported
MP7 uses HL2 SMG graph, referenced render model weapons/hl2 smg 2/fp/fp.

For all successful samples raw gun-root forward/up equal mount-adjusted aim
forward/up. Thus the root is tracking correctly while the visible barrel does
not share its axes. The map primary-trigger markers independently establish
nonidentity gun-local orientation for HL1 MP5 and HL2 SMG, versus identity for
M4A1. Exact reference pairs, graph hashes, root names, positions and quaternions
are in CE-XAM-AIM-EVIDENCE.json, reproducible with inspect_ce_xam_aim.py.
No fixed rotation estimated from photographs or new native binding.

Use those two evidenced marker rotations as the aim frame when deriving wrist
carriers. Preserve gun-local animation, grip position and all part relationships.
Selection requires complete graph identity/count/gun-node tuple. All other graphs
retain their old path, including M4A1 and stock weapons. Graph identity cannot
distinguish a different mesh reusing identical animation names/parents; broader
custom-model reuse is not claimed validated. Full dynamic marker lookup remains
outside this candidate. No gameplay projectile or crosshair change.

## Launchers remain open

Graph admission alone was insufficient. The log's success counters freeze for
both launchers; prior orientation snapshots retained the last successfully
staged gun, so those lines cannot identify the failed launcher stage. Preserve
the proven MP5 binding improvement. Do not claim launcher tracking fixed.
New bounded snapshot reports the active failed graph, stage, invalid-node mask
and both arm segment lengths. Pure staging exposes an optional diagnostic stage
without changing its result; the cold poll alone formats/logs evidence. Unknown
invalid data remains stock. Next log distinguishes input/controller/grip/hand,
node validation, left/right arm IK, floating hands, graph read and binding.

CE suite passes ASan/UBSan using documented Linux math shim and existing warning
exemption. Actual HL2 graph joins the map fixtures. Pose tests use rotated native
roots/controllers and verify the rendered muzzle axes match aim while keeping
the hand grip position; unrelated identity retains its raw root. No Windows
DLL/headset result yet. Existing title fixes and ODST pending secondary retained.

Next test: MP5, Uzi/MP7, both launchers, M4A1, several seconds each. Return log and
visual result. Candidate corrects imported-gun orientation and diagnoses launcher
fallback; does not claim all XAM problems solved. No install/release/PR/pointer
advance.
