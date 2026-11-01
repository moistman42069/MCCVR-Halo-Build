// Mod hotkeys are bare keys (src/common/hotkey_logic.h): a key that arrives with Ctrl, Alt or a Windows key held
// is another tool's chord and is passed on untouched.
#include "hotkey_logic.h"

#include <cstdio>

namespace
{
int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
} // namespace

int main()
{
    using namespace hotkey;
    const Modifiers none{};
    Modifiers ctrl;  ctrl.ctrl = true;
    Modifiers alt;   alt.alt = true;
    Modifiers win;   win.win = true;
    Modifiers all;   all.ctrl = all.alt = all.win = true;

    // The table: every key the mod acts on, and a few it does not.
    const unsigned ours[]{kF1, kF2, kF3, kF6, kF8, kF9, kF10, kF11, kPageUp, kPageDown, kEnd, kHome, kInsert};
    for (unsigned vk : ours)
        Check(IsModHotkey(vk), "every documented key is a mod hotkey");
    const unsigned notOurs[]{0x73 /*F4*/, 0x74 /*F5*/, 0x76 /*F7*/, 0x7B /*F12*/, 0x59 /*Y*/, 0x0D /*Enter*/, 0x1B /*Esc*/};
    for (unsigned vk : notOurs)
        Check(!IsModHotkey(vk), "keys the mod does not use are not mod hotkeys");

    // A bare press acts; the same key in a chord does not.
    for (unsigned vk : ours)
    {
        const bool f10 = vk == kF10;
        Check(IsHotkeyPress(true, false, false, vk, none), "a bare key-down acts");
        Check(!IsHotkeyPress(true, false, true, vk, none), "an auto-repeat does not act");
        Check(!IsHotkeyPress(true, false, false, vk, ctrl), "Ctrl + key does not act");
        Check(!IsHotkeyPress(true, false, false, vk, win), "Win + key does not act");
        Check(!IsHotkeyPress(false, true, false, vk, alt), "Alt + key (a system key-down) does not act");
        Check(!IsHotkeyPress(true, false, false, vk, all), "Ctrl + Alt + Win + key does not act");
        Check(IsHotkeyPress(false, true, false, vk, none) == f10,
              "a system key-down with no modifier acts for F10 only (Windows delivers a plain F10 that way)");
        Check(!IsHotkeyPress(false, false, false, vk, none), "a message that is not a key-down does not act");
        Check(IsForeignChord(true, false, false, vk, ctrl), "Ctrl + one of our keys is reported as a foreign chord");
        Check(IsForeignChord(false, true, false, vk, alt), "Alt + one of our keys is reported as a foreign chord");
        Check(!IsForeignChord(true, false, false, vk, none), "a bare press is not a foreign chord");
        Check(!IsForeignChord(true, false, true, vk, ctrl), "a repeating chord is reported once, not per repeat");
    }

    // The incident: Ctrl+F9, the player's replay-capture hotkey.
    Check(!IsHotkeyPress(true, false, false, kF9, ctrl), "Ctrl+F9 does not trim the pitch");
    Check(IsForeignChord(true, false, false, kF9, ctrl), "Ctrl+F9 is logged as another tool's chord");
    // The older one: Alt+F4 was never ours to act on, and is not reported either (F4 is no longer a hotkey).
    Check(!IsHotkeyPress(false, true, false, 0x73, alt), "Alt+F4 does not act");
    Check(!IsForeignChord(false, true, false, 0x73, alt), "Alt+F4 is not reported as a chord on one of our keys");

    // Keys that are not ours never act, bare or not.
    for (unsigned vk : notOurs)
    {
        Check(!IsHotkeyPress(true, false, false, vk, none), "a key the mod does not use never acts");
        Check(!IsForeignChord(true, false, false, vk, ctrl), "nor is it reported as a chord");
    }

    Check(Bare(none) && !Bare(ctrl) && !Bare(alt) && !Bare(win), "bare means no Ctrl, Alt or Windows key");

    std::printf("hotkeys: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
