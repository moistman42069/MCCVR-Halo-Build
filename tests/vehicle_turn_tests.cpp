#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "../src/common/config.h"
#include "../src/common/runtime_types.h"
#include "../src/common/halo3_vehicle_logic.h"
#include "../src/common/vehicle_control_logic.h"
#include "../src/common/vr_turn_mode.h"

static unsigned checks{};
static void Check(bool ok,const char* reason){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<.0001f;}
static GameTitle title=GameTitle::Halo3;
static uint32_t generation=7;
static GameTitle TitleAdapter_GetActiveTitle(){return title;}
static uint32_t TitleAdapter_GetGeneration(GameTitle){return generation;}
static std::atomic<uint32_t> g_halo3RuntimeGeneration{7},g_odstRuntimeGeneration{7};
enum class OdstVehicleBindingState:uint8_t{NotInstalled,Installed};
static std::atomic<uint8_t> g_odstVehicleBinding{uint8_t(OdstVehicleBindingState::Installed)};
static std::atomic<uint32_t> g_odstVehicleFpStable{uint32_t(Halo3VehicleState::Vehicle)};
struct H3Seat{Halo3VehicleState state{Halo3VehicleState::Vehicle};int seatIndex=0;} h3Seat;
static H3Seat Game_Halo3VehicleState(){return h3Seat;}
struct OdstSeatSnapshot{int seatIndex=0,parentHandle=0x12340001;} odstSeat;
static bool odstFresh=true,reachSeated=true;
static bool OdstReadSeatSnapshot(OdstSeatSnapshot& out){out=odstSeat;return odstFresh;}
static bool Game_ReachPlayerIsInVehicle(){return reachSeated;}
#include "../src/dll/vr_turn_vehicle_state.inl"

struct VrPadState{bool valid=true;float turnX=0;};
static std::atomic<bool> g_vrAim{true};
static float g_gameYawRef{};
static bool authorsSteering=false,stickAims=false,wheelActive=false,h4Active=true,h4Valid=true,h4Seated=true;
static bool Halo3SeatAuthorsSteeringNow(){return authorsSteering;}
static bool Halo3SeatStickAimsNow(){return stickAims;}
static bool Game_Halo3VehicleWheelActive(){return wheelActive;}
static bool Halo4ControllerAimActive(){return h4Active;}
struct Halo4VehicleInputState{bool seated=false;};
// The camera hook's seat stamp (production: Halo4ReadSeatStamp in game.cpp).
static bool Halo4ReadSeatStamp(Halo4VehicleInputState& out){out.seated=h4Seated;return h4Valid;}
struct H4Camera{std::atomic<float> gameYawReference{0};std::atomic<uint64_t> vrTurns{0};}g_halo4Camera;
static float WrapPi(float value){return std::remainder(value,6.28318530718f);}
static int64_t ticks=100000;
static BOOL FakeFrequency(LARGE_INTEGER* out){out->QuadPart=1000000;return TRUE;}
static BOOL FakeCounter(LARGE_INTEGER* out){out->QuadPart=ticks;return TRUE;}
#define QueryPerformanceFrequency FakeFrequency
#define QueryPerformanceCounter FakeCounter
#include "../src/dll/shared_vr_turn.inl"
#include "../src/dll/halo4_vr_turn.inl"
#undef QueryPerformanceFrequency
#undef QueryPerformanceCounter

static float Step(bool h4,float x)
{
    ticks+=10000;
    const float before=h4?g_halo4Camera.gameYawReference.load():g_gameYawRef;
    if(h4)Halo4ApplyVrTurn({true,x});else ApplyVrTurn({true,x});
    return WrapPi((h4?g_halo4Camera.gameYawReference.load():g_gameYawRef)-before);
}
static void Seated(bool h4,bool value)
{
    h4Seated=value;reachSeated=value;h3Seat.state=value?Halo3VehicleState::Vehicle:Halo3VehicleState::OnFoot;
    g_odstVehicleFpStable=uint32_t(value?Halo3VehicleState::Vehicle:Halo3VehicleState::OnFoot);
}
static void TurnTests(bool h4)
{
    g_config=Config{};g_config.turn_smooth=false;g_config.turn_snap_deg=45;g_config.turn_smooth_deg_s=120;
    Seated(h4,true);Step(h4,0);
    Check(Near(Step(h4,1),-.785398163f),"disabled option preserves seated snap");
    g_config.vehicle_smooth_turn=true;
    Check(Near(Step(h4,1),-.02094395f)&&!g_config.turn_smooth,"vehicle override smooths held stick and keeps saved snap");
    Seated(h4,false);
    Check(Step(h4,1)==0,"vehicle exit with held stick does not synthesize snap");
    Step(h4,0);Check(Near(Step(h4,1),-.785398163f),"on-foot mode returns after neutral gesture");
    Seated(h4,true);Step(h4,0);
    Check(Near(Step(h4,-1),.02094395f),"opposite vehicle direction uses smooth rate");
    g_config.vehicle_smooth_turn=false;
    Check(Step(h4,-1)==0,"disabling override with held stick does not snap");
    Step(h4,0);Check(Near(Step(h4,-1),.785398163f),"disabled override restores snap while seated");
    g_config.turn_smooth=true;Seated(h4,false);
    Check(Near(Step(h4,1),-.02094395f),"saved smooth preference remains effective outside vehicles");
    Check(Step(h4,.1f)==0,"smooth deadzone retained");
    if(!h4){authorsSteering=true;wheelActive=false;Check(Step(false,1)==0,"native driver steering keeps the stick");
        wheelActive=true;Check(Near(Step(false,1),-.02094395f),"active physical wheel releases VR turn stick");authorsSteering=wheelActive=false;
        stickAims=true;Check(Step(false,1)==0,"a seat the stick aims keeps the stick");
        wheelActive=true;Check(Step(false,1)==0,"and a wheel that is not this seat's does not free it");stickAims=wheelActive=false;
        Check(Near(Step(false,1),-.02094395f),"back in a hand-aimed seat the stick turns the view again");}
}
int main()
{
    for(auto next:{GameTitle::Halo3,GameTitle::Halo3ODST,GameTitle::HaloReach}){title=next;TurnTests(false);}
    title=GameTitle::Halo4;TurnTests(true);
    g_config.turn_smooth=false;g_config.vehicle_smooth_turn=true;h4Valid=false;Step(true,0);
    Check(Near(Step(true,1),-.785398163f),"unproven H4 occupancy retains saved mode");h4Valid=true;
    title=GameTitle::Halo3;h3Seat.state=Halo3VehicleState::Vehicle;
    Check(SharedVrTurnSeated(),"current H3 occupant is admitted");++generation;
    Check(!SharedVrTurnSeated(),"old H3 generation refused");--generation;h3Seat.seatIndex=-1;
    Check(!SharedVrTurnSeated(),"H3 missing seat refused");h3Seat.seatIndex=0;
    title=GameTitle::Halo3ODST;g_odstVehicleFpStable=uint32_t(Halo3VehicleState::Vehicle);
    Check(SharedVrTurnSeated(),"current ODST occupant admitted");odstFresh=false;
    Check(!SharedVrTurnSeated(),"stale ODST seat refused");odstFresh=true;odstSeat.parentHandle=1;
    Check(!SharedVrTurnSeated(),"unsalted ODST parent refused");odstSeat.parentHandle=0x12340001;++generation;
    Check(!SharedVrTurnSeated(),"old ODST generation refused");--generation;
    title=GameTitle::HaloReach;reachSeated=false;Check(!SharedVrTurnSeated(),"unknown Reach snapshot retains saved mode");
    title=GameTitle::Halo2;Check(!SharedVrTurnSeated(),"H2 native vehicle turning needs no shared override");
    const auto path=std::filesystem::temp_directory_path()/(L"mccvr-vehicle-turn-"+std::to_wstring(GetCurrentProcessId())+L".cfg");
    {std::ofstream f(path);f<<"turn_smooth = 0\nvehicle_smooth_turn = 1\nturn_snap_deg = 45\nturn_smooth_deg_s = 150\n";}
    ConfigLoad(path.c_str());Check(!g_config.turn_smooth&&g_config.vehicle_smooth_turn,"independent preferences load");
    ConfigSave();ConfigLoad(path.c_str());Check(!g_config.turn_smooth&&g_config.vehicle_smooth_turn&&g_config.turn_snap_deg==45&&g_config.turn_smooth_deg_s==150,"both modes and rates survive save/load");
    {std::ofstream f(path);f<<"turn_smooth = 0\n";}ConfigLoad(path.c_str());Check(!g_config.vehicle_smooth_turn,"old configs default override off");
    std::filesystem::remove(path);
    std::printf("PASS: %u vehicle turn production/config checks\n",checks);
}
