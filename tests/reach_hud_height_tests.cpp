#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
enum class GameTitle { Halo3, HaloReach };
GameTitle activeTitle = GameTitle::HaloReach;
uint32_t activeGeneration = 3;
GameTitle TitleAdapter_GetActiveTitle() { return activeTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle) { return activeGeneration; }
bool stereo = true;
bool VR_IsStereoEnabled() { return stereo; }
std::atomic<bool> g_enabled{true};
struct
{
    std::atomic<uint32_t> generation{3}, activeCallbacks{0};
    std::atomic<bool> installed{true}, armed{true}, teardownRequested{false};
} g_reachCamera;
struct { float hud_vertical_offset = 16.0f; } g_config;
constexpr float kHudHeightMin = -300.0f, kHudHeightMax = 300.0f;
static std::atomic<bool> g_aimFramePass{false};
#include "../src/dll/reach_hud_height.inl"

unsigned calls = 0, checks = 0;
bool nativeResult = true, throwNative = false, revokeDuringNative = false;
bool nativeFlag = false;
bool recurseParent = false;
std::array<float, 13> nativeBasis{};
int seenUser = 0, seenAnchor = 0;
void* seenFlags = nullptr;
void* seenDrawData = nullptr;
void* seenBasis = nullptr;
bool* seenNativeFlag = nullptr;

bool __fastcall NativeAnchor(int user, int anchor, void* flags, void* data,
    void* basis, bool* nativeAnchorFlag)
{
    ++calls;
    seenUser=user; seenAnchor=anchor; seenFlags=flags; seenDrawData=data;
    seenBasis=basis; seenNativeFlag=nativeAnchorFlag;
    if (throwNative)
        RaiseException(0xE017DEAD, 0, 0, nullptr);
    if (revokeDuringNative)
        g_reachCamera.teardownRequested.store(true);
    if (recurseParent && anchor == 0)
    {
        const bool result = ReachHudAnchorBasisDetour(user,1,flags,data,basis,nativeAnchorFlag);
        // Native parent composition may scale the child's basis. Height must
        // be applied after that composition, once, in final virtual pixels.
        static_cast<float*>(basis)[11] *= 2.0f;
        return result;
    }
    if (basis && basis != reinterpret_cast<void*>(1))
        memcpy(basis, nativeBasis.data(), sizeof(nativeBasis));
    if (nativeAnchorFlag) *nativeAnchorFlag = nativeFlag;
    return nativeResult;
}

void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); ExitProcess(1); }
}

void Reset()
{
    activeTitle=GameTitle::HaloReach; activeGeneration=3; stereo=true;
    g_enabled=true; g_reachCamera.generation=3; g_reachCamera.installed=true;
    g_reachCamera.armed=true; g_reachCamera.teardownRequested=false;
    g_reachHudHeightEnabled=true; g_reachHudHeightRedirected=false; g_aimFramePass=false;
    g_reachHudHeightApplied=0; g_reachHudHeightRefused=0;
    g_reachOrigHudAnchorBasis=&NativeAnchor; g_config.hud_vertical_offset=16;
    calls=0; nativeResult=true; throwNative=false; revokeDuringNative=false;
    recurseParent=false; nativeFlag=false;
    for (size_t i=0;i<nativeBasis.size();++i) nativeBasis[i]=float(i*17+3);
}

void Run(bool translated, const char* name)
{
    std::array<float,13> output{};
    std::array<uint64_t,8> drawData{};
    const auto pristineDrawData=drawData;
    uint32_t placementFlags=0xFEDCBA98;
    bool nativeAnchorFlag=!nativeFlag;
    const auto beforeCalls=calls;
    const bool returned=ReachHudAnchorBasisDetour(2, 0x1234ABCD,
        &placementFlags, drawData.data(), output.data(), &nativeAnchorFlag);
    Check(calls==beforeCalls+1,"native called exactly once");
    Check(returned==nativeResult,"native success/failure preserved");
    Check(seenUser==2 && seenAnchor==0x1234ABCD &&
        seenFlags==&placementFlags && seenDrawData==drawData.data() &&
        seenBasis==output.data() && seenNativeFlag==&nativeAnchorFlag,
        "six full arguments forwarded without narrowing");
    Check(drawData==pristineDrawData && placementFlags==0xFEDCBA98 &&
        nativeAnchorFlag==nativeFlag,"argument four and native sixth-argument flag preserved");
    auto expected=nativeBasis;
    if (translated) expected[11]-=g_config.hud_vertical_offset;
    Check(memcmp(expected.data(),output.data(),sizeof(expected))==0,name);
    Check(g_reachCamera.activeCallbacks==0,"callback accounting drained");
}

bool CatchNativeException()
{
    __try { ReachHudAnchorBasisDetour(0,1,nullptr,nullptr,nullptr,nullptr); }
    __except (GetExceptionCode()==0xE017DEAD ?
        EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
}

int main()
{
    for (float height : {-300.0f,-16.0f,0.0f,16.0f,300.0f})
    for (bool flag : {false,true})
    { Reset(); nativeFlag=flag; g_config.hud_vertical_offset=height;
      Run(true,"only virtual pixel Y translated for either native anchor flag"); }
    Reset(); nativeResult=false; Run(false,"failed native anchor unchanged");
    Reset(); g_reachHudHeightRedirected=true; Run(false,"captured/suppressed reticle unchanged");
    Reset(); g_aimFramePass=true; Run(false,"the aim frame's HUD height is the game's");
    Reset(); g_reachHudHeightEnabled=false; Run(false,"feature unavailable stays stock");
    Reset(); activeTitle=GameTitle::Halo3; Run(false,"other title stays stock");
    Reset(); activeGeneration=4; Run(false,"stale generation stays stock");
    Reset(); g_reachCamera.generation=0; Run(false,"zero generation stays stock");
    Reset(); g_reachCamera.installed=false; Run(false,"uninstalled stays stock");
    Reset(); g_reachCamera.armed=false; Run(false,"unarmed stays stock");
    Reset(); g_reachCamera.teardownRequested=true; Run(false,"teardown stays stock");
    Reset(); g_enabled=false; Run(false,"disabled VR stays stock");
    Reset(); stereo=false; Run(false,"flat rendering stays stock");
    Reset(); revokeDuringNative=true; Run(false,"ownership rechecked after original returns");
    for (float height : {-301.0f,301.0f,std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()})
    { Reset(); g_config.hud_vertical_offset=height; Run(false,"invalid configuration stays stock");
      Check(g_reachHudHeightRefused==1,"numeric refusal reported off hook"); }
    Reset(); nativeBasis[11]=std::numeric_limits<float>::quiet_NaN();
    Run(false,"nonfinite native Y remains untouched");
    Reset(); Check(ReachHudAnchorBasisDetour(0,1,nullptr,nullptr,nullptr,nullptr),"null basis preserves native result");
    Check(calls==1 && g_reachCamera.activeCallbacks==0,"null basis call accounted");
    Reset(); Check(ReachHudAnchorBasisDetour(0,1,nullptr,nullptr,reinterpret_cast<void*>(1),nullptr),"bad optional basis preserves native result");
    Check(g_reachHudHeightRefused==1 && g_reachHudHeightEnabled &&
        g_reachCamera.armed && g_reachCamera.activeCallbacks==0,"SEH isolated to height and counted");
    Reset(); throwNative=true; Check(CatchNativeException(),"native exception is propagated");
    Check(calls==1 && g_reachCamera.activeCallbacks==0 && g_reachHudAnchorDepth==0,
        "native exception drains callback and recursion accounting");
    Reset(); recurseParent=true;
    std::array<float,13> composed{};
    Check(ReachHudAnchorBasisDetour(0,0,nullptr,nullptr,composed.data(),nullptr),
        "parent anchor preserves nested native result");
    Check(calls==2 && g_reachHudHeightApplied==1 && g_reachHudAnchorDepth==0 &&
        composed[11]==nativeBasis[11]*2.0f-16.0f,
        "parent anchor translates once after complete native composition");
    Reset(); g_reachOrigHudAnchorBasis=nullptr;
    Check(!ReachHudAnchorBasisDetour(0,1,nullptr,nullptr,nullptr,nullptr) &&
        g_reachCamera.activeCallbacks==0,"absent original safely refuses");
    std::printf("Reach production HUD height wrapper: %u checks passed\n",checks);
    return 0;
}
