#pragma once
// WHERE THE CENTRED CROSSHAIR'S FILLS ARE (centred_crosshair_logic.h has what each one is), found as the direct drives
// find their code: by the address cascade of drive_resolve.h, drive_known.h and drive_cascade.h. Each title's table
// is resolved like a drive's -- verified at the addresses it is known to stand at, or found by search, saved for the
// next launch of a build the tables do not describe -- and the offline check runs the same tables on the game's own
// DLL files (tools/drive_address_check.cpp), so an MCC update shows at once what still resolves.
//
// TWO ROUTES TO EVERY FILL, which share no bytes but the fill's own:
//   - the WHOLE signature: the fill with the store or the call around it, anywhere in the module's code (the store's
//     displacement is the profile field, so a layout change stops it matching);
//   - its FUNCTION, by the function's own first bytes (.pdata must say a function starts there), and the fill's CORE
//     inside that function: at its recorded offset, else the one match in the function.
// Either names the instruction. Found both ways, the two must name the same one; found neither way, or not
// recognised as the stock or patched instruction, the title is not patched (MCC's setting applies). A route that is
// lost is a line NOT FOUND in the title's line, while the other still carries it.
//
// The profiles the game filled before the patch are found from the fill's caller (`lea rax, [rip + array]; imul
// reg, user, stride`): optional, and checked by the stride, which must hold the profile and the field.
//
// Every RVA below is a measurement of the retail Steam build of MCC 2025.08.16.178512.1, the Store build being the
// same build (title_build_identity.h), and none is trusted to say where anything is.
//
// ADDR-HYGIENE: resolved (Resolve). Every RVA in this file is the recorded half of a resolution.
//
// Zero engine dependencies.

#include <cstddef>
#include <cstdint>

#include "centred_crosshair_logic.h"
#include "drive_resolve.h"
#include "drive_sites.h"
#include "runtime_types.h"

namespace centred_crosshair_sites
{
using centred_crosshair::Patch;
using drive_resolve::Resolver;
using drive_sites::Anchor;

// One fill. `wholePatch` and `corePatch`: the instruction, from each signature's start. `setterCall`: from the
// instruction, the E8 that hands the value to the setter (Halo 4's changed setting); 0: none.
struct Fill
{
    Anchor whole;
    uint32_t wholePatch;
    Anchor function;
    Anchor core;
    uint32_t corePatch;
    Patch patch;
    const char* instruction;   // the name of the line that says where the instruction is
    uint32_t setterCall;
};

// The fill's caller: `lea rax, [rip + array]` at +3 (seven bytes, its disp32 at +3), `imul reg, user, stride` with
// its imm32 at `strideOffset`.
struct Profiles
{
    Anchor caller;   // pattern null: the title has no profile copy of its own (CE, Halo 2)
    uint32_t strideOffset;
    uint32_t arrayRva;   // where the array sits in the recorded build
    uint32_t stride;     // and its stride there
};
inline constexpr uint32_t kArrayLeaOffset = 3, kArrayLeaDisplacement = 3, kArrayLeaLength = 7;

struct TitleSites
{
    GameTitle title;
    const char* name;      // the title, as the crosshair's own log lines name it
    const char* logName;   // what the resolution's line begins with
    Fill fills[2];
    uint32_t fillCount;
    Anchor setter;         // pattern null: no fill calls a setter
    Profiles profiles;
    uint32_t field;        // the setting's offset in the game's profile; 0 where there is none
};

inline constexpr drive_sites::Where kEntry = drive_sites::kEntry;
inline constexpr drive_sites::Where kAnywhere = drive_sites::kAnywhere;
// A core row: in the function of the row before it (Resolve hands that row's address over itself).
constexpr drive_sites::Where In(uint32_t functionRva) { return drive_sites::Within(0, functionRva); }
inline constexpr Anchor kNone{nullptr, 0, drive_sites::kAnywhere, nullptr};
inline constexpr Profiles kNoProfiles{kNone, 0, 0, 0};

// halo3.dll: the compare, a store between, then the jump.
inline constexpr TitleSites kHalo3{
    GameTitle::Halo3, "Halo 3", "Halo 3 CROSSHAIR",
    {{{"settings fill", 0x2DA29F, kAnywhere,
       "41 80 7E 1C 01 F3 0F 11 8D ?? ?? 00 00 ?? 05 45 33 DB EB 06 41 BB 01 00 00 00 33 D2 48 8B CB E8 ?? ?? ?? ?? "
       "80 BD ?? ?? 00 00 00 41 B7 01 44 89 5B 68"},
      13,
      {"settings fill's function", 0x2D98CC, kEntry,
       "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 48 F8 FF FF"},
      {"settings fill's core", 0x2DA29F, In(0x2D98CC),
       "41 80 7E 1C 01 F3 0F 11 8D ?? ?? 00 00 ?? ?? 45 33 DB EB ?? 41 BB 01 00 00 00"},
      13, Patch::Jump, "settings fill's jump", 0},
     {}},
    1, kNone, {{"profiles' caller", 0x2D84F6, kAnywhere,
                "4C 63 CA 48 8D 05 ?? ?? ?? ?? 49 69 F9 ?? ?? 00 00 48 8B D9 48 03 F8 48 8B CF"},
               13, 0x2D3ED70, 0xD78},
    0x68};

inline constexpr TitleSites kOdst{
    GameTitle::Halo3ODST, "ODST", "ODST CROSSHAIR",
    {{{"settings fill", 0x305FF9, kAnywhere,
       "80 7E 1C 01 ?? 05 45 33 DB EB 06 41 BB 01 00 00 00 33 D2 48 8B CB E8 ?? ?? ?? ?? 80 BD ?? ?? 00 00 00 41 B7 01 "
       "44 89 5B 5C"},
      4,
      {"settings fill's function", 0x305580, kEntry,
       "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 F7 FF FF"},
      {"settings fill's core", 0x305FF9, In(0x305580), "80 7E 1C 01 ?? ?? 45 33 DB EB ?? 41 BB 01 00 00 00"}, 4,
      Patch::Jump, "settings fill's jump", 0},
     {}},
    1, kNone, {{"profiles' caller", 0x303FC6, kAnywhere,
                "4C 63 CA 48 8D 05 ?? ?? ?? ?? 49 69 F9 ?? ?? 00 00 48 8B D9 48 03 F8 48 8B CF"},
               13, 0x2D81860, 0xEB0},
    0x5C};

// haloreach.dll: the full fill, and the fill of a changed setting.
inline constexpr TitleSites kReach{
    GameTitle::HaloReach, "Reach", "Reach CROSSHAIR",
    {{{"full fill", 0x2C9664, kAnywhere,
       "80 7E 1C 01 ?? 0E E8 ?? ?? ?? ?? 83 A7 80 00 00 00 00 EB 0F E8 ?? ?? ?? ?? C7 87 80 00 00 00 01 00 00 00"},
      4,
      {"full fill's function", 0x2C8DFC, kEntry,
       "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 28 F5 FF FF 48 81 EC B0 0B 00 00"},
      {"full fill's core", 0x2C9664, In(0x2C8DFC), "80 7E 1C 01 ?? ?? E8 ?? ?? ?? ?? 83 A7 80 00 00 00 00"}, 4,
      Patch::Jump, "full fill's jump", 0},
     {{"changed-setting fill", 0x2EE03F, kAnywhere,
       "44 89 67 48 ?? ?? ?? ?? ?? 83 FB 02 73 17 3B 9F 80 00 00 00 48 8B CF 0F 95 C2 E8 ?? ?? ?? ?? 89 9F 80 00 00 00"},
      4,
      {"changed-setting fill's function", 0x2EDA08, kEntry,
       "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 F8 "
       "4C 8B 2D ?? ?? ?? ??"},
      {"changed-setting fill's core", 0x2EE03F, In(0x2EDA08),
       "44 89 67 48 ?? ?? ?? ?? ?? 83 FB 02 73 ?? 3B 9F 80 00 00 00"},
      4, Patch::Load, "changed-setting fill's load", 0}},
    2, kNone, {{"profiles' caller", 0x2C7382, kAnywhere,
                "4C 63 CA 48 8D 05 ?? ?? ?? ?? 48 8B D9 49 69 F1 ?? ?? 00 00 48 03 F0 48 8B CE"},
               16, 0x2A03C50, 0xAB8},
    0x80};

// halo4.dll: the full fill, and the fill of a changed setting, which hands the value to the setter.
inline constexpr TitleSites kHalo4{
    GameTitle::Halo4, "Halo 4", "Halo 4 CROSSHAIR",
    {{{"full fill", 0x3CC3EF, kAnywhere,
       "80 7E 1C 01 ?? 04 33 DB EB 05 BB 01 00 00 00 48 8B 7C 24 30 33 D2 48 8B CF E8 ?? ?? ?? ?? 80 7C 24 21 00 89 9F "
       "90 00 00 00"},
      4,
      {"full fill's function", 0x3CB984, kEntry,
       "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 68 F6 FF FF 48 81 EC 70 0A 00 "
       "00"},
      {"full fill's core", 0x3CC3EF, In(0x3CB984), "80 7E 1C 01 ?? ?? 33 DB EB ?? BB 01 00 00 00 48 8B 7C 24 30"}, 4,
      Patch::Jump, "full fill's jump", 0},
     {{"changed-setting fill", 0x3FB146, kAnywhere,
       "45 8A C5 ?? ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 8A C5 48 8B CF 84 C0 0F 84"},
      3,
      {"changed-setting fill's function", 0x3FAC68, kEntry,
       "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 "
       "89 45 F8 4C 8B 3D ?? ?? ?? ??"},
      {"changed-setting fill's core", 0x3FB146, In(0x3FAC68), "45 8A C5 ?? ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ??"}, 3,
      Patch::Load, "changed-setting fill's load", 8}},
    2,
    // The setter: it stores the profile's +0x90.
    {"setter", 0x3C9C30, kEntry,
     "83 FA 02 7D ?? 48 89 5C 24 08 57 48 83 EC 20 8B FA 48 8B D9 33 D2 45 84 C0 74 ?? 3B B9 90 00 00 00 74 02 B2 01 "
     "E8 ?? ?? ?? ?? 89 BB 90 00 00 00"},
    {{"profiles' caller", 0x3C91E3, kAnywhere,
      "4C 63 E2 48 8D 05 ?? ?? ?? ?? 48 8B F9 49 69 F4 ?? ?? 00 00 48 03 F0 48 8B CE"},
     16, 0x30EBED0, 0xCC0},
    0x90};

// halo1.dll and halo2.dll: the check of whether the crosshair is centred right now.
inline constexpr TitleSites kHaloCE{
    GameTitle::HaloCE, "Halo CE", "Halo CE CROSSHAIR",
    {{{"centred check", 0xBF8B7, kAnywhere,
       "0F BF C9 E8 ?? ?? ?? ?? 48 63 D0 48 8D 0D ?? ?? ?? ?? 48 69 C2 CC 0A 00 00 80 3C 08 01 ?? ?? ?? 00 00 00 48 0F "
       "BF C3 48 6B C8 58"},
      29,
      {"centred check's function", 0xBF8A0, kEntry, "40 53 48 83 EC 20 48 83 3D ?? ?? ?? ?? 00 0F B7 D9"},
      {"centred check's core", 0xBF8C9, In(0xBF8A0), "48 69 C2 CC 0A 00 00 80 3C 08 01 ?? ?? ?? 00 00 00 48 0F BF C3"},
      11, Patch::NearJump, "centred check's jump", 0},
     {}},
    1, kNone, kNoProfiles, 0};

inline constexpr TitleSites kHalo2{
    GameTitle::Halo2, "Halo 2", "Halo 2 CROSSHAIR",
    {{{"centred check", 0x3C1E2, kAnywhere,
       "83 F9 03 77 ?? 48 63 C1 48 69 D0 CC 0A 00 00 48 8D 05 ?? ?? ?? ?? 80 3C 02 01 ?? ?? E8 ?? ?? ?? ?? 84 C0 0F B6 "
       "CB BA 01 00 00 00 0F 44 CA"},
      26,
      {"centred check's function", 0x3C1D0, kEntry, "40 53 48 83 EC 20 32 DB 48 83 3D ?? ?? ?? ?? 00"},
      {"centred check's core", 0x3C1F1, In(0x3C1D0),
       "48 8D 05 ?? ?? ?? ?? 80 3C 02 01 ?? ?? E8 ?? ?? ?? ?? 84 C0 0F B6 CB"},
      11, Patch::Fallthrough, "centred check's jump", 0},
     {}},
    1, kNone, kNoProfiles, 0};

inline constexpr const TitleSites* kTitles[]{&kHalo3, &kOdst, &kReach, &kHalo4, &kHaloCE, &kHalo2};

inline const TitleSites* For(GameTitle title)
{
    for (const TitleSites* t : kTitles)
        if (t->title == title)
            return t;
    return nullptr;
}

// A table is well formed when both routes name the same instruction in the recorded build, every core lies in its
// function, and a core is long enough to be searched for there (drive_sites::kMinSearchedBytes).
constexpr bool WellFormed(const TitleSites& t)
{
    if (t.fillCount < 1 || t.fillCount > 2)
        return false;
    for (uint32_t i = 0; i < t.fillCount; ++i)
    {
        const Fill& f = t.fills[i];
        if (!f.whole.pattern || !f.function.pattern || !f.core.pattern || !f.instruction)
            return false;
        if (f.whole.where.find != drive_sites::Find::Anywhere || f.function.where.find != drive_sites::Find::Entry ||
            f.core.where.find != drive_sites::Find::Within || f.core.where.functionRva != f.function.rva)
            return false;
        if (f.whole.rva + f.wholePatch != f.core.rva + f.corePatch || f.core.rva < f.function.rva)
            return false;
        if (centred_crosshair::PatternBytes(f.core.pattern) < drive_sites::kMinSearchedBytes ||
            f.wholePatch >= centred_crosshair::PatternBytes(f.whole.pattern) ||
            f.corePatch >= centred_crosshair::PatternBytes(f.core.pattern))
            return false;
        if (f.setterCall && !t.setter.pattern)
            return false;
    }
    return (t.field != 0) == (t.profiles.caller.pattern != nullptr);
}
static_assert(WellFormed(kHalo3) && WellFormed(kOdst) && WellFormed(kReach) && WellFormed(kHalo4) &&
              WellFormed(kHaloCE) && WellFormed(kHalo2));

// What a title's resolution found. `at[i]`: fill i's instruction (0: not found or not recognised). `fills`: every
// fill was. `array`, `stride`: the profiles already filled (0: not found; the fills are patched all the same).
struct Resolved
{
    uint32_t at[2]{};
    bool fills = false;
    uint32_t array = 0;
    uint32_t stride = 0;
};

// The instruction at `rva` as the game has it now: stock or already patched, and nothing else.
inline bool Recognised(const Resolver& r, uint32_t rva, Patch patch)
{
    uint8_t bytes[centred_crosshair::kMostPatchBytes]{};
    uint8_t centred[centred_crosshair::kMostPatchBytes]{};
    size_t length = 0;
    bool patched = false;
    return rva && drive_resolve::ReadOriginal(r.image(), rva, bytes, sizeof(bytes)) &&
           centred_crosshair::CentredBytes(patch, bytes, centred, length, patched);
}

inline Resolved Resolve(Resolver& r, const TitleSites& t)
{
    Resolved out;
    uint32_t setter = 0;
    if (t.setter.pattern)
        setter = drive_sites::ResolveRow(r, t.setter, 0);
    bool all = true;
    for (uint32_t i = 0; i < t.fillCount; ++i)
    {
        const Fill& f = t.fills[i];
        const uint32_t whole = drive_sites::ResolveRow(r, f.whole, 0);
        const uint32_t function = drive_sites::ResolveRow(r, f.function, 0);
        const uint32_t core = drive_sites::ResolveRow(r, f.core, function);
        const uint32_t byWhole = whole ? whole + f.wholePatch : 0, byCore = core ? core + f.corePatch : 0;
        uint32_t at = r.Derived(f.instruction, f.whole.rva + f.wholePatch, byWhole ? byWhole : byCore);
        if (at && byWhole && byCore && byWhole != byCore)
            at = r.Refuse("its whole signature and its core in its function name different instructions");
        else if (at && !Recognised(r, at, f.patch))
            at = r.Refuse("it is not the instruction the fill is made of, stock or patched");
        if (at && f.setterCall)
        {
            const uint32_t call =
                r.Derived("its setter call", t.setter.rva, r.CallTarget(at + f.setterCall));
            if (!call || call != setter)
                at = r.Refuse(setter ? "it does not call the setter" : "the setter was not found");
        }
        out.at[i] = at;
        all = all && at;
    }
    out.fills = all;
    if (t.profiles.caller.pattern)
    {
        const Profiles& p = t.profiles;
        const uint32_t caller = drive_sites::ResolveRow(r, p.caller, 0);
        const uint32_t array = caller ? r.RipTarget(caller + kArrayLeaOffset, kArrayLeaDisplacement, kArrayLeaLength) : 0;
        out.array = r.Derived("profiles", p.arrayRva,
                              array && drive_resolve::InImage(r.image(), array, sizeof(int32_t)) ? array : 0);
        uint8_t imm[4]{};
        uint32_t stride = 0;
        if (caller && drive_resolve::ReadOriginal(r.image(), caller + p.strideOffset, imm, sizeof(imm)))
            stride = static_cast<uint32_t>(imm[0]) | static_cast<uint32_t>(imm[1]) << 8 |
                     static_cast<uint32_t>(imm[2]) << 16 | static_cast<uint32_t>(imm[3]) << 24;
        const bool holds = stride >= centred_crosshair::kProfileInElement + t.field + sizeof(int32_t) && stride < 0x10000;
        const uint64_t last = static_cast<uint64_t>(out.array) +
                              static_cast<uint64_t>(centred_crosshair::kUsers - 1) * stride +
                              centred_crosshair::kProfileInElement + t.field + sizeof(int32_t);
        out.stride = r.Proven("profiles' stride", p.stride, caller && holds ? stride : 0,
                              "it cannot hold the profile and its field");
        if (out.array && out.stride && last > r.image().size)
            out.stride = r.Refuse("the four profiles do not fit in the module");
        if (!out.stride)
            out.array = 0;
    }
    return out;
}
} // namespace centred_crosshair_sites
