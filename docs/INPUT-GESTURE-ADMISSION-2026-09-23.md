# Mapped scope and physical crouch input admission

This is source/offline evidence for an unaccepted candidate. It does not
establish controller, headset, camera-comfort, or multiplayer acceptance.

## Mapped universal scope

Halo 3's existing body-safe universal scope toggle is the target behavior:
one deliberate press/release toggles the VR scope, without entering native
zoom merely because the input source was rebound. The implementation uses
the existing `ScopeToggleDetector`, wrapped by `ScopeActionInput`.

The old input path read only trigger/grip/face/stick-click sources before
collecting D-pad gestures; a Zoom binding to a D-pad direction therefore
never reached the VR scope. The exclusive thumb-rest path returned before
scope handling altogether. `ScopeAndActionSources` now supplies both scope
and native action mapping with the same admitted sources, including the
head gesture and exclusive thumb-rest directions.

The previous scope release detector also had no selected-source or profile
identity. Changing a held Zoom binding to Unbound or a released control could
look like a deliberate release. Source changes, title/profile generations,
controller disconnection, input ownership loss, menus, pause chords, and
D-pad route changes now cancel the pending gesture. Held controls must
release before a later deliberate press can toggle the scope. Merely
cancelling a gesture does not toggle the active scope. Turning the scope
feature off retains its existing explicit deactivation path.

The pure test target `halomccvr_vr_mapping_tests` exercises the actual
production detector and source collector for every selectable physical
source, both D-pad routes, Unbound/rebind, menu/chord, generation and reconnect
boundaries. Native transport aliases remain a separate limitation: preventing
the Zoom action's own output does not make another action sharing that native
button independent.

## Optional physical crouch

`physical_crouch` defaults off. `physical_crouch_depth_m` is a downward
tracking-space displacement from calibrated normal height, defaults to
0.22 m, and is bounded to 0.08–0.65 m. Standing height is sampled when first
enabled or explicitly recalibrated/recentered; the release threshold is 70%
of the activation depth to avoid boundary chatter. The implementation does
not continuously raise its baseline when a player stretches upward.

The production path reads `VR_GetHeadPose` and the existing focused-session
tracking freshness check. It requires Gameplay mode, head and positional
tracking, stereo, a fresh focused sample, and a verified current native
crouch transport. Menus, pointer-exclusive input, vehicles/turrets, theatre,
invalid tracking and unavailable mappings suspend input. A suspended crouch
must return toward standing before rearming. Full tracking/calibration epoch
and title generation are stored separately; no truncation or XOR can hide a
simultaneous boundary.

`PhysicalCrouchInput` emits the verified native crouch transport separately
from the configurable VR button source. Setting the VR crouch button to
Unbound therefore does not disable this optional physical gesture. Discrete
buttons and either trigger transport are supported. If MCC itself has no
verified crouch binding, the feature emits nothing; it never guesses a
replacement controller button or native action flag.

Tests cover the actual production gesture-to-transport helper for six titles,
all three transport classes, VR Unbound, hysteresis, menu/focus suspension,
recenter and title generations, off/on, and missing native transport.

### Explicit limits

- The current adapter emits a held native controller action. MCC's own
  toggle-crouch preference is not overridden or read through a newly proven
  preference binding. Users must select native hold-to-crouch behavior for
  standing up to release this gesture reliably. This is not fully independent
  native action injection.
- Native crouch and tracked camera lowering can combine. In Halo 3's actual
  camera path, `dy = hpos[1] - g_headPosRef[1]`, then `oz = dy * worldScale`,
  and `pos[2] += oz` adds physical lowering to the freshly supplied native
  camera position. ODST similarly adds tracked vertical displacement to its
  native camera. Neither inspected path removes native crouch eye lowering.
  No speculative native height write or guessed camera compensation is added.
- Consequently activation, stand-up release, crouched camera height, scope,
  weapon/HUD alignment, and multiplayer body/collision behavior need headset
  testing per title, with a Halo 3 regression check. Compilation and these
  tests do not prove comfortable camera descent or obstacle clearance.

`docs/CURRENT-STATE.md` remains the accepted-build authority; this evidence
does not advance its pointer.
