// Production H4 contact boundary, included by game.cpp and its native fixture.
#include "halo4_contact_bsp.inl"
struct Halo4ContactScope
{
    bool active=false,applying=false,submitted=false;
    uint32_t owner=UINT32_MAX,target=UINT32_MAX;
    unsigned rays=0;
    contact_melee::Sweep sweep{};
    float direction[3]{};
    contact_melee::Hit worldHit{};
    uint32_t parameters[20]{};
};
thread_local Halo4ContactScope g_halo4ContactScope;
bool Halo4RedirectContactRay(uintptr_t caller,Halo4PhysicsRayCastInput* input,
    Halo4PhysicsRayCastResult* output,bool& returned)
{
    auto& scope=g_halo4ContactScope;
    if(!scope.active || caller<g_halo4Contact.base+0x601B1C ||
        caller>=g_halo4Contact.base+0x60296C) return false;
    returned=false;
    // Only the centre of the native 25-ray grid uses the physical segment.
    // The later obstruction/retarget ray is suppressed inside this scope too.
    if(caller!=g_halo4Contact.base+0x6020EA || ++scope.rays!=13 ||
        !input || !output || !g_halo4WorldCollision.originalRayCast) return true;
    auto local=*input;
    const float start[]{scope.sweep.start.x,scope.sweep.start.y,scope.sweep.start.z};
    const float end[]{scope.sweep.end.x,scope.sweep.end.y,scope.sweep.end.z};
    memcpy(local.start,start,sizeof(start));
    memcpy(local.end,end,sizeof(end));
    returned=g_halo4WorldCollision.originalRayCast(&local,output);
    if(returned && scope.worldHit.world.valid)
    {
        contact_melee::Hit fresh{};
        returned=halo4_contact_world::Decode(output,fresh) &&
            contact_melee::SameWorldSurface(scope.worldHit,fresh);
    }
    else if(returned)
        returned=output->type==4 && static_cast<uint32_t>(output->objectIndex)==scope.target &&
            Halo4ContactObject(scope.target);
    return true;
}

__declspec(noinline) void __fastcall Halo4ContactDamageDetour(uint32_t unit,int32_t damage,
    const void* definition,const void* impact,const float* direction)
{
    g_halo4Contact.callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        auto& scope=g_halo4ContactScope;
        const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool own=scope.active && scope.applying &&
            (caller==g_halo4Contact.base+0x6012F2 || caller==g_halo4Contact.base+0x601315 ||
             caller==g_halo4Contact.base+0x6013C9 || caller==g_halo4Contact.base+0x6013FD ||
             caller==g_halo4Contact.base+0x601935);
        const bool exact=own && unit==scope.owner && impact && Halo4ContactBiped(scope.owner) &&
            (scope.worldHit.world.valid ? halo4_contact_world::ImpactMatches(impact,scope.parameters) :
                (*reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(impact)+0x1C)==scope.target &&
                 Halo4ContactObject(scope.target)));
        if(g_halo4Contact.damageOriginal && (!own || exact))
        {
            g_halo4Contact.damageOriginal(unit,damage,definition,impact,own ? scope.direction : direction);
            if(own) scope.submitted=true;
        }
    }
    __finally { g_halo4Contact.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

struct Halo4ContactBackend
{
    uint32_t owner=UINT32_MAX;
    bool Query(const contact_melee::Sweep& sweep,contact_melee::Hit& hit) noexcept
    {
        Halo4PhysicsRayCastInput input{};
        Halo4PhysicsRayCastResult output{};
        if(!g_halo4WorldCollision.originalRayCast || !g_halo4Contact.flags) return false;
        input.profile=0x1A; // H4EK E71F40 / retail 601B1C native melee profile.
        memcpy(&input.collisionFlags,g_halo4Contact.flags,sizeof(uint64_t));
        const float start[]{sweep.start.x,sweep.start.y,sweep.start.z};
        const float end[]{sweep.end.x,sweep.end.y,sweep.end.z};
        memcpy(input.start,start,sizeof(start));
        memcpy(input.end,end,sizeof(end));
        input.ignoredObjects[0]=static_cast<int32_t>(owner);
        input.ignoredObjectCount=1;
        input.resultOptions[0]=1;
        g_halo4Contact.queries.fetch_add(1,std::memory_order_relaxed);
        if(!g_halo4WorldCollision.originalRayCast(&input,&output)) return false;
        if(output.type==4)
        {
            if(static_cast<uint32_t>(output.objectIndex)==owner ||
                !Halo4ContactObject(static_cast<uint32_t>(output.objectIndex))) return false;
            hit={};hit.unit=static_cast<uint32_t>(output.objectIndex);
            hit.fraction=output.fraction;
            memcpy(&hit.position,output.position,sizeof(output.position));
            memcpy(&hit.normal,output.normal,sizeof(output.normal));
            hit.object=true;hit.kind=contact_melee::HitKind::Object;
        }
        else if(!halo4_contact_world::Decode(&output,hit)) return false;
        if(!contact_melee::Finite(hit.position) || !contact_melee::Finite(hit.normal) ||
            !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1) return false;
        g_halo4Contact.contacts.fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    bool Apply(uint32_t unit,const contact_melee::Hit& hit,const contact_melee::Sweep& sweep) noexcept
    {
        const bool world=hit.kind==contact_melee::HitKind::WorldSurface && hit.world.valid;
        if(unit!=owner || !Halo4ContactBiped(owner) ||
            (world ? (hit.unit!=UINT32_MAX || hit.object) :
                (!hit.object || !Halo4ContactObject(hit.unit)))) return false;
        const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
        const float length=std::sqrt(contact_melee::Dot(delta,delta));
        if(!std::isfinite(length) || length<=1e-6f) return false;
        g_halo4ContactScope={true,false,false,owner,hit.unit,0,sweep,
            {delta.x/length,delta.y/length,delta.z/length}};
        if(world) g_halo4ContactScope.worldHit=hit;
        uint32_t parameters[20]{};
        bool accepted=false;
        __try
        {
            g_halo4Contact.build(owner,0xEA,parameters);
            if(g_halo4ContactScope.rays==25 && parameters[0]==hit.unit &&
                (world ? halo4_contact_world::ParametersMatch(parameters,hit) :
                    Halo4ContactObject(hit.unit)!=nullptr))
            {
                const int16_t mode=g_halo4Contact.simulationMode()==4 ? 1 : 0;
                if(world && g_halo4Contact.flags)
                    Halo4TryContactGlass(hit,sweep,*g_halo4Contact.flags,parameters);
                g_halo4ContactScope.applying=true;
                if(world) memcpy(g_halo4ContactScope.parameters,parameters,sizeof(parameters));
                g_halo4Contact.consume(owner,0xEA,mode,1.0f,1,parameters,nullptr);
                if(mode==1) g_halo4Contact.predicted.fetch_add(1,std::memory_order_relaxed);
                accepted=mode==1 || g_halo4ContactScope.submitted;
            }
        }
        __finally { g_halo4ContactScope.active=false; }
        return accepted;
    }
};
