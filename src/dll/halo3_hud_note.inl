// THE VEHICLE CAMERA NOTE IN HALO 3'S OWN HUD MESSAGES (vehicle_camera_note = 1; hud_note_logic.h the rules).
// Included in game.cpp after halo3_head_relative.inl.
//
// The note ("Mongoose - Over the Shoulder, Untethered - Motion") is posted through the function the game posts its
// own short HUD lines with, so it is drawn in the HUD's font, in the HUD's place, for as long as the HUD keeps any
// such line. MCC's own use of it is the "no navpoints" line: halo3 +0x2DDC60 posts it to the local user's queue
// with type 3 after checking the game is a campaign ([TLS+0x48]+0x10 == 1: the game mode, which halo3.dll compares
// with 1, 2 and 3 at some 190 sites -- campaign, multiplayer, the main menu; its line is campaign-only). This posts
// the same way, in a campaign or a multiplayer game (custom games, Forge).
//
//   +0x2D3F2C  post(user, text, type)  user < 4: the user's HUD block, [TLS+0xF0] + user*0x28C8 + 0x1F8C, to:
//   +0x2D4C34  the queue: five slots of 0xE8 bytes; the text is COPIED (96 characters with the terminator),
//              stamped with the game time at [TLS+0xC8]+0xC; with every slot in use the oldest is replaced.
//
// PROOF (addrcascade: resolved + guarded, nothing hardcoded): the post function by a unique signature that names
// the HUD users' place in TLS (0xF0) but not the HUD block's size and offset (0x28C8, 0x1F8C), which a host may
// change: Project Reclaimer enlarges every user's HUD block (0x28C8 -> 0x7FC8, its fields 0x5700 further on,
// 206 patches across the HUD code, these two among them), and the post function then reaches its own layout
// whatever it is; its one call lands on the queue, matched in place by a second
// signature that names [TLS+0xC8]; both read the SAME TLS index variable, and so does MCC's own caller, found by a
// third unique signature that names the game-mode check -- and whose type-3 call lands on the post function.
// Any one missing or disagreeing: not bound, and the note uses the mod's panel (NativeNotice_Show) as before.
//
// THREAD. The post runs on the thread that owns the HUD: the main loop's frame (+0xB2538) runs the HUD update
// (+0x2D1390, at +0xB2B0B) and then the sound listener update (+0xAB650, via +0xA9508 at +0xB2B63), which
// halo3_head_relative.inl already hooks. That hook's detour calls Halo3HudNotePost after the game's update, so the
// note is posted between two of the game's own HUD updates, never during a HUD draw. Every post also requires the
// calling thread's TLS block to hold the HUD block, the game clock and a campaign or multiplayer game.
//
// CO-OP. A HUD line is presentation, but it lives in the game's own state; in a shared co-op simulation the mod
// leaves that state as the game keeps it (Sight_FireTimeChangesAllowed false) and shows the note on its panel.
// Project Reclaimer asks its simulation type instead (hud_note::SessionAllows).

using Halo3HudMessageFn = void(__fastcall*)(int32_t user, const wchar_t* text, int32_t type);

inline constexpr char kHalo3HudMessageSig[] =
    "48 83 EC 28 83 F9 04 7D ?? 44 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 BB F0 00 00 00 4E 8B 14 C8 "
    "48 63 C1 48 69 C8 ?? ?? 00 00 4B 8B 04 1A 48 05 ?? ?? 00 00 48 03 C8 E8 ?? ?? ?? ?? 48 83 C4 28 C3";
inline constexpr size_t kHalo3HudMessageTlsDisp = 12;  // its `mov r9d, [rip+disp]`: the TLS index variable
inline constexpr size_t kHalo3HudMessageCallRel = 59;  // its call's rel32: the queue
inline constexpr char kHalo3HudQueueSig[] =
    "48 85 D2 0F 84 ?? ?? ?? ?? 48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 45 33 "
    "F6 41 8B F0 4C 8B D9 66 44 39 32 0F 84 ?? ?? ?? ?? 44 8B 0D ?? ?? ?? ?? 41 83 CA FF 65 48 8B 04 25 58 00 00 "
    "00 45 8B C6 BD C8 00 00 00";
// How many bytes a signature's text names ("48 ?? 8B" is three).
constexpr size_t Halo3HudPatternBytes(const char* pattern)
{
    size_t count = 0;
    bool inByte = false;
    for (; *pattern; ++pattern)
    {
        const bool space = *pattern == ' ';
        if (!space && !inByte)
            ++count;
        inByte = !space;
    }
    return count;
}
inline constexpr size_t kHalo3HudQueueSigBytes = Halo3HudPatternBytes(kHalo3HudQueueSig);
inline constexpr size_t kHalo3HudQueueTlsDisp = 56;
inline constexpr char kHalo3HudNavpointCallerSig[] =
    "48 89 5C 24 08 48 89 74 24 10 57 48 81 EC 30 08 00 00 65 48 8B 04 25 58 00 00 00 8B 15 ?? ?? ?? ?? 48 63 F1 "
    "48 8B 3C D0 B8 48 00 00 00 48 8B 04 38 80 78 10 01 0F 85";
inline constexpr size_t kHalo3HudNavpointCallerTlsDisp = 29;
inline constexpr size_t kHalo3HudNavpointCallerBytes = 0xD5;
inline constexpr char kHalo3HudNavpointPostSig[] = "85 F6 78 ?? 41 B8 03 00 00 00 48 8D 54 24 30 8B CE E8";
inline constexpr size_t kHalo3HudNavpointPostRel = 18;
// Each offset into a matched signature lies inside it (a rel32 or displacement is the 4 bytes after its opcode).
static_assert(kHalo3HudQueueSigBytes == 81 && kHalo3HudQueueTlsDisp + 4 <= kHalo3HudQueueSigBytes);
static_assert(Halo3HudPatternBytes(kHalo3HudMessageSig) == 68 && kHalo3HudMessageCallRel + 4 < 68 &&
              kHalo3HudMessageTlsDisp + 4 < 68);
static_assert(kHalo3HudNavpointCallerTlsDisp + 4 <= Halo3HudPatternBytes(kHalo3HudNavpointCallerSig));
static_assert(kHalo3HudNavpointPostRel == Halo3HudPatternBytes(kHalo3HudNavpointPostSig));
// The fields the post and MCC's caller read, named by the signatures above.
inline constexpr size_t kHalo3TlsGameGlobals = 0x48; // -> +0x10: the game mode, 1 campaign, 2 multiplayer
inline constexpr uint8_t kHalo3GameModeCampaign = 1;
inline constexpr uint8_t kHalo3GameModeMultiplayer = 2;
inline constexpr size_t kHalo3TlsGameTime = 0xC8;
inline constexpr size_t kHalo3TlsHudUsers = 0xF0;
inline constexpr int32_t kHalo3HudMessageType = 3;

struct Halo3HudNote
{
    std::atomic<Halo3HudMessageFn> post{nullptr};
    const uint32_t* tlsIndex = nullptr; // written before `post` is published, cleared after it is withdrawn
    // The mailbox: the cold worker's latest note. `serial` and the texts change together under the lock; `wanted`
    // mirrors `serial` for the game thread's lock-free check.
    SRWLOCK lock = SRWLOCK_INIT;
    uint32_t serial = 0;
    uint64_t askedMs = 0;
    wchar_t hudText[hud_note::kMaxChars + 1]{};
    std::wstring panelText;
    float panelSeconds = 0.0f;
    std::atomic<uint32_t> wanted{0};
    std::atomic<uint32_t> shown{0}; // claimed by whichever side showed it: the game's HUD or the mod's panel
    std::atomic<uint32_t> posted{0}, notReady{0}, faults{0}, panelFallbacks{0};
    std::atomic<int> notReadyWhy{0}; // the last Halo3HudNoteThreadReady refusal (Halo3HudNoteNotReadyText)
    uint32_t reportedPosted = 0, reportedFallbacks = 0, reportedFaults = 0; // cold worker
};
Halo3HudNote g_halo3HudNote;

bool InstallHalo3HudNote(uintptr_t base, size_t size, uint32_t generation)
{
    auto& note = g_halo3HudNote;
    note.post.store(nullptr, std::memory_order_release);
    const auto rip = [](uintptr_t at, size_t disp) { return sig::RipTarget(at + disp, at + disp + 4); };
    const char* why = nullptr;
    uintptr_t fn = 0, queue = 0, caller = 0, site = 0, tls = 0;
    if (!(fn = mcc_clock::FindUnique(base, size, kHalo3HudMessageSig)))
        why = "the post function's signature";
    else if (!(queue = rip(fn, kHalo3HudMessageCallRel)) || queue < base || queue >= base + size ||
             sig::Find(queue, kHalo3HudQueueSigBytes, kHalo3HudQueueSig) != queue)
        why = "the queue at the post function's call";
    else if ((tls = rip(fn, kHalo3HudMessageTlsDisp)) != rip(queue, kHalo3HudQueueTlsDisp))
        why = "the TLS index the post and the queue read";
    else if (g_engineTlsIndex && tls != reinterpret_cast<uintptr_t>(g_engineTlsIndex))
        why = "the TLS index against the engine's";
    else if (!(caller = mcc_clock::FindUnique(base, size, kHalo3HudNavpointCallerSig)) ||
             rip(caller, kHalo3HudNavpointCallerTlsDisp) != tls)
        why = "MCC's own caller (the game-mode check)";
    else if (!(site = sig::Find(caller, kHalo3HudNavpointCallerBytes, kHalo3HudNavpointPostSig)) ||
             rip(site, kHalo3HudNavpointPostRel) != fn)
        why = "MCC's own type-3 post";
    if (why)
    {
        LOG("HUD NOTE Halo 3: not bound (%s did not prove); the vehicle camera note uses the mod's own panel", why);
        return false;
    }
    note.tlsIndex = reinterpret_cast<const uint32_t*>(tls);
    note.post.store(reinterpret_cast<Halo3HudMessageFn>(fn), std::memory_order_release);
    LOG("HUD NOTE Halo 3: bound (generation %u): post +0x%llX -> queue +0x%llX, the way MCC's own +0x%llX posts its "
        "type-3 line; posted from the sound listener update's hook, after the HUD update, on the HUD's thread",
        generation, static_cast<unsigned long long>(fn - base), static_cast<unsigned long long>(queue - base),
        static_cast<unsigned long long>(caller - base));
    return true;
}

// Before halo3.dll goes: called once the listener hook is retired, so nothing is inside a post.
void RemoveHalo3HudNote()
{
    g_halo3HudNote.post.store(nullptr, std::memory_order_release);
    g_halo3HudNote.tlsIndex = nullptr;
}

namespace
{
bool Halo3HudNoteSessionAllows()
{
    return hud_note::SessionAllows(Sight_FireTimeChangesAllowed(), g_hostIsProjectReclaimer,
                                   Sight_SessionType(GameTitle::Halo3));
}

// The calling thread holds the HUD block, the game clock and a campaign or multiplayer game: 0. Otherwise the first
// thing missing (Halo3HudNoteNotReadyText). Guarded: no C++ object here.
int Halo3HudNoteThreadReady(const uint32_t* tlsIndex)
{
    __try
    {
        const auto* slots = reinterpret_cast<const uint8_t* const*>(__readgsqword(0x58));
        const uint8_t* block = slots ? slots[*tlsIndex] : nullptr;
        if (!block)
            return 1;
        const auto* game = *reinterpret_cast<const uint8_t* const*>(block + kHalo3TlsGameGlobals);
        if (!game)
            return 2;
        if (game[0x10] != kHalo3GameModeCampaign && game[0x10] != kHalo3GameModeMultiplayer)
            return 3;
        if (!*reinterpret_cast<void* const*>(block + kHalo3TlsGameTime))
            return 4;
        if (!*reinterpret_cast<void* const*>(block + kHalo3TlsHudUsers))
            return 5;
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 6;
    }
}

const char* Halo3HudNoteNotReadyText(int why)
{
    switch (why)
    {
    case 1: return "the thread holds no game block";
    case 2: return "no game globals";
    case 3: return "not a campaign or multiplayer game";
    case 4: return "no game clock";
    case 5: return "no HUD block";
    case 6: return "a read faulted";
    default: return "none";
    }
}

bool Halo3HudNoteCall(Halo3HudMessageFn post, const wchar_t* text)
{
    __try
    {
        CallNativeWithSpareSlots(post, 0, text, kHalo3HudMessageType);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

// The game thread, from the listener update's detour after the game's update: post the latest note the HUD has not
// shown. The common case is two atomic loads.
void Halo3HudNotePost()
{
    auto& note = g_halo3HudNote;
    const uint32_t shown = note.shown.load(std::memory_order_acquire);
    if (note.wanted.load(std::memory_order_acquire) == shown)
        return;
    const Halo3HudMessageFn post = note.post.load(std::memory_order_acquire);
    if (!post || !Halo3HudNoteSessionAllows())
        return; // the cold worker's fallback shows it on the panel
    wchar_t text[hud_note::kMaxChars + 1];
    uint32_t serial = 0;
    uint64_t askedMs = 0;
    AcquireSRWLockShared(&note.lock);
    serial = note.serial;
    askedMs = note.askedMs;
    memcpy(text, note.hudText, sizeof(text));
    ReleaseSRWLockShared(&note.lock);
    const uint64_t now = GetTickCount64();
    if (!hud_note::Due(serial, shown, now >= askedMs ? now - askedMs : 0))
        return;
    if (const int why = Halo3HudNoteThreadReady(note.tlsIndex))
    {
        note.notReady.fetch_add(1, std::memory_order_relaxed);
        note.notReadyWhy.store(why, std::memory_order_relaxed);
        return;
    }
    uint32_t expected = shown;
    if (!note.shown.compare_exchange_strong(expected, serial, std::memory_order_acq_rel))
        return; // the panel took it
    text[hud_note::kMaxChars] = L'\0';
    if (Halo3HudNoteCall(post, text))
        note.posted.fetch_add(1, std::memory_order_relaxed);
    else
        note.faults.fetch_add(1, std::memory_order_relaxed);
}

// Cold worker: show a vehicle camera note the way vehicle_camera_note says.
void Halo3VehicleNoteShow(const std::wstring& text, float seconds)
{
    auto& note = g_halo3HudNote;
    const bool hudReady = note.post.load(std::memory_order_acquire) &&
                          g_halo3HeadAudio.ready.load(std::memory_order_acquire);
    const hud_note::Route route =
        hud_note::Choose(g_config.vehicle_camera_note, hudReady, Halo3HudNoteSessionAllows());
    if (route != hud_note::Route::GameHud)
    {
        // A note still waiting for the game is older than this one: neither side shows it now.
        uint32_t shown = note.shown.load(std::memory_order_acquire);
        const uint32_t wanted = note.wanted.load(std::memory_order_acquire);
        if (wanted != shown)
            note.shown.compare_exchange_strong(shown, wanted, std::memory_order_acq_rel);
        if (route == hud_note::Route::Panel)
            NativeNotice_Show(text.c_str(), seconds);
        return;
    }
    const std::wstring hud = hud_note::ForGameHud(text);
    AcquireSRWLockExclusive(&note.lock);
    const size_t length = (std::min)(hud.size(), hud_note::kMaxChars);
    memcpy(note.hudText, hud.c_str(), length * sizeof(wchar_t));
    note.hudText[length] = L'\0';
    note.panelText = text;
    note.panelSeconds = seconds;
    note.askedMs = GetTickCount64();
    note.serial = note.serial + 1 ? note.serial + 1 : 1;
    note.wanted.store(note.serial, std::memory_order_release);
    ReleaseSRWLockExclusive(&note.lock);
}

// Cold worker, every pass: a note the game has not taken in time goes to the panel; and a line when the counts move.
void Halo3VehicleNoteTick()
{
    auto& note = g_halo3HudNote;
    const uint32_t shown = note.shown.load(std::memory_order_acquire);
    if (note.wanted.load(std::memory_order_acquire) != shown)
    {
        AcquireSRWLockShared(&note.lock);
        const uint32_t serial = note.serial;
        const uint64_t askedMs = note.askedMs;
        ReleaseSRWLockShared(&note.lock);
        const uint64_t now = GetTickCount64();
        uint32_t expected = shown;
        if (hud_note::Overdue(serial, shown, now >= askedMs ? now - askedMs : 0) &&
            note.shown.compare_exchange_strong(expected, serial, std::memory_order_acq_rel))
        {
            AcquireSRWLockShared(&note.lock);
            const std::wstring text = note.panelText;
            const float seconds = note.panelSeconds;
            ReleaseSRWLockShared(&note.lock);
            NativeNotice_Show(text.c_str(), seconds);
            note.panelFallbacks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const uint32_t posted = note.posted.load(std::memory_order_relaxed);
    const uint32_t fallbacks = note.panelFallbacks.load(std::memory_order_relaxed);
    const uint32_t faults = note.faults.load(std::memory_order_relaxed);
    if (posted != note.reportedPosted || fallbacks != note.reportedFallbacks || faults != note.reportedFaults)
    {
        note.reportedPosted = posted;
        note.reportedFallbacks = fallbacks;
        note.reportedFaults = faults;
        LOG("HUD NOTE Halo 3: %u in the game's HUD messages, %u on the mod's panel after the game did not take them "
            "(%u frames not ready, the last because %s), %u faulted",
            posted, fallbacks, note.notReady.load(std::memory_order_relaxed),
            Halo3HudNoteNotReadyText(note.notReadyWhy.load(std::memory_order_relaxed)), faults);
    }
}
