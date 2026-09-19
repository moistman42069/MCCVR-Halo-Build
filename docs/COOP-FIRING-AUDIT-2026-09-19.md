# Co-op firing investigation — not a confirmed crash fix

The user reports failure on firing even with all optional settings disabled,
and in any firing direction. Impact is not required. Focus is therefore the
mandatory firing/aim/collision/effect path, not a settings workaround.

## Inputs and limits

Two preserved d088171 logs, both Halo 3 / Steam:

- `out/coop-audit-20260919/vdxr-h3.log`: VDXR 1.0.10, Quest 3, 72 Hz;
  SHA256 `1ACEA045249407C9107BD44BFC0D6F1A5DB539C1D2DE331AFA4003D2E1F2C67F`.
- `out/coop-audit-20260919/steamvr-h3.log`: SteamVR/OpenXR 2.17.10,
  Oculus family, 90 Hz; SHA256
  `EC44C750ADDB76803CE3436B59A3518303F3C1CAC8ADFD27501C668AD2A6C2BA`.

Neither contains an exception address or stack. Both continue through level
retirement/menu module traffic. They do not establish desktop process failure
versus a co-op session exit, nor the failing peer's role. The later user report
of an all-settings-off run is additional evidence, not a claim that both
attached configurations were all off. The VDXR log reports zero barrel/dual
overrides; the SteamVR log reports two. Neither records submitted physical
melee. The VDXR timing before shooting stops has no recorded large Present
stall. The older H2 Cairo/Outskirts log remains relevant, but no common cause
has been established.

## Verified correction

`Halo3CollisionResolveDetour` wrapped Halo's **original** collision query in
`__except(EXCEPTION_EXECUTE_HANDLER) {}`. On an exception it returned zero,
even though Halo could already have partly written the collision output. The
hook remains installed when world collision is off. This violates native
exception semantics and can conceal the actual failure. It is a proven source
defect, **not proof of the reported crash's cause**.

The wrapper now uses `__finally` to release callback accounting and allows
native exceptions to reach their original handlers. It never retries. Only
the mod's added contact scheduler is inside a feature-local exception guard;
its failure preserves the completed native return/output and disables only
that optional feature. The scheduler is also used by ODST/Reach. Their active
collision wrappers already use `__finally`, as do H2 and H4. The dormant old
Reach endpoint wrapper is not installed and has not been changed.

The H3 scheduler/wrapper is included verbatim by a production-code regression
fixture. It tests all eight arguments, return/output preservation, settings
off/on native exceptions, no replay, no extra query after a native fault,
callback release, owned-query recursion exclusion and optional failure isolation.

## Native audit evidence

Offline pinned H3 retail decompilations in `out/coop-audit-20260919/`:

- `core-native.txt`: `17DF44` observer effects and `1FD748` collision vector.
- `marker-native.txt`: `343D74` markers, `184B08` interpolation, `1B403C` effects.
- `damage-native.txt`: `36FE00` damage consumer, six native arguments.
- Existing `out/reload-policy/h3-fire-audit.c`: `3683A0` has five firing
  arguments; `3524B0` is the seven-argument aim helper. The stock firing marker
  call has interpolation disabled. No missing firing argument or proven
  first-person render-buffer leak into that stock marker call was found.

Pinned H3 identity remains
`B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63`.
The original camera-effect suppression and render-pose code are not changed
on an unproven theory. H3 body-bank writing is bypassed when the installed
interpolation hook is active. Core aim still uses the existing input path.

Source review of CE ray wrappers, H2/ODST/Reach/H4 firing wrappers and active
collision dispatch confirms their original calls are outside optional-fault
catch/replay paths. Existing local-owner and generation guards remain. This
is not an all-feature network/determinism certification; custom hand-origin,
reload, physical damage and vehicle behavior still need multi-peer testing.

## Diagnostics added for the next runtime result

H3 logs cumulative native firing entries, normal returns, abnormal unwinds,
nonnull firing-data inputs and prediction flags, **for all actors**, regardless
of optional aim settings. These are not local-player shot counts or proof of
host/client role. Native arguments are forwarded unchanged.

`HaloMCCVR-native-faults.log` records first-chance hardware faults for all
titles. It preserves the previous run as `.prev`, source identity, fault
code/address, access target, saved argument registers and exception-dispatch
stack with image-relative addresses. The stack is labelled as dispatch-time,
not falsely claimed to be an unwind of the saved CPU context. A preopened
write-through file can retain evidence even when the process exits before the
worker copies it to `HaloMCCVR.log`. Normal rendering/firing performs no file
I/O for this probe. The observer always returns `EXCEPTION_CONTINUE_SEARCH`,
does not modify the exception/context, and preserves Win32 last error.

Only the first occurrence at each of up to 64 instruction addresses is saved.
Ordinary C++ exceptions, breakpoints and stack overflow are excluded. A recorded
first-chance fault may be caught normally and is **not itself a crash verdict**.
Network disconnection, deliberate abort, GPU/device loss or other failures may
produce no supported hardware fault. Absence of records is not proof of safety.

## Validation and outstanding work

Release build and all **61 CTest suites** pass. Tests include real Windows
exception dispatch, concurrent same-site faults, source/prior-log preservation,
saved context serialization, worker mirroring and continued native handling.
H3 production shot tests also check settings-off faults and the new counters.
Reach consistency check passes. Pinned contact and world-collision verifiers
pass; all seven H2/H3 dual/muzzle and ODST/Reach/H4 muzzle binding verifiers
pass. No MCC process was launched or modified.

The reported firing failure's underlying cause is still unproven. Needed next:
runtime reproduction with this evidence, target-title headset and H3 regression,
both peers/roles, checkpoints and longer sessions. Both editions remain
supported; neither edition has new headset acceptance. Accepted build pointer
is unchanged. CE multiplayer menu/grenade work was subsequently added by the
user before packaging; see CE-MULTIPLAYER-AUDIT-2026-09-19.md. Menu presentation
has a local correction; multiplayer grenade serialization remains unfinished.
