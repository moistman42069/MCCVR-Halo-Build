// Included inside game.cpp's Reach implementation and the isolated production
// wrapper fixture. HREK 0x91C490 -> pinned Reach 0x2DB54C proves this SIX-argument
// ABI: the native 0x34-byte basis is argument five, not Halo 3's argument four.
// See docs/REACH-HUD-HEIGHT-2026-09-15.md.
using ReachHudAnchorBasisFn = bool(__fastcall*)(
    int, int, void*, void*, void*, bool*);
ReachHudAnchorBasisFn g_reachOrigHudAnchorBasis = nullptr;
void* g_reachHudAnchorBasisTarget = nullptr;
std::atomic<bool> g_reachHudHeightEnabled{false};
std::atomic<uint64_t> g_reachHudHeightApplied{0};
std::atomic<uint64_t> g_reachHudHeightRefused{0};
std::atomic<uint64_t> g_reachHudHeightWorldAnchored{0};
thread_local bool g_reachHudHeightRedirected = false;
thread_local uint32_t g_reachHudAnchorDepth = 0;
// Set while one outermost anchor is composed when it, or a parent it recurses
// through, is placed on a world point.
thread_local bool g_reachHudAnchorOnWorld = false;

// THE ANCHORS PLACED ON A POINT IN THE WORLD keep the game's own place: the HUD
// height moves the HUD, not the world. By the anchor enum's HREK names (HREK
// reach_tag_test.exe 0x91C490's jump table; retail 0x2DB54C's compare tree
// follows the same set, tools/verify-reach-nav-markers.py in git history), these
// are the cases that call the marker projection, the object path or the
// projected-basis helper: 15 tracked target, 16 tracking object, 21 weapon
// target, 22 ghost reticule, 23 hologram target, 24 airstrike target, 25
// player, 26 scripted object (campaign objectives), 40-49 fireteam and
// objective objects. Shifting them put an objective's marker or arrow
// hud_vertical_offset virtual pixels away from its target (headset report,
// 2026-11-06: at -202 the arrow sat near the floor).
constexpr bool ReachHudAnchorOnWorld(int anchorType) noexcept
{
    return anchorType == 15 || anchorType == 16 ||
        (anchorType >= 21 && anchorType <= 26) ||
        (anchorType >= 40 && anchorType <= 49);
}

constexpr uintptr_t kReachHudAnchorBasisRva = 0x2DB54C;
constexpr uintptr_t kReachHudAnchorBasisEndRva = 0x2DC1BE;
constexpr char kReachHudAnchorBasisEntryAob[] =
    "48 8B C4 48 89 58 08 4C 89 48 20 55 56 57 41 54 41 55 41 56 "
    "41 57 48 8D 68 B1 48 81 EC B0 00 00 00 48 8B 7D 77 4D 8B E8 "
    "4C 8B 75 7F 41 B4 01 4C 8B 15 ?? ?? ?? ?? 33 DB";
constexpr uintptr_t kReachHudAnchorBasisOutputRva = 0x2DB65B;
constexpr char kReachHudAnchorBasisOutputAob[] =
    "F3 0F 11 47 28 F3 0F 11 4F 2C E9 80 00 00 00";
constexpr uintptr_t kReachHudAnchorBitmapCallRva = 0x2DF2D5;
constexpr uintptr_t kReachHudAnchorTextCallRva = 0x2DCA43;
constexpr uintptr_t kReachHudAnchorModelCallRva = 0x2DEC72;
constexpr uintptr_t kReachHudAnchorParentCallRva = 0x2DA8BA;

static bool ReachHudHeightOwnsStereo() noexcept
{
    const uint32_t generation =
        g_reachCamera.generation.load(std::memory_order_acquire);
    // Both native CHUD phases use this producer. Unlike authored reticle
    // capture, translating the returned HUD basis also applies to the late
    // compositing phase, so admission must not require per-eye render TLS.
    return g_reachHudHeightEnabled.load(std::memory_order_acquire) &&
        generation != 0 &&
        TitleAdapter_GetActiveTitle() == GameTitle::HaloReach &&
        TitleAdapter_GetGeneration(GameTitle::HaloReach) == generation &&
        g_reachCamera.installed.load(std::memory_order_acquire) &&
        g_reachCamera.armed.load(std::memory_order_acquire) &&
        !g_reachCamera.teardownRequested.load(std::memory_order_acquire) &&
        g_enabled.load(std::memory_order_relaxed) && VR_IsStereoEnabled();
}

static void ReachAdjustHudAnchorHeight(void* basis) noexcept
{
    // The frame's HUD height is left as the game places it (AIM FRAME ZOOM).
    if (!basis || g_reachHudHeightRedirected || g_aimFramePass.load(std::memory_order_acquire) ||
        !ReachHudHeightOwnsStereo())
        return;
    const float height = g_config.hud_vertical_offset;
    if (!std::isfinite(height) || height < kHudHeightMin ||
        height > kHudHeightMax)
    {
        g_reachHudHeightRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    __try
    {
        // Both screen and projected-object anchors prove +0x2C is virtual
        // pixel Y. The sixth output is not a world-unit flag: false-output
        // anchors have already projected their object positions. Only this
        // scalar changes; native axes/scale/visibility/anchor/flag are preserved.
        float* const y = reinterpret_cast<float*>(
            static_cast<uint8_t*>(basis) + 0x2C);
        const float current = *y;
        const float wanted = current - height;
        if (!std::isfinite(current) || !std::isfinite(wanted))
        {
            g_reachHudHeightRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        *y = wanted;
        g_reachHudHeightApplied.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Only height fails. Leave native result and the camera/session alive;
        // the worker reports the refusal outside this hot hook.
        g_reachHudHeightRefused.fetch_add(1, std::memory_order_relaxed);
    }
}

__declspec(noinline) bool __fastcall ReachHudAnchorBasisDetour(
    int userIndex, int anchorType, void* placementFlags, void* drawWidgetData,
    void* basis, bool* nativeAnchorFlag)
{
    g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
    const bool outermost = g_reachHudAnchorDepth++ == 0;
    if (outermost)
        g_reachHudAnchorOnWorld = false;
    if (ReachHudAnchorOnWorld(anchorType))
        g_reachHudAnchorOnWorld = true;
    bool result = false;
    __try
    {
        ReachHudAnchorBasisFn original = g_reachOrigHudAnchorBasis;
        if (original)
        {
            result = original(userIndex, anchorType, placementFlags,
                drawWidgetData, basis, nativeAnchorFlag);
            // Anchor 0 (parent) recurses through +0x2DA818 -> +0x2DA8BA.
            // Translate the fully composed outer result once; shifting both
            // parent and child would compound height with authored scale.
            // A widget placed on a world point, itself or through its
            // parent, stays on that point.
            if (result && outermost)
            {
                if (g_reachHudAnchorOnWorld)
                    g_reachHudHeightWorldAnchored.fetch_add(
                        1, std::memory_order_relaxed);
                else
                    ReachAdjustHudAnchorHeight(basis);
            }
        }
    }
    __finally
    {
        --g_reachHudAnchorDepth;
        g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
