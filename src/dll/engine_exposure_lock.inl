// THE ENGINE'S OWN EXPOSURE LOCK (`render_exposure_lock`), as Halo 3 and ODST share it: one engine, the same two
// signatures in each title's module, each found exactly once there.
//
// The eye-adaptation integrator tests this byte before it writes the blended exposure; nonzero freezes adaptation.
// It is named in HREK's debug menu and present in H3ODSTEK's unstripped debug-var table, and it is read at exactly
// one place in each module. The debug-var record ships with a NULL value pointer in retail, so the storage is
// decoded from that instruction instead: lock byte = match + 7 + disp32 at match+3.
//
// The byte that finds is the engine's INTERNAL copy of the post-process settings block, and the engine rebuilds that
// copy from a second, EDITABLE block once per rendered frame (`accept_edited_settings`, inlined into main_render as a
// 0x38-byte global-to-global copy) -- after the camera pass where we write and before the only instruction that
// reads the lock. So writing the internal copy alone is erased every frame before anyone looks at it (measured in
// ODST: the byte held for nine seat entries and the exposure still moved). Matching the copy itself yields BOTH
// blocks, so the source the engine propagates from is written as well as the destination:
//   editableBase = hit + 7  + disp32 at hit+3
//   internalBase = hit + 14 + disp32 at hit+10
//   lock         = base + 0x30 (m_auto_exposure_lock, the block's last field)
// The internal lock the copy yields must equal the one the adaptation test decodes: two unrelated signatures naming
// the same byte is what stands in for a hardcoded address. (Measured offline on the retail DLLs: Halo 3 lock
// +0x8AD070, editable block +0x2D3A3B0; ODST +0x8F0D70 and +0x2D7E210.)
//
// ASSERTED, never restored from a captured value: a module global that outlives our camera generation, around which
// the engine's per-view save and restore copies the whole render-debug block. Stock is the known constant 0, so
// releasing writes that constant back. And nothing in a module writes the editable copy but us, and the title's DLL
// stays resident after a title exit, so an engage left latched would freeze auto-exposure for the menus, on-foot
// play and the next level: every teardown path releases it (EngineExposureLockRelease). Authored and scripted
// cinematic exposure changes bypass the lock and still work.

inline constexpr const char* kEngineExposureLockSig =
    "40 38 2D ?? ?? ?? ?? 0F 57 C9 0F 28 C4 F3 0F 5C C6 F3 0F 5A C8";
inline constexpr const char* kEnginePostprocessAcceptSig =
    "0F 10 05 ?? ?? ?? ?? 0F 11 05 ?? ?? ?? ?? 0F 10 0D ?? ?? ?? ?? "
    "0F 11 0D ?? ?? ?? ?? 0F 10 05 ?? ?? ?? ?? 0F 11 05 ?? ?? ?? ?? "
    "F2 0F 10 0D ?? ?? ?? ?? F2 0F 11 0D ?? ?? ?? ??";

struct EngineExposureLock
{
    explicit EngineExposureLock(const char* name) noexcept : who(name) {}
    const char* who; // the log's prefix: "ODST vehicle exposure", ...
    // The engine's two copies of the lock byte: the INTERNAL one the integrator reads, and the EDITABLE one the
    // engine rebuilds it from every frame. Both or neither (a lone internal write is provably inert). Both lie in
    // the title's image, so the loader's unload notification retires them (EngineExposureLockRetireWithin) before
    // the image is unmapped, and every access holds the title image guard (title_image_guard.h): measured, a
    // teardown that released the hold after MCC had freed halo3.dll read the unmapped byte (one caught fault).
    std::atomic<uint8_t*> internal{nullptr};
    std::atomic<uint8_t*> editable{nullptr};
    std::atomic<uint32_t> state{0};
    std::atomic<uint32_t> engages{0};
    // Reads back the byte the ENGINE holds a frame after we engage, so the log reports whether the hold actually
    // took rather than only that we wrote it. That distinction is exactly what let an inert lock ship.
    std::atomic<uint32_t> verifyPending{0};
    std::atomic<uint32_t> verified{0};
};

// Both copies of the lock in the module at `base` / `size`, or neither (logged either way). Losing it costs only the
// held exposure.
inline bool EngineExposureLockResolve(EngineExposureLock& lock, uintptr_t base, size_t size)
{
    lock.internal.store(nullptr, std::memory_order_release);
    lock.editable.store(nullptr, std::memory_order_release);
    const uintptr_t exposureHit = sig::Find(base, size, kEngineExposureLockSig);
    const bool exposureUnique = exposureHit &&
        !sig::Find(exposureHit + 1, base + size - exposureHit - 1, kEngineExposureLockSig);
    uintptr_t testLock = 0;
    if (exposureUnique)
    {
        const uintptr_t lockAddress = exposureHit + 7 + *reinterpret_cast<const int32_t*>(exposureHit + 3);
        if (lockAddress >= base && lockAddress + 1 <= base + size)
            testLock = lockAddress;
    }
    if (!testLock)
    {
        LOG("%s: adaptation-lock signature missing/ambiguous (found=%d unique=%d); exposure stays the engine's and "
            "everything else is unaffected",
            lock.who, exposureHit ? 1 : 0, exposureUnique ? 1 : 0);
        return false;
    }
    const uintptr_t acceptHit = sig::Find(base, size, kEnginePostprocessAcceptSig);
    const bool acceptUnique = acceptHit &&
        !sig::Find(acceptHit + 1, base + size - acceptHit - 1, kEnginePostprocessAcceptSig);
    if (!acceptUnique)
    {
        LOG("%s: settings-block copy signature missing/ambiguous (found=%d unique=%d); the hold is left OFF because "
            "writing only the internal copy is erased every frame before the engine reads it",
            lock.who, acceptHit ? 1 : 0, acceptUnique ? 1 : 0);
        return false;
    }
    const uintptr_t editableBase = acceptHit + 7 + *reinterpret_cast<const int32_t*>(acceptHit + 3);
    const uintptr_t internalBase = acceptHit + 14 + *reinterpret_cast<const int32_t*>(acceptHit + 10);
    const uintptr_t editableLock = acceptHit + 50 + *reinterpret_cast<const int32_t*>(acceptHit + 46);
    const uintptr_t internalLock = acceptHit + 58 + *reinterpret_cast<const int32_t*>(acceptHit + 54);
    const bool inModule = editableBase >= base && editableLock + 1 <= base + size && internalBase >= base &&
                          internalLock + 1 <= base + size;
    const bool shaped = editableLock == editableBase + 0x30 && internalLock == internalBase + 0x30;
    const bool agrees = internalLock == testLock;
    if (!(inModule && shaped && agrees))
    {
        LOG("%s: settings-block copy failed its cross-check (inModule=%d shaped=%d agrees=%d; editable=+0x%llX "
            "internal=+0x%llX vs test +0x%llX); the hold is left OFF rather than writing a byte we cannot vouch for",
            lock.who, inModule ? 1 : 0, shaped ? 1 : 0, agrees ? 1 : 0, (unsigned long long)(editableLock - base),
            (unsigned long long)(internalLock - base), (unsigned long long)(testLock - base));
        return false;
    }
    lock.internal.store(reinterpret_cast<uint8_t*>(internalLock), std::memory_order_release);
    lock.editable.store(reinterpret_cast<uint8_t*>(editableLock), std::memory_order_release);
    LOG("%s: render_exposure_lock resolved at +0x%llX (from the adaptation test at +0x%llX), and the settings-block "
        "copy at +0x%llX names it and its editable source +0x%llX; a held exposure writes both",
        lock.who, (unsigned long long)(internalLock - base), (unsigned long long)(exposureHit - base),
        (unsigned long long)(acceptHit - base), (unsigned long long)(editableLock - base));
    return true;
}

// Engaged: adaptation held (both copies 1); released: the engine's own (both 0). Cheap while nothing changes: a read
// and a compare of each byte. Call from the camera pass, once a frame.
// The access itself, inside the title image guard: false when it faulted. `observed` is the internal byte as read.
inline bool EngineExposureLockWrite(EngineExposureLock& lock, uint8_t want, uint8_t& observed)
{
    bool ok = true;
    title_image_guard::BeginRead();
    uint8_t* const internal = lock.internal.load(std::memory_order_acquire);
    uint8_t* const editable = lock.editable.load(std::memory_order_acquire);
    __try
    {
        // Write the internal one first: it is the copy the engine rewrites every frame, so if the editable write
        // faults we are left latched on the self-healing copy rather than the permanent one.
        if (internal)
        {
            observed = *internal;
            if (observed != want)
                *internal = want;
        }
        if (editable && *editable != want)
            *editable = want;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    title_image_guard::EndRead();
    return ok;
}

inline void EngineExposureLockApply(EngineExposureLock& lock, bool engage)
{
    if (!lock.internal.load(std::memory_order_acquire) && !lock.editable.load(std::memory_order_acquire))
        return;
    uint8_t observed = 0xFFu;
    if (!EngineExposureLockWrite(lock, engage ? 1u : 0u, observed))
    {
        lock.internal.store(nullptr, std::memory_order_release);
        lock.editable.store(nullptr, std::memory_order_release);
        LOG("%s: FAULTED writing an adaptation switch; exposure control abandoned, everything else unaffected",
            lock.who);
        return;
    }
    // One frame after engaging, `observed` is what the engine carried into this frame. If the hold is real it reads
    // 1; if something still rebuilds it from stock it reads 0, and that is said once, loudly.
    if (engage && lock.verifyPending.load(std::memory_order_relaxed) && observed != 0xFFu)
    {
        lock.verifyPending.store(0, std::memory_order_relaxed);
        lock.verified.store(observed == 1u ? 1u : 2u, std::memory_order_relaxed);
        if (observed != 1u)
            LOG("%s: the hold is NOT taking -- the engine's own copy read %u a frame after we engaged, so something "
                "still rebuilds it; auto-exposure is effectively still the engine's",
                lock.who, (unsigned)observed);
        else
            LOG("%s: hold verified against the engine's own copy (reads 1 a frame after engaging)", lock.who);
    }
    const uint32_t state = engage ? 1u : 0u;
    if (lock.state.exchange(state, std::memory_order_relaxed) != state)
    {
        // The engage COUNT is the diagnostic that matters: a steady ride must show one engage, not a stream of them.
        const uint32_t engages = engage ? lock.engages.fetch_add(1, std::memory_order_relaxed) + 1
                                        : lock.engages.load(std::memory_order_relaxed);
        if (engage)
        {
            lock.verifyPending.store(1, std::memory_order_relaxed);
            lock.verified.store(0, std::memory_order_relaxed);
        }
        LOG("%s: adaptation %s (engage #%u; internal copy %s, editable copy %s)", lock.who,
            engage ? "HELD steady for the seat view" : "returned to the engine's own control", engages,
            lock.internal.load(std::memory_order_relaxed) ? "on" : "unavailable",
            lock.editable.load(std::memory_order_relaxed) ? "on" : "unavailable");
    }
}

// Teardown: back to the engine's own, then the pointers dropped, so nothing ever writes through an address whose
// module has gone.
inline void EngineExposureLockRelease(EngineExposureLock& lock)
{
    if (lock.internal.load(std::memory_order_acquire) || lock.editable.load(std::memory_order_acquire))
        EngineExposureLockApply(lock, false);
    lock.internal.store(nullptr, std::memory_order_release);
    lock.editable.store(nullptr, std::memory_order_release);
    lock.state.store(0, std::memory_order_relaxed);
    lock.verifyPending.store(0, std::memory_order_relaxed);
    lock.verified.store(0, std::memory_order_relaxed);
}

// The loader's unload notification for the image at `base` / `size` (title_image_guard.h: the guard held exclusively,
// the loader lock held): atomic stores only. The lock's bytes are dropped when they lie in the departing image; the
// title wrote its own stock value back with the image, so nothing is left to restore. True when it retired any.
inline bool EngineExposureLockRetireWithin(EngineExposureLock& lock, uintptr_t base, size_t size)
{
    const uintptr_t internal = reinterpret_cast<uintptr_t>(lock.internal.load(std::memory_order_acquire));
    const uintptr_t editable = reinterpret_cast<uintptr_t>(lock.editable.load(std::memory_order_acquire));
    if (!title_image_guard::Within(internal, base, size) && !title_image_guard::Within(editable, base, size))
        return false;
    lock.internal.store(nullptr, std::memory_order_release);
    lock.editable.store(nullptr, std::memory_order_release);
    lock.state.store(0, std::memory_order_relaxed);
    lock.verifyPending.store(0, std::memory_order_relaxed);
    return true;
}
