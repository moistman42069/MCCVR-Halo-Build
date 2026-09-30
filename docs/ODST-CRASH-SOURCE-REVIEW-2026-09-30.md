# ODST crash source review — 2026-09-30

## Report and evidence boundary

The September 28–29 community summary records Vixel's report of random ODST
campaign crashes and a bridge-explosion mission as a potentially repeatable
case; wwm0nkey corroborated that a crash occurred. The reviewed record contains
no exact candidate/build identity, ODST runtime log, native-fault log, dump,
exception code/address, mission/checkpoint reproduction sequence, edition,
headset, or OpenXR runtime. The bridge mission is a reproduction lead, not an
identified crash trigger. The separate Hunter report remains a distinct,
unlinked lead.

I searched the preserved `out/test-runs` logs and reviewed the community
evidence index. The available ODST-specific logs there concern older input,
camera/level-load, or feature testing; none is identified as the September
bridge-explosion crash. The preserved Reach first-chance record is unrelated
and explicitly does not establish a fatal exception. There is no crash stack
to attribute to ODST code in this evidence set.

## Source paths reviewed

- The current ODST camera teardown in `src/dll/game.cpp` disables the outer
  render detour first, waits for the outer render callback count to drain,
  disarms and clears the published view, disables the remaining detours,
  suspends process threads to check instruction pointers against detour bodies
  and MinHook's trampoline/relay slots, waits for callback and collision
  counters, and only then removes hooks. Rollback retains target/trampoline
  pointers and the pinned title module when quiescence or native-byte
  restoration cannot be proven. This review found no unsafe release ordering
  to change without contrary runtime evidence.
- `src/dll/odst_contact_melee_runtime.inl` validates full-salt object handles,
  bounds its local sweep and queues, checks returned target/type and finite hit
  data, scopes added damage to its exact native caller, and isolates tick-side
  faults. Its native update/damage detours preserve the original call and use
  `__finally` to drain callback counters. This path is optional and its
  installed/tested state is not present in the crash report.
- The ODST barrel-target feature in `src/dll/odst_muzzle_shots.inl` scopes
  target-storage changes in a lease and restores on every firing exit; optional
  adaptation faults set its feature fault state while the native fire callback
  is not replayed. Its independent-aim experiment is compile-time disabled.
  Its installed/config state is not present in the crash report.
- `src/dll/odst_scope.inl` saves bounded camera fields, restores them in
  `__finally`, and catches an optional third-view fault to disable that scope
  generation while retaining the camera core. No scope fault is linked to the
  reported crashes.
- Physical-crouch reads are wrapped in a feature-local SEH guard and discard
  their lease on failure. ODST's seat patch has dedicated lifetime tests, but
  no current crash report implicates it.

The existing tests under `tests/odst_*` cover muzzle lease/cleanup and target
refusal, optional-scope restoration/fault fallback, seat storage lifetime, and
contact-specific invariants. They are useful offline coverage, not an ODST
campaign or bridge-explosion reproduction. I did not run a full build while
other agents were changing shared files.

## Disposition

No ODST source change is justified by the reviewed crash evidence. In
particular, there is no basis to suppress a first-chance exception, remove an
SEH boundary, or alter camera readiness/teardown: those could hide the real
failure or regress working transitions. The bounded source review found
feature-local lifetime and exception safeguards but cannot establish whether
any of those features were active in the failed session.

To close the report, capture the exact build identity and matching
`HaloMCCVR.log` plus `HaloMCCVR-native-faults.log` (and a process dump if MCC
terminates) through the failure. Record the campaign mission/checkpoint,
whether it is the bridge-explosion case, exact actions immediately before the
crash, MCC edition, headset, OpenXR runtime, and which optional ODST settings
were enabled. A no-mod comparison of the same sequence will distinguish an MCC
campaign fault from an injected-mod interaction; it will not by itself identify
the responsible mod hook.

## Related CE vehicle-identity correction

The accompanying investigation in
`CE-SUBTITLE-VEHICLE-IDENTITY-RESEARCH-2026-09-30.md` was corrected after
re-reading the retail decompilation: `FUN_180af5ec8` reads `+0x2D8/+0x2DC` from
the original object definition, not the model pointer. Those fields must not
be described as the model's node table. Retail object-definition `+0x34` to
cached model-tag resolution is supported. Subsequent independent node-consumer
proof and a guarded read-only profile implementation are recorded at the top of
the CE research document. This supersedes its initial vehicle-identity blocker;
it does not change the ODST crash disposition.
