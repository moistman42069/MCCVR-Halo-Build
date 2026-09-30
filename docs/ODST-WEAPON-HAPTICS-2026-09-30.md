# ODST authored weapon haptics — 2026-09-30

Implemented as an optional, independently retired adapter. Release production-callback fixture: **118 checks, zero failures**. Own pinned native verifier: **47 checks passed**. Combined DLL build and headset validation are tracked by the parent candidate; this document does not claim headset acceptance.

The player-facing behavior matched is hand-correct authored firing feedback: primary and secondary weapon voices follow their equipped VR hand, with live support coupling only for a sole primary weapon. Generic damage, environmental and camera rumble retain their native processing. This implementation is derived from ODST's own official editing-kit executable and matched to the pinned ODST module; no Halo 3 layout was assumed.

## Pinned inputs

| File | SHA-256 |
| --- | --- |
| `halo3odst_tag_test.exe` | `354EC94158AECCE3E9D0F6463023AD5FA6D2AFE49B390E6067EBC17465C63C2D` |
| `halo3odst.dll` | `5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A` |

Official-kit source assertion descriptors identify `c:\mcc\qfe1\odst\source\game\player_rumble.cpp` and assert the four-user range. All offsets below are RVAs in these exact input images.

## Source and native response path

| Purpose | ODST kit | Pinned retail |
| --- | --- | --- |
| Admitted barrel firing response | `B11460` | `3ACCC8` |
| Unit-authored response helper | `AD7C10` | `3A1464` |
| Player effect wrapper | `6176B0` ? `617700` | `1E3B98` |
| Effect worker, including camera/visual and rumble | `617B10` | `1E3D40` |
| Rumble enqueue | `53E8B0` | Inlined in worker `1E3F6E..1E3FCA` |
| Native rumble evaluator | `53E0A0` | `1BE2B0` |
| Native rumble update | `53EBF0` | `1BE0B0` |
| Native curve evaluation | `72E570` | `27B458` |
| Native rumble initializer | `53E480` | `1BE028` |

The kit firing function validates its barrel, ammunition/state admission, increments firing counters, and applies the owner response only within its successful-admission branch. The owner call to `AD7C10` is `B11F29`; a distinct passenger loop calls at `B11FCC`. Retail preserves that structure: `3AD4B0` tests the admitted flag and skips the response block when false. Owner call `3AD8ED` returns at `3AD8F2`; passenger call `3AD9B8` returns at `3AD9BD`.

Only the first exact call enters the private response scope. The complete original response helper still executes. The worker must have the local owned unit, output user 0, exact wrapper caller `1E3CDB` or `1E3D20`, and the still-current weapon/generation/token. An outer firing request alone cannot synthesize haptics: native must actually write a matching response into its rumble bank.

ODST's native trigger-motor helper (`B0E700` ? `3B2004`) is a separate path, not mistaken for the authored two-band response. Continuous trigger/charging effects in `B1AF40` are also distinct from the admitted barrel response. They retain stock behavior in this adapter.

## Queue ownership and eviction

The own native worker uses TLS member `2B0`, four users of stride `98`, eight records of stride `0C`: full 32-bit response datum, 32-bit row and float scale. Its eight ages start at `70`; selection uses strict greater-than age, preserving the earliest slot on ties. The worker stores the full datum from `R12D` at `1E3FC1`. Only tag-table lookup uses its low 16 bits. The fixture intentionally uses salted datum `12340063` to guard against masking the stored identity.

The entire original worker runs once, outside optional SEH. That preserves its camera/visual writes before and after the rumble enqueue. The adapter compares the actual new `(full datum, selected row, exact float scale, age=0)` and all other queue/timer bytes before moving anything. It copies the row the native worker actually chose; it does not assume row zero. A changed token, owner, queue pointer, unrelated nested write or malformed envelope leaves the native result intact.

The own initializer sets each record to `(FFFFFFFF, FFFFFFFF, 1.0)`. The evaluator skips a record with either NONE tag or NONE row. These stores and the skip branch are pinned; a unique runtime initializer witness is also required before installation.

- An old empty or fully expired record is restored exactly, including its old age.
- An active old record was already evicted by the stock worker. It is **not revived**. After staging the exact new private recoil, the adapter clears that new native record to the proven NONE tuple and keeps native age zero. This preserves the native eviction while preventing recoil from leaking to the other hand when the native bank already held eight active effects.
- The other seven native records and native trigger timers are unchanged.
- The private bank also has eight voices. Once full it replaces its oldest private voice, following bounded native-style selection, instead of reverting the ninth recoil to bilateral rumble.

The own evaluator indexes the response tag table at `A9F0A8` and tag-data base at `2022AA8`. It validates the response row, whose stride is `C0`; two bands begin at row `5C`, each stride `18` (duration plus native mapping). It evaluates a band only while `age < duration`. Thus restoring a validated old record after both finite authored durations have ended cannot revive it. Invalid or unresolved old definitions conservatively retain native behavior.

## Private evaluation and lifecycle

ODST evaluates before adding delta to native ages. Private voices match that ordering, sum simultaneous voices per authored band, quantize/clamp each band, and use the shared 65/35 motor mix. General forced camera/environment values and trigger motor timers are not copied into private recoil.

The native update and evaluator have separate callbacks. The evaluator marks an actual local-user native sample during the update scope. A paused/skipped native update therefore cancels private tails rather than continuing vibration merely because the update function was called. The original evaluator's return value remains unchanged.

Full-salt own object/inventory validation reuses the already proven ODST ownership reader: unit roles `276/277`, four handles at `27C`, weapon owner-valid byte `155` and owner datum `160`. This reader is independent of the muzzle-aim configuration. Local-player, controlling-parent, vehicle, cinematic, title/runtime, stereo and exclusive-input guards remain in place. Re-equipping, role changes, generation changes, menu/tracking/focus cancellation tokens and stale update gaps retire private voices. Primary support coupling requires no secondary weapon and a current shared support relationship (or the configured legacy two-hand relationship).

Optional native reads and curve failures fault only this adapter, with deferred reporting/cleanup. Native original exceptions propagate through scope-restoring `__finally` blocks. No allocation, logging, file I/O, blocking lock or signature search occurs in hot callbacks. Installation requires each own signature and call edge uniquely; failure leaves general rumble stock.

Retirement disables every hook through `MCCVR_DisableHookForRetirement`, then checks all detour entries/trampolines and callback accounting through `WaitForNativeDetourQuiescence`. Disable, drain or remove failure retains originals/dependencies for retry. It does not disarm the working camera.

## Validation and limits

`tests/odst_weapon_haptics_tests.cpp` includes the production callback backend directly. Its 118 checks cover actual native mock writes, salted tag identity, selected response row, primary/secondary/support roles, exact native slot restoration, full active and expired banks, unaffected native damage slots, ninth private voice replacement, concurrent/nested write rejection, real invalid native pointer SEH, native exception propagation, optional faults, update ordering, paused native evaluation, cancellation, expiry and bounds.

`tools/verify-odst-weapon-haptics.py` checks both input hashes, own kit/retail call edges, admitted-fire branch, scope ABI, inline allocator fields, full-datum storage versus tag-index lookup, native expiry predicate, NONE initializer/evaluator contract, tag-base references and runtime signature uniqueness. Read-only Ghidra exports remain under ignored `out/haptics-odst-*`.

No MCC files were installed or launched. Actual controller behavior, sustained weapons, vehicle/cinematic transitions and mixed general damage feedback still require the user's headset validation before advancing the accepted-build pointer.

Final provenance refinement: a selected record whose datum/row/scale/age bytes are unchanged across the native worker is not sufficient evidence of a new enqueue. A native row/flags early return may leave a preexisting matching age-zero cue. Such indistinguishable cases now stay native. The added production regression and focused CTest now pass: 118 checks, zero failures. The pinned verifier remains 47 checks.
