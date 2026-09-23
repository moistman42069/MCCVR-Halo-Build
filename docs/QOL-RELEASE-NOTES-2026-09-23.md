# MCC VR QoL test candidate

This is a cumulative, unaccepted test candidate for Steam and Microsoft Store
MCC. The build and matching source archives identify their exact source commit.
The accepted Alpha 0.4.2 pointer is unchanged. No game installation or launch
was performed while preparing this package.

## Included community contributions

- Optional NVIDIA DLSS across all six titles, including both CE and Halo 2
  graphics modes. Adapted from Pancreations, with martysl1's Reach depth-path
  correction reconstructed and verified. Missing valid depth/camera or an
  unsupported GPU retains the ordinary full-resolution renderer.
- Native localized subtitle overlay and gameplay/theatre placement controls
  for Halo 2, Halo 3, ODST, Reach and Halo 4, based on martysl1's contribution.
  CE's localized text handoff remains unproven and retains native subtitles.
- Per-game bloom controls for Halo 3, ODST and Reach, adapting martysl1's
  final shader-consumer fix with bounded resource ownership and restoration.
  Bloom stays enabled by default; other titles retain native bloom.
- Optional simulated gunstock, adapted from Gab_dC, with head, shoulder,
  chest and adaptive references, strength and proximity release controls.
- Menu wrapping, clearer categories and resize status; retained-resource
  resize fixes, failure rollback and improved allocation diagnostics.

Godoy's reports and calibration suggestions, UnsealedWings' source, LivingFray
PR153 and marcusau2 PR1 were reviewed and are accounted for in the included
implementation ledger. Existing stronger lifecycle checks and adopted weapon
calibration were retained. No old contributor binary was substituted.

## Controls, weapons and comfort

- Shared VR action defaults with controller-profile detection, saved per-game
  overrides and an explicit Unbound choice in VR Mappings.
- Separate flashlight suppression while supporting a weapon with two hands;
  grip admission and held-input draining refined.
- Physical crouch toggle, activation-depth slider and height calibration.
  Use MCC's hold-to-crouch preference. Native camera correction is bounded to
  the tracked downward movement and does not change native physics.
- Independent primary/secondary crosshairs in Halo 2 and Halo 3, including
  handedness and authored-barrel routing.
- Saved vehicle XYZ defaults for every game, with per-model/seat adjustments
  in H2/H3/ODST/Reach/H4. CE retains per-game adjustments; its persistent
  per-vehicle reader remains WIP. H2 uses interpolated vehicle marker nodes
  when smoothing is enabled; complete stutter removal needs headset testing.
- Per-game muzzle-effect hiding retained for Halo 2 Original and Halo 4.
  Halo 2's fallback hides the local first-person particle pass more broadly.
- Optional stock and reserve-magazine placement controls; per-gun alignment,
  reload, holster, hand-offset and vehicle-turn regression coverage retained.
- Physical roomscale debt survives stick movement and catches up after native
  motion settles in the five supported adapters. CE body following and truly
  simultaneous physical/stick following remain separate pending work.

## Installer and updates

Extract the archive and open its root launcher. Detect or browse to the **base
MCC folder**; the installer creates `Halo_MCC_VR` directly inside that folder.
Steam and Microsoft Store editions are supported. Retaining configuration
preserves existing values and unknown keys while adding missing new defaults.

The launcher includes explicit game launch and GitHub release update checking
and installation. The complete `ModFiles` folder supports manual installation.
Backups, payload hashes, rollback and update-archive validation are included.
The stylized menu uses the bundled OFL-licensed Oxanium font.

## Test limits and remaining work

Passing native-contract, production-fixture, WARP and packaging checks does not
establish headset acceptance. Verify first-person vehicle movement and offsets,
crouch, subtitle placement, dual crosshairs and DLSS in the games you use.
Co-op firing failures still need a two-player reproduction with a useful native
stack. CE subtitle placement, native action independence, several custom-content
and lighting reports remain unresolved; see `IMPLEMENTATION-STATUS.md` for every
audited item's disposition.

VR remaps use verified native transports. Native actions that share a button can
remain coupled, such as Reload/Use. Unbound removes an action's own VR source.
DLSS uses camera-based motion, so moving-object ghosting remains possible;
frame generation and the rejected sampler-bias experiment are not enabled.
