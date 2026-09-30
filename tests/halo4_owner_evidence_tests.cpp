#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include "../src/common/halo4_owner_evidence.h"
#include "../src/common/weapon_hand_logic.h"

// Pure stage decision for the Halo 4 on-foot owner-evidence fallback. The
// native record reads live in game.cpp and are compile-checked there plus
// headset-verified; this covers the fail-closed policy itself.
using halo4_owner_evidence::PairRecordAction;
using halo4_owner_evidence::PairRecordMayResolve;
using halo4_owner_evidence::PairRecordValidatesFrozenPrimary;
using halo4_owner_evidence::PairRecordWeaponCandidate;
using halo4_owner_evidence::PairResolution;
using halo4_owner_evidence::PairResolutionOf;
using halo4_owner_evidence::ResolveFromFpRecord;
using halo4_owner_evidence::Stage;
using halo4_owner_evidence::StageName;
using halo4_owner_evidence::UpdatePairResolution;

static unsigned checks{};
static void Check(bool value,const char* name)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr,"H4 owner evidence: %s\n",name);
        std::exit(1);
    }
}

int main()
{
    constexpr uint32_t unit=0x12340001;
    uint32_t resolved=0;

    Check(ResolveFromFpRecord(true,2,unit,true,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::Ok,"present record, flags and on-foot unit resolve");
    Check(resolved==unit,"resolved stage publishes the proven unit");

    Check(ResolveFromFpRecord(false,2,unit,true,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::RecordMissing,"missing record rejected");
    Check(ResolveFromFpRecord(true,0,unit,true,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::Flags,"record without the filled flag rejected");
    Check(ResolveFromFpRecord(true,1,unit,true,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::Flags,"unrelated flag bits do not admit a record");
    Check(ResolveFromFpRecord(true,2,UINT32_MAX,true,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::UnitInvalid,"null unit handle rejected");
    Check(ResolveFromFpRecord(true,2,0,true,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::UnitInvalid,"zero unit handle rejected");
    Check(ResolveFromFpRecord(true,2,0xFFFF,true,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::UnitInvalid,"handle without an index rejected");
    Check(ResolveFromFpRecord(true,2,unit,false,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::UnitMissing,"unit without a live object rejected");
    Check(ResolveFromFpRecord(true,2,unit,true,0x23450002,UINT32_MAX,
        resolved)==Stage::Seated,"parented unit is seated, not on foot");
    Check(ResolveFromFpRecord(true,2,unit,true,UINT32_MAX,0x23450002,
        resolved)==Stage::ControllingParent,
        "controlling parent rejected like the muzzle target storage");

    // Precedence pins the stage the one-shot diagnostic reports.
    Check(ResolveFromFpRecord(false,0,UINT32_MAX,false,2,2,
        resolved)==Stage::RecordMissing,"record presence decides first");
    Check(ResolveFromFpRecord(true,0,UINT32_MAX,false,2,2,
        resolved)==Stage::Flags,"flags decide before the unit");
    Check(ResolveFromFpRecord(true,2,UINT32_MAX,false,2,2,
        resolved)==Stage::UnitInvalid,"unit validity decides before the object");
    Check(ResolveFromFpRecord(true,2,unit,false,2,2,
        resolved)==Stage::UnitMissing,"object presence decides before the parent");
    Check(ResolveFromFpRecord(true,2,unit,true,2,2,
        resolved)==Stage::Seated,"parent decides before the controller");

    // Fail closed: a rejected record must not publish a unit.
    resolved=unit;
    Check(ResolveFromFpRecord(true,2,UINT32_MAX,true,UINT32_MAX,UINT32_MAX,
        resolved)==Stage::UnitInvalid&&resolved==unit,
        "rejected record leaves the caller's unit untouched");

    const Stage stages[]{Stage::Ok,Stage::RecordMissing,Stage::Flags,
        Stage::UnitInvalid,Stage::UnitMissing,Stage::Seated,
        Stage::ControllingParent};
    for (const Stage stage : stages)
    {
        const char* name=StageName(stage);
        Check(name&&name[0], "every stage has a diagnostic name");
        for (const Stage other : stages)
            if (other!=stage)
                Check(!(name&&StageName(other)&&
                    std::string_view(name)==std::string_view(StageName(other))),
                    "stage names are distinct");
    }
    Check(StageName(static_cast<Stage>(200))!=nullptr,
        "unknown stage still names itself");

    // Per-pair finalization policy (A003). The H4 record order is storm hands
    // (provisional Unknown) then held weapon / native body, so the pair must
    // stay open until an invocation actually proves Present or Absent.
    // H4-A: an early non-authoritative read must not freeze the pair; the
    // later authoritative Present read finalizes it.
    PairResolution pair=PairResolution::Unresolved;
    Check(UpdatePairResolution(pair,true,false,false)==
        PairResolution::Unresolved,
        "H4-A: a provisional unknown read leaves the pair open");
    Check(UpdatePairResolution(pair,true,true,false)==PairResolution::Present,
        "H4-A: the later trusted read finalizes the pair present");
    // H4-B: reads that never prove anything stay Unresolved.
    pair=PairResolution::Unresolved;
    for (int read=0;read<4;++read)
        pair=UpdatePairResolution(pair,true,false,false);
    Check(pair==PairResolution::Unresolved,
        "H4-B: a pair with no authoritative evidence stays unresolved");
    // H4-C: authoritative Absent is terminal; a later Present read is a new
    // pair's business, never an upgrade of this one.
    Check(UpdatePairResolution(PairResolution::Unresolved,true,false,true)==
        PairResolution::Absent,
        "H4-C: proven absence finalizes the pair absent");
    Check(UpdatePairResolution(PairResolution::Absent,true,true,false)==
        PairResolution::Absent,
        "H4-C: a later present read never upgrades an absent pair");
    // H4-D: Present/Absent are terminal, including for reads outside the pair.
    Check(UpdatePairResolution(PairResolution::Present,true,false,true)==
        PairResolution::Present,
        "H4-D: an absent read never flips a present pair");
    Check(UpdatePairResolution(PairResolution::Present,false,false,false)==
        PairResolution::Present,
        "H4-D: a non-current read never changes a resolved pair");
    Check(UpdatePairResolution(PairResolution::Absent,false,true,false)==
        PairResolution::Absent,
        "H4-D: a non-current trusted read never revives an absent pair");
    Check(UpdatePairResolution(PairResolution::Absent,true,true,false)==
        PairResolution::Absent,
        "H4-D: a present read never revives an absent pair");
    // A read outside the live pair cannot resolve an open pair either.
    Check(UpdatePairResolution(PairResolution::Unresolved,false,true,false)==
        PairResolution::Unresolved,
        "a read outside the live pair cannot finalize present");
    Check(UpdatePairResolution(PairResolution::Unresolved,false,false,true)==
        PairResolution::Unresolved,
        "a stale absence read cannot finalize the live pair");
    // Storage encoding: the pair-begin attached default is not a resolution.
    Check(PairResolutionOf(false,false)==PairResolution::Unresolved&&
        PairResolutionOf(false,true)==PairResolution::Unresolved,
        "an unresolved pair ignores the pair-begin attached default");
    Check(PairResolutionOf(true,true)==PairResolution::Present&&
        PairResolutionOf(true,false)==PairResolution::Absent,
        "a resolved pair decodes its frozen decision");
    // Determinism: the same read sequence yields the same steps and result.
    const bool stepInputs[][3]{{true,false,false},{true,true,false},
        {true,false,false},{false,false,true}};
    const PairResolution stepExpected[]{PairResolution::Unresolved,
        PairResolution::Present,PairResolution::Present,PairResolution::Present};
    for (int run=0;run<2;++run)
    {
        PairResolution replay=PairResolution::Unresolved;
        for (size_t index=0;index<sizeof(stepInputs)/sizeof(stepInputs[0]);
            ++index)
        {
            replay=UpdatePairResolution(replay,stepInputs[index][0],
                stepInputs[index][1],stepInputs[index][2]);
            Check(replay==stepExpected[index],
                "the pair policy reproduces the same sequence");
        }
    }

    // F04 regression: Storm(objectIndex=unit) -> held(objectIndex=weapon),
    // relationship owner = weapon. The pair's trust must come from the
    // SEMANTIC PRIMARY at the storm-hands decision boundary (candidate
    // UINT32_MAX), never from the storm record's own unit handle, and the
    // later held record may only validate -- it must never retroactively
    // redefine the frozen hand solve.
    {
        const uint32_t weapon=0x23450002u;
        const uint32_t secondary=UINT32_MAX;
        const uint32_t stormObjectIndex=unit;   // the storm record's unit handle
        const uint32_t heldObjectIndex=weapon;  // the held record's weapon handle
        // The measured defect: the storm unit handle is not an equipped-weapon
        // candidate at all.
        Check(ResolveEquippedWeaponSlot(stormObjectIndex,weapon,secondary,
                  true,false)==-1,
            "F04: the storm unit handle never maps to equipped weapon slot 0");
        Check(PairRecordWeaponCandidate(PairRecordAction::StormHandsBoundary,
                  stormObjectIndex)==UINT32_MAX,
            "F04: the storm-hands boundary read is the semantic primary");
        Check(PairRecordWeaponCandidate(PairRecordAction::HeldWeapon,
                  heldObjectIndex)==heldObjectIndex,
            "F04: only the held record offers its own object index");
        Check(PairRecordMayResolve(PairRecordAction::StormHandsBoundary)&&
              !PairRecordMayResolve(PairRecordAction::HeldWeapon)&&
              !PairRecordMayResolve(PairRecordAction::Other),
            "F04: only the storm-hands boundary may resolve the pair");
        // The wrong-identity read (unit handle offered as the weapon
        // candidate) proves nothing, so the pair would stay open and the
        // palette would run detached: exactly the counterexample F04 records.
        Check(UpdatePairResolution(PairResolution::Unresolved,true,false,
                  false)==PairResolution::Unresolved,
            "F04: a unit-handle candidate read leaves the pair unresolved");
        // The semantic-primary read proves the relationship's weapon, so the
        // pair freezes Present before the hand palette consumes it.
        PairResolution frozen=UpdatePairResolution(PairResolution::Unresolved,
            true,true,false);
        Check(frozen==PairResolution::Present,
            "F04: semantic-primary trust freezes the pair present");
        // The later held record's exact candidate validates against the frozen
        // primary owner ...
        Check(PairRecordValidatesFrozenPrimary(PairRecordAction::HeldWeapon,
                  heldObjectIndex,true,weapon),
            "F04: the held weapon validates against the frozen primary");
        Check(!PairRecordValidatesFrozenPrimary(PairRecordAction::HeldWeapon,
                  stormObjectIndex,true,weapon),
            "F04: a unit handle never validates as the frozen primary weapon");
        Check(!PairRecordValidatesFrozenPrimary(
                  PairRecordAction::StormHandsBoundary,heldObjectIndex,true,
                  weapon),
            "F04: the boundary record has no held-validation role");
        Check(!PairRecordValidatesFrozenPrimary(PairRecordAction::HeldWeapon,
                  heldObjectIndex,false,weapon),
            "F04: an unresolved pair has no frozen primary to validate");
        // ... and can never re-resolve it in either direction.
        Check(UpdatePairResolution(frozen,true,false,true)==
              PairResolution::Present,
            "F04: the held record cannot redefine the frozen present decision");
        Check(UpdatePairResolution(PairResolution::Absent,true,true,false)==
              PairResolution::Absent,
            "F04: a held record never re-attaches an absent pair");
        // The same sequence with the second eye's boundary read reuses the
        // frozen decision instead of resampling (one-shot per pair).
        Check(UpdatePairResolution(frozen,true,true,false)==
              PairResolution::Present,
            "F04: the second eye reuses the frozen pair decision");
    }

    std::printf("H4 owner evidence: %u checks passed\n",checks);
    return 0;
}
