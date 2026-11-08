#pragma once
// SEAT FRAME READERS -- what each title's vehicle-up rebase reads, read the same way on the simulation thread, so a
// seated direct-drive write knows, in the same tick, whether the game will rotate the record and by which R(u)
// (src/common/seat_frame_logic.h has the math; docs/ALL-TITLES-AIM-2026-09-29.md, "Seats aim in world angles").
//
// Per title, the rebase's own GATE block (its TLS-index load through the seat-flags bit-4 test, excluding the jne
// our Halo 3 / ODST / Reach skip may own) and its UP GETTER (root and attached paths) are exact-byte patterns,
// unique in the module and matched at their pinned RVAs before any read; only rip displacements and call rel32s
// are wildcards. The layout below is LITERAL in those bytes: a match proves it. The rip-relative globals the
// reader dereferences (the engine TLS index, the tag tables) are decoded from the matched instructions at install,
// never assumed. tools/verify_seat_frame_conversion.py reads this table, re-proves every pattern against the
// retail modules, cross-checks each layout field against the disassembled instruction that uses it, and EXECUTES
// each title's rebase (and its getter) over a synthetic world built from these fields, comparing the result with a
// twin of SeatFrameRead + seat_frame::BuildFrame / Apply.
//
// ADDR-HYGIENE: guarded. Measured on the retail Steam modules (the Store modules are byte-identical,
// MCC-EDITIONS-EVIDENCE). Install refuses (and every seated write of that title stands down) unless both patterns
// are unique at their RVAs, the rebase's call to the getter lands on it, and every decoded global lies inside the
// module. At runtime every read is salt-checked or bounded and SEH-guarded, and the forward / up the getter logic
// produces must pass seat_frame::FrameReadAgrees (unit, perpendicular) before a frame is built from it.
//
// Zero engine dependencies: the memory access is a template parameter (the DLL reads raw memory under SEH; the
// unit tests read a synthetic world).

#include <cstdint>

#include "seat_frame_logic.h"

namespace seat_frame
{
inline constexpr uint32_t kNone = 0xFFFFFFFFu;

enum TagScheme : uint32_t
{
    kTagLinear = 0,      // Halo 3, ODST: def = base + inst[idx].addr * 4; block = base + def[field] * 4
    kTagSegmented = 1,   // Reach, Halo 4: addresses carry a 4-bit segment: seg[a >> 28] + a * 4
    kTagSplit = 2,       // Halo 2: def = base + inst[idx].offset; a negative block address uses a second base
};

struct ReaderSite
{
    const char* name;
    uint32_t rebaseRva;        // the rebase function (.pdata begin)
    uint32_t gateRva;          // gatePattern: the TLS-index load (Halo 2: the objects array load) .. the bit-4 test
    const char* gatePattern;
    uint32_t getterRva;        // the up getter (.pdata begin); getterPattern covers its root and attached paths
    const char* getterPattern;
    uint32_t getterCallRva;    // the rebase's `call getter` (E8 rel32)
    uint32_t jccRva;           // the bit-4 `jne` right after gatePattern (E9 there = our skip, seat_aim_world_frame)
    // Offsets INTO gatePattern of 7-byte rip-relative instructions (disp32 at +3), or kNone.
    uint32_t tlsIndexLoad;     // mov r8d, [rip+] -> the engine TLS index (TLS titles)
    uint32_t objectsLoad;      // mov rax, [rip+] -> the objects data array (Halo 2)
    uint32_t tagInstancesLoad; // mov rax, [rip+] -> the tag instance table (a pointer variable)
    uint32_t tagBaseLoad;      // linear / split: mov r?, [rip+] (a pointer variable); segmented: lea rcx, [rip+]
    uint32_t splitNegativeLoad;   // split: add rbx, [rip+] for a negative block address
    uint32_t splitPositiveLoad;   // split: add rbx, [rip+] for a positive one
    uint32_t objectTls;        // TLS titles: the object table's offset in the TLS block
    uint32_t objectEntries;    // the table's entries pointer (Halo 2: relative to the array header)
    uint32_t unitGate;         // dword the rebase requires != -1 (the unit is seated)
    uint32_t unitSeat;         // int16 seat index (-1: no seat)
    uint32_t objectParent;     // dword parent object handle (the unit's vehicle; the vehicle's own carrier)
    uint32_t scheme;           // TagScheme
    uint32_t definitionIndex;  // uint16 tag index in the parent object
    uint32_t seatBlock;        // the vehicle definition's seats block address field
    uint32_t seatStride;       // bytes per seat (flags dword at +0)
    uint32_t parentTypeMask;   // Halo 4: the parent must be one of these object types (entry byte +4); 0 = any
    uint32_t nodeIndex;        // byte: the attached object's node in its parent
    uint32_t nodeIndexSigned;
    uint32_t nodeTable;        // int16 / uint16: offset of the parent's node matrices (0x34 each) from its data
    uint32_t nodeTableSigned;
    uint32_t localForward;     // the object's forward (root: world; attached: parent-node-relative)
    uint32_t localUp;          // the object's up (likewise)
};

inline constexpr uint32_t kSeatFlagThirdPersonBit = 1u << 4;   // the `third person camera` flag the rebase tests
inline constexpr uint32_t kObjectEntryStride = 0x18;          // TLS titles: salt word +0, type byte +4 (Halo 4)
inline constexpr uint32_t kObjectEntryData = 0x10;            // ...data pointer +0x10
inline constexpr uint32_t kNodeMatrixStride = 0x34;           // scale, forward, left, up, position
inline constexpr uint32_t kNodeForward = 0x04, kNodeLeft = 0x10, kNodeUp = 0x1C;

// ADDR-HYGIENE: guarded (see the header). Measured on retail Steam.
inline constexpr ReaderSite kHalo3Reader{
    .name = "Halo 3",
    .rebaseRva = 0x355E20,
    .gateRva = 0x355E33,
    .gatePattern =
        "44 8B 05 ?? ?? ?? ?? 48 8B DA 0F 29 70 E8 0F 29 78 D8 44 0F 29 40 C8 44 0F 29 48 B8 44 0F 29 50 A8 44 0F 29 "
        "58 98 65 48 8B 04 25 58 00 00 00 0F B7 C9 BA 38 00 00 00 4A 8B 04 C0 4C 8B 04 10 48 8D 04 49 4D 8B 48 48 45 "
        "33 C0 49 8B 4C C1 10 83 C8 FF 39 81 78 01 00 00 0F 84 4C 01 00 00 0F B7 91 4E 02 00 00 66 3B D0 0F 84 3C 01 "
        "00 00 44 8B 59 10 41 0F B7 C3 48 8D 0C 40 49 8B 44 C9 10 4C 8B 0D ?? ?? ?? ?? 0F B7 08 48 8B 05 ?? ?? ?? ?? "
        "44 8B 54 C8 04 41 8B C0 45 85 D2 74 04 4B 8D 04 91 8B 88 5C 02 00 00 85 C9 75 05 49 8B C8 EB 04 49 8D 0C 89 "
        "48 0F BF C2 48 69 D0 D4 00 00 00 8B 04 0A C1 E8 04 A8 01",
    .getterRva = 0x345318,
    .getterPattern =
        "48 83 EC 28 44 8B 0D ?? ?? ?? ?? 4D 8B D0 65 48 8B 04 25 58 00 00 00 4C 8B C2 BA 38 00 00 00 0F B7 C9 4A 8B "
        "04 C8 4C 8B 0C 10 48 8D 04 49 49 8B 51 48 4C 8B 4C C2 10 41 83 79 10 FF 75 35 4D 85 C0 74 13 F2 41 0F 10 41 "
        "5C F2 41 0F 11 00 41 8B 41 64 41 89 40 08 4D 85 D2 74 62 F2 41 0F 10 41 68 F2 41 0F 11 02 41 8B 41 70 41 89 "
        "42 08 48 83 C4 28 C3 41 0F B7 41 10 48 8D 0C 40 49 0F BE 41 14 48 8B 54 CA 10 48 6B C0 34 4C 0F BF 9A 38 01 "
        "00 00 48 03 C2 4C 03 D8",
    .getterCallRva = 0x355F0B,
    .jccRva = 0x355EFA,
    .tlsIndexLoad = 0x00,
    .objectsLoad = kNone,
    .tagInstancesLoad = 0x89,
    .tagBaseLoad = 0x7F,
    .splitNegativeLoad = kNone,
    .splitPositiveLoad = kNone,
    .objectTls = 0x38,
    .objectEntries = 0x48,
    .unitGate = 0x178,
    .unitSeat = 0x24E,
    .objectParent = 0x10,
    .scheme = kTagLinear,
    .definitionIndex = 0x0,
    .seatBlock = 0x25C,
    .seatStride = 0xD4,
    .parentTypeMask = 0,
    .nodeIndex = 0x14,
    .nodeIndexSigned = 1,
    .nodeTable = 0x138,
    .nodeTableSigned = 1,
    .localForward = 0x5C,
    .localUp = 0x68,
};

inline constexpr ReaderSite kOdstReader{
    .name = "ODST",
    .rebaseRva = 0x39A600,
    .gateRva = 0x39A610,
    .gatePattern =
        "44 8B 05 ?? ?? ?? ?? 48 8B DA 65 48 8B 04 25 58 00 00 00 0F B7 C9 BA 20 00 00 00 4A 8B 04 C0 4C 8B 04 10 48 "
        "8D 04 49 4D 8B 50 48 45 33 C0 49 8B 4C C2 10 83 C8 FF 39 81 70 01 00 00 0F 84 76 01 00 00 0F B7 91 62 02 00 "
        "00 66 3B D0 0F 84 66 01 00 00 44 8B 49 10 44 3B C8 0F 84 59 01 00 00 41 0F B7 C1 48 8D 0C 40 49 8B 44 CA 10 "
        "4C 8B 15 ?? ?? ?? ?? 0F B7 08 48 8B 05 ?? ?? ?? ?? 44 8B 5C C8 04 41 8B C0 45 85 DB 74 04 4B 8D 04 9A 8B 88 "
        "7C 02 00 00 85 C9 75 05 49 8B C8 EB 04 49 8D 0C 8A 48 0F BF C2 48 69 D0 D4 00 00 00 8B 04 0A C1 E8 04 A8 01",
    .getterRva = 0x380AF0,
    .getterPattern =
        "48 83 EC 28 44 8B 0D ?? ?? ?? ?? 4D 8B D0 65 48 8B 04 25 58 00 00 00 4C 8B C2 BA 20 00 00 00 0F B7 C9 4A 8B "
        "04 C8 4C 8B 0C 10 48 8D 04 49 49 8B 51 48 4C 8B 4C C2 10 41 83 79 10 FF 75 35 4D 85 C0 74 13 F2 41 0F 10 41 "
        "5C F2 41 0F 11 00 41 8B 41 64 41 89 40 08 4D 85 D2 74 62 F2 41 0F 10 41 68 F2 41 0F 11 02 41 8B 41 70 41 89 "
        "42 08 48 83 C4 28 C3 41 0F B7 41 10 48 8D 0C 40 49 0F BE 41 14 48 8B 54 CA 10 48 6B C0 34 4C 0F BF 9A 2C 01 "
        "00 00 48 03 C2 4C 03 D8",
    .getterCallRva = 0x39A6D5,
    .jccRva = 0x39A6C4,
    .tlsIndexLoad = 0x00,
    .objectsLoad = kNone,
    .tagInstancesLoad = 0x76,
    .tagBaseLoad = 0x6C,
    .splitNegativeLoad = kNone,
    .splitPositiveLoad = kNone,
    .objectTls = 0x20,
    .objectEntries = 0x48,
    .unitGate = 0x170,
    .unitSeat = 0x262,
    .objectParent = 0x10,
    .scheme = kTagLinear,
    .definitionIndex = 0x0,
    .seatBlock = 0x27C,
    .seatStride = 0xD4,
    .parentTypeMask = 0,
    .nodeIndex = 0x14,
    .nodeIndexSigned = 1,
    .nodeTable = 0x12C,
    .nodeTableSigned = 1,
    .localForward = 0x5C,
    .localUp = 0x68,
};

inline constexpr ReaderSite kReachReader{
    .name = "Reach",
    .rebaseRva = 0x48ADB8,
    .gateRva = 0x48ADCB,
    .gatePattern =
        "44 8B 05 ?? ?? ?? ?? 48 8B DA 0F 29 70 E8 45 32 C9 0F 29 78 D8 44 0F 29 40 C8 44 0F 29 48 B8 44 0F 29 50 A8 "
        "44 0F 29 58 98 65 48 8B 04 25 58 00 00 00 BA 10 00 00 00 0F B7 C9 4A 8B 04 C0 4C 8B 04 10 48 8D 04 49 49 8B "
        "50 50 48 8B 4C C2 10 83 C8 FF 39 81 CC 01 00 00 0F 84 49 01 00 00 44 0F B7 91 36 03 00 00 66 41 3B C2 0F 84 "
        "37 01 00 00 44 8B 59 14 41 0F B7 C3 48 8D 0C 40 48 8B 44 CA 10 0F B7 08 48 8B 05 ?? ?? ?? ?? 8B 54 C8 04 48 "
        "8D 0D ?? ?? ?? ?? 8B C2 48 C1 E8 1C 48 8B 04 C1 44 8B 84 90 48 04 00 00 49 0F BF C2 48 6B D0 4B 49 03 D0 49 "
        "C1 E8 1C 4A 8B 04 C1 8B 14 90 C1 EA 04 F6 C2 01",
    .getterRva = 0x471F9C,
    .getterPattern =
        "48 83 EC 28 44 8B 0D ?? ?? ?? ?? 4D 8B D0 65 48 8B 04 25 58 00 00 00 4C 8B C2 BA 10 00 00 00 0F B7 C9 4A 8B "
        "04 C8 4C 8B 0C 10 48 8D 04 49 49 8B 51 50 4C 8B 4C C2 10 41 83 79 14 FF 75 35 4D 85 C0 74 13 F2 41 0F 10 41 "
        "50 F2 41 0F 11 00 41 8B 41 58 41 89 40 08 4D 85 D2 74 62 F2 41 0F 10 41 5C F2 41 0F 11 02 41 8B 41 64 41 89 "
        "42 08 48 83 C4 28 C3 41 0F B7 41 14 48 8D 0C 40 41 0F B6 41 18 48 8B 54 CA 10 48 6B C0 34 4C 0F BF 9A 7A 01 "
        "00 00 48 03 C2 4C 03 D8",
    .getterCallRva = 0x48AEA0,
    .jccRva = 0x48AE8F,
    .tlsIndexLoad = 0x00,
    .objectsLoad = kNone,
    .tagInstancesLoad = 0x84,
    .tagBaseLoad = 0x8F,
    .splitNegativeLoad = kNone,
    .splitPositiveLoad = kNone,
    .objectTls = 0x10,
    .objectEntries = 0x50,
    .unitGate = 0x1CC,
    .unitSeat = 0x336,
    .objectParent = 0x14,
    .scheme = kTagSegmented,
    .definitionIndex = 0x0,
    .seatBlock = 0x448,
    .seatStride = 0x12C,
    .parentTypeMask = 0,
    .nodeIndex = 0x18,
    .nodeIndexSigned = 0,
    .nodeTable = 0x17A,
    .nodeTableSigned = 1,
    .localForward = 0x50,
    .localUp = 0x5C,
};

inline constexpr ReaderSite kHalo2Reader{
    .name = "Halo 2",
    .rebaseRva = 0x8F0D20,
    .gateRva = 0x8F0D32,
    .gatePattern =
        "48 8B 05 ?? ?? ?? ?? 33 DB 48 8B F2 4C 8B 40 48 4D 8D 0C 00 4D 85 C0 75 03 44 8B CB 0F B7 C9 48 8D 04 49 49 "
        "8D 0C 81 E8 ?? ?? ?? ?? 48 8B F8 83 B8 40 01 00 00 FF 0F 84 DF 01 00 00 66 83 B8 10 02 00 00 FF 0F 84 D1 01 "
        "00 00 48 8B 05 ?? ?? ?? ?? 48 8B 48 48 48 8D 14 01 48 85 C9 75 03 48 8B D3 0F B7 47 14 48 8D 0C 40 48 8D 0C "
        "8A E8 ?? ?? ?? ?? 0F B7 08 48 8B 05 ?? ?? ?? ?? 48 03 C9 48 63 4C C8 08 48 8B 05 ?? ?? ?? ?? 48 63 94 01 CC "
        "01 00 00 83 FA FF 74 1D 85 D2 79 0F 8B DA 0F BA F3 1F 48 03 1D ?? ?? ?? ?? EB 0A 48 8B DA 48 03 1D ?? ?? ?? "
        "?? 48 0F BF 87 10 02 00 00 48 69 C8 B0 00 00 00 8B 04 19 C1 E8 04 A8 01",
    .getterRva = 0x8D6660,
    .getterPattern =
        "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B F2 33 ED 49 8B F8 48 "
        "8B 50 48 4C 8D 04 02 48 85 D2 75 03 44 8B C5 0F B7 C9 48 8D 04 49 48 8B 0D ?? ?? ?? ?? 41 8B 54 80 08 E8 ?? "
        "?? ?? ?? 48 8B D8 8B 50 14 83 FA FF 75 31 48 85 F6 74 0F F2 0F 10 40 70 F2 0F 11 06 8B 48 78 89 4E 08 48 85 "
        "FF 0F 84 8C 00 00 00 F2 0F 10 40 7C F2 0F 11 07 8B 80 84 00 00 00 89 47 08 EB 78 4C 89 74 24 30 44 0F B6 70 "
        "18 48 8B 05 ?? ?? ?? ?? 48 8B 48 48 48 85 C9 74 04 48 8D 2C 01 0F B7 CA 48 8D 04 49 48 8B 0D ?? ?? ?? ?? 8B "
        "54 85 08 E8 ?? ?? ?? ?? 49 0F BE CE 4C 8B 74 24 30 48 6B D1 34 48 0F BF A8 1A 01 00 00 48 03 C2 48 03 E8",
    .getterCallRva = 0x8F0E2A,
    .jccRva = 0x8F0DFE,
    .tlsIndexLoad = kNone,
    .objectsLoad = 0x00,
    .tagInstancesLoad = 0x75,
    .tagBaseLoad = 0x84,
    .splitNegativeLoad = 0xA2,
    .splitPositiveLoad = 0xAE,
    .objectTls = kNone,
    .objectEntries = 0x48,
    .unitGate = 0x140,
    .unitSeat = 0x210,
    .objectParent = 0x14,
    .scheme = kTagSplit,
    .definitionIndex = 0x0,
    .seatBlock = 0x1CC,
    .seatStride = 0xB0,
    .parentTypeMask = 0,
    .nodeIndex = 0x18,
    .nodeIndexSigned = 1,
    .nodeTable = 0x11A,
    .nodeTableSigned = 1,
    .localForward = 0x70,
    .localUp = 0x7C,
};

inline constexpr ReaderSite kHalo4Reader{
    .name = "Halo 4",
    .rebaseRva = 0x5F97DC,
    .gateRva = 0x5F97FB,
    .gatePattern =
        "44 8B 05 ?? ?? ?? ?? 33 DB 0F 29 70 E8 48 8B F2 0F 29 78 D8 44 0F 29 40 C8 44 0F 29 48 B8 44 0F 29 50 A8 44 "
        "0F 29 58 98 65 48 8B 04 25 58 00 00 00 0F B7 C9 41 B9 18 00 00 00 4A 8B 04 C0 4C 8D 04 49 4D 8B 0C 01 49 8B "
        "41 50 4A 8B 4C C0 10 83 C8 FF 39 81 9C 04 00 00 0F 84 53 01 00 00 0F B7 79 2C 66 3B C7 0F 84 46 01 00 00 44 "
        "8B 71 24 BA 03 20 00 00 41 8B CE E8 ?? ?? ?? ?? 48 85 C0 0F 84 2C 01 00 00 0F B7 48 08 48 8B 05 ?? ?? ?? ?? "
        "8B 54 C8 04 48 8D 0D ?? ?? ?? ?? 8B C2 48 C1 E8 1C 48 8B 04 C1 44 8B 84 90 14 05 00 00 45 8B C8 48 0F BF C7 "
        "48 6B D0 5B 49 C1 E9 1C 49 03 D0 4A 8B 04 C9 44 8B 04 90 41 C1 E8 04 41 F6 C0 01",
    .getterRva = 0x5D84D0,
    .getterPattern =
        "48 83 EC 28 44 8B 0D ?? ?? ?? ?? 4D 8B D0 65 48 8B 04 25 58 00 00 00 4C 8B C2 BA 18 00 00 00 0F B7 C9 4A 8B "
        "04 C8 4C 8B 0C 02 48 8D 04 49 49 8B 51 50 4C 8B 4C C2 10 41 83 79 24 FF 75 38 4D 85 C0 74 13 F2 41 0F 10 41 "
        "70 F2 41 0F 11 00 41 8B 41 78 41 89 40 08 4D 85 D2 74 65 F2 41 0F 10 41 7C F2 41 0F 11 02 41 8B 81 84 00 00 "
        "00 41 89 42 08 48 83 C4 28 C3 41 0F B7 41 24 48 8D 0C 40 41 0F B6 41 39 48 8B 54 CA 10 48 6B C0 34 44 0F B7 "
        "9A BE 01 00 00 48 03 C2 4C 03 D8",
    .getterCallRva = 0x5F98DB,
    .jccRva = 0x5F98CA,
    .tlsIndexLoad = 0x00,
    .objectsLoad = kNone,
    .tagInstancesLoad = 0x89,
    .tagBaseLoad = 0x94,
    .splitNegativeLoad = kNone,
    .splitPositiveLoad = kNone,
    .objectTls = 0x18,
    .objectEntries = 0x50,
    .unitGate = 0x49C,
    .unitSeat = 0x2C,
    .objectParent = 0x24,
    .scheme = kTagSegmented,
    .definitionIndex = 0x8,
    .seatBlock = 0x514,
    .seatStride = 0x16C,
    .parentTypeMask = 0x2003,
    .nodeIndex = 0x39,
    .nodeIndexSigned = 0,
    .nodeTable = 0x1BE,
    .nodeTableSigned = 0,
    .localForward = 0x70,
    .localUp = 0x7C,
};

// The globals a reader dereferences, decoded at install from the matched gate pattern. Addresses (not values):
// the reader loads through them every read, since the tag tables move with every map load.
struct ReaderGlobals
{
    uintptr_t tlsIndex = 0;        // uint32 engine TLS index (TLS titles)
    uintptr_t tagInstances = 0;    // pointer variable -> the tag instance table
    uintptr_t tagBase = 0;         // linear / split: pointer variable -> tag data; segmented: the segment table itself
    uintptr_t splitNegative = 0;   // split: pointer variable
    uintptr_t splitPositive = 0;   // split: pointer variable
};

// What the rebase would read for a unit, this instant.
struct ReadResult
{
    bool readable = false;     // every link the rebase itself follows read (and its value checks passed)
    bool gateSeated = false;   // the rebase's own early exits pass: the unit has a parent and a seat
    bool flagsKnown = false;
    uint32_t flags = 0;        // the occupied seat's LIVE flags word
    bool upKnown = false;      // forward and up read and agreed (FrameReadAgrees)
    float forward[3]{};
    float up[3]{};
    uint32_t parent = kNone;
    int16_t seat = -1;
};

// A vehicle's forward and up, world, by the up getter's logic: its own frame when it is a root object, else
// composed through its carrier's node. False: a link did not read (forward and up are then not to be used).
// Mem is the memory accessor described below.
template <class Mem>
bool VehicleFrameRead(const ReaderSite& site, Mem& mem, uintptr_t vehicle, float forward[3], float up[3])
{
    uint32_t carrier = 0;
    if (!mem.U32(vehicle + site.objectParent, carrier))
        return false;
    float localForward[3]{}, localUp[3]{};
    for (int i = 0; i < 3; ++i)
        if (!mem.F32(vehicle + site.localForward + 4u * i, localForward[i]) ||
            !mem.F32(vehicle + site.localUp + 4u * i, localUp[i]))
            return false;
    if (carrier == kNone)
    {
        for (int i = 0; i < 3; ++i)
        {
            forward[i] = localForward[i];
            up[i] = localUp[i];
        }
        return true;
    }
    const uintptr_t carrierData = mem.Object(carrier, 0);
    uint8_t node = 0;
    uint16_t table = 0;
    if (!carrierData || !mem.U8(vehicle + site.nodeIndex, node) || !mem.U16(carrierData + site.nodeTable, table))
        return false;
    const int64_t nodeIndex = site.nodeIndexSigned ? static_cast<int8_t>(node) : node;
    const int64_t tableOffset = site.nodeTableSigned ? static_cast<int16_t>(table) : table;
    const uintptr_t matrix =
        static_cast<uintptr_t>(carrierData + tableOffset + nodeIndex * static_cast<int64_t>(kNodeMatrixStride));
    float m[3][3]{};   // columns: forward, left, up
    const uint32_t columns[3]{kNodeForward, kNodeLeft, kNodeUp};
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 3; ++i)
            if (!mem.F32(matrix + columns[c] + 4u * i, m[c][i]))
                return false;
    for (int i = 0; i < 3; ++i)
    {
        forward[i] = localForward[0] * m[0][i] + localForward[1] * m[1][i] + localForward[2] * m[2][i];
        up[i] = localUp[0] * m[0][i] + localUp[1] * m[1][i] + localUp[2] * m[2][i];
    }
    return true;
}

// The read itself, over a memory accessor:
//   bool Mem::U8 / U16 / U32 / U64 / F32(uintptr_t address, T& out) -- false on any fault or bound
//   uintptr_t Mem::Object(uint32_t handle, uint32_t typeMask)        -- the salt-checked object data (0: none)
// The order is the rebase's own: the unit's seat gate, the parent, the definition's seat flags, then (only when
// the flags lack bit 4) the parent's up through the getter's logic. The caller decides the case.
template <class Mem>
ReadResult SeatFrameRead(const ReaderSite& site, const ReaderGlobals& g, Mem& mem, uint32_t unitHandle)
{
    ReadResult r{};
    const uintptr_t unit = mem.Object(unitHandle, 0);
    if (!unit)
        return r;
    uint32_t gate = 0, parent = 0;
    uint16_t seatWord = 0;
    if (!mem.U32(unit + site.unitGate, gate) || !mem.U16(unit + site.unitSeat, seatWord))
        return r;
    if (gate == kNone || static_cast<int16_t>(seatWord) == -1)
    {
        r.readable = true;   // the rebase's own early exit: no rotation
        return r;
    }
    if (!mem.U32(unit + site.objectParent, parent))
        return r;
    if (parent == kNone)
    {
        r.readable = true;   // ODST tests this explicitly; the others never reach it seated
        return r;
    }
    r.seat = static_cast<int16_t>(seatWord);
    r.parent = parent;
    const uintptr_t vehicle = mem.Object(parent, site.parentTypeMask);
    if (!vehicle)
    {
        // Halo 4's typed lookup (0x5DA400) failing is the rebase's own early exit; anywhere else an unresolved
        // parent is a broken link.
        if (site.parentTypeMask && mem.Object(parent, 0))
            r.readable = true;
        return r;
    }
    r.gateSeated = true;

    // The seat's flags word, exactly as the rebase addresses it.
    uint16_t definition = 0;
    if (!mem.U16(vehicle + site.definitionIndex, definition))
        return r;
    uint64_t instances = 0;
    if (!mem.U64(g.tagInstances, instances) || !instances)
        return r;
    uintptr_t flagsAt = 0;
    if (site.scheme == kTagLinear)
    {
        uint64_t base = 0;
        uint32_t address = 0, block = 0;
        if (!mem.U64(g.tagBase, base) || !base || !mem.U32(instances + definition * 8ull + 4, address) || !address)
            return r;
        const uintptr_t def = static_cast<uintptr_t>(base + address * 4ull);
        if (!mem.U32(def + site.seatBlock, block) || !block)
            return r;
        flagsAt = static_cast<uintptr_t>(base + block * 4ull + static_cast<int64_t>(r.seat) * site.seatStride);
    }
    else if (site.scheme == kTagSegmented)
    {
        uint32_t address = 0, block = 0;
        uint64_t segment = 0;
        if (!mem.U32(instances + definition * 8ull + 4, address) ||
            !mem.U64(g.tagBase + (address >> 28) * 8ull, segment))
            return r;
        if (!mem.U32(static_cast<uintptr_t>(segment + address * 4ull + site.seatBlock), block) ||
            !mem.U64(g.tagBase + (block >> 28) * 8ull, segment))
            return r;
        flagsAt = static_cast<uintptr_t>(
            segment + (static_cast<uint64_t>(block) + static_cast<int64_t>(r.seat) * (site.seatStride / 4)) * 4ull);
    }
    else if (site.scheme == kTagSplit)
    {
        uint64_t base = 0, extra = 0;
        uint32_t offset = 0, block = 0;
        if (!mem.U64(g.tagBase, base) || !base || !mem.U32(instances + definition * 16ull + 8, offset))
            return r;
        const uintptr_t def = static_cast<uintptr_t>(base + static_cast<int64_t>(static_cast<int32_t>(offset)));
        if (!mem.U32(def + site.seatBlock, block) || block == kNone)
            return r;
        if (static_cast<int32_t>(block) < 0)
        {
            if (!mem.U64(g.splitNegative, extra))
                return r;
            flagsAt = static_cast<uintptr_t>(extra + (block & 0x7FFFFFFFu));
        }
        else
        {
            if (!mem.U64(g.splitPositive, extra))
                return r;
            flagsAt = static_cast<uintptr_t>(extra + block);
        }
        flagsAt += static_cast<uintptr_t>(static_cast<int64_t>(r.seat) * site.seatStride);
    }
    else
        return r;
    if (!mem.U32(flagsAt, r.flags))
        return r;
    r.flagsKnown = true;
    r.readable = true;
    if (r.flags & kSeatFlagThirdPersonBit)
        return r;   // the rebase exits here; no up is read

    // The up getter's logic.
    if (!VehicleFrameRead(site, mem, vehicle, r.forward, r.up))
        return r;
    r.upKnown = FrameReadAgrees(r.forward, r.up);
    return r;
}

// The frame of the vehicle a seated unit sits in, whatever its seat's flags (the rebase itself reads no up for a
// seat flagged third-person camera). The same links and the same checks as the read above. False: not seated, a
// link did not read, or forward and up are not unit and perpendicular.
template <class Mem>
bool SeatedVehicleFrameRead(const ReaderSite& site, Mem& mem, uint32_t unitHandle, float forward[3], float up[3])
{
    const uintptr_t unit = mem.Object(unitHandle, 0);
    if (!unit)
        return false;
    uint32_t gate = 0, parent = 0;
    uint16_t seatWord = 0;
    if (!mem.U32(unit + site.unitGate, gate) || !mem.U16(unit + site.unitSeat, seatWord) || gate == kNone ||
        static_cast<int16_t>(seatWord) == -1 || !mem.U32(unit + site.objectParent, parent) || parent == kNone)
        return false;
    const uintptr_t vehicle = mem.Object(parent, site.parentTypeMask);
    return vehicle && VehicleFrameRead(site, mem, vehicle, forward, up) && FrameReadAgrees(forward, up);
}

// The case for this update from a read and whether our skip jump is in the rebase right now.
inline Case Decide(const ReadResult& read, bool skipPatched, Frame* frame)
{
    Frame built{};
    const bool usable = read.upKnown && BuildFrame(read.up, built);
    const Case c = Classify(read.readable, read.gateSeated, read.flagsKnown,
                            (read.flags & kSeatFlagThirdPersonBit) != 0, skipPatched, usable);
    if (frame)
        *frame = c == Case::Rebased ? built : Frame{};
    return c;
}
} // namespace seat_frame
