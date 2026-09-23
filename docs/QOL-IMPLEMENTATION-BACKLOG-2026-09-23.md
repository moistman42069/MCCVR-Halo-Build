# MCC VR quality of life implementation backlog

This is the implementation plan produced by the September 23 community audit. The target is the broad QoL release requested by the user. Items below are requirements or investigation tasks, not shipped fixes. [Evidence and message IDs](COMMUNITY-REPORT-AUDIT-2026-09-23.md) preserve reproduction details and corrections; [coverage](COMMUNITY-AUDIT-COVERAGE-2026-09-23.md) records what was reviewed. Existing approved scope in [the continuation list](CONTINUATION-REFINEMENT-LIST.md) remains in force, including its correction limiting ordinary dual-wield work to H2 and H3.

## Delivery sequence

1. Establish build-specific reproductions for co-op, ownership and transition failures. Separate campaign lockstep, competitive multiplayer and Forge; separate host/client, native/VR peers and stock/custom maps. Resolve confirmed unsafe ownership/lifetime paths before extending interactions.
2. Introduce the consistent VR action layer and gesture arbitration, then migrate settings. This is the foundation for reload, holsters, two-hand, flashlight, vehicle and controller compatibility work.
3. Address weapon/hand/body/vehicle correctness and reload usability using the same semantic actions.
4. Adapt contribution changes independently: subtitles/menu, rendering controls, measured cleanup improvements, virtual stock and DLSS. Preserve each feature's independent fallback.
5. Complete cross-title regression and user headset acceptance, then package matching build/source ZIPs without installation. A major release can contain the accepted results of many focused candidates; it should not conceal which changes lack runtime validation.

## Input and settings requirements

| ID | Required outcome | Evidence or dependency |
| --- | --- | --- |
| IN01 | Detect active OpenXR controller interaction profile and provide equivalent semantic defaults across all six games. Unknown profiles receive an explicit usable fallback rather than guessed controller identity. | User; Vive wands, WMR, DPVR compatibility profiles. Runtime family is not necessarily physical headset identity. |
| IN02 | Decouple VR actions from MCC's chosen layout, inversion and per-title mapping differences. Reach must not silently change controls. | User; Universal Recon/Reclaimer differences; H3 inversion; reload invokes H4 hologram or vehicle entry. Requires verified title-specific action adapters. |
| IN03 | Add a dedicated VR Mappings section with **per-game saved overrides** and explicit **Unbound** for any action. Keep common automatic defaults and clear reset-to-default controls. | Latest explicit user requirement. Unbound must not fall back to a native/default action. |
| IN04 | Migrate/remove scattered melee/reload button selectors without losing existing preferences. Show readable controller-aware action labels. | User; Vive wand Y/dual-wield prompt unusable. |
| IN05 | Centralize gesture/button priority: menu consumption, holster draw, reload, support-hand grip, flashlight/head gesture and weapon pickup must not trigger one another. | Opening menu reloads gun; disabled reload suppresses pickup; reload enters Warthog; retained right grip blocks support grip. |
| IN06 | Refine optional flashlight disable so it never steals two-hand grab/release. Evaluate a separate flashlight gesture toggle and smaller head activation region. | User; UnsealedWings historical head-D-pad/left-grip proposal. |
| IN07 | Fix accidental horizontal setting edits during vertical navigation; allow pointer-only navigation or suitable stick-drift suppression. | GizmoSlipTech and guydude. |
| IN08 | Organize menu into Overview, Controls/Mappings, Weapons/Aiming, Hands/Body, Reload/Holsters, Vehicles, Picture, HUD/Subtitles and Advanced/Diagnostics as needed. Keep per-game scope visible. | User; Godoy Weapon/Aim split; wrong-game shader selection and resolution confusion. Section names are proposed, not frozen. |
| IN09 | Explain settings that require restart, vehicle reentry or title-specific support at the relevant control. Retain pause/settings visibility during live multiplayer. | Resolution reportedly inert; FP vehicle toggle only applies after reentry; CE multiplayer menu vanishes. |

## Stability and multiplayer

| ID | Work item | Reproduction and acceptance focus |
| --- | --- | --- |
| ST01 | Diagnose co-op firing failure even with optional features off. | Preserved prior checkpoint reports firing in any direction fails; do not collapse into melee/vehicle workaround. Exact builds, two peers and native fault symbols required. |
| ST02 | Make true physical melee and dual wield safe in campaign co-op. | Vixel H3 kick/crash versus competitive MP success; duplicate simulation/local ownership and state restoration. |
| ST03 | Make first-person vehicles safe in co-op. | GitHub2 H3/ODST black/menu including LAN, flat/VR mixed peers; workaround off is not completion. LivingFray is design evidence only. |
| ST04 | Fix CE host/client body/action synchronization at doors, crouch obstacles, death and teammate kills. | Skates Pillar of Autumn host fine/client freezes; teammate kill loses tracking; distinguish current outgoing-action candidate from accepted behavior. |
| ST05 | Repair match end, title hopping, new mission, checkpoint/resume and respawn lifetime transitions. | Ikaros H3 post-match; Godoy CE mission/Reach resume; Chronomize ODST reentry; Sammy/Godoy H2/H4 logs. |
| ST06 | Fix Reach MP/Forge vehicle admission and restore stereo through entry/exit/overheat/death. | Campaign works for Vixel; MP driver/turret fail on different triggers; Tec menu workaround. |
| ST07 | Diagnose H2 Classic/Anniversary black/2D/crash and explosion triggers separately. | Shaun f53f0bd, PC-Genie, repeated halo2+67BA50 anchors; first-chance events are not necessarily fatal. |
| ST08 | Resolve Reach eye-allocation wait and ODST camera-admission wait independently. | Rainred versus skivy44 logs have different readiness failures despite preflight success. |
| ST09 | Preserve custom-map support while identifying content-specific incompatibilities. | Cursed CE/ODST, Ruby's CE, Reach Mythic/Prophet's Hand, Ultimate Firefight. Always compare stock map before attributing engine-wide failure. |
| ST10 | Profile title-hop slowdown and long-session degradation with timestamps and allocation/ref-count evidence. | H3 Assembly after title hopping; Unsealed leak claim; no diagnosis from a slow session alone. |

## Weapons, hands and body

| ID | Work item | Reproduction and acceptance focus |
| --- | --- | --- |
| WB01 | Correct primary and secondary projectile **origin and direction** independently in H2/H3. | H2 left projectile begins at right gun even when direction follows left; include independent obstruction tests. |
| WB02 | Honor authored-barrel versus controller aim consistently across all supported titles and eligible weapons. | Reach/H4 option allegedly ineffective; H3 Needler/Brute Shot high; H2 plasma rifle originates near face. |
| WB03 | Remove unintended recoil/animation dependence from tracked shot direction; retain intended native spread. | Godoy rapid plasma pistol/Needler; do not confuse intended dispersion with origin defect. |
| WB04 | Fix Classic/Anniversary alignment and controller pivot differences. | Godoy H2 Classic yaw/pitch preset is personal calibration, not a universal constant. |
| WB05 | Correct CE red-reticle, magnetism and homing alignment. | Chronomize clips with Eye Patch off, Vixel Needler; compare actual hit/projectile behavior rather than reticle alone. |
| WB06 | Restore support-hand acquisition while holding holster-draw grip; improve grab-zone discoverability and weapon-size handling. | livinginparadice22; CE AR two-hand; H2 difficult grab point. |
| WB07 | Separate visible-hand offsets from aim/calibration; support left-handed routing and valid mirrored dual-hand offsets. | Godoy dual sliders inert; Chronomize secondary-hand adjustment; existing approved hand-offset scope. |
| WB08 | Fix camera-mounted or invisible custom weapon/reload objects through verified marker/bone handling. | Ruby's CE sword, Reach Mythic CE Magnum/SMG, Light Rifle/Cursed ODST reload parts. No copied cross-title offsets. |
| WB09 | Make physical melee hit eligible glass, doors and small targets without body-collider obstruction. | CE bent door/glass, small-weapon reach; button melee is comparison. |
| WB10 | Investigate melee impulses causing spinning ammo-crate deaths. | Vixel repeated deaths and sword/ammo-crate incident; distinguish damage routing from physics impulse. |
| WB11 | Eliminate roomscale body drift and incorrect AI target position; preserve moving-platform and ladder behavior. | GitHub14, PC-Genie legs far away, H4 Mammoth, H2 ladders, weapon-induced camera movement. |
| WB12 | Make body visibility and physical crouch coherent and optional across titles. | User community requests, hide-body control reportedly ineffective; historical gesture interference warns against forced defaults. |
| WB13 | Investigate involuntary H3 spin at Crow's Nest door separately from cinematic camera control. | Vixel precise post-bug-fight reproduction. |
| WB14 | Add relaxed offhand pose and optional hand-near-head HUD reveal with explicit gesture ownership. | DarthBowie and Vixel; must not conflict with flashlight or grab. |

## Reload, holsters, zoom and vehicles

| ID | Work item | Reproduction and acceptance focus |
| --- | --- | --- |
| RV01 | Make manual reload work through semantic reload regardless of native layout; repair detection/range and unsupported weapon visuals. | Vixel, SilentVortigaunt, guydude; include empty/nonempty weapon and all reload-animation modes. |
| RV02 | Make Needler shake and dual-gun reload consistent where dual wield is native. | H2 Classic MP, main chat inconsistency, Godoy dual shake request. |
| RV03 | Add adjustable reserve-mag XYZ/location and optional front-hip/behind-shoulder retrieval with final feed/cock feedback. | Godoy and Chronomize; accessibility and gesture collision tests. |
| RV04 | Permit disabling button reload/swap independently while retaining physical interactions and pickup. | Godoy; guydude pickup regression. Preserve full-disable versus shortened-animation distinction. |
| RV05 | Complete circular zoom quality, refresh and cross-title support; per-weapon eligibility instead of arbitrary scope on every weapon. | Existing scope plus rocket/Spartan laser feedback. |
| RV06 | Verify zoom changes native accuracy/magnification appropriately, not merely the picture. | H3 BR/carbine pseudo-zoom spread; LivingFray two zoom-level design lead. |
| RV07 | Fix CE FP Warthog camera under chassis, inert seat sliders and invisible in-match menu. | Multiple reporters/campaign and Blood Gulch. |
| RV08 | Fix H2 steering sensitivity, Ghost controls, Warthog judder and controller-directed turret/seat aim. | Godoy, PurplePeanut, BlackHawkCJ, Vixel; separate Classic/Anniversary. |
| RV09 | Honor vehicle-view follow, snap/smooth preference, temporary smooth-in-vehicle option and menu camera positioning. | Prior user scope plus Godoy; preserve normal turn preference on exit. |
| RV10 | Correct passenger rear/left firing restrictions and horizontal offsets per verified seat. | Godoy H2/H3/ODST/CE versus Reach; do not globally remove legitimate native limits. |
| RV11 | Preserve FP selection and seat transforms through save/load, entry/exit and title transitions. | H2 checkpoint resets third-person; shared seat-trim misconfiguration in Chronomize logs. |

## Rendering, UI and performance

| ID | Work item | Reproduction and acceptance focus |
| --- | --- | --- |
| GP01 | Fix CE Anniversary light pillars/streaks and head-attached beam-tower effects. | Flare-disable toggle did not resolve; Samurai capture confirms streaks. Keep digital-glass/bridge report too. |
| GP02 | Correct CE Anniversary one-eye cinematic bars, white outline and close-up convergence. | Godoy, SilentVortigaunt, Gizmo; Vixel retracted apparent one-eye fix. |
| GP03 | Fix CE Classic unsupported-arm eye mismatch and transient HUD at crosshair on load. | Vixel; transient artifact differs from persistent doubled HUD. |
| GP04 | Correct H2 Anniversary head-attached station lights, moving-vehicle flicker and Classic AO dithering. | Godoy and PC-Genie. |
| GP05 | Investigate Reach stereo foliage/grass, gray filter and aliasing separately from flat-game LOD. | Martysl1 REACHVEG99 WIP, Vixel, Chronomize, BlackHawkCJ/Gizmo. |
| GP06 | Add carefully scoped bloom/exposure and picture controls with stock-preserving defaults. | Godoy/Martysl1/Pimax feedback; zoom/postprocess and shader-toggle corruption must be tested. |
| GP07 | Adapt native localized subtitles with gameplay/theater enable, scale, position and anchoring plus correct title arbitration. | Martysl1 H3/ODST/Reach source; H4 incomplete; CE/H2 need own evidence. |
| GP08 | Improve menu readability/wrapping and subtitle section without simply increasing every control's size. | Martysl1 1280×800/font1.9 proposal; user clutter requirement. |
| GP09 | Repair custom-HUD discovery with bounded verified tag/ownership evidence and scan backoff. | UnsealedWings source uses broad float ranges; cannot treat those as sufficient identity proof. |
| GP10 | Correct reticle hide semantics, independent dual reticles and radar/player-dot alignment. | livinginparadice H2 hide mounts reticle at camera; Godoy dual reticles; Ikaros off-center radar. |
| GP11 | Review H4 carried-forward reticle, letterbox, recenter, damage/vehicle blackout and muzzle-flash reports against current source/runtime. | Godoy explicitly labels these older; do not reintroduce obsolete fixes. |
| GP12 | Integrate optional DLSS only after title-specific depth/motion/history, resize, reentry and image-quality validation. | Pancreations source plus Martysl1 Reach delta. This is not a request for external DLSS5/ReShade/frame generation. |
| GP13 | Profile cleanup backoff, frame-wait lifecycle and actual renderWindow deadlines. | Martysl1 PERFCPU1.2; Unsealed claims partly already implemented and older worker cleanup is weaker. |
| GP14 | Resolve resolution-setting clarity/effect and direction-dependent map performance. | Gritt unchanged picture, Haloex30vs80–120; include render size, refresh/deadline and VRAM data. |
| GP15 | Retain documented resolved cases as regression checks. | H3 skybox draw-distance setting, CE doubled HUD parallel reprojection, H3 native inversion/reset, removed Toggle HUD wrapper, corrected Afterburner conflict. Do not label each a new code defect. |
| GP16 | Investigate Samurai September19 missing-geometry/black-void clip with build/title attribution before assigning a fix. | Visual evidence exists; written reproduction is absent. |

## Contributions and integration disposition

| Contribution | Preserved evidence | Integration decision |
| --- | --- | --- |
| Pancreations DLSS | Matching build/source3f86346, main message1546291228713160734; source, shader/motion/history and evidence docs inspected. | Adapt feature-by-feature. Source's own evidence has rejected/unaccepted cases and no CE integration at that baseline. Keep other-GPU fallback and HUD readability. |
| Martysl1 Reach DLSS | Source delta, config and logs, September8 ZIP. | Alternate UAV depth binding and resize-resource lifecycle are concrete leads. `dlss::ReachAlternateDepthBindEligible` is referenced but its definition is absent from both supplied source sets and current repo; reconstruct from verified requirements or obtain missing dependency before building. |
| Martysl1 subtitles/UI/bloom | Custom ZIP, full report text, status file. | H3/ODST/Reach native subtitles and arbitration, picture controls, menu wrapping, cleanup backoff; REACHVEG99 remains investigation. Author runtime claims are not current-build acceptance. |
| UnsealedWings | `dllCode.zip` recovered August19, source inspected. | Custom-HUD and rescan proposals need strong candidate validation. Do not replace current checked worker join with old unconditional close after1000ms. Async wait and handle cleanup already exist. |
| Gab_dC virtual stock | Source ZIP and geometry tests. | Optional head/shoulder/support blend for players without physical stock, separate from grab acquisition and calibration. Author mainly tested CE; test other titles and degeneracies. |
| Godoy | Edited per-title bug lists, seven pins, calibration suggestions and six logs. | Implement behavior requirements; do not impose personal pitch/yaw values as universal defaults. |
| Maso / LivingFray PR153 | Open PR head5f3cc880ca81b157c663f7ec4e7670c8890fcbdf, description/files/comments. | Transfer ownership/state-restoration concepts only. Original CE engine/D3D9 APIs and offsets are not MCC bindings. Nearest-camera fallback in actual code needs stronger ownership evidence. |
| marcusau2 PR1 | Open gun-calibration proposal. | Check current implementation before adding; design already documented as adopted locally. |
| PurplePeanut | Provolver haptics offer. | Track as optional integration lead; no supplied implementation yet. |

## Validation and release requirements

Every behavioral candidate needs exact source/configuration identity and failure-isolated behavior. Test Steam and Microsoft Store launch/environment paths, both H2 and CE renderers, supported controller families, left/right handedness, stock/custom content and relevant SP/co-op/competitive/Forge modes. A shared lifecycle change additionally needs the target-title headset result and Halo3 regression result. Record host/client, headset, runtime, refresh, repro steps and whether a workaround actually succeeded.

Input validation must cover every MCC layout without changing VR defaults, per-game override persistence, Unbound, interaction-profile changes, menu focus, held-button edges and simultaneous gestures. Rendering validation must cover title transitions, resolution changes, runtime session visibility and optional feature failure without tearing down the camera. Contributor benchmarks and passing unit tests cannot substitute for headset results.

Keep the accepted-build pointer unchanged until explicit user acceptance. Deliver matching build/source ZIPs in chat using `tools/package-candidate.ps1` without `-Install`. Do not install, launch MCC, open a PR or publish this planned release without a new explicit instruction.
