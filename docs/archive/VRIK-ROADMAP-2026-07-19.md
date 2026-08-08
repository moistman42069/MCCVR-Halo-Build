# VRIK roadmap — full-body VR embodiment (the user's stated end goal)

> **2026-07-19 approved plan (authoritative):** upper-body VRIK on the BIPED pipeline.
> The user's requirement, verbatim: "the controllers following my arms; Master Chief's chest
> remaining in its spot; me being able to push my arm completely out and have the controller
> follow." Stage A ships in build `cada1815...`: A1 = "Show body" toggle (director switches);
> A2 = the bone probe extended to every non-FP skeleton via the existing compose hooks
> (`ApplyBipedProbe`, "Bone probe" checkbox). Two yes/no observations gate Stage B (two-bone
> arm IK, solver already written in `src/dll/ik.cpp`). The physical-crouch button feature was
> REMOVED at the user's direction. No FP-viewmodel bone work ever again — proven dead.

Target experience (user's words): "like a VRChat character with running legs and crouch" —
Skyrim/Fallout-VRIK-style presence. Head and hands are tracked truth; a visible body lives
under the head; legs animate with locomotion; real crouch = in-game crouch; gun physically in
the hand.

## 2026-07-19 LIVE camscan session — findings (durable)

A guided differential-scan session (user posed the right controller up/down; I ran camscan)
located the **player biped skeleton in memory** and reframed the whole problem:

- **Skeleton found.** Differential scan (arm-up hold -> narrow changed -> narrow unchanged ->
  arm-down -> narrow changed/unchanged) converged on clean bone matrices that track the
  controller arm. Structure: **48-byte records = translation(3 floats) + orthonormal 3x3
  rotation**, positions of adjacent bones tightly clustered (a compact skeleton). It responds
  to the CONTROLLER (only the player's own biped does), so it is the player body.
- **Two copies.** A **render-history ring at RVA ~0x468xxxx** (dozens of ~15-float slots,
  groups 9-42 in the scan — the "sliding array/ring for temporal effects" the notes predicted)
  and a **static pool near RVA 0xA84xxx**. Session absolute addresses do not persist across
  launches; the RVA regions + record structure do.
- **Writer not catchable by HW watchpoint** even after fixing camscan to arm ALL 206 existing
  threads (the fix: `SetDrAllThreads`, previously only the main thread was armed). The ring
  value provably changes but no CPU single-step write was trapped — it is written by a bulk /
  indirect path. Do not sink more time into watchpointing the ring.
- **THE REAL GATE, now clear:** Halo does not render the biped's **arms/torso** in first
  person at all — only the legs (the visible "first person legs") and the separate
  camera-space weapon-arms. So posing the arm bones we found is INVISIBLE until the FP upper
  body is made to render. **Stage A1 (make the FP upper body visible) is the true gate**, not
  bone writing. Everything else waits behind it.
- **Live levers to try next session (game must be running):** `debug_first_person_models` has
  a real value slot at RVA **0x7CE8F0** (type 5). `render_first_person` (val slot RVA
  ~0x7CBFA0) and `director_disable_first_person` (~0x7D1B88) had NULL value pointers on disk
  but engine globals often initialize at runtime — poke them live with
  `camscan pokeabs <base+rva> <val>` and watch whether the body appears. `legs_shared` string
  at 0x8011F8 is a lead for the FP-legs render path (find its readers to find the region mask
  that hides the torso/arms).

## 2026-07-19 H3EK BREAKTHROUGH — the first-person body is a COMPLETE body

The Halo 3 Editing Kit (H3EK, extracted at `N:\SteamLibrary\steamapps\common\H3EK`) settles the
architecture from the real tags (read via Python ASCII extraction — `strings` is not installed;
`xxd`/Python work). Master Chief FP models at
`tags/objects/characters/masterchief/`:

- **`fp/fp.render_model`** — the camera-locked ARMS layer (nodes: `l/r_upperarm`,
  `l/r_forearm`, `l/r_hand`, `l/r_thumb`, `head`). This is the "gun/arms" we could never move
  by bones — it is drawn through the FP camera (confirmed all session).
- **`fp_body/fp_body.render_model`** — a **COMPLETE first-person body**: `l/r_upperarm`,
  `l/r_forearm`, `l/r_hand`, `l/r_thumb`, `l/r_clavicle`, `spine`, `neck`, `head`, `pelvis`,
  `l/r_thigh`, `l/r_calf`, `l/r_foot`, `l/r_toe`. **NOT legs-only.** It is already rendered in
  first person (that is the "legs" the user sees); the upper body/arms are simply at rest by
  the sides and near-clipped by the head-height camera, so they fall outside view.
- Regions are only `default`/`base` (no per-limb region to toggle) — so "first person legs"
  is NOT a region cull; the full body is drawn and the camera simply sits at head height with
  the arms resting down.

**Consequence — the true VRIK path, now grounded in real node names:** pose `fp_body`'s own
arm chain to the controllers so the arms RAISE from their resting position into view holding
the weapon. No new geometry, no un-culling, no modified game files. The exact IK chains:
- Right: `r_clavicle -> r_upperarm -> r_forearm -> r_hand` (wrist = r_hand, elbow = r_forearm,
  shoulder = r_upperarm), thumb `r_thumb`.
- Left mirrors with `l_`.
This is exactly what `src/dll/ik.cpp` (two-bone solver, no wrist clamp) was written for.

**Open, for the next live session (all cheap now):**
1. Is the memory skeleton found 2026-07-19 (48-byte matrices, RVA ~0x468xxxx / ~0xA84xxx) the
   `fp` arms or the `fp_body` body palette? Determine by posing it and seeing whether arms are
   already-in-view (fp) or swing up from the sides (fp_body).
2. Re-anchor the held weapon from the `fp` hand to the `fp_body` `r_hand`, and hide the
   redundant `fp` camera-arms once `fp_body` arms hold the gun.
3. The dev build (`halo3_tag_test.exe` / `sapien.exe`, debug menu `bin/debug_menu_init.txt`)
   can validate FP body/arm visibility toggles directly if needed.

## 2026-07-19 BREAKTHROUGH CONFIRMED + PUNCH LIST (build e344d27a)

USER-CONFIRMED on build 92c783df: "it has depth now... translating properly in the 3D space...
we finally have it actually in the 3D space." The final bug was ARMING ORDER: the per-eye
snapshot/stamp had to be armed BEFORE `g_prepareView` (which triggers the in-eye FP driver
runs), not after. Do not reorder that sequence in RenderViewHook.

### 2026-07-19 punch-list corrections (build 63508fa1) — two items falsified in the headset

- `gun_length_scale` REMOVED. User: "the length just moves the gun, its broken." Root cause is
  structural, not a tuning bug: the weapon mesh is rigid geometry on essentially one bone, and
  the engine `BoneMatrix` carries a single UNIFORM scale — a barrel-only squash is not
  expressible at the palette level. Squashing bone ORIGINS toward the wrist just translates
  the mesh. DO NOT re-attempt at this layer; a real squash would need the weapon model's own
  vertex/palette path (research item, not scheduled). Working "too long" trims: `gun_scale`
  (uniform) and `gun_forward_m` (now -0.3..0.5; negative seats the gun back into the fist).
- Mount-trimmed shared aim ray REVERTED (`Game_MountLocalAimDir` deleted). User: "the rotation
  moves the cursor too." Coupling the cursor/bullet ray to the mount trim made barrel-vs-cursor
  tuning NON-CONVERGENT: the authored offset between the wrist bone frame and the visual barrel
  is constant, and rotating the trim moved the cursor in lockstep, so the gap could never close.
  Now: cursor + bullet steering use the RAW controller aim -Z (fixed laser); gun_pitch/yaw/roll
  rotate ONLY the mesh + flash. Calibration workflow: rotate the gun until the barrel lies on
  the cursor line — convergent, one-time.

### 2026-07-19 SKELETON GROUND TRUTH + FLASH ROOT FIX + ELBOW POLES (build 8dd2d40c)

User confirms on 250ec065: gun tracks the hand and points correctly ("amazing" — auto barrel
alignment works; measured barrel-in-wrist (0.973, -0.227, -0.034) = camera-forward + ~13 deg
authored cant, invariant CONFIRMED). Left arm attached via the topological finder. Remaining
reports fixed in 8dd2d40c:
- **l_hand = 0xA2, live-proven** (third time's the truth; 0xA1 and 0x9E both falsified).
  Full FP AR skeleton recorded from the one-shot dump: 0=root(0xAEC); left chain 1(0x1F2)->
  3(0xBBC)->5(0xA2 l_hand), fingers 7-11+chains; right chain 2(0xBBB)->4(0xBCE)->6(0xA6
  r_hand), fingers 12-16+chains; GUN = node 37 (0x150F, map-local id) child of the right
  wrist, with children 38-41 (0x151D/0x1DEE/0x151F/0x2148) = the 5-bone gun subtree;
  42 = camera_control (0xD9). Weapon-specific node ids are map-local (> global list) — the
  topological fallback stays for other weapons.
- **Flash followed head + offset from gun; bullets spawned past the muzzle**: the marker path
  baked its transform against the TLS-cached render root, which can be stale/per-eye. Fixed:
  root pose rebuilt from the LIVE center camera atomics (the old headset-proven lever's
  source); TLS root contributes only scale.
- **Elbow pole bias out+down** (user: elbows "feel inward... I want them to stick outward"):
  pole = 25% authored + 75% (camera lateral * side - 0.6 * camera up), per arm side.
- Topological-finder log had no one-shot guard -> 10MB log; guarded, and the proven 0xA2 fast
  path skips the fallback entirely for the AR.

### 2026-07-19 SECOND FALSIFICATION + TOPOLOGICAL LEFT WRIST (build 250ec065)

The 0x9E fix below ALSO failed live ("L wrist -1" again, fresh log 20:43) — the disk
pointer-table slot indexing does not map to runtime record ids the way assumed. Runtime record
ids are now treated as unknowable offline. Shipped instead:
- Topological left-wrist finder (no id needed): deepest node with subtree >= 10 outside the
  right-wrist subtree and not an ancestor of the right wrist. Unique in a 43-node FP skeleton
  (l_hand subtree 16 < l_radius 17 < l_humerus 18).
- One-shot log dumps ALL 43 record ids + parents ("M3 VRIK: skeleton[..]") — the runtime dump
  is the ONLY trusted id source from now on; record the left-wrist id from the next log.
- One-shot logs for the auto barrel alignment (measured barrel-in-wrist vector + first swing
  angle) — it had shipped silent, repeating the gated-diagnostic mistake.

### 2026-07-19 LEFT ARM ID FIX + "4 ft gun" RESOLVED BY MEASUREMENT (build f0488c2a)

- **l_hand = 0x9E, not 0xA1.** The runtime log falsified the old offline derivation ("M3 VRIK:
  arm chains — ... L wrist -1"): 0xA1 never matched, so the left arm NEVER resolved in any
  build (the "left hand still on my head" reports were this bug, not IK). Authoritative method
  (use this for all future ids): halo3.dll contains a string-id POINTER table; the pointer to
  "r_hand" is unique in the whole file and anchors slot 0xA6; the table then reads l_arm=0x9C,
  l_hand=0x9E, l_leg=0xA0, l_foot=0xA2, r_arm=0xA4, r_hand=0xA6, r_leg=0xA8, r_foot=0xAA
  (ids stride 2 with null slots between). The old string-pool counting method (gave 0xA1) is
  falsified — the pool is not in id order. Scratch tooling: string_table.py / table_dump.py.
- **"Gun feels 4 ft long" was LITERALLY TRUE and is config, not code.** H3EK tag measurement:
  the FP assault rifle render_model is authored at true world scale (muzzle marker x=0.2334 wu
  vs world model 0.2278 — ratio 1.02; MA5C ~0.85 m authored). The palette math normalizes wrist
  world scale to exactly 1.0, so the gun renders true-size × gun_scale. At the drifted
  gun_scale=1.35 the gun was 1.15 m = 3.8 ft — matching the user's feel exactly. Runtime log
  confirms the wrist subtree is 21 bones = wrist + 15 hand + 5 gun bones, so gun_scale
  provably covers the gun mesh. At the reset 0.85 default the gun is 0.72 m.
- Stereo metrics audited while hunting: IPD 67.5 mm × 0.33 wu/m, hand mapping 0.33, world
  1 wu = 3.048 m — all consistent, no scale bug in the stereo rig.

### 2026-07-19 AUTO BARREL ALIGNMENT (build c880e65c)

User (with screenshot): "The gun Muzzle and bullets are not aligned, and the fov of the gun is
way too high... the sliders cannot align it." Diagnosis had TWO parts:

1. **Config drift**, not code: the live cfg had `gun_scale = 1.35` (135% — the "way too
   high FOV" giant gun; default is 0.85) and `crosshair_distance_m = 2.0` (bullets are steered
   through the cursor POINT, so a 2 m cursor makes the head-origin bullet ray cross the hand
   ray at 2 m and diverge wildly beyond — guaranteed "bullets don't match the muzzle" at any
   combat distance). Reset to 0.85 / 10 m. Lesson: read the user's live cfg BEFORE theorizing.
2. **Slider alignment is structurally non-convergent by hand** — replaced with AUTO BARREL
   ALIGNMENT. Mechanism (no string ids, no tags): in the authored FP pose the barrel lies on
   the camera-forward axis (Halo aims the viewmodel at the center reticle) and the render root
   IS the camera, so the authored barrel direction in the wrist frame = invRot(wrist)·(1,0,0)
   = row 0 of the unmodified wrist record's rotation. FpInterpolateHook measures it per frame
   into a slow EMA (rig constant per weapon; EMA rides out sway/reloads, converges ~1-2 s
   after a weapon switch). DesiredWristWorld (right hand) then applies the minimal world swing
   (ShortestArcRotation) putting that axis exactly on the controller ray — the SAME ray the
   cursor and bullet steering use. Barrel-on-cursor is now guaranteed by construction; the
   pitch/yaw/roll sliders only adjust hand posture/roll about that fixed line and CANNOT
   misalign the gun. Sliders + cfg rotation reset to 0.

Punch-list build e344d27a (one decisive build):
- Proportions: `gun_scale` default 0.85 (wrist-subtree uniform, Home/End). (`gun_length_scale`
  shipped here too — falsified and removed, see above.)
- LEFT ARM on the left controller: shipped with l_hand id 0xA1 — WRONG (null slot; never
  resolved; corrected to 0x9E in build f0488c2a, see above). Chain capture by parent walk,
  two-bone IK, mirrored yaw/roll mount, no standoff; skips when the left controller is
  untracked — all of that machinery was correct, only the id was bad.
- Alignment: ONE shared `DesiredWristWorld()` used by the visible palette, arm IK, AND the
  marker/muzzle path (flash can no longer diverge from the gun). (The second half of this item —
  reticle + bullets on the mount-trimmed ray — was falsified and reverted, see above.)
- Aim responsiveness: steering gain k 5.7 -> 12 (full stick at ~4.8 deg error); ceiling is the
  game turn rate (raise in-game look sensitivity for more).
- P0 instrumentation stripped (histogram + per-second logs); success one-shots retained.

## 2026-07-19 THE RENDER-ARCHITECTURE FINDING + PER-EYE FP RENDER (build 8c4651a4)

Phase-0 instrumentation (FP driver 0x2835D4 hooked; histogram over 10s: out=2390 eyeL=1646
eyeR=1644, one thread) settled the architecture that defeated every previous FP fix:

- The engine STAGES first-person rendering once per frame OUTSIDE the per-eye windows: the FP
  driver runs ~3x/frame there and performs all six 0x279BEC camera rebuilds (center pose,
  crushed viewmodel depth) plus the palette/skinning consumption (P0: palette consumed at
  stereoEye=-1 almost always).
- The SAME driver then runs ~2x per eye INSIDE each eye window and only DRAWS, using the staged
  center-eye camera — which is why the weapon was a zero-disparity front layer in the headset
  while each individual image (desktop mirror) looked correct.

Fix shipped (FpDriverHook, game.cpp): immediately before every IN-WINDOW driver run, stamp this
eye's world compact camera + full world derived block (FOV AND depth) into BOTH staged FP pairs
— {view+0x158, view+0x1E8} and the sub-view {view+0x6D0, view+0x8B0} — then re-run the engine's
own constant uploader. The engine's in-window draw path thus pushes OUR per-eye camera through
its own machinery. Per-frame weapon pose, per-eye camera: the standard VR renderer split. The
out-of-window staging runs stay untouched (packets/palette still built once per frame).

## 2026-07-19 VIEWMODEL DEPTH FIX (build 686ac045) — the user's "orthographic squash"

User report (precise, correct): the gun looks flat/orthographic, warps from flat->thicker when
twisted (like a squeezed lattice), and is trapped in a shallow near-field it cannot extend
forward through. This is the classic FPS viewmodel DEPTH HACK — Halo renders the first-person
layer with a crushed near-far depth slab so the gun never clips walls on a flat screen. In VR it
flattens the weapon, kills its real forward/back travel, and distorts rotation.

Root cause in our code: `RenderViewHook` was copying only the world projection's FOV diagonals
(`fpProj[0]`, `fpProj[5]`) into the FP projection, leaving the crushed DEPTH terms
(`[8..15]`) intact; and the during-render `FpCameraRebuildHook` had a `view == eyeView+0x6C8`
gate that the diagnostic proved NEVER matched, so it never applied at all.

Fix: the FP camera pair {view+0x158, view+0x1E8} is now overwritten with this eye's FULL world
camera + FULL world derived block (position, orientation, FOV AND depth) — pre-render in
RenderViewHook, and authoritatively during-render in FpCameraRebuildHook (gate removed; it now
stamps whatever 0x279BEC rebuilds during our eye pass, then re-runs the constant uploader). The
old camera-level hand-anchor math (K'=H*C^-1*K) is deleted — placement is the arm-IK bones'
job; the camera's only job is correct world projection. Expected: gun has true perspective/depth,
extends forward with the hand, stops warping when rotated. Trade-off accepted (the VR norm): the
weapon may now clip into nearby world geometry, which the depth crush previously hid.

## 2026-07-19 ARM IK IMPLEMENTED (build c314560e)

First real articulated arm, replacing the whole-assembly rigid parent. In game.cpp:
- Arm chain resolved by walking the node parent table up from the wrist (r_hand, string
  index 0xA6): `elbow = parent(wrist)` (r_forearm), `shoulder = parent(elbow)` (r_upperarm).
  A 64-bit `wristDescendants` mask captures the hand+fingers+weapon subtree.
- `ReconstructVisiblePaletteSource` arm-IK branch (config `arm_ik`, default ON, F1 toggle):
  compose each chain bone to world via the render root, take rest bone lengths, solve the
  elbow with `IK_SolveTwoBone` (ik.cpp, no wrist clamp, pole = rest elbow offset), swing the
  upperarm/forearm rest orientations onto the new child directions with `ShortestArcRotation`
  (Rodrigues), and ride the wrist subtree on one rigid delta so the gun stays gripped. Body
  and legs are left exactly as the game posed them (planted). Falls back to rigid parent if
  the chain is unresolved or IK degenerates.
- Log markers: `M3 VRIK: arm chain resolved ...` and `M3 VRIK: arm IK active ...`.

Open validation (headset): does the arm bend to the controller with the shoulder staying put?
If the chain is the camera-space `fp` arms, the shoulder sits near the eye (odd but articulated);
if it is `fp_body`'s shared skeleton, the shoulder is at the body. Either result is a concrete
step and the F1 toggle A/Bs it against the old rigid parent.

## Dependency order (each stage gates the next)

**Stage 1 — Layer possession (deployed, awaiting headset verdict).**
The first-person layer (gun/arms/HUD) rigid-parented to the right controller through the FP
render camera pair `{view+0x158, view+0x1E8}` — the only lever with headset-proven pixel
control. Bone-level writes are PROVEN invisible (two disjoint transform regimes, identical
pixels, 2026-07-19). Verdict vocabulary for the test: follows / opposite / double / nothing.

**Stage 2 — Skeleton possession (the SKSE-equivalent moment; offline hunt).**
Find the actual data the GPU skins the visible FP mesh from. Trailhead: the constant-upload
machinery (`0x2AF478(id, data, size)` funnel; known ids `0x270000` view-proj, `0x2F0003/4`
viewport). A skinning palette upload is large (~53 bones) and id-distinctive. Detour risk on
hot engine-internal functions must be ABI-verified per call site (the 0x120DF8 lesson).
Without this stage, ANY IK system has nowhere to write — Skyrim VRIK exists only because SKSE
provides this layer.

**Stage 3 — Body presence (the HaloCEVR path, cheaper than raw IK).**
Show Chief's existing third-person body in first person; the game's own locomotion animations
provide running legs, crouch, jump for free. Hide/fade the head. The CAMERA already follows
real head elevation 1:1 (live since M2 stereo) — that is the user's "follow my elevation"
requirement and must never regress. The real-crouch->crouch-button mapping exists but is OFF
by default at the user's direction (it syncs only the discrete game stance/hitbox; it is not
elevation-following and must never be presented as such).

**Stage 4 — VRIK proper.**
Arms solved shoulder->real hand poses (two-bone IK per arm), body yaw following head/hands,
gun gripped with both hands when close. Requires Stages 2+3.

## Standing facts that constrain all stages

- Renderer ignores animation-system writes for the FP layer (proven 2026-07-19).
- The FP camera pair is fully ours per eye (proven held through the render).
- Motion blur off = the ghost fix (solved, user-confirmed); do not regress.
- 120 fps + controls remain the floor; EAC stays off via official launch; AOB-only addressing.
