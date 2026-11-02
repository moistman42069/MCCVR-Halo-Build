#pragma once
#include <atomic>
#include <cstdint>

// The desktop settings window while a game is running: it shows the game's own menu picture and sends its pointer and
// typing to it, so there is one menu with two screens. The game applies and saves every change, and a change made in the
// headset is on the desktop in the next frame. The game creates this block with the menu; a window that cannot open it
// (another GPU's texture, a sandboxed edition) falls back to showing the settings without saving them.
inline constexpr wchar_t kMenuMirrorMapping[] = L"Local\\HaloMCCVRMenuMirror";
inline constexpr uint32_t kMenuMirrorVersion = 1;
inline constexpr uint64_t kMenuMirrorAttachedMs = 1500; // the game draws the closed menu only for a window this recent
inline constexpr uint64_t kMenuMirrorGoneMs = 10000;    // long enough for a level load, when the game stops presenting
inline constexpr uint32_t kMenuMirrorChars = 64;

struct MenuMirrorShared
{
    std::atomic<uint32_t> version;
    std::atomic<uint32_t> format;       // DXGI_FORMAT of the picture
    std::atomic<uint64_t> texture;      // its shared handle
    std::atomic<uint64_t> gameBeatMs;   // GetTickCount64, which is the same in both processes
    std::atomic<uint64_t> desktopBeatMs;
    std::atomic<uint32_t> pointerHit;
    std::atomic<uint32_t> pointerPressed;
    std::atomic<float> pointerU;
    std::atomic<float> pointerV;
    std::atomic<int32_t> scrollMilli;   // adds up until the game takes it
    std::atomic<uint32_t> charsWritten; // typed characters, a ring; only the desktop writes it
    std::atomic<uint16_t> chars[kMenuMirrorChars];
};
static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free);
