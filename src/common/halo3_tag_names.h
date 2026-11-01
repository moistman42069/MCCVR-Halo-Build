// HALO 3 TAG NAMES: what a tag is called ("objects\vehicles\sparrowhawk\sparrowhawk"), from the cache file's own
// tag-name table. The pure half of src/dll/halo3_tag_names.inl. No game, engine or OpenXR includes.
//
// EVIDENCE CLASS: RETAIL-DERIVED AND CHECKED OFFLINE. The header layout and both signatures are read from retail
// halo3.dll by disassembly (its cache-file loader and its own header check); the file layout is checked against
// every Halo 3 map of the stock install and of the Steam workshop, by an offline check that re-derives both from the
// patterns and the constants here.
//
// THE CACHE FILE HEADER (an MCC Halo 3 .map opens with it, 0x4000 bytes; halo3.dll keeps a copy of the loaded
// map's). halo3.dll's own check of it (+0xA619C) asks: version 13 at +4, 'head' at +0, 'foot' at +0x3FFC, the byte
// at +0xC equal to 2 ("is not built for this specific halo engine": ODST's maps carry 5, Reach's 6, Halo 4's 3,
// Halo 2 Anniversary's 4, so this is what tells a Halo 3 map from another title's), and a scenario name at +0xC0
// shorter than 32 characters. The scenario's tag path follows at +0xE0 ("levels\multi\shrine\shrine").
//
// THE NAME TABLE. Header +0x20 the number of tags, +0x24 the names buffer's address, +0x28 its size, +0x2C the
// address of the offset table: one int32 per tag, the offset of its NUL-terminated path in the buffer (negative:
// no name). A tag's name is entry (datum index & 0xFFFF). Both live in the file's DEBUG section.
//
// TWO LAYOUTS OF THE SAME FIELDS. The header ends its fixed part with a section table: +0x4CC four int32 masks and
// +0x4DC four {uint32 address, uint32 size}, for the debug, resource, tag and localization sections. A header
// address is in the section whose [address, address + size) holds it, and is stored at address + that section's
// mask in the file. The stock maps' masks are each section's file position minus its address (Sandtrap: names
// buffer 0x0593F000 at file 0x6D000, mask -0x058D2000); maps the mod tools build (HugeGrass) store every mask as 0
// and every address as a file offset, which the same rule reads unchanged. Checked against the 38 stock Halo 3 maps
// that carry a scenario and 18 workshop Halo 3 maps (campaign.map and shared.map, the two shared resource maps,
// carry no scenario and no tag index of their own): in every one the header's scenario path is the name of the
// map's scenario tag, and the table has as many entries as the tag index.
//
// IN THE GAME'S MEMORY. halo3.dll reads both into one block it allocates at startup (0x860000 bytes; the pointer
// is a global): the offset table at +0, the names buffer at +0x20000 (0x800000 bytes of room), and an array of
// name pointers at +0x820000 (0x8000 entries) built from them. Its guarded lookup, inlined at three sites, is
// exactly:  index < header+0x20  and  (uint32)offset <= 0x7FFFFF  ->  block + 0x20000 + offset.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace halo3_tag_names
{
// ---- the header ----
inline constexpr size_t kHeaderSize = 0x4000;
inline constexpr uint32_t kHeadMagic = 0x68656164; // 'head', little-endian at +0 (the file's bytes read "daeh")
inline constexpr uint32_t kFootMagic = 0x666F6F74; // 'foot' at +0x3FFC
inline constexpr size_t kFootOffset = 0x3FFC;
inline constexpr int32_t kVersion = 13;
inline constexpr size_t kEngineOffset = 0x0C;
inline constexpr uint8_t kEngineHalo3 = 2;
inline constexpr size_t kNameCountOffset = 0x20;
inline constexpr size_t kNameBufferOffset = 0x24;
inline constexpr size_t kNameBufferSizeOffset = 0x28;
inline constexpr size_t kNameTableOffset = 0x2C;
inline constexpr size_t kScenarioNameOffset = 0xC0;
inline constexpr size_t kScenarioNameRoom = 0x20;
inline constexpr size_t kScenarioPathOffset = 0xE0;
inline constexpr size_t kScenarioPathRoom = 0x100;
inline constexpr size_t kSectionMasksOffset = 0x4CC;
inline constexpr size_t kSectionsOffset = 0x4DC;
inline constexpr int kSectionCount = 4;
inline constexpr int kDebugSection = 0;

// ---- the game's block ----
inline constexpr size_t kLoadedBufferAt = 0x20000;
inline constexpr uint32_t kLoadedBufferRoom = 0x800000;
inline constexpr uint32_t kLoadedMaxNames = 0x8000;
// How much of a names buffer the mod copies at most. The largest seen: 0x15A942 bytes (a workshop Firefight map
// with 25190 tags); HugeGrass 0x10F1A0; the stock maps under 0xE6000.
inline constexpr uint32_t kMaxBufferCopy = 0x400000; // 4 MB
// The longest name read, in characters. The longest seen: 136.
inline constexpr size_t kMaxPathLength = 255;

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

struct Section
{
    uint32_t address = 0;
    uint32_t size = 0;
};
struct Header
{
    uint32_t nameCount = 0;
    uint32_t nameBuffer = 0;     // an address of the header's sections (TWO LAYOUTS above)
    uint32_t nameBufferSize = 0;
    uint32_t nameTable = 0;      // likewise
    int32_t masks[kSectionCount]{};
    Section sections[kSectionCount]{};
    char scenarioName[kScenarioNameRoom + 1]{};
    char scenarioPath[kScenarioPathRoom + 1]{};
};
enum class HeaderFault : uint8_t
{
    None = 0,
    Short,         // fewer than kHeaderSize bytes
    Magic,         // not 'head'
    Version,       // not 13
    Footer,        // not 'foot'
    Engine,        // another title's map
    ScenarioName,  // unterminated, empty or not printable
    ScenarioPath,  // likewise
    Count
};
inline const char* HeaderFaultName(HeaderFault fault)
{
    switch (fault)
    {
    case HeaderFault::None: return "a Halo 3 cache header";
    case HeaderFault::Short: return "too short for a cache header";
    case HeaderFault::Magic: return "no 'head' at its start";
    case HeaderFault::Version: return "not version 13";
    case HeaderFault::Footer: return "no 'foot' at its end";
    case HeaderFault::Engine: return "another title's map";
    case HeaderFault::ScenarioName: return "no readable scenario name";
    case HeaderFault::ScenarioPath: return "no readable scenario path";
    default: return "?";
    }
}

inline bool Printable(char c)
{
    return c >= 0x20 && c <= 0x7E;
}
// A NUL-terminated, non-empty, printable string inside `room` bytes, copied into `out` (room + 1 bytes).
inline bool ReadText(const uint8_t* at, size_t room, char* out)
{
    const void* end = std::memchr(at, 0, room);
    if (!end)
        return false;
    const size_t length = static_cast<size_t>(static_cast<const uint8_t*>(end) - at);
    if (!length)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!Printable(static_cast<char>(at[i])))
            return false;
    std::memcpy(out, at, length);
    out[length] = '\0';
    return true;
}

// The header as halo3.dll checks it, and the fields read here. Its name fields are taken as they are: what bounds
// them depends on where the table is read from (a file: FileOffset; the game's block: the room it has).
inline HeaderFault ParseHeader(const uint8_t* bytes, size_t size, Header& out)
{
    out = Header{};
    if (!bytes || size < kHeaderSize)
        return HeaderFault::Short;
    if (ReadU32(bytes) != kHeadMagic)
        return HeaderFault::Magic;
    if (ReadI32(bytes + 4) != kVersion)
        return HeaderFault::Version;
    if (ReadU32(bytes + kFootOffset) != kFootMagic)
        return HeaderFault::Footer;
    if (bytes[kEngineOffset] != kEngineHalo3)
        return HeaderFault::Engine;
    if (!ReadText(bytes + kScenarioNameOffset, kScenarioNameRoom, out.scenarioName))
        return HeaderFault::ScenarioName;
    if (!ReadText(bytes + kScenarioPathOffset, kScenarioPathRoom, out.scenarioPath))
        return HeaderFault::ScenarioPath;
    out.nameCount = ReadU32(bytes + kNameCountOffset);
    out.nameBuffer = ReadU32(bytes + kNameBufferOffset);
    out.nameBufferSize = ReadU32(bytes + kNameBufferSizeOffset);
    out.nameTable = ReadU32(bytes + kNameTableOffset);
    for (int i = 0; i < kSectionCount; ++i)
    {
        out.masks[i] = ReadI32(bytes + kSectionMasksOffset + 4 * i);
        out.sections[i].address = ReadU32(bytes + kSectionsOffset + 8 * i);
        out.sections[i].size = ReadU32(bytes + kSectionsOffset + 8 * i + 4);
    }
    return HeaderFault::None;
}

// Where [address, address + size) is stored in a file of `fileSize` bytes: in the section (with a size) that holds
// all of it, at address + that section's mask. False: in no one section, or not inside the file.
inline bool FileOffset(const Header& header, uint32_t address, uint32_t size, uint64_t fileSize, uint64_t& offset,
                       int* sectionOut = nullptr)
{
    for (int i = 0; i < kSectionCount; ++i)
    {
        const Section& section = header.sections[i];
        const uint64_t start = section.address, end = start + section.size;
        if (!section.size || address < start || uint64_t{address} + size > end)
            continue;
        const int64_t at = static_cast<int64_t>(address) + header.masks[i];
        if (at < 0 || static_cast<uint64_t>(at) + size > fileSize)
            return false;
        offset = static_cast<uint64_t>(at);
        if (sectionOut)
            *sectionOut = i;
        return true;
    }
    return false;
}

// ---- the table ----
// `offsets`: `count` little-endian int32 (any alignment); `buffer`: `bufferSize` bytes of names.
struct NameTable
{
    const uint8_t* offsets = nullptr;
    uint32_t count = 0;
    const char* buffer = nullptr;
    uint32_t bufferSize = 0;
};

// Tag `index`'s path, or empty: no such entry, no offset, an offset past the buffer, no terminator within
// kMaxPathLength characters or before the buffer ends, an empty name, or a character a tag path does not have.
inline std::string_view NameAt(const NameTable& table, uint32_t index)
{
    if (!table.offsets || !table.buffer || index >= table.count)
        return {};
    const int32_t offset = ReadI32(table.offsets + static_cast<size_t>(index) * 4);
    if (offset < 0 || static_cast<uint32_t>(offset) >= table.bufferSize)
        return {};
    const char* start = table.buffer + offset;
    const size_t room = table.bufferSize - static_cast<uint32_t>(offset);
    const void* end = std::memchr(start, 0, room < kMaxPathLength + 1 ? room : kMaxPathLength + 1);
    if (!end)
        return {};
    const size_t length = static_cast<size_t>(static_cast<const char*>(end) - start);
    for (size_t i = 0; i < length; ++i)
        if (!Printable(start[i]))
            return {};
    return {start, length};
}

inline bool EqualNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

// The first tag named `wanted` (case aside). The proof that a table is the header's map's: its scenario path is the
// name of one of its tags.
inline bool FindName(const NameTable& table, std::string_view wanted, uint32_t& index)
{
    if (wanted.empty())
        return false;
    for (uint32_t i = 0; i < table.count; ++i)
        if (EqualNoCase(NameAt(table, i), wanted))
        {
            index = i;
            return true;
        }
    return false;
}

// The last component of a path ("sparrowhawk" of "objects\vehicles\sparrowhawk\sparrowhawk").
inline std::string_view ShortName(std::string_view path)
{
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// A NAME IN THE CAMERA FILE ("hideParts", "fadeParts") AGAINST A MATERIAL'S PATH: Exact when it is the path or the
// path's last part (case aside); Variant when the last part is the name and an underscore suffix -- a variant of a
// vehicle names its materials with one ("windshield_snow" on the snow Warthog). A caller takes the Variant ones only
// when no material of the vehicle matches the name exactly, so a file written for a vehicle fits its variants and a
// name that already matches is never widened (the Sparrowhawk's "sparrowhawk_glass" stays off "sparrowhawk_glass_dark").
enum class PartNameMatch : uint8_t
{
    None,
    Exact,
    Variant,
};
inline PartNameMatch MatchPartName(std::string_view path, std::string_view name)
{
    if (name.empty() || path.empty())
        return PartNameMatch::None;
    const std::string_view last = ShortName(path);
    if (EqualNoCase(path, name) || EqualNoCase(last, name))
        return PartNameMatch::Exact;
    if (last.size() > name.size() + 1 && last[name.size()] == '_' && EqualNoCase(last.substr(0, name.size()), name))
        return PartNameMatch::Variant;
    return PartNameMatch::None;
}

// The name a vehicle the mod does not know goes by: its tag's short name in lower case, anything but a-z, 0-9, '_',
// '-' and '.' made '_' (the vehicle camera file's "match" looks for text in it, the seat memory file keys on it
// between tabs, the headset note shows it), cut to fit `capacity` with its terminator. False and "" when the path
// has no last component.
inline bool DisplayName(std::string_view path, char* out, size_t capacity)
{
    if (!out || !capacity)
        return false;
    out[0] = '\0';
    const std::string_view name = ShortName(path);
    if (name.empty())
        return false;
    size_t length = 0;
    for (const char c : name)
    {
        if (length + 1 >= capacity)
            break;
        char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        const bool keep = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '_' ||
                          lower == '-' || lower == '.';
        out[length++] = keep ? lower : '_';
    }
    out[length] = '\0';
    return length > 0;
}

// ---- the signatures (halo3.dll: each matches exactly once; the verifier checks) ----
// ADDR-HYGIENE: resolved. Neither the header copy nor the block's pointer is a constant of ours: both are decoded
// from the loader's own reads, which must name the header's four name fields as one record and the block's pointer
// three times over, and the game's guarded lookup must name the same pointer and the same count. The integers
// beside each pattern are positions INSIDE that matched pattern, not image offsets: ADDR-HYGIENE: structural (the
// unit test checks each lands on the disp32 its comment names).
//
// The cache loader reading the table (+0xA6AE7, inside +0xA6538, the function that also checks the tag index's
// 'tags'): mov r8d,[SIZE]; xor ecx,ecx; mov r9,[BLOCK]; add r8d,0xFFF; mov edx,[BUFFER]; and r8d,~0xFFF;
// add r9,0x20000; call READ (section ecx = 0, the debug section); mov bl,al; test al,al; je; mov r8d,[COUNT];
// xor ecx,ecx; mov r9,[BLOCK]; mov edx,[TABLE]; shl r8d,2; add r8d,0xFFF; and r8d,~0xFFF; call READ; mov bl,al;
// test al,al; je; mov rbp,[BLOCK]; xor edx,edx; mov r8d,0x40000; lea r14,[rbp+0x820000] (the pointer array it
// clears, then fills).
inline constexpr const char* kLoaderPattern =
    "44 8B 05 ?? ?? ?? ?? 33 C9 4C 8B 0D ?? ?? ?? ?? 41 81 C0 FF 0F 00 00 8B 15 ?? ?? ?? ?? 41 81 E0 00 F0 FF FF "
    "49 81 C1 00 00 02 00 E8 ?? ?? ?? ?? 8A D8 84 C0 0F 84 ?? ?? ?? ?? 44 8B 05 ?? ?? ?? ?? 33 C9 4C 8B 0D ?? ?? "
    "?? ?? 8B 15 ?? ?? ?? ?? 41 C1 E0 02 41 81 C0 FF 0F 00 00 41 81 E0 00 F0 FF FF E8 ?? ?? ?? ?? 8A D8 84 C0 0F "
    "84 ?? ?? ?? ?? 48 8B 2D ?? ?? ?? ?? 33 D2 41 B8 00 00 04 00 4C 8D B5 00 00 82 00";
inline constexpr size_t kLoaderBytes = 135;
inline constexpr size_t kLoaderSize = 3, kLoaderBlockA = 12, kLoaderBuffer = 25, kLoaderReadA = 43,
                        kLoaderCount = 61, kLoaderBlockB = 70, kLoaderTable = 76, kLoaderReadB = 98,
                        kLoaderBlockC = 116;
// The game's guarded lookup (+0x273565, one of three inlined copies, the one a unique pattern names):
// mov rdi,[BLOCK]; movzx eax,bx; cmp eax,[COUNT]; jae; cmp dword [rdi+rax*4],0x7FFFFF; ja; movsxd rax,[rdi+rax*4];
// lea rcx,[rdi+0x20000].
inline constexpr const char* kLookupPattern =
    "48 8B 3D ?? ?? ?? ?? 0F B7 C3 3B 05 ?? ?? ?? ?? 73 ?? 81 3C 87 FF FF 7F 00 77 ?? 48 63 04 87 48 8D 8F 00 00 "
    "02 00";
inline constexpr size_t kLookupBytes = 38;
inline constexpr size_t kLookupBlock = 3, kLookupCount = 12;

// How many bytes a pattern's text names ("48 ?? 8B" is three).
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
static_assert(PatternBytes(kLoaderPattern) == kLoaderBytes && kLoaderBlockC + 4 <= kLoaderBytes);
static_assert(PatternBytes(kLookupPattern) == kLookupBytes && kLookupCount + 4 <= kLookupBytes);

// What the rip-relative disp32 at `site + disp` names, as an offset from the module base; the disp32 is its
// instruction's last four bytes.
inline int64_t RipRva(const uint8_t* site, size_t disp, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(disp) + 4 + ReadI32(site + disp);
}
// Where the E8 at `site + opcode` lands.
inline int64_t BranchRva(const uint8_t* site, size_t opcode, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(opcode) + 5 + ReadI32(site + opcode + 1);
}

struct Sites
{
    int64_t headerRva = 0;    // the loaded map's cache header (kHeaderSize bytes)
    int64_t blockSlotRva = 0; // the pointer to the game's block
};
enum class Bind : uint8_t
{
    Bound = 0,
    LoaderMissing,      // the loader's read of the table: no match, or more than one
    LookupMissing,      // the guarded lookup: likewise
    BlockDisagrees,     // the loader's three reads of the block's pointer do not name one global
    ReadsDisagree,      // the loader's two section reads do not call one function
    NotOneHeader,       // the four name fields are not +0x20, +0x24, +0x28, +0x2C of one record
    LookupDisagrees,    // the lookup does not name the loader's block pointer and count
    Implausible,        // the header or the pointer is not inside the module, or misaligned
    Count
};
inline const char* BindName(Bind why)
{
    switch (why)
    {
    case Bind::Bound: return "bound";
    case Bind::LoaderMissing: return "the cache loader's read of the name table is missing or ambiguous";
    case Bind::LookupMissing: return "the game's guarded name lookup is missing or ambiguous";
    case Bind::BlockDisagrees: return "the loader's three reads of the name block's pointer do not agree";
    case Bind::ReadsDisagree: return "the loader's two section reads do not call one function";
    case Bind::NotOneHeader: return "the loader's four name fields are not one header's +0x20..+0x2C";
    case Bind::LookupDisagrees: return "the guarded lookup does not name the loader's block and count";
    case Bind::Implausible: return "the decoded header or block pointer is not inside the module";
    default: return "?";
    }
}
// Each site as its matched pattern's bytes and offset from the module base; null bytes = no unique match.
inline Bind DecodeSites(const uint8_t* loader, int64_t loaderRva, const uint8_t* lookup, int64_t lookupRva,
                        int64_t imageSize, Sites& out)
{
    if (!loader)
        return Bind::LoaderMissing;
    if (!lookup)
        return Bind::LookupMissing;
    const int64_t block = RipRva(loader, kLoaderBlockA, loaderRva);
    if (RipRva(loader, kLoaderBlockB, loaderRva) != block || RipRva(loader, kLoaderBlockC, loaderRva) != block)
        return Bind::BlockDisagrees;
    if (BranchRva(loader, kLoaderReadA, loaderRva) != BranchRva(loader, kLoaderReadB, loaderRva))
        return Bind::ReadsDisagree;
    const int64_t header = RipRva(loader, kLoaderCount, loaderRva) - static_cast<int64_t>(kNameCountOffset);
    if (RipRva(loader, kLoaderBuffer, loaderRva) != header + static_cast<int64_t>(kNameBufferOffset) ||
        RipRva(loader, kLoaderSize, loaderRva) != header + static_cast<int64_t>(kNameBufferSizeOffset) ||
        RipRva(loader, kLoaderTable, loaderRva) != header + static_cast<int64_t>(kNameTableOffset))
        return Bind::NotOneHeader;
    if (RipRva(lookup, kLookupBlock, lookupRva) != block ||
        RipRva(lookup, kLookupCount, lookupRva) != header + static_cast<int64_t>(kNameCountOffset))
        return Bind::LookupDisagrees;
    if (header <= 0 || header > imageSize - static_cast<int64_t>(kHeaderSize) || (header & 3) || block <= 0 ||
        block > imageSize - 8 || (block & 7))
        return Bind::Implausible;
    out.headerRva = header;
    out.blockSlotRva = block;
    return Bind::Bound;
}
} // namespace halo3_tag_names
