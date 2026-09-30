#include "halo3_avatar_backend.inl"
namespace halo3_avatar_native {
template<class T>T Read(const void* p,size_t offset=0) {T result{};std::memcpy(&result,static_cast<const uint8_t*>(p)+offset,sizeof(result));return result;}
const uint8_t* Block(const void* p,size_t offset) {
    const uint32_t encoded=Read<uint32_t>(p,offset);
    const auto* base=g_halo3TagDataBase?static_cast<const uint8_t*>(*g_halo3TagDataBase):nullptr;
    return base&&encoded?base+size_t(encoded)*4:nullptr;
}
bool Current() noexcept {
    return runtime.enabled.load(std::memory_order_acquire)&&!runtime.faulted.load(std::memory_order_acquire)&&
        runtime.generation.load(std::memory_order_acquire)==g_halo3RuntimeGeneration.load(std::memory_order_acquire)&&
        g_config.experimental_body_ik&&g_enabled.load()&&g_vrAim.load()&&VR_IsStereoEnabled()&&
        !g_scopeRenderActive.load()&&!VR_IsCutsceneTheaterActive()&&!exclusive_input::Active()&&
        TitleAdapter_GetActiveTitle()==GameTitle::Halo3&&TitleAdapter_GetRuntimeMode()==RuntimeMode::Gameplay;
}
bool Capture(uint32_t unit,void* info,Candidate& out) {
    if(!info||unit!=pair.unit||Read<uint32_t>(info,0x48)!=unit||
       (Read<uint32_t>(info,0x58)&0x10)||!runtime.world)return false;
    Candidate candidate{};candidate.info=info;candidate.unit=unit;
    candidate.tag=Read<uint32_t>(info,4);candidate.designator=Read<uint32_t>(info,8);
    if(candidate.tag==UINT32_MAX||int32_t(candidate.designator)<0)return false;
    const auto* model=Halo3LoadedTagDefinition(candidate.tag);if(!model)return false;
    if(!halo3_avatar::SelectProfile(Read<uint32_t>(model,8),Read<uint32_t>(model,0x30),candidate.profile)||
       Read<uint32_t>(model,0xB0)!=0||Read<uint32_t>(info)!=candidate.profile.regions)return false;
    const uint8_t* nodes=Block(model,0x34);if(!nodes)return false;
    uint32_t observedTag=UINT32_MAX,count=0;const void* source=nullptr;
    runtime.world(unit,Read<uint8_t>(info,0x2E),&observedTag,&source,&count);
    if(observedTag!=candidate.tag||count!=candidate.profile.count||!source)return false;
    std::memcpy(candidate.source.data(),source,count*sizeof(halo3_avatar::Matrix));
    for(unsigned i=0;i<count;++i) {
        candidate.inverse[i]=Read<halo3_avatar::Matrix>(nodes,size_t(i)*0x60+0x28);
        if(!Halo4FloatingTransformValid(candidate.source[i])||!Halo4FloatingTransformValid(candidate.inverse[i]))return false;
    }
    candidate.occupied=true;out=candidate;return true;
}
bool IsOwnFpRow(void* model,void* info) {
    if(!model||!info||(Read<uint32_t>(info,0x58)&0x10)==0)return false;
    const uint32_t object=Read<uint32_t>(info,0x48);
    if(object!=pair.unit) {
        uint32_t weapons[2]{};
        if(!Halo3ReadOwnedWeapons(pair.unit,weapons,false)||
           (object!=weapons[0]&&object!=weapons[1])||object==UINT32_MAX)return false;
    }
    const uint32_t checksum=Read<uint32_t>(model,8),count=Read<uint32_t>(model,0x30);
    AnatomicalPalmMarkers markers{};
    if(Halo3AnatomicalPalmMarkers(checksum,int(count),markers))return true;
    // Official tag exports: these FP_body palettes contain lower body only.
    return (checksum==353376263u&&count==51)||
        (checksum==319362069u&&count==55)||(checksum==269747456u&&count==55);
}
bool Prepare(Candidate& candidate) {
    if(!PairCurrent()||!pair.wristsValid||pair.fpChecksum!=candidate.profile.fpChecksum||
       pair.fpCount!=candidate.profile.fpCount||Read<uint32_t>(candidate.info,0x48)!=pair.unit||
       Read<uint32_t>(candidate.info,4)!=candidate.tag||Read<uint32_t>(candidate.info,8)!=candidate.designator)return false;
    halo3_avatar::Pose result{};
    auto request=pair.request;request.fingerInverseBind=&candidate.inverse;
    if(!halo3_avatar::Solve(candidate.profile,candidate.source,request,result)||
       !halo3_avatar::BuildGpuPose(candidate.profile,result,candidate.inverse,candidate.gpu))return false;
    candidate.ready=true;runtime.posed.fetch_add(1,std::memory_order_relaxed);return true;
}
bool SelectUpload(void* mesh,UploadScope& scope) {
    if(!mesh)return false;
    const auto* info=Read<const uint8_t*>(mesh,8);if(!info)return false;
    Candidate* candidate=nullptr;
    for(auto& c:pair.rows)if(c.ready&&c.info==info){candidate=&c;break;}
    if(!candidate||Read<uint32_t>(info,0x48)!=pair.unit||Read<uint32_t>(info,4)!=candidate->tag||
       Read<uint32_t>(info,8)!=candidate->designator)return false;
    const unsigned region=Read<uint16_t>(mesh,0x1C),meshIndex=Read<uint16_t>(mesh,0x1A);
    if(region>=candidate->profile.regions||Read<int16_t>(info,0xE+region*2)<0)return false;
    const auto* model=Halo3LoadedTagDefinition(candidate->tag);if(!model)return false;
    const uint32_t meshCount=Read<uint32_t>(model,0x68);
    const auto* meshes=Block(model,0x6C);
    if(!meshes||meshIndex>=meshCount||meshCount>4096)return false;
    const size_t offset=size_t(candidate->designator&0x0FFFFFFF)+0x4680DC4;
    const size_t bytes=0x44+candidate->profile.count*0x30;
    if(offset>runtime.size||bytes>runtime.size-offset)return false;
    const auto* skin=reinterpret_cast<const uint8_t*>(runtime.base+offset);
    if(Read<uint16_t>(skin)!=candidate->profile.count||Read<uint16_t>(skin,2)!=candidate->profile.regions)return false;
    const unsigned first=Read<uint16_t>(skin,4+region*4);
    const unsigned count=Read<uint8_t>(meshes,size_t(meshIndex)*0x4C+0x2E)==2?Read<uint16_t>(skin,6+region*4):1;
    if(!count||first>=candidate->profile.count||count>candidate->profile.count-first)return false;
    scope={candidate,first,count,nullptr,0};return true;
}
__declspec(noinline) void Observe(uint16_t tag,const BoneMatrix* destination) {
    runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);
    volatile bool fault=false;
    __try {
        if(PairCurrent()&&pair.leased&&destination&&g_halo3PlayerUnitGetter&&g_fpStereoSolveScope.armed) {
            VrContactTrackingSnapshot tracking{};
            tracking=g_fpStereoSolveScope.anatomicalTracking;
            const uint8_t* model=Halo3LoadedTagDefinition(tag);
            AnatomicalPalmMarkers markers{};
            if(tracking.headValid&&tracking.hands[0].valid&&tracking.hands[1].valid&&
               tracking.serial&&tracking.referenceEpoch&&tracking.timeNs&&model&&
               Halo3AnatomicalPalmMarkers(Read<uint32_t>(model,8),int(Read<uint32_t>(model,0x30)),markers)) {
                FpReceipt next{};next.unit=uint32_t(g_halo3PlayerUnitGetter(0));
                next.generation=runtime.generation.load();next.checksum=Read<uint32_t>(model,8);
                next.count=Read<uint32_t>(model,0x30);next.serial=tracking.serial;
                next.epoch=tracking.referenceEpoch;next.timeNs=tracking.timeNs;
                next.nonce=pair.nonce;
                std::memcpy(&next.wrists[0],destination+markers.leftNode,sizeof(next.wrists[0]));
                std::memcpy(&next.wrists[1],destination+markers.rightNode,sizeof(next.wrists[1]));
                next.valid=next.unit!=UINT32_MAX&&Halo4FloatingTransformValid(next.wrists[0])&&Halo4FloatingTransformValid(next.wrists[1]);
                if(next.valid)AdoptReceipt(next);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault)Fault();
    runtime.callbacks.fetch_sub(1,std::memory_order_acq_rel);
}
__declspec(noinline) void Begin(const void* centerCamera) {
    if(!BeginPairState())return;
    volatile bool fault=false;
    __try {
        const auto& tracking=g_fpStereoSolveScope.anatomicalTracking;
        if(Current()&&centerCamera&&g_fpStereoSolveScope.armed&&tracking.serial&&tracking.referenceEpoch&&tracking.timeNs&&
           tracking.headValid&&tracking.hands[0].valid&&tracking.hands[1].valid&&!tracking.locomotionBlocked&&
           g_halo3PlayerUnitGetter&&g_camValid.load()&&g_halo3UnitInVehicle) {
            int32_t scene=-1,shot=-1;
            const uint32_t unit=uint32_t(g_halo3PlayerUnitGetter(0));
            const auto* object=Halo3MeleeSelectionObject(unit,0);
            if(object&&unit!=UINT32_MAX&&(unit>>16)&&!g_halo3UnitInVehicle(int32_t(unit))&&
               ReadCinematicControl(scene,shot)==CinematicControlState::PlayerControlled) {
                pair.generation=runtime.generation.load();pair.unit=unit;pair.serial=tracking.serial;
                pair.epoch=tracking.referenceEpoch;pair.hideLower=g_config.body_ik_hide_lower;
                pair.request.headWorld.scale=1;
                // This exact compact camera was produced after ApplyHeadLook,
                // physical-crouch compensation and roomscale/lean. It is saved
                // before either eye's IPD/cant, unlike a native object origin.
                std::memcpy(pair.request.headWorld.translation,centerCamera,12);
                std::memcpy(pair.request.headWorld.rotation,static_cast<const uint8_t*>(centerCamera)+0xC,12);
                std::memcpy(pair.request.headWorld.rotation+6,static_cast<const uint8_t*>(centerCamera)+0x18,12);
                halo3_avatar::math::Cross(pair.request.headWorld.rotation+6,pair.request.headWorld.rotation,pair.request.headWorld.rotation+3);
                if(halo3_avatar::math::Normalize(pair.request.headWorld.rotation)&&
                   halo3_avatar::math::Normalize(pair.request.headWorld.rotation+3)&&
                   halo3_avatar::math::Normalize(pair.request.headWorld.rotation+6)) {
                    pair.active=Halo4FloatingTransformValid(pair.request.headWorld);
                    uint32_t weapons[2]{};
                    SupportGripRelationshipSnapshot support{};
                    const bool supported=VR_GetSupportGripRelationship(support)&&support.engaged&&
                        support.title==GameTitle::Halo3&&support.generation==pair.generation&&support.unit==unit;
                    if(Halo3ReadOwnedWeapons(unit,weapons,false)&&weapons[1]==UINT32_MAX&&!supported&&!tracking.twoHandAimActive) {
                        const unsigned anatomicalSupport=FingerAnatomicalSupportSide(tracking.handAlignment);
                        pair.request.fingerPoseAllowed[anatomicalSupport]=true;
                        pair.request.fingerInput[anatomicalSupport]=tracking.controllerFingers[0];
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){fault=true;}
    if(fault)Fault();
}
void End() noexcept {EndPairState();}
}
void BeginHalo3AvatarPair(const void* centerCamera){halo3_avatar_native::Begin(centerCamera);}
void EndHalo3AvatarPair(){halo3_avatar_native::End();}
void ObserveHalo3AvatarFp(uint16_t tag,const BoneMatrix* destination){halo3_avatar_native::Observe(tag,destination);}

namespace halo3_avatar_native {
struct DesiredBinding {uintptr_t base{};size_t size{};uint32_t generation{};bool lastRequested=false;} desired;
}
bool RemoveHalo3Avatar() {
    using namespace halo3_avatar_native;auto& r=runtime;
    r.enabled.store(false,std::memory_order_release);desired={};
    for(auto target:r.targets)if(target) {
        const auto status=MCCVR_DisableHookForRetirement(target);
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED&&status!=MH_ERROR_NOT_CREATED) {
            LOG("Halo 3 avatar CleanupRequired: hook disable failed; camera/arms unaffected");return false;}
    }
    const void* hooks[]{reinterpret_cast<const void*>(ExcludeHook),reinterpret_cast<const void*>(ProducerHook),
        reinterpret_cast<const void*>(RegionsHook),reinterpret_cast<const void*>(UploadHook),
        reinterpret_cast<const void*>(MapHook),reinterpret_cast<const void*>(CommitHook),
        reinterpret_cast<const void*>(SubmitAHook),reinterpret_cast<const void*>(SubmitBHook)};
    const void* originals[]{reinterpret_cast<const void*>(r.exclude),reinterpret_cast<const void*>(r.producer),
        reinterpret_cast<const void*>(r.regions),reinterpret_cast<const void*>(r.upload),
        reinterpret_cast<const void*>(r.map),reinterpret_cast<const void*>(r.commit),
        reinterpret_cast<const void*>(r.submitA),reinterpret_cast<const void*>(r.submitB)};
    if(!WaitForNativeDetourQuiescence(hooks,originals,8,r.callbacks)) {
        LOG("Halo 3 avatar CleanupRequired: stereo pair or detour ingress still active");return false;}
    for(auto& target:r.targets)if(target) {
        const auto status=MH_RemoveHook(target);
        if(status!=MH_OK&&status!=MH_ERROR_NOT_CREATED){LOG("Halo 3 avatar CleanupRequired: hook removal failed");return false;}
        target=nullptr;
    }
    r.exclude=nullptr;r.producer=nullptr;r.regions=nullptr;r.upload=nullptr;r.map=nullptr;r.commit=nullptr;
    r.submitA=nullptr;r.submitB=nullptr;r.world=nullptr;r.base=0;r.size=0;r.generation.store(0);
    return true;
}
bool InstallHalo3Avatar(uintptr_t base,size_t size,uint32_t generation) {
    using namespace halo3_avatar_native;auto& r=runtime;
    desired={base,size,generation,g_config.experimental_body_ik};
    if(!g_config.experimental_body_ik)return true;
    for(auto target:r.targets)if(target)return false;
    struct Binding{uintptr_t rva;const char* pattern;};
    constexpr Binding bindings[]{
        {0x28CDC0,"48 83 EC 28 44 8B CA 45 33 D2 48 63 D1 41 83 F9 FF 0F 84 83 00 00 00 83 FA FF 74 7E 44 8B 05 B9 D1 7A 00 48 8D 0C 52 65"},
        {0x20B3BC,"48 8B C4 48 89 58 08 44 89 48 20 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 90 02 00 00 48 8B 9C 24 F0 02 00 00 41 83 CB"},
        {0x2B32C0,"48 89 5C 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 45 33 C0 4C 8B FA 44 38 05 57 3F 7B 00 4C 8B"},
        {0x2B5450,"48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 88 50 10 57 48 83 EC 20 48 8B 41 08 48 8B E9 0F B7 50 04 48 8B 05 9F 3B 79"},
        {0x2AFB88,"40 53 48 83 EC 40 45 33 C9 4C 8D 1D 68 04 D5 FF 49 8B D8 4C 63 C1 41 8D 41 40 43 8B 8C 83 50 2B 80 00 3B C8 76 0D 3B C2"},
        {0x2AFC14,"48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 20 48 63 F9 4C 8D 35 CD 03 D5 FF 48 8B 0D EE BB 40 04 8B F2"},
        {0x2B37CC,"48 89 5C 24 18 44 88 4C 24 20 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 81 EC B0 00 00 00 0F B7 42 04 45 33 FF"},
        {0x2B3CB0,"48 89 5C 24 20 44 88 44 24 18 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 81 EC A0 00 00 00 0F B7 42 04 48 8B DA"},
        {0x28B0C8,"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 30 44 8B 15 B1 EE 7A 00 4D 8B F0 65 48"},
    };
    for(const auto& b:bindings) {
        const auto hit=sig::Find(base,size,b.pattern);
        if(hit!=base+b.rva||sig::Find(hit+1,base+size-hit-1,b.pattern)) {
            LOG("Halo 3 avatar StockFallback: own binding +0x%llX missing/ambiguous",static_cast<unsigned long long>(b.rva));return false;}
    }
    const auto call=[base,size](uintptr_t site,uintptr_t target) {
        return site+5<size&&*reinterpret_cast<const uint8_t*>(base+site)==0xE8&&
            site+5+*reinterpret_cast<const int32_t*>(base+site+1)==target;};
    if(!call(0x252C75,0x28CDC0)||!call(0x252E1E,0x20B3BC)||!call(0x20B586,0x28B0C8)||
       !call(0x2B38D2,0x2B32C0)||!call(0x2B3D9C,0x2B32C0)||
       !call(0x2B550D,0x2AFB88)||!call(0x2B5533,0x2AFC14)) {
        LOG("Halo 3 avatar StockFallback: own native call-site identity mismatch");return false;}
    r.base=base;r.size=size;r.generation.store(generation);r.faulted.store(false);
    r.world=reinterpret_cast<WorldFn>(base+0x28B0C8);
    void* hooks[]{reinterpret_cast<void*>(ExcludeHook),reinterpret_cast<void*>(ProducerHook),
        reinterpret_cast<void*>(RegionsHook),reinterpret_cast<void*>(UploadHook),
        reinterpret_cast<void*>(MapHook),reinterpret_cast<void*>(CommitHook),
        reinterpret_cast<void*>(SubmitAHook),reinterpret_cast<void*>(SubmitBHook)};
    void** originals[]{reinterpret_cast<void**>(&r.exclude),reinterpret_cast<void**>(&r.producer),
        reinterpret_cast<void**>(&r.regions),reinterpret_cast<void**>(&r.upload),
        reinterpret_cast<void**>(&r.map),reinterpret_cast<void**>(&r.commit),
        reinterpret_cast<void**>(&r.submitA),reinterpret_cast<void**>(&r.submitB)};
    for(unsigned i=0;i<8;++i) {
        auto* target=reinterpret_cast<void*>(base+bindings[i].rva);
        if(MH_CreateHook(target,hooks[i],originals[i])!=MH_OK) {
            const auto binding=desired;(void)RemoveHalo3Avatar();desired=binding;return false;}
        r.targets[i]=target;
    }
    for(auto target:r.targets)if(MH_EnableHook(target)!=MH_OK) {
        const auto binding=desired;(void)RemoveHalo3Avatar();desired=binding;return false;}
    r.enabled.store(true,std::memory_order_release);
    LOG("Halo 3 avatar Installed: exact local full-world body, private GPU output; headset validation pending");return true;
}
void ReportHalo3Avatar() {
    using namespace halo3_avatar_native;auto& r=runtime;
    const bool requested=g_config.experimental_body_ik;
    bool any=false;for(auto target:r.targets)any|=target!=nullptr;
    if(any&&(r.faulted.load()||!r.enabled.load())) {
        const auto binding=desired;(void)RemoveHalo3Avatar();desired=binding;
        any=false;for(auto target:r.targets)any|=target!=nullptr;
    }
    if(!any&&desired.base&&requested!=desired.lastRequested) {
        auto binding=desired;binding.lastRequested=requested;
        if(!requested){r.faulted.store(false);desired=binding;}
        else {r.faulted.store(false);(void)InstallHalo3Avatar(binding.base,binding.size,binding.generation);}
    } else if(r.enabled.load()&&desired.base&&!requested&&desired.lastRequested) {
        auto binding=desired;binding.lastRequested=requested;
        (void)RemoveHalo3Avatar();desired=binding;
    }
    static uint64_t previous[5]{};
    static ULONGLONG lastLog=0;
    const uint64_t now[]{r.posed.load(),r.uploads.load(),r.refusals.load(),r.ordering.load(),r.faults.load()};
    const ULONGLONG clock=GetTickCount64();
    if(std::memcmp(previous,now,sizeof(now))!=0&&(now[4]!=previous[4]||clock-lastLog>=5000)) {
        lastLog=clock;
        std::memcpy(previous,now,sizeof(now));
        LOG("Halo 3 avatar: posed=%llu uploads=%llu refusals=%llu same-pair-order-refusals=%llu optional-faults=%llu; native camera/arms retained on refusal",
            now[0],now[1],now[2],now[3],now[4]);
    }
}
