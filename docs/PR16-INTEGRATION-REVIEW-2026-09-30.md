# September 30: Gab_dC PR 16 integration review

Contributor: **Gab_dC / gabrieldch**. Public contribution:
https://github.com/moistman42069/MCCVR-Halo-Build/pull/16

Fetched PR head: `3c07886ca0d6baa5f6711f6dfa3fd7c0708c344b`.
Recipient base: `566e52c61c51f9aa5f9a8bfda5b3d5e55b695fc2`.
Five upstream commits: `e7f15f9`, `93b6261`, `75af910`, `4961314`, `3c07886`.
The accepted-build pointer remains unchanged. This integration is a local,
unaccepted candidate; it does not merge or publish the GitHub pull request.

## Included behavior

- Product Virtual Stock Plus and Standard, with independent strength settings.
- Fixed 200 ms grab/release continuity, including moving destinations and rapid
  reversals; experimental head-turn sway correction remains retired.
- Free two-hand geometry based on controller Grip positions, primary Aim
  orientation/roll and primary-owned position; adjustable offhand influence.
- Persistent support grip with per-title owner qualification, explicit absence
  distinguished from unreadable ownership, and independent one-hand fallback.
- Default weapon-switch release/reacquisition policy and optional inheritance.
- Two-hand direction smoothing (0–25, default off), shared by free two-hand,
  Plus and Standard without smoothing global tracking or acquisition geometry.
- Committed coherent input samples for the asynchronous H3/ODST/Reach/H4 aim
  consumers, including session/title/generation/handedness/space/freshness checks.
- Config v6 migration; explicit v6 `two_hand_coherent_aim = 0` remains honored.
- Local opt-in telemetry recording and standalone analysis of the closed file.
  Python analysis is optional; unavailable Python must not invalidate recordings.
  Storage is `Telemetry Recordings` beside the mod, independent of build names.
- Contributor policy tests, production-function fixtures, lifecycle guards,
  telemetry format validation, analyser and recorded engineering notes.

## Native changes reviewed with the feature

These are support-grip qualification and diagnostic changes, not evidence that
unrelated co-op, vehicle or graphics reports have been fixed.

- CE: existing validated local-player state now preserves raw-slot presence for
  tri-state evidence. First-person and unit-control consumers qualify frozen
  support geometry against the current owner; mismatches choose the independent
  primary carrier or the stock packet. Existing player/unit/reference/renderer/
  tracking epoch checks and native-call-once behavior remain.
- H2: existing owned-unit and first-person slot/object readers establish owner
  evidence. A level-live worker poll supplies liveness independently of admitted
  weapon packets. Packet, native aim and scope consumers detach stale support
  carriers without disabling their camera core.
- H3/ODST/Reach: title-specific existing equipped-role and first-person/TLS readers
  distinguish an empty primary slot from uncertainty. No cross-title offset is
  invented. Reach's consumer receipt is checked against owner and generation;
  two consumers with no prepared serial retain the documented latest-only limit.
- H4: existing H4 muzzle ownership reads feed support ownership. The guarded
  first-person fallback uses already documented TLS `+0x6A0` and the same unit
  parent/controlling-parent checks as `Halo4MuzzleTargetStorage`. Storm-hands
  boundaries resolve semantic-primary ownership; later held-weapon records
  validate the frozen decision. The vehicle-input reader adds diagnostic reasons
  without relaxing its guards; its wrapper retains an unwindable code range for
  the existing retirement procedure.
- Shot diagnostics publish fixed records only when recording is admitted. Native
  shot signatures/velocity/simulation calls remain; diagnostics are not a new
  projectile or co-op implementation.

The new committed-sample payload uses atomic members, a bounded reader retry and
explicit identity/freshness checks. Telemetry publishers use fixed queues and
atomic admission; disk I/O and analyser process creation run on workers.

## Local integration corrections and exclusions

The upstream `AGENTS.md` append was deliberately not imported: repository/user
instructions do not change merely because a contribution includes agent notes.
The upstream `tools/package-candidate.ps1` and `tools/install-candidate.ps1`
patches were not applied wholesale. Their telemetry staging/identity requirements
were handed to the launcher/packaging work, which owns the new separate launcher
and manual archives. No install script was executed.

Review found 17 newly added synchronous log sites reachable from H4 native
render/ownership or OpenXR support-input paths. They called the file logger,
which takes a critical section and flushes a file. These calls now only increment
bounded lock-free event counters in `support_diagnostics.h`. The existing 50 ms
background title worker drains and logs them. Release reasons are retained;
per-owner numeric details belong in optional telemetry. This avoids adding file
I/O, formatting or logger locks to those hot paths. A concurrent-producer test
checks counting, coalescing and single-drain behavior. The persistent-grip source
guard now checks the actual owner-change/clear branch rather than requiring a
particular following log statement.

Launcher-related CMake adjustments are coordinated with packaging: the launcher
has its own install component and an embedded administrator execution level.
The package verifier checks the actual embedded execution-level XML, the separate
launcher hash, every manual file and byte identity against the installer payload.

## Validation and remaining evidence

At integration review, all three source-guard self-tests pass (persistent grip,
aim-continuity lifecycle and hidden lab/menu policy). The standalone telemetry
analyser self-tests pass. The root packaging run is responsible for the combined
Release build, full CTest suite and exact final ZIP/source identity verification;
those results must be recorded against the final candidate, not assumed from the
contributor's reported results.

The contributor reports headset use, but no local headset test has been performed
for this cumulative candidate. All-six-title, Steam/Store and controller/runtime
regression coverage still requires user testing, especially weapon-to-unarmed,
weapon switch/death/reload/re-entry, handedness, support tracking loss, vehicles
and both graphic modes in CE/H2. These are verification requirements, not claimed
fixes for the open visual/crash reports.

Per-weapon grip capture/calibration, species-specific grip presets and one-hand
weapon policies (for example sword/plasma pistol), physical HMD yaw driving body
yaw and a new torso/neck model were discussed separately; they are **not included
in PR 16** and must not be claimed as delivered by this integration.
