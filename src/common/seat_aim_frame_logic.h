#pragma once
// SEATS AIM IN WORLD ANGLES (docs/ALL-TITLES-AIM-2026-09-29.md, "Seats aim in world angles: the C20 flag and
// the vehicle-tilt rebase"). Pure logic for src/dll/seat_aim_world_frame.inl; unit-tested in
// tests/seat_aim_frame_tests.cpp, and re-proven offline against the retail modules by
// tools/verify_seat_aim_world_frame.py (which reads the site table below straight out of this file).
//
// Halo 3 builds a player unit's aim vector from the control angles (0xE3A48, four call sites, 0xE8DD8's among
// them) and then hands it to ONE function, 0x355E20 (its only caller is the E8 at 0xE3C4B), which re-expresses
// the vector in the parent vehicle's UP frame -- unless the occupied seat's tag flags carry bit 4, the `third
// person camera` flag. Every stock player seat carries bit 4, so stock never rotates a player's aim: the control
// angles ARE world angles. Our C20 first-person seat patch clears bit 4 on the occupied seat (that is what
// gives the cockpit view), which switches the rebase ON, and the seat direct drive writes WORLD yaw/pitch:
// the hull's tilt is then added to the aim (a Banshee runs away in pitch, a Chopper's aim hops with its nose).
// ODST has the same mechanism at 0x39A600 (caller 0xFDF48 in 0xFDD30).
// PLAYERS ONLY, proven offline by the tool: 0xE3A48 / 0xFDD30 index the module's "players" data array with their
// first argument and pass the rebase the unit handle at +0x28 of that player record, so whichever call site runs,
// only a unit a player record holds can reach the rebase; an AI unit cannot.
//
// The fix makes the bit-4 test's `jne skip` unconditional (`jmp skip; nop`), so the rebase never runs: player
// seats aim in world angles as in stock while the flag patch keeps the first-person view. Stock seats all have
// bit 4, so the patch is behaviour-neutral for them, and with it a modded player computes the same aim as an
// unmodded co-op peer (see Wanted for why that holds here, and in Reach only for R-V27's seats; not in Halo 2 / CE).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace seat_aim_frame
{
// `imul rdx,rax,0xD4; mov eax,[rdx+rcx]; shr eax,4; test al,1` + the two bytes after it -- the seat-flags bit-4
// test, unique in both modules. The 6-byte jne (0F 85 rel32) starts kAnchorToJcc bytes in. The jne's own first
// two bytes are WILDCARDS on purpose: our patch turns them into `E9 xx`, and an anchor that required `0F 85`
// could never find a jump an earlier install left in place (a teardown restore that gave up), so the adopt path
// would be unreachable and the jump unmanaged. The anchor locates the site; IsStockJcc / IsSkipPatch then decide
// whose bytes are there. tools/verify_seat_aim_world_frame.py proves this pattern unique in the stock module AND
// in the module with our jump applied.
inline constexpr const char* kAnchorPattern = "48 69 D0 D4 00 00 00 8B 04 0A C1 E8 04 A8 01 ?? ??";
inline constexpr uint32_t kAnchorToJcc = 15;
// Reach's bit-4 test (0x48AE77): `imul rdx,rax,0x4B; add rdx,r8; shr r8,0x1C; mov rax,[rcx+r8*8];
// mov edx,[rax+rdx*4]; shr edx,4; test dl,1` + the jne's two opcode bytes, wildcarded for the same reason. Unique
// in the stock module and with our jump applied (tools/verify_seat_aim_world_frame.py).
inline constexpr const char* kReachAnchorPattern =
    "48 6B D0 4B 49 03 D0 49 C1 E8 1C 4A 8B 04 C1 8B 14 90 C1 EA 04 F6 C2 01 ?? ??";
inline constexpr uint32_t kReachAnchorToJcc = 24;
inline constexpr size_t kPatchBytes = 6;

struct Site
{
    const char* name;
    uint32_t rebaseRva;          // the rebase function (.pdata begin)
    uint32_t callerRva;          // its only call (E8 rel32)
    uint32_t anchorRva;          // anchorPattern, unique, here
    uint32_t jccRva;             // the jne (anchorRva + anchorToJcc)
    uint32_t skipRva;            // its target: the function's no-rotation exit
    const char* anchorPattern;   // the bit-4 test + the jne's two opcode bytes (wildcards)
    uint32_t anchorToJcc;
};

// ADDR-HYGIENE: guarded. Measured on the retail Steam modules (the Store modules are byte-identical,
// MCC-EDITIONS-EVIDENCE). The install refuses unless the anchor is unique and at anchorRva, and the six bytes at
// the jne are exactly 0F 85 with its target at skipRva (or our own E9 to skipRva + 90, which it adopts).
inline constexpr Site kHalo3Site{"Halo 3", 0x355E20, 0xE3C4B, 0x355EEB, 0x355EFA, 0x355FDD, kAnchorPattern,
                                 kAnchorToJcc};
inline constexpr Site kOdstSite{"ODST", 0x39A600, 0xFDF48, 0x39A6B5, 0x39A6C4, 0x39A7C8, kAnchorPattern, kAnchorToJcc};
// REACH: the same mechanism (docs/ALL-TITLES-AIM-2026-09-29.md, the per-title table). The rebase
// 0x48ADB8's only call is 0x45E34 in the unit-aim build 0x45C2C, which indexes the "players" data array (TLS block
// +0x18, element 0x490, data_array_new at 0x43469) with its first argument and hands the rebase the unit at +0x28
// of that record -- players only, as in Halo 3. The skip label 0x48AF72 is the function's common exit (its
// "no parent" / "no seat" early-outs take it too); it returns r9b, which the caller ignores. Our R-V27 seat patch
// clears bit 4 only on `allows weapons` passenger seats; stock Reach keeps bit 4 there, so skipping restores what
// an unmodded peer computes -- case (a). A seat whose OWN flags lack bit 4 stands the skip down (Wanted below):
// there every peer rebases, and the seat drive writes R^-1 of the aim instead -- case (b).
inline constexpr Site kReachSite{"Reach", 0x48ADB8, 0x45E34, 0x48AE77, 0x48AE8F, 0x48AF72, kReachAnchorPattern,
                                 kReachAnchorToJcc};

inline int32_t ReadI32(const uint8_t* p)
{
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
                                static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24);
}

inline void WriteI32(uint8_t* p, int32_t v)
{
    const uint32_t u = static_cast<uint32_t>(v);
    p[0] = static_cast<uint8_t>(u);
    p[1] = static_cast<uint8_t>(u >> 8);
    p[2] = static_cast<uint8_t>(u >> 16);
    p[3] = static_cast<uint8_t>(u >> 24);
}

// The rel32 an instruction of `length` bytes at `at` needs to reach `target`; false when out of range.
inline bool Rel32(uint32_t at, uint32_t length, uint32_t target, int32_t& rel)
{
    const int64_t r = static_cast<int64_t>(target) - (static_cast<int64_t>(at) + length);
    if (r < INT32_MIN || r > INT32_MAX)
        return false;
    rel = static_cast<int32_t>(r);
    return true;
}

// The stock bytes: `jne skip` (0F 85 rel32) at jcc.
inline bool IsStockJcc(const uint8_t bytes[kPatchBytes], uint32_t jcc, uint32_t skip)
{
    int32_t rel = 0;
    return bytes[0] == 0x0F && bytes[1] == 0x85 && Rel32(jcc, 6, skip, rel) && ReadI32(bytes + 2) == rel;
}

// Our bytes: `jmp skip; nop` (E9 rel32, 90) at jcc.
inline bool IsSkipPatch(const uint8_t bytes[kPatchBytes], uint32_t jcc, uint32_t skip)
{
    int32_t rel = 0;
    return bytes[0] == 0xE9 && Rel32(jcc, 5, skip, rel) && ReadI32(bytes + 1) == rel && bytes[5] == 0x90;
}

// The stock jne -> our unconditional jump to the same target (rel32 + 1: the jmp is one byte shorter).
// Refuses anything that is not exactly the stock jne to `skip`.
inline bool BuildSkipPatch(const uint8_t original[kPatchBytes], uint32_t jcc, uint32_t skip,
                           uint8_t out[kPatchBytes])
{
    int32_t rel = 0;
    if (!IsStockJcc(original, jcc, skip) || !Rel32(jcc, 5, skip, rel))
        return false;
    out[0] = 0xE9;
    WriteI32(out + 1, rel);
    out[5] = 0x90;
    return true;
}

// The inverse: our jump -> the stock jne. Lets an install adopt a patch a previous session left behind.
inline bool BuildStockJcc(const uint8_t patched[kPatchBytes], uint32_t jcc, uint32_t skip,
                          uint8_t out[kPatchBytes])
{
    int32_t rel = 0;
    if (!IsSkipPatch(patched, jcc, skip) || !Rel32(jcc, 6, skip, rel))
        return false;
    out[0] = 0x0F;
    out[1] = 0x85;
    WriteI32(out + 2, rel);
    return true;
}

// Whether the rebase is skipped. WHY THE SKIP IS STOCK BEHAVIOUR, and the only reason it is allowed: every stock
// Halo 3 / ODST player seat carries bit 4, so an unmodded game -- and every unmodded co-op peer -- never runs the
// rebase for a player; it is our own C20 patch that clears the bit. Skipping restores exactly what an unmodded
// peer computes. (Halo 2 and CE have a similar rebase, but there some stock seats natively LACK the flag and no
// patch of ours clears it, so there is nothing to skip: their seat drives write the inverse instead. Reach skips
// too, but only where R-V27 cleared the bit -- ReachWanted -- for the same reason as here.)
// So: skipped whenever the title's first-person seat patch is enabled (vehicle_first_person and
// vehicle_hide_body, as the C20 patch reads them), unless the dev A/B key says to leave the game's rebase, and
// NOT while the occupied seat natively lacks bit 4 (a custom or modded seat: there stock rebases, so we must too).
//
// AND only while the title's vehicle sampler is BOUND (vehicleSamplerBound: the binding the sampler itself gates
// on is Installed, with its engine TLS index). The sampler is the only thing that ever sees the occupied seat's
// original flags, so it is both what runs C20 and what publishes seatNativelyLacksBit4. Unbound (signature
// resolve failed, or a fault disarmed it) nothing clears bit 4, so the skip would buy nothing, and nothing would
// tell us about a natively bit-4-less seat, so it would run blind; standing down leaves exactly stock. This never
// flaps: the binding is decided once per title install and can only fall from Installed to StockFallback (the
// fault path restores the seat flag first), never back within a module generation.
//
// The residual gap, deliberately NOT closed: bound is not RUNNING. The sampler runs from the camera copy hook
// (Halo 3: every camera copy while the title generation is published; ODST: only on the single-user primary
// camera path), so a natively bit-4-less seat occupied while that hook is not sampling is not seen until it
// samples again, and for that window the skip suppresses a rebase stock would run. Gating on a sampler heartbeat
// instead would be worse: the hook stops on pauses and loads (and off ODST's single-user path), so the code patch
// would toggle -- each toggle freezes every game thread -- and it would put the rebase back while C20 may still
// hold bit 4 cleared on a seat sampled earlier, which is the very fault this patch removes.
inline bool Wanted(bool vehicleFirstPerson, bool vehicleHideBody, bool worldFrameKey, bool vehicleSamplerBound,
                   bool seatNativelyLacksBit4)
{
    return vehicleFirstPerson && vehicleHideBody && worldFrameKey && vehicleSamplerBound && !seatNativelyLacksBit4;
}

// REACH: the same rule, keyed on what R-V27 itself reads. R-V27 (ReachEnsureNativeFirstPersonSeat) clears bit 4
// while `vehicle_first_person` is on and the Reach vehicle camera binding is Installed -- it does not read
// vehicle_hide_body -- so the skip follows exactly those, and stands down while the occupied seat's ORIGINAL flags
// lack bit 4 (published by the Reach sampler from R-V27's own lease). The bound-is-not-running gap is the same as
// above (the sampler runs from the camera hooks).
inline bool ReachWanted(bool vehicleFirstPerson, bool worldFrameKey, bool cameraBindingInstalled,
                        bool seatNativelyLacksBit4)
{
    return vehicleFirstPerson && worldFrameKey && cameraBindingInstalled && !seatNativelyLacksBit4;
}

// Halo's forward for control angles: x = cos(pitch)cos(yaw), y = cos(pitch)sin(yaw), z = sin(pitch).
inline void Forward(float yaw, float pitch, float out[3])
{
    const float cp = std::cos(pitch);
    out[0] = cp * std::cos(yaw);
    out[1] = cp * std::sin(yaw);
    out[2] = std::sin(pitch);
}

// The angle between e(yaw1, pitch1) and e(yaw2, pitch2), degrees; NaN when an input is not finite.
inline float AimSeparationDeg(float yaw1, float pitch1, float yaw2, float pitch2)
{
    if (!std::isfinite(yaw1) || !std::isfinite(pitch1) || !std::isfinite(yaw2) || !std::isfinite(pitch2))
        return std::numeric_limits<float>::quiet_NaN();
    float a[3], b[3];
    Forward(yaw1, pitch1, a);
    Forward(yaw2, pitch2, b);
    // atan2(|a x b|, a.b): accurate at small angles, where acos(a.b) is not.
    const float cx = a[1] * b[2] - a[2] * b[1], cy = a[2] * b[0] - a[0] * b[2], cz = a[0] * b[1] - a[1] * b[0];
    const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), dot) * 57.2957795f;
}

// How far a vehicle's up vector leans from world up (+Z), degrees; NaN when it is not a usable vector.
inline float TiltDeg(const float up[3])
{
    if (!std::isfinite(up[0]) || !std::isfinite(up[1]) || !std::isfinite(up[2]))
        return std::numeric_limits<float>::quiet_NaN();
    const float horizontal = std::sqrt(up[0] * up[0] + up[1] * up[1]);
    if (horizontal + std::fabs(up[2]) < 1e-4f)
        return std::numeric_limits<float>::quiet_NaN();
    return std::atan2(horizontal, up[2]) * 57.2957795f;
}
} // namespace seat_aim_frame
