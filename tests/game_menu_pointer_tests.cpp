// Execute the shipping window-thread transport against fake Windows endpoints.
// No real cursor movement, input injection, timers, windows or game access.
#include <windows.h>
#include <cstdio>
#include <limits>
#include <vector>
#include "config.h"
#include "game_menu_pointer.h"
#include "game_menu_pointer_xr.h"

static HWND fakeWindow = reinterpret_cast<HWND>(uintptr_t{1});
static bool focused = true, minimized = false, f1 = false, pauseScreen = false;
static bool physicalDown = false, sendOk = true, postOk = true, moveOk = true;
static bool timerOk = true, covered = false;
static DWORD now = 1000;
static POINT cursor{};
static int width = 1000, height = 500, posts = 0, moves = 0, logs = 0;
static RuntimeMode mode = RuntimeMode::Shell;
static std::vector<DWORD> buttons;
static HWND FakeForeground() { return focused ? fakeWindow : nullptr; }
static BOOL FakeIconic(HWND) { return minimized; }
static BOOL FakeRect(HWND, RECT* r) { *r={0,0,width,height}; return TRUE; }
static BOOL FakeClient(HWND, POINT* p) { p->x += 100; p->y += 50; return TRUE; }
static HWND FakeUnder(POINT) { return covered ? nullptr : fakeWindow; }
static BOOL FakeChild(HWND, HWND) { return FALSE; }
static SHORT FakeKey(int) { return physicalDown ? SHORT(0x8000) : 0; }
static BOOL FakeMove(int x, int y) { ++moves; cursor={x,y}; return moveOk; }
static BOOL FakeCursor(POINT* p) { *p=cursor; return TRUE; }
static UINT FakeSend(UINT count, INPUT* input, int) {
    if (!sendOk) return 0;
    for (UINT i=0;i<count;++i) buttons.push_back(input[i].mi.dwFlags);
    return count;
}
static UINT_PTR FakeTimer(HWND, UINT_PTR, UINT, TIMERPROC) { return timerOk ? 42 : 0; }
static BOOL FakeKill(HWND, UINT_PTR) { return TRUE; }
static UINT FakeRegister(const wchar_t*) { return 0xC123; }
static BOOL FakePost(HWND, UINT, WPARAM, LPARAM) { ++posts; return postOk; }
static DWORD FakeTick() { return now; }
#define GetForegroundWindow FakeForeground
#define IsIconic FakeIconic
#define GetClientRect FakeRect
#define ClientToScreen FakeClient
#define WindowFromPoint FakeUnder
#define IsChild FakeChild
#define GetAsyncKeyState FakeKey
#define SetCursorPos FakeMove
#define GetCursorPos FakeCursor
#define SendInput FakeSend
#define SetTimer FakeTimer
#define KillTimer FakeKill
#define RegisterWindowMessageW FakeRegister
#define PostMessageW FakePost
#define GetTickCount FakeTick
#include "../src/dll/native_menu_pointer.cpp"

void Logf(const char*, ...) { ++logs; }
const wchar_t* LogDirectory() { return L""; }
bool Menu_IsOpen() { return f1; }
RuntimeMode TitleAdapter_GetRuntimeMode() { return mode; }
bool VR_IsPausePresentation() { return pauseScreen; }
bool VR_IsPausePresentationTarget() { return pauseScreen; }
bool VR_IsCutsceneTheaterActive() { return false; }
static bool hostPanelScreen = false;
bool D3D_HostUiPanelIsScreen() { return hostPanelScreen; }

static int checks = 0, failures = 0;
static void Check(bool ok, const char* name) {
    ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", name); }
}
static void Frame(bool active, bool trigger=false, float u=.5f, float v=.5f) {
    ++now; NativeMenuPointer_Publish(active,u,v,trigger);
    NativeMenuPointer_Message(0xC123);
}
int main()
{
    using namespace game_menu_pointer;
    for (int m=0;m<=int(RuntimeMode::Unsupported);++m)
        Check(MenuMode(RuntimeMode(m),false)==(m==int(RuntimeMode::Shell)||m==int(RuntimeMode::Paused)||m==int(RuntimeMode::Unsupported)),
            "preloaded shell and pause admit pointing; live gameplay/loading do not");
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.pose={{0,0,0,1},{0,0,-2.4f}};quad.size={4,4*2100.0f/2912};
    XrPosef head{{0,0,0,1},{0,0,0}},aim=head;
    float hitU=0,hitV=0;
    Check(RayHit(quad,aim,head,false,hitU,hitV)&&std::fabs(hitU-.5f)<.0001f&&std::fabs(hitV-.5f)<.0001f,
        "main-menu ray hits actual 2912x2100 stock quad center");
    for(bool view:{false,true}) for(float yaw:{0.0f,2.474877f}) for(float u:{.1f,.5f,.9f}) for(float v:{.1f,.5f,.9f}) {
        const XrQuaternionf rotation{0,std::sin(yaw/2),0,std::cos(yaw/2)};
        const XrVector3f anchor{-3,-.2f,-1.3f};
        head={rotation,anchor};aim.orientation=rotation;
        const auto hand=RotatePoint(rotation,{(u-.5f)*4,(.5f-v)*quad.size.height,0});
        aim.position={anchor.x+hand.x,anchor.y+hand.y,anchor.z+hand.z};
        quad.pose={{0,0,0,1},{0,0,-2.4f}};
        if(!view) {
            quad.pose.orientation=rotation;
            const auto center=RotatePoint(rotation,{0,0,-2.4f});
            quad.pose.position={anchor.x+center.x,anchor.y+center.y,anchor.z+center.z};
        }
        Check(RayHit(quad,aim,head,view,hitU,hitV)&&std::fabs(hitU-u)<.0001f&&std::fabs(hitV-v)<.0001f,
            "rotated/recentered local shell and view-space pause map full menu");
        const auto cursorQuad=CursorQuad(quad,XR_NULL_HANDLE,u,v,false);
        auto cursorLocal=InversePoint(quad.pose,cursorQuad.pose.position);
        Check(std::fabs(cursorLocal.x-(u-.5f)*4)<.0001f&&std::fabs(cursorLocal.y-(.5f-v)*quad.size.height)<.0001f,
            "visible cursor overlays exact delivered hover coordinates");
        Check(cursorQuad.space==quad.space&&cursorQuad.subImage.imageRect.extent.width==32&&
            cursorQuad.size.width>CursorQuad(quad,XR_NULL_HANDLE,u,v,true).size.width,
            "cursor keeps screen space and contracts on click");
    }
    quad.pose={{0,0,0,1},{0,0,-2.4f}};head={{0,0,0,1},{0,0,0}};aim=head;
    aim.orientation={0,1,0,0};Check(!RayHit(quad,aim,head,false,hitU,hitV),"backward ray rejects");
    aim=head;aim.position.x=3;Check(!RayHit(quad,aim,head,false,hitU,hitV),"off-menu ray rejects");
    aim=head;aim.position.z=-3;Check(!RayHit(quad,aim,head,false,hitU,hitV),"behind-menu ray rejects");
    aim=head;aim.orientation.w=0;Check(!RayHit(quad,aim,head,false,hitU,hitV),"invalid pose rejects");
    aim=head;quad.size.width=std::numeric_limits<float>::quiet_NaN();
    Check(!RayHit(quad,aim,head,false,hitU,hitV),"nonfinite quad rejects");
    Check(CursorPixel(0,0)==0&&CursorPixel(16,16)==0&&CursorPixel(16,5)==0xFFFFFFFFu&&
        CursorPixel(16,1)==0xFF101010u,"cursor has transparent surround/center and contrasting ring");
    Pointer p;
    Check(!FreshActive(Pack(now,true,std::numeric_limits<float>::quiet_NaN(),.5f,false),now),"NaN rejects");
    Check(!FreshActive(Pack(now,true,1.01f,.5f,false),now),"outside rejects");
    Check(FreshActive(Pack(0xfffffff0,true,.5f,.5f,false),10),"tick wrap stays fresh");
    Check(!FreshActive(Pack(now,true,.5f,.5f,false),now+201),"stale rejects");
    auto edge=p.Update(Pack(now,true,1,1,false),now,true,1000,500);
    Check(edge.x==999&&edge.y==499,"last pixel clamps inside client");
    Check(!p.Update(Pack(now,true,.5f,.5f,true),now,true,0,500).move,"zero client rejects");

    NativeMenuPointer_Init(fakeWindow);
    g_config.game_menu_pointer=false;
    Frame(true); Check(moves==0&&buttons.empty(),"disabled never moves or clicks");
    g_config.game_menu_pointer=true;
    Frame(true,true); Check(buttons.empty(),"held trigger at activation never clicks");
    Frame(true); Check(cursor.x>=599&&cursor.x<=600&&cursor.y>=299&&cursor.y<=300,"UV maps physical client plus screen origin");
    Frame(true,true); Check(mouseDown&&buttons.back()==MOUSEEVENTF_LEFTDOWN,"trigger presses native mouse");
    Frame(true,true); Check(buttons.size()==1,"held trigger does not repeat presses");
    Frame(false,true); Check(!mouseDown&&buttons.back()==MOUSEEVENTF_LEFTUP,"miss releases");
    Frame(true,true); Check(!mouseDown,"reenter while held requires release");
    Frame(true); Frame(true,true);
    focused=false; NativeMenuPointer_Message(WM_KILLFOCUS);
    Check(!mouseDown,"focus loss releases");
    const int beforeMoves=moves; Frame(true,true);
    Check(moves==beforeMoves,"background cannot move pointer");
    focused=true; Frame(true); Frame(true,true);
    float visualU=0,visualV=0;bool visualPressed=false;
    Check(NativeMenuPointer_ReadVisual(visualU,visualV,visualPressed)&&visualPressed,
        "visible cursor receives successfully delivered mouse press");
    f1=true; Watchdog(nullptr,0,0,0); Check(!mouseDown,"F1 releases"); f1=false;
    Check(!NativeMenuPointer_ReadVisual(visualU,visualV,visualPressed),"F1 cancellation hides native cursor");
    Frame(true); Frame(true,true); now+=201; Watchdog(nullptr,0,0,0);
    Check(!mouseDown&&!timer,"stalled render watchdog releases and stops timer");
    Frame(true); Frame(true,true); mode=RuntimeMode::Gameplay;
    Watchdog(nullptr,0,0,0); Check(!mouseDown,"gameplay transition releases");
    Frame(true); Check(!NativeMenuPointer_ConsumesTrigger(),"gameplay never consumes trigger");
    hostPanelScreen=true; Frame(true);
    Check(NativeMenuPointer_ConsumesTrigger(),"a host's screen shown as a panel is a menu during gameplay");
    hostPanelScreen=false; Watchdog(nullptr,0,0,0); Frame(true);
    Check(!NativeMenuPointer_ConsumesTrigger(),"and the pointer goes with that screen");
    mode=RuntimeMode::Paused; Frame(true);
    Check(NativeMenuPointer_ConsumesTrigger(),"pause hover consumes VR trigger only");
    mode=RuntimeMode::Unsupported; Frame(true); Frame(true,true);
    Check(mouseDown&&NativeMenuPointer_ConsumesTrigger(),"reported six-module shell now delivers trigger click");
    Frame(true);
    physicalDown=true; Frame(true,true); Check(!mouseDown,"physical drag remains native"); physicalDown=false;
    covered=true; Frame(true); Check(!mouseDown,"covered window cannot click"); covered=false;
    Frame(true); sendOk=false; Frame(true,true); Check(!mouseDown&&logs>0,"send failure stays optional and logs"); sendOk=true;
    Frame(true); Frame(true,true); sendOk=false; Frame(false);
    Check(mouseDown&&timer,"failed release retains retry ownership"); sendOk=true;
    Watchdog(nullptr,0,0,0); Check(!mouseDown,"failed release retries");
    timerOk=false; Frame(true); Check(!mouseDown,"no watchdog means no press"); timerOk=true;
    Frame(true); moveOk=false; Frame(true,true); Check(!mouseDown,"failed positioning cannot click wrong location"); moveOk=true;
    Frame(false); const int oldPosts=posts;
    NativeMenuPointer_Publish(true,.2f,.3f,false);
    NativeMenuPointer_Publish(true,.8f,.7f,false);
    Check(posts==oldPosts+1,"render messages coalesce");
    NativeMenuPointer_Message(0xC123);
    Frame(false); postOk=false; NativeMenuPointer_Publish(true,.5f,.5f,false);
    Check(!queued.load(),"failed post permits next frame retry"); postOk=true;
    NativeMenuPointer_Publish(true,.5f,.5f,false); NativeMenuPointer_Message(0xC123);
    Check(!queued.load(),"retry drains normally");
    Frame(true,true); g_config.game_menu_pointer=false; Watchdog(nullptr,0,0,0);
    Check(!mouseDown,"toggle off releases");
    {
        // The right stick as a wheel: a flick moves one notch, a hold repeats, faster when pushed further.
        game_menu_pointer::StickWheel wheel;
        Check(wheel.Step(0.f,1000)==0,"centred stick: no wheel");
        Check(wheel.Step(.2f,1050)==0,"inside the deadzone: no wheel");
        Check(wheel.Step(.5f,1100)==1,"a flick up moves one notch at once");
        int upward=0;uint32_t t=1100;
        for(int i=0;i<20;++i)upward+=wheel.Step(1.f,t+=50);
        Check(upward>=9&&upward<=11,"held fully: about ten notches a second");
        wheel.Step(0.f,t+=50);
        Check(wheel.Step(-.5f,t+=50)==-1,"released, then a flick down moves one notch down");
        int slow=0;
        for(int i=0;i<20;++i)slow+=wheel.Step(-.3f,t+=50);
        Check(slow<0&&slow>-upward,"pushed a little: slower");
        wheel.Reset();
        Check(wheel.Step(1.f,t+=50)==1,"after a reset it starts afresh");
    }
    {
        // Sideways lists: right goes down the list, left back up; the further push is the wheel.
        using game_menu_pointer::StickWheelAxis;
        Check(StickWheelAxis(0.f,1.f)==1.f&&StickWheelAxis(0.f,-1.f)==-1.f,"up and down are the wheel as before");
        Check(StickWheelAxis(1.f,0.f)==-1.f,"right scrolls down");
        Check(StickWheelAxis(-1.f,0.f)==1.f,"left scrolls up");
        Check(StickWheelAxis(.8f,.3f)==-.8f,"mostly right is right");
        Check(StickWheelAxis(.3f,-.8f)==-.8f,"mostly down is down");
        Check(StickWheelAxis(0.f,0.f)==0.f,"centred: no wheel");
    }
    {
        // A click holds where it was aimed; a hand that goes on moving is a drag.
        game_menu_pointer::ClickLock lock;
        float u=.5f,v=.5f;lock.Apply(.1f,u,v);
        Check(u==.5f&&v==.5f,"no pull: the point is the ray's");
        u=.5f;v=.5f;lock.Apply(.2f,u,v);
        u=.51f;v=.49f;lock.Apply(.7f,u,v);
        Check(u==.5f&&v==.5f,"a pull's jolt does not move the point");
        u=.6f;v=.5f;lock.Apply(.7f,u,v);
        Check(u==.6f,"past the drag distance the point follows");
        u=.5f;v=.5f;lock.Apply(.7f,u,v);
        Check(u==.5f,"and keeps following for the rest of the pull");
        lock.Apply(0.f,u,v);
        u=.7f;v=.7f;lock.Apply(.2f,u,v);
        u=.71f;lock.Apply(.2f,u,v);
        Check(u==.7f,"the next pull locks afresh");
    }
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
