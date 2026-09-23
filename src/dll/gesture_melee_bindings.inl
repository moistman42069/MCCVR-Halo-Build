// Included by game.cpp. Evidence: docs/GESTURE-MELEE-BINDING-EVIDENCE.md.
// Only the cold worker touches game memory. Input consumes generation-tagged
// atomic snapshots; there is no new native hook or game-state write.
namespace
{
#include "gesture_binding_reader.inl"

struct GestureBindingCache
{
    std::atomic<uint64_t> value[4]{};
    std::atomic<uint64_t> actions[4][vr_mapping::Count]{};
    std::atomic<uint64_t> observedMs{0};
    std::atomic<uint64_t> sequence{0};
    uintptr_t attemptedBase=0, state=0;
    uint32_t attemptedGeneration=0;
    GameTitle attemptedTitle=GameTitle::None;
    uint32_t lastTransport[4]{UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
} g_gestureBinding;

bool GestureUniqueAt(uintptr_t base,size_t size,uintptr_t rva,const char* pattern)
{
    const uintptr_t found=sig::Find(base,size,pattern);
    return rva<size && found==base+rva &&
        !sig::Find(found+1,size-(found+1-base),pattern);
}

void RefreshGestureMeleeBinding(const TitleDescriptor* title,bool levelRunning,uint64_t now)
{
    if(!title || !levelRunning)
    { g_gestureBinding.observedMs.store(0,std::memory_order_release); return; }
    const GestureBindingDescriptor* descriptor=nullptr;
    for(const auto& d:kGestureBindings) if(d.title==title->title) descriptor=&d;
    if(!descriptor) { g_gestureBinding.observedMs.store(0,std::memory_order_release); return; }
    // Pin only while this cold read/verification runs. No module pointer is
    // exposed to the input hook and no reference survives the worker tick.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(0,title->moduleName,&pinned))
    { g_gestureBinding.observedMs.store(0,std::memory_order_release); return; }
    uintptr_t base=0; size_t size=0;
    const uint32_t generation=TitleAdapter_GetGeneration(title->title);
    uint32_t transport[4]{};
    const auto& d=*descriptor;
    bool readable=sig::ModuleRange(title->moduleName,base,size) && base==reinterpret_cast<uintptr_t>(pinned);
    if(readable && (g_gestureBinding.attemptedBase!=base ||
        g_gestureBinding.attemptedGeneration!=generation || g_gestureBinding.attemptedTitle!=title->title))
    {
        g_gestureBinding.state=0;
        g_gestureBinding.attemptedBase=base;
        g_gestureBinding.attemptedGeneration=generation;
        g_gestureBinding.attemptedTitle=title->title;
        for(auto& previous:g_gestureBinding.lastTransport) previous=UINT32_MAX;
        if(GestureUniqueAt(base,size,d.readerRva,d.readerPattern) &&
           GestureUniqueAt(base,size,d.stateLoadRva,d.statePattern))
        {
            const bool ce=d.title==GameTitle::HaloCE;
            const uintptr_t state=sig::RipTarget(base+d.stateLoadRva+(ce?6:10),base+d.stateLoadRva+(ce?10:14));
            if(state>=base && state-base<=size && size-(state-base)>=4*d.stride)
                g_gestureBinding.state=state;
        }
        LOG("Gesture melee %s: active binding reader %s; generation %u",title->displayName,
            g_gestureBinding.state ? "verified (read only)" : "unavailable; gesture stays stock",generation);
    }
    readable=readable && g_gestureBinding.state && GestureReadNativeBindings(d,g_gestureBinding.state,transport,d.action);
    uint32_t actions[vr_mapping::Count][4]{};
    for(unsigned action=0;readable&&action<vr_mapping::Count;++action)
        readable=GestureReadNativeBindings(d,g_gestureBinding.state,actions[action],
            vr_mapping::NativeAction(d.title,static_cast<vr_mapping::Action>(action)));
    FreeLibrary(pinned);
    if(!readable) { g_gestureBinding.observedMs.store(0,std::memory_order_release); return; }
    g_gestureBinding.sequence.fetch_add(1,std::memory_order_acq_rel);
    for(unsigned controller=0;controller<4;++controller)
    {
        if(g_gestureBinding.lastTransport[controller]!=transport[controller])
        {
            g_gestureBinding.lastTransport[controller]=transport[controller];
            LOG("Gesture melee %s controller %u: configured transport 0x%05X%s",title->displayName,
                controller,transport[controller],transport[controller] ? "" : " (unavailable or unsupported binding; no injection)");
        }
        const uint64_t value=(uint64_t(generation)<<32) | (uint64_t(title->title)<<24) | transport[controller];
        g_gestureBinding.value[controller].store(value,std::memory_order_release);
        for(unsigned action=0;action<vr_mapping::Count;++action)
            g_gestureBinding.actions[controller][action].store(
                (uint64_t(generation)<<32)|(uint64_t(title->title)<<24)|actions[action][controller],
                std::memory_order_release);
    }
    g_gestureBinding.observedMs.store(now,std::memory_order_release);
    g_gestureBinding.sequence.fetch_add(1,std::memory_order_release);
}

uint32_t ReadGestureMeleeTransport(GameTitle title,uint32_t generation,unsigned controller,uint64_t now) noexcept
{
    if(controller>=4) return 0;
    const uint64_t observed=g_gestureBinding.observedMs.load(std::memory_order_acquire);
    if(!observed || now<observed || now-observed>150) return 0;
    const uint64_t value=g_gestureBinding.value[controller].load(std::memory_order_acquire);
    if(uint32_t(value>>32)!=generation || ((value>>24)&0xFF)!=uint64_t(title)) return 0;
    return uint32_t(value)&0x3FFFF;
}
}

bool Game_ReadVrActionBindings(vr_mapping::Transports& out,uint64_t now,unsigned controller)
{
    out={};
    if(controller>=4) return false;
    const GameTitle title=TitleAdapter_GetActiveTitle();
    const uint32_t generation=TitleAdapter_GetGeneration(title);
    const uint64_t observed=g_gestureBinding.observedMs.load(std::memory_order_acquire);
    if(!observed||now<observed||now-observed>150) return false;
    const auto sequence=g_gestureBinding.sequence.load(std::memory_order_acquire);
    if(sequence&1) return false;
    for(unsigned action=0;action<vr_mapping::Count;++action)
    {
        const uint64_t value=g_gestureBinding.actions[controller][action].load(std::memory_order_acquire);
        if(uint32_t(value>>32)!=generation||((value>>24)&0xFF)!=uint64_t(title)) {out={};return false;}
        out[action]=uint32_t(value)&0x3FFFF;
    }
    // Missing individual actions remain unavailable without discarding the
    // other verified actions (Fire/Jump can legitimately be native-unbound).
    if(sequence!=g_gestureBinding.sequence.load(std::memory_order_acquire)) {out={};return false;}
    return true;
}

uint32_t Game_VrActionTransport(vr_mapping::Action action,uint64_t now)
{
    vr_mapping::Transports out{};
    return action<vr_mapping::Count&&Game_ReadVrActionBindings(out,now)?out[action]:0;
}
