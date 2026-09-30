# Optional arm-swing locomotion

The Halo 3 behavior being preserved is ordinary head-relative native walking,
without changing native movement speed, collision or network authority. All
six title camera owners already prove on-foot control for roomscale; those
same per-title checks now also admit the separate physical-running option.
Body following itself still requires `roomscale_movement`.

`physical_running` defaults off. `physical_running_speed` limits analog demand
to 0.1–1.0 of native maximum; sensitivity ranges 0.25–3.0. The Controls page and
Quality of Life mirror edit the same saved fields. Alternating two-arm motion
ramps forward movement, while active motion takes precedence over conflicting
movement-stick demand. Turning, menu navigation and seated controls remain
ordinary input. This does not automatically activate a game's sprint ability.

The same prepared XR frame supplies head and both physical controller positions.
Head-relative velocity excludes whole-body translation. Both hands must reverse
direction recently, oppose one another and move primarily in the forward/up
plane. Single-arm punches, parallel motion and two-handed aiming do not qualify.
The command uses the existing head-relative native movement mapping, not an
engine-position or speed write. Motion is independent of game world scale.

Native on-foot checks run only on their existing camera owner thread. The
nonblocking roomscale publisher guard also protects the gesture history; the
input reader receives an atomic bounded command tagged with title, generation
and time. Repeated eyes cannot advance it, a frozen XR sample cannot extend its
100 ms lifetime, and admission/tracking/title/recenter loss clears history.
Input-side menu/pause/theatre checks prevent consuming commands there.

Offline tests cover 60/90/120/240 Hz, duplicate eyes, tracking/recenter,
whole-body/parallel/single-hand rejection, bounded speed, actual camera-to-input
transport, frozen-sample expiry and native admission loss. The 240 Hz test found
and corrected a sub-deadzone ramp reset. Core Release tests pass. No headset
acceptance, all-title runtime parity or comfort claim has been made.

Behavior reference requested by the user: their Project06-VR arm-swing running
implementation (`Sonic-P06-Quest/src/gestures.h` in the adjacent workspace;
public project https://github.com/moistman42069/Project06-VR). This is a separate
MCC implementation using MCC's existing native locomotion transport.
