# Linux / Proton Xbox-login and keyboard review — September 30, 2026

## Report evidence

The September 30 Discord review records two distinct reports:

- Count Scaphandre: MCC forced an Xbox sign-in even though the flat session
  appeared signed in; he could not type into the login prompt.
- Chronomize: on Steam Deck/Proton, the menu worked, but neither the VR
  keyboard nor Steam Deck keyboard entered text on login.

The earlier 101 KB report/log is referenced by the community audit. The current
workspace does not contain a retrievable Linux/Proton runtime log with the
reported session's exact Proton version, desktop/compositor, auth-window owner,
and keyboard path. These remain user reports, not reproduced candidate results.

## Source-backed findings

1. `src/dll/menu.cpp`'s installed MCC `WndProcHook` has been applying a
   synthetic foreground-focus policy unconditionally. It rewrites
   `WM_ACTIVATEAPP`, rewrites inactive `WM_ACTIVATE`, forces
   `WM_NCACTIVATE`, and swallows `WM_KILLFOCUS`. This includes the shell/login
   state even when immersive gameplay is not active. The text-message path is
   otherwise passed to the original window procedure when the VR menu is
   closed. This is a credible focus-handoff interference path; it is not proof
   that the hook caused either user's report.
2. The launcher is a separate Windows `WIN32` executable. Its
   `requireAdministrator` manifest is applied by the MSVC linker only to
   `halo3xr_launcher`; it is not embedded in MCC or `HaloMCCVR.dll`. Windows
   defines that manifest level as a request for administrator credentials.
   The Linux users' reports describe MCC's Xbox-auth/login UI, so the manifest
   alone does not explain their in-game keyboard failure. If the Windows
   launcher itself is run through Wine/Proton, its launch behavior is an
   untested separate compatibility question.
3. Valve's Proton 11.0 source has an AppID-specific compatibility list that
   includes MCC (976730) under a comment about Wayland login text-input delay
   when blitting happens before presentation. That entry adds `noopwr`, which
   sets `WINE_DISABLE_VULKAN_OPWR=1`. This independently supports a real
   MCC-on-Proton login-window timing concern. On Proton versions containing
   this code, Steam's normal MCC AppID path applies the workaround
   automatically. For an older Proton or a nonstandard launch path,
   `WINE_DISABLE_VULKAN_OPWR=1 %command%` is a useful diagnostic, not a
   guaranteed fix for every keyboard failure. It does not establish that this
   mod hook caused either report. Proton's public report for MCC also records historical
   Microsoft-login prompt trouble. A separate Steam-for-Linux report describes
   Steam overlay OSK input grabs interfering with keyboard and focus for Proton
   windows on KDE/XWayland; it explicitly notes Steam Deck/gamescope is a
   different compositor path, so it cannot be generalized to the Deck report.

Sources:

- [Proton source: MCC AppID workaround (lines 1157–1187)](https://github.com/ValveSoftware/Proton/blob/proton_11.0/proton#L1157-L1187)
- [Proton source: `WINE_DISABLE_VULKAN_OPWR=1` mapping (lines 1734–1735)](https://github.com/ValveSoftware/Proton/blob/proton_11.0/proton#L1734-L1735)
- [Proton issue 6481: Microsoft titles requiring Xbox sign-in on Steam Deck](https://github.com/ValveSoftware/Proton/issues/6481)
- [Proton issue 2907: MCC Microsoft-login prompt report](https://github.com/ValveSoftware/Proton/issues/2907)
- [Steam-for-Linux issue 13467: OSK focus/input grab in Proton on KDE/XWayland](https://github.com/ValveSoftware/steam-for-linux/issues/13467)
- [Microsoft application manifest execution-level semantics](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/SbsCs/application-manifests.md)

## Change and verification boundary

The compatibility correction preserves the existing focus policy for known
title runtime modes and changes only shell, unsupported, and no-known-title
states to receive genuine activation and focus-loss messages. Key/text messages
remain unmodified. The launcher keeps its Windows `requireAdministrator`
default. This is a bounded attempt to avoid masking focus while MCC is at its
shell/login UI without risking the established keepalive in loading or active
game states.

An offline policy fixture can verify mode selection, and a source-level check
can verify that the WndProc routes the messages using that policy. Neither
proves Xbox authentication or keyboard entry on Proton. No Proton runtime was
available for this candidate. Release notes should continue to list Linux/VR
and authentication as unverified; this change is a focus-scope correction, not
a claim of Linux support or a solved Xbox-login issue.
