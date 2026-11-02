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
// The camera director's side (halo3_seat_view.inl in the mod). The checks below that predate it run in the
// tag-write mode, which is what they were written for.
static bool g_writesTag=true,g_viewAvailable=true;
static int32_t g_armedUnit=-1,g_chaseUnit=-1;
static bool Halo3SeatViewWritesTag() {return g_writesTag;}
static bool Halo3SeatViewAvailable() {return g_viewAvailable;}
static void Halo3SeatViewArm(int32_t unit) {g_armedUnit=unit;}
static void Halo3SeatViewDisarm() {g_armedUnit=-1;g_chaseUnit=-1;}
static void Halo3SeatViewArmChase(int32_t unit) {g_chaseUnit=unit;}
// The vehicle cameras' side (vehicle_cameras.inl in the mod): a chase camera of the mod's own is this seat's view.
static bool g_chaseCamera=false;
static bool Halo3VehicleCameraThirdPerson() {return g_chaseCamera;}
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
    // A seat whose ORIGINAL flags lack bit 4 (custom or modded): refused, and published for the world-angle
    // rebase skip, which stands down there because stock rebases that seat too.
    reset();Check(g_halo3SeatNativeNoThirdPerson==0,"a stock seat is not reported as natively lacking bit 4");
    Halo3RestoreNativeSeatPatch();*word=0x120;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x120,"a seat natively lacking bit 4 is refused");
    Check(g_halo3SeatNativeNoThirdPerson==Halo3SeatNativeNoThirdPersonKey(4,1),"...and published with its definition and seat");
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation)&&g_halo3SeatNativeNoThirdPerson==Halo3SeatNativeNoThirdPersonKey(4,1),
        "...and stays published while the same seat is occupied");
    Halo3RestoreNativeSeatPatch();Check(g_halo3SeatNativeNoThirdPerson==0,"...and is cleared when the seat is left");
    // Such a seat shown from a chase camera of the mod's own: its occupant is told third person, in the seat view not.
    g_writesTag=false;g_viewAvailable=true;g_chaseCamera=true;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x120&&g_chaseUnit==0x1234&&g_armedUnit==-1,
        "a seat natively lacking bit 4 under a chase camera: told third person, never written");
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_chaseUnit==0x1234,"...and on every frame of it");
    g_chaseCamera=false;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_chaseUnit==-1,"...the seat view keeps the game's answer");
    g_chaseCamera=true;g_viewAvailable=false;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_chaseUnit==-1,"...and so does a director that is not answered");
    g_viewAvailable=true;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_chaseUnit==0x1234,"...until it is");
    Halo3RestoreNativeSeatPatch();Check(g_chaseUnit==-1,"...and leaving the seat ends it");
    // The body setting off: no seat view to hold, a chase camera still answered, and never disarmed frame by frame.
    g_config.vehicle_hide_body=false;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x120&&g_chaseUnit==0x1234,
        "the body setting off: a chase camera's occupant is still told third person");
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_chaseUnit==0x1234,"...on every frame of it");
    g_config.vehicle_hide_body=true;Halo3RestoreNativeSeatPatch();
    g_chaseCamera=false;g_writesTag=true;
    *word=0x130;
    reset();g_config.vehicle_hide_body=false;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation)&&*word==0x130,"config off restores live seat");
    Check(*reinterpret_cast<int32_t*>(memory+0x1000+kHalo3VehicleSeatStride+kHalo3SeatCameraTracksBlockOffset)==1,
        "native camera tracks preserved");
    // THE DEFAULT: the tag is left as loaded and the camera director is answered instead.
    g_halo3NativeSeatPatch={};g_halo3NativeSeatState=0;generation=7;raceAddress=nullptr;
    g_config.vehicle_hide_body=true;initialize(memory);*word=0x130;
    g_writesTag=false;g_viewAvailable=true;g_armedUnit=-1;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x130,"no tag write: the seat's flags stay as loaded");
    Check(g_armedUnit==0x1234&&g_halo3NativeSeatState==1,"...the occupant is armed for the director and the seat is Active");
    Check(Halo3OccupiedSeatFlags()==0x130&&Halo3NativeSeatStillOwned(g_halo3NativeSeatPatch),"...its flags are known and the lease holds");
    g_armedUnit=-1;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x130&&g_armedUnit==0x1234,"...and it is armed again on every frame");
    // A chase camera of the mod's own: the director keeps its third-person answer, and nothing else about the seat changes.
    g_chaseCamera=true;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x130&&g_armedUnit==-1&&g_halo3NativeSeatState==1,
        "a chase camera: the occupant is not armed for the director, and the seat stays Active with its flags as loaded");
    Check(g_chaseUnit==0x1234,"...and its occupant is armed for a first-person answer, which a host's hook could give");
    Check(Halo3OccupiedSeatFlags()==0x130&&Halo3NativeSeatStillOwned(g_halo3NativeSeatPatch),"...its flags stay known and the lease holds");
    g_chaseCamera=false;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_armedUnit==0x1234,"...and back in the seat view the occupant is armed again");
    g_chaseCamera=true;Halo3RestoreNativeSeatPatch();
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x130&&g_armedUnit==-1&&g_halo3NativeSeatState==1,
        "a seat entered with a chase camera already chosen is Active without the director being answered");
    g_chaseCamera=false;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_armedUnit==0x1234,"...and is armed when the seat view is chosen");
    *word=0x120;
    Check(!Halo3NativeSeatStillOwned(g_halo3NativeSeatPatch),"a tag someone else changed is not this lease's");
    *word=0x130;
    Halo3RestoreNativeSeatPatch();
    Check(*word==0x130&&g_armedUnit==-1&&g_halo3NativeSeatState==0,"leaving the seat disarms and writes nothing");
    g_viewAvailable=false;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x130&&g_armedUnit==-1&&g_halo3NativeSeatState==2,
        "no relay: stock fallback, nothing armed, nothing written");
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_armedUnit==-1,"...and it stays so while the relay is down");
    g_viewAvailable=true;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_armedUnit==0x1234&&*word==0x130,
        "...and is taken up when the relay comes up");
    g_viewAvailable=false;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&g_armedUnit==-1&&g_halo3NativeSeatState==2,
        "the relay going down mid-seat ends the lease");
    g_viewAvailable=true;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234),"...until it is back");
    g_writesTag=true;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x120&&g_armedUnit==-1,
        "switching to the tag write (dev A/B) writes it and disarms the director");
    g_writesTag=false;
    Check(Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x130&&g_armedUnit==0x1234,
        "...and switching back restores the tag and arms again");
    g_config.vehicle_hide_body=false;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x130&&g_armedUnit==-1,"config off disarms");
    g_config.vehicle_hide_body=true;
    *word=0x120;
    Check(!Halo3EnsureFirstPersonSeatFlag(4,1,generation,0x1234)&&*word==0x120&&g_armedUnit==-1,
        "a seat natively lacking bit 4 is refused in this mode too");
    Halo3RestoreNativeSeatPatch();*word=0x130;g_writesTag=true;
    Check(VirtualFree(memory,0,MEM_RELEASE)!=0,"fixture freed");
    std::printf("PASS: %u production Halo 3 seat lifetime checks\n",checks);
}
