# MCCVR Halo 4 Build

An experimental continuation of [pancreations/Halo-MCC-VR](https://github.com/pancreations/Halo-MCC-VR) that carries the project forward with playable **Halo 4** support in Halo: The Master Chief Collection.

Maintained by **@MeWhenINameMyself** on Discord.

> [!WARNING]
> This is an early, unofficial build. Launch MCC with anti-cheat disabled, do not use it in matchmaking, and expect visual bugs. Back up your existing `halomccvr.cfg` before installing.

## Download

Download **MCCVR Halo 4 Build V6** from this repository's [Releases](../../releases) page. The release ZIP contains:

- `halo3xr.dll`
- `halo3xr_launcher.exe`
- `halomccvr.cfg`

## Halo 4 features

- Per-eye stereo VR rendering with 6DOF head tracking and native headset field of view.
- Motion-controller aiming, VR turning, head-relative locomotion, and haptics.
- Floating first-person hands and the held Halo 4 weapon.
- A stereoscopic weapon crosshair aligned with the VR aim ray.
- Halo 4's HUD in the headset, including adjustable size, width/aspect, curvature, and vertical placement.
- The shared F1 configuration menu, render-resolution controls, comfort options, and HUD controls.
- Y+B controller chord for pause/resume.
- Carries forward the upstream Halo 3, Halo 3: ODST, and Halo: Reach support.

## Known issues

- Some projectile-hit and damage effects can produce graphical artifacts.
- Some cinematic or pre-rendered cutscenes do not work correctly in Halo 4.
- Certain muzzle flashes and weapon effects are currently disabled or suppressed until their VR placement is fixed.
- Halo 4 support remains experimental; broader headset, vehicle, co-op, and long-session coverage is still needed.
- MCC updates may break signature-based hooks until the mod is updated.

Please include your headset, connection method, GPU, MCC edition, and a relevant `halo3xr.log` excerpt when reporting a problem.

## Install

1. Download `MCC_VR_HALO4_V6_LOC0_POSLOCAL_YB_PAUSE_HUD_FULL_LAYOUT_V6.zip` from the latest release.
2. Open the MCC installation folder—the folder that contains the `MCC` directory.
   - **Steam:** Library > Halo: The Master Chief Collection > Manage > Browse local files.
   - **Microsoft Store / Xbox app:** Xbox app > MCC > Manage > Files > Browse, then open `Content`.
3. Create a folder named exactly `Halo_MCC_VR` in the MCC installation folder.
4. Extract all three files from the ZIP into `Halo_MCC_VR`.
5. Make SteamVR your active OpenXR runtime, start SteamVR, and run `halo3xr_launcher.exe`.

Do not place the files loose in the MCC root. To uninstall, close MCC and remove the dedicated `Halo_MCC_VR` folder.

## Build identity

| File | SHA-256 |
| --- | --- |
| Release ZIP | `379DF6DB51E940FF2BBA61350F38DDB2E182F3E38DA44A17A39A739BA7D12EA0` |
| `halo3xr.dll` | `419F2CA425A41F3FE42A2F27CFD0CE55123F71C5EF34C1C45604018285EFEA82` |
| `halo3xr_launcher.exe` | `930BEA232BFC3F8010BC2B385834DEBF796CD3DBEC02ECD0E8475E0DE8A72CE6` |
| `halomccvr.cfg` | `397CAEB348CFBC987AD69BC81FB5082F9290D9FB74CE75A1B959191AA51F9CB1` |

Windows security software may flag unsigned injection-based VR mods. Verify the hashes above and allow only the specific files if you trust them; do not disable security software globally.

## Source, credit, and license

This project is a derivative of [Halo-MCC-VR by pancreations](https://github.com/pancreations/Halo-MCC-VR). The upstream Git history and MIT license are preserved so the original work and contributors remain credited.

The V6 download is published as a provided binary build. Unless a matching source commit is documented, do not assume the repository can reproduce it bit-for-bit.

Halo is a Microsoft trademark. This project is not affiliated with or endorsed by Microsoft or Halo Studios and contains no game files.
