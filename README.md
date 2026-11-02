# Halo MCC VR (NitsuJack's builds)

OpenXR VR for Halo: The Master Chief Collection (Steam, and the Microsoft Store / Xbox app edition), and for
Halo 3 through [Project Reclaimer](https://projectreclaimer.dev). This fork builds on **moistman42069**'s
[Halo MCC VR](https://github.com/moistman42069/MCCVR-Halo-Build), itself a continuation of
**Pancreations**' [Halo-MCC-VR](https://github.com/pancreations/Halo-MCC-VR). Its builds are community test
builds, not official Halo MCC VR releases.

**Report problems with these builds here or in the
[Discord](https://discord.com/channels/747967102895390741/1546190376405049415), not on the upstream project or
to Project Reclaimer.** This fork is not made by or affiliated with the Project Reclaimer team.

## Videos

Special thanks to SilentVortigaunt for the trailer!

<a href="https://www.youtube.com/watch?v=cD3kzor54ZA"><img src="https://img.youtube.com/vi/cD3kzor54ZA/maxresdefault.jpg" alt="Trailer by SilentVortigaunt" width="640"></a>

Project Reclaimer and Vehicles Footage:

<a href="https://www.youtube.com/watch?v=0fwsnarAVTo"><img src="https://img.youtube.com/vi/0fwsnarAVTo/maxresdefault.jpg" alt="Project Reclaimer and vehicles footage" width="640"></a>

Some multiplayer footage (VR + NonVR players!)

<a href="https://www.youtube.com/watch?v=75Bxgvhl_xY"><img src="https://img.youtube.com/vi/75Bxgvhl_xY/maxresdefault.jpg" alt="Multiplayer footage, part 1" width="400"></a>
<a href="https://www.youtube.com/watch?v=L0wlvSYYtPA"><img src="https://img.youtube.com/vi/L0wlvSYYtPA/maxresdefault.jpg" alt="Multiplayer footage, part 2" width="400"></a>

*Click a picture to watch the video on YouTube.*

## Download

Get the newest build from the **[Releases page](https://github.com/MOTYSHIZ/MCCVR-Halo-Build/releases)**. Each
release's notes say what is new, what changed and what is known not to work yet. Every release has:

- a **Launcher** ZIP: the launcher plus the complete mod (`ModFiles`). Most players want this one.
- a **Manual** ZIP: a ready-to-copy `Halo_MCC_VR` folder, launcher included.
- a **Source** ZIP with the matching source, and `SHA256.txt` with the checksums.

## You need

- MCC installed (Steam, or the Microsoft Store / Xbox app edition). Project Reclaimer also needs MCC installed:
  it runs MCC's own Halo 3.
- A PC VR headset on an OpenXR runtime (SteamVR, Meta/Oculus, and others), connected before you launch.
- MCC played with anti-cheat disabled. The launcher starts it that way itself. Don't use the mod in MCC
  matchmaking.

## Install

Close MCC first. If you already have the mod, keep a copy of your `Halo_MCC_VR` folder.

**With the Launcher ZIP**

1. Extract the whole ZIP and run `HaloMCCVRLauncher.exe` (it sits beside `ModFiles`).
2. Choose your MCC install, or browse to the **base MCC folder**, for example
   `C:\Program Files (x86)\Steam\steamapps\common\Halo The Master Chief Collection` (not `Binaries\Win64`).
3. Press **INSTALL VR MOD** (**INSTALL / UPDATE FROM THIS PACKAGE** when a build is already installed). The mod
   goes into `Halo_MCC_VR` directly inside the MCC folder, and your existing settings are kept.
4. Press **LAUNCH MCC**, or **LAUNCH PROJECT RECLAIMER** in its own section (below).

If Install says access was refused (MCC is in a folder Windows protects), right-click the launcher, choose
**Run as administrator**, install, then close it and open it normally to launch. Don't launch from an
administrator launcher: the game would run as administrator too, and then ignores an OpenXR runtime chosen with
`XR_RUNTIME_JSON`.

The launcher's **CHECK FOR UPDATES** and **RELEASE PAGE** buttons follow the upstream project's releases, not
this fork's: for this fork's builds, use the Releases page above.

**With the Manual ZIP**

Copy its `Halo_MCC_VR` folder directly inside the base MCC folder. When updating, keep your own
`halomccvr.cfg`. Then run the `HaloMCCVRLauncher.exe` inside that folder. Starting MCC from Steam or the Xbox
app does not load the mod: start it from the launcher.

**Windows Defender** may flag the launcher (`Behavior:Win32/DefenseEvasion.A!ml`), most likely because it loads
the mod into the game. Check your ZIP against the release's `SHA256.txt` and allow just that item.

## Project Reclaimer

1. Install as above, and run Project Reclaimer once on its own first.
2. In the launcher's **Project Reclaimer** section, press **Detect** (it picks the version you ran last), or
   **Browse** to `project-reclaimer-client-v<version>.exe`.
3. Press **LAUNCH PROJECT RECLAIMER**.

The launcher turns on Project Reclaimer's *Centered crosshair* (so your hand can take over its aim) and changes
nothing else in its settings. For a sharp picture, set Project Reclaimer's *Render resolution* to **Native**:
it then renders at the size chosen on the mod's F1 > Picture page, from its next start. Project Reclaimer
closes the game on any memory fault, even one the mod would survive in MCC; if it closes on you, see below.

## In the headset

- **F1** opens and closes the VR settings; point your main-hand controller at the panel and use its trigger.
- **F3**, or **L3 + R3** together, recenters (L3 + R3 also toggles the settings panel).
- VR controllers act as an Xbox controller. The settings panel has the per-game mappings, and each release's
  notes list new controls (Halo 3's vehicle cameras, for example).

## Reporting a problem

Say which release you run, the game and mission, Steam or Store (or Project Reclaimer and its version), your
headset and OpenXR runtime, and what you did. Attach from your `Halo_MCC_VR` folder:

- `HaloMCCVR.log`, `HaloMCCVR-native-faults.log` and `HaloMCCVRLauncher.log`;
- for Project Reclaimer, also `HaloMCCVRReclaimerRelay.log` and Project Reclaimer's
  `Documents\My Games\Project Reclaimer\Logs\client.log`;
- if the headset froze or the PC restarted, SteamVR's `Steam\logs\vrcompositor.txt`.

Logs can contain your Windows user name and folder paths: look them over before posting them publicly.

## Credits

### The project

| Who | What |
|---|---|
| **Pancreations** | The original [Halo-MCC-VR](https://github.com/pancreations/Halo-MCC-VR), and its DLSS foundation |
| **moistman42069** | Maintains and extends [Halo MCC VR](https://github.com/moistman42069/MCCVR-Halo-Build), the project these builds are based on |
| **NitsuJack** | This fork's builds: aim, multiplayer and roomscale work, Project Reclaimer support, and the vehicle cameras ported from Halo Campaign Evolved UEVR |

### Contributions

| Who | What |
|---|---|
| **Gab_dC / gabrieldch** | Virtual stock, two-hand and support handling, coherent aim and telemetry ([upstream PR #16](https://github.com/moistman42069/MCCVR-Halo-Build/pull/16)) |
| **martysl1** | Subtitle, UI and bloom work, and the Reach DLSS leads |
| **LivingFray / Maso** | Ownership and restoration design leads |
| **marcusau2** | The earlier per-gun calibration |
| **blindcowboy24** | The first roomscale setup, written for Halo Campaign Evolved UEVR and ported here |

### Testing and proposals

| Who | What |
|---|---|
| **Godoy** | Testing and calibration reports |
| **UnsealedWings** | Performance and HUD proposals |
| **PurplePeanut** | The Provolver proposal |

Proposals are credited as reviewed: not every suggestion has been implemented.

If you'd like to support NitsuJack's mod contributions: https://ko-fi.com/nitsujack

The project keeps its [MIT license](LICENSE); third-party NVIDIA and Oxanium components keep their included
licenses.
