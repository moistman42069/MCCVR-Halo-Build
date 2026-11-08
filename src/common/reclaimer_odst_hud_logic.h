#pragma once

// PROJECT RECLAIMER'S OWN COPY OF ODST'S HUD LAYOUT RECORD (game.cpp, reclaimer_odst_hud_record.inl). Reclaimer 0.10
// draws an ODST level's HUD from its own parse of ODST's HUD globals, where each of the block's five layout records is
// a byte buffer of its own, listed as {capacity, pointer, length}. The heap also holds hundreds of freed buffers with
// the 1280x720 record's bytes and two dozen lone entries naming them (FA, 2026-11-07); only one list named all five
// records in ODST's order, and its 1280x720 buffer was the one the client draws from.
// Pure logic; unit-tested in tests/core_tests.cpp.

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace reclaimer_odst_hud
{
// ODST's s_chud_curvature_info is 0x110 bytes (halo3odst/maps/sc100.map keeps its five 0x110 apart); its virtual width
// sits 0x94 in, after the resolution flags, the curvature points and the screen basis, as in Reach's record.
inline constexpr uint64_t kRecordLength = 0x110;
inline constexpr uintptr_t kWidthOffset = 0x94;
// The longest capacity a list entry for one record is taken to have.
inline constexpr uint64_t kMaxCapacity = 0x10000;

// Whether three words {capacity, pointer, length} list `record` as a whole ODST record.
inline constexpr bool ListsRecord(const uint64_t* words, uint64_t record) noexcept
{
    return record != 0 && words[1] == record && words[2] == kRecordLength && words[0] >= kRecordLength &&
        words[0] <= kMaxCapacity;
}

struct RecordShape
{
    uint32_t width;
    uint32_t height;
};

// ODST's layout block in its own order (halo3odst/maps/sc100.map 0xE778A88, 0x110 apart).
inline constexpr int kBlockRecords = 5;
inline constexpr RecordShape kBlockShapes[kBlockRecords] = {
    {1280, 720}, {1600, 580}, {920, 690}, {1180, 470}, {1138, 640}};

// Whether five {capacity, pointer, length} entries in a row list ODST's whole block: `shapes` holds the virtual width
// and height each entry's buffer holds.
inline constexpr bool ListsBlock(const uint64_t* words, const RecordShape* shapes) noexcept
{
    for (int i = 0; i < kBlockRecords; ++i)
    {
        if (!ListsRecord(words + 3 * i, words[3 * i + 1]) || shapes[i].width != kBlockShapes[i].width ||
            shapes[i].height != kBlockShapes[i].height)
            return false;
    }
    return true;
}

// Whether `value` is one of the `count` candidate records, sorted ascending.
inline bool IsCandidate(const uintptr_t* sorted, size_t count, uint64_t value) noexcept
{
    return count && std::binary_search(sorted, sorted + count, static_cast<uintptr_t>(value));
}
} // namespace reclaimer_odst_hud
