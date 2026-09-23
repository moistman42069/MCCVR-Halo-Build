#include "../src/common/physical_crouch_logic.h"
#include "../src/common/physical_crouch_native_read.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static unsigned checks{};
static void Check(bool value,const char* label)
{++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
template<class T> static void Put(uint8_t* bytes,unsigned at,T value)
{std::memcpy(bytes+at,&value,sizeof(value));}
int main()
{
    for(bool odst:{false,true})
    {
        uint8_t unit[0x2400]{},definition[0x400]{};
        const unsigned parent=odst?0x10:0x14,scale=odst?0x8C:0xA0,
            fraction=odst?0x398:0x30C,height=odst?0x310:0x218;
        Put(unit,parent,UINT32_MAX);Put(unit,scale,2.f);
        Put(definition,height,1.f);Put(definition,odst?0x318:0x21C,.6f);
        Put(unit,0x15A,int16_t(0x600));
        const auto read=odst?physical_crouch_native::Odst:physical_crouch_native::Halo2;
        for(float blend:{0.f,.25f,.5f,.75f,1.f})
        {
            Put(unit,fraction,blend);float reduction=-1;
            Check(read(unit,definition,reduction)&&std::fabs(reduction-.8f*blend)<.00001f,
                "own-title native crouch height interpolation and scale");
        }
        float reduction=0;
        Put(unit,parent,uint32_t(0x12340001));
        Check(!read(unit,definition,reduction),"seated native camera is never compensated");
        Put(unit,parent,UINT32_MAX);
        Put(unit,fraction,std::numeric_limits<float>::quiet_NaN());
        Check(!read(unit,definition,reduction),"invalid native crouch blend rejected");
        Put(unit,fraction,1.f);
        if(odst)
        {
            Put(unit,0x600,int32_t(7));Check(!read(unit,definition,reduction),"tilted ODST up-vector keeps native camera");
            Put(unit,0x600,int32_t(0));unit[0x516]=6;Put(unit,0x51C,uint32_t(0x10));
            Check(read(unit,definition,reduction)&&reduction==0,"ODST special standing state has no crouch reduction");
        }
        else
        {
            Put(unit,0x390,uint16_t(0x2000));Check(!read(unit,definition,reduction),"H2 animation special state conservatively refused");
        }
        Check(!read(nullptr,definition,reduction)&&!read(unit,nullptr,reduction),"missing native storage leaves camera unchanged");
    }
    for(float normal:{.9f,1.2f,1.7f,2.0f}) for(float depth:{.08f,.22f,.65f})
    {
        PhysicalCrouch crouch;uint64_t now=1000;
        auto step=[&](float height,bool ready=true,uint64_t epoch=7){now+=20;return crouch.Update(height,epoch,now,ready,depth);};
        Check(!step(normal),"first normal standing or seated height calibrates without a press");
        Check(!step(normal-depth*.9f),"movement above threshold does not crouch");
        Check(step(normal-depth*1.1f),"lowering beyond selected depth presses crouch");
        Check(step(normal-depth*.8f),"hysteresis prevents boundary chatter");
        Check(!step(normal-depth*.6f),"standing above release threshold releases crouch");
        Check(step(normal-depth*1.1f),"deliberate second crouch can activate");
        Check(!step(normal-depth*1.1f,false),"menus vehicles and lost tracking release the gesture");
        Check(!step(normal-depth*1.1f),"resume cannot replay a crouch held during exclusion");
        Check(!step(normal),"standing rearms without a press");
        Check(step(normal-depth*1.1f),"fresh crouch after resume works");
        Check(!step(normal-depth*1.1f,true,8),"recenter samples new reference without crouching");
        crouch.Reset();Check(!step(normal),"disable and re-enable starts neutral");
        now+=300;Check(!step(normal-depth*1.1f),"stale sample cannot become a crouch");
        Check(!step(normal),"stale tracking rearms upright");
        Check(step(normal-depth*1.1f),"fresh tracked crouch after gap works");
        Check(!step(std::numeric_limits<float>::quiet_NaN()),"nonfinite tracking releases input");
        Check(!step(normal-depth*1.1f),"nonfinite recovery requires standing again");
    }
    PhysicalCrouch state;
    Check(!state.Update(1.7f,1,1000,true,.22f),"calibration starts neutral");
    Check(!state.Update(1.7f,1,1020,true,.4f),"sensitivity edit cannot create a crouch edge");
    Check(!state.Update(1.7f,1,1040,true,std::numeric_limits<float>::infinity()),"invalid sensitivity uses finite default and recalibrates");
    Check(state.Update(1.4f,1,1060,true,.22f),"finite fallback still permits physical crouching");
    std::printf("Physical crouch: %u checks passed\n",checks);
}
