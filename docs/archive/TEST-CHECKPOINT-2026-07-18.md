# Halo 3 VR weapon and HUD test checkpoint — 2026-07-18 (evening build)

This is the authoritative state for the next headset test. It supersedes older weapon/HUD theories
and proposed fixes elsewhere in the historical notes — including this file's own morning build.

## Exact deployed build (2026-07-19)

- Target: MCC Steam build `1.3528.0.0`, `halo3.dll`.
- Release DLL size: `1,264,128` bytes.
- SHA-256: `D1CCC0B8CEC4A4081412B12D8581E27DF2E42557D51DA2095E148CA1C5C145AA`.
- Deploys to `N:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo3xr\halo3xr.dll`
  (auto-copied the moment the running game closes).
- Contains, beyond the 0x6C8-corrected rebuild hook (inert in-window by measurement):
  1. **Motion blur off by default** (F1 toggle) — USER-CONFIRMED it removes the echo artifacts.
  2. **The measured FP-pair fix:** the 10:06 diagnostic run proved the gun/HUD renders through
     `{view+0x158, view+0x1E8}` (viewmodel tan 1.4361), that the pair is re-stomped to the
     center pose during per-eye view preparation, and that all `0x279BEC` sub-view rebuilds
     (~2500/s) happen OUTSIDE the eye windows on the render thread. RenderViewHook now
     re-copies the eye camera + derived block into the pair AFTER `g_prepareView` and re-pushes
     the FP constants via the uploader (`0x2770F0`). The post-eye tangent log shows whether any
     mid-render writer still stomps the pair (twin158 = 1.8418 after the eye render = we held;
     1.4361 = another stomper exists and gets a hardware watchpoint next).

## THE MORNING TEST'S DECISIVE RESULT — the flat gun was never a transform bug

The morning build's log (`halo3xr.log`, 08:23-08:26) proved every hook fired
(`visible FP reconstruction hooked`, `cached authored wrist->camera_control relation`,
`visible FP palette reconstructed at final root`, HUD calibration complete) — and the user still
saw: gun in "its own near field" at a different depth than the world, dead forward reach, residual
head-follow, HUD invisible, and fps 67-90 instead of 117-120.

Offline disassembly then found the real cause (all addresses build 1.3528):

- The struct our render hook receives IS overlay camera object 0 (`0x2D2F680`): the engine's view
  loop at `0x185D65` passes `rcx = r13` walking the 4-slot, `0x2820`-stride camera array.
- That object holds TWO camera pairs: world `{compact @+0x08 -> derived @+0x98}` and
  **first-person `{@+0x158, derived @+0x1E8}`**.
- `0x279BEC` (fastcall(view, byteFlag)) rebuilds the FP pair **inside every per-view render,
  immediately before each first-person draw pass** (6 call sites, `0x2837xx-0x283Bxx`): it
  re-copies the compact camera from the pointer at `view+0x2A8` (the CENTER-eye pose CamCopy
  left), forces the tangents to a fixed viewmodel FOV (`tanX' = const/tanY`, publishing
  `render_first_person_fov_scale`, value slot `0x8AC588`, written at `0x279C7A`), derives via
  `0x2A63E4`/`0x2A6980` (byte-identical to our resolved buildViewport/buildMatrices), and
  tail-jumps into the shader-constant uploader `0x2770F0(camera, derived)` (which pushes constant
  `0x270000` = the FP view-projection).
- Our per-eye IPD offset + lens cant were applied ONLY to the world pair. So the gun/arms/HUD
  layer rendered **identically in both eyes — zero stereo disparity, wrong FOV — a flat mono
  layer composited over a stereo world.** No bone-transform change could ever fix that. It
  explains verbatim: "own near field with different depth", the forward-reach wall (forward hand
  motion produces no disparity change), and the residual head-follow (a zero-disparity image is
  glued to the display during head motion/leaning).

## What this build changes

1. **Per-eye first-person camera** (`game.cpp`): a MinHook detour on `0x279BEC` (unique AOB; all
   6 call sites verified to keep no volatile registers live across the call, unlike the banned
   `0x120DF8`). After the engine's rebuild, while one of our eye renders is active and the view
   pointer matches, it overwrites `view+0x08` with that eye's finished world compact camera and
   `view+0x1E8` with that eye's derived block (both snapshotted in RenderViewHook after every
   per-eye write), then re-calls the uploader `0x2770F0` so the already-pushed constants are
   redone. Last writer before any FP draw. The driver's sub-view rebuilds (`rcx = view+0x6C8`)
   are deliberately left alone.
2. **CHUD steal-and-requad machinery deleted** (`game.cpp`, `d3d11_hook.cpp`, `vr.cpp`): the
   three CHUD hooks, the PS/VS/draw-call classifier, capture texture, hand-HUD swapchain, and
   `hud_scale` are gone. The native HUD renders untouched in both eyes again. The stock crosshair
   stays suppressed (`chud+0x146`) and our floating reticle stays. This also removes the fps
   regression (the classifier's per-draw cost and its calibration retry loop, which restarted
   ~46 times in 5 s in the morning log).

Weapon palette reconstruction, translation mapping, aim, controls: untouched from the morning
build (they were correct; the flat layer was hiding it).

## User-observed state immediately before this build

- HUD was gone; stock crosshair face-locked in older builds; HUD stretched outside the view.
- Gun followed the right controller but also the head, "in variations of regular and inverted".
- Gun lived in "its own near field" with different depth; arm extension did not reach into the
  world.
- Bullets, custom reticle, muzzle flash, and basic controller aim were already accurate.
- Gameplay camera correctly followed the headset and must remain that way.

## Proven rendering facts

- The visible gun is not built by `0x2C13B8`. That function builds marker/effect packets.
- The visible mesh path is
  `0x2C0D20 -> 0x184B08 interpolation -> 0x2C5A38 -> 0x2C561C palette consumer`.
- `0x2C561C` is a real pdata function boundary with one direct caller. Its verified ABI is:
  `RCX tag, RDX actual root, R8 destination, R9 unused, stack arg5 source, stack arg6 bone map`.
- The renderer applies `World = Root * sourceRecord`. The controller record must therefore be
  `Root^-1 * Controller`, not `Controller * Root^-1` and not a delta composed onto an already
  head-baked record.
- Halo applies common camera lag to the wrist and most bones but excludes `camera_control`.
  Combining a lagged wrist with an unlagged camera_control caused the remaining inverse-head term.
- The three CHUD functions at `0x2D27E8`, `0x2D289C`, and `0x2D2670` enqueue CPU commands. They do
  not draw HUD pixels before returning. Actual HUD pixels appear later at D3D11 draw calls.

## 2026-07-19 REWRITE — true rigid parenting (supersedes the reconstruction below)

At the user's direction ("parent it to my hand, no mask"), the cached-relation reconstruction
below is REPLACED. `ReconstructVisiblePaletteSource` and `ApplyControllerToMarkerBones` now
compute ONE rigid transform per frame from same-frame quantities only — untouched interpolated
bones, the actual render root argument, a live controller read: `T = controllerPose * mount *
(root * wristRecord)^-1`, applied to every bone as `record' = root^-1 * T * root * record`.
The wrist lands exactly on the controller in rotation AND position; the assembly stays as
authored (animations intact); barrel alignment is the constant F1 mount trim. The
`wrist^-1 * camera_control` cache, lag-consistent camera_control synthesis, and every other
sim-clock input to the visible path are no longer consumed. Telemetry (1/s wrist record
fwd/left/up) retained. Prior morning run also proved: per-eye FP camera pair now holds through
the render (post-eye twin158 = 1.8418), and hand forward reach arrives intact (0.02 -> 0.212 wu
measured during a real arm extension). Comfort levers added the same day: `gun_forward_m`
standoff slider and a reversible "Show HUD" toggle (chud+0x144).

## Historical: cached-relation weapon reconstruction (superseded above)

1. The `0x184B08` hook identifies right-hand slot 0 and copies the untouched interpolated bones to
   thread-local storage. Its live output can still preserve the proven marker/muzzle alignment.
2. The composer caches the complete authored `wrist^-1 * camera_control` relationship before the
   camera-lag pass.
3. The final `0x2C561C` hook uses the actual root argument and a private thread-local source copy.
4. It computes `desiredCamera = Root^-1 * RightController`.
5. It synthesizes a lag-consistent camera_control from the current wrist and cached authored
   relationship, then applies one rigid transform to every skinned influence.
6. Halo's original `0x2C561C` consumes that private copy and writes the final visible palette.

This path does not write the animation bank, root, overlay camera, gameplay camera, or shared camera
memory. The gameplay camera continues to receive the headset pose only.

## Current translation and reach model

Controller world position is now:

`pre-head gameplay origin + fixed F3 recenter map * (controller - recentered head origin) * world scale`

The previous expression mixed a camera position containing head lean with a fresh
controller-minus-current-head sample. That phase mismatch leaked head motion and shortened forward
reach. The new mapping is linear and orthogonal, uses one uniform scale, and has no distance clamp or
forward wall.

The morning test showed forward reach compressed WITH the palette hook proven executing; the
mono FP layer above is the explanation. The linear map itself is unchanged and correct.

## Current HUD state

Native. The steal-and-requad machinery is deleted (see "What this build changes"). The HUD now
renders through the FP camera pair, which this build makes per-eye, with the world-matched
projection instead of the engine's normalized viewmodel FOV — its apparent size/placement in the
headset is a fresh observation for this test. HUD comfort (size, centering, edge split between
eyes) is the explicit NEXT iteration; with the FP camera under our control there is finally a
clean lever for it.

## Failed approaches that must not be repeated

- Never write a controller pose to shared camera `halo3+0x2D2F680`, the camera-copy destination, or
  any gameplay camera. This made the world camera follow the hand.
- Never detour `halo3+0x120DF8`. Halo 3 is LTCG-compiled and keeps `r8/r9/r10` live across internal
  calls; compiled shims clobber them and crash during level load.
- Do not use composed `weapon+0x4A4` matrices or composer default roots as the visible mesh lever.
  Headset testing proved that they move markers and muzzle effects, not the visible gun.
- Do not use `0x2C13B8` as the visible gun consumer. It is the marker/effect packet path.
- Do not write bank record 0 or a shared skeleton root. It feeds camera_control and can move the
  camera/body.
- Do not write record scale. camera_control descends from the same hierarchy and scale changes can
  zoom the game camera.
- Do not write only one 0x800 bank half. Halo interpolates two snapshots and produces the proven
  halfway hover.
- Do not apply `Controller * Head^-1`. It produces conjugation and the observed inverse/double head
  rotation.
- Do not restore the gameplay camera to stick aim around the camera copy. It separates frames used
  by the world and first-person systems and caused head-plus-hand mixing.
- Do not use a broad FOV override to shrink the weapon/HUD. The 140-degree build broke the VR image
  and made CHUD worse.
- Do not scale inactive overlay-camera slots as HUD placement. The slider had no visible effect.
- Do not relocate CHUD leaf transforms or guessed screen anchors. Both headset-tested versions broke
  the HUD and did not make it follow the hand.
- Do not scope a HUD RTV only around the three CHUD CPU functions. Their queued GPU draws happen
  later, so the capture is empty and suppressing the second eye makes the HUD disappear.
- Do not restore an expensive probe, full-frame GPU readback, constant census, or third world
  render. Those approaches caused unacceptable CPU/GPU cost or cut 120 Hz stereo to 60 Hz.

## Honest remaining risks

- Only a headset run can confirm the substituted FP camera reaches every first-person draw pass
  in the live level.
- The rebuild's byte flag selects a variant that scales compact-camera field `+0x64`; the
  substitution ignores it and uses the world camera's value for every FP pass. If one FP subpass
  (e.g. a shadow or mirror variant) mis-renders, this is the first suspect.
- The FP camera pair lives in overlay camera object 0, which the gameplay/scope paths also read;
  the substituted bytes persist after the render until CamCopy rewrites them next frame. RE-notes
  records that writes to this object have leaked into gameplay before (a full controller pose).
  Per-eye offsets are ±1 cm and symmetric, but watch for aim jitter.
- The FP layer now renders with the world projection instead of the normalized viewmodel FOV, so
  gun and HUD apparent sizes will change; gun_scale (Home/End) is the intended size lever.
- The authored wrist relationship is published on the simulation/composer clock. A small one-frame
  animation wobble can remain, but it cannot explain steady inverse head tracking or a forward wall.
- A same-bone-count weapon switch can use the previous authored relationship for one frame.

## Required headset test and interpretation

1. Launch Halo 3, enter a level, press `F2`, then `F11`, and wait one second.
2. **Depth check:** hold the controller ~40 cm out and alternately close one eye, then the other.
   Pass: the gun's position shifts between eyes the way a real held object does. Fail: the image
   is identical in both eyes (still a flat layer — then the new hook did not run; check the log).
3. **Reach check:** head still, extend the arm fully and pull it back. Pass: the gun travels
   forward INTO the world, distance matching the controller roughly 1:1. Fail: forward wall.
4. **Head check:** hold the controller still and rotate + lean the head. Pass: the gun stays
   planted in the world. Fail: sustained or inverted motion with the head.
5. **HUD check:** the HUD is visible in both eyes. Report its size and position and whether edge
   elements still split between the eyes — that is this test's fresh observation, not a
   pass/fail.
6. Move only the head. Pass: gameplay world follows the headset and never follows the hand.
7. **Motion-blur probe (30 seconds, settles the reopened echo/artifact bug):** in MCC's menus go
   to Settings -> Video and turn **Motion Blur OFF**, then re-enter the level with F2+F11 and
   look at foliage/bright edges with the head still. Echoes gone = the artifact IS the motion
   blur pass consuming the other eye's camera as "previous frame", and the code fix becomes
   targeted. Echoes unchanged = hypothesis dead. Report which.
8. **Performance:** fps steady at the headset rate (user currently runs 90 Hz); no calibration
   log spam.
8. **Watch for regressions from the new camera writes:** any new world-view jitter, aim
   disturbance, or scope-zoom misbehavior (the FP camera pair lives in the same object the
   gameplay/scope paths read).

Expected log proof:

- `M3: FP camera rebuild hooked at halo3.dll+0x279BEC (uploader +0x2770F0); gun/HUD layer renders per-eye`
- `M3: per-eye FP camera active (gun/HUD layer now renders with the eye's world camera)`
- `visible FP reconstruction hooked` / `visible FP palette reconstructed at final root` (unchanged)
- NO `M3 HUD GPU:` lines at all (machinery deleted)

If the test fails, do not tune FOV, scales, camera values, or enable Cheat Engine. Close the game and
preserve `halo3xr.log`; the missing or present proof markers determine the next step without another
guess-and-revert loop.
