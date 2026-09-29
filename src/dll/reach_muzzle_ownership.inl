// HREK D78C90 inventory, DE4290 role bytes, D77E00 controlling parent;
// pinned retail 48B3D0, 4C2A4F/4C31B0, 48375C. Full salted handles are
// validated by Reach's existing native object-table reader before any field.
const uint8_t* ReachMuzzleObject(uint32_t handle,uint8_t wantedKind)
{
    uint8_t kind=0;const auto* object=ReachVehicleObjectData(int32_t(handle),kind);
    return object&&kind==wantedKind?object:nullptr;
}
bool ReachBarrelValidObject(uint32_t handle)
{ uint8_t kind=0;return ReachVehicleObjectData(int32_t(handle),kind)!=nullptr; }
bool ReachReadMuzzleWeapons(uint32_t owner,uint32_t weapons[2],
    bool* primaryAbsentOut=nullptr)
{
    if(primaryAbsentOut)*primaryAbsentOut=false;
    const auto* unit=ReachMuzzleObject(owner,0);if(!unit)return false;
    const uint8_t roles[]{unit[0x34A],unit[0x34B]};
    // A 0xFF primary role byte is the native explicit "no weapon in this
    // slot": report it BEFORE the validation guards so a raw empty slot is
    // never lost to an unrelated secondary-role/shared-handle rejection. Every
    // other failure stays uncertainty and is never reported as absence (F03).
    if(primaryAbsentOut)*primaryAbsentOut=roles[0]==0xFF;
    if(roles[0]>=4||(roles[1]>=4&&roles[1]!=0xFF)||roles[0]==roles[1])return false;
    for(int slot=0;slot<2;++slot)
    {
        if(roles[slot]==0xFF){weapons[slot]=UINT32_MAX;continue;}
        weapons[slot]=*reinterpret_cast<const uint32_t*>(unit+0x350+roles[slot]*4);
        const auto* weapon=ReachMuzzleObject(weapons[slot],2);if(!weapon)return false;
        // HREK reload ownership; retail 4BE0B8 independently matches all three.
        auto actual=*reinterpret_cast<const uint32_t*>(weapon+0x32C);
        if(actual==UINT32_MAX&&weapon[0x1A9])actual=*reinterpret_cast<const uint32_t*>(weapon+0x1B4);
        if(actual!=owner)return false;
    }
    return weapons[0]!=weapons[1];
}
void* ReachMuzzleTargetStorage(uint32_t owner)
{
    if(!g_reachCamera.playerUnitByOutputUser||!g_reachCamera.unitInVehicle||
        owner!=uint32_t(g_reachCamera.playerUnitByOutputUser(0))||g_reachCamera.unitInVehicle(int32_t(owner)))return nullptr;
    const auto* unit=ReachMuzzleObject(owner,0);
    if(!unit||*reinterpret_cast<const uint32_t*>(unit+0x390)!=UINT32_MAX)return nullptr;
    // HREK DE4290 aligns unit+699 to four bytes; retail 4C2F57 does the same.
    return const_cast<uint8_t*>(unit)+0x2B8;
}
