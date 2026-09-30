# September 30 candidate implementation status

**Unaccepted local candidate. This does not close every community report.**
Implementation and offline tests do not establish all-game headset parity,
campaign co-op safety or improved GPU performance. The accepted runtime remains
Alpha 0.4.2 source `1a9766c`; this candidate starts from Alpha 0.6.0 source
`566e52c` and carries its earlier work forward.

The full inherited row-by-row inventory is
`QOL-IMPLEMENTATION-STATUS-2026-09-23.md` in source and
`PREVIOUS-IMPLEMENTATION-STATUS.md` in the package. Every IN01-IN09, ST01-ST10,
WB01-WB14, RV01-RV11 and GP01-GP16 row remains applicable unless a specific
September 30 change below supersedes it. The new evidence is recorded in
`COMMUNITY-REVIEW-2026-09-30.md`; private DMs/logs are not shipped.

## New implementation

Latest bug-first pass: CE automatic camera recovery now invalidates its old
tracking-reference revision through the existing recenter path. Regression
tests reproduce stale queued/completed work before the edit and pass after;
both graphics modes reject old hand/aim contexts on recovery. The reported
gun misalignment after loads remains unconfirmed in headset, not marked fixed.
See `CE-GUN-LOAD-ALIGNMENT-2026-09-30.md`. Cumulative DLL and seven focused
CE/H3 suites pass. H3 avatar review additionally fixed cross-pair wrist reuse
and interrupted region-write rollback (45 runtime/2867 logic/62 pinned checks).

The user resumed implementation after the first `6b3f38b` package. The following
continuation changes are cumulative; they are not yet a new accepted build.
Evidence: `CONTINUATION-IMPLEMENTATION-2026-09-30.md` and
`NATIVE-VR-ACTION-BRIDGE-2026-09-30.md`.

| Continuation area | Implemented behavior | Remaining limitation |
| --- | --- | --- |
| Linux/login focus | Genuine native focus/activation is restored for shell, unsupported and no-title states; known in-game modes retain keepalive. Text-message routing is unchanged. | Source-backed compatibility correction, not reproduced Proton acceptance. Forced sign-in may involve separate auth/prefix/runtime issues; the referenced old Linux log is unavailable. See LINUX-PROTON-LOGIN-FOCUS-REVIEW-2026-09-30.md. |
| Live resolution | Resize hooks work independently of desktop fitting; explicit retry/settings/title transitions clear failed-size suppression. | Native resize still observes loading deferral, 500 ms slider settling and timeout rollback. Per-title headset acceptance pending. |
| DLSS resource lifetime | A full depth cache retires unused views at frame end so obsolete buffers do not permanently block replacement discovery. | Does not prove the cause of H2 Anniversary's reported failure; eight simultaneously recurring fallback views can still fill the table. |
| DLSS GPU validation | Production NGX wrapper passed 90 evaluations on RTX A4500 across both eyes, five modes, resize, state restoration and reinitialization. | Synthetic GPU inputs do not validate each game's motion/depth quality or performance. |
| Native action isolation | All six titles have their own converter/player-control bridges with independent named actions, Unbound, reload/use separation, physical-input coexistence and guarded lifecycle. | 255 production fixture checks and 172 pinned checks pass. Vehicles, layouts, online behavior and transitions need headset validation. |
| Tracked arms | Separate Show arms with hands control; new H2 single-weapon, Reach support-grip and H4 two-arm transactions preserve working hands/gun on solve failure. | CE/H3/ODST paths retained. Recognized models only; H2 dual-wield arms remain hidden. Headset acceptance pending. |
| CE vehicle profiles | Guarded ordered-model-node identity feeds the existing persistent model/seat adjustment bank; controls cleanup now handles all ten callback ranges. | Identical node-name rigs share profiles. Malformed/unproved models use per-game offsets. Camera placement/stutter still needs headset checks. |
| Body safety | H3's legacy switch now verifies Boolean type, writes bytes, rejects stale generations and contains guarded faults. Experimental IK takes priority and restores the legacy switches while preserving their saved preference. | Full-body visibility remains WIP in every title; the old H3 switch can interfere with FP hands. This is not an all-title body implementation. |
| Experimental full-body IK | H3 exact Chief/Elite/Arbiter world rigs and H4 exact 120-node Chief rig have guarded local avatar transactions, controller-aligned arms, head/body following, bounded native-foot retention and free-hand gestures. H4 uses solved FP palm targets and proportional arm reach. | Full-world avatars in CE/H2/ODST/Reach remain unbound. H3 Elite/Arbiter merged leg/torso geometry cannot support lower-body hiding. No optical finger tracking, contact-reactive fingers, floor-aware foot placement or headset acceptance. |
| Hand-specific haptics | Shared primary/secondary/coupled-support channels honor handedness, tracking, support release, pulse freshness and native-envelope cancellation (603 production checks, including late-writer races). CE, H2, H4 and ODST have their own authored recoil adapters; CE fixture/19 pinned checks, H2 57 production/35 pinned checks, H4 111 production/38 pinned checks and ODST 118 production/47 pinned checks pass. | H3 admitted-shot/charging adapter passes 31 production and 21 pinned checks. The cumulative CE/H2/H3/ODST/H4 DLL builds. Reach runtime bindings remain active research. General damage/vehicle XInput feedback remains shared. All-title headset acceptance is pending. |
| CE roomscale | Classic and Anniversary can use the existing guarded on-foot movement transport with coherent horizontal camera reference. | Does not complete simultaneous physical/stick following; native-motion quiet interval remains. |
| HUD gesture | Opt-in empty support hand near the head reveals hidden HUD after deliberate dwell, with release hysteresis and interaction cancellation. | Free-hand controller finger gestures are now separate from the reveal gesture. Headset usability pending. |
| Physical running | Optional alternating arm-swing locomotion with speed/sensitivity, native on-foot admission and bounded movement-stick substitution. | Core offline transport/motion checks pass at 60–240 Hz. All-title headset validation pending; native sprint is not automatically activated. |
| Quality of Life page | Collapsible sections reuse the original settings and callbacks. | New native physics requests are still being implemented; a mirrored setting does not establish completion. |
| Environment melee | Shared no-handle world-hit contract and title-specific native surface paths retained for CE/H2/H3/ODST/Reach/H4; BSP hits no longer require an object handle. | Native material destructibility, co-op authority and actual glass/world damage still require per-title runtime validation. This cannot make indestructible level geometry breakable. |

| Area | Included behavior | Acceptance limit |
| --- | --- | --- |
| Launcher | Requests administrator access through its Windows manifest. | UAC consent is still required; this is not signing or a SmartScreen fix. |
| Distribution | New installer/updater UI is a separate download. Windows manual archive includes the legacy game-launch/injection helper; Linux archive has the same files directly at ZIP root without an enclosing `Halo_MCC_VR` folder. | Direct MCC launch does not inject the DLL. Legacy helper's Proton/Wine behavior is unverified; flat layout only addresses extraction nesting. Updater still needs the complete installer archive. |
| Configuration/update | Existing retain-and-extend config, backup/rollback, source verification, Steam/Store detection, base-root installation and deliberate launch/update actions preserved. | Synthetic filesystem testing is not an actual Steam/Store installation test. |
| Preference migration | Missing replacement keys no longer mask legacy stock/smoothing values; missing per-game gun/HUD profiles inherit saved global calibration, including the old HUD alias. | Explicit new keys and per-game overrides remain authoritative. Runtime roundtrip fixtures supplement installer string checks. |
| Virtual stock | Gab_dC PR16 adaptation: Plus and Standard, grab/release aim continuity, lifecycle guards and expanded settings. | Shared aim/ownership changes require title-by-title headset testing and Halo 3 regression. |
| Free two-hand aim | Grip-position geometry with primary Aim orientation/roll, adjustable support influence, persistent support and optional smoothing. | Does not complete per-weapon grip-zone authoring or custom weapon adaptation. |
| Coherent aim | Committed-sample controller/head/grip aim with bounded freshness and title/session identity; config migration preserves the new explicit version-six override. | Not a fix claim for every muzzle-origin, recoil or network trajectory report. |
| Diagnostics | Optional local telemetry recording and analysis tools from Gab, with file operations on workers. | Python is needed for automatic analysis; absent Python must leave the saved recording and VR usable. No upload occurs. |
| Integration review | Timing-sensitive logging from the contribution is reviewed and corrected; installation scripts and repository instructions are not blindly imported. | See integration review for exact corrections/exclusions. |

## Individual finger-joint identification

The six-title pose adapters now use explicit rig-scoped joint inventories.
Records identify anatomical hand, proven named finger or unknown native digit
slot, root-to-tip ordinal, palette index and parent. CE/H2 retain full 64-bit
native graph identities; H2 maps source graph joints into the final output
palette. H3/H4 world and FP rig namespaces are kept distinct. Unknown phalanx
names and absent digits are not invented. This is active identification used by
the pose code, not collision reactions or a world-space contact transport.
See `FINGER-JOINT-IDENTITY-2026-09-30.md` for exact providers and per-rig counts.

## Avatar and finger-pose coverage in this continuation

All new posing remains behind `experimental_body_ik` (Tracked full-body IK).
Show arms with hands remains separate. Controller gestures use physical input,
not remapped gameplay actions. Held primary, dual-wield and attached support
hands preserve authored weapon grips. A pose refusal leaves the established
hands/weapon/camera paths available; no accepted pointer changes.

| Title | Runtime avatar work | Free support fingers | Remaining work |
| --- | --- | --- | --- |
| CE Classic / Anniversary | Existing tracked FP hands/arms retained. | Additional flex on own named FP digits; trigger/index and grip/other digits over native animation. No proven open-pose reset. | World rig has no finger joints; hybrid world/FP avatar and Anniversary ownership unproven. |
| Halo 2 | Existing single-weapon tracked arms retained. | Additional grip-driven flex on own FP digit chains over native animation; dual grips protected. No proven open-pose reset. | Exact digit semantics for independent index pose and full-world avatar consumer. |
| Halo 3 | Local Chief/Elite/Arbiter world rigs; headset/head/arm follow, bounded native-foot retention, GPU destination staging and region rollback. | Own inverse-bind reset, independent trigger/index and grip/other digits; actual anatomical handedness. | Elite/Arbiter lower hiding requires separate geometry; custom rigs, co-op and headset results. |
| ODST | Existing own FP arm solve retained. | Own recon/ONI FP profiles, inverse-bind reset, independent index/grip. | Proven full-world palette owner/consumer before avatar writes. |
| Reach | Existing own Spartan/Elite FP arm solve retained. | Own HREK FP profiles and exact prepared input; independent index/grip flex over native animation. Bind reset remains unproven at the runtime seam. | Full-world owner/consumer: geometry subrecords lose unit identity; camera identity alone is insufficient. |
| Halo 4 | Exact Chief world rig; own FP-to-world palm conversion, native-foot retention, proportional reach, head/leg region hiding and packet rollback. | Own world and FP bind resets, independent index/grip, authored held grips. | Headset acceptance, custom rigs, floor-aware feet and natural torso/neck/yaw behavior. |

No row claims optical fingers or Alyx-style object-contact finger reactions.
The requested all-title Alyx-style hand-physics toggle was investigated: current
collision responses constrain whole hands and do not carry labeled per-joint
surface limits. A separate query-only scheduler, fingertip geometry, curl-limit
response, identity/freshness checks and contact hysteresis are required; reusing
melee would risk damage/events or omit resting contact. No placeholder toggle is
shipped. See `PHYSICAL-CONTACT-MELEE-WORK.md` for the exact contracts and tests.
The full-world avatar request remains incomplete in four titles; those games'
FP gestures are a useful partial implementation, not full-avatar parity.

## Preserved earlier implementations

| Area | Current coverage | Still incomplete/unaccepted |
| --- | --- | --- |
| Subtitles | Localized overlay, gameplay/theatre controls in H2/H3/ODST/Reach/H4; H4 two-line queue. | CE final localized-text handoff unproven; caption duration/visibility reports need current runtime reproduction. Native CE subtitles are not a working VR-overlay claim. |
| Bloom | Per-game final-consumer controls in H3/ODST/Reach, enabled by default. | CE/H2/H4 consumer evidence and general autoexposure control absent. ODST/Reach user reports of inert controls still need runtime confirmation. |
| DLSS | Optional adapters across six titles, including CE/H2 graphics modes, Pancreations integration and martysl1 Reach path. Ordinary-render failure isolation preserved. | No universal FPS or image-quality claim; moving-object motion/ghosting, GPU availability, transitions and reported regressions need measurement. No frame generation/DLSS5-Feeder. |
| Physical crouch | Optional depth/sensitivity/calibration and title-specific camera correction across six titles. | MCC hold-to-crouch required; native toggle mode not replaced. Headset acceptance pending. |
| Vehicles | Six per-game XYZ banks and model/seat profiles, including new CE guarded identity; H2 interpolated marker sampling; independent vehicle smooth-turn preference. | No proven universal cure for H2 stutter, CE under-chassis view, passenger limits or checkpoint transitions. |
| Mapping | Profile-aware shared defaults, per-game overrides, Automatic/Unbound, dedicated page, held-input rearming and flashlight/support arbitration; six-title native bridges above. | A bridge that fails its title-specific proof retains native-layout fallback and logs it. Native hardware or other mods are not disabled by a VR Unbound setting. |
| Dual wield | Independent H2/H3 weapon/reticle paths and handedness; H2 marker-owner correction. | Dual shake reload and all visual/trajectory cases not accepted. No ordinary dual-wield expansion to other campaigns. |
| Per-gun alignment/hands | Existing per-weapon profiles, separate visual hand offsets and current reload/holster controls retained. | Custom meshes, dual-hand offsets and mirrored authoring still require reproduction/evidence. |
| Reload | Manual magazine interactions, insertion/grab haptics, placement options, independent full-disable/shortened native animation options. | Dual physical reload, shotgun shells/pump and guaranteed custom cock/feed sound event not completed. |
| Muzzle fallback | Saved H2 Original and H4 hide switches retained. | H2 fallback can suppress other local first-person particles; universal flash alignment not claimed. |
| Roomscale | Physical movement debt survives stick locomotion and catches up after native quiet, with existing guards; CE's guarded adapter now participates in both graphics modes. | Simultaneous body following remains incomplete; new CE coverage is unaccepted. |
| Performance | Existing asynchronous wait and checked worker retirement preserved. | Unsealed's weaker cleanup replacement and heuristic HUD discovery not integrated; no measured universal performance gain. |

## Open work and exact blockers

| Group | Remaining items | Required evidence/work |
| --- | --- | --- |
| Campaign/co-op | Firing/physical-melee/vehicle failures; CE host/client tracking, doors, death and crouch; Reach MP/Forge vehicles. | Current exact builds, host/client and mode, both-peer logs/failing stack, stock/custom comparison and headset reproduction. |
| Stability | New ODST random/bridge-explosion crashes; Reach transition/unload; H2 explosion/black/2D; title-hop slowdown. | Catching a first-chance fault is not evidence of a fatal defect. See stability review; no speculative suppression applied. |
| Visual rendering | CE Anniversary lights/bars/white outline, Classic arm mismatch, Anniversary world-anchored arms; H2 lights/AO; Reach foliage/smoke; H4 reticle/letterbox/damage blackouts. | Correct per-title final consumers and exact current-build captures. These are separate reports, not one generic shader bug. |
| Aim/body | Native magnetism/homing, recoil versus dispersion, passenger restrictions, physical contact/impulse deaths, H3 Crow's Nest spin. | Isolated per-title reproductions and native ownership/consumer proof before behavioral changes. |
| Fresh reports | Quest upside-down startup/blackouts, H2 pixelation, Windows foreground selection, Linux login/keyboard. | Runtime/build/environment details and reproducible sequence. Windows admin mode does not establish Linux support. |
| Proposed additions | Gab grip-location capture and per-weapon/rig policies; Provolver integration; relaxed offhand pose; new torso/neck/body-yaw model. | Complete supplied source/protocol or deliberate new design and acceptance. Not represented as shipped PR16 features. The HUD-reveal gesture is implemented separately above. |
| CE-specific research | Subtitle overlay and separate VR scope; validation of new model/seat profiles. | Proven title-specific subtitle/scope handoffs and runtime validation; no copied foreign offsets. |
| Signing | Windows SmartScreen/signature warnings. | Explicitly deferred previously; requesting admin does not solve signing. |

## Finished, WIP, planned, and not yet implemented

"Implemented" below means present in this candidate's source and subject to the
recorded offline checks. It does not mean headset accepted across six games.
The accepted baseline remains Alpha 0.4.2. The detailed tables above and the
included previous ledger preserve every earlier report and coverage limit.

| Status | Work |
| --- | --- |
| Implemented in this continuation | Guarded H3/H4 world-avatar paths; H3 legacy-switch precedence; H4 own head/palm alignment, bounded arm reach, native-foot retention, packet rollback; per-title controller finger posing with the exact limits above; six-title individual joint inventories; six-title native action bridges; CE reference retirement and vehicle identity; CE roomscale adapter; physical running; HUD-reveal gesture; live-resize/depth-cache corrections; launcher-matched menu and QOL organization; native shell/login focus correction. |
| Previously implemented and retained | Administrator-requesting Windows installer, separate launcher/manual payload, both-edition detection, settings retention/update checks; virtual stock and two-hand refinements; per-game/weapon calibration; supported subtitle/bloom/DLSS controls; physical crouch, vehicles, dual H2/H3 reticles, muzzle-hide fallback, reload/holsters, world collision and physical melee, telemetry and existing performance/lifecycle protections. |
| Implemented but awaiting runtime verification | Every new avatar and joint/pose path; body/hand alignment in both handedness modes; feet/roomscale/crouch and weapon transitions; native mapping isolation in vehicles/co-op; all-title environment melee/material effects; haptic routing; live resolution; DLSS image quality/performance; Steam and Store lifecycle regression; current installer on actual machines. Offline checks cannot close these items. |
| WIP / partial implementation | CE/H2/ODST/Reach full-world avatars; H3 Elite/Arbiter lower hiding; CE/H2/Reach open-bind finger reset (additive flex only); H2 semantic finger names and dual articulated arms; simultaneous stick/roomscale following; CE VR subtitle/scope handoffs; CE/H2/H4 bloom/autoexposure; native Reach weapon haptics (draft remains inactive); dual/shotgun/cocking reload specifics; per-weapon authored support/magazine zones; custom-rig/model coverage. |
| Investigated, no confirmed universal fix | CE gun shift after loads; Linux forced Xbox sign-in/keyboard; ODST random/explosion crashes; reported campaign co-op firing/melee/vehicles; H2 stutter/blackouts/AO; CE Anniversary lights/arm mismatch; Reach foliage/smoke and title-hop slowdown; H4 blackouts/letterbox. Concrete changes are listed individually and do not close unrelated reports. |
| Planned, functional runtime not started | Alyx-style per-finger surface reactions/hand physics (joint identity is now implemented); native world-weapon pickup and enemy disarm; grabbing/ragdolling/throwing enemies; sustained physical enemy/body contact; world gripping/climbing and wall jumps; mandatory physical weapon grip with real drop/pickup; touch-driven world interactibles; optical hand tracking. |
| Proposed or deferred, not integrated | Provolver SDK support; Gab per-weapon grip-capture workflows; advanced torso/neck/yaw and floor-aware foot placement; generalized per-material finger-contact volumes; code signing/SmartScreen resolution; rejected weaker cleanup and heuristic-HUD substitutions from outside proposals. No frame-generation integration. |

Already existing manual reload/magazine and collision interactions are not a
claim that the unstarted native physical interactions above are complete. A
future contact-reactive finger option must be separate from ordinary gestures
and must work without firing melee or damage events.

## Contributor credit and provenance

- **Pancreations:** original MCCVR and optional DLSS foundation; older `3f86346`
  archives preserved and compared rather than replacing current source.
- **Gab_dC / gabrieldch:** virtual stock, free two-hand handling, persistent
  support, coherent aim and local telemetry in PR16; original commit provenance
  remains in the integration review and source history.
- **martysl1:** subtitle/UI/bloom proposals and source, Reach alternate DLSS
  depth path; WIP vegetation is not claimed as a finished fix.
- **UnsealedWings:** performance/cleanup/custom-HUD proposals reviewed;
  existing stronger cleanup retained and unsupported claims kept separate.
- **Godoy:** detailed per-title tests, calibration and interaction proposals.
- **LivingFray / Maso:** PR153 ownership/restoration design leads; original
  CE/D3D9 bindings are not MCC bindings and were not copied.
- **marcusau2:** earlier per-gun calibration contribution already adopted.
- **PurplePeanut:** Provolver integration proposal; no completed SDK adapter
  supplied in reviewed material.
- **Community reporters:** skivy44, Vixel, wwm0nkey, Chronomize, Count Scaphandre,
  The_King_of_Normies, Totally not peakhorse, PC-Genie, and the other reporters
  credited in the September 23 audit. Reports/workarounds are credited as such,
  without pretending their authors supplied a proven source fix.

No installation, game launch or publication is part of preparing this package.
Build, test and artifact verification results accompany the delivered candidate;
they do not erase the open rows above.

Upgrade caveat: the already-distributed 0.6.0 updater expects a launcher inside
the payload. Users of that version need to run the complete new installer once
to adopt the separated layout. A private candidate does not update the public
GitHub latest-release endpoint. Legacy stock strength and smoothing settings
must be allowed to migrate rather than being masked by appended new defaults.
