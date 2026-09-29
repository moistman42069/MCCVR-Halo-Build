#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include "../src/common/runtime_types.h"
#include "../src/common/halo4_vehicle_input_logic.h"

static GameTitle title=GameTitle::Halo4;
static uint32_t generation=7,tlsIndex=0,*g_halo4EngineTlsIndex=&tlsIndex;
static struct {std::atomic<bool> armed{true},teardownRequested{false};std::atomic<uint32_t> generation{7};} g_halo4Camera;
static bool cinematic=false;
GameTitle TitleAdapter_GetActiveTitle(){return title;}
uint32_t TitleAdapter_GetGeneration(GameTitle){return generation;}
CinematicControlState ReadHalo4CinematicControl(){return cinematic?CinematicControlState::Unknown:CinematicControlState::PlayerControlled;}
static const uint8_t* fakeSlots[1]{};
static uintptr_t FakeReadGs(unsigned long){return reinterpret_cast<uintptr_t>(fakeSlots);}
namespace sig {uintptr_t Find(uintptr_t,size_t,const char*){return 0;}}
static bool WaitForNativeDetourQuiescence(const void* const* functions,const void* const*,size_t count,const std::atomic<uint32_t>& active)
{
    if (count!=1) return false;
    DWORD64 image{};
    return RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(functions[0]),&image,nullptr)&&!active.load();
}
#define __readgsqword FakeReadGs
#define LOG(...) ((void)0)
#include "../src/dll/halo4_vehicle_input.inl"
#undef LOG
#undef __readgsqword

static unsigned checks{},calls{};
constexpr uint32_t player=0x12340000,unit=0x23450001,parent=0x34560002;
static uint32_t outputUnit=unit;
static bool fault=false,changeUnit=false;
static uint32_t __fastcall OutputUnit(uint32_t user)
{
    ++calls;
    if (fault) RaiseException(0xe0424242,0,0,nullptr);
    return user==0?outputUnit+(changeUnit&&calls%2==0?0x10000:0):UINT32_MAX;
}
template<class T> static void Put(uint8_t* at,size_t offset,T value){std::memcpy(at+offset,&value,sizeof(value));}
static void Check(bool value,const char* message)
{++checks;if(!value){std::fprintf(stderr,"H4 vehicle input: %s\n",message);std::exit(1);}}

int main()
{
    std::array<uint8_t,0x200> tls{},mapping{},biped{},vehicle{};
    std::array<uint8_t,0x80> table{},entries{};
    fakeSlots[0]=tls.data();
    Put(tls.data(),0x138,mapping.data());Put(tls.data(),0x18,table.data());
    Put(table.data(),0x20,uint64_t(0x18));table[0x31]=1;
    Put(table.data(),0x44,int32_t(3));Put(table.data(),0x50,entries.data());
    Put(entries.data(),0x18,uint16_t(unit>>16));entries[0x18+4]=0;
    Put(entries.data(),0x18+0x10,biped.data());
    Put(entries.data(),0x30,uint16_t(parent>>16));entries[0x30+4]=1;
    Put(entries.data(),0x30+0x10,vehicle.data());
    Put(mapping.data(),0xb8,player);Put(mapping.data(),0xc8,unit);
    Put(mapping.data(),8,player);Put(mapping.data(),0x18,unit); // input one owns output zero
    Put(biped.data(),0x24,UINT32_MAX);Put(biped.data(),0x2c,int16_t(-1));
    auto& feature=g_halo4VehicleInput;
    feature.generation=7;feature.outputUnit=&OutputUnit;feature.ready=true;
    Halo4VehicleInputState state{};
    Check(Halo4ReadVehicleInput(state)&&state.unit==unit&&!state.seated,"own on-foot unit admitted across input/output mapping");
    Put(biped.data(),0x24,parent);Put(biped.data(),0x2c,int16_t(2));
    Check(Halo4ReadVehicleInput(state)&&state.seated&&state.parent==parent&&state.seat==2,"own native parent and seat admitted");
    // T6c: the diagnostics-only variant performs the identical reads and
    // guards but names the exact rejection exit; the shipping wrapper above
    // passes nullptr and is otherwise unchanged.
    {
        const char* reason=nullptr;
        Halo4VehicleInputState ex{};
        Check(Halo4ReadVehicleInputEx(ex,&reason)&&!reason,
            "the diagnostics reader admits the same owned state with no reason");
        g_halo4Camera.armed=false;
        reason=nullptr;
        Check(!Halo4ReadVehicleInputEx(ex,&reason)&&reason&&
              std::strcmp(reason,"camera")==0,
            "a disabled camera core names itself as the rejection");
        g_halo4Camera.armed=true;
        title=GameTitle::Halo3;
        reason=nullptr;
        Check(!Halo4ReadVehicleInputEx(ex,&reason)&&reason&&
              std::strcmp(reason,"title")==0,
            "a foreign active title names itself as the rejection");
        title=GameTitle::Halo4;
    }
    const auto tlsSaved=tls,mappingSaved=mapping,bipedSaved=biped;
    const auto tableSaved=table,entriesSaved=entries;
    for (int reject=0;reject<22;++reject)
    {
        switch(reject)
        {
        case 0:feature.ready=false;break;
        case 1:title=GameTitle::Halo3;break;
        case 2:g_halo4Camera.armed=false;break;
        case 3:g_halo4Camera.teardownRequested=true;break;
        case 4:++generation;break;
        case 5:++g_halo4Camera.generation;break;
        case 6:cinematic=true;break;
        case 7:tlsIndex=1088;break;
        case 8:fakeSlots[0]=nullptr;break;
        case 9:Put(tls.data(),0x138,uintptr_t(1));break;
        case 10:Put(mapping.data(),0xc8,unit+0x10000);break;
        case 11:Put(mapping.data(),8,player+0x10000);break;
        case 12:Put(mapping.data(),4,player);Put(mapping.data(),0x14,unit);break;
        case 13:table[0x31]=0;break;
        case 14:Put(table.data(),0x44,int32_t(1));break;
        case 15:Put(table.data(),0x20,uint64_t(0x20));break;
        case 16:Put(entries.data(),0x18,uint16_t((unit>>16)+1));break;
        case 17:Put(entries.data(),0x30,uint16_t((parent>>16)+1));break;
        case 18:Put(biped.data(),0x2c,int16_t(-1));break;
        case 19:Put(biped.data(),0x24,unit);break;
        case 20:fault=true;break;
        case 21:changeUnit=true;calls=0;break;
        }
        Halo4VehicleInputState sentinel{};sentinel.unit=17;
        Check(!Halo4ReadVehicleInput(sentinel)&&sentinel.unit==17&&!feature.callbacks.load(),
            "foreign, stale, ambiguous, missing or faulting ownership leaves output and callback lifetime intact");
        tls=tlsSaved;mapping=mappingSaved;biped=bipedSaved;table=tableSaved;entries=entriesSaved;
        fakeSlots[0]=tls.data();tlsIndex=0;feature.ready=true;title=GameTitle::Halo4;
        generation=7;g_halo4Camera.generation=7;g_halo4Camera.armed=true;
        g_halo4Camera.teardownRequested=false;cinematic=fault=changeUnit=false;
    }
    Check(feature.faults.load()==2,"both native service and inaccessible memory faults are isolated");
    feature.callbacks=1;
    Check(!RemoveHalo4VehicleInput()&&!feature.ready.load()&&feature.generation==7,
        "retirement retains dependency while a reader remains active");
    feature.callbacks=0;
    Check(RemoveHalo4VehicleInput()&&!feature.generation&&!feature.outputUnit,
        "compiled reader has unwind metadata and drains before module dependencies clear");
    std::printf("H4 vehicle input: %u checks passed\n",checks);
    return 0;
}
