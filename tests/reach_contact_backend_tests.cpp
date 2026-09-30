#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../src/common/reach_contact_world.h"

namespace
{
constexpr uint32_t owner=0x12340001,target=0x23450002;
uintptr_t returnAddress=0x10000000+0x491865;
void* TestReturnAddress() { return reinterpret_cast<void*>(returnAddress); }
struct { uintptr_t base=0x10000000;std::atomic<unsigned> activeCallbacks{}; } g_reachCamera;
using LegacyCollisionTestVectorFn=uint8_t(__fastcall*)(uint64_t,int32_t,const float*,const float*,int32_t,int32_t,int32_t,void*);
struct { void* original{}; } g_reachWorldCollision;
struct
{
    void(__fastcall* build)(uint32_t,int32_t,uint32_t*){};
    void(__fastcall* consume)(uint32_t,uint64_t,int16_t,float,uint8_t,uint32_t*,void*){};
    void(__fastcall* damageOriginal)(uint32_t,int32_t,const void*,const void*,const float*){};
    std::atomic<unsigned> queries{},contacts{},predicted{};
} g_reachContact;
bool validTarget=true;
const void* ReachVehicleObjectData(int32_t handle,uint8_t& kind)
{ kind=0;return validTarget && static_cast<uint32_t>(handle)==target ? &validTarget : nullptr; }
int16_t predictionMode=0;
int16_t ReachContactPredictionMode() { return predictionMode; }
}
#define _ReturnAddress() TestReturnAddress()
#include "../src/dll/reach_contact_melee_backend.inl"
#undef _ReturnAddress

namespace
{
unsigned checks{},failures{},damageCalls{},collisionCalls{},consumerCalls{};
bool glass=true,corruptMaterial=false,corruptParameters=false,corruptImpact=false,raiseBuild=false,raiseDamage=false;
uint32_t collisionType=1;
float damageDirection[3]{};
template<class T> void Put(void* p,unsigned at,T value)
{ std::memcpy(static_cast<uint8_t*>(p)+at,&value,sizeof(value)); }
void Check(bool okay,const char* message)
{ ++checks;if(!okay){++failures;std::fprintf(stderr,"Reach contact: %s\n",message);} }
uint8_t __fastcall Collision(uint64_t,int32_t,const float* start,const float* vector,
    int32_t ignored,int32_t,int32_t,void* result)
{
    ++collisionCalls;Check(static_cast<uint32_t>(ignored)==owner,"query retains owner");
    std::memset(result,0,0x64);Put(result,0,collisionType);Put(result,4,.5f);
    const contact_melee::Point point{start[0]+.5f*vector[0],start[1]+.5f*vector[1],start[2]+.5f*vector[2]};
    Put(result,8,point);Put(result,0x2C,contact_melee::Point{-1,0,0});
    Put(result,0x28,uint16_t(12));Put(result,0x3C,uint32_t(61));Put(result,0x40,collisionType==4?target:UINT32_MAX);
    Put(result,0x4C,uint32_t(2));Put(result,0x54,uint32_t(91));
    Put(result,0x5D,uint8_t(glass?8:0));Put(result,0x5E,uint8_t(7));Put(result,0x62,uint8_t(3));return 1;
}
void __fastcall Build(uint32_t unit,int32_t action,uint32_t* p)
{
    Check(unit==owner && action==0x8A,"native Reach builder ABI retained");
    if(raiseBuild) RaiseException(0xE043AA01,0,0,nullptr);
    for(unsigned n=0;n<20;++n) p[n]=UINT32_MAX;
    p[1]=100;p[8]=12;
    for(unsigned n=0;n<25;++n)
    {
        uint8_t raw[0x64]{};uint8_t returned{};
        Check(ReachRedirectContactVector(g_reachCamera.base+0x491E8E,17,1,
            owner,-1,-1,raw,returned),"native ray is scoped");
        if(!returned) continue;
        p[0]=collisionType==4?target:UINT32_MAX;p[5]=2;
        std::memcpy(p+13,raw+8,12);std::memcpy(p+16,raw+0x2C,12);
        if(glass && collisionType!=4){p[6]=3;p[7]=7;p[10]=61;p[11]=91;}
    }
    if(corruptParameters) ++p[10];
    if(corruptMaterial) ++p[8];
}
void __fastcall Damage(uint32_t unit,int32_t,const void*,const void*,const float* direction)
{
    Check(unit==owner,"native damage retains attacker");++damageCalls;
    if(raiseDamage) RaiseException(0xE043AA02,0,0,nullptr);
    std::memcpy(damageDirection,direction,sizeof(damageDirection));
}
void __fastcall Consume(uint32_t unit,uint64_t action,int16_t mode,float scale,uint8_t apply,uint32_t* p,void*)
{
    ++consumerCalls;Check(unit==owner && action==0x8A && scale==1 && apply==1,"native full damage/authority ABI retained");
    if(mode==1) return;
    uint8_t impact[0x3C]{};std::memcpy(impact,p+13,12);std::memcpy(impact+12,p+16,12);
    Put(impact,0x18,p[5]);Put(impact,0x1C,p[0]);Put(impact,0x20,uint16_t(p[6]));Put(impact,0x22,uint16_t(p[7]));
    Put(impact,0x24,p[10]);Put(impact,0x28,p[11]);Put(impact,0x30,uint16_t(p[8]));
    if(corruptImpact) Put(impact,0x24,uint32_t(999));
    const float originalDirection[]{0,1,0};
    ReachContactDamageDetour(unit,100,nullptr,impact,originalDirection);
}
bool QueryRaises(ReachContactBackend* backend,const contact_melee::Sweep* sweep)
{ __try {contact_melee::Hit hit;backend->Query(*sweep,hit);return false;} __except(EXCEPTION_EXECUTE_HANDLER){return true;} }
bool ApplyRaises(ReachContactBackend* backend,const contact_melee::Hit* hit,const contact_melee::Sweep* sweep)
{ __try {backend->Apply(owner,*hit,*sweep);return false;} __except(EXCEPTION_EXECUTE_HANDLER){return true;} }
}
int main()
{
    g_reachContact.build=Build;g_reachContact.consume=Consume;g_reachContact.damageOriginal=Damage;
    g_reachWorldCollision.original=reinterpret_cast<void*>(Collision);
    const contact_melee::Sweep sweep{{0,0,0},{1,0,0},{1,0,0},2,0};
    for(auto type:{1u,3u,4u}) for(bool isGlass:{false,true})
    {
        collisionType=type;glass=isGlass;ReachContactBackend backend{};backend.owner=owner;contact_melee::Hit hit{};
        const unsigned before=collisionCalls;Check(backend.Query(sweep,hit),"native world/object query admitted");
        Check(collisionCalls==before+1,"only physical center ray executes");
        Check(type==4?hit.object:(hit.unit==UINT32_MAX&&hit.world.valid&&!hit.object),"world never gains fabricated object handle");
        Check(backend.Apply(owner,hit,sweep),"native exact world/object consumer submits");
        Check(damageDirection[0]==1&&damageDirection[1]==0,"native impulse uses physical direction");
        Check(!g_reachContactDamage.active&&!g_reachContactQuery.active,"native scopes restored");
        auto wrong=hit;if(type==4)wrong.unit=123;else ++wrong.world.nativeData[4];
        Check(!backend.Apply(owner,wrong,sweep),"changed target/surface rejected");
    }
    collisionType=1;glass=true;ReachContactBackend backend{};backend.owner=owner;contact_melee::Hit hit{};
    corruptParameters=true;Check(!backend.Query(sweep,hit),"builder retarget/breakable mismatch rejected");corruptParameters=false;
    corruptMaterial=true;Check(!backend.Query(sweep,hit),"changed native material rejected");corruptMaterial=false;
    Check(backend.Query(sweep,hit),"glass query restored");corruptImpact=true;
    const auto before=damageCalls;Check(!backend.Apply(owner,hit,sweep)&&damageCalls==before,"foreign world impact suppressed");corruptImpact=false;
    predictionMode=1;Check(backend.Apply(owner,hit,sweep)&&damageCalls==before,"client uses native host request without local damage");predictionMode=-1;
    const auto calls=consumerCalls;Check(!backend.Apply(owner,hit,sweep)&&consumerCalls==calls,"invalid simulation mode rejects submission");predictionMode=0;
    raiseBuild=true;Check(QueryRaises(&backend,&sweep)&&!g_reachContactQuery.active,"native builder exception propagates and clears scope");raiseBuild=false;
    raiseDamage=true;Check(ApplyRaises(&backend,&hit,&sweep)&&!g_reachContactDamage.active&&!g_reachCamera.activeCallbacks,"native damage exception propagates and balances callbacks");raiseDamage=false;
    uint8_t raw[0x64]{};Put(raw,0,uint32_t(2));Check(!reach_contact_world::Decode(raw,hit),"unsupported native type remains rejected");
    uint8_t ret{};Check(!ReachRedirectContactVector(0,0,0,owner,-1,-1,raw,ret),"unowned native query remains stock");
    std::printf("Reach native contact backend: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
