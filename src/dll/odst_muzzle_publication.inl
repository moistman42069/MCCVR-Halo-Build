// ODST native FP users/slots: TLS598, user4F38, slot2740, weapon3C;
// retail 2E9E65/2E9E8E, independently established
// in native reload-tail evidence. Capture before AND after interpolation, so
// a later inventory lookup cannot attach an old palette to a replacement gun.
// Apart from the options: co-op safe shots stands them down, and the authored grip point goes on needing this.
bool OdstCaptureGripOwner(int player,int slot,uint32_t& unit,uint32_t& weapon) noexcept
{
    if(!g_odstMuzzleBindingsReady.load(std::memory_order_acquire) ||
        player!=0 || slot<0 || slot>1 || !g_odstEngineTlsIndex || !g_odstPlayerUnitGetter ||
        !g_odstUnitInVehicle)return false;
    __try
    {
        auto** slots=reinterpret_cast<uint8_t**>(__readgsqword(0x58));
        const auto* tls=slots?slots[*g_odstEngineTlsIndex]:nullptr;
        const auto* users=tls?*reinterpret_cast<const uint8_t* const*>(tls+0x598):nullptr;
        if(!users)return false;
        const uint32_t owner=uint32_t(g_odstPlayerUnitGetter(0));
        uint32_t weapons[2]{};
        if(owner==UINT32_MAX || g_odstUnitInVehicle(int32_t(owner)) ||
            !OdstReadMuzzleWeapons(owner,weapons) || weapons[slot]==UINT32_MAX ||
            *reinterpret_cast<const uint32_t*>(users+slot*0x2740+0x3C)!=weapons[slot])return false;
        unit=owner;weapon=weapons[slot];return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool OdstCaptureMuzzleOwner(int player,int slot,uint32_t& unit,uint32_t& weapon) noexcept
{
    return (Sight_GunBarrelAim()||Sight_ShotsFromMuzzle()) && OdstCaptureGripOwner(player,slot,unit,weapon);
}

void OdstPublishMuzzlePalette(uint16_t tag,const FpInterpolationContext& context,
    const BoneMatrix* destination,uint32_t generation) noexcept
{
    if(!(Sight_GunBarrelAim()||Sight_ShotsFromMuzzle()) || !g_odstMuzzleBindingsReady.load(std::memory_order_acquire) ||
        !context.valid || context.player!=0 || context.slot<0 || context.slot>1 ||
        !generation || context.generation!=generation || generation!=g_odstRuntimeGeneration.load() ||
        !destination || !g_fpStereoSolveScope.armed)return;
    __try
    {
        uint32_t checksum=0;
        const int count=LegacyAnatomicalRenderNodeCount(GameTitle::Halo3ODST,tag,&checksum);
        if(count<=0 || count>64 || (!weapon_muzzle::Find(GameTitle::Halo3ODST,checksum,0)&&
            !weapon_muzzle::Find(GameTitle::Halo3ODST,checksum,1)))return;
        weapon_muzzle::Palette palette{};
        const auto& tracking=g_fpStereoSolveScope.anatomicalTracking;
        uint32_t unit=UINT32_MAX,weapon=UINT32_MAX;
        if(OdstCaptureMuzzleOwner(context.player,context.slot,unit,weapon) &&
            unit==context.muzzleUnit && weapon==context.muzzleWeapon &&
            tracking.serial && tracking.referenceEpoch && tracking.timeNs>0 &&
            tracking.hands[context.slot==0?1:0].valid)
        {
            const uint64_t now=GetTickCount64();
            for(uint8_t barrel=0;barrel<2;++barrel)
            {
                const auto* marker=weapon_muzzle::Find(GameTitle::Halo3ODST,checksum,barrel);
                weapon_muzzle::Ray ray{};
                if(!marker || marker->nodeCount!=count || marker->node>=count)continue;
                const auto& node=destination[marker->node];
                if(!weapon_muzzle::Transform(*marker,node.scale,node.rotation,node.translation,ray))continue;
                // Defaults also rotate the visible wrist when sliders are zero.
                // Remove that visual frame before applying the explicit barrel trim.
                BoneMatrix visual{},neutral{};float scale=1;
                if (!DesiredWristWorld(context.slot==1,visual,scale,nullptr,&neutral) ||
                    !weapon_alignment::Reframe(visual.rotation,visual.translation,
                        neutral.rotation,neutral.translation,ray) ||
                    !weapon_alignment::ApplyOdstBarrelDefaults(checksum,neutral.rotation,ray))continue;
                palette.barrels[barrel]={GameTitle::Halo3ODST,generation,unit,weapon,checksum,
                    tracking.referenceEpoch,tracking.serial,now,tracking.timeNs,uint8_t(context.slot),
                    barrel,g_config.left_handed,ray};
            }
        }
        (void)g_odstMuzzles.Publish(GameTitle::Halo3ODST,uint8_t(context.slot),palette);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { (void)g_odstMuzzles.Publish(GameTitle::Halo3ODST,uint8_t(context.slot),{}); }
}
