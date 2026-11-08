#pragma once
// A MOD HOTKEY IS A BARE KEY.
//
// The mod's keyboard controls (F1 menu, F2 head tracking, F3 recenter, F6 leaning, F8/F9 pitch trim, F10 screen
// follow, F11 stereo, Page Up/Down lean scale, Home/End gun scale, Insert controller aim) are read from the game
// window's key-down messages. A player in a headset cannot see the keyboard, and other software sends keys of its
// own. A key that arrives with Ctrl, Alt or a Windows key held is somebody else's chord, not ours.
//
// WHAT A FOREIGN CHORD DOES WHEN IT IS TAKEN AS OURS:
//  * SteamVR's exit / dashboard flow sends Alt+F4. With F4 as a hotkey that flips the yaw sign, that inverts
//    head-turn and hand-aim. (F4, F5 and F7 are not hotkeys.)
//  * Ctrl+F9 can be another program's hotkey (a replay-capture tool's, for one). Each use adds 2 degrees of pitch
//    trim, and the reticle sits above every shot by the total (pitch_trim_logic.h has the rest).
//
// Shift is deliberately NOT treated as a foreign modifier: a keyboard player may well be holding it (sprint,
// crouch) when they reach for a hotkey, and tools do not use Shift alone for global hotkeys.
//
// Pure and unit-tested (tests/hotkey_tests.cpp). Virtual-key values are the Win32 ones, written out so this
// header needs no platform include.

namespace hotkey
{
struct Modifiers
{
    bool ctrl = false, alt = false, win = false;
};
inline bool Bare(const Modifiers& m) noexcept { return !m.ctrl && !m.alt && !m.win; }

inline constexpr unsigned kF1 = 0x70, kF2 = 0x71, kF3 = 0x72, kF6 = 0x75, kF8 = 0x77, kF9 = 0x78, kF10 = 0x79,
                          kF11 = 0x7A, kPageUp = 0x21, kPageDown = 0x22, kEnd = 0x23, kHome = 0x24, kInsert = 0x2D;

// Is this virtual key one the mod acts on at all?
inline bool IsModHotkey(unsigned vk) noexcept
{
    switch (vk)
    {
    case kF1: case kF2: case kF3: case kF6: case kF8: case kF9: case kF10: case kF11:
    case kPageUp: case kPageDown: case kEnd: case kHome: case kInsert:
        return true;
    default:
        return false;
    }
}

// Should the mod act on this key message?
//   keyDown    WM_KEYDOWN
//   sysKeyDown WM_SYSKEYDOWN (Windows delivers a plain F10 this way, with no Alt held)
//   repeat     the message is an auto-repeat (lParam bit 30)
inline bool IsHotkeyPress(bool keyDown, bool sysKeyDown, bool repeat, unsigned vk, const Modifiers& m) noexcept
{
    if (repeat || !IsModHotkey(vk) || !Bare(m))
        return false;
    return keyDown || (sysKeyDown && vk == kF10);
}

// Was one of our keys pressed as part of somebody else's chord? (For one log line; the key is passed on.)
inline bool IsForeignChord(bool keyDown, bool sysKeyDown, bool repeat, unsigned vk, const Modifiers& m) noexcept
{
    return !repeat && (keyDown || sysKeyDown) && IsModHotkey(vk) && !Bare(m);
}
} // namespace hotkey
