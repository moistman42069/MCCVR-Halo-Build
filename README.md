# Halo MCC VR

_**OpenXR Toolkit may cause performance or compatibility issues. Disable or
remove it while troubleshooting.**_

An independently maintained continuation of
[Halo-MCC-VR by pancreations](https://github.com/pancreations/Halo-MCC-VR),
maintained here by **moistman42069**. Original contributor credit, history, and
the MIT license are preserved.

## Latest release: Alpha 0.5.0 — Experimental Refinement Update

Alpha 0.5.0 adds first-person vehicle cameras across the full campaign lineup,
circular gun-side zoom in H2/H3/ODST/Reach/H4, independent H2/H3 dual-wield
trajectories, all-title barrel-origin aiming, per-gun alignment, visual hand
offsets, reload and magazine refinements, vehicle comfort controls, roomscale
corrections, and several title-specific rendering and input fixes.

> **Every new Alpha 0.5.0 feature is experimental.** Some additions may not
> work as intended for every weapon, mission, vehicle, controller, headset or
> runtime. They will be refined in future updates. Most are off by default and
> fall back to the game's stock behavior when verification fails.

Every Halo: The Master Chief Collection campaign retains a playable VR path:

- Halo: Combat Evolved Anniversary — Original and Anniversary graphics
- Halo 2: Anniversary — Classic and Anniversary graphics
- Halo 3
- Halo 3: ODST
- Halo: Reach
- Halo 4

The same build supports Steam and Microsoft Store / Xbox app. This remains an
alpha: complete campaign coverage does not mean every feature,
mission, transition, headset, or runtime combination is finished.

Read the [complete Alpha 0.5.0 release notes](releases/0.5.0/RELEASE-NOTES.md)
for the detailed title-by-title breakdown, exact limits, planned work, and
artifact hashes.

## Read this before playing

- **Wait at least seven seconds after entering a title or level before using
  Force Inject / Recover VR.** Some titles need about seven seconds to load and
  establish their native camera/display proofs. Using recovery earlier can make
  normal initialization look like a failure.
- **Switching from one campaign to another can occasionally crash MCC or leave
  the next campaign flat.** Fully close MCC, restart it through the supplied
  launcher, and load the destination campaign again.
- **Do not switch Halo CE Original/Anniversary graphics during cinematics.**
  Switch during gameplay after the cinematic has ended.
- **CE compatibility:** avoid stacking graphics wrappers or render-interception
  mods on top of MCCVR; they can interfere with injection.
- Keep your existing `halomccvr.cfg` when updating.
- Launch only with anti-cheat disabled. Do not use the mod in matchmaking.

## Downloads

- **[Download Halo-MCC-VR.zip](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.5.0/Halo-MCC-VR.zip)** — the mod for players.
- [Halo-MCC-VR-Source.zip](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.5.0/Halo-MCC-VR-Source.zip) — exact matching source and build instructions for developers.
- [Release page](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.5.0) · [SHA-256 checksums](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.5.0/SHA256.txt)

The player ZIP contains only the DLL, launcher, default config, and `README.txt`,
all at the ZIP root. Existing users should **keep their own config**.
The published runtime is the exact locally verified **d088171** build, without recompiling.
The release tag and matching source ZIP point to that same runtime source.

## Quick controller reference

- **VR settings:** click both controller sticks together, or press F1.
- **D-pad:** hold the left hand beside the left side of your head and move the
  left stick.
- **CE/H2 graphics switch:** hold the left hand beside your head and click the
  left stick. In CE, wait until any cinematic has ended.
- **Recenter:** press F3 after entering gameplay.

## Required MCC settings

Set these separately in every campaign before judging scale, aim, or
performance:

| Setting | Required value |
| --- | --- |
| Video > Max Frame Rate | 120 |
| Video > V-Sync | Off |
| Campaign Field of View | **120° in each title** |
| MCC FSR | Off |

For ODST, also set Look Sensitivity to maximum and Look Acceleration off so
native aim can keep up with the controller reticle.

## Major features

### New in Alpha 0.5.0 — experimental refinements

- **First-person vehicles:** the existing toggle now covers CE Original/
  Anniversary, H2 Classic/Anniversary, H3, ODST, Reach and H4. New CE/H2/H4
  views use verified native seated cameras and head markers.
- **Circular zoom:** an optional 1024 × 1024 weapon-side lens in H2, H3, ODST,
  Reach and H4, with per-frame refresh and handed placement. CE retains native
  zoom while its separate-lens path remains unfinished.
- **Dual and barrel aiming:** independent dual-wield trajectories in H2/H3 and
  an optional verified muzzle-origin mode across all six games.
- **Alignment:** optional per-gun saved offsets plus independent visual-only
  left/right hand-position sliders.
- **Reload refinements:** a separate shortened-animation option, stable held
  magazines, model/pose-based insertion points, textured parts, Promethean
  fallback tokens, and haptics on both grab and insertion/release.
- **Input fixes:** menu-pointer A selection, support-grip-aware flashlight
  suppression with Quest labels, and corrected ODST controller-aim admission.
- **Comfort and vehicles:** improved roomscale stopping, audited vehicle input,
  and optional automatic smooth turning while seated.
- **Rendering:** an optional CE Anniversary flare suppressor and a Reach
  second-eye foliage-wind correction.

These additions are off by default where practical and remain experimental.
See the release notes for precise title coverage and unresolved limits.

### Retained from Alpha 0.4.1 (experimental)

These features are **off by default**. Enable them individually in F1 and keep
your existing configuration when upgrading. Both handedness modes are supported.

- **Manual Reload — Weapon & Aim:** hold support-hand grip at the support-side
  hip, bring the reload part just below the gun hand, then release. Releasing
  elsewhere cancels. Includes 42 reload-part meshes across all six games and
  adjustable grab/insertion radii. Unfamiliar valid modded weapon models can use
  an optional blue generic reload item.
- **Needler shake reload:** with Manual Reload and Shake to reload needle
  weapons enabled, make one rapid out-and-back shake of the gun hand in any
  direction. No grip press is required. Let the hand settle before repeating.
  Supports recognized Needlers and Reach's Needle Rifle.
- **Weapon Holsters — Weapon & Aim:** use the weapon-side shoulder or hip zone
  to switch carried weapons. Choose grip-and-draw, a grip click, or both;
  click takes precedence when both are enabled. Radius and draw distance are
  adjustable. Release grip before another switch.
- **Menu pointer — Controls > Point at game menus:** aim the primary controller
  at the displayed native menu and use its trigger to click or drag. A white
  cursor shows pointer position. MCC must have focus; F1 pointing remains
  available independently.
- **Comfort corrections:** refined CE Anniversary beam-flare handling and
  Halo 3 Cortana cinematic-facing transitions.
- **CE vehicles carried forward:** controller-directed steering/gunner aim
  and seated native crosshair capture in Original and Anniversary graphics.
  Head look remains independent; native throttle, physics and aim limits apply.

Match each game's **MCC Reload button** and **MCC Switch Weapon button** to its
controller layout. Reload/holster gestures apply to focused, tracked, on-foot,
single-weapon gameplay; ordinary buttons remain available. Halo still owns
ammo and inventory; automatic reloads and animation timing remain native unless
the new options are enabled. Holsters do not add
inventory slots or visible body-mounted guns.

Reload parts now use authored textures, but do not have world occlusion and may
show through nearby surfaces. With animation shortening and disabling off, the native gun retains its normal reload
animation and magazine. These options do not simulate removal from the original gun. Generic items do not reproduce custom magazine geometry
or identify custom ammo types. See the [release notes](releases/0.5.0/RELEASE-NOTES.md)
for full controls, cancellation behavior and remaining coverage limits.

### Per-game controller layouts and interaction settings

Open **F1 > Weapon & Aim > Reload and holsters** (or click both sticks together to open VR settings). Enable **Manual Reload** or **Weapon Holsters** to reveal the shared setup controls.

- **Controller layout for:** choose Halo CE, Halo 2, Halo 3, ODST, Reach or Halo 4. The selector follows the active title when it changes; you can also select a game yourself. Button choices are saved separately for each title. CE's two graphics modes share one set, as do Halo 2's.
- **MCC Reload button:** choose the native controller button your selected game's MCC layout uses for reload. Visible with Manual Reload enabled; defaults to **X**.
- **MCC Switch Weapon button:** choose the native controller button your selected game's MCC layout uses to switch weapons. Visible with Weapon Holsters enabled; defaults to **Y**.
- Both button selectors offer **X, Right bumper, Left bumper, B, Y, A, Left trigger and Right trigger**. These settings tell the VR gestures which native button to send; they do not change MCC's controller preset. Match your actual in-game layout (Recon, Reclaimer, or whichever preset you use). If a gesture performs the wrong action or does nothing, check these mappings in the selected campaign.
- **Pouch / hip depth below head:** adjust the shared vertical placement from **25–85 cm** for seated or standing reach.

The smaller interaction adjustments are available alongside these mappings:

| Feature | Available controls |
| --- | --- |
| Manual reload | Independent enable toggle; **Magazine grab radius** 8–40 cm; **Magazine insertion radius** 6–30 cm |
| Reload behavior | Independent **Disable automatic reload**, **Shortened reload animation**, and full **Disable reload animation** options; all default off and require Manual Reload |
| Unfamiliar weapons | **Generic reload item for unknown weapons** toggle; uses a blue interaction item for valid unfamiliar models, without claiming a custom magazine mesh or ammo-type detection |
| Needle weapons | **Shake to reload needle weapons** toggle and **Minimum shake stroke** 6–20 cm; one rapid gun-hand out-and-back, no grip required, then let the hand settle |
| Holster placement | **Weapon-side shoulder** or **Weapon-side hip**, plus **Holster grab radius** 8–40 cm |
| Holster activation | Separate **Holster slide / draw gesture** and **Holster click gesture** toggles; click takes precedence if both are on; enable at least one |
| Slide-only holster distance | **Minimum holster draw distance** 10–50 cm; shown when slide is on and click is off |
| Menu interaction | **F1 > Controls > Point at game menus** toggle; primary-controller aim, trigger click/drag, visible white cursor, and handedness support |

Reload/holster gestures mirror for left-handed play and give a short vibration on grabs and completed gestures. They apply to focused, tracked, on-foot, single-weapon gameplay and cancel during menus, vehicles, death or tracking loss. Regular controller buttons remain available; Halo's native ammo and inventory rules still apply. Release grip before repeating a holster switch. These controls supplement the retained D-pad head-radius slider, optional Quest 3 thumb-rest/right-stick D-pad, handedness/hand-alignment options and precision arrows beside F1 sliders described below.

### Earlier additions retained

- **Halo CE Anniversary is now playable in VR**, alongside Original graphics:
  stereo/6DOF, tracked hands and guns, native HUD and reticles, full-resolution
  Anniversary eye targets, muzzle effects, gameplay graphics switching, world
  contact, physical melee, and restored vibration on both controllers.
- **Fixed opt-in left-hand alignment across all six titles**, including both
  CE and Halo 2 graphics modes. Enable Left-handed main weapon, then Fix Hand
  Alignment (Experimental). It uses title-specific authored grips, preserves
  gun placement, and remains off by default. Native finger animations and
  unusual/custom weapon grips can still need refinement.
- **Adjustable D-pad head radius:** 10–50 cm, with the existing 30 cm default.
- **Optional Quest 3 thumb-rest D-pad:** hold the physical left thumb rest and
  move the physical right stick for D-pad directions. Release to restore normal
  stick use. This stays independent of weapon handedness and is off by default.
  Existing head gestures and graphics-switch clicks remain available; sensor
  delivery depends on your runtime/controller connection.
- **All-title automatic re-entry and Force Inject / Recover VR improvements**,
  plus Reach's native HUD-height control and retained CE haptics.

### Core features

- Per-eye stereo rendering and 6DOF head tracking across all six campaigns.
- Motion-controller aiming, tracked hands and weapons, melee, grenades, and
  haptics, with optional left-handed primary-weapon routing.
- Native or title-appropriate HUD and reticle handling, title-specific HUD
  placement controls, scopes where currently supported, and F1 configuration.
- Halo CE Original and Anniversary rendering, native HUD/reticles, muzzle
  effects, gameplay graphics switching, contact, and physical melee.
- First-person vehicles in every campaign, with established per-seat adjustment
  in Halo 3, ODST and Reach and universal trims in CE, Halo 2 and Halo 4.
- Room-fixed 3D cutscene theater where supported.
- Separate world-contact, true physical-melee, and gesture-melee controls.
- Experimental roomscale body translation for H2, H3, ODST, Reach, and H4.
- Snap and smooth turning, head-relative movement, comfort options, resolution
  scaling, weapon alignment, HUD controls, and fine-step slider arrows.
- Force Inject / Recover VR routed through each selected title's verified
  lifecycle rather than an H3-only blind retry.
- Cross-title re-entry that tolerates MCC retaining inactive title modules.

## Campaign status

| Campaign | Current VR coverage | Notable limits |
| --- | --- | --- |
| Halo CE | Original/Anniversary stereo and 6DOF, hands, weapons, aim, native HUD/reticles, muzzle effects, graphics switching, haptics, contact/melee, controller-directed vehicles, seated crosshairs and experimental first-person vehicles | Separate circular lens and body-following remain unavailable; new vehicle views need broad seat/clipping tests; do not switch graphics during cinematics |
| Halo 2 | Classic/Anniversary stereo and 6DOF, hands/weapons, controller aim, native HUD/reticle handling, circular zoom, optional independent dual aim, contact/melee, handedness and experimental first-person vehicles | HUD parity, unusual seats, lower-edge visibility and broad first-person vehicle testing remain open |
| Halo 3 | Mature stereo/6DOF path, articulated arms/hands, native HUD/reticle, circular scopes, cutscenes, vehicles, optional independent dual aim, contact/melee and comfort controls | Some visibility, calibration and broad dual-weapon coverage remain open |
| ODST | Stereo/6DOF, hands/weapons, HUD/reticle, cutscenes, vehicles, contact/melee and recovery | First captioned opening scene can be black; broader vehicle/co-op coverage is open |
| Reach | Stereo/6DOF, hands/weapons, HUD/reticle, cutscenes, vehicles, contact/melee and native HUD height | HUD curvature is unavailable; passenger hands and some effects/clarity reports remain open |
| Halo 4 | Stereo/6DOF, hands/weapons, HUD/reticle controls, circular zoom, cutscenes, contact/melee, handedness, comfort and experimental first-person vehicles | Floating hands rather than full arm IK; new vehicle views need broad testing; reported damage blackout is deferred |

## Fresh installation

1. Fully close MCC and extract the **Halo-MCC-VR.zip** somewhere temporary.
2. Open MCC's installation folder:
   - Steam: Library > Halo: The Master Chief Collection > Manage > Browse local files.
   - Xbox app: MCC > Manage > Files > Browse, then open `Content` if needed.
3. Create a folder named exactly `Halo_MCC_VR` in that root.
4. Copy `HaloMCCVR.dll`, `HaloMCCVRLauncher.exe`, and `halomccvr.cfg` from the
   extracted ZIP into `Halo_MCC_VR`. Do not put the Source ZIP there.
5. Start your headset connection and active OpenXR runtime. SteamVR is the
   recommended/tested route.
6. With MCC closed, run `HaloMCCVRLauncher.exe` from `Halo_MCC_VR` and choose
   the anti-cheat-disabled path. For Microsoft Store, let the launcher activate
   MCC through the Xbox app; do not rename the executable.
7. Apply the required settings in every campaign, load a level, allow normal
   initialization, and press F3 to recenter.

### CE custom campaigns

Install **both Halo CE Campaign and Halo CE Multiplayer** before playing Cursed
Halo Again or other CE mods that require them. Original-graphics campaign mods
can depend on multiplayer assets. Missing content can leave a full loading bar
with music continuing, even without VR. Follow each mod's dependency list;
Force Inject cannot supply missing game files. The Cursed Halo author's
[installation instructions](https://www.patreon.com/infernoplus/posts/cursed-halo-81524237)
require both packs.

## Updating

1. Close MCC and back up the existing `Halo_MCC_VR` folder.
2. Replace `HaloMCCVR.dll` and `HaloMCCVRLauncher.exe`.
3. **Keep your existing `halomccvr.cfg`** so alignment, controls, seats, and
   rendering preferences remain saved.
4. Update each edition's separate `Halo_MCC_VR` folder if both Steam and Store
   versions are installed.
5. Do not load obsolete `halo3xr.dll` files alongside this build.

## Force Inject / Recover VR

VR normally enters automatically. If it does not:

1. Make sure the level is fully loaded and gameplay has begun.
2. **Wait at least seven seconds.** This gives the active engine time to load
   its module and publish current camera, display, and resource proofs.
3. Use Force Inject / Recover VR from the launcher or F1 > Status once.
4. Allow the normal camera-ready delay. Do not repeatedly inject.
5. If the OpenXR session is lost, MCC crashed, or the title stays flat, fully
   close MCC and restart it through `HaloMCCVRLauncher.exe`.

Recovery does not bypass engine identity or safety checks and cannot recreate a
lost OpenXR session.

## Known issues

- Campaign-to-campaign switching can occasionally crash or require a full MCC
  restart before the destination enters VR.
- Halo CE graphics switching is unavailable during cinematics.
- New Alpha 0.5.0 features remain experimental and may need weapon-, seat-,
  title-, controller-, headset- or runtime-specific refinement.
- CE's separate circular scope lens remains unfinished; CE retains native zoom.
- Independent dual-wield trajectories are limited to H2 and H3.
- CE/H2/H4 first-person vehicle views need broader seat, clipping and comfort
  testing; per-seat preset banks remain H3/ODST/Reach only.
- Sustained/sliding contact, arbitrary modded weapon geometry, fully unarmed
  damage, secondary-weapon damage selection, and some world-object targets need
  further work and testing.
- Gun-stock calibration, independent native body-yaw following, and CE
  roomscale body translation remain planned refinements.
- Microsoft Store can appear frozen for several seconds on first load. Wait
  before assuming it crashed.
- If performance locks to half refresh, lower `resolution_scale` and disable
  runtime motion smoothing/ASW.
- The reported Halo 2 tank-exit reticle issue remains open.
- Hardware/runtime, co-op, multiplayer, mission, vehicle, and long-session
  coverage remains incomplete. MCC updates can invalidate native signatures.

## Reporting problems

Attach `HaloMCCVR.log` and `HaloMCCVRLauncher.log`, and include:

- campaign and mission;
- Classic/Anniversary or Original/Anniversary mode where applicable;
- Steam or Microsoft Store edition;
- headset and connection method;
- OpenXR runtime and refresh rate;
- what happened before the failure, especially any title or graphics switch.

## Validation

- Alpha 0.5.0 was packaged from exact runtime source **d088171**. Release x64,
  all **59 CTest suites**, 516 production vehicle-camera checks, 24 pinned
  signature checks and the Reach consistency gate passed locally.
- Published build/source downloads and their GitHub digests match the packaged
  archives. The tag points to the exact runtime source; the public branch can
  contain later documentation-only commits.
- Alpha 0.5.0 has not been accepted as a new headset-tested baseline. The prior
  Alpha 0.4.2 acceptance and earlier all-campaign smoke coverage are preserved,
  but the new experimental additions still need broader headset feedback.

This is an unofficial derivative of
[pancreations/Halo-MCC-VR](https://github.com/pancreations/Halo-MCC-VR), under
the MIT license. It is not affiliated with Microsoft or Halo Studios and
contains no MCC game binaries or complete editing-kit files. The optional
reload accessories include isolated Halo-derived geometry, which remains
artwork of its respective owners; original kit tags, textures and executables
are not included.
