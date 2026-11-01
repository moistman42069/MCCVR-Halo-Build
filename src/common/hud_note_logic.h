#pragma once
// A NOTE IN THE GAME'S OWN HUD MESSAGES, the lines "Checkpoint... done" is written in (vehicle_camera_note = 1):
// which way a note goes, the text the HUD's font can carry, and when the game thread posts it. Pure: the runtime
// (src/dll/halo3_hud_note.inl) holds the mailbox and calls the game.
//
// The cold worker asks (a serial and its text); the game thread posts the latest it has not shown, once the asks
// have settled, so a run of quick taps posts one line rather than a stack of them. A note the game has not taken
// in time (the HUD not up, a co-op session, the function not bound) goes to the mod's own panel instead: whichever
// side claims the serial first shows it, and only that one.

#include <cstddef>
#include <cstdint>
#include <string>

namespace hud_note
{
// The game's queue copies at most 96 characters, its terminator among them.
inline constexpr std::size_t kMaxChars = 95;
// Asks closer together than this post once: the last of them.
inline constexpr uint64_t kSettleMs = 250;
// A note the game has not taken by then goes to the mod's panel.
inline constexpr uint64_t kFallbackMs = 1500;

enum class Route
{
    Off,
    GameHud,
    Panel,
};

// mode: vehicle_camera_note (0 off, 1 the game's HUD messages, 2 the mod's panel; anything else reads as 1).
// hudReady: the game's message function is bound and the hook that posts it runs. sessionAllows: the player is
// alone or the session is host-authoritative (Sight_FireTimeChangesAllowed) -- in a shared co-op simulation the
// HUD's state is left as the game keeps it and the panel shows the note.
inline Route Choose(int mode, bool hudReady, bool sessionAllows) noexcept
{
    if (mode == 0)
        return Route::Off;
    if (mode == 2 || !hudReady || !sessionAllows)
        return Route::Panel;
    return Route::GameHud;
}

// Whether this session takes a line in the game's own HUD. `oneMachineChangesAllowed` is the co-op switch's answer
// (Sight_FireTimeChangesAllowed). In Project Reclaimer the player count that answer needs reads "not read" (every log so
// far), so the switch stays closed in a synchronous game the player may be alone in. There the simulation type
// decides: only a synchronous client (2) is known to share its simulation, and an unread type (-1) is no game.
// A HUD line is text for one local user's HUD, which no two machines hold alike in the first place.
inline bool SessionAllows(bool oneMachineChangesAllowed, bool hostIsProjectReclaimer, int simulationType) noexcept
{
    return oneMachineChangesAllowed ||
        (hostIsProjectReclaimer && (simulationType == 1 || simulationType == 3 || simulationType == 4 ||
                                    simulationType == 5));
}

// The note as the HUD's font draws it: the separator " \u00B7 " as " - ", anything else outside printable ASCII as
// '?', no more than kMaxChars (a longer note ends in "..."), no spaces at either end.
inline std::wstring ForGameHud(const std::wstring& note)
{
    std::wstring out;
    out.reserve(note.size());
    for (const wchar_t c : note)
    {
        if (c == L'\u00B7')
            out.push_back(L'-');
        else if (c >= 0x20 && c < 0x7F)
            out.push_back(c);
        else
            out.push_back(L'?');
    }
    const auto trim = [&out] {
        while (!out.empty() && out.back() == L' ')
            out.pop_back();
        std::size_t lead = 0;
        while (lead < out.size() && out[lead] == L' ')
            ++lead;
        out.erase(0, lead);
    };
    trim();
    if (out.size() > kMaxChars)
    {
        out.resize(kMaxChars - 3);
        trim();
        out += L"...";
    }
    return out;
}

// The game thread posts: a note it has not shown, asked for at least kSettleMs ago.
inline bool Due(uint32_t wanted, uint32_t shown, uint64_t sinceAskedMs) noexcept
{
    return wanted != shown && sinceAskedMs >= kSettleMs;
}

// The cold worker shows it on the panel instead: still not shown kFallbackMs after it was asked for.
inline bool Overdue(uint32_t wanted, uint32_t shown, uint64_t sinceAskedMs) noexcept
{
    return wanted != shown && sinceAskedMs >= kFallbackMs;
}
} // namespace hud_note
