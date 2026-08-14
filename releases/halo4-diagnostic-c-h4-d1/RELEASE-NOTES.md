# Halo 4 diagnostic pre-release — C-H4-D1

**This is not a release. It is a pre-release of an abandoned effort, published
so the work isn't lost.** If you want the working mod, download **MCC VR Alpha
0.3.3** instead — Halo 3, ODST and Reach, tested. This package does not replace
it and was never tested in a headset by anyone.

## What's in it

The final build of the Halo 4 bring-up, plus a diagnostic that was built,
installed, and never run.

**Halo 4 works in VR in this build**, up to a point: stereo, head tracking,
6DOF, native headset FOV, controller input, motion aim, smooth turn, haptics,
and floating hands carrying the held weapon. Halo 4 draws its own HUD.

**Halo 4 is not finished.** There is no working VR crosshair — that is what the
effort died on. There are no VR vehicles, no cutscene or theatre handling, and
re-entering a level after quitting it can crash.

Halo 3, ODST and Reach are unchanged from the 0.3.3 line here, but were not
re-tested for this package.

## Why a diagnostic

One question was never answered: which command in Halo 4's HUD stream is the
crosshair. Every failed attempt was guessing at it. This build counts them and
writes the answer to `halo3xr.log` — look for lines beginning
`H4DIAG CUI IDENTITY COVERAGE`. Zero table overflow means the census is
complete and the crosshair is identified.

It is log-only. It changes no camera, hand, aim, HUD, or OpenXR behaviour.
Protocol: `docs/HALO4-PARITY-DIAGNOSTIC.md`.

## If you want to pick this up

All of the Halo 4 work is on the `feature/halo4-bringup` branch. Start with
`docs/HALO4-BRINGUP-WRAPUP.md` — it records what works, what was never
finished, and six disproven approaches that cost multiple attempts each. The
proof ledger is `docs/HALO4-SIGNATURE-EVIDENCE.md`.

Anti-cheat must be disabled, as with every build of this mod. No matchmaking.

## Identity

| Field | Value |
| --- | --- |
| Source commit | `7da8f7cb37f26e4eca0dfbb32da2648246d27115` |
| Branch | `feature/halo4-bringup` |
| Base release | MCC VR Alpha 0.3.3 |
| `halo3xr.dll` SHA-256 | `838EA58A74EBEEEEB12B8B4BD260124D1190A734D3D9B7DC84A31D66E7484B63` |
| `halo3xr_launcher.exe` SHA-256 | `A85E97F7872B6C85F4616BDC5D5926C1F166B56C1FF39D7681F07391964D4C9F` |
| ZIP SHA-256 | `55DF55DCE867B35271943C50A438BC3F8F2F71CB3F7BCF1C655FC15090F0C81B` |
| Offline gates | Release build passed, unit tests passed, Reach consistency passed |
| Headset testing | **None.** |
| Accepted | **No.** The last accepted Halo 4 state is C-H4-43, `dd99465`. |
