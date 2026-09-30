# Halo MCC VR

An independently maintained continuation of [Pancreations’ Halo-MCC-VR](https://github.com/pancreations/Halo-MCC-VR), maintained by **moistman42069**. MCCVR adds OpenXR VR support and quality-of-life features for Halo: The Master Chief Collection on Steam and Microsoft Store/Xbox app editions.

> **Alpha 0.7.0 is a work-in-progress community test prerelease.** It is being shared so players can test current changes, report bugs and provide feedback. It is not a polished or headset-accepted stable release; a more complete release will follow after testing. Keep a previous build available for rollback. A passing build/test suite does not prove behavior in every title, mission, controller, co-op session, headset or runtime.

## Downloads

Choose one player install method from the [Alpha 0.7.0 release page](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.7.0):

- **[Launcher installation](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.7.0/Halo-MCC-VR-0.7.0-Launcher.zip)** — stylized launcher plus the complete `ModFiles` payload. It can detect or browse to the MCC root, preserve and extend configuration, install/update with file checks and rollback, and launch the game on request.
- **[Manual drag-and-drop files](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.7.0/Halo-MCC-VR-0.7.0-Manual-Drag-and-Drop.zip)** — ready-to-copy `Halo_MCC_VR` folder, without the launcher executable.

For developers: [matching source ZIP](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.7.0/Halo-MCC-VR-0.7.0-Source.zip) and [SHA-256 checksums](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.7.0/SHA256.txt). The launcher’s automatic updater follows the latest stable public release; because Alpha 0.7.0 is a prerelease, get this test build from the release page.

## Install

**Launcher:** Close MCC, extract the whole Launcher ZIP, and run `HaloMCCVRLauncher.exe` beside `ModFiles`. Windows requests administrator access by default. Select a detected install or browse to the **base MCC folder**, such as `C:\Program Files (x86)\Steam\steamapps\common\Halo The Master Chief Collection`—not `Binaries\Win64`. The mod installs to `Halo_MCC_VR` directly beneath that folder. Keep configuration retention selected to preserve settings while new defaults are added. Install, update checking, update installation and game launch are separate choices.

**Manual:** Close MCC, extract the Manual ZIP, and copy its `Halo_MCC_VR` folder directly inside the base MCC folder. Preserve your existing `halomccvr.cfg` when updating. The Manual ZIP has no launcher. Users upgrading from the old 0.6.0 layout must run the complete new Launcher ZIP once because the old updater expects a launcher inside `ModFiles`.

Use MCC with anti-cheat disabled. Do not use MCCVR in matchmaking or replace original game files. Connect your OpenXR runtime before choosing Launch MCC. For the Cortana mission in Halo 3, see [this community guide](https://steamcommunity.com/sharedfiles/filedetails/?id=3567825314).

## Controls and bindings

VR controllers emulate an Xbox controller: A/B/X/Y map to A/B/X/Y, triggers to triggers, grips to bumpers, and thumbsticks to thumbsticks. Vive/WMR profiles use equivalent available inputs. Keyboard and physical gamepad input remain available.

- **F1** opens/closes VR settings; **F3** recenters. **L3 + R3 together** also recenters and toggles the VR menu. Aim your main-hand controller at the VR settings panel and use its trigger.
- **B + Y** toggles the in-game Start/menu action by default where the current title permits it; this chord can be disabled.
- The **VR Mappings** page offers Automatic, per-game overrides and Unbound. Six title-specific bridges read each game’s verified native mappings, including correction of Reach’s former trigger/X swap. VR overrides are stored per game.
- Native MCC actions may share a button/action. For example, Reload and Use can remain coupled in MCC even when their VR sources have separate choices. Unbound only affects MCCVR’s VR source; it does not disable keyboard/gamepad input or another mod. If a mapping reader cannot be proven, that title keeps its documented native fallback.
- Optional physical gestures have separate input ownership; unbinding a gameplay action does not necessarily disable its corresponding gesture.

Controller-profile touch/near-head gestures vary by runtime. Check the in-game mapping/settings pages and the [release notes](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.7.0) for current behavior and limitations.

## What is in this test build

The cumulative candidate supports CE, H2, H3, ODST, Reach and H4, with both CE and H2 graphics modes. It includes optional DLSS paths, subtitle placement controls in H2/H3/ODST/Reach/H4, bloom controls in H3/ODST/Reach, physical crouch, simulated gunstock and two-hand handling, weapon alignment/reload/holster controls, vehicle offsets/profiles, H2/H3 dual reticles, H2 Original/H4 muzzle-hide fallbacks, roomscale, physical melee/world contact, hand-specific haptics and telemetry. Their title coverage and acceptance limits differ; see the full [implementation status and WIP ledger](docs/QOL-IMPLEMENTATION-STATUS-2026-09-30.md) in the matching source tree or release payload.

New Alpha 0.7.0 work includes experimental Halo 3 Chief/Elite/Arbiter and Halo 4 Chief tracked world avatars; individual finger-joint identification in admitted rigs for all six games; title-specific free-hand poses; separate arms-with-hands controls; six title-specific native action bridges; CE recovery-reference and vehicle identity corrections; CE roomscale participation; physical running; optional HUD reveal gesture; live-resolution retry/depth-cache work; and native shell/login focus restoration relevant to Linux reports.

To try full-body IK, enable **Tracked full-body IK (experimental)**; it defaults off. Begin with H3 Chief or H4 Chief. For H3 Elite/Arbiter, keep Hide lower body off because those models share torso/leg geometry. New avatar and finger paths need headset validation.

## Known limits and planned work

- Full-world avatars in CE, H2, ODST and Reach are unfinished. H3 Elite/Arbiter lower-body hiding is unsupported. CE/H2/Reach open-hand bind resets and H2 finger naming are partial.
- Individual finger identity is implemented; Alyx-style finger contact reactions/hand physics are not. Whole-hand collision does not provide the per-joint contact behavior required.
- CE localized subtitle handoff/separate VR scope remain unfinished. CE/H2/H4 bloom and generalized auto-exposure coverage remain incomplete.
- DLSS quality, ghosting, resizing and performance vary by game/GPU and require headset testing; it is not a frame-generation feature or FPS guarantee.
- Physical crouch requires MCC hold-to-crouch. Simultaneous stick and roomscale body following is incomplete. Reach native weapon recoil haptics remain WIP.
- CE gun shift after loads, Linux forced sign-in/keyboard, ODST crashes, campaign co-op firing/melee/vehicles, H2 stutter/blackouts, CE Anniversary lighting/arms, Reach foliage/smoke/title-hop slowdown and H4 blackouts/letterbox have no confirmed universal fix in this candidate.
- Planned, not implemented: dropped-world-weapon pickup and enemy disarm; grabbing/ragdolling/throwing enemies; sustained physical enemy contact; climbing/world grip/wall jumps; mandatory physical weapon grip with dropping/pickup; touch-driven interactibles; optical hand tracking; Provolver SDK support; and per-weapon grip capture.

Read the [complete September 30 implementation ledger](docs/QOL-IMPLEMENTATION-STATUS-2026-09-30.md), [release notes](docs/QOL-RELEASE-NOTES-2026-09-30.md), [finger-joint coverage](docs/FINGER-JOINT-IDENTITY-2026-09-30.md), and [Linux/Proton focus review](docs/LINUX-PROTON-LOGIN-FOCUS-REVIEW-2026-09-30.md) for title matrices, known issues, exact WIP/planned scope, and community credits. The release ZIPs also contain the status and previous implementation ledger.

## Testing and bug reports

The Release build, 121/121 automated test suites, Reach consistency gate, synthetic Steam/Store installer checks, and source/archive hash checks passed. These checks are not headset, Proton or co-op acceptance. When reporting, include build identity; title/mission and graphics mode; Steam or Store; headset/controller profile/OpenXR runtime/refresh; host or client and peer builds; exact steps/settings; stock comparison; and relevant log or crash dump. Please say whether the behavior changes with the related experimental feature disabled. Avoid posting private logs publicly without reviewing them.

Join the [Flat2VR Discord](https://discord.gg/flat2vr) for community support and other VR mods.

## Credits

Original project and DLSS foundation: **Pancreations**. Adapted virtual stock, two-hand, support, coherent aim and telemetry: **Gab_dC / gabrieldch**, [PR #16](https://github.com/moistman42069/MCCVR-Halo-Build/pull/16). Subtitle/UI/bloom and Reach DLSS leads: **martysl1**. Testing and calibration reports: **Godoy**. Performance/HUD proposals reviewed: **UnsealedWings**. Ownership/restoration design leads: **LivingFray / Maso**, PR #153. Earlier per-gun calibration: **marcusau2**. Provolver proposal: **PurplePeanut**. Community reporters and exact attribution are listed in the implementation ledger. Reviewed proposals are credited without implying that every suggestion was implemented or that its reported issue is resolved.

The project retains its [MIT license](LICENSE); third-party NVIDIA and Oxanium components retain their included licenses.

## Earlier releases

- [Alpha 0.6.0](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.6.0)
- [Alpha 0.5.1](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.5.1)
- [Alpha 0.5.0](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.5.0)