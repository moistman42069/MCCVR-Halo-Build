// Execute shipping OpenXR action creation/binding/query against captured API
// endpoints. No OpenXR loader, game process, headset, or controller is opened.
#include <Windows.h>
#include <openxr/openxr.h>
#include "../src/dll/vr.h"
#include "../src/dll/native_vr_actions.h"
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

static bool fixtureNativeRouting=false;
bool NativeVrActions_GestureRouting(GameTitle,uint64_t) noexcept
{ return fixtureNativeRouting; }

namespace
{
unsigned checks{},failures{},attachCalls{},spaceCalls{},queryCalls{};
uintptr_t nextHandle=10;
bool failOptionalAction{},failGripAction{},failLeftGripSpace{},failRightGripSpace{};
bool rejectGripBinding{},rejectThumbrestBinding{},rejectCombinedBinding{};
bool rejectPro{},g_touchProProfileEnabled{};
XrResult queryResult=XR_SUCCESS;
XrBool32 queryActive=XR_TRUE,queryValue=XR_TRUE;
XrInstance g_instance=reinterpret_cast<XrInstance>(uintptr_t{1});
XrSession g_session=reinterpret_cast<XrSession>(uintptr_t{2});
XrActionSet g_gameplayActions=XR_NULL_HANDLE;
XrSpace g_rightAimSpace=XR_NULL_HANDLE,g_leftAimSpace=XR_NULL_HANDLE;
XrSpace g_leftGripPoseSpace=XR_NULL_HANDLE,g_rightGripPoseSpace=XR_NULL_HANDLE;
XrPath g_rightHandPath=XR_NULL_PATH,g_leftHandPath=XR_NULL_PATH;
XrAction g_rightAimAction{},g_leftAimAction{},g_supportGripPoseAction{},g_hapticAction{},g_actMove{},g_actTurn{};
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
struct Suggestion { std::string profile;std::vector<Binding> bindings;bool accepted{}; };
std::map<XrAction,Action> actions;
std::map<std::string,XrPath> paths;
std::vector<Suggestion> suggestions;
constexpr const char* touch="/interaction_profiles/oculus/touch_controller";
constexpr const char* pro="/interaction_profiles/facebook/touch_controller_pro";
constexpr const char* thumb="/user/hand/left/input/thumbrest/touch";
constexpr const char* gripL="/user/hand/left/input/grip/pose";
constexpr const char* gripR="/user/hand/right/input/grip/pose";
void Check(bool ok,const char* message)
{ ++checks;if (!ok) { ++failures;std::fprintf(stderr,"FAIL: %s\n",message); } }
std::string PathName(XrPath path)
{ for (const auto& entry:paths) if (entry.second==path) return entry.first;return {}; }
bool HasThumbrest(const Suggestion& value)
{ return std::any_of(value.bindings.begin(),value.bindings.end(),[](const Binding& b){return b.action=="left_thumbrest_touch";}); }
bool HasGripPose(const Suggestion& value)
{ return std::any_of(value.bindings.begin(),value.bindings.end(),[](const Binding& b){return b.action=="two_hand_grip_pose";}); }
}

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(
    XrInstance,const XrActionSetCreateInfo*,XrActionSet* out)
{ *out=reinterpret_cast<XrActionSet>(nextHandle++);return XR_SUCCESS; }
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance,const char* text,XrPath* out)
{ auto& value=paths[text];if (!value) value=nextHandle++;*out=value;return XR_SUCCESS; }
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(
    XrActionSet,const XrActionCreateInfo* info,XrAction* out)
{
    if ((failOptionalAction&&!std::strcmp(info->actionName,"left_thumbrest_touch"))||
        (failGripAction&&!std::strcmp(info->actionName,"two_hand_grip_pose")))
        return XR_ERROR_RUNTIME_FAILURE;
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
    const bool hasGrip=HasGripPose(value),hasThumb=HasThumbrest(value);
    const bool rejected=(hasGrip&&rejectGripBinding)||(hasThumb&&rejectThumbrestBinding)||
        (hasGrip&&hasThumb&&rejectCombinedBinding)||(rejectPro&&value.profile==pro);
    value.accepted=!rejected;
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
        (info->action==g_leftAimAction&&info->subactionPath==g_leftHandPath)||
        (info->action==g_supportGripPoseAction&&info->subactionPath==g_leftHandPath)||
        (info->action==g_supportGripPoseAction&&info->subactionPath==g_rightHandPath),
        "Existing aim spaces and optional physical grip spaces retain the correct hand");
    if (info->action==g_supportGripPoseAction&&
        ((info->subactionPath==g_leftHandPath&&failLeftGripSpace)||
         (info->subactionPath==g_rightHandPath&&failRightGripSpace)))
        return XR_ERROR_RUNTIME_FAILURE;
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
void Reset(bool failAction=false,bool rejectGrip=false,bool rejectThumb=false,
    bool rejectCombined=false,bool enablePro=false,bool unsupportedPro=false,
    bool failGrip=false,bool failLeftSpace=false,bool failRightSpace=false)
{
    actions.clear();paths.clear();suggestions.clear();attachCalls=spaceCalls=queryCalls=0;nextHandle=10;
    failOptionalAction=failAction;rejectGripBinding=rejectGrip;
    rejectThumbrestBinding=rejectThumb;rejectCombinedBinding=rejectCombined;
    failGripAction=failGrip;failLeftGripSpace=failLeftSpace;
    failRightGripSpace=failRightSpace;
    g_touchProProfileEnabled=enablePro;rejectPro=unsupportedPro;
    queryResult=XR_SUCCESS;queryActive=queryValue=XR_TRUE;
    g_gameplayActions=XR_NULL_HANDLE;g_rightAimSpace=g_leftAimSpace=XR_NULL_HANDLE;
    g_leftGripPoseSpace=g_rightGripPoseSpace=XR_NULL_HANDLE;
    for (XrAction* value:{&g_rightAimAction,&g_leftAimAction,&g_supportGripPoseAction,&g_hapticAction,&g_actMove,&g_actTurn,
        &g_actTrigL,&g_actTrigR,&g_actGripL,&g_actGripR,&g_actA,&g_actB,&g_actX,&g_actY,
        &g_actClickL,&g_actClickR,&g_actMenu,&g_actLeftThumbrest,
        &g_actFacePadL,&g_actFacePadR,&g_actFaceClickL,&g_actFaceClickR}) *value=XR_NULL_HANDLE;
}
void CheckTouch(const Suggestion& actual)
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
    const bool hasGrip=HasGripPose(actual),hasThumb=HasThumbrest(actual);
    Check(actual.bindings.size()==std::size(original)+(hasGrip?2:0)+(hasThumb?1:0),
        "Every Touch suggestion contains the complete original list, never optional-only bindings");
    for (const auto& expected:original)
        Check(std::count_if(actual.bindings.begin(),actual.bindings.end(),[&](const Binding& b){
            return b.action==expected.action&&b.path==expected.path;})==1,"Every original Touch pose/button/stick/motor binding survives optional setup");
    if (hasThumb) Check(std::count_if(actual.bindings.begin(),actual.bindings.end(),[](const Binding& b){
        return b.action=="left_thumbrest_touch"&&b.path==thumb;})==1,"Thumb-rest binding always targets the physical left hand");
    if (hasGrip)
    {
        Check(std::count_if(actual.bindings.begin(),actual.bindings.end(),[](const Binding& b){
            return b.action=="two_hand_grip_pose"&&b.path==gripL;})==1,"Optional grip-pose binding includes physical left grip pose");
        Check(std::count_if(actual.bindings.begin(),actual.bindings.end(),[](const Binding& b){
            return b.action=="two_hand_grip_pose"&&b.path==gripR;})==1,"Optional grip-pose binding includes physical right grip pose");
    }
}
void CheckProfiles(unsigned touchCalls,unsigned proCalls,
    bool expectedTouchThumb=false,bool expectedTouchGrip=false)
{
    std::map<std::string,bool> acceptedProfiles;
    const Suggestion* finalTouch=nullptr;
    for (const auto& suggestion:suggestions)
    {
        if (!suggestion.accepted)
            continue;
        acceptedProfiles[suggestion.profile]=true;
        if (suggestion.profile==touch||suggestion.profile==pro)
        {
            CheckTouch(suggestion);
            if (suggestion.profile==touch)
                finalTouch=&suggestion;
        }
        else
        {
            Check(!HasThumbrest(suggestion),"Optional Touch sensor is never added to unrelated interaction profiles");
            const size_t expected=suggestion.profile=="/interaction_profiles/khr/simple_controller"?6:17;
            const bool hasGrip=HasGripPose(suggestion);
            Check(suggestion.bindings.size()==expected+(hasGrip?2:0),
                "Other controller profiles retain their complete baseline binding count, supported pad face zones and optional grip poses only when present");
            if(suggestion.profile=="/interaction_profiles/htc/vive_controller"||
               suggestion.profile=="/interaction_profiles/microsoft/motion_controller")
                for(const Binding& required:std::vector<Binding>{{"face_pad_l","/user/hand/left/input/trackpad"},
                    {"face_pad_r","/user/hand/right/input/trackpad"},
                    {"face_click_l","/user/hand/left/input/trackpad/click"},
                    {"face_click_r","/user/hand/right/input/trackpad/click"}})
                    Check(std::count_if(suggestion.bindings.begin(),suggestion.bindings.end(),[&](const Binding& b){
                        return b.action==required.action&&b.path==required.path;})==1,"Wand/WMR face zones bind the real physical pad components");
            if (hasGrip)
            {
                Check(std::count_if(suggestion.bindings.begin(),suggestion.bindings.end(),[](const Binding& b){
                    return b.action=="two_hand_grip_pose"&&b.path==gripL;})==1,"Non-Touch optional grip-pose binding includes physical left grip pose");
                Check(std::count_if(suggestion.bindings.begin(),suggestion.bindings.end(),[](const Binding& b){
                    return b.action=="two_hand_grip_pose"&&b.path==gripR;})==1,"Non-Touch optional grip-pose binding includes physical right grip pose");
            }
        }
    }
    Check(acceptedProfiles[touch]==(touchCalls!=0)&&
        acceptedProfiles[pro]==(proCalls!=0)&&
        acceptedProfiles["/interaction_profiles/valve/index_controller"]&&
        acceptedProfiles["/interaction_profiles/microsoft/motion_controller"]&&
        acceptedProfiles["/interaction_profiles/htc/vive_controller"]&&
        acceptedProfiles["/interaction_profiles/khr/simple_controller"],
        "Profile acceptance/fallback leaves all existing controller profiles reachable");
    if (finalTouch)
        Check(HasThumbrest(*finalTouch)==expectedTouchThumb &&
            HasGripPose(*finalTouch)==expectedTouchGrip,
            "Final Touch suggestion preserves exactly the independently negotiated optional bindings");
    const bool gripActionAvailable=g_supportGripPoseAction!=XR_NULL_HANDLE;
    Check(attachCalls==1&&spaceCalls==(gripActionAvailable?4u:2u)&&
        g_rightAimSpace&&g_leftAimSpace&&
        (!gripActionAvailable || g_leftGripPoseSpace || failLeftGripSpace)&&
        (!gripActionAvailable || g_rightGripPoseSpace || failRightGripSpace),
        "Existing aim spaces survive while optional grip spaces remain independently optional");
    const auto& haptics=actions.at(g_hapticAction);
    Check(haptics.type==XR_ACTION_TYPE_VIBRATION_OUTPUT&&haptics.subactions.size()==2&&
        haptics.subactions[0]==g_leftHandPath&&haptics.subactions[1]==g_rightHandPath,"Both physical vibration outputs remain advertised");
}
}

int main()
{
    g_config.vr_action_mapping=false; // Existing legacy snapshot cases below.
    Reset(true);Check(CreateControllerActions(),"Optional thumb-rest action creation failure is nonfatal");CheckProfiles(1,0,false,true);
    Check(g_actLeftThumbrest==XR_NULL_HANDLE&&!ReadLeftThumbrestTouched()&&queryCalls==0,"Absent optional action is neutral without querying XR");
    Reset(false,true);Check(CreateControllerActions(),"Grip binding rejection is nonfatal");CheckProfiles(1,0,true,false);
    Reset(false,false,true);Check(CreateControllerActions(),"Thumb-rest binding rejection is nonfatal");CheckProfiles(1,0,false,true);
    Reset(false,true,true);Check(CreateControllerActions(),"Independent grip and thumb-rest rejection is nonfatal");CheckProfiles(1,0,false,false);
    Reset(false,false,false,true);Check(CreateControllerActions(),"Combined optional binding rejection is nonfatal");CheckProfiles(1,0,true,false);
    Reset();Check(CreateControllerActions(),"Optional Touch sensor can be added to complete baseline controls");CheckProfiles(1,0,true,true);
    Check(actions.at(g_actLeftThumbrest).type==XR_ACTION_TYPE_BOOLEAN_INPUT,"Thumb-rest action is a boolean input");
    Check(ReadLeftThumbrestTouched(),"An active true physical-left touch state activates the optional gesture");
    queryValue=XR_FALSE;Check(!ReadLeftThumbrestTouched(),"Untouched sensor is neutral");
    queryValue=XR_TRUE;queryActive=XR_FALSE;Check(!ReadLeftThumbrestTouched(),"Unbound or inactive sensor cannot activate D-pad");
    queryActive=XR_TRUE;queryResult=XR_ERROR_RUNTIME_FAILURE;Check(!ReadLeftThumbrestTouched(),"XR query failure cannot reuse a true state");
    Reset(false,false,false,false,false,false,true);
    Check(CreateControllerActions(),"Optional grip action creation failure is isolated");
    Check(g_supportGripPoseAction==XR_NULL_HANDLE&&g_leftGripPoseSpace==XR_NULL_HANDLE&&
        g_rightGripPoseSpace==XR_NULL_HANDLE,"Grip action failure prevents optional grip spaces");
    Check(g_rightAimSpace&&g_leftAimSpace,"Grip action failure preserves both existing aim spaces");
    CheckProfiles(1,0,true,false);
    Reset(false,false,false,false,false,false,false,true,false);
    Check(CreateControllerActions(),"Left optional grip space failure is isolated");
    Check(g_supportGripPoseAction&&g_leftGripPoseSpace==XR_NULL_HANDLE&&g_rightGripPoseSpace&&
        g_rightAimSpace&&g_leftAimSpace,"Left grip space failure preserves right grip and both aim spaces");
    CheckProfiles(1,0,true,true);
    Reset(false,false,false,false,false,false,false,false,true);
    Check(CreateControllerActions(),"Right optional grip space failure is isolated");
    Check(g_supportGripPoseAction&&g_leftGripPoseSpace&&g_rightGripPoseSpace==XR_NULL_HANDLE&&
        g_rightAimSpace&&g_leftAimSpace,"Right grip space failure preserves left grip and both aim spaces");
    CheckProfiles(1,0,true,true);
    Reset(false,false,false,false,true,true);Check(CreateControllerActions(),"Unsupported Touch Pro profile cannot prevent other controller setup");CheckProfiles(1,0,true,true);
    Reset(false,false,false,false,true);Check(CreateControllerActions(),"Touch Pro and Oculus receive independent complete optional lists");CheckProfiles(1,1,true,true);
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
        fixtureNativeRouting=true;
        gestureRead(false); // A cached legacy gesture cannot cross routing modes.
        const auto savedReload=g_padState.weaponReloadBinding;
        const auto savedSwitch=g_padState.weaponSwitchBinding;
        g_padState.weaponReloadBinding=NativeVrActions_GestureBit(vr_mapping::Reload);
        g_padState.weaponSwitchBinding=NativeVrActions_GestureBit(vr_mapping::SwitchWeapon);
        gestureRead(true);
        fixtureNativeRouting=false;gestureRead(false);
        g_padState.weaponReloadBinding=savedReload;
        g_padState.weaponSwitchBinding=savedSwitch;
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
