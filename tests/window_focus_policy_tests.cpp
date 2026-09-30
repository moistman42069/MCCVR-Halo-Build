#include "../src/common/window_focus_policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
namespace {unsigned checks=0;void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}}
int main(){
    const UINT focusMessages[]{WM_ACTIVATEAPP,WM_ACTIVATE,WM_NCACTIVATE,WM_KILLFOCUS,WM_MOUSEACTIVATE};
    for(auto title:{GameTitle::HaloCE,GameTitle::Halo2,GameTitle::Halo3,GameTitle::Halo3ODST,GameTitle::HaloReach,GameTitle::Halo4}) {
        for(auto mode:{RuntimeMode::Shell,RuntimeMode::Unsupported})for(auto message:focusMessages)
            Check(DecideWindowFocus(title,mode,message)==WindowFocusAction::PassThrough,"shell/native authentication receives genuine focus transitions even with retained title");
        for(auto mode:{RuntimeMode::Gameplay,RuntimeMode::Loading,RuntimeMode::Paused,RuntimeMode::Cutscene,RuntimeMode::Vehicle,RuntimeMode::Turret,RuntimeMode::Dead}) {
            Check(DecideWindowFocus(title,mode,WM_ACTIVATEAPP)==WindowFocusAction::KeepActive,"active title retains app keepalive");
            Check(DecideWindowFocus(title,mode,WM_ACTIVATE)==WindowFocusAction::KeepActive,"active title retains window keepalive");
            Check(DecideWindowFocus(title,mode,WM_NCACTIVATE)==WindowFocusAction::KeepActive,"active title retains nonclient keepalive");
            Check(DecideWindowFocus(title,mode,WM_KILLFOCUS)==WindowFocusAction::SuppressLoss,"active title retains keyboard focus keepalive");
            Check(DecideWindowFocus(title,mode,WM_MOUSEACTIVATE)==WindowFocusAction::ActivateMouse,"active title retains mouse activation");
        }
        for(auto mode:{RuntimeMode::Shell,RuntimeMode::Gameplay,RuntimeMode::Paused,RuntimeMode::Vehicle})
            for(auto message:{WM_CHAR,WM_UNICHAR,WM_IME_CHAR,WM_KEYDOWN,WM_KEYUP,WM_SYSKEYDOWN,WM_SETFOCUS})
                Check(DecideWindowFocus(title,mode,UINT(message))==WindowFocusAction::PassThrough,"focus policy never swallows native text, IME, key or focus-gain messages");
    }
    for(auto title:{GameTitle::None,GameTitle::Unknown})for(auto mode:{RuntimeMode::Shell,RuntimeMode::Gameplay,RuntimeMode::Loading})
        for(auto message:focusMessages)Check(DecideWindowFocus(title,mode,message)==WindowFocusAction::PassThrough,"unknown title cannot force focus");
    std::printf("PASS: %u window focus policy checks\n",checks);
}
