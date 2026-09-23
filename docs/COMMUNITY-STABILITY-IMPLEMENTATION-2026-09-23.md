# September 23 stability implementation and fault attribution

This is a source and offline validation record. It does not advance the accepted
build pointer or claim headset/co-op acceptance. The target experience is Halo 3's
working VR view surviving feature failures and ordinary title transitions.

## Confirmed vehicle feature retirement defect

The community transition logs repeatedly report `Vehicle first person title=5:
cleanup pending; feature stock` after CE ends, including Chronomize's September
19 15:18 log. CE/H2/H4's shared `native_vehicle_first_person.cpp::Retire` supplied
six functions to `WaitForNativeDetourQuiescence`, including `Fault`.

That shared checker requires `RtlLookupFunctionEntry` to resolve every supplied
function before it considers callback or thread quiescence. The optimized
`Fault` helper is a leaf function without unwind metadata. Consequently this
feature could never finish retirement: it retained its title module reference,
old generation and trampoline bookkeeping indefinitely. This is a proven local
lifetime defect; it is not proof of the separate firing or melee desync cause.

Retirement now checks the two actual native ingress detours and their
trampolines. Every call to `Current`, `Owner`, `Head` and `Fault` is inside those
detours' callback lease, which spans the complete original call and exception
cleanup. The detour ranges still protect entry before the counter increment;
the callback counter protects all nested work. The common quiescence checker,
thread freezing, deferred cleanup and module-release ordering are unchanged.

The production-source vehicle fixture now uses real Windows unwind lookup for
every retirement function. It checks pending callbacks, disabled ingress,
retained dependencies when removal fails, successful retry, removal of both
hooks and clearing the old generation. Release build and **537 production
vehicle checks pass**. The previous test stub checked only the callback count,
so it could not detect the missing unwind record. Target-title reentry and
Halo 3 headset regression remain required.

## Halo 4 pause query during TLS retirement

BlackHawkCJ's September 19 fault at `halo4+A0B03` records a null `rdx` and
`rcx=3`. Its caller `HaloMCCVR+86A6B` matches `Halo4ReadNativePaused`, which
invokes the already verified reason-3 pause getter. The native getter obtains
the current thread's TLS block and immediately dereferences its pause-state
pointer at `TLS+0x90`; the exact signed getter body already pins this offset.
The calling mod function catches the exception, so this is not attributed as
the terminal crash.

The reader now checks the existing validated TLS index, thread block and that
native pause-state pointer before invoking the getter. Retiring/unavailable
state remains **unknown**, rather than falsely signalling Resume. The original
SEH guard remains for teardown races; the original is never replayed. There is
no new hook or copied title offset. A fixture includes the exact production
reader and tests absent proof/getter/index/TLS slots/block/pause globals, normal
paused/unpaused values, a native exception, and later recovery: **11 Release
checks pass**. Runtime pause/Resume and title-transition validation remains due.

## Exact source attribution of mod first-chance faults

The preserved Release DLL and shipped f53f0bd candidate are byte-identical:
SHA-256 `18D4F4AB94FA8065718AF43E36020BAE989259007A4C079CF69AA94EFB72AB69`.
No production PDB was preserved. The original build object files were copied
before recompilation and relinked into an ignored analysis artifact with a map.
The complete relinked image is **not** byte-identical (library/link differences),
so the map alone was not treated as proof. Each listed fault instruction window
was compared against the shipped DLL at the same RVA; 32 bytes match exactly.
The first, second, fourth, fifth and sixth windows are unique in the relinked
text. The pause-reader body also occurs in the ODST/Reach readers, so its same
RVA and function-boundary context identify it rather than byte uniqueness alone.

| f53f0bd RVA | Function / offset | Existing handling |
|---|---|---|
| `CC6CD` | `SafeFrameScanRegion + FD` | Guarded HUD discovery access |
| `CCFC2` | `SafeFrameVerifySlot + 82` | Guarded HUD slot validation |
| `C3541` | `ReadEnginePaused + 11` | `__except` returns unavailable |
| `CD080` | `SafeWriteByte + 0` | Guarded optional write |
| `BD7F0` | `ReachReadU16 + 0` | `__except` returns unavailable |
| `E0788` | H2 `ReadDwordGuarded + 8` | `__except` returns unavailable |

These observations explain why the same first-chance anchors occur across
otherwise different reports. They are not established terminal crash sites.
The probe sees the exception before the code's handler returns; subsequent log
activity must be considered. No speculative patch is applied to suppress or
mislabel these diagnostics.

Preserved offline artifacts: `out/coop-stability-20260923/`, including copied
objects, original DLL, relink response/map, instruction matching script and PE
unwind audit. Contributor binaries were not executed; MCC was not launched.

## Remaining difficult failures

### Existing first-person vehicle lease refinement

The follow-up review found that H3 and ODST returned active immediately for the
same generation/definition/seat tuple, without checking whether a checkpoint
or map replacement had retired the actual tag storage. Module generation alone
does not establish the identity of a loaded tag allocation. H3's cleanup also
compared only the resolved flags address/value and used a non-atomic restore,
despite its comment promising preservation of a concurrent engine change.

`halo3_native_seat_patch.inl` and `odst_native_seat_patch.inl` now validate the
current tag base, instance table, definition, bounded seat block, resolved flags
address and owned flag value before accepting an existing active lease. A
replacement allocation must be acquired from its own current records. H3 now
records those identities and restores with compare/exchange; both title
acquisitions use compare/exchange and record ownership only after a successful
exchange. An engine writer racing either acquisition or cleanup wins. No
camera-track count, anchor, weapon alignment or seat semantics were changed.

The production implementations are included directly by their offline fixtures:
**75 H3 checks and 74 ODST checks pass**. They cover live restore, unchanged
active seats, same numeric seat with replacement storage, generation/map/table/
definition/address/count loss, inaccessible memory, changed native flags,
deterministic acquisition/cleanup writer races and configuration disable. H3
also verifies that the native camera-track count remains intact. These are
source-level lifetime tests; both titles still need headset vehicle regression
results. This change does not prove shared-seat-tag writes safe for campaign
co-op simulation.

### CE input evidence handoff

The native input investigation completed CE's own controller mapping proof.
See `CE-CONTROLLER-MAPPING-EVIDENCE-2026-09-23.md`: all six titles now have
independently verified read-only preference transport lookup. The actual
production parser fixture passes **31,233 checks** across all six descriptors.
The six-title static verifier checks 12 unique native patterns, six state
pointers and each title's own kit transport table. Native aliases remain a
separate integration boundary: a transport bit cannot distinguish Reload and
Interact when the engine maps both actions to that same button.

### Report-specific gaps

- **ST01 firing:** older all-options-off Halo 3 reports still lack a failure
  stack or a proven simulation divergence. The existing native-call-once and
  exception-preservation correction remains; this change is not its solution.
- **ST02 contact/dual co-op:** local contact samples cause an extra native melee
  builder invocation. Campaign lockstep requires proof that every peer consumes
  the same action/target; invoking a native damage function alone does not
  establish this. Competitive multiplayer success is not campaign proof.
- **ST03 first-person co-op:** H3/ODST still maintain a loaded seat-tag camera
  flag while occupied. The flag is shared by every same-definition seat, not
  inherently per-player. Its simulation consumers must be traced before a
  presentation-only replacement can be claimed safe. CE/H2/H4 retirement fix
  above is a separate defect.
- **ST04 CE door/crouch/death:** f53f0bd's outgoing-action adapter has native
  producer/consumer tests but no accepted two-peer result for these reports.
- **ST05 transitions:** one confirmed retirement defect is fixed above. Other
  checkpoint/menu faults and the native CE/H4 anchors still need their own
  attribution; they are not implicitly covered.
- **ST06 Reach vehicle/Forge:** native camera/seat admission varies by mode;
  campaign success does not establish Forge/MP behavior.
- **ST07 H2 faults:** repeated `halo2+67BA50` and separate explosion/2D paths
  remain separate evidence. First-chance logs continue afterward and do not
  establish which peer disconnected or whether the process terminated.
- **ST08 readiness:** Rainred's Reach eye-allocation wait remains separate from
  skivy44's ODST case. Re-reading skivy44's line574 identifies the old
  `1a9766c` zero-offset guard rejecting `offset=0.170000`, already corrected in
  `ODST-OBSERVER-OFFSET-2026-09-18.md`. That implementation is retained;
  the older report is not a new source defect or a new headset result.
