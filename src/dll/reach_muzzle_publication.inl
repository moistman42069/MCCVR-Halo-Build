// Apart from the options: co-op safe shots stands them down, and the authored grip point goes on needing this.
bool ReachCaptureGripOwner(int view,int slot,uint32_t& unit,uint32_t& weapon) noexcept
{
    if(!g_reachBarrelBindingsReady.load(std::memory_order_acquire)||view!=0||slot<0||slot>1||
        !g_reachCamera.playerUnitByOutputUser||!g_reachCamera.unitInVehicle)return false;
    __try
    {
        const uint32_t owner=uint32_t(g_reachCamera.playerUnitByOutputUser(0));uint32_t weapons[2]{};
        if(!ReachMuzzleTargetStorage(owner)||!ReachReadMuzzleWeapons(owner,weapons)||weapons[slot]==UINT32_MAX)return false;
        auto** slots=reinterpret_cast<uint8_t**>(__readgsqword(0x58));
        const auto index=*reinterpret_cast<const uint32_t*>(g_reachCamera.base+kReachEngineTlsIndexRva);
        const auto* tls=slots&&index<0x200?slots[index]:nullptr;
        // HREK 8CEED0 / retail 2B1218: TLS6A0, user53A8, slot2978, full weapon3C.
        const auto* users=tls?*reinterpret_cast<const uint8_t* const*>(tls+0x6A0):nullptr;
        if(!users||*reinterpret_cast<const uint32_t*>(users+slot*0x2978+0x3C)!=weapons[slot])return false;
        unit=owner;weapon=weapons[slot];return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReachCaptureMuzzleOwner(int view,int slot,uint32_t& unit,uint32_t& weapon) noexcept
{
    return (Sight_GunBarrelAim()||Sight_ShotsFromMuzzle())&&ReachCaptureGripOwner(view,slot,unit,weapon);
}
// WHY NO MUZZLE WAS PUBLISHED (dev builds; ReportReachMuzzle prints the window). The barrel lane reads the muzzle
// this publishes; when it finds none it refuses the shot with "no muzzle publication", and these say why.
enum ReachMuzzlePublishWhy : int
{
    kReachPublishOptionOff,kReachPublishContext,kReachPublishPairScope,kReachPublishModelUnread,
    kReachPublishModelUnknown,kReachPublishNoOwner,kReachPublishOtherOwner,kReachPublishNoTracking,
    kReachPublishNode,kReachPublishTransform,kReachPublishFault,kReachPublishCount
};
#if HALOMCCVR_DEV_DIAGNOSTICS
struct ReachMuzzlePublishCounts
{
    std::atomic<uint32_t> why[kReachPublishCount]{};
    std::atomic<uint32_t> calls{0},barrels{0};
    // The last first-person model that is not in the marker table (checksum, node count), for the log.
    std::atomic<uint32_t> unknownChecksum{0},unknownNodes{0};
} g_reachMuzzlePublishWhy;
inline void ReachMuzzleNotPublished(ReachMuzzlePublishWhy why)
{g_reachMuzzlePublishWhy.why[why].fetch_add(1,std::memory_order_relaxed);}
#else
inline void ReachMuzzleNotPublished(ReachMuzzlePublishWhy){}
#endif
void ReachPublishMuzzlePalette(uint16_t tag,const ReachFpInterpolationContext& context,const BoneMatrix* destination) noexcept
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    g_reachMuzzlePublishWhy.calls.fetch_add(1,std::memory_order_relaxed);
#endif
    if(!(Sight_GunBarrelAim()||Sight_ShotsFromMuzzle())||!g_reachBarrelBindingsReady.load(std::memory_order_acquire))
    {ReachMuzzleNotPublished(kReachPublishOptionOff);return;}
    if(!context.valid||context.interpolationView!=0||context.interpolationSlot!=0||!destination)
    {ReachMuzzleNotPublished(kReachPublishContext);return;}
    if(!g_reachFpPairScope.armed||!context.generation||context.generation!=g_reachCamera.generation.load()||
        context.preparedSerial!=g_reachFpPairScope.preparedSerial)
    {ReachMuzzleNotPublished(kReachPublishPairScope);return;}
    __try
    {
        uint32_t checksum=0;int count=0;
        if(!ReachReadRenderModelIdentity(tag,checksum,count)||count<=0||count>64)
        {ReachMuzzleNotPublished(kReachPublishModelUnread);return;}
        if(!weapon_muzzle::Find(GameTitle::HaloReach,checksum,0)&&!weapon_muzzle::Find(GameTitle::HaloReach,checksum,1))
        {
#if HALOMCCVR_DEV_DIAGNOSTICS
            g_reachMuzzlePublishWhy.unknownChecksum.store(checksum,std::memory_order_relaxed);
            g_reachMuzzlePublishWhy.unknownNodes.store(uint32_t(count),std::memory_order_relaxed);
#endif
            ReachMuzzleNotPublished(kReachPublishModelUnknown);return;
        }
        weapon_muzzle::Palette palette{};const auto& tracking=context.targets;
        uint32_t unit=UINT32_MAX,weapon=UINT32_MAX;
        if(!ReachCaptureMuzzleOwner(0,0,unit,weapon))ReachMuzzleNotPublished(kReachPublishNoOwner);
        else if(unit!=context.muzzleUnit||weapon!=context.muzzleWeapon)ReachMuzzleNotPublished(kReachPublishOtherOwner);
        else if(!(tracking.contactSerial&&tracking.contactReference&&tracking.contactSpaceEpoch&&
            tracking.contactTimeNs>0&&tracking.rightWristValid))
            ReachMuzzleNotPublished(kReachPublishNoTracking);
        else
        {
            const auto now=GetTickCount64();
            for(uint8_t barrel=0;barrel<2;++barrel)
            {
                const auto* marker=weapon_muzzle::Find(GameTitle::HaloReach,checksum,barrel);weapon_muzzle::Ray ray{};
                if(!marker)continue;
                if(marker->nodeCount!=count||marker->node>=count){ReachMuzzleNotPublished(kReachPublishNode);continue;}
                const auto& node=destination[marker->node];
                if(!weapon_muzzle::Transform(*marker,node.scale,node.rotation,node.translation,ray))
                {ReachMuzzleNotPublished(kReachPublishTransform);continue;}
                auto neutral=tracking.aimNeutral,visual=tracking.aimVisual;
                for(int i=0;i<3;++i) {
                    const float delta=tracking.rightWrist.translation[i]-visual.translation[i];
                    neutral.translation[i]+=delta;visual.translation[i]+=delta;
                }
                if (!weapon_alignment::Reframe(visual.rotation,visual.translation,
                        neutral.rotation,neutral.translation,ray)) {
                    ReachMuzzleNotPublished(kReachPublishTransform);continue;
                }
                // Rotate only the barrel ray, in the same neutral controller
                // frame used above. Keep the muzzle origin fixed.
                if(!weapon_alignment::ApplyReachBarrelDefaults(checksum,neutral.rotation,ray)) {
                    ReachMuzzleNotPublished(kReachPublishTransform);continue;
                }
                // THE RECEIPT'S SPACE IS THE TRACKING-SPACE EPOCH, the number the barrel lane reads it back with
                // (VR_GetContactTrackingSnapshot's referenceEpoch, as Halo 3's publication does). NOT
                // contactReference: that is Reach's melee HASH of the epoch, which no epoch ever equals, so every
                // barrel shot would be refused "no muzzle publication" while this publishes a muzzle for every
                // first-person palette.
                palette.barrels[barrel]={GameTitle::HaloReach,context.generation,unit,weapon,checksum,
                    tracking.contactSpaceEpoch,tracking.contactSerial,now,tracking.contactTimeNs,0,barrel,g_config.left_handed,ray};
#if HALOMCCVR_DEV_DIAGNOSTICS
                g_reachMuzzlePublishWhy.barrels.fetch_add(1,std::memory_order_relaxed);
#endif
            }
        }
        (void)g_reachBarrelMuzzles.Publish(GameTitle::HaloReach,0,palette);
    }
    __except(EXCEPTION_EXECUTE_HANDLER){ReachMuzzleNotPublished(kReachPublishFault);(void)g_reachBarrelMuzzles.Publish(GameTitle::HaloReach,0,{});}
}
