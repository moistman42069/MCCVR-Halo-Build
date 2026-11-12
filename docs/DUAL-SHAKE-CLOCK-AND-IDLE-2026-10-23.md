# Dual shake reload: pose clock and idle scheduling correction

## Headset result

User tested source `7c05a6a6e7105c21a124c4bc4be198e7a758e105` and confirms
working Halo 2 Anniversary reload, but no shake reload in Halo 2 Classic or
Halo 3, including option toggles and different weapons. Preserve the confirmed
Anniversary behavior; ODST work remains present but is not modified at runtime
by this correction.

Supplied `HaloMCCVRDUALRELOAD.log` SHA-256:
`c939e7adcef6a6a12529d7c1852600867236e5e26910c2b6da289aa6a327cbe4`.
The log identifies Steam and the compiled source above. No DLL hash supplied.

H2 switches to Classic at 10:23:16.547. Native owner ticks continue, but requests
stay at 6 until returning to Anniversary at 10:23:52.504; they then advance to
8. The later Classic interval also produces no additional requests. This places
the observed Classic failure upstream of native reload acceptance, not at a
missing binding. No separate Classic reload ABI is indicated.

H3 has bindingReady=1, fault=0 and requests=0 throughout. Native owner ticks stop
for long intervals (e.g. 1260 remains unchanged from 10:29:15 through 10:29:50).
A 150 ms owner receipt cannot stay valid on this schedule. The installed weapon
callback was incorrectly treated as an always-running simulation tick.

## Reproduced timing defect

The shared Needler detector seeds/resets on `now <= last_`. Dual reload fed it
`GetTickCount64()`, whose Windows resolution can be roughly 10–16 ms. A 90/120 Hz
pose stream can contain repeated wall-clock values, resetting a partially
completed shake. Lower-rate rendering can avoid those repeats. Previous tests
used ideal 8/11/14 ms increments and therefore did not represent that timer.

New tests feed the identical physical stroke through a simulated 16 ms clock
and through the actual pose-time domain. At 8/11/14 ms frame intervals the old
clock repeatedly resets and produces zero reloads; pose timestamps produce both
expected requests. Lower-rate 22 ms samples also succeed with pose timestamps.
This is a demonstrated code defect consistent with the renderer-dependent user
result. The old log did not record input timestamps, so it cannot prove this was
the ONLY Classic blocker on the user's machine.

The candidate passes the OpenXR predicted timestamp used for the head/controller
locates, converted from nanoseconds to milliseconds, to the dual detector only.
Wall-clock receipt/request expiry remains on GetTickCount64; clock domains are
never compared. Needler's existing behavior, thresholds, manual reload and gun
animation/ammo rules are unchanged. No speculative Classic inventory or native
reload changes were made.

## Halo 3 scheduling correction

Separate rollback `c162db87` disables the failed weapon-callback invocation while
retaining its code. The replacement invokes the feature after the existing
native unit-action update, through `Halo3ContactUpdateDetour`. This is outside
contact-melee option/fault admission. The original unit updater still runs first;
its original return and callback retirement guards are preserved.

Own-title evidence already retained in `PHYSICAL-CONTACT-MELEE-WORK.md`:
H3EK `A7E9D0` updates four unit action slots. Retail homolog `379B30` is called
from `39A5AD`; the existing installer verifies a unique signature and caller
edge. The supplied pinned H3 kit/retail binaries were rechecked for this change.
The loop handles idle slots; a weapon's trigger need not be active to enter this
outer updater. No additional hook or guessed address is installed.

The new helper requires the updated unit to equal the native local player,
rejects seated/single-weapon/unknown owners, and uses the existing full-salt H3
inventory reader. Both titles still use the same verified per-weapon reload
requests and generation/ownership checks. If the existing unit-action hook is
unavailable, the feature receives no receipts and remains stock. Installing a
separate replacement hook is not attempted.

## Diagnostics and validation

Cold-worker `Dual shake input` reports samples, ready samples, gestures, queued
requests, repeated coarse clock values, and tracking/owner/action/busy-hand
blocks. The existing native tick/request/accepted/fault counters remain. Counters
are cumulative per title; input hooks only update atomics and do not log.
These distinguish a remaining gate or detection problem from native rejection.

- 1,350 gesture/channel checks passed with GCC C++20 warnings-as-errors and
  ASan/UBSan, including the new coarse-clock regression.
- 125 tests of the production H3 unit-tick helper passed with the same settings:
  continuous idle owner receipts for two seconds with no weapon-trigger calls,
  secondary request consumption, foreign/seated/single/disabled/unbound guards,
  weapon-salt replacement and expiry when updates stop.
- Portable helper tests substitute ordinary C++ catch syntax for SEH on Linux;
  no Windows access-violation handling is claimed tested. MSVC compiles the
  actual SEH syntax in this test.
- Four-binary read-only verifier passes identities, native request routing,
  existing reload signatures, H3 unit-update signature/caller and kit loop.
- Full Windows DLL/CTest and headset confirmation remain outstanding. No DLL
  package or accepted-pointer advance. User compiles source on the authorized
  branch. The existing CI remains pinned to previous source.

A build-only dependency correction also makes the retained ODST fixture
regenerate when its Mythic evidence JSON changes; no ODST runtime edit.

Headset priority: H2 Classic, H2 Anniversary regression, then H3. Partially empty
both guns, release triggers, shake each hand separately, then both. In H3 test
while standing idle after waiting several seconds. Repeat a Classic/Anniversary
switch without changing weapons. Provide the new log even on partial success.
