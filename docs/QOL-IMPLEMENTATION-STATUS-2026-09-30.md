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

| Area | Included behavior | Acceptance limit |
| --- | --- | --- |
| Launcher | Requests administrator access through its Windows manifest. | UAC consent is still required; this is not signing or a SmartScreen fix. |
| Distribution | Launcher outside `ModFiles`; separate manual payload archive; matching complete installer/update and source archives. | Manual payload omits the launcher executable. Updater still needs the complete installer archive. |
| Configuration/update | Existing retain-and-extend config, backup/rollback, source verification, Steam/Store detection, base-root installation and deliberate launch/update actions preserved. | Synthetic filesystem testing is not an actual Steam/Store installation test. |
| Preference migration | Missing replacement keys no longer mask legacy stock/smoothing values; missing per-game gun/HUD profiles inherit saved global calibration, including the old HUD alias. | Explicit new keys and per-game overrides remain authoritative. Runtime roundtrip fixtures supplement installer string checks. |
| Virtual stock | Gab_dC PR16 adaptation: Plus and Standard, grab/release aim continuity, lifecycle guards and expanded settings. | Shared aim/ownership changes require title-by-title headset testing and Halo 3 regression. |
| Free two-hand aim | Grip-position geometry with primary Aim orientation/roll, adjustable support influence, persistent support and optional smoothing. | Does not complete per-weapon grip-zone authoring or custom weapon adaptation. |
| Coherent aim | Committed-sample controller/head/grip aim with bounded freshness and title/session identity; config migration preserves the new explicit version-six override. | Not a fix claim for every muzzle-origin, recoil or network trajectory report. |
| Diagnostics | Optional local telemetry recording and analysis tools from Gab, with file operations on workers. | Python is needed for automatic analysis; absent Python must leave the saved recording and VR usable. No upload occurs. |
| Integration review | Timing-sensitive logging from the contribution is reviewed and corrected; installation scripts and repository instructions are not blindly imported. | See integration review for exact corrections/exclusions. |

## Preserved earlier implementations

| Area | Current coverage | Still incomplete/unaccepted |
| --- | --- | --- |
| Subtitles | Localized overlay, gameplay/theatre controls in H2/H3/ODST/Reach/H4; H4 two-line queue. | CE final localized-text handoff unproven; caption duration/visibility reports need current runtime reproduction. Native CE subtitles are not a working VR-overlay claim. |
| Bloom | Per-game final-consumer controls in H3/ODST/Reach, enabled by default. | CE/H2/H4 consumer evidence and general autoexposure control absent. ODST/Reach user reports of inert controls still need runtime confirmation. |
| DLSS | Optional adapters across six titles, including CE/H2 graphics modes, Pancreations integration and martysl1 Reach path. Ordinary-render failure isolation preserved. | No universal FPS or image-quality claim; moving-object motion/ghosting, GPU availability, transitions and reported regressions need measurement. No frame generation/DLSS5-Feeder. |
| Physical crouch | Optional depth/sensitivity/calibration and title-specific camera correction across six titles. | MCC hold-to-crouch required; native toggle mode not replaced. Headset acceptance pending. |
| Vehicles | Six per-game XYZ banks; H2/H3/ODST/Reach/H4 stable model/seat profiles; H2 interpolated marker sampling; independent vehicle smooth-turn preference. | CE persistent vehicle identity unfinished; no proven universal cure for H2 stutter, CE under-chassis view, passenger limits or checkpoint transitions. |
| Mapping | Profile-aware shared defaults, per-game overrides, Automatic/Unbound, dedicated page, held-input rearming and flashlight/support arbitration. | Native aliases remain coupled. Unbound removes the VR source but cannot guarantee that another action sharing the native transport never activates it. |
| Dual wield | Independent H2/H3 weapon/reticle paths and handedness; H2 marker-owner correction. | Dual shake reload and all visual/trajectory cases not accepted. No ordinary dual-wield expansion to other campaigns. |
| Per-gun alignment/hands | Existing per-weapon profiles, separate visual hand offsets and current reload/holster controls retained. | Custom meshes, dual-hand offsets and mirrored authoring still require reproduction/evidence. |
| Reload | Manual magazine interactions, insertion/grab haptics, placement options, independent full-disable/shortened native animation options. | Dual physical reload, shotgun shells/pump and guaranteed custom cock/feed sound event not completed. |
| Muzzle fallback | Saved H2 Original and H4 hide switches retained. | H2 fallback can suppress other local first-person particles; universal flash alignment not claimed. |
| Roomscale | Physical movement debt survives stick locomotion and catches up after native quiet, with existing guards. | Simultaneous body following not completed; CE body following unsupported. |
| Performance | Existing asynchronous wait and checked worker retirement preserved. | Unsealed's weaker cleanup replacement and heuristic HUD discovery not integrated; no measured universal performance gain. |

## Open work and exact blockers

| Group | Remaining items | Required evidence/work |
| --- | --- | --- |
| Campaign/co-op | Firing/physical-melee/vehicle failures; CE host/client tracking, doors, death and crouch; Reach MP/Forge vehicles. | Current exact builds, host/client and mode, both-peer logs/failing stack, stock/custom comparison and headset reproduction. |
| Stability | New ODST random/bridge-explosion crashes; Reach transition/unload; H2 explosion/black/2D; title-hop slowdown. | Catching a first-chance fault is not evidence of a fatal defect. See stability review; no speculative suppression applied. |
| Visual rendering | CE Anniversary lights/bars/white outline, Classic arm mismatch, Anniversary world-anchored arms; H2 lights/AO; Reach foliage/smoke; H4 reticle/letterbox/damage blackouts. | Correct per-title final consumers and exact current-build captures. These are separate reports, not one generic shader bug. |
| Aim/body | Native magnetism/homing, recoil versus dispersion, passenger restrictions, physical contact/impulse deaths, H3 Crow's Nest spin. | Isolated per-title reproductions and native ownership/consumer proof before behavioral changes. |
| Fresh reports | Quest upside-down startup/blackouts, H2 pixelation, Windows foreground selection, Linux login/keyboard. | Runtime/build/environment details and reproducible sequence. Windows admin mode does not establish Linux support. |
| Proposed additions | Gab grip-location capture and per-weapon/rig policies; Provolver integration; relaxed offhand/HUD-reveal gesture; new torso/neck/body-yaw model. | Complete supplied source/protocol or deliberate new design and acceptance. Not represented as shipped PR16 features. |
| CE-specific research | Subtitle overlay, persistent per-vehicle identities, separate VR scope. | Proven title-specific handoffs/identity and runtime validation; no copied foreign offsets. |
| Signing | Windows SmartScreen/signature warnings. | Explicitly deferred previously; requesting admin does not solve signing. |

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
