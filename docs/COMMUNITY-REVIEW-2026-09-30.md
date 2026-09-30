# September 30 community review: evidence and next-build inventory

This records the Discord review preceding this candidate. It is an evidence
inventory, not a claim that every report was reproduced or fixed. The September
23 audit, coverage document and implementation ledger remain part of the record;
no old issue is closed merely because it was not repeated this week. Private logs
and contributor archives remain in ignored `out/` and are not redistributed in
the public source archive.

## Review boundary

The latest pass covered September 25-29 messages in Halo VR HUB, the Flat2VR
MCCVR WIP thread and the recent DM conversations inspected during that pass.
Halo VR HUB search returned 154 results over seven pages. Recent DMs and linked
older contributor messages were also inspected. This is the observed review
boundary, not proof of access to every private message, deleted message, thread,
attachment or future reply. Earlier September 23 coverage is separately recorded
in `COMMUNITY-AUDIT-COVERAGE-2026-09-23.md`.

Sources:

- Halo VR HUB general: https://discord.com/channels/1069072806542848020/1069072806542848023
- Flat2VR MCCVR WIP: https://discord.com/channels/747967102895390741/1546190376405049415
- Gab_dC DM: https://discord.com/channels/@me/1550772425191325817
- skivy44 DM: https://discord.com/channels/@me/1554304801154535544
- Godoy DM: https://discord.com/channels/@me/1539978287655419904
- Gab_dC PR16: https://github.com/moistman42069/MCCVR-Halo-Build/pull/16

## New reports and contribution follow-ups

| ID | Report/evidence | Disposition and needed proof |
| --- | --- | --- |
| N01 | Gab_dC PR16, five commits through `3c07886`, based on local `566e52c`: Plus/Standard virtual stock, free two-hand handling, persistent support, transitions, smoothing, coherent aim and local telemetry. | Review and adapt source; independent integration review documents exclusions/corrections. Contributor test claims are not acceptance of our rebuilt candidate. |
| N02 | Gab September 29 work on F1 grip-location capture, human/Elite rigs, per-weapon two-hand policy, primary-only plasma pistol and disabling sword support. | Proposed/forthcoming work, not silently treated as part of PR16. No completed supplied integration identified in the reviewed PR head. |
| N03 | skivy44: Reach ONI Sword Base, Quest 3 Touch, Steam Link/SteamVR, 120 Hz, Universal Reclaimer, defaults/no custom bindings; later says v0.6.0 plus USE -> Weapon Grip works around the problem. | Exact original symptom not explicit in this DM. Older f53f0bd log is not proof of a v0.6.0 regression or proof of binding causality. Keep native Use/Reload alias independence open. |
| N04 | skivy44 previous log: first-chance fault, Reach teardown/rearm, changing gameplay/loading state, frozen camera proof, eventual module unload and Present stall. | Exact build map identifies a guarded SafeFrame probe. See stability review; no claim that the caught read caused a fatal crash. |
| N05 | Vixel September 28 latest-release ODST random campaign crashes; bridge-explosion mission suggested as more repeatable. wwm0nkey corroborates September 29. | No new exact build, exception stack or full environment. Prior Hunter report is a separate possible lead, not a proven shared cause. |
| N06 | Totally not peakhorse: headset blackouts requiring restart; Quest 3 mentioned earlier, high-quality cable/latest software. | Runtime, connection path, build and logs incomplete. No demonstrated native title cause. |
| N07 | Quest 3 view upside down after MCC restart; suspected MCC-before-SteamVR launch order. Chronomize reports opening VR menu repaired their similar case. | Startup/recenter/tracking-space reproduction required; workaround is not a diagnosed correction. |
| N08 | H2 pixelated picture despite recommended settings. | Need renderer mode, live dimensions, runtime resolution, DLSS state and screenshot; no universal render-size fix asserted. |
| N09 | Arms-display option reportedly ineffective; replies disagree about affected titles. | Unspecified title cannot establish universal absence. Preserve separate CE Classic eye mismatch and CE Anniversary world-anchored VRIK reports. |
| N10 | CE Classic arms differ by eye and look flat/shiny; CE Anniversary arms stay in world during head movement/turn. | Reflectivity may be authored; world anchoring and per-eye mismatch need exact build/mode reproduction. PR16 changes support presentation but does not establish these reports fixed. |
| N11 | Vixel H3 cinematic-spin Workshop workaround, still spins at Crow's Nest command-center return and briefly in Cortana. | External map workaround only: https://steamcommunity.com/sharedfiles/filedetails/?id=3567825314 . Native spin report remains open. |
| N12 | Count Scaphandre Linux forced Xbox login despite flat session signed in, cannot type credentials. Chronomize SteamDeck/Proton earlier report: menu works, both VR and SteamDeck keyboards fail on login. | No proven Linux/Proton support or login fix. Preserve earlier 101 KB log. Windows launcher changes cannot be called a Proton solution. |
| N13 | The_King_of_Normies Windows login/input report: disabling RivaTuner overlay seemed to help; Count does not have it. | Reporter-specific workaround, not universal diagnosis; do not conflate older corrected Afterburner attribution. |
| N14 | Gab Windows native menu navigates but cannot select until Alt+Tab foregrounds MCC, each boot. | Focus behavior remains a distinct reproduction. Do not steal focus repeatedly from arbitrary apps as a speculative cure. |
| N15 | No new co-op reproduction in the reviewed date range. | Old campaign lockstep, CE host/client, vehicle and physical-melee issues remain open; silence does not establish compatibility. |
| N16 | Pancreations September 6 build/source `3f86346` re-downloaded; search found no newer DLSS delivery in that inspected result set. | Earlier contribution already adapted into current all-title optional DLSS. Do not overwrite newer source with this older archive. |
| N17 | WreckedElk's earlier RenoDX/ReShade/DLSS5-Feeder links. | Separate external neural-rendering lead; not an implemented native MCCVR fix and not included as an executable dependency. |
| N18 | User: administrator launcher by default; separate launcher from manual drag/drop files. | Implement manifest elevation, split package layout and update compatibility; test synthetic installs without touching MCC. |

## Godoy report reconciliation

The complete September 23 attachment remains represented by the prior ledger.
The following details must not disappear into a generic 'rendering' entry:

| Title | Outstanding reproduction details and reported successes |
| --- | --- |
| CE | Left-hand axes; Anniversary shoulder obstruction and white Covenant/Forerunner lights; inversion tutorial needs HMD look; low/clipped crosshair with shots above; Warthog view under chassis/inert adjustment; passenger rear/left firing; captions absent in VR; Classic arm eye mismatch; DLSS performance loss. |
| H2 | Anniversary HMD-attached lights; Classic visual calibration versus projectile reticle; snap/follow/menu camera issues; vehicle horizontal stick/vertical HMD aiming versus controller reticle; passenger rear/left limits; checkpoint starts third-person; plasma rifle head-origin; dual visible-hand sliders; Anniversary DLSS lasts about one second and quality/performance problems; dummy dual reticles; Classic cinematic caption lifetime; barrel toggle lowers reticle. Reporter marked dual right-hand roll and Anniversary Warthog flicker fixed in their test. |
| H3 | Passenger limits; rapid-fire plasma-pistol recoil/Needler dispersion versus reticle; DLSS quality 90 -> 60; cutscene captions disappear after about one second. Reporter marked Needler/Brute Shot barrel alignment fixed. Intended native dispersion must be separated from transform mismatch. |
| ODST | Similar H3 issues, additional muzzle/reticle mismatch, bloom disable reportedly inert. New random-crash reports remain separate. |
| Reach | Muzzle still head-origin on Prophet's Hand custom content; passenger offsets; per-eye smoke; severe DLSS FPS drop; bloom disable reportedly inert. Stock-map comparison required. |
| H4 | Muzzle-origin option; Mantis reticle rectangle; stale letterbox strips with theatre off; cinematic-cut recenter; shield/Mantis-melee black flashes; absent muzzle flash; gameplay and cinematic captions vanish after about one second; DLSS FPS loss. |

Godoy's Game Pass/OpenXR Toolkit context is not a controlled Steam/no-Toolkit
comparison. Personal H2 yaw/pitch values are calibration leads, not defaults for
everyone. The attachment marked split Weapon/Aim UI, magazine locations and two
dummy reticles done. It did **not** mark the requested cock/feed sound tail done.
Native shortened reload and its authored audio are not a newly guaranteed sound
event. Dual shake reload, mirrored visible-hand offsets, bloom/autoexposure and
shotgun pump/visible-shell proposals retain their individual prior-ledger status.

## Download receipts

| Private artifact | SHA-256 | Handling |
| --- | --- | --- |
| `out/discord-review-skivy44/HaloMCCVR.log.prev` (162,322 bytes) | `A61F2D91D31A70A38BE352DF336959CC058A03754CDE105666A0EDFE9E8D394A` | Read as evidence; no contributor executable run. |
| `out/discord-review-pancreations/HaloMCCVR-build-3f86346.zip` (1,407,890 bytes) | `BC11A8777794685860121181072168570F774B3611EFB89ECA466BCD3650B9A8` | Archive manifest inspected; old unaccepted candidate, not installed. |
| `out/discord-review-pancreations/HaloMCCVR-source-3f86346.zip` (3,759,535 bytes) | `2A09915727E922EBCEC276DEC026A976B9DB8CB74200DCF0B1938CE3C7CDC21C` | Source/docs compared with newer integration; no wholesale replacement. |

The separate 18 KB skivy44 startup log was read in Discord preview, not downloaded
in that pass. It identifies f53f0bd and a 120 Hz panel/roughly 60 Hz app cadence,
profile discovery then Touch recognition, and a visible stall before shell. This
is not an unexplained latest-build performance measurement. Old Pancreations
archive notes about CE being absent and H2 cleanup are historical limitations;
current CE DLSS adapters and checked retirement must be assessed from current
source. Private attachment URLs can expire; retain these hashes and copies.

## Completion rule

Source integration, passing offline checks and packaging can be completed here.
Headset parity, co-op lockstep, exact crash causes without failing stacks, GPU
performance and unspecified visual reproductions cannot be declared solved from
these artifacts. The next-build status must list those blockers explicitly, keep
all earlier scope and preserve the accepted build pointer.
