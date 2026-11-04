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
// Halo 4's thread game state (its section below): the game time, and the player mapping whose +0xB8 is output user
// 0's player; the post's 16 players.
inline constexpr size_t kHalo4TlsGameTime = 0x90;
inline constexpr size_t kHalo4TlsPlayers = 0x138;
inline constexpr size_t kHalo4PlayersOutputUser0 = 0xB8;
inline constexpr uint32_t kHalo4HudPlayers = 16;
// Halo CE's players (its section below): the local count and local player 0.
inline constexpr size_t kHaloCEPlayersLocalCount = 0xB4;
inline constexpr size_t kHaloCEPlayersLocal0 = 0xB8;

// ODST posts its HUD lines inline (no post function): the queue is called with the user's queue address, which its
// callers make as [TLS + kOdstTlsHudUsers] + user * 0x3B88 + kOdstHudQueueOffset (user 0's: the HUD users' base
// plus the offset).
using Halo3HudQueueFn = void(__fastcall*)(void* queue, const wchar_t* text, int32_t type);
// Halo 4 posts with the player and no type; Halo CE prints with the local player (their sections below).
using Halo4HudPostFn = void(__fastcall*)(uint32_t player, const wchar_t* text);
using HaloCEHudPrintFn = void(__fastcall*)(int16_t localPlayer, const wchar_t* text);

struct Halo3HudNote
{
    std::atomic<Halo3HudMessageFn> post{nullptr};
    const uint32_t* tlsIndex = nullptr; // written before `post` / `queue` is published, cleared after it is withdrawn
    // A title that posts inline (ODST): the queue itself, and where user 0's queue lies; `post` stays null. Written
    // before `queue` is published.
    std::atomic<Halo3HudQueueFn> queue{nullptr};
    size_t tlsHudUsers = kHalo3TlsHudUsers;
    size_t queueOffset = 0;
    // Whether the thread's game mode is checked before a post (Halo 3: MCC's own caller checks it, at the offset that
    // proves it; ODST's caller does not show where, so a post there needs the game clock and the HUD block alone).
    bool checksGameMode = true;
    // Halo 4: the post takes output user 0's player (the thread's TLS block + tlsPlayers, at +0xB8), and is made only
    // from the listener update the game tick calls (it returns to tickReturn; its frame is updateFrameBytes deep).
    std::atomic<Halo4HudPostFn> playerPost{nullptr};
    size_t tlsPlayers = 0;
    uintptr_t tickReturn = 0;
    uint32_t updateFrameBytes = 0;
    // Halo CE: the print takes local player 0; the image's own pointers to the messages, the game clock and the
    // players say whether it may (no TLS block). Written before `print` is published.
    std::atomic<HaloCEHudPrintFn> print{nullptr};
    const uintptr_t* ceMessages = nullptr;
    const uintptr_t* ceClock = nullptr;
    const uintptr_t* cePlayers = nullptr;
    // How many characters the game's slot keeps, and whether the line may be posted whatever the session (Halo CE's
    // section says why).
    size_t maxChars = hud_note::kMaxChars;
    bool anySession = false;
    // When the hook a title posts from last ran (Halo 4, Halo CE): a note is offered to the game only while it runs.
    std::atomic<uint64_t> siteMs{0};
    const char* const title; // whose note, for the log; fixed at construction, so the cold worker's read never races
    const GameTitle sessionTitle; // whose session type decides whether the note may use the game's HUD
    explicit Halo3HudNote(const char* name, GameTitle session = GameTitle::Halo3) : title(name), sessionTitle(session) {}
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
Halo3HudNote g_halo3HudNote{"Halo 3"};

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
// Whether this session lets the note use the game's HUD, asked of the note's own title.
bool Halo3HudNoteSessionAllows(const Halo3HudNote& note)
{
    return note.anySession ||
        hud_note::SessionAllows(Sight_FireTimeChangesAllowed(), g_hostIsProjectReclaimer,
                                Sight_SessionType(note.sessionTitle));
}

// The calling thread holds the HUD block, the game clock and (where the note checks it) a campaign or multiplayer
// game: 0. Otherwise the first thing missing (Halo3HudNoteNotReadyText). Guarded: no C++ object here.
int Halo3HudNoteThreadReady(const uint32_t* tlsIndex, size_t tlsHudUsers, bool checksGameMode)
{
    __try
    {
        const auto* slots = reinterpret_cast<const uint8_t* const*>(__readgsqword(0x58));
        const uint8_t* block = slots ? slots[*tlsIndex] : nullptr;
        if (!block)
            return 1;
        if (checksGameMode)
        {
            const auto* game = *reinterpret_cast<const uint8_t* const*>(block + kHalo3TlsGameGlobals);
            if (!game)
                return 2;
            if (game[0x10] != kHalo3GameModeCampaign && game[0x10] != kHalo3GameModeMultiplayer)
                return 3;
        }
        if (!*reinterpret_cast<void* const*>(block + kHalo3TlsGameTime))
            return 4;
        if (!*reinterpret_cast<void* const*>(block + tlsHudUsers))
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
    case 7: return "no player mapping";
    case 8: return "no local player";
    case 9: return "no HUD messages";
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

// Halo 4: output user 0's player in this thread's game state, as the game's own posters take it: 0 and `player`, or why
// not (Halo3HudNoteNotReadyText). The post indexes its 16 players' messages by the handle's low word and must never be
// handed one past them.
int Halo4HudNotePlayer(const uint32_t* tlsIndex, size_t tlsPlayers, uint32_t& player)
{
    __try
    {
        const auto* slots = reinterpret_cast<const uint8_t* const*>(__readgsqword(0x58));
        const uint8_t* block = slots ? slots[*tlsIndex] : nullptr;
        if (!block)
            return 1;
        if (!*reinterpret_cast<void* const*>(block + kHalo4TlsGameTime))
            return 4;
        const uint8_t* players = *reinterpret_cast<const uint8_t* const*>(block + tlsPlayers);
        if (!players)
            return 7;
        const uint32_t handle = *reinterpret_cast<const uint32_t*>(players + kHalo4PlayersOutputUser0);
        if (handle == 0xFFFFFFFFu || (handle & 0xFFFFu) >= kHalo4HudPlayers)
            return 8;
        player = handle;
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 6;
    }
}

// Halo CE: the game clock running, the messages allocated, and local player 0 in the game.
int HaloCEHudNoteReady(const Halo3HudNote& note)
{
    __try
    {
        const auto* clock = reinterpret_cast<const uint8_t*>(*note.ceClock);
        if (!clock || !clock[0])
            return 4;
        if (!*note.ceMessages)
            return 9;
        const auto* players = reinterpret_cast<const uint8_t*>(*note.cePlayers);
        if (!players)
            return 7;
        if (*reinterpret_cast<const int16_t*>(players + kHaloCEPlayersLocalCount) < 1 ||
            *reinterpret_cast<const uint32_t*>(players + kHaloCEPlayersLocal0) == 0xFFFFFFFFu)
            return 8;
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 6;
    }
}

bool Halo4HudNoteCall(Halo4HudPostFn post, uint32_t player, const wchar_t* text)
{
    __try
    {
        CallNativeWithSpareSlots(post, player, text);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool HaloCEHudNoteCall(HaloCEHudPrintFn print, const wchar_t* text)
{
    __try
    {
        CallNativeWithSpareSlots(print, static_cast<int16_t>(0), text);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// An inline-posting title's post, made as its own callers make it: user 0's queue in the thread's HUD block.
bool Halo3HudNoteQueueCall(Halo3HudQueueFn queue, const uint32_t* tlsIndex, size_t tlsHudUsers, size_t queueOffset,
                           const wchar_t* text)
{
    __try
    {
        const auto* slots = reinterpret_cast<uint8_t* const*>(__readgsqword(0x58));
        uint8_t* block = slots ? slots[*tlsIndex] : nullptr;
        uint8_t* users = block ? *reinterpret_cast<uint8_t* const*>(block + tlsHudUsers) : nullptr;
        if (!users)
            return false;
        CallNativeWithSpareSlots(queue, static_cast<void*>(users + queueOffset), text, kHalo3HudMessageType);
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
void HudNotePost(Halo3HudNote& note)
{
    const uint32_t shown = note.shown.load(std::memory_order_acquire);
    if (note.wanted.load(std::memory_order_acquire) == shown)
        return;
    const Halo3HudMessageFn post = note.post.load(std::memory_order_acquire);
    const Halo3HudQueueFn queue = note.queue.load(std::memory_order_acquire);
    const Halo4HudPostFn playerPost = note.playerPost.load(std::memory_order_acquire);
    const HaloCEHudPrintFn print = note.print.load(std::memory_order_acquire);
    if ((!post && !queue && !playerPost && !print) || !Halo3HudNoteSessionAllows(note))
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
    uint32_t player = 0;
    if (const int why = print ? HaloCEHudNoteReady(note)
                              : playerPost ? Halo4HudNotePlayer(note.tlsIndex, note.tlsPlayers, player)
                                           : Halo3HudNoteThreadReady(note.tlsIndex, note.tlsHudUsers,
                                                                     note.checksGameMode))
    {
        note.notReady.fetch_add(1, std::memory_order_relaxed);
        note.notReadyWhy.store(why, std::memory_order_relaxed);
        return;
    }
    uint32_t expected = shown;
    if (!note.shown.compare_exchange_strong(expected, serial, std::memory_order_acq_rel))
        return; // the panel took it
    text[note.maxChars] = L'\0';
    const bool called = post ? Halo3HudNoteCall(post, text)
        : queue ? Halo3HudNoteQueueCall(queue, note.tlsIndex, note.tlsHudUsers, note.queueOffset, text)
        : playerPost ? Halo4HudNoteCall(playerPost, player, text)
                     : HaloCEHudNoteCall(print, text);
    if (called)
        note.posted.fetch_add(1, std::memory_order_relaxed);
    else
        note.faults.fetch_add(1, std::memory_order_relaxed);
}

void Halo3HudNotePost()
{
    HudNotePost(g_halo3HudNote);
}

// Cold worker: show a vehicle camera note the way vehicle_camera_note says, through `note` (its title's), whose post
// runs from the sound listener update's hook while `audioReady` (that hook) is.
void VehicleNoteShowOn(Halo3HudNote& note, bool audioReady, const std::wstring& text, float seconds)
{
    const bool hudReady = (note.post.load(std::memory_order_acquire) || note.queue.load(std::memory_order_acquire) ||
                           note.playerPost.load(std::memory_order_acquire) ||
                           note.print.load(std::memory_order_acquire)) && audioReady;
    const hud_note::Route route =
        hud_note::Choose(g_config.vehicle_camera_note, hudReady, Halo3HudNoteSessionAllows(note));
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
    const std::wstring hud = hud_note::ForGameHud(text, note.maxChars);
    AcquireSRWLockExclusive(&note.lock);
    const size_t length = (std::min)(hud.size(), note.maxChars);
    memcpy(note.hudText, hud.c_str(), length * sizeof(wchar_t));
    note.hudText[length] = L'\0';
    note.panelText = text;
    note.panelSeconds = seconds;
    note.askedMs = GetTickCount64();
    note.serial = note.serial + 1 ? note.serial + 1 : 1;
    note.wanted.store(note.serial, std::memory_order_release);
    ReleaseSRWLockExclusive(&note.lock);
}

void Halo3VehicleNoteShow(const std::wstring& text, float seconds)
{
    VehicleNoteShowOn(g_halo3HudNote, g_halo3HeadAudio.ready.load(std::memory_order_acquire), text, seconds);
}

// Cold worker, every pass: a note the game has not taken in time goes to the panel; and a line when the counts move.
void VehicleNoteTickOn(Halo3HudNote& note)
{
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
        LOG("HUD NOTE %s: %u in the game's HUD messages, %u on the mod's panel after the game did not take them "
            "(%u frames not ready, the last because %s), %u faulted",
            note.title, posted, fallbacks, note.notReady.load(std::memory_order_relaxed),
            Halo3HudNoteNotReadyText(note.notReadyWhy.load(std::memory_order_relaxed)), faults);
    }
}

#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
// ---- ODST ------------------------------------------------------------------------------------------------------
// ODST's queue is Halo 3's (kHalo3HudQueueSig matches it once, +0x2FFFCC: five slots of 0xE8, the text copied, the
// game time at [TLS+0xC8]+0xC). ODST has no post function: its callers make the post inline, and MCC's own type-3
// caller (+0x30F234, the "no navpoints" line's homolog) does it as kOdstHudPostSiteSig pins -- `mov r8d, 3` (the type,
// and the highest user), the engine TLS index, [TLS+0x100] (Halo 3's 0xF0), user * 0x3B88 (0x28C8) + 0x2D00 (0x1F8C),
// then the call to the queue. A second caller (+0x2FEE2B) builds the same address with other registers. The post site's
// TLS index, the queue's and the mod's ODST engine TLS index must agree, and the site's call must land on the queue.
// Its game-mode check sits in a caller not proven here, so ODST's post needs the game clock and the HUD block alone;
// the note is only made in a vehicle seat. Posted from ODST's sound listener hook (halo3_head_relative.inl), which runs
// after its HUD update as Halo 3's does. ADDR-HYGIENE: resolved + guarded (cross-checked as above), withdrawn before
// the listener hook is removed.
inline constexpr char kOdstHudPostSiteSig[] =
    "41 B8 03 00 00 00 41 3B F8 77 ?? 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B9 00 01 00 00 48 8B 14 C8 48 "
    "69 CF 88 3B 00 00 4A 8B 04 0A 48 8D 54 24 30 48 05 00 2D 00 00 48 03 C8 E8 ?? ?? ?? ??";
inline constexpr size_t kOdstHudPostSiteTlsDisp = 13; // its `mov ecx, [rip+disp]`: the TLS index variable
inline constexpr size_t kOdstHudPostSiteCallRel = 62; // its call's rel32: the queue
static_assert(Halo3HudPatternBytes(kOdstHudPostSiteSig) == 66 && kOdstHudPostSiteCallRel + 4 == 66 &&
              kOdstHudPostSiteTlsDisp + 4 < 66);
inline constexpr size_t kOdstTlsHudUsers = 0x100;
inline constexpr size_t kOdstHudQueueOffset = 0x2D00;

Halo3HudNote g_odstHudNote{"ODST", GameTitle::Halo3ODST};

bool InstallOdstHudNote(uintptr_t base, size_t size, uint32_t generation)
{
    auto& note = g_odstHudNote;
    note.queue.store(nullptr, std::memory_order_release);
    const auto rip = [](uintptr_t at, size_t disp) { return sig::RipTarget(at + disp, at + disp + 4); };
    const char* why = nullptr;
    uintptr_t site = 0, queue = 0, tls = 0;
    if (!(site = mcc_clock::FindUnique(base, size, kOdstHudPostSiteSig)))
        why = "MCC's own type-3 post";
    else if (!(queue = rip(site, kOdstHudPostSiteCallRel)) || queue < base || queue >= base + size ||
             sig::Find(queue, kHalo3HudQueueSigBytes, kHalo3HudQueueSig) != queue)
        why = "the queue at the post's call";
    else if ((tls = rip(site, kOdstHudPostSiteTlsDisp)) != rip(queue, kHalo3HudQueueTlsDisp))
        why = "the TLS index the post and the queue read";
    else if (!g_odstEngineTlsIndex || tls != reinterpret_cast<uintptr_t>(g_odstEngineTlsIndex))
        why = "the TLS index against the engine's";
    if (why)
    {
        LOG("HUD NOTE ODST: not bound (%s did not prove); the vehicle camera note uses the mod's own panel", why);
        return false;
    }
    note.tlsIndex = reinterpret_cast<const uint32_t*>(tls);
    note.tlsHudUsers = kOdstTlsHudUsers;
    note.queueOffset = kOdstHudQueueOffset;
    note.checksGameMode = false;
    note.queue.store(reinterpret_cast<Halo3HudQueueFn>(queue), std::memory_order_release);
    LOG("HUD NOTE ODST: bound (generation %u): queue +0x%llX, posted as MCC's own +0x%llX posts its type-3 line, from "
        "the sound listener update's hook",
        generation, static_cast<unsigned long long>(queue - base), static_cast<unsigned long long>(site - base));
    return true;
}

// Before halo3odst.dll goes: called once the listener hook is retired, so nothing is inside a post.
void RemoveOdstHudNote()
{
    g_odstHudNote.queue.store(nullptr, std::memory_order_release);
    g_odstHudNote.tlsIndex = nullptr;
}

void OdstHudNotePost()
{
    HudNotePost(g_odstHudNote);
}

void OdstVehicleNoteShow(const std::wstring& text, float seconds)
{
    VehicleNoteShowOn(g_odstHudNote, g_odstHeadAudio.ready.load(std::memory_order_acquire), text, seconds);
}
#endif

// Whether a title's post site has run lately (Halo 4, Halo CE): the game takes the note only while it does.
bool HudNoteSiteRunning(const Halo3HudNote& note)
{
    const uint64_t at = note.siteMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    return at && now >= at && now - at <= 1000;
}

#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
// ---- Halo 4 ----------------------------------------------------------------------------------------------------
// Halo 4 has a post function of its own, +0x3BF214 post(player, text): no type; the player handle's low word indexes a
// static array of 16 players' HUD message state (+0x30FA690, 0x11C8 apart, outside the TLS game state); it takes a
// slot of the player's six-line queue (+0x350 into the entry) from the picker +0x3C1404, dropping the oldest when all
// six are up, and the copier +0x3BEE78 copies the text (95 characters and a terminator) stamped with the game time
// read from [TLS+0x90]. MCC's own "no navpoints" line (+0x3CDCAC, its call at +0x3CDD3B) and the game's checkpoint
// and zone lines (+0x3BF1BC) post through it. The game tick (+0x9A51C) runs the HUD message update (+0x3C0CD8, at
// +0x9A6E1) and then the listener update (+0x59B00, returning to +0x9A720) on the same thread; the listener update
// hands user 0's listener to the hook halo4_sound_listener.inl has, which posts this note after the hand-off -- only
// for that update's call from the tick (it has three other callers, one on the audio thread), read from its frame.
//
// PROOF (resolved + guarded, nothing hardcoded): the post by a unique signature; its picker call lands on the picker
// matched in place, its copier call on the copier matched in place, and the copier reads the engine's TLS index; MCC's
// own caller (unique) calls the post; the tick site (unique) calls the HUD message update, whose array is the post's,
// and then the listener update the listener hook bound. Any one missing or disagreeing: not bound, the panel as before.
// The line is local presentation (no network or game-state call under the post).
inline constexpr char kHalo4HudPostSig[] =
    "83 F9 FF 74 ?? 48 89 5C 24 08 57 48 83 EC 20 0F B7 C1 33 DB 48 8B FA 83 F8 10 73 ?? 48 69 D8 C8 11 00 00 48 8D "
    "05 ?? ?? ?? ?? 48 03 D8 48 8D 8B 50 03 00 00 E8 ?? ?? ?? ?? 48 85 C0 74 ?? F3 0F 10 93 40 08 00 00 4C 8B CF 48 "
    "8B D0 E8 ?? ?? ?? ?? 48 8B 5C 24 30 48 83 C4 20 5F C3";
inline constexpr size_t kHalo4HudPostArrayDisp = 38, kHalo4HudPostPickerRel = 53, kHalo4HudPostCopierRel = 77;
inline constexpr char kHalo4HudPickerSig[] =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 57 48 83 EC 40 83 79 08 FF 48 8B D9 0F 29 78 E8 F3 "
    "0F 10 3D ?? ?? ?? ?? 44 0F 29 40 D8 48 8B 01 41 BF 90 00 00 00 75 ?? 83 61 08 00 33 FF 83 61 0C 00 33 D2 FF 50 18";
inline constexpr char kHalo4HudCopierSig[] =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57 48 83 EC 30 8B 0D ?? ?? ?? ?? 45 33 F6 "
    "F3 0F 10 05 ?? ?? ?? ?? 49 8B F1 0F 29 70 C8 48 8B DA 65 48 8B 04 25 58 00 00 00 0F 28 F2 41 BD 90 00 00 00 48 "
    "8B 04 C8 4E 8B 2C 28";
inline constexpr size_t kHalo4HudCopierTlsDisp = 30;
inline constexpr char kHalo4HudNavpointCallerSig[] =
    "48 89 5C 24 18 48 89 74 24 20 57 48 81 EC 40 08 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 30 08 00 00 33 "
    "F6 8B FA 48 8B D9 40 38 B1 88 02 00 00 75 ?? F6 81 E4 01 00 00 0F 75 ?? 66 39 B1 80 01 00 00 75 ?? F6 81 78 02 "
    "00 00 0F 75 ?? 48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 48 8D 54 24 30 48 C7 44 24 28 00 04 00 00 45 33 C9 "
    "48 89 54 24 20 45 33 C0 48 8D 15 ?? ?? ?? ?? FF 90 D0 01 00 00 84 C0 74 ?? 48 8D 54 24 30 8B CF E8 ?? ?? ?? ??";
inline constexpr size_t kHalo4HudNavpointPostRel = 144;
inline constexpr char kHalo4HudTickSiteSig[] =
    "E8 ?? ?? ?? ?? 44 8B FB 48 8B 04 3B 8B 48 0C 89 0D ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 44 88 30 48 83 C0 30 48 8D "
    "0D ?? ?? ?? ?? 48 3B C1 7C ?? 48 8D 4D 40 E8 ?? ?? ?? ?? 33 C9 E8 ?? ?? ?? ??";
inline constexpr size_t kHalo4HudTickUpdateRel = 1, kHalo4HudTickListenerRel = 59, kHalo4HudTickReturn = 63;
inline constexpr char kHalo4HudUpdateSig[] =
    "40 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 58 33 ED 0F 29 74 24 40 F3 0F 10 35 ?? ?? ?? ?? 4C 8D 2D ?? ?? "
    "?? ?? 8B F5 89 AC 24 A0 00 00 00 4C 89 AC 24 B0 00 00 00 48 8B DD 48 89 9C 24 A8 00 00 00 85 F6 78 ?? 83 FE 10 "
    "49 0F 42 DD";
inline constexpr size_t kHalo4HudUpdateArrayDisp = 35;
static_assert(Halo3HudPatternBytes(kHalo4HudPostSig) == 92 && kHalo4HudPostCopierRel + 4 < 92);
static_assert(Halo3HudPatternBytes(kHalo4HudNavpointCallerSig) == kHalo4HudNavpointPostRel + 4);
static_assert(Halo3HudPatternBytes(kHalo4HudTickSiteSig) == kHalo4HudTickReturn);
static_assert(kHalo4HudCopierTlsDisp + 4 <= Halo3HudPatternBytes(kHalo4HudCopierSig) &&
              kHalo4HudUpdateArrayDisp + 4 <= Halo3HudPatternBytes(kHalo4HudUpdateSig));
inline constexpr size_t kHalo4HudPickerSigBytes = Halo3HudPatternBytes(kHalo4HudPickerSig);
inline constexpr size_t kHalo4HudCopierSigBytes = Halo3HudPatternBytes(kHalo4HudCopierSig);
inline constexpr size_t kHalo4HudUpdateSigBytes = Halo3HudPatternBytes(kHalo4HudUpdateSig);

Halo3HudNote g_halo4HudNote{"Halo 4", GameTitle::Halo4};

// `tlsIndex`: the engine's TLS index variable; `listenerUpdate`: the listener update the listener hook bound (0: not
// bound, and neither is this); `updateFrameBytes`: its frame, from its body's stack to its return address.
bool InstallHalo4HudNote(uintptr_t base, size_t size, uint32_t generation, const uint32_t* tlsIndex,
                         uintptr_t listenerUpdate, uint32_t updateFrameBytes)
{
    auto& note = g_halo4HudNote;
    note.playerPost.store(nullptr, std::memory_order_release);
    const auto rip = [](uintptr_t at, size_t disp) { return sig::RipTarget(at + disp, at + disp + 4); };
    const auto inImage = [&](uintptr_t at) { return at >= base && at < base + size; };
    const char* why = nullptr;
    uintptr_t post = 0, picker = 0, copier = 0, caller = 0, tick = 0, update = 0;
    if (!listenerUpdate || !updateFrameBytes || !tlsIndex)
        why = "the listener hook it posts from";
    else if (!(post = mcc_clock::FindUnique(base, size, kHalo4HudPostSig)))
        why = "the post function's signature";
    else if (!(picker = rip(post, kHalo4HudPostPickerRel)) || !inImage(picker) ||
             sig::Find(picker, kHalo4HudPickerSigBytes, kHalo4HudPickerSig) != picker)
        why = "the slot picker at the post's call";
    else if (!(copier = rip(post, kHalo4HudPostCopierRel)) || !inImage(copier) ||
             sig::Find(copier, kHalo4HudCopierSigBytes, kHalo4HudCopierSig) != copier)
        why = "the copier at the post's call";
    else if (rip(copier, kHalo4HudCopierTlsDisp) != reinterpret_cast<uintptr_t>(tlsIndex))
        why = "the TLS index against the engine's";
    else if (!(caller = mcc_clock::FindUnique(base, size, kHalo4HudNavpointCallerSig)) ||
             rip(caller, kHalo4HudNavpointPostRel) != post)
        why = "MCC's own post";
    else if (!(tick = mcc_clock::FindUnique(base, size, kHalo4HudTickSiteSig)) ||
             !(update = rip(tick, kHalo4HudTickUpdateRel)) || !inImage(update) ||
             sig::Find(update, kHalo4HudUpdateSigBytes, kHalo4HudUpdateSig) != update ||
             rip(update, kHalo4HudUpdateArrayDisp) != rip(post, kHalo4HudPostArrayDisp))
        why = "the tick's HUD message update";
    else if (rip(tick, kHalo4HudTickListenerRel) != listenerUpdate)
        why = "the tick's listener update";
    if (why)
    {
        LOG("HUD NOTE Halo 4: not bound (%s did not prove); the vehicle camera note uses the mod's own panel", why);
        return false;
    }
    note.tlsIndex = tlsIndex;
    note.tlsPlayers = kHalo4TlsPlayers;
    note.tickReturn = tick + kHalo4HudTickReturn;
    note.updateFrameBytes = updateFrameBytes;
    note.playerPost.store(reinterpret_cast<Halo4HudPostFn>(post), std::memory_order_release);
    LOG("HUD NOTE Halo 4: bound (generation %u): post +0x%llX (as MCC's own +0x%llX posts its line), after the tick's "
        "HUD message update +0x%llX, from the listener update it calls next (returning to +0x%llX)",
        generation, static_cast<unsigned long long>(post - base), static_cast<unsigned long long>(caller - base),
        static_cast<unsigned long long>(update - base), static_cast<unsigned long long>(note.tickReturn - base));
    return true;
}

// Before the listener hook is retired, so nothing is inside a post once that hook's calls have drained.
void RemoveHalo4HudNote()
{
    g_halo4HudNote.playerPost.store(nullptr, std::memory_order_release);
    g_halo4HudNote.tickReturn = 0;
}

// The listener hand-off's hook, after the game's hand-off, for user 0's call from the listener update. `entry`: the
// address of the hook's return address; the update's own return address lies its frame above it.
void Halo4HudNotePost(const uint8_t* entry)
{
    auto& note = g_halo4HudNote;
    if (!note.playerPost.load(std::memory_order_acquire) || !entry)
        return;
    uintptr_t updateCaller = 0;
    __try
    {
        memcpy(&updateCaller, entry + sizeof(void*) + note.updateFrameBytes, sizeof(updateCaller));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    if (!note.tickReturn || updateCaller != note.tickReturn)
        return;
    note.siteMs.store(GetTickCount64(), std::memory_order_release);
    HudNotePost(note);
}

void Halo4VehicleNoteShow(const std::wstring& text, float seconds)
{
    VehicleNoteShowOn(g_halo4HudNote, HudNoteSiteRunning(g_halo4HudNote), text, seconds);
}
#endif

// ---- Halo CE ---------------------------------------------------------------------------------------------------
// Halo CE prints a line with hud_print_message, +0xADA694 print(local player, text): the local player in cx (-1 does
// nothing; nothing else is checked, so only 0 is passed); that player's messages are [+0x2D91638] + player * 0x458;
// the slot chooser +0xADAB0C takes a free one of four, or the oldest; the text is copied with wcsncpy(63) -- no
// terminator when there are 63, so 62 at most are passed -- and stamped with the game tick read from [+0x2E9FD68]. It
// writes nothing else and calls nothing else. MCC's own "no navpoints" line (+0xB738D0, its call at +0xB73987) prints
// through it, and so does the checkpoint text (+0xB135EC, its call at +0xB13683, which also names the players'
// pointer +0x2EA2D90: local player 0 at +0xB8, the local count at +0xB4). The HUD draw fades and expires the lines
// itself; there is no separate update.
//
// THREAD. The game's own lines are printed from its update (the checkpoint and the control update that prints MCC's
// line), on the update thread; Anniversary graphics draw the HUD under the same lock the update holds. This note is
// printed from the player's unit control update (haloce_unit_control.cpp, the call at +0xAD0D56), after this tick's
// control is set: the same place the game prints its own. Not the sound listener's update, which runs after that lock
// is released.
//
// SESSION. Halo CE prints its own lines -- checkpoints, chat, multiplayer events -- on each machine for its own players
// and sends none of them; and the control update this posts from never runs on a network client. So the line is the
// game's local presentation in any session (anySession).
//
// PROOF (resolved + guarded): the print by a unique signature, whose two reads of the messages' pointer agree and whose
// call lands on the slot chooser matched in place; MCC's own caller (unique) prints through it, at its post site with
// the string it prints; the checkpoint site (unique) prints through it and names the players' pointer. Any one missing
// or disagreeing: not bound, the panel as before.
inline constexpr char kHaloCEHudPrintSig[] =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 83 CD FF 48 8B F2 66 3B CD 74 ?? 48 0F BF C1 45 33 "
    "C0 48 69 F8 58 04 00 00 8B D5 48 03 3D ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 44 8D 45 40 48 8B D6 48 8B D8 48 8D "
    "48 04 FF 15 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 89 AB 84 00 00 00 8B 51 0C 89 13 C6 83 82 00 "
    "00 00 01 41 8A 90 65 11 00 00 8D 4A 01 41 88 88 65 11 00 00 88 93 83 00 00 00 C6 87 56 04 00 00 00";
inline constexpr size_t kHaloCEHudPrintMessagesDisp = 0x32, kHaloCEHudPrintChooserRel = 0x3A,
                        kHaloCEHudPrintClockDisp = 0x55, kHaloCEHudPrintMessagesAgainDisp = 0x5C;
inline constexpr char kHaloCEHudChooserSig[] =
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 33 F6 44 8B DA 44 8B CE 0F B7 DE 44 0F B7 D6 BF FF FF FF 7F 49 0F "
    "BF C2 48 69 D0 8C 00 00 00 48 03 D1 41 83 FB FF";
inline constexpr char kHaloCEHudNavpointCallerSig[] =
    "40 53 48 81 EC C0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 B0 00 00 00 48 83 3D ?? ?? ?? ?? 00 8B D9 "
    "0F 85 ?? ?? ?? ?? 4C 8B 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 49 8B C2 48 F7 D8 48 1B C0 48 85 C1 74 ?? 4D 85 D2 "
    "48 8D 0D ?? ?? ?? ?? B8 40 00 00 00 48 0F 45 C1 83 38 02 75";
inline constexpr size_t kHaloCEHudNavpointCallerBytes = 0xD5;
inline constexpr char kHaloCEHudNavpointPostSig[] =
    "48 C7 44 24 28 3F 00 00 00 48 8D 15 ?? ?? ?? ?? 48 89 4C 24 20 45 33 C9 49 8B CA 45 33 C0 FF 90 D0 01 00 00 84 "
    "C0 74 ?? 48 8D 54 24 30 0F B7 CB E8";
inline constexpr size_t kHaloCEHudNavpointStringDisp = 12, kHaloCEHudNavpointPostRel = 0x31;
inline constexpr char kHaloCEHudNavpointString[] = "uh_hud_messages_no_navpoints";
inline constexpr char kHaloCEHudCheckpointSig[] =
    "48 8B 05 ?? ?? ?? ?? 44 39 34 07 74 ?? 0F BF CD E8 ?? ?? ?? ?? 48 8B D0 0F B7 CB E8";
inline constexpr size_t kHaloCEHudCheckpointPlayersDisp = 3, kHaloCEHudCheckpointPrintRel = 0x1C;
static_assert(Halo3HudPatternBytes(kHaloCEHudPrintSig) == 144 && kHaloCEHudPrintMessagesAgainDisp + 4 < 144);
static_assert(Halo3HudPatternBytes(kHaloCEHudNavpointPostSig) == kHaloCEHudNavpointPostRel);
static_assert(Halo3HudPatternBytes(kHaloCEHudCheckpointSig) == kHaloCEHudCheckpointPrintRel);
inline constexpr size_t kHaloCEHudChooserSigBytes = Halo3HudPatternBytes(kHaloCEHudChooserSig);

Halo3HudNote g_haloCEHudNote{"Halo CE", GameTitle::HaloCE};

bool InstallHaloCEHudNote(uintptr_t base, size_t size, uint32_t generation)
{
    auto& note = g_haloCEHudNote;
    note.print.store(nullptr, std::memory_order_release);
    const auto rip = [](uintptr_t at, size_t disp) { return sig::RipTarget(at + disp, at + disp + 4); };
    const auto inImage = [&](uintptr_t at) { return at >= base && at < base + size; };
    const char* why = nullptr;
    uintptr_t print = 0, chooser = 0, messages = 0, caller = 0, site = 0, label = 0, checkpoint = 0, players = 0;
    if (!(print = mcc_clock::FindUnique(base, size, kHaloCEHudPrintSig)))
        why = "the print function's signature";
    else if (!(chooser = rip(print, kHaloCEHudPrintChooserRel)) || !inImage(chooser) ||
             sig::Find(chooser, kHaloCEHudChooserSigBytes, kHaloCEHudChooserSig) != chooser)
        why = "the slot chooser at the print's call";
    else if (!(messages = rip(print, kHaloCEHudPrintMessagesDisp)) || !inImage(messages) ||
             rip(print, kHaloCEHudPrintMessagesAgainDisp) != messages || !inImage(rip(print, kHaloCEHudPrintClockDisp)))
        why = "the messages' and the clock's pointers the print reads";
    else if (!(caller = mcc_clock::FindUnique(base, size, kHaloCEHudNavpointCallerSig)) ||
             !(site = sig::Find(caller, kHaloCEHudNavpointCallerBytes, kHaloCEHudNavpointPostSig)) ||
             rip(site, kHaloCEHudNavpointPostRel) != print || !(label = rip(site, kHaloCEHudNavpointStringDisp)) ||
             !inImage(label) || label + sizeof(kHaloCEHudNavpointString) > base + size ||
             memcmp(reinterpret_cast<const void*>(label), kHaloCEHudNavpointString, sizeof(kHaloCEHudNavpointString)))
        why = "MCC's own print";
    else if (!(checkpoint = mcc_clock::FindUnique(base, size, kHaloCEHudCheckpointSig)) ||
             rip(checkpoint, kHaloCEHudCheckpointPrintRel) != print ||
             !(players = rip(checkpoint, kHaloCEHudCheckpointPlayersDisp)) || !inImage(players))
        why = "the checkpoint text's print";
    if (why)
    {
        LOG("HUD NOTE Halo CE: not bound (%s did not prove); the vehicle camera note uses the mod's own panel", why);
        return false;
    }
    note.ceMessages = reinterpret_cast<const uintptr_t*>(messages);
    note.ceClock = reinterpret_cast<const uintptr_t*>(rip(print, kHaloCEHudPrintClockDisp));
    note.cePlayers = reinterpret_cast<const uintptr_t*>(players);
    note.maxChars = hud_note::kHaloCEMaxChars;
    note.anySession = true;
    note.print.store(reinterpret_cast<HaloCEHudPrintFn>(print), std::memory_order_release);
    LOG("HUD NOTE Halo CE: bound (generation %u): print +0x%llX (as MCC's own +0x%llX and the checkpoint text print "
        "their lines), from the player's unit control update",
        generation, static_cast<unsigned long long>(print - base), static_cast<unsigned long long>(caller - base));
    return true;
}

// The cold worker, each pass: bound once for each generation of halo1.dll, and published while the title is up;
// withdrawn first when it goes (before the unit control hook it posts from is retired).
void HaloCEHudNotePoll(uintptr_t base, size_t size, uint32_t generation, bool active)
{
    static uint32_t tried = 0;
    static HaloCEHudPrintFn bound = nullptr;
    auto& note = g_haloCEHudNote;
    if (!active || !base || !generation)
    {
        note.print.store(nullptr, std::memory_order_release);
        return;
    }
    if (tried != generation)
    {
        tried = generation;
        bound = InstallHaloCEHudNote(base, size, generation) ? note.print.load(std::memory_order_acquire) : nullptr;
    }
    else if (bound && !note.print.load(std::memory_order_acquire))
        note.print.store(bound, std::memory_order_release);
}

// The unit control hook, after the local player's control is set by the control update.
void HaloCEHudNotePost()
{
    auto& note = g_haloCEHudNote;
    if (!note.print.load(std::memory_order_acquire))
        return;
    note.siteMs.store(GetTickCount64(), std::memory_order_release);
    HudNotePost(note);
}

void HaloCEVehicleNoteShow(const std::wstring& text, float seconds)
{
    VehicleNoteShowOn(g_haloCEHudNote, HudNoteSiteRunning(g_haloCEHudNote), text, seconds);
}

void Halo3VehicleNoteTick()
{
    VehicleNoteTickOn(g_halo3HudNote);
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    VehicleNoteTickOn(g_odstHudNote);
#endif
#if HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    VehicleNoteTickOn(g_halo4HudNote);
#endif
    VehicleNoteTickOn(g_haloCEHudNote);
}
