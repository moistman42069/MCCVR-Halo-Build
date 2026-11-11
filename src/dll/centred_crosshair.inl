// ALWAYS THE CENTRED CROSSHAIR in Halo 3 and ODST (centred_crosshair_logic.h). Run at each title install, before the
// camera transaction, and on its own: the patch is one byte of the game's settings fill, not a hook, and selects the
// game's own Centered arm. Anything not proven leaves that title on the player's setting (the Lowered fallbacks stay:
// seat_drive::SteadyPitchOffset, the capture window that follows the art). Project Reclaimer 0.10 runs a recompiled
// Halo 3 whose code this does not reach; the launcher turns on its own Centered crosshair instead.
void InstallCentredCrosshair(GameTitle title, uintptr_t base, size_t size)
{
    const char* const name = title == GameTitle::Halo3ODST ? "ODST" : "Halo 3";
    // One report per loaded module (installs are retried) and per patch made (a reloaded module can land at the same base).
    static uintptr_t s_reportedBase[2]{};
    uintptr_t& reported = s_reportedBase[title == GameTitle::Halo3ODST ? 1 : 0];
    const bool report = reported != base;
    reported = base;
    if (title == GameTitle::Halo3 && lifted::Active())
    {
        if (report)
            LOG("%s crosshair location: Project Reclaimer's own setting applies (its Centered crosshair)", name);
        return;
    }
    const uintptr_t fill = mcc_clock::FindUnique(base, size, centred_crosshair::kFillPattern);
    uint8_t before[centred_crosshair::kFillCompareSearch]{};
    uint8_t jump[centred_crosshair::kFillPatternBytes]{};
    bool patched = false;
    uint8_t field = 0;
    if (!fill || fill < base + sizeof(before) ||
        !mcc_clock::CopyCode(fill - sizeof(before), before, sizeof(before)) ||
        !mcc_clock::CopyCode(fill, jump, sizeof(jump)) ||
        !centred_crosshair::DecodeFill(jump, before, patched, field))
    {
        if (report)
            LOG("%s crosshair location: the settings fill was not found; MCC's setting applies as the player set it",
                name);
        return;
    }
    if (!patched)
    {
        auto* branch = reinterpret_cast<uint8_t*>(fill);
        DWORD oldProtect = 0;
        if (!VirtualProtect(branch, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            if (report)
                LOG("%s crosshair location: the settings fill could not be written; MCC's setting applies", name);
            return;
        }
        branch[0] = 0xEB;   // je +5 -> jmp +5: the Centered arm
        FlushInstructionCache(GetCurrentProcess(), branch, 1);
        DWORD ignored = 0;
        VirtualProtect(branch, 1, oldProtect, &ignored);
    }
    // The profiles the game filled before this: set directly, one int each.
    const uintptr_t arrayMatch = mcc_clock::FindUnique(base, size, centred_crosshair::kArrayPattern);
    uint8_t arrayBytes[centred_crosshair::kArrayStrideOffset + sizeof(uint32_t)]{};
    uintptr_t array = 0;
    uint32_t stride = 0;
    uint32_t set = 0;
    const bool arrayFound = arrayMatch && mcc_clock::CopyCode(arrayMatch, arrayBytes, sizeof(arrayBytes)) &&
        centred_crosshair::DecodeArray(arrayBytes, arrayMatch, field, array, stride) && array >= base &&
        centred_crosshair::FieldAddress(array, stride, centred_crosshair::kUsers - 1, field) + sizeof(int32_t) <=
            base + size;
    if (arrayFound)
    {
        for (uint32_t user = 0; user < centred_crosshair::kUsers; ++user)
        {
            auto* value = reinterpret_cast<volatile int32_t*>(
                centred_crosshair::FieldAddress(array, stride, user, field));
            if (*value == 0)
            {
                *value = centred_crosshair::kCentred;
                ++set;
            }
        }
    }
    if (report || !patched)
        LOG("%s crosshair location: Centered, whatever the player's MCC setting (the settings fill at +0x%llX takes "
            "the Centered arm%s; profile field +0x%X; %s)",
            name, static_cast<unsigned long long>(fill - base), patched ? ", already patched" : "",
            static_cast<unsigned>(field),
            !arrayFound ? "the profiles already filled were not found; they keep their setting until MCC fills them again"
            : set       ? "the profiles already filled are Centered now"
                        : "no profile already filled was Lowered");
}
