// Production first-person transaction tests with explicit native/XR fixtures.
// These verify palette receipt admission and unchanged-output fallback, not
// game draw visibility. Actual native CPU/GPU consumers have a separate test.
#include "../src/dll/haloce_first_person.cpp"
#include <cstdio>
#include <thread>
#include <cstdlib>

Config::Config() {}
Config g_config;

static GameTitle testTitle=GameTitle::HaloCE;
static uint32_t testGeneration=3;
static halo_ce::RenderContext testContext{};
static halo_ce::RenderContext anniversaryEyeContext{};
static halo_ce::RenderContext gameplayContext{};
static bool gameplayValid=true;
static bool concurrentGameplayReads{};
static halo_ce::Snapshot<halo_ce::RenderContext> concurrentGameplay;
static unsigned gameplayReads{},revokeGameplayOnRead{};
static bool anniversaryEyeValid=true;
static bool anniversaryPrimaryValid=true;
static halo_ce::SaberCamera particleCamera{};
static bool contextValid=true,renderContextValid=true,nativeSawInvalidated=true;
static bool lensFault{},lensRebuild{};
static float lensArgument{};
static unsigned prepareCalls{};
static unsigned quiescenceCalls{},quiescenceRanges{};
static size_t blockedQuiescenceCount{};
static uintptr_t visibilityListAddress{},visibilityModelAddress{};
static bool visibilityListValid=true,visibilityExpectPrivate{},visibilityNativeChecks=true,visibilityFault{};
static unsigned visibilityListReads{},visibilityRevokeOnRead{},visibilityNativeCalls{};
static FirstPersonVisibilityList visibilityNativeList{};
static std::array<uint8_t,0x400> visibilityNativeModel{};
static uint32_t visibilityNativeFlags{};
static uintptr_t visibilitySubmittedList{},visibilitySubmittedCaller{};
static uint8_t visibilitySubmittedPhase{};
static unsigned visibilitySubmitRecords{},visibilitySubmitCalls{};
static bool visibilitySubmitChecks=true,visibilitySubmitExpected=true;
static unsigned contactStages{},contactCommits{};
static bool contactReceiptCurrent=true,contactRejectOwnership{},contactRejectCopy{};
static HaloCELocalPlayerState targetPlayer{};
static bool targetPlayerValid{},targetFault{};
static unsigned targetNativeCalls{};
static const void* targetParameters{};
static const halo_ce::Vec3* targetOrigin{};
static halo_ce::Vec3 targetDirection{};
static uint32_t targetUnit{};static uint16_t targetTeam{};static void* targetResult{};
static halo_ce::NodeMatrix* contactNativePalette{};
void VR_PublishReloadTarget(GameTitle,uint32_t,uint64_t,uint64_t,
    const contact_melee::TrackingToWorld&,const float[3]) noexcept {}
// Persistent support grip: the durable relationship is owned by the OpenXR
// input path, which does not run under this fixture. `supportWired` models the
// feature+title applicability gate; the published evidence record is captured
// so the tri-state producer mapping can be asserted.
static bool supportWired{};
static support_grip::OwnerTuple lastSupportOwner{};
static uint32_t lastSupportTitle{};
static uint32_t lastSupportGeneration{};
static support_grip::OwnerEvidence lastSupportEvidence{
    support_grip::OwnerEvidence::Unknown};
static unsigned supportPublishCount{};
void VR_PublishSupportGripOwnerEvidence(GameTitle title,uint32_t generation,
    const support_grip::OwnerTuple& owner,
    support_grip::OwnerEvidence evidence) noexcept
{
    lastSupportTitle=uint32_t(title);lastSupportGeneration=generation;
    lastSupportOwner=owner;lastSupportEvidence=evidence;++supportPublishCount;
}
// The durable relationship is normally owned by the OpenXR input path. Tests
// can publish a readable snapshot here; the default is a readable DISENGAGED
// snapshot, which is the ordinary one-hand state.
static SupportGripRelationshipSnapshot supportRelationship{};
static bool supportRelationshipValid{true};
bool VR_GetSupportGripRelationship(
    SupportGripRelationshipSnapshot& out) noexcept
{ out=supportRelationship;return supportRelationshipValid; }
bool VR_SupportGripWiredForTitle(GameTitle) noexcept { return supportWired; }
// Weapon-order diagnostic tranche: recording is never active under test, so
// the probes stay inert through these stubs (mirrors gate-off production).
bool Telemetry_WeaponEventsAccepting() noexcept { return false; }
uint64_t Telemetry_PublishWeaponEvent(uint8_t,uint8_t,uint8_t,uint32_t,
    uint64_t,uint32_t,uint32_t,uint64_t,uint64_t) noexcept { return 0; }
static uintptr_t contactGraphDefinition{};
void HaloCEContact_ApplyPalette(const halo_ce::RenderContext& context,const halo_ce::FirstPersonBinding&,
    const halo_ce::NodeMatrix*,halo_ce::NodeMatrix*,HaloCEContactPublication& publication) noexcept
{
    ++contactStages;publication.generation=context.tracking.generation;
    if (contactRejectOwnership) contextValid=false;
    if (contactRejectCopy)
    { DWORD before{};VirtualProtect(contactNativePalette,4096,PAGE_READONLY,&before); }
}
void HaloCEContact_CommitPalette(const halo_ce::RenderContext& context,const HaloCEContactPublication& publication) noexcept
{
    ++contactCommits;halo_ce::RenderContext committed{};
    contactReceiptCurrent&=publication.generation==context.tracking.generation&&
        CurrentPaletteContext(committed)&&committed.tracking.serial==context.tracking.serial;
}
uintptr_t __fastcall ContactGraphFixture(uint32_t graph)
{ return graph==25?contactGraphDefinition:0; }
GameTitle TitleAdapter_GetActiveTitle() { return testTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return testGeneration; }
bool HaloCE_Armed() noexcept { return testTitle==GameTitle::HaloCE; }
bool HaloCE_GetRenderContext(const halo_ce::Camera&,halo_ce::RenderContext& result) noexcept
{ result=testContext;return contextValid&&renderContextValid; }
bool HaloCE_GetClassicPrimaryEyeContext(halo_ce::RenderContext& result) noexcept
{ result=testContext;return contextValid&&renderContextValid; }
bool HaloCE_GetGameplayContext(halo_ce::RenderContext& result) noexcept
{
    if (concurrentGameplayReads)
        return concurrentGameplay.Read(result)&&HaloCE_RenderContextCurrent(result);
    ++gameplayReads;
    if (revokeGameplayOnRead&&gameplayReads==revokeGameplayOnRead) gameplayValid=false;
    result=gameplayContext;return gameplayValid&&HaloCE_RenderContextCurrent(result);
}
bool HaloCE_RenderContextCurrent(const halo_ce::RenderContext& candidate) noexcept
{
    return contextValid&&testTitle==GameTitle::HaloCE&&candidate.tracking.generation==testGeneration&&
        candidate.referenceRevision==testContext.referenceRevision&&candidate.rendererEpoch==testContext.rendererEpoch&&
        candidate.tracking.spaceEpoch==testContext.tracking.spaceEpoch&&
        candidate.tracking.serial&&candidate.tracking.serial<=testContext.tracking.serial&&
        testContext.tracking.serial-candidate.tracking.serial<=8;
}
bool HaloCE_GetAnniversaryPrimaryEyeTracking(halo_ce::Tracking& tracking) noexcept
{
    tracking={};
    if (!anniversaryPrimaryValid||!anniversaryEyeValid||!renderContextValid||!HaloCE_RenderContextCurrent(anniversaryEyeContext)) return false;
    tracking=anniversaryEyeContext.tracking;return true;
}
bool HaloCE_GetAnniversaryEyeTracking(const halo_ce::SaberCamera* camera,halo_ce::Tracking& tracking) noexcept
{
    tracking={};
    if (camera!=&particleCamera||!anniversaryEyeValid||!renderContextValid||!HaloCE_RenderContextCurrent(anniversaryEyeContext)) return false;
    tracking=anniversaryEyeContext.tracking;return true;
}
bool HaloCE_GetAnniversaryPreparedListTracking(uintptr_t list,halo_ce::Tracking& tracking) noexcept
{
    ++visibilityListReads;
    if (visibilityRevokeOnRead&&visibilityListReads==visibilityRevokeOnRead) visibilityListValid=false;
    tracking={};
    if (!visibilityListValid||list!=visibilityListAddress||
        *reinterpret_cast<int32_t*>(moduleBase+0x1b7aa84)!=1||
        !HaloCE_RenderContextCurrent(testContext)) return false;
    tracking=testContext.tracking;return true;
}
void HaloCE_RecordAnniversaryVisibilitySubmission(uintptr_t list,int32_t phase,uintptr_t caller) noexcept
{
    ++visibilitySubmitRecords;visibilitySubmittedList=list;
    visibilitySubmittedPhase=static_cast<uint8_t>(phase);visibilitySubmittedCaller=caller;
}
bool HaloCEControls_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept
{state=targetPlayer;return targetPlayerValid;}
uint8_t __fastcall NativeTargetFixture(const void* parameters,const halo_ce::Vec3* origin,
    const halo_ce::Vec3* direction,uint32_t unit,uint16_t team,void* result)
{
    ++targetNativeCalls;targetParameters=parameters;targetOrigin=origin;
    targetDirection=direction?*direction:halo_ce::Vec3{};targetUnit=unit;targetTeam=team;targetResult=result;
    if(targetFault) RaiseException(0xE000CE71,0,0,nullptr);
    if(result) *static_cast<uint32_t*>(result)=0x76540009;
    return 1;
}
bool InvokeTargetFaultFixture()
{
    __try {TargetQueryHook(nullptr,nullptr,nullptr,0,0,nullptr);}
    __except(GetExceptionCode()==0xE000CE71?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
    return false;
}
void Logf(const char*,...) {}
bool WaitForNativeDetourQuiescence(const void* const* functions,const void* const* trampolines,size_t count,
    const std::atomic<uint32_t>& activeCallbacks)
{
    ++quiescenceCalls;quiescenceRanges+=unsigned(count);
    return functions&&trampolines&&count&&count<=8&&count!=blockedQuiescenceCount&&!activeCallbacks.load();
}
// The outermost prepare freezes this invocation's local context before it
// calls the native original. Capture that exact frozen scope so the
// persistent-support-grip trust/attachment decision (and the muzzle gating it
// must not disturb) can be asserted on the context that was prepared.
static bool frozenPrepareCaptured{};
static Scope frozenPrepareScope{};
void __fastcall NativePrepareFixture(int16_t user)
{
    ++prepareCalls;
    if (!user) nativeSawInvalidated&=!HaloCEFirstPerson_Armed();
    if (scope) { frozenPrepareScope=*scope;frozenPrepareCaptured=true; }
}
void __fastcall NativeLensFixture(float fov,bool rebuild)
{
    lensArgument=fov;lensRebuild=rebuild;
    if (lensFault) RaiseException(0xE000CEA1,0,0,nullptr);
}
bool InvokeLensFaultFixture()
{
    __try { ClassicLensHook(.9671381116f,true); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
void __fastcall NativeVisibilityFixture(uintptr_t list,int32_t serial,float elapsed,uintptr_t container)
{
    ++visibilityNativeCalls;
    visibilityNativeChecks&=serial==37&&elapsed==.0125f&&container==visibilityModelAddress;
    visibilityNativeChecks&=visibilityExpectPrivate?list!=visibilityListAddress:list==visibilityListAddress;
    const auto* actual=reinterpret_cast<const uint8_t*>(list);
    const auto* source=reinterpret_cast<const uint8_t*>(&visibilityNativeList);
    constexpr size_t changed=offsetof(SaberViewPair,views)+sizeof(SaberView)+offsetof(SaberView,viewIndex);
    for (size_t index=0;index<kNativeVisibilityListBytes;++index)
        visibilityNativeChecks&=actual[index]==((visibilityExpectPrivate&&index>=changed&&index<changed+4)?0:source[index]);
    uint32_t flags{};std::memcpy(&flags,visibilityNativeModel.data()+8,4);
    visibilityNativeChecks&=flags==visibilityNativeFlags;
    if (visibilityFault) RaiseException(0xE000CEA3,0,0,nullptr);
}
bool InvokeVisibilityFaultFixture()
{
    __try { VisibilityPrepareHook(visibilityListAddress,37,.0125f,visibilityModelAddress); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
void __fastcall NativeVisibilitySubmitFixture(uintptr_t nativeContext,uintptr_t list,uint8_t phase)
{
    ++visibilitySubmitCalls;
    visibilitySubmitChecks&=nativeContext==0x123456789abcdef0ull&&list==visibilityListAddress&&phase==1;
    // The production bridge must publish before entering the native routine,
    // whose worker may execute immediately on this thread or another thread.
    visibilitySubmitChecks&=visibilitySubmitExpected?
        visibilitySubmitRecords==visibilitySubmitCalls&&visibilitySubmittedList==list&&
        visibilitySubmittedPhase==phase&&visibilitySubmittedCaller!=0:visibilitySubmitRecords==0;
    if (visibilityFault) RaiseException(0xE000CEA4,0,0,nullptr);
}
bool InvokeVisibilitySubmitFaultFixture()
{
    __try { VisibilitySubmitHook(0x123456789abcdef0ull,visibilityListAddress,1); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
static ParticleBuffer* particleSource{};
static std::array<float,4096*4> particleBacking{},particleBaseline{},particleUploaded{};
static bool particleNativeChecks=true,particleExpectCorrection{},particleThrow{},particleMutate{},particleNested{},particleRevoke{};
static unsigned particleNativeCalls{};
void __fastcall NativeParticleCommitFixture(uintptr_t address,uintptr_t backend,bool immediate)
{
    ++particleNativeCalls;
    auto& buffer=*reinterpret_cast<ParticleBuffer*>(address);
    particleNativeChecks&=backend==123&&immediate&&buffer.gpu==particleSource->gpu;
    particleNativeChecks&=std::memcmp(particleBacking.data(),particleBaseline.data(),sizeof(particleBacking))==0;
    particleNativeChecks&=particleExpectCorrection?(address!=reinterpret_cast<uintptr_t>(particleSource)&&buffer.data!=particleSource->data):address==reinterpret_cast<uintptr_t>(particleSource);
    if (buffer.capacity>0&&buffer.capacity<=4096)
        std::memcpy(particleUploaded.data(),buffer.data,size_t(buffer.capacity)*16);
    if (particleMutate) { particleSource->first=7;particleSource->end=11; }
    if (particleThrow) RaiseException(0xE000CEA2,0,0,nullptr);
    buffer.first=buffer.capacity;buffer.end=0;
}
void __fastcall NativeParticleDrawFixture(const halo_ce::SaberCamera* camera,uintptr_t texture,int pass,uintptr_t batch)
{
    particleNativeChecks&=texture==55&&pass==2&&batch==66;
    const auto* outer=particleScope;
    if (particleNested)
    {
        particleNested=false;particleExpectCorrection=false;
        halo_ce::SaberCamera auxiliary{};
        // The auxiliary draw masks the outer scope even though its tracking
        // serial would otherwise be the same. Restore the native dirty range.
        const auto before=*particleSource;
        ParticleDrawHook(&auxiliary,55,2,66);
        *particleSource=before;
        particleExpectCorrection=true;
        particleNativeChecks&=particleScope==outer;
    }
    if (particleRevoke) anniversaryPrimaryValid=false;
    CommitParticleProjection(reinterpret_cast<uintptr_t>(particleSource),123,true,true);
}
bool InvokeParticleFaultFixture()
{
    __try { ParticleDrawHook(&particleCamera,55,2,66); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"CE FP transaction failed at %d: %s\n",__LINE__,#x); return 1; } } while(false)
static bool CheckContactPaletteCommit()
{
    using namespace halo_ce;
    const auto previous=testContext;
    AnimationNode nodes[4]{};
    const char* names[]{"frame root","frame l wrist","frame r wrist","frame gun"};
    for (unsigned i=0;i<4;++i)
    { std::strcpy(nodes[i].name,names[i]);nodes[i].parent=i==0?-1:i==3?2:0; }
    alignas(8) uint8_t definition[0x70]{};const int32_t count=4,cached=0x100;
    std::memcpy(definition+0x68,&count,4);std::memcpy(definition+0x6c,&cached,4);
    contactGraphDefinition=reinterpret_cast<uintptr_t>(definition);
    const intptr_t mapped=reinterpret_cast<intptr_t>(nodes),virtualBase=cached;
    std::memcpy(reinterpret_cast<void*>(moduleBase+0x2ea3410),&virtualBase,8);
    std::memcpy(reinterpret_cast<void*>(moduleBase+0x2d9ce10),&mapped,8);
    uint8_t jump[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    const auto target=reinterpret_cast<uintptr_t>(&ContactGraphFixture);std::memcpy(jump+2,&target,8);
    auto* service=reinterpret_cast<void*>(moduleBase+contract::first_person::first_person_cached_tag_get);
    DWORD protection{};
    if (!VirtualProtect(service,sizeof(jump),PAGE_EXECUTE_READWRITE,&protection)) return false;
    std::memcpy(service,jump,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),service,sizeof(jump));
    contactNativePalette=static_cast<NodeMatrix*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!contactNativePalette) return false;
    testContext.camera.position={100,200,300};testContext.camera.forward={1,0,0};testContext.camera.up={0,0,1};
    testContext.camera.viewport={0,0,800,1000};testContext.camera.window=testContext.camera.viewport;
    testContext.camera.verticalFov=1;testContext.camera.nearPlane=.01f;testContext.camera.farPlane=1000;
    testContext.reference.generation=testGeneration;testContext.reference.spaceEpoch=testContext.tracking.spaceEpoch;
    testContext.unitsPerMeter=1;testContext.positional=true;
    auto& rig=testContext.tracking.controllers;
    rig.gunScale=rig.supportScale=1;rig.armIk=rig.floatingHands=false;
    rig.physical[0]={true,{-.3f,1.2f,-.6f},{}};rig.physical[1]={true,{.3f,1.2f,-.6f},{}};
    rig.primaryAim=rig.independentPrimaryAim=rig.physical[1];rig.support=rig.physical[0];
    std::array<NodeMatrix,4> source{};
    source[1].position={100,200.3f,300};source[2].position={100,199.7f,300};source[3].position={100.4f,199.7f,300};
    bool checks=true;
    for (unsigned failure=0;failure<3;++failure)
    {
        std::memcpy(contactNativePalette,source.data(),sizeof(source));
        ++testContext.tracking.serial;
        contactRejectOwnership=failure==1;contactRejectCopy=failure==2;
        const unsigned commits=contactCommits,stages=contactStages;
        const Scope owner{testContext,contactNativePalette,true};
        const bool accepted=ApplyPalette(25,contactNativePalette,owner);
        if (contactRejectCopy) VirtualProtect(contactNativePalette,4096,PAGE_READWRITE,&protection);
        contextValid=true;
        checks&=accepted==(failure==0)&&contactStages==stages+1&&contactCommits==commits+(failure==0);
        checks&=failure==0?std::memcmp(contactNativePalette,source.data(),sizeof(source))!=0:
            std::memcmp(contactNativePalette,source.data(),sizeof(source))==0;
    }
    checks&=contactReceiptCurrent;
    contactRejectOwnership=contactRejectCopy=false;contactGraphDefinition=0;
    VirtualFree(contactNativePalette,0,MEM_RELEASE);contactNativePalette=nullptr;testContext=previous;
    return checks;
}
#include "haloce_muzzle_runtime.inl"
int main(int argc,char** argv)
{
    using namespace halo_ce;
    installed=true;active=true;retiring=false;generation=testGeneration;
    prepareHook.original=reinterpret_cast<void*>(&NativePrepareFixture);
    testContext.tracking.generation=testGeneration;testContext.tracking.spaceEpoch=4;testContext.tracking.serial=12;
    testContext.referenceRevision=6;testContext.rendererEpoch=8;
    testContext.tracking.controllers.controlsPresentationBlocked=false;
    void* native=VirtualAlloc(nullptr,contract::imageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(native);moduleBase=reinterpret_cast<uintptr_t>(native);
    int32_t& renderer=*reinterpret_cast<int32_t*>(moduleBase+0x1b7aa84);
    auto publish=[&](uint64_t age=0)
    {
        const uint64_t now=GetTickCount64()-age;
        const bool success=paletteReceipt.Publish({testContext,now});
        anniversaryEyeContext=testContext;
        gameplayContext=testContext;
        lastApplied.store(now,std::memory_order_release);return success;
    };
    CHECK(publish());CHECK(HaloCEFirstPerson_Armed());
    {
        const uint64_t stamp=GetTickCount64(),needle=0x55EA2D6F6C10C375ull;
        CHECK(paletteReceipt.Publish({testContext,stamp,needle}));lastApplied=stamp;
        CHECK(HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp)==needle);
        CHECK(HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp+150)==needle);
        CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp+151));
        CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp-1));
        CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration+1,4,stamp));
        CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration,5,stamp));
        active=false;CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp));active=true;
        retiring=true;CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp));retiring=false;
        contextValid=false;CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp));contextValid=true;
        testTitle=GameTitle::Halo3;CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp));testTitle=GameTitle::HaloCE;
        testContext.tracking.controllers.controlsPresentationBlocked=true;
        CHECK(paletteReceipt.Publish({testContext,stamp,needle}));
        CHECK(!HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp));
        testContext.tracking.controllers.controlsPresentationBlocked=false;
        CHECK(paletteReceipt.Publish({testContext,stamp,123}));
        CHECK(HaloCEFirstPerson_WeaponGraph(testGeneration,4,stamp)==123);
        CHECK(publish());
    }
    if (argc==4&&std::strcmp(argv[1],"--saber-worker-native-projection-fixture")==0)
    {
        uint32_t materialFlags{},selectorOffset{};std::array<float,96> data{};
        FILE* file{};CHECK(!fopen_s(&file,argv[2],"rb")&&file);
        CHECK(std::fread(&materialFlags,4,1,file)==1&&std::fread(&selectorOffset,4,1,file)==1&&
            std::fread(data.data(),sizeof(data),1,file)==1&&std::fgetc(file)==EOF);
        CHECK(!std::fclose(file));
        CHECK(selectorOffset==0x170||selectorOffset==0x20||selectorOffset==0x70);
        std::array<uint8_t,0x40> nativeModel{};
        std::memcpy(nativeModel.data()+0x28,&materialFlags,4);
        renderer=1;anniversaryEyeValid=false;anniversaryPrimaryValid=false;lastApplied=0;
        const bool result=ApplyTrackedProjection(data.data(),reinterpret_cast<uintptr_t>(nativeModel.data()),selectorOffset);
        CHECK(result==bool(materialFlags&0x10000000u));
        CHECK(!fopen_s(&file,argv[3],"wb")&&file);
        CHECK(std::fwrite(data.data(),sizeof(data),1,file)==1&&!std::fclose(file));
        CHECK(VirtualFree(native,0,MEM_RELEASE));
        return 0;
    }
    CHECK(CheckContactPaletteCommit());CHECK(publish());
    {
        const auto saved=testContext;
        testContext.camera.position={100,200,300};testContext.camera.forward={1,0,0};testContext.camera.up={0,0,1};
        testContext.camera.viewport={0,0,800,1000};testContext.camera.window=testContext.camera.viewport;
        testContext.camera.verticalFov=1;testContext.camera.nearPlane=.01f;testContext.camera.farPlane=1000;
        testContext.reference.generation=testGeneration;testContext.reference.spaceEpoch=testContext.tracking.spaceEpoch;
        testContext.unitsPerMeter=1;testContext.positional=true;
        testContext.tracking.controllers.primaryAim={true,{.3f,1.2f,-.6f},{}};
        targetPlayerValid=true;targetPlayer.unit=0x12340002;targetPlayer.weapon=0x56780003;
        targetPlayer.hasControlledUnit=targetPlayer.onFoot=targetPlayer.nativePreparesFirstPerson=true;
        targetPlayer.nativeInputBlocked=targetPlayer.nativeLookBlocked=false;
        aimInstalled=true;targetInstalled=true;targetQueryHook.original=reinterpret_cast<void*>(&NativeTargetFixture);
        const Vec3 origin{4,5,6},stock{0,1,0};uint32_t result{};const uint32_t parameters=0x9876;
        auto invoke=[&](uintptr_t caller,uint32_t unit=0x12340002) {
            const auto count=targetNativeCalls;
            const auto found=TargetQueryBody(&parameters,&origin,&stock,unit,7,&result,caller);
            return found==1&&targetNativeCalls==count+1&&targetParameters==&parameters&&targetOrigin==&origin&&
                targetUnit==unit&&targetTeam==7&&targetResult==&result&&result==0x76540009;
        };
        for(int mode:{0,1}) for(bool left:{false,true}) {
            renderer=mode;auto& rig=testContext.tracking.controllers;rig.leftHanded=left;
            rig.primaryAim.orientation=left?Quat{0,.38268343f,0,.92387953f}:Quat{};
            CHECK(publish());Vec3 expected{};CHECK(ControllerShotDirection(targetPlayer.unit,expected));
            for(uintptr_t offset:{uintptr_t(0xb680fd),uintptr_t(0xb683a0)}) {
                CHECK(invoke(moduleBase+offset));CHECK(Dot(targetDirection,expected)>.9999f);
            }
            CHECK(invoke(moduleBase+1));CHECK(Dot(targetDirection,stock)>.9999f);
            CHECK(invoke(moduleBase+0xb680fd,0x43210002));CHECK(Dot(targetDirection,stock)>.9999f);
        }
        for(unsigned refusal=0;refusal<5;++refusal) {
            targetPlayer.onFoot=refusal!=0;targetPlayer.nativePaused=refusal==1;
            targetPlayer.nativeInputBlocked=refusal==2;targetPlayer.nativeCinematicFlag=refusal==3;
            gameplayValid=refusal!=4;
            CHECK(invoke(moduleBase+0xb683a0));CHECK(Dot(targetDirection,stock)>.9999f);
        }
        // Persistent support grip: ControllerShotDirection consumes the
        // support-capable primaryAim only while this invocation proves the
        // durable relationship owner. A mismatch detaches to the independent
        // one-hand carrier for the invocation; a failed reader, an absent
        // weapon or a missing independent pose stays stock (returns false).
        // The applicability gate leaves the base path untouched when unwired.
        {
            const auto savedTargetPlayer=targetPlayer;
            const bool savedTargetPlayerValid=targetPlayerValid;
            const auto savedRelationship=supportRelationship;
            const bool savedRelationshipValid=supportRelationshipValid;
            gameplayValid=true;
            auto& rig=testContext.tracking.controllers;
            rig.primaryAim={true,{0,0,0},{0,0,0,1}};
            rig.primaryAimSupportDerived=true;
            rig.independentPrimaryAim={true,{0,0,0},{0,.38268343f,0,.92387953f}};
            CHECK(publish());
            Vec3 expectedPrimary{},expectedIndependent{};NodeMatrix matrix{};
            CHECK(BuildControllerMatrix(testContext.camera,testContext.tracking,testContext.reference,
                rig.primaryAim,testContext.unitsPerMeter,testContext.positional,matrix));
            expectedPrimary=matrix.forward;
            CHECK(BuildControllerMatrix(testContext.camera,testContext.tracking,testContext.reference,
                rig.independentPrimaryAim,testContext.unitsPerMeter,testContext.positional,matrix));
            expectedIndependent=matrix.forward;
            CHECK(Dot(expectedPrimary,expectedIndependent)<.9f);
            targetPlayer={};
            targetPlayer.generation=testGeneration;
            targetPlayer.unit=0x12340002u;targetPlayer.weapon=0x56780003u;
            targetPlayer.hasControlledUnit=targetPlayer.onFoot=targetPlayer.nativePreparesFirstPerson=true;
            targetPlayer.hasFirstPersonUserRecord=true;targetPlayer.weaponSlotPresent=true;
            targetPlayer.nativeInputBlocked=targetPlayer.nativeLookBlocked=false;
            targetPlayerValid=true;
            Vec3 direction{};
            // Unwired: the decision is the base one even with a readable
            // snapshot; the frozen support-derived carrier stays in use.
            supportWired=false;
            supportRelationship={};supportRelationshipValid=true;
            CHECK(ControllerShotDirection(targetPlayer.unit,direction));
            CHECK(Dot(direction,expectedPrimary)>.9999f);
            // Wired + readable DISENGAGED + a support-derived frozen pose is
            // the release discontinuity: never reuse the stale support carrier.
            supportWired=true;
            CHECK(ControllerShotDirection(targetPlayer.unit,direction));
            CHECK(Dot(direction,expectedIndependent)>.9999f);
            // Wired + readable disengaged + an ordinary (not support-derived)
            // publication keeps the existing one-hand carrier untouched.
            rig.primaryAimSupportDerived=false;
            CHECK(publish());
            CHECK(ControllerShotDirection(targetPlayer.unit,direction));
            CHECK(Dot(direction,expectedPrimary)>.9999f);
            rig.primaryAimSupportDerived=true;
            CHECK(publish());
            supportRelationship.engaged=true;
            supportRelationship.title=GameTitle::HaloCE;
            supportRelationship.generation=testGeneration;
            supportRelationship.unit=targetPlayer.unit;
            supportRelationship.weapon=targetPlayer.weapon;
            CHECK(ControllerShotDirection(targetPlayer.unit,direction));
            CHECK(Dot(direction,expectedPrimary)>.9999f);
            targetPlayer.weapon=0x99990001u;
            CHECK(ControllerShotDirection(targetPlayer.unit,direction));
            CHECK(Dot(direction,expectedIndependent)>.9999f);
            supportRelationship={};
            CHECK(ControllerShotDirection(targetPlayer.unit,direction));
            CHECK(Dot(direction,expectedIndependent)>.9999f);
            // A failed relationship read is continuity-unprovable and detaches.
            supportRelationshipValid=false;
            CHECK(ControllerShotDirection(targetPlayer.unit,direction));
            CHECK(Dot(direction,expectedIndependent)>.9999f);
            supportRelationshipValid=true;
            rig.independentPrimaryAim.valid=false;
            CHECK(publish());
            CHECK(!ControllerShotDirection(targetPlayer.unit,direction));
            rig.independentPrimaryAim.valid=true;
            CHECK(publish());
            targetPlayerValid=false;
            CHECK(!ControllerShotDirection(targetPlayer.unit,direction));
            targetPlayerValid=true;
            targetPlayer.weapon=0xffffffffu;
            CHECK(!ControllerShotDirection(targetPlayer.unit,direction));
            targetPlayer=savedTargetPlayer;targetPlayerValid=savedTargetPlayerValid;
            supportRelationship=savedRelationship;supportRelationshipValid=savedRelationshipValid;
            supportWired=false;
        }
        gameplayValid=true;targetPlayer={};targetPlayerValid=false;
        targetFault=true;const auto before=targetNativeCalls;
        CHECK(InvokeTargetFaultFixture());CHECK(callbacks.load()==0&&targetNativeCalls==before+1);targetFault=false;
        blockedQuiescenceCount=1;
        CHECK(!RemoveTargetQuery()&&targetRetiring&&aimInstalled&&installed);
        CHECK(targetQueryHook.original);blockedQuiescenceCount=0;
        CHECK(RemoveTargetQuery()&&!targetRetiring&&aimInstalled&&installed);
        testContext=saved;CHECK(publish());renderer=0;
    }
    float classicFov=.9671381116f;
    CHECK(ApplyClassicTrackedProjection(classicFov));CHECK(classicFov==-2);
    renderContextValid=false;classicFov=.9671381116f;
    CHECK(!ApplyClassicTrackedProjection(classicFov));CHECK(classicFov==.9671381116f);
    renderContextValid=true;
    ++testContext.tracking.serial;classicFov=.9671381116f;
    CHECK(!ApplyClassicTrackedProjection(classicFov));CHECK(classicFov==.9671381116f);
    --testContext.tracking.serial;
    // A foreign caller preserves the original arguments. A native structured
    // exception still drains the new hook's callback lifetime before unwinding.
    classicLensHook.original=reinterpret_cast<void*>(&NativeLensFixture);
    ClassicLensHook(.9671381116f,true);
    CHECK(lensArgument==.9671381116f&&lensRebuild&&callbacks.load()==0);
    lensFault=true;CHECK(InvokeLensFaultFixture());CHECK(callbacks.load()==0);lensFault=false;
    for (int32_t mode:{1,-1,2})
    {
        renderer=mode;classicFov=.9671381116f;
        CHECK(!ApplyClassicTrackedProjection(classicFov));CHECK(classicFov==.9671381116f);
    }
    renderer=0;
    NodeMatrix source{};source.scale=.3f;
    const SaberBoneMatrix initial{{1,0,0,0, 0,1,0,0, 0,0,1,0, 123,456,789,1}};
    SaberBoneMatrix output=initial;
    CHECK(ScaleConvertedSkin(&source,&output));CHECK(output.value[0]==.3f&&output.value[12]==123);
    // Native Anniversary skinning owns a copied bone array. The next native
    // prepare can invalidate the global palette receipt between ANY two bone
    // conversions without changing this array. Its self-contained scale must
    // apply consistently to all bones, including hidden forearms.
    for (float scale:{.00001f,.3f,1.0f,3.0f})
    {
        source.scale=scale;CHECK(publish());
        for (unsigned bone=0;bone<8;++bone)
        {
            if (bone==3) lastApplied.store(0,std::memory_order_release);
            output=initial;
            CHECK(ScaleConvertedSkin(&source,&output));
            CHECK(output.value[0]==scale&&output.value[5]==scale&&output.value[10]==scale);
            CHECK(output.value[12]==123&&output.value[13]==456&&output.value[14]==789&&output.value[15]==1);
            if (scale==1) CHECK(std::memcmp(&output,&initial,sizeof(output))==0);
        }
    }
    source.scale=.3f;CHECK(publish());
    renderer=1;
    std::array<uint8_t,0x40> model{};
    uint32_t flags=0x10000000u;std::memcpy(model.data()+0x28,&flags,sizeof(flags));
    float constants[96]{};for (size_t index=92;index<96;++index) constants[index]=1;
    CHECK(ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data())));
    for (size_t index=92;index<96;++index) CHECK(constants[index]==0);
    // Exact material offsets are independently exercised by the native writer
    // emulator. At the production transaction boundary, preserve every other
    // byte and reject partial selectors or blocked presentation atomically.
    for (size_t offset:{size_t(0x170),size_t(0x20),size_t(0x70)})
    {
        for (size_t index=0;index<96;++index) constants[index]=float(index)+.25f;
        for (size_t index=offset/4;index<offset/4+4;++index) constants[index]=1;
        float before[96]{};std::memcpy(before,constants,sizeof(before));
        CHECK(ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data()),offset));
        for (size_t index=0;index<96;++index)
            CHECK(constants[index]==((index>=offset/4&&index<offset/4+4)?0:before[index]));
        std::memcpy(constants,before,sizeof(before));constants[offset/4+2]=.5f;
        std::memcpy(before,constants,sizeof(before));
        CHECK(!ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data()),offset));
        CHECK(std::memcmp(before,constants,sizeof(before))==0);
    }
    // Native material constants are constructed on preparation workers before
    // the render thread enters either eye. A fresh gameplay policy must work
    // with no eye TLS and no latest-palette receipt.
    CHECK(publish());lastApplied.store(0,std::memory_order_release);
    anniversaryEyeValid=false;anniversaryPrimaryValid=false;
    CHECK(!HaloCEFirstPerson_Armed());
    for (size_t offset:{size_t(0x170),size_t(0x20),size_t(0x70)})
    {
        for (size_t index=offset/4;index<offset/4+4;++index) constants[index]=1;
        CHECK(ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data()),offset));
        for (size_t index=offset/4;index<offset/4+4;++index) CHECK(constants[index]==0);
    }
    gameplayValid=false;
    for (size_t index=92;index<96;++index) constants[index]=1;
    CHECK(!ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data())));
    for (size_t index=92;index<96;++index) CHECK(constants[index]==1);
    gameplayValid=true;anniversaryEyeValid=true;anniversaryPrimaryValid=true;
    // Native GLT/ZFILL/SFX workers simultaneously read the same published
    // policy. Reader-reader contention must not randomly choose different
    // lenses for color and depth when that policy has not changed at all.
    CHECK(concurrentGameplay.Publish(gameplayContext));concurrentGameplayReads=true;
    std::atomic<bool> startMaterialWorkers{};
    std::atomic<unsigned> materialMisses{};
    std::array<std::thread,8> materialWorkers;
    for (auto& worker:materialWorkers) worker=std::thread([&] {
        while (!startMaterialWorkers.load(std::memory_order_acquire)) std::this_thread::yield();
        float localConstants[96]{};
        for (unsigned pass=0;pass<20000;++pass)
        {
            const size_t offset=pass%3==0?0x170:pass%3==1?0x20:0x70;
            for (size_t lane=offset/4;lane<offset/4+4;++lane) localConstants[lane]=1;
            if (!ApplyTrackedProjection(localConstants,reinterpret_cast<uintptr_t>(model.data()),offset))
                materialMisses.fetch_add(1,std::memory_order_relaxed);
        }
    });
    startMaterialWorkers.store(true,std::memory_order_release);
    for (auto& worker:materialWorkers) worker.join();
    concurrentGameplayReads=false;
    std::printf("CE concurrent material policy: %u refused of 160000 unchanged-policy writes\n",materialMisses.load());
    CHECK(materialMisses.load()==0);
    // Fresh policy still rejects Classic, missing gameplay ownership, and a
    // policy revoked between reading native constants and publishing them.
    for (unsigned fault=0;fault<3;++fault)
    {
        for (size_t index=92;index<96;++index) constants[index]=1;
        if (fault==0) renderer=0;
        if (fault==1) gameplayValid=false;
        if (fault==2) { gameplayReads=0;revokeGameplayOnRead=2; }
        const auto changed=projectionChangedRefused.load();
        CHECK(!ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data())));
        for (size_t index=92;index<96;++index) CHECK(constants[index]==1);
        if (fault==2) CHECK(projectionChangedRefused.load()==changed+1);
        renderer=1;gameplayValid=true;revokeGameplayOnRead=0;
    }
    testContext.tracking.controllers.controlsPresentationBlocked=true;CHECK(publish());
    classicFov=.9671381116f;CHECK(!ApplyClassicTrackedProjection(classicFov));CHECK(classicFov==.9671381116f);
    for (size_t index=92;index<96;++index) constants[index]=1;
    CHECK(!ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data())));
    for (size_t index=92;index<96;++index) CHECK(constants[index]==1);
    testContext.tracking.controllers.controlsPresentationBlocked=false;

    // A renderer switch or recenter makes the previous palette ineligible,
    // even though a fresh gameplay camera/rig remains available right now.
    for (unsigned fault=0;fault<6;++fault)
    {
        CHECK(publish());
        const RenderContext saved=testContext;
        if (fault==0) ++testContext.rendererEpoch;
        if (fault==1) ++testContext.referenceRevision;
        if (fault==2) ++testContext.tracking.spaceEpoch;
        if (fault==3) testContext.tracking.serial+=9;
        if (fault==4) ++testGeneration;
        if (fault==5) testTitle=GameTitle::Halo3;
        CHECK(!HaloCEFirstPerson_Armed());output=initial;
        classicFov=.9671381116f;CHECK(!ApplyClassicTrackedProjection(classicFov));CHECK(classicFov==.9671381116f);
        // A copied source matrix retains its scale across frame/reference
        // churn. Title/feature lifetime changes still reject the consumer.
        CHECK(ScaleConvertedSkin(&source,&output)==(fault<4));
        if (fault<4) CHECK(output.value[0]==source.scale&&output.value[12]==123);
        else CHECK(std::memcmp(&output,&initial,sizeof(output))==0);
        for (size_t index=92;index<96;++index) constants[index]=1;
        CHECK(!ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data())));
        for (size_t index=92;index<96;++index) CHECK(constants[index]==1);
        testContext=saved;testGeneration=3;testTitle=GameTitle::HaloCE;
    }
    CHECK(publish(251));CHECK(!HaloCEFirstPerson_Armed());
    CHECK(publish());contextValid=false;CHECK(!HaloCEFirstPerson_Armed());contextValid=true;
    // Failed new preparation cannot claim a Classic tracked-palette receipt.
    // Fresh Anniversary material policy still selects the world lens; a copied bone
    // retains its explicit scale, and stock scale one stays byte-identical.
    CHECK(publish());PrepareHook(0);CHECK(prepareCalls==1&&nativeSawInvalidated);
    CHECK(!HaloCEFirstPerson_Armed());output=initial;
    classicFov=.9671381116f;CHECK(!ApplyClassicTrackedProjection(classicFov));CHECK(classicFov==.9671381116f);
    CHECK(ScaleConvertedSkin(&source,&output));CHECK(output.value[0]==source.scale);
    source.scale=1;output=initial;
    CHECK(ScaleConvertedSkin(&source,&output));CHECK(std::memcmp(&output,&initial,sizeof(output))==0);
    CHECK(ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data())));
    for (size_t index=92;index<96;++index) CHECK(constants[index]==0);
    anniversaryEyeValid=false;
    gameplayValid=false;
    for (size_t index=92;index<96;++index) constants[index]=1;
    CHECK(!ApplyTrackedProjection(constants,reinterpret_cast<uintptr_t>(model.data())));
    for (size_t index=92;index<96;++index) CHECK(constants[index]==1);
    anniversaryEyeValid=true;gameplayValid=true;
    // Other output users cannot invalidate the locally owned user's receipt.
    CHECK(publish());PrepareHook(1);CHECK(HaloCEFirstPerson_Armed()&&prepareCalls==2);
    CHECK(callbacks.load()==0);
    // Persistent support grip: the outermost CE prepare is the tri-state
    // producer for the wired title. A validated user record with a live weapon
    // is KnownPresent; a non-null raw slot that failed validation is Unknown,
    // never absence; a valid user record whose raw slot is empty is
    // KnownAbsent; and the unwired gate publishes nothing.
    {
        const auto savedTargetPlayer = targetPlayer;
        const bool savedTargetPlayerValid = targetPlayerValid;
        const unsigned publishesBefore = supportPublishCount;
        CHECK(publish());
        targetPlayer = {};
        targetPlayer.generation = testGeneration;
        targetPlayer.unit = 0x11110001u;
        targetPlayer.weapon = 0x22220001u;
        targetPlayer.hasFirstPersonUserRecord = true;
        targetPlayer.weaponSlotPresent = true;
        targetPlayerValid = true;
        supportWired = false;
        PrepareHook(0);
        CHECK(supportPublishCount == publishesBefore);
        supportWired = true;
        PrepareHook(0);
        CHECK(supportPublishCount == publishesBefore + 1);
        CHECK(lastSupportTitle == uint32_t(GameTitle::HaloCE) &&
            lastSupportGeneration == testGeneration &&
            lastSupportOwner.title == GameTitle::HaloCE &&
            lastSupportOwner.unit == 0x11110001u &&
            lastSupportOwner.weapon == 0x22220001u &&
            lastSupportEvidence == support_grip::OwnerEvidence::KnownPresent);
        targetPlayer = {};
        targetPlayer.generation = testGeneration;
        targetPlayer.hasFirstPersonUserRecord = true;
        targetPlayer.weaponSlotPresent = true;
        PrepareHook(0);
        CHECK(supportPublishCount == publishesBefore + 2);
        CHECK(lastSupportEvidence == support_grip::OwnerEvidence::Unknown &&
            lastSupportOwner.unit == 0xFFFFFFFFu &&
            lastSupportOwner.weapon == 0xFFFFFFFFu);
        targetPlayer.weaponSlotPresent = false;
        PrepareHook(0);
        CHECK(supportPublishCount == publishesBefore + 3);
        CHECK(lastSupportEvidence == support_grip::OwnerEvidence::KnownAbsent &&
            lastSupportOwner.unit == 0xFFFFFFFFu &&
            lastSupportOwner.weapon == 0xFFFFFFFFu);
        // No local player record at all is Unknown, never absence.
        targetPlayer = {};
        targetPlayer.generation = testGeneration;
        targetPlayerValid = false;
        PrepareHook(0);
        CHECK(supportPublishCount == publishesBefore + 4);
        CHECK(lastSupportEvidence == support_grip::OwnerEvidence::Unknown &&
            lastSupportGeneration == 0);
        targetPlayer = savedTargetPlayer;
        targetPlayerValid = savedTargetPlayerValid;
        supportWired = false;
    }
    // Persistent support grip, CE prepare-hook trust source (headset round 2):
    // the outermost prepare must resolve the invocation owner from its OWN
    // validated local-player read, never from the optional gun_barrel_aim
    // muzzle read above it. With gun_barrel_aim off (the shipping default) that
    // gated read never runs, so a decision consuming its state would read
    // generation 0 -> Unknown -> detach on every invocation: the independent
    // one-hand carrier would replace the frozen support carrier and the support
    // hand would never attach ("grip doesn't work" with PG on). The relationship
    // owner here matches the read owner, so the support hand attaches while the
    // gated muzzle read stays off; owner mismatch, a failed read and an
    // unreadable relationship still detach (fail-closed).
    {
        const auto savedContext = testContext;
        const auto savedTargetPlayer = targetPlayer;
        const bool savedTargetPlayerValid = targetPlayerValid;
        const bool savedSupportWired = supportWired;
        const auto savedRelationship = supportRelationship;
        const bool savedRelationshipValid = supportRelationshipValid;
        auto& usersSlot = *reinterpret_cast<uintptr_t*>(moduleBase+0x2d9cd90);
        const uintptr_t savedUsers = usersSlot;
        static alignas(16) uint8_t supportUsersStorage[0x2000]{};
        usersSlot = reinterpret_cast<uintptr_t>(supportUsersStorage);
        constexpr uint32_t supportUnit = 0x11110001u, supportWeapon = 0x22220001u;
        auto& rig = testContext.tracking.controllers;
        rig.gunBarrelAim = false;
        rig.primaryAim = {true,{1,2,3},{0,0,0,1}};
        rig.independentPrimaryAim = {true,{4,5,6},{0,0,0,1}};
        rig.primaryAimSupportDerived = true;
        rig.supportGripAttached = false;
        targetPlayer = {};
        targetPlayer.generation = testGeneration;
        targetPlayer.unit = supportUnit;
        targetPlayer.weapon = supportWeapon;
        targetPlayer.hasFirstPersonUserRecord = true;
        targetPlayer.weaponSlotPresent = true;
        targetPlayerValid = true;
        supportWired = true;
        supportRelationship = {};
        supportRelationship.engaged = true;
        supportRelationship.title = GameTitle::HaloCE;
        supportRelationship.generation = testGeneration;
        supportRelationship.epoch = 7;
        supportRelationship.unit = supportUnit;
        supportRelationship.weapon = supportWeapon;
        supportRelationshipValid = true;
        const auto samePose = [](const auto& a,const auto& b)
        {
            return a.valid==b.valid&&a.position.x==b.position.x&&a.position.y==b.position.y&&
                a.position.z==b.position.z&&a.orientation.x==b.orientation.x&&
                a.orientation.y==b.orientation.y&&a.orientation.z==b.orientation.z&&
                a.orientation.w==b.orientation.w;
        };
        // gun_barrel_aim off + same owner: the dedicated read proves the
        // relationship owner, the support hand stays attached, and the frozen
        // support-derived carrier is not substituted with the independent one.
        // The gated muzzle read keeps its behaviour: no muzzle origin is frozen.
        frozenPrepareCaptured = false;
        CHECK(publish());
        PrepareHook(0);
        CHECK(frozenPrepareCaptured && frozenPrepareScope.valid);
        CHECK(frozenPrepareScope.muzzleUnit == UINT32_MAX &&
            frozenPrepareScope.muzzleWeapon == UINT32_MAX);
        CHECK(frozenPrepareScope.context.tracking.controllers.supportGripAttached);
        CHECK(samePose(frozenPrepareScope.context.tracking.controllers.primaryAim,rig.primaryAim));
        CHECK(frozenPrepareScope.context.tracking.controllers.primaryAimSupportDerived);
        // A different relationship owner still detaches: this is real owner
        // evidence, not a blanket attach for any state.
        supportRelationship.weapon = supportWeapon ^ 0x10000u;
        frozenPrepareCaptured = false;
        PrepareHook(0);
        CHECK(frozenPrepareCaptured &&
            !frozenPrepareScope.context.tracking.controllers.supportGripAttached);
        CHECK(samePose(frozenPrepareScope.context.tracking.controllers.primaryAim,rig.independentPrimaryAim));
        CHECK(!frozenPrepareScope.context.tracking.controllers.primaryAimSupportDerived);
        // A failed validated read fails closed: even though the fixture still
        // exposes a matching raw record, the decision must not trust it (the
        // reset state reads as generation 0 -> Unknown -> detach).
        supportRelationship.weapon = supportWeapon;
        targetPlayerValid = false;
        frozenPrepareCaptured = false;
        PrepareHook(0);
        CHECK(frozenPrepareCaptured &&
            !frozenPrepareScope.context.tracking.controllers.supportGripAttached);
        CHECK(samePose(frozenPrepareScope.context.tracking.controllers.primaryAim,rig.independentPrimaryAim));
        CHECK(!frozenPrepareScope.context.tracking.controllers.primaryAimSupportDerived);
        // An unreadable durable relationship still detaches.
        targetPlayerValid = true;
        supportRelationshipValid = false;
        frozenPrepareCaptured = false;
        PrepareHook(0);
        CHECK(frozenPrepareCaptured &&
            !frozenPrepareScope.context.tracking.controllers.supportGripAttached);
        CHECK(samePose(frozenPrepareScope.context.tracking.controllers.primaryAim,rig.independentPrimaryAim));
        CHECK(!frozenPrepareScope.context.tracking.controllers.primaryAimSupportDerived);
        // PG off: no durable read, no substitution, the frozen invocation is the
        // untouched base context.
        supportRelationshipValid = true;
        supportWired = false;
        frozenPrepareCaptured = false;
        PrepareHook(0);
        CHECK(frozenPrepareCaptured &&
            !frozenPrepareScope.context.tracking.controllers.supportGripAttached);
        CHECK(samePose(frozenPrepareScope.context.tracking.controllers.primaryAim,rig.primaryAim));
        CHECK(frozenPrepareScope.context.tracking.controllers.primaryAimSupportDerived);
        frozenPrepareCaptured = false;
        usersSlot = savedUsers;
        testContext = savedContext;
        targetPlayer = savedTargetPlayer;
        targetPlayerValid = savedTargetPlayerValid;
        supportWired = savedSupportWired;
        supportRelationship = savedRelationship;
        supportRelationshipValid = savedRelationshipValid;
        CHECK(publish());
    }
    // The shared production stack verifier rejects batches above eight. A
    // blocked second batch must retain the entire native lifetime for retry.
    // No real game hooks/module are needed to verify this admission boundary.
    // Actual production particle draw + upload hooks own a private full-capacity
    // copy, with only FP lens selectors changed. Source emitters/backing stay
    // exact through partial offsets, exceptions and nested auxiliary draws.
    CHECK(publish());renderContextValid=true;anniversaryEyeValid=true;anniversaryPrimaryValid=true;
    particleProjectionInstalled=true;
    particleDrawHook.original=reinterpret_cast<void*>(&NativeParticleDrawFixture);
    particleCommitHook.original=reinterpret_cast<void*>(&NativeParticleCommitFixture);
    for (const auto shape: {std::pair{0,201},std::pair{117,4096}})
    {
        const int first=shape.first,capacity=shape.second;
        particleBacking.fill(77);
        for (size_t emitter=0;emitter<9;++emitter)
            particleBacking[size_t(first)*4+(21+emitter*20+10)*4]=emitter%2?0.0f:1.0f;
        particleBaseline=particleBacking;
        ParticleBuffer nativeBuffer{moduleBase+0x17f8c78,first,first+201,capacity,0,particleBacking.data(),0,789};
        particleSource=&nativeBuffer;particleExpectCorrection=true;
        ParticleDrawHook(&particleCamera,55,2,66);
        CHECK(particleNativeChecks&&callbacks.load()==0&&!particleScope);
        CHECK(nativeBuffer.first==capacity&&nativeBuffer.end==0);
        for (size_t index=0;index<size_t(capacity)*4;++index)
        {
            bool selector=false;
            for (size_t emitter=0;emitter<9;++emitter)
                selector|=index==size_t(first)*4+(21+emitter*20+10)*4;
            CHECK(particleUploaded[index]==(selector?0.0f:particleBaseline[index]));
        }
        CHECK(particleBacking==particleBaseline);
        nativeBuffer.first=first;nativeBuffer.end=first+201;
        particleThrow=true;const ParticleBuffer before=nativeBuffer;
        CHECK(InvokeParticleFaultFixture());particleThrow=false;
        CHECK(particleNativeChecks&&!std::memcmp(&nativeBuffer,&before,sizeof(before)));
        CHECK(particleBacking==particleBaseline&&callbacks.load()==0&&!particleScope);
        particleNested=true;ParticleDrawHook(&particleCamera,55,2,66);
        CHECK(particleNativeChecks&&!particleScope&&callbacks.load()==0);
        nativeBuffer=before;particleMutate=true;
        ParticleDrawHook(&particleCamera,55,2,66);particleMutate=false;
        CHECK(nativeBuffer.first==7&&nativeBuffer.end==11&&particleBacking==particleBaseline);
        nativeBuffer=before;particleRevoke=true;particleExpectCorrection=false;
        ParticleDrawHook(&particleCamera,55,2,66);particleRevoke=false;anniversaryPrimaryValid=true;
        CHECK(particleNativeChecks&&particleBacking==particleBaseline);
        nativeBuffer=before;
        anniversaryPrimaryValid=false;
        const auto observedDraws=particleDrawObserved.load(),refusedEyes=particleEyeRefused.load();
        ParticleDrawHook(&particleCamera,55,2,66);anniversaryPrimaryValid=true;
        CHECK(particleNativeChecks&&particleBacking==particleBaseline);
        CHECK(particleDrawObserved.load()==observedDraws+1&&particleEyeRefused.load()==refusedEyes+1);
        nativeBuffer=before;nativeBuffer.gpu=0;
        ParticleDrawHook(&particleCamera,55,2,66);
        CHECK(particleNativeChecks&&particleBacking==particleBaseline);
        nativeBuffer=before;nativeBuffer.first=capacity+1;
        ParticleDrawHook(&particleCamera,55,2,66);
        CHECK(particleNativeChecks&&particleBacking==particleBaseline);
        nativeBuffer=before;
        particleBacking[size_t(first)*4+31*4]=.5f;particleBaseline=particleBacking;
        ParticleDrawHook(&particleCamera,55,2,66);
        CHECK(particleNativeChecks&&particleBacking==particleBaseline);
        nativeBuffer=before;nativeBuffer.first=INT_MAX;nativeBuffer.end=INT_MIN;
        // A malformed range falls through unchanged to this explicit fake
        // native service; no arithmetic overflow or optional feature write.
        ParticleDrawHook(&particleCamera,55,2,66);
        CHECK(particleNativeChecks&&particleBacking==particleBaseline);
    }
    CHECK(particleNativeCalls>=14);
    // Actual optional visibility transaction: only the secondary source-player
    // selector changes in a borrowed complete list; native flags, all cameras,
    // auxiliary views and opaque tail bytes remain byte-identical.
    renderer=1;CHECK(publish());visibilityInstalled=true;
    visibilityPrepareHook.original=reinterpret_cast<void*>(&NativeVisibilityFixture);
    visibilitySubmitHook.original=reinterpret_cast<void*>(&NativeVisibilitySubmitFixture);
    visibilityListAddress=reinterpret_cast<uintptr_t>(&visibilityNativeList);
    visibilityModelAddress=reinterpret_cast<uintptr_t>(visibilityNativeModel.data());
    FirstPersonVisibilityRecord registration{0,0x1234,7,0,visibilityModelAddress};
    const uintptr_t registrations=reinterpret_cast<uintptr_t>(&registration);
    const int32_t one=1;const uint32_t modelTag=0x1234,modelFlags=0x4400;
    std::memcpy(reinterpret_cast<void*>(moduleBase+0x2b050e8),&registrations,8);
    std::memcpy(reinterpret_cast<void*>(moduleBase+0x2b050f0),&one,4);
    std::memcpy(reinterpret_cast<void*>(moduleBase+0x1b7aa88),&modelTag,4);
    std::memcpy(visibilityNativeModel.data()+4,&modelFlags,4);
    for (size_t index=0;index<sizeof(visibilityNativeList);++index)
        reinterpret_cast<uint8_t*>(&visibilityNativeList)[index]=uint8_t(index*37+11);
    visibilityNativeList.primary.count=4;
    visibilityNativeList.primary.views[0].viewIndex=0;
    visibilityNativeList.primary.views[1].viewIndex=1;
    const auto listBefore=visibilityNativeList;
    auto visibilityCall=[&](bool expected)
    {
        visibilityExpectPrivate=expected;const auto calls=visibilityNativeCalls;
        VisibilityPrepareHook(visibilityListAddress,37,.0125f,visibilityModelAddress);
        return visibilityNativeChecks&&visibilityNativeCalls==calls+1&&callbacks.load()==0&&
            !std::memcmp(&visibilityNativeList,&listBefore,sizeof(listBefore));
    };
    for (uint32_t flags:{0u,0x80u,0x100u,0x180u,0x98760180u})
    {
        visibilityNativeFlags=flags;std::memcpy(visibilityNativeModel.data()+8,&flags,4);
        CHECK(visibilityCall(true)); // Hidden-weapon flags remain native consumer policy.
    }
    visibilityFault=true;visibilityExpectPrivate=true;
    CHECK(InvokeVisibilityFaultFixture());visibilityFault=false;
    if (!visibilityNativeChecks||callbacks.load()||std::memcmp(&visibilityNativeList,&listBefore,sizeof(listBefore)))
        std::fprintf(stderr,"visibility exception cleanup: native=%d callbacks=%u sourceChanged=%d\n",
            visibilityNativeChecks,callbacks.load(),std::memcmp(&visibilityNativeList,&listBefore,sizeof(listBefore))!=0);
    CHECK(visibilityNativeChecks&&callbacks.load()==0&&!std::memcmp(&visibilityNativeList,&listBefore,sizeof(listBefore)));
    visibilityListValid=false;CHECK(visibilityCall(false));visibilityListValid=true;
    visibilityListReads=0;visibilityRevokeOnRead=2;CHECK(visibilityCall(false));
    visibilityRevokeOnRead=0;visibilityListValid=true;CHECK(visibilityCall(true));
    registration.player=1;CHECK(visibilityCall(false));registration.player=0;
    registration.model=0x5555;CHECK(visibilityCall(false));registration.model=modelTag;
    registration.container=0;CHECK(visibilityCall(false));registration.container=visibilityModelAddress;
    *reinterpret_cast<int32_t*>(moduleBase+0x2b050f0)=257;CHECK(visibilityCall(false));
    *reinterpret_cast<int32_t*>(moduleBase+0x2b050f0)=1;
    std::memset(visibilityNativeModel.data()+4,0,4);CHECK(visibilityCall(false));
    std::memcpy(visibilityNativeModel.data()+4,&modelFlags,4);
    renderer=0;CHECK(visibilityCall(false));renderer=1;
    testContext.tracking.controllers.controlsPresentationBlocked=true;CHECK(visibilityCall(false));
    testContext.tracking.controllers.controlsPresentationBlocked=false;
    testTitle=GameTitle::Halo3;CHECK(visibilityCall(false));testTitle=GameTitle::HaloCE;
    visibilityInstalled=false;CHECK(visibilityCall(false));visibilityInstalled=true;
    CHECK(visibilityCall(true));
    VisibilitySubmitHook(0x123456789abcdef0ull,visibilityListAddress,1);
    CHECK(visibilitySubmitChecks&&visibilitySubmitRecords==1&&visibilitySubmitCalls==1&&callbacks.load()==0);
    visibilityFault=true;CHECK(InvokeVisibilitySubmitFaultFixture());visibilityFault=false;
    CHECK(visibilitySubmitChecks&&visibilitySubmitRecords==2&&visibilitySubmitCalls==2&&callbacks.load()==0);
    visibilityInstalled=false;visibilitySubmitExpected=false;visibilitySubmitRecords=visibilitySubmitCalls=0;
    VisibilitySubmitHook(0x123456789abcdef0ull,visibilityListAddress,1);
    CHECK(visibilitySubmitChecks&&visibilitySubmitCalls==1&&callbacks.load()==0);
    // Failed optional cleanup retains its trampolines and working camera/core.
    blockedQuiescenceCount=2;
    CHECK(!RemoveVisibility()&&visibilityRetiring&&installed.load()&&generation.load()==testGeneration);
    CHECK(visibilityPrepareHook.original&&visibilitySubmitHook.original);
    blockedQuiescenceCount=0;CHECK(RemoveVisibility()&&!visibilityRetiring&&installed.load());
    CHECK(!visibilityPrepareHook.original&&!visibilitySubmitHook.original);
    {
        // Weapon-order diagnostic: pure CE stable-commit decision. A commit
        // requires valid identity on both sides of the original prepare and
        // an unchanged generation/unit/weapon.
        HaloCELocalPlayerState before{};
        before.generation=testGeneration;before.hasControlledUnit=true;
        before.onFoot=true;before.nativePreparesFirstPerson=true;
        before.nativeInputBlocked=false;before.nativeLookBlocked=false;
        before.unit=0x11110001u;before.weapon=0x22220001u;
        HaloCELocalPlayerState after=before;
        CHECK(DiagnosticCeStableCommit(before,true,after,true));
        after.weapon=0x33330001u;
        CHECK(!DiagnosticCeStableCommit(before,true,after,true));
        after=before;after.nativePaused=true;
        CHECK(!DiagnosticCeStableCommit(before,true,after,true));
        after=before;after.generation=testGeneration+1;
        CHECK(!DiagnosticCeStableCommit(before,true,after,true));
        after=before;
        CHECK(!DiagnosticCeStableCommit(before,false,after,true));
        CHECK(!DiagnosticCeStableCommit(before,true,after,false));
    }
    RunCeMuzzleTests();
    quiescenceCalls=quiescenceRanges=0;
    blockedQuiescenceCount=7;
    CHECK(!Remove());CHECK(retiring.load()&&generation.load()==testGeneration);
    CHECK(prepareHook.original==reinterpret_cast<void*>(&NativePrepareFixture));
    CHECK(quiescenceCalls==2&&quiescenceRanges==15);
    blockedQuiescenceCount=0;quiescenceCalls=quiescenceRanges=0;
    CHECK(Remove());CHECK(!retiring.load()&&!generation.load()&&!prepareHook.original);
    CHECK(quiescenceCalls==2&&quiescenceRanges==15);
    CHECK(VirtualFree(native,0,MEM_RELEASE));
    std::puts("PASS production CE first-person receipt: Classic/Anniversary projection, continuous controller targeting, isolated particles, visibility, recovery and fifteen-hook retirement");
    return 0;
}
