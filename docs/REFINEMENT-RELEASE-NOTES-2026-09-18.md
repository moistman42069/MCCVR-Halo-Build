# September 19 co-op diagnostic and CE menu candidate

## Changes in this follow-up

- Corrected H3's collision wrapper swallowing native exceptions as collision
  misses. Native calls now retain their own exception behavior; failures in
  the mod's extra contact query isolate only that optional feature.
- Added observe-only hardware-fault evidence for every title in
  `HaloMCCVR-native-faults.log`, plus H3 native firing completion counters.
  Keep this file alongside `HaloMCCVR.log` when reporting the next failure.
- CE controller Menu/Start can request the in-match pause/settings screen
  while multiplayer simulation continues. Native input suppression retains
  an explicitly requested menu; resuming input returns to stereo. Native
  single-player pause handling remains supported.

**Not resolved:** the reported co-op firing failure's root cause is still
unproven. Both supplied logs continue through level retirement and contain no
fault stack. This is a diagnostic candidate, not a confirmed co-op crash fix.
CE multiplayer grenade tracking is also unfinished: its client update bypasses
the existing campaign adapter, and a proper fix must change the outgoing action
consistently with host simulation and local movement. No speculative network
patch is included. Menu visibility still needs a headset test.

Release build, all 61 test suites, pinned firing/contact/world bindings and
Reach consistency checks are validated locally. No game launch, installation,
publication or accepted-build pointer change. Both editions remain supported.
Detailed evidence is in `COOP-FIRING-AUDIT-2026-09-19.md` and
`CE-MULTIPLAYER-AUDIT-2026-09-19.md` in the matching source ZIP.

All inherited additions and outstanding refinements below remain recorded.

# Previous cumulative vehicle-camera additions

## First-person vehicles follow-up

Added CE (Classic/Anniversary), Halo 2 (Classic/Anniversary) and Halo 4 adapters
to the existing **Sit in the seat (first person)** toggle. These select the
native first-person camera and position it at the local seated character's
verified head marker. Universal position sliders apply; per-seat preset banks
remain H3/ODST/Reach only. Toggle-off returns native camera selection. Native
entry/exit transitions and special cameras remain intact. Missing bindings or
head markers leave this optional feature native, with worker diagnostics.

CE steering and reticle guards now accept a first-person occupied seat. Existing
Halo 3, ODST and Reach camera paths and all preceding fixes are preserved.
The new views need headset testing, including body/weapon visibility, cockpit
clipping and animated-head comfort. The new titles do not yet have separately
ported authored anchors, body-hide or personal-weapon overrides. This is a
camera implementation candidate, not confirmation that every vehicle is finished.
See NATIVE-VEHICLE-FIRST-PERSON-2026-09-19.md in the matching source ZIP.

## Menu cursor, shortened reload flashlight and held-magazine follow-up

Menu fix from candidate5ba2c6d: while the in-game menu cursor is
active, A passes through from VR controllers and physical XInput to select the
pointed item. Other inputs remain suppressed. Thumbrest D-pad/F1 exclusivity
is unchanged. A held while leaving the menu drains until release so it cannot
become a gameplay action. Existing pointer-trigger clicking is retained.
Regression checks cover A-only passage and held-input draining; packaging reruns
the full build/test/gate.

Shortened reload now retains the final quarter of the selected native animation
when its insertion marker is absent, at an endpoint, or its native insertion
countdown covers the full reload. This correction covers CE, H2, H3, ODST, Reach
and H4; usable interior markers still retain their authored tail. Animation
frames and gameplay ticks are scaled separately. CE tag-bank resolution also
accepts valid signed virtual bases. Full-disable remains separate and takes
precedence. Native ammunition transfer and ownership guards remain in place.

The supplied 5ba2c6d Steam/SteamVR log shows CE (3) and Reach (4) shortened-reload
attempts all fell back, without access faults. It does not identify which guard
rejected each attempt. The confirmed missing-marker rejection is corrected;
new rejection-stage logging distinguishes any remaining cause. All-title native
fixtures and unique binding checks verify this candidate locally. Actual final
motion, including whether each weapon visibly cocks, still needs headset testing;
the fallback selects a final section, not a guaranteed authored cocking marker.

Flashlight fix: the previous default blocked RB, but support grip emits LB.
The corrected default blocks that grip's button output without changing tracking
or two-hand aiming. Unversioned default RB selections migrate to support grip;
other custom selections are preserved. After saving, explicit RB selections also
persist. Quest labels follow handedness and Reach's on-foot trigger/X swap.
This uses the mod's known mapping; it does not auto-read custom MCC layouts.
Menus remain usable and held blocked buttons still drain on release.

Held-magazine fix: unrelated trigger/button input and the old four-second timeout
no longer drop a magazine while grip remains held. An active claimed grip also
prevents thumb-rest D-pad takeover. Release/insertion still respects native input
and safety checks; actual weapon changes, tracking loss and menu transitions
cancel as before. The second 5ba2c6d log shows Halo2 BR/SMG, five grabs and one
reload request, but does not identify the precise cancellation cause. These
verified cancellation paths are corrected; headset confirmation remains needed.

One build supports Steam and Microsoft Store and all six Halo titles. Based on
accepted Alpha 0.4.2 source `1a9766ca971a9e5f09b942d508abecd9753bdb35`.
This is a test candidate, not headset-accepted or completion of the entire
standing list. Nothing was installed, MCC was not launched, and the accepted
pointer is unchanged.

## Implemented in this candidate

- Circular 1024x1024 gun-side zoom lens: H3, ODST, Reach, H4 and H2 Classic/
  Anniversary. R3 release toggles it; right-stick up/down changes magnification.
  Main views remain wide; placement follows handedness. The lens refreshes every
  rendered frame rather than alternating expensive/cheap frames. The old refresh
  divisor is readable but ignored. Actual headset clarity/frame rate remain
  untested, and the extra world render costs GPU time. CE retains native zoom.
- **F1 > Picture > Disable CE Anniversary lens flares**, default **off**.
  `ce_anniversary_disable_lens_flares` suppresses the verified native flare draws
  only in owned Anniversary VR eyes. Both eyes use the same frozen setting.
  Turning it off restores existing flare behavior. World lighting, CE Classic
  and other titles remain unchanged. This is a workaround to test against the
  white/cyan streak, not a confirmed diagnosis or general lighting fix.
- Separate default-off **Shortened reload animation**, all six titles. Manual
  insertion retains the native chambering tail and ammunition rules. Existing
  full animation disable is preserved and takes priority. Missing native
  animation evidence keeps the full reload.
- Authored textures for all 42 extracted visible magazine models; orange reload
  token for known H4 Promethean zero-geometry models.
- Magazine insertion/release haptic alongside grab feedback; per-weapon insertion
  targets derived from the actual visible model and current pose.
- Optional per-gun alignment saving/automatic selection; separate left/right
  visible-hand position sliders. Zero offsets preserve native bytes; weapons,
  shots and interaction coordinates remain independent of visual-hand offsets.
- Default-off independent dual aim for **H2 and H3**, including per-hand native
  target acquisition and later homing consumers. No ordinary dual-wield expansion
  is enabled in ODST, Reach or H4.
- Default-off authored barrel origin/direction option in all six titles using
  each title's verified muzzle data and native shot/target handling.
- Roomscale stopping-tail/reference-history corrections for H2/H3/ODST/Reach/H4.
  Movement uses native collision-constrained walking. CE body following stays off.
- Vehicle input audit and targeted H2/CE/H4 corrections: coherent H2 steering/view
  references, unrotated seated throttle, CE seated turn/camera follow and H4
  seat-aware input. Existing good title behavior retained.
- Optional automatic smooth turn in vehicles preserves saved on-foot snap.
  H2 retains continuous native seated controls.
- Optional flashlight-input disable preserving two-hand grip.
- Reach wind-state replay prevents the second eye advancing the same foliage
  wind state again; other foliage mismatch causes remain unconfirmed.
- ODST camera-admission correction for the log-21 right-stick-aim report;
  analogous title paths audited. Headset confirmation remains due.
- H2 full-salt ownership, no replay of faulted native aim calculations and safe
  observer retirement; ODST checkpoint seat-storage lifetime correction.
- CE continuous controller-directed native targeting preserving native range,
  team, visibility, homing and reticle rules.
- Radial walking deadzone preserves direction. Thumbrest D-pad and successfully
  delivered menu-pointer modes consume conflicting XR/gamepad/melee inputs and
  drain held controls before normal input resumes.
- HUD-hide option covers all seven D3D11 draw variants; native callbacks and
  menus remain available.

Earlier all-title manual VR recovery, anatomical handedness, separate contact/
physical-melee/gesture controls, melee speeds and slider arrows are preserved.
Recovery was already expanded beyond H3 in the September 15 work.

## Investigated or partial, still unfinished

- **CE separate circular zoom lens, both graphics modes.** Anniversary's third
  primary view reuses/clears left-eye depth before the left scene draws. Pinned
  native selector/binder/clear emulation confirms this. Replaying the whole
  frame also consumes worker-completion state twice. Neither route is enabled.
  Independent scope rendering/resources remain to implement; Classic's separate
  adapter is also not implemented.
- **CE Anniversary streak/lighting cause.** Video shows the vertical streak, but
  the log has no synchronized effect attribution. New toggle effectiveness needs
  the user's comparison; broader lighting is not declared fixed.
- **H2 Cairo firing / Outskirts post-cutscene crash and vehicle checkpoint crash.**
  Ownership/lifetime defects were corrected, but supplied logs do not establish
  these reported exceptions' causes. Co-op compatibility remains unconfirmed.
- **Reach grass/broader foliage.** Wind advancement corrected; other causes and
  version/headset-specific reports remain open.
- **CE reticle/targeting appearance.** Brief red flash and in-headset homing/color
  behavior remain unconfirmed despite continuous-acquisition implementation.
- **Contact/melee:** sustained/sliding jitter, sticking, unarmed/secondary damage,
  ODST Mythic SMG, damageable world objects, custom weapons and other-headset
  coverage need further work/testing. Runtime model bounds are already present.
- **Alignment/handedness:** stock calibration, dual-weapon profile behavior and
  complete interaction/haptic acceptance remain to verify. New offsets and
  hidden-anchor checks pass locally.
- **Transitions/recovery:** all-title recovery exists; exhaustive mission,
  multiplayer, title-switch, custom-map and long-session regression tests remain.
  Zoom timing/clarity, handedness and Halo 3 headset regression are unverified.

## Not implemented in this continuation / retained queue

- H2 Classic/Anniversary and H4 first-person vehicle presentation/seat expansion,
  beyond the input and reference corrections above.
- H3/H2 Anniversary lower-edge/corner visibility when looking up, preserving H2
  Classic visibility.
- Enabling/validating CE roomscale body following.
- Remaining version-specific Reach graphics-setting/image-quality/body/contact
  reports not closed by the specific changes above.

Explicitly deferred: H4 damage blackout, minor H2 tank-exit reticle report and
independent head-following body yaw. Launcher signing/SmartScreen warnings are
excluded. Non-native ordinary dual-wield expansion was removed from scope by the
user. No deferred request is marked complete.

## Validation and test priorities

Packaging runs Release, all 58 CTest suites and the Reach consistency gate.
Production scope fixtures: ODST257 checks, Reach899, H43478, H2both226, including
fault cleanup and state restoration. CE native projection emulation:384 cases;
depth-routing emulation includes the rejected third-view case. These local checks
are not live gameplay or headset acceptance.

First compare the CE streak with Picture's flare toggle off/on. Then test R3 zoom
in implemented titles, both handedness modes, transitions and ordinary H3 play.
Test reported crash scenarios separately. Include edition, runtime, headset and
package source identity with results.

Full preserved scope: `docs/CONTINUATION-REFINEMENT-LIST.md`. Current evidence:
`docs/ZOOM-AND-CE-FLARES-2026-09-18.md` and
`docs/REFINEMENT-WORK-2026-09-18.md` in the matching source ZIP.
