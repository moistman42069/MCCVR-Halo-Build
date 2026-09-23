#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../src/common/halo3_vehicle_logic.h"

enum class GameTitle { Halo3 };
static uint32_t generation=7;
uint32_t TitleAdapter_GetGeneration(GameTitle) {return generation;}
static struct {bool vehicle_first_person=true,vehicle_hide_body=true;} g_config;
static struct {unsigned serial=0;bool valid=false;} g_halo3NativeAnchorCalibration;
static void* tagBaseSlot{},*instanceSlot{};
static void** g_halo3TagDataBase=&tagBaseSlot,**g_halo3TagInstanceTable=&instanceSlot;
static unsigned char* liveDefinition{};
static unsigned resolves{},checks{};
static unsigned char* Halo3LoadedTagDefinition(uint32_t datum)
{++resolves;return datum==4?liveDefinition:nullptr;}
static volatile LONG* raceAddress{};
static LONG RaceCompareExchange(volatile LONG* address,LONG exchange,LONG expected)
{
    if(address==raceAddress) {*address=0x987;raceAddress=nullptr;}
    return _InterlockedCompareExchange(address,exchange,expected);
}
#undef InterlockedCompareExchange
#define InterlockedCompareExchange RaceCompareExchange
#include "../src/dll/halo3_native_seat_patch.inl"
#undef InterlockedCompareExchange

static void Check(bool condition,const char* message)
{++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}

int main()
{
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x8000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(memory!=nullptr,"fixture allocation");
    auto initialize=[&](unsigned char* base) {
        tagBaseSlot=base;instanceSlot=base+0x2000;liveDefinition=base+0x100;
        *reinterpret_cast<uint32_t*>(base+0x2000+4*8+4)=0x40;
        *reinterpret_cast<int32_t*>(liveDefinition+kHalo3VehicleSeatsBlockOffset)=2;
        *reinterpret_cast<uint32_t*>(liveDefinition+kHalo3VehicleSeatsBlockOffset+4)=0x400;
        *reinterpret_cast<uint32_t*>(base+0x1000+kHalo3VehicleSeatStride)=0x130;
        *reinterpret_cast<int32_t*>(base+0x1000+kHalo3VehicleSeatStride+kHalo3SeatCameraTracksBlockOffset)=1;
    };
    auto reset=[&] {
        g_halo3NativeSeatPatch={};g_halo3NativeSeatState=0;generation=7;
        raceAddress=nullptr;g_config.vehicle_hide_body=true;
        initialize(memory);
        Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation),"native seat acquisition");
        Check(*reinterpret_cast<uint32_t*>(memory+0x1000+kHalo3VehicleSeatStride)==0x120,"only camera bit cleared");
    };
    auto* word=reinterpret_cast<uint32_t*>(memory+0x1000+kHalo3VehicleSeatStride);
    reset();Halo3RestoreNativeSeatPatch();Check(*word==0x130,"owned live storage restored");
    reset();*word=0x987;Halo3RestoreNativeSeatPatch();Check(*word==0x987,"external writer preserved");
    for(unsigned refusal=0;refusal<8;++refusal) {
        reset();
        switch(refusal) {
        case 0:++generation;break;
        case 1:tagBaseSlot=nullptr;break;
        case 2:tagBaseSlot=memory+0x4000;break;
        case 3:instanceSlot=memory+0x3000;break;
        case 4:liveDefinition=memory+0x2000;break;
        case 5:*reinterpret_cast<int32_t*>(liveDefinition+kHalo3VehicleSeatsBlockOffset)=1;break;
        case 6:*reinterpret_cast<uint32_t*>(liveDefinition+kHalo3VehicleSeatsBlockOffset+4)=0x600;break;
        case 7:liveDefinition=nullptr;break;
        }
        Check(!Halo3NativeSeatStillOwned(g_halo3NativeSeatPatch),"same-seat fast path rejects stale identity");
        const auto before=resolves;
        Halo3RestoreNativeSeatPatch();
        Check(*word==0x120,"stale storage never restored");
        Check(!g_halo3NativeSeatPatch.active&&g_halo3NativeSeatState==0,"stale lease retired independently");
        if(refusal==0)Check(resolves==before,"generation refusal precedes tag resolution");
    }
    reset();
    const auto serial=g_halo3NativeSeatSerial.load();
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation)&&g_halo3NativeSeatSerial==serial,
        "unchanged live seat preserves lease without rewrite");
    initialize(memory+0x4000);
    auto* replacement=reinterpret_cast<uint32_t*>(memory+0x5000+kHalo3VehicleSeatStride);
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation)&&*replacement==0x120,
        "same numeric seat reacquires replacement map storage");
    Check(*word==0x120&&g_halo3NativeSeatPatch.flags==replacement,
        "old retired storage is untouched while new storage is owned");
    Halo3RestoreNativeSeatPatch();Check(*replacement==0x130,"replacement storage restores its own value");
    reset();*word=0x987;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x987,
        "changed live native flags are not reported as an active VR lease");
    reset();Halo3RestoreNativeSeatPatch();raceAddress=reinterpret_cast<volatile LONG*>(word);
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x987&&!g_halo3NativeSeatPatch.active,
        "writer racing acquisition wins without an owned patch or rollback");
    reset();raceAddress=reinterpret_cast<volatile LONG*>(word);Halo3RestoreNativeSeatPatch();
    Check(*word==0x987,"writer racing retirement wins atomically");
    reset();
    DWORD previous{};
    Check(VirtualProtect(memory+0x1000,0x1000,PAGE_NOACCESS,&previous)!=0,"retire flag page");
    Check(!Halo3NativeSeatStillOwned(g_halo3NativeSeatPatch),"unreadable active lease is unavailable");
    Halo3RestoreNativeSeatPatch();
    Check(!g_halo3NativeSeatPatch.active&&g_halo3NativeSeatState==0,"fault isolated during cleanup");
    Check(VirtualProtect(memory+0x1000,0x1000,PAGE_READWRITE,&previous)!=0,"restore fixture page");
    Check(*word==0x120,"unreadable storage untouched");
    reset();g_config.vehicle_hide_body=false;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x130,"config off restores live seat");
    Check(*reinterpret_cast<int32_t*>(memory+0x1000+kHalo3VehicleSeatStride+kHalo3SeatCameraTracksBlockOffset)==1,
        "native camera tracks preserved");
    Check(VirtualFree(memory,0,MEM_RELEASE)!=0,"fixture freed");
    std::printf("PASS: %u production Halo 3 seat lifetime checks\n",checks);
}
