#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "../src/common/halo2_world_collision_logic.h"
#include "../src/common/halo2_vehicle_identity.h"
using Halo2GraphDefinitionGetFn=const void*(__fastcall*)(uint32_t);
std::atomic<bool> g_vehicleIdentityVerified{true};
std::atomic<uintptr_t> g_graphDefinitionGet{},g_halo2TagDataBaseSlot{};
unsigned checks=0;
uint32_t parentHandle=0x12340007,modelTag=0x4321000A,renderTag=0x5678000B;
uint16_t definition=9;
alignas(8) uint8_t parent[0x220]{},object[0x40]{},model[8]{},render[0x1C]{},data[0x200]{};
const uint8_t* dataBase=data;
bool mutate=false;
const void* Halo2ObjectFromIndex(uint32_t handle){return handle==parentHandle?parent:nullptr;}
const void* __fastcall Get(uint32_t tag){
    if(tag==definition)return object;
    if(tag==modelTag)return model;
    if(tag==renderTag){if(mutate)parent[0]++;return render;}
    return nullptr;
}
bool CountPatternMatches(uintptr_t,size_t,const char*,uintptr_t&,uint32_t&)noexcept{return false;}
#include "../src/dll/halo2_vehicle_identity.inl"
void Check(bool pass,const char* reason){++checks;if(!pass){std::printf("FAIL: %s\n",reason);std::exit(1);}}
template<class T>void Put(uint8_t* bytes,size_t at,T value){std::memcpy(bytes+at,&value,sizeof(value));}
int main(){
    g_graphDefinitionGet=reinterpret_cast<uintptr_t>(&Get);g_halo2TagDataBaseSlot=reinterpret_cast<uintptr_t>(&dataBase);
    const halo2_vehicle_identity::Model* warthog=nullptr;
    for(const auto& candidate:halo2_vehicle_identity::kModels)
        if(!std::strcmp(candidate.name,"Warthog"))warthog=&candidate;
    Check(warthog!=nullptr,"official Warthog present");
    Put(parent,0,definition);parent[0xAA]=1;Put(parent,0x118,static_cast<int16_t>(warthog->nodes*0x34));
    Put(object,0x38,modelTag);Put(model,4,renderTag);Put(render,0x14,int32_t{1});Put(render,0x18,int32_t{0x100});
    std::memcpy(data+0x100,warthog->bounds,24);
    Check(ReadHalo2VehicleIdentity(parentHandle)==warthog->identity,"production native chain identifies Warthog");
    for(unsigned map=1;map<16;++map){
        parentHandle+=0x10000;definition+=17;modelTag+=0x10020;renderTag+=0x10030;
        Put(parent,0,definition);Put(object,0x38,modelTag);Put(model,4,renderTag);
        Check(ReadHalo2VehicleIdentity(parentHandle)==warthog->identity,"map-local handles do not alter persistent model identity");
    }
    Check(ReadHalo2VehicleIdentity(parentHandle-0x10000)==0,"stale parent salt refused");
    g_vehicleIdentityVerified=false;Check(ReadHalo2VehicleIdentity(parentHandle)==0,"missing optional native proof stays unknown");g_vehicleIdentityVerified=true;
    parent[0xAA]=0;Check(ReadHalo2VehicleIdentity(parentHandle)==0,"nonvehicle object refused");parent[0xAA]=1;
    Put(parent,0x118,int16_t{53});Check(ReadHalo2VehicleIdentity(parentHandle)==0,"partial native matrix stride refused");
    Put(parent,0x118,static_cast<int16_t>(warthog->nodes*0x34));
    Put(data,0x100,10000.f);Check(ReadHalo2VehicleIdentity(parentHandle)==0,"unknown/custom model retains game fallback");
    Put(data,0x100,std::numeric_limits<float>::quiet_NaN());Check(ReadHalo2VehicleIdentity(parentHandle)==0,"nonfinite model bounds refused");
    std::memcpy(data+0x100,warthog->bounds,24);mutate=true;
    Check(ReadHalo2VehicleIdentity(parentHandle)==0,"definition changing during lookup rejected");mutate=false;Put(parent,0,definition);
    Put(render,0x14,int32_t{2});Check(ReadHalo2VehicleIdentity(parentHandle)==0,"multiple compression records not guessed");
    unsigned unique=0;
    for(const auto& candidate:halo2_vehicle_identity::kModels){
        const auto* resolved=halo2_vehicle_identity::Find(candidate.bounds,candidate.nodes);
        if(resolved){++unique;Check(resolved->identity==candidate.identity,"each unambiguous official tuple retains its own key");}
    }
    Check(unique>50,"catalog covers official vehicle/submodel collection");
    Check(!halo2_vehicle_identity::Name(0),"unknown identity has no invented name");
    std::printf("PASS: %u H2 persistent vehicle identity checks, %u unique official models\n",checks,unique);
}
