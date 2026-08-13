# Halo 4 parity diagnostic

This document describes the log-only C-H4-D1 diagnostic candidate. It does not
claim acceptance and does not advance `docs/CURRENT-STATE.md`'s accepted
C-H4-43 pointer.

## Purpose

The normal Halo 4 log already measures the proven VR paths: stereo camera and
projection readback, headset FOV cover, 6DOF, motion aim, runtime mode, CUI
capture/upload, Storm hand palettes, and held-model carry. C-H4-D1 adds the
missing complete census at the existing H4EK-proven gameplay-CUI dispatcher:

- every command type executed in the gameplay CUI stream, as a two-second
  histogram;
- every distinct type-`0x28` transform ID, with replay and normal-pass counts;
- each transform's payload size, stack depth, scale, and X/Y translation;
- the gameplay CUI call's window, render-buffer channel, mode, and flag;
- explicit bounded-table overflow and unreadable-header counts;
- a periodic parity-coverage line that distinguishes observed systems from
  systems the current proven hooks cannot observe.

The callback only performs bounded reads and atomic updates. It performs no
logging, allocation, file I/O, locking, COM work, signature scan, or GPU
readback. The existing worker writes all `H4DIAG` lines to `halo3xr.log`.
Nothing in the diagnostic selects, suppresses, redirects, or draws a command;
the C-H4-49 behavior is unchanged.

The transform table holds 32 exact identities. `H4DIAG CUI IDENTITY COVERAGE`
must report zero overflow before the census can be treated as complete. An
overflow is a diagnostic refusal, not permission to merge IDs.

## Headset capture run

Use the edition you normally play. Anti-cheat must remain disabled as for every
mod run. Do not edit `halomccvr.cfg`; the diagnostic is baked into this one
candidate and automatically activates only inside the proven Halo 4 camera
core.

Run one campaign mission for roughly 15 minutes:

1. Spend one minute on foot with the assault rifle. Look and lean in every
   direction, walk, turn, aim, fire, reload, throw a grenade, zoom, take damage,
   and allow the shield to recharge.
2. Repeat firing, reloading, zooming, and weapon switching with at least a
   pistol, a scoped weapon, a Covenant weapon, and a weapon with a visibly
   different reticle.
3. Pause and resume, reach a checkpoint, die, and restart from the checkpoint.
4. Watch one in-engine campaign cinematic without skipping it.
5. Enter a ground vehicle as driver, passenger, and gunner where the mission
   permits. Drive, turn the turret, fire, take damage, and exit. If available,
   also fly one aircraft.
6. Return on foot, use the assault rifle again for ten seconds, then quit to the
   MCC menu normally.

Report the MCC edition, OpenXR runtime, headset, refresh rate, and what was
visibly wrong. Preserve both `halo3xr.log` and `halo3xr.log.prev` from that
edition's `Halo_MCC_VR` directory.

## Interpretation boundary

This run can settle the remaining CUI/reticle identity problem and validate all
currently implemented Halo 4 VR systems in one session. It cannot manufacture
semantic bindings absent from the current hooks. Vehicle seat/camera/projectile
ownership, cutscene/theater state, and the native HUD-layout consumer remain
explicitly reported as `NOT OBSERVABLE FROM CURRENT PROVEN HOOKS`; those need
their own H4EK-first bindings before any player-visible implementation. Static
or visual coincidence in this diagnostic is not authorization to copy a Halo 3,
ODST, or Reach offset.

