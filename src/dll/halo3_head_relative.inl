// Included in game.cpp (inside its anonymous namespace) after halo3_avatar.inl. Declared ahead of ApplyHeadLook, which
// publishes the head (Halo3HeadRelativePublish); installed and retired with the Halo 3 core (InstallHook,
// RemoveInstalledGameHooks); reported by the cold worker (Halo3HeadRelativeReport).
//
// THE MOTION TRACKER BY THE HEAD'S HEADING, Halo 3 (src/common/halo3_head_relative_logic.h has the mechanism, the
// signatures and the arithmetic; docs/RE-notes.md "Halo 3 motion tracker" the evidence).
//
// The player aims with the hand and looks with the head. Halo 3's tracker turns its contacts by ONE heading, the
// player-control facing yaw, which direct drive steers with the hand: sweep the gun and every contact swings, turn
// the head and nothing moves. The tracker update (halo3 +0x32DA98) turns that yaw into a sine / cosine pair and hands
// it to the two builders that place a contact (+0x32D9A8 a world point, +0x32D680 an object), which nothing else
// calls. The detours on those two builders replace the pair with the pair of the HEAD's heading.
//
//   * WHAT IT CANNOT TOUCH. It reads one integer of the tracker's own state (the output user) and rewrites two float
//     ARGUMENTS of the tracker's own builders. No write to the game at all: not player control, not the aim record,
//     not the observer or any camera, nothing a shot, the firing assist, the view or the network reads.
//   * THE HEAD crosses threads (the two clocks): ApplyHeadLook, on the camera thread, publishes the forward and up
//     it has just written into the view (world space), stamped on the sight clock with the title generation, and
//     whether that frame was player-controlled. The builders run where the HUD updates. A sample older than
//     100 ms, from another generation or from a cinematic frame is refused.
//   * EVERY REFUSAL IS THE GAME'S OWN PAIR, untouched, counted by reason (head_relative::Use): the switch off, not
//     output user 0, no / stale / foreign sample, a cinematic, no heading. Identity and freshness only.
//   * PROOF (addrcascade: resolved + guarded, nothing hardcoded): nine signatures, each unique in the image; both
//     hook targets proven twice (the update's call operand and the builder's own entry pattern at that address);
//     the argument slots and the placement arithmetic are in the patterns; ValueAgreement: the pi/2 constant, the
//     sign mask, the two trig calls resolve to the C runtime's sinf and cosf (in that order), and the update's unit
//     call is the output-user unit getter the vehicle lane bound independently. Any miss: StockFallback, one line.
//   * LTCG: `py -3 tools/audit_caller_area.py halo3.dll 32D9A8:10:i 32D680:11:i` is clean (the builders read only
//     their declared stack arguments; +0x32D9A8 has one call site, +0x32DBC3, and +0x32D680 two, +0x32DC4E and
//     +0x32DD2E, all inside the tracker update, and none keeps a volatile register across the call); both
//     detours forward through CallNativeWithSpareSlots.
// Cost per contact: one seqlock read, one clock read, a dozen float operations. No allocation, no logging.
//
// THE SOUND LISTENER BY THE HEAD'S ORIENTATION, Halo 3 (the second half of this file; the same header has its
// mechanism). The sound manager's listener update (halo3 +0xAB650) rebuilds listener 0 from the observer's forward
// and up three times per loop pass, the last time after observer_update has rebuilt that camera from the game's own
// aim and before the render in which this mod writes the head: the listener turns with the gun. One detour on that
// update runs the game's own first and then rewrites listener 0's basis with the head's (forward, left = up x
// forward, up) and re-expresses the listener-relative velocity, so the world velocity is unchanged. Position,
// location, scale and the other listeners are untouched; only the sound manager's own record is written. Every
// call is rewritten (the rewrite is idempotent from one head sample). Same gates and refusals as the tracker, under
// audio_follows_head, plus a guard on the record itself: scale exactly 1 and rows orthonormal with left = up x
// forward, or nothing is written. `py -3 tools/audit_caller_area.py halo3.dll AB650:8:v` is clean (no caller-area
// slot touched; four native callers, none keeps a volatile register or reads a return value).

using Halo3TrackerPointFn = bool(__fastcall*)(void* self, const float* point, void* blip, const float* origin,
                                               float sinA, float cosA);
using Halo3TrackerObjectFn = bool(__fastcall*)(void* self, int slot, void* blip, const float* origin,
                                                const float* point, float sinA, float cosA);

// The head, as the camera thread last rendered with it (seqlock; publishers take turns through `writing`).
struct HeadRelativePublication
{
    std::atomic<bool> writing{false};
    std::atomic<uint32_t> seq{0};
    std::atomic<float> forward[3]{}, up[3]{};
    std::atomic<uint64_t> stampUs{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<bool> gameplay{false}, valid{false};
};

struct Halo3HeadRelativeLane
{
    const char* title = "Halo 3";
    const std::atomic<uint32_t>* runtimeGeneration = nullptr;
    // The hooks (worker-owned; the detours read `ready` and the originals).
    void* pointTarget = nullptr;
    void* objectTarget = nullptr;
    Halo3TrackerPointFn pointOriginal = nullptr;
    Halo3TrackerObjectFn objectOriginal = nullptr;
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> ready{false};
    int32_t userOffset = 0;   // the output user index in the tracker state, decoded at install
    // THE PLAYER'S OWN CONTACT (halo3_head_relative_logic.h, WHICH CONTACT IS THE PLAYER'S OWN): the title's
    // output-user unit getter (the lane's owner points this at its global; null there = not bound), and whether
    // the install proved both that the update calls that very getter and the tracked entry's layout. Without
    // the proof no contact is centred.
    int (__fastcall* const* unitGetter)(int) = nullptr;
    bool ownUnitProven = false;
    // The getter the install proved the update calls: the detour calls THIS pointer, never the global again (the
    // worker nulls and re-binds that global at a re-resolve). Null whenever the lane is not installed.
    std::atomic<int (__fastcall*)(int)> provenUnitGetter{nullptr};
    HeadRelativePublication head;
    // Verdicts, counted where the builders run; the cold worker reads and clears them.
    std::atomic<uint32_t> uses[static_cast<size_t>(head_relative::Use::Count)]{};
    std::atomic<uint32_t> faults{0};
    std::atomic<float> lastNative[2]{}, lastUsed[2]{};   // the two headings of the last substitution (x, y)
#if HALOMCCVR_DEV_DIAGNOSTICS
    // WHERE IS THE TRACKER CENTRED (dev)? The update's first object contact is the player's own unit (slot 0,
    // no point). Its blip as the builder placed it, in fractions of the radius: x right, y DOWN. (0, 0) means
    // the tracker is centred on the unit. The game centres it on the unit's HEAD MARKER (the update calls
    // +0x355460, marker 0x9F), which the animated skeleton carries away from the body: the player then sees
    // the dot that stands for them sitting below the centre.
    std::atomic<float> ownBlipX{0.0f}, ownBlipY{0.0f}, ownRangeWu{0.0f};
    std::atomic<uint32_t> ownBlips{0};
#endif
    // Cold worker only.
    int reportedSwitch = -1;
    int reportedState = -1;
    uint64_t reportMs = 0;
};
Halo3HeadRelativeLane g_halo3HeadRelative;

// The last heading the head had, on the thread that builds the tracker.
thread_local halo3_head_relative::Hold t_halo3TrackerHold;

// Camera thread: the view this frame renders with. `forward` / `up` null: the frame is not the player's (a
// cutscene theatre), published so the tracker's refusal names it.
void HeadRelativePublishTo(HeadRelativePublication& p, uint32_t generation, const float* forward, const float* up,
                           bool gameplay)
{
    if (!generation || p.writing.exchange(true, std::memory_order_acquire))
        return;
    const bool vectors = forward && up;
    const bool finite = !vectors || (head_relative::Finite3(forward) && head_relative::Finite3(up));
    p.seq.fetch_add(1, std::memory_order_acq_rel);
    for (int i = 0; i < 3; ++i)
    {
        p.forward[i].store(vectors && finite ? forward[i] : 0.0f, std::memory_order_relaxed);
        p.up[i].store(vectors && finite ? up[i] : 0.0f, std::memory_order_relaxed);
    }
    p.stampUs.store(Sight_NowUs(), std::memory_order_relaxed);
    p.generation.store(generation, std::memory_order_relaxed);
    p.gameplay.store(gameplay && vectors && finite, std::memory_order_relaxed);
    p.valid.store(finite, std::memory_order_relaxed);
    p.seq.fetch_add(1, std::memory_order_release);
    p.writing.store(false, std::memory_order_release);
}

// Any thread: a coherent copy of the last published head, or false while a publisher is mid-write.
bool HeadRelativeRead(const HeadRelativePublication& p, head_relative::HeadSample& out, bool& gameplay)
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const uint32_t before = p.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        for (int i = 0; i < 3; ++i)
        {
            out.forward[i] = p.forward[i].load(std::memory_order_relaxed);
            out.up[i] = p.up[i].load(std::memory_order_relaxed);
        }
        out.stampUs = p.stampUs.load(std::memory_order_relaxed);
        out.generation = p.generation.load(std::memory_order_relaxed);
        out.valid = p.valid.load(std::memory_order_relaxed);
        gameplay = p.gameplay.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (p.seq.load(std::memory_order_relaxed) == before)
            return true;
    }
    return false;
}

void Halo3HeadRelativePublish(const float* forward, const float* up, bool gameplay)
{
    HeadRelativePublishTo(g_halo3HeadRelative.head, g_halo3RuntimeGeneration.load(std::memory_order_acquire), forward,
                          up, gameplay);
}

namespace
{
// The output user the tracker state belongs to. Guarded: the state is the game's own argument.
bool Halo3TrackerUser(const void* self, int32_t offset, int32_t& user)
{
    __try
    {
        if (!self)
            return false;
        user = *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(self) + offset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// One contact placement: the pair to hand the builder (the game's own unless every gate passes). `hold`: the last
// heading the head had, the calling title's own, on this thread. Returns whether this is the local player's
// tracker with the player's switch on: all the own-blip rule needs.
bool Halo3TrackerPair(Halo3HeadRelativeLane& lane, halo3_head_relative::Hold& hold, const void* self, float& sinA,
                      float& cosA)
{
    halo3_head_relative::Request in{};
    in.switchOn = lane.ready.load(std::memory_order_acquire) && g_config.tracker_follows_head;
    if (in.switchOn)
    {
        if (!Halo3TrackerUser(self, lane.userOffset, in.user))
        {
            in.user = -1;
            lane.faults.fetch_add(1, std::memory_order_relaxed);
        }
        if (in.user == 0)
        {
            in.sampleRead = HeadRelativeRead(lane.head, in.sample, in.gameplay);
            in.generation = lane.runtimeGeneration ? lane.runtimeGeneration->load(std::memory_order_acquire) : 0;
            in.nowUs = Sight_NowUs();
            in.sign = g_config.tracker_head_sign;
            in.offsetRad = g_config.tracker_head_offset_deg * head_relative::kDegToRad;
        }
    }
    float native[3], used[3];
    const head_relative::Use use = halo3_head_relative::Substitute(in, hold, sinA, cosA, native, used);
    lane.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
    if (use == head_relative::Use::Ok)
        for (int i = 0; i < 2; ++i)
        {
            lane.lastNative[i].store(native[i], std::memory_order_relaxed);
            lane.lastUsed[i].store(used[i], std::memory_order_relaxed);
        }
    return in.switchOn && in.user == 0;
}
} // namespace

// Is the tracked entry in `slot` the output user's own unit? Guarded: the entry is the game's own state, and the
// getter is the game's own function (the one the update called for this user a moment ago, on this thread).
// ADDR-HYGIENE: guarded -- the entry's stride and handle offset are the builder's own bytes, compared at install
// (kObjectSlotEntryBytes); the getter is the bound one only if the update's unit call lands on it.
__declspec(noinline) bool Halo3TrackerSlotIsOwn(Halo3HeadRelativeLane& lane, const void* self, int slot) noexcept
{
    using namespace halo3_head_relative;
    if (!self || slot < 0 || slot >= kTrackedEntries)
        return false;
    const auto getter = lane.provenUnitGetter.load(std::memory_order_acquire);
    if (!getter)
        return false;
    __try
    {
        const int32_t entry = *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(self) +
                                                              static_cast<size_t>(slot) * kTrackedEntryBytes);
        return entry != -1 && SlotIsOwnUnit(slot, entry, getter(0));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        lane.faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// ADDR-HYGIENE: dev-only (a diagnostic read, never a write, guarded). The blip's x / y are its first two
// floats (the point builder's own stores at +0x32DA6C / +0x32DA85) and the range is the float both builders
// read at state + 0xCBC, in world units.
__declspec(noinline) void Halo3TrackerNoteOwnBlip(Halo3HeadRelativeLane& lane, const void* self,
                                                  const void* blip) noexcept
{
    __try
    {
        if (!self || !blip)
            return;
        const float* placed = static_cast<const float*>(blip);
        const float range = *reinterpret_cast<const float*>(static_cast<const uint8_t*>(self) + 0xCBC);
        if (!std::isfinite(placed[0]) || !std::isfinite(placed[1]) || !std::isfinite(range))
            return;
        lane.ownBlipX.store(placed[0], std::memory_order_relaxed);
        lane.ownBlipY.store(placed[1], std::memory_order_relaxed);
        lane.ownRangeWu.store(range, std::memory_order_relaxed);
        lane.ownBlips.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
#endif

__declspec(noinline) bool __fastcall Halo3TrackerPointDetour(void* self, const float* point, void* blip,
                                                             const float* origin, float sinA, float cosA)
{
    auto& lane = g_halo3HeadRelative;
    lane.callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool placed = false;
    __try
    {
        Halo3TrackerPair(lane, t_halo3TrackerHold, self, sinA, cosA);
        if (lane.pointOriginal)
            placed = CallNativeWithSpareSlots(lane.pointOriginal, self, point, blip, origin, sinA, cosA);
    }
    __finally
    {
        lane.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return placed;
}

__declspec(noinline) bool __fastcall Halo3TrackerObjectDetour(void* self, int slot, void* blip, const float* origin,
                                                              const float* point, float sinA, float cosA)
{
    auto& lane = g_halo3HeadRelative;
    lane.callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool placed = false;
    __try
    {
        const bool own = Halo3TrackerPair(lane, t_halo3TrackerHold, self, sinA, cosA);
        // The player's own blip goes AT the centre (halo3_head_relative_logic.h, OwnBlipPoint): the game measures it
        // from the unit's head marker to the body, 0.4 m and more off the centre. Own = the tracked entry that
        // holds the player's unit, whichever of the twenty slots that is; never another unit that sits in slot 0.
        const bool ownSlot = own && !point && Halo3TrackerSlotIsOwn(lane, self, slot);
        const float* placedPoint = halo3_head_relative::OwnBlipPoint(own, ownSlot, point, origin);
        if (lane.objectOriginal)
            placed = CallNativeWithSpareSlots(lane.objectOriginal, self, slot, blip, origin, placedPoint, sinA, cosA);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (placed && ownSlot)
            Halo3TrackerNoteOwnBlip(lane, self, blip);
#endif
    }
    __finally
    {
        lane.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return placed;
}

// Worker: retire both hooks (disable, prove nothing is inside, remove). Idempotent; false = try again.
bool RemoveHeadRelativeTracker(Halo3HeadRelativeLane& lane, const void* pointDetour, const void* objectDetour)
{
    lane.ready.store(false, std::memory_order_release);
    lane.provenUnitGetter.store(nullptr, std::memory_order_release);
    if (!lane.pointTarget && !lane.objectTarget)
        return true;
    for (void* target : {lane.pointTarget, lane.objectTarget})
    {
        if (!target)
            continue;
        const auto status = MCCVR_DisableHookForRetirement(target);
        if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
        {
            LOG("TRACKER HEAD %s CleanupRequired: a tracker hook did not disable (%d)", lane.title,
                static_cast<int>(status));
            return false;
        }
    }
    const void* functions[]{pointDetour, objectDetour};
    const void* trampolines[]{reinterpret_cast<const void*>(lane.pointOriginal),
                              reinterpret_cast<const void*>(lane.objectOriginal)};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 2, lane.callbacks))
    {
        LOG("TRACKER HEAD %s CleanupRequired: the tracker detours are busy", lane.title);
        return false;
    }
    for (void** target : {&lane.pointTarget, &lane.objectTarget})
    {
        if (!*target)
            continue;
        const auto removed = MH_RemoveHook(*target);
        if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
        {
            LOG("TRACKER HEAD %s CleanupRequired: a tracker hook was not removed (%d)", lane.title,
                static_cast<int>(removed));
            return false;
        }
        *target = nullptr;
    }
    lane.pointOriginal = nullptr;
    lane.objectOriginal = nullptr;
    return true;
}

namespace
{
// What the install decoded from the image.
struct Halo3TrackerProof
{
    uintptr_t update = 0, point = 0, object = 0;
    int32_t userOffset = 0;
    bool unitGetterAgrees = false;   // the update's unit call is the getter the vehicle lane bound
    bool slotEntryAgrees = false;    // the object builder indexes its tracked entry as kObjectSlotEntryBytes says
    const char* trig = "";           // how sinf / cosf were proven
};

// `function`(x) against the C runtime's own sine or cosine at two angles. Guarded: `function` is a pointer read
// from the title's import table.
bool Halo3TrackerTrigAnswers(float (*function)(float), bool sine)
{
    const float angles[2]{0.5f, -2.25f};
    __try
    {
        for (int i = 0; i < 2; ++i)
        {
            const float expected = sine ? std::sin(angles[i]) : std::cos(angles[i]);
            if (!(std::fabs(function(angles[i]) - expected) <= 1.0e-5f))
                return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Every site of the tracker's heading, from the game's own bytes. Null = proven; otherwise what did not prove.
// Every read is of matched pattern bytes or of an address checked to lie inside the image first.
const char* Halo3TrackerProve(uintptr_t base, size_t size, uintptr_t unitGetter, Halo3TrackerProof& out)
{
    namespace t = halo3_head_relative;
    const auto unique = [&](const char* pattern) -> uintptr_t {
        const uintptr_t hit = sig::Find(base, size, pattern);
        return hit && !sig::Find(hit + 1, base + size - hit - 1, pattern) ? hit : 0;
    };
    const auto inside = [&](uintptr_t address, size_t bytes) {
        return address >= base && address <= base + size && bytes <= base + size - address;
    };
    const auto i32 = [](uintptr_t at) {
        int32_t v = 0;
        memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v));
        return v;
    };
    // The target of the E8 at `at` (a matched byte), or 0.
    const auto callTarget = [&](uintptr_t at) -> uintptr_t {
        if (*reinterpret_cast<const uint8_t*>(at) != 0xE8)
            return 0;
        const uintptr_t target = at + 5 + i32(at + 1);
        return inside(target, 16) ? target : 0;
    };
    const auto within = [](uintptr_t site, uintptr_t entry, uint32_t extent) {
        return site > entry && site - entry < extent;
    };

    const uintptr_t update = unique(t::kUpdateEntryPattern);
    if (!update)
        return "the tracker update's entry";
    const uintptr_t trig = unique(t::kTrigPattern);
    if (!trig || !within(trig, update, t::kUpdateExtent))
        return "the heading's sine / cosine inside the update";
    const uintptr_t pointCall = unique(t::kPointCallPattern);
    const uintptr_t objectCallA = unique(t::kObjectCallAPattern);
    const uintptr_t objectCallB = unique(t::kObjectCallBPattern);
    if (!pointCall || !objectCallA || !objectCallB || !within(pointCall, update, t::kUpdateExtent) ||
        !within(objectCallA, update, t::kUpdateExtent) || !within(objectCallB, update, t::kUpdateExtent) ||
        !(trig < pointCall && pointCall < objectCallA && objectCallA < objectCallB))
        return "the update's three builder calls";
    const uintptr_t point = callTarget(pointCall + t::kPointCall);
    const uintptr_t object = callTarget(objectCallA + t::kObjectCallA);
    if (!point || !object || point == object || object != callTarget(objectCallB + t::kObjectCallB))
        return "the builder calls' targets";
    // The second, independent site of each hook target: its own entry, and its placement arithmetic inside it.
    if (unique(t::kPointEntryPattern) != point)
        return "the point builder's entry at the update's call target";
    if (unique(t::kObjectEntryPattern) != object)
        return "the object builder's entry at the update's call target";
    const uintptr_t pointPlace = unique(t::kPointPlacePattern), objectPlace = unique(t::kObjectPlacePattern);
    if (!pointPlace || !within(pointPlace, point, t::kPointExtent))
        return "the point builder's placement";
    if (!objectPlace || !within(objectPlace, object, t::kObjectExtent))
        return "the object builder's placement";
    // ValueAgreement: the angle is -(yaw + pi/2) ...
    const uintptr_t halfPiAt = trig + t::kTrigHalfPi + 4 + i32(trig + t::kTrigHalfPi);
    const uintptr_t maskAt = trig + t::kTrigMask + 4 + i32(trig + t::kTrigMask);
    if (!inside(halfPiAt, sizeof(float)) || !inside(maskAt, sizeof(uint32_t)))
        return "the heading's constants lie outside the module";
    float halfPi = 0.0f;
    uint32_t mask = 0;
    memcpy(&halfPi, reinterpret_cast<const void*>(halfPiAt), sizeof(halfPi));
    memcpy(&mask, reinterpret_cast<const void*>(maskAt), sizeof(mask));
    if (!(std::fabs(halfPi - 1.5707964f) <= 1.0e-6f) || mask != 0x80000000u)
        return "the heading's constants are not pi/2 and the sign mask";
    // ... and call A is sinf, call B cosf: each is an import thunk (jmp [slot]) whose slot holds the C runtime's own.
    float (*imported[2])(float) = {nullptr, nullptr};
    const int callAt[2]{t::kTrigCallA, t::kTrigCallB};
    for (int i = 0; i < 2; ++i)
    {
        const uintptr_t thunk = callTarget(trig + callAt[i]);
        if (!thunk || !inside(thunk, 6) || *reinterpret_cast<const uint8_t*>(thunk) != 0xFF ||
            *reinterpret_cast<const uint8_t*>(thunk + 1) != 0x25)
            return "the heading's trig calls are not import thunks";
        const uintptr_t slot = thunk + 6 + i32(thunk + 2);
        if (!inside(slot, sizeof(void*)))
            return "a trig import slot lies outside the module";
        uintptr_t function = 0;
        memcpy(&function, reinterpret_cast<const void*>(slot), sizeof(function));
        if (!function || inside(function, 1))
            return "a trig import is not bound";
        imported[i] = reinterpret_cast<float (*)(float)>(function);
    }
    const HMODULE runtime = GetModuleHandleW(L"ucrtbase.dll");
    const void* runtimeSin = runtime ? reinterpret_cast<const void*>(GetProcAddress(runtime, "sinf")) : nullptr;
    const void* runtimeCos = runtime ? reinterpret_cast<const void*>(GetProcAddress(runtime, "cosf")) : nullptr;
    if (runtimeSin && runtimeCos && reinterpret_cast<const void*>(imported[0]) == runtimeSin &&
        reinterpret_cast<const void*>(imported[1]) == runtimeCos)
        out.trig = "the imports are ucrtbase's sinf and cosf";
    else if (Halo3TrackerTrigAnswers(imported[0], true) && Halo3TrackerTrigAnswers(imported[1], false))
        out.trig = "the imports answer as sinf and cosf";
    else
        return "the heading's trig calls are not sinf then cosf";
    // The output user: the update reads it from its state and asks for that user's unit.
    out.userOffset = i32(update + t::kUpdateUserDisp);
    if (out.userOffset <= 0 || out.userOffset > 0x10000 || (out.userOffset & 3))
        return "the output user's offset did not decode plausibly";
    const uintptr_t unitCall = callTarget(update + t::kUpdateUnitCall);
    if (!unitCall)
        return "the update's unit call";
    if (unitGetter && unitCall != unitGetter)
        return "the update's unit call is not the output-user unit getter";
    out.unitGetterAgrees = unitGetter != 0;
    // The tracked entry's layout, from the builder's own next instructions. Not a condition of the install: a
    // builder that indexes its entries differently only loses the own-blip rule.
    {
        const uintptr_t after = object + (sizeof(t::kObjectEntryPattern) / 3);
        out.slotEntryAgrees = inside(after, sizeof(t::kObjectSlotEntryBytes)) &&
            memcmp(reinterpret_cast<const void*>(after), t::kObjectSlotEntryBytes,
                   sizeof(t::kObjectSlotEntryBytes)) == 0;
    }
    out.update = update;
    out.point = point;
    out.object = object;
    return nullptr;
}
} // namespace

// Install thread. Its own transaction: any miss leaves the game's own tracker, said once.
bool InstallHeadRelativeTracker(Halo3HeadRelativeLane& lane, uintptr_t base, size_t size, uint32_t generation,
                                uintptr_t unitGetter, void* pointDetour, void* objectDetour)
{
    lane.ready.store(false, std::memory_order_release);
    if (lane.pointTarget || lane.objectTarget)
    {
        LOG("TRACKER HEAD %s: StockFallback: a previous tracker hook has not retired", lane.title);
        return false;
    }
    Halo3TrackerProof proof{};
    if (const char* why = Halo3TrackerProve(base, size, unitGetter, proof))
    {
        LOG("TRACKER HEAD %s: StockFallback: %s did not prove; the motion tracker keeps the game's own heading "
            "(the aim)", lane.title, why);
        return false;
    }
    lane.userOffset = proof.userOffset;
    for (auto& use : lane.uses)
        use.store(0, std::memory_order_relaxed);
    lane.faults.store(0, std::memory_order_relaxed);
    lane.reportedState = -1;
    void* const point = reinterpret_cast<void*>(proof.point);
    void* const object = reinterpret_cast<void*>(proof.object);
    if (MH_CreateHook(point, pointDetour, reinterpret_cast<void**>(&lane.pointOriginal)) != MH_OK)
    {
        lane.pointOriginal = nullptr;
        LOG("TRACKER HEAD %s: StockFallback: the point builder's hook was not created", lane.title);
        return false;
    }
    lane.pointTarget = point;
    if (MH_CreateHook(object, objectDetour, reinterpret_cast<void**>(&lane.objectOriginal)) != MH_OK)
    {
        lane.objectOriginal = nullptr;
        LOG("TRACKER HEAD %s: StockFallback: the object builder's hook was not created", lane.title);
        (void)RemoveHeadRelativeTracker(lane, pointDetour, objectDetour);
        return false;
    }
    lane.objectTarget = object;
    lane.ownUnitProven = proof.unitGetterAgrees && proof.slotEntryAgrees && lane.unitGetter != nullptr;
    lane.provenUnitGetter.store(lane.ownUnitProven ? reinterpret_cast<int (__fastcall*)(int)>(unitGetter) : nullptr,
                                std::memory_order_release);
    lane.ready.store(true, std::memory_order_release);
    if (MCCVR_EnableHook(point) != MH_OK || MCCVR_EnableHook(object) != MH_OK)
    {
        LOG("TRACKER HEAD %s: StockFallback: a tracker hook did not enable", lane.title);
        (void)RemoveHeadRelativeTracker(lane, pointDetour, objectDetour);
        return false;
    }
    LOG("TRACKER HEAD %s: Installed (generation %u): the tracker update +0x%llX turns its user's player-control yaw into "
        "sinf / cosf of -(yaw + pi/2) and hands the pair to its contact builders +0x%llX (world points) and +0x%llX "
        "(objects), which nothing else calls; both are hooked and get the pair of the HEAD's heading for output user "
        "0 (tracker state +0x%X). Proven: nine unique signatures, both targets at the update's call operands and "
        "at their own entries, pi/2 and the sign mask, %s, the unit call %s. The player's own dot is %s. Nothing in "
        "the game is written. tracker_follows_head is %s",
        lane.title, generation, static_cast<unsigned long long>(proof.update - base),
        static_cast<unsigned long long>(proof.point - base), static_cast<unsigned long long>(proof.object - base),
        static_cast<unsigned>(proof.userOffset), proof.trig,
        proof.unitGetterAgrees ? "is the bound output-user unit getter" : "unchecked (no unit getter bound)",
        lane.ownUnitProven ? "centred: it is the tracked entry (of twenty, 12 bytes each, the object's handle "
                             "first) that holds that getter's unit"
                           : proof.slotEntryAgrees ? "left where the game puts it (no unit getter to know it by)"
                                                   : "left where the game puts it (the builder indexes its "
                                                     "tracked entries differently on this image)",
        g_config.tracker_follows_head ? "on" : "off");
    lane.reportedSwitch = g_config.tracker_follows_head ? 1 : 0;
    return true;
}

// ====================================================================================================
// THE SOUND LISTENER (the header of this file; src/common/halo3_head_relative_logic.h).

using Halo3ListenerUpdateFn = void(__fastcall*)(bool);

struct Halo3HeadAudioLane
{
    const char* title = "Halo 3";
    const std::atomic<uint32_t>* runtimeGeneration = nullptr;
    const HeadRelativePublication* head = nullptr;   // what ApplyHeadLook publishes (the tracker lane's)
    // The hook (worker-owned; the detour reads `ready` and the original).
    void* target = nullptr;
    Halo3ListenerUpdateFn original = nullptr;
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> ready{false};
    // Decoded at install: the static that holds the sound manager's globals, and listener 0's fields in them.
    uintptr_t globalsPointer = 0;
    int32_t locationOffset = 0, builtOffset = 0, matrixOffset = 0, velocityOffset = 0;
    // Verdicts, counted where the listener is updated; the cold worker reads and clears them.
    std::atomic<uint32_t> uses[static_cast<size_t>(head_relative::Use::Count)]{};
    std::atomic<uint32_t> faults{0};
    std::atomic<float> lastNative[3]{}, lastHead[3]{};   // the two forwards of the last rewrite
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Which thread updates the listeners (dev): the first one seen since the install, and how many updates came
    // from any other. Offline, two entries reach the update through pointers, so their thread is not provable;
    // a second thread here would mean a reader could see the game's basis between its write and ours.
    std::atomic<uint32_t> firstThread{0}, otherThread{0}, otherThreadUpdates{0}, updates{0};
#endif
    // Cold worker only.
    int reportedSwitch = -1;
    int reportedState = -1;
    uint64_t reportMs = 0;
};
Halo3HeadAudioLane g_halo3HeadAudio;

namespace
{
// Listener 0, after the game's own update: read it, and when every gate passes store the head's basis and the
// same world velocity in it. Guarded: the record is the sound manager's, reached through a static of the title.
head_relative::Use Halo3ListenerRewrite(Halo3HeadAudioLane& lane)
{
    namespace t = halo3_head_relative;
    using head_relative::Use;
    __try
    {
        uint8_t* const globals = *reinterpret_cast<uint8_t* const*>(lane.globalsPointer);
        if (!globals)
            return Use::NotLocalUser;
        uint8_t* const matrix = globals + lane.matrixOffset;
        uint8_t* const velocityAt = globals + lane.velocityOffset;
        t::ListenerRequest in{};
        in.switchOn = true;
        // The game's own test of a listener (its sound readers, +0x15AD6C / +0x15AD7A): built, and a location set.
        in.listener = globals[lane.builtOffset] != 0 && globals[lane.locationOffset] != 0xFF;
        t::ListenerBasis basis;
        float velocity[3];
        memcpy(&in.scale, matrix + t::kMatrixScale, sizeof(in.scale));
        memcpy(basis.forward, matrix + t::kMatrixForward, sizeof(basis.forward));
        memcpy(basis.left, matrix + t::kMatrixLeft, sizeof(basis.left));
        memcpy(basis.up, matrix + t::kMatrixUp, sizeof(basis.up));
        memcpy(velocity, velocityAt, sizeof(velocity));
        if (in.listener)
        {
            in.sampleRead = lane.head && HeadRelativeRead(*lane.head, in.sample, in.gameplay);
            in.generation = lane.runtimeGeneration ? lane.runtimeGeneration->load(std::memory_order_acquire) : 0;
            in.nowUs = Sight_NowUs();
        }
        const float nativeForward[3]{basis.forward[0], basis.forward[1], basis.forward[2]};
        const Use use = t::SubstituteListener(in, basis, velocity);
        if (use == Use::Ok)
        {
            memcpy(matrix + t::kMatrixForward, basis.forward, sizeof(basis.forward));
            memcpy(matrix + t::kMatrixLeft, basis.left, sizeof(basis.left));
            memcpy(matrix + t::kMatrixUp, basis.up, sizeof(basis.up));
            memcpy(velocityAt, velocity, sizeof(velocity));
            for (int i = 0; i < 3; ++i)
            {
                lane.lastNative[i].store(nativeForward[i], std::memory_order_relaxed);
                lane.lastHead[i].store(basis.forward[i], std::memory_order_relaxed);
            }
        }
        return use;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        lane.faults.fetch_add(1, std::memory_order_relaxed);
        return head_relative::Use::NotLocalUser;
    }
}

void Halo3ListenerApplyHead(Halo3HeadAudioLane& lane)
{
    const head_relative::Use use = lane.ready.load(std::memory_order_acquire) && g_config.audio_follows_head
                                       ? Halo3ListenerRewrite(lane)
                                       : head_relative::Use::SwitchOff;
    lane.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
}
} // namespace

__declspec(noinline) void __fastcall Halo3ListenerUpdateDetour(bool flag)
{
    Halo3HookCalled(Halo3Hook::SoundListener);
    auto& lane = g_halo3HeadAudio;
    lane.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (lane.original)
            CallNativeWithSpareSlots(lane.original, flag);
        Halo3ListenerApplyHead(lane);
        // The vehicle camera note (halo3_hud_note.inl): this update runs on the HUD's thread, after its update.
        Halo3HudNotePost();
#if HALOMCCVR_DEV_DIAGNOSTICS
        const uint32_t thread = GetCurrentThreadId();
        uint32_t first = 0;
        lane.updates.fetch_add(1, std::memory_order_relaxed);
        if (!lane.firstThread.compare_exchange_strong(first, thread, std::memory_order_relaxed) && first != thread)
        {
            lane.otherThread.store(thread, std::memory_order_relaxed);
            lane.otherThreadUpdates.fetch_add(1, std::memory_order_relaxed);
        }
#endif
    }
    __finally
    {
        lane.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// Worker: retire the listener hook (disable, prove nothing is inside, remove). Idempotent; false = try again.
bool RemoveHeadRelativeAudio(Halo3HeadAudioLane& lane, const void* detour)
{
    lane.ready.store(false, std::memory_order_release);
    if (!lane.target)
        return true;
    const auto status = MCCVR_DisableHookForRetirement(lane.target);
    if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
    {
        LOG("AUDIO HEAD %s CleanupRequired: the listener hook did not disable (%d)", lane.title,
            static_cast<int>(status));
        return false;
    }
    const void* functions[]{detour};
    const void* trampolines[]{reinterpret_cast<const void*>(lane.original)};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, lane.callbacks))
    {
        LOG("AUDIO HEAD %s CleanupRequired: the listener detour is busy", lane.title);
        return false;
    }
    const auto removed = MH_RemoveHook(lane.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("AUDIO HEAD %s CleanupRequired: the listener hook was not removed (%d)", lane.title,
            static_cast<int>(removed));
        return false;
    }
    lane.target = nullptr;
    lane.original = nullptr;
    lane.globalsPointer = 0;
    return true;
}

namespace
{
struct Halo3ListenerProof
{
    uintptr_t update = 0, globalsPointer = 0, matrixHelper = 0, velocityHelper = 0;
    int32_t locationOffset = 0, builtOffset = 0, matrixOffset = 0, velocityOffset = 0, observerStride = 0;
    uint8_t stride = 0;
};

// Every site of the listener's build, from the game's own bytes. Null = proven; otherwise what did not prove.
// Every read is of matched pattern bytes or of an address checked to lie inside the image first.
const char* Halo3ListenerProve(uintptr_t base, size_t size, Halo3ListenerProof& out)
{
    namespace t = halo3_head_relative;
    const auto unique = [&](const char* pattern) -> uintptr_t {
        const uintptr_t hit = sig::Find(base, size, pattern);
        return hit && !sig::Find(hit + 1, base + size - hit - 1, pattern) ? hit : 0;
    };
    const auto inside = [&](uintptr_t address, size_t bytes) {
        return address >= base && address <= base + size && bytes <= base + size - address;
    };
    const auto i32 = [](uintptr_t at) {
        int32_t v = 0;
        memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v));
        return v;
    };
    const auto callTarget = [&](uintptr_t at) -> uintptr_t {
        if (*reinterpret_cast<const uint8_t*>(at) != 0xE8)
            return 0;
        const uintptr_t target = at + 5 + i32(at + 1);
        return inside(target, 16) ? target : 0;
    };
    const auto within = [](uintptr_t site, uintptr_t entry, uint32_t extent) {
        return site > entry && site - entry < extent;
    };

    const uintptr_t update = unique(t::kListenerEntryPattern);
    if (!update)
        return "the listener update's entry";
    const uintptr_t init = unique(t::kListenerInitPattern);
    const uintptr_t globalsSite = unique(t::kListenerGlobalsPattern);
    const uintptr_t built = unique(t::kListenerBuiltPattern);
    const uintptr_t build = unique(t::kListenerBuildPattern);
    const uintptr_t loop = unique(t::kListenerLoopPattern);
    if (!init || !globalsSite || !built || !build || !loop || !within(init, update, t::kListenerUpdateExtent) ||
        !within(globalsSite, update, t::kListenerUpdateExtent) || !within(built, update, t::kListenerUpdateExtent) ||
        !within(build, update, t::kListenerUpdateExtent) || !within(loop, update, t::kListenerUpdateExtent) ||
        !(init < globalsSite && globalsSite < built && built < build && build < loop))
        return "the listener loop inside the update (its start, its globals, the built mark, one listener's build, "
               "its foot)";
    // The helpers, each proven twice: the update's call operand and the helper's own whole body there.
    const uintptr_t matrixHelper = callTarget(build + t::kListenerMatrixCall);
    const uintptr_t velocityHelper = callTarget(build + t::kListenerVelocityCall);
    if (!matrixHelper || unique(t::kListenerMatrixHelperPattern) != matrixHelper)
        return "the matrix helper (scale 1, forward, left = up x forward, up) at the build's first call";
    if (!velocityHelper || unique(t::kListenerVelocityHelperPattern) != velocityHelper)
        return "the velocity helper (a vector into the matrix's basis) at the build's second call";
    // The record: the globals pointer, and listener 0's location, matrix and velocity in the globals.
    const uintptr_t globalsPointer = globalsSite + t::kListenerGlobalsRel + 4 + i32(globalsSite + t::kListenerGlobalsRel);
    if (!inside(globalsPointer, sizeof(void*)) || (globalsPointer & 7))
        return "the sound globals pointer lies outside the module";
    out.matrixOffset = i32(build + t::kListenerMatrixDisp);
    out.velocityOffset = i32(build + t::kListenerVelocityDisp);
    out.locationOffset = i32(build + t::kListenerLocationDisp);
    out.builtOffset = i32(built + t::kListenerBuiltDisp);
    // ValueAgreement on the layout: the location word sits 8 bytes before the matrix (record +4 and +0xC) with the
    // built mark between them (record +8), the velocity right after the 0x34-byte matrix, and one record (the
    // loop's stride) holds all of it.
    out.stride = *reinterpret_cast<const uint8_t*>(loop + t::kListenerLoopStride);
    out.observerStride = i32(loop + t::kListenerLoopObserverA);
    if (out.locationOffset < 4 || out.locationOffset > 0x4000 || out.builtOffset != out.locationOffset + 4 ||
        out.matrixOffset != out.locationOffset + 8 ||
        out.velocityOffset != out.matrixOffset + t::kMatrixPosition + 12 ||
        out.stride < out.velocityOffset + 12 - (out.locationOffset - 4) || out.stride > 0x7F)
        return "the listener record's layout (location, built mark, matrix, velocity, stride) does not agree with "
               "the matrix's";
    if (out.observerStride < 0x100 || out.observerStride != i32(loop + t::kListenerLoopObserverB))
        return "the loop's observer stride";
    out.update = update;
    out.globalsPointer = globalsPointer;
    out.matrixHelper = matrixHelper;
    out.velocityHelper = velocityHelper;
    return nullptr;
}
} // namespace

// Install thread. Its own transaction: any miss leaves the game's own listener, said once.
bool InstallHeadRelativeAudio(Halo3HeadAudioLane& lane, uintptr_t base, size_t size, uint32_t generation,
                              void* detour)
{
    lane.ready.store(false, std::memory_order_release);
    if (lane.target)
    {
        LOG("AUDIO HEAD %s: StockFallback: a previous listener hook has not retired", lane.title);
        return false;
    }
    Halo3ListenerProof proof{};
    if (const char* why = Halo3ListenerProve(base, size, proof))
    {
        LOG("AUDIO HEAD %s: StockFallback: %s did not prove; the sound listener keeps the game's own orientation "
            "(the aim)", lane.title, why);
        return false;
    }
    lane.globalsPointer = proof.globalsPointer;
    lane.locationOffset = proof.locationOffset;
    lane.builtOffset = proof.builtOffset;
    lane.matrixOffset = proof.matrixOffset;
    lane.velocityOffset = proof.velocityOffset;
    for (auto& use : lane.uses)
        use.store(0, std::memory_order_relaxed);
    lane.faults.store(0, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    lane.firstThread.store(0, std::memory_order_relaxed);
    lane.otherThread.store(0, std::memory_order_relaxed);
    lane.otherThreadUpdates.store(0, std::memory_order_relaxed);
    lane.updates.store(0, std::memory_order_relaxed);
#endif
    lane.reportedState = -1;
    void* const target = reinterpret_cast<void*>(proof.update);
    if (MH_CreateHook(target, detour, reinterpret_cast<void**>(&lane.original)) != MH_OK)
    {
        lane.original = nullptr;
        LOG("AUDIO HEAD %s: StockFallback: the listener hook was not created", lane.title);
        return false;
    }
    lane.target = target;
    lane.ready.store(true, std::memory_order_release);
    if (MCCVR_EnableHook(target) != MH_OK)
    {
        LOG("AUDIO HEAD %s: StockFallback: the listener hook did not enable", lane.title);
        (void)RemoveHeadRelativeAudio(lane, detour);
        return false;
    }
    LOG("AUDIO HEAD %s: Installed (generation %u): the listener update +0x%llX rebuilds listener 0 (sound globals at "
        "+0x%llX: location +0x%X, matrix +0x%X, relative velocity +0x%X, 0x%X bytes a listener) from the observer's "
        "forward and up through +0x%llX (scale 1, forward, left = up x forward, up) and +0x%llX; after each update its "
        "basis becomes the HEAD's and its velocity is re-expressed in it; position, location and the other listeners "
        "are untouched. Proven: eight unique signatures, both helpers at the update's call operands and by their whole "
        "bodies, the record's layout against the matrix's; each rewrite also requires scale 1 and orthonormal rows. "
        "audio_follows_head is %s",
        lane.title, generation, static_cast<unsigned long long>(proof.update - base),
        static_cast<unsigned long long>(proof.globalsPointer - base), static_cast<unsigned>(proof.locationOffset),
        static_cast<unsigned>(proof.matrixOffset), static_cast<unsigned>(proof.velocityOffset),
        static_cast<unsigned>(proof.stride), static_cast<unsigned long long>(proof.matrixHelper - base),
        static_cast<unsigned long long>(proof.velocityHelper - base), g_config.audio_follows_head ? "on" : "off");
    lane.reportedSwitch = g_config.audio_follows_head ? 1 : 0;
    return true;
}

// Both halves, each its own transaction (a miss in one leaves the other).
bool InstallHalo3HeadRelative(uintptr_t base, size_t size, uint32_t generation)
{
    auto& lane = g_halo3HeadRelative;
    lane.title = "Halo 3";
    lane.runtimeGeneration = &g_halo3RuntimeGeneration;
    lane.unitGetter = &g_halo3PlayerUnitGetter;
    const bool tracker = InstallHeadRelativeTracker(lane, base, size, generation,
                                                    reinterpret_cast<uintptr_t>(g_halo3PlayerUnitGetter),
                                                    reinterpret_cast<void*>(&Halo3TrackerPointDetour),
                                                    reinterpret_cast<void*>(&Halo3TrackerObjectDetour));
    auto& audio = g_halo3HeadAudio;
    audio.title = "Halo 3";
    audio.runtimeGeneration = &g_halo3RuntimeGeneration;
    audio.head = &lane.head;
    const bool listener = InstallHeadRelativeAudio(audio, base, size, generation,
                                                   reinterpret_cast<void*>(&Halo3ListenerUpdateDetour));
    if (listener)
        Halo3HookWatchInstalled(Halo3Hook::SoundListener, reinterpret_cast<uintptr_t>(audio.target));
    return tracker || listener;
}

bool RemoveHalo3HeadRelative()
{
    const bool tracker =
        RemoveHeadRelativeTracker(g_halo3HeadRelative, reinterpret_cast<const void*>(&Halo3TrackerPointDetour),
                                  reinterpret_cast<const void*>(&Halo3TrackerObjectDetour));
    const bool listener =
        RemoveHeadRelativeAudio(g_halo3HeadAudio, reinterpret_cast<const void*>(&Halo3ListenerUpdateDetour));
    return tracker && listener;
}

// Cold worker, every 2 s. Every build: the player's switch when it changes, and the tracker's state when it
// changes (placing by the head / the game's own, with the reason). Dev builds: the window.
void HeadRelativeTrackerReport(Halo3HeadRelativeLane& lane)
{
    const uint64_t now = GetTickCount64();
    if (now - lane.reportMs < 2000)
        return;
    lane.reportMs = now;
    if (!lane.ready.load(std::memory_order_acquire))
        return;
    using head_relative::Use;
    const int switchNow = g_config.tracker_follows_head ? 1 : 0;
    if (lane.reportedSwitch != switchNow)
    {
        lane.reportedSwitch = switchNow;
        LOG("TRACKER HEAD %s: the switch is now %s: the motion tracker turns with %s", lane.title,
            switchNow ? "ON" : "OFF", switchNow ? "the head" : "the aim, as the game makes it");
    }
    uint32_t counts[static_cast<size_t>(Use::Count)]{};
    uint32_t total = 0, refused = 0;
    size_t most = static_cast<size_t>(Use::Count);
    for (size_t i = 0; i < static_cast<size_t>(Use::Count); ++i)
    {
        counts[i] = lane.uses[i].exchange(0, std::memory_order_relaxed);
        total += counts[i];
        if (i != static_cast<size_t>(Use::Ok))
        {
            refused += counts[i];
            if (counts[i] && (most == static_cast<size_t>(Use::Count) || counts[i] > counts[most]))
                most = i;
        }
    }
    const uint32_t faults = lane.faults.exchange(0, std::memory_order_relaxed);
    if (!total && !faults)
        return;
    const uint32_t used = counts[static_cast<size_t>(Use::Ok)];
    // The state: 0 = by the head; 1 + reason = the game's own for that reason (the larger share of the window).
    const int state = used >= refused ? 0 : 1 + static_cast<int>(most);
    if (state != lane.reportedState)
    {
        lane.reportedState = state;
        if (!state)
            LOG("TRACKER HEAD %s: contacts are placed by the head's heading", lane.title);
        else if (most != static_cast<size_t>(Use::SwitchOff))
            LOG("TRACKER HEAD %s: the game's own tracker (the aim): %s", lane.title,
                head_relative::UseName(static_cast<Use>(most)));
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    const float nativeDeg = std::atan2(lane.lastNative[1].load(std::memory_order_relaxed),
                                       lane.lastNative[0].load(std::memory_order_relaxed)) * head_relative::kRadToDeg;
    const float usedDeg = std::atan2(lane.lastUsed[1].load(std::memory_order_relaxed),
                                     lane.lastUsed[0].load(std::memory_order_relaxed)) * head_relative::kRadToDeg;
    float turned = usedDeg - nativeDeg;
    while (turned > 180.0f)
        turned -= 360.0f;
    while (turned <= -180.0f)
        turned += 360.0f;
    LOG("TRACKER HEAD %s (dev): %u contact placements by the head (last: the game's heading %.1f deg, handed %.1f deg, "
        "turned %+.1f; sign %d, offset %.1f deg); refused %u: switch off %u, not the local user %u, not gameplay %u, "
        "no head sample %u, another generation %u, from the future %u, stale %u, no heading %u; state read faults %u",
        lane.title, used, used ? nativeDeg : 0.0f, used ? usedDeg : 0.0f, used ? turned : 0.0f,
        g_config.tracker_head_sign < 0 ? -1 : 1, g_config.tracker_head_offset_deg, refused,
        counts[static_cast<size_t>(Use::SwitchOff)], counts[static_cast<size_t>(Use::NotLocalUser)],
        counts[static_cast<size_t>(Use::NotGameplay)], counts[static_cast<size_t>(Use::NoSample)],
        counts[static_cast<size_t>(Use::OtherGeneration)], counts[static_cast<size_t>(Use::FromTheFuture)],
        counts[static_cast<size_t>(Use::Stale)], counts[static_cast<size_t>(Use::NoHeading)], faults);
    if (const uint32_t own = lane.ownBlips.exchange(0, std::memory_order_relaxed))
    {
        const float x = lane.ownBlipX.load(std::memory_order_relaxed), y = lane.ownBlipY.load(std::memory_order_relaxed);
        const float rangeM = lane.ownRangeWu.load(std::memory_order_relaxed) * 3.048f;
        if (x == 0.0f && y == 0.0f)
            LOG("TRACKER HEAD %s (dev): the player's own blip, %u placements; last AT THE CENTRE (x 0, y 0 of the "
                "radius; radius %.1f m)",
                lane.title, own, rangeM);
        else
            LOG("TRACKER HEAD %s (dev): the player's own blip, %u placements; last at x %+.4f y %+.4f of the radius = "
                "%+.2f m right, %.2f m %s the centre (radius %.1f m). (0, 0) = centred; the game's own placement is "
                "the body measured from the unit's head marker",
                lane.title, own, x, y, x * rangeM, std::fabs(y) * rangeM, y > 0.0f ? "below" : "above", rangeM);
    }
#endif
}

// Cold worker, every 2 s. Every build: the player's switch when it changes, and the listener's state when it
// changes (by the head / the game's own, with the reason). Dev builds: the window.
void HeadRelativeAudioReport(Halo3HeadAudioLane& lane)
{
    const uint64_t now = GetTickCount64();
    if (now - lane.reportMs < 2000)
        return;
    lane.reportMs = now;
    if (!lane.ready.load(std::memory_order_acquire))
        return;
    using head_relative::Use;
    const int switchNow = g_config.audio_follows_head ? 1 : 0;
    if (lane.reportedSwitch != switchNow)
    {
        lane.reportedSwitch = switchNow;
        LOG("AUDIO HEAD %s: the switch is now %s: you hear the world from the way %s", lane.title,
            switchNow ? "ON" : "OFF", switchNow ? "your head faces" : "the gun points, as the game makes it");
    }
    uint32_t counts[static_cast<size_t>(Use::Count)]{};
    uint32_t total = 0, refused = 0;
    size_t most = static_cast<size_t>(Use::Count);
    for (size_t i = 0; i < static_cast<size_t>(Use::Count); ++i)
    {
        counts[i] = lane.uses[i].exchange(0, std::memory_order_relaxed);
        total += counts[i];
        if (i != static_cast<size_t>(Use::Ok))
        {
            refused += counts[i];
            if (counts[i] && (most == static_cast<size_t>(Use::Count) || counts[i] > counts[most]))
                most = i;
        }
    }
    const uint32_t faults = lane.faults.exchange(0, std::memory_order_relaxed);
    if (!total && !faults)
        return;
    const uint32_t used = counts[static_cast<size_t>(Use::Ok)];
    const int state = used >= refused ? 0 : 1 + static_cast<int>(most);
    if (state != lane.reportedState)
    {
        lane.reportedState = state;
        if (!state)
            LOG("AUDIO HEAD %s: the sound listener has the head's orientation", lane.title);
        else if (most != static_cast<size_t>(Use::SwitchOff))
            LOG("AUDIO HEAD %s: the game's own listener (the aim): %s", lane.title,
                most == static_cast<size_t>(Use::NotLocalUser)
                    ? "the game has no listener for the local user"
                    : most == static_cast<size_t>(Use::NoHeading)
                          ? "the listener record or the head has no usable basis"
                          : head_relative::UseName(static_cast<Use>(most)));
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    float native[3], head[3];
    for (int i = 0; i < 3; ++i)
    {
        native[i] = lane.lastNative[i].load(std::memory_order_relaxed);
        head[i] = lane.lastHead[i].load(std::memory_order_relaxed);
    }
    const auto yawDeg = [](const float v[3]) { return std::atan2(v[1], v[0]) * head_relative::kRadToDeg; };
    const auto pitchDeg = [](const float v[3]) {
        return std::atan2(v[2], std::sqrt(v[0] * v[0] + v[1] * v[1])) * head_relative::kRadToDeg;
    };
    LOG("AUDIO HEAD %s (dev): %u listener updates rewritten with the head (last: the game's forward yaw %.1f pitch "
        "%.1f deg, the head's yaw %.1f pitch %.1f); refused %u: switch off %u, no listener for the local user %u, not "
        "gameplay %u, no head sample %u, another generation %u, from the future %u, stale %u, no usable basis %u; "
        "record read faults %u; threads since the install: %u updates, first on thread %u, %u on other threads "
        "(last other %u)",
        lane.title, used, used ? yawDeg(native) : 0.0f, used ? pitchDeg(native) : 0.0f, used ? yawDeg(head) : 0.0f,
        used ? pitchDeg(head) : 0.0f, refused, counts[static_cast<size_t>(Use::SwitchOff)],
        counts[static_cast<size_t>(Use::NotLocalUser)], counts[static_cast<size_t>(Use::NotGameplay)],
        counts[static_cast<size_t>(Use::NoSample)], counts[static_cast<size_t>(Use::OtherGeneration)],
        counts[static_cast<size_t>(Use::FromTheFuture)], counts[static_cast<size_t>(Use::Stale)],
        counts[static_cast<size_t>(Use::NoHeading)], faults, lane.updates.load(std::memory_order_relaxed),
        lane.firstThread.load(std::memory_order_relaxed), lane.otherThreadUpdates.load(std::memory_order_relaxed),
        lane.otherThread.load(std::memory_order_relaxed));
#endif
}

void Halo3HeadRelativeReport()
{
    HeadRelativeTrackerReport(g_halo3HeadRelative);
    HeadRelativeAudioReport(g_halo3HeadAudio);
}

// ====================================================================================================
// ODST (halo3odst.dll carries the same tracker and listener code: every signature above matches exactly once
// there too; tools/verify-halo3-motion-tracker.py odst, tools/verify-halo3-sound-listener.py odst). The same two
// seams, proofs, gates and refusals, with ODST's own lanes, detours, head publication (OdstApplyHeadLook) and
// title generation. NOT CONFIRMED IN GAME: built, unit-tested and proven offline only. Log: TRACKER HEAD ODST /
// AUDIO HEAD ODST. `py -3 tools/audit_caller_area.py halo3odst.dll 3654B0:10:i 365188:11:i B8898:8:v` is clean.
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
Halo3HeadRelativeLane g_odstHeadRelative;
Halo3HeadAudioLane g_odstHeadAudio;
thread_local halo3_head_relative::Hold t_odstTrackerHold;

void OdstHeadRelativePublish(const float* forward, const float* up, bool gameplay)
{
    HeadRelativePublishTo(g_odstHeadRelative.head, g_odstRuntimeGeneration.load(std::memory_order_acquire), forward,
                          up, gameplay);
}

__declspec(noinline) bool __fastcall OdstTrackerPointDetour(void* self, const float* point, void* blip,
                                                            const float* origin, float sinA, float cosA)
{
    auto& lane = g_odstHeadRelative;
    lane.callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool placed = false;
    __try
    {
        Halo3TrackerPair(lane, t_odstTrackerHold, self, sinA, cosA);
        if (lane.pointOriginal)
            placed = CallNativeWithSpareSlots(lane.pointOriginal, self, point, blip, origin, sinA, cosA);
    }
    __finally
    {
        lane.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return placed;
}

__declspec(noinline) bool __fastcall OdstTrackerObjectDetour(void* self, int slot, void* blip, const float* origin,
                                                             const float* point, float sinA, float cosA)
{
    auto& lane = g_odstHeadRelative;
    lane.callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool placed = false;
    __try
    {
        const bool own = Halo3TrackerPair(lane, t_odstTrackerHold, self, sinA, cosA);
        // The player's own blip at the centre, as in Halo 3. ODST's update +0x3655A0 is Halo 3's +0x32DA98
        // instruction for instruction but for where player control keeps the heading (read from the retail
        // file): the origin from the unit's head marker (+0x399C38), then the same first object call (+0x365759)
        // with a null point. A static read; not confirmed in game. As in Halo 3, own = the tracked entry
        // that holds the player's unit, in whichever of the twenty slots; never another unit that sits in slot 0.
        const bool ownSlot = own && !point && Halo3TrackerSlotIsOwn(lane, self, slot);
        const float* placedPoint = halo3_head_relative::OwnBlipPoint(own, ownSlot, point, origin);
        if (lane.objectOriginal)
            placed = CallNativeWithSpareSlots(lane.objectOriginal, self, slot, blip, origin, placedPoint, sinA, cosA);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (placed && ownSlot)
            Halo3TrackerNoteOwnBlip(lane, self, blip);
#endif
    }
    __finally
    {
        lane.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return placed;
}

__declspec(noinline) void __fastcall OdstListenerUpdateDetour(bool flag)
{
    auto& lane = g_odstHeadAudio;
    lane.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (lane.original)
            CallNativeWithSpareSlots(lane.original, flag);
        Halo3ListenerApplyHead(lane);
#if HALOMCCVR_DEV_DIAGNOSTICS
        const uint32_t thread = GetCurrentThreadId();
        uint32_t first = 0;
        lane.updates.fetch_add(1, std::memory_order_relaxed);
        if (!lane.firstThread.compare_exchange_strong(first, thread, std::memory_order_relaxed) && first != thread)
        {
            lane.otherThread.store(thread, std::memory_order_relaxed);
            lane.otherThreadUpdates.fetch_add(1, std::memory_order_relaxed);
        }
#endif
    }
    __finally
    {
        lane.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// Both halves, each its own transaction (a miss in one leaves the other).
bool InstallOdstHeadRelative(uintptr_t base, size_t size, uint32_t generation)
{
    auto& lane = g_odstHeadRelative;
    lane.title = "ODST";
    lane.runtimeGeneration = &g_odstRuntimeGeneration;
    lane.unitGetter = &g_odstPlayerUnitGetter;
    const bool tracker = InstallHeadRelativeTracker(lane, base, size, generation,
                                                    reinterpret_cast<uintptr_t>(g_odstPlayerUnitGetter),
                                                    reinterpret_cast<void*>(&OdstTrackerPointDetour),
                                                    reinterpret_cast<void*>(&OdstTrackerObjectDetour));
    auto& audio = g_odstHeadAudio;
    audio.title = "ODST";
    audio.runtimeGeneration = &g_odstRuntimeGeneration;
    audio.head = &lane.head;
    const bool listener = InstallHeadRelativeAudio(audio, base, size, generation,
                                                   reinterpret_cast<void*>(&OdstListenerUpdateDetour));
    return tracker || listener;
}

bool RemoveOdstHeadRelative()
{
    const bool tracker =
        RemoveHeadRelativeTracker(g_odstHeadRelative, reinterpret_cast<const void*>(&OdstTrackerPointDetour),
                                  reinterpret_cast<const void*>(&OdstTrackerObjectDetour));
    const bool listener =
        RemoveHeadRelativeAudio(g_odstHeadAudio, reinterpret_cast<const void*>(&OdstListenerUpdateDetour));
    return tracker && listener;
}

void OdstHeadRelativeReport()
{
    HeadRelativeTrackerReport(g_odstHeadRelative);
    HeadRelativeAudioReport(g_odstHeadAudio);
}
#endif
