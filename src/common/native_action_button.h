#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "vr_action_mapping.h"

// H3/ODST and Reach/H4 have different native action-owner field widths.
// Common held counters/value prefix is independently established by each kit's
// XInput converter and abstraction updater. Never use this as a CE/H2 layout.
namespace native_action_button {
struct Counters {
    uint16_t milliseconds = 0;
    uint8_t frames = 0;
    uint8_t flags = 0;
    float amount = 0;
};
static_assert(sizeof(Counters) == 8);
struct Gen3Record {
    Counters state{};
    uint8_t action = 0xff, owner = 0xff;
    uint16_t padding = 0;
};
struct LaterRecord {
    Counters state{};
    uint32_t action = 0x7f, owner = 0x7f;
};
static_assert(sizeof(Gen3Record) == 12 && offsetof(Gen3Record, owner) == 9);
static_assert(sizeof(LaterRecord) == 16 && offsetof(LaterRecord, owner) == 12);

// The native converter advances once per native input update, not once per XR
// frame or accessor query. Counts saturate exactly like the native digital path.
inline void Advance(Counters& state, bool down, unsigned deltaMs) noexcept {
    if (!down) {
        state.milliseconds = 0;
        state.frames = 0;
        state.amount = 0;
        state.flags &= ~uint8_t{1};
        return;
    }
    state.frames = static_cast<uint8_t>(std::min(unsigned(state.frames) + 1u, 255u));
    state.milliseconds = static_cast<uint16_t>(std::min(
        unsigned(state.milliseconds) + std::min(deltaMs, 65535u), 65535u));
    state.amount = 1.0f;
}
inline constexpr bool IsGen3(GameTitle title) noexcept {
    return title == GameTitle::Halo3 || title == GameTitle::Halo3ODST;
}
// UI glyphs can name the shared gamepad alias while the actual player-control
// consumer has a dedicated keyboard action. H3's kit proves action 0x26 writes
// reload-held -> reload-edge -> primary weapon reload, independently of Use 3.
// Preserve NativeAction's existing transport lookup for fallback compatibility.
inline constexpr unsigned ConsumerAction(GameTitle title, vr_mapping::Action action) noexcept {
    if (title == GameTitle::Halo3 && action == vr_mapping::Reload) return 0x26;
    if (title == GameTitle::Halo3ODST && action == vr_mapping::Reload) return 0x2A;
    if (title == GameTitle::Halo4 && action == vr_mapping::Reload) return 0x31;
    return vr_mapping::NativeAction(title, action);
}
// Identical consumer IDs cannot be separated by swapping an abstract pointer.
inline constexpr uint32_t UniqueActionMask(GameTitle title) noexcept {
    uint32_t result = 0;
    if (!IsGen3(title) && title != GameTitle::HaloReach && title != GameTitle::Halo4)
        return 0;
    for (unsigned action = 0; action < vr_mapping::Count; ++action) {
        const auto id = ConsumerAction(title, static_cast<vr_mapping::Action>(action));
        if (id == vr_mapping::kNoAction) continue;
        bool unique = true;
        for (unsigned other = 0; other < vr_mapping::Count; ++other)
            if (other != action && ConsumerAction(title,
                static_cast<vr_mapping::Action>(other)) == id) unique = false;
        if (unique) result |= 1u << action;
    }
    return result;
}
inline constexpr int SemanticAction(GameTitle title, unsigned nativeAction) noexcept {
    const auto supported = UniqueActionMask(title);
    for (unsigned action = 0; action < vr_mapping::Count; ++action)
        if ((supported & (1u << action)) && ConsumerAction(title,
            static_cast<vr_mapping::Action>(action)) == nativeAction) return static_cast<int>(action);
    return -1;
}
struct Record {
    alignas(8) std::array<uint8_t, 16> bytes{};
    void Reset(GameTitle title, unsigned action) noexcept {
        bytes.fill(0);
        if (IsGen3(title)) {
            Gen3Record value{};value.action = static_cast<uint8_t>(action);
            std::memcpy(bytes.data(), &value, sizeof(value));
        } else {
            LaterRecord value{};value.action = action;
            std::memcpy(bytes.data(), &value, sizeof(value));
        }
    }
    Counters ReadCounters() const noexcept {
        Counters result{};std::memcpy(&result,bytes.data(),sizeof(result));return result;
    }
    void Advance(bool down, unsigned deltaMs) noexcept {
        auto state=ReadCounters();native_action_button::Advance(state,down,deltaMs);
        std::memcpy(bytes.data(),&state,sizeof(state));
    }
};
}
