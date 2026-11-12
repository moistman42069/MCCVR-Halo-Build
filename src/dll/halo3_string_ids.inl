// HALO 3 STRING IDS AT RUNTIME: the text of the string ids the loaded map's tags use ("canopy", "r_hand"), so a
// vehicle camera can name the parts of its vehicle it hides and the parts file can list them. The layout and the
// proof are halo3_string_ids.h's. Included by game.cpp after the tag names (halo3_tag_names.inl), whose life it
// follows: bound at the Halo 3 install, read once per map by the cold worker, withdrawn at removal.
//
// WHERE THE STRINGS COME FROM. halo3.dll keeps the loaded map's string ids in a static buffer of its own module; the
// install decodes the buffer, the count beside it, the module's global table and the header copy from three unique
// signatures, and copies the table's ids once. The cold worker then copies the buffer once per map. A copy is used
// only once it proves itself: the header is a Halo 3 one, the game's own count is the header's, the buffer holds
// exactly that many strings, and three of its globals read as the module table's names for them.
//
// THREADS. The cold worker alone installs, removes, copies and reads (Halo3StringIdText). Nothing is published; the
// render thread never sees a string (it is given region masks, worked out here).
//
// CO-OP. Read-only: nothing is written to the game.

struct Halo3StringIdState
{
    const uint8_t* header = nullptr;
    const uint32_t* countSlot = nullptr;
    const char* buffer = nullptr;
    std::vector<uint64_t> globalIds;
    std::vector<const char*> globalNames; // into halo3.dll; read under a guard
    uint32_t generation = 0;
    bool loaded = false;
    bool ending = false;
    uint64_t stamp = 0;
    uint64_t nextAttemptMs = 0;
    uint64_t nextStampMs = 0;
    uint32_t failures = 0;
    std::string lastFault;
    std::vector<char> copy;
    halo3_string_ids::Strings strings;
};
Halo3StringIdState g_halo3StringIds;

namespace
{
bool Halo3StringIdsCopy(void* out, const void* from, size_t size)
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
// The module table's name for global number `n`, NUL-terminated within `room` (guarded).
bool Halo3StringIdsGlobalName(const char* name, char* out, size_t room)
{
    __try
    {
        size_t i = 0;
        for (; i + 1 < room && name[i]; ++i)
            out[i] = name[i];
        out[i] = '\0';
        return !name[i];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

void Halo3StringIdsForget()
{
    Halo3StringIdState& ids = g_halo3StringIds;
    ids.loaded = false;
    ids.strings.Clear();
    std::vector<char>().swap(ids.copy);
}

bool InstallHalo3StringIds(uintptr_t base, size_t size, uint32_t generation)
{
    namespace si = halo3_string_ids;
    Halo3StringIdState& ids = g_halo3StringIds;
    ids.header = nullptr;
    ids.countSlot = nullptr;
    ids.buffer = nullptr;
    ids.globalIds.clear();
    ids.globalNames.clear();
    const uintptr_t loader = mcc_clock::FindUnique(base, size, si::kLoaderPattern);
    const uintptr_t builder = mcc_clock::FindUnique(base, size, si::kBuilderPattern);
    const uintptr_t call = mcc_clock::FindUnique(base, size, si::kCallPattern);
    uint8_t loaderBytes[si::kLoaderBytes] = {}, builderBytes[si::kBuilderBytes] = {}, callBytes[si::kCallBytes] = {};
    if (loader)
        sig::CopyUnhooked(loader, loaderBytes, sizeof(loaderBytes));
    if (builder)
        sig::CopyUnhooked(builder, builderBytes, sizeof(builderBytes));
    if (call)
        sig::CopyUnhooked(call, callBytes, sizeof(callBytes));
    si::Sites sites;
    si::Bind why = si::DecodeSites(loader ? loaderBytes : nullptr, static_cast<int64_t>(loader - base),
                                   builder ? builderBytes : nullptr, static_cast<int64_t>(builder - base),
                                   call ? callBytes : nullptr, static_cast<int64_t>(call - base),
                                   static_cast<int64_t>(size), sites);
    std::vector<uint8_t> table;
    if (why == si::Bind::Bound)
    {
        table.resize(static_cast<size_t>(sites.globals) * 16);
        if (!Halo3StringIdsCopy(table.data(), reinterpret_cast<const void*>(base + sites.tableRva), table.size()))
            why = si::Bind::Implausible;
    }
    if (why == si::Bind::Bound)
    {
        ids.globalIds.resize(sites.globals);
        ids.globalNames.resize(sites.globals);
        for (uint32_t n = 0; n < sites.globals; ++n)
        {
            ids.globalIds[n] = si::ReadU64(table.data() + static_cast<size_t>(n) * 16);
            const uint64_t name = si::ReadU64(table.data() + static_cast<size_t>(n) * 16 + 8);
            if (name < base || name >= base + size)
            {
                why = si::Bind::Implausible;
                break;
            }
            ids.globalNames[n] = reinterpret_cast<const char*>(name);
        }
    }
    if (why != si::Bind::Bound)
    {
        ids.globalIds.clear();
        ids.globalNames.clear();
        LOG("H3 string ids: not bound (%s); a vehicle camera's hideRegions is not applied and the parts file names no "
            "parts",
            si::BindName(why));
        return false;
    }
    ids.header = reinterpret_cast<const uint8_t*>(base + static_cast<uintptr_t>(sites.headerRva));
    ids.countSlot = reinterpret_cast<const uint32_t*>(base + static_cast<uintptr_t>(sites.countRva));
    ids.buffer = reinterpret_cast<const char*>(base + static_cast<uintptr_t>(sites.bufferRva));
    LOG("H3 string ids: bound (generation %u): the string buffer +0x%llX and its count +0x%llX from the string "
        "loader +0x%llX, the module's %u global ids +0x%llX from the hash builder +0x%llX, the header copy +0x%llX",
        generation, static_cast<unsigned long long>(sites.bufferRva), static_cast<unsigned long long>(sites.countRva),
        static_cast<unsigned long long>(loader - base), sites.globals,
        static_cast<unsigned long long>(sites.tableRva), static_cast<unsigned long long>(builder - base),
        static_cast<unsigned long long>(sites.headerRva));
    return true;
}

void RemoveHalo3StringIds()
{
    Halo3StringIdState& ids = g_halo3StringIds;
    ids.header = nullptr;
    ids.countSlot = nullptr;
    ids.buffer = nullptr;
    ids.generation = 0;
    std::vector<uint64_t>().swap(ids.globalIds);
    std::vector<const char*>().swap(ids.globalNames);
    Halo3StringIdsForget();
}

// Copies the loaded map's strings and proves them. Empty = done; else why not.
std::string Halo3StringIdsLoad()
{
    namespace si = halo3_string_ids;
    namespace tn = halo3_tag_names;
    Halo3StringIdState& ids = g_halo3StringIds;
    uint8_t head[0x40];
    if (!Halo3StringIdsCopy(head, ids.header, sizeof(head)))
        return "the loaded header could not be read";
    if (si::ReadU32(head) != tn::kHeadMagic || si::ReadI32(head + 4) != tn::kVersion ||
        head[tn::kEngineOffset] != tn::kEngineHalo3)
        return "the loaded header is not a Halo 3 map's";
    const uint32_t count = si::ReadU32(head + si::kCountOffset);
    const uint32_t size = si::ReadU32(head + si::kBufferSizeOffset);
    uint32_t loaded = 0;
    if (!Halo3StringIdsCopy(&loaded, ids.countSlot, sizeof(loaded)) || loaded != count)
        return "the game has not read this header's strings (its count is not the header's)";
    if (!size || size > si::kMaxBufferCopy)
        return "the strings take more room than the mod reads";
    ids.copy.resize(size);
    if (!Halo3StringIdsCopy(ids.copy.data(), ids.buffer, size))
        return "the game's string buffer could not be read";
    si::Fault fault = ids.strings.Build(ids.globalIds.data(), static_cast<uint32_t>(ids.globalIds.size()),
                                        ids.copy.data(), size, count);
    // Three globals, read in the buffer and in the module's table, must agree: the first named one, the middle, the
    // last.
    if (fault == si::Fault::None)
    {
        const uint32_t globals = ids.strings.GlobalCount();
        const uint32_t probes[] = {1u, globals / 2, globals - 1};
        for (const uint32_t n : probes)
        {
            char name[96];
            if (n >= globals || !Halo3StringIdsGlobalName(ids.globalNames[n], name, sizeof(name)) ||
                ids.strings.At(n) != std::string_view(name))
            {
                fault = si::Fault::Disagrees;
                ids.strings.Clear();
                break;
            }
        }
    }
    if (fault != si::Fault::None)
        return si::FaultName(fault);
    uint8_t fixed[kHalo3TagNamesStampBytes];
    ids.stamp = Halo3TagNamesCopy(fixed, ids.header, sizeof(fixed)) ? Halo3TagNamesStamp(fixed) : 0;
    LOG("H3 string ids: %u read (%u of them the map's own, from id 0x%X), %u KB copied from the game's own buffer",
        ids.strings.Count(), ids.strings.Count() - ids.strings.GlobalCount(), ids.strings.FirstMapIndex(),
        static_cast<unsigned>((size + 1023) / 1024));
    return {};
}

// The cold worker, every pass: once per map, and again when the map changes under one generation.
void Halo3StringIds_ColdTick()
{
    Halo3StringIdState& ids = g_halo3StringIds;
    const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
    if (!ids.header || !generation)
    {
        if (ids.generation || ids.loaded)
        {
            ids.generation = 0;
            Halo3StringIdsForget();
        }
        return;
    }
    const uint64_t now = GetTickCount64();
    if (generation != ids.generation)
    {
        Halo3StringIdsForget();
        ids.generation = generation;
        ids.nextAttemptMs = 0;
        ids.failures = 0;
        ids.ending = false;
        ids.lastFault.clear();
    }
    else if (ids.loaded)
    {
        if (now < ids.nextStampMs)
            return;
        ids.nextStampMs = now + 1000;
        uint8_t fixed[kHalo3TagNamesStampBytes];
        const bool read = Halo3TagNamesCopy(fixed, ids.header, sizeof(fixed));
        if (read && Halo3TagNamesStamp(fixed) == ids.stamp)
            return;
        Halo3StringIdsForget();
        ids.nextAttemptMs = 0;
        ids.failures = 0;
        ids.lastFault.clear();
        ids.ending = !read || halo3_string_ids::ReadU32(fixed) != halo3_tag_names::kHeadMagic;
    }
    if (now < ids.nextAttemptMs)
        return;
    const std::string fault = Halo3StringIdsLoad();
    if (fault.empty())
    {
        ids.loaded = true;
        ids.ending = false;
        ids.failures = 0;
        ids.nextStampMs = now + 1000;
        return;
    }
    Halo3StringIdsForget();
    if (ids.ending)
    {
        ids.nextAttemptMs = now + 1000;
        return;
    }
    ids.failures = ids.failures < 4 ? ids.failures + 1 : 4;
    ids.nextAttemptMs = now + (1000ull << ids.failures);
    if (fault != ids.lastFault)
    {
        LOG("H3 string ids: not read for generation %u (%s); trying again", generation, fault.c_str());
        ids.lastFault = fault;
    }
}

// The cold worker: string id `id`'s text in the map being played; empty while the strings are not read.
std::string_view Halo3StringIdText(uint32_t id)
{
    const Halo3StringIdState& ids = g_halo3StringIds;
    if (!ids.loaded || ids.generation != g_halo3RuntimeGeneration.load(std::memory_order_acquire))
        return {};
    return ids.strings.Text(id);
}
