# Halo 4 bring-up — work record and suspension state

Written 2026-08-14, when work on `feature/halo4-bringup` stopped. **No release
was cut for any of this.** The published release on GitHub is still MCC VR
Alpha 0.3.3 (`94dc09f`), covering Halo 3, ODST and Reach, and none of the work
described here reached it.

This file exists so the branch is legible without replaying 155 commits. It is
a record, not instructions.

## Pointers as they stand

| Pointer | Value |
| --- | --- |
| Published release | MCC VR Alpha 0.3.3, `94dc09f` — Halo 3, ODST, Reach. Untouched by this branch. |
| Development baseline | `f4c641f` (2026-08-06) |
| Branch | `feature/halo4-bringup`, 155 commits ahead of `master`, clean tree |
| Accepted Halo 4 pointer | **C-H4-43**, `dd99465` — the last state the user headset-accepted |
| Head of branch | `47fa631` — the C-H4-D1 diagnostic, log-only, never captured |
| Installed on this machine | **C-H4-D1** (`7da8f7c`), in both Steam and Microsoft Store — *newer than the accepted pointer and never tested* |

The installed build is not the accepted build. Anyone picking this up should
either run the D1 capture described below or roll the install back to C-H4-43.

## Span of the work

2026-08-06 to 2026-08-13, eight days, 60 distinct candidates (C-H4-1 through
C-H4-49, plus the C-H4-11a/b and C-H4-43i..q sub-series). 23 of those commits
are reverts. Per-candidate evidence is in `docs/HALO4-SIGNATURE-EVIDENCE.md`
(E-H4-1 .. E-H4-34) and `docs/HALO4-CUI-EVIDENCE.md`; per-candidate headset
results are in `docs/CURRENT-STATE.md`.

## What Halo 4 does today, headset-confirmed

On the accepted C-H4-43 build:

- stereo rendering with distinct per-eye content, sustained, no drops;
- head tracking and 6DOF lean, with the headset owning pitch so the look stick
  no longer tilts the player's horizon (C-H4-9);
- native headset FOV — the engine builds the frustum from the observer's own
  tangent field, and the log confirms it contains the headset's (C-H4-8);
- controller input through the shared virtual-gamepad transport (C-H4-1);
- motion aim, smooth turn, haptics, head-relative locomotion (C-H4-10);
- floating first-person hands with the held weapon carried, no IK, using the
  official `left_hand` marker aligned to the cross-title controller frame
  (C-H4-23 .. C-H4-43);
- Halo 4's own HUD, which the engine already draws — unlike Reach, it needed no
  CHUD work to appear.

That is a playable VR bring-up. It is what the user meant by "finally at a good
state".

## What was never finished

**The VR crosshair.** This is where the last fourteen candidates went
(C-H4-43i through C-H4-49) and none of them passed a headset test. The goal was
the same authored-reticle path the other three titles use: capture the native
crosshair art, upload it through the shared chain, and draw it on the
weapon-ray quad so bullets land where the crosshair points.

The blocker is that Halo 4 has no widget-scoped draw hook. Its whole CUI stream
replays through one dispatcher, so the capture inherits whatever viewport
happened to be live — which the `SCENEPROBE` logs show ranging from the full
`4834x3486` raster down to a `1209x872` quarter slice, frame to frame. Every
attempt to fix framing either captured the wrong region (a corner or blob of
other HUD content, reported as a "random asset") or lost the rest of the HUD.
C-H4-49 replaced the live viewport with a fixed raster-centred region at the
4x ratio proven on Halo 3/ODST. It was built, installed, and never tested.

**The HUD layout** (C-H4-44) was built and rejected before headset testing;
the basis writer is dormant in the tree. Halo 4 HUD height, scale, aspect and
curvature are stock.

**Vehicles, cutscenes, and theatre** were never started for Halo 4. No seat,
camera, projectile, or cinematic ownership exists, and the current hooks cannot
observe any of it — the D1 diagnostic reports those as
`NOT OBSERVABLE FROM CURRENT PROVEN HOOKS` by design rather than guessing.

**Level re-entry** crashes on a NULL `first_person_weapons` block. Root-caused
in E-H4-11, not fixed.

## Dead ends — do not restart here

Each of these cost multiple candidates and is disproven, not merely untried:

- **`fp_orientations` / the `first_person_weapons` node block is not the render
  input.** The addresses, strides and layout in E-H4-15/16 are all correct and
  the write genuinely lands and survives readback — the engine republishes the
  block from its animation system every frame. It is telemetry written *out*,
  not data read *from*. Proof in E-H4-18.
- **The two first-person banks are in different spaces.** The body bank's fill
  root is NULL (model space); the weapon banks are camera-rooted (world). About
  fifteen candidates assumed one space for both. E-H4-22.
- **Arm IK was being solved on the gun.** Halo 4 does not call the shared
  solver the way ODST and Reach do. Gate on the engine's fill flag, never on
  geometry. E-H4-21c.
- **Assert strings are compiled out of retail Halo 4.** Never anchor on strings;
  anchor on the camera-anchor array at `0x30AD1C0`.
- **The RTV scene-target heuristic from Halo 3 does not transfer.** Take the
  last full-size colour target of an eye.
- **Reach's script-table chain does not apply to Halo 4.** H4 has its own
  registrar chain, proven in E-H4-1.

## If anyone resumes

The one concrete next step is already built and installed: run the C-H4-D1
capture protocol in `docs/HALO4-PARITY-DIAGNOSTIC.md` — roughly fifteen minutes
of one campaign mission — and read `H4DIAG CUI IDENTITY COVERAGE` from
`halo3xr.log`. It is log-only and changes no behaviour. If it reports zero
table overflow, it settles which type-`0x28` transform identity is the reticle,
which is the fact every one of the last fourteen candidates was guessing at.

If that is not wanted, roll the install back to C-H4-43 from
`out/deploy-backups` and leave Halo 4 where it was accepted.

## Cross-title items still open

Untouched by the Halo 4 work and carried forward from 0.3.3:

- micro stutter above 90 Hz (measured as not caused by the mod);
- Theatre shows no 3D on Quest 3;
- cross-title kick-to-menu on level load;
- Reach navpoint transform;
- the F1 menu restructure, where the right stick still turns the player with
  the menu open.

All three shipped titles remain working on the accepted line.
