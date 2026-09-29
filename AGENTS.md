# Halo MCC VR agent contract

## User delivery override (2026-09-05)

Resume continuity: read `docs/ACTIVE-WORK-CHECKPOINT.md` for the latest user
priority, packaging hold/WIP exception, preserved work and unresolved items.
Read `docs/CONTINUATION-REFINEMENT-LIST.md` for the user-approved standing scope
whenever the user says "continue", including in a new chat.

Deliver the build ZIP and matching source ZIP in Codex chat, then wait for the
user's testing and instructions. Run `tools/package-candidate.ps1` without
`-Install`. Do not install, move/replace game-folder files, launch MCC, or open
a PR without a new explicit request. This overrides the older automatic
deployment instructions below. Local source edits, builds, tests and commits
remain part of preparing the requested candidate. Both editions stay supported.

Read `CLAUDE.md` and `docs/CURRENT-STATE.md` before changing code.
`docs/CURRENT-STATE.md` is the authoritative accepted-build pointer.
Reverse-engineering facts live in the evidence documents under `docs/`.

This file was rewritten on 2026-07-26. The previous version mandated a single
"strict implementation-parity" architecture across three different engines and
required that any failure inside a claimed VR transaction tear the whole thing
down. Those two rules, plus a direct self-contradiction about removing dormant
code, produced a day of regressions on Halo: Reach. Keep this file short. A rule
that has never prevented a real defect is not worth the damage it causes.

## What parity means

Halo 3's headset-confirmed experience is the reference for every other title.

**Parity is the player experience, not the code.** A title has parity when it
honours the same `halomccvr.cfg` knobs, looks and feels the same in the headset,
and behaves the same from the player's seat. Halo 3, ODST and Reach are
different engines; identical implementations are neither required nor expected.

- Find each engine's own way to achieve the behavior. If an engine genuinely
  lacks the construct another title uses, say so plainly and record it in the
  evidence docs. Reach has no `game_is_playback`-gated class-2 CHUD crosshair
  predicate (`docs/REACH-SIGNATURE-EVIDENCE.md`); two candidates were lost
  hunting a construct that does not exist in that engine.
- A shared VR-side implementation is allowed when an engine lacks the native
  path, provided it is deliberate, logged, and documented as a difference. The
  procedural VR reticle the ODST camera core ships is the model.
- Silent degradation is still forbidden. Choosing a different, stated
  implementation is not degradation.
- State the Halo 3 behavior being matched before implementing a title feature.
- A shared-code or lifecycle change needs a target-title headset result and a
  Halo 3 regression result.

## Failure isolation

**A feature failing must never take down a working VR path.** This is the rule
whose absence caused the 2026-07-26 Reach regressions.

- Every player-visible feature is its own transaction. If it cannot be proven,
  or it fails at runtime, it degrades to stock **for that feature only**, loudly
  and in the log. It must never disarm the camera core, end the OpenXR session,
  or gate arming.
- Build optional features on ODST's fail-open shape
  (`InstallOdstCrosshairHider`: `StockFallback` / `CleanupRequired` /
  `Installed`).
- For the core per-eye render, "reject" means **drop that frame** and keep
  going. It does not mean end the session, detach VR, or unhook. Halo 3 and ODST
  skip the frame and recover; every title should.
- Any code path that disarms a title core must log why, naming itself. A silent
  teardown is a bug: it cost a full day of unattributable failures.

## Verify, do not trust the narrative

A comment, a doc paragraph, or a previous session's confident claim is a lead,
not a fact. On 2026-07-27 two stale claims each cost hours: "Reach has no
authored capture" and "the ODST camera core installs no authored capture yet"
were both false when read, and both caused a title to paint an opaque
procedural crosshair over art it had already captured. A third - treating an
existing capture pipeline as working code that merely needed an address - hid
four identical crashes.

- Before building on "title X cannot do Y", check whether the code still says
  that. Prefer a live check (`Game_TitleCapturesAuthoredCrosshair()` reports
  what is actually installed) over a hardcoded title list, which goes stale
  silently.
- When a write is verified correct but has no visible effect, ask whether the
  consumer selects among several copies of that data by a runtime condition
  (resolution class, skin, collection) before concluding the field is dead.
  Reach's HUD sliders were "inert" for hours because the engine reads a
  different curvature record than the one being written.
- Measure before optimising. Three reasoned performance fixes in a row missed,
  two made it worse; the answer came from `renderWindow p95` in preserved logs
  and from the user's own "90Hz is solid, 120Hz halves" observation. Frame rate
  problems here are usually deadline problems, not throughput problems.

## Evidence

- Locate engine code with unique signatures verified against the pinned module.
  Never ship a guessed or copied address.
- Zero or multiple signature matches block **that hook**. That blocks VR
  ownership only when the hook is required for camera ownership; for an optional
  feature hook, only that feature stays stock.
- Never copy a Halo 3 offset, struct member, bone, marker, tag meaning, or
  constant into another title without title-specific evidence.
- **Reach facts come from HREK. Retail is not a discovery tool.** Understand
  what a system does, and what its functions and structures are, from the
  official HREK executables and tags -- they carry full symbols, source paths,
  assert text and tag definitions. `haloreach.dll` is stripped and optimized;
  reading it to *discover* behavior produces plausible-looking wrong answers,
  which is exactly how 2026-07-26 was lost.
- Use the pinned `haloreach.dll` only to **match and verify** something HREK
  already explained: to locate the homologous function, confirm a layout, or
  check uniqueness before hooking. Never to invent a binding.
- Never derive bindings from Reclaimer or archived console binaries at all.
- Never write a theory into an evidence document as a finding. Ship a probe, or
  write down the negative result.

## Changing code

- One evidence-backed behavioral change per candidate. Unique commit and
  artifact hash. Untested or failed candidates do not advance the pointer.
- Diagnostic-only instrumentation that does not change gameplay, tracking,
  aiming, input, rendering, or title output may be developed as its own
  evidence candidate. Keep diagnostic instrumentation separate from
  behavioral tuning so recorded evidence describes the behavior under test
  rather than a simultaneous behavior change.
- Revert a failed experiment when it fails, as its own commit, before starting
  the next one. Do not stack onto a known-failed candidate.
- **Reverting means disabling the behavior, not deleting the code.** Deleting
  dormant code is a separate, later task: one understood path per candidate,
  each headset tested. Cleanup commit `42a1276` and the 2026-07-26 crosshair
  cleanup both built, passed tests and passed the gate, then broke the runtime.
  If in doubt, leave inert code alone; a disabled hook costs nothing at runtime.
- `tools/check-reach-fp-parity.ps1` is a consistency check, not a design
  authority. It guards against reintroducing disproven architectures. It must
  never be used to freeze implementation text. If a rule there blocks a correct
  change, fix the rule and say so in the commit.

## Safety

- Never hook `halo3+0x120DF8`.
- Never patch game files on disk or interact with Easy Anti-Cheat.
- Keep logging, file I/O, locks, COM, allocation, and signature scanning out of
  render and palette hot hooks.
- Diagnostic capture in a hot/prepared-frame path may read state already
  captured for that same frame, perform bounded allocation-free side-effect-
  free calculations on it (including a small fixed set of telemetry-only pure
  counterfactual solves), read `QueryPerformanceCounter`, fill a fixed-size
  trivially-copyable record, publish through a preallocated lock-free SPSC
  ring, and update lock-free atomics. It must not format/serialize, allocate,
  log, perform file I/O, take locks, wait, or signal/notify a per-frame kernel
  object.
- Diagnostic capture must reuse the frame's existing OpenXR samples. Extra
  `xrSyncActions`, `xrGetActionState*`, `xrLocateSpace`, `xrLocateViews`, or
  other runtime sampling calls solely for telemetry are a separate
  evidence-backed change and are not allowed in the hot recorder path.
- Diagnostic serialization and file I/O belong on a dedicated worker created
  outside loader lock. Recorder failure must fail open and must not change
  tracking, aiming, rendering, input, or title behavior. Do not add a new
  third-party dependency solely for diagnostic transport or serialization.
  Telemetry-only counterfactuals must not mutate Config, the selected runtime
  Test Profile, diagnostic override state, tracking state, or gameplay output.
- Preserve finite-value, bounds, index, count, and teardown guards.
- `camscan` is opt-in and has process-memory write modes. Never build or run a
  write mode without explicit approval.

## Deployment

- `tools/package-candidate.ps1` builds, tests, packages into ignored `out/`, and
  automatically installs that exact manifest-verified candidate into
  `Halo_MCC_VR` for every MCC edition present (Steam and Microsoft Store). Do
  not ask for a separate install confirmation.
- It requires MCC closed, preserves the prior files under `out/deploy-backups`,
  and never launches MCC or changes an existing `halomccvr.cfg`.
- One build serves both editions: the per-game modules are byte-identical apart
  from their Authenticode signature (`docs/MCC-EDITIONS-EVIDENCE.md`). Only the
  launcher is edition-aware.

## MCC editions

Both the Steam and the Microsoft Store (Xbox app / Game Pass) editions are
supported and **every candidate installs to both, every time**. The user
alternates between them, so never deploy to only one.

- Ask which edition a result came from, or read it: `HaloMCCVR.log` names it on
  the `MCC edition:` line, right under the build identity, alongside the
  `OpenXR runtime:` and `headset:` lines. A report that does not identify the
  edition, runtime and headset is not reproducible.
- The Store executable **cannot** be started with `CreateProcess` — it exits
  with code 0 in about 210 ms because the process has no package identity, and
  no `CreateProcess` attribute can grant it. It is started through
  `IApplicationActivationManager` instead. Renaming the executable to the Steam
  name does not fix this and never did.
- A game-code difference is never a valid explanation for an
  edition-specific bug. The modules are byte-identical; look at launch,
  environment or host-process differences instead.
- `tools/install-candidate.ps1` requires git `HEAD` to equal the package's
  `source_commit`. To reinstall an older candidate, check out that commit first.
- Verify the installed DLL's SHA-256 separately; the log does not contain it.
  Match the source and configuration in the log's first line.

## Acceptance

The user's headset result is the acceptance test. A clean build, passing tests,
and a passing gate prove nothing about runtime behavior — every broken Reach
build on 2026-07-26 passed all three. Advance `docs/CURRENT-STATE.md` only after
explicit headset acceptance.

When a runtime failure is being diagnosed, compare the new log against the
preserved log in `out/deploy-backups/<hash>-before-<hash>/HaloMCCVR.log`. Do not
theorize past a log that disagrees.
