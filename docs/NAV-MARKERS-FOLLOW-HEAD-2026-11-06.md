# Objective markers follow the head, not the gun: Reach and Halo 4 (2026-11-06)

Report (headset, both titles): "the objective markers still only show instead of offscreen arrows if I point my
weapon at the objective."

Status: built, unit-tested, **not run in a game or a headset**. Branch `fix/nav-markers-follow-head`, cut from
`controls-diagram` at `645ffa6e`. Commits `273d9056` (Reach) and `f4c5bde2` (Halo 4). Switch:
`nav_markers_follow_head` (F1 > HUD > Waypoints, "Markers follow your head, not your gun", default on, live).

## Why earlier work did not fix it

The Reach marker work (`reach_nav_markers.inl`, `reach_nav_projection.inl`) corrected where a marker is DRAWN:
inside each eye's render the marker projection gets the eye's pose and field of view, and an off-screen arrow is
brought onto a ring about the head. It was measured with a King of the Hill hill marker in multiplayer. It did
not touch where the game DECIDES whether an objective is on or off screen, and that decision is made elsewhere,
from the aim. Halo 4 had no marker work at all.

## Reach (read statically: retail `haloreach.dll` SHA-256 738DD2D2...0894, names from HREK `reach_tag_test.exe`)

- Campaign objectives are HUD "scripted objects" (`chud_track_object_with_priority`, `chud_track_flag...`). A
  widget picks its on-screen or off-screen art from the per-user state bits, HREK's `<scripted object> offscreen`
  among them (also `tracked target is offscreen` and the fireteam `... offscreen` states).
- Those bits are filled by the per-user HUD update `+0x2BF11C`, which the game's update reaches through
  `+0x2C073C` (not `player_view_render +0x26C6DC`, where the HUD is drawn). The state builders `+0x2B8288` and
  `+0x2BADA4` answer each with the marker projection `+0x2E1430` and read the record's off-screen value back:
  `+0x2DB0D8` (a world point; ring when the HUD setting asks), `+0x2DB034` (a cutscene flag, no ring: behind the
  camera), `+0x2DB158` (an object, through the object markers' wrapper `+0x2DAE98`).
- The projection builds its camera from the observer camera at the observer-camera hook's CHUD site
  (`+0x2E1525`). The hook gave it the eye's pose only inside an eye's render, so in the update the tests used the
  game's own camera, which this mod steers down the controller ray. Hence: marker only while the gun points near
  the objective, and an off-screen state (arrow art) whenever the gun points away, wherever the head looks.

Change: outside an eye's render, the CHUD site's camera gets the head of the last committed frame (position,
forward, up, published by the render thread with a clock and generation; the eye's vertical field of view under
`nav_markers_on_target`). With `nav_marker_arrow_in_view` on, the object markers' call and the world-point test's
call (new signature, one match at `+0x2DB10F`, proved to call the projection from another function) also get the
head's ring, so a test says "off screen" exactly where the draw puts the arrow.

Not changed: the navpoint list's choice of which navpoint shows its distance (`+0x2CB01C`, a dot product with the
observer camera's forward; it reads the observer directly).

## Halo 4 (read statically: retail `halo4.dll` SHA-256 7C53E7D5...FA84 only; H4EK is not on this machine)

Evidence class: **retail-derived lead, not H4EK-verified**, as for the Halo 4 tracker. The hook therefore proves
at run time, before it changes anything, that the camera it is handed is the aim camera.

- The HUD is laid out in the interface pass and replayed into each eye's picture. The per-user layout
  `+0x4CF2C4` (from a HUD component's table method `+0x4224F0`) builds ONE camera from the user's observer
  (`+0x40B0B8`: engine TLS `+0x680`, `0x428` per user, `+0x15C`) and hands it to nine widget groups; another
  family (`+0x3F25BC`) uses the player-view stack element `+0x10DAFE0`. Both place every world-anchored widget
  through `+0x40B5F4`, which projects the point (`+0x40B180`) and, for a clamped widget, pulls it to the bounds
  and returns the arrow's angle.
- The stereo transaction writes each eye's camera into the observer only around that eye's render and restores
  the game's own afterwards (`Halo4RestoreMonoCamera`, which also rebuilds the stack element). So the layout ran
  with the aim camera: markers on screen only while the gun pointed near them, placed where the gun would see them.

Change: a detour on `+0x40B5F4`. Outside the eye passes, once proven, the call gets a copy of its camera with the
head's position, forward and up (and the eyes' last vertical field of view under `nav_markers_on_target`) and a
projection the game's own builder (`+0x38F658`, the camera build's last call) makes from the copy. Proof: the
camera's heading agrees with the published aim (the game's own camera before head look) on 30 calls across 10
degrees; 120 disagreements in a row withdraw it. `tools/audit_caller_area.py` (from git history): `40B5F4:16:i`
and `38F658:4:v` clean.

## Log lines

- Reach, at level load: `NAV MARKERS Reach: Installed: ... Outside an eye's render (the HUD's on/off-screen tests)
  it is given the head's pose instead of the aim's; follow-head switch=1`, and `... the HUD update's world-point
  off-screen test gets the head's ring too: its call of the projection returns to haloreach.dll+0x2DB139`.
- Reach, once in play: `NAV MARKERS Reach: the HUD's on/off-screen tests (objective markers and their arrows) now
  look from the head, not the gun`.
- Halo 4, at level load: `NAV MARKERS Halo 4: Installed (RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED): ...`.
- Halo 4, after a few seconds of moving the gun about: `NAV MARKERS Halo 4: the HUD's marker camera is proven to
  be the aim camera ...`, then `... objective markers and their arrows are placed from the head, not the gun`.
  If the proof line never appears, nothing was changed in Halo 4; that is the first thing to report.
- Dev builds add a two-second window for each with every verdict counted by reason.

## Headset check (both games, a campaign level with an objective marker)

1. Look straight at the objective with the gun pointing well away from it (down, or to the side). The marker
   should show.
2. Point the gun at the objective and turn your head away from it. The marker should go and its off-screen arrow
   should show, on the objective's side.
3. Turn your head slowly toward the objective: the arrow should turn back into the marker as it comes into view.
4. F1 > HUD > Waypoints, untick "Markers follow your head, not your gun": the old behaviour (marker only where the
   gun points) should come back. Tick it again.
5. Halo 4: say whether the marker also sits on the objective when you look slightly beside it, or drifts.
   Untick "Markers use the headset's field of view" to compare.

## Not shown / open

- Nothing here has run in a game. The thread the Reach update runs on, and Halo 4's interface pass, are read
  statically; the counters in the dev report name what actually reaches each verdict.
- Halo 4's placement with the eyes' field of view assumes the replayed HUD fills each eye's picture as the eye's
  own camera does (the same window and aspect); not measured.
- Reach's navpoint distance pick (`+0x2CB01C`) still follows the gun.
- Halo 4's second widget family (`+0x3F25BC`) reads the stack element; it goes through the same detour, but which
  widgets it carries was not established.
