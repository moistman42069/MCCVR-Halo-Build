# Halo MCC VR

An independently maintained continuation of [Halo-MCC-VR by pancreations](https://github.com/pancreations/Halo-MCC-VR), maintained here by **moistman42069**. Original contributor credit, project history and the MIT license are preserved.

## Latest release: Alpha 0.6.0 — Major QoL Experimental Prerelease

This cumulative test release adds a new stylized installer and updater, reorganized VR settings, optional physical crouch, simulated gunstock, subtitle controls in five titles, bloom controls in three titles, DLSS integration across all six games, and vehicle, weapon and rendering refinements. It supports Steam and Microsoft Store / Xbox app editions.

> **Experimental prerelease.** Offline builds and tests do not establish headset acceptance. Some changes need in-game testing, and several co-op, title-specific rendering and input issues remain open. Review the [complete release notes](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.6.0) and the included implementation status before updating. Keep your previous build available for rollback.

### Downloads

- [Player installer ZIP](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.6.0/Halo-MCC-VR-0.6.0-Installer.zip)
- [Matching source ZIP](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.6.0/Halo-MCC-VR-0.6.0-Source.zip)
- [SHA-256 checksums](https://github.com/moistman42069/MCCVR-Halo-Build/releases/download/MCC_VR_ALPHA_0.6.0/SHA256.txt)

The installer archive includes `HaloMCCVRLauncher.exe` and the complete `ModFiles` payload for manual installation. The launcher detects Steam and Store installs, or lets you browse to the **base MCC folder**; it installs into `Halo_MCC_VR` directly inside that folder. It can retain and extend your existing config, verify files, back up replaced mod files and roll back failures. Launch and GitHub update installation are separate, explicit actions. The launcher uses the bundled OFL-licensed Oxanium font.

### Included changes

- Optional NVIDIA DLSS integration across CE, H2, H3, ODST, Reach and H4, including both CE and H2 graphics modes. Pancreations' integration and martysl1's Reach depth-path correction are included. If required data or GPU support is unavailable, the normal renderer remains available.
- Native localized subtitle overlay and gameplay/theatre position and scale controls in H2, H3, ODST, Reach and H4.
- Per-game bloom controls in H3, ODST and Reach, with resource bounds and restoration. Bloom remains enabled by default; other games use native bloom.
- Optional simulated gunstock with head, shoulder, chest and adaptive references, strength and proximity release.
- Physical crouch toggle, activation-depth adjustment, calibration and title-specific read-only camera corrections. It is off by default and requires MCC hold-to-crouch.
- Separate primary and secondary crosshairs in H2 Classic/Anniversary and H3, following the corresponding weapons and handedness.
- Per-game vehicle camera XYZ settings in all six titles; stable model/seat profiles in H2, H3, ODST, Reach and H4. H2 selects interpolated marker nodes when smoothing is enabled.
- Per-game H2 Original and H4 muzzle-effect hide options, with a broader first-person particle suppression fallback in H2.
- Shared controller-profile-aware VR action defaults, per-game overrides and Unbound, with a dedicated mappings page. Flashlight and support-grip arbitration was refined.
- Wider, reorganized settings pages, wrapped labels, resize status, retained-resource resize handling, rollback and more detailed allocation diagnostics.
- Roomscale movement debt is retained during stick movement and applied after native movement settles, with recenter, teleport and tracking-loss guards.

### Known limits

- CE localized subtitle placement and persistent per-vehicle profiles are unfinished. CE has per-game vehicle offsets and native subtitles.
- VR bindings still use verified MCC native transports. Actions sharing a native button can remain coupled; Unbound is not full native action independence.
- Roomscale uses delayed catch-up, not simultaneous body following during stick locomotion. CE body following remains unsupported.
- H2 interpolation addresses one verified marker timing mismatch; complete vehicle stutter removal has not been confirmed. Vehicle positioning and seat transitions need headset testing in every title.
- Co-op firing/crashes and several multiplayer ownership, transition, custom-content and title-specific visual reports remain unresolved. The release notes and `IMPLEMENTATION-STATUS.md` list them individually.
- DLSS moving-object ghosting is possible; frame generation is not included. CE zoom retains native behavior; the separate CE VR scope is unfinished.
- UnsealedWings' older cleanup replacement, broad heuristic HUD discovery and unverified texture-array performance claims were reviewed but not integrated. Existing asynchronous wait and stronger cleanup remain. Adaptive cleanup backoff also remains unvalidated.
- This build has passed offline checks only; it has not received headset acceptance. Keep anti-cheat disabled and do not use this mod in matchmaking.

### Install and update

1. Close MCC and extract the full installer ZIP.
2. Open `HaloMCCVRLauncher.exe`.
3. Select the detected MCC edition or browse to the base MCC folder. Installation creates `Halo_MCC_VR` directly beneath that folder, beside the game directory structure (not inside `Binaries/Win64`).
4. Keep configuration retention enabled to preserve current values while adding new defaults. Replaced mod files are backed up.
5. Launch MCC from the launcher when ready. Update checking and installation are separate explicit actions.

For manual installation, copy the contents of `ModFiles` into `Halo_MCC_VR` under the base MCC folder. Keep the existing `halomccvr.cfg` when updating. See `MANUAL-README.txt` in the archive.

## Project and validation

The full build commit and artifact hashes are recorded in `BUILD-IDENTITY.txt` and `SHA256.txt`. The Release build, 83 automated test suites, Reach consistency gate, package/source hash verification and synthetic Steam/Store installer tests passed. This does not verify headset behavior or resolve the open reports listed above. The accepted runtime pointer remains Alpha 0.4.2 until headset acceptance; this prerelease must not be treated as a replacement for the accepted baseline.

See `RELEASE-NOTES.md` and `IMPLEMENTATION-STATUS.md` in the installer ZIP for detailed coverage, WIP items and unresolved reports. Source and evidence documents are included in the matching source ZIP.

## Earlier releases

- [Alpha 0.5.1 — CE Multiplayer Tracking Experimental Prerelease](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.5.1)
- [Alpha 0.5.0 — Experimental Refinement Update](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.5.0)
- [Alpha 0.4.2 — Quick Patch Update](https://github.com/moistman42069/MCCVR-Halo-Build/releases/tag/MCC_VR_ALPHA_0.4.2)

