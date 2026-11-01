// HALO 3 TAG NAMES AT RUNTIME: what each vehicle's tag is called in the map being played, so a vehicle the mod does
// not know goes by its own name (objects\vehicles\sparrowhawk\sparrowhawk -> "sparrowhawk") and a mod's vehicle is
// not taken for the stock one whose physics it shares. The layout and the proof are halo3_tag_names.h's; the rule
// that uses a path is halo3_vehicle_logic.h's (Halo3ResolveVehicleIdByPath). Included by game.cpp ahead of the
// vehicle identity cache.
//
// WHERE THE NAMES COME FROM. halo3.dll keeps the loaded map's tag-name table in memory (halo3_tag_names.h, IN THE
// GAME'S MEMORY), so no file is opened: the install decodes the loaded header's copy and the name block's pointer
// from two unique signatures, and the cold worker copies the table out once per map. Before anything is used the
// copy must prove it belongs to the map being played: the header must be a Halo 3 one, its name count and buffer
// size must fit the game's own block, and the scenario path the header names must be the name of a copied tag.
//
// THREADS.
//   The cold worker (WaitThread) installs, removes, copies (Halo3TagNames_ColdTick) and alone reads the copy
//     (Halo3TagNameOf). It allocates and logs; it reads game memory only under a fault guard.
//   The camera thread's identity cache (Halo3ResolveIdentityCached) reads one published byte per tag index
//     (Halo3TagPathVerdictNow): lock-free, no game memory, no allocation.
//
// ONCE PER MAP. A new runtime generation (every level) is read again, and so is a header copy that changes under one
// generation (a 512-byte look once a second; the game clears its copy when it disposes of a map). Until a copy has
// proved itself, and whenever none can, every verdict is kHalo3PathNoName: the identity is the physics and the radius
// alone and an unknown vehicle keeps its custom_<fingerprint> name, exactly as without this file. The log says
// which, once per map.
//
// CO-OP. Read-only: nothing is written to the game.

// Room for a seat's vehicle name (Halo3SeatVehicleName): a tag's short name, cut to 63 characters (the longest a
// vehicle tag has in the stock and workshop Halo 3 maps is 30), "custom_" and eight hex digits, or a known vehicle's.
inline constexpr size_t kHalo3VehicleNameCapacity = 64;

// Published by the cold worker for the camera thread: per tag index, Halo3PathVerdict of its name.
struct Halo3TagPathPublished
{
    std::atomic<uint32_t> seq{0};        // even = stable
    std::atomic<uint32_t> serial{0};     // +1 at every publication or withdrawal: the identity cache's key
    std::atomic<uint32_t> generation{0}; // the runtime generation the verdicts are for; 0 = none
    std::atomic<uint32_t> count{0};
    std::atomic<uint8_t> verdict[halo3_tag_names::kLoadedMaxNames];
};
Halo3TagPathPublished g_halo3TagPaths;

// The camera thread: what tag `defIndex`'s name says in `generation`, and the publication it was read from (~0 when
// the cold worker was writing: resolve again later). kHalo3PathNoName when nothing is published for `generation`.
uint8_t Halo3TagPathVerdictNow(uint32_t defIndex, uint32_t generation, uint32_t& serial)
{
    const Halo3TagPathPublished& published = g_halo3TagPaths;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const uint32_t readSerial = published.serial.load(std::memory_order_relaxed);
        uint8_t verdict = kHalo3PathNoName;
        if (generation && published.generation.load(std::memory_order_relaxed) == generation &&
            defIndex < published.count.load(std::memory_order_relaxed))
            verdict = published.verdict[defIndex].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (published.seq.load(std::memory_order_relaxed) != before)
            continue;
        serial = readSerial;
        return verdict;
    }
    serial = ~0u;
    return kHalo3PathNoName;
}

// The cold worker's state. The two pointers are into halo3.dll, set by the install and cleared by the removal.
struct Halo3TagNameState
{
    const uint8_t* header = nullptr;
    const uint8_t* const* blockSlot = nullptr;
    uint32_t generation = 0;  // the generation last read for
    bool loaded = false;      // the copy below is that generation's map's, proven
    bool published = false;   // g_halo3TagPaths holds something to withdraw
    bool ending = false;      // the game cleared the loaded header: its map is being disposed of
    uint64_t loadedMs = 0;
    uint64_t stamp = 0;       // the header's fixed part, hashed, when the copy was made
    uint64_t nextAttemptMs = 0;
    uint64_t nextStampMs = 0;
    uint32_t failures = 0;
    std::string lastFault;    // logged once per generation per fault
    halo3_tag_names::Header parsed;
    std::vector<uint8_t> headerBytes;
    std::vector<uint8_t> offsets;
    std::vector<char> buffer;
    std::vector<uint8_t> verdicts;
};
Halo3TagNameState g_halo3TagNames;

namespace
{
// Guarded: plain helpers, so the SEH frames hold no C++ object.
bool Halo3TagNamesCopy(void* out, const void* from, size_t size)
{
    __try
    {
        std::memcpy(out, from, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
bool Halo3TagNamesReadSlot(const uint8_t* const* slot, const uint8_t*& value)
{
    value = nullptr;
    __try
    {
        value = *slot;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

// The header's fixed part (sizes, section addresses, checksums, build date, scenario), hashed: it changes with the map.
inline constexpr size_t kHalo3TagNamesStampBytes = 0x200;
uint64_t Halo3TagNamesStamp(const uint8_t* bytes)
{
    uint64_t hash = 14695981039346656037ull; // FNV-1a 64
    for (size_t i = 0; i < kHalo3TagNamesStampBytes; ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

void Halo3TagPathsPublish(uint32_t generation, const uint8_t* verdicts, uint32_t count)
{
    Halo3TagPathPublished& published = g_halo3TagPaths;
    if (count > halo3_tag_names::kLoadedMaxNames)
        count = halo3_tag_names::kLoadedMaxNames;
    published.seq.fetch_add(1, std::memory_order_acq_rel); // -> odd
    std::atomic_thread_fence(std::memory_order_release);
    published.serial.fetch_add(1, std::memory_order_relaxed);
    published.generation.store(generation, std::memory_order_relaxed);
    published.count.store(count, std::memory_order_relaxed);
    for (uint32_t i = 0; i < count; ++i)
        published.verdict[i].store(verdicts[i], std::memory_order_relaxed);
    published.seq.fetch_add(1, std::memory_order_release); // -> even
    g_halo3TagNames.published = generation != 0;
}

// Back to no names: every identity by physics and radius, every unknown vehicle by its fingerprint.
void Halo3TagNamesForget(bool release)
{
    Halo3TagNameState& names = g_halo3TagNames;
    if (names.published)
        Halo3TagPathsPublish(0, nullptr, 0);
    names.loaded = false;
    names.offsets.clear();
    names.buffer.clear();
    names.verdicts.clear();
    if (release)
    {
        std::vector<uint8_t>().swap(names.offsets);
        std::vector<char>().swap(names.buffer);
        std::vector<uint8_t>().swap(names.verdicts);
        std::vector<uint8_t>().swap(names.headerBytes);
    }
}

bool InstallHalo3TagNames(uintptr_t base, size_t size, uint32_t generation)
{
    namespace tn = halo3_tag_names;
    Halo3TagNameState& names = g_halo3TagNames;
    names.header = nullptr;
    names.blockSlot = nullptr;
    const uintptr_t loader = mcc_clock::FindUnique(base, size, tn::kLoaderPattern);
    const uintptr_t lookup = mcc_clock::FindUnique(base, size, tn::kLookupPattern);
    // The bytes as the game has them (a hook of this mod's undone), as the match itself compared them.
    uint8_t loaderBytes[tn::kLoaderBytes] = {}, lookupBytes[tn::kLookupBytes] = {};
    if (loader)
        sig::CopyUnhooked(loader, loaderBytes, sizeof(loaderBytes));
    if (lookup)
        sig::CopyUnhooked(lookup, lookupBytes, sizeof(lookupBytes));
    tn::Sites sites;
    const tn::Bind why = tn::DecodeSites(loader ? loaderBytes : nullptr, static_cast<int64_t>(loader - base),
                                         lookup ? lookupBytes : nullptr, static_cast<int64_t>(lookup - base),
                                         static_cast<int64_t>(size), sites);
    if (why != tn::Bind::Bound)
    {
        LOG("H3 tag names: not bound (%s); vehicles are identified by their physics alone and one the mod does not "
            "know is named custom_<fingerprint>",
            tn::BindName(why));
        return false;
    }
    names.header = reinterpret_cast<const uint8_t*>(base + static_cast<uintptr_t>(sites.headerRva));
    names.blockSlot = reinterpret_cast<const uint8_t* const*>(base + static_cast<uintptr_t>(sites.blockSlotRva));
    LOG("H3 tag names: bound (generation %u): the loaded map's header copy +0x%llX and the name block's pointer "
        "+0x%llX, from the cache loader's read of the table +0x%llX and the game's guarded name lookup +0x%llX, "
        "which agree",
        generation, static_cast<unsigned long long>(sites.headerRva),
        static_cast<unsigned long long>(sites.blockSlotRva), static_cast<unsigned long long>(loader - base),
        static_cast<unsigned long long>(lookup - base));
    return true;
}

// Before halo3.dll goes (the cold worker's Halo 3 cleanup).
void RemoveHalo3TagNames()
{
    Halo3TagNameState& names = g_halo3TagNames;
    names.header = nullptr;
    names.blockSlot = nullptr;
    names.generation = 0;
    Halo3TagNamesForget(true);
}

// Copies the loaded map's table and proves it is that map's; then publishes the verdicts. Empty = done; else why not.
std::string Halo3TagNamesLoad(uint32_t generation)
{
    namespace tn = halo3_tag_names;
    Halo3TagNameState& names = g_halo3TagNames;
    names.headerBytes.resize(tn::kHeaderSize);
    if (!Halo3TagNamesCopy(names.headerBytes.data(), names.header, tn::kHeaderSize))
        return "the loaded header could not be read";
    const tn::HeaderFault fault = tn::ParseHeader(names.headerBytes.data(), names.headerBytes.size(), names.parsed);
    if (fault != tn::HeaderFault::None)
        return std::string("the loaded header is not a Halo 3 map's: ") + tn::HeaderFaultName(fault);
    const tn::Header& header = names.parsed;
    char text[160];
    if (!header.nameCount || header.nameCount > tn::kLoadedMaxNames)
    {
        std::snprintf(text, sizeof(text), "the header names %u tags; the game's block holds 1 to %u", header.nameCount,
                      tn::kLoadedMaxNames);
        return text;
    }
    if (!header.nameBufferSize || header.nameBufferSize > tn::kLoadedBufferRoom ||
        header.nameBufferSize > tn::kMaxBufferCopy)
    {
        std::snprintf(text, sizeof(text), "the names take %u bytes; the mod reads 1 to %u", header.nameBufferSize,
                      tn::kMaxBufferCopy);
        return text;
    }
    const uint8_t* block = nullptr;
    if (!Halo3TagNamesReadSlot(names.blockSlot, block) || !block)
        return "the game has no name block";
    names.offsets.resize(static_cast<size_t>(header.nameCount) * 4);
    names.buffer.resize(header.nameBufferSize);
    if (!Halo3TagNamesCopy(names.offsets.data(), block, names.offsets.size()) ||
        !Halo3TagNamesCopy(names.buffer.data(), block + tn::kLoadedBufferAt, names.buffer.size()))
        return "the game's name block could not be read";
    const tn::NameTable table{names.offsets.data(), header.nameCount, names.buffer.data(), header.nameBufferSize};
    uint32_t scenarioTag = 0;
    if (!tn::FindName(table, header.scenarioPath, scenarioTag))
        return std::string("the game's name table does not hold this map's scenario (") + header.scenarioPath +
               "): still loading, or not this map's";
    names.verdicts.assign(header.nameCount, kHalo3PathNoName);
    uint32_t named = 0, stock = 0;
    for (uint32_t i = 0; i < header.nameCount; ++i)
    {
        const std::string_view path = tn::NameAt(table, i);
        if (path.empty())
            continue;
        ++named;
        names.verdicts[i] = Halo3PathVerdict(path.data(), path.size());
        stock += names.verdicts[i] != kHalo3PathNotStock ? 1u : 0u;
    }
    names.stamp = Halo3TagNamesStamp(names.headerBytes.data());
    Halo3TagPathsPublish(generation, names.verdicts.data(), header.nameCount);
    LOG("H3 tag names: %u of %u tags named in %s (%s; the scenario's path at tag %04X), %u KB copied from the game's "
        "own table; %u carry a stock vehicle's path (a vehicle's model, collision and the like share its name). A "
        "vehicle at any other path is one the mod does not know, named by its tag",
        named, header.nameCount, header.scenarioPath, header.scenarioName, scenarioTag,
        static_cast<unsigned>((names.offsets.size() + names.buffer.size() + 1023) / 1024), stock);
    return {};
}

// The cold worker, every pass: reads the names of each map once, and notices a map changing under one generation.
void Halo3TagNames_ColdTick()
{
    Halo3TagNameState& names = g_halo3TagNames;
    const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
    if (!names.header || !names.blockSlot || !generation)
    {
        if (names.generation || names.published)
        {
            names.generation = 0;
            Halo3TagNamesForget(!names.header);
        }
        return;
    }
    const uint64_t now = GetTickCount64();
    if (generation != names.generation)
    {
        Halo3TagNamesForget(false);
        names.generation = generation;
        names.nextAttemptMs = 0;
        names.failures = 0;
        names.ending = false;
        names.lastFault.clear();
    }
    else if (names.loaded)
    {
        if (now < names.nextStampMs)
            return;
        names.nextStampMs = now + 1000;
        uint8_t fixed[kHalo3TagNamesStampBytes];
        const bool read = Halo3TagNamesCopy(fixed, names.header, sizeof(fixed));
        if (read && Halo3TagNamesStamp(fixed) == names.stamp)
            return;
        Halo3TagNamesForget(false);
        names.nextAttemptMs = 0;
        names.failures = 0;
        names.lastFault.clear();
        // The game clears its header copy when a map is disposed of (+0xA6D60): the map is ending, which is not a
        // failure to report. Another map's header under the same generation is read like any new one.
        names.ending = !read || halo3_tag_names::ReadU32(fixed) != halo3_tag_names::kHeadMagic;
        LOG("H3 tag names: %s", names.ending
                                    ? "the loaded map's header was cleared (the map is ending); its names are withdrawn"
                                    : "the loaded map's header changed within one generation; its names are read again");
    }
    if (now < names.nextAttemptMs)
        return;
    const std::string fault = Halo3TagNamesLoad(generation);
    if (fault.empty())
    {
        names.loaded = true;
        names.ending = false;
        names.failures = 0;
        names.loadedMs = now;
        names.nextStampMs = now + 1000;
        return;
    }
    Halo3TagNamesForget(false);
    if (names.ending)
    {
        names.nextAttemptMs = now + 1000; // quietly, until the next map is in or the generation ends
        return;
    }
    names.failures = names.failures < 4 ? names.failures + 1 : 4;
    names.nextAttemptMs = now + (1000ull << names.failures); // 2, 4, 8, then every 16 s
    if (fault != names.lastFault)
    {
        LOG("H3 tag names: not read for generation %u (%s); vehicles are identified by their physics alone and one "
            "the mod does not know is named custom_<fingerprint>. Trying again",
            generation, fault.c_str());
        names.lastFault = fault;
    }
}

// The cold worker: the map's names have been published long enough for the camera thread to have resolved the seat
// with them (a few of its frames), so a seat's identity read now is the one the names gave.
bool Halo3TagNamesSettled()
{
    const Halo3TagNameState& names = g_halo3TagNames;
    return names.loaded && GetTickCount64() - names.loadedMs >= 250;
}

// The cold worker: tag `defIndex`'s path in the map being played; empty while its names are not read.
std::string_view Halo3TagNameOf(uint32_t defIndex)
{
    const Halo3TagNameState& names = g_halo3TagNames;
    if (!names.loaded || defIndex > 0xFFFFu ||
        names.generation != g_halo3RuntimeGeneration.load(std::memory_order_acquire))
        return {};
    const halo3_tag_names::NameTable table{names.offsets.data(), names.parsed.nameCount, names.buffer.data(),
                                           names.parsed.nameBufferSize};
    return halo3_tag_names::NameAt(table, defIndex);
}

// The cold worker: what that path says (Halo3PathVerdict); kHalo3PathNoName while the names are not read.
uint8_t Halo3TagPathVerdictOf(uint32_t defIndex)
{
    const std::string_view path = Halo3TagNameOf(defIndex);
    return path.empty() ? kHalo3PathNoName : Halo3PathVerdict(path.data(), path.size());
}
