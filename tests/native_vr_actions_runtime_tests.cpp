// Compile the production optional adapter with simulated native originals.
// No game process, module patch, game-file access or controller is required.
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
static ULONGLONG clockNow=1000;
static ULONGLONG WINAPI MockClock(){return clockNow;}
static BOOL WINAPI MockModule(DWORD,LPCWSTR,HMODULE*);
static BOOL WINAPI MockFree(HMODULE);
static void* MockReturnAddress();
static PRUNTIME_FUNCTION WINAPI MockLookup(DWORD64,PDWORD64,PUNWIND_HISTORY_TABLE);
#define GetTickCount64 MockClock
#define GetModuleHandleExW MockModule
#define FreeLibrary MockFree
#define _ReturnAddress MockReturnAddress
#define RtlLookupFunctionEntry MockLookup
#include "../src/dll/native_vr_actions.cpp"
#undef GetTickCount64
#undef GetModuleHandleExW
#undef FreeLibrary
#undef _ReturnAddress
#undef RtlLookupFunctionEntry

Config::Config()=default;
Config g_config{};
static unsigned checks{}, converterCalls{}, readerCalls{}, creates{}, disables{}, removes{}, frees{};
static unsigned failCreate{}, failEnable{};
static bool quiescent=true, proof=true;
static bool fallbackVerified=false;
static uint32_t testGeneration=7;
static GameTitle testTitle=GameTitle::HaloReach;
static bool switchGenerationOnTitleRead=false;
static std::vector<unsigned char> image(0x3000000);
static uintptr_t mockCallerRva=consumerBegin+0x10;
static native_action_button::LaterRecord physical{},h4Physical{};
static native_action_button::Gen3Record h3Physical{},odstPhysical{};
static unsigned char raw[16]{}, wrongRaw[16]{};
static void Check(bool value,const char* why) {
    ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
void Logf(const char*,...){}
bool Game_ReadVrActionBindings(vr_mapping::Transports& out,uint64_t,unsigned){out.fill(0x4000);return fallbackVerified;}
GameTitle TitleAdapter_GetActiveTitle(){
    if(switchGenerationOnTitleRead){switchGenerationOnTitleRead=false;runtime.generation=++testGeneration;}
    return testTitle;
}
uint32_t TitleAdapter_GetGeneration(GameTitle){return testGeneration;}
const TitleDescriptor* TitleRegistry_Find(GameTitle){
    static const TitleDescriptor d{GameTitle::HaloReach,L"fixture", "Reach",true,0,0};return &d;
}
static BOOL WINAPI MockModule(DWORD,LPCWSTR,HMODULE* out){*out=reinterpret_cast<HMODULE>(image.data());return TRUE;}
static BOOL WINAPI MockFree(HMODULE){++frees;return TRUE;}
static void* MockReturnAddress(){return image.data()+mockCallerRva;}
static PRUNTIME_FUNCTION WINAPI MockLookup(DWORD64 address,PDWORD64 base,PUNWIND_HISTORY_TABLE){
    static RUNTIME_FUNCTION function{static_cast<DWORD>(consumerBegin),static_cast<DWORD>(consumerEnd),0};
    static RUNTIME_FUNCTION ce{static_cast<DWORD>(native_ce::consumerRva),static_cast<DWORD>(native_ce::consumerEnd),0};
    static RUNTIME_FUNCTION h3{static_cast<DWORD>(native_h3::consumerBegin),static_cast<DWORD>(native_h3::consumerEnd),0};
    *base=reinterpret_cast<DWORD64>(image.data());
    if(address==*base+native_h3::consumerBegin)return &h3;
    static RUNTIME_FUNCTION odst{static_cast<DWORD>(native_odst::consumerBegin),static_cast<DWORD>(native_odst::consumerEnd),0};
    if(address==*base+native_odst::consumerBegin)return &odst;
    static RUNTIME_FUNCTION h4{static_cast<DWORD>(native_h4::consumerBegin),static_cast<DWORD>(native_h4::consumerEnd),0};
    if(address==*base+native_h4::consumerBegin)return &h4;
    static RUNTIME_FUNCTION h2{static_cast<DWORD>(native_h2::consumerBegin),static_cast<DWORD>(native_h2::consumerEnd),0};
    if(address==*base+native_h2::consumerBegin)return &h2;
    return address==*base+native_ce::consumerRva?&ce:&function;
}
namespace sig {
uintptr_t Find(uintptr_t base,size_t,const char* pattern){
    if(!proof || base!=reinterpret_cast<uintptr_t>(image.data()))return 0;
    if(testTitle==GameTitle::Halo2){
        if(pattern==native_h2::converterPattern)return base+native_h2::converterRva;
        if(pattern==native_h2::copierPattern)return base+native_h2::copierRva;
        if(pattern==native_h2::elapsedPattern)return base+native_h2::elapsedRva;
        if(pattern==native_h2::cadenceProofPattern)return base+native_h2::cadenceProofRva;
        if(pattern==native_h2::consumerEntryPattern)return base+native_h2::consumerBegin;
        if(pattern==native_h2::callerPattern)return base+native_h2::callerProofRva;
        if(pattern==native_h2::consumerReloadPattern)return base+native_h2::consumerReloadRva;
    }
    if(testTitle==GameTitle::Halo4){
        if(pattern==native_h4::converterPattern)return base+native_h4::converterRva;
        if(pattern==native_h4::padProofPattern)return base+native_h4::padProofRva;
        if(pattern==native_h4::controllerLoopPattern)return base+native_h4::controllerLoopRva;
        if(pattern==native_h4::consumerEntryPattern)return base+native_h4::consumerBegin;
        if(pattern==native_h4::consumerReloadPattern)return base+native_h4::consumerReloadRva;
        const auto& d=kGestureBindings[native_h4::readerIndex];
        if(pattern==d.readerPattern)return base+d.readerRva;
        if(pattern==d.statePattern)return base+d.stateLoadRva;
    }
    if(pattern==converterPattern)return base+converterRva;
    if(pattern==padProofPattern)return base+padProofRva;
    if(pattern==controllerLoopPattern)return base+controllerLoopRva;
    if(pattern==consumerEntryPattern)return base+consumerBegin;
    if(pattern==consumerReloadPattern)return base+consumerReloadRva;
    if(pattern==native_ce::converterPattern)return base+native_ce::converterRva;
    if(pattern==native_ce::consumerPattern)return base+native_ce::consumerRva;
    if(pattern==native_ce::getterPattern)return base+native_ce::getterRva;
    if(pattern==native_ce::stateProofPattern)return base+native_ce::stateProofRva;
    if(pattern==native_ce::callerPattern)return base+native_ce::callerProofRva;
    if(pattern==native_h3::converterPattern)return base+native_h3::converterRva;
    if(pattern==native_h3::consumerEntryPattern)return base+native_h3::consumerBegin;
    if(pattern==native_h3::consumerReloadPattern)return base+native_h3::consumerReloadRva;
    if(pattern==native_h3::controllerProofPattern)return base+native_h3::controllerProofRva;
    const auto& h3=kGestureBindings[native_h3::readerIndex];
    if(pattern==h3.readerPattern)return base+h3.readerRva;
    if(pattern==h3.statePattern)return base+h3.stateLoadRva;
    if(pattern==native_odst::converterPattern)return base+native_odst::converterRva;
    if(pattern==native_odst::consumerEntryPattern)return base+native_odst::consumerBegin;
    if(pattern==native_odst::consumerReloadPattern)return base+native_odst::consumerReloadRva;
    if(pattern==native_odst::controllerProofPattern)return base+native_odst::controllerProofRva;
    const auto& odst=kGestureBindings[native_odst::readerIndex];
    if(pattern==odst.readerPattern)return base+odst.readerRva;
    if(pattern==odst.statePattern)return base+odst.stateLoadRva;
    const auto& d=kGestureBindings[readerIndex];
    return base+(pattern==d.readerPattern?d.readerRva:d.stateLoadRva);
}
uintptr_t RipTarget(uintptr_t disp,uintptr_t){
    const auto base=reinterpret_cast<uintptr_t>(image.data());
    if(disp==base+kGestureBindings[native_h4::readerIndex].stateLoadRva+10)return base+0x120000;
    if(disp==base+native_h4::padProofRva+10)return base+0x130000;
    if(disp==base+native_h2::copierRva+14)return base+0x140000;
    if(disp==base+native_h2::elapsedRva+2)return base+0x150000;
    if(disp==base+native_ce::getterRva+6)return base+0x2EA2890;
    if(disp==base+kGestureBindings[native_h3::readerIndex].stateLoadRva+10)return base+0x100000;
    if(disp==base+kGestureBindings[native_odst::readerIndex].stateLoadRva+10)return base+0x110000;
    return base+(disp==base+padProofRva+10?0xE0000:0xF0000);
}
bool ModuleRange(const wchar_t*,uintptr_t& base,size_t& size){base=reinterpret_cast<uintptr_t>(image.data());size=image.size();return true;}
}
static void __fastcall OriginalConvert(uintptr_t first,int delta,const void* input,void* out,void* axes,void* activity){
    ++converterCalls;
    Check(first==0x12345678 && delta==16 && (input==raw||input==wrongRaw) &&
        (out==image.data()+0xE0000||out==image.data()+0xE003C) && axes==nullptr && activity==nullptr,
        "converter trampoline receives every native argument unchanged");
}
static void* __fastcall OriginalRead(void*,unsigned action){++readerCalls;physical.action=action;return &physical;}
static void __fastcall OriginalH3Convert(uintptr_t controller,int delta,const void* input,void* output,void* axes,void* activity){
    Check(controller<4&&delta==16&&(input==raw||input==wrongRaw)&&output==wrongRaw&&axes==nullptr&&activity==nullptr,
        "H3 real controller index and six converter arguments reach native original");
}
static void* __fastcall OriginalH3Read(void*,unsigned action){h3Physical.action=static_cast<uint8_t>(action);return &h3Physical;}
static void __fastcall OriginalOdstConvert(uintptr_t controller,int delta,const void* input,void* output,void* axes,void* activity){
    Check(controller<4&&delta==16&&(input==raw||input==wrongRaw)&&output==wrongRaw&&axes==nullptr&&activity==nullptr,
        "ODST real controller index and six converter arguments reach native original");
}
static void* __fastcall OriginalOdstRead(void*,unsigned action){odstPhysical.action=static_cast<uint8_t>(action);return &odstPhysical;}
static void __fastcall OriginalH4Convert(uintptr_t unused,int delta,const void* input,void* output,void* axes,void* activity){
    Check(unused==0x87654321&&delta==16&&(input==raw||input==wrongRaw)&&
        (output==image.data()+0x130000||output==image.data()+0x13003C)&&axes==nullptr&&activity==nullptr,
        "H4 unused first argument and six converter arguments reach native original unchanged");
}
static void* __fastcall OriginalH4Read(void*,unsigned action){h4Physical.action=action;return &h4Physical;}
static unsigned char h2Output[0xC0]{};
static bool h2ThrowOriginal=false;
static void __fastcall OriginalH2Convert(unsigned controller,void* prefs,void* input,void* axes,void* look,void* output){
    Check(controller<4&&prefs==wrongRaw&&input==raw&&axes==nullptr&&look==nullptr&&
        (output==image.data()+0x140000||output==image.data()+0x1400C0),"H2 abstraction ABI reaches original unchanged");
}
static void __fastcall OriginalH2Copy(unsigned controller,void* output){
    if(h2ThrowOriginal)RaiseException(0xE0001234,0,0,nullptr);
    if(output!=reinterpret_cast<void*>(1))std::memcpy(output,image.data()+0x140000+controller*0xC0,0xC0);
}
static unsigned ceCalls{},ceMode{};
static unsigned char observedCe[0x60]{};
static unsigned char* CeState(){return image.data()+0x2EA2890;}
static void __fastcall OriginalCeConvert(unsigned controller,int delta,const void* input,void* output){
    Check(controller<4&&delta==16&&input==raw&&output==wrongRaw,"CE converter keeps all four native arguments");
}
static void __fastcall OriginalCeConsumer(int user,unsigned controller,float delta,void* output){
    ++ceCalls;Check(user>=0&&user<4&&controller<4&&delta==.016f&&output==wrongRaw,"CE consumer preserves native ABI");
    std::memcpy(observedCe,CeState()+controller*0x60,0x60);
    CeState()[0x5C]=1;
    if(ceMode==1){ceMode=0;RaiseException(0xE0001234,0,0,nullptr);}
    if(ceMode==2){
        ceMode=0;
        native_ce::ConsumerHook(1,0,.016f,wrongRaw);
        Check(observedCe[8]==0,"nested nonlocal native consumer cannot see outer VR fire overlay");
        Check(CeState()[8]!=0,"outer VR overlay resumes after nested stock consumer");
    }
}
MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID,LPVOID* original){
    ++creates;if(creates==failCreate)return MH_ERROR_UNSUPPORTED_FUNCTION;
    if(target==image.data()+native_h3::converterRva){*original=reinterpret_cast<void*>(&OriginalH3Convert);return MH_OK;}
    if(target==image.data()+kGestureBindings[native_h3::readerIndex].readerRva){*original=reinterpret_cast<void*>(&OriginalH3Read);return MH_OK;}
    if(target==image.data()+native_odst::converterRva){*original=reinterpret_cast<void*>(&OriginalOdstConvert);return MH_OK;}
    if(target==image.data()+kGestureBindings[native_odst::readerIndex].readerRva){*original=reinterpret_cast<void*>(&OriginalOdstRead);return MH_OK;}
    if(target==image.data()+native_h4::converterRva){*original=reinterpret_cast<void*>(&OriginalH4Convert);return MH_OK;}
    if(target==image.data()+kGestureBindings[native_h4::readerIndex].readerRva){*original=reinterpret_cast<void*>(&OriginalH4Read);return MH_OK;}
    if(target==image.data()+native_h2::converterRva){*original=reinterpret_cast<void*>(&OriginalH2Convert);return MH_OK;}
    if(target==image.data()+native_h2::copierRva){*original=reinterpret_cast<void*>(&OriginalH2Copy);return MH_OK;}
    if(target==image.data()+native_ce::converterRva){*original=reinterpret_cast<void*>(&OriginalCeConvert);return MH_OK;}
    if(target==image.data()+native_ce::consumerRva){*original=reinterpret_cast<void*>(&OriginalCeConsumer);return MH_OK;}
    *original=target==image.data()+converterRva?reinterpret_cast<void*>(&OriginalConvert):reinterpret_cast<void*>(&OriginalRead);
    return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID target){
    const unsigned role=(target==image.data()+converterRva||target==image.data()+native_ce::converterRva||target==image.data()+native_h3::converterRva||target==image.data()+native_odst::converterRva||target==image.data()+native_h4::converterRva||target==image.data()+native_h2::converterRva)?1:2;
    return role==failEnable?MH_ERROR_MEMORY_PROTECT:MH_OK;
}
MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID){++disables;return MH_OK;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){++removes;return MH_OK;}
bool WaitForNativeDetourQuiescence(const void* const*,const void* const*,size_t,const std::atomic<uint32_t>& callbacks){
    Check(callbacks==0,"retirement requires zero active callbacks");return quiescent;
}
static void* State(){return image.data()+0xF0000;}
static void ConvertPoll(){ConverterHook(0x12345678,16,raw,image.data()+0xE0000,nullptr,nullptr);}
static void Poll(uint32_t actions,bool direct){
    ++clockNow;NativeVrActions_BeginPoll();NativeVrActions_Publish(raw,testTitle,clockNow,actions,direct);
    NativeVrActions_EndPoll();ConvertPoll();
}
static native_action_button::LaterRecord* ReadAction(vr_mapping::Action action){
    return static_cast<native_action_button::LaterRecord*>(ReaderHook(State(),vr_mapping::NativeAction(testTitle,action)));
}
static void Install(){
    *reinterpret_cast<unsigned*>(image.data()+0xF0000+0x6FC)=0;
    NativeVrActions_Poll();Check(runtime.admitted,"production worker admits unique fixture proofs and both hooks");
}
static void CePoll(uint32_t actions,bool direct){
    ++clockNow;NativeVrActions_BeginPoll();NativeVrActions_Publish(raw,testTitle,clockNow,actions,direct);
    NativeVrActions_EndPoll();native_ce::ConverterHook(0,16,raw,wrongRaw);
}
static void CeConsume(unsigned controller=0){native_ce::ConsumerHook(0,controller,.016f,wrongRaw);}
static bool CatchCeException(){
    __try {CeConsume();}
    __except(GetExceptionCode()==0xE0001234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
static void TestCe(){
    testTitle=GameTitle::HaloCE;++testGeneration;creates=0;failCreate=failEnable=0;
    NativeVrActions_Poll();mockCallerRva=native_ce::callerReturn;
    Check(native_ce::runtime.admitted,"CE separately admits its own converter/consumer and layout proofs");
    CePoll(1u<<vr_mapping::Fire,false);CeConsume();
    Check(!observedCe[8]&&NativeVrActions_CanRoute(testTitle,clockNow),"CE warmup keeps legacy state and observes owned cadence");
    CeState()[0x4C]=0xAB;CeState()[0x5D]=0xCD;
    CePoll(1u<<vr_mapping::Fire,true);CeConsume();
    Check(observedCe[8]==1&&*reinterpret_cast<uint16_t*>(observedCe+0x28)==16,"CE direct fire has native converter frame/ms counters");
    Check(!CeState()[8]&&!*reinterpret_cast<uint16_t*>(CeState()+0x28),"CE scoped counters restore before next native abstraction update");
    Check(CeState()[0x5C]==1&&CeState()[0x4C]==0xAB&&CeState()[0x5D]==0xCD,"CE native side effect and movement/device state survive overlay");
    CeConsume();Check(observedCe[8]==1,"repeated CE consumers cannot advance digital cadence");
    CeState()[8]=9;*reinterpret_cast<uint16_t*>(CeState()+0x28)=90;CeConsume();
    Check(observedCe[8]==9&&CeState()[8]==9&&*reinterpret_cast<uint16_t*>(CeState()+0x28)==90,"CE physical held action wins over virtual state");
    CeState()[8]=0;*reinterpret_cast<uint16_t*>(CeState()+0x28)=0;
    CePoll(1u<<vr_mapping::Reload,true);CeConsume();
    Check(observedCe[14]==1&&!observedCe[3]&&!observedCe[8],"CE Reload and Use stay separate regardless of transport aliases");
    CePoll(1u<<vr_mapping::Interact,true);CeConsume();
    Check(observedCe[3]==1&&!observedCe[14],"CE independently routes Use without Reload");
    CePoll(0,true);CeConsume();Check(!observedCe[3],"CE Unbound/release removes virtual held action");
    CePoll(1u<<vr_mapping::Fire,true);CeConsume(1);Check(!observedCe[8],"CE other controller receives untouched native state");
    mockCallerRva=0;CeConsume();Check(!observedCe[8],"CE non-player caller receives stock state");mockCallerRva=native_ce::callerReturn;
    ceMode=2;CeConsume();Check(!CeState()[8]&&!native_ce::consumerDepth,"CE reentrant overlay is fully restored");
    ceMode=1;Check(CatchCeException(),"CE original native exception propagates through bridge");
    Check(!CeState()[8]&&native_ce::runtime.callbacks==0&&!native_ce::consumerDepth&&!native_ce::activeOverlay,
        "CE exception restores borrowed fields and all lifecycle counters");
    NativeVrActions_BeginPoll();NativeVrActions_EndPoll();CeConsume();Check(!observedCe[8],"CE menu cancellation immediately removes borrowed input");
    NativeVrActions_Publish(raw,testTitle,clockNow,1u<<vr_mapping::Fire,true);CeConsume();
    Check(!observedCe[8],"CE replacement publication cannot revive canceled converter sample");
    CePoll(1u<<vr_mapping::Fire,true);publication.title=GameTitle::HaloReach;
    Check(!NativeVrActions_CanRoute(testTitle,clockNow),"same generation in another title cannot admit CE publication");
    CeConsume();Check(!observedCe[8],"another title publication cannot keep a previous CE overlay alive");
    CePoll(1u<<vr_mapping::Fire,true);clockNow+=151;CeConsume();Check(!observedCe[8],"CE expired publication returns native state");
    CePoll(1u<<vr_mapping::Fire,true);native_ce::runtime.state=1;CeConsume();
    Check(native_ce::runtime.faulted,"guarded CE state access fault marks optional bridge faulted");
    Check(!observedCe[8],"guarded CE state access fault invokes original with stock data");
    quiescent=false;NativeVrActions_Poll();
    Check(native_ce::runtime.module&&!native_ce::runtime.admitted,"CE failed quiescence retains trampolines only");
    quiescent=true;NativeVrActions_Poll();Check(!native_ce::runtime.module,"CE optional fault cleans up independently");
    ++testGeneration;creates=0;failCreate=2;NativeVrActions_Poll();
    Check(!native_ce::runtime.module&&!native_ce::runtime.admitted,"CE partial install failure leaves transport fallback");
    failCreate=0;testTitle=GameTitle::None;
}
static void H3Poll(uint32_t actions,bool direct){
    ++clockNow;NativeVrActions_BeginPoll();NativeVrActions_Publish(raw,testTitle,clockNow,actions,direct);
    NativeVrActions_EndPoll();native_h3::ConverterHook(0,16,raw,wrongRaw,nullptr,nullptr);
}
static native_action_button::Gen3Record* H3Read(vr_mapping::Action action){
    return static_cast<native_action_button::Gen3Record*>(native_h3::ReaderHook(image.data()+0x100000,
        native_action_button::ConsumerAction(GameTitle::Halo3,action)));
}
static void TestH3(){
    testTitle=GameTitle::Halo3;++testGeneration;creates=0;failCreate=failEnable=0;
    *reinterpret_cast<unsigned*>(image.data()+0x100000+0x514)=0;
    NativeVrActions_Poll();mockCallerRva=native_h3::consumerBegin+0x10;
    Check(native_h3::runtime.admitted,"H3 admits independent converter/controller/consumer proofs");
    H3Poll(1u<<vr_mapping::Reload,false);
    Check(H3Read(vr_mapping::Reload)==&h3Physical&&NativeVrActions_CanRoute(testTitle,clockNow),"H3 warmup retains native state before direct routing");
    H3Poll(1u<<vr_mapping::Reload,true);
    auto* reload=H3Read(vr_mapping::Reload);
    Check(reload!=&h3Physical&&reload->action==0x26&&reload->owner==0xff&&reload->state.frames==1&&reload->state.milliseconds==16,
        "H3 dedicated reload uses real 12-byte record and native cadence");
    Check(H3Read(vr_mapping::Interact)==&h3Physical,"H3 Reload does not emit generic Use 3");
    reload->state.flags|=1;reload->owner=0x26;
    H3Poll(1u<<vr_mapping::Reload,true);reload=H3Read(vr_mapping::Reload);
    Check(reload->state.flags==1&&reload->owner==0x26&&reload->state.frames==2,"H3 native consumption and owner survive held converter updates");
    H3Poll(0,true);Check(H3Read(vr_mapping::Reload)==&h3Physical,"H3 Unbound/release removes virtual reload");
    H3Poll(1u<<vr_mapping::Reload,true);reload=H3Read(vr_mapping::Reload);
    Check(reload->state.flags==0&&reload->state.frames==1,"H3 release clears consumed flag before a new press");
    H3Poll(1u<<vr_mapping::Interact,true);
    Check(H3Read(vr_mapping::Interact)!=&h3Physical&&H3Read(vr_mapping::Reload)==&h3Physical,"H3 Use and Reload remain separately bindable");
    h3Physical.state.frames=7;h3Physical.state.amount=.5f;
    Check(H3Read(vr_mapping::Interact)==&h3Physical&&h3Physical.state.amount==.5f,"H3 physical input retains native analog and consumption record");
    h3Physical.state={};mockCallerRva=0;
    Check(H3Read(vr_mapping::Interact)==&h3Physical,"H3 abstraction updater never receives writable virtual record");
    mockCallerRva=native_h3::consumerBegin+0x10;
    Check(native_h3::ReaderHook(image.data()+0x100518,3)==&h3Physical,"H3 other controller record is untouched");
    NativeVrActions_BeginPoll();NativeVrActions_EndPoll();
    Check(H3Read(vr_mapping::Interact)==&h3Physical,"H3 menu cancellation ends virtual hold immediately");
    NativeVrActions_Publish(raw,testTitle,clockNow,1u<<vr_mapping::Interact,true);
    Check(H3Read(vr_mapping::Interact)==&h3Physical,"H3 new publication cannot revive a canceled converter");
    native_h3::ConverterHook(1,16,wrongRaw,wrongRaw,nullptr,nullptr);
    Check(H3Read(vr_mapping::Interact)==&h3Physical,"H3 other-controller converter cannot complete owned cadence");
    native_h3::ConverterHook(0,16,wrongRaw,wrongRaw,nullptr,nullptr);
    Check(H3Read(vr_mapping::Interact)!=&h3Physical,"H3 copied native input route uses proven controller zero");
    clockNow+=151;Check(H3Read(vr_mapping::Interact)==&h3Physical&&!NativeVrActions_CanRoute(testTitle,clockNow),"H3 expired publication falls back");
    H3Poll(1u<<vr_mapping::Fire,true);native_h3::runtime.state=1;
    Check(native_h3::ReaderHook(reinterpret_cast<void*>(1),8)==&h3Physical&&native_h3::runtime.faulted,"H3 guarded state fault returns native and isolates optional feature");
    quiescent=false;NativeVrActions_Poll();Check(native_h3::runtime.module&&!native_h3::runtime.admitted,"H3 pending callbacks retain disabled trampolines");
    quiescent=true;NativeVrActions_Poll();Check(!native_h3::runtime.module,"H3 fault retirement frees only after quiescence");
    ++testGeneration;creates=0;failCreate=2;NativeVrActions_Poll();
    Check(!native_h3::runtime.module&&!native_h3::runtime.admitted,"H3 partial hook failure leaves transport fallback");
    failCreate=0;testTitle=GameTitle::None;
}
static void OdstPoll(uint32_t actions,bool direct){
    ++clockNow;NativeVrActions_BeginPoll();NativeVrActions_Publish(raw,testTitle,clockNow,actions,direct);
    NativeVrActions_EndPoll();native_odst::ConverterHook(0,16,raw,wrongRaw,nullptr,nullptr);
}
static native_action_button::Gen3Record* OdstRead(vr_mapping::Action action){
    return static_cast<native_action_button::Gen3Record*>(native_odst::ReaderHook(image.data()+0x110000,
        native_action_button::ConsumerAction(GameTitle::Halo3ODST,action)));
}
static void TestOdst(){
    testTitle=GameTitle::Halo3ODST;++testGeneration;creates=0;failCreate=failEnable=0;
    *reinterpret_cast<unsigned*>(image.data()+0x110000+0x554)=0;
    NativeVrActions_Poll();mockCallerRva=native_odst::consumerBegin+0x10;
    Check(native_odst::runtime.admitted,"ODST admits independent converter/controller/consumer proofs");
    OdstPoll(1u<<vr_mapping::Reload,false);
    Check(OdstRead(vr_mapping::Reload)==&odstPhysical&&NativeVrActions_CanRoute(testTitle,clockNow),"ODST warmup retains native state before direct routing");
    OdstPoll(1u<<vr_mapping::Reload,true);
    auto* reload=OdstRead(vr_mapping::Reload);
    Check(reload!=&odstPhysical&&reload->action==0x2A&&reload->owner==0xff&&reload->state.frames==1&&reload->state.milliseconds==16,
        "ODST dedicated reload uses real 12-byte record and native cadence");
    Check(OdstRead(vr_mapping::Interact)==&odstPhysical,"ODST Reload does not emit generic Use 3");
    reload->state.flags|=1;reload->owner=0x2A;
    OdstPoll(1u<<vr_mapping::Reload,true);reload=OdstRead(vr_mapping::Reload);
    Check(reload->state.flags==1&&reload->owner==0x2A&&reload->state.frames==2,"ODST native consumption and owner survive held converter updates");
    OdstPoll(0,true);Check(OdstRead(vr_mapping::Reload)==&odstPhysical,"ODST Unbound/release removes virtual reload");
    OdstPoll(1u<<vr_mapping::Reload,true);reload=OdstRead(vr_mapping::Reload);
    Check(reload->state.flags==0&&reload->state.frames==1,"ODST release clears consumed flag before a new press");
    OdstPoll(1u<<vr_mapping::Interact,true);
    Check(OdstRead(vr_mapping::Interact)!=&odstPhysical&&OdstRead(vr_mapping::Reload)==&odstPhysical,"ODST Use and Reload remain separately bindable");
    odstPhysical.state.frames=7;odstPhysical.state.amount=.5f;
    Check(OdstRead(vr_mapping::Interact)==&odstPhysical&&odstPhysical.state.amount==.5f,"ODST physical input retains native analog and consumption record");
    odstPhysical.state={};mockCallerRva=0;
    Check(OdstRead(vr_mapping::Interact)==&odstPhysical,"ODST abstraction updater never receives writable virtual record");
    mockCallerRva=native_odst::consumerBegin+0x10;
    Check(native_odst::ReaderHook(image.data()+0x110558,3)==&odstPhysical,"ODST other controller record is untouched");
    NativeVrActions_BeginPoll();NativeVrActions_EndPoll();
    Check(OdstRead(vr_mapping::Interact)==&odstPhysical,"ODST menu cancellation ends virtual hold immediately");
    NativeVrActions_Publish(raw,testTitle,clockNow,1u<<vr_mapping::Interact,true);
    Check(OdstRead(vr_mapping::Interact)==&odstPhysical,"ODST new publication cannot revive a canceled converter");
    native_odst::ConverterHook(1,16,wrongRaw,wrongRaw,nullptr,nullptr);
    Check(OdstRead(vr_mapping::Interact)==&odstPhysical,"ODST other-controller converter cannot complete owned cadence");
    native_odst::ConverterHook(0,16,wrongRaw,wrongRaw,nullptr,nullptr);
    Check(OdstRead(vr_mapping::Interact)!=&odstPhysical,"ODST copied native input route uses proven controller zero");
    clockNow+=151;Check(OdstRead(vr_mapping::Interact)==&odstPhysical&&!NativeVrActions_CanRoute(testTitle,clockNow),"ODST expired publication falls back");
    OdstPoll(1u<<vr_mapping::Fire,true);native_odst::runtime.state=1;
    Check(native_odst::ReaderHook(reinterpret_cast<void*>(1),8)==&odstPhysical&&native_odst::runtime.faulted,"ODST guarded state fault returns native and isolates optional feature");
    quiescent=false;NativeVrActions_Poll();Check(native_odst::runtime.module&&!native_odst::runtime.admitted,"ODST pending callbacks retain disabled trampolines");
    quiescent=true;NativeVrActions_Poll();Check(!native_odst::runtime.module,"ODST fault retirement frees only after quiescence");
    ++testGeneration;creates=0;failCreate=2;NativeVrActions_Poll();
    Check(!native_odst::runtime.module&&!native_odst::runtime.admitted,"ODST partial hook failure leaves transport fallback");
    failCreate=0;testTitle=GameTitle::None;
}
static void H4Poll(uint32_t actions,bool direct){
    ++clockNow;NativeVrActions_BeginPoll();NativeVrActions_Publish(raw,testTitle,clockNow,actions,direct);
    NativeVrActions_EndPoll();native_h4::ConverterHook(0x87654321,16,raw,image.data()+0x130000,nullptr,nullptr);
}
static native_action_button::LaterRecord* H4Read(vr_mapping::Action action){
    return static_cast<native_action_button::LaterRecord*>(native_h4::ReaderHook(image.data()+0x120000,
        native_action_button::ConsumerAction(GameTitle::Halo4,action)));
}
static void TestH4(){
    testTitle=GameTitle::Halo4;++testGeneration;creates=0;failCreate=failEnable=0;
    *reinterpret_cast<unsigned*>(image.data()+0x120000+0x7E4)=0;
    NativeVrActions_Poll();mockCallerRva=native_h4::consumerBegin+0x10;
    Check(native_h4::runtime.admitted,"H4 admits independent converter/output-row/consumer proofs");
    H4Poll(1u<<vr_mapping::Reload,false);
    Check(H4Read(vr_mapping::Reload)==&h4Physical&&NativeVrActions_CanRoute(testTitle,clockNow),"H4 warmup retains native state before direct routing");
    H4Poll(1u<<vr_mapping::Reload,true);
    auto* reload=H4Read(vr_mapping::Reload);
    Check(reload!=&h4Physical&&reload->action==0x31&&reload->owner==0x7f&&reload->state.frames==1&&reload->state.milliseconds==16,
        "H4 dedicated reload uses real 16-byte record and native cadence");
    Check(H4Read(vr_mapping::Interact)==&h4Physical,"H4 Reload does not emit generic Use 2");
    reload->state.flags|=1;reload->owner=0x31;
    H4Poll(1u<<vr_mapping::Reload,true);reload=H4Read(vr_mapping::Reload);
    Check(reload->state.flags==1&&reload->owner==0x31&&reload->state.frames==2,"H4 native consumption and owner survive held converter updates");
    H4Poll(0,true);Check(H4Read(vr_mapping::Reload)==&h4Physical,"H4 Unbound/release removes virtual reload");
    H4Poll(1u<<vr_mapping::Reload,true);reload=H4Read(vr_mapping::Reload);
    Check(reload->state.flags==0&&reload->state.frames==1,"H4 release clears consumed flag before a new press");
    H4Poll(1u<<vr_mapping::Interact,true);
    Check(H4Read(vr_mapping::Interact)!=&h4Physical&&H4Read(vr_mapping::Reload)==&h4Physical,"H4 Use and Reload remain separately bindable");
    h4Physical.state.frames=7;h4Physical.state.amount=.5f;
    Check(H4Read(vr_mapping::Interact)==&h4Physical&&h4Physical.state.amount==.5f,"H4 physical input retains native analog and consumption record");
    h4Physical.state={};mockCallerRva=0;
    Check(H4Read(vr_mapping::Interact)==&h4Physical,"H4 abstraction updater never receives writable virtual record");
    mockCallerRva=native_h4::consumerBegin+0x10;
    Check(native_h4::ReaderHook(image.data()+0x1207E8,2)==&h4Physical,"H4 other controller record is untouched");
    NativeVrActions_BeginPoll();NativeVrActions_EndPoll();
    Check(H4Read(vr_mapping::Interact)==&h4Physical,"H4 menu cancellation ends virtual hold immediately");
    NativeVrActions_Publish(raw,testTitle,clockNow,1u<<vr_mapping::Interact,true);
    Check(H4Read(vr_mapping::Interact)==&h4Physical,"H4 new publication cannot revive a canceled converter");
    native_h4::ConverterHook(0x87654321,16,wrongRaw,image.data()+0x13003C,nullptr,nullptr);
    Check(H4Read(vr_mapping::Interact)==&h4Physical,"H4 other-controller converter cannot complete owned cadence");
    native_h4::ConverterHook(0x87654321,16,wrongRaw,image.data()+0x130000,nullptr,nullptr);
    Check(H4Read(vr_mapping::Interact)!=&h4Physical,"H4 copied native input route uses proven output row zero");
    clockNow+=151;Check(H4Read(vr_mapping::Interact)==&h4Physical&&!NativeVrActions_CanRoute(testTitle,clockNow),"H4 expired publication falls back");
    H4Poll(1u<<vr_mapping::Fire,true);native_h4::runtime.state=1;
    Check(native_h4::ReaderHook(reinterpret_cast<void*>(1),8)==&h4Physical&&native_h4::runtime.faulted,"H4 guarded state fault returns native and isolates optional feature");
    quiescent=false;NativeVrActions_Poll();Check(native_h4::runtime.module&&!native_h4::runtime.admitted,"H4 pending callbacks retain disabled trampolines");
    quiescent=true;NativeVrActions_Poll();Check(!native_h4::runtime.module,"H4 fault retirement frees only after quiescence");
    ++testGeneration;creates=0;failCreate=2;NativeVrActions_Poll();
    Check(!native_h4::runtime.module&&!native_h4::runtime.admitted,"H4 partial hook failure leaves transport fallback");
    failCreate=0;testTitle=GameTitle::None;
}
static void H2Poll(uint32_t actions,bool direct){
    ++clockNow;NativeVrActions_BeginPoll();NativeVrActions_Publish(raw,testTitle,clockNow,actions,direct);
    NativeVrActions_EndPoll();native_h2::ConverterHook(0,wrongRaw,raw,nullptr,nullptr,image.data()+0x140000);
}
static void H2Copy(){native_h2::CopierHook(0,h2Output);}
static bool CatchH2Exception(){
    __try {H2Copy();return false;} __except(GetExceptionCode()==0xE0001234?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
static void TestH2(){
    testTitle=GameTitle::Halo2;++testGeneration;creates=0;failCreate=failEnable=0;
    *reinterpret_cast<unsigned*>(image.data()+0x150000)=16;
    NativeVrActions_Poll();mockCallerRva=native_h2::callerReturn;
    Check(native_h2::runtime.admitted,"H2 owns unique abstraction/copy/cadence proofs");
    H2Poll(1u<<vr_mapping::Reload,false);H2Copy();
    Check(!h2Output[0x2E]&&NativeVrActions_CanRoute(testTitle,clockNow),"H2 observed owned cadence warms up without injecting");
    H2Poll(1u<<vr_mapping::Reload,true);H2Copy();
    Check(h2Output[0x2E]==1&&*reinterpret_cast<uint16_t*>(h2Output+0x8C)==16&&!h2Output[0x23],"H2 native private copy separates Reload2E from Use23");
    Check(!image[0x140000+0x2E],"H2 global abstraction remains untouched");
    H2Copy();Check(h2Output[0x2E]==1,"H2 repeated native copies cannot advance held frames");
    H2Poll(1u<<vr_mapping::Reload,true);H2Copy();Check(h2Output[0x2E]==2,"H2 held count follows abstraction cadence");
    H2Poll(0,true);H2Copy();Check(!h2Output[0x2E],"H2 Unbound/release clears only virtual input");
    H2Poll(1u<<vr_mapping::Interact,true);H2Copy();Check(h2Output[0x23]==1&&!h2Output[0x2E],"H2 independently rebound Use does not reload");
    image[0x140000+0x23]=8;*reinterpret_cast<uint16_t*>(image.data()+0x140000+0x76)=128;
    H2Copy();Check(h2Output[0x23]==8&&*reinterpret_cast<uint16_t*>(h2Output+0x76)==128,"H2 physical hold preserves its frames and milliseconds");
    std::memset(image.data()+0x140000,0,0xC0);
    *reinterpret_cast<float*>(image.data()+0x140000+0x98)=.7f;
    H2Poll((1u<<vr_mapping::Fire)|(1u<<vr_mapping::Grenade),true);H2Copy();
    Check(h2Output[10]==1&&h2Output[0x16]==1&&h2Output[0x18]==1&&*reinterpret_cast<float*>(h2Output+0x90)==1,
        "H2 primary fire honors its native normal/dual/vehicle context families and analog trigger");
    Check(h2Output[7]==1&&h2Output[0x17]==1&&h2Output[0x19]==1&&*reinterpret_cast<float*>(h2Output+0x94)==1,
        "H2 support trigger supplies deliberate grenade/secondary context family");
    Check(*reinterpret_cast<float*>(h2Output+0x98)==.7f&&*reinterpret_cast<float*>(image.data()+0x140000+0x90)==0,
        "H2 movement and global native trigger stay unchanged");
    *reinterpret_cast<float*>(image.data()+0x140000+0x90)=.4f;H2Copy();
    Check(*reinterpret_cast<float*>(h2Output+0x90)==.4f,"H2 physical analog trigger wins over virtual trigger");
    mockCallerRva=0;H2Copy();Check(!h2Output[10],"H2 non-player native copies cannot consume virtual actions");
    mockCallerRva=native_h2::callerReturn;
    native_h2::CopierHook(1,h2Output);Check(!h2Output[10],"H2 controller one stays native");
    NativeVrActions_BeginPoll();NativeVrActions_EndPoll();H2Copy();Check(!h2Output[10],"H2 cancellation expires private snapshot immediately");
    NativeVrActions_Publish(raw,testTitle,clockNow,1u<<vr_mapping::Fire,true);H2Copy();Check(!h2Output[10],"H2 replacement publication cannot revive canceled cadence");
    native_h2::ConverterHook(1,wrongRaw,raw,nullptr,nullptr,image.data()+0x1400C0);H2Copy();Check(!h2Output[10],"H2 another controller cannot complete cadence");
    H2Poll(1u<<vr_mapping::Fire,true);H2Copy();Check(h2Output[10],"H2 owned abstraction recovers after menu gap");
    clockNow+=151;H2Copy();Check(!h2Output[10]&&!NativeVrActions_CanRoute(testTitle,clockNow),"H2 expired publication cannot linger");
    H2Poll(1u<<vr_mapping::Fire,true);h2ThrowOriginal=true;
    Check(CatchH2Exception()&&native_h2::runtime.callbacks==0,"H2 native copy exception propagates with balanced lifecycle guard");h2ThrowOriginal=false;
    native_h2::CopierHook(0,reinterpret_cast<void*>(1));Check(native_h2::runtime.faulted,"H2 invalid copy destination faults only optional bridge");
    quiescent=false;NativeVrActions_Poll();Check(native_h2::runtime.module&&!native_h2::runtime.admitted,"H2 pending callback retains disabled hooks");
    quiescent=true;NativeVrActions_Poll();Check(!native_h2::runtime.module,"H2 quiescent retirement releases hooks");
    ++testGeneration;creates=0;failCreate=2;NativeVrActions_Poll();Check(!native_h2::runtime.module,"H2 partial hook install rolls back");
    ++testGeneration;creates=0;failCreate=0;NativeVrActions_Poll();native_h2::runtime.elapsed=1;
    H2Poll(1u<<vr_mapping::Fire,true);Check(native_h2::runtime.faulted,"H2 elapsed input clock access is guarded");
    NativeVrActions_Poll();testTitle=GameTitle::None;
}
int main(){
    g_config.vr_action_mapping=true;NativeVrActions_Poll();
    Check(creates==0&&!runtime.module,"transport reader must be verified before optional reader prologue is patched");
    fallbackVerified=true;Install();
    Check(!NativeVrActions_CanRoute(testTitle,clockNow),"no direct route before observed native cadence");
    Poll(1u<<vr_mapping::Fire,false);
    Check(!NativeVrActions_CanRoute(testTitle,clockNow),"converter observation alone cannot admit direct routing");
    Check(ReadAction(vr_mapping::Fire)==&physical,"first legacy poll stays on original button records");
    Check(NativeVrActions_CanRoute(testTitle,clockNow),"same-thread owned reader completes cadence handshake");
    bool producerReady=false;
    std::thread producer([&]{
        producerReady=NativeVrActions_CanRoute(testTitle,clockNow);
        NativeVrActions_BeginPoll();NativeVrActions_Publish(wrongRaw,testTitle,clockNow,1u<<vr_mapping::Grenade,true);
        NativeVrActions_EndPoll();
    });producer.join();
    ConvertPoll();
    Check(producerReady&&ReadAction(vr_mapping::Grenade)!=&physical,
        "MCC polling thread publishes coherently to a different native converter/reader thread");

    vr_mapping::Mapper mapper;vr_mapping::Overrides overrides{};vr_mapping::Transports transports{};
    transports.fill(0x4000); // every native action intentionally aliases X
    mapper.ApplyDetailed(0,overrides,transports,7,true);
    auto legacy=mapper.ApplyDetailed(vr_mapping::Bit(vr_mapping::PrimaryTrigger),overrides,transports,7,true);
    Check(legacy.Has(vr_mapping::Fire)&&legacy.transports==0x4000,"legacy source before route transition");
    for(unsigned i=0;i<vr_mapping::Count;++i)transports[i]=NativeVrActions_GestureBit(static_cast<vr_mapping::Action>(i));
    auto direct=mapper.ApplyDetailed(vr_mapping::Bit(vr_mapping::PrimaryTrigger),overrides,transports,7,true);
    Check(!direct.actions,"legacy-to-direct transition drains held trigger before new native press");
    Poll(direct.actions,true);Check(ReadAction(vr_mapping::Fire)==&physical,"transition cannot repeat native press edge");
    mapper.ApplyDetailed(0,overrides,transports,7,true);
    direct=mapper.ApplyDetailed(vr_mapping::Bit(vr_mapping::PrimaryTrigger),overrides,transports,7,true);
    Poll(direct.actions,true);
    auto* fire=ReadAction(vr_mapping::Fire);
    Check(fire!=&physical&&fire->state.frames==1&&fire->state.milliseconds==16&&fire->state.amount==1,
        "actual reader exposes first virtual fire at native converter cadence");
    Check(!(fire->state.flags&1)&&(fire->owner==0x7F||fire->owner==fire->action),
        "private record satisfies Reach kit's actual native ownership/consumption predicate");
    Check(ReadAction(vr_mapping::Grenade)==&physical&&ReadAction(vr_mapping::Reload)==&physical,
        "unpressed actions do not receive fire despite identical native transport aliases");
    Check(ReadAction(vr_mapping::Fire)==fire&&fire->state.frames==1,"repeated reader calls cannot advance held frames");
    mockCallerRva=consumerEnd;
    Check(ReadAction(vr_mapping::Fire)==&physical,
        "native abstraction updater and other non-player consumers never receive writable private VR records");
    physical.state={};mockCallerRva=consumerBegin+0x10;
    Check(ReadAction(vr_mapping::Fire)==fire&&fire->state.frames==1,
        "native keyboard-counter writes cannot erase the virtual held record");
    fire->owner=fire->action;fire->state.flags|=1;
    Poll(direct.actions,true);fire=ReadAction(vr_mapping::Fire);
    Check(fire->state.frames==2&&fire->state.milliseconds==32&&(fire->state.flags&1)&&fire->owner==fire->action,
        "consumption and ownership persist while held, as native abstraction does");
    Poll(0,true);Check(ReadAction(vr_mapping::Fire)==&physical,"release returns physical native record");
    Poll(direct.actions,true);fire=ReadAction(vr_mapping::Fire);
    Check(fire->state.frames==1&&!(fire->state.flags&1),"new press after release clears consumed flag");
    physical.state.frames=3;physical.state.milliseconds=48;physical.state.amount=.7f;physical.state.flags=1;
    Check(ReadAction(vr_mapping::Fire)==&physical&&physical.state.amount==.7f&&physical.state.flags==1,
        "physical gamepad or keyboard retains native analog amount and consumption when VR also holds");
    physical.state={};
    overrides[vr_mapping::Fire]=vr_mapping::Unbound;
    direct=mapper.ApplyDetailed(vr_mapping::Bit(vr_mapping::PrimaryTrigger),overrides,transports,7,true);
    Poll(direct.actions,true);Check(ReadAction(vr_mapping::Fire)==&physical,"Unbound releases virtual native action");
    overrides[vr_mapping::Reload]=vr_mapping::A;overrides[vr_mapping::Interact]=vr_mapping::B;
    mapper.ApplyDetailed(0,overrides,transports,7,true);
    direct=mapper.ApplyDetailed(vr_mapping::Bit(vr_mapping::A),overrides,transports,7,true);
    Poll(direct.actions,true);
    Check(ReadAction(vr_mapping::Reload)!=&physical&&ReadAction(vr_mapping::Interact)==&physical,
        "Reach reload and generic use remain distinct at production native reader");
    clockNow+=149;ConvertPoll();
    Check(ReadAction(vr_mapping::Reload)!=&physical,"publication remains valid immediately before its expiry");
    clockNow+=2;
    Check(ReadAction(vr_mapping::Reload)==&physical&&!NativeVrActions_CanRoute(testTitle,clockNow),
        "recent converter cannot extend an expired publication's lifetime");
    Poll(0,false);ReadAction(vr_mapping::Reload);Poll(direct.actions,true);
    NativeVrActions_BeginPoll();
    NativeVrActions_EndPoll();
    Check(ReadAction(vr_mapping::Reload)==&physical,"menu/cancel poll invalidates outstanding virtual input immediately");
    NativeVrActions_BeginPoll();
    NativeVrActions_Publish(raw,testTitle,clockNow,1u<<vr_mapping::Fire,true);
    NativeVrActions_EndPoll();
    Check(ReadAction(vr_mapping::Reload)==&physical,
        "a new publication cannot revive the previous converter sample after cancellation");
    clockNow+=151;Check(!NativeVrActions_CanRoute(testTitle,clockNow),"long native poll gap requires fresh cadence admission");
    NativeVrActions_Publish(raw,testTitle,clockNow,1u<<vr_mapping::Fire,true);
    NativeVrActions_EndPoll();
    ConverterHook(0x12345678,16,wrongRaw,image.data()+0xE003C,nullptr,nullptr);
    Check(ReadAction(vr_mapping::Fire)==&physical,"another controller's converter output cannot advance action bridge");
    ConverterHook(0x12345678,16,wrongRaw,image.data()+0xE0000,nullptr,nullptr);
    Check(ReadAction(vr_mapping::Fire)!=&physical,"MCC copied-input buffer admits only through proven output row zero");
    Poll(1u<<vr_mapping::Fire,true);
    Check(ReaderHook(image.data()+0xF0700,7)==&physical,"another local controller state stays native");
    *reinterpret_cast<unsigned*>(image.data()+0xF0000+0x6FC)=1;
    Check(ReadAction(vr_mapping::Fire)==&physical,"owned-state controller field is verified live");
    *reinterpret_cast<unsigned*>(image.data()+0xF0000+0x6FC)=0;
    switchGenerationOnTitleRead=true;
    NativeVrActions_BeginPoll();NativeVrActions_Publish(raw,testTitle,clockNow,1u<<vr_mapping::Fire,true);
    NativeVrActions_EndPoll();
    Check(publication.generation!=testGeneration,
        "retire/reinstall between admission and commit cannot relabel old actions with new generation");
    ++testGeneration;Check(!NativeVrActions_CanRoute(testTitle,clockNow),"title generation immediately expires bridge");
    testTitle=GameTitle::None;NativeVrActions_Poll();
    Check(!runtime.module&&disables==2&&removes==2&&frees==1,"generation retirement removes both hooks after quiescence");
    testTitle=GameTitle::HaloReach;creates=0;failCreate=2;NativeVrActions_Poll();
    Check(!runtime.module&&!runtime.admitted&&removes==3&&frees==2,
        "second-hook failure cleans first hook and releases module without core teardown");
    const auto oldCreates=creates;NativeVrActions_Poll();Check(creates==oldCreates,"failed generation is not retried every worker tick");
    ++testGeneration;creates=0;failCreate=0;failEnable=2;NativeVrActions_Poll();
    Check(!runtime.module&&removes==5&&frees==3,"enable failure also removes created inactive trampoline");
    ++testGeneration;creates=0;failEnable=0;Install();Poll(1u<<vr_mapping::Fire,true);
    runtime.state=1;
    Check(ReaderHook(reinterpret_cast<void*>(1),7)==&physical&&runtime.faulted,
        "Reach guarded state access fault returns native record and marks optional bridge faulted");
    quiescent=false;NativeVrActions_Poll();
    Check(runtime.module&&!runtime.admitted&&removes==5,"failed quiescence retains trampolines with admission closed");
    quiescent=true;NativeVrActions_Poll();
    Check(!runtime.module&&removes==7&&frees==4,"later quiescence retires faulted optional bridge safely");
    Check(runtime.callbacks==0&&converterCalls>0&&readerCalls>0,"all actual hook exits balance callbacks");
    TestCe();
    TestH3();
    TestOdst();
    TestH4();
    TestH2();
    std::printf("native VR action runtime: %u checks passed\n",checks);
}
