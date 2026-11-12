// THE GAME'S OWN FIRST-PERSON ANSWER, Halo 2 (src/common/director_camera_logic.h).
//
// Halo 2's director keeps, per user, the camera's update function; director_get_perspective(user) answers 0
// (first person) exactly when that function is first_person_camera_update and the record's transition timer is
// zero. Roomscale's admission and two-handed aim read the same two fields for user 0 instead of asking for the
// first-person weapon palette, which an unarmed player does not have.
//
// WHICH THREAD. The director records are a static array in halo2.dll's data (not engine TLS), so any thread can
// read them: the read is one pointer and one float under SEH, with no call into the game. The pointer is compared
// with a function address, never called. A torn read cannot matter (two independent aligned loads; the answer is
// re-read every frame), and the admission adds its own on-foot and presentation gates around it.
//
// ADDR-HYGIENE: resolved -- the record's address, stride and both field offsets and the update function's address
// are decoded from director_get_perspective itself and from an independent writer of the same record, each matched
// uniquely in the module, which must agree (director_camera::DecodeHalo2DirectorSites; offline:
// tools/verify-halo2-director-camera.py). Unbound = no camera rung = the first-person palette packet rung
// alone, said once.
struct Halo2DirectorPerspectiveBinding
{
    std::atomic<bool> bound{false};
    std::atomic<uint32_t> faults{0};
    uintptr_t moduleBase = 0;         // the module instance it was decoded from
    uintptr_t record = 0;             // user 0's director record
    uintptr_t firstPersonUpdate = 0;  // first_person_camera_update
    uint32_t updateOffset = 0, transitionOffset = 0;
};
Halo2DirectorPerspectiveBinding g_halo2DirectorPerspective;

void RetireHalo2DirectorPerspective() noexcept
{
    g_halo2DirectorPerspective.bound.store(false, std::memory_order_release);
}

// Management thread, with the observer's install. Reads only; no hook.
bool BindHalo2DirectorPerspective(uintptr_t base, size_t size) noexcept
{
    using namespace director_camera;
    auto& b = g_halo2DirectorPerspective;
    RetireHalo2DirectorPerspective();
    b.faults.store(0, std::memory_order_relaxed);
    const auto unique = [&](const char* pattern) -> uintptr_t {
        uintptr_t match = 0;
        uint32_t count = 0;
        return CountPatternMatches(base, size, pattern, match, count) && count == 1 ? match : 0;
    };
    const uintptr_t perspective = unique(kHalo2DirectorPerspectivePattern);
    const uintptr_t store = unique(kHalo2DirectorFirstPersonStorePattern);
    Halo2DirectorSites sites{};
    Halo2DirectorBind result = DecodeHalo2DirectorSites(
        reinterpret_cast<const uint8_t*>(perspective), static_cast<int64_t>(perspective - base),
        reinterpret_cast<const uint8_t*>(store), static_cast<int64_t>(store - base), sites);
    // The record and the function must lie inside the module: four users' records, and a code address.
    if (result == Halo2DirectorBind::Bound &&
        (static_cast<uint64_t>(sites.globalsRva) + 4ull * sites.stride > size ||
         static_cast<uint64_t>(sites.firstPersonUpdateRva) >= size))
        result = Halo2DirectorBind::Implausible;
    if (result != Halo2DirectorBind::Bound)
    {
        LOG("First-person camera Halo 2: StockFallback: the director's perspective did not bind (%s); first "
            "person is proven by the first-person palette packet only, as before (an unarmed player is refused)",
            Halo2DirectorBindName(result));
        return false;
    }
    b.moduleBase = base;
    b.record = base + static_cast<uintptr_t>(sites.globalsRva);
    b.firstPersonUpdate = base + static_cast<uintptr_t>(sites.firstPersonUpdateRva);
    b.updateOffset = sites.updateOffset;
    b.transitionOffset = sites.transitionOffset;
    b.bound.store(true, std::memory_order_release);
    LOG("First-person camera Halo 2: director perspective bound: user 0's record at +0x%llX (stride 0x%X), camera "
        "update +0x%X, transition +0x%X, first_person_camera_update +0x%llX (director_get_perspective +0x%llX and "
        "the first-person store +0x%llX agree); the first-person update with no transition is first person, with "
        "or without a weapon",
        static_cast<unsigned long long>(sites.globalsRva), sites.stride, sites.updateOffset,
        sites.transitionOffset, static_cast<unsigned long long>(sites.firstPersonUpdateRva),
        static_cast<unsigned long long>(sites.perspectiveFnRva), static_cast<unsigned long long>(store - base));
    return true;
}

// Any thread. Unavailable: not bound. Unread: the observer is not armed in a live level, the record has no
// update function yet, or the read faulted.
director_camera::Signal ReadHalo2DirectorPerspective() noexcept
{
    using director_camera::Signal;
    auto& b = g_halo2DirectorPerspective;
    if (!b.bound.load(std::memory_order_acquire))
        return Signal::Unavailable;
    // Only for the module instance it was decoded from, armed in a live level: RemoveCore clears g_armed and
    // raises the teardown flag before the module can go, and InstallCore retires the binding before anything else.
    if (!g_armed.load(std::memory_order_acquire) || !g_levelLive.load(std::memory_order_acquire) ||
        g_teardownRequested.load(std::memory_order_acquire) ||
        g_moduleBase.load(std::memory_order_acquire) != b.moduleBase)
        return Signal::Unread;
    uint64_t update = 0;
    float transition = 0.0f;
    __try
    {
        update = *reinterpret_cast<const uint64_t*>(b.record + b.updateOffset);
        transition = *reinterpret_cast<const float*>(b.record + b.transitionOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        b.faults.fetch_add(1, std::memory_order_relaxed);
        return Signal::Unread;
    }
    return director_camera::Halo2PerspectiveSignal(true, true, update, b.firstPersonUpdate, transition);
}
