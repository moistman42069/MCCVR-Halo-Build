// HALO 4: THE SOUND LISTENER FOLLOWS THE HEAD -- the runtime half. The sites, the pure logic and the reasoning are
// in src/common/halo4_sound_listener_logic.h; the write-up is docs/HALO4-HEAD-RELATIVE-2026-10-06.md ("Sound").
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED. Every binding here is read from retail halo4.dll
// alone (no editing-kit symbols), and the names are ours (the hooked function's is the one the image
// itself exports it under). The lane therefore (a) binds nothing unless
// every static link holds on the loaded image, and (b) changes nothing until the nine floats it means to replace
// have agreed, at run time and value for value, with the listener record it found by another route
// (HandOffProof). Until then every call is forwarded untouched and counted as "unproven".
//
// In short: the listener update builds user 0's listener from the game's own camera -- the aim -- and, in the
// same pass, hands the audio middleware nine floats made from the same forward, up and position. This lane hooks
// that one hand-off. For user 0 it forwards a private copy carrying the head's forward and up and the listener
// moved by the head's offset from the game's camera, and rewrites user 0's record to match. Game memory written:
// user 0's listener record (36 bytes of rows, 12 of position, 12 of velocity). Nothing else: not the camera, not
// player control, not the aim, nothing replicated.
//
// The head comes from halo4_head_relative.inl's publication (Halo4ReadHeadFrame): one sample per stereo pair,
// with a clock and a generation.

// ---- THE HAND-OFF ----------------------------------------------------------------------------------------------
// Retail +0x9B8250, which the image exports as AK::SoundEngine::SetListenerPosition(const AkListenerPosition&,
// unsigned long) returning AKRESULT: (nine floats: front, top, position; the listener's index).
// tools/audit_caller_area.py 9B8250:8:i is clean: it touches no caller slot past its home area, and its one
// native caller (the update) keeps no volatile register across the call and reads no return value. On entry it
// reads rcx and edx. The whole of rax goes back, so the result reaches the caller unchanged.
using Halo4ListenerHandOffFn = uint64_t (__fastcall*)(const float* nine, int32_t user);
constexpr size_t kHalo4ListenerUseCount = static_cast<size_t>(halo4_sound_listener::ListenerUse::Count);
constexpr size_t kHalo4ListenerStateCount = static_cast<size_t>(halo4_sound_listener::RecordState::Count);
struct Halo4ListenerHeadRuntime
{
    uintptr_t base = 0;
    size_t size = 0;
    uint32_t generation = 0;
    void* target = nullptr;
    void* original = nullptr;                    // Halo4ListenerHandOffFn
    uintptr_t updateReturn = 0;                  // where the update's call of the hand-off returns
    uint8_t* const* globalsSlot = nullptr;       // the image's own pointer to the sound manager's globals
    halo4_sound_listener::ListenerSites sites;   // decoded from the game's own instructions at install
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> callbacks{0};
    // The window the report drains: every call lands in exactly one use; user 0's calls from the update also in
    // one record state.
    std::atomic<uint32_t> uses[kHalo4ListenerUseCount]{};
    std::atomic<uint32_t> states[kHalo4ListenerStateCount]{};
    std::atomic<uint32_t> recordWrites{0}, fromEye{0}, faults{0}, mixedReads{0}, overlaps{0};
    std::atomic<uintptr_t> otherReturn{0};  // where the last call that was NOT the update's returns
    std::atomic<float> lastGameForward[3]{}, lastHeadForward[3]{}, lastHeadUp[3]{}, lastMoved{0.0f};
    // The proof belongs to user 0's hand-off. Which threads the update runs on, and whether two of them can be
    // inside at once, is not known statically. `inside` is tried and never waited for: a call that finds another
    // one inside does not step the proof (it is counted), and it uses `proven` as it stands.
    std::atomic<bool> inside{false};
    halo4_sound_listener::HandOffProof proof;  // under `inside`
    std::atomic<bool> proven{false};
    std::atomic<uint32_t> provenCount{0}, withdrawnCount{0};
    // The report's thread only.
    int reportedSwitch = -1;
    uint32_t reportedProven = 0, reportedWithdrawn = 0;
    bool reportedFirstUse = false, reportedFirstRefusal = false, reportedOtherCaller = false;
} g_halo4ListenerHead;

#if HALOMCCVR_DEV_DIAGNOSTICS
// THE MEASUREMENT (dev). Does Halo 4's sound follow the aim as the game builds it, and does the lane turn it to
// the head? For
// every build of user 0's listener that has a usable head sample: the angle between the listener's forward and
// the head's, between it and the aim's (the game's own camera of the frame the head was published from), and
// the distance from the listener to the head -- once as the game built it and once as this lane left the copy
// the middleware gets. "As the game built it" counts only the builds made from the game's OWN camera: a build
// that copied one of this mod's eye cameras (BuiltFromEye) says nothing about the game and is counted apart.
// Sums and peaks in fixed point (1/1000 degree; 1/1000000 world unit), drained by the report.
enum Halo4ListenerMeasured : int
{
    kHalo4MeasureBuiltHeadDeg = 0,
    kHalo4MeasureBuiltAimDeg,
    kHalo4MeasureBuiltHeadDistance,
    kHalo4MeasureLeftHeadDeg,
    kHalo4MeasureLeftAimDeg,
    kHalo4MeasureLeftHeadDistance,
    kHalo4MeasureApartDeg,  // the head's forward against the aim's
    kHalo4MeasureCount
};
// WHICH CALL OF THE UPDATE EACH BUILD CAME FROM, AND ON WHICH THREAD (dev). The update has four direct callers;
// which thread each runs on is not known statically. The update's own return address is read from its frame
// (the hand-off is entered with the update's rsp; the frame's size is decoded from the update's prologue).
constexpr int kHalo4ListenerSiteCapacity = 8;
struct Halo4ListenerMeasure
{
    std::atomic<uint32_t> count{0};     // builds measured
    std::atomic<uint32_t> ownCamera{0}; // of them, built from the game's own camera (the "as built" numbers)
    std::atomic<uint64_t> sum[kHalo4MeasureCount]{};
    std::atomic<uint32_t> peak[kHalo4MeasureCount]{};
    std::atomic<float> lastAimYawDeg{0.0f};
    uintptr_t returns[kHalo4ListenerSiteCapacity]{};  // written at install, before the hook is enabled
    int count_sites = 0, found = 0;
    // Index kHalo4ListenerSiteCapacity = a caller of the update that is none of the direct call sites.
    std::atomic<uint32_t> calls[kHalo4ListenerSiteCapacity + 1]{}, changed[kHalo4ListenerSiteCapacity + 1]{};
    std::atomic<uint32_t> thread[kHalo4ListenerSiteCapacity + 1]{};
} g_halo4ListenerMeasure;
void Halo4ListenerMeasureAdd(int which, float value, float scale) noexcept
{
    auto& measure = g_halo4ListenerMeasure;
    const float scaled = value * scale;
    const uint32_t fixed = std::isfinite(scaled) && scaled > 0.0f
                               ? (scaled < 4.0e9f ? static_cast<uint32_t>(scaled) : 4000000000u)
                               : 0u;
    measure.sum[which].fetch_add(fixed, std::memory_order_relaxed);
    uint32_t seen = measure.peak[which].load(std::memory_order_relaxed);
    while (fixed > seen && !measure.peak[which].compare_exchange_weak(seen, fixed, std::memory_order_relaxed))
    {
    }
}
// The hand-off's thread. `built` / `left`: the nine floats as the game made them and as this call forwards them.
void Halo4ListenerMeasureBuild(const float built[9], const float left[9],
                               const halo4_sound_listener::HeadFrame& frame) noexcept
{
    using namespace halo4_sound_listener;
    auto& measure = g_halo4ListenerMeasure;
    float forward[3], up[3], position[3];
    FromHandOff(built, forward, up, position);
    if (!BuiltFromEye(position, frame))
    {
        const Measurement asBuilt = Measure(forward, position, frame);
        Halo4ListenerMeasureAdd(kHalo4MeasureBuiltHeadDeg, asBuilt.fromHeadDeg, 1000.0f);
        Halo4ListenerMeasureAdd(kHalo4MeasureBuiltAimDeg, asBuilt.fromAimDeg, 1000.0f);
        Halo4ListenerMeasureAdd(kHalo4MeasureBuiltHeadDistance, asBuilt.fromHead, 1000000.0f);
        measure.ownCamera.fetch_add(1, std::memory_order_relaxed);
    }
    FromHandOff(left, forward, up, position);
    const Measurement asLeft = Measure(forward, position, frame);
    Halo4ListenerMeasureAdd(kHalo4MeasureLeftHeadDeg, asLeft.fromHeadDeg, 1000.0f);
    Halo4ListenerMeasureAdd(kHalo4MeasureLeftAimDeg, asLeft.fromAimDeg, 1000.0f);
    Halo4ListenerMeasureAdd(kHalo4MeasureLeftHeadDistance, asLeft.fromHead, 1000000.0f);
    Halo4ListenerMeasureAdd(kHalo4MeasureApartDeg, AngleDeg(frame.sample.forward, frame.aimForward), 1000.0f);
    measure.lastAimYawDeg.store(atan2f(frame.aimForward[1], frame.aimForward[0]) * head_relative::kRadToDeg,
                                std::memory_order_relaxed);
    measure.count.fetch_add(1, std::memory_order_relaxed);
}
void Halo4ListenerNoteBuild(uintptr_t updateCaller, bool changed) noexcept
{
    auto& measure = g_halo4ListenerMeasure;
    int site = kHalo4ListenerSiteCapacity;
    for (int i = 0; i < measure.count_sites; ++i)
        if (measure.returns[i] == updateCaller)
        {
            site = i;
            break;
        }
    measure.calls[site].fetch_add(1, std::memory_order_relaxed);
    if (changed)
        measure.changed[site].fetch_add(1, std::memory_order_relaxed);
    measure.thread[site].store(GetCurrentThreadId(), std::memory_order_relaxed);
}
// The return addresses of every direct `call` to `target` in the image's executable sections. Cold.
__declspec(noinline) int Halo4ListenerCallers(uintptr_t base, size_t size, uintptr_t target, uintptr_t* returns,
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
            if (bytes < 5 || section->VirtualAddress > size || bytes > size - section->VirtualAddress)
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
#endif

// The update's thread, inside its loop: the game has just stored this user's listener and is handing the same
// forward, up and position to the middleware. Decides what that hand-off is given -- `nine` itself, or `handed`
// filled with the head's -- and, when the record agreed, rewrites user 0's record to match. No allocation, no
// logging, no waiting (one flag that is tried, never waited for). `entry`: the address of this call's return
// address (dev builds: the update's own caller is read from its frame; null otherwise).
__declspec(noinline) const float* Halo4ListenerApplyHead(const float* nine, int32_t user, uintptr_t caller,
                                                         const uint8_t* entry, float handed[9]) noexcept
{
    using namespace halo4_sound_listener;
    auto& feature = g_halo4ListenerHead;
    ListenerUse use = ListenerUse::NoSample;
    const float* pass = nine;
    volatile bool holding = false;  // this call is user 0's and holds `inside` (the handler reads it too)
    __try
    {
        ListenerRequest request;
        request.fromUpdate = caller == feature.updateReturn;
        request.user = user;
        float game[9];
        memcpy(game, nine, sizeof(game));  // one read of the caller's floats
        const ListenerSites& sites = feature.sites;
        ListenerRecord record;
        RecordState state = RecordState::Unreadable;
        uint8_t* listener = nullptr;  // output user 0's is the first of the array: offsets are from the globals
        const bool local = request.fromUpdate && user == 0;
        if (local)
        {
            listener = feature.globalsSlot ? *feature.globalsSlot : nullptr;
            if (listener)
            {
                const uint8_t* matrix = listener + sites.matrixOffset;
                record.readable = true;
                memcpy(&record.header, listener + sites.headerOffset, sizeof(record.header));
                memcpy(&record.footer, listener + sites.footerOffset, sizeof(record.footer));
                record.valid = listener[sites.validOffset];
                memcpy(&record.scale, matrix + kMatrixScale, sizeof(float));
                memcpy(record.basis.forward, matrix + kMatrixForward, sizeof(record.basis.forward));
                memcpy(record.basis.left, matrix + kMatrixLeft, sizeof(record.basis.left));
                memcpy(record.basis.up, matrix + kMatrixUp, sizeof(record.basis.up));
                memcpy(record.position, matrix + kMatrixPosition, sizeof(record.position));
                memcpy(record.velocity, listener + sites.velocityOffset, sizeof(record.velocity));
            }
            state = CheckRecord(record, game);
            feature.states[static_cast<size_t>(state)].fetch_add(1, std::memory_order_relaxed);
            // The proof runs whatever the switch says: it is about the game, not about the player's choice.
            if (feature.inside.exchange(true, std::memory_order_acquire))
                feature.overlaps.fetch_add(1, std::memory_order_relaxed);
            else
            {
                holding = true;
                const ProofStep step = StepProof(feature.proof, state, game);
                if (step == ProofStep::Proven)
                {
                    feature.proven.store(true, std::memory_order_release);
                    feature.provenCount.fetch_add(1, std::memory_order_relaxed);
                }
                else if (step == ProofStep::Withdrawn)
                {
                    feature.proven.store(false, std::memory_order_release);
                    feature.withdrawnCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        request.proven = feature.proven.load(std::memory_order_acquire);
        request.switchOn = g_config.audio_follows_head;
        const uint32_t parts = static_cast<uint32_t>(g_config.audio_head_halo4_parts);
        request.movePosition = (parts & 4u) != 0;
        request.nowUs = Sight_NowUs();
        const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
        request.generation = generation == feature.generation ? generation : 0;
        HeadFrame frame;
        bool gameplay = false;
        if (!Halo4ReadHeadFrame(frame, gameplay))
        {
            frame = HeadFrame{};
            feature.mixedReads.fetch_add(1, std::memory_order_relaxed);
        }
        request.gameplay = gameplay;
        ListenerResult result;
        use = DecideListener(request, frame, game, record, state, result);
        if (ListenerChanged(use))
        {
            if (result.fromEye)
                feature.fromEye.fetch_add(1, std::memory_order_relaxed);
            if (parts & 1u)
            {
                memcpy(handed, result.handOff, sizeof(result.handOff));
                pass = handed;
            }
            if ((parts & 2u) && result.recordToo && listener)
            {
                uint8_t* matrix = listener + sites.matrixOffset;
                memcpy(matrix + kMatrixForward, result.basis.forward, sizeof(result.basis.forward));
                memcpy(matrix + kMatrixLeft, result.basis.left, sizeof(result.basis.left));
                memcpy(matrix + kMatrixUp, result.basis.up, sizeof(result.basis.up));
                memcpy(matrix + kMatrixPosition, result.position, sizeof(result.position));
                memcpy(listener + sites.velocityOffset, result.velocity, sizeof(result.velocity));
                feature.recordWrites.fetch_add(1, std::memory_order_relaxed);
            }
            float gameForward[3], gameUp[3], gamePosition[3];
            FromHandOff(game, gameForward, gameUp, gamePosition);
            for (int i = 0; i < 3; ++i)
            {
                feature.lastGameForward[i].store(gameForward[i], std::memory_order_relaxed);
                feature.lastHeadForward[i].store(result.basis.forward[i], std::memory_order_relaxed);
                feature.lastHeadUp[i].store(result.basis.up[i], std::memory_order_relaxed);
            }
            feature.lastMoved.store(Distance(result.position, gamePosition), std::memory_order_relaxed);
        }
        if (!request.fromUpdate)
            feature.otherReturn.store(caller, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (local)
        {
            if (head_relative::SampleUse(frame.sample, request.generation, request.nowUs) == head_relative::Use::Ok &&
                head_relative::Finite3(frame.position) && head_relative::Finite3(frame.aimForward))
                Halo4ListenerMeasureBuild(game, pass, frame);
            uintptr_t updateCaller = 0;
            if (entry)
                memcpy(&updateCaller, entry + sizeof(void*) + sites.updateFrameBytes, sizeof(updateCaller));
            Halo4ListenerNoteBuild(updateCaller, ListenerChanged(use));
        }
#else
        (void)entry;
#endif
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (holding)
            feature.inside.store(false, std::memory_order_release);
        feature.faults.fetch_add(1, std::memory_order_relaxed);
        return nine;
    }
    if (holding)
        feature.inside.store(false, std::memory_order_release);
    feature.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
    return pass;
}
__declspec(noinline) uint64_t __fastcall Halo4ListenerHandOffDetour(const float* nine, int32_t user)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
#if HALOMCCVR_DEV_DIAGNOSTICS
    const uint8_t* entry = reinterpret_cast<const uint8_t*>(_AddressOfReturnAddress());
#else
    const uint8_t* entry = nullptr;
#endif
    auto& feature = g_halo4ListenerHead;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t answer = 0;
    __try
    {
        // The private copy lives in this frame for the length of the call: the hand-off copies what it reads into
        // its own command and keeps no pointer to the nine floats.
        float handed[9];
        const float* pass = nine;
        if (nine && feature.enabled.load(std::memory_order_acquire))
            pass = Halo4ListenerApplyHead(nine, user, caller, entry, handed);
        if (const auto original = reinterpret_cast<Halo4ListenerHandOffFn>(feature.original))
            answer = CallNativeWithSpareSlots(original, pass, user);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
    return answer;
}

// What the image exports under `name`, read from its own export table; 0 = no such export, or a forwarder.
// Cold: the title's install thread.
__declspec(noinline) uintptr_t Halo4ListenerExported(uintptr_t base, size_t size, const char* name) noexcept
{
    __try
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_EXPORT_DIRECTORY) ||
            directory.VirtualAddress >= size || directory.Size > size - directory.VirtualAddress)
            return 0;
        const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
        const uint64_t nameCount = exports->NumberOfNames, functionCount = exports->NumberOfFunctions;
        const uint64_t names = exports->AddressOfNames, ordinals = exports->AddressOfNameOrdinals;
        const uint64_t functions = exports->AddressOfFunctions;
        if (names + nameCount * 4 > size || ordinals + nameCount * 2 > size || functions + functionCount * 4 > size)
            return 0;
        const size_t length = strlen(name) + 1;
        for (uint64_t i = 0; i < nameCount; ++i)
        {
            uint32_t nameRva = 0, rva = 0;
            uint16_t ordinal = 0;
            memcpy(&nameRva, reinterpret_cast<const void*>(base + names + i * 4), sizeof(nameRva));
            if (nameRva >= size || length > size - nameRva ||
                memcmp(reinterpret_cast<const void*>(base + nameRva), name, length) != 0)
                continue;
            memcpy(&ordinal, reinterpret_cast<const void*>(base + ordinals + i * 2), sizeof(ordinal));
            if (ordinal >= functionCount)
                return 0;
            memcpy(&rva, reinterpret_cast<const void*>(base + functions + ordinal * 4ull), sizeof(rva));
            // An address inside the export directory is a forwarder's text, not this image's code.
            if (!rva || rva >= size ||
                (rva >= directory.VirtualAddress && rva - directory.VirtualAddress < directory.Size))
                return 0;
            return base + rva;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return 0;
}

bool RemoveHalo4SoundListener()
{
    auto& feature = g_halo4ListenerHead;
    feature.enabled.store(false, std::memory_order_release);
    if (!feature.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(feature.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("AUDIO HEAD Halo 4: CleanupRequired: disable failed (%d)", static_cast<int>(disabled));
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo4ListenerHandOffDetour)};
    const void* trampolines[]{feature.original};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, feature.callbacks))
    {
        LOG("AUDIO HEAD Halo 4: CleanupRequired: the detour is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(feature.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("AUDIO HEAD Halo 4: CleanupRequired: removal failed (%d)", static_cast<int>(removed));
        return false;
    }
    feature.target = nullptr;
    feature.original = nullptr;
    feature.globalsSlot = nullptr;
    return true;
}
// Cold, on the title's install thread, after the tracker's. Binds nothing unless every static link holds on this
// image; a refusal is the game's own listener, said once. The tracker binds or fails on its own.
bool InstallHalo4SoundListener(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace halo4_sound_listener;
    auto& feature = g_halo4ListenerHead;
    if (feature.target)
    {
        LOG("AUDIO HEAD Halo 4: StockFallback: the previous install was not retired");
        return false;
    }
    if (!base || !generation || size < 0x1000)
    {
        LOG("AUDIO HEAD Halo 4: StockFallback: no title image or generation");
        return false;
    }
    const uintptr_t head = Halo4HeadRelativeUnique(base, size, kListenerUpdateHeadPattern);
    const uintptr_t site = Halo4HeadRelativeUnique(base, size, kListenerHandOffSitePattern);
    const uintptr_t writer = Halo4HeadRelativeUnique(base, size, kListenerSignatureWriterPattern);
    const uintptr_t handOff = Halo4HeadRelativeUnique(base, size, kListenerHandOffPattern);
    const uintptr_t exported = Halo4ListenerExported(base, size, kListenerHandOffExport);
    uint8_t headBytes[kUpdateHeadBytes]{}, siteBytes[kSiteBytes]{}, writerBytes[kWriterBytes]{};
    uint8_t handOffBytes[kHandOffBytes]{};
    const bool headRead = Halo4HeadRelativeRead(base, size, head, headBytes, kUpdateHeadBytes) &&
                          Halo4HeadRelativeFunctionRva(base, head) == static_cast<int64_t>(head - base);
    const bool siteRead = Halo4HeadRelativeRead(base, size, site, siteBytes, kSiteBytes);
    const bool writerRead = Halo4HeadRelativeRead(base, size, writer, writerBytes, kWriterBytes);
    const bool handOffRead = Halo4HeadRelativeRead(base, size, handOff, handOffBytes, kHandOffBytes) &&
                             Halo4HeadRelativeFunctionRva(base, handOff) == static_cast<int64_t>(handOff - base);
    ListenerSites sites;
    ListenerBind bind =
        DecodeListenerSites(headRead ? headBytes : nullptr, static_cast<int64_t>(head - base),
                            siteRead ? siteBytes : nullptr, static_cast<int64_t>(site - base),
                            siteRead ? Halo4HeadRelativeFunctionRva(base, site) : -1,
                            writerRead ? writerBytes : nullptr, handOffRead ? handOffBytes : nullptr,
                            static_cast<int64_t>(handOff - base),
                            exported ? static_cast<int64_t>(exported - base) : -1, sites);
    if (bind == ListenerBind::Bound)
    {
        const uintptr_t builder = base + static_cast<uintptr_t>(sites.builderRva);
        const uintptr_t transform = base + static_cast<uintptr_t>(sites.transformRva);
        uint8_t builderBytes[kMatrixBuilderBytes]{}, transformBytes[kVelocityTransformBytes]{};
        uint8_t signMaskBytes[sizeof(uint32_t)]{}, slotBytes[sizeof(void*)]{};
        const bool builderRead = Halo4HeadRelativeFunctionRva(base, builder) == sites.builderRva &&
                                 Halo4HeadRelativeRead(base, size, builder, builderBytes, kMatrixBuilderBytes);
        const bool transformRead =
            Halo4HeadRelativeFunctionRva(base, transform) == sites.transformRva &&
            Halo4HeadRelativeRead(base, size, transform, transformBytes, kVelocityTransformBytes);
        const bool signMaskRead = Halo4HeadRelativeRead(base, size, base + static_cast<uintptr_t>(sites.signMaskRva),
                                                        signMaskBytes, sizeof(signMaskBytes));
        bind = CheckListenerHelpers(builderRead ? builderBytes : nullptr, transformRead ? transformBytes : nullptr,
                                    signMaskRead ? signMaskBytes : nullptr);
        // The globals pointer's own slot must be the image's, and readable.
        if (bind == ListenerBind::Bound &&
            !Halo4HeadRelativeRead(base, size, base + static_cast<uintptr_t>(sites.globalsRva), slotBytes,
                                   sizeof(slotBytes)))
            bind = ListenerBind::Implausible;
    }
    if (bind != ListenerBind::Bound)
    {
        LOG("AUDIO HEAD Halo 4: StockFallback: %s (update %s, store-and-hand-off site %s, signature writer %s, "
            "hand-off %s, its export %s); the game's own listener. Retail-derived lead, not H4EK-verified",
            ListenerBindName(bind), head ? "matched" : "unmatched", site ? "matched" : "unmatched",
            writer ? "matched" : "unmatched", handOff ? "matched" : "unmatched", exported ? "found" : "not found");
        return false;
    }
    feature.base = base;
    feature.size = size;
    feature.generation = generation;
    feature.sites = sites;
    feature.updateReturn = base + static_cast<uintptr_t>(sites.returnRva);
    feature.globalsSlot = reinterpret_cast<uint8_t* const*>(base + static_cast<uintptr_t>(sites.globalsRva));
    feature.proof = HandOffProof{};
    feature.inside.store(false, std::memory_order_release);
    feature.proven.store(false, std::memory_order_release);
    feature.provenCount.store(0, std::memory_order_relaxed);
    feature.withdrawnCount.store(0, std::memory_order_relaxed);
    feature.reportedSwitch = -1;
    feature.reportedProven = feature.reportedWithdrawn = 0;
    feature.reportedFirstUse = feature.reportedFirstRefusal = feature.reportedOtherCaller = false;
    feature.otherReturn.store(0, std::memory_order_relaxed);
    for (auto& count : feature.uses)
        count.store(0, std::memory_order_relaxed);
    for (auto& count : feature.states)
        count.store(0, std::memory_order_relaxed);
    feature.recordWrites.store(0, std::memory_order_relaxed);
    feature.fromEye.store(0, std::memory_order_relaxed);
    feature.faults.store(0, std::memory_order_relaxed);
    feature.mixedReads.store(0, std::memory_order_relaxed);
    feature.overlaps.store(0, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        auto& measure = g_halo4ListenerMeasure;
        measure.found = Halo4ListenerCallers(base, size, head, measure.returns, kHalo4ListenerSiteCapacity);
        measure.count_sites = measure.found < kHalo4ListenerSiteCapacity ? measure.found : kHalo4ListenerSiteCapacity;
        measure.count.store(0, std::memory_order_relaxed);
        measure.ownCamera.store(0, std::memory_order_relaxed);
        for (int i = 0; i < kHalo4MeasureCount; ++i)
        {
            measure.sum[i].store(0, std::memory_order_relaxed);
            measure.peak[i].store(0, std::memory_order_relaxed);
        }
        for (int i = 0; i <= kHalo4ListenerSiteCapacity; ++i)
        {
            measure.calls[i].store(0, std::memory_order_relaxed);
            measure.changed[i].store(0, std::memory_order_relaxed);
            measure.thread[i].store(0, std::memory_order_relaxed);
        }
    }
#endif
    void* target = reinterpret_cast<void*>(handOff);
    const auto created =
        MH_CreateHook(target, reinterpret_cast<void*>(&Halo4ListenerHandOffDetour), &feature.original);
    if (created != MH_OK)
    {
        feature.original = nullptr;
        LOG("AUDIO HEAD Halo 4: StockFallback: hook creation failed (%d)", static_cast<int>(created));
        return false;
    }
    feature.target = target;
    const auto enabled = MH_EnableHook(target);
    if (enabled != MH_OK)
    {
        LOG("AUDIO HEAD Halo 4: StockFallback: hook enable failed (%d)", static_cast<int>(enabled));
        (void)RemoveHalo4SoundListener();
        return false;
    }
    feature.enabled.store(true, std::memory_order_release);
    LOG("AUDIO HEAD Halo 4: Installed (RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED): the listener hand-off "
        "halo4.dll+0x%llX is hooked (the image exports it as AK::SoundEngine::SetListenerPosition); the listener "
        "update +0x%llX calls it (returning to +0x%llX) with nine floats "
        "(forward, up, position, each x negated) right after it stores the same three in user 0's record: the "
        "sound globals (pointer at +0x%llX) + 0x%X, 0x%X long between the game's own 'lshd' and 'lsft', matrix at "
        "+0x%X (builder +0x%llX), velocity at +0x%X (transform +0x%llX). UNPROVEN until those nine floats have "
        "equalled that record's forward, up and position on %u calls in a row: until then nothing is changed. "
        "Then, in gameplay and from a head sample no older than %u ms, the middleware's copy and the record carry "
        "the head's forward and up, and the listener moves by the head's offset from the game's camera; switch=%d "
        "parts=%d",
        static_cast<unsigned long long>(sites.handOffRva), static_cast<unsigned long long>(sites.updateRva),
        static_cast<unsigned long long>(sites.returnRva), static_cast<unsigned long long>(sites.globalsRva),
        sites.headerOffset, sites.stride, sites.matrixOffset, static_cast<unsigned long long>(sites.builderRva),
        sites.velocityOffset, static_cast<unsigned long long>(sites.transformRva), kProofCalls,
        static_cast<unsigned>(head_relative::kMaxSampleAgeUs / 1000), g_config.audio_follows_head ? 1 : 0,
        g_config.audio_head_halo4_parts & 7);
    return true;
}

const char* Halo4ListenerUseName(size_t use)
{
    return halo4_sound_listener::ListenerUseName(static_cast<halo4_sound_listener::ListenerUse>(use));
}
#if HALOMCCVR_DEV_DIAGNOSTICS
const char* Halo4ListenerStateName(size_t state)
{
    return halo4_sound_listener::RecordStateName(static_cast<halo4_sound_listener::RecordState>(state));
}
#endif
// "name=count, name=count" for a report line, leaving out the entries below `first`; returns the length written.
int Halo4ListenerCounts(char* text, size_t capacity, const uint32_t* counts, size_t count, size_t first,
                        const char* (*name)(size_t))
{
    int length = 0;
    text[0] = 0;
    for (size_t i = first; i < count; ++i)
    {
        if (!counts[i] || length >= static_cast<int>(capacity) - 64)
            continue;
        const int wrote = _snprintf_s(text + length, capacity - length, _TRUNCATE, "%s%s=%u", length ? ", " : "",
                                      name(i), counts[i]);
        if (wrote > 0)
            length += wrote;
    }
    return length;
}
// Degrees of a unit forward: its heading counter-clockwise from +X, and its elevation.
void Halo4ListenerAngles(const float forward[3], float& yawDeg, float& pitchDeg)
{
    yawDeg = atan2f(forward[1], forward[0]) * head_relative::kRadToDeg;
    pitchDeg = asinf(Clamp(forward[2], -1.0f, 1.0f)) * head_relative::kRadToDeg;
}
// The worker's report (every 2 s). Every build: the proof passing or being withdrawn, the switch changing, the
// first use and the first refusal after an install. Dev builds: the window's counts and the measurement.
void ReportHalo4SoundListener()
{
    using namespace halo4_sound_listener;
    auto& feature = g_halo4ListenerHead;
    if (!feature.target)
        return;
    const uint32_t provenCount = feature.provenCount.load(std::memory_order_relaxed);
    const uint32_t withdrawnCount = feature.withdrawnCount.load(std::memory_order_relaxed);
    if (provenCount != feature.reportedProven)
    {
        feature.reportedProven = provenCount;
        LOG("AUDIO HEAD Halo 4: PROVEN at run time: the hand-off's nine floats equalled user 0's listener record "
            "(forward, up and position, each x negated) on %u calls in a row; the listener may now be given the "
            "head",
            kProofCalls);
    }
    if (withdrawnCount != feature.reportedWithdrawn)
    {
        feature.reportedWithdrawn = withdrawnCount;
        LOG("AUDIO HEAD Halo 4: PROOF WITHDRAWN: the hand-off's nine floats stopped equalling user 0's listener "
            "record (%u calls in a row); the game's own listener until it is proven again",
            kProofWithdrawRun);
    }
    const int switchNow = g_config.audio_follows_head ? 1 : 0;
    if (feature.reportedSwitch != switchNow)
    {
        if (feature.reportedSwitch >= 0)
            LOG("AUDIO HEAD Halo 4: switch %s: sound is now heard from %s", switchNow ? "on" : "off",
                switchNow ? "your head (once proven)" : "the game's own camera (the aim)");
        feature.reportedSwitch = switchNow;
    }
    uint32_t uses[kHalo4ListenerUseCount]{}, states[kHalo4ListenerStateCount]{};
    uint32_t refused = 0, differed = 0;
    constexpr size_t firstRefusal = static_cast<size_t>(ListenerUse::OtherCaller);
    for (size_t i = 0; i < kHalo4ListenerUseCount; ++i)
    {
        uses[i] = feature.uses[i].exchange(0, std::memory_order_relaxed);
        if (i >= firstRefusal)
            refused += uses[i];
    }
    for (size_t i = 0; i < kHalo4ListenerStateCount; ++i)
    {
        states[i] = feature.states[i].exchange(0, std::memory_order_relaxed);
        if (i != static_cast<size_t>(RecordState::Agrees))
            differed += states[i];
    }
    const uint32_t both = uses[static_cast<size_t>(ListenerUse::Used)];
    const uint32_t handOffOnly = uses[static_cast<size_t>(ListenerUse::HandOffOnly)];
    const uint32_t changed = both + handOffOnly;
    const uint32_t recordWrites = feature.recordWrites.exchange(0, std::memory_order_relaxed);
    const uint32_t fromEye = feature.fromEye.exchange(0, std::memory_order_relaxed);
    const uint32_t faults = feature.faults.exchange(0, std::memory_order_relaxed);
    const uint32_t mixed = feature.mixedReads.exchange(0, std::memory_order_relaxed);
    const uint32_t overlaps = feature.overlaps.exchange(0, std::memory_order_relaxed);
    if (!changed && !refused && !faults)
        return;
    const uintptr_t otherReturn = feature.otherReturn.load(std::memory_order_relaxed);
    if (otherReturn && !feature.reportedOtherCaller)
    {
        feature.reportedOtherCaller = true;
        // The hand-off is an export: a caller outside the image is possible, and has no offset in it.
        const bool inside = otherReturn >= feature.base && otherReturn - feature.base < feature.size;
        LOG("AUDIO HEAD Halo 4: the hand-off was also called from another place (returning to %s0x%llX): that "
            "call is forwarded untouched",
            inside ? "halo4.dll+" : "an address outside halo4.dll, ",
            static_cast<unsigned long long>(inside ? otherReturn - feature.base : otherReturn));
    }
    char reasons[384];
    const int length = Halo4ListenerCounts(reasons, sizeof(reasons), uses, kHalo4ListenerUseCount, firstRefusal,
                                           &Halo4ListenerUseName);
    float gameForward[3], headForward[3], headUp[3];
    for (int i = 0; i < 3; ++i)
    {
        gameForward[i] = feature.lastGameForward[i].load(std::memory_order_relaxed);
        headForward[i] = feature.lastHeadForward[i].load(std::memory_order_relaxed);
        headUp[i] = feature.lastHeadUp[i].load(std::memory_order_relaxed);
    }
    float gameYaw = 0.0f, gamePitch = 0.0f, headYaw = 0.0f, headPitch = 0.0f;
    Halo4ListenerAngles(gameForward, gameYaw, gamePitch);
    Halo4ListenerAngles(headForward, headYaw, headPitch);
    // The head's lean: how far its up leans from the upright up of the same forward (0 = level).
    const float headLean = asinf(Clamp(headUp[0] * headForward[1] - headUp[1] * headForward[0], -1.0f, 1.0f)) *
                           head_relative::kRadToDeg;
    // World units to centimetres (the world scale is world units per metre).
    const float worldScale = g_worldScale.load(std::memory_order_acquire);
    const float cmPerUnit = std::isfinite(worldScale) && worldScale > 1.0e-4f ? 100.0f / worldScale : 0.0f;
    const float movedCm = feature.lastMoved.load(std::memory_order_relaxed) * cmPerUnit;
    if (changed && !feature.reportedFirstUse)
    {
        feature.reportedFirstUse = true;
        LOG("AUDIO HEAD Halo 4: following the head: the listener now faces yaw %.1f pitch %.1f deg, lean %.1f deg, "
            "moved %.1f cm (the game's own listener faced yaw %.1f pitch %.1f deg)",
            headYaw, headPitch, headLean, movedCm, gameYaw, gamePitch);
    }
    if (!changed && !feature.reportedFirstRefusal)
    {
        feature.reportedFirstRefusal = true;
        LOG("AUDIO HEAD Halo 4: the game's own listener for now: %s%s", length ? reasons : "a fault",
            faults && length ? ", and faults" : "");
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    char recordStates[256];
    const int statesLength = Halo4ListenerCounts(recordStates, sizeof(recordStates), states, kHalo4ListenerStateCount,
                                                 static_cast<size_t>(RecordState::Unreadable),
                                                 &Halo4ListenerStateName);
    LOG("AUDIO HEAD Halo 4 (dev): changed %u (the middleware's copy and the record %u, the middleware's copy only "
        "%u; record writes %u; %u of them built from one of the mod's eye cameras); the game's listener yaw %.1f "
        "pitch %.1f deg, the head's yaw %.1f pitch %.1f lean %.1f deg, moved %.1f cm (last); proof %s: the record "
        "agreed %u, did not %u (%s); refused %u: %s; faults %u, mixed sample reads %u, overlapping builds %u; the "
        "head published on thread %u; switch=%d parts=%d",
        changed, both, handOffOnly, recordWrites, fromEye, gameYaw, gamePitch, headYaw, headPitch, headLean, movedCm,
        feature.proven.load(std::memory_order_acquire) ? "PASSED" : "pending",
        states[static_cast<size_t>(RecordState::Agrees)], differed,
        statesLength ? recordStates : "none", refused, length ? reasons : "none", faults, mixed, overlaps,
        g_halo4HeadPublication.thread.load(std::memory_order_relaxed), switchNow,
        g_config.audio_head_halo4_parts & 7);
    // The measurement: one line that says what the game's listener follows and what the lane left.
    auto& measure = g_halo4ListenerMeasure;
    const uint32_t measured = measure.count.exchange(0, std::memory_order_relaxed);
    const uint32_t ownCamera = measure.ownCamera.exchange(0, std::memory_order_relaxed);
    float mean[kHalo4MeasureCount]{}, peak[kHalo4MeasureCount]{};
    for (int i = 0; i < kHalo4MeasureCount; ++i)
    {
        const bool distance = i == kHalo4MeasureBuiltHeadDistance || i == kHalo4MeasureLeftHeadDistance;
        const bool asBuilt = i == kHalo4MeasureBuiltHeadDeg || i == kHalo4MeasureBuiltAimDeg ||
                             i == kHalo4MeasureBuiltHeadDistance;
        const uint32_t samples = asBuilt ? ownCamera : measured;
        const double unit = distance ? cmPerUnit / 1000000.0 : 1.0 / 1000.0;
        const uint64_t sum = measure.sum[i].exchange(0, std::memory_order_relaxed);
        const uint32_t top = measure.peak[i].exchange(0, std::memory_order_relaxed);
        mean[i] = samples ? static_cast<float>(static_cast<double>(sum) / samples * unit) : 0.0f;
        peak[i] = static_cast<float>(top * unit);
    }
    if (measured)
    {
        float driveYaw = 0.0f;
        const bool driveLive = Halo4TrackerAimReference(driveYaw);
        // No build from the game's own camera in this window: nothing to say about the game's listener.
        const Follows built = ownCamera ? WhatItFollows(mean[kHalo4MeasureBuiltHeadDeg],
                                                        mean[kHalo4MeasureBuiltAimDeg], mean[kHalo4MeasureApartDeg])
                                        : Follows::Inconclusive;
        const Follows left = WhatItFollows(mean[kHalo4MeasureLeftHeadDeg], mean[kHalo4MeasureLeftAimDeg],
                                           mean[kHalo4MeasureApartDeg]);
        LOG("AUDIO HEAD Halo 4 (dev): MEASURE over %u builds of user 0's listener (mean, max): the head and the "
            "aim were %.1f deg apart (%.1f). AS THE GAME BUILT IT (%u builds from its own camera; %u more copied "
            "one of the mod's eye cameras and are left out): forward %.1f deg from the head's (%.1f) and "
            "%.1f deg from the aim's (%.1f), position %.1f cm from the head (%.1f): the game's listener %s. AS THE "
            "LANE LEFT IT (the middleware's copy, all builds): forward %.1f deg from the head's (%.1f) and %.1f "
            "deg from the aim's (%.1f), position %.1f cm from the head (%.1f): it %s. The aim here is the game's "
            "own camera of the frame the head was published from (its yaw %.1f deg); the direct drive %s (yaw "
            "%.1f deg)",
            measured, mean[kHalo4MeasureApartDeg], peak[kHalo4MeasureApartDeg], ownCamera, measured - ownCamera,
            mean[kHalo4MeasureBuiltHeadDeg],
            peak[kHalo4MeasureBuiltHeadDeg], mean[kHalo4MeasureBuiltAimDeg], peak[kHalo4MeasureBuiltAimDeg],
            mean[kHalo4MeasureBuiltHeadDistance], peak[kHalo4MeasureBuiltHeadDistance], FollowsName(built),
            mean[kHalo4MeasureLeftHeadDeg], peak[kHalo4MeasureLeftHeadDeg], mean[kHalo4MeasureLeftAimDeg],
            peak[kHalo4MeasureLeftAimDeg], mean[kHalo4MeasureLeftHeadDistance],
            peak[kHalo4MeasureLeftHeadDistance], FollowsName(left),
            measure.lastAimYawDeg.load(std::memory_order_relaxed),
            driveLive ? "is live and last wrote this aim" : "has no live aim to compare",
            driveLive ? halo4_head_relative::WrapPi(driveYaw) * head_relative::kRadToDeg : 0.0f);
    }
    // The builds of this window by the update's own caller: which ran, how often, on which thread, how many changed.
    char builds[512];
    int written = 0;
    builds[0] = 0;
    for (int i = 0; i <= kHalo4ListenerSiteCapacity; ++i)
    {
        const uint32_t calls = measure.calls[i].exchange(0, std::memory_order_relaxed);
        const uint32_t turned = measure.changed[i].exchange(0, std::memory_order_relaxed);
        if (!calls || written >= static_cast<int>(sizeof(builds)) - 96)
            continue;
        const bool known = i < measure.count_sites;
        const int wrote = _snprintf_s(
            builds + written, sizeof(builds) - written, _TRUNCATE, "%s%s+0x%llX x%u (thread %u, changed %u)",
            written ? ", " : "", known ? "returning to halo4.dll" : "another caller ",
            static_cast<unsigned long long>(known ? measure.returns[i] - feature.base : 0), calls,
            measure.thread[i].load(std::memory_order_relaxed), turned);
        if (wrote > 0)
            written += wrote;
    }
    LOG("AUDIO HEAD Halo 4 (dev): listener builds by the update's caller (%d direct call sites in the image): %s",
        measure.found, written ? builds : "none");
#else
    (void)mixed;
    (void)overlaps;
    (void)recordWrites;
    (void)differed;
    (void)handOffOnly;
    (void)fromEye;
#endif
}
