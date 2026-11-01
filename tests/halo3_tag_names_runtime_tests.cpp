// Halo 3 tag names at runtime (src/dll/halo3_tag_names.inl), run against a synthetic halo3.dll image: the install
// finds and decodes both sites, and the cold worker reads a map's names once, proves them that map's, publishes the
// camera thread's verdicts, notices a new map under one generation and a map ending, refuses what does not prove
// itself (logging each fault once), backs off, and withdraws everything on removal.
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "halo3_tag_names.h"
#include "halo3_vehicle_logic.h"

namespace
{
std::vector<std::string> g_log;
uint64_t g_now = 1000;
} // namespace
#define LOG(...)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        char logLine[1024];                                                                                            \
        std::snprintf(logLine, sizeof(logLine), __VA_ARGS__);                                                          \
        g_log.emplace_back(logLine);                                                                                   \
    } while (0)
#define GetTickCount64() g_now
std::atomic<uint32_t> g_halo3RuntimeGeneration{0};

namespace sig
{
uint32_t CopyUnhooked(uintptr_t address, void* out, size_t size)
{
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return 0;
}
} // namespace sig
namespace mcc_clock
{
uintptr_t FindUnique(uintptr_t base, size_t size, const char* pattern)
{
    std::vector<int> bytes;
    for (const char* p = pattern; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        bytes.push_back(p[0] == '?' ? -1 : static_cast<int>(std::stoul(std::string(p, 2), nullptr, 16)));
        p += 2;
    }
    const auto* image = reinterpret_cast<const uint8_t*>(base);
    uintptr_t found = 0;
    int matches = 0;
    for (size_t at = 0; at + bytes.size() <= size; ++at)
    {
        size_t i = 0;
        while (i < bytes.size() && (bytes[i] < 0 || image[at + i] == bytes[i]))
            ++i;
        if (i == bytes.size())
        {
            found = base + at;
            ++matches;
        }
    }
    return matches == 1 ? found : 0;
}
} // namespace mcc_clock

#include "../src/dll/halo3_tag_names.inl"

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
        for (const std::string& line : g_log)
            std::printf("    log: %s\n", line.c_str());
    }
}
bool Logged(std::string_view text)
{
    for (const std::string& line : g_log)
        if (line.find(text) != std::string::npos)
            return true;
    return false;
}
size_t CountLogged(std::string_view text)
{
    size_t count = 0;
    for (const std::string& line : g_log)
        count += line.find(text) != std::string::npos ? 1 : 0;
    return count;
}

using namespace halo3_tag_names;

// The module: the loader's read at +0x1000, the lookup at +0x3000, the block's pointer at +0x8000, the header copy at
// +0x10000.
constexpr size_t kImageSize = 0x18000, kLoaderAt = 0x1000, kLookupAt = 0x3000, kSlotAt = 0x8000, kHeaderAt = 0x10000;

void Put32(uint8_t* at, uint32_t value)
{
    std::memcpy(at, &value, 4);
}
void PlacePattern(std::vector<uint8_t>& image, size_t at, const char* pattern)
{
    size_t i = 0;
    for (const char* p = pattern; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        image[at + i++] = p[0] == '?' ? 0x90 : static_cast<uint8_t>(std::stoul(std::string(p, 2), nullptr, 16));
        p += 2;
    }
}
void PutRip(std::vector<uint8_t>& image, size_t site, size_t disp, size_t target)
{
    Put32(image.data() + site + disp, static_cast<uint32_t>(static_cast<int64_t>(target) -
                                                            static_cast<int64_t>(site + disp + 4)));
}
std::vector<uint8_t> MakeImage(bool withLookup = true)
{
    std::vector<uint8_t> image(kImageSize, 0xCC);
    PlacePattern(image, kLoaderAt, kLoaderPattern);
    PutRip(image, kLoaderAt, kLoaderSize, kHeaderAt + kNameBufferSizeOffset);
    PutRip(image, kLoaderAt, kLoaderBlockA, kSlotAt);
    PutRip(image, kLoaderAt, kLoaderBuffer, kHeaderAt + kNameBufferOffset);
    PutRip(image, kLoaderAt, kLoaderCount, kHeaderAt + kNameCountOffset);
    PutRip(image, kLoaderAt, kLoaderBlockB, kSlotAt);
    PutRip(image, kLoaderAt, kLoaderTable, kHeaderAt + kNameTableOffset);
    PutRip(image, kLoaderAt, kLoaderBlockC, kSlotAt);
    const size_t reader = 0x5000;
    Put32(image.data() + kLoaderAt + kLoaderReadA + 1,
          static_cast<uint32_t>(static_cast<int64_t>(reader) - static_cast<int64_t>(kLoaderAt + kLoaderReadA + 5)));
    Put32(image.data() + kLoaderAt + kLoaderReadB + 1,
          static_cast<uint32_t>(static_cast<int64_t>(reader) - static_cast<int64_t>(kLoaderAt + kLoaderReadB + 5)));
    if (withLookup)
    {
        PlacePattern(image, kLookupAt, kLookupPattern);
        PutRip(image, kLookupAt, kLookupBlock, kSlotAt);
        PutRip(image, kLookupAt, kLookupCount, kHeaderAt + kNameCountOffset);
    }
    std::memset(image.data() + kSlotAt, 0, 8);
    std::memset(image.data() + kHeaderAt, 0, kHeaderSize);
    return image;
}

// A map: its header written into the image's copy, its names into a block like the game's (offsets at +0, names at
// +0x20000). Index 0 has no name.
struct Map
{
    std::vector<uint8_t> block;
};
void LoadMap(std::vector<uint8_t>& image, Map& map, const char* scenarioName, const char* scenarioPath,
             const std::vector<std::string>& names, uint8_t engine = kEngineHalo3)
{
    std::string buffer;
    std::vector<int32_t> offsets;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i].empty())
        {
            offsets.push_back(-1);
            continue;
        }
        offsets.push_back(static_cast<int32_t>(buffer.size()));
        buffer += names[i];
        buffer.push_back('\0');
    }
    map.block.assign(kLoadedBufferAt + buffer.size(), 0);
    std::memcpy(map.block.data(), offsets.data(), offsets.size() * 4);
    std::memcpy(map.block.data() + kLoadedBufferAt, buffer.data(), buffer.size());
    uint8_t* header = image.data() + kHeaderAt;
    std::memset(header, 0, kHeaderSize);
    Put32(header, kHeadMagic);
    Put32(header + 4, static_cast<uint32_t>(kVersion));
    header[kEngineOffset] = engine;
    Put32(header + kFootOffset, kFootMagic);
    std::memcpy(header + kScenarioNameOffset, scenarioName, std::strlen(scenarioName) + 1);
    std::memcpy(header + kScenarioPathOffset, scenarioPath, std::strlen(scenarioPath) + 1);
    Put32(header + kNameCountOffset, static_cast<uint32_t>(offsets.size()));
    Put32(header + kNameBufferOffset, 0x7A932000);
    Put32(header + kNameBufferSizeOffset, static_cast<uint32_t>(buffer.size()));
    Put32(header + kNameTableOffset, 0x7AA42000);
    const uint8_t* blockAddress = map.block.data();
    std::memcpy(image.data() + kSlotAt, &blockAddress, sizeof(blockAddress));
}

uint8_t VerdictNow(uint32_t index, uint32_t generation)
{
    uint32_t serial = 0;
    return Halo3TagPathVerdictNow(index, generation, serial);
}
uint32_t SerialNow()
{
    return g_halo3TagPaths.serial.load();
}
void Tick(uint64_t advanceMs = 0)
{
    g_now += advanceMs;
    Halo3TagNames_ColdTick();
}

const std::vector<std::string> kHugeGrass = {
    "",
    "globals\\globals",
    "levels\\multi\\hugeass\\hugeass",
    "objects\\vehicles\\sparrowhawk\\sparrowhawk",
    "objects\\vehicles\\warthog\\warthog",
    "objects\\levels\\multi\\shrine\\behemoth\\behemoth",
};
const std::vector<std::string> kSandtrap = {
    "", "globals\\globals", "objects\\vehicles\\scorpion\\scorpion", "levels\\multi\\shrine\\shrine",
};

void TestInstall()
{
    auto image = MakeImage(false);
    const auto base = reinterpret_cast<uintptr_t>(image.data());
    g_log.clear();
    Check(!InstallHalo3TagNames(base, image.size(), 1) && Logged("not bound (the game's guarded name lookup") &&
              !g_halo3TagNames.header,
          "with no lookup site the names are not bound, and the log says why");
    Tick(5000);
    Check(VerdictNow(1, 1) == kHalo3PathNoName, "not bound: every verdict is no name");
}

void TestColdWorker()
{
    auto image = MakeImage();
    const auto base = reinterpret_cast<uintptr_t>(image.data());
    g_log.clear();
    Check(InstallHalo3TagNames(base, image.size(), 7) && Logged("H3 tag names: bound (generation 7)") &&
              g_halo3TagNames.header == image.data() + kHeaderAt &&
              reinterpret_cast<const uint8_t*>(g_halo3TagNames.blockSlot) == image.data() + kSlotAt,
          "the install decodes the header copy and the block's pointer from the image");

    Map hugegrass;
    LoadMap(image, hugegrass, "hugeass", "levels\\multi\\hugeass\\hugeass", kHugeGrass);
    g_halo3RuntimeGeneration = 0;
    Tick();
    Check(!g_halo3TagNames.loaded && VerdictNow(3, 7) == kHalo3PathNoName, "no generation: nothing is read");

    g_halo3RuntimeGeneration = 7;
    const uint32_t before = SerialNow();
    g_log.clear();
    Tick();
    Check(g_halo3TagNames.loaded && Logged("H3 tag names: 5 of 6 tags named in levels\\multi\\hugeass\\hugeass") &&
              Logged("2 carry a stock vehicle's path"),
          "a new generation's map is read once, and the log says how much");
    Check(SerialNow() != before, "the publication moves the identity cache's key");
    Check(VerdictNow(3, 7) == kHalo3PathNotStock &&
              VerdictNow(4, 7) == static_cast<uint8_t>(Halo3VehicleId::Warthog) &&
              VerdictNow(5, 7) == static_cast<uint8_t>(Halo3VehicleId::Elephant) && VerdictNow(0, 7) == kHalo3PathNoName,
          "the camera thread's verdicts: the Sparrowhawk is no stock vehicle, the Warthog and the Elephant are");
    Check(VerdictNow(3, 8) == kHalo3PathNoName && VerdictNow(6, 7) == kHalo3PathNoName && VerdictNow(0x189E, 7) ==
                                                                                               kHalo3PathNoName,
          "another generation's, or a tag past the table: no name");
    Check(Halo3TagNameOf(3) == "objects\\vehicles\\sparrowhawk\\sparrowhawk" && Halo3TagNameOf(0).empty() &&
              Halo3TagNameOf(0x10003).empty(),
          "the cold worker's own copy names the tag");
    Check(Halo3TagPathVerdictOf(5) == static_cast<uint8_t>(Halo3VehicleId::Elephant), "and says what its path is");
    Check(!Halo3TagNamesSettled(), "not settled the moment it is published");
    g_now += 300;
    Check(Halo3TagNamesSettled(), "settled once the camera thread has had a few frames");

    // A frame of the camera thread while the worker is writing: no name, and a key that never matches.
    g_halo3TagPaths.seq.fetch_add(1);
    uint32_t serial = 0;
    Check(Halo3TagPathVerdictNow(3, 7, serial) == kHalo3PathNoName && serial == ~0u,
          "mid-publication: no name this frame, and the identity is resolved again next frame");
    g_halo3TagPaths.seq.fetch_add(1);

    // The same map a second later: nothing is read again.
    const uint32_t steady = SerialNow();
    g_log.clear();
    Tick(1000);
    Tick(1000);
    Check(SerialNow() == steady && g_log.empty(), "an unchanged header is not read again");

    // The game copies another map's header in under the same generation.
    Map sandtrap;
    LoadMap(image, sandtrap, "shrine", "levels\\multi\\shrine\\shrine", kSandtrap);
    g_log.clear();
    Tick(1000);
    Check(Logged("header changed within one generation") && Logged("4 tags named in levels\\multi\\shrine\\shrine") &&
              VerdictNow(2, 7) == static_cast<uint8_t>(Halo3VehicleId::Scorpion) && VerdictNow(3, 7) == kHalo3PathNotStock &&
              Halo3TagNameOf(4).empty(),
          "a new map under one generation is read again, and only its names stand");

    // The game disposes of the map: its header copy is cleared.
    std::memset(image.data() + kHeaderAt, 0, kHeaderSize);
    g_log.clear();
    Tick(1000);
    Check(Logged("header was cleared (the map is ending)") && VerdictNow(2, 7) == kHalo3PathNoName &&
              !g_halo3TagNames.loaded,
          "a map ending withdraws its names");
    for (int i = 0; i < 5; ++i)
        Tick(1000);
    Check(!Logged("not read for generation"), "and is not reported as a failure while the next map loads");
    LoadMap(image, hugegrass, "hugeass", "levels\\multi\\hugeass\\hugeass", kHugeGrass);
    Tick(1000);
    Check(g_halo3TagNames.loaded && VerdictNow(3, 7) == kHalo3PathNotStock, "the next map in is read");

    // A new generation whose table does not hold its own scenario: refused, logged once, tried again with backoff.
    LoadMap(image, sandtrap, "shrine", "levels\\multi\\shrine\\other", kSandtrap);
    g_halo3RuntimeGeneration = 8;
    g_log.clear();
    Tick();
    Check(!g_halo3TagNames.loaded && VerdictNow(2, 8) == kHalo3PathNoName && VerdictNow(3, 7) == kHalo3PathNoName &&
              Logged("not read for generation 8 (the game's name table does not hold this map's scenario"),
          "a table that is not its header's map's is refused, the old generation's verdicts withdrawn");
    for (int i = 0; i < 40; ++i)
        Tick(500);
    Check(CountLogged("not read for generation 8") == 1, "the same fault is logged once");
    LoadMap(image, sandtrap, "shrine", "levels\\multi\\shrine\\shrine", kSandtrap);
    Tick(16000);
    Check(g_halo3TagNames.loaded && VerdictNow(2, 8) == static_cast<uint8_t>(Halo3VehicleId::Scorpion),
          "once it proves itself it is read");

    // Faults of the header and of the counts, each its own line.
    struct Case
    {
        const char* what;
        uint8_t engine;
        uint32_t count;
        uint32_t size;
        const char* expect;
    };
    const Case cases[] = {
        {"another title's header", 5, 0, 0, "another title's map"},
        {"no tags", kEngineHalo3, 0, 0, "the header names 0 tags"},
        {"more tags than the block holds", kEngineHalo3, kLoadedMaxNames + 1, 0, "the header names 32769 tags"},
        {"a buffer larger than the mod reads", kEngineHalo3, 4, kMaxBufferCopy + 1, "the names take 4194305 bytes"},
    };
    uint32_t generation = 9;
    for (const Case& c : cases)
    {
        LoadMap(image, sandtrap, "shrine", "levels\\multi\\shrine\\shrine", kSandtrap, c.engine);
        if (c.count || c.engine == kEngineHalo3)
            Put32(image.data() + kHeaderAt + kNameCountOffset, c.count);
        if (c.size)
            Put32(image.data() + kHeaderAt + kNameBufferSizeOffset, c.size);
        g_halo3RuntimeGeneration = generation++;
        g_log.clear();
        Tick();
        Check(!g_halo3TagNames.loaded && Logged(c.expect), c.what);
    }
    LoadMap(image, sandtrap, "shrine", "levels\\multi\\shrine\\shrine", kSandtrap);
    const uint8_t* none = nullptr;
    std::memcpy(image.data() + kSlotAt, &none, sizeof(none));
    g_halo3RuntimeGeneration = generation++;
    g_log.clear();
    Tick();
    Check(!g_halo3TagNames.loaded && Logged("the game has no name block"), "no block: refused");

    // Removal withdraws everything; the worker does nothing more.
    LoadMap(image, sandtrap, "shrine", "levels\\multi\\shrine\\shrine", kSandtrap);
    g_halo3RuntimeGeneration = generation++;
    Tick();
    Check(g_halo3TagNames.loaded, "read again before the removal");
    RemoveHalo3TagNames();
    Check(!g_halo3TagNames.header && !g_halo3TagNames.loaded && VerdictNow(2, generation - 1) == kHalo3PathNoName &&
              g_halo3TagNames.buffer.capacity() == 0,
          "the removal withdraws the verdicts and frees the copy");
    g_log.clear();
    Tick(20000);
    Check(g_log.empty() && !g_halo3TagNames.loaded, "and nothing is read after it");

    // The title ends (generation 0) while bound: withdrawn, the memory kept for the next level.
    InstallHalo3TagNames(base, image.size(), 20);
    g_halo3RuntimeGeneration = 20;
    Tick();
    Check(g_halo3TagNames.loaded, "a new install reads the map");
    g_halo3RuntimeGeneration = 0;
    Tick();
    Check(!g_halo3TagNames.loaded && VerdictNow(2, 20) == kHalo3PathNoName, "generation 0 withdraws the names");
}
} // namespace

int main()
{
    TestInstall();
    TestColdWorker();
    std::printf("halo3_tag_names_runtime_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
