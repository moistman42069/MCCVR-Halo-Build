#pragma once
#include <cstdint>

// Halo 4 on-foot owner evidence fallback. The optional vehicle-input helper
// (halo4_vehicle_input.inl) is an optimised admission path, never a
// prerequisite: when its runtime reader cannot establish the local output
// unit, the engine's own first-person weapon record carries the same proof.
//
// This module supplies only the stage decision for that fallback. The caller
// performs the native reads (record flags/unit, unit object, parent, and
// controlling parent) and owns the surrounding fail-closed handling: any
// stage other than Ok means "no evidence", never "absent owner".
//
// `outUnit` is written only on Ok, so a rejected record cannot hand a caller
// a half-proven unit.
namespace halo4_owner_evidence
{

enum class Stage
{
    Ok,
    RecordMissing,
    Flags,
    UnitInvalid,
    UnitMissing,
    Seated,
    ControllingParent,
};

// recordPresent   : the TLS first-person record pointer was readable
// flags           : record +0x0; bit1 marks a filled first-person record
// unit            : record +0x4 (local unit handle)
// unitObjectPresent: engine object lookup for `unit` succeeded
// unitParent      : unit object +0x24 (UINT32_MAX when on foot)
// unitController  : unit object +0x694 (UINT32_MAX when uncontrolled)
inline Stage ResolveFromFpRecord(bool recordPresent, uint32_t flags,
    uint32_t unit, bool unitObjectPresent, uint32_t unitParent,
    uint32_t unitController, uint32_t& outUnit) noexcept
{
    if (!recordPresent) return Stage::RecordMissing;
    if ((flags & 2u) == 0) return Stage::Flags;
    if (unit == UINT32_MAX || (unit >> 16) == 0) return Stage::UnitInvalid;
    if (!unitObjectPresent) return Stage::UnitMissing;
    if (unitParent != UINT32_MAX) return Stage::Seated;
    if (unitController != UINT32_MAX) return Stage::ControllingParent;
    outUnit = unit;
    return Stage::Ok;
}

inline const char* StageName(Stage stage) noexcept
{
    switch (stage)
    {
    case Stage::Ok: return "ok";
    case Stage::RecordMissing: return "record-missing";
    case Stage::Flags: return "flags";
    case Stage::UnitInvalid: return "unit-invalid";
    case Stage::UnitMissing: return "unit-missing";
    case Stage::Seated: return "seated";
    case Stage::ControllingParent: return "controlling-parent";
    }
    return "unknown";
}

// Per-pair owner-evidence finalization for the H4 floating pair.
//
// The model-skinning detour sees several first-person records per pair. Only a
// record whose own invocation PROVED its owner may freeze the pair: the first
// readable record is normally the flag-1 storm-hands record, whose object
// index is not the held weapon, so its read is provisional Unknown and must
// leave the pair open for the later held-weapon or native-body record to prove
// Present or Absent. A non-authoritative read must never freeze the pair.
//
// Terminal: once a pair is Present or Absent, later reads never change it. A
// read that does not belong to the live pair is ignored entirely.
enum class PairResolution
{
    Unresolved = 0,
    Present = 1,
    Absent = 2,
};

// The pair's stored encoding: `resolved` is the terminal flag and `attached`
// is the frozen presentation decision. An unresolved pair is Unresolved
// whatever the pair-begin default wrote into `attached`.
inline PairResolution PairResolutionOf(bool resolved, bool attached) noexcept
{
    if (!resolved) return PairResolution::Unresolved;
    return attached ? PairResolution::Present : PairResolution::Absent;
}

// current        : this pair's resolution so far
// pairCurrent    : the read belongs to the live floating pair
// trusted        : the invocation proved the relationship's current owner
// evidenceAbsent : the invocation proved the primary owner absent
inline PairResolution UpdatePairResolution(PairResolution current,
    bool pairCurrent, bool trusted, bool evidenceAbsent) noexcept
{
    if (current != PairResolution::Unresolved) return current;
    if (!pairCurrent) return PairResolution::Unresolved;
    if (trusted) return PairResolution::Present;
    if (evidenceAbsent) return PairResolution::Absent;
    return PairResolution::Unresolved;
}

// ---------------------------------------------------------------------------
// Per-record identity policy (the F04 repair)
// ---------------------------------------------------------------------------
//
// The established Halo 4 floating record order per eye is:
//
//     storm hands  objectIndex = the local UNIT handle
//     held weapon  objectIndex = the held WEAPON handle
//     native body  never participates
//
// The storm-hands record is the one whose palette consumes the frozen owner
// decision, but its object index is NOT a weapon candidate: offering it to the
// equipped-slot mapping can only fail (or, worse, name an unrelated weapon) and
// resolves the invocation Unknown exactly when the hand palette needs the
// decision. The trust is therefore resolved from the SEMANTIC PRIMARY itself
// (`candidate = UINT32_MAX`) at the storm-hands decision boundary. The later
// held-weapon record may offer its own object index, but only to VALIDATE the
// pair's already-frozen primary owner -- never to re-resolve a pair whose hand
// solve already ran.
enum class PairRecordAction : uint8_t
{
    Other = 0,
    StormHandsBoundary = 1,
    HeldWeapon = 2,
};

// The weapon candidate this record's owner read may offer. Only the held
// record offers its own object index; every other record reads the semantic
// primary slot (UINT32_MAX).
inline constexpr uint32_t PairRecordWeaponCandidate(PairRecordAction action,
    uint32_t objectIndex) noexcept
{
    return action == PairRecordAction::HeldWeapon ? objectIndex : 0xFFFFFFFFu;
}

// Whether this record may advance the pair's one-shot trust decision. Only the
// storm-hands decision boundary may: a provisional Unknown leaves the pair open
// for later records, and no later record may retroactively redefine the hand
// solve it already ran.
inline constexpr bool PairRecordMayResolve(PairRecordAction action) noexcept
{
    return action == PairRecordAction::StormHandsBoundary;
}

// The later held-weapon record validates its own object index against the
// pair's frozen Present decision without re-resolving anything. The frozen
// owner carries a full weapon handle, so a valid candidate is a complete
// handle equal to it.
inline constexpr bool PairRecordValidatesFrozenPrimary(PairRecordAction action,
    uint32_t objectIndex, bool frozenPresent,
    uint32_t frozenPrimaryWeapon) noexcept
{
    return action == PairRecordAction::HeldWeapon && frozenPresent &&
        objectIndex != 0xFFFFFFFFu && (objectIndex >> 16) != 0 &&
        objectIndex == frozenPrimaryWeapon;
}

}
