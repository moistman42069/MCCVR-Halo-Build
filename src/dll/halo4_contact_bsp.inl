// Own H4EK collision-BSP vector traversal, matched through retail 24F53C.
// This supplements a confirmed Havok world hit with an actual BSP surface;
// render triangle indices are never interpreted as breakable IDs.
#include <xmmintrin.h>
struct Halo4ContactBspRuntime
{
    std::atomic<bool> enabled{false};
    std::atomic<bool> faulted{false};
    void*(__fastcall* view)(void*,uint32_t,uintptr_t,void*)=nullptr;
    __m128(__fastcall* initialize)(void*,uint32_t,const void*,const void*,
        uintptr_t,uintptr_t,const float*,const float*,float,void*)=nullptr;
    uint8_t(__fastcall* standard[2])(void*,uint32_t,const void*){};
    uint8_t(__fastcall* supernode[2])(void*,uint32_t,int32_t,int32_t,int32_t,const void*){};
    uint8_t(__fastcall* valid)(uint32_t,int32_t,int32_t,uint32_t)=nullptr;
} g_halo4ContactBsp;

__declspec(noinline) bool Halo4ResolveContactGlass(const contact_melee::Hit& hit,
    const contact_melee::Sweep& sweep,uint64_t collisionFlags,uint32_t* parameters)
{
    if(!g_halo4ContactBsp.enabled || g_halo4ContactBsp.faulted.load(std::memory_order_acquire) ||
        !hit.world.valid || hit.world.nativeData[0]>=32 || !parameters) return false;
    uintptr_t view[2]{};
    g_halo4ContactBsp.view(view,hit.world.nativeData[0],0,nullptr);
    const unsigned representation=view[0] ? 0u : 1u;
    const auto* bsp=reinterpret_cast<const uint8_t*>(view[representation]);
    if(!bsp) return false;
    // H4EK 4EDF90 derives exactly these flags for the collision-BSP path.
    uint32_t flags=static_cast<uint32_t>((collisionFlags>>12)&0x3F);
    if(!(flags&3)) flags|=3;
    const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
    const float start[]{sweep.start.x,sweep.start.y,sweep.start.z};
    const float vector[]{delta.x,delta.y,delta.z};
    alignas(16) uint8_t context[0x120]{};
    alignas(16) uint8_t result[0x28]{};
    // The actual native return is XMM0, not the decompiler's guessed integer.
    __m128 span=g_halo4ContactBsp.initialize(context,flags,bsp,nullptr,
        0,0,start,vector,1.0f,result);
    const bool found=halo4_contact_world::Read<uint32_t>(bsp,0xC) ?
        g_halo4ContactBsp.supernode[representation](context,0,0,0,-1,&span)!=0 :
        g_halo4ContactBsp.standard[representation](context,0,&span)!=0;
    if(!found || (result[0x1D]&0x10) || !(result[0x1D]&8) || result[0x1E]==0xFF || result[0x1F]==0xFF) return false;
    const auto surface=halo4_contact_world::Read<int32_t>(result,0x14);
    const float fraction=halo4_contact_world::Read<float>(result,0);
    const auto* plane=halo4_contact_world::Read<const float*>(result,8);
    if(surface<0 || !plane || !std::isfinite(fraction) || fraction<0 || fraction>1 ||
        std::fabs(fraction-hit.fraction)>0.01f) return false;
    const contact_melee::Point point{sweep.start.x+fraction*delta.x,
        sweep.start.y+fraction*delta.y,sweep.start.z+fraction*delta.z};
    const auto distance=contact_melee::Subtract(point,hit.position);
    contact_melee::Point normal{plane[0],plane[1],plane[2]};
    if(result[0x1C]) normal={-normal.x,-normal.y,-normal.z};
    const float normal2=contact_melee::Dot(normal,normal);
    const float hitNormal2=contact_melee::Dot(hit.normal,hit.normal);
    if(!contact_melee::Finite(point) || !contact_melee::Finite(normal) ||
        !std::isfinite(normal2) || !std::isfinite(hitNormal2) ||
        contact_melee::Dot(distance,distance)>0.000025f || normal2<0.5f || hitNormal2<0.5f ||
        contact_melee::Dot(normal,hit.normal)<0.98f*std::sqrt(normal2*hitNormal2) ||
        !g_halo4ContactBsp.valid(hit.world.nativeData[0],-1,result[0x1F],result[0x1E])) return false;
    // Actual collision surface/set from the native BSP, with no fabricated
    // object or instance. Native damage retains thresholds and host ownership.
    parameters[6]=result[0x1F];parameters[7]=result[0x1E];
    parameters[10]=UINT32_MAX;parameters[11]=static_cast<uint32_t>(surface);
    return true;
}

__declspec(noinline) bool Halo4TryContactGlass(const contact_melee::Hit& hit,
    const contact_melee::Sweep& sweep,uint64_t flags,uint32_t* parameters) noexcept
{
    volatile bool resolved=false;
    volatile bool faulted=false;
    __try { resolved=Halo4ResolveContactGlass(hit,sweep,flags,parameters); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        // Isolate the optional BSP supplement; the confirmed material contact
        // and existing object damage continue. Reported outside the hot path.
        faulted=true;
    }
    if(faulted) g_halo4ContactBsp.faulted.store(true,std::memory_order_release);
    return resolved;
}
