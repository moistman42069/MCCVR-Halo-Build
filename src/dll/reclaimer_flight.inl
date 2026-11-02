// Included by game.cpp. PROJECT RECLAIMER'S FLIGHT CONTROLS (reclaimer_flight_buttons).
//
// halo3.dll reads a vtol-class flyer's climb and dive (actions 0x22 and 0x23, MCC's Ascend and Descend; the
// Hornet's class, which a Sparrowhawk, Falcon or Vulture on a mod map also is) only while a per-player controller
// preference allows it. Otherwise it climbs and dives by the look's pitch past a threshold, as Halo 3 first
// shipped, and its default preferences set that byte. MCC fills the preference from its own settings; Project
// Reclaimer leaves the default, so its flyers ignore the buttons while the controller is in use. With the setting
// on, the byte is cleared for every local player; turned off, each is put back as it was.
//
// PROOF (resolved + guarded, nothing hardcoded): the gate function by a unique signature that loads the image's
// base (lea r8, [rip + disp], which must land on the module's base) and names the preference by its RVA
// (controller 0's byte; the controllers are 0x21C apart, the stride in the same instruction). Its one call site
// reads 0x22 and 0x23 straight after it (a second unique signature, whose call must land on the gate). The byte
// must lie in a writable section of the image. Anything else: nothing is written, and the status says why. The
// same bytes match in the retail image and in the one Project Reclaimer patches (0.9.8).
namespace
{
inline constexpr char kReclaimerFlightGateSig[] =
    "33 C0 48 63 D1 4C 8D 05 ?? ?? ?? ?? 83 F9 03 77 0A 41 8B 8C 90 ?? ?? ?? ?? EB 06 8B 0D ?? ?? ?? ?? "
    "85 C9 75 0F 48 69 C2 1C 02 00 00 42 8A 84 00 ?? ?? ?? ?? C3";
inline constexpr size_t kReclaimerFlightBaseDisp = 8;   // lea r8, [rip + disp]: the image's base
inline constexpr size_t kReclaimerFlightBaseNext = 12;
inline constexpr size_t kReclaimerFlightPrefDisp = 48;  // mov al, [rax + r8 + disp]: controller 0's byte (an RVA)
inline constexpr char kReclaimerFlightCallerSig[] =
    "E8 ?? ?? ?? ?? 84 C0 75 ?? BA 22 00 00 00 49 8B CF E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? BA 23 00 00 00";
inline constexpr uintptr_t kReclaimerFlightStride = 0x21C;
inline constexpr unsigned kReclaimerFlightPlayers = 4;

struct ReclaimerFlightState
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    bool attempted = false;
    uint8_t* flag = nullptr;                         // controller 0's byte; the others follow at the stride
    uint8_t original[kReclaimerFlightPlayers]{};     // each one's value before the mod first changed it
    bool changed[kReclaimerFlightPlayers]{};
    uint32_t reapplied = 0;                          // times the host set one back after the mod cleared it
};
ReclaimerFlightState g_reclaimerFlight;
std::atomic<int> g_reclaimerFlightStatus{0};         // 0 not in a game, 1 buttons, 2 classic, 3 not found

bool ReclaimerFlightWritable(uintptr_t base, uintptr_t address, size_t bytes)
{
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        const uintptr_t start = base + section->VirtualAddress;
        const uintptr_t end = start + (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
        if (address >= start && address + bytes <= end)
            return (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
    }
    return false;
}

// The address of controller 0's flight preference in this image, or null with `why` saying what failed.
uint8_t* ResolveReclaimerFlight(uintptr_t base, size_t size, const char*& why)
{
    const uintptr_t gate = sig::Find(base, size, kReclaimerFlightGateSig);
    if (!gate) { why = "the flight preference's reader is not in this halo3.dll"; return nullptr; }
    if (sig::Find(gate + 1, size - (gate + 1 - base), kReclaimerFlightGateSig))
    { why = "the flight preference's reader matched twice"; return nullptr; }
    const int32_t baseDisp = *reinterpret_cast<const int32_t*>(gate + kReclaimerFlightBaseDisp);
    if (gate + kReclaimerFlightBaseNext + baseDisp != base)
    { why = "the reader does not load this image's base"; return nullptr; }
    const uintptr_t caller = sig::Find(base, size, kReclaimerFlightCallerSig);
    if (!caller || sig::Find(caller + 1, size - (caller + 1 - base), kReclaimerFlightCallerSig))
    { why = "the climb and dive reads after the reader are not where they were"; return nullptr; }
    if (caller + 5 + *reinterpret_cast<const int32_t*>(caller + 1) != gate)
    { why = "the climb and dive reads do not ask this reader"; return nullptr; }
    const int32_t rva = *reinterpret_cast<const int32_t*>(gate + kReclaimerFlightPrefDisp);
    const uintptr_t flag = base + static_cast<uint32_t>(rva);
    if (rva <= 0 || !ReclaimerFlightWritable(base, flag, kReclaimerFlightStride * (kReclaimerFlightPlayers - 1) + 1))
    { why = "the preference is not in the image's data"; return nullptr; }
    return reinterpret_cast<uint8_t*>(flag);
}

// Cold worker, every poll: resolve once an image, then hold each local player's preference where the setting wants
// it. A host that sets it back (its defaults applied again) is answered on the next poll.
void ReclaimerFlightPoll(const TitleDescriptor* title)
{
    if (!g_hostIsProjectReclaimer || !title || title->title != GameTitle::Halo3)
    {
        g_reclaimerFlightStatus.store(0, std::memory_order_relaxed);
        return;
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(0, title->moduleName, &pinned))
    {
        g_reclaimerFlightStatus.store(0, std::memory_order_relaxed);
        return;
    }
    uintptr_t base = 0;
    size_t size = 0;
    const uint32_t generation = TitleAdapter_GetGeneration(title->title);
    ReclaimerFlightState& s = g_reclaimerFlight;
    if (sig::ModuleRange(title->moduleName, base, size) && base == reinterpret_cast<uintptr_t>(pinned))
    {
        if (s.base != base || s.generation != generation)
        {
            s = ReclaimerFlightState{};
            s.base = base;
            s.generation = generation;
        }
        if (!s.attempted)
        {
            s.attempted = true;
            const char* why = "";
            s.flag = ResolveReclaimerFlight(base, size, why);
            if (s.flag)
                LOG("Reclaimer flight: Halo 3's flight preference found at +0x%llX (controllers 0x%llX apart); "
                    "flyers %s", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(s.flag) - base),
                    static_cast<unsigned long long>(kReclaimerFlightStride),
                    g_config.reclaimer_flight_buttons ? "climb and dive with the buttons (Ascend / Descend)"
                                                      : "keep the classic look-to-climb (reclaimer_flight_buttons 0)");
            else
                LOG("Reclaimer flight: %s; flyers keep Project Reclaimer's own controls", why);
        }
        if (!s.flag)
            g_reclaimerFlightStatus.store(3, std::memory_order_relaxed);
        else
        {
            const bool buttons = g_config.reclaimer_flight_buttons;
            for (unsigned c = 0; c < kReclaimerFlightPlayers; ++c)
            {
                volatile uint8_t* const byte = s.flag + c * kReclaimerFlightStride;
                const uint8_t now = *byte;
                if (buttons && now != 0)
                {
                    if (!s.changed[c])
                    {
                        s.original[c] = now;
                        s.changed[c] = true;
                    }
                    else if (++s.reapplied <= 4)
                        LOG("Reclaimer flight: player %u's preference was set back to classic; buttons again", c);
                    *byte = 0;
                }
                else if (!buttons && s.changed[c])
                {
                    if (now == 0)
                        *byte = s.original[c];
                    s.changed[c] = false;
                }
            }
            g_reclaimerFlightStatus.store(buttons ? 1 : 2, std::memory_order_relaxed);
        }
    }
    FreeLibrary(pinned);
}
}

// For the Vehicles panel: what the setting does in this host right now.
const char* Game_ReclaimerFlightStatus()
{
    switch (g_reclaimerFlightStatus.load(std::memory_order_relaxed))
    {
    case 1: return "Flyers climb and dive with Ascend and Descend (right A and the left grip by default), as in MCC.";
    case 2: return "Flyers climb and dive by aiming up or down, as Halo 3 first shipped.";
    case 3: return "This version of Halo 3 does not match: flyers keep Project Reclaimer's own controls.";
    default: return "Applies once Halo 3 is running.";
    }
}
