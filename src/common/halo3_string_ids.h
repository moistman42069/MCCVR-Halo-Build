// HALO 3 STRING IDS: the text of a string id ("canopy", "r_hand") in the map being played. Tag data names its
// regions, nodes, markers and the like by string id; this is what turns one into the name a camera file or a parts
// file uses. The pure half of src/dll/halo3_string_ids.inl. No game, engine or OpenXR includes.
//
// EVIDENCE CLASS: RETAIL-DERIVED AND CHECKED OFFLINE. The layout below is read from retail halo3.dll by disassembly
// (its string-id loader +0x108B14, the hash builder +0x108C40, string_id_get_string +0x108BE0) and checked against
// the stock maps (Shrine, Sandbox, The Covenant): in each, the buffer's strings are in id order, it holds exactly
// the header's count of them, and its first 0xD24 are the module's own table's names, word for word.
//
// TWO PARTS, ONE NUMBERING. Every string id has a number n, its place in the loaded map's buffer:
//   * The GLOBAL ids are halo3.dll's own: a static table of { uint64 id; const char* name } entries, in id order,
//     where id = namespace << 19 | index. Global id number n is entry n. On retail, 0xD24 entries in ten namespaces;
//     namespace 0 holds 1177 of them (indexes 0..0x498).
//   * The MAP'S OWN ids are namespace 0 from the next index on (0x499 on retail): id number n = 0xD24 + index -
//     0x499. Both numbers are taken from the table itself here (its length, and one past namespace 0's last index).
// Tag data stores a string id bare ("canopy" in Shrine is 0x1C3D after the 0xD24 globals; "head" is 0x9F).
//
// THE MAP'S STRINGS. Its cache header (the same 0x4000-byte copy the tag names come from, halo3_tag_names.h) holds
// +0x30 the number of string ids and +0x38 the size of their buffer. halo3.dll's loader reads the buffer -- every
// string, globals first, NUL after each -- into one static buffer in the module, and stores the count in a global
// beside it; the per-string offsets it reads are freed once the hash maps are built, so the strings are found here
// by walking the buffer.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace halo3_string_ids
{
// ---- the header (halo3_tag_names.h has the rest of it) ----
inline constexpr size_t kCountOffset = 0x30;
inline constexpr size_t kBufferOffset = 0x34;
inline constexpr size_t kBufferSizeOffset = 0x38;
inline constexpr size_t kOffsetTableOffset = 0x3C;
// The most the mod copies. The stock maps take 0x53910 (Sandbox) to 0xA31B6 (The Covenant).
inline constexpr uint32_t kMaxBufferCopy = 0x400000; // 4 MB
inline constexpr uint32_t kMaxCount = 0x40000;
// A global table this long or longer is not the one this was read from (retail: 0xD24).
inline constexpr uint32_t kMaxGlobals = 0x4000;
inline constexpr uint32_t kIndexBits = 19;
inline constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;

inline uint32_t ReadU32(const uint8_t* at)
{
    uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
inline int32_t ReadI32(const uint8_t* at)
{
    int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
inline uint64_t ReadU64(const uint8_t* at)
{
    uint64_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// ---- the strings ----
enum class Fault : uint8_t
{
    None = 0,
    NoGlobals,      // the module's table is empty or implausibly long
    GlobalsOrder,   // its ids are not in increasing order, or a namespace past 15
    NoMapIds,       // namespace 0 is empty: no first map id to number from
    Count,          // the header's count is below the globals' or above kMaxCount
    Size,           // the buffer is empty, larger than kMaxBufferCopy, or does not end in a NUL
    Strings,        // it does not hold exactly the header's count of strings
    Disagrees,      // a global's text in the buffer is not the module table's name for it
    Total
};
inline const char* FaultName(Fault fault)
{
    switch (fault)
    {
    case Fault::None: return "read";
    case Fault::NoGlobals: return "the module's string table is empty or too long";
    case Fault::GlobalsOrder: return "the module's string table is not in id order";
    case Fault::NoMapIds: return "the module's string table has no namespace 0";
    case Fault::Count: return "the header's string count does not fit the module's table";
    case Fault::Size: return "the string buffer is empty, too large or unterminated";
    case Fault::Strings: return "the string buffer does not hold the header's count of strings";
    case Fault::Disagrees: return "the string buffer's globals are not the module's own names";
    default: return "?";
    }
}

// One map's strings. Build() takes the module's global ids (in table order) and the map's buffer, both copied by
// the caller; Text() then answers without allocating.
class Strings
{
public:
    Fault Build(const uint64_t* globalIds, uint32_t globalCount, const char* buffer, uint32_t size, uint32_t count)
    {
        Clear();
        if (!globalIds || !globalCount || globalCount > kMaxGlobals)
            return Fault::NoGlobals;
        uint32_t lastZero = 0;
        bool anyZero = false;
        ids_.resize(globalCount);
        for (uint32_t n = 0; n < globalCount; ++n)
        {
            const uint64_t id = globalIds[n];
            if (id > 0xFFFFFFFFull || (id >> kIndexBits) > 15 || (n && id <= globalIds[n - 1]))
            {
                Clear();
                return Fault::GlobalsOrder;
            }
            ids_[n] = static_cast<uint32_t>(id);
            if ((id >> kIndexBits) == 0)
            {
                lastZero = static_cast<uint32_t>(id);
                anyZero = true;
            }
        }
        if (!anyZero)
        {
            Clear();
            return Fault::NoMapIds;
        }
        if (count < globalCount || count > kMaxCount)
        {
            Clear();
            return Fault::Count;
        }
        if (!buffer || !size || size > kMaxBufferCopy || buffer[size - 1] != '\0')
        {
            Clear();
            return Fault::Size;
        }
        starts_.reserve(count);
        uint32_t at = 0;
        while (at < size)
        {
            if (starts_.size() == count)
            {
                Clear();
                return Fault::Strings; // more strings than the header says
            }
            starts_.push_back(at);
            const void* end = std::memchr(buffer + at, 0, size - at);
            at = static_cast<uint32_t>(static_cast<const char*>(end) - buffer) + 1;
        }
        if (starts_.size() != count)
        {
            Clear();
            return Fault::Strings;
        }
        buffer_ = buffer;
        size_ = size;
        firstMapIndex_ = lastZero + 1;
        return Fault::None;
    }
    void Clear()
    {
        ids_.clear();
        starts_.clear();
        buffer_ = nullptr;
        size_ = 0;
        firstMapIndex_ = 0;
    }
    bool Ready() const { return buffer_ != nullptr; }
    uint32_t Count() const { return static_cast<uint32_t>(starts_.size()); }
    uint32_t GlobalCount() const { return static_cast<uint32_t>(ids_.size()); }
    uint32_t FirstMapIndex() const { return firstMapIndex_; }

    // String number `n` (its place in the buffer); empty when there is none.
    std::string_view At(uint32_t n) const
    {
        if (!buffer_ || n >= starts_.size())
            return {};
        const uint32_t start = starts_[n];
        const uint32_t end = n + 1 < starts_.size() ? starts_[n + 1] - 1 : size_ - 1;
        return {buffer_ + start, end - start};
    }
    // The number of string id `id`; false when it is neither a global nor one of this map's.
    bool Number(uint32_t id, uint32_t& n) const
    {
        if (!buffer_)
            return false;
        if ((id >> kIndexBits) == 0 && (id & kIndexMask) >= firstMapIndex_)
        {
            const uint64_t number = uint64_t{GlobalCount()} + (id & kIndexMask) - firstMapIndex_;
            if (number >= starts_.size())
                return false;
            n = static_cast<uint32_t>(number);
            return true;
        }
        const auto found = std::lower_bound(ids_.begin(), ids_.end(), id);
        if (found == ids_.end() || *found != id)
            return false;
        n = static_cast<uint32_t>(found - ids_.begin());
        return true;
    }
    // The text of string id `id`; empty when it has none (0, the null id, is empty too).
    std::string_view Text(uint32_t id) const
    {
        uint32_t n = 0;
        return Number(id, n) ? At(n) : std::string_view{};
    }

private:
    std::vector<uint32_t> ids_;
    std::vector<uint32_t> starts_;
    const char* buffer_ = nullptr;
    uint32_t size_ = 0;
    uint32_t firstMapIndex_ = 0;
};

// ---- the signatures (halo3.dll: each matches exactly once) ----
// ADDR-HYGIENE: resolved. Nothing here is a constant of ours: the static buffer, the count beside it, the global
// table and the header copy are decoded from the game's own code, and must agree with each other (the loader and the
// hash builder name one buffer; the cache loader's call lands on the string loader). The integers beside each
// pattern are positions INSIDE that matched pattern, not image offsets: ADDR-HYGIENE: structural.
//
// The string loader (+0x108B28, 0x14 into +0x108B14; rcx/rsi = the header copy): mov eax,[rcx+0x30];
// mov rsi,rcx; mov [COUNT],eax; lea rcx,[...]; ...; mov edx,[rsi+0x3C]; ...; call READ; ...;
// mov r8d,[rsi+0x38]; lea r9,[BUFFER]; mov edx,[rsi+0x34].
inline constexpr const char* kLoaderPattern =
    "8B 41 30 48 8B F1 89 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 8D 1C 85 FF 0F 00 00 48 8B 05 ?? ?? ?? ?? 81 E3 00 F0 "
    "FF FF 8B D3 8B EB FF 10 48 8B F8 85 DB 74 ?? 8B 56 3C 4C 8B CF 44 8B C3 33 C9 E8 ?? ?? ?? ?? 8A D8 84 C0 74 ?? "
    "44 8B 46 38 4C 8D 0D ?? ?? ?? ?? 8B 56 34";
inline constexpr size_t kLoaderBytes = 88;
inline constexpr size_t kLoaderCount = 8, kLoaderBuffer = 81;
// How far before the match the loader's entry may be (retail: 0x14).
inline constexpr int64_t kLoaderEntryReach = 0x40;
// The hash builder (+0x108C5C): lea rdi,[TABLE]; movsxd rax,[rsi]; mov rdx,rax; mov [rsp+0x40],eax;
// lea rax,[BUFFER]; add rdx,rax; test ebx,ebx; js; cmp ebx,GLOBALS; jae.
inline constexpr const char* kBuilderPattern =
    "48 8D 3D ?? ?? ?? ?? 48 63 06 48 8B D0 89 44 24 40 48 8D 05 ?? ?? ?? ?? 48 03 D0 85 DB 78 ?? 81 FB ?? ?? ?? ?? 73";
inline constexpr size_t kBuilderBytes = 38;
inline constexpr size_t kBuilderTable = 3, kBuilderBuffer = 20, kBuilderGlobals = 33;
// The cache loader's call (+0xA6ACA): lea rcx,[HEADER]; mov [...],r9; call LOADER; mov bl,al; test al,al; je;
// mov r8d,[...].
inline constexpr const char* kCallPattern =
    "48 8D 0D ?? ?? ?? ?? 4C 89 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 8A D8 84 C0 0F 84 ?? ?? ?? ?? 44 8B 05";
inline constexpr size_t kCallBytes = 32;
inline constexpr size_t kCallHeader = 3, kCallLoader = 14;

constexpr size_t PatternBytes(const char* pattern)
{
    size_t count = 0;
    bool inByte = false;
    for (; *pattern; ++pattern)
    {
        const bool space = *pattern == ' ';
        if (!space && !inByte)
            ++count;
        inByte = !space;
    }
    return count;
}
static_assert(PatternBytes(kLoaderPattern) == kLoaderBytes && kLoaderBuffer + 4 <= kLoaderBytes);
static_assert(PatternBytes(kBuilderPattern) == kBuilderBytes && kBuilderGlobals + 4 <= kBuilderBytes);
static_assert(PatternBytes(kCallPattern) == kCallBytes && kCallLoader + 5 <= kCallBytes);

inline int64_t RipRva(const uint8_t* site, size_t disp, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(disp) + 4 + ReadI32(site + disp);
}
inline int64_t BranchRva(const uint8_t* site, size_t opcode, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(opcode) + 5 + ReadI32(site + opcode + 1);
}

struct Sites
{
    int64_t headerRva = 0; // the loaded map's cache header copy
    int64_t countRva = 0;  // the loader's copy of the header's string count
    int64_t bufferRva = 0; // the static string buffer
    int64_t tableRva = 0;  // the global table: globals x { uint64 id; const char* name }
    uint32_t globals = 0;
};
enum class Bind : uint8_t
{
    Bound = 0,
    LoaderMissing,   // the string loader: no unique match
    BuilderMissing,  // the hash builder: likewise
    CallMissing,     // the cache loader's call: likewise
    CallDisagrees,   // that call does not land just before the string loader's match
    BufferDisagrees, // the loader and the builder do not name one buffer
    Implausible,     // an address outside the module, misaligned, or a global count of 0 or past kMaxGlobals
    Total
};
inline const char* BindName(Bind why)
{
    switch (why)
    {
    case Bind::Bound: return "bound";
    case Bind::LoaderMissing: return "the string loader is missing or ambiguous";
    case Bind::BuilderMissing: return "the string hash builder is missing or ambiguous";
    case Bind::CallMissing: return "the cache loader's call of the string loader is missing or ambiguous";
    case Bind::CallDisagrees: return "the cache loader's call does not land on the string loader";
    case Bind::BufferDisagrees: return "the string loader and the hash builder do not name one buffer";
    case Bind::Implausible: return "a decoded address is not inside the module, or the global count is implausible";
    default: return "?";
    }
}
inline Bind DecodeSites(const uint8_t* loader, int64_t loaderRva, const uint8_t* builder, int64_t builderRva,
                        const uint8_t* call, int64_t callRva, int64_t imageSize, Sites& out)
{
    if (!loader)
        return Bind::LoaderMissing;
    if (!builder)
        return Bind::BuilderMissing;
    if (!call)
        return Bind::CallMissing;
    const int64_t entry = BranchRva(call, kCallLoader, callRva);
    if (entry > loaderRva || loaderRva - entry > kLoaderEntryReach)
        return Bind::CallDisagrees;
    const int64_t buffer = RipRva(loader, kLoaderBuffer, loaderRva);
    if (RipRva(builder, kBuilderBuffer, builderRva) != buffer)
        return Bind::BufferDisagrees;
    Sites sites;
    sites.headerRva = RipRva(call, kCallHeader, callRva);
    sites.countRva = RipRva(loader, kLoaderCount, loaderRva);
    sites.bufferRva = buffer;
    sites.tableRva = RipRva(builder, kBuilderTable, builderRva);
    sites.globals = ReadU32(builder + kBuilderGlobals);
    const auto inside = [imageSize](int64_t rva, int64_t bytes) { return rva > 0 && rva + bytes <= imageSize; };
    if (!sites.globals || sites.globals > kMaxGlobals || !inside(sites.headerRva, 0x4000) || (sites.headerRva & 3) ||
        !inside(sites.countRva, 4) || (sites.countRva & 3) || !inside(sites.bufferRva, 1) ||
        !inside(sites.tableRva, int64_t{sites.globals} * 16) || (sites.tableRva & 7))
        return Bind::Implausible;
    out = sites;
    return Bind::Bound;
}
} // namespace halo3_string_ids
