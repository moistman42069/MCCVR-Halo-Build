# CE multiplayer menu and grenade investigation

## Follow-up implementation: E-CE-NETWORK-INPUT-1

The user requests completing the multiplayer tracking change after diagnostic
candidate ef4bf9a. This section supersedes the unfinished implementation status
below, while retaining the investigation evidence and runtime limits.

Target behavior matches Halo 3's controller-directed native aiming/throwing,
with native actions reaching both client prediction and authoritative simulation.
CE's ordinary network action has only one direction: remote/native body facing,
aiming and looking follow the aiming controller together. Independent networked
head/body facing is not represented by that protocol. Rendering still applies
HMD tracking independently through the unmodified native camera angle records.

An independent optional hook runs the native action builder A9A8A4 exactly once
and then changes only its local output yaw/pitch and on-foot horizontal throttle.
It admits the A997B8 input-loop call at A99888/return A9988D, requires the source
to equal the verified local player's control+0x14+inputUser*0x58, and restricts
the operation to connection types 1 (client) and 2 (server). Full player/unit,
generation, camera/reference/renderer, input blocking and tracking guards are
rechecked before publication. Other local input users and remote inputs are
untouched. Campaign/local type 0 and playback type 3 retain their old paths.

The output is changed BEFORE the loop copies/submits that same action. Retail
B7CF44 copies all four 0x30-byte actions into the native queued frame at
2C983D4 when 1B7B630 is nonzero; the other branch forwards the same pointer to
C66948. See `out/coop-audit-20260919/ce-action-submit.txt`. No wire packet size,
protocol, action flags, predicted assist, inventory, grenade selection or native
timing changes. Negative atan2 yaw is wrapped into the native nonnegative
angular domain. Native AD0304 consumes the resulting yaw/pitch unchanged.

On foot, forward/left throttle is rotated by originalYaw-newYaw, preserving
the world direction represented by the original action. This composes with the
existing head-relative XInput mapping rather than applying HMD yaw twice.
Vertical throttle and magnitude remain intact. The later local camera-basis
movement override and fresh-pose unit packet override are bypassed in network
sessions: native body interpolation, physics, prediction and authority consume
the same transmitted input. Real latency and abrupt body-turn interpolation
still require a two-peer movement test; this is not a full network emulator.

Seated actions retain native drive throttle. B0584C is the native direction-only
seat transform called by AD0304: on-foot or the appropriate seat flag is identity;
the other branch builds an orthonormal frame from the native parent orientation.
The adapter evaluates it on three private basis vectors, validates orthonormality
and handedness, and inverse-transforms tracked world aim before encoding it.
The native receiver then applies its seat transform exactly once. Parent, seat
and perspective are rechecked. Native seat limits and driver/gunner forwarding
remain native. The existing coherent vehicle/controller reticle presentation
is retained; network simulation receives the direction through the outgoing
action instead of the old local-only unit override.

Hook evidence and exact unique signatures/operands are in
`HALOCE-NETWORK-INPUT-CONTRACTS.json`, integrated into the evidence manifest and
generated runtime checks. A9A8A4 is a leaf/tail-jump entry without its own x64
unwind record; its exact bytes and unique input-loop call are verified instead.
The other native functions retain their actual unwind verification where used.
Installation/retirement are independent of the camera and campaign adapters;
faulted original calls propagate once, and pending cleanup retains dependencies.

Validation: production tests cover client/host/local/playback selection, exact
local source identity, on-foot rebasing, seated inverse transform, byte
preservation, malformed/stale/blocked inputs, seat transitions, native exceptions
and partial installation/retirement. `test_ce_network_input_native.py` executes
125 production-generated action cases through the real pinned action creator,
250 client/server native queued-frame submissions and AD0304/on-foot seat
conversion. Only the separate aim-assist encoder is modeled in that fixture.
The existing native unit/grenade/vehicle tests cover the downstream native
handoffs. No transport latency, complete simulation or headset result is claimed.
Both graphics modes and both editions use the same CE module path.

Final local checks: Release build and all 62 CTest suites pass; pinned CE
loaded-image groups and an actual MinHook create/enable/disable/remove cycle
over a private copy of A9A8A4 pass. Downstream fixtures pass 16 unit/grenade
handoffs, 64 independent camera headings, 20 native movement cases and 40
driver/gunner handoffs (80 packet writes). Reach consistency gate passes.

The separate reported co-op firing crash remains unproven. This correction does
not advance the accepted build pointer or certify all-title multiplayer stability.

User clarified the invisible menu is pause/settings during a match, not the
MCC lobby. Target experience matches Halo 3: present the native menu on the
head-locked screen and return to stereo when gameplay resumes.

## Menu presentation correction

The previous CE implementation treated the simulation pause byte as an
authoritative menu state. It suppressed the existing explicit controller
Menu/Start presentation request and reconciled an unpaused clock back to
stereo. A local multiplayer menu need not pause simulation.

CE now retains the existing explicit Menu/Start presentation request. The
verified local input suppression predicates from E-CE-FP3 keep that request
active while simulation runs. Resumption of native input clears it after the
existing 50 ms correction delay. A request the engine does not accept gets a
500 ms entry grace period before reconciliation. Native simulation pause
continues to work in single-player. Ownership loss clears presentation.

Suppression alone NEVER opens a menu: it also occurs for other native reasons.
The new reader resolves output-user-zero's full player identity to exactly one
input user without requiring a live unit/weapon. Unknown mapping is not resume.
It uses the already verified player-state addresses and predicates consumed
by retail A9915C, not a new guessed menu flag. No game state is written.

Tests cover an always-running multiplayer clock, native suppression, Resume,
unknown samples, failed requests, cinematic suppression without a request,
ownership loss and malformed/ambiguous native mappings. Headset visibility and
keyboard-only menu entry remain unverified. This is a presentation candidate,
not confirmation of every way MCC can open an overlay.

## Grenades: confirmed gap, network fix unfinished

Pinned retail SHA-256:
`0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C`.
Official HCEEK SHA-256:
`FC9E2B6193C6F6D9FF988278B0D39A983747F3FDBDECA7CAFADD28F1F0C53E73`.

Offline decompilation evidence preserved under `out/coop-audit-20260919`:

- `ce-connection-enum.txt`: official kit functions 00598030 and 0056B720
  assert connection type 1 is network client and 2 is network server;
  00593850 asserts type 0 is local.
- `ce-action-native.txt`: retail AAF7D0 dispatches type 1 to AD102C and
  types 0/2/3 to AD0720. A9A8A4 constructs the 0x30-byte outgoing action;
  its yaw/pitch are at +4/+8. AD0304 converts those angles to a direction.
- `ce-player-network-native.txt`: AD102C calls unit control AFE098 at
  AD14CE (return AD14D3). The existing adapter admits only ordinary caller
  AD0D56 (return AD0D5B). This explains a concrete client-side tracking gap.
- Existing `out/ce-fp-native-control-loop.txt`: A997B8 calls A9A8A4 at
  A99888 before copying/submitting the local action through B7CF44.

Merely admitting AD14D3 would modify local prediction after the outgoing action
was formed. The host would still receive the old aim. That is not a complete
multiplayer fix and could create additional prediction disagreement.

The native network action supplies one direction for facing, aiming and looking.
A correct outgoing controller-direction change must also preserve native
movement coordinates and agreement between local prediction and the host;
campaign currently has a separate private movement-basis adapter. That work is
not established here. No speculative packet or remote-player modification is
shipped. Multiplayer grenade tracking remains UNFINISHED, with this exact
producer/consumer gap preserved for the next pass.
