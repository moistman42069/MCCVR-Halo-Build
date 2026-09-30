# CE gun alignment after loading: investigation and recovery hardening

The user identifies gun misalignment after level loads, in both Original and
Anniversary. This is not a report of world/horizon tilt. Exact weapon, positional
versus rotational error, affected build and mission/checkpoint sequence are not
yet established. Retain this as an open headset reproduction.

## Evidence checked

The September 23/30 saved community audits do not identify this exact message.
The nearby Godoy CE new-mission report is a crash, not proof of gun alignment
failure. Its retained log index names f53f0bd, Store, SteamVR 2.17.10 and a
holographic controller profile; those details cannot be assigned to this report.
Live Discord access was attempted twice, including a tool reset, but the browser
tool failed to initialize: "failed to write kernel assets ... path specified".
No new live Discord messages were read.

Source review found the CE palette builder rereads graph/node data per invocation,
stages its edits and revalidates its render context before committing. It does
not retain a map-local bone pointer as a gun alignment cache. Config profiles
use the ordered node identity rather than a map-local tag handle. These checks
do not reproduce or rule out a visual fault in the game.

## Implemented recovery correction

`HaloCE_Poll` previously set only `recenter=true` when the native camera heartbeat
expired. In contrast, explicit `HaloCE_Recenter()` advances `referenceRevision`
and clears the completed-eye receipt. Automatic recovery now calls that same
existing invalidation function before reseeding the tracking reference.

This prevents queued/completed camera work and first-person/aim contexts using
the previous reference from becoming valid again when the recenter flag is
consumed, even with the same title generation, XR space and tracking serial.
The existing camera rearm policy and hook lifetime remain unchanged. No saved
weapon calibration is modified and no engine address or binding is added.

The production runtime fixture reproduced two failures before the source edit:
old completed eyes could regain admission after simulated automatic recovery,
and queued old-reference work could complete after that recovery. Both cases
pass after the edit. Additional coverage checks that both graphics modes reject
pre-load hand/aim contexts after recovery. Continuing XR tracking also provides
existing age/serial safeguards; the fixture intentionally holds those identities
constant to test reference retirement independently. This is not a claim that
the user's actual load sequence follows that exact schedule.

## Verification and remaining work

- Cumulative Release DLL build passed, including the latest H3 avatar safety fixes.
- Seven focused suites passed: CE first-person logic/runtime, Anniversary runtime,
  Classic runtime, controls runtime, and H3 avatar logic/runtime.
- Reach consistency gate and `git diff --check` passed.
- H3 avatar focused checks: 2,317 logic, 45 production runtime, 62 pinned evidence.
- Before-change test log: `out/review-20260930-ce-reentry-red.log` (two failures).
- Build log: `out/review-20260930-ce-reentry-green-build.log`.
- After-change test log: `out/review-20260930-ce-reentry-green-tests.log`.
- DLL SHA-256: `0ED28207B7F5F730399EE6EE7E1BFF025797106CEA6EAA01DA576B7A8730D1C5`.

The current working tree is cumulative and uncommitted; this DLL is not a newly
accepted release or packaged candidate. No installation, game launch or accepted
pointer change occurred. MCC was already running and was left alone.

Headset validation must compare the same weapon before/after a new mission and
checkpoint reload in both graphics modes, retaining the user's existing config.
Record whether the mesh moves relative to the controller, whether the reticle
or actual shot diverges, and whether recentering repairs it. The gun report stays
open until that observation is available; recovery hardening is not proof of its
root cause or a guarantee that it is resolved.
