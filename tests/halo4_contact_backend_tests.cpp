#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../src/common/halo4_contact_world.h"
namespace
{
constexpr uint32_t owner=0x12340001,target=0x23450002;
uintptr_t returnAddress=0x10000000+0x601935;
void* TestReturnAddress(){return reinterpret_cast<void*>(returnAddress);}
#pragma pack(push,1)
struct Halo4PhysicsRayCastInput
{
    uint32_t profile{},collisionFlags{},additionalFlags{};float start[3]{},end[3]{};
    int32_t ignoredObjects[64]{};uint8_t ignoredObjectCount{},padding[3]{},resultOptions[4]{};
};
struct Halo4PhysicsRayCastResult
{
    uint32_t type{};float fraction{},position[3]{},normal[3]{};uint8_t unknown20[8]{};
    int32_t objectIndex=-1;uint8_t remaining[0x70-0x2C]{};
};
#pragma pack(pop)
static_assert(sizeof(Halo4PhysicsRayCastInput)==0x12C&&sizeof(Halo4PhysicsRayCastResult)==0x70);
struct {bool(__fastcall* originalRayCast)(Halo4PhysicsRayCastInput*,Halo4PhysicsRayCastResult*){};}g_halo4WorldCollision;
struct
{
    uintptr_t base=0x10000000;std::atomic<unsigned> callbacks{},queries{},contacts{},predicted{};
    const uint64_t* flags{};
    void(__fastcall* build)(uint32_t,int32_t,uint32_t*){};
    void(__fastcall* consume)(uint32_t,uint64_t,int16_t,float,uint8_t,uint32_t*,void*){};
    void(__fastcall* damageOriginal)(uint32_t,int32_t,const void*,const void*,const float*){};
    uint32_t(__fastcall* simulationMode)(){};
}g_halo4Contact;
bool validTarget=true;
const uint8_t* Halo4ContactObject(uint32_t handle)
{return validTarget&&(handle==owner||handle==target)?reinterpret_cast<const uint8_t*>(&validTarget):nullptr;}
const uint8_t* Halo4ContactBiped(uint32_t handle){return handle==owner?Halo4ContactObject(handle):nullptr;}
}
#define _ReturnAddress() TestReturnAddress()
#include "../src/dll/halo4_contact_melee_backend.inl"
#undef _ReturnAddress
namespace
{
unsigned checks{},failures{},damageCalls{},castCalls{},consumerCalls{};
uint32_t collisionType=3,shapeKey=91,mode{};bool corruptMaterial{},corruptParameters{},corruptImpact{},raiseBuild{},raiseDamage{},differentReplay{};
float direction[3]{};
uint32_t consumed[20]{};
template<class T>void Put(void* p,unsigned at,T value){std::memcpy(static_cast<uint8_t*>(p)+at,&value,sizeof(value));}
void Check(bool v,const char* message){++checks;if(!v){++failures;std::fprintf(stderr,"H4 contact: %s\n",message);}}
uint32_t __fastcall Mode(){return mode;}
bool __fastcall Cast(Halo4PhysicsRayCastInput* input,Halo4PhysicsRayCastResult* out)
{
    ++castCalls;Check(input->profile==0x1A,"H4 native profile retained");
    Check(input->ignoredObjects[0]==int32_t(owner)&&input->ignoredObjectCount==1,"H4 owner filter retained");
    *out={};out->type=collisionType;out->fraction=.5f;
    for(unsigned n=0;n<3;++n)out->position[n]=(input->start[n]+input->end[n])*.5f;
    out->normal[0]=-1;out->objectIndex=collisionType==4?target:-1;
    Put(out,0x20,uint32_t(2));Put(out,0x24,uint32_t(3));Put(out,0x38,uint16_t(12));
    Put(out,0x60,uint64_t(0x12345000));Put(out,0x68,shapeKey+(differentReplay&&g_halo4ContactScope.active?1u:0u));
    // Native padding must not become part of surface identity.
    Put(out,0x35,uint8_t(g_halo4ContactScope.active?37:83));return true;
}
void __fastcall Build(uint32_t unit,int32_t action,uint32_t* p)
{
    Check(unit==owner&&action==0xEA,"H4 native builder ABI");if(raiseBuild)RaiseException(0xE043BB01,0,0,nullptr);
    for(unsigned n=0;n<20;++n)p[n]=UINT32_MAX;p[1]=100;p[8]=12;
    for(unsigned n=0;n<25;++n)
    {
        Halo4PhysicsRayCastInput input{};input.profile=0x1A;input.ignoredObjects[0]=owner;input.ignoredObjectCount=1;
        Halo4PhysicsRayCastResult output{};bool returned{};
        Check(Halo4RedirectContactRay(g_halo4Contact.base+0x6020EA,&input,&output,returned),"H4 native fan scoped");
        if(returned){p[0]=collisionType==4?target:UINT32_MAX;p[5]=2;std::memcpy(p+13,output.position,12);std::memcpy(p+16,output.normal,12);}
    }
    if(corruptParameters)++p[5];
    if(corruptMaterial)++p[8];
}
void __fastcall Damage(uint32_t unit,int32_t,const void*,const void*,const float* d)
{Check(unit==owner,"H4 attacker retained");++damageCalls;if(raiseDamage)RaiseException(0xE043BB02,0,0,nullptr);std::memcpy(direction,d,12);}
void __fastcall Consume(uint32_t unit,uint64_t action,int16_t simulation,float scale,uint8_t apply,uint32_t* p,void*)
{
    std::memcpy(consumed,p,sizeof(consumed));
    ++consumerCalls;Check(unit==owner&&action==0xEA&&scale==1&&apply==1,"H4 native authority/full-damage ABI");if(simulation==1)return;
    uint8_t impact[0x3C]{};std::memcpy(impact,p+13,12);std::memcpy(impact+12,p+16,12);
    Put(impact,0x18,p[5]);Put(impact,0x1C,p[0]);Put(impact,0x20,uint16_t(p[6]));Put(impact,0x22,uint16_t(p[7]));
    Put(impact,0x24,p[10]);Put(impact,0x28,p[11]);Put(impact,0x30,uint16_t(p[8]));
    if(corruptImpact)Put(impact,0x18,uint32_t(999));const float nativeDirection[]{0,1,0};
    Halo4ContactDamageDetour(unit,100,nullptr,impact,nativeDirection);
}
uint8_t bspBytes[0x6C]{},*bspResult{};
unsigned bspRepresentation{},bspTree{},bspViewCalls{};bool bspFault{},bspValid=true,bspMismatch{};
float bspPlane[]{-1,0,0,0};
void* __fastcall BspView(void* output,uint32_t bsp,uintptr_t unused,void* transform)
{
    ++bspViewCalls;Check(bsp==2&&!unused&&!transform,"H4 own BSP resource/view ABI");
    if(bspFault)RaiseException(0xE043BB03,0,0,nullptr);
    auto** view=static_cast<void**>(output);view[0]=view[1]=nullptr;view[bspRepresentation]=bspBytes;
    Put(bspBytes,0xC,uint32_t(bspTree));return output;
}
__m128 __fastcall BspInitialize(void* context,uint32_t flags,const void* bsp,const void* material,
    uintptr_t edgeFlags,uintptr_t edges,const float* start,const float* vector,float limit,void* output)
{
    Check(!(reinterpret_cast<uintptr_t>(context)&15)&&bsp==bspBytes,"H4 aligned native traversal context");
    Check(flags==3&&!material&&!edgeFlags&&!edges&&limit==1,"H4 native BSP traversal admission ABI");
    Check(start[0]==0&&vector[0]==1,"H4 BSP uses actual hand segment");
    bspResult=static_cast<uint8_t*>(output);return _mm_set_ps(1,0,1,0);
}
uint8_t BspVisit(const void* span)
{
    float values[4]{};std::memcpy(values,span,sizeof(values));
    Check(values[0]==0&&values[1]==1&&values[2]==0&&values[3]==1,"H4 SIMD native span preserved");
    Put(bspResult,0,bspMismatch?.7f:.5f);Put(bspResult,8,static_cast<const float*>(bspPlane));
    Put(bspResult,0x14,int32_t(91));Put(bspResult,0x1D,uint8_t(8));
    Put(bspResult,0x1E,uint8_t(3));Put(bspResult,0x1F,uint8_t(7));return 1;
}
uint8_t __fastcall BspStandard(void*,uint32_t index,const void* span)
{Check(!bspTree&&index==0,"H4 standard BSP branch");return BspVisit(span);}
uint8_t __fastcall BspSupernode(void*,uint32_t index,int32_t a,int32_t b,int32_t plane,const void* span)
{Check(bspTree&&index==0&&a==0&&b==0&&plane==-1,"H4 supernode BSP branch ABI");return BspVisit(span);}
uint8_t __fastcall BspValid(uint32_t bsp,int32_t instance,int32_t set,uint32_t surface)
{Check(bsp==2&&instance==-1&&set==7&&surface==3,"H4 native authored surface validity tuple");return bspValid?1:0;}
bool ApplyRaises(Halo4ContactBackend* backend,const contact_melee::Hit* hit,const contact_melee::Sweep* sweep)
{__try{return backend->Apply(owner,*hit,*sweep),false;}__except(EXCEPTION_EXECUTE_HANDLER){return true;}}
}
int main()
{
    const uint64_t flags=17;g_halo4Contact.flags=&flags;g_halo4Contact.build=Build;g_halo4Contact.consume=Consume;
    g_halo4Contact.damageOriginal=Damage;g_halo4Contact.simulationMode=Mode;g_halo4WorldCollision.originalRayCast=Cast;
    const contact_melee::Sweep sweep{{0,0,0},{1,0,0},{1,0,0},2,0};
    for(auto type:{1u,3u,4u})
    {
        collisionType=type;Halo4ContactBackend backend{};backend.owner=owner;contact_melee::Hit hit{};
        Check(backend.Query(sweep,hit),"H4 world/object query admitted");const auto before=castCalls;
        Check(backend.Apply(owner,hit,sweep),"H4 exact native world/object submission");
        Check(castCalls==before+1,"only physical center native fan ray executes");
        Check(direction[0]==1&&direction[1]==0,"physical hand impulse direction retained");
        Check(!g_halo4ContactScope.active&&!g_halo4Contact.callbacks,"scope/callback balance");
        if(type!=4)
        {
            Check(!hit.object&&hit.unit==UINT32_MAX&&hit.world.valid,"world has no fake object");
            differentReplay=true;const auto calls=consumerCalls;Check(!backend.Apply(owner,hit,sweep)&&calls==consumerCalls,"changed native surface replay rejected");differentReplay=false;
        }
    }
    collisionType=3;Halo4ContactBackend backend{};backend.owner=owner;contact_melee::Hit hit{};Check(backend.Query(sweep,hit),"H4 world reset");
    corruptParameters=true;Check(!backend.Apply(owner,hit,sweep),"H4 builder retarget rejected");corruptParameters=false;
    corruptMaterial=true;Check(!backend.Apply(owner,hit,sweep),"H4 changed native material rejected");corruptMaterial=false;
    corruptImpact=true;const auto before=damageCalls;Check(!backend.Apply(owner,hit,sweep)&&before==damageCalls,"H4 foreign world impact rejected");corruptImpact=false;
    mode=4;Check(backend.Apply(owner,hit,sweep)&&before==damageCalls,"H4 client retains native host request");mode=0;
    raiseBuild=true;Check(ApplyRaises(&backend,&hit,&sweep)&&!g_halo4ContactScope.active,"H4 builder exception propagates with scope cleanup");raiseBuild=false;
    raiseDamage=true;Check(ApplyRaises(&backend,&hit,&sweep)&&!g_halo4ContactScope.active&&!g_halo4Contact.callbacks,"H4 damage exception propagates with cleanup");raiseDamage=false;
    g_halo4ContactBsp.view=BspView;g_halo4ContactBsp.initialize=BspInitialize;g_halo4ContactBsp.valid=BspValid;
    for(unsigned n=0;n<2;++n){g_halo4ContactBsp.standard[n]=BspStandard;g_halo4ContactBsp.supernode[n]=BspSupernode;}
    g_halo4ContactBsp.enabled=true;
    for(bspRepresentation=0;bspRepresentation<2;++bspRepresentation)for(bspTree=0;bspTree<2;++bspTree)
    {
        Check(backend.Apply(owner,hit,sweep),"H4 native BSP glass reaches unchanged damage consumer");
        Check(consumed[6]==7&&consumed[7]==3&&consumed[10]==UINT32_MAX&&consumed[11]==91,"H4 real BSP breakable IDs preserved without object handle");
    }
    bspRepresentation=0;bspTree=0;bspMismatch=true;
    Check(backend.Apply(owner,hit,sweep)&&consumed[6]==UINT32_MAX,"H4 different BSP surface retains material-only contact");bspMismatch=false;
    bspValid=false;Check(backend.Apply(owner,hit,sweep)&&consumed[6]==UINT32_MAX,"H4 broken native glass retains material-only contact");bspValid=true;
    bspFault=true;
    const bool faultContact=backend.Apply(owner,hit,sweep);
    Check(faultContact,"H4 BSP fault retains native material contact");
    Check(g_halo4ContactBsp.faulted.load(),"H4 BSP fault disables only supplement");
    Check(consumed[6]==UINT32_MAX,"H4 BSP fault retains native tuple sentinels");bspFault=false;
    const unsigned queriesAfterFault=bspViewCalls;
    Check(backend.Apply(owner,hit,sweep)&&consumed[6]==UINT32_MAX&&bspViewCalls==queriesAfterFault,
        "H4 faulted BSP supplement stays bypassed while material contact continues");
    std::printf("H4 native contact backend: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
