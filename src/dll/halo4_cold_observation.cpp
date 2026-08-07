#include "halo4_cold_observation.h"

#include <windows.h>

#include "sigscan.h"
#include "../common/halo4_render_logic.h"
#include "../common/log.h"

namespace
{
    // One COMPLETED observation per module instance. MCC unloads and reloads
    // game DLLs freely; a new base or generation is a new instance and
    // re-earns its proof. The latch is written only AFTER the module pin
    // succeeds: a transient pin failure must retry, not permanently consume
    // the instance's single attempt (the level-load gate had exactly this
    // one-shot defect before Rearm() existed - see game.cpp).
    uintptr_t g_attemptedBase = 0;
    uint32_t g_attemptedGeneration = 0;
    bool g_attempted = false;
    // The PASS verdict, tagged with the generation that earned it, so a later
    // module instance can never inherit an earlier instance's proof.
    uint32_t g_passedGeneration = 0;
    bool g_passed = false;
    // One-time log throttles; retries stay silent.
    uint32_t g_withheldLoggedGeneration = 0;
    uint32_t g_pinFailLoggedGeneration = 0;

    // Refcount pin held only for the duration of one preflight, and only
    // taken after the level-load gate PROVED the level running. Taking a pin
    // during a level load is the exact touch that produced the load bounce
    // (docs/ODST-LEVEL-LOAD-LOCKOUT.md); the gate ordering is load-bearing.
    class Halo4ModulePin
    {
    public:
        Halo4ModulePin() noexcept = default;
        ~Halo4ModulePin()
        {
            Reset();
        }
        Halo4ModulePin(const Halo4ModulePin&) = delete;
        Halo4ModulePin& operator=(const Halo4ModulePin&) = delete;

        bool Acquire(uintptr_t expectedBase) noexcept
        {
            Reset();
            HMODULE module = nullptr;
            if (!expectedBase ||
                !GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(expectedBase), &module) ||
                reinterpret_cast<uintptr_t>(module) != expectedBase)
            {
                if (module)
                    FreeLibrary(module);
                return false;
            }
            m_module = module;
            return true;
        }

        bool IsCurrent(uintptr_t expectedBase) const noexcept
        {
            return m_module && expectedBase &&
                reinterpret_cast<uintptr_t>(m_module) == expectedBase &&
                GetModuleHandleW(L"halo4.dll") == m_module;
        }

    private:
        void Reset() noexcept
        {
            if (m_module)
            {
                FreeLibrary(m_module);
                m_module = nullptr;
            }
        }

        HMODULE m_module = nullptr;
    };

    bool VerifyLoadedPeIdentity(uintptr_t base, size_t size) noexcept
    {
        __try
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
                return false;
            const size_t ntOffset = static_cast<size_t>(dos->e_lfanew);
            if (ntOffset > size || sizeof(IMAGE_NT_HEADERS64) > size - ntOffset)
                return false;
            const auto* nt =
                reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + ntOffset);
            return nt->Signature == IMAGE_NT_SIGNATURE &&
                nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
                nt->FileHeader.TimeDateStamp == kHalo4RetailPeTimestamp &&
                nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
                nt->OptionalHeader.SizeOfImage == kHalo4RetailImageSize;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

bool Halo4ColdObservation_Pending(uint32_t generation) noexcept
{
    return !(g_attempted && g_attemptedGeneration == generation);
}

bool Halo4ColdObservation_Passed(uint32_t generation) noexcept
{
    return generation != 0 && g_passed && g_passedGeneration == generation;
}

void Halo4ColdObservation_Poll(
    uintptr_t moduleBase, size_t moduleSize, uint32_t generation,
    bool halo4LevelRunning, bool gateArrayProven) noexcept
{
    if (!moduleBase || !moduleSize || !generation || !halo4LevelRunning)
        return;
    if (!gateArrayProven)
    {
        // The gate opened fail-open: it could not resolve the player-view
        // array in this module, which is the stale-evidence case (an MCC
        // update). Scanning here would be a whole-image touch on a module
        // whose loading state the gate cannot actually see - refuse, loudly,
        // once.
        if (g_withheldLoggedGeneration != generation)
        {
            g_withheldLoggedGeneration = generation;
            LOG("Halo 4 cold observation WITHHELD: the level-load gate could "
                "not prove this module's player-view array, so halo4.dll is "
                "not scanned or pinned (the pinned evidence likely does not "
                "describe this build); nothing was verified");
        }
        return;
    }
    if (g_attempted && g_attemptedBase == moduleBase &&
        g_attemptedGeneration == generation)
        return;

    Halo4ModulePin pin;
    if (!pin.Acquire(moduleBase))
    {
        // Not latched: the loader may simply be mid-transition. Retry on a
        // later tick; say so once so a persistent failure is visible.
        if (g_pinFailLoggedGeneration != generation)
        {
            g_pinFailLoggedGeneration = generation;
            LOG("Halo 4 cold observation: halo4.dll could not be pinned at "
                "its observed base; the attempt is not consumed and will "
                "retry");
        }
        return;
    }
    g_attempted = true;
    g_attemptedBase = moduleBase;
    g_attemptedGeneration = generation;

    Halo4ColdObservationResult result{};
    result.moduleRangeValid = moduleSize == kHalo4RetailImageSize;
    result.peIdentity = VerifyLoadedPeIdentity(moduleBase, moduleSize);

    if (result.moduleRangeValid && result.peIdentity)
    {
        for (const Halo4RetailAnchor& anchor : kHalo4RetailAnchors)
        {
            const uintptr_t hit =
                sig::Find(moduleBase, moduleSize, anchor.pattern);
            if (!hit)
            {
                LOG("Halo 4 cold observation: anchor %s matched ZERO times "
                    "(pinned RVA 0x%X)", anchor.name, anchor.rva);
                continue;
            }
            if (sig::Find(hit + 1, moduleBase + moduleSize - hit - 1,
                          anchor.pattern))
            {
                LOG("Halo 4 cold observation: anchor %s is NOT unique in the "
                    "loaded image (first match RVA 0x%zX, pinned RVA 0x%X)",
                    anchor.name, hit - moduleBase, anchor.rva);
                continue;
            }
            ++result.anchorsMatchedOnce;
            if (hit - moduleBase == anchor.rva)
                ++result.anchorsAtPinnedRva;
            else
                LOG("Halo 4 cold observation: anchor %s matched once but "
                    "moved (RVA 0x%zX, pinned 0x%X)",
                    anchor.name, hit - moduleBase, anchor.rva);
            if (anchor.ripDispOffset != 0)
            {
                // The disp32 sits inside the matched bytes, so the read is
                // within the pinned module at a location the match proved.
                const uintptr_t target = sig::RipTarget(
                    hit + anchor.ripDispOffset,
                    hit + anchor.ripDispOffset + 4);
                if (target - moduleBase == anchor.ripTargetRva)
                    ++result.ripTargetsAtPinnedRva;
                else
                    LOG("Halo 4 cold observation: anchor %s rip decode "
                        "landed at RVA 0x%zX, pinned 0x%X",
                        anchor.name, target - moduleBase, anchor.ripTargetRva);
            }
        }
    }
    else
    {
        LOG("Halo 4 cold observation: identity precheck failed (module size "
            "0x%zX vs pinned 0x%zX, peIdentity=%d); anchor scans skipped - "
            "the verdict is already FAIL",
            moduleSize, kHalo4RetailImageSize, result.peIdentity ? 1 : 0);
    }

    result.mappingStable = pin.IsCurrent(moduleBase);

    g_passed = Halo4ColdObservationPass(result);
    g_passedGeneration = g_passed ? generation : 0;

    if (Halo4ColdObservationPass(result))
    {
        LOG("Halo 4 cold observation PASS (C-H4-2): loaded image matches the "
            "pinned retail identity (PE timestamp 0x%08X, SizeOfImage 0x%08X); "
            "all %zu E-H4-4 anchors unique at their pinned RVAs; player-view "
            "array 0x%X (stride 0x%X) and g_view_stack_top 0x%X decode "
            "correctly from the loaded bytes; runtime hooks remain disabled "
            "by design",
            kHalo4RetailPeTimestamp,
            static_cast<uint32_t>(kHalo4RetailImageSize),
            kHalo4RetailAnchorCount, kHalo4PlayerViewArrayRva,
            kHalo4PlayerViewStride, kHalo4ViewStackTopRva);
    }
    else
    {
        LOG("Halo 4 cold observation FAIL (C-H4-2): range=%d peIdentity=%d "
            "anchorsOnce=%u/%zu anchorsPinned=%u/%zu ripDecodes=%u/%u "
            "mappingStable=%d; this build's Halo 4 evidence does NOT describe "
            "the loaded image; hooks were never enabled and nothing changes",
            result.moduleRangeValid ? 1 : 0, result.peIdentity ? 1 : 0,
            result.anchorsMatchedOnce, kHalo4RetailAnchorCount,
            result.anchorsAtPinnedRva, kHalo4RetailAnchorCount,
            result.ripTargetsAtPinnedRva, kHalo4RetailAnchorRipTargets,
            result.mappingStable ? 1 : 0);
    }
}
