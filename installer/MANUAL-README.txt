HALO MCC VR - MANUAL SETUP
===========================

Supports Halo 3, Halo 3: ODST and Halo: Reach. Reach is new in 0.3.0.
0.3.1 adds Microsoft Store / Xbox app (Game Pass) support, fixes double vision
on ALVR, adds a room-fixed cutscene theatre to all three games, fixes Reach's
black-world bug on the sniper rifle, gives Reach native vehicle controls, adds
an L3+R3 VR-space recenter/menu chord and a Y+B pause chord (ODST/Reach), and
rebuilds the F1 menu into a sidebar layout.
0.3.2 adds first-person vehicles to Halo 3.
0.3.3 extends first-person vehicles to ODST and Reach, so all three games are
covered, and adds the brightness slider to ODST and Reach.


READ THIS FIRST: YOU MUST ADJUST EACH SEAT YOURSELF
---------------------------------------------------
First-person vehicles ship with a starting camera position for every seat, but
that is a starting point, NOT a finished setting. Everybody's height, play
space and headset sit differently, so a seat that looks right for one player
will be too low, too far forward or too far back for another.

Adjust each seat yourself, once, and it is remembered:

   1. Get into the seat you want to fix - driver, passenger, gunner or turret.
   2. Press F1 and open the "Vehicles" category.
   3. Move these three sliders until the seat feels right:

         Seat forward (m)
         Seat height (m)
         Seat left / right (m)

   4. Get out. It saves to halomccvr.cfg by itself.

While you are sitting in a seat, those sliders adjust THAT SEAT ONLY. A
vehicle's driver, its passengers and its gunner each remember their own
position, in each game, so do the ones you actually use and ignore the rest.

While you are ON FOOT the same sliders set the shared starting point that every
seat you have not adjusted follows - useful for a quick global height nudge,
but be aware it moves all three games at once. If you push it somewhere bad, a
"Reset the universal trim" button appears under the sliders and puts it back.

On Reach the left trigger and X are swapped compared to Halo 3 and ODST, so
grenades sit on X.

Works with both the Steam copy of MCC and the Microsoft Store / Xbox app
(Game Pass) copy. You do NOT need to rename anything - if you renamed
MCCWinStore-Win64-Shipping.exe to make an older build work, rename it back.

There is no installer, uninstaller, deploy, or restore script.

INSTALL
-------
1. Open the main game folder.

   Steam:            right-click Halo: The Master Chief Collection and choose
                     Manage > Browse local files.
   Microsoft Store:  in the Xbox app, click the "..." next to Halo: MCC and
                     choose Manage > Files > Browse... Then open the Content
                     folder inside it.

   The correct folder is the one that contains the MCC folder. On the
   Microsoft Store that is the Content folder, next to MicrosoftGame.config.

2. In that folder, create a folder named exactly:

   Halo_MCC_VR

3. Copy these three release files into it:

   HaloMCCVR.dll
   HaloMCCVRLauncher.exe
   halomccvr.cfg

4. Make SteamVR the default OpenXR runtime, then run HaloMCCVRLauncher.exe.

If you are upgrading from an older build, remove the obsolete halo3xr.dll and
halo3xr_launcher.exe after copying the new files. Do not run the old launcher;
the new launcher refuses duplicate injection under either DLL name.

   Steam:            start Steam and SteamVR first.
   Microsoft Store:  start SteamVR, and be signed in to the Xbox app. Steam
                     itself does not have to be running.

   On the Microsoft Store the launcher starts the game for you through the
   Xbox app - do NOT start Halo: MCC first. The game takes a few seconds
   longer to appear than on Steam; that is normal, just wait.

The final path must be one of:

   Halo The Master Chief Collection\Halo_MCC_VR\HaloMCCVRLauncher.exe
   ...\Halo- The Master Chief Collection\Content\Halo_MCC_VR\HaloMCCVRLauncher.exe

Do not put the files loose in the main MCC folder. Launch only through the
included launcher and never use the mod in anti-cheat-enabled matchmaking.

The launcher writes the edition it detected into HaloMCCVRLauncher.log, so if it
ever picks the wrong one that line tells you.

GAME PASS: THE 9-SECOND FREEZE ON THE FIRST LOADING SCREEN
----------------------------------------------------------
On the Microsoft Store / Xbox app edition only, MCC's loading screen locks up
for about nine seconds shortly after launch. In the headset it looks like a
crash: the picture freezes, and since your headset keeps re-projecting the last
frame it was handed, you can still look around a completely still image.

It is NOT a crash and it is NOT the mod - it is MCC's own game loop stopping.
Measured back to back on one PC: the Store edition stalls 9.0 seconds every
launch, the Steam edition stalls zero times. Anti-cheat and encrypted game
content were both tested and ruled out.

Just wait. It clears by itself and the game runs normally afterwards.
HaloMCCVR.log records it as a STALL line naming how long the game was gone.

Frame rate on Game Pass is NOT worse than Steam - both settle at the same
ceiling. If yours feels halved, that is your VR runtime's motion smoothing /
ASW pacing the game at half your headset's refresh because it cannot hold the
full rate. The log prints "app cadence" next to "panel" so you can see it.
Turn motion smoothing off, or lower resolution_scale.

UPDATE - REPLACE YOUR CONFIG
----------------------------
Close MCC completely, then replace ALL THREE files: HaloMCCVR.dll,
HaloMCCVRLauncher.exe AND halomccvr.cfg.

Replace the config. Do not keep your old one. This is different from previous
updates, which told you to keep it.

0.3.0 and later add settings that older config files do not contain, and there
is no migration step: any setting your old file is missing silently falls back to a
built-in default instead of the shipped value. The most visible casualty is
fit_desktop_window, whose built-in default is off while the shipped config turns
it on - keeping an old config can therefore cap your headset frame rate.
Sharpening, HUD and weapon-alignment values regress the same way. 0.3.1 adds new
menu and cutscene-theatre settings that older config files do not have at all,
and 0.3.3 adds the whole first-person vehicle block, including the per-seat
positions for every ODST and Reach seat.

If you want your own tuning back, copy your old halomccvr.cfg somewhere safe
first, install the new one, then re-apply your preferences through F1.

SETTINGS
--------
halomccvr.cfg ships in the ZIP as a tuned configuration, not bare defaults.
Every value has a description, default, and allowed range. Edit it with MCC
closed or use the in-game F1 menu.

If the file is missing the game regenerates it, but a regenerated file contains
bare built-in defaults rather than the shipped tuning - so keep a copy of the
shipped one.

Required MCC settings:

   Video Max Frame Rate:            120
   Video V-Sync:                    Off
   Halo 3 Field of View:            120
   ODST Look Sensitivity:           Maximum
   ODST Look Acceleration:          Off
   MCC FSR:                         Off

VERIFY
------
For a published release, compare the DLL, launcher, and ZIP hashes with the
official GitHub release page. For a local build, use CANDIDATE-MANIFEST.json in
that unique package. A local candidate is not headset-accepted merely because
it was built from accepted source.

If two PCs behave differently, first compare their installed hashes and
halomccvr.cfg files, confirm SteamVR is the default OpenXR runtime on both, and
fully close every MCC process before relaunching.

Windows security software may warn on unsigned injection-based VR mods. Download
only from the official GitHub release, verify the hashes, and allow only the DLL
and launcher rather than disabling security software globally.

REMOVE
------
Close MCC completely, then delete only the dedicated Halo_MCC_VR folder you
created. Never delete the main Halo The Master Chief Collection folder.
