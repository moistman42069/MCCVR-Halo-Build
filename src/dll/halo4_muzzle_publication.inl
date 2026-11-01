struct Halo4MuzzlePending
{
    bool admitted=false;
    uint32_t generation=0,unit=UINT32_MAX,weapon=UINT32_MAX;
    uint8_t slot=0;
    weapon_muzzle::Palette palette{};
};
Halo4MuzzlePending Halo4PrepareMuzzlePalette(uint32_t weapon,uint16_t renderModelIndex,
    uint32_t checksum,int count,const BoneMatrix* palette)
{
    Halo4MuzzlePending pending{};
    if(!(Sight_GunBarrelAim()||Sight_ShotsFromMuzzle())||count<=0||count>kHalo4FirstPersonBankTransforms||
        !Halo4FloatingPairMatchesCurrent())return pending;
    __try
    {
        if(!Halo4CaptureMuzzleOwner(weapon,pending.unit,pending.slot))return pending;
        pending.admitted=true;pending.weapon=weapon;pending.generation=g_halo4FloatingPair.generation;
        if(!palette)return pending;
        const auto* fp=Halo4VehicleRead<const uint8_t*>(Halo4MuzzleTls(),0x6A0);
        if(Halo4VehicleRead<uint16_t>(fp,pending.slot*0x2EC8+0x74)!=renderModelIndex)return pending;
        const auto& tracking=g_halo4FloatingPair.contactFrames[pending.slot==0?1:0];
        if(!tracking.serial||tracking.serial!=g_halo4FloatingPair.preparedSerial||
            !tracking.referenceEpoch||tracking.timeNs<=0)return pending;
        const auto now=GetTickCount64();
        for(uint8_t barrel=0;barrel<2;++barrel)
        {
            const auto* marker=weapon_muzzle::Find(GameTitle::Halo4,checksum,barrel);weapon_muzzle::Ray ray{};
            if(!marker||marker->nodeCount!=count||marker->node>=count)continue;
            const auto& node=palette[marker->node];
            if(!weapon_muzzle::Transform(*marker,node.scale,node.rotation,node.translation,ray))continue;
            if (pending.slot==0) {
                const bool oneHand=VR_SupportGripWiredForTitle(GameTitle::Halo4) &&
                    !g_halo4FloatingPair.supportGripAttached && g_halo4FloatingPair.oneHandRightTargetValid &&
                    support_grip::AimSupportDerived(g_halo4FloatingPair.rightTargetValid,g_halo4FloatingPair.twoHandAimActive);
                if (!(oneHand?g_halo4FloatingPair.oneHandAimNeutralValid:g_halo4FloatingPair.aimNeutralValid)) continue;
                auto visual=oneHand?g_halo4FloatingPair.oneHandAimVisual:g_halo4FloatingPair.aimVisual;
                auto neutral=oneHand?g_halo4FloatingPair.oneHandAimNeutral:g_halo4FloatingPair.aimNeutral;
                const auto& current=oneHand?g_halo4FloatingPair.oneHandRightTargetWorld:g_halo4FloatingPair.rightTargetWorld;
                for(int i=0;i<3;++i) {
                    const float delta=current.translation[i]-visual.translation[i];
                    visual.translation[i]+=delta;neutral.translation[i]+=delta;
                }
                if (!weapon_alignment::Reframe(visual.rotation,visual.translation,
                        neutral.rotation,neutral.translation,ray)) continue;
            }
            pending.palette.barrels[barrel]={GameTitle::Halo4,pending.generation,pending.unit,weapon,checksum,
                tracking.referenceEpoch,tracking.serial,now,tracking.timeNs,pending.slot,barrel,g_config.left_handed,ray};
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER){pending.palette={};}
    return pending;
}
void Halo4CommitMuzzlePalette(const Halo4MuzzlePending& pending)
{
    if(!pending.admitted)return;
    weapon_muzzle::Palette palette{};
    __try
    {
        uint32_t owner=UINT32_MAX;uint8_t slot=0;
        if(pending.generation==g_halo4Camera.generation.load()&&Halo4FloatingPairMatchesCurrent()&&
            Halo4CaptureMuzzleOwner(pending.weapon,owner,slot)&&owner==pending.unit&&slot==pending.slot)
            palette=pending.palette;
    }
    __except(EXCEPTION_EXECUTE_HANDLER){}
    (void)g_halo4BarrelMuzzles.Publish(GameTitle::Halo4,pending.slot,palette);
}
