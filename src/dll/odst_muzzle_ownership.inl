// ODST-only full-salt object table and inventory proof. The title's existing
// contact validator performs bounds/salt checks; no contact feature need be on.
const uint8_t* OdstMuzzleObject(uint32_t handle,uint8_t kind)
{
    if(!OdstContactObject(handle))return nullptr;
    const auto* table=*reinterpret_cast<const uint8_t* const*>(OdstContactTls()+kOdstTlsObjectTableOffset);
    const auto* entries=*reinterpret_cast<const uint8_t* const*>(table+0x48);
    const auto* entry=entries+(handle&0xFFFF)*0x18;
    return entry[3]==kind?*reinterpret_cast<const uint8_t* const*>(entry+0x10):nullptr;
}
bool OdstReadMuzzleWeapons(uint32_t owner,uint32_t weapons[2],
    bool* primaryAbsentOut=nullptr)
{
    if(primaryAbsentOut)*primaryAbsentOut=false;
    const auto* unit=OdstMuzzleObject(owner,0);
    if(!unit)return false;
    // ODSTEK ADE940 and retail 39AB60: four handles at +27C. The firing
    // data helper proves primary/secondary role bytes +276/+277 separately.
    const uint8_t roles[2]{unit[0x276],unit[0x277]};
    // A 0xFF primary role byte is the native explicit "no weapon in this
    // slot": report it BEFORE the validation guards so a raw empty slot is
    // never lost to an unrelated secondary-role/shared-handle rejection. Every
    // other failure stays uncertainty and is never reported as absence (F03).
    if(primaryAbsentOut)*primaryAbsentOut=roles[0]==0xFF;
    if(roles[0]>=4||(roles[1]>=4&&roles[1]!=0xFF)||roles[0]==roles[1])return false;
    for(int slot=0;slot<2;++slot)
    {
        if(roles[slot]==0xFF){weapons[slot]=UINT32_MAX;continue;}
        weapons[slot]=*reinterpret_cast<const uint32_t*>(unit+0x27C+roles[slot]*4);
        const auto* weapon=OdstMuzzleObject(weapons[slot],2);
        if(!weapon||!weapon[0x155]||*reinterpret_cast<const uint32_t*>(weapon+0x160)!=owner)return false;
    }
    return weapons[0]!=weapons[1];
}
void* OdstMuzzleTargetStorage(uint32_t owner)
{
    if(!g_odstPlayerUnitGetter||!g_odstUnitInVehicle||
        owner!=uint32_t(g_odstPlayerUnitGetter(0))||g_odstUnitInVehicle(int32_t(owner)))return nullptr;
    const auto* unit=OdstMuzzleObject(owner,0);
    // ODSTEK B122B0 follows controlling parent +2B8, targeting +228.
    if(!unit||*reinterpret_cast<const uint32_t*>(unit+0x2B8)!=UINT32_MAX)return nullptr;
    return const_cast<uint8_t*>(unit)+0x228;
}
