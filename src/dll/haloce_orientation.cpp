#include "haloce_orientation.h"
#include "haloce_controls.h"
#include "haloce_stereo_core.h"
#include "haloce_native_bindings.h"
#include "hook_quiescence.h"
#include "title_adapter.h"
#include "../common/haloce_controls_logic.h"
#include "../common/haloce_audio_logic.h"
#if HALOMCCVR_DEV_DIAGNOSTICS
#include "../common/haloce_sound_listener_logic.h"   // the dev sound listener witness (below)
#endif
#include "../common/minhook_lifecycle.h"
#include "../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include <array>
#include <cstring>

namespace
{
using namespace halo_ce;
// HCEEK player_effects.c camera transform; independently matched to the native
// matrix composed into both CE camera records. See E-CE-ORIENTATION-1.
using EffectMatrix=std::array<float,13>;
constexpr EffectMatrix identityEffect{1,1,0,0,0,1,0,0,0,1,0,0,0};
using EffectFn=void(__fastcall*)(int16_t,EffectMatrix*);
using AudioFn=void(__fastcall*)(int32_t,const AudioListenerPacket*);
struct Feature
{
    void* target{};
    void* original{};
    bool enabled{};
    std::atomic<bool> ready{};
    uint32_t rejectedGeneration{};
    uintptr_t rejectedBase{};
    bool cleanupReported{};
};
Feature effect,audio;
HMODULE retained{};
uintptr_t moduleBase{};
std::atomic<bool> active{},retiring{};
std::atomic<uint32_t> generation{},callbacks{};
std::atomic<uint64_t> suppressed{},stockEffects{},exceptions{},declined{};
std::atomic<uint64_t> headListeners{},stockListeners{},audioDeclined{};
uint64_t lastReport{};
bool retirementReported{};
uintptr_t pinFailureBase{};
uint32_t pinFailureGeneration{};

bool Current() noexcept
{
    return active.load(std::memory_order_acquire)&&!retiring.load(std::memory_order_acquire)&&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load(std::memory_order_acquire);
}
bool Gameplay(RenderContext& context) noexcept
{
    HaloCELocalPlayerState state{};
    return Current()&&HaloCEControls_GetLocomotionFrame(state,context)&&
        state.generation==generation.load(std::memory_order_acquire)&&
        state.generation==context.tracking.generation&&Current()&&HaloCE_RenderContextCurrent(context);
}
bool WriteEffect(EffectMatrix* destination) noexcept
{
    if (!destination) return false;
    __try { *destination=identityEffect;return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void EffectBody(int16_t outputUser,EffectMatrix* matrix,uintptr_t caller)
{
    const auto original=reinterpret_cast<EffectFn>(effect.original);
    if (!original) return;
    // Let native timers, RNG, accumulated effects and interpolation advance.
    // Only the exact local camera's returned transform is replaced afterward.
    original(outputUser,matrix);
    RenderContext context{};
    if (caller==moduleBase+0xac461d&&outputUser==0&&
        effect.ready.load(std::memory_order_acquire)&&Gameplay(context)&&
        effect.ready.load(std::memory_order_acquire)&&HaloCE_RenderContextCurrent(context))
    {
        if (WriteEffect(matrix)) { suppressed.fetch_add(1,std::memory_order_relaxed);return; }
        declined.fetch_add(1,std::memory_order_relaxed);
    }
    stockEffects.fetch_add(1,std::memory_order_relaxed);
}
__declspec(noinline) void __fastcall EffectHook(int16_t outputUser,EffectMatrix* matrix)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { EffectBody(outputUser,matrix,caller); }
    __finally
    {
        if (AbnormalTermination()) exceptions.fetch_add(1,std::memory_order_relaxed);
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
bool ReadListener(const AudioListenerPacket* source,AudioListenerPacket& output) noexcept
{
    if (!source) return false;
    __try { output=*source;return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#if HALOMCCVR_DEV_DIAGNOSTICS
// THE SOUND LISTENER WITNESS, Halo CE (dev builds only; src/common/haloce_sound_listener_logic.h says what it
// measures and why). It adds no hook: it samples inside this file's own listener detour, for the producer's call
// for listener 0, and it never touches the packet that goes on to the backend. One sampler at a time (a flag
// that is tried, never waited for); a finished window crosses to the worker under a sequence lock. No
// allocation, no logging, no waiting here: the line is formatted in HaloCEOrientation_Poll.
struct ListenerWitness
{
    std::atomic<bool> sampling{};
    listener_witness::Window open;        // under `sampling`
    uint32_t lastThread{};                // under `sampling`
    uint64_t nextSerial{1},lastPacketMs{};  // under `sampling`
    std::atomic<uint32_t> version{},overlapped{};
    listener_witness::Window published;
    uint64_t reportedSerial{},lastWindowMs{};   // the worker only
};
ListenerWitness listenerWitness;
// `handed`: the private head packet when the hook replaces the game's, else null. `admitted`: the gameplay
// receipt the hook admitted with, or null (not on foot, not gameplay, or the adaptation declined): then the
// witness asks for the last gameplay context itself, so a seat is still held against the head.
void WitnessListener(const AudioListenerPacket* packet,const AudioListenerPacket* handed,
    const RenderContext* admitted) noexcept
{
    using namespace listener_witness;
    auto& w=listenerWitness;
    if (w.sampling.exchange(true,std::memory_order_acquire))
    { w.overlapped.fetch_add(1,std::memory_order_relaxed);return; }
    const uint64_t now=GetTickCount64();
    auto& open=w.open;
    const auto publish=[&w,&open](uint64_t closedMs)
    {
        open.closedMs=closedMs;open.serial=w.nextSerial++;
        w.version.fetch_add(1,std::memory_order_acq_rel);   // odd: being written
        w.published=open;
        w.version.fetch_add(1,std::memory_order_release);   // even: readable
        open=listener_witness::Window{};
    };
    // The packets stopped for a second with a window open: it ends where they stopped.
    if (open.openedMs&&w.lastPacketMs&&now-w.lastPacketMs>1000) publish(w.lastPacketMs);
    w.lastPacketMs=now;
    if (!open.openedMs) open.openedMs=now;
    const uint32_t thread=GetCurrentThreadId();
    if (w.lastThread&&w.lastThread!=thread) ++open.threadChanges;
    w.lastThread=thread;open.thread=thread;
    AudioListenerPacket native{};
    if (!ReadListener(packet,native)) ++open.refused[static_cast<size_t>(Refusal::Unreadable)];
    else
    {
        RenderContext own{};const RenderContext* context=admitted;
        if (!context&&Current()&&HaloCE_GetGameplayContext(own)) context=&own;
        WitnessPacket(open,native,handed,context);
    }
    if (now-open.openedMs>=kWindowMs) publish(now);
    w.sampling.store(false,std::memory_order_release);
}
// The worker, with this file's two-second report.
void ReportListenerWitness(uint64_t now) noexcept
{
    using namespace listener_witness;
    auto& w=listenerWitness;
    listener_witness::Window window;bool have=false;
    for (int attempt=0;attempt<4&&!have;++attempt)
    {
        const uint32_t before=w.version.load(std::memory_order_acquire);
        if (before&1u) continue;
        window=w.published;
        have=w.version.load(std::memory_order_acquire)==before;
    }
    if (!have||!window.serial||window.serial==w.reportedSerial)
    {
        // No window closed since the last report: ordinary once (the two clocks are not in step); after three
        // seconds no listener packet is reaching the hook at all.
        if (!w.lastWindowMs) w.lastWindowMs=now;
        if (now-w.lastWindowMs>=3000)
            LOG("SOUND LISTENER Halo CE (dev witness): NO SAMPLES: no listener packet for listener 0 reached the "
                "backend hook in the last %llu ms (the listener hook is %s; a menu, a loading screen, or the sound "
                "manager is not running)",static_cast<unsigned long long>(now-w.lastWindowMs),
                audio.ready.load()?"installed":"NOT installed: the game's own listener is what it hears");
        return;
    }
    w.reportedSerial=window.serial;w.lastWindowMs=now;
    window.refused[static_cast<size_t>(Refusal::Overlapped)]+=w.overlapped.exchange(0,std::memory_order_relaxed);
    char refusals[512],headMissing[256];
    RefusalText(window,refusals,sizeof(refusals));
    HeadMissingText(window,headMissing,sizeof(headMissing));
    const Measure& hf=window[M::HeadForward];const Measure& hu=window[M::HeadUp];
    const Measure& hd=window[M::HeadDistance];const Measure& af=window[M::AimForward];
    const Measure& cf=window[M::CameraForward];const Measure& cu=window[M::CameraUp];
    const Measure& cd=window[M::CameraDistance];const Measure& apart=window[M::HeadAimApart];
    const Measure& xf=window[M::HandedHeadForward];const Measure& xu=window[M::HandedHeadUp];
    const char* handed=!xf.count?"NO HEAD TO HOLD IT AGAINST":
        Follows(xf,&xu,kFollowsDeg)?"ORIENTED BY THE HEAD":"NOT ORIENTED BY THE HEAD";
    LOG("SOUND LISTENER Halo CE (dev witness): HANDED TO THE SOUND BACKEND: %s, %s | the GAME'S OWN listener: %s "
        "| handed vs HEAD: forward mean %.2f max %.2f deg, up mean %.2f max %.2f deg (n=%u) | the game's own vs "
        "HEAD: forward mean %.2f max %.2f deg, up mean %.2f max %.2f deg, position mean %.4f max %.4f wu (n=%u; "
        "the hook never changes the position) | the game's own vs the HAND'S AIM: forward mean %.2f max %.2f deg "
        "(n=%u) | the game's own vs THE GAME'S OWN CAMERA (the stock centre camera): forward mean %.2f max %.2f "
        "deg, up mean %.2f max %.2f deg, position mean %.4f max %.4f wu (n=%u) | head and aim apart: mean %.2f max "
        "%.2f deg (n=%u) | packets: %u taken (%u handed on with the head's orientation by this mod's listener "
        "hook, %u handed on as the game built them), %u refused (%s); of the taken, no usable head on %u (%s), no "
        "hand aim on %u; seat frames %u | sampled in the listener hook (the backend entry, the producer's call for "
        "listener 0) on thread %u (%u thread changes), window %llu ms | matrix: the packet's scale +0x48, forward "
        "+0x4C, left +0x58 = up x forward, up +0x64, position +0x70 (E-CE-ORIENTATION-2); each sample's rows were "
        "a rotation of scale 1",
        handed,PlaceName(Position(window)),VerdictName(Orientation(window)),xf.Mean(),xf.max,xu.Mean(),xu.max,
        xf.count,hf.Mean(),hf.max,hu.Mean(),hu.max,hd.Mean(),hd.max,hf.count,af.Mean(),af.max,af.count,cf.Mean(),
        cf.max,cu.Mean(),cu.max,cd.Mean(),cd.max,cf.count,apart.Mean(),apart.max,apart.count,window.taken,
        window.handedHead,window.handedStock,window.Refused(),refusals,window.HeadMissing(),headMissing,
        window.aimMissing,window.seated,window.thread,window.threadChanges,
        static_cast<unsigned long long>(window.closedMs-window.openedMs));
}
#endif
void AudioBody(int32_t listener,const AudioListenerPacket* packet,uintptr_t caller)
{
    const auto original=reinterpret_cast<AudioFn>(audio.original);
    if (!original) return;
    RenderContext context{};AudioListenerPacket source{},adapted{};
    if (caller==moduleBase+0xb4cefe&&listener==0&&
        audio.ready.load(std::memory_order_acquire)&&Gameplay(context))
    {
        if (ReadListener(packet,source)&&BuildHeadListener(context,source,adapted)&&
            audio.ready.load(std::memory_order_acquire)&&Current()&&HaloCE_RenderContextCurrent(context))
        {
#if HALOMCCVR_DEV_DIAGNOSTICS
            WitnessListener(packet,&adapted,&context);   // dev: reads only
#endif
            // The verified backend consumes its argument synchronously. A private
            // copy keeps the native observer and sound-manager globals untouched.
            original(listener,&adapted);headListeners.fetch_add(1,std::memory_order_relaxed);return;
        }
        audioDeclined.fetch_add(1,std::memory_order_relaxed);
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Dev: the producer's call for listener 0 that goes on as the game built it (a seat, a cinematic, a menu,
    // a declined adaptation). Reads only.
    if (caller==moduleBase+0xb4cefe&&listener==0) WitnessListener(packet,nullptr,nullptr);
#endif
    original(listener,packet);stockListeners.fetch_add(1,std::memory_order_relaxed);
}
__declspec(noinline) void __fastcall AudioHook(int32_t listener,const AudioListenerPacket* packet)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { AudioBody(listener,packet,caller); }
    __finally
    {
        if (AbnormalTermination()) exceptions.fetch_add(1,std::memory_order_relaxed);
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
bool RemoveFeature(Feature& feature,const void* hook) noexcept
{
    feature.ready=false;
    if (feature.enabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(feature.target);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return false;
        feature.enabled=false;
    }
    const void* functions[]{hook};
    const void* originals[]{feature.original};
    if (feature.target&&!WaitForNativeDetourQuiescence(functions,originals,1,callbacks)) return false;
    if (feature.target)
    {
        const auto result=MH_RemoveHook(feature.target);
        if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return false;
    }
    if (callbacks.load(std::memory_order_acquire)) return false;
    feature.target=feature.original=nullptr;return true;
}
bool Remove() noexcept
{
    active=false;retiring=true;effect.ready=false;audio.ready=false;
    const bool effectRemoved=RemoveFeature(effect,reinterpret_cast<const void*>(&EffectHook));
    const bool audioRemoved=RemoveFeature(audio,reinterpret_cast<const void*>(&AudioHook));
    if (!effectRemoved||!audioRemoved) return false;
    if (retained) { FreeLibrary(retained);retained=nullptr; }
    moduleBase=0;generation=0;retiring=false;return true;
}
bool InstallFeature(Feature& feature,size_t size,const NativeContractSet& contracts,
    uintptr_t rva,void* hook,const char* name) noexcept
{
    const char* failure{};
    if (!VerifyNativeFeatureBindings(moduleBase,size,generation.load(),contracts,failure))
    { LOG("CE %s stock fallback: %s",name,failure?failure:"binding failure");return false; }
    void* target=reinterpret_cast<void*>(moduleBase+rva);
    auto result=MH_CreateHook(target,hook,&feature.original);
    if (result!=MH_OK)
    { LOG("CE %s stock fallback: create status %d",name,result);return false; }
    feature.target=target;
    result=MH_EnableHook(target);
    if (result!=MH_OK)
    { LOG("CE %s stock fallback: enable status %d",name,result);return false; }
    feature.enabled=true;feature.ready=true;
    LOG("CE %s installed",name);return true;
}
}

bool HaloCEOrientation_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive) noexcept
{
    if (retained&&(!isActive||base!=moduleBase||gen!=generation.load()||retiring.load()))
    {
        if (!Remove())
        {
            if (!retirementReported)
            {
                LOG("CE orientation retirement pending: optional callbacks disabled for admission; module/trampolines retained until disable, quiescence and removal succeed");
                retirementReported=true;
            }
            return false;
        }
        retirementReported=false;
    }
    active.store(isActive,std::memory_order_release);
    if (!isActive||!base||!gen) return false;
    if (!retained)
    {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(base),&retained))
        {
            if (pinFailureBase!=base||pinFailureGeneration!=gen)
            {
                LOG("CE orientation stock fallback: module pin failed gen=%u error=%lu",gen,GetLastError());
                pinFailureBase=base;pinFailureGeneration=gen;
            }
            return false;
        }
        pinFailureBase=0;pinFailureGeneration=0;
        moduleBase=base;generation=gen;retiring=false;
    }
    auto pollFeature=[&](Feature& feature,const void* hook,const NativeContractSet& contracts,
        uintptr_t rva,const char* name)
    {
        if (feature.ready.load()) return;
        // A failed enable still owns a trampoline. Finish only this feature's
        // cleanup before retrying; the healthy sibling remains installed.
        if (feature.target&&!RemoveFeature(feature,hook))
        {
            if (!feature.cleanupReported)
            {
                LOG("CE %s stock fallback: partial installation cleanup pending; sibling feature remains independent",name);
                feature.cleanupReported=true;
            }
            return;
        }
        feature.cleanupReported=false;
        if (gen==feature.rejectedGeneration&&base==feature.rejectedBase) return;
        if (!InstallFeature(feature,size,contracts,rva,const_cast<void*>(hook),name))
        { feature.rejectedGeneration=gen;feature.rejectedBase=base; }
    };
    pollFeature(effect,reinterpret_cast<const void*>(&EffectHook),
        {contract::camera_effect::entries,contract::camera_effect::witnesses,
            contract::camera_effect::relatives,contract::camera_effect::pointers},
        contract::camera_effect::camera_effect_transform,
        "camera recoil/shake: native state advances; tracked local camera consumes identity effect");
    pollFeature(audio,reinterpret_cast<const void*>(&AudioHook),
        {contract::audio_listener::entries,contract::audio_listener::witnesses,
            contract::audio_listener::relatives,contract::audio_listener::pointers},
        contract::audio_listener::audio_listener_submit,
        "headset audio orientation: private native listener packet; native position/velocity/environment retained");
    const uint64_t now=GetTickCount64();
    if (now-lastReport>=2000)
    {
        lastReport=now;
        LOG("CE orientation gen=%u effect=%d suppressed=%llu stock=%llu declined=%llu audio=%d head=%llu stockAudio=%llu declinedAudio=%llu exceptions=%llu",
            gen,effect.ready.load(),suppressed.load(),stockEffects.load(),declined.load(),
            audio.ready.load(),headListeners.load(),stockListeners.load(),audioDeclined.load(),exceptions.load());
#if HALOMCCVR_DEV_DIAGNOSTICS
        ReportListenerWitness(now);
#endif
    }
    return Current()&&(effect.ready.load()||audio.ready.load());
}
