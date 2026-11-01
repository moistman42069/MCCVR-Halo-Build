# Launcher and installer

The existing Steam suspended-process launch and Microsoft Store packaged
activation code remains in `launcher.cpp`. A new menu selects an installation
and calls that same launch path only when the user selects **Launch MCC**.
Installation and update discovery do not start MCC.

**Unattended launch (`--headless`, dev builds only).** A launcher built with
`HALOMCCVR_DEV_DIAGNOSTICS` (the `dev` preset) accepts
`HaloMCCVRLauncher.exe --headless`: it skips the menu and launches the mod in
the launcher's OWN folder (the menu's Launch always targets
`<game>\Halo_MCC_VR`, so a side-by-side test install is launched this way). It
never shows a dialog: every message, including what a dialog would have said,
goes to `HaloMCCVRLauncher.log`, and the exit code reports the result: 0 = the
game is running after 12 s, 2 = Steam edition and no `steam.exe` process, 1 =
any other failure. It never offers VR recovery for a running game, and on the
Store edition it does not wait for a manual start if activation fails. The test
harness (`tools/Enter-Reach-FA.ps1`) uses it so an unattended run cannot leave a
modal box on someone's desktop. The harness starts it with
`__COMPAT_LAYER=RunAsInvoker`, so no UAC prompt can appear whatever the
launcher's manifest asks for;
the launcher removes that variable before it starts the game. A player build
never parses the switch, and `tools/verify-qol-package.py` refuses a packaged
launcher that contains it.

The exit code 2 for a missing Steam applies in every build. So does logging:
every message box shown while a launch runs writes its full text to the log
(`ERROR shown to user: ...`, `NOTICE shown to user: ...`). Boxes shown before
a launch starts (the update helper, a failure of the menu itself) are not
logged, because the log belongs to the mod folder being launched.

Package layout:

```
HaloMCCVRLauncher.exe
ModFiles/
  HaloMCCVR.dll
  halomccvr.cfg
  INSTALL-MANIFEST.sha256
  BUILD-IDENTITY.txt
  assets/fonts/Oxanium.ttf
  assets/fonts/OFL-Oxanium.txt
  assets/fonts/SOURCE.txt
  ...licenses, notes and other distributed assets...
```

The installer/updater executable is a separate player download beside
`ModFiles`. The Windows manual archive includes the legacy game-launch/injection
helper within `Halo_MCC_VR`, but does not include the installer/updater UI. The
Linux compatible manual archive has the same payload files directly at ZIP root
to avoid an extra extraction folder. `ModFiles` remains the payload for manual
installation. Its
manifest is UTF-8/ASCII, one
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
payload hashes. The launcher runs with the user's own rights (its manifest is
`asInvoker`): the game it starts inherits its token, and an elevated game
ignores an OpenXR runtime chosen with `XR_RUNTIME_JSON`, so VR would not start
for a player who selects the runtime that way. If Windows refuses the install
folder, the installer says to run the launcher as administrator for that
install, and to open it normally afterwards to launch. The launcher executable
is installed as a separate file beside the mod payload inside `Halo_MCC_VR`;
it is not part of the source `ModFiles` tree; the separately packaged manual
archives provide the legacy launch helper for manual users.

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

## Project Reclaimer (experimental)

The menu's last section starts Project Reclaimer, a standalone client that runs MCC's `halo3.dll`, with the VR
mod. It needs `HaloMCCVR.dll` and `HaloMCCVRReclaimerRelay.dll` beside the launcher (or in its `ModFiles`
folder); the relay is not in a player package.

- **The path** of the game program (`project-reclaimer-client-v<version>.exe`, or `project-reclaimer-v<version>.exe`
  before 0.9.23) is typed, pasted, or picked with
  Browse. It is kept as the one line of `reclaimer_client.txt` beside the launcher and read back at the next
  start. Clearing the field forgets it.
- **Detect** looks, in this order, where the client's own join links point (it registers a `reclaimer:` link
  for the user the first time it runs: `Software\Classes\reclaimer\shell\open\command`), at a client that is
  running now, and in the folders around the launcher. With no path kept, the menu offers what Detect finds
  when it opens and keeps it only once it is used.
- **The line under the path** gives the client's version from the file itself (the client updates itself in
  place and keeps its first name) and whether the VR mod has been run with that version.
- **Launch Project Reclaimer** starts the client suspended, loads the relay and resumes it. Logs and settings
  are the launcher folder's own: `HaloMCCVRLauncher.log`, `HaloMCCVRReclaimerRelay.log`, `HaloMCCVR.log`,
  `halomccvr.cfg`.

`HaloMCCVRReclaimerLauncher.exe` is the same program as a one-click start: in a folder with the relay and a
client it can find (the kept path first), it starts that client with no menu. Where it finds none it opens
the menu. `HaloMCCVRLauncher.exe` always opens the menu.

Validation: `halomccvr_reclaimer_host_logic_tests` covers the path tidying and the join-link command. The menu
was drawn and read off screen; the launch from the menu's button has not been run.
