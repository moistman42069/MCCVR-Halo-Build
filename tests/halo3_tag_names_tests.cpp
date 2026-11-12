// Halo 3 tag names (src/common/halo3_tag_names.h): the cache header as halo3.dll checks it, both layouts of the
// name table's addresses (a stock map's section-relative ones and a mod-built map's file offsets) on small
// synthetic files, every refusal of a name, the short name a vehicle the mod does not know goes by, and the decode
// of the two signatures on images built from their own patterns; and the stock vehicles by tag path
// (src/common/halo3_vehicle_logic.h), with what the rule makes of vehicles read offline from Sandtrap and HugeGrass.
#include "halo3_tag_names.h"
#include "halo3_vehicle_logic.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

using namespace halo3_tag_names;

void PutU32(std::vector<uint8_t>& bytes, size_t at, uint32_t value)
{
    if (bytes.size() < at + 4)
        bytes.resize(at + 4);
    std::memcpy(bytes.data() + at, &value, 4);
}
void PutText(std::vector<uint8_t>& bytes, size_t at, std::string_view text)
{
    if (bytes.size() < at + text.size() + 1)
        bytes.resize(at + text.size() + 1);
    std::memcpy(bytes.data() + at, text.data(), text.size());
    bytes[at + text.size()] = 0;
}

std::vector<uint8_t> MakeHeader(const char* scenarioName = "shrine",
                                const char* scenarioPath = "levels\\multi\\shrine\\shrine")
{
    std::vector<uint8_t> header(kHeaderSize, 0);
    PutU32(header, 0, kHeadMagic);
    PutU32(header, 4, static_cast<uint32_t>(kVersion));
    header[kEngineOffset] = kEngineHalo3;
    header[kEngineOffset + 1] = 2;
    PutU32(header, kFootOffset, kFootMagic);
    PutText(header, kScenarioNameOffset, scenarioName);
    PutText(header, kScenarioPathOffset, scenarioPath);
    return header;
}

void TestHeader()
{
    std::vector<uint8_t> bytes = MakeHeader();
    PutU32(bytes, kNameCountOffset, 11921);
    PutU32(bytes, kNameBufferOffset, 0x0593F000);
    PutU32(bytes, kNameBufferSizeOffset, 0x9ED1D);
    PutU32(bytes, kNameTableOffset, 0x059DE000);
    PutU32(bytes, kSectionMasksOffset, static_cast<uint32_t>(-0x058D2000));
    PutU32(bytes, kSectionsOffset, 0x058D6000);
    PutU32(bytes, kSectionsOffset + 4, 0x114000);
    Header header;
    Check(ParseHeader(bytes.data(), bytes.size(), header) == HeaderFault::None, "a Halo 3 header parses");
    Check(header.nameCount == 11921 && header.nameBuffer == 0x0593F000 && header.nameBufferSize == 0x9ED1D &&
              header.nameTable == 0x059DE000,
          "the name fields are +0x20 count, +0x24 buffer, +0x28 size, +0x2C table");
    Check(header.masks[0] == -0x058D2000 && header.sections[0].address == 0x058D6000 &&
              header.sections[0].size == 0x114000,
          "the section table: +0x4CC masks, +0x4DC {address, size}");
    Check(std::string_view(header.scenarioName) == "shrine" &&
              std::string_view(header.scenarioPath) == "levels\\multi\\shrine\\shrine",
          "the scenario name at +0xC0 and its path at +0xE0");

    Check(ParseHeader(bytes.data(), kHeaderSize - 1, header) == HeaderFault::Short, "a short header is refused");
    Check(ParseHeader(nullptr, kHeaderSize, header) == HeaderFault::Short, "no header is refused");
    auto broken = bytes;
    broken[0] = 'h';
    Check(ParseHeader(broken.data(), broken.size(), header) == HeaderFault::Magic, "no 'head': refused");
    broken = bytes;
    PutU32(broken, 4, 12);
    Check(ParseHeader(broken.data(), broken.size(), header) == HeaderFault::Version, "version 12: refused");
    broken = bytes;
    PutU32(broken, kFootOffset, 0);
    Check(ParseHeader(broken.data(), broken.size(), header) == HeaderFault::Footer, "no 'foot': refused");
    for (const uint8_t other : {uint8_t{0}, uint8_t{3}, uint8_t{4}, uint8_t{5}, uint8_t{6}})
    {
        broken = bytes;
        broken[kEngineOffset] = other;
        Check(ParseHeader(broken.data(), broken.size(), header) == HeaderFault::Engine,
              "another title's map (CE 0, Halo 4 3, H2A 4, ODST 5, Reach 6): refused");
    }
    broken = bytes;
    std::memset(broken.data() + kScenarioNameOffset, 'a', kScenarioNameRoom);
    Check(ParseHeader(broken.data(), broken.size(), header) == HeaderFault::ScenarioName,
          "a scenario name with no terminator in its 32 bytes is refused, as halo3.dll refuses it");
    broken = bytes;
    broken[kScenarioNameOffset] = 0;
    Check(ParseHeader(broken.data(), broken.size(), header) == HeaderFault::ScenarioName, "no scenario name");
    broken = bytes;
    broken[kScenarioNameOffset + 1] = 0x07;
    Check(ParseHeader(broken.data(), broken.size(), header) == HeaderFault::ScenarioName,
          "a scenario name that is not printable");
    broken = bytes;
    std::memset(broken.data() + kScenarioPathOffset, 'a', kScenarioPathRoom);
    Check(ParseHeader(broken.data(), broken.size(), header) == HeaderFault::ScenarioPath,
          "a scenario path with no terminator in its 256 bytes is refused");
    auto longest = MakeHeader("abcdefghijklmnopqrstuvwxyz01234", std::string(255, 'p').c_str());
    Check(ParseHeader(longest.data(), longest.size(), header) == HeaderFault::None &&
              std::strlen(header.scenarioName) == 31 && std::strlen(header.scenarioPath) == 255,
          "the longest scenario name (31) and path (255) are read whole");
    for (int fault = 0; fault < static_cast<int>(HeaderFault::Count); ++fault)
        Check(std::strcmp(HeaderFaultName(static_cast<HeaderFault>(fault)), "?") != 0, "every header fault is named");
}

void TestFileOffset()
{
    Header header;
    // Sandtrap's own section table: debug, resource, tag, localization.
    header.masks[0] = -0x058D2000;
    header.masks[1] = 0x118000;
    header.masks[2] = -0x93C000;
    header.masks[3] = 0x03701000;
    header.sections[0] = {0x058D6000, 0x114000};
    header.sections[1] = {0, 0x04F96000};
    header.sections[2] = {0x059EA000, 0x03DD0000};
    header.sections[3] = {0x0577D000, 0x159000};
    const uint64_t fileSize = 0x08FD7000;
    uint64_t offset = 0;
    int section = -1;
    Check(FileOffset(header, 0x0593F000, 0x9ED1D, fileSize, offset, &section) && offset == 0x6D000 && section == 0,
          "Sandtrap: the names buffer 0x0593F000 is at file 0x6D000 in the debug section");
    Check(FileOffset(header, 0x059DE000, 11921 * 4, fileSize, offset, &section) && offset == 0x10C000 &&
              section == 0,
          "Sandtrap: the offset table 0x059DE000 is at file 0x10C000");
    Check(FileOffset(header, 0x059EA000, 0x10, fileSize, offset, &section) && offset == 0x050AE000 && section == 2,
          "an address of the tag section takes the tag section's mask");
    Check(!FileOffset(header, 0x059E9FF0, 0x20, fileSize, offset),
          "a range that runs past its section's end is in no one section");
    Check(!FileOffset(header, 0x0A000000, 4, fileSize, offset), "an address of no section is refused");
    Check(!FileOffset(header, 0x0593F000, 0x9ED1D, 0x6D000, offset), "a range past the file's end is refused");
    Header negative = header;
    negative.masks[0] = -0x7FFFFFFF;
    Check(!FileOffset(negative, 0x0593F000, 4, fileSize, offset), "a mask that lands before the file is refused");

    // A mod-built map (HugeGrass): every mask 0, every address a file offset.
    Header mod;
    mod.sections[0] = {0x7A885000, 0x1D1000};
    mod.sections[1] = {0x4000, 0x7A711000};
    mod.sections[2] = {0x7AA56000, 0x05CD0000};
    mod.sections[3] = {0x7A715000, 0x170000};
    Check(FileOffset(mod, 0x7A932000, 0x10F1A0, 0x80726000, offset, &section) && offset == 0x7A932000 &&
              section == 0,
          "HugeGrass: the names buffer is at its own address, in the debug section");
    Check(FileOffset(mod, 0x7AA42000, 19940 * 4, 0x80726000, offset, &section) && offset == 0x7AA42000,
          "HugeGrass: the offset table is at its own address");
    Header empty;
    Check(!FileOffset(empty, 0, 0, 0x1000, offset), "a header with no sections places nothing");
    Header wrap = mod;
    wrap.sections[0] = {0xFFFFF000, 0x1000};
    Check(!FileOffset(wrap, 0xFFFFFF00, 0x200, ~0ull, offset), "a range that wraps past 4 GB is in no section");
}

// One small cache file in either layout: header, then a debug section holding the names buffer at +0x100 and the
// offset table at +0x1000. `base` is the debug section's address; the file holds it at 0x4000.
struct SyntheticFile
{
    std::vector<uint8_t> bytes;
    std::vector<std::string> names;
};
SyntheticFile MakeFile(uint32_t base, bool stockLayout)
{
    SyntheticFile file;
    file.names = {"", "globals\\globals", "objects\\vehicles\\sparrowhawk\\sparrowhawk",
                  "levels\\multi\\hugeass\\hugeass", "objects\\Vehicles\\Pelican_Multiplayer\\Pelican_Multiplayer"};
    file.bytes = MakeHeader("hugeass", "levels\\multi\\hugeass\\hugeass");
    const uint32_t filePos = 0x4000;
    const uint32_t sectionSize = 0x2000;
    file.bytes.resize(filePos + sectionSize, 0);
    std::vector<int32_t> offsets;
    uint32_t cursor = 0;
    std::vector<uint8_t> buffer;
    for (size_t i = 0; i < file.names.size(); ++i)
    {
        if (i == 0)
        {
            offsets.push_back(-1);
            continue;
        }
        offsets.push_back(static_cast<int32_t>(cursor));
        buffer.insert(buffer.end(), file.names[i].begin(), file.names[i].end());
        buffer.push_back(0);
        cursor = static_cast<uint32_t>(buffer.size());
    }
    std::memcpy(file.bytes.data() + filePos + 0x100, buffer.data(), buffer.size());
    std::memcpy(file.bytes.data() + filePos + 0x1000, offsets.data(), offsets.size() * 4);
    const uint32_t address = stockLayout ? base : filePos;
    PutU32(file.bytes, kNameCountOffset, static_cast<uint32_t>(offsets.size()));
    PutU32(file.bytes, kNameBufferOffset, address + 0x100);
    PutU32(file.bytes, kNameBufferSizeOffset, static_cast<uint32_t>(buffer.size()));
    PutU32(file.bytes, kNameTableOffset, address + 0x1000);
    PutU32(file.bytes, kSectionMasksOffset, stockLayout ? filePos - base : 0);
    PutU32(file.bytes, kSectionsOffset, address);
    PutU32(file.bytes, kSectionsOffset + 4, sectionSize);
    return file;
}

void TestFileTable(bool stockLayout)
{
    const SyntheticFile file = MakeFile(0x058D6000, stockLayout);
    Header header;
    Check(ParseHeader(file.bytes.data(), file.bytes.size(), header) == HeaderFault::None,
          stockLayout ? "stock layout: the header parses" : "mod layout: the header parses");
    uint64_t tableAt = 0, bufferAt = 0;
    int tableSection = -1, bufferSection = -1;
    const bool placed = FileOffset(header, header.nameTable, header.nameCount * 4, file.bytes.size(), tableAt,
                                   &tableSection) &&
                        FileOffset(header, header.nameBuffer, header.nameBufferSize, file.bytes.size(), bufferAt,
                                   &bufferSection);
    Check(placed && tableAt == 0x5000 && bufferAt == 0x4100 && tableSection == kDebugSection &&
              bufferSection == kDebugSection,
          stockLayout ? "stock layout: table and buffer translate to their file places"
                      : "mod layout: table and buffer are at their own addresses");
    if (!placed)
        return;
    NameTable table{file.bytes.data() + tableAt, header.nameCount,
                    reinterpret_cast<const char*>(file.bytes.data() + bufferAt), header.nameBufferSize};
    Check(NameAt(table, 0).empty(), "a tag with no offset has no name");
    Check(NameAt(table, 2) == "objects\\vehicles\\sparrowhawk\\sparrowhawk", "a tag's name by its index");
    Check(NameAt(table, 0x189E & 0xFFFF).empty(), "an index past the table has no name");
    uint32_t scenario = 0;
    Check(FindName(table, header.scenarioPath, scenario) && scenario == 3,
          "the header's scenario path names one of the table's tags: the table is that map's");
    Check(!FindName(table, "levels\\multi\\shrine\\shrine", scenario), "another map's scenario is not in it");
    Check(FindName(table, "OBJECTS\\VEHICLES\\SPARROWHAWK\\SPARROWHAWK", scenario) && scenario == 2,
          "names are found case aside");
    char name[64];
    Check(DisplayName(NameAt(table, 4), name, sizeof(name)) && std::string_view(name) == "pelican_multiplayer",
          "a vehicle's short name, lower case");
}

void TestNameRefusals()
{
    std::string buffer;
    std::vector<int32_t> offsets;
    const auto add = [&](const std::string& text, bool terminate = true) {
        offsets.push_back(static_cast<int32_t>(buffer.size()));
        buffer += text;
        if (terminate)
            buffer.push_back('\0');
    };
    add("objects\\vehicles\\warthog\\warthog");     // 0
    add(std::string(kMaxPathLength, 'a'));          // 1: the longest read
    add(std::string(kMaxPathLength + 1, 'b'));      // 2: one longer
    add(std::string("objects\\bad\x01name"));       // 3: not printable
    add("");                                        // 4: empty
    add("object\\vehicles\\tx130 turret\\turret_test"); // 5: a space, as some mod tags have
    offsets.push_back(-5);                          // 6: negative
    offsets.push_back(0x7FFFFFFF);                  // 7: past the buffer
    add("objects\\unterminated", false);            // 8: the buffer ends inside it
    NameTable table{reinterpret_cast<const uint8_t*>(offsets.data()), static_cast<uint32_t>(offsets.size()),
                    buffer.data(), static_cast<uint32_t>(buffer.size())};
    Check(NameAt(table, 0) == "objects\\vehicles\\warthog\\warthog", "a plain name");
    Check(NameAt(table, 1).size() == kMaxPathLength, "a name of the longest length read is read whole");
    Check(NameAt(table, 2).empty(), "a longer name is refused, not cut");
    Check(NameAt(table, 3).empty(), "a name with a character a path does not have is refused");
    Check(NameAt(table, 4).empty(), "an empty name is no name");
    Check(NameAt(table, 5) == "object\\vehicles\\tx130 turret\\turret_test", "a name with a space is read");
    Check(NameAt(table, 6).empty(), "a negative offset is no name");
    Check(NameAt(table, 7).empty(), "an offset past the buffer is no name");
    Check(NameAt(table, 8).empty(), "a name the buffer ends inside is refused");
    Check(NameAt(table, static_cast<uint32_t>(offsets.size())).empty(), "an index past the table");
    Check(NameAt(NameTable{}, 0).empty(), "an empty table has no names");
    // Offsets in a raw byte copy need not be aligned.
    std::vector<uint8_t> unaligned(1 + offsets.size() * 4);
    std::memcpy(unaligned.data() + 1, offsets.data(), offsets.size() * 4);
    NameTable shifted = table;
    shifted.offsets = unaligned.data() + 1;
    Check(NameAt(shifted, 0) == NameAt(table, 0), "offsets are read at any alignment");
}

void TestDisplayName()
{
    char out[64];
    Check(DisplayName("objects\\vehicles\\sparrowhawk\\sparrowhawk", out, sizeof(out)) &&
              std::string_view(out) == "sparrowhawk",
          "the last path component");
    Check(DisplayName("objects\\levels\\multi\\shrine\\behemoth\\behemoth", out, sizeof(out)) &&
              std::string_view(out) == "behemoth",
          "the Elephant's tag is called behemoth");
    Check(DisplayName("Objects\\Vehicles\\Falcon\\Falcon_GL", out, sizeof(out)) &&
              std::string_view(out) == "falcon_gl",
          "lower case");
    Check(DisplayName("object\\vehicles\\v-wing\\v-wing 2.0", out, sizeof(out)) &&
              std::string_view(out) == "v-wing_2.0",
          "'-' and '.' stay, a space is '_'");
    Check(DisplayName("objects\\vehicles\\a\tb", out, sizeof(out)) && std::string_view(out) == "a_b",
          "a tab (the seat memory file's separator) cannot get through");
    Check(DisplayName("plain", out, sizeof(out)) && std::string_view(out) == "plain", "a name with no path");
    Check(DisplayName("objects/vehicles/mammoth/mammoth", out, sizeof(out)) && std::string_view(out) == "mammoth",
          "a forward slash separates too");
    Check(!DisplayName("objects\\vehicles\\", out, sizeof(out)) && out[0] == '\0', "no last component: no name");
    Check(!DisplayName("", out, sizeof(out)) && out[0] == '\0', "an empty path: no name");
    char small[8];
    Check(DisplayName("objects\\vehicles\\sparrowhawk_weapons_dummy", small, sizeof(small)) &&
              std::string_view(small) == "sparrow",
          "cut to fit, terminated");
    Check(!DisplayName("x", nullptr, 8) && !DisplayName("x", out, 0), "no room: nothing written");
}

std::vector<uint8_t> FromPattern(const char* pattern)
{
    std::vector<uint8_t> out;
    for (const char* p = pattern; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        if (p[0] == '?')
            out.push_back(0xCC);
        else
            out.push_back(static_cast<uint8_t>(std::stoul(std::string(p, 2), nullptr, 16)));
        p += 2;
    }
    return out;
}
bool Matches(const char* pattern, const std::vector<uint8_t>& bytes)
{
    const auto fixed = FromPattern(pattern);
    if (fixed.size() != bytes.size())
        return false;
    size_t i = 0;
    for (const char* p = pattern; *p; ++i)
    {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;
        if (p[0] != '?' && fixed[i] != bytes[i])
            return false;
        p += 2;
    }
    return true;
}
void PutRip(std::vector<uint8_t>& site, size_t disp, int64_t siteRva, int64_t target)
{
    const int32_t value = static_cast<int32_t>(target - (siteRva + static_cast<int64_t>(disp) + 4));
    std::memcpy(site.data() + disp, &value, 4);
}
void PutCall(std::vector<uint8_t>& site, size_t opcode, int64_t siteRva, int64_t target)
{
    const int32_t value = static_cast<int32_t>(target - (siteRva + static_cast<int64_t>(opcode) + 5));
    std::memcpy(site.data() + opcode + 1, &value, 4);
}
bool OpcodeBefore(const std::vector<uint8_t>& site, size_t disp, std::initializer_list<uint8_t> opcode)
{
    size_t at = disp - opcode.size();
    for (const uint8_t b : opcode)
        if (site[at++] != b)
            return false;
    return true;
}

void TestSignatures()
{
    // The halo3.dll this was read from: the loader's read at +0xA6AE7, the lookup at +0x273565, the header copy at
    // +0x20A9058 and the block's pointer at +0xA49148.
    const int64_t loaderRva = 0xA6AE7, lookupRva = 0x273565, header = 0x20A9058, block = 0xA49148, reader = 0xA73B8;
    const int64_t image = 0x4800000;
    auto loader = FromPattern(kLoaderPattern);
    auto lookup = FromPattern(kLookupPattern);
    Check(loader.size() == kLoaderBytes && lookup.size() == kLookupBytes, "the patterns' lengths");
    Check(OpcodeBefore(loader, kLoaderSize, {0x44, 0x8B, 0x05}) && OpcodeBefore(loader, kLoaderBlockA, {0x4C, 0x8B, 0x0D}) &&
              OpcodeBefore(loader, kLoaderBuffer, {0x8B, 0x15}) && loader[kLoaderReadA] == 0xE8 &&
              OpcodeBefore(loader, kLoaderCount, {0x44, 0x8B, 0x05}) &&
              OpcodeBefore(loader, kLoaderBlockB, {0x4C, 0x8B, 0x0D}) && OpcodeBefore(loader, kLoaderTable, {0x8B, 0x15}) &&
              loader[kLoaderReadB] == 0xE8 && OpcodeBefore(loader, kLoaderBlockC, {0x48, 0x8B, 0x2D}),
          "each loader position is the disp32 (or call) its comment names");
    Check(OpcodeBefore(lookup, kLookupBlock, {0x48, 0x8B, 0x3D}) && OpcodeBefore(lookup, kLookupCount, {0x3B, 0x05}),
          "each lookup position is the disp32 its comment names");
    // The layout constants are bytes of the patterns themselves.
    const uint8_t addNames[] = {0x49, 0x81, 0xC1, 0x00, 0x00, 0x02, 0x00};
    const uint8_t clearPointers[] = {0x4C, 0x8D, 0xB5, 0x00, 0x00, 0x82, 0x00};
    const uint8_t limit[] = {0x81, 0x3C, 0x87, 0xFF, 0xFF, 0x7F, 0x00};
    Check(std::memcmp(loader.data() + 36, addNames, sizeof(addNames)) == 0 &&
              std::memcmp(loader.data() + 128, clearPointers, sizeof(clearPointers)) == 0 &&
              std::memcmp(lookup.data() + 18, limit, sizeof(limit)) == 0 && kLoadedBufferAt == 0x20000,
          "the names at block+0x20000, the pointer array at +0x820000, offsets up to 0x7FFFFF: in the code itself");

    PutRip(loader, kLoaderSize, loaderRva, header + 0x28);
    PutRip(loader, kLoaderBlockA, loaderRva, block);
    PutRip(loader, kLoaderBuffer, loaderRva, header + 0x24);
    PutCall(loader, kLoaderReadA, loaderRva, reader);
    PutRip(loader, kLoaderCount, loaderRva, header + 0x20);
    PutRip(loader, kLoaderBlockB, loaderRva, block);
    PutRip(loader, kLoaderTable, loaderRva, header + 0x2C);
    PutCall(loader, kLoaderReadB, loaderRva, reader);
    PutRip(loader, kLoaderBlockC, loaderRva, block);
    PutRip(lookup, kLookupBlock, lookupRva, block);
    PutRip(lookup, kLookupCount, lookupRva, header + 0x20);
    Check(Matches(kLoaderPattern, loader) && Matches(kLookupPattern, lookup), "the built sites match their patterns");

    Sites sites;
    Check(DecodeSites(loader.data(), loaderRva, lookup.data(), lookupRva, image, sites) == Bind::Bound &&
              sites.headerRva == header && sites.blockSlotRva == block,
          "the header copy and the block's pointer decode from the two sites");
    Check(DecodeSites(nullptr, loaderRva, lookup.data(), lookupRva, image, sites) == Bind::LoaderMissing,
          "no loader: not bound");
    Check(DecodeSites(loader.data(), loaderRva, nullptr, lookupRva, image, sites) == Bind::LookupMissing,
          "no lookup: not bound");
    auto bad = loader;
    PutRip(bad, kLoaderBlockC, loaderRva, block + 8);
    Check(DecodeSites(bad.data(), loaderRva, lookup.data(), lookupRva, image, sites) == Bind::BlockDisagrees,
          "the loader's reads of the block's pointer must agree");
    bad = loader;
    PutCall(bad, kLoaderReadB, loaderRva, reader + 0x10);
    Check(DecodeSites(bad.data(), loaderRva, lookup.data(), lookupRva, image, sites) == Bind::ReadsDisagree,
          "both section reads must call one function");
    for (const size_t field : {kLoaderSize, kLoaderBuffer, kLoaderTable})
    {
        bad = loader;
        PutRip(bad, field, loaderRva, header + 0x30);
        Check(DecodeSites(bad.data(), loaderRva, lookup.data(), lookupRva, image, sites) == Bind::NotOneHeader,
              "a name field that is not its place in one header (a neighbouring field) is refused");
    }
    auto badLookup = lookup;
    PutRip(badLookup, kLookupBlock, lookupRva, block + 8);
    Check(DecodeSites(loader.data(), loaderRva, badLookup.data(), lookupRva, image, sites) == Bind::LookupDisagrees,
          "the lookup must name the loader's block pointer");
    badLookup = lookup;
    PutRip(badLookup, kLookupCount, lookupRva, header + 0x24);
    Check(DecodeSites(loader.data(), loaderRva, badLookup.data(), lookupRva, image, sites) == Bind::LookupDisagrees,
          "the lookup must bound by the loader's count");
    Check(DecodeSites(loader.data(), loaderRva, lookup.data(), lookupRva, header + kHeaderSize - 4, sites) ==
              Bind::Implausible,
          "a header that does not fit inside the module is refused");
    auto misaligned = loader;
    auto misalignedLookup = lookup;
    for (const size_t at : {kLoaderBlockA, kLoaderBlockB, kLoaderBlockC})
        PutRip(misaligned, at, loaderRva, block + 4);
    PutRip(misalignedLookup, kLookupBlock, lookupRva, block + 4);
    Check(DecodeSites(misaligned.data(), loaderRva, misalignedLookup.data(), lookupRva, image, sites) ==
              Bind::Implausible,
          "a pointer that is not on eight bytes is refused");
    for (int why = 0; why < static_cast<int>(Bind::Count); ++why)
        Check(std::strcmp(BindName(static_cast<Bind>(why)), "?") != 0, "every bind result is named");
}

Halo3DefinitionFields Def(int physics, float radius, float moment = -1.0f, int specific = -1)
{
    Halo3DefinitionFields def;
    def.physicsType = physics;
    def.boundingRadius = radius;
    def.jeepValid = moment >= 0.0f;
    def.engineMoment = moment;
    def.scoutValid = specific >= 0;
    def.specificType = specific;
    return def;
}
uint8_t Verdict(std::string_view path)
{
    return Halo3PathVerdict(path.data(), path.size());
}
uint8_t Stock(Halo3VehicleId id)
{
    return static_cast<uint8_t>(id);
}

void TestStockPaths()
{
    bool wellFormed = true;
    for (const Halo3StockVehiclePath& stock : kHalo3StockVehiclePaths)
    {
        const std::string_view path = stock.path;
        wellFormed = wellFormed && path.rfind("objects\\", 0) == 0 && stock.id != Halo3VehicleId::Unknown &&
                     stock.id != Halo3VehicleId::StationaryTurret && Verdict(path) == Stock(stock.id);
        for (const char c : path)
            wellFormed = wellFormed && !(c >= 'A' && c <= 'Z');
        int same = 0;
        for (const Halo3StockVehiclePath& other : kHalo3StockVehiclePaths)
            same += path == other.path ? 1 : 0;
        wellFormed = wellFormed && same == 1;
    }
    Check(wellFormed, "every stock path is lower case, under objects\\, listed once, and names its own vehicle");
    Check(Stock(Halo3VehicleId::Elephant) < kHalo3PathNotStock, "no identity is a verdict code");
    Check(Verdict("objects\\levels\\multi\\shrine\\behemoth\\behemoth") == Stock(Halo3VehicleId::Elephant),
          "the Elephant's tag is a stock path");
    Check(Verdict("Objects\\Vehicles\\Warthog\\Warthog") == Stock(Halo3VehicleId::Warthog),
          "a path is matched case aside");
    Check(Verdict("objects\\vehicles\\sparrowhawk\\sparrowhawk") == kHalo3PathNotStock &&
              Verdict("objects\\vehicles\\warthog\\warthog_fast") == kHalo3PathNotStock &&
              Verdict("objects\\vehicles\\warthog\\warthog\\x") == kHalo3PathNotStock &&
              Verdict("objects\\vehicles\\warthog") == kHalo3PathNotStock,
          "any other path, a stock one's prefix or extension included, is not a stock vehicle's");
    Check(Verdict("") == kHalo3PathNoName && Halo3PathVerdict(nullptr, 4) == kHalo3PathNoName, "no name");

    // Sandtrap, read offline (physics type, definition radius, jeep engine moment, scout specific type).
    const auto elephant = Def(0, 5.129f), scorpion = Def(0, 2.2f), banshee = Def(4, 1.4f),
               mongoose = Def(1, 0.8f, 650.0f), warthog = Def(1, 1.0f, 2000.0f), elephantGun = Def(5, 0.8f),
               hornet = Def(7, 2.268f), chopper = Def(8, 1.307f), ghost = Def(3, 0.85f, -1.0f, 1),
               wraith = Def(3, 1.8f, -1.0f, 3), mauler = Def(3, 1.435f, -1.0f, 4);
    Check(Halo3ResolveVehicleIdByPath(elephant, Stock(Halo3VehicleId::Elephant)) == Halo3VehicleId::Elephant &&
              Halo3ResolveVehicleIdByPath(scorpion, Stock(Halo3VehicleId::Scorpion)) == Halo3VehicleId::Scorpion &&
              Halo3ResolveVehicleIdByPath(banshee, Stock(Halo3VehicleId::Banshee)) == Halo3VehicleId::Banshee &&
              Halo3ResolveVehicleIdByPath(mongoose, Stock(Halo3VehicleId::Mongoose)) == Halo3VehicleId::Mongoose &&
              Halo3ResolveVehicleIdByPath(warthog, Stock(Halo3VehicleId::Warthog)) == Halo3VehicleId::Warthog &&
              Halo3ResolveVehicleIdByPath(hornet, Stock(Halo3VehicleId::Hornet)) == Halo3VehicleId::Hornet &&
              Halo3ResolveVehicleIdByPath(chopper, Stock(Halo3VehicleId::Chopper)) == Halo3VehicleId::Chopper &&
              Halo3ResolveVehicleIdByPath(ghost, Stock(Halo3VehicleId::Ghost)) == Halo3VehicleId::Ghost &&
              Halo3ResolveVehicleIdByPath(wraith, Stock(Halo3VehicleId::Wraith)) == Halo3VehicleId::Wraith &&
              Halo3ResolveVehicleIdByPath(mauler, Stock(Halo3VehicleId::Mauler)) == Halo3VehicleId::Mauler,
          "every stock vehicle at its own path, with its own physics: that vehicle");
    Check(Halo3ResolveVehicleIdByPath(elephantGun, kHalo3PathNotStock) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleId(elephantGun) == Halo3VehicleId::Unknown,
          "the Elephant's gun reads as nothing either way (a mounted gun takes its carrier's identity)");
    const Halo3DefinitionFields allDefs[] = {elephant, scorpion, banshee, mongoose, warthog, hornet,
                                             chopper,  ghost,    wraith,  mauler,   elephantGun};
    bool unchanged = true;
    for (const auto& def : allDefs)
        unchanged = unchanged && Halo3ResolveVehicleIdByPath(def, kHalo3PathNoName) == Halo3ResolveVehicleId(def);
    Check(unchanged, "with no name the rule is the physics and the radius, unchanged");
    auto elephantUnproven = elephant;
    elephantUnproven.boundingRadius = -1.0f;
    Check(Halo3ResolveVehicleIdByPath(elephantUnproven, Stock(Halo3VehicleId::Elephant)) == Halo3VehicleId::Elephant &&
              Halo3ResolveVehicleId(elephantUnproven) == Halo3VehicleId::Scorpion,
          "the Elephant's path with an unproven radius is the Elephant (physics alone would say Scorpion)");
    Check(Halo3ResolveVehicleIdByPath(scorpion, Stock(Halo3VehicleId::Elephant)) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleIdByPath(mongoose, Stock(Halo3VehicleId::Warthog)) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleIdByPath(Def(4, 2.6f), Stock(Halo3VehicleId::Banshee)) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleIdByPath(ghost, Stock(Halo3VehicleId::Wraith)) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleIdByPath(chopper, Stock(Halo3VehicleId::Hornet)) == Halo3VehicleId::Unknown,
          "a stock path whose physics or radius is another vehicle's is one the mod does not know");
    Check(Halo3ResolveVehicleIdByPath(warthog, Stock(Halo3VehicleId::Unknown)) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleIdByPath(warthog, Stock(Halo3VehicleId::StationaryTurret)) == Halo3VehicleId::Unknown,
          "a verdict that names no vehicle resolves to none");

    // HugeGrass, read offline: by physics a Hornet, a Hornet, a Warthog, a Ghost, a Wraith and a Mauler.
    const auto sparrowhawk = Def(7, 3.4f), falcon = Def(7, 5.0f), mammoth = Def(1, 22.0f, 1800.0f),
               spectre = Def(3, 1.3f, -1.0f, 1), shadow = Def(3, 5.0f, -1.0f, 3), skiff = Def(3, 7.5f, -1.0f, 4);
    Check(Halo3ResolveVehicleId(mammoth) == Halo3VehicleId::Warthog &&
              Halo3ResolveVehicleId(spectre) == Halo3VehicleId::Ghost,
          "HugeGrass without names: its Mammoth reads as a Warthog, its Spectre as a Ghost");
    Check(Halo3ResolveVehicleId(sparrowhawk) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleId(falcon) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleId(Def(7, 2.268f)) == Halo3VehicleId::Hornet &&
              Halo3ResolveVehicleId(Def(7, -1.0f)) == Halo3VehicleId::Hornet,
          "without names a flyer with the Hornet's physics is the Hornet only at its radius (2.268 wu), or unproven");
    const Halo3DefinitionFields mods[] = {sparrowhawk, falcon, mammoth, spectre, shadow, skiff};
    bool unknown = true;
    for (const auto& def : mods)
        unknown = unknown && Halo3ResolveVehicleIdByPath(def, kHalo3PathNotStock) == Halo3VehicleId::Unknown;
    Check(unknown, "HugeGrass with names: none of them is a stock vehicle, nor the same size as one");
    Check(Halo3ResolveVehicleIdByPath(Def(7, 2.268f), Verdict("objects\\vehicles\\hornet\\hornet")) ==
                  Halo3VehicleId::Hornet &&
              Halo3ResolveVehicleIdByPath(Def(1, 1.0f, 2000.0f), Verdict("objects\\vehicles\\warthog\\warthog")) ==
                  Halo3VehicleId::Warthog,
          "HugeGrass's own copies of the stock Hornet and Warthog stay those vehicles");
}

void TestSameSize()
{
    // The stock radii as the base tags hold them (float32), each within the tolerance of the table's.
    struct StockRadius
    {
        Halo3VehicleId id;
        float tag;
    };
    const StockRadius stock[] = {{Halo3VehicleId::Scorpion, 2.200000047683716f}, {Halo3VehicleId::Elephant, 5.1290202140808105f},
                           {Halo3VehicleId::Banshee, 1.399999976158142f},  {Halo3VehicleId::Hornet, 2.26774001121521f},
                           {Halo3VehicleId::Warthog, 1.0f},                {Halo3VehicleId::Mongoose, 0.800000011920929f},
                           {Halo3VehicleId::Ghost, 0.8500000238418579f},   {Halo3VehicleId::Wraith, 1.7999999523162842f},
                           {Halo3VehicleId::Mauler, 1.4348019361495972f},  {Halo3VehicleId::Chopper, 1.307170033454895f}};
    bool table = true;
    for (const StockRadius& s : stock)
        table = table && Halo3StockBoundingRadiusWu(s.id) > 0.0f &&
                Halo3RadiusMatches(s.tag, Halo3StockBoundingRadiusWu(s.id)) &&
                std::fabs(s.tag - Halo3StockBoundingRadiusWu(s.id)) < 0.001f;
    Check(table, "every stock vehicle has its base tag's radius");
    Check(Halo3StockBoundingRadiusWu(Halo3VehicleId::Unknown) == 0.0f &&
              Halo3StockBoundingRadiusWu(Halo3VehicleId::StationaryTurret) == 0.0f && !Halo3RadiusMatches(1.0f, 0.0f),
          "no other identity has one, and no radius matches an unknown one");

    // HugeGrass's variants at paths of their own, read offline: the same size as their stock vehicles.
    const Halo3DefinitionFields fast[] = {
        Def(1, 1.0f, 2000.0f),     Def(1, 0.800000011920929f, 650.0f), Def(3, 0.8500000238418579f, -1.0f, 1),
        Def(3, 1.7999999523162842f, -1.0f, 3), Def(3, 1.4348019361495972f, -1.0f, 4), Def(8, 1.307170033454895f),
        Def(4, 1.399999976158142f), Def(0, 2.200000047683716f), Def(0, 5.1290202140808105f),
        Def(7, 2.26774001121521f)};
    const Halo3VehicleId as[] = {Halo3VehicleId::Warthog, Halo3VehicleId::Mongoose, Halo3VehicleId::Ghost,
                                 Halo3VehicleId::Wraith,  Halo3VehicleId::Mauler,   Halo3VehicleId::Chopper,
                                 Halo3VehicleId::Banshee, Halo3VehicleId::Scorpion, Halo3VehicleId::Elephant,
                                 Halo3VehicleId::Hornet};
    bool promoted = true;
    for (size_t i = 0; i < sizeof(as) / sizeof(as[0]); ++i)
        promoted = promoted && Halo3SameSizeStockVehicle(fast[i]) == as[i] &&
                   Halo3ResolveVehicleIdByPath(fast[i], kHalo3PathNotStock) == as[i];
    Check(promoted, "a variant at a path of its own with a stock vehicle's physics and size is that vehicle "
                    "(warthog_fast, mongoose_fast, ghost_fast, wraith_fast, mauler_fast, brute_chopper_fast, "
                    "banshee_fast, scorpion_fast, behemoth_fast)");

    Check(Halo3SameSizeStockVehicle(Def(1, 1.04f, 2000.0f)) == Halo3VehicleId::Warthog &&
              Halo3SameSizeStockVehicle(Def(1, 0.96f, 2000.0f)) == Halo3VehicleId::Warthog,
          "within the tolerance (5%): the same size");
    Check(Halo3SameSizeStockVehicle(Def(1, 1.06f, 2000.0f)) == Halo3VehicleId::Unknown &&
              Halo3SameSizeStockVehicle(Def(1, 0.94f, 2000.0f)) == Halo3VehicleId::Unknown &&
              Halo3SameSizeStockVehicle(Def(3, 1.3f, -1.0f, 1)) == Halo3VehicleId::Unknown &&
              Halo3SameSizeStockVehicle(Def(1, 22.0f, 1800.0f)) == Halo3VehicleId::Unknown,
          "off by more than the tolerance: not promoted (a Spectre against the Ghost, the Mammoth against the Warthog)");
    Check(Halo3SameSizeStockVehicle(Def(1, -1.0f, 2000.0f)) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleIdByPath(Def(1, -1.0f, 2000.0f), kHalo3PathNotStock) == Halo3VehicleId::Unknown &&
              Halo3ResolveVehicleIdByPath(Def(1, -1.0f, 2000.0f), kHalo3PathNoName) == Halo3VehicleId::Warthog &&
              Halo3ResolveVehicleIdByPath(Def(4, -1.0f), kHalo3PathNotStock) == Halo3VehicleId::Unknown,
          "an unproven radius never promotes (HugeGrass's Lich, 32 wu, is past the proven range)");
    Check(Halo3SameSizeStockVehicle(Def(0, 1.0f)) == Halo3VehicleId::Unknown &&
              Halo3SameSizeStockVehicle(Def(5, 0.8f)) == Halo3VehicleId::Unknown &&
              Halo3SameSizeStockVehicle(Def(7, 3.4f)) == Halo3VehicleId::Unknown &&
              Halo3SameSizeStockVehicle(Def(1, 1.0f, 1200.0f)) == Halo3VehicleId::Unknown,
          "physics that read as no stock vehicle are not promoted (a 1 wu tank, a turret, the Sparrowhawk, an engine "
          "moment of no stock jeep)");

    // The names they go by.
    const uint8_t warthogPath = Stock(Halo3VehicleId::Warthog);
    Check(Halo3SeatNameSource(Halo3VehicleId::Warthog, warthogPath, true) == Halo3VehicleNameSource::Identity &&
              Halo3SeatNameSource(Halo3VehicleId::Warthog, kHalo3PathNoName, false) == Halo3VehicleNameSource::Identity,
          "a stock vehicle, or one with no names to go by: the identity's name");
    Check(Halo3SeatNameSource(Halo3VehicleId::Warthog, kHalo3PathNotStock, true) == Halo3VehicleNameSource::Tag &&
              Halo3SeatNameSource(Halo3VehicleId::Warthog, kHalo3PathNotStock, false) ==
                  Halo3VehicleNameSource::Identity,
          "a same-size variant keeps its own name (warthog_fast), the identity's only when it has none");
    Check(Halo3SeatNameSource(Halo3VehicleId::Unknown, kHalo3PathNotStock, true) == Halo3VehicleNameSource::Tag &&
              Halo3SeatNameSource(Halo3VehicleId::Unknown, warthogPath, true) == Halo3VehicleNameSource::Tag &&
              Halo3SeatNameSource(Halo3VehicleId::Unknown, kHalo3PathNoName, false) ==
                  Halo3VehicleNameSource::Fingerprint,
          "one the mod does not know: its tag's name, else its fingerprint");
    Check(Halo3SeatNameSource(Halo3VehicleId::StationaryTurret, kHalo3PathNotStock, true) ==
              Halo3VehicleNameSource::Identity,
          "a stationary turret stays a turret");
}
// A camera file's part name against a material path: exact first; a variant's suffix only as a fallback the caller
// takes when nothing matches exactly.
void TestPartNames()
{
    const std::string_view snow = "objects/vehicles/warthog/shaders/windshield_snow";
    Check(MatchPartName(snow, "windshield_snow") == PartNameMatch::Exact, "a material's own last part is exact");
    Check(MatchPartName(snow, snow) == PartNameMatch::Exact, "and so is its whole path");
    Check(MatchPartName(snow, "WINDSHIELD") == PartNameMatch::Variant, "a name and an underscore suffix is a variant");
    Check(MatchPartName(snow, "windshield_") == PartNameMatch::None, "a bare underscore is not a name");
    Check(MatchPartName(snow, "wind") == PartNameMatch::None, "a name cut short of the underscore is nothing");
    Check(MatchPartName("objects/vehicles/sparrowhawk/shaders/sparrowhawk_glass_dark", "sparrowhawk_glass") ==
              PartNameMatch::Variant,
          "the Sparrowhawk's dark glass is only a variant of its glass, taken when its glass is not there");
    Check(MatchPartName(snow, "") == PartNameMatch::None && MatchPartName("", "glass") == PartNameMatch::None,
          "empty names match nothing");
}
} // namespace

int main()
{
    TestHeader();
    TestFileOffset();
    TestFileTable(true);
    TestFileTable(false);
    TestNameRefusals();
    TestDisplayName();
    TestSignatures();
    TestStockPaths();
    TestSameSize();
    TestPartNames();
    std::printf("halo3_tag_names_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
