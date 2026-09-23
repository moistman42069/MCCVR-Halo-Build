# Launcher and installer

The existing Steam suspended-process launch and Microsoft Store packaged
activation code remains in `launcher.cpp`. A new menu selects an installation
and calls that same launch path only when the user selects **Launch MCC**.
Installation and update discovery do not start MCC.

Package layout:

```
HaloMCCVRLauncher.exe
ModFiles/
  HaloMCCVRLauncher.exe
  HaloMCCVR.dll
  halomccvr.cfg
  INSTALL-MANIFEST.sha256
  BUILD-IDENTITY.txt
  assets/fonts/Oxanium.ttf
  assets/fonts/OFL-Oxanium.txt
  assets/fonts/SOURCE.txt
  ...licenses, notes and other distributed assets...
```

The top launcher and `ModFiles` launcher must have identical bytes. `ModFiles`
remains the complete manual-install payload. Its manifest is UTF-8/ASCII, one
`SHA256  relative/path` per file, excluding the manifest itself. Paths are
relative to `ModFiles`; backslashes and forward slashes are supported. Do not
list directories, duplicate names, absolute paths or the manifest itself.
`BUILD-IDENTITY.txt` contains `source_commit=<40 hex>`,
`build_kind=UNTESTED_LOCAL_CANDIDATE` (or `PUBLIC_RELEASE`) and
`release_tag=<exact published GitHub tag, otherwise empty>`, one per line.
The menu displays the installed identity, describes discovery as the latest
public release, and labels replacement of a candidate as Install public release.

Detection reads Steam registry/library metadata and the current Windows
account's `Microsoft.Chelan_8wekyb3d8bbwe` package registration. A bounded
fixed-drive XboxGames/XBOX fallback and Browse cover additional libraries.
Each result must contain one of MCC's actual shipping executable layouts.
The installer always targets that root's dedicated `Halo_MCC_VR` folder.
It refuses active MCC processes, path junctions/symbolic links and corrupt
payload hashes. It never modifies the game's original files or elevates itself.

Existing files that are replaced are copied and verified under
`Halo_MCC_VR/backups/<unique timestamp>`. A replacement failure restores earlier
replacements from that backup. Unknown files and logs remain untouched. An
interrupted process/power loss is not an atomic directory transaction: keep the
backup and rerun the installer before launching if installation was interrupted.

Keep-settings defaults on. Existing settings, explicit Unbound values, comments
and unknown/custom keys are retained; absent new defaults are appended once.
Legacy `halo3xr.cfg` is imported if the primary file is absent. Migration marker
keys and calibration keys that still require runtime migration are not blindly
appended. Unchecking keep-settings installs this build's defaults, with the
previous primary config included in the same backup.

**Check for updates** reads only the latest stable release from
`https://api.github.com/repos/moistman42069/MCCVR-Halo-Build/releases/latest`.
It never uses the retired pancreations origin. **Install update** downloads the
single player ZIP over HTTPS and verifies GitHub's asset size and SHA-256 digest.
ZIP extraction rejects traversal, absolute paths, duplicate Windows names,
reserved device names, symlinks and excessive expanded sizes before extracting.
The downloaded package's `ModFiles` manifest is verified again during install.

Updates are cached under `%LOCALAPPDATA%/HaloMCCVR/Updates`. A verified copy of
the currently running installer waits for the current launcher to exit before
replacing files; after installation it opens the newly installed launcher menu.
This is an explicit latest-public-release action, not an automatic downgrade or
semantic-version claim for unpublished test candidates. Older public archives
without the new payload manifest, or releases without a GitHub SHA-256 digest,
require their manual instructions. The release page remains available.

The menu privately loads unchanged OFL-licensed Oxanium. An installed Halo font
can be used for the heading; no proprietary font or game artwork is bundled.
Bahnschrift is the system fallback. Both editions use the same menu and payload.

Validation: `halomccvr_installer_tests` exercises pure policy and synthetic
Steam/Store layouts, first install, cfg retention/reset, hashing, backups,
locked-destination rollback and strict GitHub JSON parsing.
`halomccvr_updater_archive_tests` exercises the real Windows ZIP extraction path
with valid and malicious local archives. Neither test launches or writes to a
real MCC installation. Native Steam/Store launch behavior still requires user
runtime testing; those paths were preserved, not runtime-accepted by these tests.
