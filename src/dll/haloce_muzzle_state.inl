// HCEEK-derived native target record is 0x10 bytes, not a later-title target.
using MuzzleFireFn=void(__fastcall*)(uint32_t,int16_t,uint32_t,int16_t,const void*,uint8_t);
using MuzzleMarkersFn=int16_t(__fastcall*)(uint32_t,const char*,void*,int16_t);
using MuzzleQueryFn=void(__fastcall*)(int32_t,int16_t,Vec3*,void*);
using MuzzleDirectQueryFn=void(__fastcall*)(int32_t,int16_t,void*);
Hook muzzleFireHook,muzzleMarkersHook,muzzleQueryHook,muzzleDirectQueryHook;
std::atomic<bool> muzzleInstalled{},muzzleFaulted{};
bool muzzleRetiring{};uint32_t muzzleFailedGeneration{};
uint64_t muzzleInstalledAt{};
std::atomic<uint64_t> muzzleApplied{},muzzleRefused{},muzzleRestoreRefused{};
struct MuzzlePalette
{RenderContext context{};weapon_muzzle::Palette palette{};uint32_t unit=UINT32_MAX,weapon=UINT32_MAX;};
Snapshot<MuzzlePalette> muzzlePalette;
struct MuzzleQueryContext
{uint32_t generation{},unit=UINT32_MAX;int32_t input=-1;int16_t zoom=-1;bool direct{};uint64_t at{};};
struct MuzzleShot
{bool active{},query{},queryApplied{};uint32_t unit=UINT32_MAX;Vec3 position{},direction{};};
struct MuzzleRequest
{uint32_t weapon=UINT32_MAX;int16_t barrel=-1;bool replicated{};NativeShotTargetLease<0x10>* lease{};};
thread_local MuzzleQueryContext muzzleQueryContext;
thread_local MuzzleShot muzzleShot;
thread_local MuzzleRequest muzzleRequest;
bool MuzzleShotMatches(uint32_t unit) noexcept
{return muzzleShot.active&&!muzzleShot.query&&muzzleShot.unit==unit;}
bool MuzzleOwner(uint32_t unit,uint32_t weapon) noexcept
{
    HaloCELocalPlayerState state{};
    return LocalOnFootShooter(unit)&&HaloCEControls_GetLocalPlayerState(state)&&
        state.unit==unit&&state.weapon==weapon&&state.generation==generation.load();
}
void PublishMuzzlePalette(const Scope& owner,const FirstPersonBinding& binding,
    const NodeMatrix* nodes,uint64_t now) noexcept
{
    MuzzlePalette pending{};const auto& context=owner.context;
    if(muzzleInstalled.load()&&!muzzleFaulted.load()&&
        (context.tracking.controllers.gunBarrelAim||context.tracking.controllers.muzzleShots)&&
        nodes&&binding.count&&MuzzleOwner(owner.muzzleUnit,owner.muzzleWeapon))
    {
        pending.context=context;pending.unit=owner.muzzleUnit;pending.weapon=owner.muzzleWeapon;
        for(uint8_t barrel=0;barrel<2;++barrel)
        {
            const auto* marker=weapon_muzzle::Find(GameTitle::HaloCE,binding.nodeIdentity,barrel);
            if(!marker||marker->nodeCount!=binding.count||marker->node>=binding.count)continue;
            const auto& node=nodes[marker->node];weapon_muzzle::Ray ray{};
            const float basis[]{node.forward.x,node.forward.y,node.forward.z,
                node.left.x,node.left.y,node.left.z,node.up.x,node.up.y,node.up.z};
            if(!weapon_muzzle::Transform(*marker,node.scale,basis,&node.position.x,ray))continue;
            NodeMatrix visual{},neutral{};
            if (!BuildControllerMatrix(context.camera,context.tracking,context.reference,
                    context.tracking.controllers.primaryAim,context.unitsPerMeter,context.positional,visual)) continue;
            neutral=visual;auto neutralRig=context.tracking.controllers;
            neutralRig.visualPitchDeg=neutralRig.visualYawDeg=neutralRig.visualRollDeg=0;
            if (!ApplyVisualMount(context.tracking.controllers,context.unitsPerMeter,true,visual) ||
                !ApplyVisualMount(neutralRig,context.unitsPerMeter,true,neutral)) continue;
            const float visualBasis[]{visual.forward.x,visual.forward.y,visual.forward.z,
                visual.left.x,visual.left.y,visual.left.z,visual.up.x,visual.up.y,visual.up.z};
            const float neutralBasis[]{neutral.forward.x,neutral.forward.y,neutral.forward.z,
                neutral.left.x,neutral.left.y,neutral.left.z,neutral.up.x,neutral.up.y,neutral.up.z};
            if (!weapon_alignment::Reframe(visualBasis,&visual.position.x,
                    neutralBasis,&neutral.position.x,ray)) continue;
            pending.palette.barrels[barrel]={GameTitle::HaloCE,context.tracking.generation,pending.unit,pending.weapon,
                binding.nodeIdentity,context.tracking.spaceEpoch,context.tracking.serial,now,
                context.tracking.predictedDisplayTimeNs,0,barrel,context.tracking.controllers.leftHanded,ray};
        }
    }
    (void)muzzlePalette.Publish(pending);
}
// forShot: a barrel SHOT may also use the muzzle for shots from the muzzle; continuous target acquisition
// (TargetQueryBody) follows the barrel only under the barrel aim itself.
bool ReadMuzzle(uint32_t unit,uint32_t weapon,int16_t barrel,weapon_muzzle::Receipt& out,bool forShot=false) noexcept
{
    MuzzlePalette sample{};RenderContext live{},committed{};
    if(barrel<0||barrel>=2||!muzzleInstalled.load()||muzzleFaulted.load()||!AimCurrent()||
        !CurrentPaletteContext(committed)||!HaloCE_GetGameplayContext(live)||
        !(live.tracking.controllers.gunBarrelAim||(forShot&&live.tracking.controllers.muzzleShots))||
        live.tracking.controllers.controlsPresentationBlocked||
        !live.tracking.controllers.primaryAim.valid||!MuzzleOwner(unit,weapon)||!muzzlePalette.Read(sample)||
        sample.unit!=unit||sample.weapon!=weapon||!HaloCE_RenderContextCurrent(sample.context)||
        sample.context.rendererEpoch!=live.rendererEpoch||sample.context.referenceRevision!=live.referenceRevision)
        return false;
    const auto& candidate=sample.palette.barrels[barrel];
    if(!weapon_muzzle::Fresh(candidate,GameTitle::HaloCE,live.tracking.generation,unit,weapon,
        live.tracking.spaceEpoch,GetTickCount64(),live.tracking.predictedDisplayTimeNs,0,uint8_t(barrel),
        live.tracking.controllers.leftHanded))return false;
    out=candidate;return true;
}
void* MuzzleTargetStorage(uint32_t unit)
{
    if(!LocalOnFootShooter(unit))return nullptr;
    using ObjectFn=uintptr_t(__fastcall*)(uint32_t,uint32_t);
    const auto object=reinterpret_cast<ObjectFn>(moduleBase+contract::player_state::state_object_try_get)(unit,3);
    // Own HCEEK 8EFD90 and retail B7A374: parent308, target1E0.
    if(!object||*reinterpret_cast<const uint32_t*>(object+0x308)!=UINT32_MAX)return nullptr;
    return reinterpret_cast<void*>(object+0x1E0);
}
void RestoreMuzzleTarget(NativeShotTargetLease<0x10>& lease)
{
    if(!lease.active)return;
    __try
    {if(!lease.Restore(generation.load(),lease.owner,MuzzleTargetStorage(lease.owner)))++muzzleRestoreRefused;}
    __except(EXCEPTION_EXECUTE_HANDLER)
    {lease.active=false;muzzleFaulted=true;++muzzleRestoreRefused;}
}
