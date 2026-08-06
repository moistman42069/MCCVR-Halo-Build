# Project instructions

Halo MCC VR is a native C++20 OpenXR mod for Halo: The Master Chief Collection,
supporting both the Steam and the Microsoft Store editions. The published
cumulative release (Alpha 0.3.3) supports Halo 3, Halo 3: ODST and Halo: Reach.
Halo 4 is in bring-up from the `f4c641f` development baseline.

Read and obey `AGENTS.md`, then read `docs/CURRENT-STATE.md` before changing
code. Use `docs/RE-notes.md` only for verified Halo 3 facts and the ODST evidence
documents for ODST-specific signatures and layouts.

## User and acceptance

The user is a game modder, not a programmer. Explain test steps in plain
language. A build or log is supporting evidence; the user's headset result is
the acceptance test.

## Workflow

1. Confirm the branch descends from the accepted source pointer.
2. Make one evidence-backed behavioral change per candidate.
3. Configure, build, and test the cumulative Release preset described in
   `BUILDING.md`. Stop on any compiler or test failure.
4. Package only into the repository's ignored `out/` directory.
5. A successful candidate package automatically installs its exact
   manifest-verified DLL and launcher into the dedicated `Halo_MCC_VR` folder of
   every MCC edition installed on this machine (Steam and Microsoft Store),
   after confirming MCC is closed, preserving the previous install, and
   verifying installed hashes. Never launch the game or change configuration.
6. For a requested headset test, record the source commit, DLL SHA-256, unique
   package path, embedded log source/configuration, title coverage, MCC edition,
   OpenXR runtime, headset, and result. Verify the installed file's hash
   separately; the log does not contain it.
7. Advance `docs/CURRENT-STATE.md` only after explicit headset acceptance.
8. Revert failed behavior before making another candidate.

`tools/install-candidate.ps1` is the only deployment path and is invoked
automatically by `tools/package-candidate.ps1` after every clean candidate build.
It accepts only a manifest-backed package under `out/candidates`, requires MCC
closed, preserves the prior DLL/launcher/log/config under `out/deploy-backups`,
verifies staged and installed hashes, never launches MCC, and never changes an
existing `halomccvr.cfg`. It installs into every requested edition whose MCC
install is present, seeding a first-time folder from the live config so both
editions run identical settings, and reports any edition it skipped. Old
deploy/restore scripts remain forbidden.

## Non-negotiable implementation rules

- Locate engine code with unique signatures; never ship a guessed hardcoded
  address.
- Never hook `halo3+0x120DF8`.
- Keep hot hooks deterministic and allocation-free.
- Do not patch game files or run with anti-cheat enabled.
- Preserve one universal `halomccvr.cfg` and F1 experience.
- Use H3ODSTEK and `halo3odst.dll` as primary ODST evidence. Halo 3 offsets and
  semantics are not ODST proof.
- Support both the Steam and Microsoft Store (Xbox app / Game Pass) editions.
  Install every candidate to both, every time — the user alternates between
  them. `halo3xr.log` names the edition, the OpenXR runtime and the headset;
  ask which one a result came from rather than assuming. See
  `docs/MCC-EDITIONS-EVIDENCE.md`, and note that the game modules are
  byte-identical across editions, so a game-code difference never explains an
  edition-specific bug.

Definition of done is: clean diff, Release build, passing tests, exact candidate
identity, requested headset confirmation, and any required Halo 3 regression.
