#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../src/common/odst_vehicle_logic.h"

enum class GameTitle { Halo3ODST };
static uint32_t generation=7;
uint32_t TitleAdapter_GetGeneration(GameTitle) {return generation;}
static struct {bool vehicle_first_person=true,vehicle_hide_body=true;} g_config;
// One-machine changes allowed (co-op safe shots off) unless a test says otherwise.
static bool fireTimeAllowed=true;
static bool Sight_FireTimeChangesAllowed(){return fireTimeAllowed;}
#define LOG(...) ((void)0)
static void* tagBaseSlot{},*instanceSlot{};
static void** g_odstTagDataBase=&tagBaseSlot,**g_odstTagInstanceTable=&instanceSlot;
static unsigned char* liveDefinition{};
static unsigned resolves{},checks{};
static unsigned char* OdstLoadedTagDefinition(uint32_t datum)
{++resolves;return datum==4?liveDefinition:nullptr;}
static volatile LONG* raceAddress{};
static LONG RaceCompareExchange(volatile LONG* address,LONG exchange,LONG expected)
{
    if(address==raceAddress) {*address=0x987;raceAddress=nullptr;}
    return _InterlockedCompareExchange(address,exchange,expected);
}
#undef InterlockedCompareExchange
#define InterlockedCompareExchange RaceCompareExchange
#include "../src/dll/odst_native_seat_patch.inl"
#undef InterlockedCompareExchange

static void Check(bool condition,const char* message)
{++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}

int main()
{
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(memory!=nullptr,"fixture allocation");
    auto reset=[&] {
        g_odstNativeSeatPatch={};g_odstNativeSeatState=0;generation=7;
        raceAddress=nullptr;g_config.vehicle_hide_body=true;
        tagBaseSlot=memory;instanceSlot=memory+0x2000;liveDefinition=memory;
        *reinterpret_cast<int32_t*>(memory+kOdstVehicleSeatsCountOffset)=2;
        *reinterpret_cast<uint32_t*>(memory+kOdstVehicleSeatsDataOffset)=0x400;
        *reinterpret_cast<uint32_t*>(memory+0x1000+kOdstVehicleSeatStride)=0x130;
        Check(OdstEnsureFirstPersonSeatFlag(4,1,generation),"native seat acquisition");
        Check(*reinterpret_cast<uint32_t*>(memory+0x1000+kOdstVehicleSeatStride)==0x120,"only camera bit cleared");
    };
    auto* word=reinterpret_cast<uint32_t*>(memory+0x1000+kOdstVehicleSeatStride);
    reset();OdstRestoreNativeSeatPatch();Check(*word==0x130,"owned live storage restored");
    reset();*word=0x987;OdstRestoreNativeSeatPatch();Check(*word==0x987,"external writer preserved");
    for(unsigned refusal=0;refusal<8;++refusal) {
        reset();
        switch(refusal) {
        case 0:++generation;break;
        case 1:tagBaseSlot=nullptr;break;
        case 2:tagBaseSlot=memory+0x2000;break;
        case 3:instanceSlot=memory+0x3000;break;
        case 4:liveDefinition=memory+0x2000;break;
        case 5:*reinterpret_cast<int32_t*>(memory+kOdstVehicleSeatsCountOffset)=1;break;
        case 6:*reinterpret_cast<uint32_t*>(memory+kOdstVehicleSeatsDataOffset)=0x600;break;
        case 7:liveDefinition=nullptr;break;
        }
        Check(!OdstNativeSeatStillOwned(g_odstNativeSeatPatch),"same-seat fast path rejects stale identity");
        const auto before=resolves;
        OdstRestoreNativeSeatPatch();
        Check(*word==0x120,"stale storage never restored");
        Check(!g_odstNativeSeatPatch.active&&g_odstNativeSeatState==0,"stale lease retired independently");
        if(refusal==0)Check(resolves==before,"generation refusal precedes tag resolution");
    }
    reset();
    const auto serial=g_odstNativeSeatSerial.load();
    Check(OdstEnsureFirstPersonSeatFlag(4,1,generation)&&g_odstNativeSeatSerial==serial,
        "unchanged live seat preserves lease without rewrite");
    *reinterpret_cast<uint32_t*>(memory+kOdstVehicleSeatsDataOffset)=0x600;
    auto* replacement=reinterpret_cast<uint32_t*>(memory+0x1800+kOdstVehicleSeatStride);
    *replacement=0x130;
    Check(OdstEnsureFirstPersonSeatFlag(4,1,generation)&&*replacement==0x120,
        "same numeric seat reacquires replacement storage");
    Check(*word==0x120&&g_odstNativeSeatPatch.flags==replacement,"old storage untouched during reacquisition");
    OdstRestoreNativeSeatPatch();Check(*replacement==0x130,"replacement restores its own value");
    reset();*word=0x987;
    Check(!OdstEnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x987,
        "changed native flags are not reported as an active VR lease");
    reset();OdstRestoreNativeSeatPatch();raceAddress=reinterpret_cast<volatile LONG*>(word);
    Check(!OdstEnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x987&&!g_odstNativeSeatPatch.active,
        "writer racing acquisition wins without rollback");
    reset();raceAddress=reinterpret_cast<volatile LONG*>(word);OdstRestoreNativeSeatPatch();
    Check(*word==0x987,"writer racing retirement wins atomically");
    reset();
    DWORD previous{};
    Check(VirtualProtect(memory+0x1000,0x1000,PAGE_NOACCESS,&previous)!=0,"retire flag page");
    Check(!OdstNativeSeatStillOwned(g_odstNativeSeatPatch),"unreadable active lease is unavailable");
    // Valid-looking metadata with disappearing storage must fail only this feature.
    OdstRestoreNativeSeatPatch();
    Check(!g_odstNativeSeatPatch.active&&g_odstNativeSeatState==0,"fault isolated during cleanup");
    Check(VirtualProtect(memory+0x1000,0x1000,PAGE_READWRITE,&previous)!=0,"restore fixture page");
    Check(*word==0x120,"unreadable storage untouched");
    // A seat whose ORIGINAL flags lack bit 4 (custom or modded): refused, and published for the world-angle
    // rebase skip, which stands down there because stock rebases that seat too.
    reset();Check(g_odstSeatNativeNoThirdPerson==0,"a stock seat is not reported as natively lacking bit 4");
    OdstRestoreNativeSeatPatch();*word=0x120;
    Check(!OdstEnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x120,"a seat natively lacking bit 4 is refused");
    Check(g_odstSeatNativeNoThirdPerson==OdstSeatNativeNoThirdPersonKey(4,1),"...and published with its definition and seat");
    Check(!OdstEnsureFirstPersonSeatFlag(4,1,generation)&&g_odstSeatNativeNoThirdPerson==OdstSeatNativeNoThirdPersonKey(4,1),
        "...and stays published while the same seat is occupied");
    OdstRestoreNativeSeatPatch();Check(g_odstSeatNativeNoThirdPerson==0,"...and is cleared when the seat is left");
    *word=0x130;
    reset();g_config.vehicle_hide_body=false;
    Check(!OdstEnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x130,"config off restores live seat");
    // Co-op safe: a seat already taken is handed back, and no seat is taken while the switch is on.
    reset();fireTimeAllowed=false;
    Check(!OdstEnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x130&&g_odstNativeSeatState==0,
        "co-op safe restores the live seat and leaves it stock");
    Check(!OdstEnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x130,"...and never edits it while the switch is on");
    fireTimeAllowed=true;
    Check(OdstEnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x120,"...and takes it again once changes are allowed");
    OdstRestoreNativeSeatPatch();
    Check(VirtualFree(memory,0,MEM_RELEASE)!=0,"fixture freed");
    std::printf("PASS: %u production ODST seat lifetime checks\n",checks);
}
