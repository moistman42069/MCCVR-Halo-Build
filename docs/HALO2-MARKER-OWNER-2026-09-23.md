# H2 firing marker owner and muzzle-flash fallback

Status: implemented and checked offline; headset/co-op acceptance pending.
This does not advance `CURRENT-STATE.md`.

## Report matched to the actual source

Godoy's edited message `1551026756096495618` reports the left dual plasma gun
aiming left while spawning from the right gun, including obstruction at the
wrong origin. `out/community-audit-20260923/godoy-h2-20260919.log` identifies
source `f53f0bd750408b8d40106f92098ff09c46edd568`, Microsoft Store. Its barrel
aim and independent-dual settings are enabled. The aggregate log contains
1,720 applied / 10,315 refused barrel corrections and 1,688 primary / 32
secondary independent rays. Those counts establish partial admission; they
do not identify the reason for every refusal.

The relevant H2 firing and palette publication source was unchanged between
that build and this investigation. The publication already uses different
primary/secondary matrices and full weapon handles after final palette commit.
The independent direction-only path deliberately retains native origins.
Vixel's separate f53f0bd log has barrel aim disabled, so this barrel correction
cannot be presented as a demonstrated solution to that report.

## Native owner selection

Official H2EK `halo2_tag_test.exe` SHA-256:
`D0B71186D3948C48DDD02E2CCB88FA13E77E25A3D8F7FA60922F23A2A0073E36`.
Pinned retail `halo2.dll` SHA-256:
`DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946`.

The kit firing function `49C960` calls selector `4A0F50`, then marker wrapper
`46F630` and resolver `467EB0`. The selector gets the weapon object/definition,
calls the existing native predicate `4704F0`, and returns either the weapon
handle or the weapon object's full parent handle at `+14`. It chooses the
parent when the predicate is true or the model is NONE, provided the parent
itself is not NONE. The predicate is not given an invented semantic name here.

Retail independently matches that flow at `8E8990`: object accessor `8D7000`,
definition table `15E4B30`/base `15E4B38`, model field `+38`, predicate `8D7150`
and parent `+14`. The firing call edge `8E4B94 -> 8E8990` places the selector's
return in ECX before `8E4BAA -> 8D6570 -> 8D11A0`. Thus a legitimate marker
request can name the owned biped rather than the currently firing weapon.

The old adapter rejected that case with `object != request.weapon` before it
could apply a proven per-slot muzzle receipt. `halo2_muzzle_shots.inl` now also
accepts the exact locally owned unit in the current native firing request.
It retains full salted handle checks, current slot/weapon identity, weapon
parent ownership, on-foot/local owner admission, thread-scoped native query,
generation, time/space, marker count/capacity and target-lease restoration.
Other units, other weapons and unrelated marker calls remain native.

## Verification and limits

- `verify-halo2-muzzle-bindings.py` checks the pinned image, four unique
  marker/selector witnesses, three call edges, both hook unwind entries and
  the actual marker origin/direction consumers.
- `re/test_h2_marker_owner_native.py` executes the actual retail selector in
  24 offline cases: both weapon handles, salted parent/NONE, both model states
  and both native predicate results. Object access and predicate results are
  explicitly modeled services; this is not a live engine test.
- `halomccvr_halo2_independent_shots_tests` passes 3,067 production-path checks.
  The marker fixture now uses distinct left/right origins and tests both
  native parent-owner routes, foreign-owner rejection and existing cleanup.

The repeated `halo2+67BA50` fault is separate: its instruction reads `[rcx-4]`
with RCX zero in the supplied logs. The recorded stack lacks the original
native caller and logging continues afterward. No projectile fix is claimed
to explain that first-chance exception or a campaign disconnect.

## Per-game hide fallback

The previous automatic hide behavior is now explicitly configurable, default
on for H2 and H4. This remains independent of projectile correction. Neither
the new owner admission nor a matching bullet origin proves flash alignment.

H2's existing `76DC90` particle renderer gate suppresses only the engine's
current-user first-person dispatch while the live Classic byte `E70CF8` is
zero. `hide_muzzle_flash_halo2` gates that existing behavior immediately;
Anniversary and world dispatches always remain native. It is a broad local
first-person particle fallback, not a claim to isolate only a flash sprite.

H4's existing suppression has four routes: three local first-person effect
bridges and the separate mode-one particle deny at `27BD36`. Turning only the
bridge off would leave some muzzle particles hidden. The new worker transition
changes the exact three-byte particle sequence and bridge enable together,
with threads frozen, ownership rechecked and any instruction pointer inside
the replacement extent refused. The routes/cave remain installed; ordinary
settings changes do not free native dependencies. A failed transition remains
pending, is logged, retains the previous policy, and does not disarm VR.
Startup also honors `hide_muzzle_flash_halo4=0`.

`muzzle_suppression_tests.cpp` includes the actual H2 detour, H4 worker transition
and effect bridge: 2,071 checks pass, covering lifecycle/renderer/config gates,
stock-call count, native exception callback balance, all 256 H4 descriptor
flags, repeated toggles, distinct native byte sequences, foreign byte changes,
freeze/context failures and interior-instruction refusal. The fixture models
the thread snapshot; real in-headset switching still needs acceptance.

No working hide fallback is claimed for CE/H3/ODST/Reach. In particular,
Reach's dormant CHUD heuristic hid the grenade indicator, and the prior broad
mode gate was explicitly rejected in headset testing. Those experiments stay
disabled. H3's existing aligned muzzle presentation remains the experience
reference; these H2/H4 controls provide the requested explicit fallback.
