// Execute the production camera/selector detours with bounded native fixtures.
#include "../src/dll/native_vehicle_first_person.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

static GameTitle activeTitle=GameTitle::None;
static uint32_t activeGeneration=7;
static NativeVehicleCameraOwner owned{7,0x12340001,0x56780002,0,0x10000};
static bool ownedValid=true,changeSeat=false,changeParent=false,changeGeneration=false,markerFault=false,
    stockNonfinite=false;
static int nativeMode=2,nativeResult=1,markerCount=1,checks=0,stockCalls=0;
static float markerX=10.0f;
static unsigned removedHooks=0,disabledHooks=0;
static bool refuseRemoval=false;
static void Check(bool condition,const char* name)
{++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
void Logf(const char*,...) {}
// One-machine changes allowed (co-op safe shots off) unless a test says otherwise.
static bool fireTimeAllowed=true;
bool Sight_FireTimeChangesAllowed() {return fireTimeAllowed;}
// A stand-in for the engine's TLS block: this module's own, with the frame interpolator's state pointer in it.
extern "C" unsigned long _tls_index;
static thread_local const uint8_t* t_interpolatorState=nullptr;
const wchar_t* LogDirectory() {return L"";}
GameTitle TitleAdapter_GetActiveTitle() {return activeTitle;}
uint32_t TitleAdapter_GetGeneration(GameTitle) {return activeGeneration;}
const TitleDescriptor* TitleRegistry_Find(GameTitle) {return nullptr;}
float Game_GetWorldScale() {return 0.5f;}
// The title's camera core as the lane is told it (game.h GameTitleCoreState): no title says, unless a test does.
static bool coreSays=false,coreInstalled=false;
static std::atomic<bool> coreTearingDown{false};
static unsigned pinned=0,unpinned=0;
bool Game_TitleCoreState(GameTitle,GameTitleCoreState& out) noexcept
{out={};if(!coreSays)return false;out.installed=coreInstalled;out.tearingDown=&coreTearingDown;return true;}
void HostUnloadGate_Pin(HMODULE,std::atomic<bool>*,const std::atomic<uint64_t>*,const char*,bool) {++pinned;}
void HostUnloadGate_Unpin(HMODULE) {++unpinned;}
bool Game_ReadVehicleCameraOwner(GameTitle title,NativeVehicleCameraOwner& output) noexcept
{output=owned;return ownedValid&&title==activeTitle;}
// The sight clock: a millisecond further on at every read.
uint64_t Sight_NowUs() {static uint64_t us=1000;return us+=1000;}
#if HALOMCCVR_DEV_DIAGNOSTICS
void Game_Halo4CameraOwnerReport() noexcept {}
void Halo2Observer6Dof_DevGameClock(long long& tick,float& factor) noexcept {tick=-1;factor=0;}
#endif
namespace sig {uintptr_t Find(uintptr_t,size_t,const char*) {return 0;}}
bool WaitForNativeDetourQuiescence(const void* const* functions,const void* const*,size_t count,
    const std::atomic<uint32_t>& callbacks)
{
    // Match the production range-admission contract. A stub checking only the
    // callback count missed the Release leaf-helper retirement deadlock.
    for(size_t i=0;i<count;++i) {
        DWORD64 imageBase=0;
        const auto* range=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(functions[i]),&imageBase,nullptr);
        if(!range||range->EndAddress<=range->BeginAddress)return false;
    }
    return callbacks.load()==0;
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_ERROR_UNSUPPORTED_FUNCTION;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID)
{if(refuseRemoval)return MH_ERROR_NOT_CREATED;++removedHooks;return MH_OK;}
extern "C" MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID) {++disabledHooks;return MH_OK;}
template<unsigned I> uint32_t __fastcall StockSelector(uint32_t,Mode<I>* mode)
{*mode=Mode<I>(nativeMode);++stockCalls;return nativeResult;}
void __fastcall StockCamera(uint32_t,const void*,void* result)
{
    ++stockCalls;std::memset(result,0x5a,128);
    const float point[3]{stockNonfinite?std::numeric_limits<float>::quiet_NaN():-10.0f,-20,-30};
    std::memcpy(static_cast<uint8_t*>(result)+4,point,12);
}
int16_t FillMarker(void* output)
{
    if(markerFault) RaiseException(0xe0424242,0,0,nullptr);
    std::memset(output,0,128);
    const float one=1.0f;std::memcpy(static_cast<uint8_t*>(output)+60,&one,4);
    std::memcpy(static_cast<uint8_t*>(output)+92,&one,4);
    const float point[3]{markerX,20,30};std::memcpy(static_cast<uint8_t*>(output)+96,point,12);
    if(changeSeat) ++owned.seat;
    if(changeParent) ++owned.parent;
    if(changeGeneration) ++activeGeneration;
    return int16_t(markerCount);
}
int16_t __fastcall CeMarker(uint32_t unit,const char* marker,void* output,int16_t maximum)
{Check(unit==owned.unit&&std::strcmp(marker,"head")==0&&maximum==1,"CE marker ABI");return FillMarker(output);}
int16_t __fastcall H2Marker(uint32_t unit,uint32_t marker,void* output,int16_t maximum)
{Check(unit==owned.unit&&marker==0x4000095&&maximum==1,"H2 marker ABI");return FillMarker(output);}
// The full query, asked for no marker: the vehicle's root node, as simulated or (interpolated) as drawn an eighth
// of a unit behind it.
static int vehicleRootQueries=0;
// A dev build's frame trace asks for the vehicle's two frames once more per placed camera.
#if HALOMCCVR_DEV_DIAGNOSTICS
constexpr int kTraceRootQueries=2;
#else
constexpr int kTraceRootQueries=0;
#endif
int16_t __fastcall H2InterpolatedMarker(uint32_t object,uint32_t marker,void* output,int16_t maximum,uint8_t direct,uint8_t interpolated)
{
    Check(object==owned.parent&&marker==0&&maximum==1&&!direct&&interpolated<=1,"H2 vehicle root query ABI");
    ++vehicleRootQueries;std::memset(output,0,128);
    const float frame[13]{1, 1,0,0, 0,1,0, 0,0,1, interpolated?99.875f:100.0f,200,300};
    std::memcpy(static_cast<uint8_t*>(output)+56,frame,sizeof(frame));return 1;
}
int16_t __fastcall H4Marker(uint32_t unit,uint32_t marker,void* output,int16_t maximum,uint8_t a,uint8_t b,uint8_t c)
{
    if (marker==0) {
        // The same query asked for no marker: the vehicle's root node, as simulated (c 0) or as drawn (c 1).
        Check(unit==owned.parent&&maximum==1&&!a&&!b&&c<=1,"H4 vehicle root query ABI");
        ++vehicleRootQueries;std::memset(output,0,128);
        const float frame[13]{1, 1,0,0, 0,1,0, 0,0,1, c?99.875f:100.0f,200,300};
        std::memcpy(static_cast<uint8_t*>(output)+56,frame,sizeof(frame));return 1;
    }
    Check(unit==owned.unit&&marker==0x122&&maximum==1&&!a&&!b&&c==1,"H4 marker ABI and interpolation");
    return FillMarker(output);
}
template<unsigned I> void Run()
{
    auto& r=runtime[I];activeTitle=kBindings[I].title;activeGeneration=7;
    owned={7,0x12340001,0x56780002,0,0x10000};ownedValid=true;
    r.generation=7;r.requested=true;r.faulted=false;
    r.original[Selector]=reinterpret_cast<void*>(&StockSelector<I>);
    r.original[Camera]=reinterpret_cast<void*>(&StockCamera);
    r.marker=I==0?reinterpret_cast<void*>(&CeMarker):I==1?reinterpret_cast<void*>(&H2Marker):reinterpret_cast<void*>(&H4Marker);
    Mode<I> mode{};alignas(16) uint8_t result[128]{};float position[3]{};
    auto camera=[&] {CameraHook<I>(owned.unit,nullptr,result);std::memcpy(position,result+4,12);};
    nativeMode=2;nativeResult=1;markerCount=1;markerX=10;
    g_config.vehicle_cam_forward_m=g_config.vehicle_cam_up_m=g_config.vehicle_cam_right_m=0;
    Check(SelectHook<I>(owned.unit,&mode)==0&&mode==2,"occupied seat chooses first person");
    camera();Check(position[0]==10&&position[1]==20&&position[2]==30,"camera at occupant head after native evaluation");
    for(unsigned j=0;j<128;++j) if(j<4||j>=16) Check(result[j]==0x5a,"all other camera fields preserved");
    if constexpr(I==2) {
        // The placed point is published for the render transaction (the Broadsword's view stands on it).
        NativeVehicleSeatPoint placed{};
        Check(NativeVehicleFirstPerson_PlacedPoint(activeTitle,placed)&&placed.point[0]==10&&placed.point[1]==20&&
            placed.point[2]==30&&placed.parent==owned.parent&&placed.seat==owned.seat&&placed.generation==7&&
            placed.vehicleIdentity==owned.vehicleIdentity&&placed.us,"H4 placed point published with its seat");
        Check(!NativeVehicleFirstPerson_PlacedPoint(GameTitle::Halo2,placed),"the placed point answers only Halo 4");
        activeGeneration=8;
        Check(!NativeVehicleFirstPerson_PlacedPoint(activeTitle,placed),"a point from another generation is no answer");
        activeGeneration=7;
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    if constexpr(I==2) {
        // Stage 0's native seat eye: the native point the override replaced, as an offset from the placed point.
        NativeVehicleSeatEyeDelta eye{};
        Check(NativeVehicleFirstPerson_SeatEyeDelta(activeTitle,eye)&&eye.parent==owned.parent&&eye.seat==owned.seat&&
            eye.generation==7&&eye.delta[0]==-20&&eye.delta[1]==-40&&eye.delta[2]==-60&&eye.ms,
            "H4 native seat eye is the native point minus the placed head, read before the overwrite");
        activeTitle=GameTitle::Halo2;
        Check(!NativeVehicleFirstPerson_SeatEyeDelta(GameTitle::Halo2,eye)&&!NativeVehicleFirstPerson_SeatEyeDelta(
            GameTitle::Halo4,eye),"the seat eye answers only for Halo 4 while it is the active title");
        activeTitle=GameTitle::Halo4;
        const auto published=r.eyePublished.load();
        stockNonfinite=true;camera();stockNonfinite=false;
        Check(r.eyePublished.load()==published&&NativeVehicleFirstPerson_SeatEyeDelta(activeTitle,eye)&&
            eye.delta[0]==-20,"a non-finite native point publishes nothing and keeps the last offset");
        nativeMode=0;camera();nativeMode=2;
        Check(r.eyePublished.load()==published,"a seat view that is not ours publishes no offset");
        (void)r.eyeWriting.test_and_set();camera();
        Check(r.eyePublished.load()==published&&r.eyeSkipped.load()==1,"a busy writer skips rather than tear");
        r.eyeWriting.clear();camera();
        Check(r.eyePublished.load()==published+1,"the next free writer publishes");
        activeGeneration=8;
        Check(!NativeVehicleFirstPerson_SeatEyeDelta(activeTitle,eye),"an offset from another generation is no answer");
        activeGeneration=7;r.faulted=true;
        Check(!NativeVehicleFirstPerson_SeatEyeDelta(activeTitle,eye),"a faulted feature publishes no eye");
        r.faulted=false;
        // The first-person witness: armed, the selector hook records what it decided for this thread.
        NativeVehicleFirstPerson_ArmSelectWitness();
        Check(NativeVehicleFirstPerson_TakeSelectWitness().calls==0,"an armed witness with no selector call is empty");
        // Taken (disarmed) and NOT re-armed: arming would clear whatever the unarmed call wrote, so the check
        // could never fail. Take again and the witness must still be empty.
        (void)SelectHook<I>(owned.unit,&mode);
        Check(NativeVehicleFirstPerson_TakeSelectWitness().calls==0,"an unarmed selector call is not recorded");
        NativeVehicleFirstPerson_ArmSelectWitness();
        (void)SelectHook<I>(owned.unit,&mode);(void)NativeVehicleFirstPerson_TakeSelectWitness();
        (void)SelectHook<I>(owned.unit,&mode);
        Check(NativeVehicleFirstPerson_TakeSelectWitness().calls==1,"a call after the take is not added to the taken one");
        NativeVehicleFirstPerson_ArmSelectWitness();
        (void)SelectHook<I>(owned.unit,&mode);
        auto w=NativeVehicleFirstPerson_TakeSelectWitness();
        Check(w.calls==1&&w.unit==owned.unit&&w.nativeResult==1&&w.result==0&&w.overridden&&w.mode==2,
            "the witness records the override taking the seat to first person");
        nativeMode=1;NativeVehicleFirstPerson_ArmSelectWitness();
        (void)SelectHook<I>(owned.unit,&mode);(void)SelectHook<I>(owned.unit,&mode);
        w=NativeVehicleFirstPerson_TakeSelectWitness();nativeMode=2;
        Check(w.calls==2&&w.result==1&&!w.overridden&&w.mode==1,"a native chase answer is recorded, and every call counted");
        std::thread other([]{NativeVehicleFirstPerson_ArmSelectWitness();});other.join();
        (void)SelectHook<I>(owned.unit,&mode);
        Check(NativeVehicleFirstPerson_TakeSelectWitness().calls==2,"another thread's arming does not arm this one");
    }
#endif
    g_config.vehicle_cam_forward_m=0.2f;g_config.vehicle_cam_up_m=0.4f;g_config.vehicle_cam_right_m=0.6f;
    camera();Check(std::fabs(position[0]-10.1f)<0.0001f&&std::fabs(position[1]-19.7f)<0.0001f&&
        std::fabs(position[2]-30.2f)<0.0001f,"universal seat trims use verified marker axes and world scale");
    const auto game=static_cast<unsigned>(activeTitle)-1;
    for(unsigned axis=0;axis<3;++axis){g_config.vehicle_cam_game[game][axis]=.1f*(axis+1);g_config.vehicle_cam_game_set[game][axis]=true;}
    camera();Check(std::fabs(position[0]-10.05f)<.0001f&&std::fabs(position[1]-19.85f)<.0001f&&
        std::fabs(position[2]-30.1f)<.0001f,"actual native camera consumes current title's three overrides");
    for(unsigned axis=0;axis<3;++axis)g_config.vehicle_cam_game_set[game][axis]=false;
    g_config.vehicle_cam_forward_m=g_config.vehicle_cam_up_m=g_config.vehicle_cam_right_m=0;
    owned.vehicleIdentity=0x123456789abcdef0ull;
    const int modelSlot=ConfigEnsureVehicleModelTrim(g_config,activeTitle,owned.vehicleIdentity,owned.seat);
    g_config.vehicle_model_trims[modelSlot].value[0]=.46f;g_config.vehicle_model_trims[modelSlot].set[0]=true;
    camera();Check(std::fabs(position[0]-10.23f)<.0001f,"stable vehicle/seat override reaches actual native camera");
    uint64_t publishedIdentity=0;int publishedSeat=-1;
    Check(NativeVehicleFirstPerson_TrimTarget(activeTitle,publishedIdentity,publishedSeat)&&
        publishedIdentity==owned.vehicleIdentity&&publishedSeat==owned.seat,"menu publication carries stable vehicle and seat identity");
    ++owned.seat;camera();Check(position[0]==10,"same vehicle's other seat inherits only game fallback");--owned.seat;
    ++owned.vehicleIdentity;camera();Check(position[0]==10,"different vehicle cannot reuse previous preset");
    owned.vehicleIdentity=0;camera();Check(position[0]==10&&!NativeVehicleFirstPerson_TrimTarget(activeTitle,publishedIdentity,publishedSeat),
        "unknown vehicle retains fallback and revokes previous identified menu target");
    if constexpr(I==1) {
        r.markerInterpolated=reinterpret_cast<void*>(&H2InterpolatedMarker);
        g_config.vehicle_cam_smoothing=true;vehicleRootQueries=0;camera();
        Check(std::fabs(position[0]-9.875f)<.0001f&&vehicleRootQueries==2+kTraceRootQueries,
            "H2 smoothing carries the camera by the vehicle as drawn between simulation ticks");
        vehicleRootQueries=0;Check(SelectHook<I>(owned.unit,&mode)==0&&vehicleRootQueries==0,
            "H2 selector asks only whether the head marker is there");
        g_config.vehicle_cam_smoothing=false;vehicleRootQueries=0;camera();
        Check(position[0]==10&&vehicleRootQueries==kTraceRootQueries,
            "H2 smoothing off leaves the simulation's marker");
        g_config.vehicle_cam_smoothing=true;r.markerInterpolated=nullptr;camera();
        Check(position[0]==10,"H2 without the full query leaves the simulation's marker");
    }
    for(int transition:{0,1,3,4,5}) {
        nativeMode=transition;Check(SelectHook<I>(owned.unit,&mode)==1,"transitions and special cameras preserved");
        camera();Check(position[0]==-10,"transition native position preserved");
    }
    nativeMode=2;r.requested=false;
    Check(SelectHook<I>(owned.unit,&mode)==1,"toggle off restores native choice");camera();Check(position[0]==-10,"toggle off native position");r.requested=true;
    Check(SelectHook<I>(0x99990003,&mode)==1,"remote player excluded");
    ownedValid=false;camera();Check(position[0]==-10,"missing owner stays native");ownedValid=true;
    for(int missing:{0,-1,2}) {
        markerCount=missing;Check(SelectHook<I>(owned.unit,&mode)==1,"invalid head count stays chase");
        camera();Check(position[0]==-10,"missing marker does not use fallback body position");
    }
    markerCount=1;markerX=std::numeric_limits<float>::quiet_NaN();
    Check(SelectHook<I>(owned.unit,&mode)==1,"nonfinite head stays native");markerX=10;
    changeSeat=true;Check(SelectHook<I>(owned.unit,&mode)==1,"seat change rejects stale marker");
    camera();Check(position[0]==-10,"camera rejects changed seat");changeSeat=false;
    changeParent=true;Check(SelectHook<I>(owned.unit,&mode)==1,"parent change rejects stale marker");
    camera();Check(position[0]==-10,"camera rejects changed parent");changeParent=false;
    changeGeneration=true;Check(SelectHook<I>(owned.unit,&mode)==1,"map generation change rejects marker");
    changeGeneration=false;camera();Check(position[0]==-10,"old generation remains stock");activeGeneration=7;
    markerFault=true;const auto faultResult=SelectHook<I>(owned.unit,&mode);
    Check(faultResult==1&&r.faulted,"native marker fault isolates feature");markerFault=false;
    camera();Check(position[0]==-10,"faulted feature forwards native camera");
    r.faulted=false;markerFault=true;camera();markerFault=false;
    Check(position[0]==-10&&r.faulted,"camera marker fault preserves native result and disables feature");
    Check(r.callbacks==0,"all callbacks drained including exception");
    r.faulted=false;owned.seat=-1;Check(SelectHook<I>(owned.unit,&mode)==1,"exit seat stays native");
    owned.seat=0;Check(SelectHook<I>(owned.unit,&mode)==0,"new valid seat recovers without cached anchor");
    nativeResult=0;Check(SelectHook<I>(owned.unit,&mode)==0,"native first-person seats remain first person");

    r.target[0]=reinterpret_cast<void*>(uintptr_t(0x1000+I*2));
    r.target[1]=reinterpret_cast<void*>(uintptr_t(0x1001+I*2));
    r.enabled[0]=r.enabled[1]=true;r.attempted=r.ready=true;
    const auto removedBefore=removedHooks,disabledBefore=disabledHooks;
    r.callbacks=1;
    Check(!Retire(I),"active camera callback defers retirement");
    Check(!r.requested&&removedHooks==removedBefore&&r.original[0]&&r.original[1],
        "closed admission retains active callback dependencies");
    Check(disabledHooks==disabledBefore+2,"both native ingress hooks disabled before waiting");
    r.callbacks=0;refuseRemoval=true;
    Check(!Retire(I)&&r.original[0]&&r.original[1],"failed removal preserves retryable trampolines");
    refuseRemoval=false;
    Check(Retire(I),"Release detour ranges allow drained feature retirement");
    Check(removedHooks==removedBefore+2&&!r.target[0]&&!r.target[1]&&!r.original[0]&&!r.original[1],
        "retirement removes exactly two drained hooks");
    Check(!r.attempted&&!r.ready&&!r.generation&&!r.marker,
        "retirement clears old generation so next title can initialize");
    // A TITLE WHOSE CAMERA CORE SAYS WHEN ITS HOOKS ARE IN (game.h GameTitleCoreState): the lane lets go of the game
    // DLL when the core is out, in the same title and generation, tells the unload gate, and takes nothing again
    // while the core stays out. A title that does not say keeps the reference until the title changes.
    {
        HMODULE self=nullptr;
        GetModuleHandleExW(0,nullptr,&self);   // a reference of the test's own for the lane to free
        r.module=self;r.generation=activeGeneration;r.attempted=true;
        const auto unpinnedBefore=unpinned;
        coreSays=false;
        NativeVehicleFirstPerson_Poll();
        Check(r.module==self&&unpinned==unpinnedBefore,"a title that does not say: the reference is kept");
        coreSays=true;coreInstalled=true;
        NativeVehicleFirstPerson_Poll();
        Check(r.module==self&&unpinned==unpinnedBefore,"the core installed: the reference is kept");
        coreInstalled=false;
        NativeVehicleFirstPerson_Poll();
        Check(!r.module&&unpinned==unpinnedBefore+1,
            "the core out in the same title and generation: the lane lets go and tells the gate");
        NativeVehicleFirstPerson_Poll();
        Check(!r.module&&!r.attempted&&unpinned==unpinnedBefore+1,"nothing is taken again while the core is out");
        coreSays=false;
    }
}
// CO-OP SAFE: the override answers the frame and never the simulation.
template<unsigned I> void RunCoopSafe()
{
    auto& r=runtime[I];activeTitle=kBindings[I].title;activeGeneration=7;
    owned={7,0x12340001,0x56780002,0,0x10000};ownedValid=true;
    r.generation=7;r.requested=true;r.faulted=false;
    r.original[Selector]=reinterpret_cast<void*>(&StockSelector<I>);
    r.original[Camera]=reinterpret_cast<void*>(&StockCamera);
    r.marker=I==0?reinterpret_cast<void*>(&CeMarker):I==1?reinterpret_cast<void*>(&H2Marker):reinterpret_cast<void*>(&H4Marker);
    r.markerInterpolated=nullptr;
    Mode<I> mode{};alignas(16) uint8_t result[128]{};float position[3]{};
    auto camera=[&] {CameraHook<I>(owned.unit,nullptr,result);std::memcpy(position,result+4,12);};
    nativeMode=2;nativeResult=1;markerCount=1;markerX=10;
    g_config.vehicle_cam_forward_m=g_config.vehicle_cam_up_m=g_config.vehicle_cam_right_m=0;
    const auto owns=[&] {const bool select=SelectHook<I>(owned.unit,&mode)==0;camera();return select&&position[0]==10;};
    const auto stock=[&] {const bool select=SelectHook<I>(owned.unit,&mode)==1;camera();return select&&position[0]==-10;};
    uint8_t state[8]{};
    const auto* block=reinterpret_cast<const uint8_t*>(reinterpret_cast<const uintptr_t*>(__readgsqword(0x58))[_tls_index]);
    const auto tick=[&](bool known,bool inside) {
        t_interpolatorState=state;state[3]=inside?1:0;
        r.tlsIndex=known?reinterpret_cast<const uint32_t*>(&_tls_index):nullptr;
        r.tickSlot=uint32_t(reinterpret_cast<const uint8_t*>(&t_interpolatorState)-block);r.tickByte=3;
    };
    fireTimeAllowed=true;tick(true,true);
    Check(owns(),"one-machine changes allowed: the override answers inside a tick too");
    tick(false,false);
    Check(owns(),"one-machine changes allowed: the override answers where the tick is not read");
    fireTimeAllowed=false;
    if constexpr (I==1) {
        tick(true,true);
        Check(owns(),"Halo 2 is stood down by its own caller, never by a tick read");
        NativeVehicleFirstPerson_SetStockOnThisThread(true);
        Check(stock(),"Halo 2's simulation caller gets the game's own camera");
        NativeVehicleFirstPerson_SetStockOnThisThread(false);
        Check(owns(),"and the override answers again once that call returns");
    } else {
        const auto before=r.simulationCalls.load();
        tick(true,true);
        Check(stock(),"co-op safe, inside a tick: the simulation gets the game's own camera");
        Check(r.simulationCalls.load()==before+1,"and the selector call for our seat is counted");
        tick(true,false);
        Check(owns(),"co-op safe, outside a tick: the frame gets the placed camera");
        tick(false,false);
        Check(stock(),"co-op safe, tick not read: the override answers nothing");
        tick(true,false);t_interpolatorState=nullptr;
        Check(stock(),"co-op safe, no interpolator state on this thread: the override answers nothing");
        t_interpolatorState=state;
        Check(SelectHook<I>(0x99990003,&mode)==1&&r.simulationCalls.load()==before+3,
            "another unit's call is the game's own and is not counted as ours");
    }
    fireTimeAllowed=true;r.tlsIndex=nullptr;r.tickSlot=r.tickByte=0;t_interpolatorState=nullptr;
    r.requested=false;r.original[Selector]=r.original[Camera]=nullptr;r.marker=nullptr;r.generation=0;
    Check(r.callbacks==0,"co-op safe paths drain their callbacks");
}
int main()
{
    Run<0>();Run<1>();Run<2>();
    RunCoopSafe<0>();RunCoopSafe<1>();RunCoopSafe<2>();
    std::printf("%d native vehicle first-person checks passed\n",checks);return 0;
}
