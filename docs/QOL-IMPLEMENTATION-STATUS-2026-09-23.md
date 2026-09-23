# September 23 QoL implementation ledger

Final candidate inventory; packaging requires passing combined validation. Every row
from `QOL-IMPLEMENTATION-BACKLOG-2026-09-23.md` is retained here. **Implemented**
means source plus offline checks, not headset acceptance. **Partial** means the
specified requirement is not fully met. **Open** means no supported fix is being
claimed. Previously accepted behavior is preserved; `CURRENT-STATE.md` stays at
the accepted 1a9766c baseline. This ledger must accompany the test ZIP.

## Input and settings

| ID | Disposition and remaining work |
| --- | --- |
| IN01 | Implemented advertised OpenXR profile detection, common action defaults, explicit unknown fallback labels, and Vive/WMR pad face zones. Hardware-family testing remains required; simple controllers have fewer physical controls. |
| IN02 | **Partial:** six verified native binding readers remove the hard-coded Reach LT/X exchange and follow native layout changes. Native shared button/action aliases remain coupled; there is no fully independent native action override. Inversion and every native layout are not headset accepted. |
| IN03 | Implemented per-game overrides, Automatic, Unbound, reset, persistence and held-input rearming. **Partial independence:** Unbound removes that action's own VR source; another action sharing a native transport can still activate it. |
| IN04 | Dedicated mappings page replaces scattered manual melee/reload transport choices; controller/profile and source labels added. Legacy transport keys remain readable for legacy routing; automatic mode resolves the actual native binding. |
| IN05 | Menu-held inputs drain; mapped zoom handles D-pad routes and cancels on profile/rebind/menu boundaries; completed holster grip no longer blocks support acquisition. Existing magazine ownership retained. Native reload/use aliases still need deeper native action separation. |
| IN06 | Separate flashlight suppression during two-hand support; current-frame grab/release admission and held-source drain preserve grip tracking. Head gesture radius remains adjustable. Native transport aliases can still couple flashlight to another action. |
| IN07 | Existing native-menu linear stick mapping avoids boosting small off-axis input; F1 already uses pointer/scroll. Slider arrows retained, long labels wrap. No confirmed universal cure for the community navigation report; reproduce with current build/menu identity before another input rewrite. |
| IN08 | VR Mappings, Reload & Holsters and Subtitles added; weapon aiming, vehicles, hands/body, picture and diagnostics retain dedicated groups. Advanced stock and magazine-placement controls use collapsible sections. |
| IN09 | Picture page distinguishes requested/live/planned dimensions and resize status; subtitle support is stated beside controls. Existing CE live-multiplayer pause/settings retention preserved. Vehicle reentry and per-title seat-bank limits remain explicit. |

## Stability and multiplayer

| ID | Disposition and remaining work |
| --- | --- |
| ST01 | **Open root cause:** optional-features-off co-op firing failure remains unproven. Existing observe-only native fault logs and H3 firing counters retained. Historical fault addresses were matched to exact build objects; guarded first-chance reads are not called fatal crashes. Needs two-peer failing-stack evidence. |
| ST02 | Existing local ownership, target leases, contact guards and H3 native exception propagation retained and regression tested. **Open** claim that physical melee/dual wield is co-op safe in every reported reproduction; competitive success does not establish campaign lockstep safety. |
| ST03 | H3/ODST seat flag leases now revalidate full live tag/map/storage identity and avoid restoring into replaced storage. CE/H2/H4 retirement no longer demands an unwind entry for a leaf helper. These correct concrete lifetime defects, **not** proof the reported co-op black/menu failure is resolved. LivingFray ownership concepts informed review; no foreign offsets copied. |
| ST04 | Existing CE outgoing native action/aim/locomotion adapter preserved. It handles network grenade direction and native authority/prediction consistently, but host/client doors, death, crouch and teammate-kill regressions still require a two-peer run. |
| ST05 | H4 missing TLS pause state is guarded; seat generation/storage retirement corrections above apply. Existing title lifecycle tests retained. Every checkpoint/mission/respawn/title-hop crash is not assigned one generic fix. |
| ST06 | **Open:** Reach MP/Forge vehicle admission and overheat/death stereo loss require current exact-mode reproduction. Campaign vehicle success is not Forge acceptance. |
| ST07 | H2 marker-owner correction is separately evidenced. Repeated halo2+67BA50 is a null native free-path read without the original native caller in supplied logs; subsequent logging continues. **Open** explosion/black/2D/crash attribution. |
| ST08 | skivy44 ODST log is an older already-corrected zero-offset readiness gate. Reach allocation worker now reports exact HRESULT/eye/descriptor/stage/device removal, separating that report; actual allocation failure is not yet proven fixed. |
| ST09 | Bounded native model/marker guards and generic reload fallback preserved. No blanket custom-map compatibility claim; missing CE Multiplayer dependency is documented separately from actual unsupported custom geometry. |
| ST10 | Retirement correction removes one proven avoidable pin. Old Unsealed cleanup is weaker than current checked lifecycle and was not substituted. **Open** measured attribution of long-session/title-hop slowdown. |

## Weapons, hands and body

| ID | Disposition and remaining work |
| --- | --- |
| WB01 | H2 now accepts the proven local parent unit as a marker owner while preserving exact firing weapon/slot/receipt. Distinct primary/secondary origins tested. H3 existing per-hand paths retained. H2 direction-only mode still retains native origin; the reported barrel-disabled case is not claimed fixed. |
| WB02 | Existing per-title authored-barrel and controller paths preserved; H2 owner correction improves valid barrel admission. Per-gun configuration regression covers title switches, multiple weapons, H2 graphics modes and unknown identities. Per-weapon visual/impact acceptance still required. |
| WB03 | Existing rigid controller/barrel receipt logic retained; native dispersion remains. **Open** attribution of rapid-fire recoil/animation reports where no current comparison proves which transform diverges. |
| WB04 | Separate H2 Classic alignment and saved per-gun profiles retained/tested. Godoy's personal offsets are not universal defaults. No claim that every controller pivot is headset tuned. |
| WB05 | Existing CE native targeting and homing adapters retained. **Open** comparative red-reticle/magnetism/Needler runtime behavior with skulls and custom content controlled. |
| WB06 | Completed holster-draw primary grip no longer blocks support grab. Actual gesture regression tests added. Existing barrel-line/palm/zone adjustments retained; optional virtual stock remains separate from grab acquisition. |
| WB07 | Separate visible left/right hand offsets and title/handedness paths retained; zero-offset/left-hand fixtures remain. All reported dual hand/mesh positions still require headset visual confirmation. |
| WB08 | Existing verified model identities and generic unknown reload item retained. **Open** custom sword/Mythic/Light Rifle/Cursed-specific marker or bone adaptations; no copied cross-title bindings. |
| WB09 | Existing native contact/gesture melee paths retained. **Open** eligible door/glass/small-target/body-obstruction refinements without a proven per-title contact route for the reported cases. |
| WB10 | **Open:** spinning ammo-crate deaths need native damage/physics attribution; no guessed impulse suppression. |
| WB11 | Retained physical movement debt now survives stick movement and catches up after 150 ms of observed native quiet, with teleport/recenter/tracking-loss guards and production transport coverage. This is deferred catch-up, not simultaneous body following. CE body following remains unsupported. |
| WB12 | Optional physical crouch with adjustable depth/calibration and hysteresis is implemented, off by default. Read-only per-title native camera correction prevents the native crouch blend from being added a second time to tracked head lowering; H3/ODST/H2/CE use their own height evaluators and Reach/H4 use their own smoothed cached-height paths. Requires MCC hold-to-crouch; native toggle mode is not overridden. Headset acceptance remains required. |
| WB13 | **Open:** Crow's Nest involuntary spin remains a distinct reproduction, not labeled a cinematic-camera fix. |
| WB14 | **Open:** relaxed offhand pose and hand-near-head HUD reveal are not implemented. Existing head D-pad/flashlight/grip arbitration must be respected by a future gesture. |

## Reload, holsters, zoom and vehicles

| ID | Disposition and remaining work |
| --- | --- |
| RV01 | Physical reload/swap now resolve verified native action transport. Existing insertion targets, unknown model fallback and shortened/full animation options retained. Native aliases and unsupported model visuals remain limits. |
| RV02 | Existing single-weapon Needler shake preserved/tested. **Open** dual-gun shake reload; the current interaction state deliberately declines dual-wield physical reload rather than injecting into an unproven hand. |
| RV03 | Added independent reserve-magazine XYZ adjustments and support-side front-hip/behind-shoulder locations; handedness, identity-change cancellation and finite bounds tested. Existing insertion/grab haptics retained. Final authored cock/feed audio is native, not a new guaranteed audio event. |
| RV04 | Per-game Unbound permits removing button reload/swap while leaving physical gesture requests. **Partial** because native Use/Reload aliases can still overlap; no fully independent pickup guarantee. |
| RV05 | Existing circular per-frame zoom H2/H3/ODST/Reach/H4 retained, with corrected mapped gesture handling. CE separate lens remains unfinished/previously withdrawn; native CE zoom remains. Weapon eligibility refinement is not complete. |
| RV06 | **Open** proof of native accuracy/magnification coupling for every universal VR scope; visual zoom alone does not establish native accuracy changes. |
| RV07 | Existing CE local seated head-marker FP camera and live MP pause correction are retained. Six per-game vehicle offset defaults now include CE. **Partial:** CE does not yet have a stable persistent per-vehicle identity reader, so CE uses its per-game fallback; under-chassis reproduction and offsets need headset testing. |
| RV08 | H2 native steering corrections are retained. First-person marker sampling now selects the native interpolated node bank when vehicle smoothing is enabled, correcting one proven tick-rate/visible-vehicle mismatch. Stable H2 model/seat profiles and per-game fallback offsets are persisted. **Unverified:** complete removal of Warthog/Ghost judder and both renderer modes require headset testing. |
| RV09 | Existing separate smooth-in-vehicle preference, native seat limits and normal turn restoration retained/tested. Menu/theatre/seat transition positioning still needs runtime checks. |
| RV10 | **Open** per-seat passenger rear/left restrictions and offsets beyond verified current adapters; legitimate native seat limits remain. |
| RV11 | Corrected H3/ODST storage identity leases and CE/H2/H4 hook retirement. All six have saved per-game vehicle XYZ fallback offsets. H2/H4 now have stable model/seat profiles alongside existing H3/ODST/Reach seat banks; unknown identities fall back to per-game values. CE persistent per-vehicle identity remains WIP. Checkpoint/reentry and all seat transforms remain headset acceptance items. |

## Rendering, UI and performance

| ID | Disposition and remaining work |
| --- | --- |
| GP01 | **Open:** CE Anniversary light pillars, head-attached tower beams and digital-glass/bridge issues are not fixed by asserting the existing default-off flare workaround works. Reports that the workaround failed remain recorded. |
| GP02 | **Open:** CE Anniversary one-eye cinematic bars, white outlines and close-up convergence; the retracted apparent fix stays retracted. |
| GP03 | **Open:** CE Classic unsupported-arm eye mismatch and transient loading HUD require separate reproduction. Existing doubled-HUD parallel-reprojection resolution is a different case. |
| GP04 | **Open:** H2 Anniversary attached lights/vehicle flicker and Classic AO dithering have no supported new fix here. |
| GP05 | **Open:** Reach vegetation/gray filter/aliasing; donor REACHVEG99 is explicitly WIP, not silently promoted to production. |
| GP06 | Per-game bloom controls are implemented for H3/ODST/Reach at the verified final shader consumer. Bounded resource ownership, backbuffer exclusion, state restoration and WARP tests are included. Bloom remains enabled by default; CE/H2/H4 retain native bloom because no verified equivalent consumer is claimed. |
| GP07 | Native localized subtitle overlay and gameplay/theatre placement controls are implemented for H2/H3/ODST/Reach/H4 using exact title callers and the shared native interface. H4's two-line queue is bounded and aggregated. **WIP:** CE's localized-text emission handoff remains unproven; CE retains native subtitles and its placement controls stay hidden. |
| GP08 | Wider wrapped settings panel, subtitle page, separated mappings/reload groups, collapsible advanced options and long slider-label wrapping implemented. Launcher typography/layout separately rendered and checked. Headset readability remains unaccepted. |
| GP09 | Existing validated HUD ownership retained. Unsealed's broad float ranges are insufficient identity proof; no unsafe custom-HUD rescan admitted. Custom discovery remains open. |
| GP10 | Two independent crosshairs are implemented for H2 Classic/Anniversary and H3 using separate weapon poses, handedness and single-glyph compositor images. H2 hide behavior and radar/player-dot alignment remain separate headset checks. |
| GP11 | Older H4 reports matched to current source; per-game muzzle-flash fallback now controls all existing suppression routes coherently. This does not fix every letterbox/recenter/damage/vehicle blackout report. |
| GP12 | Pancreations DLSS is adapted with the pinned SDK/runtime, title-owned depth/camera receipts, independent ordinary-render fallback and resize lifecycle. Marty's Reach alternate UAV path is reconstructed. CE Classic uses its verified raster frustum/depth receipt and CE Anniversary its own Saber camera/depth path, completing offline integration across all six titles and both CE/H2 graphics modes. Image quality, GPU support and headset acceptance remain pending. |
| GP13 | Existing async frame wait and checked worker cleanup retained; old unconditional close-after-timeout rejected. Adaptive cleanup contribution still needs measured acceptance, no universal FPS claim. |
| GP14 | Live/planned/output dimensions and failure/restart state now visible; live resize manages retained resources and rollback. Direction-dependent map performance remains unprofiled; DLSS availability is not evidence of solving that report. |
| GP15 | Resolved cases kept in audit: skybox distance, CE reprojection doubled HUD, native inversion/reset, removed Toggle HUD wrapper, corrected Afterburner attribution. None presented as a newly implemented code fix. |
| GP16 | **Open:** Samurai missing-geometry/black-void clip lacks confirmed title/build/reproduction attribution; no invented root cause. |

## Contributions and added delivery requirements

- **Pancreations:** source and matching artifacts retained and inspected; optional
  DLSS integration separately validated. No contributor binary executed.
- **martysl1:** subtitles, wrapping, bloom, cleanup and Reach DLSS delta reviewed;
  missing helper reconstructed, WIP vegetation and unsafe broad edits excluded.
- **Godoy:** per-title reports and calibration leads remain individual rows;
  personal pitch/yaw values are not imposed globally.
- **UnsealedWings:** source reviewed; weaker cleanup and heuristic-only HUD
  discovery not transplanted. Existing async wait/cleanup is preserved.
- **Gab_dC:** optional virtual stock with head/shoulder/chest/adaptive references,
  strength and proximity release; original solver preserved when off. Separate
  experimental OpenXR grip-pose endpoint excluded explicitly.
- **LivingFray PR153 / Maso:** state ownership/restoration concepts used in review;
  original CE/D3D9 APIs, nearest-camera fallback and offsets are not MCC evidence.
- **marcusau2 PR1:** per-gun calibration was already adopted locally; existing
  configuration and weapon-switch regression tests cover it.
- **PurplePeanut Provolver:** offer retained as an integration lead; no supplied
  implementation or haptic device protocol is available to integrate.
- **Installer/launcher:** Steam/Store detection, browse, complete ModFiles manual
  payload, verified backups/rollback, cfg retention plus new keys, deliberate
  launch, GitHub update discovery/download/install and source identity display.
  Synthetic install/rollback/malicious archive tests pass; no actual MCC install
  or launch performed. OFL Oxanium supplies the stylized heading/body typography.
- **Muzzle hide:** separate saved H2 Original/H4 switches preserve the existing
  hide defaults. H2 suppresses the local first-person particle pass, which can
  also hide other particles. Other games have no newly claimed hide path.
- **Physical crouch, roomscale and all-title DLSS:** latest explicit additions
  are carried in WB11/WB12/GP12 and the active checkpoint; none may be omitted
  from final release notes or called universally accepted by offline tests.

No publication, PR, game launch, installed-file replacement or accepted-pointer
update is authorized by this preparation task. Deliver matching test build and
source ZIPs with this ledger; retain unresolved reproduction/evidence work.
