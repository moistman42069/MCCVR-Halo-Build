// REACH: THE MOTION TRACKER AND THE SOUND LISTENER FOLLOW THE HEAD -- the runtime half. The evidence, the sites
// and the pure logic are in src/common/reach_head_relative_logic.h; the write-up is
// docs/REACH-HEAD-RELATIVE-2026-10-06.md.
//
// In short. The player aims with the hand and looks with the head, and Reach has one heading for both:
//   * its tracker takes its heading from player control's desired yaw -- the record this mod's direct drive
//     writes the HAND's direction into -- so its contacts swing with the gun;
//   * its sound listener is built from the observer camera, which this mod's aim steering points down the
//     controller ray, so the ears turn with the gun too.
// Each gets its own narrow seam, in the consumer's own data, after the game wrote it and before anything read it:
// the tracker's one heading float, and output user 0's listener rows. Neither reads or writes the aim record, a
// camera, or anything a shot or the network sees. Two independent features: one failing to bind or to retire
// says so and leaves the other alone.

// ---- THE HEAD, as the frame that rendered with it published it (the two-clocks rule) ---------------------------
// Written by the render thread (ReachMainRenderViewBody, at the head camera's commit), read by whoever places
// something against where the player looks. A sequence lock: an odd sequence is a write in progress, and a read
// that saw the sequence move is retried. Every field is an atomic, so a torn read is impossible, only a mixed
// one, and the sequence refuses those.
struct ReachHeadPublication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<float> forward[3]{}, up[3]{};
    std::atomic<uint64_t> stampUs{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint32_t> flags{0};  // bit 0: the mod's head view was live; bit 1: a cinematic held the camera
    std::atomic<uint32_t> thread{0};  // the publishing thread, for the dev report (not under the sequence)
} g_reachHeadPublication;
constexpr uint32_t kReachHeadLive = 1u, kReachHeadCinematic = 2u;

// The render thread, once per committed head camera. `headCenter` is the committed compact camera (forward at
// +0x0C, up at +0x18, in the game's world); `headLookApplied` is false in the cutscene theater, where the
// camera is the authored one and the head did not steer it.
void ReachPublishHeadSample(const unsigned char* headCenter, bool headLookApplied, uint32_t generation) noexcept
{
    auto& published = g_reachHeadPublication;
    const float* forward = reinterpret_cast<const float*>(headCenter + 0x0C);
    const float* up = reinterpret_cast<const float*>(headCenter + 0x18);
    const bool live = headLookApplied && generation && head_relative::Finite3(forward) && head_relative::Finite3(up);
    const uint32_t flags = (live ? kReachHeadLive : 0u) |
                           (g_reachCinematicLocked.load(std::memory_order_relaxed) ? kReachHeadCinematic : 0u);
    const uint64_t nowUs = Sight_NowUs();
    const uint32_t sequence = published.sequence.load();
    published.sequence.store(sequence + 1);
    for (int i = 0; i < 3; ++i)
    {
        published.forward[i].store(forward[i]);
        published.up[i].store(up[i]);
    }
    published.stampUs.store(nowUs);
    published.generation.store(generation);
    published.flags.store(flags);
    published.sequence.store(sequence + 2);
#if HALOMCCVR_DEV_DIAGNOSTICS
    published.thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
#endif
}
// Any thread. False = nothing coherent could be read (the writer was mid-write four times running).
bool ReachReadHeadSample(head_relative::HeadSample& sample, bool& cinematic) noexcept
{
    auto& published = g_reachHeadPublication;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before = published.sequence.load();
        if (before & 1u)
            continue;
        for (int i = 0; i < 3; ++i)
        {
            sample.forward[i] = published.forward[i].load();
            sample.up[i] = published.up[i].load();
        }
        sample.stampUs = published.stampUs.load();
        sample.generation = published.generation.load();
        const uint32_t flags = published.flags.load();
        if (published.sequence.load() != before)
            continue;
        sample.valid = (flags & kReachHeadLive) != 0;
        cinematic = (flags & kReachHeadCinematic) != 0;
        return true;
    }
    return false;
}

// ---- what both seams share: proving the image, hooking, retiring -----------------------------------------------
// The game's bytes at a fixed place, as they stood before any hook of ours. False = unreadable.
__declspec(noinline) bool ReachHeadRelativeCopy(uintptr_t address, void* out, size_t size) noexcept
{
    __try
    {
        (void)sig::CopyUnhooked(address, out, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
bool ReachHeadRelativeInside(uintptr_t base, size_t size, uintptr_t address, size_t bytes)
{
    return address >= base && bytes <= size && address - base <= size - bytes;
}
// `bytes` of the image at `address` when it is inside the image and readable.
bool ReachHeadRelativeRead(uintptr_t base, size_t size, uintptr_t address, void* out, size_t bytes)
{
    return address && ReachHeadRelativeInside(base, size, address, bytes) && ReachHeadRelativeCopy(address, out, bytes);
}
// A pattern's one match in the image, or 0 when it is missing or matches twice.
uintptr_t ReachHeadRelativeUnique(uintptr_t base, size_t size, const char* pattern)
{
    const uintptr_t match = sig::Find(base, size, pattern);
    if (!match || match + 1 >= base + size)
        return 0;
    return sig::Find(match + 1, base + size - match - 1, pattern) ? 0 : match;
}
// The beginning of the function `address` lies in, from the image's own function table, as an offset from
// `base`; -1 when the table has no entry for it or it belongs to another image.
int64_t ReachHeadRelativeFunctionRva(uintptr_t base, uintptr_t address, int64_t* endRva = nullptr)
{
    DWORD64 imageBase = 0;
    const auto* function = RtlLookupFunctionEntry(address, &imageBase, nullptr);
    if (!function || imageBase != base)
        return -1;
    if (endRva)
        *endRva = static_cast<int64_t>(function->EndAddress);
    return static_cast<int64_t>(function->BeginAddress);
}
// Creates and enables one hook. False leaves whatever was created in `target` / `original` for the caller to
// retire, and has said why.
bool ReachHeadRelativeHook(const char* tag, void* address, void* detour, void*& target, void*& original)
{
    const auto created = MH_CreateHook(address, detour, &original);
    if (created != MH_OK)
    {
        original = nullptr;
        LOG("%s HEAD Reach: StockFallback: hook creation failed (%d)", tag, static_cast<int>(created));
        return false;
    }
    target = address;
    const auto enabled = MH_EnableHook(address);
    if (enabled != MH_OK)
    {
        LOG("%s HEAD Reach: StockFallback: hook enable failed (%d)", tag, static_cast<int>(enabled));
        return false;
    }
    return true;
}
// Disables, waits for the detour to be quiet, removes. True when nothing is left installed.
bool ReachHeadRelativeRetire(const char* tag, std::atomic<bool>& enabled, void*& target, void*& original,
                             const void* detour, const std::atomic<uint32_t>& callbacks)
{
    enabled.store(false, std::memory_order_release);
    if (!target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("%s HEAD Reach: CleanupRequired: disable failed (%d)", tag, static_cast<int>(disabled));
        return false;
    }
    const void* functions[]{detour};
    const void* trampolines[]{original};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, callbacks))
    {
        LOG("%s HEAD Reach: CleanupRequired: the detour is busy", tag);
        return false;
    }
    const auto removed = MH_RemoveHook(target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("%s HEAD Reach: CleanupRequired: removal failed (%d)", tag, static_cast<int>(removed));
        return false;
    }
    target = nullptr;
    original = nullptr;
    return true;
}
// "reason=count, reason=count" for a report line; returns the length written.
int ReachHeadRelativeReasons(char* text, size_t capacity, const uint32_t* counts, size_t count, size_t used,
                             const char* (*name)(size_t))
{
    int length = 0;
    text[0] = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (i == used || !counts[i] || length >= static_cast<int>(capacity) - 48)
            continue;
        const int wrote = _snprintf_s(text + length, capacity - length, _TRUNCATE, "%s%s=%u", length ? ", " : "",
                                      name(i), counts[i]);
        if (wrote > 0)
            length += wrote;
    }
    return length;
}

// ---- THE TRACKER -----------------------------------------------------------------------------------------------
// HREK reach_tag_test.exe+0x954FD0 / retail +0x2EC164: void(tracker). tools/audit_caller_area.py 2EC164:8:v is
// clean -- it touches no caller slot past its home area and its one native caller keeps no volatile register
// across the call -- and it reads no register but rcx on entry.
using ReachTrackerReaderFn = void (__fastcall*)(void*);
constexpr size_t kReachTrackerUseCount = static_cast<size_t>(head_relative::Use::Count);
struct ReachTrackerHeadRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    void* target = nullptr;
    void* original = nullptr;                    // ReachTrackerReaderFn
    uint32_t headingOffset = 0, userOffset = 0;  // decoded from the game's own instructions at install
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> callbacks{0};
    // The window the report drains: every call lands in exactly one of these.
    std::atomic<uint32_t> uses[kReachTrackerUseCount]{};
    std::atomic<uint32_t> faults{0}, mixedReads{0};
    std::atomic<float> lastNativeDeg{0.0f}, lastHeadDeg{0.0f};
    std::atomic<uint32_t> lastThread{0};  // the thread the game last built the tracker on (the dev report prints it)
    head_relative::HeadingHold hold;  // the tracker update's thread only
    // The report's thread only.
    int reportedSwitch = -1;
    bool reportedFirstUse = false, reportedFirstRefusal = false;
} g_reachTrackerHead;

// The tracker update's thread, between the game's store of the heading and its first read. No allocation, no
// logging, no lock; every refusal leaves the game's own heading and is counted by reason.
__declspec(noinline) void ReachTrackerApplyHead(void* tracker) noexcept
{
    auto& feature = g_reachTrackerHead;
    head_relative::Use use = head_relative::Use::NoSample;
    __try
    {
        reach_head_relative::TrackerRequest request;
        request.switchOn = g_config.tracker_follows_head;
        request.sign = g_config.tracker_head_sign;
        request.offsetDeg = g_config.tracker_head_offset_deg;
        request.nowUs = Sight_NowUs();
        // This hook belongs to one load of the title; a sample from another is not this tracker's head.
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        request.generation = generation == feature.generation ? generation : 0;
        head_relative::HeadSample sample;
        bool cinematic = false;
        if (!ReachReadHeadSample(sample, cinematic))
        {
            sample = head_relative::HeadSample{};
            feature.mixedReads.fetch_add(1, std::memory_order_relaxed);
        }
        request.gameplay = !cinematic;
        auto* bytes = static_cast<uint8_t*>(tracker);
        request.outputUser = *reinterpret_cast<const int32_t*>(bytes + feature.userOffset);
        float* field = reinterpret_cast<float*>(bytes + feature.headingOffset);
        const float native = *field;
        float replaced = native, headYaw = 0.0f;
        use = reach_head_relative::TrackerField(request, sample, feature.hold, native, replaced, &headYaw);
        if (use == head_relative::Use::Ok)
        {
            *field = replaced;
            feature.lastNativeDeg.store(reach_head_relative::YawFromField(native) * head_relative::kRadToDeg,
                                        std::memory_order_relaxed);
            feature.lastHeadDeg.store(headYaw * head_relative::kRadToDeg, std::memory_order_relaxed);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        feature.faults.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    feature.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    feature.lastThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
#endif
}
__declspec(noinline) void __fastcall ReachTrackerHeadDetour(void* tracker)
{
    auto& feature = g_reachTrackerHead;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (tracker && feature.enabled.load(std::memory_order_acquire)
#if HALOMCCVR_DEV_DIAGNOSTICS
            && !(g_config.dev_sim_bisect & 256)
#endif
            )
            ReachTrackerApplyHead(tracker);
        if (const auto original = reinterpret_cast<ReachTrackerReaderFn>(feature.original))
            CallNativeWithSpareSlots(original, tracker);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}
bool RemoveReachTrackerHead()
{
    auto& feature = g_reachTrackerHead;
    return ReachHeadRelativeRetire("TRACKER", feature.enabled, feature.target, feature.original,
                                   reinterpret_cast<const void*>(&ReachTrackerHeadDetour), feature.callbacks);
}
// Cold, on the title's install thread, after the camera core. Binds nothing unless every link proves itself on
// this image; a refusal is the game's own tracker, said once.
bool InstallReachTrackerHead(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace reach_head_relative;
    auto& feature = g_reachTrackerHead;
    if (feature.target)
    {
        LOG("TRACKER HEAD Reach: StockFallback: the previous install was not retired");
        return false;
    }
    const uintptr_t site = ReachHeadRelativeUnique(base, size, kTrackerHeadingSitePattern);
    const uintptr_t updateHead = ReachHeadRelativeUnique(base, size, kTrackerUpdateHeadPattern);
    const uintptr_t firstHead = ReachHeadRelativeUnique(base, size, kTrackerFirstReaderHeadPattern);
    uint8_t siteBytes[kSiteBytes]{}, headBytes[kUpdateHeadBytes]{}, firstBytes[kFirstReaderBytes]{};
    uint8_t thirdBytes[kThirdReaderBytes]{}, halfPiBytes[sizeof(float)]{};
    const bool siteRead = ReachHeadRelativeRead(base, size, site, siteBytes, kSiteBytes);
    const bool headRead = ReachHeadRelativeRead(base, size, updateHead, headBytes, kUpdateHeadBytes);
    const bool firstRead = ReachHeadRelativeRead(base, size, firstHead, firstBytes, kFirstReaderBytes);
    int64_t firstEndRva = 0;
    const bool firstIsFunction =
        firstRead &&
        ReachHeadRelativeFunctionRva(base, firstHead, &firstEndRva) == static_cast<int64_t>(firstHead - base) &&
        firstEndRva >= static_cast<int64_t>(firstHead - base + kFirstReaderBytes);
    TrackerSites sites;
    TrackerBind bind = DecodeTrackerSites(siteRead ? siteBytes : nullptr, static_cast<int64_t>(site - base),
                                          headRead ? headBytes : nullptr, static_cast<int64_t>(updateHead - base),
                                          siteRead ? ReachHeadRelativeFunctionRva(base, site) : -1,
                                          firstIsFunction ? firstBytes : nullptr,
                                          static_cast<int64_t>(firstHead - base), sites);
    if (bind == TrackerBind::Bound)
    {
        const uintptr_t third = base + static_cast<uintptr_t>(sites.thirdReaderRva);
        const uintptr_t halfPi = base + static_cast<uintptr_t>(sites.halfPiRva);
        const bool readersRead = ReachHeadRelativeFunctionRva(base, third) == sites.thirdReaderRva &&
                                 ReachHeadRelativeRead(base, size, third, thirdBytes, kThirdReaderBytes) &&
                                 ReachHeadRelativeRead(base, size, halfPi, halfPiBytes, sizeof(float));
        bind = CheckTrackerReaders(firstBytes, readersRead ? thirdBytes : nullptr, halfPiBytes, sites);
    }
    if (bind != TrackerBind::Bound)
    {
        LOG("TRACKER HEAD Reach: StockFallback: %s (store %s, update %s, first reader %s); the game's own tracker",
            TrackerBindName(bind), site ? "matched" : "unmatched", updateHead ? "matched" : "unmatched",
            firstHead ? "matched" : "unmatched");
        return false;
    }
    feature.base = base;
    feature.generation = generation;
    feature.headingOffset = sites.headingOffset;
    feature.userOffset = sites.userOffset;
    feature.hold = head_relative::HeadingHold{};
    feature.reportedSwitch = -1;
    feature.reportedFirstUse = feature.reportedFirstRefusal = false;
    for (auto& count : feature.uses)
        count.store(0, std::memory_order_relaxed);
    feature.faults.store(0, std::memory_order_relaxed);
    feature.mixedReads.store(0, std::memory_order_relaxed);
    if (!ReachHeadRelativeHook("TRACKER", reinterpret_cast<void*>(firstHead),
                               reinterpret_cast<void*>(&ReachTrackerHeadDetour), feature.target, feature.original))
    {
        (void)RemoveReachTrackerHead();
        return false;
    }
    feature.enabled.store(true, std::memory_order_release);
    LOG("TRACKER HEAD Reach: Installed: the tracker's first heading reader haloreach.dll+0x%llX is hooked; the "
        "heading is tracker+0x%X (the game stores player control's desired yaw + pi/2 there at +0x%llX, and the "
        "other reader +0x%llX reads the same field), the tracker's user is +0x%X; follows the head for output "
        "user 0 in gameplay, from a head sample no older than %u ms; switch=%d sign=%d offset=%.1f deg",
        static_cast<unsigned long long>(sites.firstReaderRva), sites.headingOffset,
        static_cast<unsigned long long>(site - base + kSiteHeadingDisp - 4),
        static_cast<unsigned long long>(sites.thirdReaderRva), sites.userOffset,
        static_cast<unsigned>(head_relative::kMaxSampleAgeUs / 1000), g_config.tracker_follows_head ? 1 : 0,
        g_config.tracker_head_sign < 0 ? -1 : 1, g_config.tracker_head_offset_deg);
    return true;
}

// ---- THE SOUND LISTENER ----------------------------------------------------------------------------------------
// HREK reach_tag_test.exe+0xC71E0 (sound_manager.cpp) / retail +0x28820: void(bool) -- the bool only selects
// whether the platform's own listener properties are sent afterwards. tools/audit_caller_area.py 28820:8:v is
// clean: no caller slot past its home area, and none of its four native callers keeps a volatile register
// across the call or reads a return value.
using ReachListenerUpdateFn = void (__fastcall*)(uint8_t);
constexpr size_t kReachListenerUseCount = static_cast<size_t>(reach_head_relative::ListenerUse::Count);
struct ReachListenerHeadRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    void* target = nullptr;
    void* original = nullptr;                  // ReachListenerUpdateFn
    uint8_t* const* globalsSlot = nullptr;     // the image's own pointer to the sound manager's globals
    reach_head_relative::ListenerSites sites;  // decoded from the game's own instructions at install
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint32_t> uses[kReachListenerUseCount]{};
    std::atomic<uint32_t> faults{0}, mixedReads{0};
    std::atomic<float> lastNativeForward[3]{}, lastHeadForward[3]{}, lastHeadUp[3]{};
    // The report's thread only.
    int reportedSwitch = -1;
    bool reportedFirstUse = false, reportedFirstRefusal = false;
} g_reachListenerHead;

#if HALOMCCVR_DEV_DIAGNOSTICS
// WHICH CALL BUILDS THE LISTENER THE MIX USES (dev). The game rebuilds its listeners from four call sites
// (reach_head_relative_logic.h); which thread each runs on and their order within a frame are not known
// statically. Every build is counted by the call site it returned to and the thread it ran on, and the report
// prints the window, so one run says which builds happen, where, and that each one was turned.
constexpr int kReachListenerSiteCapacity = 8;
struct ReachListenerCallSites
{
    uintptr_t returns[kReachListenerSiteCapacity]{};  // written at install, before the hook is enabled
    int count = 0, found = 0;
    // Index kReachListenerSiteCapacity = a caller that is none of the direct call sites.
    std::atomic<uint32_t> calls[kReachListenerSiteCapacity + 1]{}, turned[kReachListenerSiteCapacity + 1]{};
    std::atomic<uint32_t> thread[kReachListenerSiteCapacity + 1]{};
} g_reachListenerCallSites;
// The return addresses of every direct `call` to `target` in the image's executable sections. Cold.
__declspec(noinline) int ReachHeadRelativeCallers(uintptr_t base, size_t size, uintptr_t target, uintptr_t* returns,
                                                 int capacity) noexcept
{
    int found = 0;
    __try
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;
            const uintptr_t begin = base + section->VirtualAddress;
            const size_t bytes = section->Misc.VirtualSize;
            if (bytes < 5 || !ReachHeadRelativeInside(base, size, begin, bytes))
                continue;
            const auto* code = reinterpret_cast<const uint8_t*>(begin);
            for (size_t i = 0; i + 5 <= bytes; ++i)
            {
                if (code[i] != 0xE8)
                    continue;
                int32_t rel = 0;
                memcpy(&rel, code + i + 1, sizeof(rel));
                if (begin + i + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(rel)) != target)
                    continue;
                if (found < capacity)
                    returns[found] = begin + i + 5;
                ++found;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return found;
}
void ReachListenerNoteBuild(uintptr_t caller, bool turned) noexcept
{
    auto& sites = g_reachListenerCallSites;
    int site = kReachListenerSiteCapacity;
    for (int i = 0; i < sites.count; ++i)
        if (sites.returns[i] == caller)
        {
            site = i;
            break;
        }
    sites.calls[site].fetch_add(1, std::memory_order_relaxed);
    if (turned)
        sites.turned[site].fetch_add(1, std::memory_order_relaxed);
    sites.thread[site].store(GetCurrentThreadId(), std::memory_order_relaxed);
}
#endif

// The sound update's thread, right after the game built its listeners and before it places a sound against
// them. Output user 0's listener only: its three rows and its velocity. No allocation, no logging, no lock;
// every refusal leaves the game's own listener and is counted by reason. `caller`: the game's call site this
// build returns to (dev builds; 0 otherwise).
__declspec(noinline) void ReachListenerApplyHead(uintptr_t caller) noexcept
{
    using namespace reach_head_relative;
    auto& feature = g_reachListenerHead;
    ListenerUse use = ListenerUse::NoSample;
    __try
    {
        ListenerRequest request;
        request.switchOn = g_config.audio_follows_head;
        request.nowUs = Sight_NowUs();
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        request.generation = generation == feature.generation ? generation : 0;
        head_relative::HeadSample sample;
        bool cinematic = false;
        if (!ReachReadHeadSample(sample, cinematic))
        {
            sample = head_relative::HeadSample{};
            feature.mixedReads.fetch_add(1, std::memory_order_relaxed);
        }
        request.gameplay = !cinematic;
        // Output user 0's listener is the first of the array: the globals + the header's offset.
        const ListenerSites& sites = feature.sites;
        uint8_t* globals = feature.globalsSlot ? *feature.globalsSlot : nullptr;
        uint8_t* matrix = globals ? globals + sites.matrixOffset : nullptr;
        uint8_t* velocity = globals ? globals + sites.velocityOffset : nullptr;
        ListenerRecord record;
        if (globals)
        {
            memcpy(&record.header, globals + sites.headerOffset, sizeof(record.header));
            memcpy(&record.footer, globals + sites.footerOffset, sizeof(record.footer));
            record.valid = globals[sites.validOffset];
            memcpy(&record.scale, matrix + kMatrixScale, sizeof(float));
            memcpy(record.basis.forward, matrix + kMatrixForward, sizeof(record.basis.forward));
            memcpy(record.basis.left, matrix + kMatrixLeft, sizeof(record.basis.left));
            memcpy(record.basis.up, matrix + kMatrixUp, sizeof(record.basis.up));
            memcpy(record.velocity, velocity, sizeof(record.velocity));
        }
        ListenerBasis basis;
        float local[3]{};
        use = ListenerOrientation(request, sample, record, basis, local);
        if (use == ListenerUse::Used && globals)
        {
            memcpy(matrix + kMatrixForward, basis.forward, sizeof(basis.forward));
            memcpy(matrix + kMatrixLeft, basis.left, sizeof(basis.left));
            memcpy(matrix + kMatrixUp, basis.up, sizeof(basis.up));
            memcpy(velocity, local, sizeof(local));
            for (int i = 0; i < 3; ++i)
            {
                feature.lastNativeForward[i].store(record.basis.forward[i], std::memory_order_relaxed);
                feature.lastHeadForward[i].store(basis.forward[i], std::memory_order_relaxed);
                feature.lastHeadUp[i].store(basis.up[i], std::memory_order_relaxed);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        feature.faults.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    feature.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    ReachListenerNoteBuild(caller, use == ListenerUse::Used);
#else
    (void)caller;
#endif
}
__declspec(noinline) void __fastcall ReachListenerHeadDetour(uint8_t sendPlatformProperties)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
#else
    const uintptr_t caller = 0;
#endif
    auto& feature = g_reachListenerHead;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        // The game builds every listener exactly as it always does; only then is user 0's turned to the head.
        if (const auto original = reinterpret_cast<ReachListenerUpdateFn>(feature.original))
            CallNativeWithSpareSlots(original, sendPlatformProperties);
        if (feature.enabled.load(std::memory_order_acquire)
#if HALOMCCVR_DEV_DIAGNOSTICS
            && !(g_config.dev_sim_bisect & 256)
#endif
            )
            ReachListenerApplyHead(caller);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}
bool RemoveReachListenerHead()
{
    auto& feature = g_reachListenerHead;
    return ReachHeadRelativeRetire("AUDIO", feature.enabled, feature.target, feature.original,
                                   reinterpret_cast<const void*>(&ReachListenerHeadDetour), feature.callbacks);
}
bool InstallReachListenerHead(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace reach_head_relative;
    auto& feature = g_reachListenerHead;
    if (feature.target)
    {
        LOG("AUDIO HEAD Reach: StockFallback: the previous install was not retired");
        return false;
    }
    const uintptr_t head = ReachHeadRelativeUnique(base, size, kListenerUpdateHeadPattern);
    const uintptr_t store = ReachHeadRelativeUnique(base, size, kListenerStoreSitePattern);
    const uintptr_t writer = ReachHeadRelativeUnique(base, size, kListenerSignatureWriterPattern);
    uint8_t headBytes[kListenerHeadBytes]{}, storeBytes[kListenerStoreBytes]{}, writerBytes[kListenerWriterBytes]{};
    uint8_t builderBytes[kMatrixBuilderBytes]{}, transformBytes[kVelocityTransformBytes]{};
    const bool headRead = ReachHeadRelativeRead(base, size, head, headBytes, kListenerHeadBytes) &&
                          ReachHeadRelativeFunctionRva(base, head) == static_cast<int64_t>(head - base);
    const bool storeRead = ReachHeadRelativeRead(base, size, store, storeBytes, kListenerStoreBytes);
    const bool writerRead = ReachHeadRelativeRead(base, size, writer, writerBytes, kListenerWriterBytes);
    ListenerSites sites;
    ListenerBind bind = DecodeListenerSites(headRead ? headBytes : nullptr, static_cast<int64_t>(head - base),
                                            storeRead ? storeBytes : nullptr, static_cast<int64_t>(store - base),
                                            storeRead ? ReachHeadRelativeFunctionRva(base, store) : -1,
                                            writerRead ? writerBytes : nullptr, sites);
    if (bind == ListenerBind::Bound)
    {
        const uintptr_t builder = base + static_cast<uintptr_t>(sites.builderRva);
        const uintptr_t transform = base + static_cast<uintptr_t>(sites.transformRva);
        const uintptr_t slot = base + static_cast<uintptr_t>(sites.globalsRva);
        const bool helpersRead = ReachHeadRelativeInside(base, size, slot, sizeof(void*)) &&
                                 ReachHeadRelativeFunctionRva(base, builder) == sites.builderRva &&
                                 ReachHeadRelativeFunctionRva(base, transform) == sites.transformRva &&
                                 ReachHeadRelativeRead(base, size, builder, builderBytes, kMatrixBuilderBytes) &&
                                 ReachHeadRelativeRead(base, size, transform, transformBytes, kVelocityTransformBytes);
        bind = CheckListenerHelpers(helpersRead ? builderBytes : nullptr, helpersRead ? transformBytes : nullptr);
    }
    if (bind != ListenerBind::Bound)
    {
        LOG("AUDIO HEAD Reach: StockFallback: %s (update %s, store %s, signature writer %s); the game's own listener",
            ListenerBindName(bind), head ? "matched" : "unmatched", store ? "matched" : "unmatched",
            writer ? "matched" : "unmatched");
        return false;
    }
    feature.base = base;
    feature.generation = generation;
    feature.sites = sites;
    feature.globalsSlot = reinterpret_cast<uint8_t* const*>(base + static_cast<uintptr_t>(sites.globalsRva));
    feature.reportedSwitch = -1;
    feature.reportedFirstUse = feature.reportedFirstRefusal = false;
    for (auto& count : feature.uses)
        count.store(0, std::memory_order_relaxed);
    feature.faults.store(0, std::memory_order_relaxed);
    feature.mixedReads.store(0, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        auto& callSites = g_reachListenerCallSites;
        callSites.found = ReachHeadRelativeCallers(base, size, head, callSites.returns, kReachListenerSiteCapacity);
        callSites.count = callSites.found < kReachListenerSiteCapacity ? callSites.found : kReachListenerSiteCapacity;
        for (int i = 0; i <= kReachListenerSiteCapacity; ++i)
        {
            callSites.calls[i].store(0, std::memory_order_relaxed);
            callSites.turned[i].store(0, std::memory_order_relaxed);
            callSites.thread[i].store(0, std::memory_order_relaxed);
        }
    }
#endif
    if (!ReachHeadRelativeHook("AUDIO", reinterpret_cast<void*>(head),
                               reinterpret_cast<void*>(&ReachListenerHeadDetour), feature.target, feature.original))
    {
        (void)RemoveReachListenerHead();
        return false;
    }
    feature.enabled.store(true, std::memory_order_release);
    LOG("AUDIO HEAD Reach: Installed: the sound manager's listener update haloreach.dll+0x%llX is hooked; output "
        "user 0's listener is the sound globals (pointer at +0x%llX) + 0x%X, 0x%X long between the game's own "
        "'lshd' and 'lsft' signatures, its matrix at +0x%X and its velocity at +0x%X; after the game builds it "
        "from the observer camera (the aim), its rows become the head's forward, left and up in gameplay, from a "
        "head sample no older than %u ms; its position is never changed; switch=%d",
        static_cast<unsigned long long>(head - base), static_cast<unsigned long long>(sites.globalsRva),
        sites.headerOffset, sites.stride, sites.matrixOffset, sites.velocityOffset,
        static_cast<unsigned>(head_relative::kMaxSampleAgeUs / 1000), g_config.audio_follows_head ? 1 : 0);
    return true;
}

// ---- both, for the title's lifecycle ---------------------------------------------------------------------------
// Cold, on the title's install thread, after the camera core. Each seam proves and binds itself alone.
bool InstallReachHeadRelative(uintptr_t base, size_t size, uint32_t generation)
{
    if (!base || !generation || size < 0x1000)
    {
        LOG("TRACKER HEAD Reach: StockFallback: no title image or generation");
        LOG("AUDIO HEAD Reach: StockFallback: no title image or generation");
        return false;
    }
    const bool tracker = InstallReachTrackerHead(base, size, generation);
    const bool listener = InstallReachListenerHead(base, size, generation);
    return tracker || listener;
}
// At title unload, before the camera core goes. False = a detour could not be retired yet; the caller retries.
bool RemoveReachHeadRelative()
{
    const bool tracker = RemoveReachTrackerHead();
    const bool listener = RemoveReachListenerHead();
    return tracker && listener;
}

const char* ReachTrackerUseName(size_t use) { return head_relative::UseName(static_cast<head_relative::Use>(use)); }
const char* ReachListenerUseName(size_t use)
{
    return reach_head_relative::ListenerUseName(static_cast<reach_head_relative::ListenerUse>(use));
}
// Degrees of a unit forward: its heading counter-clockwise from +X, and its elevation.
void ReachHeadRelativeAngles(const float forward[3], float& yawDeg, float& pitchDeg)
{
    yawDeg = atan2f(forward[1], forward[0]) * head_relative::kRadToDeg;
    pitchDeg = asinf(Clamp(forward[2], -1.0f, 1.0f)) * head_relative::kRadToDeg;
}

// The worker's two-second report. Every build: one line when a switch changes, and one the first time the head
// is used (or first refused) after an install. Dev builds: the window's counts.
void ReportReachTrackerHead()
{
    auto& feature = g_reachTrackerHead;
    if (!feature.target)
        return;
    using head_relative::Use;
    const int switchNow = g_config.tracker_follows_head ? 1 : 0;
    if (feature.reportedSwitch != switchNow)
    {
        if (feature.reportedSwitch >= 0)
            LOG("TRACKER HEAD Reach: switch %s: the tracker now turns with %s", switchNow ? "on" : "off",
                switchNow ? "your head" : "the game's own heading (the aim)");
        feature.reportedSwitch = switchNow;
    }
    uint32_t uses[kReachTrackerUseCount]{}, refused = 0;
    for (size_t i = 0; i < kReachTrackerUseCount; ++i)
    {
        uses[i] = feature.uses[i].exchange(0, std::memory_order_relaxed);
        if (i != static_cast<size_t>(Use::Ok))
            refused += uses[i];
    }
    const uint32_t used = uses[static_cast<size_t>(Use::Ok)];
    const uint32_t faults = feature.faults.exchange(0, std::memory_order_relaxed);
    const uint32_t mixed = feature.mixedReads.exchange(0, std::memory_order_relaxed);
    if (!used && !refused && !faults)
        return;
    char reasons[256];
    const int length = ReachHeadRelativeReasons(reasons, sizeof(reasons), uses, kReachTrackerUseCount,
                                                static_cast<size_t>(Use::Ok), &ReachTrackerUseName);
    if (used && !feature.reportedFirstUse)
    {
        feature.reportedFirstUse = true;
        LOG("TRACKER HEAD Reach: following the head: first contacts placed by the head's heading (the game's "
            "heading was %.1f deg, the head's %.1f deg)",
            feature.lastNativeDeg.load(std::memory_order_relaxed),
            feature.lastHeadDeg.load(std::memory_order_relaxed));
    }
    if (!used && !feature.reportedFirstRefusal)
    {
        feature.reportedFirstRefusal = true;
        LOG("TRACKER HEAD Reach: the game's own tracker for now: %s%s", length ? reasons : "a fault",
            faults && length ? ", and faults" : "");
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    LOG("TRACKER HEAD Reach (dev): substituted %u; the game's heading %.1f deg, the head's %.1f deg (last); "
        "refused %u: %s; faults %u, mixed sample reads %u; built on thread %u, the head published on thread %u; "
        "switch=%d sign=%d offset=%.1f deg",
        used, feature.lastNativeDeg.load(std::memory_order_relaxed),
        feature.lastHeadDeg.load(std::memory_order_relaxed), refused, length ? reasons : "none", faults, mixed,
        feature.lastThread.load(std::memory_order_relaxed),
        g_reachHeadPublication.thread.load(std::memory_order_relaxed), switchNow,
        g_config.tracker_head_sign < 0 ? -1 : 1, g_config.tracker_head_offset_deg);
#else
    (void)mixed;
#endif
}
void ReportReachListenerHead()
{
    using reach_head_relative::ListenerUse;
    auto& feature = g_reachListenerHead;
    if (!feature.target)
        return;
    const int switchNow = g_config.audio_follows_head ? 1 : 0;
    if (feature.reportedSwitch != switchNow)
    {
        if (feature.reportedSwitch >= 0)
            LOG("AUDIO HEAD Reach: switch %s: sound is now heard from %s", switchNow ? "on" : "off",
                switchNow ? "your head's orientation" : "the game's own camera (the aim)");
        feature.reportedSwitch = switchNow;
    }
    uint32_t uses[kReachListenerUseCount]{}, refused = 0;
    for (size_t i = 0; i < kReachListenerUseCount; ++i)
    {
        uses[i] = feature.uses[i].exchange(0, std::memory_order_relaxed);
        if (i != static_cast<size_t>(ListenerUse::Used))
            refused += uses[i];
    }
    const uint32_t used = uses[static_cast<size_t>(ListenerUse::Used)];
    const uint32_t faults = feature.faults.exchange(0, std::memory_order_relaxed);
    const uint32_t mixed = feature.mixedReads.exchange(0, std::memory_order_relaxed);
    if (!used && !refused && !faults)
        return;
    char reasons[320];
    const int length = ReachHeadRelativeReasons(reasons, sizeof(reasons), uses, kReachListenerUseCount,
                                                static_cast<size_t>(ListenerUse::Used), &ReachListenerUseName);
    float nativeForward[3], headForward[3], headUp[3];
    for (int i = 0; i < 3; ++i)
    {
        nativeForward[i] = feature.lastNativeForward[i].load(std::memory_order_relaxed);
        headForward[i] = feature.lastHeadForward[i].load(std::memory_order_relaxed);
        headUp[i] = feature.lastHeadUp[i].load(std::memory_order_relaxed);
    }
    float nativeYaw = 0.0f, nativePitch = 0.0f, headYaw = 0.0f, headPitch = 0.0f;
    ReachHeadRelativeAngles(nativeForward, nativeYaw, nativePitch);
    ReachHeadRelativeAngles(headForward, headYaw, headPitch);
    // The head's roll: how far its up leans from the upright up of the same forward (0 = level).
    const float headRoll = asinf(Clamp(headUp[0] * headForward[1] - headUp[1] * headForward[0], -1.0f, 1.0f)) *
                           head_relative::kRadToDeg;
    if (used && !feature.reportedFirstUse)
    {
        feature.reportedFirstUse = true;
        LOG("AUDIO HEAD Reach: following the head: the listener now faces yaw %.1f pitch %.1f deg, lean %.1f deg "
            "(the game's own listener faced yaw %.1f pitch %.1f deg)",
            headYaw, headPitch, headRoll, nativeYaw, nativePitch);
    }
    if (!used && !feature.reportedFirstRefusal)
    {
        feature.reportedFirstRefusal = true;
        LOG("AUDIO HEAD Reach: the game's own listener for now: %s%s", length ? reasons : "a fault",
            faults && length ? ", and faults" : "");
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    LOG("AUDIO HEAD Reach (dev): turned %u; the game's listener yaw %.1f pitch %.1f deg, the head's yaw %.1f "
        "pitch %.1f lean %.1f deg (last); refused %u: %s; faults %u, mixed sample reads %u; switch=%d",
        used, nativeYaw, nativePitch, headYaw, headPitch, headRoll, refused, length ? reasons : "none", faults,
        mixed, switchNow);
    // The builds of this window by the game's call site: which ran, how often, on which thread, how many turned.
    auto& callSites = g_reachListenerCallSites;
    char builds[512];
    int written = 0;
    builds[0] = 0;
    for (int i = 0; i <= kReachListenerSiteCapacity; ++i)
    {
        const uint32_t calls = callSites.calls[i].exchange(0, std::memory_order_relaxed);
        const uint32_t turned = callSites.turned[i].exchange(0, std::memory_order_relaxed);
        if (!calls || written >= static_cast<int>(sizeof(builds)) - 96)
            continue;
        const bool known = i < callSites.count;
        const int wrote = _snprintf_s(
            builds + written, sizeof(builds) - written, _TRUNCATE, "%s%s+0x%llX x%u (thread %u, turned %u)",
            written ? ", " : "", known ? "returning to haloreach.dll" : "another caller ",
            static_cast<unsigned long long>(known ? callSites.returns[i] - feature.base : 0), calls,
            callSites.thread[i].load(std::memory_order_relaxed), turned);
        if (wrote > 0)
            written += wrote;
    }
    LOG("AUDIO HEAD Reach (dev): listener builds by call site (%d direct call sites in the image): %s",
        callSites.found, written ? builds : "none");
#else
    (void)mixed;
#endif
}
void ReportReachHeadRelative()
{
    ReportReachTrackerHead();
    ReportReachListenerHead();
}
