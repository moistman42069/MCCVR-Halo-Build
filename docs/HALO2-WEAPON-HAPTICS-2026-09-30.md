# Halo 2 authored recoil by holding hand

Implemented in the uncommitted September 30 continuation; no headset acceptance.
The target experience refines Halo 3's existing vibration: local weapon recoil
belongs to the hand holding that weapon, with an engaged support hand receiving
coupled recoil. Dual weapons have independent envelopes. General damage,
scripted vibration, and vehicle feedback keep the native mixed-rumble path.

## Own-kit and retail proof

Discovery used official H2EK `halo2_tag_test.exe`, SHA-256
`D0B71186D3948C48DDD02E2CCB88FA13E77E25A3D8F7FA60922F23A2A0073E36`.
Retail matching used pinned `halo2.dll`, SHA-256
`DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946`.
Addresses below are kit virtual addresses and retail RVAs respectively.

| Boundary | H2EK | Retail |
|---|---:|---:|
| Outer weapon firing/effects transaction | 89E5A0 | 8E6180 |
| Owner recoil helper called after native firing admission | 884420 | 8F5A20 |
| General player-effects worker | 499490 | 6D6F40 |
| Vibration-only enqueue | 4D3150 | 73B890 |
| Native vibration update | 4D3280 | 73B9F0 |
| Native mixed-envelope evaluator | 4D2AD0 | 73B300 |
| Authored mapping evaluator | 5CF760 | 7774C0 |

The existing projectile hook at 8E4940 does not enclose the outer recoil helper.
The verified native chain is 8E6B39 -> 8F5A20, 8F5B28 -> 6D6F40, and
6D71EA -> 73B890. Enqueue's fourth argument is a float in XMM3; the curve
helper receives the mapping pointer in RCX and normalized elapsed time/range
in XMM1/XMM2. These are independently checked instruction boundaries, not
decompiler-inferred ABI guesses.

Native per-user storage is 0x88 bytes: eight 12-byte tag/row/scale entries,
eight ages at +0x60, then persistent motor offsets at +0x80/+0x84. Damage
effect definitions have a row block at +0x6C/+0x70. Retail rows are 0x4C bytes
(the editing-kit row is 0x58); two duration/mapping bands begin at +0x24,
stride 12. Block addresses select the native normal/alternate bases through
bit 31. The private adapter copies the two authored bands and invokes the
native mapping helper. It does not reuse the mixed evaluator for its private
bank because that evaluator also adds global scripted rumble.

The native updater optionally accumulates a configured time interval and only
then evaluates users. The adapter brackets that update and observes only its
actual user-zero evaluator call. It applies the same quantized delta once;
it does not substitute a guessed frame rate or wall-clock decay.

Evidence dumps are preserved under `out/h2-haptic-*-20260930.txt`.
`tools/verify-halo2-weapon-haptics.py` passes 35 own-kit/pinned checks, including
unique cold patterns, call edges, layout/ABI witnesses, and RIP-relative globals.

## Runtime and failure boundaries

Five independent optional hooks scope the outer weapon, exact owner-effect
call, vibration-only enqueue, native update and native evaluator. Full salted
weapon handles and the existing verified local first-person inventory reader
resolve primary/secondary roles. An unrelated or remote effect retains its
original enqueue. The original effects and game update always run exactly once.
Eight private voices per role preserve authored overlap, duration, curve and
scale; the shared OpenXR consumer applies handedness and intensity.

Each voice carries the XR hand's cancellation token. Lost tracking, menu/focus
loss, session retirement and token changes retire prior recoil rather than
replaying it on recovery. Stale owner/generation or a missed native update also
retires voices. A native curve/read fault disables this optional adapter and
returns subsequent vibration to stock, without touching camera ownership.
Original native exceptions propagate after TLS/callback cleanup.

The live observer cleanup now retires these hooks before removing the weapon
reader or clearing module dependencies. No game files are modified.

The compiled production-body fixture passes 57 checks, covering one-handed,
secondary and concurrent dual recoil, support, foreign ownership, unchanged
general output, native timing, stale/paused/token cancellation, stock fallback
and exception cleanup, including actual invalid definition pointers and owner
read faults. A held secondary weapon prevents primary support coupling even
when that secondary has no active recoil. The shared OpenXR fixture passes 545 checks including
handedness and cancellation tokens. These are offline checks, not proof of
Classic/Anniversary headset behavior or co-op acceptance. The cumulative DLL
rebuild after the live-cleanup refinement passes, as does its production cleanup
fixture. Build log: `out/review-20260930-haptic-final-review-build.log`.
