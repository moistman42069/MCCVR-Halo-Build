#pragma once
// SEATEXIT witness: pure logic for the DEV-ONLY seat-change input witness (seat_exit_witness.cpp).
//
// The question it answers: when the player leaves (or enters) a vehicle seat in Halo 3 / ODST, was our
// own code sending the title's Use / Exit action in the moments before, and if so which subsystem put it
// there? The dll records one compact Sample per XInput poll whose state CHANGED (plus a heartbeat), and
// on a raw seated-state edge prints the recent samples through the helpers below. Everything here is
// allocation-free and Windows-free so it unit-tests standalone (tests/seat_exit_witness_tests.cpp).
#include "vr_action_mapping.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace seat_witness
{
// Raw controller state (after the OpenXR read and face-zone merge, before any mapping).
enum RawBit : uint32_t
{
    RawA = 1u << 0, RawB = 1u << 1, RawX = 1u << 2, RawY = 1u << 3,
    RawClickL = 1u << 4, RawClickR = 1u << 5, RawMenu = 1u << 6,
    RawThumbrestL = 1u << 7, RawThumbrestR = 1u << 8,      // capacitive thumb-rest touch
    RawStickTouchL = 1u << 9, RawStickTouchR = 1u << 10,   // capacitive thumbstick touch
    RawThumbrestDpad = 1u << 11,   // quest_thumbrest_dpad route active this sample
    RawExclusive = 1u << 12,       // pad.exclusiveInput (thumb-rest D-pad or native menu pointer)
    RawDpadGesture = 1u << 13,     // head D-pad gesture (dpad_head_radius) active
    RawPadValid = 1u << 14,        // VR pad sample valid
    RawF1Menu = 1u << 15,          // our F1 menu open
};
inline constexpr uint32_t kRawBitCount = 16;
inline constexpr const char* kRawNames[kRawBitCount]{"A","B","X","Y","L3","R3","Menu","TRL","TRR",
    "STL","STR","TRdpad","excl","headDpad","valid","F1"};

// Which branch of the XInput merge produced the sample.
enum Route : uint8_t { RouteGated, RouteNoPad, RouteF1Menu, RouteExclusive, RouteNormal, RouteCount };
inline constexpr const char* kRouteNames[RouteCount]{"GATE","NOPAD","F1","EXCL","NRM"};

enum Flag : uint8_t
{
    FlagSemantic = 1u << 0,   // vr_action_mapping route (semantic mapper) active
    FlagDirect = 1u << 1,     // bridge direct route (native reader) in use
    FlagPublished = 1u << 2,  // NativeVrActions_Publish was called this poll
    FlagWheel = 1u << 3,      // Halo 3 steering-wheel gesture swallowed the grips
    FlagChord = 1u << 4,      // L3+R3 or Y+B chord consumed clicks this poll
    FlagGameplay = 1u << 5,   // runtime mode is gameplay/vehicle/turret
};

// Subsystems that can put a transport bit into the pad the game reads.
enum Subsystem : uint32_t
{
    SubPhysical = 1u << 0,  // a physical gamepad / dev synthetic pad, passed through
    SubMapper = 1u << 1,    // semantic mapper (vr_action_mapping) or the legacy direct button copy
    SubCrouch = 1u << 2,    // physical crouch
    SubGesture = 1u << 3,   // gesture melee
    SubWeapon = 1u << 4,    // weapon interaction (manual reload / holsters)
    SubDpad = 1u << 5,      // head D-pad gesture or thumb-rest D-pad buttons
    SubStart = 1u << 6,     // Menu -> Start (incl. the ODST latch)
    SubBridge = 1u << 7,    // semantic action handed to the native bridge (publication)
};
inline constexpr unsigned kSubsystemCount = 8;
inline constexpr const char* kSubsystemNames[kSubsystemCount]{"physical","mapper","crouch","gesture",
    "weapon","dpad","start","bridge"};

// Transport encoding is the one the dll already uses: XInput wButtons in bits 0..15, LT = bit 16,
// RT = bit 17.
inline constexpr uint32_t kTransportLT = 1u << 16, kTransportRT = 1u << 17;
inline constexpr uint32_t kTransportMask = 0x3FFFFu;

struct Sample
{
    uint64_t ms = 0;                     // QPC milliseconds (same clock as the seat event)
    uint32_t raw = 0;                    // RawBit
    int8_t moveX = 0, moveY = 0, turnX = 0, turnY = 0;   // percent, -100..100
    uint8_t trigL = 0, trigR = 0, gripL = 0, gripR = 0;  // percent, 0..100
    uint32_t sources = 0;                // vr_mapping source bits handed to the mapper
    uint32_t actions = 0;                // semantic action bits published to the bridge
    uint32_t physical = 0;               // transport bits already in the pad before our merge
    uint32_t mapper = 0, crouch = 0, gesture = 0, weapon = 0, dpad = 0, start = 0;
    uint32_t fed = 0;                    // final transport bits the game reads this poll
    uint32_t useMask = 0;                // transport bits of the title's Use/Exit action (0 = unknown)
    uint16_t thread = 0;                 // low 16 bits of the polling thread id
    uint8_t route = RouteGated, flags = 0;
};

inline int8_t QuantizeAxis(float v) noexcept
{
    if (!(v == v)) return 0;
    if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
    return static_cast<int8_t>(v * 100.0f + (v >= 0 ? 0.5f : -0.5f));
}
inline uint8_t QuantizeUnit(float v) noexcept
{
    if (!(v == v) || v <= 0.0f) return 0;
    if (v > 1.0f) v = 1.0f;
    return static_cast<uint8_t>(v * 100.0f + 0.5f);
}
// Analog values only count as a CHANGE when they cross a zone the input code actually acts on
// (trigger 0.15, grip 0.6, D-pad/stick 0.5); otherwise a driving stick would make every sample "new".
inline int AxisZone(int8_t v) noexcept { return v > 50 ? 1 : (v < -50 ? -1 : 0); }
inline int UnitZone(uint8_t v) noexcept { return v >= 60 ? 2 : (v > 15 ? 1 : 0); }

inline bool SameState(const Sample& a, const Sample& b) noexcept
{
    return a.raw == b.raw && a.sources == b.sources && a.actions == b.actions &&
        a.physical == b.physical && a.mapper == b.mapper && a.crouch == b.crouch &&
        a.gesture == b.gesture && a.weapon == b.weapon && a.dpad == b.dpad && a.start == b.start &&
        a.fed == b.fed && a.useMask == b.useMask && a.route == b.route && a.flags == b.flags &&
        // thread is deliberately NOT state: two polling threads with the same pad are one state.
        AxisZone(a.moveX) == AxisZone(b.moveX) && AxisZone(a.moveY) == AxisZone(b.moveY) &&
        AxisZone(a.turnX) == AxisZone(b.turnX) && AxisZone(a.turnY) == AxisZone(b.turnY) &&
        UnitZone(a.trigL) == UnitZone(b.trigL) && UnitZone(a.trigR) == UnitZone(b.trigR) &&
        UnitZone(a.gripL) == UnitZone(b.gripL) && UnitZone(a.gripR) == UnitZone(b.gripR);
}

// Semantic actions that reach the same native action as Use/Pick up. Halo 3 and ODST put Reload and
// Use on one native action (3), which in a vehicle is EXIT; Reach keeps them apart (3 vs 0x3E).
inline uint32_t UseActions(GameTitle title) noexcept
{
    const unsigned use = vr_mapping::NativeAction(title, vr_mapping::Interact);
    if (use == vr_mapping::kNoAction) return 1u << vr_mapping::Interact;
    uint32_t mask = 0;
    for (unsigned a = 0; a < vr_mapping::Count; ++a)
        if (vr_mapping::NativeAction(title, static_cast<vr_mapping::Action>(a)) == use) mask |= 1u << a;
    return mask;
}
// The transport bits those actions ride on in the player's current MCC layout.
inline uint32_t UseTransportMask(GameTitle title, const vr_mapping::Transports& native) noexcept
{
    const uint32_t actions = UseActions(title);
    uint32_t mask = 0;
    for (unsigned a = 0; a < vr_mapping::Count; ++a)
        if (actions & (1u << a)) mask |= native[a];
    return mask & kTransportMask;
}

inline uint32_t SubsystemsCarrying(const Sample& s, uint32_t transportMask, uint32_t actionMask) noexcept
{
    uint32_t subs = 0;
    if (s.physical & transportMask) subs |= SubPhysical;
    if (s.mapper & transportMask) subs |= SubMapper;
    if (s.crouch & transportMask) subs |= SubCrouch;
    if (s.gesture & transportMask) subs |= SubGesture;
    if (s.weapon & transportMask) subs |= SubWeapon;
    if (s.dpad & transportMask) subs |= SubDpad;
    if (s.start & transportMask) subs |= SubStart;
    if (s.actions & actionMask) subs |= SubBridge;
    return subs;
}

struct UseVerdict
{
    bool sent = false;         // our code was handing the game Use/Exit at some point in the window
    bool known = false;        // the Use transport mask was known for at least one sample
    bool rawX = false;         // the raw X button was down while it was sent
    int64_t firstMs = 0, lastMs = 0;   // relative to the event (negative = before)
    uint32_t subsystems = 0;   // Subsystem bits that carried it
    uint32_t sources = 0;      // vr_mapping sources down while it was sent
    uint32_t otherActions = 0; // every other semantic action published in the window
    uint32_t otherFed = 0;     // every transport bit fed in the window
    uint32_t useMask = 0;      // last known Use transport mask
    bool covered = false;      // the samples cover the whole window
};

// samples: chronological (oldest first). A sample's state holds until the next sample, so the sample at
// or before the window start counts for the window.
inline UseVerdict EvaluateUse(const Sample* samples, size_t count, uint64_t eventMs, uint64_t windowMs,
    uint32_t useActions) noexcept
{
    UseVerdict v{};
    const uint64_t from = eventMs > windowMs ? eventMs - windowMs : 0;
    size_t begin = count;
    for (size_t i = 0; i < count; ++i)
    {
        if (samples[i].ms > eventMs) break;
        if (samples[i].ms <= from) begin = i;            // latest sample at/before the window start
        else if (begin == count) begin = i;              // nothing before the window: first inside it
    }
    if (begin == count) return v;
    v.covered = samples[begin].ms <= from;
    for (size_t i = begin; i < count && samples[i].ms <= eventMs; ++i)
    {
        const Sample& s = samples[i];
        if (s.useMask) { v.known = true; v.useMask = s.useMask; }
        v.otherActions |= s.actions & ~useActions;
        v.otherFed |= s.fed;
        const bool viaTransport = s.useMask && (s.fed & s.useMask);
        const bool viaAction = (s.actions & useActions) != 0;
        if (!viaTransport && !viaAction) continue;
        const int64_t rel = static_cast<int64_t>(s.ms > from ? s.ms : from) - static_cast<int64_t>(eventMs);
        if (!v.sent) v.firstMs = rel;
        v.lastMs = rel;
        v.sent = true;
        v.subsystems |= SubsystemsCarrying(s, s.useMask, useActions);
        v.sources |= s.sources;
        if (s.raw & RawX) v.rawX = true;
    }
    if (v.useMask) v.otherFed &= ~v.useMask;
    return v;
}

// Pick the samples worth printing: the state at the window start, then every sample whose state differs
// from the one before it, up to toMs. If there are more than maxOut, the first is kept and the NEWEST
// maxOut-1 changes (closest to the event) are kept; *omitted counts the rest. Returns the number of
// indices written to out.
template<class T, class Same>
size_t CompressBy(const T* samples, size_t count, uint64_t fromMs, uint64_t toMs,
    size_t* out, size_t maxOut, size_t* omitted, Same same) noexcept
{
    if (omitted) *omitted = 0;
    if (!count || !maxOut) return 0;
    size_t first = count;
    for (size_t i = 0; i < count && samples[i].ms <= toMs; ++i)
    {
        if (samples[i].ms <= fromMs) first = i;
        else if (first == count) first = i;
    }
    if (first == count) return 0;
    size_t n = 0, dropped = 0;
    out[n++] = first;
    for (size_t i = first + 1; i < count && samples[i].ms <= toMs; ++i)
    {
        if (same(samples[i], samples[i - 1])) continue;
        if (n < maxOut) { out[n++] = i; continue; }
        if (maxOut < 2) { ++dropped; continue; }
        // Full: slide the kept changes left by one (keeping out[0], the window-start state).
        for (size_t k = 1; k + 1 < maxOut; ++k) out[k] = out[k + 1];
        out[maxOut - 1] = i;
        ++dropped;
    }
    if (omitted) *omitted = dropped;
    return n;
}
inline size_t Compress(const Sample* samples, size_t count, uint64_t fromMs, uint64_t toMs,
    size_t* out, size_t maxOut, size_t* omitted) noexcept
{
    return CompressBy(samples, count, fromMs, toMs, out, maxOut, omitted,
        [](const Sample& a, const Sample& b) { return SameState(a, b); });
}

// ---- formatting (bounded snprintf into caller buffers) ----
inline size_t Append(char* buf, size_t cap, size_t len, const char* text) noexcept
{
    if (!buf || len >= cap) return len;
    const int w = std::snprintf(buf + len, cap - len, "%s", text);
    if (w < 0) return len;
    return (len + static_cast<size_t>(w) < cap) ? len + static_cast<size_t>(w) : cap - 1;
}
// XInput button / trigger names for a transport mask, e.g. "X+RB+RT", or "-" when empty.
inline size_t AppendTransport(char* buf, size_t cap, size_t len, uint32_t mask) noexcept
{
    static constexpr const char* names[18]{"DU","DD","DL","DR","Start","Back","LS","RS","LB","RB",
        "b10","b11","A","B","X","Y","LT","RT"};
    if (!(mask & kTransportMask)) return Append(buf, cap, len, "-");
    bool any = false;
    for (unsigned b = 0; b < 18; ++b)
        if (mask & (1u << b)) { if (any) len = Append(buf, cap, len, "+"); len = Append(buf, cap, len, names[b]); any = true; }
    return len;
}
inline size_t AppendActions(char* buf, size_t cap, size_t len, uint32_t mask) noexcept
{
    static constexpr const char* names[vr_mapping::Count]{"Fire","Gren","Jump","Melee","Reload","Use",
        "SwWpn","SwGren","Equip","Crouch","Zoom","Light","Sprint"};
    if (!(mask & ((1u << vr_mapping::Count) - 1))) return Append(buf, cap, len, "-");
    bool any = false;
    for (unsigned a = 0; a < vr_mapping::Count; ++a)
        if (mask & (1u << a)) { if (any) len = Append(buf, cap, len, "+"); len = Append(buf, cap, len, names[a]); any = true; }
    return len;
}
inline size_t AppendSources(char* buf, size_t cap, size_t len, uint32_t mask) noexcept
{
    static constexpr const char* names[vr_mapping::SourceCount]{"auto","unb","PT","ST","PG","SG","A","B",
        "X","Y","L3","R3","DpU","DpD","DpL","DpR"};
    if (!mask) return Append(buf, cap, len, "-");
    bool any = false;
    for (unsigned s = 0; s < vr_mapping::SourceCount; ++s)
        if (mask & (1u << s)) { if (any) len = Append(buf, cap, len, "+"); len = Append(buf, cap, len, names[s]); any = true; }
    return len;
}
inline size_t AppendRaw(char* buf, size_t cap, size_t len, uint32_t raw) noexcept
{
    // "valid" is the normal case: print only its absence.
    const uint32_t shown = raw & ~RawPadValid;
    if (!shown) return Append(buf, cap, len, (raw & RawPadValid) ? "-" : "NOPAD");
    bool any = false;
    if (!(raw & RawPadValid)) { len = Append(buf, cap, len, "NOPAD"); any = true; }
    for (unsigned b = 0; b < kRawBitCount; ++b)
        if (shown & (1u << b)) { if (any) len = Append(buf, cap, len, "+"); len = Append(buf, cap, len, kRawNames[b]); any = true; }
    return len;
}
inline size_t AppendSubsystems(char* buf, size_t cap, size_t len, uint32_t mask) noexcept
{
    if (!mask) return Append(buf, cap, len, "-");
    bool any = false;
    for (unsigned b = 0; b < kSubsystemCount; ++b)
        if (mask & (1u << b)) { if (any) len = Append(buf, cap, len, "+"); len = Append(buf, cap, len, kSubsystemNames[b]); any = true; }
    return len;
}

// One sample line, relative to the event. Only non-empty contributors are printed.
inline size_t FormatSample(char* buf, size_t cap, const Sample& s, uint64_t eventMs) noexcept
{
    if (!buf || !cap) return 0;
    buf[0] = 0;
    char tmp[96];
    const long long rel = static_cast<long long>(s.ms) - static_cast<long long>(eventMs);
    std::snprintf(tmp, sizeof(tmp), "%+6lldms %-5s t%04x raw[", rel,
        s.route < RouteCount ? kRouteNames[s.route] : "?", static_cast<unsigned>(s.thread));
    size_t len = Append(buf, cap, 0, tmp);
    len = AppendRaw(buf, cap, len, s.raw);
    std::snprintf(tmp, sizeof(tmp), "] mv%+.2f,%+.2f tn%+.2f,%+.2f tr%.2f,%.2f gp%.2f,%.2f src[",
        s.moveX / 100.0, s.moveY / 100.0, s.turnX / 100.0, s.turnY / 100.0,
        s.trigL / 100.0, s.trigR / 100.0, s.gripL / 100.0, s.gripR / 100.0);
    len = Append(buf, cap, len, tmp);
    len = AppendSources(buf, cap, len, s.sources);
    len = Append(buf, cap, len, "] act[");
    len = AppendActions(buf, cap, len, s.actions);
    len = Append(buf, cap, len, "] fed[");
    len = AppendTransport(buf, cap, len, s.fed);
    len = Append(buf, cap, len, "]");
    const struct { const char* name; uint32_t mask; } parts[]{{"phys",s.physical},{"map",s.mapper},
        {"crouch",s.crouch},{"gest",s.gesture},{"wpn",s.weapon},{"dpad",s.dpad},{"start",s.start}};
    for (const auto& p : parts)
        if (p.mask & kTransportMask)
        {
            len = Append(buf, cap, len, " ");
            len = Append(buf, cap, len, p.name);
            len = Append(buf, cap, len, "[");
            len = AppendTransport(buf, cap, len, p.mask);
            len = Append(buf, cap, len, "]");
        }
    if (s.useMask && (s.fed & s.useMask)) len = Append(buf, cap, len, " <USE>");
    if (s.flags)
    {
        std::snprintf(tmp, sizeof(tmp), " f%s%s%s%s%s%s", (s.flags & FlagSemantic) ? "S" : "",
            (s.flags & FlagDirect) ? "D" : "", (s.flags & FlagPublished) ? "P" : "",
            (s.flags & FlagWheel) ? "W" : "", (s.flags & FlagChord) ? "C" : "",
            (s.flags & FlagGameplay) ? "G" : "");
        len = Append(buf, cap, len, tmp);
    }
    return len;
}

// Edge-log rate limiter for the dev controller-edge lines: at most `burst` lines per `windowMs`; the
// suppressed count is handed back on the next allowed line so nothing silently disappears.
struct EdgeLimiter
{
    uint64_t windowStart = 0;
    unsigned inWindow = 0, suppressed = 0;
    // Returns true when the caller may log; *carried receives the number of edges suppressed since the
    // last allowed line.
    bool Allow(uint64_t now, unsigned burst, uint64_t windowMs, unsigned* carried) noexcept
    {
        if (now < windowStart || now - windowStart >= windowMs) { windowStart = now; inWindow = 0; }
        if (inWindow >= burst) { ++suppressed; return false; }
        ++inWindow;
        if (carried) *carried = suppressed;
        suppressed = 0;
        return true;
    }
};

// ---- seat direct drive (legacy_direct_drive.inl, sim thread): one DriveSample per accumulator call ----
enum DriveOutcome : uint8_t
{
    DriveNone, DriveOff, DriveNoHook, DriveNoRecord, DriveGapKept, DriveSkipped, DriveArming, DriveDisarmed,
    DriveUnverified, DriveWrote, DriveNotFresh, DriveFault, DrivePutBack, DrivePutBackRefused, DriveEndUpdate,
    DriveOutcomeCount
};
inline constexpr const char* kDriveOutcomeNames[DriveOutcomeCount]{"none","off","nohook","norecord","gapkept",
    "skipped","arming","DISARMED","unverified","wrote","notfresh","FAULT","putback","putback-refused","endupdate"};
// Caller kinds, in aim_caller::Caller order (Stick, Impulse, Sway, Kick, NoUnit, Unknown).
inline constexpr unsigned kDriveCallerCount = 6;
inline constexpr const char* kDriveCallerNames[kDriveCallerCount]{"stick","impulse","sway","kick","no-unit",
    "unknown"};
enum DriveFlag : uint16_t
{
    DriveArmed = 1u << 0, DriveFresh = 1u << 1, DriveSeatedSet = 1u << 2, DriveOnFoot = 1u << 3,
    DriveSeatConfirmed = 1u << 4, DriveStint = 1u << 5, DriveYawOk = 1u << 6, DrivePitchOk = 1u << 7,
    DrivePitchFailed = 1u << 8, DriveLimited = 1u << 9, DriveCoherent = 1u << 10, DriveTitleSeated = 1u << 11,
};
inline constexpr unsigned kDriveFlagCount = 12;
inline constexpr const char* kDriveFlagNames[kDriveFlagCount]{"armed","fresh","pubSeat","onFoot","seatOK",
    "stint","yawOK","pitchOK","pitchFAIL","limited","coherent","titleSeat"};

struct DriveSample
{
    uint64_t ms = 0;                      // QPC milliseconds
    uint32_t pcLow = 0;                   // the player_control record (low 32 bits of its address)
    uint32_t seatKey = 0;                 // seat_drive::SeatKey of the publication
    float nativeYaw = 0, nativePitch = 0; // the record as the native update left it (radians)
    float wroteYaw = 0, wrotePitch = 0;   // what we wrote (NaN = axis not written)
    float setYaw = 0, setPitch = 0;       // the input thread's target
    float aimYaw = 0, aimPitch = 0;       // the game's own aim, as the input thread read it
    uint16_t pubAgeMs = 0xFFFF;           // age of the publication (capped)
    uint16_t flags = 0;                   // DriveFlag
    uint8_t slot = 0;                     // 0 Halo 3, 1 ODST, 2 Halo 4
    uint8_t caller = 0;                   // aim_caller::Caller
    uint8_t outcome = DriveNone;
    uint8_t axes = 0;                     // low nibble: axes wanted (1 yaw, 2 pitch); high nibble: axes written
    uint8_t refusal = 0;                  // put-back refused: aim_caller::Refusal
};

inline bool DriveSameState(const DriveSample& a, const DriveSample& b) noexcept
{
    return a.pcLow == b.pcLow && a.seatKey == b.seatKey && a.flags == b.flags && a.slot == b.slot &&
        a.caller == b.caller && a.outcome == b.outcome && a.axes == b.axes && a.refusal == b.refusal;
}

inline float WrapRad(float a) noexcept
{
    constexpr float twoPi = 6.28318531f, pi = 3.14159265f;
    if (!(a == a) || a > 1e6f || a < -1e6f) return a;
    while (a > pi) a -= twoPi;
    while (a <= -pi) a += twoPi;
    return a;
}
inline float AbsDeg(float rad) noexcept { return (rad < 0 ? -rad : rad) * 57.2957795f; }

// One time bucket of drive calls for one title slot.
struct DriveBucket
{
    uint64_t fromMs = 0, toMs = 0;
    uint32_t byCaller[kDriveCallerCount]{};
    uint32_t byOutcome[DriveOutcomeCount]{};
    uint32_t yawWrites = 0, pitchWrites = 0;
    float maxWroteVsAimYaw = 0, maxWroteVsAimPitch = 0;    // deg: our write against the game's aim
    float maxDriftYaw = 0, maxDriftPitch = 0;              // deg: native at a stick call vs our previous write
    float maxNativeVsAimYaw = 0, maxNativeVsAimPitch = 0;  // deg: the record against the game's aim
    uint32_t pcChanges = 0;
    // RAW (unwrapped) ranges, deg: a yaw written on another 2*pi branch than the record shows up here and
    // in branchJumps, where the wrapped differences above would hide it.
    float minWroteYaw = 0, maxWroteYaw = 0, minNativeYaw = 0, maxNativeYaw = 0, minAimYaw = 0, maxAimYaw = 0;
    float minWrotePitch = 0, maxWrotePitch = 0;
    bool haveWroteYaw = false, haveNativeYaw = false, haveAimYaw = false, haveWrotePitch = false;
    uint32_t branchJumps = 0;    // stick writes whose raw yaw differs from the record's raw yaw by more than pi
    uint32_t bigSteps = 0;       // stick writes moving the record's yaw or pitch by more than 30 deg raw
};
inline void Widen(float v, float& lo, float& hi, bool& have) noexcept
{
    if (!(v == v)) return;
    if (!have) { lo = hi = v; have = true; return; }
    if (v < lo) lo = v;
    if (v > hi) hi = v;
}

// samples chronological; buckets of bucketMs from fromMs up to toMs. Returns the number of buckets filled.
inline size_t SummarizeDrive(const DriveSample* samples, size_t count, uint8_t slot, uint64_t fromMs,
    uint64_t toMs, uint64_t bucketMs, DriveBucket* out, size_t maxBuckets) noexcept
{
    if (!bucketMs || !maxBuckets || toMs <= fromMs) return 0;
    size_t buckets = static_cast<size_t>((toMs - fromMs + bucketMs - 1) / bucketMs);
    if (buckets > maxBuckets) buckets = maxBuckets;
    for (size_t b = 0; b < buckets; ++b)
    {
        out[b] = DriveBucket{};
        out[b].fromMs = fromMs + b * bucketMs;
        out[b].toMs = out[b].fromMs + bucketMs;
    }
    bool haveWrite = false, havePc = false;
    uint32_t lastPc = 0;
    float lastYaw = 0, lastPitch = 0;
    bool lastYawValid = false, lastPitchValid = false;
    for (size_t i = 0; i < count; ++i)
    {
        const DriveSample& s = samples[i];
        if (s.slot != slot) continue;
        const bool inWindow = s.ms >= fromMs && s.ms < fromMs + buckets * bucketMs;
        DriveBucket* b = inWindow ? &out[(s.ms - fromMs) / bucketMs] : nullptr;
        if (b)
        {
            if (s.caller < kDriveCallerCount) ++b->byCaller[s.caller];
            if (s.outcome < DriveOutcomeCount) ++b->byOutcome[s.outcome];
            if (havePc && s.pcLow != lastPc) ++b->pcChanges;
        }
        if (b && s.caller == 0 && haveWrite && s.pcLow == lastPc)
        {
            if (lastYawValid)
            { const float d = AbsDeg(WrapRad(s.nativeYaw - lastYaw)); if (d > b->maxDriftYaw) b->maxDriftYaw = d; }
            if (lastPitchValid)
            { const float d = AbsDeg(s.nativePitch - lastPitch); if (d > b->maxDriftPitch) b->maxDriftPitch = d; }
        }
        if (b && s.caller == 0)
        {
            Widen(s.nativeYaw * 57.2957795f, b->minNativeYaw, b->maxNativeYaw, b->haveNativeYaw);
            if (s.flags & DriveFresh) Widen(s.aimYaw * 57.2957795f, b->minAimYaw, b->maxAimYaw, b->haveAimYaw);
        }
        if (b && s.caller == 0 && (s.flags & DriveFresh))
        {
            const float ny = AbsDeg(WrapRad(s.nativeYaw - s.aimYaw)), np = AbsDeg(s.nativePitch - s.aimPitch);
            if (ny > b->maxNativeVsAimYaw) b->maxNativeVsAimYaw = ny;
            if (np > b->maxNativeVsAimPitch) b->maxNativeVsAimPitch = np;
        }
        const bool wroteYaw = s.wroteYaw == s.wroteYaw, wrotePitch = s.wrotePitch == s.wrotePitch;
        if (s.caller == 0 && s.outcome == DriveWrote)
        {
            if (b)
            {
                if (wroteYaw)
                {
                    Widen(s.wroteYaw * 57.2957795f, b->minWroteYaw, b->maxWroteYaw, b->haveWroteYaw);
                    const float raw = s.wroteYaw - s.nativeYaw;
                    if (raw > 3.14159265f || raw < -3.14159265f) ++b->branchJumps;
                    if (raw > 0.5235988f || raw < -0.5235988f) ++b->bigSteps;
                    ++b->yawWrites;
                    const float d = AbsDeg(WrapRad(s.wroteYaw - s.aimYaw));
                    if (d > b->maxWroteVsAimYaw) b->maxWroteVsAimYaw = d;
                }
                if (wrotePitch)
                {
                    Widen(s.wrotePitch * 57.2957795f, b->minWrotePitch, b->maxWrotePitch, b->haveWrotePitch);
                    const float rawP = s.wrotePitch - s.nativePitch;
                    if ((rawP > 0.5235988f || rawP < -0.5235988f) &&
                        !(wroteYaw && ((s.wroteYaw - s.nativeYaw) > 0.5235988f || (s.wroteYaw - s.nativeYaw) < -0.5235988f)))
                        ++b->bigSteps;
                    ++b->pitchWrites;
                    const float d = AbsDeg(s.wrotePitch - s.aimPitch);
                    if (d > b->maxWroteVsAimPitch) b->maxWroteVsAimPitch = d;
                }
            }
            haveWrite = true;
            lastYaw = s.wroteYaw; lastPitch = s.wrotePitch;
            lastYawValid = wroteYaw; lastPitchValid = wrotePitch;
        }
        else if (s.caller == 0)
        {
            haveWrite = false;   // a stick call that wrote nothing ends the drift reference
        }
        lastPc = s.pcLow;
        havePc = true;
    }
    return buckets;
}

// ---- the game's own Use record, as the native player-control builder reads it (native_vr_actions_*) ----
struct NativeActionEntry
{
    uint64_t ms = 0;
    uint16_t heldMs = 0;     // Counters.milliseconds
    uint8_t frames = 0;      // Counters.frames (0 = not held)
    uint8_t action = 0;      // native action id (3 = Use/Exit in Halo 3 / ODST)
    uint8_t title = 0;
    uint8_t present = 0;     // the reader returned a record
    uint8_t heartbeat = 0;   // stored because time passed, not because the held state changed
};

struct NativeHeldSpan
{
    bool any = false;            // held at some point in the window
    int64_t firstMs = 0, lastMs = 0;
    uint8_t maxFrames = 0;
    uint16_t maxHeldMs = 0;
    size_t reads = 0;            // entries inside the window (toggles + heartbeats)
    bool covered = false;        // an entry at or before the window start exists
};

// Entries chronological; an entry's held state holds until the next entry of the same title and action.
inline NativeHeldSpan EvaluateNativeHeld(const NativeActionEntry* e, size_t count, uint8_t title,
    uint8_t action, uint64_t eventMs, uint64_t windowMs) noexcept
{
    NativeHeldSpan span{};
    const uint64_t from = eventMs > windowMs ? eventMs - windowMs : 0;
    const NativeActionEntry* state = nullptr;
    const auto rel = [eventMs](uint64_t ms) { return static_cast<int64_t>(ms) - static_cast<int64_t>(eventMs); };
    for (size_t i = 0; i < count; ++i)
    {
        const NativeActionEntry& x = e[i];
        if (x.title != title || x.action != action) continue;
        if (x.ms > eventMs) break;
        if (x.ms <= from) { state = &x; span.covered = true; continue; }
        if (state && state->frames)
        {
            if (!span.any) span.firstMs = rel(state->ms > from ? state->ms : from);
            span.any = true;
            span.lastMs = rel(x.ms);
        }
        if (x.frames > span.maxFrames) span.maxFrames = x.frames;
        if (x.heldMs > span.maxHeldMs) span.maxHeldMs = x.heldMs;
        ++span.reads;
        state = &x;
    }
    if (state && state->frames)
    {
        if (!span.any) span.firstMs = rel(state->ms > from ? state->ms : from);
        span.any = true;
        span.lastMs = 0;
        if (state->frames > span.maxFrames) span.maxFrames = state->frames;
        if (state->heldMs > span.maxHeldMs) span.maxHeldMs = state->heldMs;
    }
    return span;
}

// ---- the vehicle's own frame (the Halo 3 sampler's proven object frame) ----
struct VehicleFrameEntry
{
    uint64_t ms = 0;
    float pos[3]{}, fwd[3]{}, up[3]{};
};
struct VehicleKinematics
{
    bool valid = false;
    float up[3]{}, fwd[3]{};
    float speed = 0;        // world units / s between the last two frames
    float maxSpeed = 0;     // over the window
    float minUpZ = 1;       // over the window (< 0 = upside down)
    int64_t lastMs = 0;     // the last frame, relative to the event
    unsigned frames = 0;    // frames in the window
};
// frames chronological (one vehicle stint). Speeds use consecutive frames at least minDtMs apart.
inline VehicleKinematics SummarizeVehicle(const VehicleFrameEntry* f, size_t count, uint64_t eventMs,
    uint64_t windowMs, uint64_t minDtMs = 5) noexcept
{
    VehicleKinematics k{};
    const uint64_t from = eventMs > windowMs ? eventMs - windowMs : 0;
    const VehicleFrameEntry* prev = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        const VehicleFrameEntry& x = f[i];
        if (x.ms > eventMs) break;
        if (x.ms >= from)
        {
            ++k.frames;
            if (x.up[2] < k.minUpZ) k.minUpZ = x.up[2];
            if (prev && x.ms >= prev->ms + minDtMs)
            {
                const float dx = x.pos[0] - prev->pos[0], dy = x.pos[1] - prev->pos[1], dz = x.pos[2] - prev->pos[2];
                float d2 = dx * dx + dy * dy + dz * dz, d = d2;
                // sqrt by Newton (keeps the header <cmath>-free)
                if (d2 > 0) { d = d2 > 1 ? d2 : 1; for (int n = 0; n < 20; ++n) d = 0.5f * (d + d2 / d); }
                k.speed = d * 1000.0f / static_cast<float>(x.ms - prev->ms);
                if (k.speed > k.maxSpeed) k.maxSpeed = k.speed;
            }
            k.valid = true;
            for (int a = 0; a < 3; ++a) { k.up[a] = x.up[a]; k.fwd[a] = x.fwd[a]; }
            k.lastMs = static_cast<int64_t>(x.ms) - static_cast<int64_t>(eventMs);
        }
        if (!prev || x.ms >= prev->ms + minDtMs) prev = &x;
    }
    return k;
}
}
