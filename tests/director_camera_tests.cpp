// The game's own first-person answer for Reach, Halo 4 and Halo 2 (src/common/director_camera_logic.h): the
// decoders against the pinned retail modules' own bytes, the Halo 2 perspective rule, the value agreement with
// the view model, and what the new rung does to roomscale's admission and to two-handed aim's exit.
// Pure logic: no clock, no engine. The bytes are the matched code sites exactly as the pinned modules have them
// (haloreach.dll 738DD2D2...0894, halo4.dll 7C53E7D5...FA84, halo2.dll DE65B4F4...1946); the offline verifiers
// tools/verify-{reach,halo4,halo2}-director-camera.py prove the same sites unique in the whole modules.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include "../src/common/director_camera_logic.h"
#include "../src/common/roomscale_admission_logic.h"
#include "../src/dll/sigscan.h"

namespace
{
    // reach +0xC5B6D: c_director::set_camera_mode's store
    const uint8_t kReachStore[111]{
        0xE8, 0x12, 0xFB, 0xFF, 0xFF, 0x8B, 0x15, 0xA0, 0x1F, 0xB5, 0x00, 0x65, 0x48, 0x8B, 0x0C, 0x25, 0x58, 0x00, 0x00, 0x00,
        0x4C, 0x63, 0x86, 0x80, 0x01, 0x00, 0x00, 0x41, 0xB9, 0x88, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x0C, 0xD1, 0x49, 0x8B, 0x14,
        0x09, 0x4F, 0x8D, 0x0C, 0x40, 0x42, 0x39, 0x84, 0x8A, 0xB4, 0x06, 0x00, 0x00, 0x75, 0x0D, 0x4B, 0x8D, 0x0C, 0x40, 0x39,
        0xAC, 0x8A, 0xB8, 0x06, 0x00, 0x00, 0x74, 0x32, 0x42, 0x89, 0x84, 0x8A, 0xB4, 0x06, 0x00, 0x00, 0x8B, 0xDF, 0x48, 0x63,
        0x86, 0x80, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x0C, 0x40, 0x89, 0xAC, 0x8A, 0xB8, 0x06, 0x00, 0x00, 0x8B, 0xB6, 0x80, 0x01,
        0x00, 0x00, 0x8B, 0xD3, 0x8B, 0xCE, 0xE8, 0x20, 0xDC, 0x1E, 0x00,
    };
    // reach +0x2B37FC: the first-person weapons' gate
    const uint8_t kReachGate[91]{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x8B, 0x05, 0x0B, 0x43, 0x96, 0x00, 0x33, 0xDB, 0x65,
        0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x48, 0x63, 0xF9, 0x4E, 0x8B, 0x1C, 0xC0, 0x4C, 0x69, 0xC7, 0xA8, 0x53,
        0x00, 0x00, 0xB8, 0xA0, 0x06, 0x00, 0x00, 0x4E, 0x03, 0x04, 0x18, 0x48, 0x63, 0xC2, 0x4C, 0x69, 0xC8, 0x78, 0x29, 0x00,
        0x00, 0x43, 0xF6, 0x44, 0x01, 0x30, 0x01, 0x74, 0x53, 0xB8, 0x88, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x0C, 0x7F, 0x4A, 0x8B,
        0x04, 0x18, 0x39, 0x9C, 0x88, 0xB4, 0x06, 0x00, 0x00, 0x75, 0x3D,
    };
    // reach +0x715AC: the sandbox-editor predicate's head
    const uint8_t kReachPredicate[54]{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x32, 0xC9, 0x44, 0x8B,
        0xD1, 0x48, 0x8B, 0xDA, 0x83, 0xF9, 0xFF, 0x0F, 0x84, 0xBB, 0x00, 0x00, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00,
        0x00, 0x00, 0x44, 0x8B, 0x05, 0x3B, 0x65, 0xBA, 0x00, 0xBE, 0x20, 0x00, 0x00, 0x00,
    };
    // reach +0x1342A0: c_game_director::update's player / unit / predicate calls
    const uint8_t kReachCallSite[45]{
        0xE8, 0x23, 0xFC, 0xF1, 0xFF, 0x8B, 0xCB, 0x8B, 0xF0, 0xE8, 0x4A, 0xFC, 0xF1, 0xFF, 0x45, 0x32, 0xE4, 0xC6, 0x45, 0xBB,
        0x00, 0x44, 0x88, 0x65, 0xB7, 0x44, 0x8B, 0xF8, 0x83, 0xFE, 0xFF, 0x74, 0x0C, 0x33, 0xD2, 0x8B, 0xCE, 0xE8, 0xE2, 0xD2,
        0xF3, 0xFF, 0x88, 0x45, 0xBB,
    };
    // reach +0x53EC8: player index by output user
    const uint8_t kReachPlayerGetter[48]{
        0x83, 0xC8, 0xFF, 0x83, 0xF9, 0x03, 0x77, 0x27, 0x8B, 0x15, 0x42, 0x3C, 0xBC, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58,
        0x00, 0x00, 0x00, 0x41, 0xB8, 0x58, 0x01, 0x00, 0x00, 0x48, 0x63, 0xC9, 0x48, 0x8B, 0x04, 0xD0, 0x4A, 0x8B, 0x04, 0x00,
        0x8B, 0x84, 0x88, 0xB8, 0x00, 0x00, 0x00, 0xC3,
    };
    // halo4 +0x10C1D8
    const uint8_t kHalo4Store[111]{
        0xE8, 0xFF, 0xF9, 0xFF, 0xFF, 0x8B, 0x15, 0x35, 0xB0, 0xF4, 0x00, 0x65, 0x48, 0x8B, 0x0C, 0x25, 0x58, 0x00, 0x00, 0x00,
        0x4C, 0x63, 0x86, 0xA0, 0x01, 0x00, 0x00, 0x41, 0xB9, 0x50, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x0C, 0xD1, 0x49, 0x8B, 0x14,
        0x09, 0x4F, 0x8D, 0x0C, 0x40, 0x42, 0x39, 0x84, 0x8A, 0x44, 0x07, 0x00, 0x00, 0x75, 0x0D, 0x4B, 0x8D, 0x0C, 0x40, 0x39,
        0xAC, 0x8A, 0x48, 0x07, 0x00, 0x00, 0x74, 0x32, 0x42, 0x89, 0x84, 0x8A, 0x44, 0x07, 0x00, 0x00, 0x8B, 0xDF, 0x48, 0x63,
        0x86, 0xA0, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x0C, 0x40, 0x89, 0xAC, 0x8A, 0x48, 0x07, 0x00, 0x00, 0x8B, 0xB6, 0xA0, 0x01,
        0x00, 0x00, 0x8B, 0xD3, 0x8B, 0xCE, 0xE8, 0x05, 0xAA, 0x2A, 0x00,
    };
    // halo4 +0x3B6C4C
    const uint8_t kHalo4Gate[91]{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x8B, 0x05, 0xBB, 0x05, 0xCA, 0x00, 0x33, 0xDB, 0x65,
        0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x48, 0x63, 0xF9, 0x4E, 0x8B, 0x1C, 0xC0, 0x4C, 0x69, 0xC7, 0x48, 0x5F,
        0x00, 0x00, 0xB8, 0xA0, 0x06, 0x00, 0x00, 0x4E, 0x03, 0x04, 0x18, 0x48, 0x63, 0xC2, 0x4C, 0x69, 0xC8, 0xC8, 0x2E, 0x00,
        0x00, 0x43, 0xF6, 0x44, 0x01, 0x60, 0x01, 0x74, 0x5C, 0xB8, 0x50, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x0C, 0x7F, 0x4A, 0x8B,
        0x04, 0x18, 0x39, 0x9C, 0x88, 0x44, 0x07, 0x00, 0x00, 0x75, 0x46,
    };
    // halo4 +0xBA400
    const uint8_t kHalo4Predicate[54]{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x32, 0xC9, 0x44, 0x8B,
        0xD1, 0x48, 0x8B, 0xDA, 0x83, 0xF9, 0xFF, 0x0F, 0x84, 0xBE, 0x00, 0x00, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00,
        0x00, 0x00, 0x44, 0x8B, 0x05, 0xE7, 0xCD, 0xF9, 0x00, 0xBE, 0x10, 0x00, 0x00, 0x00,
    };
    // halo4 +0x20571B
    const uint8_t kHalo4CallSite[56]{
        0xE8, 0x64, 0x04, 0xE9, 0xFF, 0x41, 0x8B, 0xC9, 0x8B, 0xF0, 0xE8, 0x8A, 0x04, 0xE9, 0xFF, 0x45, 0x33, 0xED, 0x44, 0x8B,
        0xF0, 0x44, 0x88, 0x6D, 0xB8, 0x45, 0x8A, 0xE5, 0x44, 0x88, 0x6D, 0xB9, 0x41, 0xBF, 0x08, 0x00, 0x00, 0x00, 0x41, 0x8D,
        0x5D, 0x01, 0x83, 0xFE, 0xFF, 0x74, 0x4E, 0x33, 0xD2, 0x8B, 0xCE, 0xE8, 0xAD, 0x4C, 0xEB, 0xFF,
    };
    // halo4 +0x95B84
    const uint8_t kHalo4PlayerGetter[48]{
        0x83, 0xC8, 0xFF, 0x83, 0xF9, 0x03, 0x77, 0x27, 0x8B, 0x15, 0x86, 0x16, 0xFC, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58,
        0x00, 0x00, 0x00, 0x41, 0xB8, 0x38, 0x01, 0x00, 0x00, 0x48, 0x63, 0xC9, 0x48, 0x8B, 0x04, 0xD0, 0x4A, 0x8B, 0x04, 0x00,
        0x8B, 0x84, 0x88, 0xB8, 0x00, 0x00, 0x00, 0xC3,
    };
    // halo2 +0x6D4EC0: director_get_perspective
    const uint8_t kHalo2Perspective[84]{
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x63, 0xC1, 0x4C, 0x8D, 0x05, 0x42, 0x63, 0xF1, 0x00, 0x48, 0x69, 0xC8, 0x50, 0x01, 0x00,
        0x00, 0x48, 0x8D, 0x15, 0xD4, 0x8F, 0x0A, 0x00, 0x4A, 0x8B, 0x44, 0x01, 0x20, 0x48, 0x3B, 0xC2, 0x75, 0x18, 0xF3, 0x42,
        0x0F, 0x10, 0x4C, 0x01, 0x14, 0x0F, 0x57, 0xC0, 0x0F, 0x2E, 0xC8, 0x7A, 0x40, 0x75, 0x3E, 0x33, 0xC0, 0x48, 0x83, 0xC4,
        0x28, 0xC3, 0x48, 0x8D, 0x0D, 0xEB, 0x7A, 0x0A, 0x00, 0x48, 0x3B, 0xC1, 0x75, 0x0A, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48,
        0x83, 0xC4, 0x28, 0xC3,
    };
    // halo2 +0x6D55B5: the director's first-person camera store
    const uint8_t kHalo2Store[69]{
        0x48, 0x69, 0xD8, 0x50, 0x01, 0x00, 0x00, 0x33, 0xC0, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x48, 0x8D, 0x3D, 0x46, 0x5C, 0xF1,
        0x00, 0x48, 0x8D, 0x4F, 0x28, 0x48, 0x03, 0xCB, 0x66, 0x89, 0x44, 0x3B, 0x70, 0xE8, 0xC5, 0x88, 0x0A, 0x00, 0xC7, 0x84,
        0x3B, 0x28, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x48, 0x8D, 0x05, 0xC3, 0x88, 0x0A, 0x00, 0x48, 0x89, 0x44, 0x3B,
        0x20, 0xC6, 0x84, 0x3B, 0x24, 0x01, 0x00, 0x00, 0x00,
    };

    size_t Tokens(const char* pattern)
    {
        size_t n = 0;
        bool in = false;
        for (const char* c = pattern; *c; ++c)
        {
            if (*c == ' ')
                in = false;
            else if (!in)
            {
                in = true;
                ++n;
            }
        }
        return n;
    }
    template <size_t N> bool Matches(const char* pattern, const uint8_t (&bytes)[N])
    {
        const auto base = reinterpret_cast<uintptr_t>(&bytes[0]);
        return Tokens(pattern) == N && sig::Find(base, N, pattern) == base;
    }
}

int RunDirectorCameraTests()
{
    using namespace director_camera;
    int failures = 0;
    auto Check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };

    // ---- The signatures are as long as their decoders assume, and match the pinned bytes ----------------------
    Check(Tokens(kSetModeStoreSitePattern) == kSetModeStoreSiteBytes &&
          Tokens(kFirstPersonWeaponGatePattern) == kFirstPersonWeaponGateBytes &&
          Tokens(kSandboxEditorPredicateHeadPattern) == kSandboxEditorPredicateHeadBytes &&
          Tokens(kOutputUserPlayerGetterPattern) == kOutputUserPlayerGetterBytes &&
          Tokens(kReachEditorCallSite.pattern) == kReachEditorCallSite.bytes &&
          Tokens(kHalo4EditorCallSite.pattern) == kHalo4EditorCallSite.bytes &&
          Tokens(kHalo2DirectorPerspectivePattern) == kHalo2DirectorPerspectiveBytes &&
          Tokens(kHalo2DirectorFirstPersonStorePattern) == kHalo2DirectorFirstPersonStoreBytes,
        "every signature is as long as its decoder assumes");
    Check(Matches(kSetModeStoreSitePattern, kReachStore) && Matches(kSetModeStoreSitePattern, kHalo4Store),
        "one set_camera_mode store pattern matches Reach's and Halo 4's pinned bytes");
    Check(Matches(kFirstPersonWeaponGatePattern, kReachGate) && Matches(kFirstPersonWeaponGatePattern, kHalo4Gate),
        "one first-person weapon gate pattern matches both titles' pinned bytes");
    Check(Matches(kSandboxEditorPredicateHeadPattern, kReachPredicate) &&
          Matches(kSandboxEditorPredicateHeadPattern, kHalo4Predicate),
        "one sandbox-editor predicate head matches both titles' pinned bytes");
    Check(Matches(kOutputUserPlayerGetterPattern, kReachPlayerGetter) &&
          Matches(kOutputUserPlayerGetterPattern, kHalo4PlayerGetter),
        "one player getter body matches both titles' pinned bytes");
    Check(Matches(kReachEditorCallSite.pattern, kReachCallSite) && Matches(kHalo4EditorCallSite.pattern, kHalo4CallSite) &&
          !sig::Find(reinterpret_cast<uintptr_t>(kHalo4CallSite), sizeof kHalo4CallSite, kReachEditorCallSite.pattern) &&
          !sig::Find(reinterpret_cast<uintptr_t>(kReachCallSite), sizeof kReachCallSite, kHalo4EditorCallSite.pattern),
        "each title's director call site matches its own pattern and not the other's");
    Check(Matches(kHalo2DirectorPerspectivePattern, kHalo2Perspective) &&
          Matches(kHalo2DirectorFirstPersonStorePattern, kHalo2Store),
        "Halo 2's two director sites match the pinned bytes");

    // ---- Reach: the record, from the pinned sites at their own offsets --------------------------------------
    {
        constexpr int64_t storeRva = 0xC5B6D, gateRva = 0x2B37FC, tlsRva = 0xC17B18;
        RecordSites sites{};
        Check(DecodeRecordSites(kReachStore, storeRva, kReachGate, gateRva, tlsRva, sites) == RecordBind::Bound &&
              sites.blockOffset == 0x188 && sites.perspectiveOffset == 0x6B4 && sites.modeOffset == 0x6B8 &&
              sites.tlsIndexRva == tlsRva,
            "Reach: block TLS +0x188, perspective +0x6B4, mode +0x6B8, engine TLS index +0xC17B18");
        Check(CallRva(kReachStore, kSetModeCallNotify, storeRva) == gateRva,
            "Reach: set_camera_mode's store calls the first-person weapon gate it was matched beside");
        Check(DecodeRecordSites(nullptr, 0, kReachGate, gateRva, tlsRva, sites) == RecordBind::StoreSiteMissing &&
              DecodeRecordSites(kReachStore, storeRva, nullptr, 0, tlsRva, sites) == RecordBind::GateMissing,
            "a missing site binds nothing, and says which");
        Check(DecodeRecordSites(kReachStore, storeRva, kReachGate, gateRva + 0x10, tlsRva, sites) ==
                  RecordBind::CallMismatch,
            "a gate that set_camera_mode does not call: refused");
        Check(DecodeRecordSites(kReachStore, storeRva, kReachGate, gateRva, tlsRva + 4, sites) ==
                  RecordBind::TlsMismatch,
            "a TLS index that is not the engine TLS index the title bound: refused");
        const auto mutated = [&](bool inStore, size_t at, uint8_t flip, RecordBind expected, const char* what) {
            uint8_t store[sizeof kReachStore], gate[sizeof kReachGate];
            std::memcpy(store, kReachStore, sizeof store);
            std::memcpy(gate, kReachGate, sizeof gate);
            (inStore ? store : gate)[at] ^= flip;
            RecordSites out{};
            Check(DecodeRecordSites(store, storeRva, gate, gateRva, tlsRva, out) == expected && out.blockOffset == 0,
                what);
        };
        mutated(false, kGateBlock, 0x08, RecordBind::FieldMismatch, "the two sites' blocks disagree: nothing bound");
        mutated(false, kGatePerspective, 0x08, RecordBind::FieldMismatch,
            "the gate's perspective field disagrees: nothing bound");
        mutated(true, kSetModePerspectiveStore, 0x08, RecordBind::FieldMismatch,
            "the store writes another field than it compares: nothing bound");
        mutated(true, kSetModeModeStore, 0x08, RecordBind::FieldMismatch,
            "the mode store is another field than the mode compare: nothing bound");
        mutated(true, kSetModeUserSecond, 0x08, RecordBind::FieldMismatch,
            "the director's user index is read from two different fields: nothing bound");
        mutated(false, kGateTlsDisp, 0x04, RecordBind::TlsMismatch,
            "the gate loads another TLS index than the store: nothing bound");
        // A neighbouring field: both perspective uses moved together, the mode left behind.
        {
            uint8_t store[sizeof kReachStore], gate[sizeof kReachGate];
            std::memcpy(store, kReachStore, sizeof store);
            std::memcpy(gate, kReachGate, sizeof gate);
            store[kSetModePerspective] = 0xB0; store[kSetModePerspectiveStore] = 0xB0; gate[kGatePerspective] = 0xB0;
            RecordSites out{};
            Check(DecodeRecordSites(store, storeRva, gate, gateRva, tlsRva, out) == RecordBind::FieldMismatch,
                "a mode that is not the field right after the perspective: nothing bound");
            store[kSetModeMode] = 0xB4; store[kSetModeModeStore] = 0xB4;
            store[kSetModeBlock] = 0x8C; gate[kGateBlock] = 0x8C;
            Check(DecodeRecordSites(store, storeRva, gate, gateRva, tlsRva, out) == RecordBind::Implausible,
                "a misaligned block offset: refused as implausible");
        }
    }

    // ---- Halo 4: the same decoder, its own constants ---------------------------------------------------------
    {
        constexpr int64_t storeRva = 0x10C1D8, gateRva = 0x3B6C4C, tlsRva = 0x1057218;
        RecordSites sites{};
        Check(DecodeRecordSites(kHalo4Store, storeRva, kHalo4Gate, gateRva, tlsRva, sites) == RecordBind::Bound &&
              sites.blockOffset == 0x150 && sites.perspectiveOffset == 0x744 && sites.modeOffset == 0x748 &&
              sites.tlsIndexRva == tlsRva,
            "Halo 4: block TLS +0x150, perspective +0x744, mode +0x748, engine TLS index +0x1057218");
        // One title's store beside the other's gate never binds.
        Check(DecodeRecordSites(kHalo4Store, storeRva, kReachGate, gateRva, tlsRva, sites) != RecordBind::Bound &&
              DecodeRecordSites(kReachStore, 0xC5B6D, kHalo4Gate, 0x2B37FC, 0xC17B18, sites) != RecordBind::Bound,
            "sites from two different titles do not prove each other");
    }

    // ---- The Monitor predicate's sites ------------------------------------------------------------------------
    {
        // Reach: predicate +0x715AC, call site +0x1342A0, player getter +0x53EC8, unit getter +0x53EF8.
        constexpr int64_t predicateRva = 0x715AC, siteRva = 0x1342A0, playerRva = 0x53EC8, unitRva = 0x53EF8,
                          tlsRva = 0xC17B18;
        EditorSites sites{};
        const auto decode = [&](bool body = true, int64_t unit = 0x53EF8, int64_t tls = 0xC17B18) {
            sites = {};
            return DecodeEditorSites(kReachEditorCallSite, kReachPredicate, predicateRva, kReachCallSite, siteRva,
                kReachPlayerGetter, body, unit, tls, sites);
        };
        Check(decode() == EditorBind::Bound && sites.predicateRva == predicateRva &&
              sites.playerGetterRva == playerRva && sites.unitGetterRva == unitRva,
            "Reach: the director calls the player getter +0x53EC8, the unit getter +0x53EF8 and the predicate +0x715AC");
        Check(decode(true, -1) == EditorBind::Bound, "no unit getter bound elsewhere to compare: still bound");
        Check(decode(false) == EditorBind::PlayerGetterBody, "a first call target without the getter's body: refused");
        Check(decode(true, unitRva + 0x30) == EditorBind::UnitGetterMismatch,
            "the call site's unit getter is not the one the seat sampler calls: refused");
        Check(decode(true, unitRva, tlsRva + 4) == EditorBind::TlsMismatch,
            "a TLS operand that is not the engine TLS index: refused");
        Check(DecodeEditorSites(kReachEditorCallSite, nullptr, 0, kReachCallSite, siteRva, kReachPlayerGetter, true,
                  unitRva, tlsRva, sites) == EditorBind::PredicateMissing &&
              DecodeEditorSites(kReachEditorCallSite, kReachPredicate, predicateRva, nullptr, 0, kReachPlayerGetter,
                  true, unitRva, tlsRva, sites) == EditorBind::CallSiteMissing &&
              DecodeEditorSites(kReachEditorCallSite, kReachPredicate, predicateRva, kReachCallSite, siteRva, nullptr,
                  false, unitRva, tlsRva, sites) == EditorBind::PlayerGetterBody,
            "a missing predicate, call site or getter binds nothing, and says which");
        Check(DecodeEditorSites(kReachEditorCallSite, kReachPredicate, predicateRva + 0x10, kReachCallSite, siteRva,
                  kReachPlayerGetter, true, unitRva, tlsRva, sites) == EditorBind::TargetMismatch,
            "the director calls something other than the matched predicate: refused");
        {
            // The first call retargeted onto the unit getter: a "player getter" that is the unit getter.
            uint8_t site[sizeof kReachCallSite];
            std::memcpy(site, kReachCallSite, sizeof site);
            const int32_t rel = int32_t(unitRva - (siteRva + 5));
            std::memcpy(site + 1, &rel, 4);
            Check(DecodeEditorSites(kReachEditorCallSite, kReachPredicate, predicateRva, site, siteRva,
                      kReachPlayerGetter, true, unitRva, tlsRva, sites) == EditorBind::PlayerGetterSame,
                "a player getter that is the unit getter itself: refused");
        }
        // Halo 4: predicate +0xBA400, call site +0x20571B, player getter +0x95B84, unit getter +0x95BB4.
        EditorSites h4{};
        Check(DecodeEditorSites(kHalo4EditorCallSite, kHalo4Predicate, 0xBA400, kHalo4CallSite, 0x20571B,
                  kHalo4PlayerGetter, true, 0x95BB4, 0x1057218, h4) == EditorBind::Bound &&
              h4.predicateRva == 0xBA400 && h4.playerGetterRva == 0x95B84 && h4.unitGetterRva == 0x95BB4,
            "Halo 4: the director calls the player getter +0x95B84, the unit getter +0x95BB4 and the predicate +0xBA400");
        bool named = true;
        for (size_t i = 0; i < size_t(EditorBind::Count); ++i)
            named = named && EditorBindName(EditorBind(i))[0] != '?';
        for (size_t i = 0; i < size_t(RecordBind::Count); ++i)
            named = named && RecordBindName(RecordBind(i))[0] != '?';
        for (size_t i = 0; i < size_t(Halo2DirectorBind::Count); ++i)
            named = named && Halo2DirectorBindName(Halo2DirectorBind(i))[0] != '?';
        Check(named, "every bind failure has a name");
    }

    // ---- Halo 2: director_get_perspective and the first-person store ------------------------------------------
    {
        constexpr int64_t perspectiveRva = 0x6D4EC0, storeRva = 0x6D55B5;
        Halo2DirectorSites sites{};
        Check(DecodeHalo2DirectorSites(kHalo2Perspective, perspectiveRva, kHalo2Store, storeRva, sites) ==
                  Halo2DirectorBind::Bound &&
              sites.globalsRva == 0x15EB210 && sites.stride == 0x150 && sites.updateOffset == 0x20 &&
              sites.transitionOffset == 0x14 && sites.firstPersonUpdateRva == 0x77DEB0 &&
              sites.followingUpdateRva == 0x77C9F0 && sites.perspectiveFnRva == perspectiveRva,
            "Halo 2: records at +0x15EB210, stride 0x150, update +0x20, transition +0x14, first-person update +0x77DEB0");
        Check(DecodeHalo2DirectorSites(nullptr, 0, kHalo2Store, storeRva, sites) ==
                  Halo2DirectorBind::PerspectiveMissing &&
              DecodeHalo2DirectorSites(kHalo2Perspective, perspectiveRva, nullptr, 0, sites) ==
                  Halo2DirectorBind::StoreSiteMissing,
            "a missing site binds nothing, and says which");
        const auto mutated = [&](bool inPerspective, size_t at, uint8_t flip, Halo2DirectorBind expected,
                                 const char* what) {
            uint8_t perspective[sizeof kHalo2Perspective], store[sizeof kHalo2Store];
            std::memcpy(perspective, kHalo2Perspective, sizeof perspective);
            std::memcpy(store, kHalo2Store, sizeof store);
            (inPerspective ? perspective : store)[at] ^= flip;
            Halo2DirectorSites out{};
            Check(DecodeHalo2DirectorSites(perspective, perspectiveRva, store, storeRva, out) == expected &&
                  out.globalsRva == 0, what);
        };
        mutated(false, kH2StoreGlobalsDisp, 0x10, Halo2DirectorBind::GlobalsMismatch,
            "the writer addresses another record than the reader: nothing bound");
        mutated(false, kH2StoreStride, 0x08, Halo2DirectorBind::FieldMismatch, "the strides disagree: nothing bound");
        mutated(false, kH2StoreUpdate, 0x08, Halo2DirectorBind::FieldMismatch,
            "the writer stores the update function in another field than the reader compares: nothing bound");
        mutated(false, kH2StoreFirstPersonDisp, 0x10, Halo2DirectorBind::UpdateMismatch,
            "the two sites name different first-person update functions: nothing bound");
        mutated(true, kH2PerspTransition, 0x01, Halo2DirectorBind::Implausible,
            "a misaligned transition field: refused as implausible");
        {
            // The reader's "following" function made the same as its first-person one: no way to tell them apart.
            uint8_t perspective[sizeof kHalo2Perspective];
            std::memcpy(perspective, kHalo2Perspective, sizeof perspective);
            const int32_t disp = int32_t(0x77DEB0 - (perspectiveRva + int64_t(kH2PerspFollowingNext)));
            std::memcpy(perspective + kH2PerspFollowingDisp, &disp, 4);
            Halo2DirectorSites out{};
            Check(DecodeHalo2DirectorSites(perspective, perspectiveRva, kHalo2Store, storeRva, out) ==
                      Halo2DirectorBind::UpdateMismatch,
                "a first-person update that is also the following one: refused");
        }
        // The rule director_get_perspective itself applies for 0.
        const uint64_t firstPerson = 0x7FF00077DEB0ull, following = 0x7FF00077C9F0ull;
        Check(Halo2PerspectiveSignal(true, true, firstPerson, firstPerson, 0.0f) == Signal::Yes,
            "Halo 2: the first-person update with no transition is first person");
        Check(Halo2PerspectiveSignal(true, true, firstPerson, firstPerson, 0.25f) == Signal::No,
            "Halo 2: the first-person update while it blends in is not yet first person (the game answers 3)");
        Check(Halo2PerspectiveSignal(true, true, following, firstPerson, 0.0f) == Signal::No,
            "Halo 2: the following camera (a seat, a third-person view) is not first person");
        Check(Halo2PerspectiveSignal(true, true, 0x7FF0007186D0ull, firstPerson, 0.0f) == Signal::No,
            "Halo 2: a scripted (cinematic) or dead camera is not first person");
        Check(Halo2PerspectiveSignal(true, true, 0, firstPerson, 0.0f) == Signal::Unread,
            "Halo 2: a record with no camera yet is no answer");
        Check(Halo2PerspectiveSignal(true, false, firstPerson, firstPerson, 0.0f) == Signal::Unread &&
              Halo2PerspectiveSignal(false, false, firstPerson, firstPerson, 0.0f) == Signal::Unavailable,
            "Halo 2: unread and unbound are told apart");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(Halo2PerspectiveSignal(true, true, firstPerson, firstPerson, nan) == Signal::No,
            "Halo 2: a transition that is not a number is not zero (the game's own unordered compare)");
    }

    // ---- The record as the admission reads it: Reach's and Halo 4's modes (HREK's class names) -------------------
    {
        using fp_view::DirectorCameraSignal;
        Check(kModeFirstPerson == 3 && kPerspectiveFirstPerson == 0, "c_first_person_camera: type 3, perspective 0");
        Check(DirectorCameraSignal(true, true, kModeFirstPerson, kPerspectiveFirstPerson) == Signal::Yes,
            "the first-person camera from the first-person perspective: first person");
        Check(DirectorCameraSignal(true, true, kModeFollowing, 1) == Signal::No,
            "the following camera (Armor Lock, Hardlight Shield, a third-person seat): not first person");
        Check(DirectorCameraSignal(true, true, kModeSandbox, kPerspectiveFirstPerson) == Signal::No,
            "c_sandbox_camera (the Monitor holding an object) reports perspective 0 and is still not first person");
        Check(DirectorCameraSignal(true, true, 4, 1) == Signal::No && DirectorCameraSignal(true, true, 8, 1) == Signal::No,
            "the dead and respawning cameras: not first person");
        Check(DirectorCameraSignal(true, true, 6, 0) == Signal::No && DirectorCameraSignal(true, true, 7, 0) == Signal::No,
            "a scripted or authored (cinematic) camera: not first person whatever perspective it reports");
        Check(DirectorCameraSignal(true, true, kModeFirstPerson, 1) == Signal::No,
            "the first-person camera still blending in (get_perspective answers 1): not yet");
        Check(DirectorCameraSignal(true, true, kModeFirstPerson, 3) == Signal::No,
            "the game not in progress (get_perspective answers 3): not first person");
        Check(DirectorCameraSignal(true, true, 12, 1) == Signal::No, "the space-fighter camera: not first person");
    }

    // ---- What the new rung does to the admission (Reach / Halo 4 / Halo 2) --------------------------------------
    {
        using namespace roomscale_admission;
        // On foot, in control, the camera rung bound, the Monitor answered.
        const auto bound = [] {
            TitleEvidence e{};
            e.cinematic = Signal::No; e.unit = Signal::Yes; e.seated = Signal::No;
            e.camera = Signal::Yes; e.monitor = Signal::No; e.viewModel = Signal::Yes;
            return e;
        };
        TitleEvidence e = bound();
        Check(ClassifyTitle(e) == Refusal::None, "armed, first-person camera: admitted");
        e.viewModel = Signal::No;
        Check(ClassifyTitle(e) == Refusal::None,
            "UNARMED on foot with the game's first-person camera (the gap this closes): admitted without a view model");
        // Reach's own evidence has no unit signal unless the sampler said "no unit".
        e.unit = Signal::Unavailable;
        Check(ClassifyTitle(e) == Refusal::None, "Reach unarmed (seat sampler on foot, no unit signal): admitted");
        // A native zoom: the view model stops, the camera stays first person.
        Check(ClassifyTitle(e) == Refusal::None, "zoomed: admitted, the view model is not asked");
        // Armor Lock / Hardlight Shield / Thruster Pack / an assassination: the following camera.
        e = bound(); e.camera = Signal::No;
        Check(ClassifyTitle(e) == Refusal::ThirdPersonCamera,
            "a third-person armor ability: refused by the camera even with a view model inside its window");
        e.viewModel = Signal::No;
        Check(ClassifyTitle(e) == Refusal::ThirdPersonCamera, "and with none");
        // The Monitor: idle (first-person camera) and holding an object (the sandbox camera).
        e = bound(); e.viewModel = Signal::No; e.monitor = Signal::Yes;
        Check(ClassifyTitle(e) == Refusal::Monitor, "an idle Forge Monitor (mode 3, no view model): refused by the game's word");
        e.camera = Signal::No;
        Check(ClassifyTitle(e) == Refusal::Monitor, "the Monitor holding an object (the sandbox camera): refused");
        // The Monitor check did not bind, faulted, or was not read this sample: only the view model can vouch.
        e = bound(); e.viewModel = Signal::No; e.monitor = Signal::Unavailable;
        Check(ClassifyTitle(e) == Refusal::MonitorUnknown,
            "first-person camera, no view model, Monitor check unbound: refused as before the change");
        e.monitor = Signal::Unread;
        Check(ClassifyTitle(e) == Refusal::MonitorUnknown, "... or unread");
        e.viewModel = Signal::Yes;
        Check(ClassifyTitle(e) == Refusal::None, "an armed player does not need the Monitor answer");
        // Death, a seat, a cinematic: named before the camera, as ever.
        e = bound(); e.unit = Signal::No; e.camera = Signal::No;
        Check(ClassifyTitle(e) == Refusal::NoUnit, "death: no unit");
        e = bound(); e.seated = Signal::Yes;
        Check(ClassifyTitle(e) == Refusal::Seated, "a seat, even a first-person one: seated");
        e = bound(); e.cinematic = Signal::Yes; e.camera = Signal::No;
        Check(ClassifyTitle(e) == Refusal::Cinematic, "a cinematic");
        // The rung unbound on this build (or condemned): exactly the behaviour before.
        e = bound(); e.camera = Signal::Unavailable; e.monitor = Signal::Unavailable;
        Check(ClassifyTitle(e) == Refusal::None, "no camera rung, view model drawn: admitted as before");
        e.viewModel = Signal::No;
        Check(ClassifyTitle(e) == Refusal::NoFirstPersonModel, "no camera rung, unarmed: refused as before");
        // A stale publication (the camera thread has not sampled inside the window).
        e = bound(); e.camera = Signal::Unread; e.monitor = Signal::Unread;
        Check(ClassifyTitle(e) == Refusal::None, "camera unread for a frame, view model drawn: admitted");
        e.viewModel = Signal::No;
        Check(ClassifyTitle(e) == Refusal::NoFirstPersonModel, "camera unread and unarmed: refused, never guessed");
        // Halo 2: no Forge, so the camera alone decides.
        e = bound(); e.viewModel = Signal::No;
        Check(ClassifyTitle(e) == Refusal::None, "Halo 2 unarmed, first-person update with no transition: admitted");

        // The dev line names each rung for the title it was read in.
        uint64_t counts[size_t(Refusal::Count)]{};
        counts[size_t(Refusal::ThirdPersonCamera)] = 90;
        char line[512];
        const RungSources reach = TitleRungSources(GameTitle::HaloReach);
        FormatRefusals(line, sizeof line, counts, Refusal::ThirdPersonCamera,
            PackRungs(Signal::No, Signal::No, Signal::No), &reach);
        Check(std::strcmp(line, " camera not first person=90; at the last one (camera not first person):"
                  " game camera first person [director mode 3 / perspective 0]=no,"
                  " Forge Monitor [sandbox-editor predicate]=no,"
                  " first-person view model [first-person interpolation twice in 150 ms]=no") == 0,
            "the dev line names Reach's three rungs by what each reads");
        const RungSources halo2 = TitleRungSources(GameTitle::Halo2);
        const RungSources halo4 = TitleRungSources(GameTitle::Halo4);
        Check(halo2.camera && std::strstr(halo2.camera, "first-person update") && std::strstr(halo2.monitor, "no Forge") &&
              std::strstr(halo2.viewModel, "palette") && std::strstr(halo4.viewModel, "skinning") &&
              std::strstr(halo4.monitor, "sandbox-editor"),
            "Halo 2's and Halo 4's rungs are named for what they read");
        bool allNamed = true;
        for (GameTitle title : {GameTitle::HaloCE, GameTitle::Halo2, GameTitle::Halo3, GameTitle::Halo3ODST,
                                GameTitle::HaloReach, GameTitle::Halo4})
        {
            const RungSources s = TitleRungSources(title);
            allNamed = allNamed && s.camera && s.monitor && s.viewModel;
        }
        Check(allNamed && !TitleRungSources(GameTitle::None).camera, "every title names its rungs; no title, none");
        // Every count at its largest with the longest sources still fits the report's line and is terminated.
        for (auto& count : counts) count = UINT64_MAX;
        char big[1024];
        const int all = FormatRefusals(big, sizeof big, counts, Refusal::MonitorUnknown, 0xFF, &reach);
        Check(all > 0 && size_t(all) < sizeof big - 80 && big[all] == 0, "the named line fits the report's buffer");
        bool bounded = true;
        for (size_t size = 1; size <= 160; ++size)
        {
            char small[161];
            std::memset(small, 0x7F, sizeof small);
            const int wrote = FormatRefusals(small, size, counts, Refusal::Monitor, 0x2A, &reach);
            bounded = bounded && wrote >= 0 && size_t(wrote) < size && small[wrote] == 0 && small[size] == 0x7F;
        }
        Check(bounded, "the named line is truncated, never overrun, whatever the buffer");
    }

    // ---- Value agreement: the rung against the view model it outranks ---------------------------------------
    {
        using Step = RungTrust::Step;
        RungTrust trust;
        // An unarmed start says nothing either way: the rung is used unverified.
        for (int i = 0; i < 5000; ++i)
            Check(trust.Observe(Signal::Yes, Signal::No) == Step::None, "unarmed frames are no evidence");
        Check(!trust.Proven() && !trust.condemned && trust.agreed == 0, "unverified is not known-bad");
        // A weapon is picked up: the view model draws and the camera says first person.
        Step last = Step::None;
        for (uint32_t i = 0; i < RungTrust::kProven; ++i)
            last = trust.Observe(Signal::Yes, Signal::Yes);
        Check(last == Step::Proven && trust.Proven(), "agreeing kProven times proves the rung, said once");
        Check(trust.Observe(Signal::Yes, Signal::Yes) == Step::None, "... once");
        // Proven, nothing condemns it: a later disagreement (a blend, a stray witness hit) is the game's business.
        for (uint32_t i = 0; i < RungTrust::kCondemn * 3; ++i)
            last = trust.Observe(Signal::No, Signal::Yes);
        Check(last == Step::None && !trust.condemned, "a proven rung is never condemned");

        // A rung that reads the wrong field or meaning: the view model draws, the camera never says first person.
        trust.Reset();
        for (uint32_t i = 0; i + 1 < RungTrust::kCondemn; ++i)
            last = trust.Observe(Signal::No, Signal::Yes);
        Check(last == Step::None && !trust.condemned, "one frame short of the run: still used");
        Check(trust.Observe(Signal::No, Signal::Yes) == Step::Condemned && trust.condemned,
            "kCondemn disagreements in a row with no agreement ever: condemned, said once");
        Check(trust.Observe(Signal::Yes, Signal::Yes) == Step::None && trust.condemned && !trust.Proven(),
            "condemned for the generation: nothing re-arms it");
        trust.Reset();
        Check(!trust.condemned && trust.agreed == 0 && trust.disagreedRun == 0, "a new generation starts over");

        // An agreement inside the run restarts it; unread / unbound camera frames neither count nor reset.
        for (uint32_t i = 0; i + 1 < RungTrust::kCondemn; ++i)
            trust.Observe(Signal::No, Signal::Yes);
        trust.Observe(Signal::Yes, Signal::Yes);
        for (uint32_t i = 0; i + 1 < RungTrust::kCondemn; ++i)
            last = trust.Observe(Signal::No, Signal::Yes);
        Check(!trust.condemned && last == Step::None, "one agreement inside the run restarts the count");
        trust.Reset();
        for (uint32_t i = 0; i + 1 < RungTrust::kCondemn; ++i)
            trust.Observe(Signal::No, Signal::Yes);
        for (int i = 0; i < 100; ++i)
        {
            trust.Observe(Signal::Unread, Signal::Yes);
            trust.Observe(Signal::Unavailable, Signal::Yes);
            trust.Observe(Signal::No, Signal::No);
            trust.Observe(Signal::No, Signal::Unread);
        }
        Check(!trust.condemned, "an unread camera, or a view model that is not drawn, is no disagreement");
        Check(trust.Observe(Signal::No, Signal::Yes) == Step::Condemned, "and does not forgive one either");
    }
    return failures;
}
