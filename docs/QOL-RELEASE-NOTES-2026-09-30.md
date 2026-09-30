# MCCVR September 30 community candidate

Local test candidate after Alpha 0.6.0. **Not headset accepted, not a claim that
every reported bug is fixed.** Exact source and file hashes are in the included
candidate manifest and BUILD-IDENTITY.txt. The accepted pointer remains Alpha
0.4.2 while this larger candidate is evaluated.

## Changes in this candidate

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

## Current limitations that matter when testing

| Feature/report | Status |
| --- | --- |
| Subtitles | VR overlay/placement in H2/H3/ODST/Reach/H4; CE localized handoff still research. Caption lifetime/visibility reports are not declared universally fixed. |
| Bloom | Controls in H3/ODST/Reach; other titles retain native bloom. General autoexposure and every visual report are not solved by this control. |
| DLSS | Optional six-title integration including CE/H2 graphics modes; performance, temporal quality and all-title transitions need GPU/headset testing. No frame generation. |
| Physical crouch | Implemented, off by default; depth/sensitivity/calibration available. Requires native hold-to-crouch. |
| First-person vehicles | Six per-game offset banks; per-model/seat banks in H2/H3/ODST/Reach/H4. CE persistent vehicle identity unfinished. H2 interpolation change is not proof all stutter is gone. |
| Native input independence | Per-game VR mappings and Unbound exist; native action aliases can still couple actions. |
| Roomscale | Retained movement catches up after native movement settles; simultaneous body follow and CE body follow remain incomplete. |
| Co-op/crashes | Campaign firing/melee/vehicle, CE host/client and new ODST crash reports remain unresolved where failing evidence is absent. |
| Visual reports | CE lights/arms, H2 lights/AO, Reach smoke/foliage and H4 blackout/letterbox reports remain individual open investigations. |
| New proposed features | Per-weapon grip capture/policies and Provolver support were not supplied as completed integrations in the reviewed contribution. |
| Linux/Proton | Forced login and keyboard/focus reports remain unverified; Windows elevation does not establish Linux compatibility. |

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

Delivery requires the cumulative Release build, automated tests, Reach
consistency checks, synthetic installer/update checks and archive/hash/source
verification. These establish offline correctness only. Test shared changes in
the target titles and Halo 3, both handedness modes, grab/release/weapon swaps,
vehicles, death/checkpoint/title transitions and the relevant co-op modes before
calling the new candidate accepted. Record edition, runtime, headset, refresh,
source/configuration and the exact reproduction with any report.
