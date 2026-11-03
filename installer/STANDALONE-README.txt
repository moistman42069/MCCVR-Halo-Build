HALO MCC VR - LAUNCHER
======================

This launcher installs and starts Halo MCC VR, an OpenXR VR mod for Halo: The
Master Chief Collection on Windows (Steam and Microsoft Store / Xbox app). The
mod itself is not in this ZIP: the launcher downloads it from the project's
GitHub releases and checks it before installing.

GET STARTED
-----------
1. Close MCC and extract this ZIP anywhere.
2. Open HaloMCCVRLauncher.exe. It does not ask for administrator access. If
   Windows refuses the install folder, run it once as administrator to install,
   then open it normally.
3. Under 01 / GAME INSTALLATION, choose your MCC. Use BROWSE if it isn't listed.
4. Under 02 / RELEASE UPDATES, choose a repository or press LATEST, pick a
   version, then press the install button. MCC stays closed.
5. Press LAUNCH MCC when your headset is ready.

Use MCC with anti-cheat disabled, and do not use the mod in matchmaking.

The mod installs into a Halo_MCC_VR folder inside your MCC folder, and a copy of
the launcher goes there too. Later updates also update the launcher you opened.

IF YOUR ANTIVIRUS FLAGS THE LAUNCHER
------------------------------------
Windows Defender or another antivirus may flag HaloMCCVRLauncher.exe, for
example as Behavior:Win32/DefenseEvasion.A!ml. The launcher is not code-signed,
it downloads files, and it loads the mod into MCC, which is the kind of behavior
these tools watch for.

Before allowing it, make sure you got this ZIP from the project's release page
and that its SHA-256 matches the SHA256 list published with the release. Then
allow just that item rather than turning your antivirus off. If it doesn't
match, delete it and report it.

LICENSES
--------
LICENSE                   Halo MCC VR (MIT)
THIRD-PARTY-LICENSES.txt  Third-party components
OFL-Oxanium.txt           The Oxanium font built into the launcher (SIL OFL 1.1)
