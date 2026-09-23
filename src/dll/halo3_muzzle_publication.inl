// H3's own FP users/slots and weapon-handle member, independently established
// in native reload-tail evidence. Capture before AND after interpolation, so
// a later inventory lookup cannot attach an old palette to a replacement gun.
bool Halo3CaptureMuzzleOwner(int player,int slot,uint32_t& unit,uint32_t& weapon) noexcept
{
    if(!g_config.gun_barrel_aim || !g_halo3MuzzleBindingsReady.load(std::memory_order_acquire) ||
        player!=0 || slot<0 || slot>1 || !g_engineTlsIndex || !g_halo3PlayerUnitGetter ||
        !g_halo3UnitInVehicle)return false;
    __try
    {
        auto** slots=reinterpret_cast<uint8_t**>(__readgsqword(0x58));
        const auto* tls=slots?slots[*g_engineTlsIndex]:nullptr;
        const auto* users=tls?*reinterpret_cast<const uint8_t* const*>(tls+0x568):nullptr;
        if(!users)return false;
        const uint32_t owner=uint32_t(g_halo3PlayerUnitGetter(0));
        uint32_t weapons[2]{};
        if(owner==UINT32_MAX || g_halo3UnitInVehicle(int32_t(owner)) ||
            !Halo3ReadOwnedWeapons(owner,weapons,false) || weapons[slot]==UINT32_MAX ||
            *reinterpret_cast<const uint32_t*>(users+slot*0x11BC+0x3C)!=weapons[slot])return false;
        unit=owner;weapon=weapons[slot];return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

void Halo3PublishMuzzlePalette(uint16_t tag,const FpInterpolationContext& context,
    const BoneMatrix* destination,uint32_t generation) noexcept
{
    if(!g_config.gun_barrel_aim || !g_halo3MuzzleBindingsReady.load(std::memory_order_acquire) ||
        !context.valid || context.player!=0 || context.slot<0 || context.slot>1 ||
        !generation || context.generation!=generation || generation!=g_halo3RuntimeGeneration.load() ||
        !destination || !g_fpStereoSolveScope.armed)return;
    __try
    {
        uint32_t checksum=0;
        const int count=LegacyAnatomicalRenderNodeCount(GameTitle::Halo3,tag,&checksum);
        if(count<=0 || count>64 || (!weapon_muzzle::Find(GameTitle::Halo3,checksum,0)&&
            !weapon_muzzle::Find(GameTitle::Halo3,checksum,1)))return;
        weapon_muzzle::Palette palette{};
        const auto& tracking=g_fpStereoSolveScope.anatomicalTracking;
        uint32_t unit=UINT32_MAX,weapon=UINT32_MAX;
        if(Halo3CaptureMuzzleOwner(context.player,context.slot,unit,weapon) &&
            unit==context.muzzleUnit && weapon==context.muzzleWeapon &&
            tracking.serial && tracking.referenceEpoch && tracking.timeNs>0 &&
            tracking.hands[context.slot==0?1:0].valid)
        {
            const uint64_t now=GetTickCount64();
            for(uint8_t barrel=0;barrel<2;++barrel)
            {
                const auto* marker=weapon_muzzle::Find(GameTitle::Halo3,checksum,barrel);
                weapon_muzzle::Ray ray{};
                if(!marker || marker->nodeCount!=count || marker->node>=count)continue;
                const auto& node=destination[marker->node];
                if(!weapon_muzzle::Transform(*marker,node.scale,node.rotation,node.translation,ray))continue;
                palette.barrels[barrel]={GameTitle::Halo3,generation,unit,weapon,checksum,
                    tracking.referenceEpoch,tracking.serial,now,tracking.timeNs,uint8_t(context.slot),
                    barrel,g_config.left_handed,ray};
            }
        }
        (void)g_halo3Muzzles.Publish(GameTitle::Halo3,uint8_t(context.slot),palette);
        // Same accepted XR-to-world mapping as the H3 visible carrier and
        // reload receiver. Project only after this palette has committed.
        if(g_baseCamValid.load()) {
            contact_melee::TrackingToWorld transform{};
            const float sh=sinf(g_headYawRef),ch=cosf(g_headYawRef);
            const float cg=cosf(g_gameYawRef),sg=sinf(g_gameYawRef);
            transform.unitsPerMetre=g_worldScale.load();
            transform.axis[0]={cg*sh+sg*ch,sg*sh-cg*ch,0};
            transform.axis[1]={0,0,1};
            transform.axis[2]={-cg*ch+sg*sh,-sg*ch-cg*sh,0};
            const auto reference=transform.World({g_headPosRef[0],g_headPosRef[1],g_headPosRef[2]});
            transform.origin={g_baseCamX.load()-reference.x,g_baseCamY.load()-reference.y,
                g_baseCamZ.load()-reference.z};
            VR_PublishWeaponReticleRay(GameTitle::Halo3,uint8_t(context.slot),palette.barrels[0],transform);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { (void)g_halo3Muzzles.Publish(GameTitle::Halo3,uint8_t(context.slot),{}); }
}
