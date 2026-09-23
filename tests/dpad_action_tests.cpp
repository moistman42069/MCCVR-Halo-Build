// Execute shipping OpenXR action creation/binding/query against captured API
// endpoints. No OpenXR loader, game process, headset, or controller is opened.
#include <Windows.h>
#include <openxr/openxr.h>
#include "../src/dll/vr.h"
#include "../src/common/config.h"
#include "../src/common/weapon_interaction_logic.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
unsigned checks{},failures{},attachCalls{},spaceCalls{},queryCalls{};
uintptr_t nextHandle=10;
bool failOptionalAction{},rejectOptionalBinding{},rejectPro{},g_touchProProfileEnabled{};
XrResult queryResult=XR_SUCCESS;
XrBool32 queryActive=XR_TRUE,queryValue=XR_TRUE;
XrInstance g_instance=reinterpret_cast<XrInstance>(uintptr_t{1});
XrSession g_session=reinterpret_cast<XrSession>(uintptr_t{2});
XrActionSet g_gameplayActions=XR_NULL_HANDLE;
XrSpace g_rightAimSpace=XR_NULL_HANDLE,g_leftAimSpace=XR_NULL_HANDLE;
XrPath g_rightHandPath=XR_NULL_PATH,g_leftHandPath=XR_NULL_PATH;
XrAction g_rightAimAction{},g_leftAimAction{},g_hapticAction{},g_actMove{},g_actTurn{};
XrAction g_actTrigL{},g_actTrigR{},g_actGripL{},g_actGripR{},g_actA{},g_actB{},g_actX{},g_actY{};
XrAction g_actClickL{},g_actClickR{},g_actMenu{},g_actLeftThumbrest{};
XrAction g_actFacePadL{},g_actFacePadR{},g_actFaceClickL{},g_actFaceClickR{};
vr_mapping::Transports fixtureNativeBindings{};
uint32_t Game_VrActionTransport(vr_mapping::Action action,uint64_t) {return fixtureNativeBindings[action];}
CRITICAL_SECTION g_headCs{};
bool g_headCsInit{},fixtureMenu{};
VrPadState g_padState{};
std::atomic<uint64_t> g_thumbrestDpadSampleMs{};
std::atomic<int> g_sessionStateShared{XR_SESSION_STATE_FOCUSED};
uint64_t fixtureNow=1000;
uint64_t fixtureWeaponGraph=0;
std::atomic<uint64_t> g_contactSpaceEpoch{1};
GameTitle fixtureTitle=GameTitle::Halo3;
RuntimeMode fixtureMode=RuntimeMode::Gameplay;
uint32_t fixtureGeneration=1;
bool fixtureHeadTracking=true,fixtureStereo=true,fixturePause=false,fixturePauseTarget=false,fixtureTheater=false;
GameTitle TitleAdapter_GetActiveTitle() { return fixtureTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return fixtureGeneration; }
RuntimeMode TitleAdapter_GetRuntimeMode() { return fixtureMode; }
bool Game_IsHeadTracking() { return fixtureHeadTracking; }
uint64_t FixtureTick() { return fixtureNow; }
struct Action { std::string name;XrActionType type;std::vector<XrPath> subactions; };
struct Binding { std::string action,path; };
struct Suggestion { std::string profile;std::vector<Binding> bindings; };
std::map<XrAction,Action> actions;
std::map<std::string,XrPath> paths;
std::vector<Suggestion> suggestions;
constexpr const char* touch="/interaction_profiles/oculus/touch_controller";
constexpr const char* pro="/interaction_profiles/facebook/touch_controller_pro";
constexpr const char* thumb="/user/hand/left/input/thumbrest/touch";
void Check(bool ok,const char* message)
{ ++checks;if (!ok) { ++failures;std::fprintf(stderr,"FAIL: %s\n",message); } }
std::string PathName(XrPath path)
{ for (const auto& entry:paths) if (entry.second==path) return entry.first;return {}; }
bool HasOptional(const Suggestion& value)
{ return std::any_of(value.bindings.begin(),value.bindings.end(),[](const Binding& b){return b.action=="left_thumbrest_touch";}); }
}

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(
    XrInstance,const XrActionSetCreateInfo*,XrActionSet* out)
{ *out=reinterpret_cast<XrActionSet>(nextHandle++);return XR_SUCCESS; }
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance,const char* text,XrPath* out)
{ auto& value=paths[text];if (!value) value=nextHandle++;*out=value;return XR_SUCCESS; }
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(
    XrActionSet,const XrActionCreateInfo* info,XrAction* out)
{
    if (failOptionalAction&&!std::strcmp(info->actionName,"left_thumbrest_touch")) return XR_ERROR_RUNTIME_FAILURE;
    *out=reinterpret_cast<XrAction>(nextHandle++);
    Action value{info->actionName,info->actionType,{}};
    if (info->countSubactionPaths) value.subactions.assign(info->subactionPaths,info->subactionPaths+info->countSubactionPaths);
    actions.emplace(*out,std::move(value));return XR_SUCCESS;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(
    XrInstance,const XrInteractionProfileSuggestedBinding* info)
{
    Suggestion value{PathName(info->interactionProfile),{}};
    for (uint32_t i=0;i<info->countSuggestedBindings;++i)
    {
        const auto& binding=info->suggestedBindings[i];
        value.bindings.push_back({actions.at(binding.action).name,PathName(binding.binding)});
    }
    const bool rejected=(rejectOptionalBinding&&HasOptional(value))||(rejectPro&&value.profile==pro);
    suggestions.push_back(std::move(value));return rejected?XR_ERROR_PATH_UNSUPPORTED:XR_SUCCESS;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(
    XrSession,const XrSessionActionSetsAttachInfo* info)
{
    ++attachCalls;Check(info->countActionSets==1&&info->actionSets[0]==g_gameplayActions,
        "Optional D-pad setup preserves attachment of the complete gameplay action set");return XR_SUCCESS;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(
    XrSession,const XrActionSpaceCreateInfo* info,XrSpace* out)
{
    ++spaceCalls;Check((info->action==g_rightAimAction&&info->subactionPath==g_rightHandPath)||
        (info->action==g_leftAimAction&&info->subactionPath==g_leftHandPath),"Existing physical aim spaces retain the correct hand");
    *out=reinterpret_cast<XrSpace>(nextHandle++);return XR_SUCCESS;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(
    XrSession,const XrActionStateGetInfo* info,XrActionStateBoolean* out)
{
    ++queryCalls;Check(info->action==g_actLeftThumbrest,"Touch query addresses only the physical-left thumb-rest action");
    out->isActive=queryActive;out->currentState=queryValue;return queryResult;
}

bool Menu_IsOpen() { return fixtureMenu; }
bool fixtureNativePointer = false;
bool NativeMenuPointer_ConsumesTrigger() { return fixtureNativePointer; }
uint64_t VR_GetWeaponModelIdentity(GameTitle,uint32_t,uint64_t,uint64_t) noexcept { return fixtureWeaponGraph; }
bool VR_IsStereoEnabled() { return fixtureStereo; }
bool VR_IsPausePresentation() { return fixturePause; }
bool VR_IsPausePresentationTarget() { return fixturePauseTarget; }
bool VR_IsCutsceneTheaterActive() { return fixtureTheater; }
#define LOG(...) ((void)0)
#define GetTickCount64 FixtureTick
#include "dpad_action_functions.inl"
#undef GetTickCount64
#undef LOG

namespace
{
void Reset(bool failAction=false,bool rejectBinding=false,bool enablePro=false,bool unsupportedPro=false)
{
    actions.clear();paths.clear();suggestions.clear();attachCalls=spaceCalls=queryCalls=0;nextHandle=10;
    failOptionalAction=failAction;rejectOptionalBinding=rejectBinding;
    g_touchProProfileEnabled=enablePro;rejectPro=unsupportedPro;
    queryResult=XR_SUCCESS;queryActive=queryValue=XR_TRUE;
    g_gameplayActions=XR_NULL_HANDLE;g_rightAimSpace=g_leftAimSpace=XR_NULL_HANDLE;
    for (XrAction* value:{&g_rightAimAction,&g_leftAimAction,&g_hapticAction,&g_actMove,&g_actTurn,
        &g_actTrigL,&g_actTrigR,&g_actGripL,&g_actGripR,&g_actA,&g_actB,&g_actX,&g_actY,
        &g_actClickL,&g_actClickR,&g_actMenu,&g_actLeftThumbrest,
        &g_actFacePadL,&g_actFacePadR,&g_actFaceClickL,&g_actFaceClickR}) *value=XR_NULL_HANDLE;
}
void CheckTouch(const Suggestion& actual,bool optional)
{
    const Binding original[]{
        {"right_aim_pose","/user/hand/right/input/aim/pose"},{"left_aim_pose","/user/hand/left/input/aim/pose"},
        {"move","/user/hand/left/input/thumbstick"},{"turn","/user/hand/right/input/thumbstick"},
        {"trigger_l","/user/hand/left/input/trigger/value"},{"trigger_r","/user/hand/right/input/trigger/value"},
        {"grip_l","/user/hand/left/input/squeeze/value"},{"grip_r","/user/hand/right/input/squeeze/value"},
        {"btn_a","/user/hand/right/input/a/click"},{"btn_b","/user/hand/right/input/b/click"},
        {"btn_x","/user/hand/left/input/x/click"},{"btn_y","/user/hand/left/input/y/click"},
        {"click_l","/user/hand/left/input/thumbstick/click"},{"click_r","/user/hand/right/input/thumbstick/click"},
        {"menu","/user/hand/left/input/menu/click"},{"menu","/user/hand/right/input/system/click"},
        {"game_haptics","/user/hand/left/output/haptic"},{"game_haptics","/user/hand/right/output/haptic"}};
    Check(actual.bindings.size()==std::size(original)+(optional?1:0),"Every Touch suggestion contains the complete original list, never thumb-rest only");
    for (const auto& expected:original)
        Check(std::count_if(actual.bindings.begin(),actual.bindings.end(),[&](const Binding& b){
            return b.action==expected.action&&b.path==expected.path;})==1,"Every original Touch pose/button/stick/motor binding survives optional setup");
    Check(HasOptional(actual)==optional,"Optional binding is present only in the intended complete suggestion");
    if (optional) Check(std::count_if(actual.bindings.begin(),actual.bindings.end(),[](const Binding& b){
        return b.action=="left_thumbrest_touch"&&b.path==thumb;})==1,"Thumb-rest binding always targets the physical left hand");
}
void CheckProfiles(unsigned touchCalls,unsigned proCalls)
{
    unsigned touchCount=0,proCount=0,otherCount=0;
    for (const auto& suggestion:suggestions)
    {
        if (suggestion.profile==touch||suggestion.profile==pro)
        {
            unsigned& count=suggestion.profile==touch?touchCount:proCount;
            CheckTouch(suggestion,!failOptionalAction&&count==0);++count;
        }
        else
        {
            ++otherCount;Check(!HasOptional(suggestion),"Optional Touch sensor is never added to unrelated interaction profiles");
            const size_t expected=suggestion.profile=="/interaction_profiles/khr/simple_controller"?6:17;
            Check(suggestion.bindings.size()==expected,"Other controller profiles include their complete controls and supported pad face zones");
            if(suggestion.profile=="/interaction_profiles/htc/vive_controller"||
               suggestion.profile=="/interaction_profiles/microsoft/motion_controller")
                for(const Binding& required:std::vector<Binding>{{"face_pad_l","/user/hand/left/input/trackpad"},
                    {"face_pad_r","/user/hand/right/input/trackpad"},
                    {"face_click_l","/user/hand/left/input/trackpad/click"},
                    {"face_click_r","/user/hand/right/input/trackpad/click"}})
                    Check(std::count_if(suggestion.bindings.begin(),suggestion.bindings.end(),[&](const Binding& b){
                        return b.action==required.action&&b.path==required.path;})==1,"Wand/WMR face zones bind the real physical pad components");
        }
    }
    Check(touchCount==touchCalls&&proCount==proCalls&&otherCount==4,"Profile acceptance/fallback leaves all existing controller profiles reachable");
    Check(attachCalls==1&&spaceCalls==2&&g_rightAimSpace&&g_leftAimSpace,"Both tracked hands survive optional action/profile rejection");
    const auto& haptics=actions.at(g_hapticAction);
    Check(haptics.type==XR_ACTION_TYPE_VIBRATION_OUTPUT&&haptics.subactions.size()==2&&
        haptics.subactions[0]==g_leftHandPath&&haptics.subactions[1]==g_rightHandPath,"Both physical vibration outputs remain advertised");
}
}

int main()
{
    g_config.vr_action_mapping=false; // Existing legacy snapshot cases below.
    Reset(true);Check(CreateControllerActions(),"Optional thumb-rest action creation failure is nonfatal");CheckProfiles(1,0);
    Check(g_actLeftThumbrest==XR_NULL_HANDLE&&!ReadLeftThumbrestTouched()&&queryCalls==0,"Absent optional action is neutral without querying XR");
    Reset(false,true);Check(CreateControllerActions(),"Optional binding rejection is nonfatal");CheckProfiles(2,0);
    Reset();Check(CreateControllerActions(),"Optional Touch sensor can be added to complete baseline controls");CheckProfiles(1,0);
    Check(actions.at(g_actLeftThumbrest).type==XR_ACTION_TYPE_BOOLEAN_INPUT,"Thumb-rest action is a boolean input");
    Check(ReadLeftThumbrestTouched(),"An active true physical-left touch state activates the optional gesture");
    queryValue=XR_FALSE;Check(!ReadLeftThumbrestTouched(),"Untouched sensor is neutral");
    queryValue=XR_TRUE;queryActive=XR_FALSE;Check(!ReadLeftThumbrestTouched(),"Unbound or inactive sensor cannot activate D-pad");
    queryActive=XR_TRUE;queryResult=XR_ERROR_RUNTIME_FAILURE;Check(!ReadLeftThumbrestTouched(),"XR query failure cannot reuse a true state");
    Reset(false,false,true,true);Check(CreateControllerActions(),"Unsupported Touch Pro profile cannot prevent other controller setup");CheckProfiles(1,2);
    Reset(false,false,true);Check(CreateControllerActions(),"Touch Pro and Oculus receive independent complete optional lists");CheckProfiles(1,1);
    InitializeCriticalSection(&g_headCs);g_headCsInit=true;
    g_config.quest_thumbrest_dpad=true;
    g_padState.valid=true;g_padState.moveX=0.4f;g_padState.b=true;
    g_padState.thumbrestDpad=true;g_padState.dpadX=0.8f;g_padState.dpadY=-0.9f;
    const auto read=[&](bool expected) {
        VrPadState sample{};VR_GetPadState(sample);
        Check(sample.thumbrestDpad==expected&&sample.dpadX==(expected?0.8f:0.0f)&&
            sample.dpadY==(expected?-0.9f:0.0f),"Published touch modifier respects freshness, focus, menu and setting guards");
        Check(sample.valid&&sample.moveX==0.4f&&sample.b&&!sample.turnX&&!sample.turnY,
            "Optional D-pad denial preserves the rest of the accepted pad snapshot");
    };
    g_thumbrestDpadSampleMs=1000;read(true);
    g_config.quest_thumbrest_dpad=false;read(false);g_config.quest_thumbrest_dpad=true;
    g_sessionStateShared=XR_SESSION_STATE_VISIBLE;read(false);g_sessionStateShared=XR_SESSION_STATE_FOCUSED;
    fixtureMenu=true;read(false);fixtureMenu=false;
    g_thumbrestDpadSampleMs=0;read(false); // Synchronization/focus failure invalidation.
    g_thumbrestDpadSampleMs=1001;read(false); // Clock must not run backwards.
    g_thumbrestDpadSampleMs=750;read(true);
    g_thumbrestDpadSampleMs=749;read(false);
    // Exercise the shipping XInput-facing snapshot validator, not a model of
    // it. No action may escape into another title, mode or controller role.
    for(int title=1;title<=6;++title)
    {
        fixtureTitle=static_cast<GameTitle>(title);
        const int index=title-1;
        g_config.manual_reload=g_config.weapon_holsters=true;
        g_padState.weaponTitle=fixtureTitle;g_padState.weaponGeneration=fixtureGeneration;
        g_padState.weaponSpace=1;g_padState.weaponOptions=3;
        g_padState.weaponSampleMs=fixtureNow;g_padState.weaponPulseUntilMs=fixtureNow+120;
        g_padState.weaponReloadBinding=weapon_interaction::Button(g_config.weapon_reload_button[index]);
        g_padState.weaponSwitchBinding=weapon_interaction::Button(g_config.weapon_switch_button[index]);
        g_padState.weaponButtons=0x4000;g_padState.weaponConsumeSupport=true;
        const auto gestureRead=[&](bool expected) {
            VrPadState pad{};VR_GetPadState(pad);
            Check((pad.weaponButtons==0x4000)==expected&&pad.weaponConsumeSupport==expected,
                "shipping pad reader admits only current focused gesture publication");
            Check(pad.valid&&pad.b&&pad.moveX==0.4f,"gesture validation preserves ordinary gamepad input");
        };
        gestureRead(true);
        for(auto mode:{RuntimeMode::Shell,RuntimeMode::Loading,RuntimeMode::Paused,RuntimeMode::Cutscene,
                      RuntimeMode::Vehicle,RuntimeMode::Turret,RuntimeMode::Dead,RuntimeMode::Unsupported})
        {fixtureMode=mode;gestureRead(false);}
        fixtureMode=RuntimeMode::Gameplay;
        fixturePause=true;gestureRead(false);fixturePause=false;
        fixturePauseTarget=true;gestureRead(false);fixturePauseTarget=false;
        fixtureTheater=true;gestureRead(false);fixtureTheater=false;
        fixtureStereo=false;gestureRead(false);fixtureStereo=true;
        fixtureHeadTracking=false;gestureRead(false);fixtureHeadTracking=true;
        fixtureMenu=true;gestureRead(false);fixtureMenu=false;
        g_sessionStateShared=XR_SESSION_STATE_VISIBLE;gestureRead(false);g_sessionStateShared=XR_SESSION_STATE_FOCUSED;
        ++fixtureGeneration;gestureRead(false);--fixtureGeneration;
        ++g_contactSpaceEpoch;gestureRead(false);--g_contactSpaceEpoch;
        g_config.left_handed=true;gestureRead(false);g_config.left_handed=false;
        g_config.manual_reload=false;gestureRead(false);g_config.manual_reload=true;
        g_config.weapon_holster_click=true;gestureRead(false);g_config.weapon_holster_click=false;
        g_config.weapon_holster_slide=false;gestureRead(false);g_config.weapon_holster_slide=true;
        g_config.weapon_needler_shake=true;gestureRead(false);g_config.weapon_needler_shake=false;
        ++g_config.weapon_reload_button[index];gestureRead(false);--g_config.weapon_reload_button[index];
        g_config.vr_action_mapping=true;
        fixtureNativeBindings[vr_mapping::Reload]=g_padState.weaponReloadBinding;
        fixtureNativeBindings[vr_mapping::SwitchWeapon]=g_padState.weaponSwitchBinding;
        gestureRead(true);
        fixtureNativeBindings[vr_mapping::Reload]^=0x1000;gestureRead(false);
        fixtureNativeBindings[vr_mapping::Reload]=0;gestureRead(false);
        g_config.vr_action_mapping=false;gestureRead(true);
        g_padState.weaponTitle=GameTitle::None;gestureRead(false);g_padState.weaponTitle=fixtureTitle;
        g_padState.weaponSampleMs=fixtureNow-151;gestureRead(false);g_padState.weaponSampleMs=fixtureNow;
        g_padState.weaponPulseUntilMs=fixtureNow;VrPadState expired{};VR_GetPadState(expired);
        Check(!expired.weaponButtons&&expired.weaponConsumeSupport,"pulse expires while claimed grip remains consumed");
    }
    fixtureTitle=GameTitle::HaloCE;
    g_padState.weaponTitle=fixtureTitle;g_padState.weaponSampleMs=fixtureNow;
    g_padState.weaponPulseUntilMs=fixtureNow+120;g_padState.weaponOptions=35;
    g_config.weapon_needler_shake=true;fixtureWeaponGraph=0x55EA2D6F6C10C375ull;
    g_padState.weaponGraph=fixtureWeaponGraph;
    VrPadState needle{};VR_GetPadState(needle);
    Check(needle.weaponButtons==0x4000,"current CE needle identity admits pulse");
    fixtureWeaponGraph=0;VR_GetPadState(needle);
    Check(!needle.weaponButtons,"lost or replaced CE graph cancels published reload");
    fixtureNativePointer=true; g_padState.trigR=0.8f;
    VR_GetPadState(needle); Check(needle.trigR==0,"native menu click consumes only outgoing primary trigger");
    Check(g_padState.trigR==0.8f,"native pointer retains original trigger sample");
    fixtureNativePointer=false; VR_GetPadState(needle);
    Check(needle.trigR==0,"held menu click cannot fire on resume or toggle-off");
    g_padState.trigR=0; VR_GetPadState(needle);
    g_padState.trigR=0.8f; VR_GetPadState(needle);
    Check(needle.trigR==0.8f,"fresh gameplay trigger is unchanged after release");
    g_headCsInit=false;VrPadState absent{};absent.valid=true;absent.thumbrestDpad=true;
    VR_GetPadState(absent);Check(!absent.valid&&!absent.thumbrestDpad&&!absent.dpadX&&!absent.dpadY,
        "An unavailable controller snapshot cannot retain D-pad input");
    DeleteCriticalSection(&g_headCs);
    std::printf("D-pad OpenXR production action setup: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
