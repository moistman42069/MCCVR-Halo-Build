#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "../src/common/dual_wield_reload_channel.h"

// Native boundaries are bounded fakes. Non-Windows runs cover normal admission,
// not OS access-violation handling; MSVC compiles the production SEH unchanged.
#ifndef _MSC_VER
#undef __try
#define __try try
#define __except(...) catch(...)
#else
#include <windows.h>
#endif
static struct {bool dual_wield_shake_reload=true;} g_config;
static uint32_t local=0x12340001;
static bool seated=false,dual=true;
static uint32_t held[2]{0x23450002,0x34560003};
static std::atomic<uint32_t> g_halo3RuntimeGeneration{7};
static uint32_t GetUnit(int) {return local;}
static bool InVehicle(int32_t) {return seated;}
static auto g_halo3PlayerUnitGetter=&GetUnit;
static auto g_halo3UnitInVehicle=&InVehicle;
static bool Halo3ReadOwnedDualWeapons(uint32_t unit,uint32_t out[2])
{if(!dual||unit!=local)return false;out[0]=held[0];out[1]=held[1];return true;}
static dual_wield_shake::Channel channel;
static uint64_t now=1000;
static unsigned callbacks=0;
static void VR_DualWieldObserveOwner(GameTitle title,uint32_t generation,
    uint32_t unit,const uint32_t weapons[2]) noexcept
{
    ++callbacks;
    (void)channel.ObserveAndConsume({title,generation,unit,{weapons[0],weapons[1]},1,false},now);
}
#include "../src/dll/halo3_dual_shake_reload.inl"
static unsigned checks=0;
static void Check(bool ok,const char* text)
{++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
int main()
{
    dual_wield_shake::Owner owner;
    // No trigger or haptic callback occurs: idle native unit ticks alone must
    // keep the owner receipt alive. The tick only observes; a shake is a button press.
    for(unsigned i=0;i<120;++i) {
        Halo3DualShakeUnitTick(local);
        Check(channel.Read(now,owner),"idle owner remains fresh beyond the 150ms expiry");
        now+=17;
    }
    Check(owner.unit==local&&owner.weapons[0]==held[0]&&owner.weapons[1]==held[1],
        "the owner receipt names the local unit and both held weapons in slot order");
    const auto before=callbacks;
    Halo3DualShakeUnitTick(local+1);
    seated=true;Halo3DualShakeUnitTick(local);seated=false;
    dual=false;Halo3DualShakeUnitTick(local);dual=true;
    g_config.dual_wield_shake_reload=false;Halo3DualShakeUnitTick(local);
    g_config.dual_wield_shake_reload=true;
    g_halo3PlayerUnitGetter=nullptr;Halo3DualShakeUnitTick(local);g_halo3PlayerUnitGetter=&GetUnit;
    g_halo3UnitInVehicle=nullptr;Halo3DualShakeUnitTick(local);g_halo3UnitInVehicle=&InVehicle;
    Check(callbacks==before,"foreign, seated, single-weapon, disabled and unbound updates stay stock");
    held[1]+=0x10000;
    Halo3DualShakeUnitTick(local);
    dual_wield_shake::Owner replaced;
    Check(channel.Read(now,replaced)&&replaced.weapons[1]==held[1]&&!(replaced==owner),
        "a replaced weapon publishes a new owner pair");
    now+=151;
    Check(!channel.Read(now,owner),"stopped native updates still expire");
    std::printf("Halo 3 dual shake idle routing: %u checks passed\n",checks);
}
