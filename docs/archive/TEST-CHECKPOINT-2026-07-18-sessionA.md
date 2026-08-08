# Session A test checkpoint (2026-07-18)

Build deployed to `halo3xr\halo3xr.dll` (1,323,520 bytes). One decisive headset session.
This build ships **behavior fixes** (bullet origin, reticle color) + **diagnostics** for the
next sessions (per-eye IK divergence, perf counters, HUD/enemy-red probes). Nothing here
touches the RenderViewHook eye loop, the ghost machinery, or the sway shim.

## What changed

**Behavior (on by default):**
- **Bullet origin fix** (`bullet_snap=1`). The dead `ApplyControllerToComposedWristSubtree`
  is now actually called from the compose hooks, snapping the projectile/effect-origin bones
  onto the same controller wrist target as the visible gun + muzzle flash. Bullets should
  leave the barrel instead of a point ahead/left of it.
- **Reticle color** now configurable (`reticle_r/g/b`, default = Halo-3 light blue
  0.62/0.87/1.0, lighter + less saturated than the old hardcoded cyan). Repaints live when
  changed in the F1 menu (new color picker + "Halo blue" reset button). Outline is a darkened
  version of the same hue.
- **Enemy-red plumbing** is in (`VR_SetReticleEnemy`): the reticle repaints red when an enemy
  is targeted — but the game-side target-lock byte isn't known yet, so it never fires until
  the HUD probe below finds it (Session C2 wires it).

**Perf (safe, on by default):**
- FP driver camera stamp now skipped when the eye camera is already in place (memcmp guard) —
  avoids ~2 redundant constant uploads per eye. Logs `PERF: FP driver camera stamps=.. skips=..`.
- `PERF: FP palette full solves N/sec` counter — the before/after metric for Session B's
  once-per-frame IK cache. Expect ~2× fps today.
- `PERF: eye blit uses FAST/SLOW path` one-time line — confirms the cheap CopyResource path.
- vr.cpp per-frame layer vectors reused instead of reallocated.

**Diagnostics (opt-in, `hud_probe=1`):**
- `IK-PROBE dRoot=.. dCenterRoot=.. dDesiredWrist=.. dElbow=.. dLens=..` (always on when arm IK
  runs) — names which per-eye input diverges behind the left-arm split. dRoot large is expected
  (eye offset); any other nonzero value is the culprit for Session B.
- `HUD-PROBE` — logs CHUD bytes that change; aim at an enemy to find the red-reticle state byte,
  toggle HUD elements to find visibility flags.
- `HUD-VARS` — dumps HUD/safe-area/crosshair debug-var names at startup for the edge-crop fix.

## User headset test (one Halo 3 level)

1. **Bullets:** stand ~2 m from a wall, fire. Do tracers now leave the **gun muzzle** (not a
   point ahead/left)? Muzzle flash should look unchanged. Then fire while swinging the gun and
   while turning your head — report if the origin drifts *only during motion* (that decides
   whether we arm the sway shim in Session B).
3. **Reticle color:** does it now read as a light Halo blue? Tweak it in F1 → "Aim crosshair"
   → color picker; "Halo blue" button resets. Report the RGB you like.
4. **FPS:** read the number in the F1 title bar; note it (baseline for Session B).
5. **Left arm split (no fix yet, just confirm still present):** close one eye then the other —
   does the bare left arm still jump between eyes? (Session B fixes this; the log's IK-PROBE
   lines tell us why.)

### For the HUD investigation (optional, same or separate session)
Set `hud_probe = 1` in `halo3xr\halo3xr.cfg`, launch, load a level, then:
- Aim your crosshair **at an enemy**, then **away**, a few times. (finds the red byte)
- Open F1, toggle "Show HUD" off/on. (finds the master flag; element flags need the engine's
  own toggles which we'll drive next)
- Quit. Send `halo3xr\halo3xr.log`. Set `hud_probe = 0` again afterward.

## Send back
- `halo3xr\halo3xr.log` (has IK-PROBE, PERF, and any HUD-* lines).
- Your answers to 1–5 above and the reticle RGB you settled on.

## Not in this build (next sessions, by design)
- Left-arm-split fix + IK once-per-frame (Session B — needs this build's IK-PROBE readout).
- Auto gun orientation w/ zero sliders (Session C).
- HUD edge-crop shrink + element-only HUD + enemy-red wire-up (Session C2 — needs HUD-VARS /
  HUD-PROBE readout).
- Auto-VR on level load, melee-by-swing, vehicle stick fix (Session D).
- ODST + other titles (Phase E, after Halo 3 is perfect).
