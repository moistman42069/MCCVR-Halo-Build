// Executes the shipping XInput -> ownership policy -> OpenXR haptic functions.
// Only the clock, menu/session state, and OpenXR endpoint are simulated. The
// title registry, runtime publication/resolution and amplitude helpers are real.
// No MCC process, controller driver, or OpenXR session is opened.
#include <Windows.h>
#include <Xinput.h>
#include <openxr/openxr.h>
#include "../src/common/config.h"
#include "../src/common/input_logic.h"
#include "../src/common/weapon_haptic_pulses.h"
#include "../src/dll/title_adapter.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <limits>
#include <thread>
#include <vector>

bool VR_PulseWeaponHaptics(GameTitle,uint32_t,bool,bool,float,uint64_t=0) noexcept;

namespace
{
unsigned checks{},failures{};
uint64_t testNow=10000;
bool testMenu{};
GameTitle testTitle=GameTitle::HaloCE;
RuntimeMode testMode=RuntimeMode::Gameplay;
std::unique_ptr<TitleRuntimeState> testRuntime=std::make_unique<TitleRuntimeState>();
XrSession g_session=reinterpret_cast<XrSession>(uintptr_t{1});
XrAction g_hapticAction=reinterpret_cast<XrAction>(uintptr_t{2});
XrPath g_leftHandPath=11,g_rightHandPath=22;
XrSessionState g_sessionState=XR_SESSION_STATE_FOCUSED;
std::atomic<bool> g_capturedLeftHanded{};
std::atomic<float> g_requestedHaptics{},g_peakHaptics{},g_contactHaptics[2]{};
WeaponHapticPulses g_weaponHaptics;
WeaponHapticAdmission g_weaponHapticAdmission;
bool testTwoHand{};
struct Output { XrPath path{};float amplitude{};XrDuration duration{};float frequency{};bool stop{}; };
std::vector<Output> outputs;
void Check(bool ok,const char* message)
{
    ++checks;
    if (!ok) { ++failures;std::fprintf(stderr,"FAIL: %s\n",message); }
}
bool Near(float a,float b) { return std::fabs(a-b)<0.00001f; }
uint64_t TestGetTickCount64() { return testNow; }
TitleAdapterRuntimeSnapshot RuntimeSnapshot(uint64_t now)
{ return {testRuntime->Resolve(now,MakeTitleRuntimeHeartbeatPolicy()),false}; }
}

RuntimeMode TitleAdapter_GetRuntimeMode() { return testMode; }
GameTitle TitleAdapter_GetActiveTitle() { return testTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle title) { return testRuntime->Generation(title); }
bool VR_IsTwoHandAiming() { return testTwoHand; }
bool Menu_IsOpen() { return testMenu; }
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(
    XrSession session,const XrHapticActionInfo* info,const XrHapticBaseHeader* feedback)
{
    Check(session==g_session&&info&&info->action==g_hapticAction&&feedback&&
        feedback->type==XR_TYPE_HAPTIC_VIBRATION,"OpenXR output has the actual session/action/vibration type");
    const auto& pulse=*reinterpret_cast<const XrHapticVibration*>(feedback);
    outputs.push_back({info->subactionPath,pulse.amplitude,pulse.duration,pulse.frequency,false});
    return XR_SUCCESS;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(
    XrSession session,const XrHapticActionInfo* info)
{
    Check(session==g_session&&info&&info->action==g_hapticAction,"OpenXR stop has the actual session/action");
    outputs.push_back({info->subactionPath,0,0,0,true});
    return XR_SUCCESS;
}

#define GetTickCount64 TestGetTickCount64
#include "haptics_runtime_functions.inl"
#undef GetTickCount64

namespace
{
uint32_t PublishedCapabilities(GameTitle title)
{ return title==GameTitle::HaloCE?kCePublishedCapabilities:kHalo3RuntimeCapabilities; }
void Publish(bool armed=true,bool retiring=false)
{
    ++testNow; // Runtime publication deliberately refuses repeated heartbeats.
    const auto gen=testRuntime->Generation(testTitle);
    Check(testRuntime->PublishLifecycle(testTitle,gen,
        {true,armed,retiring,PublishedCapabilities(testTitle)}),"Publish current title lifecycle");
    Check(testRuntime->PublishMode(testTitle,gen,testMode)==!retiring,
        "Runtime mode publication respects current title retirement");
    Check(testRuntime->PublishHeartbeat(testTitle,gen,testNow-1),"Publish current camera heartbeat");
}
void Begin(GameTitle title=GameTitle::HaloCE)
{
    testRuntime=std::make_unique<TitleRuntimeState>();
    testMode=RuntimeMode::Shell;
    ApplyControllerHaptics(false, false); // Also resets the production function's static active flags.
    outputs.clear();testNow+=1000;testTitle=title;testMenu=false;testTwoHand=false;
    g_sessionState=XR_SESSION_STATE_FOCUSED;g_capturedLeftHanded=false;
    g_config.haptic_intensity=0.86f;
    TitleRuntimeModuleSet modules{};
    modules.availabilityMask=TitleRuntimeAvailabilityBit(title);
    modules.moduleBases[TitleRuntimeSlotIndex(title)]=0x100000;
    Check(testRuntime->PublishModuleSet(modules,testNow-100),"Publish actual title module generation");
    testMode=RuntimeMode::Gameplay;Publish();
}
DWORD Send(WORD low,WORD high,DWORD user=0)
{
    XINPUT_VIBRATION vibration{low,high};
    return ProcessSetState(ERROR_DEVICE_NOT_CONNECTED,user,&vibration);
}
void Frame(uint64_t elapsed=41,bool tracking=true,bool refresh=true)
{
    testNow+=elapsed;
    if (refresh) testRuntime->PublishHeartbeat(testTitle,testRuntime->Generation(testTitle),testNow-1);
    ApplyControllerHaptics(tracking, tracking);
}
void Pair(float expected,const char* message)
{
    Check(outputs.size()==2&&!outputs[0].stop&&!outputs[1].stop&&
        outputs[0].path==g_leftHandPath&&outputs[1].path==g_rightHandPath&&
        Near(outputs[0].amplitude,expected)&&Near(outputs[1].amplitude,expected)&&
        outputs[0].duration==50000000&&outputs[1].duration==50000000&&
        outputs[0].frequency==XR_FREQUENCY_UNSPECIFIED&&
        outputs[1].frequency==XR_FREQUENCY_UNSPECIFIED,message);
}
bool HasApply()
{ return std::any_of(outputs.begin(),outputs.end(),[](const Output& value){return !value.stop;}); }
void StoppedPair(const char* message)
{
    Check(outputs.size()==2&&outputs[0].stop&&outputs[1].stop&&
        outputs[0].path==g_leftHandPath&&outputs[1].path==g_rightHandPath,message);
}
void TestGunRumble(GameTitle title)
{
    Begin(title);
    const auto* descriptor=TitleRegistry_Find(title);
    Check(descriptor&&(descriptor->capabilities&TitleCapability_Haptics),
        "Title descriptor admits the common haptic output");
    Check(Game_HasTitleCapability(TitleCapability_Haptics),
        "Actual armed title publication admits game and contact haptics");
    Check(Send(65535,0)==ERROR_SUCCESS,"Virtual slot zero remains connected with no physical gamepad");
    Frame();Pair(0.65f*0.86f,"Low motor gun rumble reaches both controllers at shared intensity");
    Begin(title);Send(0,65535);Frame();Pair(0.35f*0.86f,"High motor gun rumble reaches both controllers");
    Begin(title);Send(65535,65535);Frame();Pair(0.86f,"Both gun motor bands blend to full amplitude");
    outputs.clear();Frame(20);Check(outputs.empty(),"Held rumble respects the 40 ms OpenXR reapply interval");
    Frame(20);Pair(0.86f,"Held rumble is renewed after the interval");
    Send(0,0);Frame(); // The last carried held peak may be consumed once before the zero sample.
    outputs.clear();Frame();StoppedPair("A zero motor update stops both controllers");
    outputs.clear();Send(65535,0);Send(0,0);Frame();
    Pair(0.65f*0.86f,"A gunshot that starts and stops between VR frames retains its peak on both hands");
    outputs.clear();Frame();StoppedPair("A captured short gunshot stops after one applied pulse");
}
void TestIntensityAndContacts()
{
    Begin();Send(65535,65535);g_config.haptic_intensity=0;Frame();
    Check(!HasApply(),"Controller vibration zero disables both game and contact output");
    Begin();Send(65535,65535);g_config.haptic_intensity=2;Frame();Pair(1,"Intensity above one clamps safely");
    Begin();Send(65535,65535);g_config.haptic_intensity=-1;Frame();Check(!HasApply(),"Negative intensity clamps off");
    Begin();VR_PulseContactHaptics(true,0.18f);Frame();
    Check(outputs.size()==1&&outputs[0].path==g_leftHandPath&&Near(outputs[0].amplitude,0.18f*0.86f),
        "CE left-hand contact pulse reaches the left controller");
    Begin();VR_PulseContactHaptics(false,0.35f);Frame();
    Check(outputs.size()==1&&outputs[0].path==g_rightHandPath&&Near(outputs[0].amplitude,0.35f*0.86f),
        "CE right-hand contact pulse reaches the right controller");
    Begin();g_capturedLeftHanded=true;VR_PulseContactHaptics(true,0.18f);Frame();
    Check(outputs.size()==1&&outputs[0].path==g_rightHandPath,"Left-handed role mapping preserves actual physical hand output");
    Begin();Send(65535,0);VR_PulseContactHaptics(false,0.2f);Frame();
    Pair(0.65f*0.86f,"Contact feedback cannot suppress the game's stronger gun rumble");
    Begin();Send(0,65535);VR_PulseContactHaptics(true,0.8f);Frame();
    Check(outputs.size()==2&&Near(outputs[0].amplitude,0.8f*0.86f)&&Near(outputs[1].amplitude,0.35f*0.86f),
        "Contact merges independently per hand without replacing game feedback");
}
void TestStopsAndOwnership()
{
    const RuntimeMode forbidden[]{RuntimeMode::Shell,RuntimeMode::Loading,RuntimeMode::Paused,
        RuntimeMode::Cutscene,RuntimeMode::Dead,RuntimeMode::Unsupported};
    for (auto mode:forbidden)
    {
        Begin();Send(65535,65535);Frame();outputs.clear();testMode=mode;Frame();
        StoppedPair("Leaving gameplay stops both controllers for each disallowed runtime mode");
    }
    for (auto mode:{RuntimeMode::Vehicle,RuntimeMode::Turret})
    { Begin();testMode=mode;Send(65535,65535);Frame();Pair(0.86f,"Vehicle and turret modes preserve native rumble"); }
    Begin();Send(65535,65535);Frame();outputs.clear();testMenu=true;Frame();
    StoppedPair("Opening the VR menu stops both controllers");
    Begin();Send(65535,65535);Frame();outputs.clear();Frame(41,false);
    StoppedPair("Lost tracking stops both controllers");
    Begin();Send(65535,65535);Frame();outputs.clear();g_sessionState=XR_SESSION_STATE_VISIBLE;Frame();
    StoppedPair("Lost session focus stops both controllers");
    Begin();VR_PulseContactHaptics(true,0.8f);testMenu=true;Frame();testMenu=false;outputs.clear();Frame();
    Check(!HasApply(),"A contact pulse raised under the menu never replays after closing it");
    Begin();Send(65535,65535);Frame();outputs.clear();Publish(false);Frame();
    StoppedPair("An unarmed camera owner immediately stops both controllers");
    Check(Send(65535,65535)==ERROR_SUCCESS&&!g_requestedHaptics.load(),
        "Unarmed title policy clears rumble while preserving virtual gamepad connection");
    Publish();outputs.clear();Frame();Check(!HasApply(),"Rearming cannot replay a prior title's rumble");
    Send(65535,65535);outputs.clear();Frame();Pair(0.86f,"A fresh gunshot works after rearming");
    Begin();Send(65535,65535);Frame();outputs.clear();Publish(true,true);Frame();
    StoppedPair("Retiring title ownership stops both controllers");
    Begin();Send(65535,65535);Frame();outputs.clear();Frame(500,true,false);
    StoppedPair("An expired title heartbeat stops both controllers");
    Begin();Check(Send(65535,65535,1)==ERROR_DEVICE_NOT_CONNECTED,
        "Another physical controller slot keeps its native result");Frame();Check(!HasApply(),"Other slots cannot drive virtual-controller haptics");
    Check(ProcessSetState(ERROR_DEVICE_NOT_CONNECTED,0,nullptr)==ERROR_DEVICE_NOT_CONNECTED,
        "Null vibration requests preserve their native result");
}
void TestIndependentTracking()
{
    Begin();VR_SetGameHaptics(std::numeric_limits<float>::quiet_NaN());
    VR_PulseContactHaptics(false,std::numeric_limits<float>::infinity());Frame();
    Check(!HasApply()&&std::isfinite(g_requestedHaptics.load()),
        "Nonfinite requests cannot reach OpenXR or poison the sustained request");
    Begin();Send(65535,65535);g_config.haptic_intensity=std::numeric_limits<float>::quiet_NaN();Frame();
    Check(!HasApply(),"Nonfinite saved intensity cannot reach OpenXR");
    Begin();Send(65535,65535);VR_PulseContactHaptics(false,.9f);
    g_config.haptic_intensity=0;Frame();g_config.haptic_intensity=.86f;outputs.clear();Frame();
    Check(!HasApply(),"Re-enabling vibration does not replay feedback accumulated while disabled");
    for (bool leftHanded : {false, true})
    {
        Begin();g_capturedLeftHanded=leftHanded;
        VR_PulseContactHaptics(false,.6f);
        ApplyControllerHaptics(true,false);
        Check(outputs.size()==1&&!outputs[0].stop&&
            outputs[0].path==(leftHanded?g_leftHandPath:g_rightHandPath),
            "A tracked primary receives contact feedback without a tracked support controller");
        outputs.clear();testNow+=1;
        VR_PulseContactHaptics(false,.9f);
        ApplyControllerHaptics(false,true);
        Check(outputs.size()==1&&outputs[0].stop&&
            outputs[0].path==(leftHanded?g_leftHandPath:g_rightHandPath),
            "Tracking loss stops the physical primary immediately inside the reapply throttle");
        outputs.clear();testNow+=41;
        ApplyControllerHaptics(true,true);
        Check(!HasApply(),"Lost-hand contact pulses cannot replay after tracking recovery");
        Begin();g_capturedLeftHanded=leftHanded;
        VR_PulseContactHaptics(true,.4f);
        ApplyControllerHaptics(false,true);
        Check(outputs.size()==1&&!outputs[0].stop&&
            outputs[0].path==(leftHanded?g_rightHandPath:g_leftHandPath),
            "A tracked support receives feedback independently of primary tracking");
    }
    Begin();Send(65535,65535);testMenu=true;Frame();testMenu=false;
    outputs.clear();Frame();
    Check(!HasApply(),"Blocked game rumble does not replay after closing the menu");
    Begin();Send(65535,65535);g_sessionState=XR_SESSION_STATE_VISIBLE;Frame();
    g_sessionState=XR_SESSION_STATE_FOCUSED;outputs.clear();Frame();
    Check(!HasApply(),"Blocked game rumble does not replay after focus returns");
}
void TestWeaponRouting()
{
    for(bool leftHanded:{false,true})
    {
        Begin();Frame();outputs.clear();g_capturedLeftHanded=leftHanded;
        const auto generation=TitleAdapter_GetGeneration(testTitle);
        Check(VR_PulseWeaponHaptics(testTitle,generation,false,false,.7f),"Accept current primary recoil");
        Frame();
        Check(outputs.size()==1&&!outputs[0].stop&&
            outputs[0].path==(leftHanded?g_leftHandPath:g_rightHandPath)&&Near(outputs[0].amplitude,.7f*.86f),
            "One-handed recoil reaches only its holding physical controller");
        Frame();outputs.clear();
        VR_PulseWeaponHaptics(testTitle,generation,true,false,.4f);Frame();
        Check(outputs.size()==1&&!outputs[0].stop&&
            outputs[0].path==(leftHanded?g_rightHandPath:g_leftHandPath),
            "Secondary weapon recoil independently reaches the other physical controller");
        Frame();outputs.clear();testTwoHand=true;
        VR_PulseWeaponHaptics(testTitle,generation,false,true,.6f);Frame();
        Check(outputs.size()==2&&!outputs[0].stop&&!outputs[1].stop,
            "An engaged support grip shares primary recoil");
        Frame();outputs.clear();
        VR_PulseWeaponHaptics(testTitle,generation,false,true,.6f);testTwoHand=false;Frame();
        Check(outputs.size()==1&&!outputs[0].stop&&
            outputs[0].path==(leftHanded?g_leftHandPath:g_rightHandPath),
            "Releasing support before delivery prevents queued coupled recoil on that hand");
        Frame();outputs.clear();
        Check(!VR_PulseWeaponHaptics(GameTitle::Halo4,generation,false,false,1)&&
            !VR_PulseWeaponHaptics(testTitle,generation+1,false,false,1),
            "A foreign title or generation cannot publish recoil");
        Frame();Check(!HasApply(),"Rejected recoil cannot leak into the current title");
        testTwoHand=true;VR_PulseWeaponHaptics(testTitle,generation,false,true,.6f);Frame();
        outputs.clear();testTwoHand=false;VR_PulseWeaponHaptics(testTitle,generation,false,false,.6f);Frame(1);
        Check(outputs.size()==1&&outputs[0].stop&&
            outputs[0].path==(leftHanded?g_rightHandPath:g_leftHandPath),
            "Support release stops that hand immediately inside the reapply throttle");
        Frame();outputs.clear();VR_PulseWeaponHaptics(testTitle,generation,false,false,1);
        Frame(101);Check(!HasApply(),"A weapon pulse expires if the XR consumer misses its delivery window");
        outputs.clear();VR_PulseWeaponHaptics(testTitle,generation,false,false,.2f);Frame();
        Check(outputs.size()==1&&Near(outputs[0].amplitude,.2f*.86f),
            "A fresh weak pulse cannot inherit the expired strong peak");
    }
}
void TestWeaponCancellationTokens()
{
    Begin();Frame();const auto gen=testRuntime->Generation(testTitle);
    const auto primary=VR_WeaponHapticToken(testTitle,gen,false);
    const auto secondary=VR_WeaponHapticToken(testTitle,gen,true);
    Check(primary&&secondary,"tracked focused hands expose native envelope tokens");
    ApplyControllerHaptics(true,false);
    Check(VR_WeaponHapticToken(testTitle,gen,false)==primary&&
        VR_WeaponHapticToken(testTitle,gen,true)==0,"support tracking loss preserves primary voice token");
    Frame();Check(VR_WeaponHapticToken(testTitle,gen,true)!=secondary,
        "returning support cannot resume an old secondary weapon envelope");
    testMenu=true;Frame();Check(!VR_WeaponHapticToken(testTitle,gen,false),"menu invalidates native recoil admission");
    testMenu=false;Frame();const auto resumed=VR_WeaponHapticToken(testTitle,gen,false);
    Check(resumed&&resumed!=primary&&!VR_PulseWeaponHaptics(testTitle,gen,false,false,.8f,primary),
        "late pulse from a cancelled native voice is rejected after menu recovery");
    Check(VR_PulseWeaponHaptics(testTitle,gen,false,false,.8f,resumed),"fresh resumed shot uses new token");Frame();
    g_sessionState=XR_SESSION_STATE_VISIBLE;Frame();Check(!VR_WeaponHapticToken(testTitle,gen,false),"focus loss cancels native voices");
    g_sessionState=XR_SESSION_STATE_FOCUSED;Frame();Check(VR_WeaponHapticToken(testTitle,gen,false)!=resumed,"focus recovery has a new token");
    testNow+=101;Check(!VR_WeaponHapticToken(testTitle,gen,false),"stopped XR frame publisher expires admission");
}
void TestLateWeaponPublication()
{
    for(bool leftHanded:{false,true}) {
        Begin();g_capturedLeftHanded=leftHanded;Frame();
        const auto gen=testRuntime->Generation(testTitle);
        const auto oldPrimary=VR_WeaponHapticToken(testTitle,gen,false);
        testMenu=true;Frame();testMenu=false;Frame();outputs.clear();
        // Model a native producer paused after admission but before Raise:
        // cancellation and reopening have already happened when it resumes.
        Check(g_weaponHaptics.Raise(testTitle,gen,1,.9f,testNow,oldPrimary),
            "simulate old admitted writer publishing after the XR clear");
        Frame();Check(!HasApply(),"consumer rejects late pre-cancellation recoil in either handedness");
        const auto primary=VR_WeaponHapticToken(testTitle,gen,false);
        Check(VR_PulseWeaponHaptics(testTitle,gen,false,false,.2f,primary),"fresh epoch publishes after stale packet");
        Check(!g_weaponHaptics.Raise(testTitle,gen,1,1.0f,testNow,oldPrimary),
            "late old writer cannot overwrite a newer admitted peak");
        Frame();Check(outputs.size()==1&&Near(outputs[0].amplitude,.2f*.86f)&&
            outputs[0].path==(leftHanded?g_leftHandPath:g_rightHandPath),
            "fresh weak recoil keeps its physical hand and cannot inherit cancelled strength");
    }
    Begin();Frame();const auto gen=testRuntime->Generation(testTitle);
    const auto primary=VR_WeaponHapticToken(testTitle,gen,false);
    const auto oldSupport=VR_WeaponHapticToken(testTitle,gen,true);
    ApplyControllerHaptics(true,false);Frame();testTwoHand=true;outputs.clear();
    Check(g_weaponHaptics.Raise(testTitle,gen,2,.8f,testNow,primary,oldSupport),
        "simulate coupled writer resuming after only support regained tracking");
    Frame();Check(!HasApply(),"support epoch protects coupled pulse even when primary epoch is unchanged");
    Check(VR_PulseWeaponHaptics(testTitle,gen,false,true,.3f,primary),"fresh coupled pulse uses both current hand epochs");
    Check(!g_weaponHaptics.Raise(testTitle,gen,2,.9f,testNow,primary,oldSupport),
        "old support epoch cannot replace fresh coupled feedback");
    Frame();Pair(.3f*.86f,"current two-hand recoil reaches both live controllers");
    Begin();Frame();const auto priorGap=VR_WeaponHapticToken(testTitle,gen,false);
    Frame(101);outputs.clear();
    Check(VR_WeaponHapticToken(testTitle,gen,false)!=priorGap,
        "a stalled XR publisher resumes with a fresh cancellation epoch");
    g_weaponHaptics.Raise(testTitle,gen,1,.9f,testNow,priorGap);Frame();
    Check(!HasApply(),"late recoil cannot resume after an XR publication gap");
}
void TestConcurrentWeaponPublication()
{
    WeaponHapticPulses packets;
    std::atomic<bool> start{},invalid{};
    auto publish=[&](uint64_t epoch,float amplitude) {
        while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned i=0;i<50000;++i)
            packets.Raise(GameTitle::Halo3,7,1,amplitude,100,epoch);
    };
    std::thread cancelled(publish,3,.9f),current(publish,5,.2f);
    start.store(true,std::memory_order_release);
    for(unsigned i=0;i<50000;++i) {
        const float value=packets.Read(GameTitle::Halo3,7,1,(i&1)!=0,100,5);
        if(value!=0&&!Near(value,.2f))invalid=true;
        if(i%97==0)packets.Clear();
    }
    cancelled.join();current.join();
    Check(!invalid.load(),"concurrent publication/clear/consumption never mixes a cancelled epoch's amplitude into a current packet");
}
}

int main()
{
    TestGunRumble(GameTitle::Halo3);
    // Original and Anniversary share this exact CE ownership/capability path;
    // the rendering mode never participates in any extracted haptic function.
    TestGunRumble(GameTitle::HaloCE);
    TestIntensityAndContacts();TestStopsAndOwnership();TestIndependentTracking();TestWeaponRouting();TestWeaponCancellationTokens();TestLateWeaponPublication();TestConcurrentWeaponPublication();
    std::printf("Haptics production runtime: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
