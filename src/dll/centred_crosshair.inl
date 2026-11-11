// ALWAYS THE CENTRED CROSSHAIR in every title (centred_crosshair_logic.h). Run at each title's install, on its own: the
// patch is a few bytes of the game's settings fills (CE and Halo 2: its centred check), not a hook, and selects the
// game's own Centered arm. A title is patched whole or not at all; anything not proven leaves it on the player's setting (the
// Lowered fallbacks stay: seat_drive::SteadyPitchOffset, the capture window that follows the art, Halo 4's Aim Frame
// Zoom recentre wait). Project Reclaimer 0.10 runs a recompiled Halo 3 whose code this does not reach; the launcher
// turns on its own Centered crosshair instead.
void InstallCentredCrosshair(GameTitle title, uintptr_t base, size_t size, uint32_t generation)
{
    using namespace centred_crosshair;
    const Title* t = nullptr;
    size_t slot = 0;
    switch (title)
    {
    case GameTitle::Halo3: t = &kHalo3, slot = 0; break;
    case GameTitle::Halo3ODST: t = &kOdst, slot = 1; break;
    case GameTitle::HaloReach: t = &kReach, slot = 2; break;
    case GameTitle::Halo4: t = &kHalo4, slot = 3; break;
    case GameTitle::HaloCE: t = &kHaloCE, slot = 4; break;
    case GameTitle::Halo2: t = &kHalo2, slot = 5; break;
    default: return;
    }
    // Once per loaded module: installs are retried, and a module reloaded at the same base is a new generation.
    static struct
    {
        uintptr_t base;
        uint32_t generation;
    } s_done[6]{};
    if (s_done[slot].base == base && s_done[slot].generation == generation)
        return;
    s_done[slot] = {base, generation};
    if (title == GameTitle::Halo3 && lifted::Active())
    {
        LOG("%s crosshair location: Project Reclaimer's own setting applies (its Centered crosshair)", t->name);
        return;
    }

    struct Found
    {
        uintptr_t at;
        uint8_t centred[kMostPatchBytes];
        size_t length;
        bool patched;
    };
    Found found[2]{};
    if (t->siteCount > sizeof(found) / sizeof(found[0]))
        return;
    for (size_t i = 0; i < t->siteCount; ++i)
    {
        const Site& site = t->sites[i];
        const size_t bytes = PatternBytes(site.pattern);
        const uintptr_t match = mcc_clock::FindUnique(base, size, site.pattern);
        uint8_t code[64]{};
        uint8_t callee[kCalleeBytes]{};
        uintptr_t target = 0;
        const bool ok = match && bytes <= sizeof(code) && mcc_clock::CopyCode(match, code, bytes) &&
            CentredBytes(site.patch, code + site.patchOffset, found[i].centred, found[i].length, found[i].patched) &&
            (!site.callee ||
             ((target = CallTarget(code + site.callOffset, match + site.callOffset)) >= base &&
              target + kCalleeBytes <= base + size && mcc_clock::CopyCode(target, callee, kCalleeBytes) &&
              Matches(callee, site.callee)));
        if (!ok)
        {
            LOG("%s crosshair location: %s %zu of %zu was not found; MCC's setting applies as the player set it",
                t->name, t->array.pattern ? "settings fill" : "centred check", i + 1, t->siteCount);
            return;
        }
        found[i].at = match + site.patchOffset;
    }

    size_t madeNow = 0;
    for (size_t i = 0; i < t->siteCount; ++i)
    {
        if (found[i].patched)
            continue;
        auto* code = reinterpret_cast<uint8_t*>(found[i].at);
        DWORD oldProtect = 0;
        if (!VirtualProtect(code, found[i].length, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LOG("%s crosshair location: settings fill %zu could not be written; %s", t->name, i + 1,
                madeNow ? "the fills before it are Centered, the rest follow MCC's setting"
                        : "MCC's setting applies");
            return;
        }
        std::memcpy(code, found[i].centred, found[i].length);
        FlushInstructionCache(GetCurrentProcess(), code, found[i].length);
        DWORD ignored = 0;
        VirtualProtect(code, found[i].length, oldProtect, &ignored);
        ++madeNow;
    }

    // The profiles the game filled before this: set directly, one int each. CE and Halo 2 have no copy of their own.
    const uintptr_t arrayMatch = t->array.pattern ? mcc_clock::FindUnique(base, size, t->array.pattern) : 0;
    uint8_t arrayBytes[32]{};
    uintptr_t array = 0;
    uint32_t stride = 0;
    uint32_t set = 0;
    const bool arrayFound = arrayMatch && t->array.strideOffset + sizeof(uint32_t) <= sizeof(arrayBytes) &&
        mcc_clock::CopyCode(arrayMatch, arrayBytes, t->array.strideOffset + sizeof(uint32_t)) &&
        DecodeArray(t->array, arrayBytes, arrayMatch, t->field, array, stride) && array >= base &&
        FieldAddress(array, stride, kUsers - 1, t->field) + sizeof(int32_t) <= base + size;
    if (arrayFound)
    {
        for (uint32_t user = 0; user < kUsers; ++user)
        {
            auto* value = reinterpret_cast<volatile int32_t*>(FieldAddress(array, stride, user, t->field));
            if (*value == 0)
            {
                *value = kCentred;
                ++set;
            }
        }
    }
    char fills[48]{};
    std::snprintf(fills, sizeof(fills), t->siteCount == 1 ? "+0x%llX" : "+0x%llX and +0x%llX",
                  static_cast<unsigned long long>(found[0].at - base),
                  static_cast<unsigned long long>(found[t->siteCount - 1].at - base));
    if (!t->array.pattern)
    {
        LOG("%s crosshair location: Centered, whatever the player's MCC setting (the game's centred check at %s reads "
            "Centered%s)",
            t->name, fills, madeNow ? "" : ", already patched");
        return;
    }
    LOG("%s crosshair location: Centered, whatever the player's MCC setting (the settings fill%s at %s take%s the "
        "Centered arm%s; profile field +0x%X; %s)",
        t->name, t->siteCount == 1 ? "" : "s", fills, t->siteCount == 1 ? "s" : "",
        madeNow ? "" : ", already patched", static_cast<unsigned>(t->field),
        !arrayFound ? "the profiles already filled were not found; they keep their setting until MCC fills them again"
        : set       ? "the profiles already filled are Centered now"
                    : "no profile already filled was Lowered");
}
