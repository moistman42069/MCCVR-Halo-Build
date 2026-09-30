# Community stability follow-up — 2026-09-30

This review follows the September 23 community audit and the fresh September
29 reports. It examines preserved logs and current source only. No game was
launched, no multiplayer session was reproduced, and no headset result was
obtained. It does not advance `CURRENT-STATE.md` or establish that all reports
are fixed.

## skivy44 Reach transition / first-chance record

Artifact: `out/discord-review-skivy44/HaloMCCVR.log.prev`, SHA-256
`A61F2D91D31A70A38BE352DF336959CC058A03754CDE105666A0EDFE9E8D394A`.
The log identifies source `f53f0bd750408b8d40106f92098ff09c46edd568`, Steam,
SteamVR/OpenXR Meta compatibility 2.18.1, Oculus-family profile and 120 Hz.
Reach stereo was active shortly before the event; this is not a current-build
log.

At 08:49:05.552 the observe-only fault recorder captured a read access violation
at `HaloMCCVR.dll+0xCCFC2`, reading `0x7FF499B45FB0`. The September 23 exact
source attribution maps this instruction to `SafeFrameVerifySlot + 0x82`, in
Reach HUD safe-frame candidate validation. That function compares a candidate
anchor and reads candidate values inside `__try`; an inaccessible candidate is
rejected by its exception handler. The fault record explicitly says
first-chance and “may be caught”; it is not by itself proof of an uncaught
exception or process crash. The implementation’s intended outcome for this
particular read is stock HUD fallback for the candidate.

The surrounding event remains concerning and unresolved. Immediately after the
fault the log records repeated `gameplay -> loading -> gameplay` transitions,
alternate-eye stereo turning off, Reach core teardown, module re-detection,
then an unsupported/shell state. The load gate sees the camera frozen while a
game-time value is still changing and correctly holds reinstallation. The log
continues for roughly ten seconds, then reports no MCC title module and a
1000-ms visible Present stall. This establishes a title transition/unload and
visible freeze in that session, but does not establish that the caught safe
frame read caused it. There is no uncaught mod exception, exact native title
fault, or two-peer context tying cause to effect in this record. Do not remove
the guard or bypass the load gate based on this evidence.

The same `CCFC2` anchor is documented in other reports as a guarded slot
validation read. Repeated first-chance addresses across builds are diagnostic
anchors, not independent crash diagnoses. A useful next capture needs the
matching current build, the exact in-game transition/reproduction, and both
peers’ outcomes if multiplayer is involved. Preserve the native-fault log and
the main log through the visible stall/module unload.

## Other recent reports and prior fixes

- skivy44’s separate Reach control report says `VR Mappings -> USE -> Weapon
  Grip` fixed the symptom “for now.” The DM does not state the original symptom
  precisely, so this does not establish a mapping regression or root cause.
- The September 23 evidence index identifies an older ODST report whose camera
  observer rejected a valid nonzero offset (`0.170000`). The implementation
  correction is already recorded in `ODST-OBSERVER-OFFSET-2026-09-18.md`; that
  old log is not evidence of a current defect. The separate September 28/29
  reports of random ODST campaign crashes have no attached runtime, mission
  identity, edition, runtime or reproduction details in the reviewed messages.
  They remain open and cannot be assigned to the offset fix or another path.
- Other ODST transition/readiness cases in the preserved audit are distinct:
  camera readiness, eye allocation, session-visible stalls and native fault
  records must not be collapsed into one “ODST crash” cause. The current
  acceptance set does not include long ODST campaigns or checkpoint/mission
  transition coverage.

## Co-op and transition status carried forward

- **Halo 3 firing failure:** root cause remains unproven, including the report
  with optional settings off and firing in any direction. The September 19
  correction preserves native collision-query exceptions and isolates only
  added contact work; it is not a confirmed fix for the reported co-op failure.
  Existing fault files do not identify the failing peer or a shared simulation
  divergence. A two-peer reproduction needs both roles, exact build/config and
  synchronized logs through failure.
- **Physical melee / dual wield:** local native contact and shot paths have
  guarded ownership, but campaign lockstep safety is not established. A
  competitive multiplayer success does not validate campaign co-op.
- **First-person vehicle flags in co-op:** H3/ODST camera flag ownership can
  affect shared same-definition seat data. Tag replacement retirement checks
  were strengthened, but that does not prove the underlying shared simulation
  write is safe for two peers.
- **CE multiplayer actions:** the outgoing native action adapter and local
  menu presentation work have offline production fixtures. Host/client doors,
  death, crouch and teammate-kill behavior still require a two-peer result.
- **Halo 2 / title faults:** repeated first-chance records, including
  `halo2+0x67BA50`, remain non-diagnostic without the native caller and failure
  outcome. Later log continuation means they must not be called terminal crashes
  solely from their presence.
- **Reach multiplayer / Forge:** the saved skivy44 log is Reach campaign ONI
  Sword Base, not an MP/Forge reproduction. MP/Forge vehicle admission and
  overheat/death stereo loss remain separate, unverified reports.

## Disposition

No code change is justified by this log: the only attributed fault is already
contained by the intended feature-local guard, and the later unload does not
establish a causal edge to that read. No speculative lifecycle or hook change
was made. Existing native lifecycle fixtures and the September 23 stability
implementation remain the current offline evidence; they are not rerun or
re-certified here. The supported conclusion is narrower: one older Reach run
had a caught mod-side candidate read followed by a title transition/unload and
visible stall, with root cause unresolved; ODST random campaign crashes and
the major co-op reports also remain open pending reproducible current-build
evidence.
