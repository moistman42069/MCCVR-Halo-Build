// HALO 3 PART DRAW: a vehicle camera's "hideParts" and "fadeParts" -- the parts of the player's own vehicle whose
// material it names are not drawn (a hornet's glass inside its hull mesh, the Longsword's glass beside its screen),
// or are drawn with less of their tint and glare. Included by game.cpp after the avatar (halo3_avatar.inl); the cold
// worker's half is halo3_vehicle_parts.inl, the fading rule halo3_glass_fade.h.
//
// THE PART DRAW. halo3.dll draws one part of one mesh of one object per call of its part draw (+0x2B5590 on retail,
// found by a unique signature, or behind another program's detour of it: FindOnce), from the opaque and the
// transparent passes alike; neither caller reads a result. Its record (rcx) names the object's row (+8: info, whose
// +4 is the render model's tag index and +0x48 the object), the mesh (+0x1A) and the part (+0x18); the part's
// material is the first int16 of the render model's mesh -> parts entry. A call whose object, model and material
// the seat's camera hides is not passed on; every other call is, untouched.
//
// WHERE A MATERIAL'S PARAMETERS REACH THE GPU (fading; read from the shader select +0x28A9FC). Inside that call,
// when the material's shaders are bound anew, two uploads (+0x2898DC and +0x289A68, one per shader stage) map that
// stage's constant buffer and copy the render method's real parameters into it straight from tag memory, by a
// mapping in the shader's entry (a word at entry+8, resp. entry+2: count << 10 | first; each mapping is four bytes,
// the low byte of its first word the constant register, its third byte the parameter). Then the material's bind
// (+0x289B90) overlays the ANIMATED parameters: it copies them all into a buffer of its own, lets +0x2894B0 apply the
// animated functions to that copy, and writes only the animated ones into the mapped buffers. The commit comes after.
// So a fade scales, for the draw it is noted for: each upload's mapped buffer right after the upload filled it, and
// the bind's copy before its animated parameters are written out (once per bind: a value already scaled is left).
// Each is checked to be of the postprocess entry the cold worker read for that material, so a material the game
// swapped in for this draw is left alone. The shader select skips the uploads while the same shaders stay bound; a
// faded draw clears that note (+0x28A9FC's own "nothing cached" byte) before and after itself, so its own uploads
// run and the next draw of that material by anything else uploads it again unfaded.
//
// PRECOMPILED CONSTANTS. Most materials do not upload at all: their postprocess entry holds, at +0x84, a handle to a
// constant buffer built once (-1: none), and the shader set (+0x28A3F0) then binds that buffer and skips both uploads
// (measured: 1100 faded draws per 6 s reached the select for the material, 0 uploads ran in them). Both paths bind
// the material's constants to the same register (13, vertex and pixel stage: the persistent buffer at +0x28A6F7 and
// +0x28A75C, the uploaded ones at +0x28AB8B and through +0x2AFC14), so a faded draw sets that handle to -1 for its own
// duration -- the game then uploads this material as it does one without a precompiled buffer, and the fade scales
// those uploads -- and puts it back before anything else draws. That one int in the material's tag data is the only
// thing written there, on the render thread, inside the draw.
//
// INSTALLED ONLY WHILE WANTED. The report (every 2 s) installs the part draw hook while the camera file asks any
// Halo 3 camera for "hideParts" or "fadeParts", the bind and upload hooks while it asks for "fadeParts", and removes
// each when the file no longer does; retirement with Halo 3 removes them all first. Forwards go through
// CallNativeWithSpareSlots; tools/audit_caller_area.py finds none of the four natives relying on its caller's stack or
// volatile registers. While installed and nothing is published, their cost per call is an atomic load or two and a
// thread-local test.
//
// CO-OP. Only this machine's drawing: nothing the simulation reads is touched.

namespace halo3_part_draw
{
using DrawFn = void(__fastcall*)(void*, uint32_t);
using BindFn = uint64_t(__fastcall*)(uint32_t, void*, void*, void*, void*, void*, void*);
using UploadFn = void(__fastcall*)(void*, void*, void*);
struct Hook
{
    void* target = nullptr;
    void* original = nullptr;
    bool refused = false; // its signature was not found for this binding: not tried again
};
struct Runtime
{
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> skipped{0}, faded{0}, uploads{0}, swapped{0};
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Dev builds: how far a faded draw gets -- draws noted, the shader select left holding this material's render
    // method after one (it ran for it), and upload calls in all and during a noted draw.
    std::atomic<uint64_t> fadeDraws{0}, fadeSelected{0}, uploadCalls{0}, uploadCallsInFade{0}, bindCallsInFade{0},
        fadeDynamic{0};
#endif
    Hook draw, bind, uploadA, uploadB;
    uint8_t* const* mappedA = nullptr; // the upload's mapped constant buffer, as each stores it
    uint8_t* const* mappedB = nullptr;
    volatile uint8_t* cacheNote = nullptr; // the shader select's "the shaders bound are cached" byte
    uintptr_t base = 0;
    size_t size = 0;
} runtime;

// What the part draw notes for the uploads and the bind inside it, on its own thread.
struct FadeScope
{
    bool active = false;
    Halo3PartFade fade{};
    uint32_t writtenMask = 0;
    float written[halo3_glass_fade::kMaxParameters][4]{};
};
thread_local FadeScope fadeScope;

// ADDR-HYGIENE: resolved. The part draw's own prologue and its first reads (the record's info, the tag instance
// table, the info's render model index), unique in retail halo3.dll; the hook goes where it matches.
inline constexpr const char* kDrawPattern =
    "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 89 50 10 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B 41 08 33 ED "
    "48 8B F1 44 8B FD 48 8B 0D ?? ?? ?? ?? 44 0F B7 40 04";
// ADDR-HYGIENE: resolved. The animated-parameter step's prologue and first writes (its fifth argument into r13, the
// buffer into rbp, the fourth argument's word cleared), unique in retail halo3.dll.
inline constexpr const char* kBindPattern =
    "48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 89 48 08 57 41 54 41 55 41 56 41 57 48 83 EC 30 4C 8B AC 24 80 00 "
    "00 00 45 33 DB 48 8B EA 66 45 89 19";
// ADDR-HYGIENE: resolved. The two uploads, from their entry to where each stores its mapped buffer (the last four
// bytes, a rip-relative disp32: the slot this reads): the postprocess's real and integer blocks, the entry's size
// byte (+0x18 / +0x19), the map call for constant slot 2 / 1, and the mapping word (+8 / +2). Unique in retail.
inline constexpr const char* kUploadAPattern =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 83 79 20 00 48 8B DA 48 8B 15 ?? ?? "
    "?? ?? 49 8B F0 75 04 33 FF EB 07 8B 41 20 48 8D 3C 82 83 79 2C 00 8B 69 34 75 05 45 33 F6 EB 07 8B 41 2C 4C 8D "
    "34 82 8A 43 18 84 C0 0F 84 ?? ?? ?? ?? 0F B6 D0 4C 8D 05 ?? ?? ?? ?? C1 E2 04 B9 02 00 00 00 89 15 ?? ?? ?? ?? "
    "E8 ?? ?? ?? ?? 0F B7 4B 08 45 33 C0 41 BB 00 FC FF FF 48 89 05 ?? ?? ?? ??";
inline constexpr size_t kUploadABytes = 136, kUploadAMapped = 132, kUploadAWord = 8;
inline constexpr const char* kUploadBPattern =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 83 79 20 00 48 8B DA 48 8B 15 ?? ?? ?? ?? 49 8B E8 "
    "75 04 33 FF EB 07 8B 41 20 48 8D 3C 82 83 79 2C 00 75 04 33 F6 EB 07 8B 41 2C 48 8D 34 82 8A 43 19 84 C0 0F 84 "
    "?? ?? ?? ?? 0F B6 D0 4C 8D 05 ?? ?? ?? ?? C1 E2 04 B9 01 00 00 00 89 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F B7 4B 02 "
    "45 33 C0 41 BA 00 FC FF FF 48 89 05 ?? ?? ?? ??";
inline constexpr size_t kUploadBBytes = 127, kUploadBMapped = 123, kUploadBWord = 2;
// ADDR-HYGIENE: resolved. The shader select's prologue and its first test, `cmp byte [rip+disp32], 0` (the disp32 at
// +27, an imm8 after it): the byte that says the bound shaders are cached. Unique in retail.
inline constexpr const char* kSelectPattern =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30 80 3D ?? ?? ?? ?? 00 41 8B C1 40 8A 6C "
    "24 68 41 8B F0 8B 7C 24 60";
inline constexpr size_t kSelectBytes = 47, kSelectNote = 27;

// The object, render model and part material this call draws. Guarded: a read that faults is not a part of ours.
__declspec(noinline) bool ReadPart(const void* record, uint32_t& object, uint32_t& model, uint32_t& material)
{
    __try
    {
        const auto* r = static_cast<const uint8_t*>(record);
        const uint8_t* info = nullptr;
        std::memcpy(&info, r + 8, sizeof(info));
        if (!info)
            return false;
        uint32_t flags = 0;
        uint16_t tag = 0, meshIndex = 0, partIndex = 0;
        std::memcpy(&object, info + 0x48, 4);
        std::memcpy(&flags, info + 0x58, 4);
        std::memcpy(&tag, info + 4, 2);
        if (flags & 0x10)
            return false; // a first-person row
        const uint8_t* render = Halo3LoadedTagDefinition(tag);
        const uint8_t* tagBase = g_halo3TagDataBase ? static_cast<const uint8_t*>(*g_halo3TagDataBase) : nullptr;
        if (!render || !tagBase)
            return false;
        std::memcpy(&meshIndex, r + 0x1A, 2);
        std::memcpy(&partIndex, r + 0x18, 2);
        uint32_t meshCount = 0, meshAddress = 0, partCount = 0, partAddress = 0;
        std::memcpy(&meshCount, render + 0x68, 4);
        std::memcpy(&meshAddress, render + 0x6C, 4);
        if (meshIndex >= meshCount || !meshAddress)
            return false;
        const uint8_t* mesh = tagBase + static_cast<size_t>(meshAddress) * 4 + static_cast<size_t>(meshIndex) * 0x4C;
        std::memcpy(&partCount, mesh, 4);
        std::memcpy(&partAddress, mesh + 4, 4);
        if (partIndex >= partCount || !partAddress)
            return false;
        int16_t index = -1;
        std::memcpy(&index, tagBase + static_cast<size_t>(partAddress) * 4 + static_cast<size_t>(partIndex) * 0x10, 2);
        if (index < 0)
            return false;
        model = tag;
        material = static_cast<uint32_t>(index);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The player's own view of a seat (the seated body's gate, without the avatar's own state).
bool SeatView()
{
    const RuntimeMode mode = TitleAdapter_GetRuntimeMode();
    return g_enabled.load() && VR_IsStereoEnabled() && !g_scopeRenderActive.load() && !VR_IsCutsceneTheaterActive() &&
           TitleAdapter_GetActiveTitle() == GameTitle::Halo3 &&
           (mode == RuntimeMode::Vehicle || mode == RuntimeMode::Turret);
}

// The shader select's cached-shaders note cleared, so the next select uploads. Guarded.
__declspec(noinline) void ForgetBoundShaders()
{
    __try
    {
        if (runtime.cacheNote)
            *runtime.cacheNote = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

#if HALOMCCVR_DEV_DIAGNOSTICS
bool SelectHolds(uintptr_t method);
#endif

// For one faded draw: the material's precompiled-constants handle (postprocess +0x84) set to -1, so the game uploads
// its constants. False when it already has none. Guarded.
inline constexpr size_t kPrecompiledConstants = 0x84;
__declspec(noinline) bool UsePerDrawConstants(uintptr_t postprocess, int32_t& saved)
{
    __try
    {
        auto* handle = reinterpret_cast<volatile int32_t*>(postprocess + kPrecompiledConstants);
        saved = *handle;
        if (saved == -1)
            return false;
        *handle = -1;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
__declspec(noinline) void RestorePrecompiledConstants(uintptr_t postprocess, int32_t saved)
{
    __try
    {
        *reinterpret_cast<volatile int32_t*>(postprocess + kPrecompiledConstants) = saved;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void __fastcall DrawHook(void* record, uint32_t entry)
{
    Halo3HookCalled(Halo3Hook::PartDraw);
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool fading = false;
    bool perDraw = false;
    int32_t savedHandle = -1;
    uintptr_t fadedEntry = 0;
    __try
    {
        bool skip = false;
        const bool hides = g_halo3VehicleHide.partsAny.load(std::memory_order_relaxed) != 0;
        const bool fades = g_halo3VehicleFade.any.load(std::memory_order_relaxed) != 0;
        uint32_t object = 0, model = 0, material = 0;
        if ((hides || fades) && ReadPart(record, object, model, material) && SeatView())
        {
            const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
            if (hides && Halo3VehicleHidesMaterial(generation, object, model, material))
                skip = true;
            else if (fades && Halo3VehicleFadeFor(generation, object, model, material, fadeScope.fade))
            {
                fadeScope.active = true;
                fadeScope.writtenMask = 0;
                fading = true;
                fadedEntry = fadeScope.fade.postprocess;
                perDraw = UsePerDrawConstants(fadedEntry, savedHandle);
                ForgetBoundShaders(); // its own uploads run, whatever was bound before it
            }
        }
        if (skip)
            runtime.skipped.fetch_add(1, std::memory_order_relaxed);
        else
            CallNativeWithSpareSlots(reinterpret_cast<DrawFn>(runtime.draw.original), record, entry);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (fading)
        {
            runtime.fadeDraws.fetch_add(1, std::memory_order_relaxed);
            if (perDraw)
                runtime.fadeDynamic.fetch_add(1, std::memory_order_relaxed);
            if (SelectHolds(fadeScope.fade.method))
                runtime.fadeSelected.fetch_add(1, std::memory_order_relaxed);
        }
#endif
    }
    __finally
    {
        fadeScope.active = false;
        if (perDraw)
            RestorePrecompiledConstants(fadedEntry, savedHandle);
        if (fading)
            ForgetBoundShaders(); // the next draw of this material, by anything, binds it unfaded
        runtime.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// Dev builds: whether the shader select's cache now holds `method` (it stores the render method it selected 8 bytes
// after its cached-shaders byte). Guarded.
__declspec(noinline) bool SelectHolds(uintptr_t method)
{
    __try
    {
        if (!runtime.cacheNote || !method)
            return false;
        uintptr_t held = 0;
        std::memcpy(&held, const_cast<const uint8_t*>(runtime.cacheNote) + 8, sizeof(held));
        return held == method;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
#endif

// The noted fade applied to one parameter value the game just wrote, once per bind. Not guarded: its callers are.
void FadeValue(uint32_t parameter, float (&value)[4], bool once)
{
    FadeScope& scope = fadeScope;
    const auto op = static_cast<halo3_glass_fade::Op>(scope.fade.op[parameter]);
    if (op == halo3_glass_fade::Op::None)
        return;
    if (once && (scope.writtenMask & (1u << parameter)) &&
        std::memcmp(value, scope.written[parameter], sizeof(value)) == 0)
        return; // this bind's copy already holds the faded value
    halo3_glass_fade::Apply(op, scope.fade.kept, value);
    if (once)
    {
        std::memcpy(scope.written[parameter], value, sizeof(value));
        scope.writtenMask |= 1u << parameter;
    }
}

// An upload of `postprocess` just filled its stage's mapped buffer by the mapping at `table` that the word at
// `entry` + `wordAt` names: the parameters the fade names, scaled in that buffer. Guarded.
__declspec(noinline) void FadeUpload(uint8_t* const* mapped, const void* postprocess, const void* entry, size_t wordAt,
                                     const void* table)
{
    __try
    {
        FadeScope& scope = fadeScope;
        if (reinterpret_cast<uintptr_t>(postprocess) != scope.fade.postprocess)
        {
            runtime.swapped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        uint8_t* buffer = mapped ? *mapped : nullptr;
        if (!buffer || !entry || !table)
            return;
        uint16_t word = 0;
        std::memcpy(&word, static_cast<const uint8_t*>(entry) + wordAt, 2);
        const uint32_t count = word >> 10, first = word & 0x3FFu;
        const auto* mapping = static_cast<const uint8_t*>(table);
        bool any = false;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t* m = mapping + static_cast<size_t>(first + i) * 4;
            const uint32_t reg = m[0], parameter = m[2];
            if (parameter >= scope.fade.parameters || parameter >= static_cast<uint32_t>(halo3_glass_fade::kMaxParameters) ||
                scope.fade.op[parameter] == 0)
                continue;
            float value[4];
            std::memcpy(value, buffer + static_cast<size_t>(reg) * 16, sizeof(value));
            FadeValue(parameter, value, false);
            std::memcpy(buffer + static_cast<size_t>(reg) * 16, value, sizeof(value));
            any = true;
        }
        if (any)
            runtime.uploads.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void __fastcall UploadAHook(void* postprocess, void* entry, void* table)
{
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        CallNativeWithSpareSlots(reinterpret_cast<UploadFn>(runtime.uploadA.original), postprocess, entry, table);
#if HALOMCCVR_DEV_DIAGNOSTICS
        runtime.uploadCalls.fetch_add(1, std::memory_order_relaxed);
        if (fadeScope.active)
            runtime.uploadCallsInFade.fetch_add(1, std::memory_order_relaxed);
#endif
        if (fadeScope.active)
            FadeUpload(runtime.mappedA, postprocess, entry, kUploadAWord, table);
    }
    __finally
    {
        runtime.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void __fastcall UploadBHook(void* postprocess, void* entry, void* table)
{
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        CallNativeWithSpareSlots(reinterpret_cast<UploadFn>(runtime.uploadB.original), postprocess, entry, table);
#if HALOMCCVR_DEV_DIAGNOSTICS
        runtime.uploadCalls.fetch_add(1, std::memory_order_relaxed);
        if (fadeScope.active)
            runtime.uploadCallsInFade.fetch_add(1, std::memory_order_relaxed);
#endif
        if (fadeScope.active)
            FadeUpload(runtime.mappedB, postprocess, entry, kUploadBWord, table);
    }
    __finally
    {
        runtime.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// The bind's copy of the parameters (`parameters`, one float4 each), with the noted fade applied once. `entry` is
// the bind's sixth argument, the postprocess entry + 0x50 (its seventh is the entry + 0x5C). Guarded.
__declspec(noinline) void FadeCopy(void* parameters, const void* entry)
{
    __try
    {
        FadeScope& scope = fadeScope;
        if (reinterpret_cast<uintptr_t>(entry) - 0x50 != scope.fade.postprocess)
        {
            runtime.swapped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto* values = static_cast<float(*)[4]>(parameters);
        uint32_t count = scope.fade.parameters;
        if (count > static_cast<uint32_t>(halo3_glass_fade::kMaxParameters))
            count = halo3_glass_fade::kMaxParameters;
        const uint32_t before = scope.writtenMask;
        for (uint32_t k = 0; k < count; ++k)
            FadeValue(k, values[k], true);
        if (scope.writtenMask != before)
            runtime.faded.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

uint64_t __fastcall BindHook(uint32_t a1, void* parameters, void* a3, void* a4, void* a5, void* entry, void* a7)
{
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        result = CallNativeWithSpareSlots(reinterpret_cast<BindFn>(runtime.bind.original), a1, parameters, a3, a4, a5,
                                          entry, a7);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (fadeScope.active)
            runtime.bindCallsInFade.fetch_add(1, std::memory_order_relaxed);
#endif
        if (fadeScope.active && parameters && entry)
            FadeCopy(parameters, entry);
    }
    __finally
    {
        runtime.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

bool Remove(Hook& hook, const void* detour, const char* what)
{
    if (!hook.target)
        return true;
    const MH_STATUS disabled = MCCVR_DisableHookForRetirement(hook.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("Halo 3 vehicle parts CleanupRequired: the %s hook did not disable (MinHook status %d)", what,
            static_cast<int>(disabled));
        return false;
    }
    const void* hooks[] = {detour};
    const void* originals[] = {hook.original};
    if (!WaitForNativeDetourQuiescence(hooks, originals, 1, runtime.callbacks))
    {
        LOG("Halo 3 vehicle parts CleanupRequired: the %s hook is still being run", what);
        return false;
    }
    const MH_STATUS removed = MH_RemoveHook(hook.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("Halo 3 vehicle parts CleanupRequired: the %s hook was not removed (MinHook status %d)", what,
            static_cast<int>(removed));
        return false;
    }
    hook.target = nullptr;
    hook.original = nullptr;
    return true;
}

// The unique match of `pattern`, or 0 (and `hook.refused`, with the log line) when there is none or more than one.
// A function another program has detoured first (foreign_detour.h: Project Reclaimer's client jumps from the part
// draw's first 15 bytes -- the jump and a NOP -- into its own executable) is found by the rest of its signature,
// provided the jump is exactly that absolute jump, padded with nothing but NOPs, and lands in another loaded module's
// executable code. The hook then goes in front of that
// detour, which still runs: the trampoline MinHook makes from the jump continues into it.
uintptr_t FindOnce(Hook& hook, const char* pattern, const char* what)
{
    uintptr_t hit = sig::Find(runtime.base, runtime.size, pattern);
    if (hit && sig::Find(hit + 1, runtime.base + runtime.size - hit - 1, pattern))
    {
        hook.refused = true;
        LOG("Halo 3 vehicle parts: the %s is ambiguous; that part of the camera file is not applied", what);
        return 0;
    }
    if (hit)
        return hit;
    // The jump with no padding first, then with one NOP more each time: the first padding that matches decides.
    std::string jumped;
    uintptr_t detoured = 0;
    size_t padding = 0;
    for (; padding <= foreign_detour::kMaximumPadding && !detoured; ++padding)
    {
        jumped = foreign_detour::JumpedPattern(pattern, padding);
        if (!jumped.empty())
            detoured = sig::Find(runtime.base, runtime.size, jumped.c_str());
    }
    const bool unique = detoured &&
        !sig::Find(detoured + 1, runtime.base + runtime.size - detoured - 1, jumped.c_str());
    if (unique)
    {
        uint8_t head[foreign_detour::kAbsoluteJumpBytes]{};
        std::memcpy(head, reinterpret_cast<const void*>(detoured), sizeof(head));
        const uintptr_t target = static_cast<uintptr_t>(foreign_detour::JumpTarget(head));
        HMODULE owner = nullptr;
        MEMORY_BASIC_INFORMATION page{};
        const bool outside = target < runtime.base || target >= runtime.base + runtime.size;
        const bool inModule = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                 reinterpret_cast<LPCWSTR>(target), &owner) && owner;
        const bool executable = VirtualQuery(reinterpret_cast<const void*>(target), &page, sizeof(page)) ==
                                    sizeof(page) && page.State == MEM_COMMIT &&
                                (page.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                                 PAGE_EXECUTE_WRITECOPY)) != 0;
        if (outside && inModule && executable)
        {
            wchar_t path[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(owner, path, MAX_PATH);
            const wchar_t* leaf = path;
            for (DWORD i = 0; i < length; ++i)
                if (path[i] == L'\\' || path[i] == L'/')
                    leaf = path + i + 1;
            LOG("Halo 3 vehicle parts: the %s is detoured by %ls (its first %zu bytes jump to +0x%llX there); the "
                "hook goes in front of that detour, which still runs after it",
                what, leaf, foreign_detour::kAbsoluteJumpBytes + padding - 1,
                static_cast<unsigned long long>(target - reinterpret_cast<uintptr_t>(owner)));
            return detoured;
        }
        hook.refused = true;
        LOG("Halo 3 vehicle parts: the %s starts with a jump that does not land in another module's code "
            "(outside=%d module=%d executable=%d); that part of the camera file is not applied",
            what, outside ? 1 : 0, inModule ? 1 : 0, executable ? 1 : 0);
        return 0;
    }
    hook.refused = true;
    LOG("Halo 3 vehicle parts: the %s is %s; that part of the camera file is not applied", what,
        detoured ? "ambiguous behind a detour" : "not found");
    return 0;
}

bool InstallAt(Hook& hook, uintptr_t hit, void* detour, const char* what)
{
    void* target = reinterpret_cast<void*>(hit);
    const MH_STATUS created = MH_CreateHook(target, detour, &hook.original);
    if (created != MH_OK)
    {
        hook.refused = true;
        LOG("Halo 3 vehicle parts: the %s hook was not created (MinHook status %d)", what, static_cast<int>(created));
        return false;
    }
    hook.target = target;
    const MH_STATUS enabled = MH_EnableHook(target);
    if (enabled != MH_OK)
    {
        LOG("Halo 3 vehicle parts: the %s hook was not enabled (MinHook status %d)", what, static_cast<int>(enabled));
        (void)Remove(hook, detour, what);
        hook.refused = true;
        return false;
    }
    LOG("Halo 3 vehicle parts: the %s hook is in (+0x%llX)", what, static_cast<unsigned long long>(hit - runtime.base));
    return true;
}

bool Install(Hook& hook, const char* pattern, void* detour, const char* what)
{
    if (hook.target || !runtime.base || hook.refused)
        return hook.target != nullptr;
    const uintptr_t hit = FindOnce(hook, pattern, what);
    return hit && InstallAt(hook, hit, detour, what);
}

// An upload: hooked where its pattern matches, its mapped-buffer slot decoded from the same match.
bool InstallUpload(Hook& hook, const char* pattern, size_t bytes, size_t mappedAt, void* detour, uint8_t* const*& slot,
                   const char* what)
{
    if (hook.target || !runtime.base || hook.refused)
        return hook.target != nullptr;
    const uintptr_t hit = FindOnce(hook, pattern, what);
    if (!hit)
        return false;
    std::vector<uint8_t> code(bytes);
    sig::CopyUnhooked(hit, code.data(), code.size());
    int32_t disp = 0;
    std::memcpy(&disp, code.data() + mappedAt, 4);
    const uintptr_t at = hit + mappedAt + 4 + static_cast<intptr_t>(disp);
    if (at < runtime.base || at + 8 > runtime.base + runtime.size || (at & 7))
    {
        hook.refused = true;
        LOG("Halo 3 vehicle parts: the %s's buffer slot decodes outside the module; fadeParts is not applied", what);
        return false;
    }
    slot = reinterpret_cast<uint8_t* const*>(at);
    return InstallAt(hook, hit, detour, what);
}

// The shader select's cached-shaders byte; null (and fading without forcing the uploads) when not found.
void BindCacheNote()
{
    if (runtime.cacheNote || !runtime.base)
        return;
    Hook none;
    const uintptr_t hit = FindOnce(none, kSelectPattern, "shader select");
    if (!hit)
        return;
    uint8_t code[kSelectBytes] = {};
    sig::CopyUnhooked(hit, code, sizeof(code));
    int32_t disp = 0;
    std::memcpy(&disp, code + kSelectNote, 4);
    const uintptr_t at = hit + kSelectNote + 4 + 1 + static_cast<intptr_t>(disp); // an imm8 follows the disp32
    if (at >= runtime.base && at < runtime.base + runtime.size)
        runtime.cacheNote = reinterpret_cast<volatile uint8_t*>(at);
}
} // namespace halo3_part_draw

// The Halo 3 install: where halo3.dll is. Nothing is hooked until a camera asks for "hideParts" or "fadeParts".
void BindHalo3PartDraw(uintptr_t base, size_t size, uint32_t)
{
    using namespace halo3_part_draw;
    runtime.base = base;
    runtime.size = size;
    for (Hook* hook : {&runtime.draw, &runtime.bind, &runtime.uploadA, &runtime.uploadB})
        hook->refused = false;
    runtime.mappedA = runtime.mappedB = nullptr;
    runtime.cacheNote = nullptr;
}

// The fading hooks out (they only run inside the part draw).
bool RemoveHalo3PartFade()
{
    using namespace halo3_part_draw;
    return Remove(runtime.bind, reinterpret_cast<const void*>(BindHook), "material bind") &&
           Remove(runtime.uploadA, reinterpret_cast<const void*>(UploadAHook), "constant upload A") &&
           Remove(runtime.uploadB, reinterpret_cast<const void*>(UploadBHook), "constant upload B");
}

// Halo 3's retirement, before halo3.dll goes: the fading hooks first, then the part draw.
bool RemoveHalo3PartDraw()
{
    using namespace halo3_part_draw;
    if (!RemoveHalo3PartFade() || !Remove(runtime.draw, reinterpret_cast<const void*>(DrawHook), "part draw"))
        return false;
    runtime.base = 0;
    runtime.size = 0;
    runtime.mappedA = runtime.mappedB = nullptr;
    runtime.cacheNote = nullptr;
    return true;
}

// Every report (about 2 s): each hook in while wanted and out when not, and what they did.
void ReportHalo3PartDraw()
{
    using namespace halo3_part_draw;
    const bool fade = Halo3VehicleCameraFadePartsWanted();
    const bool draw = fade || Halo3VehicleCameraHidePartsWanted();
    if (draw && !runtime.draw.target)
        (void)Install(runtime.draw, kDrawPattern, reinterpret_cast<void*>(DrawHook), "part draw");
    if (fade && runtime.draw.target)
    {
        BindCacheNote();
        if (!runtime.uploadA.target)
            (void)InstallUpload(runtime.uploadA, kUploadAPattern, kUploadABytes, kUploadAMapped,
                                reinterpret_cast<void*>(UploadAHook), runtime.mappedA, "constant upload A");
        if (!runtime.uploadB.target)
            (void)InstallUpload(runtime.uploadB, kUploadBPattern, kUploadBBytes, kUploadBMapped,
                                reinterpret_cast<void*>(UploadBHook), runtime.mappedB, "constant upload B");
        if (!runtime.bind.target)
            (void)Install(runtime.bind, kBindPattern, reinterpret_cast<void*>(BindHook), "material bind");
    }
    if (!fade && (runtime.bind.target || runtime.uploadA.target || runtime.uploadB.target))
        (void)RemoveHalo3PartFade();
    if (!draw && runtime.draw.target && !runtime.bind.target && !runtime.uploadA.target && !runtime.uploadB.target)
        (void)Remove(runtime.draw, reinterpret_cast<const void*>(DrawHook), "part draw");
    static uint64_t shown[4]{};
    static ULONGLONG logged = 0;
    const uint64_t now[4]{runtime.skipped.load(std::memory_order_relaxed), runtime.uploads.load(std::memory_order_relaxed),
                          runtime.faded.load(std::memory_order_relaxed), runtime.swapped.load(std::memory_order_relaxed)};
    const ULONGLONG clock = GetTickCount64();
    if (std::memcmp(now, shown, sizeof(now)) != 0 && clock - logged >= 5000)
    {
        logged = clock;
        LOG("Halo 3 vehicle parts: since the last line, %llu parts of the seat's vehicle left out (hideParts); its glass "
            "faded in %llu constant uploads and %llu animated binds (fadeParts), %llu left as they were (another "
            "material bound for the part)",
            static_cast<unsigned long long>(now[0] - shown[0]), static_cast<unsigned long long>(now[1] - shown[1]),
            static_cast<unsigned long long>(now[2] - shown[2]), static_cast<unsigned long long>(now[3] - shown[3]));
        std::memcpy(shown, now, sizeof(now));
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    static uint64_t probeShown[6]{};
    static ULONGLONG probeLogged = 0;
    const uint64_t probe[6]{runtime.fadeDraws.load(), runtime.fadeSelected.load(), runtime.uploadCalls.load(),
                            runtime.uploadCallsInFade.load(), runtime.bindCallsInFade.load(),
                            runtime.fadeDynamic.load()};
    if (probe[0] != probeShown[0] && clock - probeLogged >= 5000)
    {
        probeLogged = clock;
        LOG("DEV vehicle parts fade probe: since the last line, %llu faded draws, after %llu of them the shader select "
            "held the material's render method; %llu constant uploads in all, %llu during a faded draw; %llu material "
            "binds during a faded draw; %llu faded draws switched from precompiled constants to per-draw uploads",
            static_cast<unsigned long long>(probe[0] - probeShown[0]),
            static_cast<unsigned long long>(probe[1] - probeShown[1]),
            static_cast<unsigned long long>(probe[2] - probeShown[2]),
            static_cast<unsigned long long>(probe[3] - probeShown[3]),
            static_cast<unsigned long long>(probe[4] - probeShown[4]),
            static_cast<unsigned long long>(probe[5] - probeShown[5]));
        std::memcpy(probeShown, probe, sizeof(probe));
    }
#endif
}
