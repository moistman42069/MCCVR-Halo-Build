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
- [Antivirus flags the launcher](#antivirus-flags-the-launcher)
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

## Antivirus flags the launcher

Windows Security (Microsoft Defender) or another antivirus reports `HaloMCCVRLauncher.exe` as a "Trojan" and removes
or blocks it. The names seen so far are `Trojan:Script/Wacatac.H!ml`, `Trojan:Win32/Wacatac.C!ml`,
`Trojan:Win32/Bearfoos.A!ml`, `Trojan:Win32/Bearfoos.B!ml` and `Behavior:Win32/DefenseEvasion.A!ml`. An update can
stop with a message from the launcher saying your antivirus blocked a file, or with `Cannot read file for SHA-256`.

**Why it happens:** these are false alarms. The launcher isn't code-signed, and it does things malware also does: it
downloads updates, replaces itself with the new version, and loads the VR mod into the game. Antivirus that guesses with
machine learning can flag a program like that. The `!ml` at the end of the name means it is such a guess, not a known
virus.

**Check that your copy is genuine:** download it only from the release page. On that page, `SHA256.txt` lists the
checksum of each ZIP. In PowerShell, `Get-FileHash <the ZIP>` shows your download's checksum; it must match. The full
source of every release is published on the same page.

**Try:**

1. Open **Windows Security > Virus & threat protection > Protection history**, open the entry for the Halo MCC VR file,
   and choose **Actions > Allow on device** (or **Restore**).
2. **You may need to do this a couple of times.** Defender can flag the same launcher again under a different name (for
   example `Wacatac.H!ml`, then `Wacatac.C!ml`). Allow each entry that names a Halo MCC VR file.
3. **If files are missing afterwards, extract the ZIP again.** When Windows Security quarantined a file, allowing it
   doesn't always put it back in the folder. Once you've allowed the entries, extract the release ZIP again (or
   reinstall) so every file is there.
4. Allowing goes by the detection's name, so it also lets other files with that name through. To allow only the mod
   instead, add folder exclusions: **Virus & threat protection > Manage settings > Exclusions > Add an exclusion >
   Folder**, for the folder you extracted the launcher to and for your `Halo_MCC_VR` folder. Then restore the file from
   Protection history.
5. Report it to Microsoft as wrongly detected at <https://www.microsoft.com/wdsi/filesubmission>. Each report helps
   clear that file for everyone.

**It isn't only this mod.** Defender's machine learning regularly flags VR mods that load into games, Wacatac in
particular:

- **UEVR**: Defender has flagged its `uevrinjector.exe` as `Trojan:Win32/Wacatac.H!ml`, the same name we see
  ([UEVR issue #79](https://github.com/praydog/UEVR/issues/79)).
- **REFramework**, praydog's framework behind his RE Engine VR mods: its `dinput8.dll` has been flagged as
  `Trojan:Win32/Wacatac.B!ml`
  ([Steam discussion](https://steamcommunity.com/app/2246340/discussions/0/599646477861552433)).
- **FS25VR**, the Farming Simulator 25 VR mod: its author calls its Wacatac warning "a false positive from Microsoft's
  machine-learning scanner, which distrusts VR injectors in general"
  ([release notes](https://github.com/nick10180/FS25VR/releases/tag/v0.1.2)).
- Microsoft explains false positives and how to report them:
  [Address false positives in Microsoft Defender](https://learn.microsoft.com/en-us/defender-endpoint/defender-endpoint-false-positives-negatives)
  and [how Defender names detections](https://learn.microsoft.com/en-us/defender-xdr/malware-naming).

**What we're doing:** reporting these files to Microsoft, and working on ways to get Defender's machine learning to
treat the mod's files as safe, such as code signing and keeping the part that loads the mod in a separate, unchanging
file. That takes a bit of time.

**In the logs:** Windows Security's Protection history names the file and the threat. The launcher's own message names
the file it couldn't use.

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
