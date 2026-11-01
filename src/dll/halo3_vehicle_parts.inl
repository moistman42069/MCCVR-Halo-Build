// HALO 3 VEHICLE PARTS: the regions, materials, nodes and markers of each vehicle the player sits in, by name, and
// the regions a vehicle camera's "hideRegions" leaves out of the player's own vehicle. Included by
// vehicle_cameras.inl (the cold worker's helpers are its), after the tag names and string ids it reads names with.
//
// THE RENDER MODEL (as halo3.dll's own code reads it): a
// vehicle's definition names its model (+0x40), the model its render model (+0x0C). In the render model, each
// block is { int32 count; uint32 address } with its elements at the tag data base + address * 4:
//   +0x0C regions, 16 bytes each: string id, then its permutations block (0x18 bytes each: string id first);
//   +0x30 nodes, 0x60 bytes each: string id first;
//   +0x3C marker groups, 0x10 bytes each: string id, then its markers block;
//   +0x48 materials, 0x24 bytes each: the render method's tag datum at +0xC.
// Checked offline against the stock maps and HugeGrass; read here on the cold worker only, under a fault guard.
//
// HIDING A REGION. A row of the region hook (halo3_avatar_backend.inl) is one object's draw: info+0x48 its object,
// info+4 its render model, info+0xE one mesh choice per region (-1 = not drawn). The cold worker works out which
// regions of the seat's vehicle a camera names and publishes them with the vehicle's object handle; the stereo
// pair's Begin takes them for that seat, and the hook writes -1 for those regions in that object's row only, for
// the submit that asked, as the seated body does. Other players' views, other vehicles and the simulation are
// untouched.
//
// CO-OP. Read-only toward the game but for that per-submit row write, which the seated body already makes.

inline constexpr uint32_t kHalo3PartsMaxRegions = 32;
// A draw's row has room for 16 regions' mesh choices (info+0xE..+0x2D), so only those can be left out.
inline constexpr uint32_t kHalo3PartsHideableRegions = halo3_avatar::RegionCapacity;
inline constexpr uint32_t kHalo3PartsMaxPermutations = 16;
inline constexpr uint32_t kHalo3PartsMaxNodes = 256;
inline constexpr uint32_t kHalo3PartsMaxMarkerGroups = 256;
inline constexpr uint32_t kHalo3PartsMaxMaterials = 256;
inline constexpr wchar_t kHalo3PartsFile[] = L"halomccvr_vehicle_parts.txt";

// What the region hook's pair takes for the seat (one writer, the cold worker).
struct Halo3VehicleHidePublished
{
    std::atomic<uint32_t> seq{0}; // even = stable
    std::atomic<uint32_t> any{0}; // a mask is published: the pair's whole cost while none is
    uint32_t generation = 0;
    uint32_t vehicle = UINT32_MAX;     // the seat's vehicle (its object handle)
    uint32_t renderModel = UINT32_MAX; // its render model's tag index (a row's info+4, low 16 bits)
    uint32_t regions = 0;              // its region count
    uint32_t mask = 0;                 // the regions not drawn
    // "hideParts": the materials whose parts are not drawn (bit i of materials[i / 64]); partsAny is the draw hook's
    // whole cost while none are.
    std::atomic<uint32_t> partsAny{0};
    uint64_t materials[4]{};
};
Halo3VehicleHidePublished g_halo3VehicleHide;

void Halo3VehicleHidePublish(uint32_t generation, uint32_t vehicle, uint32_t renderModel, uint32_t regions,
                             uint32_t mask, const uint64_t (&materials)[4])
{
    Halo3VehicleHidePublished& p = g_halo3VehicleHide;
    p.seq.fetch_add(1, std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_release);
    p.generation = generation;
    p.vehicle = vehicle;
    p.renderModel = renderModel;
    p.regions = regions;
    p.mask = mask;
    std::memcpy(p.materials, materials, sizeof(p.materials));
    p.any.store(mask ? 1u : 0u, std::memory_order_relaxed);
    p.partsAny.store((materials[0] | materials[1] | materials[2] | materials[3]) ? 1u : 0u,
                     std::memory_order_relaxed);
    p.seq.fetch_add(1, std::memory_order_release);
}
void Halo3VehicleHideWithdraw()
{
    const uint64_t none[4] = {};
    Halo3VehicleHidePublish(0, UINT32_MAX, UINT32_MAX, 0, 0, none);
}

// The draw hook (render thread, every part drawn while any is published): whether part material `material` of
// render model `model` on object `object` is one the seat's camera hides in `generation`.
bool Halo3VehicleHidesMaterial(uint32_t generation, uint32_t object, uint32_t model, uint32_t material)
{
    const Halo3VehicleHidePublished& p = g_halo3VehicleHide;
    if (!p.partsAny.load(std::memory_order_acquire) || material >= 256)
        return false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = p.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const uint32_t g = p.generation, v = p.vehicle, m = p.renderModel;
        const uint64_t word = p.materials[material / 64];
        std::atomic_thread_fence(std::memory_order_acquire);
        if (p.seq.load(std::memory_order_relaxed) != before)
            continue;
        return g == generation && v == object && m == model && ((word >> (material % 64)) & 1u) != 0;
    }
    return false;
}

// Whether the camera file asks any Halo 3 camera to hide a part by its material (one writer, the cold worker): the
// draw hook is installed only while it does (halo3_part_draw.inl).
std::atomic<bool> g_halo3VehiclePartsWanted{false};
bool Halo3VehicleCameraHidePartsWanted()
{
    return g_halo3VehiclePartsWanted.load(std::memory_order_acquire);
}

// "fadeParts": one faded material of the seat's vehicle -- its index in the render model, the postprocess entry its
// render method binds (the bind hook checks it is that one), and what to do to each of its parameters.
struct Halo3PartFade
{
    uint32_t material = UINT32_MAX;
    uintptr_t method = 0; // the render method's definition (what the shader select is handed)
    uintptr_t postprocess = 0;
    uint32_t parameters = 0;
    float kept = 1.0f;
    uint8_t op[halo3_glass_fade::kMaxParameters]{};
};
struct Halo3VehicleFadePublished
{
    std::atomic<uint32_t> seq{0}; // even = stable
    std::atomic<uint32_t> any{0}; // the draw hook's whole cost for fading while none is published
    uint32_t generation = 0;
    uint32_t vehicle = UINT32_MAX;
    uint32_t renderModel = UINT32_MAX;
    uint32_t count = 0;
    Halo3PartFade fades[vehicle_camera_file::kMaxFadeParts]{};
};
Halo3VehicleFadePublished g_halo3VehicleFade;

void Halo3VehicleFadePublish(uint32_t generation, uint32_t vehicle, uint32_t renderModel, const Halo3PartFade* fades,
                             uint32_t count)
{
    Halo3VehicleFadePublished& p = g_halo3VehicleFade;
    if (count > static_cast<uint32_t>(vehicle_camera_file::kMaxFadeParts))
        count = vehicle_camera_file::kMaxFadeParts;
    p.seq.fetch_add(1, std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_release);
    p.generation = generation;
    p.vehicle = vehicle;
    p.renderModel = renderModel;
    p.count = count;
    for (uint32_t i = 0; i < count; ++i)
        p.fades[i] = fades[i];
    p.any.store(count ? 1u : 0u, std::memory_order_relaxed);
    p.seq.fetch_add(1, std::memory_order_release);
}

// The draw hook (render thread): the fade of part material `material` of render model `model` on object `object` in
// `generation`, if the seat's camera fades it.
bool Halo3VehicleFadeFor(uint32_t generation, uint32_t object, uint32_t model, uint32_t material, Halo3PartFade& out)
{
    const Halo3VehicleFadePublished& p = g_halo3VehicleFade;
    if (!p.any.load(std::memory_order_acquire))
        return false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = p.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const uint32_t g = p.generation, v = p.vehicle, m = p.renderModel;
        uint32_t n = p.count;
        if (n > static_cast<uint32_t>(vehicle_camera_file::kMaxFadeParts))
            n = vehicle_camera_file::kMaxFadeParts;
        bool found = false;
        if (g == generation && v == object && m == model)
            for (uint32_t i = 0; i < n && !found; ++i)
                if (p.fades[i].material == material)
                {
                    out = p.fades[i];
                    found = true;
                }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (p.seq.load(std::memory_order_relaxed) != before)
            continue;
        return found;
    }
    return false;
}

// Whether the camera file asks any Halo 3 camera to fade a part (one writer, the cold worker): the bind hook is
// installed only while it does (halo3_part_draw.inl).
std::atomic<bool> g_halo3VehicleFadeWanted{false};
bool Halo3VehicleCameraFadePartsWanted()
{
    return g_halo3VehicleFadeWanted.load(std::memory_order_acquire);
}

// The stereo pair (render thread): the regions not drawn for the seat's vehicle `vehicle` in `generation`. False when
// none are, or the record was being written both times it was read.
bool Halo3VehicleHideRegions(uint32_t generation, uint32_t vehicle, uint32_t& renderModel, uint32_t& regions,
                             uint32_t& mask)
{
    const Halo3VehicleHidePublished& p = g_halo3VehicleHide;
    if (!p.any.load(std::memory_order_acquire))
        return false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = p.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const uint32_t g = p.generation, v = p.vehicle, m = p.renderModel, n = p.regions, k = p.mask;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (p.seq.load(std::memory_order_relaxed) != before)
            continue;
        if (!k || g != generation || v != vehicle)
            return false;
        renderModel = m;
        regions = n;
        mask = k;
        return true;
    }
    return false;
}

// Whether the camera file asks any Halo 3 camera to hide a region (one writer, the cold worker): the region hook is
// installed only while it does (halo3_avatar.inl).
std::atomic<bool> g_halo3VehicleHideWanted{false};
bool Halo3VehicleCameraHideRegionsWanted()
{
    return g_halo3VehicleHideWanted.load(std::memory_order_acquire);
}

// One render model as read: string ids and tag datums only, no text (that needs the cold worker's copies).
struct Halo3RenderModelRaw
{
    uint32_t renderModel = UINT32_MAX; // tag datum
    uint32_t checksum = 0;
    uint32_t regionCount = 0;
    uint32_t regionSid[kHalo3PartsMaxRegions]{};
    uint32_t permutationCount[kHalo3PartsMaxRegions]{};
    uint32_t permutationSid[kHalo3PartsMaxRegions][kHalo3PartsMaxPermutations]{};
    uint32_t nodeCount = 0;
    uint32_t nodeSid[kHalo3PartsMaxNodes]{};
    uint32_t markerGroupCount = 0;
    uint32_t markerGroupSid[kHalo3PartsMaxMarkerGroups]{};
    uint32_t markerCount[kHalo3PartsMaxMarkerGroups]{};
    uint32_t materialCount = 0;
    uint32_t materialMethod[kHalo3PartsMaxMaterials]{}; // the render method's tag datum
};

namespace
{
uint32_t Halo3PartsU32(const uint8_t* at)
{
    uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
// A block's elements: count at `def + at`, address after it. Null with count 0 when it has none.
const uint8_t* Halo3PartsBlock(const uint8_t* tagBase, const uint8_t* def, size_t at, uint32_t& count)
{
    count = Halo3PartsU32(def + at);
    const uint32_t address = Halo3PartsU32(def + at + 4);
    if (!count || !address)
    {
        count = 0;
        return nullptr;
    }
    return tagBase + static_cast<size_t>(address) * 4;
}
// Guarded: plain data only, so the SEH frame holds no C++ object. False on a fault or a model it cannot read.
bool Halo3ReadRenderModelRaw(uint32_t vehicleDefIndex, Halo3RenderModelRaw& out)
{
    __try
    {
        const uint8_t* tagBase = g_halo3TagDataBase ? static_cast<const uint8_t*>(*g_halo3TagDataBase) : nullptr;
        const uint8_t* vehicle = Halo3LoadedTagDefinition(vehicleDefIndex);
        if (!tagBase || !vehicle)
            return false;
        const uint8_t* model = Halo3LoadedTagDefinition(Halo3PartsU32(vehicle + kHalo3VehicleModelDatumOffset));
        if (!model)
            return false;
        out.renderModel = Halo3PartsU32(model + kHalo3ModelRenderModelDatumOffset);
        const uint8_t* render = Halo3LoadedTagDefinition(out.renderModel);
        if (!render)
            return false;
        out.checksum = Halo3PartsU32(render + 8);
        uint32_t count = 0;
        const uint8_t* regions = Halo3PartsBlock(tagBase, render, 0x0C, count);
        if (count > kHalo3PartsMaxRegions)
            return false;
        out.regionCount = count;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t* region = regions + static_cast<size_t>(i) * 16;
            out.regionSid[i] = Halo3PartsU32(region);
            uint32_t perms = 0;
            const uint8_t* permutations = Halo3PartsBlock(tagBase, region, 4, perms);
            out.permutationCount[i] = perms;
            for (uint32_t k = 0; k < perms && k < kHalo3PartsMaxPermutations; ++k)
                out.permutationSid[i][k] = Halo3PartsU32(permutations + static_cast<size_t>(k) * 0x18);
        }
        const uint8_t* nodes = Halo3PartsBlock(tagBase, render, 0x30, count);
        out.nodeCount = count < kHalo3PartsMaxNodes ? count : kHalo3PartsMaxNodes;
        for (uint32_t i = 0; i < out.nodeCount; ++i)
            out.nodeSid[i] = Halo3PartsU32(nodes + static_cast<size_t>(i) * 0x60);
        const uint8_t* groups = Halo3PartsBlock(tagBase, render, 0x3C, count);
        out.markerGroupCount = count < kHalo3PartsMaxMarkerGroups ? count : kHalo3PartsMaxMarkerGroups;
        for (uint32_t i = 0; i < out.markerGroupCount; ++i)
        {
            out.markerGroupSid[i] = Halo3PartsU32(groups + static_cast<size_t>(i) * 0x10);
            out.markerCount[i] = Halo3PartsU32(groups + static_cast<size_t>(i) * 0x10 + 4);
        }
        const uint8_t* materials = Halo3PartsBlock(tagBase, render, 0x48, count);
        out.materialCount = count < kHalo3PartsMaxMaterials ? count : kHalo3PartsMaxMaterials;
        for (uint32_t i = 0; i < out.materialCount; ++i)
            out.materialMethod[i] = Halo3PartsU32(materials + static_cast<size_t>(i) * 0x24 + 0xC);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// One material's render method as read (guarded, plain data): its first postprocess entry, blend mode and the string
// ids of its template's real parameters.
struct Halo3MaterialRaw
{
    uintptr_t method = 0;
    uintptr_t postprocess = 0;
    int32_t blend = -1;
    uint32_t parameters = 0;
    uint32_t nameSid[halo3_glass_fade::kMaxParameters]{};
};
bool Halo3ReadMaterialRaw(uint32_t methodDatum, Halo3MaterialRaw& out)
{
    __try
    {
        const uint8_t* tagBase = g_halo3TagDataBase ? static_cast<const uint8_t*>(*g_halo3TagDataBase) : nullptr;
        const uint8_t* method = Halo3LoadedTagDefinition(methodDatum);
        if (!tagBase || !method)
            return false;
        uint32_t count = 0;
        const uint8_t* postprocess = Halo3PartsBlock(tagBase, method, 0x28, count);
        if (!postprocess)
            return false;
        out.method = reinterpret_cast<uintptr_t>(method);
        out.postprocess = reinterpret_cast<uintptr_t>(postprocess);
        std::memcpy(&out.blend, postprocess + 0x68, 4);
        out.parameters = Halo3PartsU32(postprocess + 0x1C);
        const uint8_t* templ = Halo3LoadedTagDefinition(Halo3PartsU32(postprocess + 0x0C));
        if (!templ)
            return false;
        uint32_t names = 0;
        const uint8_t* sids = Halo3PartsBlock(tagBase, templ, 0x48, names);
        if (names != out.parameters || names > static_cast<uint32_t>(halo3_glass_fade::kMaxParameters))
            return false;
        for (uint32_t i = 0; i < names; ++i)
            out.nameSid[i] = Halo3PartsU32(sids + static_cast<size_t>(i) * 4);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

std::string Halo3PartsSidText(uint32_t sid)
{
    const std::string_view text = Halo3StringIdText(sid);
    if (!text.empty())
        return std::string(text);
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), sid ? "sid:%X" : "-", sid);
    return buffer;
}

bool Halo3PartsSameName(std::string_view a, std::string_view b)
{
    return halo3_tag_names::EqualNoCase(a, b);
}
} // namespace

// The vehicle's parts as the parts file lists them.
std::string Halo3VehiclePartsBlock(const std::string& vehiclePath, const Halo3RenderModelRaw& raw)
{
    std::string out = "[" + vehiclePath + "]\r\n";
    char line[256];
    const std::string_view renderPath = Halo3TagNameOf(raw.renderModel & 0xFFFFu);
    std::snprintf(line, sizeof(line), "render model: %s (checksum %08X, %u nodes)\r\n",
                  renderPath.empty() ? "?" : std::string(renderPath).c_str(), raw.checksum, raw.nodeCount);
    out += line;
    out += "regions (a camera's hideRegions names these, or their numbers):\r\n";
    for (uint32_t i = 0; i < raw.regionCount; ++i)
    {
        std::snprintf(line, sizeof(line), "  %u %s", i, Halo3PartsSidText(raw.regionSid[i]).c_str());
        out += line;
        const uint32_t perms = raw.permutationCount[i];
        if (perms)
        {
            out += " (permutations:";
            for (uint32_t k = 0; k < perms && k < kHalo3PartsMaxPermutations; ++k)
                out += (k ? ", " : " ") + Halo3PartsSidText(raw.permutationSid[i][k]);
            if (perms > kHalo3PartsMaxPermutations)
                out += ", ...";
            out += ")";
        }
        out += "\r\n";
    }
    out += "materials (a camera's hideParts and fadeParts name these):\r\n";
    for (uint32_t i = 0; i < raw.materialCount; ++i)
    {
        const std::string_view method = Halo3TagNameOf(raw.materialMethod[i] & 0xFFFFu);
        std::snprintf(line, sizeof(line), "  %u %s\r\n", i, method.empty() ? "?" : std::string(method).c_str());
        out += line;
    }
    out += "nodes:";
    for (uint32_t i = 0; i < raw.nodeCount; ++i)
        out += (i ? ", " : " ") + Halo3PartsSidText(raw.nodeSid[i]);
    out += "\r\nmarkers:";
    for (uint32_t i = 0; i < raw.markerGroupCount; ++i)
    {
        out += (i ? ", " : " ") + Halo3PartsSidText(raw.markerGroupSid[i]);
        if (raw.markerCount[i] > 1)
            out += " (" + std::to_string(raw.markerCount[i]) + ")";
    }
    out += "\r\n\r\n";
    return out;
}

// The parts file with `block` in it for `vehiclePath`: the file's other vehicles kept in their order, this one's
// block replaced where it was or added at the end.
std::string Halo3VehiclePartsMerge(const std::string& existing, const std::string& vehiclePath,
                                   const std::string& block)
{
    static const char kHead[] =
        "# Halo 3 vehicle parts, written by the mod each time you sit in a vehicle it has not listed yet (or whose\r\n"
        "# parts changed). A vehicle camera in halomccvr_vehicle_cameras.json names regions from here in its\r\n"
        "# \"hideRegions\", and materials (the last part of a path is enough) in its \"hideParts\" and \"fadeParts\".\r\n"
        "# Edits here are not read back; delete the file to start it over.\r\n\r\n";
    std::string out = kHead;
    bool placed = false;
    const std::string wanted = "[" + vehiclePath + "]";
    size_t at = existing.find("\n[");
    if (!existing.empty() && existing[0] == '[')
        at = 0;
    else if (at != std::string::npos)
        ++at;
    while (at != std::string::npos && at < existing.size())
    {
        size_t next = existing.find("\n[", at + 1);
        const std::string piece = existing.substr(at, next == std::string::npos ? std::string::npos : next + 1 - at);
        const size_t eol = piece.find_first_of("\r\n");
        if (piece.substr(0, eol) == wanted)
        {
            out += block;
            placed = true;
        }
        else
        {
            out += piece;
            if (piece.size() < 2 || piece.compare(piece.size() - 2, 2, "\n\n") != 0)
                if (piece.size() < 4 || piece.compare(piece.size() - 4, 4, "\r\n\r\n") != 0)
                    out += "\r\n";
        }
        at = next == std::string::npos ? std::string::npos : next + 1;
    }
    if (!placed)
        out += block;
    return out;
}

// The regions of `raw` that `names` (a camera's "hideRegions", joined by commas) asks to hide; `missing` gets the
// names the model has no region for.
uint32_t Halo3VehicleHideMask(const Halo3RenderModelRaw& raw, const char* names, std::string& missing)
{
    uint32_t mask = 0;
    std::string_view rest(names);
    while (!rest.empty())
    {
        const size_t comma = rest.find(',');
        const std::string_view name = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        if (name.empty())
            continue;
        bool found = false;
        const bool number = std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (number)
        {
            const uint32_t index = static_cast<uint32_t>(std::strtoul(std::string(name).c_str(), nullptr, 10));
            if (index < raw.regionCount && index < kHalo3PartsHideableRegions)
            {
                mask |= 1u << index;
                found = true;
            }
        }
        else
        {
            for (uint32_t i = 0; i < raw.regionCount && i < kHalo3PartsHideableRegions; ++i)
                if (Halo3PartsSameName(Halo3StringIdText(raw.regionSid[i]), name))
                {
                    mask |= 1u << i;
                    found = true;
                }
        }
        if (!found)
            missing += (missing.empty() ? "" : ", ") + std::string(name);
    }
    return mask;
}

// The materials of `raw` that `names` (a camera's "hideParts", joined by commas) asks to hide, by a material's
// render method path or its last part; `missing` gets the names no material has.
void Halo3VehicleHideMaterials(const Halo3RenderModelRaw& raw, const char* names, uint64_t (&materials)[4],
                               std::string& missing)
{
    std::string_view rest(names);
    while (!rest.empty())
    {
        const size_t comma = rest.find(',');
        const std::string_view name = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        if (name.empty())
            continue;
        // Exact matches, else a variant's (halo3_tag_names::MatchPartName).
        uint64_t exact[4]{}, variant[4]{};
        bool anyExact = false, anyVariant = false;
        for (uint32_t i = 0; i < raw.materialCount && i < 256; ++i)
        {
            const auto match =
                halo3_tag_names::MatchPartName(Halo3TagNameOf(raw.materialMethod[i] & 0xFFFFu), name);
            if (match == halo3_tag_names::PartNameMatch::Exact)
            {
                exact[i / 64] |= 1ull << (i % 64);
                anyExact = true;
            }
            else if (match == halo3_tag_names::PartNameMatch::Variant)
            {
                variant[i / 64] |= 1ull << (i % 64);
                anyVariant = true;
            }
        }
        const uint64_t* picked = anyExact ? exact : variant;
        for (int k = 0; k < 4; ++k)
            materials[k] |= picked[k];
        const bool found = anyExact || anyVariant;
        if (!found)
            missing += (missing.empty() ? "" : ", ") + std::string(name);
    }
}

// The cold worker's side: what it last did, so a pass that changes nothing costs a few comparisons.
struct Halo3VehiclePartsState
{
    uint32_t generation = 0;
    uint32_t definitionIndex = UINT32_MAX;
    uint32_t vehicle = UINT32_MAX;
    std::string hide;      // the names last resolved
    std::string hideParts; // likewise, "hideParts"
    std::string fadeParts; // likewise, "fadeParts" with its amounts
    bool fadePublished = false;
    bool namesRead = false;
    bool published = false;
    std::vector<std::string> written; // "generation:definition" listed in the parts file this session
    uint64_t nextAttemptMs = 0;
};
Halo3VehiclePartsState g_halo3VehicleParts;

// Every cold pass: the parts file for the seat's vehicle, and the regions its camera hides.
void Halo3VehicleParts_ColdTick(const vehicle_camera_select::ActiveCamera& active, const VehicleCameraSeatKey& seat,
                                bool seated, uint32_t definitionIndex, uint64_t nowMs)
{
    Halo3VehiclePartsState& s = g_halo3VehicleParts;
    const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
    const bool namesRead = g_halo3StringIds.loaded && g_halo3TagNames.loaded;
    const std::string hide = seated && active.valid ? active.hideRegions : "";
    const std::string hideParts = seated && active.valid ? active.hideParts : "";
    std::string fadeParts;
    if (seated && active.valid && active.fadeParts[0])
    {
        fadeParts = active.fadeParts;
        for (const float kept : active.fadeKept)
            fadeParts += "|" + std::to_string(kept);
    }
    const uint32_t vehicle = seated ? static_cast<uint32_t>(seat.vehicle) : UINT32_MAX;
    const bool changed = generation != s.generation || definitionIndex != s.definitionIndex ||
                         vehicle != s.vehicle || hide != s.hide || hideParts != s.hideParts ||
                         fadeParts != s.fadeParts ||
                         namesRead != s.namesRead;
    if (!changed && nowMs < s.nextAttemptMs)
        return;
    if (!changed && s.nextAttemptMs == 0)
        return;
    s.generation = generation;
    s.definitionIndex = definitionIndex;
    s.vehicle = vehicle;
    s.hide = hide;
    s.hideParts = hideParts;
    s.fadeParts = fadeParts;
    s.namesRead = namesRead;
    s.nextAttemptMs = 0;
    if (!seated || definitionIndex == UINT32_MAX || !generation)
    {
        if (s.published)
        {
            Halo3VehicleHideWithdraw();
            s.published = false;
        }
        if (s.fadePublished)
        {
            Halo3VehicleFadePublish(0, UINT32_MAX, UINT32_MAX, nullptr, 0);
            s.fadePublished = false;
        }
        return;
    }
    auto raw = std::make_unique<Halo3RenderModelRaw>();
    if (!Halo3ReadRenderModelRaw(definitionIndex, *raw))
    {
        if (s.published)
        {
            Halo3VehicleHideWithdraw();
            s.published = false;
        }
        if (s.fadePublished)
        {
            Halo3VehicleFadePublish(0, UINT32_MAX, UINT32_MAX, nullptr, 0);
            s.fadePublished = false;
        }
        s.nextAttemptMs = nowMs + 2000; // a model mid-load; once more shortly
        return;
    }
    const std::string_view path = Halo3TagNameOf(definitionIndex & 0xFFFFu);
    // THE PARTS FILE: each vehicle once a session per map, once its names can be read.
    const std::string key = std::to_string(generation) + ":" + std::to_string(definitionIndex & 0xFFFFu);
    if (namesRead && !path.empty() &&
        std::find(s.written.begin(), s.written.end(), key) == s.written.end())
    {
        s.written.push_back(key);
        const std::wstring dir = LogDirectory();
        if (!dir.empty())
        {
            const std::wstring file = dir + kHalo3PartsFile;
            std::string existing;
            (void)VehicleCameraReadText(file, existing);
            const std::string block = Halo3VehiclePartsBlock(std::string(path), *raw);
            const std::string merged = Halo3VehiclePartsMerge(existing, std::string(path), block);
            if (merged != existing)
            {
                if (VehicleCameraWriteText(file, merged))
                    LOG("Vehicle parts: %s listed in halomccvr_vehicle_parts.txt (%u regions, %u materials, %u "
                        "nodes, %u marker names)",
                        std::string(path).c_str(), raw->regionCount, raw->materialCount, raw->nodeCount,
                        raw->markerGroupCount);
                else
                    LOG("Vehicle parts: halomccvr_vehicle_parts.txt could not be written (%lu)", GetLastError());
            }
        }
    }
    // THE REGIONS ITS CAMERA HIDES.
    uint32_t mask = 0;
    if (!hide.empty())
    {
        std::string missing;
        if (!namesRead)
        {
            // Numbers need no names; names wait for them.
            mask = Halo3VehicleHideMask(*raw, hide.c_str(), missing);
            if (!missing.empty())
                s.nextAttemptMs = nowMs + 1000;
        }
        else
        {
            mask = Halo3VehicleHideMask(*raw, hide.c_str(), missing);
            std::string regions;
            for (uint32_t i = 0; i < raw->regionCount; ++i)
                regions += (i ? ", " : "") + std::to_string(i) + " " + Halo3PartsSidText(raw->regionSid[i]);
            if (!missing.empty())
                LOG("Vehicle parts: %s has no region %s (its regions: %s); the rest of the camera's hideRegions "
                    "applies",
                    path.empty() ? "this vehicle" : std::string(path).c_str(), missing.c_str(), regions.c_str());
            else
                LOG("Vehicle parts: %s drawn without region(s) %s in this camera (mask %08X of %u regions)",
                    path.empty() ? "this vehicle" : std::string(path).c_str(), hide.c_str(), mask, raw->regionCount);
        }
    }
    // THE PARTS ITS CAMERA HIDES, by material: names need the tag names.
    uint64_t materials[4] = {};
    if (!hideParts.empty())
    {
        std::string missing;
        if (!g_halo3TagNames.loaded)
            s.nextAttemptMs = nowMs + 1000;
        else
        {
            Halo3VehicleHideMaterials(*raw, hideParts.c_str(), materials, missing);
            std::string list;
            for (uint32_t i = 0; i < raw->materialCount; ++i)
            {
                const std::string_view method = Halo3TagNameOf(raw->materialMethod[i] & 0xFFFFu);
                list += (i ? ", " : "") + std::string(method.empty() ? "?" : halo3_tag_names::ShortName(method));
            }
            if (!missing.empty())
                LOG("Vehicle parts: %s has no material %s (its materials: %s); the rest of the camera's hideParts "
                    "applies",
                    path.empty() ? "this vehicle" : std::string(path).c_str(), missing.c_str(), list.c_str());
            else
                LOG("Vehicle parts: %s drawn without its parts of material(s) %s in this camera",
                    path.empty() ? "this vehicle" : std::string(path).c_str(), hideParts.c_str());
        }
    }
    Halo3VehicleHidePublish(generation, vehicle, raw->renderModel & 0xFFFFu, raw->regionCount, mask, materials);
    s.published = mask != 0 || (materials[0] | materials[1] | materials[2] | materials[3]) != 0;

    // THE GLASS ITS CAMERA FADES: each named material's render method, its blend mode and its parameters by name
    // (halo3_glass_fade.h). Names need the tag names and the string ids.
    Halo3PartFade fades[vehicle_camera_file::kMaxFadeParts]{};
    uint32_t fadeCount = 0;
    if (seated && active.valid && active.fadeParts[0])
    {
        if (!namesRead)
            s.nextAttemptMs = nowMs + 1000;
        else
        {
            std::string_view rest(active.fadeParts);
            int index = 0;
            std::string report, missing;
            while (!rest.empty() && index < vehicle_camera_file::kMaxFadeParts)
            {
                const size_t comma = rest.find(',');
                const std::string_view name = rest.substr(0, comma);
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
                const float kept = active.fadeKept[index++];
                // Exact matches, else a variant's (halo3_tag_names::MatchPartName).
                bool anyExact = false;
                for (uint32_t i = 0; i < raw->materialCount && i < 256 && !anyExact; ++i)
                    anyExact = halo3_tag_names::MatchPartName(Halo3TagNameOf(raw->materialMethod[i] & 0xFFFFu),
                                                              name) == halo3_tag_names::PartNameMatch::Exact;
                const auto wanted =
                    anyExact ? halo3_tag_names::PartNameMatch::Exact : halo3_tag_names::PartNameMatch::Variant;
                bool found = false;
                for (uint32_t i = 0; i < raw->materialCount && i < 256; ++i)
                {
                    const std::string_view method = Halo3TagNameOf(raw->materialMethod[i] & 0xFFFFu);
                    if (halo3_tag_names::MatchPartName(method, name) != wanted)
                        continue;
                    found = true;
                    Halo3MaterialRaw material;
                    if (!Halo3ReadMaterialRaw(raw->materialMethod[i], material))
                    {
                        report += std::string(report.empty() ? "" : "; ") + std::string(name) +
                                  ": its render method could not be read";
                        continue;
                    }
                    Halo3PartFade fade;
                    fade.material = i;
                    fade.method = material.method;
                    fade.postprocess = material.postprocess;
                    fade.parameters = material.parameters;
                    fade.kept = kept;
                    std::string scaled;
                    for (uint32_t k = 0; k < material.parameters; ++k)
                    {
                        const std::string_view parameter = Halo3StringIdText(material.nameSid[k]);
                        fade.op[k] = static_cast<uint8_t>(halo3_glass_fade::OpFor(material.blend, parameter));
                        if (fade.op[k])
                            scaled += (scaled.empty() ? "" : ", ") + std::string(parameter);
                    }
                    char line[224];
                    const std::string as = anyExact ? std::string()
                                                    : " as " + std::string(halo3_tag_names::ShortName(method));
                    std::snprintf(line, sizeof(line), "%s%s (%s) kept %.2f", std::string(name).c_str(), as.c_str(),
                                  halo3_glass_fade::BlendName(material.blend), static_cast<double>(kept));
                    report += std::string(report.empty() ? "" : "; ") + line +
                              (scaled.empty() ? ": nothing in it can fade (hide it instead)" : " through " + scaled);
                    if (!scaled.empty() && fadeCount < static_cast<uint32_t>(vehicle_camera_file::kMaxFadeParts))
                        fades[fadeCount++] = fade;
                }
                if (!found)
                    missing += (missing.empty() ? "" : ", ") + std::string(name);
            }
            if (!missing.empty())
                LOG("Vehicle parts: %s has no material %s to fade", path.empty() ? "this vehicle"
                                                                           : std::string(path).c_str(),
                    missing.c_str());
            if (!report.empty())
                LOG("Vehicle parts: %s glass faded in this camera: %s",
                    path.empty() ? "this vehicle" : std::string(path).c_str(), report.c_str());
        }
    }
    if (fadeCount || s.fadePublished)
        Halo3VehicleFadePublish(generation, vehicle, raw->renderModel & 0xFFFFu, fades, fadeCount);
    s.fadePublished = fadeCount != 0;
}
