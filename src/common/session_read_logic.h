#pragma once

// THE SESSION, AS THE GAME HOLDS IT -- for the log and for the automatic co-op switch (sight_layer.h
// Sight_NoteSession, coop_safe_shots = 2). Three facts a title's
// simulation thread can read in one pass:
//
//   * the SIMULATION TYPE the game was started with. The games name the values themselves (Halo 3's table of
//     "game_simulation" names): 0 none, 1 local, 2 sync-client, 3 sync-server, 4 dist-client, 5 dist-server. A
//     campaign level is a synchronous simulation even alone (a solo level reads sync-server), so the type tells a
//     co-op JOINER (sync-client) and a host-authoritative game (dist-*) apart, and nothing about a co-op host;
//   * the PLAYERS IN THE GAME: the count the engine keeps beside its players array -- +1 when a player is created,
//     -1 when one leaves -- and compares with 1 for its own solo tests;
//   * the MACHINES OTHER THAN THIS ONE: the engine's machine mask (17 machines at most) without the local
//     machine's bit, which is what its own "is anyone else in this game" test reads.
//
// The count and the mask sit in a block with no name of its own, so they are believed only when the players array
// read through the same layout proves the layout is this build's: its name, its data-array signature, its
// maximum of 16 players and its datum size. A different build reads "not read", never a wrong number.
//
// Pure logic: the caller supplies the three pointers and guards the reads (they are the engine's memory).
// Unit-tested in tests/core_tests.cpp.

#include <cstdint>
#include <cstring>

namespace session_read
{
inline constexpr int kNotRead = -1;
inline constexpr uint32_t kDataArraySignature = 0x64407440u;   // 'd@t@'
inline constexpr int kMaximumPlayers = 16;
inline constexpr uint64_t kMachineBits = 0x1FFFFu;             // 17 machines

struct Layout
{
    // Game globals: where the simulation type is, and how wide. `progressFlags`: bytes +0 / +1 say a game is in
    // progress (false: the type is read whenever the globals exist).
    uint32_t typeOffset = 0;
    bool typeDword = false;
    bool progressFlags = true;
    // The players array's header: the older layout keeps maximum, datum size and signature at +0x20 / +0x24 /
    // +0x2C as 32-bit fields; the newer one keeps a 64-bit datum size at +0x20, the signature at +0x28 and the
    // maximum at +0x2C.
    bool wideHeader = false;
    uint32_t datumSize = 0;
    // The block holding the count (int32 at +0), the machine mask and the local machine's index.
    uint32_t maskOffset = 0;
    bool mask64 = false;
    uint32_t localOffset = 0;
};

// Engine thread-local block offsets of the three pointers, for the titles that keep them there.
struct TlsSlots
{
    uint32_t gameGlobals = 0, playersArray = 0, countBlock = 0;
};

// ADDR-HYGIENE: guarded (the players array's own name, signature, maximum and datum size are checked before the
// count beside it is believed). A layout that does not prove reads "not read", which the automatic co-op switch
// counts as shared: a build these offsets were not measured on can only keep the stand-downs on.
inline constexpr Layout kHalo3{0x11, false, true, false, 0x2F8, 0x08, false, 0x7C};
inline constexpr TlsSlots kHalo3Slots{0x48, 0x10, 0x40};
inline constexpr Layout kOdst{0x11, false, true, false, 0x418, 0x08, false, 0x7C};
inline constexpr TlsSlots kOdstSlots{0x40, 0x10, 0x58};
inline constexpr Layout kReach{0x11, false, true, true, 0x490, 0x04, false, 0x78};
inline constexpr TlsSlots kReachSlots{0x48, 0x18, 0x118};
inline constexpr Layout kHalo4{0x1C, true, true, true, 0xF18, 0x08, true, 0x80};
inline constexpr TlsSlots kHalo4Slots{0x40, 0x08, 0x110};
// Halo 2 keeps the three pointers in module globals (halo2_observer_6dof.cpp reads them); its type is a signed
// byte with no progress flags beside it.
inline constexpr Layout kHalo2{0x0C, false, false, false, 0x224, 0x2C, false, 0xC4};

struct Session
{
    int type = kNotRead;      // the simulation type; kNotRead: no game in progress, or unreadable
    int players = kNotRead;   // players in the game; kNotRead: the layout did not prove
    int others = kNotRead;    // machines other than this one; kNotRead: not proven, or no local machine
};

inline const char* TypeName(int type)
{
    switch (type)
    {
    case 0: return "none";
    case 1: return "local";
    case 2: return "sync-client";
    case 3: return "sync-server";
    case 4: return "dist-client";
    case 5: return "dist-server";
    default: return "unknown";
    }
}

namespace detail
{
template <class T> inline T Load(const uint8_t* at)
{
    T value{};
    std::memcpy(&value, at, sizeof(value));
    return value;
}
inline int Bits(uint64_t value)
{
    int n = 0;
    for (; value; value &= value - 1)
        ++n;
    return n;
}
}   // namespace detail

// `gameGlobals`, `playersHeader`, `countBlock`: the three pointers as the engine holds them now (any may be null).
inline Session Read(const uint8_t* gameGlobals, const uint8_t* playersHeader, const uint8_t* countBlock,
                    const Layout& layout)
{
    using detail::Load;
    Session s{};
    if (!gameGlobals || (layout.progressFlags && !(gameGlobals[0] || gameGlobals[1])))
        return s;
    const int64_t type = layout.typeDword ? int64_t(Load<uint32_t>(gameGlobals + layout.typeOffset))
                                          : int64_t(Load<int8_t>(gameGlobals + layout.typeOffset));
    if (type < 0 || type > 255)
        return s;
    s.type = int(type);
    if (!playersHeader || !countBlock)
        return s;
    const uint32_t signature = Load<uint32_t>(playersHeader + (layout.wideHeader ? 0x28 : 0x2C));
    const int32_t maximum = Load<int32_t>(playersHeader + (layout.wideHeader ? 0x2C : 0x20));
    const uint64_t size = layout.wideHeader ? Load<uint64_t>(playersHeader + 0x20)
                                            : uint64_t(Load<uint32_t>(playersHeader + 0x24));
    if (std::memcmp(playersHeader, "players", 8) != 0 || signature != kDataArraySignature ||
        maximum != kMaximumPlayers || size != layout.datumSize)
        return s;
    const int32_t players = Load<int32_t>(countBlock);
    if (players < 0 || players > kMaximumPlayers)
        return s;
    s.players = players;
    const int32_t local = Load<int32_t>(countBlock + layout.localOffset);
    if (local < 0 || local > 16)
        return s;
    const uint64_t mask = layout.mask64 ? Load<uint64_t>(countBlock + layout.maskOffset)
                                        : uint64_t(Load<uint32_t>(countBlock + layout.maskOffset));
    s.others = detail::Bits(mask & kMachineBits & ~(uint64_t(1) << local));
    return s;
}

// ---- whether a change made on this machine only can end the session ------------------------------------------
// A synchronous session computes one simulation on every machine from shared input, so a change made inside one
// machine's simulation ends it; a distributed one has an authority, and the same change there is a misprediction.
//   Alone          a synchronous (or local) game with one player and no other machine: nothing to disagree with;
//   Authoritative  a distributed game (Firefight, custom games): no shared simulation to break;
//   Shared         a synchronous client, or a synchronous server with anyone else in it;
//   Unknown        anything not read. Unknown is never safe.
enum class Standing : uint8_t { Unknown = 0, Alone, Authoritative, Shared };

inline Standing Classify(const Session& s)
{
    if (s.type == 4 || s.type == 5)
        return Standing::Authoritative;
    if (s.type == 2)
        return Standing::Shared;
    if (s.type == 1 || s.type == 3)
    {
        if (s.players == 1 && s.others == 0)
            return Standing::Alone;
        if (s.players > 1 || s.others > 0)
            return Standing::Shared;
    }
    return Standing::Unknown;
}

inline bool OneMachineChangesSafe(Standing standing)
{
    return standing == Standing::Alone || standing == Standing::Authoritative;
}

inline const char* StandingText(Standing standing)
{
    switch (standing)
    {
    case Standing::Alone: return "alone in a synchronous game";
    case Standing::Authoritative: return "a host-authoritative game (Firefight, custom games)";
    case Standing::Shared: return "a shared simulation (campaign co-op, Spartan Ops)";
    default: return "not read";
    }
}

// THE AUTOMATIC SWITCH'S GATE. The session is read by the title's simulation thread on every control update; the
// gate opens only on a run of `kGateSettleNotes` consecutive notes that all say safe, and closes at once on one
// that does not. Notes that stop coming are no evidence of anything: after `kGateStaleMs` without one the gate is
// closed, and the next note starts the run again -- so does a note from another title -- because the level, the
// lobby and the session may all have changed in between. Every failure of the read leaves the gate closed.
inline constexpr unsigned kGateSettleNotes = 30;
inline constexpr unsigned long long kGateStaleMs = 5000;

// The run of safe notes after one more note. `continued`: the previous note was this title's and within
// kGateStaleMs.
inline unsigned GateNote(unsigned safeNotes, bool safeNow, bool continued)
{
    if (!safeNow)
        return 0;
    if (!continued)
        return 1;
    return safeNotes < 0x7FFFFFFFu ? safeNotes + 1 : safeNotes;
}

// `sinceLastNoteMs`: how long ago the newest note arrived.
inline bool GateOpen(unsigned safeNotes, unsigned long long sinceLastNoteMs)
{
    return safeNotes >= kGateSettleNotes && sinceLastNoteMs <= kGateStaleMs;
}

// The same from an engine thread's TLS block (null: nothing read).
inline Session ReadTls(const uint8_t* tls, const TlsSlots& slots, const Layout& layout)
{
    if (!tls)
        return Session{};
    return Read(detail::Load<const uint8_t*>(tls + slots.gameGlobals),
                detail::Load<const uint8_t*>(tls + slots.playersArray),
                detail::Load<const uint8_t*>(tls + slots.countBlock), layout);
}
}   // namespace session_read
