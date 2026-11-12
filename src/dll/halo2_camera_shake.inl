// Halo 2's camera shake out of the headset view (halo2_camera_shake_logic.h; halo2_camera_shake = 1 keeps it).
// Optional and failure-isolated: a module whose bytes differ keeps the game's shake and every other path.
#include "../common/halo2_camera_shake_logic.h"
namespace h2_shake {
namespace proof=halo2_camera_shake;
using ShakeFn=void(__fastcall*)(int32_t,void*);
struct Hook {void* target{};void* original{};};
Hook animationHook,screenHook;
std::atomic<bool> enabled{};
std::atomic<uint32_t> callbacks{};
// Frames whose matrix the routine moved: shake undone, or kept because the player wants it.
std::atomic<uint64_t> kept{},undone{};
// Runs the game's own routine, then puts the matrix it was handed back unless the player wants the shake. Hot path:
// a 52-byte copy each way, no allocation, no logging.
void Run(const Hook& hook,int32_t user,void* matrix) noexcept {
    const auto original=reinterpret_cast<ShakeFn>(hook.original);
    if(!original)return;
    if(!matrix) {original(user,matrix);return;}
    uint8_t before[proof::kMatrixBytes];
    std::memcpy(before,matrix,sizeof(before));
    original(user,matrix);
    if(std::memcmp(before,matrix,sizeof(before))==0)return;
    if(g_config.halo2_camera_shake) {kept.fetch_add(1,std::memory_order_relaxed);return;}
    std::memcpy(matrix,before,sizeof(before));
    undone.fetch_add(1,std::memory_order_relaxed);
}
void __fastcall AnimationCamera(int32_t user,void* matrix) {
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    Run(animationHook,user,matrix);
    callbacks.fetch_sub(1,std::memory_order_acq_rel);
}
void __fastcall ScreenShake(int32_t user,void* matrix) {
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    Run(screenHook,user,matrix);
    callbacks.fetch_sub(1,std::memory_order_acq_rel);
}
} // namespace h2_shake

bool RemoveHalo2CameraShake() {
    using namespace h2_shake;
    enabled=false;
    Hook* hooks[]{&animationHook,&screenHook};
    for(auto* hook:hooks)if(hook->target) {
        const auto status=MCCVR_DisableHookForRetirement(hook->target);
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED&&status!=MH_ERROR_NOT_CREATED)return false;
    }
    const void* functions[]{reinterpret_cast<void*>(&AnimationCamera),reinterpret_cast<void*>(&ScreenShake)};
    const void* originals[]{animationHook.original,screenHook.original};
    if(!WaitForNativeDetourQuiescence(functions,originals,2,callbacks))return false;
    for(auto* hook:hooks)if(hook->target) {
        const auto status=MH_RemoveHook(hook->target);
        if(status!=MH_OK&&status!=MH_ERROR_NOT_CREATED)return false;
        *hook={};
    }
    return true;
}
bool InstallHalo2CameraShake(uintptr_t module,size_t size) {
    using namespace h2_shake;
    if(enabled.load())return true;
    if(animationHook.target||screenHook.target)
    {LOG("Halo 2 camera shake CleanupRequired: prior hooks retained");return false;}
    struct Binding {const char* pattern;uint32_t rva;};
    const Binding bindings[]{{proof::kAnimationCameraPattern,proof::kAnimationCameraRva},
        {proof::kScreenShakePattern,proof::kScreenShakeRva},{proof::kCallSitesPattern,proof::kCallSitesRva}};
    for(const auto& binding:bindings) {
        uintptr_t found{};uint32_t count{};
        if(!CountPatternMatches(module,size,binding.pattern,found,count)||count!=1||found!=module+binding.rva)
        {LOG("Halo 2 camera shake StockFallback: +%X missing or ambiguous; the game's shake stays",binding.rva);return false;}
    }
    // The observer transform still calls exactly these two.
    struct Edge {uint32_t call,target;};
    const Edge edges[]{{proof::kCallSitesRva+proof::kAnimationCameraCallOffset,proof::kAnimationCameraRva},
        {proof::kCallSitesRva+proof::kScreenShakeCallOffset,proof::kScreenShakeRva}};
    for(const auto& edge:edges) {
        int32_t rel{};std::memcpy(&rel,reinterpret_cast<const void*>(module+edge.call+1),sizeof(rel));
        if(*reinterpret_cast<const uint8_t*>(module+edge.call)!=0xE8||
            module+edge.call+5+static_cast<intptr_t>(rel)!=module+edge.target)
        {LOG("Halo 2 camera shake StockFallback: the call +%X changed; the game's shake stays",edge.call);return false;}
    }
    struct Entry {Hook* hook;uint32_t rva;void* detour;};
    const Entry entries[]{{&animationHook,proof::kAnimationCameraRva,reinterpret_cast<void*>(&AnimationCamera)},
        {&screenHook,proof::kScreenShakeRva,reinterpret_cast<void*>(&ScreenShake)}};
    for(const auto& entry:entries) {
        void* target=reinterpret_cast<void*>(module+entry.rva);
        if(MH_CreateHook(target,entry.detour,&entry.hook->original)!=MH_OK) {
            LOG("Halo 2 camera shake StockFallback: create +%X failed",entry.rva);
            (void)RemoveHalo2CameraShake();return false;
        }
        entry.hook->target=target;
    }
    for(const auto& entry:entries)if(MH_EnableHook(entry.hook->target)!=MH_OK) {
        LOG("Halo 2 camera shake StockFallback: enable +%X failed",entry.rva);
        (void)RemoveHalo2CameraShake();return false;
    }
    enabled=true;
    LOG("Halo 2 camera shake Installed: the weapon animation's camera +0x%X and the screen shake +0x%X run as the "
        "game's, then their shake is undone unless halo2_camera_shake = 1 (now %d)",
        proof::kAnimationCameraRva,proof::kScreenShakeRva,g_config.halo2_camera_shake?1:0);
    return true;
}
void ReportHalo2CameraShake() {
    using namespace h2_shake;
    const uint64_t undoneNow=undone.exchange(0),keptNow=kept.exchange(0);
    if(undoneNow||keptNow)
        LOG("Halo 2 camera shake: %llu shaken passes undone, %llu kept (halo2_camera_shake = %d)",
            static_cast<unsigned long long>(undoneNow),static_cast<unsigned long long>(keptNow),
            g_config.halo2_camera_shake?1:0);
}
