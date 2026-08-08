# Current state

Authoritative as of 2026-08-07. This file is the only active accepted-build
pointer. Detailed pre-cleanup experiments remain available in Git history; they
are evidence, not instructions.

> **Start here: the baseline is `f4c641f`** - the section immediately below
> this note. User-directed on 2026-08-06: "no this build is our baseline now."
> It supersedes MCC VR Alpha 0.3.3 (`94dc09f`) as the development baseline for
> all work, Halo 4 included. 0.3.3 remains the *published* release on GitHub
> and its section is retained below for its per-behavior evidence and its open
> list, every item of which carries forward.
>
> The 0.3.3 section also supersedes the 0.3.1 public release and the
> 2026-08-06 Reach first-person vehicle development baseline, which are both
> retained below for their per-behavior evidence and their open lists. Every
> item still open on the Reach vehicle line carries forward - read it before
> relying on anything.
> Everything dated earlier is history.
> Several older sections describe Reach features as impossible, mandatory, or
> not yet built that have since been built and headset-confirmed - in
> particular the authored crosshair, which older text calls unimplementable in
> Reach. Trust the baseline section and `docs/RE-notes.md` over any older
> narrative here.
>
> **A comment is not evidence.** Two separate stale claims - "Reach has no
> authored capture" and "the ODST camera core installs no authored capture yet"
> - were false when read, and each cost hours because they were believed
> instead of checked. If a comment or doc says a title cannot do something,
> verify it against the code before building on it.

## CURRENT BASELINE: `f4c641f` - 2026-08-06

**This is the development baseline for all work, Halo 4 included.** It
supersedes MCC VR Alpha 0.3.3 (`94dc09f`), which remains the published release
on GitHub but is no longer what development descends from. Set by direct user
instruction: "ok we're building halo 4 off this one we just spent hours
testing", then "no this build is our baseline now."

**Scope of that directive, stated plainly so it is not overread.** The user ran
these exact bytes and set them as the baseline; they did not enumerate
per-behavior acceptance, and the three commits below were never individually
confirmed in a headset. Treat the baseline as user-set and the individual
items as evidenced only by what the log and the sections below actually show.

| Identity | Value |
| --- | --- |
| Runtime source | `f4c641f7b1b707991f2bda71ba485090a16f1e9a` (branch `feature/halo3-vehicle-view-follow`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/f4c641f-reach-fp-parity-20260806-180258546Z` |
| `halo3xr.dll` SHA-256 | `1C6101FD63B4A86822FC110CE86AE29EA174E50CA949A58A801265DEAA98537A` |
| `halo3xr_launcher.exe` SHA-256 | `930BEA232BFC3F8010BC2B385834DEBF796CD3DBEC02ECD0E8475E0DE8A72CE6` |
| Title coverage | Halo 3, Halo 3: ODST, Halo: Reach |
| Installed editions | Steam and Microsoft Store; DLL hashes verified independently in both `Halo_MCC_VR` folders |
| Ran on | Steam, VirtualDesktopXR 1.0.10, Meta Quest 3 - a 36-minute 90 Hz session and a shorter 120 Hz session, both on these exact bytes |

**Three commits over 0.3.3, none of which changed the frame rate:**

1. **`49e59b2` - one cached source view per texture, instead of one in total.**
   `AcquireSrcSrv` held a SINGLE shader resource view keyed on the source
   pointer, and the stereo publish alternates between two different eye
   textures, so it released and recreated a view on every eye of every frame -
   a COM call in a hot hook, which `AGENTS.md` bans. Measured effect: view
   creations fell from ~2 per frame to **4 in a 36-minute session**
   (`upload reuse: ... 360 hit / 0 miss ... 4 created (+0 this window)`).
   Worth microseconds against an 8.33 ms budget. A correctness and
   allocation-churn fix, **not** a performance fix - do not report it as one.
2. **`997c5cd` - GPU timestamps around the eye publish.** The valuable one.
   Before it, nothing in this repo had ever measured GPU time: `renderWindow`,
   `xrEndFrame` and fps are all CPU wall clock, and a tree-wide search found no
   `D3D11_QUERY_TIMESTAMP` anywhere. Non-blocking, 4 frames in flight,
   discards disjoint frames. Logs every two seconds:
   `IQ GPU: eye publish 0.321 ms/frame (resolve 0.200 + post 0.121, both eyes)`.
3. **`f4c641f` - intermediate texture pool raised 4 -> 32.** The capacity is a
   ceiling, not a preallocation; `core_tests` pins that 50 frames of one shape
   occupy exactly one slot. Measured live at `intermediates 0/32 live = 0 KB`
   for a whole session: the pool is never reached in normal play, because the
   eye caches are directly samplable and skip it.

**What the measurement bought, which is the real reason this is the baseline:**
the eye publish costs **0.321 ms/frame for both eyes**, under 4% of a 120 Hz
budget. That closes, on evidence rather than argument, the idea of removing the
second (RCAS sharpen) publish pass - it is worth 0.120 ms. It also means no
buffering or publish-path change can move this mod's frame rate, and the
remaining cost is the scene being rendered twice at `resolution_scale`.

**Open on this line - do not treat as working:**

- **Micro stutter above 90 Hz is OPEN and unfixed.** At 120 Hz the average
  holds (`fps 120 (stereo on)`) but the tail does not: frame interval p95
  9.55-14.64 ms, p99 10.33-16.83 ms against an 8.33 ms period. At 90 Hz the
  same tail fits inside 11.1 ms and the session is clean with ~0 missed frames
  in gameplay. Not the mod's GPU work (0.321 ms). `renderWindow` and
  `xrEndFrame` ANTI-correlate, which is a blocking wait moving between the two
  stamps rather than two independent costs.
- **`docs/FRAME-PACING-120-60-EVIDENCE.md`'s "do not reopen without
  contradictory measurement" list is STALE and must not be relied on.** It was
  written 2026-07-26 on a different runtime. Two entries are provably false
  now: it excluded `xrEndFrame` at "approximately 0.35-1.66 ms" when the
  current measurement is **3.90-8.67 ms**, and it excluded "the eye blit, which
  uses fast `CopyResource` with sample count 1" when the eye blit has not used
  `CopyResource` since the image-quality pipeline landed. Its exclusion of
  SteamVR Motion Smoothing is irrelevant on VirtualDesktopXR, whose own
  reprojection was never tested. Re-measure before reusing any number in it.
- None of the three commits was individually confirmed in a headset. The user
  ran the bytes and reported no regression; that is not per-item acceptance.

## ACCEPTED, HALO 4 BRING-UP LINE: C-H4-1 adapter identity + controller input - 2026-08-06

**This does not move the baseline.** `f4c641f` above remains the development
baseline and 0.3.3 remains the published release. This is the first Halo 4
candidate and the first Halo 4 headset touch, on branch
`feature/halo4-bringup`. Title coverage of the shipped product is unchanged:
Halo 3, ODST and Reach.

| Identity | Value |
| --- | --- |
| Accepted source | `954359b7f786b78c76824b662ead3c1fc8cd7917` (branch `feature/halo4-bringup`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/954359b-reach-fp-parity-20260806-212516151Z` |
| `halo3xr.dll` SHA-256 | `8B327A0B2FFC20135ECBEB71BEA698C78908EC1AA7C09C810CA329482ADE74AD` |
| Installed editions | Steam and Microsoft Store; DLL hashes verified independently in both `Halo_MCC_VR` folders |
| Accepted runtime | Steam edition, VirtualDesktopXR 1.0.10, Meta Quest 3 at 120 Hz; Halo 4 window `17:20:46`-`17:21:54` |
| Headset result | Accepted: "itested halo 4 i think the controls work" |
| Preserved evidence | `out/test-runs/954359b-halo4-c1-controller-steam-pass-20260806-172046` |
| Preserved log SHA-256 | `07B3030B41662411D1C1235348D61EB62F8AD80624E8873095FF4568806BBBE6` |

**What is now true.** Halo 4 is a recognised title. The adapter detects
`halo4.dll`, prints its pinned identity from compile-time constants, and
enables the shared virtual-controller transport, so VR controllers drive Halo 4
as a gamepad. Nothing else changes: `stereo off` for the whole Halo 4 window,
no hook created, no camera owned, no load bounce, zero warnings or errors.

**What this is NOT.** Halo 4 renders flat in the headset - no stereo, no
head-tracked camera. That is the design of C-H4-1, not a shortfall. The
gamepad transport is a process-wide XInput hook shared with the other titles,
so its working in Halo 4 was never seriously at risk; the load's real value is
the *negative* result, that arming the registry row was inert.

**Open on this line - do not treat as covered:**

- On this accepted C-H4-1 line, the ~68 s window is menu-heavy and **no Halo 4
  level-load or level-exit cycle was exercised**. The later unaccepted C-H4-2
  run did log-verify the load gate and loaded-image identity; that evidence does
  not retroactively change C-H4-1's accepted behavior.
- On C-H4-1, the pinned identity is `NOT yet verified against the loaded image`
  by construction - nothing in `halo4.dll` is read, not even PE headers. The
  later C-H4-2 preflight closed that evidence gap on its own unaccepted line.
- Everything on the `f4c641f` baseline and the 0.3.3 line carries forward.

Per-candidate evidence, the full log citation and the cross-title regression
datapoints are in `docs/HALO4-SIGNATURE-EVIDENCE.md` under "Candidate status".

### Installed forward ledger — NOT accepted; C-H4-1 remains the rollback pointer

The installed files can be newer than the accepted pointer. That is the case
now: C-H4-9 is installed in both editions for a look-pitch headset test, but
**none of C-H4-2 through C-H4-9 advances C-H4-1**.

| Candidate | Result / status |
| --- | --- |
| C-H4-2 `3656da9` | Level-load gate and loaded-image cold observation log-verified PASS; explicit acceptance and repeat exit/reload remain pending. |
| C-H4-3 `2987dc2` | **FAILED:** wrapper replay ran but all eyes were uncaptured, `layers=0`, black headset; no head-pose read. |
| C-H4-4 `68daa27` | **FAILED:** captured the wrong deferred target; unlit scene without lighting/shadows/post/HUD. |
| C-H4-5 `89b89ef` | **FAILED / user rejected:** lit distinct eyes and sustained `layers=1`, but malformed 3D/FOV, no head tracking/6DOF/HUD. |
| C-H4-6 `4fc3c84` | **FAILED:** zero completed pairs, visible stall/title bounce, wrong FOV representation, and wrong outer-function return ABI. Behavior reverted by `7d58a68`. |
| C-H4-7 `dbf1382` | **PASSED its own claim, experience REJECTED (2026-08-08):** 226-243 pairs/2s, `geometry TAKING`, zero drops, distinct eye pixels, 120 fps. The user rejected it for the two things it deliberately excluded - no 6DOF, and an FOV that did not fill the headset. It also exposed a NEW defect: Halo 4's stock cover (50.46/41.14 deg) does not contain PSVR2's frustum (61.5/53.0), so the whole slice was submitted at the wrong FOV (`M2 WARNING`). Evidence preserved at `out/test-runs/dbf1382-halo4-c7-stock-geometry-20260808-0553`. |
| C-H4-8 `6cf0b76` | **PASSED both of its own log claims, experience REJECTED (2026-08-08):** `geometry TAKING`, 137 pairs/2s, `138 tracked frames`, `reference captured`, `lean 0.006 world units (6DOF ON)`, `276 widened eyes`, `calibration learned`, engine built `61.75/53.31 deg`, `contains headset frustum: YES` - the `M2 WARNING` is gone. Stereo, head tracking, 6DOF and native FOV all work. Rejected for one thing it did not cover: the look stick's vertical axis pitches the engine's camera and C-H4-8 adds head pitch ON TOP of it, so the stick tilts the world away from the player's real horizon. |
| C-H4-9 `0e450d5` | **PITCH CLAIM PASSED, shot line MISSED (2026-08-08):** "6dof is working and it looks and runs great" - the stick no longer tilts the world, and the log confirms `head pitch ... (ABSOLUTE, headset owns pitch)`, 242 tracked frames/2s, `lean ... = +0.024/+0.010/-0.006 xyz`. The loop's third part missed: "shots don't follow my view", explicitly deprioritised by the user. Cause measured, not guessed - see below. |

| C-H4-10 `140e15d` | **RAN 2026-08-08, no fault reported.** Log: `aim GRANTED, haptics GRANTED, room scale GRANTED, locomotion head-relative`, `MOTION AIM: the hand steers, the stick turns`. Not explicitly accepted. |
| C-H4-11 `28a20a7` | **REFUSED, as designed (2026-08-08):** "no floaty hands, gun still stuck to my face". It wrote nothing; its probe proved the whole addressing chain live (2 weapon slots, field value 85) and disproved two reads - see E-H4-17. |
| C-H4-11a `e9bd65b` | **INSTALLED, HEADSET-PENDING:** Halo 4's own first-person gun and arms placed on the controller, with the node format proven from live engine values before anything is written. |

| C-H4-11a installed identity | Value |
| --- | --- |
| Source | `e9bd65b824592daffd5adde4ddf578f254e58508` (branch `feature/halo4-bringup`) |
| Package | `out/candidates/e9bd65b-halo4-c11a-first-person-hands-live-bank-20260808-163811888Z` |
| `halo3xr.dll` SHA-256 | `240E608B21A415B0B039478866688C92D4B7734512167A9A690C61D59F91EF27` |
| `halo3xr_launcher.exe` SHA-256 | `5625F9A5782741B705C57F4C9C7628B8135FCE270C9CE815E427125AED5F9444` |
| Installed editions | Steam and Microsoft Store; both installed hashes independently matched |
| Offline gates | Release x64 build, `core_tests`, Reach consistency gate - all passing |
| Acceptance | **Not accepted.** C-H4-1 remains the rollback pointer. |

**What changed from C-H4-11.** It read the wrong half of the record. `+0x15D4`
is the node COUNT, not an index, and the live node bank is at `+0x00` - `+0xF00`
is the previous-frame copy. Full derivation in E-H4-17; the zeros the probe
returned are what proved both.

**What C-H4-11a must show.** `Halo 4 C-H4-11a hands:` reading `node format
PROVEN from live values, placing`, placed frames climbing, and an engine stock
ROOT node with `|quat|` about 1.000 and a sane scale. In the headset: the gun
and arms follow your controller.

| C-H4-11 identity (superseded) | Value |
| --- | --- |
| Source | `28a20a7018bb9858772cb3dc8f84333fb25c2468` (branch `feature/halo4-bringup`) |
| Package | `out/candidates/28a20a7-halo4-c11-first-person-hands-20260808-161616964Z` |
| `halo3xr.dll` SHA-256 | `FE9C1AAE44ADB9ECBA33B4F378C954D6ED5DBFF73548B6058D579BAA6876729A` |
| `halo3xr_launcher.exe` SHA-256 | `5625F9A5782741B705C57F4C9C7628B8135FCE270C9CE815E427125AED5F9444` |
| Installed editions | Steam and Microsoft Store; package and both installed hashes independently matched |
| Preserved previous installs | `out/deploy-backups/765d3d7-steam-before-28a20a7-20260808-161617878Z`, `...-store-...` |
| Offline gates | Release x64 build, `core_tests`, Reach consistency gate - all passing |
| Acceptance | **Not accepted.** C-H4-1 remains the rollback pointer. |

**What C-H4-11 must show.** `Halo 4 C-H4-11 hands:` should read `node format
PROVEN from live values, placing`, with placed frames climbing, a weapon slot
count of 1 or 2, a root node index, and an engine stock node whose `|quat|` is
about 1.000 with a sane scale. In the headset the gun and arms follow your
controller instead of your head; `halo4_hands = 0` returns them to stock and
the three `halo4_hand_*_m` trims nudge the placement.

**If it reads `REFUSED`,** the 0x20-byte node is not `{rotation, translation,
scale}` and NOTHING was written - the reported `|quat|`, scale and translation
are then the evidence for what it actually is, and that is the next step rather
than a failure to work around.

| C-H4-10 identity (superseded) | Value |
| --- | --- |
| Source | `140e15dcdba983b02bc99444f707f1ef61492c56` (branch `feature/halo4-bringup`) |
| Behavior commit | `8395c97`; `8915840` and `140e15d` are manifest/installer fixes only |
| Package | `out/candidates/140e15d-halo4-c10-motion-aim-turn-rumble-20260808-130741925Z` |
| `halo3xr.dll` SHA-256 | `765D3D7844F863A6755029991EAD22614BE83ECD14DA683EB99D9B787B990A47` |
| `halo3xr_launcher.exe` SHA-256 | `B578160109BDE9A94AF12099A5E6A7509CB6C8513265749A2F0BA9B006F141A6` |
| Installed editions | Steam and Microsoft Store; package and both installed DLL/launcher hashes independently matched after install |
| Preserved previous installs | `out/deploy-backups/33fc9e4-steam-before-140e15d-20260808-130742787Z`, `...-store-...` |
| Offline gates | Release x64 build, `core_tests`, Reach consistency gate - all passing |
| Acceptance | **Not accepted.** C-H4-1 remains the rollback pointer. |

| C-H4-9 identity (superseded) | Value |
| --- | --- |
| Source | `0e450d504ef2f37971281fc756f67ae55676e498` (branch `feature/halo4-bringup`) |
| Package | `out/candidates/0e450d5-halo4-c9-headset-owns-pitch-20260808-121246432Z` |
| `halo3xr.dll` SHA-256 | `33FC9E41612D8AC1A92F4CC1A92E26DFA9BB5B3E4AB5DAA67F33F5C5A31D3579` |
| `halo3xr_launcher.exe` SHA-256 | `B578160109BDE9A94AF12099A5E6A7509CB6C8513265749A2F0BA9B006F141A6` |
| Installed editions | Steam and Microsoft Store; package and both installed DLL/launcher hashes independently matched after install |
| Preserved previous installs | `out/deploy-backups/f4164eb-steam-before-0e450d5-20260808-121247428Z`, `...-store-before-0e450d5-20260808-121247428Z` |
| Acceptance | **Not accepted**, and superseded by C-H4-10 above. Its pitch/orientation behaviour carries forward unchanged. |

The superseded C-H4-8 bytes were source
`6cf0b76f027369d115d4df67c01e6218921663d4` (behavior `55e890d`), DLL SHA-256
`F4164EB2B3DF41459CD1839101BFA72768D4A0EEA263D5AB36B0A47E817AE660`, package
`out/candidates/6cf0b76-halo4-c8-headtracking-fov-20260808-114408358Z`. Its run
is the reference log for everything C-H4-9 must not regress.

**C-H4-10 is the installed candidate: motion aim, VR turn and rumble.** The
headset run above also corrected a premise this project had carried since
C-H4-5: **Halo 4 already draws its HUD** ("i can see the hud"), because its CUI
arrives inside the captured scene target. Halo 4 therefore needs no HUD
redirect, unlike the other three titles, and that ladder rung is cancelled
rather than deferred.

What C-H4-10 must show: `Halo 4 C-H4-10 shared systems:` with aim, haptics and
room scale all `GRANTED`, `locomotion head-relative`, `yaw reference captured`,
and a non-zero VR turn count when you use the stick; and `Halo 4 C-H4-10
look/aim: MOTION AIM: the hand steers, the stick turns`. In the headset: point
your hand and shoot there, turn with the stick, feel rumble, and walk where you
look. Expect **two reticles** - Halo 4's own centred one keeps drawing and now
reports the middle of your view rather than the gun; the floating procedural one
on the controller ray is the truthful one. Insert drops back to C-H4-9's stick
yaw + headset pitch; F2 drops back to C-H4-8.

**C-H4-9 RESULT, 2026-08-08 (Steam, SteamVR/OpenXR 2.17.6, PSVR2, 120 Hz).**
The user's words: *"shots don't follow my view but that doesn't matter, 6dof is
working and it looks and runs great."* Evidence preserved at
`out/test-runs/0e450d5-halo4-c9-look-pitch-steam-psvr2-20260808-0741`
(log SHA-256 `688B06CE1CA05552763FAFEE5669BE4DF4235C9FA526898EC97C5DC15B27862A`).

- **The pitch/orientation claim PASSED.** `head pitch ... (ABSOLUTE, headset
  owns pitch)`, 242 tracked frames per 2 s, `lean 0.027 world units =
  +0.020/+0.017/-0.006 xyz` - 6DOF confirmed moving on all three axes
  separately, which the old magnitude-only line could not show. The stick no
  longer tilts the world.
- **The shot-line half MISSED**, and the log names the mechanism. The loop runs
  and does converge - it learned `direction +1`, and mean |error| over the 64
  reported windows is 1.79 deg - but `min step` latches at **2.758 deg** for 39
  of those 64 windows, which sets the rest band to enter 1.65 deg / exit 4.14
  deg. The gun therefore parks up to ~1.7 deg off the view and does not
  re-engage until 4.1 deg; at 20 m that is 0.6-1.4 m of miss, with no crosshair
  drawn to make it visible. Two windows exceeded 8 deg (max 20.8).
- **Why `min step` is wrong, measured rather than theorised.** The window shows
  1354 commanded + 96 parked polls in 2 s = ~725 XInput polls per second against
  a 120 Hz publication, so MCC polls the pad about six times per rendered frame
  while the engine pitch is republished once. `AimServoObserve` therefore sees
  five zero-steps and one whole-frame step instead of one per-poll step, and its
  deliberate rise-immediately/decay-slowly rule latches the lump. The observer
  must be driven by the publication serial, not by the poll.

The shot line is deferred by explicit user choice, not closed. It belongs with
controller aim (C-H4-12), where the reticle makes the error visible; fixing the
sampling without a crosshair would be tuning a number nobody can see.

**What C-H4-9 had to show in the headset.** One claim, on its own log line:

1. **Your head owns up and down.** Standing with your head level, the horizon is
   level, whatever the look stick has been doing. Pushing the stick up or down
   no longer tilts the world - it aims the gun, and the view stays where your
   head put it. Looking up and firing sends the shot up. Log: `Halo 4 C-H4-9
   look pitch:` `the headset owns the vertical axis`, a small `error`, a
   `learned direction` of `+1` or `-1`, and `commanded` polls falling away to
   `parked` ones as the gun settles.

Everything C-H4-8 already proved must stay proved: `geometry TAKING`, completed
pairs > 0, exact-zero camera readback error, center `0/0`, zero drops and zero
uncaptured eyes, `contains headset frustum: YES`, and - now reported per axis so
6DOF is provable on each one - `Halo 4 C-H4-9 head tracking: ... lean ... =
+x/+y/+z xyz`, with `head pitch ... (ABSOLUTE, headset owns pitch)`.

Test at 90 Hz first so the separately open 120 Hz pacing tail cannot confound the
result. F2 turns the whole pitch behaviour off and returns C-H4-8's, which is the
A/B if anything feels wrong.

**Watch for, and report if seen:** ghosting, smearing or trailing behind moving
objects. Halo 4's per-eye setup gives the engine's own previous-frame history an
inter-eye delta, and Halo 4's motion-blur variables are kind-unproven for this
title so nothing was bound blind. If it appears, turn motion blur off in MCC's
own Graphics > Screen Effects menu; that isolates it without a code change.

**A Halo 3 regression is owed before this pointer can move.** `vr.cpp`'s prepare
ordering and the `Game_IsStereoGeometryOnlyBringup` predicate are shared code.

## SUPERSEDED AS BASELINE, STILL THE PUBLISHED RELEASE: MCC VR Alpha 0.3.3 - 2026-08-06

**`MCC_VR_ALPHA_0.3.3`, the FP Vehicle Update, is what is published on GitHub
and what users are running. It is NO LONGER the development baseline** -
`f4c641f` above replaced it by user direction on 2026-08-06. Everything in this
section still applies to the shipped product and every open item carries
forward. It supersedes
`MCC_VR_ALPHA_0.3.2` publicly and supersedes the Reach first-person vehicle
development baseline `558d0bf` recorded below. The user's directive on this
exact source and DLL: "the dll i tested on steam is the best version we have
and our new baseline once we begin halo 4."

| Identity | Value |
| --- | --- |
| Runtime source | `94dc09fd0a6579d723de71792ae4c3f62ce4fb7e` |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/94dc09f-reach-fp-parity-20260806-150849513Z` |
| `halo3xr.dll` SHA-256 | `44A82E28B65F8FD6D0A52FF2C87A55C37EFC8B5888DEE6836DEE9AEF89DE026D` |
| `halo3xr_launcher.exe` SHA-256 | `930BEA232BFC3F8010BC2B385834DEBF796CD3DBEC02ECD0E8475E0DE8A72CE6` |
| `halomccvr.cfg` SHA-256 | `E941BC189B57B9ED11EB62DCF8D6AE1C5787936074C2358EB6AF99A377C97975` |
| ZIP SHA-256 | `C1CC84C1F2278E622F0A439E4DC3791A4E2264DEE8F1F71E48D61346D3AFE69D` |
| Title coverage | Halo 3, Halo 3: ODST, Halo: Reach |
| Installed editions | Steam and Microsoft Store; DLL, launcher and config hashes verified independently in both `Halo_MCC_VR` folders |
| Accepted run | Steam edition, VirtualDesktopXR 1.0.10, Meta Quest 3, session starting `10:12:30` on this exact source (`halo3xr.log` records `source 94dc09f...`) |
| Headset result | Accepted. The Microsoft Store install was separately confirmed to run, but the tuning and the acceptance run were both on Steam. |

**The shipped `halomccvr.cfg` is the user's live Steam config**, copied verbatim
at packaging time, at the user's explicit direction: the Steam edition is where
the 0.3.3 seat tuning was performed, and the Store install was only checked for
liveness. Its universal trim is intact at the accepted `0.10 / 0.05 / 0.00`.

**Release contents over 0.3.2**, all from the 67 commits in
`MCC_VR_ALPHA_0.3.2..94dc09f`; the user-facing wording is in
`releases/0.3.3/RELEASE-NOTES.md` and the full itemisation in
`releases/0.3.3/manifest.json`:

1. **First-person vehicles in ODST and Reach**, completing all three titles.
   Every seat placed, each with its own config line and F1 sliders.
2. **Turret aim parked and the reticle moved onto the barrel** (Halo 3, ODST).
3. **Passenger shots on the sight line at every range** (all three titles).
4. **Halo 3 seat entry faces the nose** whichever way `vehicle_view_follow` is
   set.
5. **The shared universal seat trim can no longer be destroyed** by an
   unidentified vehicle, with a one-click reset and a loud config-load report.
6. **`game_brightness` drives ODST and Reach**, plus an ODST steady-exposure
   seat option.
7. **Nothing is touched in a title whose level is still loading** - the fix for
   the load bounce, widened from "do not install" to "do not touch".
8. **Reach: a native pause no longer replays a seat entry on resume.**
9. **Perf: ODST reaches stereo ~2 s sooner; signature scanning is ~7x faster**
   with byte-identical results, tested against the original loop.

**Open on this line - carried forward, do not treat as working:**

- Reach passenger floating hands are still not drawn. Start from the
  `ReachProcessFpPalette` collapse, not from the render gate; the engine's
  first-person transaction ran 658 times during a 6 s passenger occupation, so
  admission is not the fault.
- ODST seat exposure hold, and the new ODST/Reach brightness paths, shipped in
  the accepted build but were never individually confirmed in a headset.
- Regression debt from R-V25's shared-code change is discharged only to the
  extent the user's own play covered it; no enumerated Halo 3/ODST regression
  pass was recorded.
- Everything still open on the superseded baselines below carries forward.

## SUPERSEDED DEVELOPMENT BASELINE: Reach first-person vehicles - 2026-08-06

**Superseded the same day by the 0.3.3 public release above, which contains this
line plus the Halo 3/ODST/turret/brightness/load-gate work built on top of it.**
Retained for its per-behavior evidence and its open list.

**The development baseline was source
`558d0bf1ffec148ffe9b6088df52b00c05381bea` (`halo3xr.dll` SHA-256
`F1DA4FA2FE34D20977C90342674DC7E4D81AF001801CF65F9C5A59613D95490F`).**
The user directed this after running that exact build: "alright that's great,
this will be our new baseline going forward, not perfect but good enough for
now." It supersedes the ODST vehicle baseline `118b8bde` recorded below,
whose own results carry forward unchanged.
Read the open list before relying on anything here.

| Identity | Value |
| --- | --- |
| Runtime source | `558d0bf1ffec148ffe9b6088df52b00c05381bea` |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/558d0bf-reach-fp-parity-20260806-070420272Z` |
| `halo3xr.dll` SHA-256 | `F1DA4FA2FE34D20977C90342674DC7E4D81AF001801CF65F9C5A59613D95490F` |
| `halo3xr_launcher.exe` SHA-256 | `E412F4003C46DD2D76AECEFAB411AEFA42593057027D508A3245BD6D871F081A` |
| Title coverage | Halo 3, Halo 3: ODST, Halo: Reach |
| Installed editions | Steam and Microsoft Store; DLL hashes verified independently in both `Halo_MCC_VR` folders |
| Accepted run | Steam edition, VirtualDesktopXR 1.0.10, Meta Quest 3 at 90 Hz, session `02:06:50`-`02:17:26` on this exact source |

**This is a "good enough for now" directive, not a per-item acceptance.** The
user did not enumerate which behaviors they checked. Treat every item below as
either directly evidenced by that session's log or as open.

**What this line fixed, over three candidates (R-V25, R-V26, R-V27):**

1. **Seat trims can no longer be destroyed from the F1 menu.** An unidentified
   Reach vehicle used to decode to trim slot -1, which meant the *universal*
   trim shared by Halo 3, ODST and every unadjusted Reach seat - so tuning that
   seat rewrote the base for all three titles. Unmatched vehicles now key their
   own row. The panel also offers a one-click reset for the universal trim,
   which earlier sessions had walked from 0.10/0.05 to -1.00/+1.02.
2. **Sliders behave like Halo 3's in all three titles.** The -1 mm/+1 mm
   buttons, the `Edit universal trim` / `Return to occupied seat` buttons and
   the three-decimal display are gone; Reach's slider now travels Halo 3's own
   distance either side of the seat's authored Blender base instead of spanning
   128 m.
3. **Mounted weapons resolve their identity.** Every HREK type-16 mounted
   weapon now also resolves at retail's turret physics type 6; the rocket
   Warthog turret used to log `unmatched vehicle tuple` despite its tuple being
   in the table bit for bit.
4. **A Reach seat's reset returns it to its authored Blender row**, not to the
   shared universal trim.
5. **The passenger seat reports first person.** The occupied seat's
   `third person camera` bit is cleared once per occupation with Halo 3 C20's
   lifetime, scoped to seats that author `allows weapons`. The accepted log
   shows `warthog raw seat 1` at `seat flags 00000860 (third-person=0
   allows-weapons=1)` while every driver, turret and gunner seat keeps its own
   flags untouched (`04020014`, `0404001C`, `00080818`, ...).
6. **A passenger's shots leave the sight ray they aim down.** The origin is the
   presented controller ray's own origin rather than the rendered eye, so the
   shot line and the crosshair are one line at every range instead of crossing
   at `crosshair_distance_m`.

**Open on this line - do not treat as working:**

- **Passenger floating hands are still not visible.** The user's last words on
  it were "the floaty hands aren't working". **The accepted session's own log
  now narrows this decisively:** the engine's first-person arms/weapon
  transaction ran **658 times during the ~6 s Warthog passenger occupation**
  (`2346` at entry `02:16:16`, `3004` at exit `02:16:21`). The engine *is*
  building the occupant's first-person models in that seat. The remaining fault
  is therefore on our side - placement, or the palette collapse in
  `ReachProcessFpPalette` that scales everything except the wrist descendants
  and the held object to 0.0001 - and **not** engine admission. Three
  candidates were spent on admission theories before this number existed; start
  from the palette, not from the render gate.
- **The passenger shot line is unconfirmed.** R-V27's change shipped in this
  build but the user reported no result for it.
- **Regression debt.** R-V25 changed shared code the other two titles use: the
  F1 seat-trim slider block and the universal-trim defaults. No Halo 3 or ODST
  headset result has been recorded against this line, and `AGENTS.md` requires
  one for a shared-code change before release.
- Everything still open on the ODST baseline below carries forward, including
  the ODST/Halo 3 level-load lockout.

## SUPERSEDED BASELINE: ODST first-person vehicles - 2026-08-04

**Superseded on 2026-08-06 by the Reach first-person vehicle baseline at the
top of this file.** It was source `118b8bde850d17a147e99ecff7f9d9214b6ca380`
(`halo3xr.dll` SHA-256
`7CB49B543A9D70B3AF2B60847A4618980A5D7E0E24B5A4DC24726FCEBC2B9690`), directed
by the user on 2026-08-04 with "lets push forward this is our new baseline".
Its ODST results and its two headset-UNVERIFIED items below still stand and
carry forward, because the Reach line descends from it and changed no ODST
behavior.

| Identity | Value |
| --- | --- |
| Runtime source | `118b8bde850d17a147e99ecff7f9d9214b6ca380` |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/118b8bd-reach-fp-parity-20260804-042612769Z` |
| `halo3xr.dll` SHA-256 | `7CB49B543A9D70B3AF2B60847A4618980A5D7E0E24B5A4DC24726FCEBC2B9690` |
| `halo3xr_launcher.exe` SHA-256 | `A2AF02477EA2BD6947F4468CB43168BA7DE132A433B6945591A4F69E75633E11` |
| Title coverage | Halo 3, Halo 3: ODST, Halo: Reach |
| Installed editions | Steam and Microsoft Store; hashes verified independently after install |

**What is headset-CONFIRMED in this line:** ODST first-person vehicle seats
(seat parenting, hidden body, hands on the vehicle, per-seat F1 trims, view
follow) and the ODST crosshair updating on a weapon switch. The user confirmed
the seat experience and stopped reporting the crosshair fault.

**What is headset-UNVERIFIED in this build - do not treat as working:**

1. **Seat exposure hold** (O6). The previous two attempts were provably inert
   or mislabelled; this one writes the copy the engine propagates from. Never
   observed in a headset. The log now prints either `hold verified against the
   engine's own copy` or `the hold is NOT taking`, which settles it from one
   session.
2. **Passenger shot origin** (O6). Moves the shot line onto the sight line at
   the firing call site. Never observed in a headset.

The user's words when setting this baseline were "I'll come back to it when
users complain of your failure" - i.e. these two were explicitly NOT tested,
not accepted. If either is reported broken, `docs/ODST-VEHICLE-EVIDENCE.md`
section O-E6 has the full binary evidence and O-E6d names the one remaining
untested mechanism (the luminance history advancing 3x per frame from
alternating eyes).

**Also open on this line:** the ODST/Halo 3 level-load lockout, parked by user
choice; see `docs/ODST-LEVEL-LOAD-LOCKOUT.md`. Its undone decisive test is a
no-mod control run.

## SUPERSEDED CANDIDATE: complete Reach vehicle repair - 2026-08-05

Superseded by the 2026-08-06 Reach baseline at the top of this file, which
contains this work plus the R-V25/R-V26/R-V27 repairs on top of it. Kept for
its per-behavior evidence. When written, the accepted pointer was 118b8bde. Implementation commit f370185 is
the new unaccepted Reach vehicle repair bundle; the documentation/packaging-only
descendant packaged from it must be identified by its candidate manifest. A
passing build or install does not advance this pointer.

The immediately preceding candidate was rejected. Source
03766bd672fd9c241d1929285d6e0d63887cb157, DLL SHA-256
E70BA41FE8D1C5E071D73E12D2C9A8D7329490A496C43ECB70D366AAA1718DAB,
ran on Steam / VirtualDesktopXR 1.0.10 / Quest 3 / 90 Hz. The user reported a
missing crosshair, broken Warthog turrets, vehicle shaking and a visible player
model. Its guessed primary_trigger path was reverted by 2aa700e; no working
View Follow OFF or Blender placement work was rolled back.

The f370185 bundle contains all outstanding requested behaviors together:

1. View Follow OFF retains the headset-reported good driving path. View Follow
   ON retains R-V19's render-matched carrier basis. The rejected between-frame
   seat-camera-mode lease is permanently dormant, removing its twice-per-frame
   camera-state toggles and shake source.
2. Reach's actual unit-camera hide-player bit 0x0004 is scoped only around the
   admitted normal-player outer render. It hides the controlling player's
   seated world model while preserving first-person arms, weapons, both View
   Follow settings, and unrelated camera flags.
3. The authored floating controller crosshair remains visible for every Reach
   vehicle seat; neither occupant unit+0x214 nor the old clamped/native vehicle
   sight can replace it during entry or seated play.
4. The exact native projectile unit-adjust transaction keeps the engine-
   selected active barrel and origin, then redirects only the exact local
   vehicle weapon's central pre-spread direction through the presented
   stabilized VR sight. Stock spread, ballistics, aim assist and tracking
   remain stock. Fresh full-key data is bounded to 50 ms for 72-144 Hz.
5. The 25 built-in Blender camera placements and exact retail aliases remain
   present and config-round-trip tested, including Scorpion, Shade Plasma,
   Shade Flak and Plasma Turret. The implementation has no runtime Workshop
   dependency.

Release x64 builds, the full core tests, the Reach parity gate and static
lifecycle review pass. Headset acceptance still requires both View Follow
options, Warthog/Scorpion/Covenant turrets, body visibility, crosshair and
near/far firing checks, followed by Halo 3 and ODST regressions. Record edition,
runtime, headset and refresh rate. Until the user explicitly accepts that exact
installed DLL, this section is evidence only.

## NOTE: Halo 3 vehicle view-follow user-stated working - 2026-08-03

The user stated "view follow works" and directed development (ODST first-person
vehicles, then Reach) to continue on `feature/halo3-vehicle-view-follow`. The
last build the user ran is source `1d6dcb6` (contains the C25 pitch-follow and
wheel-turn candidate; DLL SHA-256 `B5B5CAC1...`, Steam edition,
VirtualDesktopXR 1.0.10, Quest 3, session 2026-08-02 15:07). No preserved log
shows a `H3 vehicle pitch follow:` transition and the live config ends with
`vehicle_view_follow = 0`, so this is recorded as a user endorsement of the
follow experience, not a component-level C25 acceptance; the baseline pointer
below is unchanged. Details in `docs/HALO3-VEHICLE-EVIDENCE.md` C25 status.

## SUPERSEDED PUBLIC RELEASE: MCC VR Alpha 0.3.1 - 2026-07-30

**Superseded publicly by `MCC_VR_ALPHA_0.3.2` and then by
`MCC_VR_ALPHA_0.3.3` at the top of this file.** Retained for its per-behavior
evidence; its policies (ship the maintainer's live config, replace rather than
keep, tested bytes = shipped bytes) all still apply.

**The public known-good product was `MCC_VR_ALPHA_0.3.1`, a hotfixes update
over 0.3.0.** It superseded `MCC_VR_ALPHA_0.3.0`.

| Identity | Value |
| --- | --- |
| Runtime source | `60f3929f63474459e51ce3d641fc14dc5d49529b` |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/60f3929-reach-fp-parity-20260730-104312277Z` |
| `halo3xr.dll` SHA-256 | `5B4A852C3175021AD433373BACB825BCC5D0EDFC52AF620A26C8A2C55F00BA64` |
| `halo3xr_launcher.exe` SHA-256 | `9F510506981B882BD571E892B88AD26951CA08948C7DADB031088060E918824A` |
| `halomccvr.cfg` SHA-256 | `5847BF57E18133954C172103CD13F65560896AC198A119CC4BA1BE1202F36C2D` |
| ZIP SHA-256 | `D139C7E7D507E114EB8F54A10D4FD1DAAC797D4722D9E5C29ADB969DBBB9B2AF` |
| Title coverage | Halo 3, Halo 3: ODST, Halo: Reach |
| Installed editions | Steam and Microsoft Store; hashes verified independently in both `Halo_MCC_VR` folders after install |
| Accepted runtime | Steam edition, SteamVR/OpenXR 2.17.6, Meta Quest 3 via Meta Link/Air Link at 120 Hz |
| Headset result | Accepted. Game boots into VR on both editions; ALVR double-vision fix confirmed stable; the room-fixed cutscene theatre entered correctly on a Halo 3 cutscene with real 3D depth on this exact build. |

This release folds in everything accepted on `fix/halo3-theatre-fov-06e76a2`
since the 0.3.1 hotfix was first staged: Game Pass/Store support and the ALVR
double-vision fix (both already staged), the universal cutscene theatre
(including the native-FOV two-view projection path now headset-accepted on
Quest 3 - see `docs/CUTSCENE-THEATRE-EVIDENCE.md`), the Reach sniper
black-world fix, Reach native vehicle controls, the L3+R3 recenter/menu chord,
the Y+B pause chord in ODST and Reach, and the rebuilt sidebar F1 menu with
welcome page.

**Known gap:** the PSVR2 regression for the current theatre projection path is
not yet re-run against this exact build. The prior PSVR2 result was against an
earlier theatre implementation and does not carry over automatically.

Two policies from 0.3.0 continue to apply: the ZIP ships the maintainer's own
live, headset-tuned `halomccvr.cfg` and users are told to replace their old one
(no config migration exists); and this exact published ZIP is the tested
artifact - do not rebuild and republish without repeating headset verification,
since the DLL embeds its compile timestamp and a rebuild is never byte-identical.

## ACCEPTED DEVELOPMENT BASELINE: Reach crosshair survives its own bloom - 2026-08-02

**This is the current development baseline for the next hotfix/feature update.**
It fixes GitHub #70 (also reported directly by the user): Halo: Reach's VR
crosshair disappeared for about a second every time the player took damage.

| Identity | Value |
| --- | --- |
| Baseline source | `5e47b648b65e983ec84f834c7c656bd3e87d84aa` (branch `feature/halo3-vehicles`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/5e47b64-reach-fp-parity-20260802-054336728Z` |
| `halo3xr.dll` SHA-256 | `F3CFB2E15F49B1F861A5E64B886D5ADF38FAD36B4C58E308182189D7EE68DF64` |
| `halo3xr_launcher.exe` SHA-256 | `D3CB54AE7920D0FDFB633B85D405986DC2C6C1BE0F92DB7C2B8CE9B10438C405` |
| Installed editions | Steam and Microsoft Store; DLL hashes verified independently in both `Halo_MCC_VR` folders |
| Accepted runtime | Steam edition, VirtualDesktopXR 1.0.10, Meta Quest 3 |
| Headset result | Accepted: "ok it's working now". Halo 3 regression accepted separately in the same session: "halo 3 works". |
| Preserved evidence | `out/test-runs/5e47b64-reach-crosshair-damage-and-halo3-regression-pass-20260802-010200` |
| Preserved log SHA-256 | `11EF65CA45A2EA1AF332402CE28885EA495AFCE2508114BFDCD0EC8633A33664` |

**Root cause: Reach blooms its crosshair out of the capture crop.** Official
HREK CHUD exports (`out/hrek-evidence/chud`) show the Reach crosshair
collection is five bitmap widgets - `ar_reticule`, `l_crosshair`,
`r_crosshair`, `t_crosshair`, `b_crosshair` - which is exactly the `pieces 5`
the runtime reports. Every one carries
`external input B = weapon barrel error scale` and drives its `active`
animation from `extern 2`, that same input. Taking a hit spikes barrel error,
the four petals bloom outward, and they leave the capture crop - a magnified
centre crop holding only `kReticleSize/(gameWidth*2)`, about 9% of screen
width. The empty middle was then published over the good art.

**The fix.** The capture target carries a mip chain; on a frame that is about
to pay the blocking swapchain upload anyway, the 8x8 level is read back and its
alpha summed. A capture holding no ink, or under half the ink of the crosshair
currently on the quad, is refused and the last good crosshair stays. Only
measured-good captures ever reach the swapchain. Crop widening was rejected on
purpose: it costs either apparent size or sharpness, and bloom is not the
aiming feedback in VR that it is on a flat screen, where the crosshair rides
the controller ray.

**What the accepted log proves.** `art` is the measured ink and `blankHeld` the
refusals per window. Reach reaches `art 0` with `blankHeld` at 137, 154, 180
and **zero** uploads in those windows - the guard held the good crosshair
through every bloom. Halo 3 in the same session reports `pieces 0, held 0,
art 479, blankHeld 0`: it supplies no piece count so that guard is inert there
by construction, its ink never drops, and not one publish was refused.

**Three earlier candidates are in this build and did nothing for this bug.**
They are recorded because each one *eliminated* something:

- `c2d9149` settles a CHANGED art identity before it may replace held art.
  Inert here - the key never left `8E28E5B60B57DCA3`.
- `ecb018c` refuses a capture with FEWER class-2 pieces than the held art.
  Inert here - all five widgets keep drawing, they are merely clipped.
- `0dab3d7` introduced the ink measurement, which is what proved the mechanism
  (`art 343` good, `art 0` blank). Its anti-stuck escape hatch fired on
  consecutive-hold count alone, so an EMPTY capture satisfied it after 24
  refusals and was published anyway - `art 0` windows still showed 6-7 uploads,
  about three blanks per second. `5e47b64` requires that escape to hold at
  least some ink.

Between them these eliminate, by measurement rather than argument, the art
identity, the piece count, the quad submission gates and the swapchain: across
a whole combat session the quad read `SUBMITTED ... heldArt=1` with zero
`NOT submitted` lines while the crosshair still vanished.

**Debt carried by this baseline.** `c2d9149` and `ecb018c` are proven inert for
this defect and remain live in the shipped bytes. Removing them is a separate,
headset-tested cleanup step, one path at a time - deleting dormant Reach
crosshair code broke the runtime once already (2026-07-26). Do not fold that
cleanup into a feature candidate.

**Process record, because it was expensive.** The answer was in
`out/hrek-evidence/chud` the entire time and three candidates were spent
reasoning about the publish path instead of reading it. Two rules follow:
search the existing evidence exports before proposing any runtime probe, and a
restated symptom is not a test result - check the log's build stamp *and* that
the session actually contains the event before treating a report as a failure.

## ACCEPTED: Halo 3 animated CHUD crosshair - 2026-08-01

This was the development baseline until the Reach crosshair result above
superseded it. It is on `feature/halo3-vehicles`, sits one commit above the
first-person vehicle baseline recorded below, and is not yet published. Its
Halo 3 behavior was re-confirmed in the headset on the `5e47b64` baseline
above, which is the regression that shared publish-path change owed.

| Identity | Value |
| --- | --- |
| Baseline source | `643a6c191eacc5a0c3c722125a55494559a859f8` (branch `feature/halo3-vehicles`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/643a6c1-reach-fp-parity-20260801-133044697Z` |
| `halo3xr.dll` SHA-256 | `84D62264B544E351827FD779DE7C8331B04010A84DDB586280E0A2EEE7D358D7` |
| `halo3xr_launcher.exe` SHA-256 | `FCF795CDC96329C14E9F9EA2998663BBC7BCE4BA88A3EC49FB2C4D4F081D4633` |
| Installed editions | Steam and Microsoft Store; DLL and launcher hashes verified independently in both `Halo_MCC_VR` folders |
| Accepted runtime | Steam edition, SteamVR/OpenXR 2.17.6, Quest 3 (`'SteamVR/OpenXR : oculus'`, vendor `0x28DE`) at 120 Hz |
| Headset result | Accepted: "test was great crosshair is working again!" |
| Preserved evidence | `out/test-runs/643a6c1-halo3-crosshair-animation-steam-pass-20260801-091400` |
| Preserved log SHA-256 | `1FAD263DE243B20E4FF93FD8223A6EB50915EE4D442529B9493E336D1BC597E1` |

**The trade this candidate made, and what the log measured.** The publish is
paid for out of the per-frame offscreen CHUD capture Halo 3 was already doing:
Halo 3 now samples the native CHUD only on the frames it publishes, so the
capture cost leaves five frames in six and the upload joins the sixth. The
preserved log settles both halves of that trade with numbers, not a prediction:

- **The publish rate is exactly the design.** 360 of the 610 reported windows
  read `40 uploaded, 0 skipped`, i.e. 20 publishes per second at 120 fps -
  one in six, the floor `crosshair_animation_frames = 6` allows - and a further
  153 windows sit at 36-39. Exactly one window in the whole session skipped a
  single upload. The sample gap and the publish floor are the same number by
  construction, so no capture work was spent and discarded.
- **The frame rate held.** `fps ... (stereo on)` reads 119-120 against a
  120 Hz panel through the animated stretch, so restoring the animation did not
  reintroduce the performance loss that the plain 1-in-6 cadence caused on
  2026-07-30.

This closes the regression that the `ec3c981` -> `f205ce9` -> `06e76a2`
performance pass introduced, and the `06e76a2` colour-ordering edge stays
refuted: it was still frozen in the headset. ODST keeps its 1-in-30 sample and
identity policy; Reach keeps its own bounded animation cadence.

The same session entered the Halo 3 cutscene theatre three times on this exact
build (`08:53:50`, `08:59:33`, `09:14:19`), so it is also the reference run for
the theatre work that follows it.

## ACCEPTED: Halo 3 first-person vehicles - 2026-08-01

This was the development baseline until the crosshair result above superseded
it; the crosshair candidate is the commit immediately after it. It is on
`feature/halo3-vehicles`, descends from the 0.3.1 line, and is not yet
published. It does not replace the 0.3.1 archive above.

| Identity | Value |
| --- | --- |
| Baseline source | `efe8bdba28c349a0a96a1ccd6d4acf7eda75f0d1` (branch `feature/halo3-vehicles`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/efe8bdb-reach-fp-parity-20260801-130145401Z` |
| `halo3xr.dll` SHA-256 | `0E9DB6DA57CB9D7C61D55BE07998E2161DF6CEC7A8B511C9D81BC60CE2537F9E` |
| `halo3xr_launcher.exe` SHA-256 | `778C212491020CE53D564D18D364A5CCBA4A8115037131D8E576B58FFE497D38` |
| `halomccvr.cfg` SHA-256 | `D0F12A32716385739051B5DF45AF634FB5AE9B3C1B2EC1DA33F0219DD48392E5` |
| Installed editions | Steam and Microsoft Store; DLL, launcher and config hashes verified independently in both `Halo_MCC_VR` folders |
| Accepted runtime | Microsoft Store (Game Pass) edition, SteamVR/OpenXR 2.17.6, Quest 3 (`'SteamVR/OpenXR : oculus'`, vendor `0x28DE`) at 120 Hz |
| Headset result | Accepted. First-person vehicles, hidden character model, passenger floating hands/gun, and the seat re-centre all confirmed good in-headset. |
| Preserved evidence | `out/test-runs/d62e3ac-halo3-fp-vehicles-gamepass-pass-20260801-072357` |
| Preserved log SHA-256 | `407976C30036ED8AC62CDE99D1B24FC346C091ACB75FB6B2389B9E240788326F` |

**What the headset run proves.** The preserved log is the accepted Game Pass
session on `d62e3ac` (C23). It records the vehicle evidence chain resolving
uniquely, `H3 first-person seat: Active` on seat entry, the nose-align, the
play-space re-centre firing on a settled edge, and - the decisive C22 counter -
`head-parented` holding at **100%** of sampled frames with `anchor fallback 0%`
across every ten-second window after entry. Before C22 that percentage toggled
between 0% and 42% window to window, and each toggle was the camera step the
user reported as the vehicle "resetting". Continuity, not a smoothing filter, is
what removed it.

**Scope of the acceptance versus the exact baseline bytes.** The accepted
headset run is `d62e3ac`. The baseline commit `efe8bdb` (C24) sits one commit
later and changes **only** built-in defaults and the shipped configuration - no
runtime behavior differs between them. `efe8bdb`'s exact bytes are installed and
hash-verified in both editions but have not themselves been run yet, so any
future report against this baseline should confirm the log's first line names
`efe8bdba28c349a0a96a1ccd6d4acf7eda75f0d1`.

**The five accepted behaviors, in order.**

- **C20 `8547b7d` - hide the character model in a first-person seat.** Halo 3
  seat flag bit 4 (third-person camera) is re-read from the loaded tag on every
  camera evaluation and never latched at entry, so clearing it at runtime takes
  effect immediately and restores completely. Clearing it makes the occupied
  seat report first person, and Halo then stops drawing the player's own model.
  The camera anchor is not touched. Third-person driving is unaffected because
  the patch applies only while `vehicle_first_person` and `vehicle_hide_body`
  are both on and only to the seat actually occupied.
- **C21 `8fdd238` - arms ride the seat, not the occupant's head.** Under a
  cleared bit 4 the engine camera source *is* the occupant's `head` marker, so
  the controller hand origin was inheriting head animation. The origin now
  selects the seat body anchor. It is deliberately not lowered to a literal
  chest point: controller displacement is measured from the room-space head
  reference captured at that seat, so lowering the origin would drop the hands
  rather than re-parent them.
- **C22 `7649997` - saturate the head contribution instead of dropping it.**
  The old code gated the head-parented contribution on a magnitude limit, so
  crossing the limit switched the contribution off entirely and stepped the
  camera. It now clamps to the same limit and stays continuous.
- **C23 `d62e3ac` - bounce strength, and a play-space re-centre on both seat
  edges.** `kHalo3HeadMaximumLocalDelta = 0.08` is **world units**, i.e. 24 cm
  of camera travel, which only became continuously felt once C22 stopped
  gating it; `vehicle_bounce` scales it (shipped at `0.35`). The user
  explicitly rejected smoothing as the remedy, so this is a plain strength
  scale - no filter, no averaging, no added latency. Separately, nothing
  re-neutralised play-space position on exit, so a settled seat entry or exit
  now requests a **position-only** re-centre. The yaw half is deliberately never
  requested: it would destroy the entry nose-align on the way in and the
  hull-follow heading on the way out.
- **C24 `efe8bdb` - ship the maintainer's tuned configuration as the default.**
  `vehicle_view_follow` now compiles to `false`, and the six headset-tuned seat
  placements are compiled into `kConfigShippedSeatTrims` and applied by
  `Config::Config()`.

**Maintenance hazard, knowingly accepted.** The vehicle tuning now exists in two
places: the shipped `halomccvr.cfg` and `kConfigShippedSeatTrims` in
`src/common/config.h`. It is duplicated on purpose - `src/common/config.cpp`
performs no migration, so a user who keeps an older config silently falls back
to compiled defaults, which is exactly the `fit_desktop_window` trap recorded
under 0.3.0. **A future retune must update both**, or the compiled fallback goes
stale. Only the axes the maintainer actually moved are baked in; every untouched
axis still follows the universal trim.

Full per-candidate disassembly, RVA tables and seat-layout evidence are in
`docs/HALO3-VEHICLE-EVIDENCE.md`.

### Background to the accepted crosshair candidate

This section is the reasoning that produced `643a6c1`; it is **accepted**, and
the result table is in the baseline section at the top of this file. It was
built on the `efe8bdb` vehicle baseline above. The user reported against
`efe8bdb` that Halo 3's crosshair shows no shooting animation and no red/green
target state.

**What was already known.** The 2026-07-27 entry "The crosshair blackout: one
defect behind three symptoms" records the mechanism exactly: the art key
describes WHICH widgets drew, not how they look, so animation frames, red/green
tints and fades all leave it unchanged and the publish is skipped. `716a635`
fixed this for all three titles on a bounded cadence and was accepted with "I
tested all three halos, and their crosshairs are working good. The performance
is good." Halo 3 was moved off that cadence on 2026-07-30 by a performance pass
(`ec3c981` -> `f205ce9` -> `06e76a2`), not by any headset failure of the
animation. The colour-ordering edge `06e76a2` added in its place is now
reported as still frozen in the headset.

**The constraint that pass established, which this candidate must respect.**
With zero steady uploads the Halo 3 render window is 6-7 ms against an 8.33 ms
budget at 120 Hz, and the publish is a blocking OpenXR acquire/wait/copy/release
measured at ~4-5 ms. Simply restoring the bounded cadence was reported as a
performance loss, so restoring the animation on its own is not enough.

**The change.** The publish is paid for out of work Halo 3 was already doing
every frame. `60f3929` proved the offscreen capture is itself a real per-frame
cost - it redirects the render target and re-draws every class-2 widget piece,
saving and restoring immediate-context state each time - and throttled ODST's
to one sample in thirty; that shipped in 0.3.1. Halo 3 was still paying that
cost on every frame while publishing almost none of it. Halo 3 now samples the
native CHUD only on the frames it publishes: the capture cost is removed from
five frames in six, and the upload is added to the sixth. The sample gap and
the publish floor are the same number by construction, so a sampled frame
always publishes and no capture work is spent and discarded.

**Deliberately not claimed at the time, and since measured.** Whether that trade
is net-neutral or better in the render window was left to the headset rather
than predicted. The accepted run answered it: 20 publishes per second with one
skipped upload in the whole session, at 119-120 fps. Two things made that
session decisive rather than a guess:

- `crosshair_animation_frames` is live in the F1 menu (Crosshair category).
  0 turns the animation off and holds one image - strictly cheaper than the
  current shipped behavior, because the slow sample replaces a per-frame one.
  6 (default) is the fastest publish the floor allows, 60 the slowest.
- The log now reports `Halo 3 reticle upload: N uploaded, M skipped ...` every
  two seconds, exactly as Reach's already does, so a report comes with the
  achieved publish rate beside the felt frame rate.

ODST and Reach are untouched: ODST keeps its 1-in-30 sample and identity
policy, Reach keeps its bounded animation cadence.

## Development milestones folded into 0.3.1

The two results below were headset-accepted ahead of the 0.3.1 release above
and are now part of its shipped bytes; they are kept here as their own
evidence entries rather than merged into the release block.

### ACCEPTED: L3+R3 VR-space recenter/menu chord - 2026-07-29

| Identity | Value |
| --- | --- |
| Accepted source | `081002cdf85ae40151b657101a1101930083cad8` |
| Candidate package | `out/candidates/081002c-reach-fp-parity-20260730-004638634Z` |
| `halo3xr.dll` SHA-256 | `EA7D114CFED85D3E0A5EB2993422CA0918FC2850549680F3976A058B7D43FB66` |
| Installed editions | Steam and Microsoft Store; hashes verified independently |
| Accepted runtime | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz |
| Headset result | Accepted. Pressing L3+R3 correctly recenters VR space and toggles the F1 menu. |

The same debounced L3+R3 edge now recenters the immersive camera and room-fixed
theatre origin before opening the menu. The existing R3 zoom suppression and
menu click consumption remain intact.

### ACCEPTED: ODST authored-look theatre qualification - 2026-07-29

| Identity | Value |
| --- | --- |
| Accepted source | `b2b3fa2ffd88c6f2285889090d85dfe3a069b5f3` |
| Candidate package | `out/candidates/b2b3fa2-reach-fp-parity-20260730-003242667Z` |
| `halo3xr.dll` SHA-256 | `25906FE4C76B7AD87B4CFFE597777AD0F83EAB2AD05BFD0352E09BF30959479D` |
| Installed editions | Steam and Microsoft Store; hashes verified independently |
| Accepted runtime | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz |
| Headset result | Accepted. ODST's locked opening remains visible in theatre through the clouds reveal, and the right-stick-controllable drop-pod shots remain immersive VR. |

The rejected `player_camera_control` qualifier at `14fc96e` disabled legitimate
ODST theatre and remains negative evidence. This accepted build instead reads
the engine's live per-shot maximum-look-angle constraints: any current or
pending look freedom publishes `PlayerControlled`; only proven zero freedom
retains `AuthoredLocked`. Missing proof falls back only this feature to
immersive VR.

## PUBLIC RELEASE: MCC VR Alpha 0.3.0 - 2026-07-27 (superseded, rollback baseline)

**`MCC_VR_ALPHA_0.3.0` was the first release to include Halo: Reach.** It
superseded `MCC_VR_ALPHA_0.2.2` (Halo 3 + ODST) and is now itself superseded by
`MCC_VR_ALPHA_0.3.1` above. Kept here as the rollback baseline.
Everything below this section is development history.

| Identity | Value |
| --- | --- |
| Runtime source | `4b85134bdef1cd5785a4e9246bd5c92191fe6647` |
| Build | Release x64, preset `release`, ODST ON, Reach ON |
| Candidate package | `out/candidates/4b85134-reach-fp-parity-20260727-145719316Z` |
| `halo3xr.dll` SHA-256 | `CE43FC67A72D14B6D1D9508C4BB6D8461A7733A303CC94B5784BA0274CE64E9F` |
| `halo3xr_launcher.exe` SHA-256 | `0433A47883AAA9516C25F1830F8DC33EB15098CABDC04EDC223250B1EFBF25F0` |
| `halomccvr.cfg` SHA-256 | `D4D4AA1A687174EB5A01859353AE67B987F636013132FF711EADC4C3157A8317` |
| ZIP SHA-256 | `BE1C084F3F2D40CA95A22B66DF4644DF4A3576F7D2D70E001FB11B50AB4C6922` |
| Title coverage | Halo 3, Halo 3: ODST, Halo: Reach |
| Headset result | Accepted. Halo 3 + ODST regression passed, clearing the debt owed for `5cd1181` and `32d92a7`. The exact published ZIP was additionally validated on a second machine (laptop) before release. |

Two things about this release are deliberate and must not be silently reverted.

**The ZIP ships `halomccvr.cfg`, and users are told to REPLACE their old one.**
This reverses the advice given through 0.2.2. `src/common/config.cpp` parses
`config_version` but performs no migration, so any key an older config lacks
silently falls back to its built-in default instead of the shipped value.
`fit_desktop_window` defaults to `false` while the shipped config sets it to
`1`, so a retained old config can reintroduce the desktop-window frame-rate cap.
The shipped file is the maintainer's live, headset-tuned config, copied verbatim
at packaging time - not generated defaults.

**Release rebuilds are not byte-identical** (the DLL embeds compile date/time).
The published ZIP is the exact artifact that passed the second-machine test.
Do not rebuild and republish without repeating that validation.

### REJECTED: preserve Reach's native per-eye FP camera rebuild - 2026-07-29

This result is evidence only and does not advance the public pointer above.
The user's exact E490 baseline is source `9cb88d7` and DLL SHA-256
`E4905011247978F570C17739FB7631FF91CB5F4870ADF5B42DC986E4A1585C88`.
The failed candidates after it have been behaviorally reverted. Static review
then found one older E490 behavior in the exact weapon-sensitive FP stage: the
Reach FP camera detour's post-original whole-block overwrite.

| Identity | Value |
| --- | --- |
| Tested source | `5a4101caab5c9f22262703323caa3cdedef6265e` |
| Candidate package | `out/candidates/5a4101c-reach-fp-parity-20260729-153523252Z` |
| `halo3xr.dll` SHA-256 | `833B249ED970EC91AA2B2964017427F944596C49CB9C5E5DB8EACCA2E359F06A` |
| Installed editions | Steam and Microsoft Store; hashes verified separately |
| Runtime | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz |
| Headset result | Complete regression: black static-world geometry unchanged; stereo/3D and interior visibility newly broken |
| Preserved evidence | `out/test-runs/5a4101c-reach-native-fp-rebuild-fail-20260729-103809` |
| Preserved log SHA-256 | `ACCABA6DED2F9B9507D9F7711EC2814F9D93AE4A1F856012F42E25BB086907F7` |

Official HREK and the pinned retail module prove that Reach's native FP rebuild
copies its source compact camera from `view+0x08`, preserves the weapon scale at
`view+0x10`, applies its FP FOV, applies the weapon-dependent `+0x64` near/AA
adjustment, rebuilds the derived block, applies the current-blur postprocess,
and uploads the finished ViewVS/ViewPS constants. The old detour let that full
transaction finish, then replaced the native compact and derived blocks with a
raw world pair and manually uploaded the replacement. That made our hook the
last writer over the title's sniper-sensitive result.

The rejected candidate passed the exact eye compact to the native rebuild
through a bounded `0x18`-byte input proxy. Reach itself now performs both the
FP-draw rebuild and the post-draw restore for each eye. It does not disable the
FP pass, restore a 2D viewmodel, or disconnect the hand/head separation. The
worker reports ACTIVE only after both eyes have completed both native phases in
one stable prepared serial. Build, core tests, and the Reach consistency gate
passed before packaging, and the runtime log proves the new path reported
ACTIVE. Those facts did not predict the headset result: the black geometry was
unchanged and the proxy introduced a complete 3D/interior regression. The
behavior is reverted before further diagnosis.

Official HREK separately proves a shared attachment boundary:
the FP material pass binds `_surface_depth_stencil` with albedo/normal targets;
the immediately following world-static-lighting pass reuses that exact
depth/stencil surface with the lighting accumulation target. The failed headset
result proves that preserving the native FP camera rebuild did not correct the
reported pixels; the shared attachment fact must not be promoted to a cause.

### ACCEPTED: commit each rebuilt Reach outer eye before world rendering - 2026-07-29

This is the current headset-accepted development pointer. It does not silently
republish or replace the public Alpha 0.3.0 archive above.

| Identity | Value |
| --- | --- |
| Accepted source | `74e1477d7ae02ea57f754ac76dfd99678d9028ec` |
| Candidate package | `out/candidates/74e1477-reach-fp-parity-20260729-161950871Z` |
| `halo3xr.dll` SHA-256 | `5BB673ABBA8BA9D4B0BC667D1009CE41880ABCB1F8EFDC31F7E118659C20C898` |
| Installed editions | Steam and Microsoft Store; hashes verified independently |
| Accepted runtime | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz |
| Headset result | Accepted: sniper-triggered black static-world textures fixed in both eyes; paired weapon toggle remains normal; prior 3D/interior regression absent |
| Preserved evidence | `out/test-runs/74e1477-reach-outer-camera-commit-pass-20260729-112718` |
| Preserved log SHA-256 | `064BD5E47FDE39F8E1D66EC4BA376372D5B974EFD32ED2FE4CD79ADFAE0BF3F9` |

This candidate matches one existing Halo 3/ODST behavior that Reach uniquely
omitted: after rebuilding the current eye's CPU camera, commit that exact eye to
the title's renderer before entering the world render.

The paired same-scene screenshots supplied by the user are the controlling
runtime evidence: with the sniper selected, exact static rock/terrain surfaces
are solid black; after switching weapons, those surfaces render normally while
the sky and HUD remain correct. The user also reports that the black surfaces
retain geometry/depth and differ between eyes. This candidate does not label
that as projection, SSAO, shadow, rain, or missing geometry.

Pinned retail and official HREK close a missing state-commit edge. Reach's stock
outer camera-stack push invokes callback `haloreach+0x26BFD4`; it rebinds the
viewport/scissor state and uploads the active `player_view+0x490` camera bank to
ViewVS/ViewPS. Our inner stereo hook rebuilt `player_view+0x490` for each eye
after that stock centre-camera push, but entered `player_view_render` without
invoking the callback again. The first later per-eye camera upload was inside
the weapon-dependent nested first-person stage. Halo 3 and ODST already perform
their equivalent upload/preparation before each eye render.

The candidate invokes the exact no-argument outer callback once after each
eye's camera-state/matrix rebuild and before `player_view_render`. The callback
is identity-gated by a unique loaded-image AOB and complete body hash, and the
worker reports ACTIVE only after one complete two-eye render/copy transaction
returns with both callback bits for one stable prepared serial. Projection
math, FP camera rebuild behavior, culling
policy, SSAO, shadows, depth handling, and controller/head separation are
unchanged.

The exact accepted log reports the unconditional BOUND line and then both-eye
ACTIVE at 11:24:10. The user's immediate headset result was explicit: “ITS
FIXED.” Because this candidate changed only the missing native state-commit
edge after the rejected candidates were reverted, the outer-eye commit is now
the headset-confirmed fix for the reported weapon-sensitive black static-world
surfaces.

### REJECTED: invalid Reach post-palette wrist write as black-world cause - 2026-07-29

This result is evidence only and does not advance the public pointer above.
Candidate `bba9f535bda36b2be64462827ac9c453f94009dd` disabled only the
failed `511eb0b` post-palette write that copied
an absolute-world controller wrist matrix directly into one root-relative live
skeleton record. Reach had already performed the correct world-to-record
conversion over the complete graph, and the visible gun had already consumed
its private correct palette. The invalid single-record write then remained live
through both later world-eye renders with no success-path restore.

This is active-hook cleanup, not a native SSAO, shadow, depth-buffer, culling,
camera-orientation, or rain theory. It does not restore a stock viewmodel or 2D
path. The valid controller transform, controller-held gun/hands, per-eye camera,
and accepted loaded-tag muzzle retarget all remained active. The exact Steam /
SteamVR 2.17.6 / PSVR2 headset run reached 204,258 prevented invocations with
zero executions and reported both-eye FP camera ACTIVE. The black static-world
geometry was unchanged, conclusively rejecting this late skeleton write as its
cause. The safety correction remains disabled. Preserved evidence:
`out/test-runs/bba9f53-reach-world-black-wrist-write-fail-20260729-101820`
(log SHA-256
`950B210A9F0D267E01DD8C5FCA359D23626FE98EEB5EE1F8EEB206179CC4C993`).
Full binding and ordering evidence is in `docs/REACH-SIGNATURE-EVIDENCE.md`.

### REJECTED: Reach black-world effect-owner gate - 2026-07-29

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested source | `b510b5e5e361441befa1e2dbb6bf509c77216f2e` |
| Candidate package | `out/candidates/b510b5e-reach-fp-parity-20260729-091346123Z` |
| `halo3xr.dll` SHA-256 | `D140145AABCA2EC8ABDBB90294A51BBCB5C87FFA8A3988CEB36D2BAFB59F7EB9` |
| Runtime | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz |
| Headset result | Black world geometry unchanged |
| Preserved evidence | `out/test-runs/b510b5e-reach-world-black-effect-gate-fail-20260729-042409` |

The candidate added exactly one runtime gate: only effects carrying Reach's
first-person-weapon-owner low nibble could enter
`ReachReparentEffectMatrix`. The runtime counters prove the gate executed:
215,356 ordinary `world/no-fp-user` calls caused no proximity attempts,
while 252 first-person-owned calls produced 224 re-parents. Since the visual
defect was identical, broad effect-matrix re-parenting is rejected as the
black-world cause. The behavior is disabled in the following revert commit
before any new candidate.

### REJECTED: Reach static-world lightmap-shadow isolation - 2026-07-29

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested source | `839aed764dcaa9969c457ec919992f44015d4f02` |
| Candidate package | `out/candidates/839aed7-reach-fp-parity-20260729-110212499Z` |
| `halo3xr.dll` SHA-256 | `BEAA872494E042DDE4EFEF583946EAA9BD72218890F81DBA959C036900F8B66C` |
| Runtime | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz |
| Headset result | Black static-world geometry unchanged |
| Preserved evidence | `out/test-runs/839aed7-reach-world-black-lightmap-shadow-fail-20260729-061119` |
| Preserved log SHA-256 | `40923EF9CDD1BEBECB6D8660CAB98E9385AB5BF81FB1C48986731930051B4A96` |

The candidate disabled the exact `render_lightmap_shadows` boolean only while
stock `player_view_render` executed each admitted VR eye, then restored it in
`__finally`. The headset log proves the exact binding armed and records at least
two completed `1 -> 0 -> 1` scopes with zero write or restore failures. The
visual defect was unchanged, so Reach's object lightmap-shadow pass is rejected
as the black-world cause. The implementation remains dormant as evidence; the
following revert commit prevents it from arming before another candidate.

### REJECTED AS CAUSE: stale Reach DrawIndexed diagnostic - 2026-07-29

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested source | `a50da4ad7a9cb26e8f976b95dd4fcceb3e41b8ff` |
| Candidate package | `out/candidates/a50da4a-reach-fp-parity-20260729-112514784Z` |
| `halo3xr.dll` SHA-256 | `AC6DF361770758EEA457126829F109D6A55893F8385CDC74E94879B74976E111` |
| Runtime | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz |
| Headset result | Black static-world geometry unchanged |
| Preserved evidence | `out/test-runs/a50da4a-reach-world-black-draw-hook-removal-fail-20260729-063330` |
| Preserved log SHA-256 | `FD28C568CA1FE55E378B85DFCE8A62FBE33CEDC37CE63942A81FDD5F47604659` |

The failed `839aed7` run and the exact E490 baseline both prove the obsolete
probe was still active. Before selected draws it allocated a staging GPU buffer,
issued `CopyResource`, blocked in `Map(D3D11_MAP_READ)`, and synchronously logged
five lines before calling the real draw. Even outside matching samples, its
detour queried viewport and vertex-buffer state for every small indexed Reach
draw. The captured shape was a fullscreen transition quad, not world geometry,
so this is a direct hook-isolation test rather than a claim that the probe is
already the proven root cause.

The exact candidate log contains `DrawIndexed diagnostic hook intentionally
disabled`, contains no draw/vertex samples, and proves the Reach camera and
stereo path armed. The black geometry was unchanged, so this detour is rejected
as the cause. It remains disabled because re-enabling synchronous GPU readback,
allocation, locked logging, and `fflush` in a render hot hook would knowingly
restore a safety defect. That cleanup does not advance the accepted pointer and
still requires the normal Halo 3 regression before a release.

### REJECTED: Reach owned-eye native SSAO/HDAO isolation - 2026-07-29

This result is evidence only and does not advance the public pointer above.

| Identity | Value |
| --- | --- |
| Source | `8d7af6e25418c2d2c86aa518d8d8f199a6e7080d` |
| Candidate package | `out/candidates/8d7af6e-reach-fp-parity-20260729-140858597Z` |
| `halo3xr.dll` SHA-256 | `99693E1488ACA887DEEA09BA4D438EC5D4C446FF5911A9ED04060342D0F7E7A6` |
| `halo3xr_launcher.exe` SHA-256 | `EB9EE3FFEE013D14EB44D7911FCC051921D7136A0704F0440791BE7C093D0843` |
| Deployment | Independently hash-verified in both Steam and Store `Halo_MCC_VR` folders; MCC was not launched and config was unchanged |
| Runtime | Steam edition, SteamVR/OpenXR 2.17.6, PSVR2 at 120 Hz |
| Headset result | Black static-world geometry unchanged |
| Preserved evidence | `out/test-runs/8d7af6e-reach-world-black-ssao-fail-20260729-091522` |
| Preserved log SHA-256 | `9567C19D0610E4D7C0651456517ABFF36CA6C15BCE2F33F8FE9661F06CEC7420` |

It makes exactly one behavioral change: the pinned retail native SSAO/HDAO
callee at `haloreach.dll+0x2A13A0` returns without drawing only for an exact
owned VR-eye invocation from the sole player-view call at `+0x26E81D`. Every
flat, screenshot, nested, unowned, non-current-eye, and non-Reach call executes
the original function. It does not clear depth, modify camera orientation or
culling, toggle title TLS flags, change OpenXR submission, or provide a 2D/stock
render fallback.

This boundary was not exercised by the prior shadow-mask candidates. HREK and
the pinned retail module prove that SSAO uses private downsample/depth/normal
intermediates and then writes its final result to shared surface index 2,
`_surface_shadow_mask`, after the old pre-player-view clear. The old asynchronous
readback sampled only the centre 64x64 texels; preserved clear-off logs show
eye-different mask contents from roughly 24% to 100% lit, disproving the stale
claim that this surface was permanently white. The exact executable chain and
preserved log paths are recorded in `docs/REACH-SIGNATURE-EVIDENCE.md`.

The runtime proof is deliberately stronger than an “armed” line. Install pins
the unique 35-byte callee entry, unique 21-byte caller context, exact rel32 call,
both final output calls, and the unique surface-index-2 helper. The hot detour
increments lock-free eye counters only; the worker logs BOUND and ARMED with
zero samples and then reports the first actual per-eye suppression. A headset
result is invalid if that execution line never shows both eyes advancing.

The exact log proves this candidate was not inert: suppression reached 3,602
calls in eye 0 and 3,602 calls in eye 1 with 66 unowned calls passed through to
stock, the per-eye first-person camera reported ACTIVE, and stereo held 120 Hz.
The user's headset result was unchanged. Native SSAO/HDAO is therefore rejected
as the black-world cause. The following revert disables the behavior while
retaining its verified code and evidence.

### ACCEPTED: Reach muzzle height - 2026-07-27

`muzzle_height_m` raises the re-parented muzzle effect origin - the flash and
the point rounds appear to leave - along the gun's own up axis. Headset-accepted
at **0.21**, which is the value in the shipped config.

The defect it fixes: `ReachReparentEffectMatrix` transfers Reach's effect
markers off the stock head-anchored weapon onto the controller-driven gun, and
that transfer is deliberately translation-only. Preserving the authored marker
offset exactly is what puts the origin on the barrel line, but it also carries
the authored *height* straight over, which read in-headset as several inches
low. The lift is applied after the projectile's origin and direction resolve, so
where rounds land is untouched - the user confirmed impact was already correct
before the fix and unchanged after. `Game_ComputeAimStick` was not modified.

This is NOT the rejected 2026-07-25 projectile-origin lineage. That work tried
to relocate the projectile spawn itself and failed on coordinate-space errors
("too high/left", then "too far right and rearward"), which is why the docs
reject centimetre offsets. This is a visual-origin calibration on an already
correct barrel alignment, and the exclusion of offset-based fixes in
`docs/REACH-SIGNATURE-EVIDENCE.md` refers to that different problem.

## Previous private cumulative source (superseded by 0.3.0)

The prior development pointer was commit
`a5524d3fe58e4ed5507c27429ccca52a3d4fdf7d` on `reach/campaign-parity`.
It descends from the accepted 0.2.2 runtime source and was an accepted private
milestone, not a public release or tag.

| Identity | Value |
| --- | --- |
| Headset-tested runtime source | `a5524d3fe58e4ed5507c27429ccca52a3d4fdf7d` |
| Build | Release x64, preset `release-reach-private`, ODST ON, Reach ON |
| Candidate package | `out/candidates/a5524d3-reach-private-20260724-023052584Z` |
| `halo3xr.dll` SHA-256 | `2BD8C0A8675C393715AD52F29301984B1A57CE45B5340070713F153E2CADE2A2` |
| `halo3xr_launcher.exe` SHA-256 | `ED0540A7A6F758543F1E828E73C35435D0CA092D259BAE285472804276F8A441` |
| Reach milestone | Shared virtual-controller transport only; Reach runtime hooks remain OFF |
| Preserved test evidence | `out/test-runs/a5524d3-reach-h3-odst-headset-pass-20260724-023358Z` |

### ACCEPTED: native per-eye FOV submission (ALVR double vision) - 2026-07-28

Headset confirmed by the user across a couple of runs covering **all three
titles** (Halo 3, ODST, Reach), so the Halo 3 regression for this shared-path
change is covered. Fixes double vision on ALVR without regressing the PSVR2
baseline.

| Identity | Value |
| --- | --- |
| Source | `73dfe323b2e5f87d2111f8594fb82c6d35897992` (`fix/native-fov-projection`, off `4b85134`) |
| Build | Release x64, preset `release`, ODST ON, Reach ON, ReachRender ON |
| Candidate package | `out/candidates/73dfe32-reach-fp-parity-20260728-141241183Z` |
| `halo3xr.dll` SHA-256 | `233B45DD9761EFCC2B7B366A5CE91B799EECD235B06068E7216AA73D82DEF014` |
| `halo3xr_launcher.exe` SHA-256 | `EAF6D3E622D04A9A2FDEBD1F7B10ECAE20FA3A5A40DC2289BE86445485AFE9B1` |

**Symptom.** Warped, stretched, doubled image in gameplay only - never in menus,
which are one flat quad identical in both eyes. Reported by testers on wired
Quest connections and reproduced by the user on ALVR.

**Root cause.** Halo can only raster a symmetric frustum, so `RenderViewHook`
renders a symmetric *cover* wide enough to contain the headset's asymmetric
per-eye angles, and `SubmitPreparedFrame` submitted that cover as the projection
layer's FOV across the whole slice. That is legal OpenXR and SteamVR's
compositor resolves it, but ALVR lens-corrects and reprojects from its own view
parameters rather than the layer's, so a layer whose FOV is not the native view
FOV is sampled with the wrong frustum. Upstream `alvr-org/ALVR#1306`, open since
2022: "ALVR does not account for the missing space in the frame when the FOV is
any lower than 100%".

**Fix.** Submit the runtime's own per-eye FOV and let `subImage.imageRect`
select the sub-rectangle of the cover corresponding to it, deriving the
submitted FOV back from the rounded integer rect so the pair describes one
frustum exactly. No resolution is lost - the compositor was already cropping to
that region. **No per-headset constants**: everything derives from what the
runtime reports, so this generalises to any headset and any compositor.

**The A/B that found it.** One Quest 3, one build (`2458ed8`), identical
reported optics (`L-54.0 R40.0 U44.0 D-55.0`, IPD 69.3 mm): clean through
Virtual Desktop and Steam Link, doubled through ALVR at both 120 Hz and 90 Hz.
The user's decisive observation was that the image snaps correct the instant the
SteamVR dashboard composites it, which is ALVR drawing at its own native FOV.

Accepted-run evidence, same DLL, zero `M2 WARNING` lines:

| Headset | Submitted layer |
| --- | --- |
| Quest 3 / ALVR | `cover 54.0/55.0 deg -> rect (512,444)+2112x2296 of 2624x2740, fov L-40.0 R54.0 U44.0 D-55.0` |
| PSVR2 (regression) | `cover 61.5/53.0 deg -> rect (897,0)+2795x3764 of 3692x3764, fov L-43.4 R61.5 U53.0 D-53.0` |

The submitted FOV equals the runtime's native per-eye FOV exactly in both. The
PSVR2 rect is full height because that headset's vertical FOV is symmetric, so
only the horizontal over-render is cropped.

**Two theories refuted on the way - do not resurrect.** (1) `xrLocateViews`
running twice per frame (`2458ed8`, parked on `fix/stereo-single-locate-per-frame`):
a real defect and a correct-by-spec fix, but not this bug - the symptom was
unchanged. (2) Quest 3's vertically asymmetric FOV versus PSVR2's symmetric FOV,
built on the hardcoded `kNativeRenderWidth/Height = 2912x2100` whose launcher
comment cites "PSVR2's symmetric coverage ... aspect near 1.386:1" - killed by
VD and Steam Link being clean on the same Quest 3 optics. Refresh rate was also
ruled out: ALVR missed ~17x more frame deadlines than VD, yet the 90 Hz ALVR run
still doubled.

**Process note.** Preserved logs were labelled by headset from a *code comment*
rather than evidence, and two theories were built on that wrong attribution
before the user caught it. The mod did not call `xrGetSystemProperties`, so no
log recorded which headset ran - SteamVR fronts PSVR2, Index, Virtual Desktop,
Steam Link and ALVR alike. Fixed separately.

### ACCEPTED: Reach VR crosshair + left hand + no-unhook - 2026-07-26

Headset confirmed by the user in one session. Reach is still experimental; the
product pointer is unchanged.

| Result | Candidate | DLL SHA-256 |
| --- | --- | --- |
| VR crosshair on the controller ray | `bc66451` | `CD6EC9BC12061F7B6E63CFDCA4F5E677E0D036C0A08B52129C789A5A03DFB265` |
| Left hand returned to the controller | `32f666e` | `B3FEA059D76E7405AAF4D8C6C88085C56BB3107F9A83CFA4113B5176F1FC2211` |
| Failed eye frame skips, never unhooks | `c43e5cc` | `680F0A9F677F707E7749E9A8FD16C4D0DAE16DDD4CE09F428500418F41CE50C0` |

User confirmation: the crosshair is present and aimable, bullets track it, the
left hand is back on the controller, and floating hands work.

**Why the crosshair was missing for so long.** Reach never published a title
lifecycle. Halo 3 has `PublishHalo3Lifecycle` and ODST has
`PublishOdstLifecycle`; Reach had no equivalent, so its runtime slot reported
`armed=false` with zero capabilities permanently.
`TitleRuntimeMaskUnarmedCapabilities` then stripped every arm-gated capability,
`Game_HasTitleCapability(TitleCapability_ControllerAim)` returned false, and the
reticle admission short-circuited before `EnsureReticleChain` could run - the
log showed the stereo swapchain created with no crosshair chain beside it.
`PublishReachLifecycle` fixes the gap. The reticle additionally now asks
`Game_OwnsReachAuthoredReticle()` directly, exactly as ODST asks
`Game_IsCameraOnlyBringup()`, so it no longer depends on the shared snapshot
being settled.

**ArmIk is deliberately withheld** from `kReachRuntimeCapabilities`. Publishing
the lifecycle turned on every arm-gated capability at once, and `ArmIk`
immediately attached the left hand to the player's face because Reach's arm IK
is not solved. Grant it only after that is proven in the headset.

**Two silent teardown paths removed.** Both called
`Game_RejectReachAuthoredReticle`, which disarms the core and removes every
hook. `projection.viewCount != 2` fires on an ordinary one-frame ordering gap,
and `!handled` fired once after 3.5 minutes of correct rendering. Both now skip
the frame, keep the core armed, and log a rate-limited reason. This is the
behavior Halo 3 and ODST already had, and it is why the same binary could work
in one session and die in 9 ms in the next: the failure was timing-dependent and
the response was permanent. `Game_RejectReachAuthoredReticle` now takes a reason
string and all callers name themselves; both fixes above were diagnosed from
that single log line in seconds.

**Still open, reported in the same session:**
- Reach's flat centre crosshair is still drawn. The CHUD alpha array at
  `chud_globals + 0x32C + i*4` (crosshair is `i=2`, i.e. `+0x334`) is REAL -
  derived from every `chud_fade_*_for_player` implementation - but writing it is
  inert: it read `1.000` across 860 live samples while the mod wrote `0` every
  frame. The real draw path is not located.
- World-anchored CHUD navpoints (Noble Team markers) sit in the wrong place in
  3D and move inversely with the weapon. Not CHUD memory: nothing there changed
  while the gun swung. HREK `chud_navpoints.cpp` gives the structure - 20
  entries, stride `0x88`, `position_worldspace` at `+0x3C/+0x40/+0x44`, reached
  through a TLS block. Retail chain: `ai_add_navpoint` ->
  `haloreach.dll+0x1A1A7C` -> worker `+0x6C2E68`, which reaches its data via TLS
  slot `+0x30`.
- One muzzle flash is stuck at screen centre while another tracks the weapon.
  Very likely the same transform bug as the navpoints.



- The installed DLL and launcher were hashed separately after the manual copy
  and matched the candidate manifest exactly.
- The first runtime-log line reported source
  `a5524d3fe58e4ed5507c27429ccca52a3d4fdf7d`, ODST ON, Reach ON, compiled
  `Jul 23 2026 21:30:25`.
- Title coverage in one MCC session was Reach, Halo 3, ODST, then Reach again.
  The user confirmed Reach worked without breaking Halo 3 or ODST.
- Reach was detected with controller-only admission. Its XInput
  `reads`/`padValid`/`merged` counters continued rising while stereo remained
  off, as required for this milestone.
- Halo 3 subsequently armed stereo and its accepted first-person path. ODST
  then completed preflight, armed stereo/6DOF, exercised native pause teardown,
  and safely returned ownership before Reach regained controller transport.
- This result does not authorize or claim Reach camera, stereo, 6DOF, aim,
  movement, HUD, IK, haptics, or lifecycle hooks.

### Reach controller and cross-title headset confirmation - 2026-07-23
### Partial/failed Reach floating-hands / FP-camera result - 2026-07-25

Candidate `a1dcb7beeb0bec56b3b7c04a6f15a897eaa63fa4` combined the
Reach-only forced-floating-hands palette policy with the first Reach
first-person camera-rebuild detour. Its exact packaged and deployed identity
was:

| Identity | Value |
| --- | --- |
| Candidate package | `out/candidates/a1dcb7b-reach-fp-parity-20260725-085113864Z` |
| `halo3xr.dll` SHA-256 | `68793B3052EE2AE60F197526A7A215913FA7EEEFC3BB4177A613EE4AAFFF70A0` |
| `halo3xr_launcher.exe` SHA-256 | `F9C3778B5AD993E26039BA731698A3E2BCD10159F00F6C31C15C5E450DE516FB` |
| Headset result | Hands were a little better, but the gun/both-hands FOV, projection, apparent location, and tracking-distance coverage were incorrect |

The runtime log proved that the Reach FP-camera hook installed and the camera
core armed. It also proved that the private-palette path executed by reporting
`Reach FP forced floating-hands active`. It did **not** prove that the
post-original world-camera substitution executed.

Pinned retail and HREK static evidence now explains that missing proof. Every
visible FP wrapper copies the outer camera into, then pushes, a dedicated nested
camera-stack workspace at `haloreach+0x00CFAC20`. That workspace holds the
compact camera at `+0`, the derived/projection block at `+0x1E4`, and callback
`haloreach+0x0000C380` at `+0x2A8`; the wrappers pass the exact FP view at
`haloreach+0x00BB8F68` to rebuild `0x00286C6C` before popping the nested
workspace. The candidate instead required the live stack top to equal the outer
default workspace `0x00C9FAE0` and selected that outer workspace's `+0x1E4`.
That guard is impossible during a normal visible FP rebuild, so the intended
camera swap returned before its copies and uploader call.

The corrected nested-workspace candidate is pending. `a1dcb7b` is a
partial/failed headset result and does **not** advance the accepted pointer;
that pointer remains `a5524d3fe58e4ed5507c27429ccca52a3d4fdf7d`.

### Unaccepted Reach nested FP projection / residual ownership result - 2026-07-25

Candidate `fe0c48e4282fc17d05ca2de0d8dcaa36c95483dd` corrected the
first-person camera destination to the exact nested workspace. Its packaged and
separately verified deployed identity was:

| Identity | Value |
| --- | --- |
| Candidate package | `out/candidates/fe0c48e-reach-fp-parity-20260725-094807875Z` |
| `halo3xr.dll` SHA-256 | `5D1767B492A5369EABAD312B10F5528588D76B2AD4B647BCB64284717F310E1A` |
| `halo3xr_launcher.exe` SHA-256 | `0758315C8091A46E7F1798FE1B1D7F1E704A61B9BF43CF799467A1A702F5E5E4` |
| Headset result | The major FP presentation improved, leaving a small left-hand fragment/ribbon following the right controller and a small hand/gun offset during physical head turns |

The runtime first line matched the exact source. It reported both-eye nested
world-camera uploads, `Reach FP forced floating-hands active: body=47 live=52`,
and the native weapon-IK bypass. The remaining fragment is therefore downstream
palette ownership, not a failed projection swap or native IK overwrite.

Official HREK exports identify the exact leak. Spartan glove vertices blend
`l_hand` with its parent `l_forearm`, while every `flair_forearm` permutation is
rigid on `l_radius`. Candidate `754b34b` therefore tested moving the complete
hidden left influence branch with the controller while retaining hand-only
visibility. Its exact headset failure below proves that ownership alone is not
enough when those hidden records are still collapsed at separate joint pivots.

The head-turn offset is a separate Reach-only defect: prepared wrist targets
are already absolute `gameplayBase + tracked room offset`, but the rejected
diagnostic-era render-root rebase adds tracked head translation again at both
marker and palette consumption. Its removal remains a separate headset
candidate so the two behavioral corrections are not stacked. `fe0c48e` remains
unaccepted and does not advance the pointer above.

### Failed Reach left-branch ownership / collapse-pivot result - 2026-07-25

Candidate `754b34b2acfdae81c6dbd833d3b7bd7b0e1e7b3d` moved the exact
HREK-proven left influence closure with the left controller, then retained the
existing hand-only visibility mask. Its exact packaged and deployed identity
was:

| Identity | Value |
| --- | --- |
| Candidate package | `out/candidates/754b34b-reach-fp-parity-20260725-115117345Z` |
| `halo3xr.dll` SHA-256 | `BE95F23246B5AAAD1C0C492C7C4660D3EEDB075D0A610B47BF924129C899EDD0` |
| `halo3xr_launcher.exe` SHA-256 | `3A8C3216B64C910E6EB60029D3DA63CA9305BD857297C47F227B7E7163CABFE0` |
| Headset result | Slightly better, but a severe black wrist/forearm ribbon still stretched from the independently tracked left hand; gun and both hands still drifted slightly with physical head turns |

The installed hashes and first log line matched this exact candidate. Reach
reported the Spartan 47-over-52 private palette, native weapon-IK bypass, and
both-eye nested world FP projection active with no palette validation,
finite-value, or frame-order warning. The preserved log is
`out/test-runs/754b34b-reach-wrist-collapse-fail-20260725-070700Z/halo3xr.log`,
SHA-256 `ED23B61BE925F78FB6C52B3A7258808EE66B41B5FBA20B4E79A5B012A9D8F9C5`.

The official HREK mesh exporter resolves the remaining artifact. In both
Spartan arms meshes, all 103 nondegenerate left `spartan_rubber_suit` triangles
span two to four of `l_upperarm`, `l_forearm`, `l_humerus`, and `l_radius`.
The glove independently has 58 `l_forearm`/`l_hand` cross-weight vertices over
96 triangles. The failed candidate moved those records together, but the final
floating-hands pass still assigned scale `0.0001` at four distinct arm-joint
translations. Linear skinning therefore drew the photographed strip between
those collapsed pivots. Elite independently has 51 cross-weight vertices over
83 hand/forearm triangles plus 96 body triangles spanning multiple auxiliary
bones. The replacement candidate collapses all four hidden auxiliary records at
the final solved left-wrist record, retaining the hand-only visible mask.

The head-turn drift remains the separate prepared-target rebase defect described
above and is not stacked into this wrist candidate. `754b34b` is failed and does
not advance the accepted pointer.

### Partial Reach left-wrist anchor pass / residual right-wrist result - 2026-07-25

Candidate `765604cc631a1c0042a468738c7545ffbbd9208a` replaced the failed
left-branch behavior by co-locating all four hidden left-arm skin influences at
the solved left wrist before their tiny collapse. Its exact packaged and
deployed identity was:

| Identity | Value |
| --- | --- |
| Candidate package | `out/candidates/765604c-reach-fp-parity-20260725-121927950Z` |
| `halo3xr.dll` SHA-256 | `196BE16C85D2837DA9E8822FC896122F6CFC47653196FD020F9D0109D2F804AE` |
| `halo3xr_launcher.exe` SHA-256 | `C09CC08C8B6463A356BE05567374EBDB0426F2F1379B2ADAB59CB93AB2671C2B` |
| Headset result | The left-hand ribbon was fixed, while the equivalent severe stretched strip remained at the right/weapon wrist |

The installed hashes and first log line matched this exact source. Reach again
reported the Spartan 47-over-52 private palette, exact native weapon-IK bypass,
and both-eye nested world FP projection, with zero frame-order failures and
clean teardown. The preserved log is
`out/test-runs/765604c-reach-left-pass-right-wrist-fail-20260725-122522Z/halo3xr.log`,
SHA-256 `7E18D3D53127A1B5235B99C4395E01DA9C8296CDE5CFCE55B5B228F02D730F57`.

The same official HREK exports independently resolve the right artifact.
Spartan has 59 `r_forearm`/`r_hand` cross-weight vertices over 96 triangles and
71 right `spartan_rubber_suit` vertices over 103 multi-auxiliary triangles.
Elite has 51 cross-weight vertices over 83 triangles and 75 right body vertices
over 99 multi-auxiliary triangles. Those right auxiliary output nodes map in
both official graphs to sources `{6,9,10,14}`
(`r_upperarm/r_forearm/r_humerus/r_radius`), exact mask `0x4640`; the solved
right wrist is source 13. Appended held objects begin only after body prefix 47
for Spartan or 41 for Elite, so this mask cannot touch the weapon. The forward
candidate retains the headset-confirmed left anchor and applies the identical
wrist-local collapse only to this independently proven hidden right set.

The physical-head-turn drift remains a separate prepared-target rebase defect
and is still not stacked into the wrist work. `765604c` is a narrow left-wrist
headset pass but not a cumulative accepted candidate; the accepted pointer
remains `a5524d3fe58e4ed5507c27429ccca52a3d4fdf7d`.

### Reach both-wrist anchor pass / remaining head-translation drift - 2026-07-25

Candidate `7467d264957b9753a29f7e003b7415b8d888adfb` added only the
independently HREK-proven right-wrist collapse anchor while retaining the
headset-confirmed left anchor. Its exact packaged and deployed identity was:

| Identity | Value |
| --- | --- |
| Candidate package | `out/candidates/7467d26-reach-fp-parity-20260725-124219703Z` |
| `halo3xr.dll` SHA-256 | `7CBED662A7428644FDAAD58A780FEE329900436E620CDBC0D5B65640E710C057` |
| `halo3xr_launcher.exe` SHA-256 | `B0098D1F73227B24BCBE79C050DD2625B0989BA203E65B2D64FE6EF2360A492F` |
| Headset result | Both wrist-ribbon fixes looked great; the gun and both hands still followed physical head translation slightly |

The installed hashes and first log line matched this exact source. Reach
reported the exact native weapon-IK bypass, forced 47-over-52/53 floating-hands
palettes, both-eye nested world FP projection, zero frame-order failures, and
clean teardown. The preserved log is
`out/test-runs/7467d26-reach-both-wrists-pass-head-drift-20260725-125800Z/halo3xr.log`,
SHA-256 `F15C1C1A141E0315F0DF6D3A6E0F8B7238E91E5844A0A490787C372FA4BB1FA0`.

This accepts the two wrist-local collapse anchors as the basis for the next
isolated candidate, but does not advance the cumulative accepted pointer. The
remaining motion is the separate prepared-target rebase defect: the controller
targets already equal pre-head gameplay base plus tracked room displacement,
then Reach alone adds the render root's head translation again. The next
candidate removes only that second addition, matching Halo 3/ODST's absolute
controller-world target ownership. The accepted pointer remains
`a5524d3fe58e4ed5507c27429ccca52a3d4fdf7d`.

### Reach wrist/head-decoupling pass / projectile-origin mismatch - 2026-07-25

Candidate `03396fa5201c8b086caf2a452ab29964b8dee609` removed only the
rejected second render-head translation from the already absolute prepared
controller targets while retaining both headset-passed wrist anchors. Its exact
packaged and separately verified deployed identity was:

| Identity | Value |
| --- | --- |
| Candidate package | `out/candidates/03396fa-reach-fp-parity-20260725-130616667Z` |
| `halo3xr.dll` SHA-256 | `3677FBF2BF69E991E9204B8F7D0D587041AD65DC7C77B8E3FE509EADE6BC70EE` |
| `halo3xr_launcher.exe` SHA-256 | `A4B475B5DF2DBA2E36C0AFFCDF26CFEC268CCEC1261E0A1C64095B07E28BCB29` |
| Headset result | Both wrist fixes remained good and the gun/both hands stopped following physical head translation; projectile origin still appeared too high/from the left of the controller-owned gun |

The preserved runtime log is
`out/test-runs/03396fa-reach-head-decoupling-pass-muzzle-offset-20260725-133049Z/halo3xr.log`,
SHA-256 `35719DBD951CAFD031B89BDAA2AED3CF1612F6A8D04EC5F83DCD8A88792BC9BC`.
This is a narrow Reach Spartan wrist/head-pose acceptance only. Halo 3, ODST,
Elite, broader weapons, lifecycle transitions, HUD, and crosshair were not
regression-tested, so the cumulative accepted pointer above remains unchanged.

The follow-up projectile/marker lineage is rejected. Candidate
`354327bb0d033e081fb95e46cb74010c36c2e083` packaged as
`out/candidates/354327b-reach-fp-parity-20260725-172635238Z` with DLL SHA-256
`9131E8337D26204ADE9E3B71AB08354BE975073D28C2614B093259201C4CEFAB`.
The same unaccepted runtime remained in
`f9f4fecee70bccb2d91c77319c438c237d13fb85`, packaged as
`out/candidates/f9f4fec-reach-fp-parity-20260726-144300646Z` with DLL SHA-256
`9BC56CC275F374542752941506BE141FA78C22E06417B632F8AC6DF21D3745A8`.
The current headset observation is that the projectile origin is too far right
and rearward instead of centered in front of the visible barrel. Neither
candidate advances an accepted pointer; the installed hash for that observation
still needs separate confirmation before it can be attributed to one exact DLL.

The failed projectile-origin relay, first-person marker-query/composer detours,
published-marker path, firing-frame write, and every runtime fallback belonging
to that experiment have now
been surgically removed. Reach bullets and muzzle markers remain wholly stock
while the accepted camera, hands, display, image-quality, and frame-pacing work
is preserved. Future bullet-alignment work requires fresh proof from official
HREK/Reach mod tools only. Archived retail-binary or Reclaimer-derived facts are
historical context, not admissible evidence for another runtime implementation.

### UNTESTED Reach HREK-only authored-crosshair bridge - 2026-07-26

The forward candidate adds only the Halo 3/ODST authored-crosshair transaction
to the preserved Reach camera/hands/display/image-quality/frame-pacing line.
Its new proof uses official HREK build `2023.07.17.176677.1-QFE1` and official
mod-tool tag exports only. No retail-binary or Reclaimer-derived fact selected
the hook, descriptor layout, scripting class, or widget.

The pinned official executable hashes are `reach_tag_test.exe`
`CBDD8448A87A433B0DFFC0DE47D06DB7A18B4BF868B96B057135DAA86790ABA8`,
`reach_tag_play.exe`
`450DFFE824DDE4C9866E4448491B8B41D82995DC93159260A4DEF07D059E732E`,
and `sapien_play.exe`
`1FDA21569B38C189EC88124C1A682DCCED8FBEE11ACFD4D2605F46663B26175B`.
HREK's nine-value CHUD scripting enum identifies exact class `2` as
`crosshair`. Official XML export of all 63 top-level CHUD definitions found 283
top-level collections, including 85 class-2 collections across 55 definitions.
The assault-rifle widget exposes authored normal/enemy/friendly color outputs,
so the bridge captures Reach's live reticle art and hostile-red/friendly-green
state rather than drawing a procedural approximation.

The mandatory cold proof locates the common exact 33-byte HREK
`chud_draw_widget` entry in the loaded title and accepts only one complete
optimized official layout: `reach_tag_play.exe` RVA `0x0056B15C`, body `0x424`,
SHA-256
`81EBDE1BB1CF9337C01BA861B0CAF70980EBF6871DE079334B5BFB77ABA8978E`,
or `sapien_play.exe` RVA `0x008265F4`, body `0x483`, SHA-256
`C0EA71FA6BD0D26CA2EBAAED58FA182FE0CD8288274D3459271AD62CA2B9099E`.
It also verifies argument retention, the fifth argument, descriptor-`+0x04`
class read, unwind start, and complete body boundary/size. The hashes pin the
official HREK evidence bodies; they are not loaded-title hash requirements
because the explicitly wildcarded rel32 operands differ by official link
layout. Full disassembly of both pinned bodies has no call or tail-jump edge to
its own entry, so the hook's one original-trampoline call is non-recursive.

Zero matches, multiple matches, or any boundary/body-size/fixed-layout mismatch
rejects installation before the Reach camera/VR core owns anything. No Reach VR
feature claims that frame and no alternate Reach VR mode is armed. The crosshair
bridge is mandatory, and no VR-with-flat-stock-crosshair, widget-name,
procedural, or approximation implementation exists. Explicit shared
configuration remains authoritative: `crosshair=0` suppresses the widget,
while `kill_reticle=0` requests stock drawing by user choice.

Before creating any hook, cold admission preallocates the authored-reticle
swapchain, every swapchain RTV, and the private authored texture/RTV under the
Reach display resource lock. The Reach CHUD hot hook then uses only the prepared
capture entry: it validates existing resources and the prepared frame, saves
fixed D3D state, and performs no allocation, signature scan, logging, or lazy
resource creation.
OpenXR session/device readiness is release-published only after `xrBeginSession`
and the exclusive frame-wait worker both succeed, and is revoked before stopping
or fatal drain. It is a retryable install precondition. A title generation is latched rejected
only after that readiness proof and a real swapchain/RTV/texture failure; merely
winning the Reach Present proof before session creation cannot poison it.

During owned stereo, an unreadable HREK descriptor or eye index outside `0..1`
is an explicit transaction rejection, not permission to draw a possibly flat
class-2 widget. Both conditions use the same eye invalidation, exact-generation
latch, disarm, and teardown path as capture loss. Only draws outside Reach
ownership and readable non-class-2 widgets select automatic stock drawing;
`kill_reticle=0` remains the separate intentional user configuration.
If teardown or an explicit VR disable revokes ownership after an eye has already
entered the hook, that matching eye is marked failed and its entire CHUD pass is
suppressed. The same rule applies as soon as the title adapter stops naming
Reach active or publishes a different Reach module generation, even before the
worker finishes hook teardown. The abandoned eye cannot fall through to a flat
draw, capture shared art, or publish an eye copy.

HREK's source-named `player_view_render` order places
`chud_draw_screen_LDR` and `chud_draw_screen` after first-person work and before
return, so class-2 capture remains inside the same per-eye world, weapon, native
CHUD, and capture transaction as Halo 3/ODST. The configured capture eye feeds
the authored VR quad while the other eye suppresses the flat copy.
Every newly admitted outer attempt invalidates authored readiness left by an
earlier attempt in the same prepared frame. If either eye emits class 2 while
`crosshair=1` and `kill_reticle=1`, that exact pair cannot publish until the
configured-eye capture completes. When Reach itself emits no class-2 widget in
either eye, no quad is submitted; that is the current authored gameplay state,
not reuse of earlier art or selection of another reticle implementation.

If that prevalidated capture target is lost, the hook suppresses class 2,
immediately invalidates the current eye pair, disarms the core, latches failure
for that exact title-module generation, and requests the existing verified
six-hook teardown. Callback plus frozen-thread detour/trampoline quiescence is
required before removal and module release, and the latch prevents reinstall
until a fresh generation. Upload failure takes the same path only after
rechecking the compositor's captured generation against the active Reach title,
live adapter generation, and installed owner; stale failures cannot disarm a
new generation.
An already claimed transaction is never rerun through Reach's flat renderer.
While teardown is pending, disarmed matching inner calls are suppressed; only
the pre-ownership safety interval may execute the untouched engine call.

The compositor admits Reach's projection only when Reach is active, both Reach
eye copies completed the exact current prepared serial, live installed/armed
stereo ownership remains valid, the XR world swapchain acquire and wait both
returned exact `XR_SUCCESS`, both eyes resolved successfully, and the world
image release returned exact `XR_SUCCESS`. If
class 2 was captured for that exact attempt, its authored texture must be ready
for the same serial and its acquire, wait, upload, and release must each complete
with exact `XR_SUCCESS` before the quad is eligible. A timeout or
session-loss-pending result aborts the whole layer set and enters terminal
session recovery. If `xrBeginFrame` already succeeded, an empty `xrEndFrame`
closes that frame without referencing a potentially outstanding swapchain image.
Reach never enters this path through
`TitleCapability_ControllerAim` alone. A Reach upload failure does not enter the
legacy transparent/procedural swapchain-maintenance path, and live exact
ownership is rechecked after upload before any Reach layer can be queued. The
failure clears both copied-eye serials and ages out authored readiness, so the
current world projection, authored quad, and scope layer all disappear together.

This candidate does **not** enable Reach HUD layout. HREK exposes three skins
with five curvature records each, so Halo 3/ODST anchors, counts, and destination
offsets cannot be copied. HUD sizing, aspect, curvature, and height remain
withheld for a separate official-HREK candidate. Bullets and muzzle markers also
remain wholly stock after the rejected projectile lineage was removed.

The candidate is **UNTESTED**: it has no accepted artifact identity or headset
result and does not advance the accepted pointer. Acceptance requires the exact
clean commit/DLL hash to show no flat center reticle, authored VR reticle art,
friendly-green and hostile-red changes, shared crosshair configuration parity,
safe title transitions/teardown, and a Halo 3 regression for the touched shared
authored-reticle composition path.

## ACCEPTED: Reach vibration, cutscene facing, and a LIVE crosshair - 2026-07-27

**Headset confirmed by the user across all three titles:** "I tested all three
halos, and their crosshairs are working good. The performance is good."

| Identity | Value |
| --- | --- |
| Accepted source | `716a635362d0b26d52bfe74d5d271a10768ae3c3` |
| `halo3xr.dll` SHA-256 | `B97ED6CDF213421D2FBE05B8B9BFFA1270B8C314370B457BF32CA3A5AB5FEA54` |
| `halo3xr_launcher.exe` SHA-256 | `B32C0001F297465C0924E18A85126D0C01F5084FEDF3F9389CA49160BFBA66BF` |
| Branch | `reach/frame-skip` |

Confirmed working: Reach controller vibration; yaw realignment at authored
cutscene cuts; the authored crosshair now animating with live colour states on
Reach, Halo 3 and ODST; `crosshair=0` hiding every crosshair including Reach's
flat one; and no frame-rate cost.

> The "yaw realignment at authored cutscene cuts" accepted here was only
> **partly** true, and this entry overstated it. It realigned at cutscene start
> and end but missed the cuts in between; see "ACCEPTED: Reach cutscene facing
> at every shot cut - 2026-07-27" below for the evidence and the completed fix.
> The acceptance was taken from a session that never watched a shot change
> mid-cutscene - a reminder to confirm the specific behavior, not the feature
> name.

### The crosshair blackout: one defect behind three symptoms

The VR crosshair displayed a single frozen snapshot. The art key describes
WHICH widgets drew, not how they look, so Reach's animation frames, red/green
state tints and fades all left it unchanged and the upload was skipped
forever. Three reported symptoms, one cause: no animation, dead colour states,
and - when the frozen snapshot was captured during a fade-out - a permanently
blank crosshair.

The decisive evidence was a heartbeat log at the moment of a blackout: every
gate open (`authoredThisFrame=1`, quad `SUBMITTED`, `heldArt=1`) with the key
frozen at `DFFEE7EAB8A8F81F` to the end of the session. The art was captured
and displayed the whole time; it was blank. The fix refreshes on the existing
frame-gap floor, which keeps the ~4-5ms blocking upload off every frame at
120Hz.

**Four wrong theories preceded it, and the pattern is worth keeping:** the
CHUD alpha/fade write, the redirect entry point (which cost Reach's 3D), the
art-key ordering, and "Reach stops emitting the widget". Each was plausible
and each was disproven by instrumentation. Two lessons: a diagnostic that
cannot distinguish its own hypotheses is worse than none (the class histogram
and the quad heartbeat each overturned a conclusion drawn from silence), and
the player's description of *related* symptoms - here "the animations are
gone, it doesn't turn green" - identified the mechanism after four
code-first attempts missed it.

### Still open, with the user's exact requirements

- **Character tags AND objectives must follow the HEAD ONLY.** They currently
  float with the hand as well. See the HUD element identification and
  navpoint evidence below.
- **Both muzzle flash elements must follow the HAND.** One already does; the
  second is stuck at the face. Move it, do not suppress it.
- **Reach needs a pause state**, matching Halo 3 and ODST: native pause should
  publish `RuntimeMode::Paused` and switch presentation to head-locked 2D via
  `VR_RequestPausePresentation`, then restore stereo on unpause. Reach
  currently publishes `Gameplay` whenever its core is armed
  (`Game_AutoVrTick`), so a pause menu keeps full stereo and the menu stick is
  treated as locomotion - the known limit recorded in
  `Game_MoveStickIsLocomotion`. Halo 3's `LocateNativePauseFlag` /
  `ReadOdstEnginePaused` are the shape to follow; Reach's own pause flag still
  needs locating from HREK.
- **Subtitles need a readable plane** (they are the interface text layer, not
  a CHUD widget - see below).

### ACCEPTED: Reach cutscene facing at every shot cut - 2026-07-27

Headset confirmed: "That one, it works now."

| Identity | Value |
| --- | --- |
| Accepted source | `32d92a74db4d41a46e5956bbf7c0737abe1283e7` |
| `halo3xr.dll` SHA-256 | `F7E9CBD0906F081AC01F0F8D51A416DA8654C39ECC0D0C6EE7BB7224FA9131D2` |
| `halo3xr_launcher.exe` SHA-256 | `B32C0001F297465C0924E18A85126D0C01F5084FEDF3F9389CA49160BFBA66BF` |
| Candidate package | `out/candidates/32d92a7-reach-fp-parity-20260727-132926196Z` |
| Build | Release x64, preset `release`, Halo 3 + ODST + Reach |
| Branch | `reach/frame-skip` |
| Preserved evidence | `out/test-runs/32d92a7-reach-cutscene-facing-pass-20260727Z` |

The earlier acceptance realigned only at cutscene start and end. The user
reported still facing the wrong way, and narrowed it precisely: "I can't even
tell if the first shot is oriented, but I know for sure that the subsequent
ones aren't," in every cutscene.

Root cause, from the installed 08:19 log on build `5cd1181`: cut detection
watched cinematic-globals `+0x28` alone, and that stamp changed exactly twice
across a 65-second cutscene (08:12:52, 08:13:20, then the exit at 08:13:57).
Every shot in between inherited the previous shot's facing. `+0x28` is an
authored boundary stamp, **not** a per-shot marker - the older comment claiming
it "changes at every authored cut" was written from a session that only ever
observed cutscene starts and ends.

The fix detects the cut from the authored camera itself: within a shot the
cinematic camera moves continuously, at a cut it jumps. Each frame compares the
pristine stock camera against the previous frame's and treats a yaw step over
~20 degrees (`kReachCineCutYawRadians`) or a position step over 2 world units
(`kReachCineCutJumpUnitsSq`) as a cut, ORed with the existing stamp and
fade-end edges. Thresholds are deliberately loose: the fastest authored whip
pan is ~1.7 deg on a 120 Hz frame and ~7 deg across a 33 ms hitch.

Gated on the cinematic-in-progress byte at `+0x24`, newly read from the same
log that proved `+0x26` and `+0x28`: it reads 1 only for the duration of the
cutscene and 0 through the menu and all following gameplay, so the test cannot
fire during play, where the camera legitimately jumps on snap turn, respawn and
vehicle entry. Halo 3 and ODST keep their own scene/shot path untouched.

The worker line now reports the split - `Reach cutscene facing: realigned to
the authored camera (cut N, M of them from the camera-jump test)` - so a future
log shows which detector is carrying the cutscene.

### ACCEPTED: Reach muzzle flash on the gun - 2026-07-27 NEW REACH BASELINE

Headset confirmed: "Oh, you fixed it, dude. This is our new baseline."

| Identity | Value |
| --- | --- |
| Accepted source | `b942078c9870509db53ebb5de5dc7d0f32a22a75` |
| `halo3xr.dll` SHA-256 | `F3CFF979E86A8C9AA12492C7ED29C027CCBFE5B749722F56E62C4799FE47655A` |
| Branch | `reach/frame-skip` |

The face-stuck muzzle flash was first-person-only particle systems authored on
non-majority markers. Six weapons were affected (assault_rifle, dmr, needler,
sniper_rifle, spartan_laser, spike_rifle); the fix rewrites each odd system's
location u16 in the LOADED tag onto the majority marker (primary_trigger,
index 0 in all six), per weapon as the player picks it up, restored on
teardown. Decode chain read from the engine's own emission gate 0x001D4DB4;
handle table/pool resolved from a unique 20-byte signature. No IK, no bones,
no code patch. Log proof: "Reach muzzle: RETARGETED ...".

The road there is preserved in this file and the commit history: the proof
that both flashes are camera-mode-1 systems (a4a2ed4), the eliminations
(effect-location resolver, CHUD widgets, mode-2 systems, lights), and the
disabled experiments behind constexpr-false flags.

**Halo 3 + ODST regression: PASSED 2026-07-27** - user tested both titles on
this baseline: "no discernible regression at least on my end." The cumulative
build's cross-title contract holds with all the new Reach hooks in place.

Outstanding after this baseline: bullets/crosshair-vs-gun-mesh verification
pass (user-requested, no IK translation); rain (deferred known bug); spartan
laser side-vent steam now at the muzzle (cosmetic, exempt on request).

### ACCEPTED: Reach character tags and objective markers off the hand - 2026-07-27

Headset confirmed: "you actually fixed the character tags and objective
markers. It's not perfect, but it's still very doable, so we can stick with
that." Crosshair, guns, hands and 3D depth all confirmed unaffected. Halo 3 and
ODST regression NOT yet run.

| Identity | Value |
| --- | --- |
| Accepted source | `6bd17db47b6e653ec66198287df19b9b4d56e6aa` |
| `halo3xr.dll` SHA-256 | `121D4FDFEBDC174691018156978AF3F6F38EAA5989019793865B5E0EBCC8528F` |

Stock Reach has ONE camera parent: `render_camera_from_observer_camera`
(`haloreach.dll+0x00287DFC`) builds both the world render camera and the CHUD
projection camera from the same observer camera. Our aim steering points that
observer camera down the controller ray while head-look goes to a private
render-side copy, so everything except the world followed the hand. The seventh,
optional hook restores the single parent by copying the per-eye head camera the
world was already rendered from into the destination for every non-world call
site.

**The RAIN is not fixed.** The site telemetry from the accepted session shows
only three of the six call sites were ever exercised - site 2 (world render,
never corrected), site 3 (`+0x26FA47`), and site 5 (CHUD projection).

> **CORRECTION 2026-07-27.** An earlier version of this section concluded from
> that telemetry that "no rain consumer goes through
> `render_camera_from_observer_camera` at all; the rain reads a different
> camera." That was a theory written as a finding, and it is wrong. The weather
> pass is retail `0x00260830` (homolog of HREK `weather.cpp` `0x00815240`, sole
> assert `weather.cpp:407`), sole caller `player_view_render` at `0x0026C82F`.
> It reads the DEFAULT WORKSPACE `0x00C9FAE0` - position `+0x00`, forward
> `+0x0C`, up `+0x18` - and advects by `pos + fwd*0.15`. That workspace IS
> `kReachDefaultWorkspaceRva`, and site 2 is what builds it. So the rain does
> reach this function, through the one site deliberately never corrected.
>
> **But that consumer is already head-parented, and correcting site 2 would
> change nothing.** Order, all measured: the outer main render calls the world
> camera build at `0x000C36D6` (site 2 fires) BEFORE calling `main_render_view`
> `0x000C31F4`, which the mod hooks. `ReachMainRenderViewBody` then commits the
> head-centre camera into `workspace+0x00` AND
> `workspace+kReachSecondaryCompactOffset` before calling the original, and
> nothing rebuilds `0xC9FAE0` afterwards. Every `player_view_render` that
> follows therefore reads a head-derived camera. `0x00260830` is exonerated.
> **Do not "fix" site 2** - it would risk the accepted Reach 3D for no gain.
>
> The real suspect is a DIFFERENT rain consumer. The `render_rain` debug var
> resolves to `0x00B4444C` (cstring `0x009EB110`, pointer `0x00B40FF0`, entry
> type 5). It has exactly five readers: `0x002599E8`, `0x0025E318`,
> `0x0026C6DC` (the path already proven head-parented), `0x0026E7B4`,
> `0x0026E974`. The strongest suspect is `0x0026E974`, which reads
> `kReachCameraStackPointers[kReachCameraStackDepth] + 0x154` - the camera
> STACK (`0x00C878A8` indexed by `0x00B43ABC`), not the workspace - and
> republishes that position into `0x00B43E20/24/28`. The mod deliberately does
> not own the camera stack. Next step is offline and costs no headset time:
> disassemble the four unexamined readers and determine which camera each one
> reads.
>
> Measured dead ends, recorded so they are not retried: HREK
> `render_rain_sheets.cpp` (`0x00874D70`, `0x008759D0`) has zero callers and
> zero pointer refs, and retail carries no `rain_sheets` string; and every rain
> sub-toggle except `render_rain` resolves to a NULL backing global in retail,
> so the debug-var table gives no further leverage.

**Unjustified write to review in the shipped hook.** The detour is a denylist
(correct everything except the world site), and two of the sites it corrects
write PERSISTENT MODULE GLOBALS rather than a transient camera: site 3
(`+0x26FA47`) and site 4 (`+0x26FB13`) both target `haloreach+0x00C9FF90`, and
both are called with a NULL source (`xor edx,edx` at `0x26FA39`), i.e. the
engine is deliberately building a DEFAULT camera there. Site 3 is exercised and
is being corrected today. Stamping a per-eye VR head camera over a deliberately
defaulted camera in a global that outlives the eye scope is not justified by any
evidence we have. Site 5 (CHUD projection) writes a stack camera that dies
inside `0x2E1430` and is the only correction plausibly responsible for the
accepted marker/tag fix. Narrowing the denylist to an allowlist of site 5 is
strictly subtractive and should be done as its own candidate - separately, so
that if the markers regress it is unambiguous that site 3 was load-bearing.

**Instrumentation defect to fix, not a behavior defect:** the worker logs the
site table only when the SET of exercised sites changes, so the accepted session
printed one snapshot 57 ms after arming with `0 head-locked` on every row and
never printed again. The correction demonstrably works (headset result), but the
counters cannot show it. Make the report periodic before relying on it again.

### ACCEPTED: Reach native pause state - 2026-07-27

Headset confirmed by the user: "I tested it, and it worked." Reach remains
experimental; the product pointer is unchanged.

| Identity | Value |
| --- | --- |
| Accepted source | `bc74d6345fbf64a1ab6f1ffc6c9e07378a3fadfb` |
| `halo3xr.dll` SHA-256 | `26A91813CDCA5D4A718E224DCA293AB5D5360CE1B59E11507996D769FA19A398` |
| Branch | `reach/frame-skip` |

Reach now publishes `RuntimeMode::Paused`, switches to the head-locked 2D view
on the pause edge and restores stereo on unpause, keeping its camera core armed
throughout (Halo 3's shape, deliberately not ODST's teardown).

The flag was found by observing the running game, not by reading the binary,
and the parallel HREK pass afterwards explained why nothing else would have
worked: Reach stores pause as a 16-bit **pause-reason bitfield** at
`game_time_globals+0x02` (TLS slot `0xA0`), not as a boolean, so Halo 3's and
ODST's single-pause-byte shape could never have located it. It also recorded a
trap worth keeping: testing that bitfield for "nonzero" would have FAILED in
play, because bit 0 is a re-entrancy latch the engine sets during normal game
time updates (retail `0x5CD21` sets, `0x5CE64` clears, reached from the
game-time update path at `0x5C4C8`). Only bit 2 is the pause menu. The observed
host-channel byte avoids that entirely.

### Superseded UNTESTED record for the same candidate

Installed on top of the accepted `716a635` state; does not advance the pointer.

| Identity | Value |
| --- | --- |
| Candidate source | `bc74d6345fbf64a1ab6f1ffc6c9e07378a3fadfb` |
| Candidate package | `out/candidates/bc74d63-reach-fp-parity-20260727-083725972Z` |
| `halo3xr.dll` SHA-256 | `26A91813CDCA5D4A718E224DCA293AB5D5360CE1B59E11507996D769FA19A398` |
| `halo3xr_launcher.exe` SHA-256 | `B32C0001F297465C0924E18A85126D0C01F5084FEDF3F9389CA49160BFBA66BF` (unchanged) |
| Preserved previous install | `out/deploy-backups/b97ed6c-before-bc74d63-20260727-083726732Z` |
| Installed hash verified separately | yes, matches the manifest |
| Headset result | **PENDING** |

**The flag was found by observing the running game, not by reading the binary.**
`haloreach.dll+0x00C1A0E2`, 1 = paused, 0 = running. HREK names the system but
cannot supply an address: `game_paused` is a registered debug variable whose
value pointer is null even at runtime (function-backed, unlike
`render_far_clip_distance`), and the owner is `c_start_menu_pause_component`
- "Pauses the game while the component exists" - whose symbols are stripped
from retail. Full derivation, including the exact 45-byte owner signature and
why the bare store instruction (39 matches) is not usable on its own, is in
`docs/REACH-SIGNATURE-EVIDENCE.md`.

Three independent lines agree: a read-only writable-page differential across
three paused and two unpaused captures at different places in the level (2175
boolean survivors); a code-reference filter over 23 rip-relative byte-access
forms that reduced those 2175 to **four**, of which this one has one writer and
eight readers spread through the engine; and a 10 Hz live watch during a
five-second pause cadence that produced six clean alternating transitions
4.9-6.2 s apart and eliminated the other three.

**Behavior follows Halo 3, deliberately not ODST.** Halo 3 flips presentation
and keeps its camera core armed. ODST tears the core down for Save & Quit
safety, and that is exactly what produced its slow-rearm defect. Reach now
publishes `RuntimeMode::Paused`, requests head-locked 2D on the pause edge,
restores stereo on unpause, and keeps the core armed throughout. The armed-core
locomotion fallback in `Game_MoveStickIsLocomotion` honours pause as well -
without that it would keep answering "locomotion" during the menu and the
original stick defect would survive the fix.

Fail-open in every branch: missing, ambiguous, out-of-range or non-boolean
results log once and leave Reach exactly as it behaved before (always
`Gameplay` while armed). It never blocks arming and never disarms the camera
core. Disarm and title-exit both clear the override so a 2D presentation cannot
be stranded across a transition. Halo 3 and ODST are untouched.

`tools/check-reach-fp-parity.ps1` rejects "optional Reach CHUD hook target
publication" identically on clean `e748aef`, so that rejection is pre-existing
and unrelated to this candidate.

Acceptance: in Reach, pausing switches to the flat head-locked view and
unpausing returns to stereo; the left stick navigates the pause menu instead of
walking; and Halo 3 + ODST pause behaviour is unchanged. Log lines to expect:
`Reach pause state: native flag at haloreach.dll+0xC1A0E2` once per level, then
`Reach pause presentation: native pause entered/exited` per pause.

## Superseded baseline: authored crosshairs on all three titles - 2026-07-27

**This is the current working baseline.** Halo 3, ODST and Halo: Reach all
display their own authored CHUD crosshair art on the VR aim ray, the flat
native crosshair is gone from the HUD, and the per-frame cost of doing it has
been removed. ODST additionally arms promptly and recovers from its own menu.

| Identity | Value |
| --- | --- |
| Source | `675cc15c36c2a873f2a38a128b73f41548bb79a5` |
| `halo3xr.dll` SHA-256 | `DCFFA1C025CEF5BFB41D8FC6F26F8C96330D539800141D93989094FA78828D26` |
| `halo3xr_launcher.exe` SHA-256 | `B32C0001F297465C0924E18A85126D0C01F5084FEDF3F9389CA49160BFBA66BF` |
| Branch | `reach/frame-skip` |

Headset-confirmed by the user during this session: Reach HUD sliders, Reach
authored crosshair on the aim ray with the flat one gone, restored frame rate,
Halo 3 unaffected/working, ODST authored crosshair, and ODST arming. The final
candidate above (ODST menu recovery) was installed and declared the baseline;
its menu open/close cycle is the one item still worth an explicit re-check.

### Reach HUD scale and aspect

Reach authors one curvature record **per screen shape**, five per skin. Only
the widescreen record was ever written, but the VR per-eye target is
`3752x3828` - aspect 0.98, not widescreen - so the engine reads the
`fullscreen standard` record (920x690) that nothing touched. `HudLayoutAdapter`
now carries alternate resolution-class anchors and matches any of them.
`hud_size` and `hud_aspect` work. `hud_curvature` and `hud_vertical_offset`
still do NOT work for Reach and are documented as such in `halomccvr.cfg`.

### The authored crosshair, and why it took so long

Four separate defects were stacked on top of each other. Each is worth knowing
because each produced a convincing but wrong symptom:

1. **Wrong address, then right address.** HREK's compiled `chud_draw_widget`
   does not byte-match MCC's retail build - verified, zero matches, even for a
   signature that two independent official HREK builds agree on. The working
   address `haloreach.dll+0x2DA364` was found by tracing the real call graph
   forward from the already-proven `kReachPlayerViewRenderRva`.
2. **Truncated argument widths crashed the game** four times, always
   `0xC0000005` at `haloreach.dll+0x2ED80C`. The detour declared arguments 3
   and 4 as `unsigned short`/`unsigned char`; Reach uses both as full 32-bit
   values (`0x2DA39D`, `0x2DA41D`, `0x2DA39A`). The truncated widget index sent
   back into the engine selected the wrong branch and decoded an invalid Blam
   pool handle.
3. **The class filter could never have worked.** `descriptor+4` is a WIDGET
   INDEX, not a scripting class - `0x2ED80C` is a three-tier index accessor
   (strides `0x27`/`0x21`/`0x20`). The class lives on the owning COLLECTION,
   reached via `descriptor+3`. This matters because 1092 of 1143 drawn widgets
   in the official CHUD exports author their class as "undefined/use parent";
   only 51 carry an explicit class. Per-widget filtering hid whichever widget
   sat at index 2 - one arc of the crosshair - and nothing else.
4. **Reach was allowed to capture art it was never allowed to display**, and
   then painted over it. `shouldUploadAuthoredReticle` carried `!reachTitle`,
   and Reach was still listed as a title with no authored capture, so it
   painted the procedural reticle FULLY OPAQUE into the same swapchain the
   captured art lives in.

### Performance: it was never a 40% slowdown

Frame rate is a deadline problem, not a throughput one. Measured `renderWindow`
p95 from preserved logs:

| build | renderWindow p95 |
| --- | --- |
| no crosshair hook | 6.6 - 7.9 ms |
| crosshair hidden, art not published | 6.0 - 8.6 ms |
| art published, per-frame upload | 11.6 - 12.5 ms |

At 90Hz the budget is 11.1ms and everything fit; at 120Hz it is 8.33ms, every
frame missed, and the compositor halved to exactly 60. Publishing the art cost
~4-5ms because it performed a blocking OpenXR swapchain
acquire/wait/copy/release every frame for art that changes only on a weapon
swap, zoom or reticle-state change.

The fix, now shared by all three titles:
- The captured art's identity is folded into a key. The upload happens only
  when that key changes; a frame-gap floor bounds it if the key ever churns.
- A capture containing no crosshair widgets (key 0) is never published, so a
  blank image can never overwrite good art.
- Once the swapchain holds authored art it is **held**. Repainting the
  procedural reticle over it was both the flashing and a per-frame cost.
- Whether a title captures authored art is now a **live fact** -
  `Game_TitleCapturesAuthoredCrosshair()` reports whether the capture hooks are
  actually installed - instead of a hardcoded title list that went stale twice.
  If capture is not installed, the procedural reticle stays visible, so a
  failed signature scan degrades to a visible crosshair rather than none.

### ODST arming and menu recovery

- **Arming.** `OdstFreshCameraDebounce` restarted its one-second stability
  interval on any not-fresh poll, but ODST's readiness tail toggles ~10x/second
  during ordinary play (only the final tail boolean moves). The interval could
  essentially never complete, so ODST armed by luck - slow, and a fast
  pause/unpause frequently never re-armed. Gaps up to 350ms no longer restart
  the interval; longer gaps still do.
- **Menus.** An unsupported/menu camera mode called `BlockUntilTitleExit`, so
  opening ODST's menu once permanently killed VR for the session: presentation
  returned to stereo with no camera core behind it. It now uses the same
  `BlockUntilReload` gate level transitions use, which requires the camera to
  be seen not-ready then ready again - only true once the menu is gone. Scoped
  entirely inside `#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP`; no other title is
  affected.

### Known-incomplete in this baseline

- `hud_curvature` and `hud_vertical_offset` do not work for Reach.
- The D-pad-gesture left-stick-click -> Back binding (`0ef9820`) is present and
  documented in `halomccvr.cfg` but **did not work** in the headset. Either
  find ODST's real map button or remove the binding; do not treat it as
  working.
- Reach bullets/muzzle markers remain stock.

### UNTESTED: Reach vibration + cinematic-state probe - 2026-07-27

Installed on top of the baseline above; does not advance the pointer.

| Identity | Value |
| --- | --- |
| Candidate source | `c24a89b64e25ad11fb757b4e3dcdabd72b535637` |
| Candidate package | `out/candidates/c24a89b-reach-fp-parity-20260727-054021218Z` |
| `halo3xr.dll` SHA-256 | `B6FD227D905FE4BA16F717DE64B08062DB9BD052B65B7E2DE7218577832E2C7D` |
| `halo3xr_launcher.exe` SHA-256 | `B32C0001F297465C0924E18A85126D0C01F5084FEDF3F9389CA49160BFBA66BF` (unchanged) |
| Preserved previous install | `out/deploy-backups/dcffa1c-before-c24a89b-20260727-054021914Z` |
| Headset result | **PENDING** |

**Why Reach never vibrated (root-caused from the preserved 2026-07-26 log +
code).** Reach published no title-runtime heartbeat and its heartbeat-policy
freshness window was zero, and `ResolveTitleRuntime` unconditionally
disqualifies a candidate without a fresh heartbeat. Reach was therefore never
the resolved owner: `Game_HasTitleCapability(TitleCapability_Haptics)` always
denied, the XInput SetState hook discarded every captured rumble request, and
the 50 ms worker republished fallback `Loading` over the present path's
`Gameplay` - the "Runtime mode: gameplay -> loading" flap ~10x/second visible
through whole Reach sessions. Aim and the reticle only worked because each
carries a direct ownership bypass; haptics has no bypass.

**The behavioral change (one):** the armed Reach camera block now publishes a
`HaloReach` heartbeat per Present (homolog of Halo 3's `CamCopyHook` and
ODST's cam-copy heartbeat), and the policy grants Reach Halo 3's 500 ms
window. Armed Reach resolves as owner, Haptics flows through the same shared
gates as the other titles, the runtime mode holds Gameplay (flap and its log
spam end), and teardown expires ownership within the window.

**Restored:** the peak-hold rumble latch (`SampleHapticPeak`, commit
`4bcda82`, re-applying headset-confirmed `7e0fb71`). The current line never
contained it - it survives only on
`recovery/pre-github-restore-feature-20260722`; the GitHub-restore regraft
dropped it. Without it, one-frame gunfire pulses alias to zero and rumble is
intermittent - the exact ODST bug already fixed once. Title-agnostic: Halo 3
and ODST need a rumble regression check with this candidate.

**REACHCINE probe (log-only, fail-open).** For the remaining cutscene work.
Pinned `haloreach.dll` registers game-state members "cinematic globals"
(0x40 bytes) and "cinematic globals non deterministic" (0x10) at exactly one
site, which caches per-engine-thread member pointers in the module TLS block
(the design HREK's `__tls_set_g_cinematic_globals_allocator` symbol names).
The unique registration signature decodes at runtime: the module TLS-index
dword (retail RVA `0xC17B18`), member slots `[tls+0xE0]` / `[tls+0x448]`, and
the verifying name string - nothing hardcoded. The engine-thread sampler
snapshots both members per owned frame; the worker logs a baseline, per-dword
changes (timer-like dwords mute themselves), and a stall report if cutscenes
bypass the armed camera path. Field meanings are deliberately unassigned: the
first headset cutscene run (Winter Contingency opening) labels
in_progress/scene/shot from the `REACHCINE` lines, after which the actual
per-cut yaw rebase (the shared `g_gameYawRef`/`g_headYawRef` mechanism Halo 3
and ODST already use) ships as its own candidate.

Acceptance for this candidate: controller rumble in Reach gameplay (gunfire,
damage) that does not cut in and out, Halo 3 + ODST rumble regression, and a
log capture spanning the Reach opening cutscene for the probe.

**Headset result (2026-07-27, Winter Contingency):** Reach vibration WORKS
(user-confirmed; Halo 3/ODST rumble regression still untested). The
runtime-mode flap is gone (6 transitions all session vs thousands). The
REACHCINE probe labeled the fields; see the follow-up candidate below. One
regression appeared: the VR crosshair was absent until 00:47:39 - see below.

### UNTESTED: Reach crosshair-from-frame-one + cutscene cut realign - 2026-07-27

| Identity | Value |
| --- | --- |
| Candidate source | `68286d61bc39bd6e312aaf6c6ffc1168e0cb0fab` |
| Candidate package | `out/candidates/68286d6-reach-fp-parity-20260727-060859139Z` |
| `halo3xr.dll` SHA-256 | `D0DAC5AC6A44045C81459CD7D3DFE558376195CE04965E6CE6B23ED72720DDFE` |
| Preserved previous install | `out/deploy-backups/b6fd227-before-68286d6-20260727-060859894Z` |
| Headset result | **PENDING** |

**Crosshair root cause (log-proven).** The c24a89b session ran key-0 authored
captures with no visible crosshair from 00:43:58 until 00:47:39, then
captured normally. `VR_PrepareAuthoredReticleResources` - the cold
preparation Reach's hot capture entry REQUIRES (it refuses lazy allocation) -
had NO remaining callers; the call was lost in the 6c772fc/4162290
cleanup-revert cycle. It kept "working" anyway because the unsettled shared
snapshot misdetected the title constantly, dropping the capture entry into
the Halo 3/ODST lazy branch which created the texture by accident. The
heartbeat settled ownership and removed that crutch; only the 00:47:33-39
checkpoint boundary (title momentarily unsettled) created the texture. Fix:
`ReachCameraCore_Poll` cold-prepares on the worker, idempotent, logged once
per generation; Failed logs loudly and never blocks the camera core. Halo 3
and ODST were never on the strict path and keep their lazy branch untouched;
their crosshair behavior is not affected by either the bug or the fix.

**REACHCINE probe findings (now consumed by the realign).** In Reach's
0x40-byte "cinematic globals": `+0x28` = current-shot start stamp, changes at
every authored cut (incl. the 00:44:57.7 no-fade cut), frozen during
gameplay; byte `+0x26` = screen-visible, rises when a fade-black ends,
anti-correlated with fade floats at `+0x30..0x3C`; the member resets at
checkpoint boundaries. The 0x10-byte non-deterministic member is the live
dialog/subtitle line state: `+0x00` line id, `+0x04` duration seconds,
`+0x08` remaining-time countdown, `+0x0C` a second id - zeros between lines.
Cut realign: `ReachBuildHeadCullCamera` detects the +0x28/+0x26 edges from
the same-thread sample; `ReachApplyHeadLook` realigns yaw (only) to the new
authored facing, Halo 3/ODST semantics; one worker log line per realign.

**Headset result (2026-07-27):** both fixes CONFIRMED by log and player. The
crosshair published 0.7s after arming (key `789BF424` at 01:13:06.473) instead
of 3.5 minutes in, and both authored cuts realigned
(`Reach cutscene facing: realigned to the authored camera (cut 1/cut 2)`).
Two further defects were then reported; see the next candidate.

### UNTESTED: Reach crosshair=0 teardown fix + REACHHUD diagnostic - 2026-07-27

| Identity | Value |
| --- | --- |
| Candidate source | `fafebc62b41a128f0282bb266da11b422081c35e` |
| Candidate package | `out/candidates/fafebc6-reach-fp-parity-20260727-062510426Z` |
| `halo3xr.dll` SHA-256 | `A5270CA29940F59492F38D8B97D1C0D6812DFC0B2A7B563C68178C603519EAAD` |
| Preserved previous install | `out/deploy-backups/d0dac5a-before-fafebc6-20260727-062511131Z` |
| Headset result | **FAILED - Reach lost 3D entirely. Superseded by `298d270` below.** |

> `fafebc6` also switched the crosshair redirect to the PREPARED capture
> entry "for symmetry" with its End call. That entry refuses unless every
> prepared resource already exists, and the refusal path disarmed the core:
> armed `01:29:20.548` -> stereo OFF `01:29:20.602` -> core removed
> `01:29:21.174`. Reverted in `298d270`, together with the teardown
> consequence that made it fatal.

**`crosshair=0` tore down VR (fixed).** With the crosshair disabled,
`ReachDecideChudCrosshairAction` returns `Suppress`, which still requires the
render-target redirect - that redirect is Reach's ONLY way to keep a widget
off the eye, since it has no visibility predicate to NOP the way Halo 3 and
ODST do. `BeginAuthoredReticleCaptureInternal` refused whenever
`g_config.crosshair` was 0, and the Reach hook treats a refused redirect as
capture-target loss: reject, disarm, six-hook teardown. The prepared (Reach)
entry no longer applies that check; display remains gated by
`g_config.crosshair` in the compositor, so `crosshair=0` now means no
crosshair anywhere with VR untouched. The lazy entry keeps the check, so
Halo 3/ODST are unchanged. Fixed a latent hot-path violation beside it: the
Reach hook began with the lazy (allocating) entry while ending with the
prepared one; both sides now use the prepared entry.

**Crosshair vanished mid-level (diagnosed next run, deliberately not
guessed).** The user tied it to on-screen OBJECTIVE text (initially reported
as subtitles, then corrected). Three mechanisms could produce it and the
current log cannot separate them: the engine stops emitting class-2 widgets,
the collection descriptor stops being readable, or the art key churns and
republishes different art. `REACHHUD` counters now report exactly which -
hot hook bumps atomics only, the 50 ms worker logs a 2s class-2 drought,
unreadable-descriptor counts with the alternate-path flag, rejected eye
transactions, and every published art-key change. Silent while healthy.

**Still open from the same session (need their own evidence pass):**
### UNTESTED: Reach redirect-loss no longer disarms the core - 2026-07-27

| Identity | Value |
| --- | --- |
| Candidate source | `298d270a98b286bbc41bdab1ea68e0ef0d91d3b6` |
| Candidate package | `out/candidates/298d270-reach-fp-parity-20260727-063300640Z` |
| `halo3xr.dll` SHA-256 | `333B154C14A34A55FBE8E20B117A4446F52B026449B02B60667FAB11168748B8` |
| Preserved previous install | `out/deploy-backups/a5270ca-before-298d270-20260727-063301337Z` |
| Headset result | **PASS for 3D.** Reach armed `01:40:14` and held stereo for the whole session, no teardown. REACHHUD reported `crosshair redirect unavailable 11/24 times ... the camera core stayed armed` at `01:43:54`, the moment the user disabled the crosshair - the failure isolation working exactly as designed. Two defects remained; see below. |

### UNTESTED: Reach `crosshair=0` hides the flat crosshair too - 2026-07-27

| Identity | Value |
| --- | --- |
| Candidate source | `a683ae359ece1fcc4d642c17209a3b858b7337e9` |
| Candidate package | `out/candidates/a683ae3-reach-fp-parity-20260727-064939631Z` |
| `halo3xr.dll` SHA-256 | `57A3C775C24EA4D30E12D41D1B2D7F8455103EA1E7E45CA3F46C0E0F7E8D1300` |
| Preserved previous install | `out/deploy-backups/333b154-before-a683ae3-20260727-064940345Z` |
| Headset result | **PASS.** User: "the hide crosshair bug is fixed". `crosshair=0` now shows no crosshair of any kind, and VR stays up. |

### UNTESTED: Reach objective-driven crosshair loss - 2026-07-27

| Identity | Value |
| --- | --- |
| Candidate source | `578b82de2529d27778d653d02bcb6c86fecc389f` |
| Candidate package | `out/candidates/578b82d-reach-fp-parity-20260727-065952281Z` |
| `halo3xr.dll` SHA-256 | `434A63A82446A7C11F3C5ABFE3EAD96BDCCEB58C5DAFC68D0399F10B24D2E3CC` |
| Preserved previous install | `out/deploy-backups/57a3c77-before-578b82d-20260727-065952978Z` |
| Headset result | **PENDING** |

> **HEADSET RESULT: FAILED. The theory below is DISPROVEN - do not build on
> it.** With the alpha write disabled, Winter Contingency (cutscene ->
> gameplay -> objective) still lost the VR crosshair at the objective, and
> the same drought line still appears:
> `[02:04:00.892] no class-2 crosshair widget drawn for 2s ... (unreadable
> descriptors 0, rejects 0)`. The alpha/fade write was therefore NOT the
> cause. It stays disabled (it is genuinely redundant and no flat crosshair
> was reported), but it fixes nothing.
>
> **The diagnostic has a blind spot that must be closed before the next
> attempt.** The counters classify each draw as class-2 or unreadable. A
> widget that is READABLE but resolves to a class other than 2 is counted by
> neither - so "0 unreadable, 0 rejects, no class-2" is equally consistent
> with two very different things: Reach genuinely stopped drawing the
> crosshair, OR it is still drawing it and our collection-class resolution
> stopped returning 2 for it. That second case is plausible precisely because
> the class is reached through the owning COLLECTION via `descriptor+3`, and
> adding objective widgets can shift collection indices. Add a
> readable-but-not-class-2 counter (and the observed class values) and one
> run decides it. Do not guess between them again.
>
> Session shape for reference: armed `02:01:14`, droughts at `02:01:58`
> (cutscene), recovery `02:02:55`, drought `02:02:57`, recovery `02:03:04`,
> drought `02:03:47`, recovery `02:03:49`, final drought `02:04:00` at the
> objective. The log ends `02:04:02`, so permanence is the player's report,
> not a log fact.

**Root cause: the mod was deleting its own art source.** REACHHUD named it in
the reproduction session:
`[01:56:51.484] no class-2 crosshair widget drawn for 2s - the engine stopped
emitting it (unreadable descriptors 0, rejects 0)`, with no recovery line
afterwards. Zero unreadable descriptors and zero rejects rules out the
capture path entirely - Reach stops emitting the widget, so there is nothing
to capture, the authored quad stops being submitted, and the crosshair is
gone for the rest of the level.

`SuppressReachNativeCrosshair` writes 0 to the crosshair's own alpha, fade
target and fade duration in `chud_globals`, every admitted frame. Reach's
CHUD is left holding a fully faded-out crosshair, and once an objective event
makes it re-evaluate that fade state it stops drawing the widget at all.

It is also redundant: the render-target redirect is what keeps the flat
crosshair off the eye, proven by the `crosshair=0` pass above where the
redirect runs with no alpha write and no flat crosshair appears. Disabled
behind a named constant rather than deleted. If the flat crosshair ever leaks
through the redirect, fix the redirect - this write cannot be made safe,
because any CHUD state change that consults the fade record can latch the
widget off permanently. Halo 3 and ODST use their own visibility predicate
and never call this Reach-only path.

Turning the crosshair off revealed Reach's ORIGINAL flat crosshair - the
opposite of the setting's meaning, and players who want no crosshair at all
must be able to have that. Reach has no visibility predicate to NOP and its
CHUD alpha write is inert, so the render-target redirect is the only thing
that keeps the native crosshair off the eye; the plain capture entry refused
whenever `crosshair=0`. New `VR_BeginAuthoredReticleRedirect` keeps the lazy
resource creation (so a transient resource state can never fail it, unlike
the prepared entry that caused the 3D regression) and drops the
crosshair-enabled requirement. Reach's three hide paths use it. Display is
still gated on `g_config.crosshair` in the compositor, so `crosshair=0` now
means no crosshair anywhere.

Deliberately surgical: with `crosshair=1` the new entry is behaviourally
identical to `298d270`, so normal play cannot regress. Halo 3's capture path
(`game.cpp:711`) and the prepared entry's contract are untouched.

**Objective-driven crosshair loss - mechanism identified, fix is its own
candidate.** The same log shows the published art key moving to a transient
value and returning ~50 ms later (`9C04306C` -> `F82FB8B1` -> `9C04306C` at
`01:43:16`, and the same shape repeatedly), with no class-2 drought, no
unreadable descriptors and no rejects. So it is art-key churn republishing
different art, not the engine dropping the widget: when an objective is
given, the drawn class-2 widget set changes, the key changes with it, and
the re-upload publishes art that does not contain the crosshair.

Reverts the `fafebc6` prepared-entry switch that killed Reach 3D, and then
removes the reason it was fatal: a momentarily unavailable render-target
redirect now marks the eye (so a partially captured pair never publishes -
the compositor skips that frame and retries) and keeps the camera core
armed, instead of disarming and requesting six-hook teardown. Both the Begin
and End failure paths use `ReportReachRedirectUnavailable`; the worker logs
the count. This is the same correction applied to the two silent teardown
paths on 2026-07-26, and with it neither this regression nor the
`crosshair=0` teardown could have killed VR. `crosshair=0` safety and the
REACHHUD counters from `fafebc6` are retained.

**Lesson worth keeping:** the begin/end asymmetry was real, and "fixing" it
was still wrong, because the failure consequence - not the entry point - was
the actual defect. Remove a fatal consequence before tightening the thing
that can trigger it.

### Reach HUD element identification - 2026-07-27 (official HREK evidence)

Answering "which ones are the subtitles and which are the character tags".
These are **three different systems**, which is why they fail differently and
why the user correctly observes that the rest of the HUD is fine:

| Element | System | HREK evidence |
| --- | --- | --- |
| Character tags / navpoints | CHUD navpoint system, world-anchored and projected each frame | `chud_navpoints.cpp` (`0x18A51C0`); 20 entries, stride `0x88`, `position_worldspace` at `+0x3C`; retail `ai_add_navpoint` -> `+0x1A1A7C` -> worker `+0x6C2E68` via TLS slot `+0x30` |
| Objectives | CHUD objective element | `cinematic_set_chud_objective` (`0x17D09D8`) |
| Subtitles | **NOT a CHUD widget** - the interface text/subtitle layer, with its own font, colour and rect settings | `subtitle font` (`0x169ED70`), `subtitle color` (`0x169F168`), `subtitle rect width` (`0x169F1D0`), `display_subtitles`, `subtitle: failed to find string %s` |

Subtitles being a separate layer is the significant finding: they are
composited outside the CHUD path the mod already owns, so their double
vision has a different cause than any CHUD element and needs its own
compositing fix rather than a navpoint-style transform fix.

### Reach muzzle flash: why there are two, and where the second comes from

The user reports one flash correctly tracking the controller-held gun and a
**second element stuck at the face**, and wants both on the hand.

Official HREK shows Reach's effect system tags each effect with
`first_person_weapon_output_user_index` and `first_person_weapon_user_mask`
(asserts at `0x16CB780`, `0x16CC170`, `0x16CD5D8`, `0x16CD6B0`), plus a
dedicated `first_person_weapons.cpp` (`0x1866E60`) whose render code even
carries a `didn't find trigger marker for weapon` diagnostic (`0x1859E60`).
So an effect is either attached to the first-person weapon - rendering in FP
space, which is why that element follows the controller-driven weapon the
palette work already moves - or it is an ordinary world-space effect spawned
at the weapon object's marker. Reach's sim weapon is still wholly stock and
head-anchored in this build, so a world-space muzzle effect is emitted at the
player's head: exactly the reported face-stuck flash.

**CORRECTED 2026-07-27 - the label and the "both consumers" claim below are
both wrong.** `haloreach.dll+0x120FDC` is a bool-taking wrapper with exactly
ONE caller (`0x11BFDC`); its non-first-person path just calls `0x120EC4`. The
real effect-location resolver is `0x00120EC4-0x00120FD8`, the homolog of HREK
`0x36AB70` (HREK `effects.cpp` asserts 7630/7638/7649/7650). See the correction
in `docs/REACH-SIGNATURE-EVIDENCE.md`. Retained below only as the historical
text.

The marker query both consumers reach is already proven:
`first_person_weapon_get_marker`, `haloreach.dll+0x120FDC` through
`+0x1210D3`, ABI
`void __fastcall(firstPersonWeapon, uint16_t markerIndex, BoneMatrix* outMatrix, bool firstPerson)`,
exact entry signature recorded in `docs/REACH-SIGNATURE-EVIDENCE.md`.
**There is currently no hook on it** - the marker-query detour was surgically
removed with the failed projectile-origin lineage. Re-adding one is the
implementation path for putting the second element on the hand, and it must
be scoped to the effect/marker consumer only: the removed lineage failed
because it moved the projectile origin, which is a different consumer and is
not what is being asked for here.

The user's exact requirements for the remaining HUD work, stated 2026-07-27:

- **Character tags must follow the HEAD ONLY.** Today they follow head AND
  hand ("two parents"): correct while aiming at the character, warping as the
  hand moves away. This is the shape of a projection consuming the
  first-person/weapon camera while the world renders from the head camera.
  **SOLVED 2026-07-27 - and the cause is our own code, not the engine.**
  The needed fact was determined rather than assumed, and the answer refutes
  the FP-workspace theory that used to sit here.

  The CHUD world-to-screen projection is retail `haloreach.dll+0x002E1430`
  (bounds `0x2E1430-0x2E1A69`), homolog of HREK `0x0092D980`. It reads
  `observer[user].camera` - TLS block `+0x688`, `observerGlobals + 0x154 +
  user*0x410` - and this is a genuine READ, not an ordering inference: the
  camera it builds is consumed by the screen math itself at `0x2E1567`,
  `0x2E1574`, `0x2E1595` (world point minus camera position) before
  `0x288C10`. A full operand scan of the function finds 21 rip-relative
  operands over 14 distinct globals and **zero** references to
  `kReachActiveView`, either camera-stack global, the FP camera workspace, the
  default workspace, the player-view array, the FP view, or the render-camera
  owner. The old "it inherits whatever first-person work left current" theory
  is DEAD - do not rebuild on it.

  **Stock Reach has exactly ONE parent.** `0x00287DFC`
  (`render_camera_from_observer_camera`) feeds BOTH the world render camera
  (called `0x0026C2D9`) and the CHUD projection camera (called `0x002E1520`)
  from the same `observer[user].camera`.

  **We are what splits it.** `Game_ComputeAimStick` drives Reach's sim/observer
  camera onto the right-controller ray, while `ReachBuildHeadCullCamera`
  applies head-look to a PRIVATE copy installed render-side only ("Read-only:
  `stockCompact` is never modified"). World renders from the head; markers
  project from the hand. Halo 3 and ODST do not have this bug because both
  apply head-look INSIDE their camera-copy hook and never restore the source -
  see the load-bearing comment at `game.cpp:5067-5073`, "Do not scope this
  write again." Reach has no camera-copy hook at all; its heartbeat is
  synthesised in the present path.

  **Why no fix has shipped yet: scope.** HREK's `chud_anchor_type_enum`
  (`0x01870CC0`) puts `<campaign fireteam member>` and the objective anchors in
  the same world-object anchor group as `backpack weapon`, `grenade`, `weapon
  target`, `ghost reticule`, `hologram target`, `airstrike target` and
  `lasing target object`. One fix moves every object-anchored widget, not just
  tags and objectives, which conflicts with the user's exclusive requirement.
  (`motion sensor` is a screen anchor and is genuinely unaffected.) A
  `REACHPROJ` log-only probe is the next step; the decisive question it answers
  is whether any class-2 crosshair widget reaches `0x2E1430`, because the
  headset-accepted crosshair depends on it.
- **BOTH muzzle flash elements must follow the HAND.** There are two. One
  already tracks the controller-held gun correctly and must not be disturbed;
  the second is stuck at the player's face. The requirement is to move the
  second onto the same hand-tracked transform as the first - NOT to suppress
  it, and NOT to move the tags. See the mechanism section above.
- **Objective text and subtitles must sit on a readable plane.** Subtitles
  currently give double vision (drawn per-eye with divergent projection or
  effectively at infinity rather than at a converged HUD depth). The user
  states the other HUD elements do not have this problem, which is itself a
  strong clue: compare how those two text elements are composited against a
  known-good HUD element. Lead: the REACHCINE non-deterministic member is
  the live dialog line (id/duration/remaining).

### ACCEPTED: Reach native HUD layout (size + aspect) - 2026-07-27

Headset confirmed by the user: Reach's HUD sliders work. Reach remains
experimental; the product pointer is unchanged.

| Identity | Value |
| --- | --- |
| Accepted source | `c99cce49cc03ad76bbe3477821d0190f3ae5d653` |
| `halo3xr.dll` SHA-256 | `9A833A25D72A35DA1879ABFF34CA25A05C29CA7E28DDD35A6B86F394E1BAD472` |
| Headset result | HUD size/aspect apply and are adjustable; took 30-45s to take effect |

**Why every earlier candidate failed.** Reach authors one curvature record per
screen shape, five per skin. Every prior candidate matched only
`fullscreen wide{720p fullscreen}` (1280x720 virtual canvas). The VR per-eye
render target is `3752x3828` - aspect 0.98, not widescreen - so the engine
reads the `fullscreen standard{480i fullscreen}` record (920x690), which was
never written. The field, offsets, and write were all correct throughout; the
record was wrong. `HudLayoutAdapter` now carries alternate resolution-class
anchors and the scanner/verifier match any of them. Halo 3 and ODST carry zero
alternates and are unchanged.

Covering both aspect classes is deliberately resolution-independent: these are
the game's authored virtual-canvas constants, so the engine's own choice
between records stops mattering. Do not replace this with render-aspect
detection.

**`hud_curvature` does not work for Reach.** `hud_size` and `hud_aspect` apply;
the curvature slider has no effect. Open defect, not a closed question.

**Remaining before a Reach release:**
- VR crosshair replacement from the CHUD widget.
- Controller vibration / haptics.
- Cutscene camera orientation, matching what Halo 3 and ODST already do.

Candidate `c4b6f610e7b0cab64dc0f53b2316db68c63b1e5f` (DLL
`C1E3952A2B6BB61BF37D8FED2D60F41B1D5657DEA7F462A9AC9684778A0F0476`) follows up
on the 30-45s delay: the first scan runs before the level's tag data is
resident and the retry cooldown was a flat 15s, so an early miss cost 15s plus
a ~4s scan. It is now 2s while the title is settling and 15s after. That
candidate is installed and **headset-pending**.

### UNTESTED Reach native HUD layout (size + aspect) - 2026-07-26

Reach laid its HUD out at its authored ~0.87 safe frame while Halo 3 and ODST
were pulled in to the user's `hud_size`, so Reach's HUD looked enormous beside
them. This candidate gives Reach the same shared HUD layout writer, against
Reach's own record.

| Identity | Value |
| --- | --- |
| Candidate source | `3df1c196b97e691ada98f180a432701848ae568e` |
| Candidate package | `out/candidates/3df1c19-reach-fp-parity-20260726-223801744Z` |
| `halo3xr.dll` SHA-256 | `EF7B5452685F4301559DD6C2610B03E8A5F84B9D8DA4A91A3274B3BB0DF015D4` |
| `halo3xr_launcher.exe` SHA-256 | `CC959758F723EDEE6D433D8D341340C958FB7FF44CCDD9B0F45437B791031F9C` |
| Preserved previous install | `out/deploy-backups/79eb9c4-before-3df1c19-20260726-223802433Z` |
| Headset result | **PENDING** |

The installed DLL was hashed separately after install and matched the manifest.

Evidence is official HREK only. HREK's `chud_curvature_info_block` load-time
postprocess (`reach_tag_test.exe 0x8E7170`, the code behind
`"Curvature points are invalid.  Defaulting to no curvature"`) writes the
identity curvature grid to `+0x04..+0x4B`, pinning the record start; the
official `chud_globals_definition` export gives the field order after it. Reach
inserts nine curvature points, a derived screen transform basis, a
`vehicle 3d sensor radius` and four minimap points that Halo 3 does not have, so
its `global safe frame horiz./vert.` pair sits 60 bytes after virtual width
instead of 24. No Halo 3 or ODST anchor, offset or record count was copied.

`HudLayoutAdapter` now carries the record shape - anchor length, wildcard mask,
safe-frame offset, and whether the record has a depth field - instead of
assuming Halo 3's. Halo 3 and ODST keep their exact 24-byte anchor, safe-frame
offset 24 and depth at -28, so their behavior is unchanged; that shared path is
what the required Halo 3 regression covers.

`hud_curvature` is deliberately **not** written for Reach. Reach has no
`dest offset z`; its curvature is folded into the derived basis when the tag
block is postprocessed at load, so writing the authored points at runtime would
be inert exactly like the CHUD alpha array. The adapter declares the depth field
absent, and the log and config file both say so rather than shipping a dead
knob. Making it live needs the derived basis written, or Reach's own builder
re-run over the record; that is a separate candidate. `hud_vertical_offset`
also stays Halo 3/ODST-only until Reach's `chud_compute_anchor_basis` homolog is
located.

Acceptance requires: Reach's HUD visibly shrinking to match Halo 3 at the same
`hud_size`, `hud_aspect` behaving the same way, a `SAFEFRAME [Halo: Reach]` line
reporting three accepted blocks, and a Halo 3 regression for the shared writer.

### Failed Reach final-palette-only candidate - 2026-07-24

Candidate `abea61f0daf2b70ba779a40a3a2ad72b3debf121` implemented the
final-palette reconstruction architecture described below. Retail Reach builder
`0x2AF648` makes separate interpolation then
palette submissions at `0x2AF85A -> 0x2B52EC` and
`0x2AF8F6 -> 0x2B52EC`. Official HREK tags independently identify the two
visible consumers: `objects\characters\spartans\fp\fp.render_model` is the
47-node first-person arms model, while
`objects\characters\spartans\fp_body\fp_body.render_model` is the separate
82-node first-person body model. Treating only the 47-node submission as visible
was not parity and is rejected.

The exact 47/41-node Spartan/Elite map still performs stock-only layout
discovery. On a later pair, that verified animation layout is selected by title
generation plus interpolation view/id/slot and full live count, not by a
temporary source-buffer address. Each retail interpolation transaction receives
its own bounded context. Each final palette consumes the newest exact
source-pointer match and reconstructs the complete live source (through the
retail 120-node bound) from its untouched snapshot into private scratch before
calling the stock palette builder. The 47-node arms palette and the 82-node body
palette therefore receive the same right- and left-wrist solution. The exact
appended held-object range inherits the solved right-wrist delta inside that
same reconstruction.

As in Halo 3/ODST, the mutated live interpolation graph exists only for
marker/muzzle/attachment consumers and receives one rigid right-controller
transform. No visible palette consumes it. The failed Reach-only separated-hand
live-graph owner, body-only admission gate, and source-pointer-keyed layout cache
have been removed; they are not dormant fallbacks. Unknown or invalid layouts
remain wholly stock rather than partially modified.

The installed DLL SHA-256 was
`61C70876A8BC883D5277A7070EF38E2CB350476B6BFAFD1943B96C6EF67ADF91`.
The runtime first line matched source `abea61f0...`; Reach armed and reported
`body=47 live=52 arm_ik=1 floating_hands=0`. The headset result was still no
visible change: the forearm moved while the left hand remained attached to the
gun/right hand. Evidence is preserved under
`out/test-runs/abea61f-reach-fp-parity-no-visible-change-20260725-052246Z`.
This candidate is failed and does **not** advance the accepted pointer.

### Failed Reach native weapon-IK parity gate / no-3D result - 2026-07-25

The missing accepted Halo 3/ODST behavior is now identified exactly: after the
palette transaction, both accepted titles bypass native flat-screen weapon IK
so its support-hand solve cannot reattach the controller-owned hand to the gun.
The final-palette candidate omitted that stage.

Candidate `cd0a7c136caa2972d9d57f5e44929adb88b96069` added that
title-native bypass, but its runtime proof incorrectly required retail's debug
descriptor to publish the HREK development value pointer. Retail leaves that
descriptor field unpublished. The exact installed DLL SHA-256 was
`ECA7202AE4132AC18A6E8C403C0FE4322616E380FC098D05E4B16135FB25D174`.
Cold preflight and display-worker proof passed, then the native weapon-IK proof
failed open before Reach installed any camera hooks. The headset therefore
showed stock flat-screen output with no 3D. Evidence is preserved under
`out/test-runs/cd0a7c1-reach-no-3d-weapon-ik-proof-fail-20260725-053853Z`.
This candidate is failed and does **not** advance the accepted pointer.

Pinned HREK exposes the type-5 boolean
`debug_animation_fp_weapon_ik_disable`. Its development table entry at
`0x201AD98` publishes value pointer `0x4F40A60`; the post-palette homolog
compares that byte at `0x8D3162` and jumps to its existing no-weapon-IK
epilogue at `0x8D338B`. Pinned retail Reach repeats the same execution edge but
does not publish the value pointer in its descriptor: the unique decision AOB
at `0x2B506E` directly compares exact byte `0x4E38B61` at `0x2B507F`, and
`0x2B5085` jumps to epilogue `0x2B52D1` when it is nonzero.

Forward source requires the exact retail descriptor name/type identity, unique
retail AOB, decoded RIP-relative value target, decoded stock branch target, and
bounded boolean value. It binds the control from that pinned shipping consumer
instruction, sets it for the complete Reach VR transaction, and restores its
original value during verified teardown. There is no runtime probe, skeleton
guess, alternate owner, or fallback implementation. Headset acceptance is
pending.

The forward-corrected candidate `d721068ac6ace2f2d2b6c8107c2f3d18494e43bd`
is installed with DLL SHA-256
`46ABD7BFF4AAF083A8CEF6AB174831458A06FAE7EBA85815D327ACECE2ABF226`.
The installed hash was verified separately, `arm_ik=1` and `floating_hands=0`
remain unchanged, MCC was not launched, and headset acceptance is pending.

### Failed Reach separated-hand graph result - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `6e31751cce1dd78a315f5418be8d7d2736e1f2a9` |
| Candidate package | `out/candidates/6e31751-reach-two-arm-ik-20260725-042432527Z` |
| `halo3xr.dll` SHA-256 | `49DC585C1E57FB54D197198E2A46AE95AA1CBF0FEE565EDCD62BBE740B9E8715` |
| Headset result | Nothing changed: the left forearm moved, but the visible left hand remained stuck to the gun/right-hand assembly |

The installed hash and first log line matched this exact clean source, and the
log reported the Reach two-arm path active with a 47-node palette over a
52-node live graph. This rejects the separated source-owner change. Combined
with retail `0x2AF648` and the official HREK 47-node arms / 82-node body split,
the result identifies the architectural fault: `6e31751` admitted and solved
only the 47-node palette transaction while allowing the second visible body
palette to consume stock/live data. Per the user's explicit instruction, this
failed DLL is not rolled back in the MCC installation; it is simply not an
accepted pointer or basis for the forward implementation.

### Failed Reach whole-graph hand-parenting result - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `e08b538f715a01d868a9460f05afae6a0cc0410e-dirty` |
| `halo3xr.dll` SHA-256 | `236D06940F47E38876A54DF4AFE07E4C484AED2E025865AF55121E022E1772AC` |
| Preserved failure evidence | `out/test-runs/e08b538-dirty-reach-hands-parented-20260725-040508Z` |
| Headset result | The left forearm moved, but the left hand remained stuck to the gun/right-hand assembly instead of tracking independently |

The runtime proved both OpenXR aim poses valid and tracked, put the left target
on the correct side, and reported both exact 47-node arms-palette wrists at
their targets with zero error. The live graph also ran. Controller tracking,
snapshot publication, and the analytic wrist solve are therefore ruled out.
The later `6e31751` separated-owner result also produced no visible change, so
both live-graph ownership models are rejected. The forward path no longer uses
either model for visible geometry; it reconstructs every final palette from its
own untouched interpolation transaction, matching Halo 3/ODST.

### Failed Reach two-arm scope-timing result - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `7ea6aca845a698f7994ef355e76a7361fe6f154e` |
| Candidate package | `out/candidates/7ea6aca-reach-two-arm-ik-20260724-214242079Z` |
| `halo3xr.dll` SHA-256 | `61C1DAECF3EE4D8891C88F4C973655ACDA949F61B938A865CDB628D7FB225F81` |
| Preserved failure evidence | `out/test-runs/7ea6aca-reach-ik-no-layout-20260725-001238Z` |
| Headset result | Reach camera/stereo and controller aim armed, but no arm IK appeared |

The exact installed DLL and configuration (`arm_ik=1`, `floating_hands=0`)
were correct. The runtime log contained no layout-learned, two-arm-active, or
layout-rejected status. Static retail call edges explain that exact absence:
`main_render_view` performs FP preparation through
`0x256724 -> 0x264530 -> 0x2AF648`, including interpolation at `0x2AF85A`,
before it calls `player_view_render` at `0x0C33C4`. Source `7ea6aca` armed the FP
scope only in the later inner stereo transaction, so every interpolation
callback failed the scope guard and every palette remained stock. The forward
candidate moves scope ownership to the admitted outer boundary, preserves it
through both eyes, and keeps nested renders stock while restoring the bounded
parent live graph and context. No additional probe is required.

### Unaccepted Reach camera headset result - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `0f7b6321ddc1830ad8a95c2bca8e472e3d837fff` |
| Candidate package | `out/candidates/0f7b632-reach-camera-20260724-093221973Z` |
| `halo3xr.dll` SHA-256 | `C5A33D3994695334CBB2F8DD0F108A42B1A86DF6BB3B2A2F646EF6A89EE01C40` |
| Preserved failure evidence | `out/test-runs/0f7b632-reach-3d-warp-input-fail-20260724-094945Z` |
| Headset result | Distinct 3D and translation reached the headset; projection warped on head turns, black outer borders remained, and stock look competed with HMD look |

The installed artifact matched its manifest and the runtime log proved both
Reach eye copies were current. The failure was traced to a concrete view
contract mismatch: Reach rastered approximately 61.5/53-degree horizontal/
vertical half-FOV at `2912x2100`, but OpenXR received Halo 3's approximately
47.5/48.1-degree defaults. The next forward candidate binds the actual Reach
projection and eye copies to the same prepared frame and gives the armed tracked
camera exclusive visual look-stick ownership. It remains headset-pending and
does not claim Reach weapon/body aim, HUD, or arm IK.

### Unaccepted Reach stereo-pass / culling-fail result - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `f953bbe373df22dbbd4b41c344c1226b738260ba` |
| Candidate package | `out/candidates/f953bbe-reach-camera-20260724-102013546Z` |
| `halo3xr.dll` SHA-256 | `2B492F23ECF7CBB158B5EE4072B01CDE1F4BF7439C8BFF8886649547269AC980` |
| `halo3xr_launcher.exe` SHA-256 | `B5E5D136D8283B3B8AE5864AC6EC43FB65D99DC987F64C0D6030A86425F29DDE` |
| Preserved failure evidence | `out/test-runs/f953bbe-reach-stereo-pass-culling-fail-20260724-102643Z` |
| Headset result | The Reach 3D looked great, but world visibility/culling followed the gun/stock aim camera instead of the headset |

The exact installed DLL matched the candidate manifest. The runtime armed
Reach stereo/6DOF, sustained approximately 100 FPS, submitted an OpenXR
projection layer, and reported zero frame-order failures. Retail and pinned
HREK evidence then isolated the remaining ordering defect: `main_render_view`
computes visibility from the secondary workspace camera at `+0x154/+0x1E4`
before the inner `player_view_render` hook installs either HMD eye. The next
forward candidate builds and mirrors one head-centred binocular-union camera at
the exact normal outer boundary before visibility. Its union covers the actual
widened symmetric image each canted eye rasterizes, and the bounded player-view
state receives the same centre as coherent pre-ownership state. Both eyes then derive from
that centre without applying turn, head pose, or lean twice. Head/pad/eye data is
one lock-free exact-frame snapshot, and title teardown proves callback/relay
quiescence before releasing hooks or the retained Reach module.
The stock pre-head direction remains separate and is not claimed as Reach
projectile or controller aim.

### Unaccepted Reach head-cull black-screen result - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `065f62a05a5b7ed4d733ac2ebfd30b5093190c73` |
| Candidate package | `out/candidates/065f62a-reach-camera-20260724-113854197Z` |
| `halo3xr.dll` SHA-256 | `12C6E10BD94B4022A57A697F5A9632786E8FF95AFEB0D139DCE632656038031C` |
| Preserved failure evidence | `out/test-runs/065f62a-reach-head-cull-black-20260724-114200Z` |
| Headset result | Reach became black immediately after stereo armed; the focused OpenXR session submitted zero layers |

The exact installed hash matched the candidate. A fresh Reach re-entry
reproduced `stereo on` followed by `focused shouldRender=1 layers=0`, with no
OpenXR frame-order or display-resource failure. The pinned retail image proves
the normal camera stack is empty at depth `-1`, and its push changes `-1` to
slot/depth `0`. Source `065f62a` incorrectly rejected every negative pre-push
depth in both its outer and propagated inner gates, so neither eye render/copy
could run. The forward correction changes only that proven admission bound to
`-1..2`, retains exact current-depth `pre+1` within `0..3`, and preserves the
head-owned visibility work. It remains headset-pending.

### Unaccepted Reach camera/culling pass, temporal-fog fail - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `86864bd088867a8e67950eb7d013d1c29d9f2d45` |
| Candidate package | `out/candidates/86864bd-reach-camera-20260724-115400094Z` |
| `halo3xr.dll` SHA-256 | `E66598671EBB602BF5D5B46CAA45F3E0678073603E8150C3A2641723B5DFD209` |
| `halo3xr_launcher.exe` SHA-256 | `4FAA18942886540FD4D212608D2485F17A7D68E575327CA6BF31D0252562ADAC` |
| Preserved headset evidence | `out/test-runs/86864bd-reach-camera-pass-fog-eye-fail-20260724-120101Z` |
| Headset result | Stereo, projection, 6DOF, head-owned visibility, and stick/head coherence looked great; fog/haze appeared eye-swapped and followed head motion |

The installed hashes matched the package. Reach submitted a projection layer at
approximately 94-120 FPS with zero frame-order failures, and the user confirmed
that the earlier gun-owned culling defect was gone. The full log SHA-256 is
`DFB8588BC7808C1902B97C219281AD3CE6B88C6479206EBD3A04973F61E9488F`.
The user also noted somewhat high VRAM use; the exact run allocated a bounded
approximately 395.5 MiB of logical mod/OpenXR texture payload at the runtime's
recommended `3400x3468` eye size, with no per-frame allocation or leak evidence.

Unlike accepted Halo 3 and ODST, Reach still ran its native temporal motion blur.
Pinned retail and HREK evidence identifies unique type-6 float controls
`motion_blur_scale` and `motion_blur_max`, authored as `0.35` and `0.08`.
This led to the first title-native suppression candidate below. Camera, culling,
eye order, projection, and capture remained unchanged.

### Unaccepted Reach invalid-distortion-constants result - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `facf6b0713ace0432e709916184d938fc553f4b1` |
| Candidate package | `out/candidates/facf6b0-reach-camera-20260724-122131028Z` |
| `halo3xr.dll` SHA-256 | `38BEEF66535A01E0AAC76A6FCFA52117183EEE305F2512663654DB29D6C492A0` |
| `halo3xr_launcher.exe` SHA-256 | `09E1F0450F2C667E43FF8E63F56CC8B08FA34BF9E458DB85DD64F5DA1D6EB5E7` |
| Preserved headset evidence | `out/test-runs/facf6b0-reach-alpha-fog-fail-20260724-072511Z` |
| Headset result | The fog-like contribution remained as a translucent/alpha texture following the head; this was not a valid blur-off result because both distortion operands were zeroed |

The installed hashes and the source identity in the first log line matched the
package. The full preserved log SHA-256 is
`380697D91F174E82B944211D515119A4C76058C7E1FFE07DD86AC4EF2C3854F3`.
The exact retail `apply_distortions` constant builder divides
`motion_blur_max / motion_blur_scale` at `0x00287561`, then divides the scaled
maximum by twice the scale at `0x002875AD`; HREK independently performs the same
operations at `0x0086BBA9` and `0x0086BBF9`. Source `facf6b0` wrote both
authored controls to zero, so both ratios became `0/0` NaNs inside the
screen-space distortion pass. Source `03f0bff` therefore preserved and
reasserted the positive authored scale and zeroed only the maximum. Its exact
headset result below proved that finite policy was active but also proved the fog
artifact was not native motion blur.

### Unaccepted Reach finite-blur-controls / opposite-head fog result - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `03f0bffbec5a4bdbe0b0784b47aeafc581505f1b` |
| Candidate package | `out/candidates/03f0bff-reach-camera-20260724-124956918Z` |
| `halo3xr.dll` SHA-256 | `456584DF50DF7B7941008BCF23EBC488F24938EE3D2C5B2E8F6A6FEEB182F6BB` |
| `halo3xr_launcher.exe` SHA-256 | `DA7525BFC4036A6D8F533F92A589C6F495A95CC956F2F7745018CFAC1694870C` |
| Preserved headset evidence | `out/test-runs/03f0bff-reach-alpha-persists-live-20260724-125506Z` |
| Headset result | Stereo, projection, 6DOF, head-owned culling, and stick/head coherence remained good; a translucent fog layer persisted and moved opposite headset motion instead of remaining world-stationary |

The installed hashes matched the package and the first log line reported the
exact source above. A live read of the pinned retail controls proved
`motion_blur_max=0.0` and the finite authored `motion_blur_scale=0.35`, so this
was a valid max-only blur-off result with no zero-over-zero distortion constants.
Reach remained focused with one opaque OpenXR projection layer, two current eye
caches, and zero frame-order failures. The artifact therefore is neither native
motion blur nor a separate OpenXR overlay; it is baked into Reach's rendered
screen-space fog work.

Pinned retail and HREK code then isolated the matching screen-aligned patchy-fog
pass. Retail `player_view_render` tests bit `0x08` at global RVA `0x00CA0240`
at `0x0026CC59`; when clear it calls the patchy helper at
`0x0026CC65 -> 0x0026EFEC`. HREK independently names the corresponding
resources `_surface_patchy_fog_buffer0/1` and `Patchy Fog Global Parameters`.
Source `b0710dc` sets only that proven skip bit immediately around each admitted
VR eye render and restores only that bit in `__finally`, preserving atmospheric
fog, distortion, camera/culling, eye order, capture, and all unclaimed or
non-owned renders. Its exact headset result is recorded below.

### Unaccepted Reach patchy-fog headset pass / scale-performance follow-up - 2026-07-24

This result is evidence only and does not advance the accepted pointer above.

| Identity | Value |
| --- | --- |
| Tested runtime source | `b0710dc01b1b6e5deec64830a42d33f19e1a52f1` |
| Candidate package | `out/candidates/b0710dc-reach-camera-20260724-132647552Z` |
| `halo3xr.dll` SHA-256 | `FF43BC89C5AFEC799DA43EB78EC58CC173B113DEC208FEE69E5F2B6235376C35` |
| `halo3xr_launcher.exe` SHA-256 | `AAE13DBDB454DEF58D4922F08C1D7E981E3AE408D84368182540F7D59D043615` |
| Preserved headset evidence | `out/test-runs/b0710dc-reach-patchy-fog-headset-20260724-132935Z` |
| Headset result | The opposite-moving translucent fog layer was gone and the image looked good; remaining follow-up is slightly small world scale versus Halo 3/ODST and a performance dip |

The installed files matched the candidate manifest, and the first log line
reported the exact source above. Reach's cold preflight and display proof passed,
the exact patchy-fog skip was active around each admitted eye, and stereo, head
tracking, and 6DOF armed after the safety interval. Focused frames retained
current private eye caches and zero frame-order failures. Logged stereo samples
were 50, 46, 57, 55, and 58 FPS. The full log SHA-256 is
`4A1B86F38E4799D2A48FF04E70F6316CAC4B7214AC7180BBAE8E6D2BA2F012`.
The same low cadence was already present before Reach loaded or stereo armed;
the per-eye patchy-fog wrapper is therefore not a causal performance regression.

The user explicitly confirmed that this fixed the fog defect. This is a narrow
Reach headset pass, not cumulative acceptance: exact physical-scale calibration,
performance follow-up, and Halo 3 plus ODST regressions remain pending.

### Reach stock runtime observation - 2026-07-23

The external read-only Reach observer from source
`5d34180ca935e7e32d0b1b2beffb014d198c774f` was run against stock
anti-cheat-disabled MCC. This was not a mod installation or candidate launch
and does not change either accepted source pointer.

| Identity | Value |
| --- | --- |
| Observer package | `out/diagnostics/5d34180-reach-runtime-observer-20260723-233050073Z` |
| Observer EXE SHA-256 | `AC43FA4F65256DF1CB46B9C0471DDA97E3120265AEDC876E0A3A73FC6A86CF6A` |
| Preserved run | `out/test-runs/5d34180-stock-reach-observer-20260724-025036448Z` |
| Evidence log SHA-256 | `3C36AF1F06FC428E914AB0C71330838587B020335EFBF2B017F8EF178768212D` |
| Observer result | `OBSERVATIONS_RECORDED_UNASSESSED`, reviewed as limited runtime corroboration |

- Two loaded-image preflights passed: the complete `main_render_view` checks
  were exact, while the frustum check used a unique 24-byte prefix of the
  canonical 25-byte entry. Across two admitted sessions the observer recorded
  29,507 accepted exact-slot transactions, 29,496 valid camera samples,
  seven one-second stable windows, zero invalid cameras, zero multi-owner
  intervals, and zero module-snapshot failures.
- The external observer paused and reset sampling during multi-title
  ambiguity, reran source `5d34180`'s preflight (including its unique 24-byte
  frustum-prefix check) on re-admission, then recorded one Reach unload/title
  exit.
- Every transaction was slot 0, so the run corroborates the array base but not
  the `0xA40` stride or split-screen behavior.
- Subsequent pinned retail/HREK analysis resolved the second caller as Reach's
  screenshot tile/bloom path, established its exact stock-only routing
  requirement, bounded the synchronous camera workspace and its `0x2B0`
  render-scope snapshot, and identified surface group 1 as the swapchain
  display target written by late
  native CHUD. It also selected exact inner candidate `player_view_render`,
  proved its identity and active-scope lifetime, and bounded the stock
  pre-scope camera rebuild. Production outer-owner propagation, live serial
  reuse, inside-scope/OpenXR camera mutation, live target identity/copy, broader
  lifecycle/device-loss behavior, callback quiescence and teardown,
  stereo/OpenXR, and headset behavior remain unproven. All Reach runtime hooks
  remain unauthorized and disabled.

## Accepted cumulative release

The current known-good product is the public
[`MCC_VR_ALPHA_0.2.2`](https://github.com/pancreations/Halo-MCC-VR/releases/tag/MCC_VR_ALPHA_0.2.2)
Halo 3 + ODST release. It supersedes `MCC_VR_ALPHA_0.2.1`.

| Identity | Value |
| --- | --- |
| Release tag commit | `e2c049e5c3b98ce466f6072da4e0aa55ccc88e10` |
| Headset-tested runtime source | `3a2a11bfc66b36e70f60282e91c9d5436f2e18d1` |
| Build | Release x64, `HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP=ON` |
| Release ZIP SHA-256 | `43E52AEF5A2D1647A8F3AE6AEFDB6C22F0C67C7AA06FD70D327FB3E00ACF5DCC` |
| `halo3xr.dll` SHA-256 | `1E3F0F7E1D67DB7F322FF0B2C0236CA8708E4C9EC204EDE83484DBD6BBAF3BD6` |
| `halo3xr_launcher.exe` SHA-256 | `FA95B264630D42594581E4D2F8E1103FE4DB2D0711714DA4F62AA6175155C534` |

The 0.2.2 runtime source `3a2a11b` builds directly on the accepted 0.2.1 line
(`034c4a6`) with three evidence-backed ODST fixes (GitHub issue #18 -
head-relative movement, cinematic FOV parity, and steady rumble) and one XInput
connection-stability fix (the in-game menu no longer dies after Save & Quit).
The tag adds only release documentation and packaging on top of that tested
runtime; `src/` and `tests/` are otherwise the accepted line. The launcher was
rebuilt from the same source, so its bytes differ from 0.2.1 while its behavior
does not.

### Previous accepted release (rollback baseline)

`MCC_VR_ALPHA_0.2.1` remains a protected rollback baseline. Its runtime is
byte-identical to `034c4a6`; the tag added only documentation and packaging.

| Identity | Value |
| --- | --- |
| Release tag commit | `3d7989e1a8e0cb34747a91801c4525ef70b29866` |
| Headset-tested runtime source | `034c4a68e362b334d7994aa9e694243abf2aade5` |
| Release ZIP SHA-256 | `C5AE012BC379CBC7A909652D297DC0E8059CDBF41D26260771B385F8F729B124` |
| `halo3xr.dll` SHA-256 | `B7363F79650E42A04D4CED6A3F51F57A6B4C2F376FF00298A6173A8287752CEF` |
| `halo3xr_launcher.exe` SHA-256 | `BDC0A20F56DF72CDDE68E5D0AB621321FBDE91DA427B6C24142B38336D33EA6D` |

Protected rollback copies of the 0.2.1 ZIP:

- the official GitHub release asset;
- `dist/HaloMCCVR-odst-menu-fix-034c4a6.zip`;
- the user's external safe-folder copy.

An artifact is evidence, not an automatic deployment source. Never install it
unless the user explicitly asks.

### 0.2.2 headset confirmation - 2026-07-23

- Source commit: `3a2a11bfc66b36e70f60282e91c9d5436f2e18d1` (branch
  `cleanup/release-0.2.1`), built Release x64 with
  `HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP=ON`.
- Candidate package: `out/candidates/3a2a11b-20260723-142432262Z`.
- `halo3xr.dll` SHA-256
  `1E3F0F7E1D67DB7F322FF0B2C0236CA8708E4C9EC204EDE83484DBD6BBAF3BD6`; the
  installed file's hash was verified separately after the manual copy and
  matched. The log does not contain the hash.
- Log source: the first line reported `source 3a2a11b...`, ODST ON, compiled
  `Jul 23 2026 09:24:22`, from the canonical `Halo_MCC_VR\halo3xr.log`.
- Title coverage: Halo 3: ODST (stereo, head-relative movement, cutscenes,
  rumble, Save & Quit to menu, and cross-title re-entry). Halo 3 shares the
  touched XInput controller path.
- Result: the user confirmed all three issue #18 fixes still feel right and that
  the in-game menu no longer goes dead after Save & Quit, from both plain
  gameplay and a cutscene.
- Runtime evidence: the retained `M3 DIAG` line held `gateIdle=0` through normal
  play, then rose to `100` and `106` across a Save & Quit / title-teardown
  window while `reads`/`padValid`/`merged` kept climbing. The mod answered the
  slot-0 polls in that brief gated window as connected and idle, so MCC never
  latched a false controller disconnect and the pad stayed live.

## Desktop stale-version audit

This audit predates the 0.2.2 hotfix and describes the earlier 0.2.1 install
(`halo3xr.dll` `B7363F79...`), which was accepted at the time. As of 0.2.2 the
accepted `halo3xr.dll` is `1E3F0F7E...`.

The 2026-07-23 desktop installation is not running an older binary:

- installed DLL hash is the accepted `B7363F79...`;
- installed launcher hash is the accepted `BDC0A20F...`;
- the first log line reports embedded build `Jul 22 2026 12:59:32`;
- the launcher log resolves the canonical `Halo_MCC_VR` folder;
- the desktop shortcut points to that launcher;
- no second mod DLL or launcher exists under the MCC installation.

The old repository build trees contained unaccepted DLLs, and the legacy
scripts could either build ODST support OFF or restore an older vibration DLL.
Those build trees and scripts are not part of the clean baseline. If behavior
still differs from the laptop, investigate configuration, OpenXR/runtime state,
MCC title-module state, and the exact log; do not assume a source rollback.

The audited desktop log also showed the known multi-module ambiguity during
title switching and an ODST camera-readiness tail toggling before the user
paused. Those are runtime observations from the accepted binary, not proof of a
stale install.

### Desktop ODST headset confirmation - 2026-07-23

- Runtime source: `034c4a68e362b334d7994aa9e694243abf2aade5`.
- Installed artifact:
  `N:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\Halo_MCC_VR\halo3xr.dll`,
  SHA-256 `B7363F79650E42A04D4CED6A3F51F57A6B4C2F376FF00298A6173A8287752CEF`.
- Result: the user explicitly confirmed that ODST hooked, worked, and remained
  playable during a brief headset session.
- Runtime evidence: ODST was detected at `06:27:14.962`, its core hooks finished
  installing at `06:27:25.848`, and stereo, head tracking, 6DOF, controller aim,
  VRIK, authored crosshair, and distinct per-eye output armed at `06:27:36.205`.
  The delay covered title/camera loading, an OpenXR visible-but-unfocused
  interval, and the required one-second fresh-camera safety interval.
- A read-only 250-sample layout capture found only slot 0 active; it made no
  game-memory writes. No new build was installed, and no Halo 3 regression was
  run in this brief ODST-only session. The accepted-build pointer is unchanged.

## Headset-confirmed coverage

Halo 3:

- stereo, 6DOF, controller input and aim, head-relative movement;
- articulated arms, support hand, floating hands, and tested dual wield;
- native HUD, authored VR crosshair, HUD controls, scopes, and resolution scale;
- cutscene facing, pause/resume, death/respawn, mission exit/re-entry;
- smooth turn, recoil suppression, haptics, and shared configuration.

ODST on the accepted build:

- stereo, 6DOF, head-relative movement, snap/smooth turn;
- controller-driven gun/hands, arm IK, two-hand and floating-hand options;
- native HUD, authored floating crosshair, HUD controls, and vibration;
- stereo cutscenes with head look and authored-shot facing;
- death/respawn recovery, one tested drivable car, and cross-title re-entry;
- in-game menu stick fix from GitHub issue 9.

## Known limitations

- ODST's first captioned opening cutscene can be black; skip that first scene
  once.
- MCC can retain multiple title modules and return a level load to the menu.
  Fully restart MCC as the release workaround.
- ODST brightness must remain at the game default; the attempted brightness
  hook hid the entire HUD and was reverted.
- Only one ODST car is headset-confirmed. Broader weapon, vehicle, turret,
  passenger-gun, co-op, headset, and long-session coverage remains open.
- Full-body legs/torso are not implemented; VRIK covers first-person arms.
- Projectile direction follows the controller, but Halo still owns the actual
  fire origin.
- A local rebuild is not byte-reproducible because compile date/time and
  toolchain output are embedded. Use the release ZIP for exact accepted bytes.

## Rules that survive cleanup

- One cumulative multi-title line: every accepted build retains Halo 3 and ODST.
- Halo 3 is the player-facing parity foundation for new titles.
- Per-title offsets, signatures, layouts, bones, markers, tags, and calibration
  require per-title evidence.
- Render world, first-person weapon, native CHUD, and capture for each eye as one
  lifecycle transaction after the one-second fresh-camera safety interval.
- Never hook `halo3+0x120DF8`.
- Never write guessed camera, animation, model-root, or CHUD offsets.
- Unique signatures only. Zero or multiple matches install no hook and acquire
  no VR ownership. Once claimed, a transaction failure is terminal for that
  transaction and never rerenders through a flat or stock path.
- Never patch game files or interact with Easy Anti-Cheat.
- A successful candidate package automatically installs only its exact
  manifest-verified DLL/launcher through `tools/install-candidate.ps1`, with MCC
  closed, the prior install preserved, and post-copy hashes verified. It never
  launches MCC or changes `halomccvr.cfg`; restore/uninstall scripts remain
  forbidden.

## Candidate and acceptance workflow

1. Start from this accepted source line.
2. Make one behavioral change and give it a unique commit.
3. Build and test the cumulative Release preset from `BUILDING.md`.
4. Use the safe package command to create a unique candidate under `out/`;
   never overwrite the accepted ZIP or reuse a candidate directory. After every
   successful build/test/package, it automatically backs up the current install,
   deploys that exact candidate, and verifies the installed hashes.
5. Record source commit, DLL hash, unique package path, embedded log
   source/configuration, title coverage, and headset result. Verify the installed
   hash separately because the log does not contain it.
6. Advance this pointer only after explicit acceptance. A failed or untested
   candidate is reverted and does not advance the line.
7. Run a Halo 3 regression whenever shared code or cross-title lifecycle state
   changes.

## Evidence map

- `docs/RE-notes.md`: verified Halo 3 reverse-engineering facts.
- `docs/EDITING-KIT-EVIDENCE.md`: evidence policy.
- `docs/ODST-SIGNATURE-EVIDENCE.md`: ODST signatures and HUD evidence.
- `docs/ODST-CAMERA-LAYOUT.md`: ODST camera/view layouts.
- `docs/ODST-WEAPON-IK-EVIDENCE.md`: ODST weapon and skeleton evidence.
- `docs/REACH-EVIDENCE-MANIFEST.json`: pinned Reach retail/HREK identities and
  preliminary evidence-only RVAs; not an accepted runtime pointer.
- `docs/REACH-SIGNATURE-EVIDENCE.md`: Reach proof ledger; controller transport
  is headset-accepted, while camera-core candidates and their exact headset
  results remain unaccepted until this pointer advances explicitly.
- `docs/TITLE-RUNTIME-OWNERSHIP.md`: accepted shared heartbeat/generation
  ownership contract and its cross-title regression evidence.
- `docs/RESOLUTION-FSR-INVESTIGATION.md`: active (not accepted) findings for the
  resolution-scaling and FSR feature work — verified facts, labeled hypotheses,
  and open questions. No behavioral change shipped.
- `docs/HISTORY.md`: how to retrieve the full pre-cleanup ledger.
- `releases/0.2.2/manifest.json`: current machine-readable release identity.
- `releases/0.2.1/manifest.json`: protected rollback release identity.
