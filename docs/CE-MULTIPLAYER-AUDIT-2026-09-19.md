# CE multiplayer menu and grenade investigation

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
