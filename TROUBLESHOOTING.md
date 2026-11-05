# Troubleshooting

Find your problem below. Each entry says what to try, and what the logs show when it is this problem. If none
of them fits, see [Asking for help](#asking-for-help) at the end.

- [Project Reclaimer stays flat, with no error message](#project-reclaimer-stays-flat-with-no-error-message)
- [The picture judders on Virtual Desktop](#the-picture-judders-on-virtual-desktop)
- [VR does not start, or the wrong VR software starts](#vr-does-not-start-or-the-wrong-vr-software-starts)
- [The game crashes a few seconds after the headset picture appears](#the-game-crashes-a-few-seconds-after-the-headset-picture-appears)
- [Project Reclaimer closes as soon as it starts (NVIDIA)](#project-reclaimer-closes-as-soon-as-it-starts-nvidia)
- [Project Reclaimer is very slow or crashes in fullscreen](#project-reclaimer-is-very-slow-or-crashes-in-fullscreen)
- [You can't choose Yes in a menu, or a menu won't scroll (more than one monitor)](#you-cant-choose-yes-in-a-menu-or-a-menu-wont-scroll-more-than-one-monitor)
- [I updated, but nothing changed](#i-updated-but-nothing-changed)
- [Asking for help](#asking-for-help): where the logs are, and what to send

## Project Reclaimer stays flat, with no error message

Project Reclaimer opens and plays on your monitor, but nothing shows in the headset. MCC works in VR.

**Try:** exit **RivaTuner Statistics Server** and **MSI Afterburner** from the system tray, then launch again.
To keep them running, set RivaTuner's **Application detection level** to **None** for the Project Reclaimer
client program.

Other overlays that draw over games can do the same. If you use one, exit it and try again.

**In the logs:** `HaloMCCVR.log` finds your headset (`headset found on runtime ...`) but never says
`creating VR session`. The launcher log still ends with `launch looks good`, because the game itself is running.

## The picture judders on Virtual Desktop

**Try:** in the Virtual Desktop Streamer app on your PC, set the **OpenXR runtime** to **SteamVR**, and start
SteamVR before the game.

**In the logs:** the `OpenXR runtime:` line near the top of `HaloMCCVR.log` shows which runtime the game used.
`VirtualDesktopXR` there means Virtual Desktop's own runtime, not SteamVR.

## VR does not start, or the wrong VR software starts

**Try:** don't run the launcher or the game as administrator. Close the launcher and open it again with a normal
double-click. Also check the **Compatibility** tab of the launcher's and the game's Properties, and untick
**Run this program as an administrator**.

A game started as administrator ignores some ways of choosing your headset's VR software, so it falls back to
the PC's default (often SteamVR).

**In the logs:** `HaloMCCVRLauncher.log` has a line starting `NOTE: the launcher is running as administrator`.

## The game crashes a few seconds after the headset picture appears

This is usually an **OpenXR API layer**: an add-on that sits between the game and your VR software, such as a
frame-generation, upscaling or overlay tool.

**Try:** turn off OpenXR add-ons one at a time and launch again after each.

The mod already keeps **OFXR Bridge** frame generation out of the game, because it crashes the mod. If you
changed `allow_ofxr_bridge` to `1` in `halomccvr.cfg`, set it back to `0`.

**In the logs:** `HaloMCCVR.log` lists every OpenXR layer it finds, with whether it loaded, starting at the
line `OpenXR implicit layers:`. OFXR Bridge gets its own `WARNING` or `OFXR Bridge:` line.

## Project Reclaimer closes as soon as it starts (NVIDIA)

The mod handles this on its own. If it still happens on an NVIDIA graphics card:

**Try:** in **NVIDIA Control Panel** > **Manage 3D settings**, set **Vulkan/OpenGL present method** to
**Prefer native**.

**In the logs:** `HaloMCCVR.log` has a line like `the host made a 1x1 swapchain ... too small to be the game's
picture`. That line on its own is normal: it means the mod set that window aside.

## Project Reclaimer is very slow or crashes in fullscreen

**Try:**
- Leave **F1** > **Picture** > **Headset picture resolution** > **Fit to my monitor** ticked.
- Use **Borderless** in Project Reclaimer's own display settings, not exclusive fullscreen. The launcher
  switches exclusive fullscreen to borderless each time it starts Project Reclaimer. Don't switch it back while
  you play in VR.

## You can't choose Yes in a menu, or a menu won't scroll (more than one monitor)

In MCC's menus, a choice such as **Yes** when MCC asks you to confirm can't be selected, or a list won't scroll. It
happens on PCs with more than one monitor.

**Try:** play with only your main monitor on. Press **Windows key + P** and choose **PC screen only**, then launch.
Afterwards, press **Windows key + P** again and choose **Extend** to get your other monitors back. Turning the other
monitors off works too.

## I updated, but nothing changed

Unzipping a new download doesn't change your game. **LAUNCH MCC**, and **Launch Project Reclaimer** when MCC is
installed, both run the copy installed in your MCC folder.

**Try:** in the launcher, pick the version you want, then press the **INSTALL** button (for example
**INSTALL THIS DOWNLOAD** or **INSTALL UPDATE**). The **Installed:** line under the game list shows the build
you now have.

**In the logs:** the first line of `HaloMCCVR.log` names the build that ran (`source ...`).

## Asking for help

### Where the logs are

The **MOD FOLDER** button in the launcher opens the folder: `Halo_MCC_VR` inside your MCC install. If you play
Project Reclaimer without MCC installed, the logs are in the folder the launcher is in.

| File | What it is |
| --- | --- |
| `HaloMCCVR.log` | The mod itself. The run before it is kept as `HaloMCCVR.log.prev`. |
| `HaloMCCVRLauncher.log` | The launcher: what it started, and any error it showed you. |
| `HaloMCCVRReclaimerRelay.log` | Project Reclaimer only: how the mod reached the game. |
| `HaloMCCVR-native-faults.log` | Errors inside the game, recorded as they happen. |

Each launch starts these logs again, so copy them right after the problem happens, before you launch again.

### What to send

Send us the logs in one `.zip` file:
1. Open the folder with the launcher's **MOD FOLDER** button.
2. Select the four log files above, and `HaloMCCVR.log.prev` if it is there.
3. Right-click them and choose **Compress to ZIP file** (Windows 11), or **Send to** > **Compressed (zipped)
   folder** (Windows 10).

Then tell us:
- MCC (Steam or Microsoft Store) or Project Reclaimer
- your headset, and how it connects (Link cable, Air Link, Virtual Desktop, Steam Link, direct)
- your graphics card
- any overlay or OpenXR add-on you run (RivaTuner, MSI Afterburner, OFXR Bridge, OpenXR Toolkit and the like)
