# Building Halo MCC VR

This builds one cumulative Halo 3 + ODST + Halo: Reach runtime. Every title is a
permanent part of the single Release build -- there is no experimental Reach flag
to toggle. An unproven or mismatched adapter does not acquire VR ownership; once
a transaction is claimed, failure rejects it instead of rerendering a flat path.
Every generated file stays under ignored `out/`; nothing writes to an MCC
installation.

## Requirements

- Windows x64.
- Visual Studio 2022 with **Desktop development with C++**.
- CMake 3.24 or newer.
- Git and network access for the first dependency download.

OpenXR, MinHook, and Dear ImGui are pinned to exact commits in
`CMakeLists.txt`. Fetches are shallow and shared under `out/deps`.

## Build and test

From a Developer PowerShell:

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
```

There is a single `release` preset. It always builds Release x64 with Halo 3,
ODST, and Halo: Reach compiled in permanently. `camscan` is excluded: it is an
opt-in diagnostic with process-memory write modes, not a product target. The
standalone Reach runtime observer is also excluded and must be selected by name;
it is never linked into `halo3xr.dll`. The build identity line reports
`ODST=ON, Reach=ON, ReachRender=ON`.

## How the permanent Reach camera core behaves

Reach ownership is all-or-nothing: no Reach VR hook is enabled until its full
runtime proof passes.
The 50 ms title worker verifies the exact loaded Reach PE/file identity, every
manifest-pinned executable signature and function-body hash, the required caller
and first-person-camera flow edges, and fixed ranges once per sole-title admission
epoch, and the Present path validates
the exact swapchain buffer 0 and builds two private per-eye caches. Only after
that proof and a one-second fresh-camera safety interval does the worker install
six mandatory hooks -- inner/outer stereo, interpolation, visible palette, the
exact first-person camera rebuild, and the HREK-proven class-2 CHUD widget
transaction -- and arm the per-eye transaction. A failed cold proof admits no
Reach VR ownership. After ownership, any mandatory authored-crosshair failure
invalidates the eye pair, disarms that exact title generation, and enters
verified teardown; no partial flat, procedural, transparent, or approximate
Reach VR mode continues. Before ownership arms, an exact inner call may still
execute the untouched engine renderer with the same bounded head-centre
camera/matrices. Once an eye transaction is claimed, failure suppresses that
call and enters teardown; it is never rerun through the flat renderer or
published as a completed Reach stereo pair.
Halo 3 and ODST are never touched. When Reach is armed the log reports `Reach camera core armed` and
`Reach camera bring-up: head tracking, stereo, and 6DOF ON`.

Before Reach computes CPU visibility, the exact normal outer hook reads one
lock-free snapshot containing the matching prepared frame's head, pad, and both
OpenXR views. It applies shared turn and HMD pose once to a local head-centre
camera, widens each asymmetric eye to the symmetric fixed-aspect image Reach
actually rasterizes, then rotates all eight widened corners through their
relative-eye orientations to select a binocular angular-union FOV. The proven stock
frustum/projection helpers rebuild that centre and mirror it to the secondary
render camera that `player_view_compute_visibility` consumes. The original outer
function still runs exactly once. Its bounded player-view camera/matrix state is
rebuilt to the same centre for the coherent pre-ownership path, then that state and only the
proven `0x2A8` workspace camera-pair bytes are restored afterward; the
engine-owned callback remains untouched.

Each eye then derives from that same head centre using the runtime's exact view
offset, cant, and FOV. Reach's single vertical FOV is widened only enough to
cover that eye at the compact camera's validated render aspect; the built
projection matrix is decoded and coverage-checked before rendering. The eye
texture and decoded raster FOV are published only after a successful copy, and
both must match the exact prepared OpenXR frame serial before a projection layer
can be submitted. Active Reach never falls back to a Halo 3/ODST eye cache.
Like accepted Halo 3/ODST, CPU visibility is head-centred and actual eye-origin
translation is applied only to the two rasters. The angular union is conservative
for orientation/FOV but is not claimed to exactly enclose translated near-plane
corners; close doorway/peripheral geometry remains an explicit headset test.

During each admitted eye render, Reach first executes its complete stock
first-person camera rebuild, including the native FOV-control side effects. The
camera detour then substitutes that eye's already validated world compact camera
and `0xC4` derived/projection block in the rebuild's exact nested camera-stack
workspace for the crushed viewmodel pair and reruns Reach's own two-pointer
constant uploader. Only after that world-projection upload returns does the hot
path publish its lock-free execution status for worker-thread logging. This
mirrors the accepted Halo 3/ODST last-writer transaction; non-owned, nested,
screenshot, and teardown paths do not enter the substitution scope.

Reach also consumes the universal `motion_blur` comfort setting with the same
player-visible outcome as the accepted Halo 3/ODST paths. After the pinned
loaded-image proof, the worker resolves Reach's unique native type-6
`motion_blur_scale` and `motion_blur_max` float controls by name and requires
their exact proven retail value slots. Reach's `apply_distortions` pass divides
maximum by scale, so the exact normal stereo boundary preserves/reasserts the
positive authored scale and zeros only the maximum when the VR default
`motion_blur=0` is active. `motion_blur=1` restores both authored values, and
title teardown restores them only after all Reach hooks are quiescent.

While the tracked camera is armed, XInput suppresses stock RX/RY so the game
cannot create a competing look transform under the HMD view. The original
pre-head camera direction is not reused as culling or claimed as projectile aim.
The current unaccepted candidate also carries Reach controller aim and guarded
two-arm IK through the proven interpolation and visible-palette boundaries.

Reach's authored crosshair reuses the Halo 3/ODST native-widget transaction.
The cold worker accepts only one loaded function with the official optimized
HREK entry, unwind extent, five-argument ABI, and descriptor-`+4` class read;
the bridge has no retail-derived RVA or Reclaimer input. It pre-creates the
OpenXR/D3D capture resources before hook installation, so the hot widget hook
only redirects exact scripting class `2`. The configured capture eye supplies
the live authored texture (including friendly-green and hostile-red state), the
other eye suppresses the flat copy, and the compositor admits Reach only with
the matching completed eye pair, live owner, and prepared-frame serial. Every
newly admitted outer attempt first invalidates prior-attempt authored art. If
either eye emits class 2 while the authored crosshair is required, the configured
capture must complete before the final eye can publish; an attempt in which Reach
emits no class 2 intentionally submits no quad. XR acquire/wait, both world-eye
resolves, world-chain release, authored upload and authored-chain release all
require exact `XR_SUCCESS` and precede layer admission. Timeout and
session-loss-pending results abort the complete layer set, enter terminal
session recovery, and pair any begun frame with an empty `xrEndFrame` without
referencing a potentially outstanding image. A proof, capture, resolve, release,
or ownership failure rejects and tears down the complete Reach transaction;
there is no mixed flat, procedural, transparent, or widget-name substitute.

Native HUD layout remains withheld. Reach aim/IK are not release-accepted until the
exact packaged DLL passes the headset matrix and Halo 3/ODST regressions.

For the current Reach headset candidate, the user explicitly approved one
temporary title-specific presentation difference: Reach forces floating hands
regardless of the universal `arm_ik` and `floating_hands` values. Its verified
right hand plus appended held-object range remain right-controller-owned; the
verified left-hand source mask receives its own left-controller wrist delta in
private final-palette scratch, and every non-hand/non-held Reach FP node is
collapsed. Halo 3 and ODST still consume the universal settings unchanged.

Signature scanning, file hashing, resource allocation, critical sections, and
candidate logging stay out of all Reach engine render callbacks. Exact tracking
is published with two fixed slots and lock-free pin/claim atomics; a changing or
stale serial fails immediately. Present performs only the
bounded identity/field snapshot plus `GetBuffer(0)`, device/descriptor capture,
and COM retention described above; eye allocation and proof publication remain
on the worker. Title teardown disables all six Reach hooks, verifies callback and
MinHook relay/wrapper RIP quiescence, then removes trampolines and releases the
retained title module; a failed proof retains state and retries without rearming.

## Verify local Reach evidence

After installing and extracting the official HREK, run the offline preflight:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\reach-preflight.ps1
```

It read-opens the configured installed retail `haloreach.dll`, HREK build tag,
and pinned `reach_tag_test.exe` evidence binary to verify their exact hashes/PE
identities and the retail module's local PE section table. It does not launch
or attach to an MCC process, inject, write memory, change page protection,
install a detour, or copy kit/game assets into the repository.

## Build and package the standalone Reach observer

The Reach observer is a diagnostics-only executable. When explicitly run, it
read-opens an already running anti-cheat-disabled MCC process with only
`PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`; it does not inject, install a
hook, or write game memory. Building or packaging it does not run it.

At runtime the observer records the SHA-256 of its own running executable in
the first log line. It refuses to run from inside the MCC installation, refuses
a log path there, and resolves the process image, observer, install root, and
output parent through file handles to normalized volume-GUID paths. It then
holds the output-parent handle without delete sharing and creates the log
as a single name relative to that exact handle with `NtCreateFile`,
`FILE_CREATE`, read sharing only, and `FILE_OPEN_REPARSE_POINT`. That closes
path-alias/junction, ancestor-rename, and raced-leaf routing, gives the log one
writer, and prevents an existing log from being overwritten. Its self-hash is
also read from the already-open running executable handle, which denies
write/delete sharing and remains retained through the first log line.

To build the observer without packaging or starting MCC:

```powershell
cmake --preset release
cmake --build --preset release --target `
  reach_runtime_observer halomccvr_core_tests
ctest --preset release
```

Commit the intended diagnostic source first, then create a uniquely identified
package:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\package-reach-observer.ps1
```

The packaging command refuses a dirty worktree or a commit that does not
descend from the accepted 0.2.2 runtime source. It also requires the evidence
manifest to retain `runtime_observer.status=IMPLEMENTED_UNRUN`, an empty
`observed_runtime_results` array, false observer proof/hook fields, and false
player-view-transaction proof/hook fields. It configures the normal Release
preset, builds only the observer and core tests, runs the tests and offline
Reach evidence preflight, and rejects observer source that requests anything
other than query/read process access or names a write, injection, debugger, or
process-launch API. It also checks that the executable self-hash remains bound
to the already-open running image, the MCC path refusals use canonical handles,
and the single-writer log remains coupled to handle-relative `FILE_CREATE`.

It stages a new directory such as:

```text
out/diagnostics/1a2b3c4-reach-runtime-observer-20260723-120000000Z/
```

That directory contains `reach-runtime-observer.exe`, the observer runbook,
the license, and `DIAGNOSTIC-MANIFEST.json`. The manifest records the exact
source commit, expected retail Reach module identity, read-only process rights,
file sizes and SHA-256 hashes, observer runtime guards, and `UNRUN` status. The
command never launches or attaches to an MCC process, never runs the observer,
and never writes to the MCC installation. Its offline preflight read-opens the
configured installed `haloreach.dll` and HREK evidence. Build and dependency
outputs may be created elsewhere under ignored `out/`; package staging is
confined to `out/diagnostics`.

Do not copy the observer into MCC. When a runtime capture is explicitly
requested, keep it in its diagnostic package and follow
`REACH-RUNTIME-OBSERVER.md`; packaging alone does not advance Reach proof or
authorize hooks.

## Create a test candidate

Commit the intended source first, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\package-candidate.ps1
```

There are no title switches or alternate candidate presets. The command builds
and tests the one cumulative Release configuration, then records the permanent
Reach preflight, capture, engine-write, camera-core, runtime-hook, nested
first-person camera workspace, and world-projection execution-status fields in
the candidate manifest. After packaging succeeds it automatically invokes
`tools/install-candidate.ps1` for those exact bytes.

**The rebuild is incremental, not clean** - verified 2026-08-06 against
`tools/package-candidate.ps1`, which passes `--clean-first` only when you opt
in with `-Clean`. That is deliberate: a clean rebuild recompiled the whole tree
every time and cost minutes per candidate. Two consequences worth knowing:
a stale object file can in principle survive into a candidate, so pass `-Clean`
after a build-system change; and packaging is not a substitute for checking
that your own incremental build actually produced every target.

The command refuses a dirty worktree. It does **not** run
`tools/check-reach-fp-parity.ps1` — verified 2026-08-06, the packager contains
no invocation of it — yet it still writes
`reach_fp_h3_odst_transaction_parity_gate = $true` into the candidate manifest
(`tools/package-candidate.ps1:167`). **That manifest field therefore asserts a
gate that never ran; do not read it as evidence.** Run the parity script by
hand when you need it. The description below is of what that script checks, not
of anything packaging enforces: it fails if the
rejected single-context, body-only, or separated-live-owner paths reappear, or
if bounded per-transaction contexts, exact source-pointer palette matching,
full-source scratch reconstruction, the appended held-object boundary, or the
exact title-native support-hand weapon-IK bypass/restore, or the repository
parity contract are missing. It then reconfigures, rebuilds, reruns
tests, and creates a new directory such as:

```text
out/candidates/1a2b3c4-reach-fp-parity-20260723-120000000Z/
```

It contains only the DLL, launcher, license, generic manual, and a
`CANDIDATE-MANIFEST.json` with the full commit, base release 0.2.2, ODST and
Reach build states, explicit `reach_controller_input_enabled` and
`reach_runtime_hooks_enabled` states, required
`reach_fp_nested_camera_workspace=true` and
`reach_fp_world_projection_execution_status=true` gates, exact file sizes, and
SHA-256 hashes. The installer rejects the package if either projection-isolation
gate is absent or false. It never reuses a candidate directory and never labels
rebuilt bytes as release 0.2.2.

Deployment is automatic after the clean package completes. The installer accepts
only the new manifest-backed directory under `out/candidates`, requires MCC and
the launcher closed, preserves the prior DLL/launcher/log/config under
`out/deploy-backups`, verifies staged and installed hashes, and leaves
`halomccvr.cfg` unchanged. It never launches MCC. A rebuild remains unaccepted
until its exact installed hash passes a headset test.

## Inspect the published 0.2.2 source

Use a separate clean clone or worktree so historical outputs cannot mix with
active candidates:

```powershell
git switch --detach MCC_VR_ALPHA_0.2.2
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The release tag is `e2c049e5c3b98ce466f6072da4e0aa55ccc88e10`; its headset-tested
runtime source is `3a2a11bfc66b36e70f60282e91c9d5436f2e18d1`. Exact dependency
commits and published hashes are recorded in `releases/0.2.2/manifest.json`.

## Exact bytes

The exact headset-accepted DLL and launcher come from the official binary asset
`MCC_VR_ALPHA_0.2.2.zip`, SHA-256
`43E52AEF5A2D1647A8F3AE6AEFDB6C22F0C67C7AA06FD70D327FB3E00ACF5DCC`.
The ignored local exact-byte copy is `dist/MCC_VR_ALPHA_0.2.2.zip`. A local build
gets a new hash because compile time and toolchain output affect its bytes.
Never substitute a rebuild for accepted artifacts or overwrite the preserved
ZIP.
