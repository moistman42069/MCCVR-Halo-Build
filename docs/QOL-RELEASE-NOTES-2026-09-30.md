# MCCVR September 30 community candidate

Local test candidate after Alpha 0.6.0. **Not headset accepted, not a claim that
every reported bug is fixed.** Exact source and file hashes are in the included
candidate manifest and BUILD-IDENTITY.txt. The accepted pointer remains Alpha
0.4.2 while this larger candidate is evaluated.

## Changes in this candidate

- Native MCC shell/login windows now receive genuine focus and activation
  changes. The old global override is retained for known in-game modes,
  including loading, vehicles, pause and cutscenes. This addresses a concrete
  source-level risk behind the Linux keyboard report; forced Xbox sign-in and
  actual Proton text entry still need runtime testing. The Windows launcher
  continues to request administrator access.

- Individual finger joints are now identified in all six titles' admitted rigs,
  with per-hand/per-digit native joint IDs, parent relationships and rig-specific
  palette indices. H2 source-to-render mapping is explicit; uncertain H2 digit
  names stay Unknown. Existing finger pose code consumes these inventories.
  This is the foundation for later contact reactions, not an Alyx-style physics
  implementation or optical finger tracking.

- Experimental full-body IK now has guarded Halo 3 Chief/Elite/Arbiter and Halo 4
  Chief runtime paths. Both follow the headset and solved controller hand targets,
  retain native feet within reach, and preserve the ordinary VR path when a pose
  cannot be admitted. Halo 4 gains proportional arm reach and its own palm-marker
  conversion so body hands follow the weapon-hand alignment.
- The same experimental option adds controller-driven free-hand responses on the
  title-specific rigs. H3/H4/ODST reset proven binds for open, point and fist poses.
  CE/Reach add separate trigger/index and grip/other-digit flex over native poses;
  H2 adds conservative grip-only flex. These three paths do not claim a full open
  hand reset. Held/dual/two-handed grips stay authored. These are not optical finger
  tracking or object-reactive fingers. See the six-title coverage chart in
  `IMPLEMENTATION-STATUS.md`; CE/H2/ODST/Reach full-world avatars remain unfinished.
- CE automatic camera recovery retires its old tracking reference. Regression
  fixtures cover both graphics modes and stale queued/completed work. The reported
  gun shift after level loading still needs headset reproduction; this is a tested
  lifecycle correction, not confirmation that every reported shift is fixed.

- The VR menu matches the launcher's navy/cyan styling and uses its bundled
  Oxanium font for headings and navigation. The font is embedded and adds no
  runtime file dependency; existing controls and panel interaction remain.

- Continuation after the first September 30 package adds live resolution changes
  with desktop fitting off, explicit resize retries, and recovery from obsolete
  DLSS depth-cache entries. The production DLSS wrapper passed 90 synthetic GPU
  evaluations; all-game headset performance remains unverified.
- All six games gain evidence-backed native action bridges for independent VR
  mappings, including separate Reload/Use and Unbound. Their own native consumer
  contracts are retained; layouts, vehicles and online behavior need runtime testing.
- Show arms with hands is separate from the experimental body switch. New
  H2 single-weapon, Reach support-grip and H4 arm paths preserve tracked hands
  and weapons when a solve fails. H2 dual-wield arms remain hidden; universal
  full-body visibility remains WIP. H3's legacy body switch
  has byte-width, stale-generation and failure-isolation corrections.
- CE gains persistent vehicle/seat adjustments keyed by verified model-node
  names. Unknown models retain per-game offsets; identical node-name rigs share
  a profile. A CE controls cleanup range-count defect is also fixed.
- CE Classic and Anniversary gain guarded roomscale body following through the
  existing movement transport. Simultaneous stick/physical following is still
  unfinished.
- An optional empty-support-hand-near-head gesture temporarily reveals hidden
  HUD, with dwell and interaction guards. Free-hand controller posing is separate from this HUD gesture.
- The launcher requests administrator privileges by default through Windows
  UAC. The user still chooses whether to approve that prompt.
- The launcher executable sits outside the manual `ModFiles` folder. A separate
  manual-mod ZIP contains `Halo_MCC_VR` and no launcher executable. The complete
  build ZIP remains available for installation and GitHub updates.
- Retaining settings now preserves legacy stock-strength/smoothing migrations
  and global gun/HUD calibration inherited by missing per-game profiles. New
  defaults no longer mask those saved preferences; explicit per-game values
  still win.
- Gab_dC's PR16 stock and two-hand improvements are adapted locally: Plus and
  Standard stock, grab/release transitions, persistent support attachment,
  adjustable offhand influence, Grip-based free two-hand geometry and optional
  two-hand smoothing. Virtual stock and smoothing remain optional.
- Coherent committed-sample aim keeps the asynchronous native aiming path on
  matching controller/head/grip data, with freshness and lifecycle guards.
- Optional local telemetry recording and analysis support aim diagnostics.
  Recordings live in `Halo_MCC_VR/Telemetry Recordings`; analysis tools live in
  `TelemetryAnalyser`. Automatic analysis requires Python. Recording failure or
  unavailable Python does not disable VR, and there is no automatic upload.
- Integration review replaces new synchronous hot-path diagnostic logging with
  bounded counters drained by the existing worker. This is a concrete safety
  correction, not a measured universal performance improvement.

The earlier physical crouch, per-game/per-weapon settings, vehicle camera banks,
reload/holster controls, dual H2/H3 reticles, H2 Original/H4 muzzle-hide fallback,
subtitle controls, bloom controls and optional DLSS work remain included. Exact
coverage and limitations follow in `IMPLEMENTATION-STATUS.md` and the complete
inherited `PREVIOUS-IMPLEMENTATION-STATUS.md`.

## Install, update and manual files

Extract the complete build ZIP and run the root `HaloMCCVRLauncher.exe`. It
requests administrator access, detects Steam and Microsoft Store editions, and
supports browsing to the **base MCC folder**, for example:

`C:\Program Files (x86)\Steam\steamapps\common\Halo The Master Chief Collection`

The installed mod goes into `Halo_MCC_VR` directly inside that folder. Keep
configuration retention enabled to preserve settings and add new defaults.
Backup/rollback and explicit launch/check-update/install-update actions remain.
The updater consumes the complete installer archive, not the manual-only ZIP.

When coming from the old 0.6.0 launcher, download and run this complete new
installer once. The old updater requires a launcher inside its payload and
cannot install the newly separated layout. Subsequent compatible updates use
the new launcher. The current private candidate does not create a public update
release or change what GitHub's latest-release endpoint returns.

For manual installation, extract the separate manual ZIP's `Halo_MCC_VR` folder
under the base MCC folder, or copy the complete build's `ModFiles` contents into
that folder. Preserve an existing `halomccvr.cfg` when updating manually. The
manual payload deliberately contains no launcher; manual-only users can use the
separately supplied launcher executable when they need the launch/install UI.
See `MANUAL-README.txt` for the exact copy/launch procedure. Administrator mode
does not provide code signing or eliminate SmartScreen warnings.

No installed MCC files were replaced and MCC was not launched while preparing
this candidate. Nothing is published to GitHub by this preparation step.

## Trying the experimental avatars

Enable **Tracked full-body IK (experimental)** in Body / Quality of Life.
It is off by default. H3 IK takes precedence over the legacy Show full body
setting so that setting cannot remove the tracked FP wrist data. Start in H3
as Chief and in H4 as Chief; check looking down, both hands, support grip,
left-handed mode, physical crouch, turning/walking, weapon changes and reloads.
Enable Hide lower body if preferred. H3 Elite/Arbiter require that option off
because their torso and legs share geometry. Unknown rigs keep the normal FP
view. Fingers on a held gun keep native grips; free hands use physical input.
These avatars still require headset validation; no new candidate is accepted
just because offline checks pass.

## Current limitations that matter when testing

| Feature/report | Status |
| --- | --- |
| Subtitles | VR overlay/placement in H2/H3/ODST/Reach/H4; CE localized handoff still research. Caption lifetime/visibility reports are not declared universally fixed. |
| Bloom | Controls in H3/ODST/Reach; other titles retain native bloom. General autoexposure and every visual report are not solved by this control. |
| DLSS | Optional six-title integration including CE/H2 graphics modes; performance, temporal quality and all-title transitions need GPU/headset testing. No frame generation. |
| Physical crouch | Implemented, off by default; depth/sensitivity/calibration available. Requires native hold-to-crouch. |
| First-person vehicles | Six per-game and model/seat offset banks, including new CE model identity. Identical CE node-name rigs share a profile. H2 interpolation change is not proof all stutter is gone. |
| Native input independence | All six games have guarded direct action bridges with per-game mappings and Unbound. Unsupported/failed binding proof retains the stated native-layout fallback; runtime acceptance is pending. |
| Roomscale | Guarded movement transport now includes CE Classic/Anniversary. Retained movement catches up after native movement settles; simultaneous stick/physical following remains unfinished. |
| Body representation | Separate tracked arms; experimental H3/H4 full-world avatars and title-specific free-hand controller gestures. CE/H2/ODST/Reach world avatars remain unfinished. Hide lower body is supported only by rigs with separate leg geometry; H3 Elite/Arbiter require it off. Headset testing remains pending. |
| Co-op/crashes | Campaign firing/melee/vehicle, CE host/client and new ODST crash reports remain unresolved where failing evidence is absent. |
| Visual reports | CE lights/arms, H2 lights/AO, Reach smoke/foliage and H4 blackout/letterbox reports remain individual open investigations. |
| Alyx-style hand physics | Investigated, not implemented: current whole-hand collision lacks per-joint contact limits and an independent stationary query transport. No nonfunctional toggle included. |
| New proposed features | Per-weapon grip capture/policies and Provolver support were not supplied as completed integrations in the reviewed contribution. |
| Linux/Proton | Shell/login focus override corrected and mode/message policy tested offline. Forced login and Proton keyboard behavior remain unverified; this is not a claim of full Linux support. The official Proton MCC login workaround and its limits are documented in LINUX-LOGIN-REVIEW.md. |

The supplied Reach fault log was from f53f0bd. Its caught SafeFrame read is
followed by title transitions/unload and a stall; that is not proof the read
caused a fatal crash. This candidate does not hide that distinction or assign
every transition failure the same cure.

## Credits

**Pancreations** created the original MCCVR project and DLSS foundation.
**Gab_dC / gabrieldch** supplied the newer virtual stock, free two-hand,
persistent support, coherent aim and telemetry contribution in
[PR16](https://github.com/moistman42069/MCCVR-Halo-Build/pull/16).
**martysl1** supplied subtitle/UI/bloom work and the Reach DLSS depth-path lead.
**Godoy** provided extensive per-title testing and feature/calibration reports.
**UnsealedWings** supplied performance and HUD proposals; weaker cleanup and
unproven heuristic discovery were reviewed but not substituted for current code.
**LivingFray / Maso** supplied ownership/restoration design leads through PR153,
and **marcusau2** contributed the earlier per-gun calibration work.
**PurplePeanut** proposed Provolver support, still an integration lead.

Thanks to all reporters recorded in the September 23 and September 30 audits,
including skivy44, Vixel, Chronomize and the fresh ODST, startup and input reports.
Contributor runtime results are credited without presenting them as acceptance
of these newly rebuilt bytes. Original licenses and the OFL Oxanium font license
remain included.

## Validation boundary

The cumulative Release build, **121/121 automated suites** and Reach consistency
gate passed before packaging. The package runner repeats the required checks
from the clean source commit; its manifest and checksum file identify the exact
delivered bytes. Source-level corrections and synthetic fault tests are not
headset or Proton acceptance.

Delivery requires the cumulative Release build, automated tests, Reach
consistency checks, synthetic installer/update checks and archive/hash/source
verification. These establish offline correctness only. Test shared changes in
the target titles and Halo 3, both handedness modes, grab/release/weapon swaps,
vehicles, death/checkpoint/title transitions and the relevant co-op modes before
calling the new candidate accepted. Record edition, runtime, headset, refresh,
source/configuration and the exact reproduction with any report.
