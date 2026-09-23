#pragma once
#include "../common/runtime_types.h"
#include <cstdint>
#include <memory>
#include <vector>

struct NativeSubtitleImage
{
    static constexpr uint32_t width = 2048, height = 512;
    GameTitle title = GameTitle::None;
    uint32_t generation = 0;
    uint64_t expiresAtMs = 0, revision = 0;
    bool theatre = false;
    std::shared_ptr<const std::vector<uint8_t>> rgba;
};

// Worker only: install the independent native observer, retire stale captions,
// and rasterize changed text. No work is added to title camera ownership.
void NativeSubtitles_Poll(bool levelRunning);
std::shared_ptr<const NativeSubtitleImage> NativeSubtitles_Read();
void NativeSubtitles_SetReachSources(uint32_t generation,
    uintptr_t gameplayReturn, uintptr_t theatreReturn) noexcept;

// Bounded, nonblocking ingress for independently verified title sources.
// Copies UTF-16 immediately; never retains an engine-owned string pointer.
void NativeSubtitles_Capture(GameTitle title, uint32_t generation,
    const wchar_t* text, float seconds, bool theatre,
    uint8_t channel=0, bool refreshed=false) noexcept;
