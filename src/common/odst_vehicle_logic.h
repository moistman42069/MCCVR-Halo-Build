#pragma once

#include <cmath>
#include <cstdint>

// Halo 3: ODST vehicle-state logic. Like halo3_vehicle_logic.h this file
// contains no Windows, MinHook, logging or engine access: it is constants and
// pure functions only, so the unit tests can exercise every decision without a
// game.
//
// EVERY constant here is ODST-specific and was derived from the pinned
// halo3odst.dll (SHA-256 5BB20976...829A) and H3ODSTEK, then independently
// re-derived by an adversarial verification pass. See
// docs/ODST-VEHICLE-EVIDENCE.md O-E2. Halo 3's values are NOT reusable: ODST
// grew _object_definition 0xF8 -> 0x108 and its unit block 0x1C4 -> 0x1D4, so
// the whole layout sits +0x20 further out, and the object-table TLS slot moved
// independently of that shift. The Halo 3 byte patterns match ZERO times in
// halo3odst.dll (and these match zero times in halo3.dll), which is what makes
// a mis-bound title fail loud instead of reading garbage.

// Expected RVAs in the pinned module. Logging and cross-check only: the
// runtime binds by unique signature and never ships a hardcoded address.
inline constexpr uintptr_t kOdstUnitInVehicleNativeRva = 0x3C54CC;
inline constexpr uintptr_t kOdstPlayerUnitGetterRva = 0x109260;
inline constexpr uintptr_t kOdstVehicleTypeAccessorRva = 0x3DB46C;
inline constexpr uintptr_t kOdstEngineTlsIndexRva = 0xA8FB1C;
inline constexpr uintptr_t kOdstInterpolatedNodesRva = 0x1B3938;
inline constexpr uintptr_t kOdstMarkersInternalRva = 0x37F514;

// Engine TLS block -> object table. ODST uses +0x20 where Halo 3 uses +0x38;
// this is not part of the +0x20 definition shift, it is an independent
// difference (proven twice: unit_in_vehicle at 0x3C54F5 and
// object_get_ultimate_parent at 0x38234B both `mov r32, 0x20`).
inline constexpr size_t kOdstTlsObjectTableOffset = 0x20;
// The object table itself is laid out exactly as Halo 3's.
inline constexpr size_t kOdstObjectTableEntriesOffset = 0x48;
inline constexpr size_t kOdstObjectEntryStride = 0x18;
inline constexpr size_t kOdstObjectEntryDataOffset = 0x10;
inline constexpr size_t kOdstObjectEntryKindOffset = 3;
inline constexpr unsigned kOdstObjectKindVehicle = 1;
inline constexpr size_t kOdstObjectParentOffset = 0x10;
// The node on the PARENT that an attached child (a seated unit, a mounted gun)
// hangs from. Signed int8; 0xFF (-1) means "no specific node", and the engine
// then falls back to the parent's own orientation. Proven for ODST at
// halo3odst.dll+0x3CDF0F (`cmp byte [rdi+0x14], 0xFF`) and +0x3CDF2F
// (`movsx rax, byte [rdi+0x14]` then `imul rax,rax,0x34`), in the same body
// that reads the parent handle at +0x10 and indexes the node bank through
// ODST's own +0x12C -- so the surrounding shifts are real while this field
// genuinely stayed at +0x14. Thirteen sites read it in each title, all at the
// same offset. The runtime still bounds-checks the value it reads.
inline constexpr size_t kOdstObjectParentNodeOffset = 0x14;
inline constexpr int8_t kOdstObjectParentNodeNone = -1;
// A parent node index this large is not a node, it is a wrong offset.
inline constexpr int kOdstMaximumRenderNodes = 255;
// Unit seat word: 0xFFFF (read as int16 -1) when the unit is not seated.
// Halo 3 reads +0x24E; ODST's own seat-mode chooser at 0x1535D3 reads +0x262.
inline constexpr size_t kOdstUnitSeatWordOffset = 0x262;

// The object's bounding sphere and its frame, as Halo 3's (halo3_vehicle_logic.h kHalo3Object*Offset) -- read only by
// the vehicle cameras. ADDR-HYGIENE: guarded -- field offsets proven in the pinned module, and every read is checked
// at run time (the frame orthonormal, its origin inside its own bounds) before anything is placed from it.
//   +0x50 position, +0x5C forward, +0x68 up: halo3odst +0x380C34 (object_get_world_matrix, the homolog of halo3
//     +0x3453DC, the same instructions with ODST's object-table TLS slot 0x20) builds the object's matrix from
//     exactly these. An attached object's are relative to its parent's attachment node, as in Halo 3.
//   +0x1C bounding-sphere centre, +0x28 radius: halo3odst +0x3DB93C (the homolog of halo3 +0x397230, same shape)
//     takes `movss xmm0, [obj+0x28]` and `lea r9, [obj+0x1C]` into its sphere query.
inline constexpr size_t kOdstObjectBoundsCenterOffset = 0x1C;
inline constexpr size_t kOdstObjectBoundsRadiusOffset = 0x28;
inline constexpr size_t kOdstObjectPositionOffset = 0x50;
inline constexpr size_t kOdstObjectForwardOffset = 0x5C;
inline constexpr size_t kOdstObjectUpOffset = 0x68;

// THE OBSERVER (the game's own camera for a player), read by the vehicle cameras as Halo 3's is: an array of 0x3D0-byte
// records at an engine TLS slot. Its record is Halo 3's -- halo3odst +0x1AEEF9 reads the camera's focus (the pivot a
// seat's third-person camera turns about) at +0x18C beside +0x1AC and +0x194 exactly as halo3 +0x18032C does, and
// +0x1ACBF7 its position (+0x11C) and forward (+0x144) as halo3 +0x17E04B -- but the slot differs (0x5A8, Halo 3's
// 0x578). The slot and the stride are decoded from the matched observer camera effect (kObserverCameraEffectSig, unique
// in each module): `mov edi, imm32` (BF) at +0x56 loads the slot, `imul rbx, rsi, imm32` (48 69 DE) at +0x6A the stride.
// ADDR-HYGIENE: resolved -- read from the matched function, its opcode bytes and the stride checked before the slot
// is taken; a mismatch leaves the observer unread (the "thirdperson" camera then stands at the mod's stand-in).
inline constexpr size_t kOdstObserverSlotLoadOffset = 0x56;
inline constexpr size_t kOdstObserverStrideLoadOffset = 0x6A;
inline constexpr uint32_t kOdstObserverStride = 0x3D0;

// The observer slot decoded from the effect function's bytes at +0x56 (5 bytes) and +0x6A (7 bytes): 0 when either
// instruction is not the expected one, the stride is not a record's, or the slot is not a plausible TLS member.
inline uint32_t OdstDecodeObserverSlot(const uint8_t slotLoad[5], const uint8_t strideLoad[7])
{
    if (!slotLoad || !strideLoad || slotLoad[0] != 0xBF || strideLoad[0] != 0x48 || strideLoad[1] != 0x69 ||
        strideLoad[2] != 0xDE)
        return 0;
    const uint32_t slot = uint32_t{slotLoad[1]} | uint32_t{slotLoad[2]} << 8 | uint32_t{slotLoad[3]} << 16 |
                          uint32_t{slotLoad[4]} << 24;
    const uint32_t stride = uint32_t{strideLoad[3]} | uint32_t{strideLoad[4]} << 8 | uint32_t{strideLoad[5]} << 16 |
                            uint32_t{strideLoad[6]} << 24;
    if (stride != kOdstObserverStride || slot == 0 || slot > 0x2000 || (slot & 7u) != 0)
        return 0;
    return slot;
}

// Live interpolated node bank on the object. Halo 3's cluster sits at
// +0x132/+0x134/+0x136/+0x138; ODST's is the same cluster shifted -0xC.
inline constexpr size_t kOdstObjectOrientationBankSizeOffset = 0x126;
inline constexpr size_t kOdstObjectOrientationBankRelOffset = 0x128;
inline constexpr size_t kOdstObjectNodeBankSizeOffset = 0x12A;
inline constexpr size_t kOdstObjectNodeBankRelOffset = 0x12C;
// Node matrices are the same 0x34-byte real_matrix4x3 Halo 3 uses, so
// Halo3Matrix4x3 and its pure transform helpers are shared, not duplicated.
inline constexpr size_t kOdstNodeMatrixStride = 0x34;

// The object table is the engine's generic 0x60-byte "data array" header,
// created as data_new_full("object", 0x800, 0x18). Every field below was
// proven from data_initialize/datum_new/datum_delete and is byte-identical in
// Halo 3 — only the TLS slot that reaches the table differs between titles.
// This is what lets a probe enumerate EVERY live object (including AI-crewed
// and parked vehicles) instead of only the one the player is handle-linked to.
inline constexpr size_t kOdstDataArrayNameOffset = 0x00;
inline constexpr size_t kOdstDataArrayNameSize = 0x20;
inline constexpr size_t kOdstDataArrayMaxCountOffset = 0x20;
inline constexpr size_t kOdstDataArrayElementSizeOffset = 0x24;
inline constexpr size_t kOdstDataArrayValidOffset = 0x29;
inline constexpr size_t kOdstDataArraySignatureOffset = 0x2C;
// Iteration bound: the high-water mark. Slots below it may still be holes, so
// a walk must skip them; actual_count is NEVER a valid bound.
inline constexpr size_t kOdstDataArrayFirstUnallocatedOffset = 0x3C;
inline constexpr size_t kOdstDataArrayActualCountOffset = 0x40;
inline constexpr size_t kOdstDataArrayElementsOffset = 0x48;
inline constexpr uint32_t kOdstDataArraySignature = 0x64407440;  // 'd@t@'
inline constexpr uint32_t kOdstObjectTableCapacity = 0x800;
// An entry's identifier word. Zero means the slot is free; live identifiers
// are always >= 0x8000 (the allocator wraps 0xFFFF back to 0x8000), and a
// handle is (identifier << 16) | index. 0xFFFF is NOT a free sentinel here.
inline constexpr size_t kOdstObjectEntryIdentifierOffset = 0x00;

// Everything a probe must confirm before walking the table. Pure so the rule
// is unit-testable: a header that fails any part of this is not the object
// table this build expects, and the walk must not happen at all.
struct OdstDataArrayHeaderView
{
    bool nameIsObject = false;
    uint32_t signature = 0;
    uint32_t maximumCount = 0;
    uint32_t elementSize = 0;
    uint32_t firstUnallocated = 0;
    uint8_t valid = 0;
};

inline bool OdstObjectTableIsWalkable(const OdstDataArrayHeaderView& header)
{
    if (!header.nameIsObject)
        return false;
    if (header.signature != kOdstDataArraySignature)
        return false;
    if (header.elementSize != kOdstObjectEntryStride)
        return false;
    if (header.maximumCount != kOdstObjectTableCapacity)
        return false;
    if (header.valid != 1)
        return false;
    return header.firstUnallocated <= header.maximumCount;
}

inline bool OdstObjectEntryIsLive(uint16_t identifier)
{
    return identifier != 0;
}

// Loaded vehicle definition. Seat stride and flag bit order are identical to
// Halo 3; the block offsets are not.
inline constexpr size_t kOdstVehicleSeatsCountOffset = 0x278;
inline constexpr size_t kOdstVehicleSeatsDataOffset = 0x27C;
inline constexpr size_t kOdstVehicleSeatStride = 0xD4;
inline constexpr size_t kOdstSeatFlagsOffset = 0x00;
inline constexpr uint32_t kOdstSeatThirdPersonCameraBit = 1u << 4;
inline constexpr uint32_t kOdstSeatDriverBit = 1u << 2;
inline constexpr uint32_t kOdstSeatGunnerBit = 1u << 3;
inline constexpr uint32_t kOdstSeatInvalidForPlayerBit = 1u << 16;
// "allows weapons": the occupant fires their OWN weapon from this seat rather
// than the vehicle's. Warthog passenger 0x1070 has it; the driver's 0x40014
// does not. This is what separates a seat whose shots leave the occupant's own
// eye point from one whose shots leave a vehicle barrel.
inline constexpr uint32_t kOdstSeatAllowsWeaponsBit = 1u << 5;
inline constexpr size_t kOdstSeatCameraMarkerNameOffset = 0x6C;
// Physics-type blocks: 10 records of 0xC. The accessor returns the first
// non-empty index, or 0xB when the definition authors none (walk-up turrets
// and the shade both author none).
inline constexpr size_t kOdstVehiclePhysicsBlocksOffset = 0x2E0;
inline constexpr size_t kOdstVehiclePhysicsRecordStride = 0xC;
inline constexpr int kOdstPhysicsTypeCount = 10;
inline constexpr int kOdstPhysicsTypeNoneAuthored = 0xB;
inline constexpr int kOdstPhysicsTypeTurret = 5;
// Discriminator fields inside those records, both confirmed by retail ODST
// instructions: the jeep engine sub-struct base is taken at
// +0x4147C2 (`lea rdx, [elem+0x14]`) and its first float divided as the engine
// moment at +0x4276ED; the scout's specific type is compared as an int8 at
// +0x417432 (`cmp byte [rax+0x28], 4`).
inline constexpr size_t kOdstJeepEngineMomentOffset = 0x14;
inline constexpr size_t kOdstScoutSpecificTypeOffset = 0x28;

// Loaded-tag chain to the render model. Identical to Halo 3.
inline constexpr size_t kOdstVehicleModelRefOffset = 0x40;
inline constexpr size_t kOdstModelRenderRefOffset = 0x0C;
inline constexpr size_t kOdstRenderModelNodesCountOffset = 0x30;
inline constexpr size_t kOdstRenderModelNodesDataOffset = 0x34;
inline constexpr size_t kOdstRenderModelNodeStride = 0x60;
inline constexpr size_t kOdstRenderModelNodeInverseOffset = 0x28;

// Occupant head marker. Independently derived from ODST's own static string-id
// table (base RVA 0x7C4E80, record #159), NOT carried over from Halo 3 —
// built-in ids demonstrably drift between the titles past id 0xC5. It happens
// to land on the same value.
inline constexpr uint32_t kOdstHeadMarkerStringId = 0x9F;

// Seat NAMES (driver/passenger/gunner/camera) are absent from the static
// string-id table entirely: they are tag-content ids registered per map cache,
// so they can never be compile-time constants. Seats are keyed by index.

enum class OdstVehicleBindingState : uint8_t
{
    NotInstalled = 0,
    Installed,
    StockFallback,
};

// What the probe observed about one object-table entry. Pure data so the
// classification below is unit-testable without an engine.
struct OdstProbeVehicleRecord
{
    bool dataValid = false;
    uint32_t definitionIndex = 0xFFFFFFFFu;
    int physicsType = -1;
    // Node bank as read from the object.
    int nodeBankByteSize = -1;
    int nodeBankRelOffset = 0;
    // Nodes the render model declares for this definition.
    int tagNodeCount = -1;
};

// A node bank is trustworthy only when its byte size is a positive whole
// number of 0x34-byte matrices AND that count matches the render model's own
// node count. Anything else means the offsets are wrong for this title and the
// runtime must stay stock rather than transform a garbage matrix.
inline bool OdstNodeBankIsCoherent(const OdstProbeVehicleRecord& record)
{
    if (record.nodeBankByteSize <= 0)
        return false;
    if (static_cast<size_t>(record.nodeBankByteSize) % kOdstNodeMatrixStride)
        return false;
    const int matrices =
        record.nodeBankByteSize / static_cast<int>(kOdstNodeMatrixStride);
    if (matrices <= 0 || matrices > 255)
        return false;
    if (record.tagNodeCount <= 0)
        return false;
    return matrices == record.tagNodeCount;
}

// Halo 3's identities, reused where ODST ships the same vehicle, plus the two
// ODST additions. Kept as a separate enum so a future Halo 3 enum change can
// never silently re-key ODST seats.
enum class OdstVehicleId : uint8_t
{
    Unknown = 0,
    Scorpion,
    Warthog,
    Mongoose,
    Ghost,
    Wraith,
    Mauler,
    Banshee,
    Hornet,
    Chopper,
    StationaryTurret,
    // ODST additions. The shade is a real vehicle the player drives (5
    // missions); Halo 3's campaign never let the mod meet one.
    Shade,
};

struct OdstDefinitionFields
{
    int physicsType = -1;
    bool jeepValid = false;
    float engineMoment = 0.0f;
    bool scoutValid = false;
    int specificType = -1;
    // Seat 0's flag word, used only to separate the shade (a driven vehicle)
    // from a walk-up turret: both author zero physics blocks.
    bool seatFlagsValid = false;
    uint32_t seat0Flags = 0;
    // The native predicate's answer for the occupant. A walk-up turret reports
    // 0; a real vehicle reports 1.
    bool inVehicle = false;
};

inline constexpr float kOdstWarthogEngineMoment = 2000.0f;
inline constexpr float kOdstMongooseEngineMoment = 650.0f;
inline constexpr float kOdstEngineMomentTolerance = 200.0f;

// Identity resolution. Physics types and discriminator semantics were proven
// from ODST's own accessor and tag exports; the values coincide with Halo 3's
// because both titles ship the same authored vehicles, not because Halo 3's
// were assumed.
//
// The shade/turret split is the one ODST-specific rule: neither authors a
// physics block (accessor returns 0xB, or 5 for the explicit turret type), so
// the seat's own driver bit separates a driven shade from a walk-up gun.
inline OdstVehicleId OdstResolveVehicleId(const OdstDefinitionFields& def)
{
    switch (def.physicsType)
    {
    case 0: return OdstVehicleId::Scorpion;      // human_tank
    case 4: return OdstVehicleId::Banshee;       // alien_fighter
    case 7: return OdstVehicleId::Hornet;        // vtol
    case 8: return OdstVehicleId::Chopper;       // chopper
    case 1:                                       // human_jeep
        if (!def.jeepValid || !std::isfinite(def.engineMoment))
            return OdstVehicleId::Unknown;
        if (std::fabs(def.engineMoment - kOdstWarthogEngineMoment) <=
            kOdstEngineMomentTolerance)
            return OdstVehicleId::Warthog;
        if (std::fabs(def.engineMoment - kOdstMongooseEngineMoment) <=
            kOdstEngineMomentTolerance)
            return OdstVehicleId::Mongoose;
        return OdstVehicleId::Unknown;
    case 3:                                       // alien_scout
        if (!def.scoutValid)
            return OdstVehicleId::Unknown;
        if (def.specificType == 1) return OdstVehicleId::Ghost;
        if (def.specificType == 3) return OdstVehicleId::Wraith;
        if (def.specificType == 4) return OdstVehicleId::Mauler;
        return OdstVehicleId::Unknown;
    case kOdstPhysicsTypeTurret:                  // explicit turret
    case kOdstPhysicsTypeNoneAuthored:            // nothing authored
        if (!def.seatFlagsValid)
            return OdstVehicleId::Unknown;
        // A driven vehicle: seat 0 is a driver seat and the engine agrees the
        // occupant is in a vehicle. Walk-up turrets satisfy neither.
        if ((def.seat0Flags & kOdstSeatDriverBit) && def.inVehicle)
            return OdstVehicleId::Shade;
        // O8: a turret the engine does NOT call a vehicle is a walk-up
        // emplacement, whatever its seat 0 authors. The 2026-08-06 probe
        // caught the Covenant gun as `native=0 ... type=5 seats=1
        // seat0Flags=0x800C (driver=1 gunner=1)`: it carries the driver bit,
        // so the test below rejected it and it resolved to Unknown - no
        // identity, therefore no authored seat point, therefore the camera sat
        // at the vehicle root. That is the reported "I'm in the ground".
        //
        // This deliberately keys on inVehicle being FALSE, which is the exact
        // opposite scoping to the rejected O7. Every in-vehicle turret,
        // including the Warthog's own mounted gun (`def=0xE32 native=1
        // seats=2`), reaches this point unchanged and is decided by the two
        // rules above exactly as before. O7 broke that gun by sweeping the
        // in-vehicle side; this cannot touch it.
        if (!def.inVehicle)
            return OdstVehicleId::StationaryTurret;
        if (!(def.seat0Flags & kOdstSeatDriverBit))
            return OdstVehicleId::StationaryTurret;
        return OdstVehicleId::Unknown;
    default:
        return OdstVehicleId::Unknown;
    }
}

inline const char* OdstVehicleIdName(OdstVehicleId id)
{
    switch (id)
    {
    case OdstVehicleId::Scorpion: return "scorpion";
    case OdstVehicleId::Warthog: return "warthog";
    case OdstVehicleId::Mongoose: return "mongoose";
    case OdstVehicleId::Ghost: return "ghost";
    case OdstVehicleId::Wraith: return "wraith";
    case OdstVehicleId::Mauler: return "prowler";
    case OdstVehicleId::Banshee: return "banshee";
    case OdstVehicleId::Hornet: return "hornet";
    case OdstVehicleId::Chopper: return "chopper";
    case OdstVehicleId::StationaryTurret: return "turret";
    case OdstVehicleId::Shade: return "shade";
    case OdstVehicleId::Unknown: break;
    }
    return "unknown";
}

// A seat word of -1 (0xFFFF) means on foot. The probe uses this as its
// negative control: if the offset were wrong for this title, an on-foot player
// would not read exactly -1 frame after frame.
inline bool OdstSeatWordMeansUnseated(int seatWord)
{
    return seatWord == -1;
}

// Authored first-person seat points, vehicle/carrier tag space, world units —
// the same convention and the same struct shape as kHalo3SeatPoints.
//
// PROVENANCE (docs/ODST-VEHICLE-EVIDENCE.md O-E1/O-E1b). Every vehicle ODST
// shares with Halo 3 was diffed tag-for-tag: the render-model node hierarchies
// are byte-identical (max default-translation delta 0.0), all shared marker
// translations agree within 1e-4 wu, and every player seat's label, marker,
// flag word and camera marker match. Those seats therefore REUSE the
// headset-accepted Halo 3 values verbatim rather than being re-authored — a
// re-author could only introduce drift from a placement the user already
// accepted in the headset.
//
// The remaining entries are seats Halo 3 never had. They are composed from the
// tags' own seat markers through the render-model default node chain, plus the
// kit's seated-eye convention of 0.33 wu above the seat marker — the same way
// every accepted Halo 3 root-seat point was authored. Two independent
// cross-checks validated that composition: the mauler turret's seat marker
// composes exactly onto the engine's own gunner camera marker, and the
// scorpion turret attach composes onto the accepted Halo 3 mounted-gunner
// point in x and y.
struct OdstSeatPoint
{
    OdstVehicleId vehicle;
    int8_t seatIndex;
    bool carrierFrame;
    float x, y, z;
};

inline constexpr OdstSeatPoint kOdstSeatPoints[] = {
    // ---- Reused verbatim from the accepted Halo 3 table ----
    {OdstVehicleId::Warthog,  0, false,  0.0299f,  0.1683f, 0.6663f},
    {OdstVehicleId::Warthog,  1, false, -0.0139f, -0.1903f, 0.6601f},
    {OdstVehicleId::Mongoose, 0, false, -0.0476f,  0.0000f, 0.5207f},
    {OdstVehicleId::Mongoose, 1, false, -0.4368f,  0.0000f, 0.5967f},
    {OdstVehicleId::Ghost,    0, false, -0.1078f,  0.0000f, 0.4491f},
    {OdstVehicleId::Wraith,   0, false,  0.3060f,  0.0000f, 1.0573f},
    {OdstVehicleId::Chopper,  0, false, -0.4396f,  0.0000f, 0.7348f},
    {OdstVehicleId::Banshee,  0, false,  0.4190f,  0.0000f, 0.4119f},
    {OdstVehicleId::Scorpion, 0, false,  0.0326f,  0.1893f, 1.0226f},
    // No ODST campaign scenario places a Prowler or a Hornet, but both tags
    // ship and diffed identical to Halo 3's, so carrying their accepted points
    // costs nothing and covers a mod or a Firefight variant that spawns one.
    {OdstVehicleId::Mauler,   0, false, -1.0738f,  0.0000f, 0.6381f},
    {OdstVehicleId::Mauler,   1, false, -0.2252f,  0.4659f, 0.7430f},
    {OdstVehicleId::Mauler,   2, false, -0.2389f, -0.4790f, 0.6764f},
    {OdstVehicleId::Hornet,   0, false,  1.0059f, -0.0098f, 0.6517f},
    {OdstVehicleId::Hornet,   1, false,  0.5215f,  0.3566f, 0.5583f},
    {OdstVehicleId::Hornet,   2, false,  0.5208f, -0.3379f, 0.5830f},
    // Mounted-turret gunners, authored in the CARRIER's frame.
    {OdstVehicleId::Warthog,  0, true,  -0.5832f,  0.0000f, 1.0624f},
    {OdstVehicleId::Scorpion, 0, true,   0.4673f, -0.1903f, 0.8659f},
    {OdstVehicleId::Wraith,   0, true,  -0.1886f,  0.0000f, 1.1956f},
    {OdstVehicleId::Mauler,   0, true,   0.0054f,  0.0000f, 0.9307f},
    // The walk-up machinegun turret / plasma cannon / missile pod share one
    // point, exactly as they do in Halo 3.
    {OdstVehicleId::StationaryTurret, 0, false, -0.2368f, 0.0000f, 0.5743f},

    // ---- ODST-only seats ----
    // The scorpion's four corner riders. Halo 3 marks these invalid for the
    // player; ODST clears that bit (flags 0x10270 -> 0x270) and squads ride
    // the tank, so the player can take them too. Each rider sits on its own
    // tread-cover node, which is why the lateral values are so large.
    {OdstVehicleId::Scorpion, 1, false,  0.3217f,  0.8483f, 0.7864f},
    {OdstVehicleId::Scorpion, 2, false,  0.5289f, -0.8483f, 0.7890f},
    {OdstVehicleId::Scorpion, 3, false, -0.7294f,  0.9483f, 0.7910f},
    {OdstVehicleId::Scorpion, 4, false, -0.5277f, -0.9483f, 0.7896f},
    // The shade. Its authored shade_d_camera marker sits at the ORIGIN of
    // node 0 (degenerate), so the seat marker on node 1 (`floater`) is the
    // only usable anchor. That node rotates with the gun, so resolving it
    // through the live node matrix makes the eye ride the mount.
    {OdstVehicleId::Shade,    0, false, -0.1252f,  0.0000f, 0.9791f},
};

inline constexpr const OdstSeatPoint* OdstFindSeatPoint(
    OdstVehicleId vehicle, int seatIndex, bool mountedTurret)
{
    if (vehicle == OdstVehicleId::Unknown)
        return nullptr;
    for (const OdstSeatPoint& p : kOdstSeatPoints)
    {
        if (p.vehicle == vehicle && p.carrierFrame == mountedTurret &&
            static_cast<int>(p.seatIndex) == seatIndex)
            return &p;
    }
    return nullptr;
}

// Behaviour predicates. These mirror the Halo 3 rules because ODST ships the
// same authored vehicles and the same control scheme for them, with the shade
// added: the shade is a driven turret whose hull IS its aim, so it is treated
// like a walk-up turret for steering (nothing authors it) while still getting
// a seat point.
inline constexpr bool OdstSeatIsDriver(OdstVehicleId id, int seatIndex,
                                       bool mountedTurret)
{
    return !mountedTurret && seatIndex == 0 &&
        id != OdstVehicleId::Unknown &&
        id != OdstVehicleId::StationaryTurret &&
        id != OdstVehicleId::Shade;
}

inline constexpr bool OdstVehicleIsLookSteered(OdstVehicleId id)
{
    switch (id)
    {
    case OdstVehicleId::Warthog:
    case OdstVehicleId::Mongoose:
    case OdstVehicleId::Ghost:
    case OdstVehicleId::Mauler:
    case OdstVehicleId::Chopper:
    case OdstVehicleId::Banshee:
    case OdstVehicleId::Hornet:
        return true;
    default:  // Scorpion, Wraith, shade, turrets, Unknown
        return false;
    }
}

inline constexpr bool OdstVehicleUsesWheel(OdstVehicleId id)
{
    return OdstVehicleIsLookSteered(id) &&
        id != OdstVehicleId::Banshee && id != OdstVehicleId::Hornet;
}

inline constexpr bool OdstSeatFollowsHull(OdstVehicleId id, int seatIndex,
                                          bool mountedTurret)
{
    if (id == OdstVehicleId::Unknown ||
        id == OdstVehicleId::StationaryTurret)
        return false;
    // The shade's own body IS what its aim turns, exactly like a walk-up
    // turret's, so following it would cancel the closed loop's feedback.
    if (id == OdstVehicleId::Shade)
        return false;
    return OdstFindSeatPoint(id, seatIndex, mountedTurret) != nullptr;
}

inline constexpr bool OdstSeatFollowsPitch(OdstVehicleId id, int seatIndex,
                                           bool mountedTurret)
{
    if (!OdstSeatFollowsHull(id, seatIndex, mountedTurret))
        return false;
    return id != OdstVehicleId::Banshee && id != OdstVehicleId::Hornet;
}

// A seat whose weapon is a physically slewing gun rather than the occupant's
// own: the walk-up emplacements, the shade, and any mounted gunner. Their aim
// carries an authored rate cap the closed loop cannot exceed (H3EK shade_d is
// 15 deg/s on both axes, warthog_g is 60 deg/s yaw with NO pitch cap), so they
// take the servo's rest hysteresis and the barrel-riding reticle. Every other
// seat keeps exactly the loop it shipped with.
inline constexpr bool OdstSeatIsTurret(OdstVehicleId id, bool mountedTurret)
{
    if (mountedTurret)
        return true;
    return id == OdstVehicleId::StationaryTurret ||
        id == OdstVehicleId::Shade;
}

inline constexpr bool OdstSeatAuthorsSteering(OdstVehicleId id, int seatIndex,
                                              bool mountedTurret,
                                              bool followEnabled)
{
    return followEnabled && OdstSeatIsDriver(id, seatIndex, mountedTurret) &&
        OdstVehicleIsLookSteered(id);
}
