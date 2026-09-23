#include <Windows.h>
#include <Xinput.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../src/common/runtime_types.h"

namespace {
#include "../src/dll/gesture_binding_reader.inl"
}

static unsigned checks=0;
static void Check(bool condition,const char* label)
{
    ++checks;
    if(!condition) {std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}
}
template<class T> static void Put(std::vector<uint8_t>& bytes,size_t offset,T value)
{std::memcpy(bytes.data()+offset,&value,sizeof(value));}

static uint32_t Expected(GameTitle title,unsigned button)
{
    static constexpr uint32_t common[]={
        1u<<16,1u<<17,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_BACK,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER};
    static constexpr uint32_t h2[]={
        1u<<16,1u<<17,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_BACK,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_Y};
    return button>=16?0:(title==GameTitle::Halo2?h2[button]:common[button]);
}

static void SetBinding(const GestureBindingDescriptor& d,std::vector<uint8_t>& memory,
    unsigned controller,unsigned action,int button)
{
    const size_t record=controller*d.stride;
    if(d.title==GameTitle::HaloCE)
        Put<int16_t>(memory,record+d.remap+action*2,static_cast<int16_t>(button));
    else if(d.title==GameTitle::Halo2) {
        const size_t block=record+action*100;
        Put<int32_t>(memory,block+0x1C,1);
        Put<int32_t>(memory,block+0x20,2);
        Put<int32_t>(memory,block+0x24,button);
        Put<int32_t>(memory,block+0x28,0);
    } else {
        Put<uint32_t>(memory,record+d.controllerOffset,controller);
        Put<uint32_t>(memory,record+d.remap+action*4,static_cast<uint32_t>(button));
    }
}

int main()
{
    for(const auto& d:kGestureBindings) {
        std::vector<uint8_t> memory(4*d.stride+0x200,0xFF);
        for(unsigned action=0;action<d.actionCount;++action) {
            // Distinct controller rows catch CE's additional controller*42
            // selection and later titles' independent preference-bank strides.
            for(unsigned shift=0;shift<16;++shift) {
                for(unsigned c=0;c<4;++c) SetBinding(d,memory,c,action,(shift+c)%16);
                uint32_t transport[4]{};
                Check(GestureReadNativeBindings(d,reinterpret_cast<uintptr_t>(memory.data()),
                    transport,action),"live production parser");
                for(unsigned c=0;c<4;++c)
                    Check(transport[c]==Expected(d.title,(shift+c)%16),"semantic transport per controller/action");
            }
        }
        for(int bad:{-1,16,32767}) {
            for(unsigned c=0;c<4;++c) SetBinding(d,memory,c,d.action,bad);
            uint32_t transport[4]{};
            Check(GestureReadNativeBindings(d,reinterpret_cast<uintptr_t>(memory.data()),
                transport,d.action),"unbound/malformed binding has no memory fault");
            for(auto value:transport) Check(value==0,"unbound/malformed binding emits no input");
        }
        uint32_t unavailable[4]{};
        Check(GestureReadNativeBindings(d,0,unavailable,d.actionCount),
            "unsupported action does not read game memory");
        Check(!GestureReadNativeBindings(d,0,unavailable,d.action),
            "retired native preference memory is contained by production SEH");
        for(unsigned c=0;c<4;++c) SetBinding(d,memory,c,d.action,10);
        if(d.title!=GameTitle::HaloCE&&d.title!=GameTitle::Halo2) {
            Put<uint32_t>(memory,d.controllerOffset,3);
            uint32_t transport[4]{};
            Check(GestureReadNativeBindings(d,reinterpret_cast<uintptr_t>(memory.data()),
                transport,d.action)&&transport[0]==0&&transport[1]!=0,
                "mismatched preference owner rejects only that controller");
        }
        if(d.title==GameTitle::Halo2) {
            for(int badCount:{-1,0,9}) {
                Put<int32_t>(memory,d.action*100+0x1C,badCount);
                uint32_t transport[4]{};
                Check(GestureReadNativeBindings(d,reinterpret_cast<uintptr_t>(memory.data()),
                    transport,d.action)&&transport[0]==0&&transport[1]!=0,
                    "H2 count bounds preserve other controller rows");
            }
            SetBinding(d,memory,0,d.action,10);
            Put<int32_t>(memory,d.action*100+0x28,1);
            uint32_t held[4]{};
            Check(GestureReadNativeBindings(d,reinterpret_cast<uintptr_t>(memory.data()),
                held,d.action)&&held[0]==0,"H2 held binding is not guessed as a normal press");
            SetBinding(d,memory,0,d.action,10);
            Put<int32_t>(memory,d.action*100+0x20,1);
            uint32_t keyboard[4]{};
            Check(GestureReadNativeBindings(d,reinterpret_cast<uintptr_t>(memory.data()),
                keyboard,d.action)&&keyboard[0]==0,"H2 keyboard binding is not a gamepad button");
        }
    }
    // Inaccessible storage models a native retirement; the caller must discard
    // the entire false result instead of publishing partial transports.
    const auto& ce=kGestureBindings[0];
    void* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    Check(inaccessible!=nullptr,"inaccessible fixture allocation");
    uint32_t transport[4]{};
    Check(!GestureReadNativeBindings(ce,reinterpret_cast<uintptr_t>(inaccessible),transport,ce.action),
        "native inaccessible pages are contained");
    VirtualFree(inaccessible,0,MEM_RELEASE);
    std::printf("%u production native action binding checks passed across six titles\n",checks);
}
