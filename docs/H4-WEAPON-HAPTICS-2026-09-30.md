# Halo 4 native weapon haptics, September 30

Status: final Release fixture passes 81 checks; focused CTest passes. Pinned
proof passes 35 checks. The initial adapter DLL built; the cumulative DLL
rebuild with the final SEH/lifecycle refinements is pending the separate H3
compile correction. No game launch, installation or headset acceptance. This document does
not advance the accepted build pointer.

The requested behavior follows actual weapon ownership: primary and secondary
weapon recoil goes to its holding hand, primary support coupling follows the
current support relationship, and shared transport applies left-handed routing.
General damage, vehicle and other XInput rumble retain their existing transport.

## Own-title source proof

Official H4EK `halo4_tag_test.exe` SHA-256:
`B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6`.
Pinned retail `halo4.dll` SHA-256:
`7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84`.
Both supported MCC editions use the same engine code, as separately documented
in MCC-EDITIONS-EVIDENCE.md.

| Role | H4EK RVA | Retail RVA |
| --- | --- | --- |
| Barrel firing update | E9D160 | 614FC0 |
| Admitted firing response scope | E9DD50 | 6161DC |
| Unit authored response helper | E78230 | 603DE0 |
| Player effects worker | 4982A0 | 1E6694 |
| Rumble-only enqueue | 22ED50 | 145E80 |
| Rumble update | 22F0F0 | 145C48 |
| Source attenuation update | 499A50 | 1E7B64 |
| Native rumble evaluation | 22E4F0 | 1460C8 |
| Authored mapping evaluation | 3C9720 | 234E08 then 235460 |

The firing update calls the response scope only after native firing admission:
kit call E9DA88, retail call 61539E after `test r13b,r13b` / conditional skip.
Empty-fire uses its separate native path. The scope's first unit response call
is the owner/controlling parent (kit E9DDE0, retail 616232, return 616237).
Its second call is passenger feedback (kit E9DE99, retail 616324); it is excluded.
The owned unit response can contain native drdf or jpt effects; its camera,
sound, visual and player effects still execute through the original function.
No hand pulse is fabricated solely because a trigger is held.

The player-effects worker calls the standalone rumble enqueue at retail 1E69F0
(return 1E69F5). H4 differs from an inlined allocator: intercepting this existing
function avoids writing or restoring any native queue slots. The worker's
numeric flag 8 suppresses only rumble, but this implementation does not change
that flag or skip the worker. An earlier firing-tail lead was rejected: the
outer fire function's E9C712 damage call is AOE damage excluding the shooter,
not proof of local weapon recoil.

## Native envelopes and guarded ownership

Native rumble has eight slots, each 0x34 bytes: tag+0, scalar+4, source definition
0x2C bytes at+8. Source+4 is its output user; source+28 is attenuation. Ages
live at native user state+1B0. Each rumble tag has two 0x18-byte motor bands,
each beginning with duration followed by a native mapping definition.

Retail ABI proves enqueue scale in XMM2. The private evaluator preserves native
`age / duration`, source attenuation and scalar, calls mapping 234E08 with
XMM1 normalized age/XMM2=1, then passes its XMM0 result as XMM1 to 235460.
It sums/clamps motor bands and uses the existing 65%/35% VR motor weighting.
It deliberately excludes forced/general motor values and global camera rumble
which the original native updater continues to send through generic transport.

The read-only owner proof uses the already title-proven full-salt object and
inventory role readers: H4 FP TLS +6A0, first output record active flag 2/unit +4,
held role record stride 2EC8/weapon +6C, unit roles 63A/63B and four handles +640.
It verifies weapon owner +624 (471/480 fallback), exact FP held weapon, no parent
24, no controlling parent694, active generation, gameplay/cinematic state,
current stereo/controller ownership and input user 0. It is independent of the
`gun_barrel_aim` setting. Other users, passengers, unknown ownership and vehicles
retain stock rumble. Native original exceptions propagate.

The private eight-voice bank uses a nonblocking atomic writer. Contention or
capture validation failure falls back to the original enqueue. Each voice
records owner, weapon, semantic hand, title generation and shared XR haptic
cancellation token. Changed token, lost ownership, stale >100 ms, invalid native
delta, menu/loading or generation change retires its tail. No file I/O,
allocation, signatures, logging or blocking locks run in these callbacks.
Private faults disable only this adapter; a deferred report logs and retires its
hooks independently of the camera. Teardown retains dependencies if callbacks
or native hooks still require a cleanup retry.

## Verification and limits

`tools/verify-halo4-weapon-haptics.py`: 35 pinned checks pass, covering both
hashes, official source edges, retail source/consumer/evaluation edges, ABI,
queue layout, tag references and all 12 unique runtime signatures.

`tests/halo4_weapon_haptics_tests.cpp` includes the production callback and
voice implementation with mocked native boundaries. Coverage includes original
calls, independent roles, support coupling, passenger/nested exclusion, native
curve timing/attenuation, stock fallback, cancellation epochs, lifetime changes,
contention and propagated native exceptions. The final Release run passes 81 checks, including SEH fault publication,
actual invalid native pointers, nonfinite native outputs, stale token retirement
and original-call exception behavior. Focused CTest passes.
An optimized SEH fixture exposed fault publication inside an inlined handler;
private guarded helpers now latch a volatile flag and publish the fault outside
the handler, while original engine exceptions continue to propagate.

Controller feel, real-map weapon coverage, support release and both editions
still require the user's headset test. Unsupported/invalid native definitions
retain stock behavior; arbitrary timed pulses are not a substitute.

Read-only raw evidence (ignored local artifacts):
`out/haptics-h4-kit-effects-update.txt`,
`out/haptics-h4-kit-firing-effects.txt`,
`out/haptics-h4-retail-effects.txt`,
`out/haptics-h4-retail-worker.txt`,
`out/haptics-h4-retail-evaluation.txt`,
`out/haptics-h4-retail-source-final.txt`.

Retirement disables every target through MCCVR_DisableHookForRetirement and waits for both callback accounting and detour-entry/trampoline quiescence. Disable or drain failure retains the original functions and dependencies for deferred retry. Primary support coupling explicitly requires no equipped secondary weapon. The expanded production fixture passed 81 checks; headset verification remains pending.

A subsequent timing review found that native `145C48` may skip evaluation entirely, or skip an unmapped user, despite being called with a positive delta. The adapter now has a fifth hook on the already-proven evaluator `1460C8`. Only an actual evaluation of user zero's queue (own TLS `2D0`, other users stride `1D8`) during the enclosing native update admits private evaluation. Skipped/foreign evaluation cancels the private tail. The stock evaluator result and exceptions are preserved, and all five targets participate in safe retirement. The pinned verifier now passes **38 checks**; the expanded production fixture passes **111 checks, zero failures**, and focused CTest passes (81 was the prior fixture result).
