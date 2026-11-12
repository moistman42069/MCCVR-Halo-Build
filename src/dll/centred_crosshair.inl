// ALWAYS THE CENTRED CROSSHAIR in every title (centred_crosshair_logic.h). Run at each title's install, on its own: the
// patch is a few bytes of the game's settings fills (CE and Halo 2: its centred check), not a hook, and selects the
// game's own Centered arm. Where the fills are comes from the direct drives' address cascade (centred_crosshair_sites.h,
// drive_cascade.h): verified at the addresses they are known to stand at, or found by search, by either of two routes
// to each fill. A title is patched whole or not at all; anything not proven leaves it on the player's setting (the
// Lowered fallbacks stay: seat_drive::SteadyPitchOffset, the capture window that follows the art, Halo 4's Aim Frame
// Zoom recentre wait). Project Reclaimer 0.10 runs a recompiled Halo 3 whose code this does not reach; the launcher
// turns on its own Centered crosshair instead.
void InstallCentredCrosshair(GameTitle title, uintptr_t base, size_t size, uint32_t generation)
{
    using namespace centred_crosshair;
    const centred_crosshair_sites::TitleSites* const t = centred_crosshair_sites::For(title);
    if (!t)
        return;
    size_t slot = 0;
    while (centred_crosshair_sites::kTitles[slot] != t)
        ++slot;
    // Once per loaded module: installs are retried, and a module reloaded at the same base is a new generation.
    static struct
    {
        uintptr_t base;
        uint32_t generation;
    } s_done[sizeof(centred_crosshair_sites::kTitles) / sizeof(centred_crosshair_sites::kTitles[0])]{};
    if (s_done[slot].base == base && s_done[slot].generation == generation)
        return;
    s_done[slot] = {base, generation};
    if (title == GameTitle::Halo3 && lifted::Active())
    {
        LOG("%s crosshair location: Project Reclaimer's own setting applies (its Centered crosshair)", t->name);
        return;
    }

    drive_cascade::Resolution<centred_crosshair_sites::Resolved> resolution;
    const bool opened = drive_cascade::Resolve(base, size, drive_cascade::Drive{title, "crosshair"}, resolution,
                                               [t](drive_resolve::Resolver& r) { return centred_crosshair_sites::Resolve(r, *t); });
    if (opened)
        drive_cascade::Announce(t->logName, resolution);
    const centred_crosshair_sites::Resolved& found = resolution.found;
    const char* const what = t->profiles.caller.pattern ? "settings fill" : "centred check";
    if (!opened || !found.fills)
    {
        char first[280] = "";
        if (opened)
            drive_cascade::FirstFailure(resolution.report, first, sizeof(first));
        LOG("%s crosshair location: a %s was not found (%s); MCC's setting applies as the player set it", t->name, what,
            opened ? first : (resolution.why ? resolution.why : "the module did not open"));
        return;
    }

    // THE DECISION, on the bytes the game has now at the places found: each stock or already patched.
    struct Planned
    {
        uintptr_t at;
        uint8_t centred[kMostPatchBytes];
        size_t length;
        bool patched;
    };
    Planned planned[2]{};
    for (uint32_t i = 0; i < t->fillCount; ++i)
    {
        uint8_t code[kMostPatchBytes]{};
        planned[i].at = base + found.at[i];
        if (!mcc_clock::CopyCode(planned[i].at, code, sizeof(code)) ||
            !CentredBytes(t->fills[i].patch, code, planned[i].centred, planned[i].length, planned[i].patched))
        {
            LOG("%s crosshair location: %s %u of %u changed before it could be written; MCC's setting applies", t->name,
                what, i + 1, t->fillCount);
            return;
        }
    }

    size_t madeNow = 0;
    for (uint32_t i = 0; i < t->fillCount; ++i)
    {
        if (planned[i].patched)
            continue;
        auto* code = reinterpret_cast<uint8_t*>(planned[i].at);
        DWORD oldProtect = 0;
        if (!VirtualProtect(code, planned[i].length, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LOG("%s crosshair location: %s %u could not be written; %s", t->name, what, i + 1,
                madeNow ? "the fills before it are Centered, the rest follow MCC's setting"
                        : "MCC's setting applies");
            return;
        }
        std::memcpy(code, planned[i].centred, planned[i].length);
        FlushInstructionCache(GetCurrentProcess(), code, planned[i].length);
        DWORD ignored = 0;
        VirtualProtect(code, planned[i].length, oldProtect, &ignored);
        ++madeNow;
    }

    // The profiles the game filled before this: set directly, one int each. CE and Halo 2 have no copy of their own.
    uint32_t set = 0;
    if (found.array && found.stride)
    {
        for (uint32_t user = 0; user < kUsers; ++user)
        {
            auto* value =
                reinterpret_cast<volatile int32_t*>(FieldAddress(base + found.array, found.stride, user, t->field));
            if (*value == 0)
            {
                *value = kCentred;
                ++set;
            }
        }
    }
    char fills[48]{};
    std::snprintf(fills, sizeof(fills), t->fillCount == 1 ? "+0x%X" : "+0x%X and +0x%X", found.at[0],
                  found.at[t->fillCount - 1]);
    if (!t->profiles.caller.pattern)
    {
        LOG("%s crosshair location: Centered, whatever the player's MCC setting (the game's centred check at %s reads "
            "Centered%s)",
            t->name, fills, madeNow ? "" : ", already patched");
        return;
    }
    LOG("%s crosshair location: Centered, whatever the player's MCC setting (the settings fill%s at %s take%s the "
        "Centered arm%s; profile field +0x%X; %s)",
        t->name, t->fillCount == 1 ? "" : "s", fills, t->fillCount == 1 ? "s" : "", madeNow ? "" : ", already patched",
        static_cast<unsigned>(t->field),
        !(found.array && found.stride)
            ? "the profiles already filled were not found; they keep their setting until MCC fills them again"
        : set ? "the profiles already filled are Centered now"
              : "no profile already filled was Lowered");
}
