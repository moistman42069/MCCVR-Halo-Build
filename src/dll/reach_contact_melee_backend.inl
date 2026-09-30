// Production native contact query/apply boundary; shared with the executable fixture.
struct ReachContactQueryScope
{
    bool active=false;
    uint32_t unit=UINT32_MAX,object=UINT32_MAX;
    unsigned calls=0;
    contact_melee::Sweep sweep{};
    float fraction=0;
    uint8_t collision[0x64]{};
    contact_melee::Hit worldHit{};
};
thread_local ReachContactQueryScope g_reachContactQuery;
struct ReachContactDamageScope
{
    bool active=false,submitted=false;
    uint32_t owner=UINT32_MAX,target=UINT32_MAX;
    float direction[3]{};
    bool world=false;
    uint32_t parameters[20]{};
};
thread_local ReachContactDamageScope g_reachContactDamage;

__declspec(noinline) void __fastcall ReachContactDamageDetour(uint32_t unit,int32_t damage,
    const void* definition,const void* impact,const float* direction)
{
    g_reachCamera.activeCallbacks.fetch_add(1,std::memory_order_acq_rel);
    __try
    {
        auto& scope=g_reachContactDamage;
        const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool own=scope.active &&
            caller>=g_reachCamera.base+0x491100 && caller<g_reachCamera.base+0x4918EC;
        uint8_t kind=0xFF;
        const bool exact=own && unit==scope.owner && impact &&
            (scope.world ? reach_contact_world::ImpactMatches(impact,scope.parameters) :
                (*reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(impact)+0x1C)==scope.target &&
                 ReachVehicleObjectData(static_cast<int32_t>(scope.target),kind) && kind<32));
        // HREK D6CD60's fifth argument is the optional impulse direction.
        // Preserve stock damage/material/ownership; redirect only our exact hit.
        if(g_reachContact.damageOriginal && (!own || exact))
        {
            g_reachContact.damageOriginal(unit,damage,definition,impact,own ? scope.direction : direction);
            if(own) scope.submitted=true;
        }
    }
    __finally { g_reachCamera.activeCallbacks.fetch_sub(1,std::memory_order_acq_rel); }
}

bool ReachRedirectContactVector(uintptr_t caller,uint64_t flags,int32_t mode,
    int32_t ignoredA,int32_t ignoredB,int32_t ignoredC,void* result,uint8_t& returned)
{
    auto& scope=g_reachContactQuery;
    if(!scope.active || caller!=g_reachCamera.base+0x491E8E ||
        static_cast<uint32_t>(ignoredA)!=scope.unit) return false;
    returned=0;
    // HREK D6C1B0 and retail 4919D4 enumerate [-2,2] x [-2,2]. Only
    // their centre sample has zero native aim-assist priority. Substitute the
    // physical segment there and suppress the other 24 head-directed rays.
    if(++scope.calls!=13 || !result || !g_reachWorldCollision.original) return true;
    const float start[]{scope.sweep.start.x,scope.sweep.start.y,scope.sweep.start.z};
    const auto delta=contact_melee::Subtract(scope.sweep.end,scope.sweep.start);
    const float vector[]{delta.x,delta.y,delta.z};
    const auto original=reinterpret_cast<LegacyCollisionTestVectorFn>(g_reachWorldCollision.original);
    returned=original(flags,mode,start,vector,ignoredA,ignoredB,ignoredC,result);
    g_reachContact.queries.fetch_add(1,std::memory_order_relaxed);
    if(returned)
    {
        uint32_t type=0;
        memcpy(&type,result,sizeof(type));
        if(type==4)
        {
            memcpy(&scope.object,static_cast<const uint8_t*>(result)+0x40,sizeof(scope.object));
            memcpy(&scope.fraction,static_cast<const uint8_t*>(result)+4,sizeof(scope.fraction));
        }
        else if(reach_contact_world::Decode(result,scope.worldHit))
        {
            memcpy(scope.collision,result,sizeof(scope.collision));
            scope.fraction=scope.worldHit.fraction;
        }
    }
    return true;
}

struct ReachContactBackend
{
    uint32_t owner=UINT32_MAX;
    uint32_t selected[20]{};
    unsigned selectedPoint=0;
    float selectedFraction=2;
    contact_melee::Hit selectedHit{};
    bool Query(const contact_melee::Sweep& sweep,contact_melee::Hit& hit) noexcept
    {
        uint32_t parameters[20]{};
        g_reachContactQuery={};
        g_reachContactQuery.active=true;
        g_reachContactQuery.unit=owner;
        g_reachContactQuery.sweep=sweep;
        __try { g_reachContact.build(owner,0x8A,parameters); }
        __finally { g_reachContactQuery.active=false; }
        const auto& query=g_reachContactQuery;
        uint8_t kind=0xFF;
        if(query.calls!=25) return false;
        if(query.worldHit.world.valid)
        {
            if(!reach_contact_world::ParametersMatch(parameters,query.collision,query.worldHit)) return false;
            hit=query.worldHit;
        }
        else
        {
            if(query.object==UINT32_MAX || parameters[0]!=query.object ||
                query.object==owner || parameters[1]==UINT32_MAX ||
                !ReachVehicleObjectData(static_cast<int32_t>(query.object),kind) || kind>=32) return false;
            hit={}; hit.unit=query.object; hit.fraction=query.fraction; hit.object=true;
            hit.kind=contact_melee::HitKind::Object;
            memcpy(&hit.position,parameters+0xD,sizeof(hit.position));
            memcpy(&hit.normal,parameters+0x10,sizeof(hit.normal));
        }
        if(!contact_melee::Finite(hit.position) || !contact_melee::Finite(hit.normal) ||
            !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1) return false;
        if(hit.fraction<selectedFraction)
        {
            memcpy(selected,parameters,sizeof(selected));
            selectedPoint=sweep.pointIndex;
            selectedFraction=hit.fraction;
            selectedHit=hit;
        }
        g_reachContact.contacts.fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    bool Apply(uint32_t unit,const contact_melee::Hit& hit,const contact_melee::Sweep& sweep) noexcept
    {
        uint8_t kind=0xFF;
        const bool world=hit.kind==contact_melee::HitKind::WorldSurface && hit.world.valid;
        if(unit!=owner || selected[0]!=hit.unit || selectedPoint!=sweep.pointIndex ||
            (world ? !contact_melee::SameWorldSurface(hit,selectedHit) :
                (!hit.object || !ReachVehicleObjectData(static_cast<int32_t>(hit.unit),kind) || kind>=32))) return false;
        const int16_t mode=ReachContactPredictionMode();
        if(mode<0) return false;
        if(mode==1) g_reachContact.predicted.fetch_add(1,std::memory_order_relaxed);
        const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
        const float length=std::sqrt(contact_melee::Dot(delta,delta));
        if(!std::isfinite(length) || length<=1e-6f) return false;
        g_reachContactDamage={true,false,owner,hit.unit,
            {delta.x/length,delta.y/length,delta.z/length}};
        g_reachContactDamage.world=world;
        if(world) memcpy(g_reachContactDamage.parameters,selected,sizeof(selected));
        // The consumer clamps this value to [0,1] and forwards it as the
        // damage multiplier. A physical strike uses full authored damage;
        // swing speed is solely the admission threshold, not damage scaling.
        __try { g_reachContact.consume(owner,0x8A,mode,1.0f,1,selected,nullptr); }
        __finally { g_reachContactDamage.active=false; }
        // A client submits a native host request; authoritative simulation
        // must actually reach the matching native damage constructor.
        return mode==1 || g_reachContactDamage.submitted;
    }
};
