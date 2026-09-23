// H2 publishes its committed render-model palette, after carrier trimming and
// collision correction. Never substitute the controller or an unclamped node.
weapon_muzzle::Store g_halo2Muzzles;
void Halo2PublishMuzzlePalette(const Halo2VisibleConsumerContext& context,uint8_t slot,
    uint64_t identity,const float* matrices,uint32_t count) noexcept
{
    weapon_muzzle::Palette palette{};
    const uint32_t weapon=slot==0?context.weaponObject:context.secondaryWeaponObject;
    if(context.valid && context.user==kOwnedUser && context.muzzleGeneration &&
        context.muzzleGeneration==g_generation.load(std::memory_order_acquire) &&
        context.unitObject!=UINT32_MAX && weapon!=UINT32_MAX &&
        matrices && count && count<=kHalo2FirstPersonPaletteCapacity &&
        context.muzzleSpace && context.muzzleSerial && context.muzzleTimeNs>0)
    {
        const uint64_t now=GetTickCount64();
        for(uint8_t barrel=0;barrel<2;++barrel)
        {
            const auto* marker=weapon_muzzle::Find(GameTitle::Halo2,identity,barrel);
            Halo2FirstPersonTransform node{};
            weapon_muzzle::Ray ray{};
            if(!marker || marker->nodeCount!=count || marker->node>=count ||
                !Halo2ReadFirstPersonTransform(matrices+marker->node*kHalo2FirstPersonNodeFloats,node) ||
                !weapon_muzzle::Transform(*marker,node.scale,node.rotation,node.translation,ray))continue;
            palette.barrels[barrel]={GameTitle::Halo2,context.muzzleGeneration,context.unitObject,
                weapon,identity,context.muzzleSpace,context.muzzleSerial,now,context.muzzleTimeNs,
                slot,barrel,context.muzzleLeftHanded,ray};
        }
    }
    (void)g_halo2Muzzles.Publish(GameTitle::Halo2,slot,palette);
    if(slot<2 && context.contactFrames[slot==0?1:0].serial==context.muzzleSerial)
        VR_PublishWeaponReticleRay(GameTitle::Halo2,slot,palette.barrels[0],
            context.contactFrames[slot==0?1:0].transform);
}
