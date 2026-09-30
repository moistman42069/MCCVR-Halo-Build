# September 30 renewed implementation evidence

Uncommitted continuation after `6b3f38b`, not a released or headset-accepted
candidate. The accepted source pointer remains unchanged. No MCC launch,
installation or game-folder writes are part of these checks.

## Reference behavior and implementation

### Hand-specific haptics work

The requested behavior refines Halo 3's existing authored feedback: recoil on
the weapon's holding hand, shared recoil only for an engaged support grip,
independent dual-wield feedback, and separate general damage/vehicle feedback.
The current blended XInput bridge cannot identify the source of an envelope;
its motor bands are frequencies, not VR hand identities. CE now has the first
native source-tagged recoil adapter. H2 now has its own implemented adapter
and production tests; H4's production adapter also passes its focused tests.
ODST's own production adapter now passes its fixture and pinned proof. H3's
admitted-shot and separate charging paths pass their production fixture (31
checks) and 21 pinned checks. Reach's native binding work remains active.
See HALO2-WEAPON-HAPTICS-2026-09-30.md for H2's separate native proof.

The shared OpenXR consumer now admits each tracked hand independently, stops
a lost hand before the 40 ms reapply throttle, and discards contact pulses for
that lost hand. Global menu/focus/tracking shutdown also clears retained game
rumble so an old pulse cannot replay after recovery. Physical hand routing
still follows the captured handedness. The generated production-function
fixture now passes 603 checks, including both handedness modes, one-controller
tracking loss, immediate stop, contact freshness, menu/focus recovery, independent
primary/secondary recoil, coupled support release and expiration. Release
`halo3xr` builds; CE's production native-routing fixture also passes. Logs:
`out/review-20260930-weapon-haptics-build.log`. No headset acceptance or claim of
complete all-title weapon-specific haptics is implied.

Native private envelopes now retain an XR hand cancellation token, not just a
pending pulse. Tracking/menu/focus/session loss invalidates it so the native
updater cannot recreate old recoil after recovery. Primary and secondary tokens
are independent; support loss does not cancel a tracked primary weapon.
CE's injected native-curve fault and cancellation tests pass. H2's production
fixture passes 57 checks (including real invalid-pointer and owner-read faults)
and its pinned proof passes 35. H4's first production fixture exposed three optional
SEH recovery failures; these are corrected, with 111 production checks and 38
pinned checks passing. Combined DLL rebuild after the latest H2 live-cleanup
refinement passes, as does its cleanup fixture; log:
`out/review-20260930-haptic-final-review-build.log`. The subsequent combined
CE/H2/H3/ODST/H4 and shared-transport DLL build passes in
`out/review-20260930-haptic-cumulative-build.log`; that invocation's separate
avatar-helper target failed on a missing include and is being corrected.
No native adapter has headset acceptance yet; Reach integration remains open.

CE's additional production tests pass for invalid envelope pointers, local-state
read faults and propagation of original native firing exceptions. ODST passes
118 production checks and 47 pinned checks, including native active-queue
eviction, full tag identity and ninth-cue private-bank replacement. H4 retirement
now uses entry/trampoline quiescence before freeing hooks. These latest lifecycle
changes pass the cumulative DLL build above. Reach's corrected opaque source
and curve helper passes 19 checks; that is not a native runtime adapter. Its
previously empty retail matching database has been populated from the pinned
module; own HREK-led matching continues.

Shared pulse packets also retain source/support epochs through consumption.
This rejects a native writer that resumes after XR cancellation and publishes
after the old queue was cleared; it cannot overwrite a newer epoch's peak.
The support epoch is checked separately even when the primary hand remained
tracked. An XR publication gap over 100 ms changes the epoch on recovery.
Deterministic reordered-publication and concurrent writer/clear/consumer tests
pass within the 603-check fixture and the cumulative DLL build above.

H3 cross-review found that the first trigger-state scope missed ordinary barrel
fire; its corrected own-kit admitted-shot adapter is now implemented. Raw pinned evaluator comparisons
also disproved interpreting Ghidra's `-NAN` text as a particular float sentinel:
the actual instructions compare integer `UINT32_MAX`. Corrected integer queue
identity, full native-queue retirement, separate charging, stale support grip
and failed-cleanup retry pass the production tests. H3 headset acceptance and
all-title runtime confirmation remain outstanding.

CE source-attribution evidence:
official HCEEK trigger update `8F5EC0` constructs a firing-damage event, adds
flag 8 and calls generic damage `7F4440` at `8F6A94`. Its matched retail
`B78DE8` calls `B9EA28` at `B79871` (return `B79876`). This occurs outside
the inner projectile-fire hook, so that hook alone is insufficient to scope
authored recoil. The generic local-player effects helper `52E640` invokes
the vibration allocator `594C90`, whose matched retail allocator is `B9B518`
(caller `BAB8A0` inside `BAB708`). It copies/scales a 0x3C authored two-band
envelope into one of eight slots. HCEEK `594EA0` evaluates envelopes using
curve helper `7B4140`, amplitudes/durations at +0/+4 and +0x14/+0x18, and
ages at player-record +0x1E0. Retail evaluator `B9B69C` matches that behavior
and invokes curve helper `C72528`, but also adds global scripted vibration;
calling it against private recoil slots would incorrectly include that general
effect. These findings support separate source-tagged envelopes, not a guessed
time window that would reroute explosions. Logs are the ignored
`out/ce-haptic-*-20260930.txt` dumps.

The optional CE adapter now scopes the exact native trigger's full weapon
handle, then scopes only the verified firing-damage call for the matching local
unit/current weapon. At the matching vibration allocator call it captures the
authored envelope into eight private slots. All other effects and allocations
call the original native paths. The native vibration-update hook evaluates
private envelopes with CE's own verified curve helper and 1/30-second age step;
general damage, scripted vibration and vehicle rumble remain native. Invalid
definitions or unavailable hooks leave stock vibration and the VR camera intact.
An expired owner/weapon/generation or paused input retires private envelopes.
No native gameplay/damage transaction is changed by this routing.

The shared transport separates primary, secondary and coupled-support peaks,
with title/generation and 100 ms freshness. The XR consumer maps roles through
handedness and rechecks actual two-hand aim before sharing recoil. Support release
stops that channel before the reapply throttle. `tools/verify-ce-weapon-haptics.py`
passes 19 official-kit/retail checks. The CE fixture executes the production
trigger, damage, envelope and update wrappers with native services mocked,
covering authored decay, local/full-handle identity, foreign damage, unsupported
curves and scope restoration. Native output in a headset remains unaccepted.

### Launcher-matched menu presentation

The VR menu now uses the launcher's navy/cyan palette, pale text, square panels
and unchanged OFL Oxanium font for navigation and headings. Body text retains
the existing font and scale. Font bytes are embedded at configure time, so the
menu adds no disk read or system-font dependency. `menu_style_preview` is an
optional offline D3D11/WARP tool using the production style and font atlas; its
render at `out/review-20260930-menu-style.bmp` was inspected for text clipping,
contrast and navigation width. It is a style preview, not a headset or full
in-game interaction test. Menu input, panel dragging and per-title controls
remain on their existing paths.

The user's additional full-body avatar IK and finger-posing request is distinct
from arm articulation and native-body visibility. CircuitLord's official
[installer description](https://github.com/CircuitLord/CircuitLordVRModInstaller)
confirms full-body IK as the requested Titanfall reference; it does not provide
a Halo rig or a validated Halo native consumer. Existing MCC input provides
grip/trigger values but no general finger-joint tracking integration. Any added
pose must use exact frame/hand ownership, respect held-weapon poses and validate
each title's skeleton rather than copying bone indices between games.

Halo 3 remains the experience reference: tracked hands retain their controller
targets, optional arms bend toward those targets, ordinary rendering survives
an optional feature failure, and settings changes preserve working camera
ownership. New title paths must establish their own native data contracts.

### Live rendering size and optional DLSS

The production wrapper already supported the E/CNN model as preset 6, but the
menu omitted it and config loading clamped it to 5 (M). E is now selectable and
survives save/load; the real config roundtrip fixture covers the previously
silent model replacement. This exposes an existing supported choice, without
changing defaults or claiming that one model is fastest on every GPU.

The resize hooks previously depended on `fit_desktop_window`. They now initialize
independently; physical window fitting remains optional. A plan still travels
through MCC's existing window-thread resize transaction, observes loading
deferral, and retains the timeout rollback. The slider settles for 500 ms before
dispatch; live does not mean that native resource recreation takes zero time.

A failed dimension pair previously suppressed that same size until restart.
Explicit settings changes, title-generation changes and the menu retry now clear
the suppression. The pair uses one atomic value instead of separately published
width and height. Missing required resize hooks still preserve native sizes.

The bounded depth-view table also previously retained every entry until resize
or title teardown. Once full, replacement buffers could never be learned even
when all retained buffers belonged to an old scene. Under table pressure, frame
retirement now releases entries unused during that frame, retaining current-eye
views and compacting ownership. Recurring binds remain pointer-only; retirement
adds no COM work to native draw/palette hooks. This is a source-proven lifetime
correction, not proof of the cause of the reported H2 Anniversary one-second
failure. More than eight simultaneously recurring fallback views can still
exhaust this bounded table; explicit scene-bound depth has separate ownership.

`tools/dlss-gpu-smoke.cpp` uses the production NGX wrapper on an isolated D3D11
device, without MCC or a headset. On September 30 it passed 90 evaluations on
the NVIDIA RTX A4500, driver 32.0.16.1088: both eyes, all five quality modes,
768x512 -> 1024x682 -> 768x512 outputs, per-eye resource recreation, fresh
eye-specific color, viewport restoration and shutdown/reinitialization. Runtime
310.7.0.0 SHA-256:
`BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E`.
Logs remain in ignored `out/review-20260930-gpu-results.log` and
`out/dlss-gpu-smoke-20260930/dlss-gpu-smoke.log`.

The optional `--benchmark` run additionally passed 160 evaluations at 3292x2374
output across all modes. Twelve GPU-timestamp samples per eye/mode followed
warmup. Means ranged 1.411-3.755 ms and observed p95 ranged 2.247-8.717 ms on
this shared RTX A4500 environment. These short synthetic samples measure only
NGX evaluation, exclude MCC rendering/copies/motion preparation, and are noisy;
they neither rank game quality modes nor predict headset frame rates. Results:
`out/review-20260930-gpu-benchmark.log`. They establish that upscaling has a GPU
cost that must be measured alongside saved native rendering time.

This checks wrapper execution on this GPU, not native per-title motion/depth
quality, headset performance, or all-title transition acceptance. Pancreations'
foundation and martysl1's Reach depth-path contribution remain credited in the
release inventory; an older source archive was not substituted for current code.

### CE guarded roomscale body following

Match Halo 3's guarded native movement transport without writing unit position.
CE's existing native local/on-foot/first-person admission now reaches that
transport in both graphics modes. The heading uses the same horizontal tracking
frame as CE's eye transform, rather than mixing camera pitch into the movement
axes. Anniversary prepares the reference once at the first primary eye append,
after recovering the untracked native camera through already-verified fields;
both eyes use that reference. Physical-crouch correction is applied afterward
and is not persisted into the horizontal calibration reference.

Production CE runtime tests cover pitched native camera recovery, shared
reference movement, stale revision and presentation blocking. Common input
tests now exercise all six title admissions, including CE. This does not solve
simultaneous stick/physical follow: the shared native-motion attribution still
waits for its existing quiet interval. Platform motion, moving co-op seats and
that simultaneous-motion requirement remain open.

### Intentional hand-near-head HUD reveal

`hud_reveal_near_head` defaults off; `hud_reveal_radius_m` defaults to 0.22 m and
is clamped to 0.10-0.40 m. The empty support hand must leave the area before
arming, then dwell inside for 200 ms. Four-centimetre release hysteresis prevents
flicker. A held grip/trigger, support attachment, dual wield, magazine ownership,
menu, tracking/focus interruption or changed tracking/title identity cancels the
gesture. The saved hide-HUD setting does not change and no action is consumed.
Rendering reads one bounded-freshness atomic publication. Gesture and config
roundtrip tests passed. This implements the reveal request, not the separate
relaxed-offhand pose proposal.

### Arms, full body and native actions

These are active integration/research tasks. The existing saved `floating_hands`
key is presented as inverse "Show arms with hands", separately from `body_wip`
("Show full body"). No conflicting duplicate config aliases were introduced.
Refer to the agent evidence and final implementation inventory for title-level
coverage after integration; absence of an accepted native full-body consumer is
not represented as support. H2 rejected arm candidates must not be re-enabled
without new evidence.

The additional title seam review found a safe Reach extension. Its visible
palette is rebuilt from the exact `untouchedLive` snapshot and pair-frozen
right wrist target. During support grip, the current rigid fallback transforms
the authored left support wrist by `desiredRight * inverse(stockRight)`.
Reach's optional arm solve now derives that same final wrist endpoint, then
solves both chains as one transaction. If either endpoint or arm solve fails,
it skips the candidate/cache and rebuilds the complete palette with the
existing rigid right-hand/gun path. The committed-solve gate controls whether
arm branches remain visible. Core tests cover rigid endpoint equivalence,
inverse failure and visibility failure cases; headset confirmation remains
pending.

Halo 2's active C-H2-63 path independently owns controller wrists and gun pose,
while earlier arm-transaction candidates were rejected. H2EK's Chief and
Elite first-person render-model tags both have `base -> upperarm -> forearm ->
hand`, with named authored joints and matching ~0.094 / ~0.103 native-unit
links. The existing live binding reads the current graph's hand flags and
parent table and cross-checks the engine's wrist lookup; it now derives
shoulder/elbow indices from that same per-packet graph. The optional single
weapon final-packet mode retains those four joints, analytically solves both
arms toward the already committed wrists, and commits with the existing hands
and gun packet. Any endpoint/chain failure restores the legacy hidden-arm
presentation while keeping the proven controller hands and gun. It is selected
only when the player shows arms with hands and arm IK is enabled; it never uses
the rejected interpolator paths. Dual-wield still takes the previously tested
safe hidden-arm route because its two weapon-owned source chains need a separate
verified transaction.

Halo 4's official H4EK body identity is now encoded as the exact
`storm_masterchief` checksum `0x17010100` plus 120 nodes. A non-mutating retail
observer freezes the semantic local unit only when the existing owner reader
and first-person agreement both succeed on the exact Storm hands record; it
then counts exact flag-0 body identity and object-index matches. No retail run
has supplied these counters yet, so native-body visibility remains stock.
Even a same-unit match would establish ownership only; region-level safety and
headset validation are still required before a visibility control can affect
that palette.

The old H3 `body_wip` path was not a proven full-body reference. It forces
`render_first_person=0` as part of a guessed combination of debug switches,
which can conflict with tracked FP hands. It also used `int32_t` writes for
type-5 Boolean slots. Official H3EK places `debug_first_person_models` at
`417A951`, immediately beside `debug_first_person_hide_base` at `417A952`;
the field is one byte. Retail records at `8A49B0`, `8A95B8` and `8A7518` have
null value pointers on disk, so even runtime availability needs verification.

The legacy switch is now type-checked, uses guarded byte reads/writes, and is
bound to the owning H3 generation. A failed write attempts to restore the other
original bytes and disables only the switch, with a worker-side diagnostic.
Its native camera hook no longer logs on toggle. The production fixture checks
neighboring-byte integrity, on/off restoration, stale-title/generation rejection
and partial-write failure. This is a safety correction, not full-body completion.
Read-only kit/decompile outputs remain under `out/review-20260930-*-body-*` and
`out/review-20260930-*-debugvars.txt`.

All six titles now have native action isolation against their own verified
input converter/player-control paths, with 255 production-hook fixture checks
and 172 pinned kit/retail checks. These paths keep native input, held counters,
consumption, ownership and optional-feature retirement separate. Runtime
acceptance remains pending. The earlier conclusion that H3 cannot separate
Reload/Use at its abstract reader was premature: the kit's player-control
consumer reads a dedicated reload action that its UI glyph path did not expose.
That action is now matched in retail and integrated in H3's own adapter.
Do not generalize a UI binding identity to the gameplay action contract.

### CE persistent vehicle profiles

The current seated-owner reader now resolves the vehicle definition's own
model reference and fingerprints its bounded ordered model-node names. This
feeds the existing per-title/model/seat camera profile store and menu. Neither
the object handle, datum index nor cache address enters the saved key. Models
with identical ordered node names share a profile; this is not a unique key
for every custom tag variant. Unreadable, changing or oversized definitions
fall back to the existing per-game camera offsets without disabling vehicles.

The native CE consumers at AF5EC8 and B2A1AC match the HCEEK object-to-mod2
and model-node paths. Cold admission verifies unique executable signatures,
unwind entries and node-table operands. Guarded reads revalidate the cache,
definition, model, node names, seat, object and generation before publishing.
The production fixture covers datum/cache relocation, differing models,
unreadable memory, malformed names/counts, optional-proof failure and a seat
change during lookup. Pinned binding verification passes 15 checks.

CE controls retirement also previously passed nine callback ranges to a shared
helper limited to eight. The helper now admits up to eighteen bounded stack
ranges; CE explicitly includes the seated-owner wrapper, making ten. The
fixture validates all ten actual compiled unwind ranges on both retirement
attempts. No game installation or headset acceptance accompanies these checks.

### Additional DLSS model validation

The isolated GPU smoke tool's `--all-models` mode passed 42 evaluations:
Default, F, J, K, L, M and E, both eyes, three frames each, at Quality 1024x682.
This adds execution coverage for every selectable model on the test RTX A4500.
It does not establish all-title motion-vector quality or compare model speed.
