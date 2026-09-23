// Execute the production camera/selector detours with bounded native fixtures.
#include "../src/dll/native_vehicle_first_person.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

static GameTitle activeTitle=GameTitle::None;
static uint32_t activeGeneration=7;
static NativeVehicleCameraOwner owned{7,0x12340001,0x56780002,0,0x10000};
static bool ownedValid=true,changeSeat=false,changeParent=false,changeGeneration=false,markerFault=false;
static int nativeMode=2,nativeResult=1,markerCount=1,checks=0,stockCalls=0;
static float markerX=10.0f;
static unsigned removedHooks=0,disabledHooks=0;
static bool refuseRemoval=false;
static void Check(bool condition,const char* name)
{++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
void Logf(const char*,...) {}
const wchar_t* LogDirectory() {return L"";}
GameTitle TitleAdapter_GetActiveTitle() {return activeTitle;}
uint32_t TitleAdapter_GetGeneration(GameTitle) {return activeGeneration;}
const TitleDescriptor* TitleRegistry_Find(GameTitle) {return nullptr;}
float Game_GetWorldScale() {return 0.5f;}
bool Game_ReadVehicleCameraOwner(GameTitle title,NativeVehicleCameraOwner& output) noexcept
{output=owned;return ownedValid&&title==activeTitle;}
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
    const float point[3]{-10,-20,-30};std::memcpy(static_cast<uint8_t*>(result)+4,point,12);
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
int16_t __fastcall H2InterpolatedMarker(uint32_t unit,uint32_t marker,void* output,int16_t maximum,uint8_t direct,uint8_t interpolated)
{
    Check(unit==owned.unit&&marker==0x4000095&&maximum==1&&!direct&&interpolated==1,"H2 full marker interpolation ABI");
    const auto count=FillMarker(output);const float renderX=markerX+.125f;
    std::memcpy(static_cast<uint8_t*>(output)+96,&renderX,4);return count;
}
int16_t __fastcall H4Marker(uint32_t unit,uint32_t marker,void* output,int16_t maximum,uint8_t a,uint8_t b,uint8_t c)
{Check(unit==owned.unit&&marker==0x122&&maximum==1&&!a&&!b&&c==1,"H4 marker ABI and interpolation");return FillMarker(output);}
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
        g_config.vehicle_cam_smoothing=true;camera();
        Check(position[0]==10.125f,"H2 smoothing selects rendered marker position between simulation ticks");
        g_config.vehicle_cam_smoothing=false;camera();Check(position[0]==10,"H2 smoothing off restores simulation marker wrapper");
        g_config.vehicle_cam_smoothing=true;r.markerInterpolated=nullptr;
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
}
int main()
{Run<0>();Run<1>();Run<2>();std::printf("%d native vehicle first-person checks passed\n",checks);return 0;}
