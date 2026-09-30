#include "odst_finger_pose_backend.inl"
namespace odst_finger_runtime {
std::atomic<uint64_t> applications{0},refusals{0},faults{0};
std::atomic<uint32_t> faultedGeneration{0};
bool Prepare(uint16_t tag,const FpInterpolationContext& context,uint32_t generation,
    odst_fingers::Pose& inverse,odst_fingers::Request& request) {
    if(!g_config.experimental_body_ik||!generation||faultedGeneration.load()==generation||
       !g_odstCamera.armed.load()||g_odstCamera.teardownRequested.load()||
       generation!=g_odstRuntimeGeneration.load()||context.generation!=generation||
       !context.valid||context.player!=0||context.slot!=0||!g_fpStereoSolveScope.armed||
       g_scopeRenderActive.load()||!g_enabled.load()||!g_vrAim.load()||!VR_IsStereoEnabled()||
       VR_IsCutsceneTheaterActive()||exclusive_input::Active()||
       TitleAdapter_GetActiveTitle()!=GameTitle::Halo3ODST||TitleAdapter_GetRuntimeMode()!=RuntimeMode::Gameplay)return false;
    const auto& tracking=g_fpStereoSolveScope.anatomicalTracking;
    VrContactTrackingSnapshot current{};
    if(!tracking.serial||!tracking.referenceEpoch||tracking.timeNs<=0||!tracking.headValid||
       !tracking.hands[0].valid||!VR_GetContactTrackingSnapshot(current)||
       current.serial!=tracking.serial||current.referenceEpoch!=tracking.referenceEpoch||
       !g_odstPlayerUnitGetter||!g_odstEngineTlsIndex||*g_odstEngineTlsIndex>=0x200)return false;
    const auto* model=OdstLoadedTagDefinition(tag);if(!model)return false;
    std::memcpy(&request.checksum,model+8,4);
    std::memcpy(&request.count,model+kOdstRenderModelNodesCountOffset,4);
    AnatomicalPalmMarkers markers{};
    if(!OdstAnatomicalPalmMarkers(request.checksum,int(request.count),markers))return false;
    const uint32_t unit=uint32_t(g_odstPlayerUnitGetter(0));
    int32_t scene=-1,shot=-1;uint32_t weapons[2]{};
    if(!OdstMuzzleTargetStorage(unit)||ReadOdstCinematicControl(scene,shot)!=CinematicControlState::PlayerControlled||
       !OdstReadMuzzleWeapons(unit,weapons))return false;
    // Retained ODSTEK/reload proof: native FP slot's actual weapon must still
    // match the live full-salt inventory owner at the committed palette.
    const auto* tls=OdstContactTls();
    const auto* users=tls?*reinterpret_cast<const uint8_t* const*>(tls+0x598):nullptr;
    if(!users||*reinterpret_cast<const uint32_t*>(users+0x3C)!=weapons[0])return false;
    SupportGripRelationshipSnapshot support{};
    const bool attached=VR_GetSupportGripRelationship(support)&&support.engaged&&
        support.title==GameTitle::Halo3ODST&&support.generation==generation&&
        support.unit==unit&&support.weapon==weapons[0];
    if(!odst_fingers::ConfigureFreeSupport(request,tracking.handAlignment,weapons[1]!=UINT32_MAX,
        attached||tracking.twoHandAimActive||g_fpStereoSolveScope.twoHandAimActive,tracking.controllerFingers[0]))return false;
    const auto* base=g_odstTagDataBase?static_cast<const uint8_t*>(*g_odstTagDataBase):nullptr;
    uint32_t address=0;std::memcpy(&address,model+kOdstRenderModelNodesDataOffset,4);
    if(!base||!address)return false;
    const auto* nodes=base+size_t(address)*4;
    for(unsigned node=0;node<odst_fingers::Count;++node)
        std::memcpy(&inverse[node],nodes+size_t(node)*kOdstRenderModelNodeStride+kOdstRenderModelNodeInverseOffset,sizeof(inverse[node]));
    return true;
}
}
__declspec(noinline) void OdstApplyControllerFingers(uint16_t tag,const FpInterpolationContext& context,
    BoneMatrix* destination,uint32_t generation) noexcept {
    using namespace odst_finger_runtime;
    if(!g_config.experimental_body_ik||!destination)return;
    odst_fingers::Pose inverse{};odst_fingers::Request request{};
    volatile bool fault=false;bool prepared=false;
    __try {prepared=Prepare(tag,context,generation,inverse,request);}
    __except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault){faultedGeneration.store(generation);faults.fetch_add(1);return;}
    if(!prepared)return;
    static_assert(sizeof(BoneMatrix)==sizeof(odst_fingers::Matrix));
    const auto result=odst_fingers::Apply(reinterpret_cast<odst_fingers::Matrix*>(destination),inverse,request);
    if(result==odst_fingers::Result::Applied)applications.fetch_add(1,std::memory_order_relaxed);
    else if(result==odst_fingers::Result::Faulted){faultedGeneration.store(generation);faults.fetch_add(1);}
    else if(result==odst_fingers::Result::Refused)refusals.fetch_add(1,std::memory_order_relaxed);
}
void ReportOdstControllerFingers() {
    using namespace odst_finger_runtime;
    if(!g_config.experimental_body_ik)faultedGeneration.store(0);
    static uint64_t last[3]{};static ULONGLONG reportedAt=0;
    const uint64_t now[]{applications.load(),refusals.load(),faults.load()};const auto clock=GetTickCount64();
    if(std::memcmp(last,now,sizeof(now))&&(clock-reportedAt>=5000||now[2]!=last[2])) {
        std::memcpy(last,now,sizeof(now));reportedAt=clock;
        LOG("ODST controller fingers: applied=%llu refused=%llu optional-faults=%llu; authored weapon/support grips retained; full-world avatar not implemented",now[0],now[1],now[2]);
    }
}
