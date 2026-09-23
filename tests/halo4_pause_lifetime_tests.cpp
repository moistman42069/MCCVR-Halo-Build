#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static unsigned checks=0,nativeCalls=0;
static bool nativePaused=false,nativeFault=false;
static bool __fastcall NativePause(int32_t reason)
{
    if(reason!=3)std::abort();
    ++nativeCalls;
    if(nativeFault)RaiseException(0xe0424242,0,0,nullptr);
    return nativePaused;
}
static struct {
    std::atomic<bool> pauseProven{true};
    bool(__fastcall* pauseReason)(int32_t)=&NativePause;
} g_halo4Restoration;
static uint32_t index=4;
static uint32_t* g_halo4EngineTlsIndex=&index;
static const uint8_t* slots[0x200]{};
static const uint8_t* const* currentSlots=slots;
static uintptr_t FixtureTls(unsigned long offset)
{if(offset!=0x58)std::abort();return reinterpret_cast<uintptr_t>(currentSlots);}
#define __readgsqword FixtureTls
#include "../src/dll/halo4_pause_reader.inl"
#undef __readgsqword

static void Check(bool condition,const char* name)
{++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
int main()
{
    alignas(void*) uint8_t tls[0x100]{};
    uint8_t pauseData[4]{};
    slots[4]=tls;
    const void* pausePointer=pauseData;
    std::memcpy(tls+0x90,&pausePointer,sizeof(pausePointer));
    bool paused=false;
    for(bool value:{false,true}) {
        nativePaused=value;paused=!value;
        Check(Halo4ReadNativePaused(paused)&&paused==value,"live native reason-three state retained");
    }
    const auto before=nativeCalls;
    for(unsigned missing=0;missing<7;++missing) {
        g_halo4Restoration.pauseProven=true;g_halo4Restoration.pauseReason=&NativePause;
        g_halo4EngineTlsIndex=&index;index=4;currentSlots=slots;slots[4]=tls;
        std::memcpy(tls+0x90,&pausePointer,sizeof(pausePointer));
        switch(missing) {
        case 0:g_halo4Restoration.pauseProven=false;break;
        case 1:g_halo4Restoration.pauseReason=nullptr;break;
        case 2:g_halo4EngineTlsIndex=nullptr;break;
        case 3:index=0x200;break;
        case 4:currentSlots=nullptr;break;
        case 5:slots[4]=nullptr;break;
        case 6:std::memset(tls+0x90,0,sizeof(void*));break;
        }
        paused=true;
        Check(!Halo4ReadNativePaused(paused)&&nativeCalls==before,
            "retired or absent native thread state is unknown without native call");
    }
    std::memcpy(tls+0x90,&pausePointer,sizeof(pausePointer));
    nativeFault=true;
    Check(!Halo4ReadNativePaused(paused)&&nativeCalls==before+1,
        "post-check native exception stays feature-local without replay");
    nativeFault=false;
    Check(Halo4ReadNativePaused(paused)&&paused==nativePaused,
        "fresh native thread state recovers after transient loss");
    std::printf("%u Halo 4 production pause lifetime checks passed\n",checks);
}
