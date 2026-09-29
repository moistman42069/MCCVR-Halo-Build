weapon_muzzle::Store g_halo4BarrelMuzzles;
std::atomic<bool> g_halo4BarrelBindingsReady{false};
const uint8_t* Halo4MuzzleTls()
{
    if(!g_halo4EngineTlsIndex||*g_halo4EngineTlsIndex>=1088)return nullptr;
    auto** slots=reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
    return slots?slots[*g_halo4EngineTlsIndex]:nullptr;
}
const uint8_t* Halo4MuzzleObject(uint32_t handle,uint32_t kinds)
{
    const auto* tls=Halo4MuzzleTls();
    return tls?Halo4VehicleObject(Halo4VehicleRead<const uint8_t*>(tls,0x18),handle,kinds):nullptr;
}
bool Halo4BarrelValidObject(uint32_t handle){return Halo4MuzzleObject(handle,UINT32_MAX)!=nullptr;}
bool Halo4ReadMuzzleWeapons(uint32_t owner,uint32_t weapons[2],
    bool* primaryAbsentOut = nullptr)
{
    if(primaryAbsentOut)*primaryAbsentOut=false;
    const auto* unit=Halo4MuzzleObject(owner,1);if(!unit)return false;
    // H4EK E67B60: four inventory entries; retail 5F9E20 / 5FA1C4.
    const uint8_t roles[]{unit[0x63A],unit[0x63B]};
    // A 0xFF role byte is the native explicit "no weapon in this slot". It is
    // recorded BEFORE the validation guards below (which reject it), so an
    // explicitly empty primary slot is a real KnownAbsent state and never an
    // unexplained refusal (F03/F17).
    if(primaryAbsentOut)*primaryAbsentOut=roles[0]==0xFF;
    if(roles[0]>=4||(roles[1]>=4&&roles[1]!=0xFF)||roles[0]==roles[1])return false;
    for(int slot=0;slot<2;++slot)
    {
        if(roles[slot]==0xFF){weapons[slot]=UINT32_MAX;continue;}
        weapons[slot]=Halo4VehicleRead<uint32_t>(unit,0x640+roles[slot]*4);
        const auto* weapon=Halo4MuzzleObject(weapons[slot],4);if(!weapon)return false;
        auto actual=Halo4VehicleRead<uint32_t>(weapon,0x624);
        if(actual==UINT32_MAX&&weapon[0x471])actual=Halo4VehicleRead<uint32_t>(weapon,0x480);
        if(actual!=owner)return false;
    }
    return weapons[0]!=weapons[1];
}
void* Halo4MuzzleTargetStorage(uint32_t owner)
{
    Halo4VehicleInputState seat{};
    if(!Halo4ReadVehicleInput(seat)||seat.unit!=owner||seat.seated)return nullptr;
    const auto* unit=Halo4MuzzleObject(owner,1);
    // H4EK E80C00/E977E0, retail 5F1ABC/6176B8. Controlling parents
    // must not receive a lease for the local biped's target.
    if(!unit||Halo4VehicleRead<uint32_t>(unit,0x694)!=UINT32_MAX)return nullptr;
    return const_cast<uint8_t*>(unit)+0x5A8;
}
bool Halo4CaptureMuzzleOwner(uint32_t weapon,uint32_t& owner,uint8_t& slot)
{
    if(!g_config.gun_barrel_aim||!g_halo4BarrelBindingsReady.load(std::memory_order_acquire))return false;
    Halo4VehicleInputState seat{};uint32_t weapons[2]{};
    if(!Halo4ReadVehicleInput(seat)||seat.seated||!Halo4MuzzleTargetStorage(seat.unit)||
        !Halo4ReadMuzzleWeapons(seat.unit,weapons))return false;
    const int selected=ResolveEquippedWeaponSlot(weapon,weapons[0],weapons[1],true,weapons[1]!=UINT32_MAX);
    if(selected<0)return false;
    const auto* tls=Halo4MuzzleTls();
    const auto* fp=tls?Halo4VehicleRead<const uint8_t*>(tls,0x6A0):nullptr;
    // Own FP producer 92A1F0 -> 3B1B4C: user5F48, slot2EC8,
    // unit+4, held weapon+6C, held render tag+74.
    if(!fp||!(Halo4VehicleRead<uint32_t>(fp,0)&2)||Halo4VehicleRead<uint32_t>(fp,4)!=seat.unit||
        Halo4VehicleRead<uint32_t>(fp,selected*0x2EC8+0x6C)!=weapon)return false;
    owner=seat.unit;slot=uint8_t(selected);return true;
}
