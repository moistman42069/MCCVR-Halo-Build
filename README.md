### REACH/3/ODST Now Playable in VR 
### First-person vehicles are here — in all three games.

> # ⚠️ Before you drive: you have to set up your own seats
>
> Every seat ships with a starting camera position, but that is a **starting
> point, not a finished setting**. Everyone's height, play space and headset sit
> differently, so a seat that looks right for me will be too low, too far
> forward or too far back for you. **This is normal and it is a one-time job per
> seat.**
>
> 1. Get into the seat you want to fix — driver, passenger, gunner or turret.
> 2. Press **F1** and open the **Vehicles** category.
> 3. Move **Seat forward (m)**, **Seat height (m)** and **Seat left / right (m)**
>    until it feels right.
> 4. Get out. It saves itself.
>
> While you're **in a seat**, those sliders move **that seat only** — a
> vehicle's driver, its passengers and its gunner each remember their own spot,
> in each game. Do the ones you actually use and ignore the rest.
>
> While you're **on foot**, the same sliders set the shared starting point every
> seat you haven't adjusted follows. That one moves all three games at once, so
> use it for a quick overall height nudge. If you push it somewhere bad, a
> **Reset the universal trim** button appears under the sliders.

## 🆕 What's new in 0.3.3

1. **First-person vehicles in ODST and Reach** — the Halo 3 vehicle mode from
   0.3.2 now covers all three games. Sit in the seat instead of floating behind
   the vehicle, with your hands on the wheel, your own body hidden, and every
   seat placed: drivers, passengers, gunners and turrets.
2. **Turrets hold still and shoot where you're pointing** — mounted turrets in
   Halo 3 and ODST used to twitch up and down chasing the crosshair. They sit
   still now, and the reticle rides the gun barrel, so what you see is where the
   shot goes.
3. **Passenger guns shoot down the sight line** — riding shotgun in a Warthog,
   your shots now leave the line you're actually aiming down at every range,
   instead of only lining up at one distance.
4. **The brightness slider works in ODST and Reach** — `game_brightness` used to
   move Halo 3 only. One slider now moves all three games.
5. **No more loading a level twice** — the mod no longer touches a game while
   its level is still loading, which is what used to bounce you back to the main
   menu on the first load.
6. **Faster into VR** — ODST reaches stereo about two seconds sooner, and the
   startup scan every game does is roughly 7× faster.
7. **Your seat settings can't be wiped any more** — an unrecognised vehicle used
   to write over the shared setting that all three games fall back on. Each
   vehicle now keeps its own line, and there's a one-click reset if the shared
   one ever looks wrong.

## 🚧 What I'm working on RN
| Feature                                            | Status         |
| -------------------------------------------------- | -------------- |
| Xbox App / Game Pass Support                       | ✅ Added in 0.3.1 |
| ALVR / Virtual Desktop / Meta Link Support         | ✅ Fixed in 0.3.1 |
| Cutscene 3d Theater Mode                           | ✅ Added in 0.3.1 |
| Complete F1 Menu UI Restructure and Reorganization | ✅ Added in 0.3.1 |
| Reach: black-world fix on sniper                   | ✅ Fixed in 0.3.1 |
| Halo 3 first-person vehicles                       | ✅ Added in 0.3.2 |
| ODST first-person vehicles                         | ✅ Added in 0.3.3 |
| Reach first-person vehicles                        | ✅ Added in 0.3.3 |
| Turret aim + brightness in ODST/Reach              | ✅ Fixed in 0.3.3 |
| Halo 4                                             | 🟡 Up next |
| SMAA T2X Support                                   | not possilbe atm |
| Gamepad support/Head-Aiming Mode                   | 🟡 In Progress |
| Scopes and weapon zoom fixes                       | 🟡 In Progress |
| Theater mode subtitles                              | 🟡 In Progress |

> **DO NOT ACCEPT FIXES FOR THIS MOD FROM OUTSIDE SOURCES. WE DON'T KNOW WHAT'S
> IN THEM.**
>
> [Quad-Views-Foveated](https://github.com/mbucchia/Quad-Views-Foveated) is
> **optional and no longer required** — try it only if you want a GPU
> performance boost.
>
> **Steam VR players: use the SteamVR Beta branch**, with SteamVR set as your
> default OpenXR runtime — that's what makes Virtual Desktop's VDXR mode work.
>
> **WINSTORE/XBOX APP: do not rename your game .exe to the Steam version.** The
> launcher now looks for the real Game Pass executable — if you renamed
> `MCCWinStore-Win64-Shipping.exe` in a previous update, rename it back.
>
> Compatibility across headsets may vary. If it doesn't work, try a different
> way to connect if you can. or leave an issue with your spec set up. I'll try to find a play tester with your headset.
>
> **Let me know in the [issues](https://github.com/pancreations/Halo-MCC-VR/issues)
> ** and someone or I can help you.
>
> **Please list your specs if you're having issues.** Saves me a bit of time

# Halo MCC VR

> **Hi, I'm [pancreations](https://www.instagram.com/pancreations/)** — a 3D
> animator. I really don't like AI art, but I also really want to play MCC in
> VR, so I'm taking one for the team. Follow me on Instagram if you like silly
> animations made by humans in Blender. 
>
>
A native OpenXR VR mod for Halo 3, Halo 3: ODST and Halo: Reach in Halo: The
Master Chief Collection — both the Steam edition and the Microsoft Store / Xbox
app (Game Pass) edition.

The current release is
[FP Vehicle Update — MCC VR Alpha 0.3.3](https://github.com/pancreations/Halo-MCC-VR/releases/tag/MCC_VR_ALPHA_0.3.3).
It brings **first-person vehicles to all three games**: ODST and Reach join
Halo 3, with every seat placed — drivers, passengers, gunners and turrets — plus
vehicle-relative hands, body hiding, seat recentering, optional view follow, and
per-seat tuning in the F1 menu. It also stops turrets twitching, puts passenger
and turret shots on the sight line, makes the brightness slider work in ODST and
Reach, and fixes having to load a level twice. It is an alpha: use it at your
own risk, launch only without anti-cheat, and expect incomplete hardware and
gameplay coverage.

## What works

- Per-eye stereo and 6DOF head tracking.
- Motion-controller input, weapon aim, arm IK, snap/smooth turning, melee,
  grenades, and menu control.
- Native HUD, authored floating weapon crosshair, scopes, resolution scaling,
  comfort controls, and a shared F1 configuration menu.
- Halo 3 campaign behavior, including cutscenes, pause/resume, death/respawn,
  and mission transitions.
- **First-person vehicles in all three games**: a cockpit-style seat view,
  stable vehicle-relative hands, optional body hiding, seat recentering, an
  optional ground-vehicle view-follow mode, and per-seat position sliders in the
  F1 menu. Halo 3 was new in 0.3.2; ODST and Reach are new in 0.3.3. Drivers,
  passengers, gunners and mounted turrets are all covered, including the
  Warthog's chaingun/gauss/rocket variants, Scorpion, Wraith, Ghost, Revenant,
  Banshee, Falcon, Mongoose, Chopper, Prowler, Hornet and the Covenant Shade
  turrets.
- Turret seats hold still instead of twitching, and the reticle rides the gun
  barrel so shots go where the crosshair is. New in 0.3.3.
- ODST stereo, controls, weapons/hands, native HUD, cutscenes, vibration,
  death/respawn recovery, and first-person vehicles.
- Halo: Reach stereo, controls, weapon aim, hands/gun, HUD, cutscenes and
  vibration, plus native vehicle controls and a fix for sniper-triggered black
  static-world geometry, both new in 0.3.1, and first-person vehicles new in
  0.3.3. Reach is new in 0.3.0 and is the earliest of the three titles.
- The `game_brightness` slider moves all three games. New in 0.3.3 — it used to
  affect Halo 3 only.
- Both MCC editions from one download: Steam, and Microsoft Store / Xbox app
  (Game Pass). All three titles work on either, and no game file is renamed.
- A room-fixed 3D cutscene theatre for all three games, new in 0.3.1: cutscenes
  play on a screen that stays put in the room instead of riding your head, with
  the authored cinematic framing and field of view preserved. Controlled by
  `cutscene_theater_depth` and Flip Depth in the F1 menu.
- L3+R3 recenters VR space and toggles the F1 menu; Y+B is the pause chord in
  ODST and Reach. Both new in 0.3.1.
- A rebuilt F1 menu: sidebar categories, a locked panel with a grab handle, and
  a one-time welcome page. New in 0.3.1.

On Reach the left trigger and X are swapped compared to Halo 3 and ODST, so
grenades sit on X.

Known limitations:

- ODST's first captioned opening cutscene can be black. Skip that first scene
  once; do not repeatedly skip or you will miss the working drop sequence.
- MCC can retain multiple title modules after switching games. If a level
  returns to the menu, fully close and restart MCC.
- On Reach, character tags and navpoints are misplaced in 3D, and the
  `hud_curvature` and `hud_vertical_offset` settings have no effect.
- The right stick still turns you while the F1 menu is open.
- **Microsoft Store / Game Pass only: MCC freezes for about nine seconds on the
  first loading screen. It has not crashed — wait it out.** See below.
- **Vehicle seats need your own adjustment** — see the notice at the top. The
  shipped positions are a starting point, not a finished setting.
- In Reach passenger seats your floating hands aren't drawn yet. The seat, the
  aim and the shooting work; the hands are still being chased down.
- Broader co-op, headset and long-session coverage is still needed across all
  three games.

### Game Pass: the nine-second freeze on the first loading screen

On the Microsoft Store / Xbox app edition, MCC's own loading screen locks up for
roughly nine seconds shortly after launch. In the headset this looks alarming:
the picture freezes, and because your headset keeps re-projecting the last frame
it was given, you can still look around a completely still image. It reads
exactly like a crash.

**It is not a crash, and it is not the mod.** It is MCC's own game loop stopping.
Measured on the same PC, same headset, same build, back to back: the Store
edition stalls 9.0 seconds every single launch; the Steam edition records zero
stalls. Two obvious explanations were tested and ruled out — no anti-cheat module
is loaded, and both editions read the same 44 plain, identical map files off the
same SSD. The stall lands within 74 ms of exactly 9.0 seconds on every run, which
looks like something timing out rather than something loading.

Just wait. It clears on its own and the game runs normally afterwards. If you
want to confirm what happened, `Halo_MCC_VR\HaloMCCVR.log` records it as a `STALL`
line naming how long the game was gone.

Frame rate on Game Pass is **not** worse than Steam — both settle at the same
ceiling. If yours feels like it halves, that is your VR runtime's motion
smoothing / ASW pacing the game at half your headset's refresh because it cannot
hold the full rate; the log prints `app cadence` next to `panel` so you can see
it. Turn motion smoothing off, or lower `resolution_scale`, to get out of it.

The exact accepted source and artifact hashes are in
[docs/CURRENT-STATE.md](docs/CURRENT-STATE.md).

## Install

There is no installer script. Both the Steam copy of MCC and the Microsoft Store
/ Xbox app (Game Pass) copy are supported by the same download, and neither
needs any game file renamed.

1. Download the binary asset
   `MCC_VR_ALPHA_0.3.3.zip` from the official `0.3.3` release page.
2. Open MCC's main game folder — the one that contains the `MCC` folder.
   - **Steam:** **Manage > Browse local files**.
   - **Microsoft Store / Game Pass:** in the Xbox app, **... > Manage > Files >
     Browse**, then open the `Content` folder inside it (the one next to
     `MicrosoftGame.config`).
3. Create a folder named exactly `Halo_MCC_VR` in that folder.
4. Copy `HaloMCCVR.dll`, `HaloMCCVRLauncher.exe` and `halomccvr.cfg` into that
   folder.
   When upgrading from an older build, remove the obsolete `halo3xr.dll` and
   `halo3xr_launcher.exe`; do not keep using the old launcher.
5. Make SteamVR the default OpenXR runtime and start SteamVR, then run
   `HaloMCCVRLauncher.exe`. On Steam, start Steam as well; the Microsoft Store
   edition does not need Steam running for the game itself, but you must be
   signed in to the Xbox app.

On the Microsoft Store edition the launcher starts the game for you through the
Xbox app, so do **not** start Halo: MCC yourself first. The game takes a few
seconds longer to appear than on Steam — that is expected.

The final path must end in one of:

```text
Halo The Master Chief Collection\Halo_MCC_VR\HaloMCCVRLauncher.exe
Halo- The Master Chief Collection\Content\Halo_MCC_VR\HaloMCCVRLauncher.exe
```

Do not place the files loose in the MCC root. To uninstall, close MCC and delete
only the dedicated `Halo_MCC_VR` folder.

If you previously renamed `MCCWinStore-Win64-Shipping.exe` to the Steam name to
force an older build to launch, rename it back. The launcher records the edition
it detected in `HaloMCCVRLauncher.log`.

### Updating to 0.3.3 — replace your config

**Replace `halomccvr.cfg` with the one in the ZIP. Do not keep your old one.**
This is different from updates before 0.3.0, which told you to keep it.

0.3.0 and later add settings that older config files do not contain, and there is
no migration step: any setting your old file is missing silently falls back to a
built-in default rather than the shipped value. The most visible casualty is
`fit_desktop_window`, whose built-in default is off while the shipped config
turns it on — keeping an old config can therefore cap your headset frame rate.
Sharpening, HUD and weapon-alignment values regress the same way. 0.3.3 adds the
whole first-person vehicle block, including a starting position for every ODST
and Reach seat, which an older config has no equivalent for at all.

The shipped config is a tuned, tested configuration rather than bare defaults.
If you want your own tuning back, copy your old `halomccvr.cfg` somewhere safe
first, install the new one, then re-apply your preferences through the F1 menu.

If one PC behaves differently, first confirm SteamVR is still the default
OpenXR runtime, fully close every MCC process before relaunching, compare the
installed hashes below, and compare `halomccvr.cfg`. Do not use a repository
build folder as an installation source.

Release `0.3.3` hashes:

```text
ZIP      C1CC84C1F2278E622F0A439E4DC3791A4E2264DEE8F1F71E48D61346D3AFE69D
DLL      44A82E28B65F8FD6D0A52FF2C87A55C37EFC8B5888DEE6836DEE9AEF89DE026D
Launcher 930BEA232BFC3F8010BC2B385834DEBF796CD3DBEC02ECD0E8475E0DE8A72CE6
Config   E941BC189B57B9ED11EB62DCF8D6AE1C5787936074C2358EB6AF99A377C97975
```

Windows security software may flag or quarantine unsigned injection-based VR
mods. Download only from the official release, verify the hashes, inspect the
source if desired, and allow only the two release binaries rather than disabling
security software globally.

## Required MCC settings

| Setting | Value |
| --- | --- |
| Video > Max Frame Rate | 120 |
| Video > V-Sync | Off |
| Halo 3 > Field of View | 120 |
| ODST > Look Sensitivity | Maximum |
| ODST > Look Acceleration | Off |
| MCC FSR | Off |

ODST's look settings control how quickly bullet direction catches the
motion-controller crosshair. If shots trail, confirm sensitivity is at maximum
and acceleration is off.

Settings live in `Halo_MCC_VR\halomccvr.cfg`, shipped in the ZIP as a tuned
configuration. The F1 menu edits the same values, and the game regenerates the
file if it is missing — but a regenerated file contains bare built-in defaults,
not the shipped tuning, so keep a copy of the shipped one.

## Build from source

See [BUILDING.md](BUILDING.md). A clean build uses the accepted source and
configuration, but produces a new, unaccepted candidate and file hash. Only the
published hashes remain accepted until that rebuilt candidate passes a headset
test. Use the released ZIP when you need the exact accepted binaries.

## Development evidence

- [Current accepted baseline](docs/CURRENT-STATE.md)
- [Halo 3 reverse-engineering facts](docs/RE-notes.md)
- [ODST signatures](docs/ODST-SIGNATURE-EVIDENCE.md)
- [ODST camera layout](docs/ODST-CAMERA-LAYOUT.md)
- [ODST weapon/IK evidence](docs/ODST-WEAPON-IK-EVIDENCE.md)
- [Reach evidence manifest](docs/REACH-EVIDENCE-MANIFEST.json)
- [Reach signature evidence](docs/REACH-SIGNATURE-EVIDENCE.md)
- [Shared title-runtime ownership candidate](docs/TITLE-RUNTIME-OWNERSHIP.md)
- [Per-title evidence policy](docs/EDITING-KIT-EVIDENCE.md)

The code was written by Claude and Codex under the direction of a human modder
who made the product and reverse-engineering decisions and performed the headset
tests. No human reviewed every line. The project is licensed under the
[MIT License](LICENSE).

Inspired by HaloCEVR by LivingFray and ReclaimerVR by Nibre. Halo is a Microsoft
trademark; this project is not affiliated with Microsoft or Halo Studios.
