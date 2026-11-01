// THE THIRD-PERSON BOOM'S GAME SIDE (vehicle_boom_logic.h the rules, vehicle_cameras.inl the runtime): what the
// flat game does with a seat's look and its third-person camera, read from halo3.dll. Included in game.cpp before
// vehicle_cameras.inl. No hook: anchors and reads only.
//
//   SEAT CAMERA   +0x355898, the unit camera that the look update and the following camera both ask for: a seated
//                 unit's camera is its parent's seat record + 0x68 (the seats block's address at definition
//                 +0x25C, 0xD4 a seat); a unit on foot has its own at definition +0x110.
//   LOOK LIMITS   +0xF7DF0, the player's look update: the pitch is held to the seat camera's range (+0x10, +0x14,
//                 radians) where either end is non-zero, each end held to the update's own two constants (+-85.5
//                 deg, read from the module here); else to those two.
//   TRACK         +0x213CF4, the following camera: its look pitch (held to +-90 deg) is solved to a parameter of
//                 the seat camera's track by the track's own orientations (+0x214A54), and the track's positions at
//                 that parameter (+0x2146C0: +0x214D80 through four points, +0xA10EC for the orientations) are the
//                 camera's place from its pivot, laid along the look's heading. The tracks block is at camera +0x18
//                 (count) / +0x1C (address), 0x10 an element with the 'trak' tag index at +0xC; none, or an empty
//                 reference: the game globals' default at +0x3FC. A 'trak' has its control points at +4 / +8, 0x1C
//                 each: position, then orientation (i, j, k, w). Fewer than four: no track.
//   INVERSION     +0x1871BC, the gamepad abstraction: the right stick's Y becomes the look pitch negated by the
//                 controller's preference byte +0x200, and a second pitch negated by +0x202. The player-control
//                 builder (+0xF42D8) takes the second where +0xF91F4 says the player pilots an aircraft -- the driver's
//                 seat (flags bit 2) of a vehicle whose physics type is a flyer (2, 4, 7) -- and the first everywhere
//                 else. Preferences are 0x21C a controller at the abstraction's base; the mod's pad is controller 0.
//
// ADDR-HYGIENE: resolved (every address decoded from a unique signature, nothing hardcoded) + guarded (each decode
// is cross-checked against another: the look update's call lands on the seat camera function; the following
// camera's calls land on the solver and the track, which match in place, and it asks the seat camera function; the
// track's calls land on the cubic and the rotation, which match in place with their constants; the seat camera's and
// the track's tag tables are the mod's own; the abstraction's preference base is where the builder's state reads the
// two pitches; the pilot test's type call is the mod's own type accessor; and each pair of sites within one function
// shares that function's unwind entry). Any part failing is unbound on its own and says so: no track, the boom
// orbits the pivot; no limits, +-85.5 deg; no inversion, not inverted.

// The seat camera within a seat record, and its fields (+0x355898, +0xF8334, +0x2146C0).
inline constexpr uintptr_t kHalo3SeatCameraOffset = 0x68;
inline constexpr uintptr_t kHalo3CameraPitchMinOffset = 0x10;
inline constexpr uintptr_t kHalo3CameraPitchMaxOffset = 0x14;
inline constexpr uintptr_t kHalo3CameraTracksCountOffset = 0x18;
inline constexpr uintptr_t kHalo3CameraTracksAddressOffset = 0x1C;
inline constexpr size_t kHalo3CameraTrackStride = 0x10;
inline constexpr uintptr_t kHalo3CameraTrackTagOffset = 0xC;
inline constexpr uintptr_t kHalo3GlobalsDefaultTrackOffset = 0x3FC;
inline constexpr uintptr_t kHalo3TrackPointsCountOffset = 0x4;
inline constexpr uintptr_t kHalo3TrackPointsAddressOffset = 0x8;
inline constexpr size_t kHalo3TrackPointStride = 0x1C;
inline constexpr uintptr_t kHalo3TrackPointOrientationOffset = 0xC;
inline constexpr uint32_t kHalo3SeatDriverBit = 1u << 2;
// The controller preferences (+0x1871BC) and the builder's state (+0xF43CA): the look and flight inversion bytes,
// and where the abstraction's pitch lands in the state the builder reads.
inline constexpr size_t kHalo3PadPreferencesStride = 0x21C;
inline constexpr uintptr_t kHalo3PadLookInvertOffset = 0x200;
inline constexpr uintptr_t kHalo3PadFlightInvertOffset = 0x202;
inline constexpr uintptr_t kHalo3PadPitchOffset = 0x1498;
inline constexpr uintptr_t kHalo3BuilderPitchOffset = 0x504;
static_assert(vehicle_camera_file::kBoomPitchUnset == vehicle_boom::kBoomPitchUnset &&
                  vehicle_camera_file::kMaxBoomPitch == vehicle_boom::kMaxBoomPitchDeg,
              "the camera file's boomPitch and the boom's are one value");

// +0x35592F, in +0x355898: a seated unit's camera is its parent's seat record + 0x68. Tag base at +12, instance table
// at +22.
inline constexpr char kBoomSeatCameraSig[] =
    "48 8B 48 48 48 8B 44 D1 10 48 8B 15 ?? ?? ?? ?? 0F B7 08 48 8B 05 ?? ?? ?? ?? 44 8B 44 C8 04 45 85 C0 75 04 33 "
    "C0 EB 04 4A 8D 04 82 8B 88 5C 02 00 00 85 C9 75 04 33 C9 EB 04 48 8D 0C 8A 49 0F BF C2 4C 69 C0 D4 00 00 00 44 "
    "89 0B 49 8D 40 68 48 03 C1 48 89 43 08 0F B7 87 4E 02 00 00 66 89 43 04 41 8B 04 08 83 E0 14 3C 14 75 39 48 8D "
    "53 10 41 8B C9 E8 ?? ?? ?? ?? EB 2B 0F B7 0F 48 8B 05 ?? ?? ?? ?? 8B 54 C8 04 85 D2 75 04 33 C0 EB 0B 48 8B 05 ?? "
    "?? ?? ?? 48 8D 04 90 48 05 10 01 00 00 48 89 43 08";
// +0xF7E65, in +0xF7DF0: the look update loads its two limits (+75: the upper, +100: the lower) and asks the seat
// camera function (+105) for its camera, into [rsp+0x58].
inline constexpr char kBoomLookHeadSig[] =
    "65 48 8B 04 25 58 00 00 00 4C 63 E1 49 69 DC 10 01 00 00 48 8B 34 D0 B8 C0 00 00 00 48 89 74 24 30 48 8B 3C 30 "
    "48 8B 05 ?? ?? ?? ?? 8B 88 08 04 00 00 85 C9 75 05 45 33 ED EB 0B 48 8B 05 ?? ?? ?? ?? 4C 8D 2C 88 F3 44 0F 10 "
    "2D ?? ?? ?? ?? 48 8D 54 24 50 8B 8C 3B C0 00 00 00 45 0F 28 C5 F3 44 0F 10 0D ?? ?? ?? ?? E8 ?? ?? ?? ??";
// +0xF8334, in +0xF7DF0: the camera's range (+0x10, +0x14) where either end is set, each held to the lower limit
// (+69, +88) and the upper.
inline constexpr char kBoomLookClampSig[] =
    "48 8B 44 24 58 F3 44 0F 10 15 ?? ?? ?? ?? 48 85 C0 0F 84 CD 01 00 00 F3 0F 10 48 14 41 0F 2E CB F3 0F 10 70 0C "
    "7A 0F 75 0D F3 0F 10 40 10 41 0F 2E C3 7A 02 74 30 F3 44 0F 10 48 10 44 0F 28 C1 F3 44 0F 5F 0D ?? ?? ?? ?? 45 "
    "0F 2F E9 77 04 45 0F 28 CD F3 44 0F 5F 05 ?? ?? ?? ?? 45 0F 2F E8 77 04 45 0F 28 C5";
// +0x214052, in +0x213CF4: the look pitch held to +-90 deg, solved on the camera's track (+44), the track's place
// and forward at that parameter (+68), into [rbp-0x69] and [rbp-0x59].
inline constexpr char kBoomFollowPitchSig[] =
    "F3 44 0F 58 56 1C F3 0F 10 05 ?? ?? ?? ?? F3 44 0F 5F 15 ?? ?? ?? ?? 41 0F 2F C2 77 04 44 0F 28 D0 44 8A C3 41 "
    "0F 28 CA 49 8B CE E8 ?? ?? ?? ?? 48 8B 4D BF 4C 8D 4D 97 4C 8D 45 A7 88 5C 24 20 0F 28 C8 E8 ?? ?? ?? ??";
// +0x21419B, in +0x213CF4: the command's focus distance is the place's length, and its focus offset that length
// along the look plus the place: forward, -left (the observer's right), up.
inline constexpr char kBoomFollowOffsetSig[] =
    "F3 44 0F 10 4D 9B F3 0F 10 75 97 41 0F 28 C1 F3 44 0F 10 45 9F 0F 28 CE F3 41 0F 59 C1 41 0F 28 D0 F3 0F 59 CE "
    "F3 41 0F 59 D0 F3 0F 58 C1 F3 0F 58 C2 E8 ?? ?? ?? ?? 0F 28 F8 41 0F 28 C2 F3 0F 11 7F 24 E8 ?? ?? ?? ?? F3 0F "
    "59 C7 F3 0F 58 C6 F3 0F 59 46 20 F3 0F 11 47 10 41 0F 28 C2 F3 44 0F 59 4E 20 44 0F 57 0D ?? ?? ?? ?? F3 44 0F "
    "11 4F 14 E8 ?? ?? ?? ?? 8B 4D B7 48 8D 57 44 F3 0F 59 C7 45 33 C0 F3 41 0F 58 C0 F3 0F 59 46 20 F3 0F 11 47 18";
// +0x214A54 whole: the solver. Its span's end at +42 (1), the half at +82 (0.5), its call to the track at +90, the
// stopping width at +166 (1e-4).
inline constexpr char kBoomSolverSig[] =
    "48 8B C4 48 89 58 08 57 48 81 EC 80 00 00 00 0F 29 70 E8 41 8A D8 0F 29 78 D8 48 8B F9 44 0F 29 40 C8 0F 57 FF "
    "F3 44 0F 10 05 ?? ?? ?? ?? 44 0F 29 48 B8 44 0F 28 C9 41 0F 28 F0 88 5C 24 20 F3 0F 58 F7 45 33 C9 4C 8D 44 24 "
    "30 48 8B CF F3 0F 59 35 ?? ?? ?? ?? 0F 28 CE E8 ?? ?? ?? ?? F3 0F 10 4C 24 34 F3 0F 10 44 24 30 F3 0F 59 C9 F3 "
    "0F 59 C0 F3 0F 58 C8 0F 28 C1 E8 ?? ?? ?? ?? 0F 28 C8 F3 0F 10 44 24 38 E8 ?? ?? ?? ?? 44 0F 2F C8 76 05 0F 28 FE "
    "EB 04 44 0F 28 C6 41 0F 28 C8 F3 0F 5C CF 0F 2F 0D ?? ?? ?? ?? 77 8B 0F 28 7C 24 60 4C 8D 9C 24 80 00 00 00 49 "
    "8B 5B 10 0F 28 C6 45 0F 28 43 D0 45 0F 28 4B C0 0F 28 74 24 70 49 8B E3 5F C3";
// +0x2146C0's head: the tracks block (+0x18, +0x1C, 0x10 an element, the tag at +0xC), the globals' default
// (+139, +0x3FC), the 'trak' (+4 count of at least 4, +8, 0x1C a point, the orientation at +0xC) and the
// orientations turned (+370). Tag base at +44, instance table at +166.
inline constexpr char kBoomTrackSig[] =
    "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A9 48 81 EC C0 00 00 00 8B 51 "
    "18 45 33 D2 48 8B 1D ?? ?? ?? ?? 49 8B F1 0F 29 70 C8 49 8B F8 0F 29 78 B8 44 0F 29 40 A8 44 0F 28 C1 0F B6 45 "
    "7F 85 D2 74 25 FF CA 3B C2 0F 4E D0 44 39 51 1C 75 05 41 8B CA EB 07 8B 41 1C 48 8D 0C 83 48 63 C2 48 C1 E0 04 48 "
    "03 C1 EB 03 49 8B C2 41 83 CB FF 48 85 C0 74 08 8B 48 0C 41 3B CB 75 1B 48 8B 05 ?? ?? ?? ?? 8B 88 FC 03 00 00 49 "
    "8B C2 85 C9 74 04 48 8D 04 8B 8B 48 0C 48 8B 05 ?? ?? ?? ?? 0F B7 C9 44 39 54 C8 04 0F 84 74 02 00 00 8B 44 C8 "
    "04 4C 8D 0C 83 4D 85 C9 0F 84 63 02 00 00 41 8B 49 04 41 BF 04 00 00 00 41 3B CF 0F 8C 50 02 00 00 F3 0F 10 3D "
    "?? ?? ?? ?? 8D 41 FF 66 0F 6E C8 0F 5B C9 0F 28 C1 F3 41 0F 59 C0 F3 0F 5E F9 F3 0F 2C D0 44 0F B7 F2 66 85 D2 7E "
    "20 45 0F BF C6 41 8D 40 04 3B C1 7F 0A 0F BF C2 FF C8 44 3B C0 7E 0A 66 45 03 F3 66 45 85 F6 7F E0 4D 0F BF EE 48 "
    "85 FF 0F 84 EE 00 00 00 44 21 55 B3 4C 8D 45 CF 44 21 55 B7 4D 6B E5 1C C7 45 AF 00 00 80 3F 45 39 51 08 75 05 49 "
    "8B C2 EB 08 41 8B 41 08 48 8D 04 83 41 0F 10 44 04 0C 48 8D 55 AF 48 8D 4D BF F3 0F 7F 45 BF E8 ?? ?? ?? ?? 49 83 "
    "C4 1C 49 83 C0 0C 49 83 EF 01 75 C7";
// +0x214844, in +0x2146C0: the turned orientations through the cubic (+52).
inline constexpr char kBoomTrackForwardSig[] =
    "F3 0F 10 5D F3 F3 0F 10 55 E7 F3 0F 10 4D DB F3 0F 10 45 CF F3 44 0F 11 44 24 30 66 41 0F 6E F5 0F 5B F6 F3 0F "
    "11 7C 24 28 F3 0F 59 F7 F3 0F 11 74 24 20 E8 ?? ?? ?? ??";
// +0x2148E2, in +0x2146C0: the four points' positions (+0) through the cubic (+142).
inline constexpr char kBoomTrackPositionSig[] =
    "48 85 F6 0F 84 37 01 00 00 41 8B 49 08 4D 8B DA 85 C9 74 04 4C 8D 1C 8B 49 8D 45 03 4D 8B CA 4C 6B F8 1C 85 C9 "
    "74 04 4C 8D 0C 8B 49 8D 45 02 4D 8B C2 48 6B F8 1C 85 C9 74 04 4C 8D 04 8B 49 8D 45 01 48 6B D0 1C 85 C9 74 04 "
    "4C 8D 14 8B F3 43 0F 10 1C 1F F3 42 0F 10 14 0F F3 42 0F 10 0C 02 49 0F BF C6 48 6B C8 1C F3 44 0F 11 44 24 30 66 "
    "41 0F 6E F5 0F 5B F6 F3 0F 11 7C 24 28 F3 42 0F 10 04 11 F3 0F 59 F7 F3 0F 11 74 24 20 E8 ?? ?? ?? ?? F3 0F 11 06";
// +0x214D80 whole: the cubic, its third difference over 3 (+84).
inline constexpr char kBoomCubicSig[] =
    "48 8B C4 48 83 EC 48 F3 0F 10 6C 24 78 0F 29 78 E8 F3 0F 58 ED 44 0F 29 40 D8 0F 28 FA F3 0F 10 94 24 80 00 00 "
    "00 F3 0F 5C DF 44 0F 29 50 C8 44 0F 28 C2 44 0F 29 1C 24 44 0F 28 D1 44 0F 28 D8 F3 41 0F 5C FA F3 0F 10 44 24 78 "
    "F3 45 0F 5C D3 F3 0F 59 05 ?? ?? ?? ?? 0F 28 E5 F3 0F 58 64 24 70 F3 0F 5C DF 0F 28 CA F3 0F 5C 54 24 70 F3 41 "
    "0F 5C FA F3 44 0F 5C C4 F3 0F 5C DF F3 44 0F 59 C3 F3 44 0F 5E C0 F3 0F 10 44 24 70 F3 0F 58 44 24 78 F3 44 0F "
    "58 C7 0F 28 78 E8 F3 0F 5C C8 F3 44 0F 59 C1 F3 44 0F 5E C5 F3 45 0F 58 C2 44 0F 28 50 C8 F3 44 0F 59 C2 F3 44 "
    "0F 5E 44 24 78 F3 45 0F 58 C3 44 0F 28 1C 24 41 0F 28 C0 44 0F 28 40 D8 48 83 C4 48 C3";
// +0xA10EC whole: a vector turned by a quaternion (i, j, k, w), its "2w^2 - 1" (+190).
inline constexpr char kBoomRotateSig[] =
    "48 8B C4 48 83 EC 78 F3 0F 10 6A 08 4C 8D 18 F3 0F 10 61 08 0F 28 CD 0F 29 70 E8 0F 28 D5 F3 0F 10 72 04 0F 29 "
    "78 D8 0F 28 C6 F3 0F 10 3A 44 0F 29 40 C8 0F 28 DF F3 44 0F 10 41 04 44 0F 29 48 B8 44 0F 28 CF 44 0F 29 50 A8 F3 "
    "44 0F 10 11 F3 45 0F 59 CA F3 41 0F 59 C0 44 0F 29 58 98 F3 44 0F 10 59 0C 44 0F 29 24 24 F3 44 0F 58 C8 F3 0F 59 "
    "CC 45 0F 28 E3 F3 45 0F 59 E3 0F 28 C4 F3 0F 59 C6 F3 45 0F 58 DB F3 44 0F 58 C9 F3 41 0F 59 D0 41 0F 28 CA F3 0F "
    "59 DC F3 0F 5C D0 0F 28 C7 F3 45 0F 58 E4 F3 41 0F 59 F8 45 0F 28 43 C8 F3 45 0F 58 C9 F3 41 0F 59 D3 F3 44 0F 5C "
    "25 ?? ?? ?? ?? F3 41 0F 59 C9 F3 41 0F 59 C4 F3 0F 58 C8 41 0F 28 C2 F3 44 0F 59 D6 0F 28 70 E8 F3 0F 59 C5 F3 0F "
    "58 D1 41 0F 28 CC F3 44 0F 5C D7 0F 28 78 D8 F3 0F 5C D8 41 0F 28 C1 F3 41 0F 11 10 F3 0F 59 4A 04 F3 0F 59 41 04 "
    "F3 45 0F 59 D3 F3 41 0F 59 DB F3 0F 58 C8 45 0F 28 5B 98 F3 0F 58 D9 F3 41 0F 11 58 04 F3 44 0F 59 62 08 F3 44 0F "
    "59 49 08 F3 45 0F 58 E1 45 0F 28 4B B8 F3 45 0F 58 D4 45 0F 28 63 88 F3 45 0F 11 50 08 45 0F 28 53 A8 49 8B E3 C3";
// +0x187211, in +0x1871BC: the preferences' base (+6), 0x21C a controller, beside the abstraction's 0x518.
inline constexpr char kBoomPadHeadSig[] =
    "8B 42 34 4C 8D 3D ?? ?? ?? ?? 48 63 F1 48 69 DE 18 05 00 00 48 69 EE 1C 02 00 00 42 89 84 3B 84 14 00 00 8B 42 "
    "38 42 89 84 3B 88 14 00 00 42 C6 84 3B A4 14 00 00 00 41 80 7C 2F 14 00";
// +0x1875CA, in +0x1871BC: the second pitch (+0x149C) negated by +0x202, the look pitch (+0x1498) by +0x200.
inline constexpr char kBoomPadInvertSig[] =
    "41 80 BC 2F 02 02 00 00 00 74 06 41 0F 28 C4 EB 04 41 0F 28 C1 F3 42 0F 10 8C 3B 98 14 00 00 F3 0F 59 C1 F3 42 "
    "0F 11 84 3B 9C 14 00 00 41 80 BC 2F 00 02 00 00 00 75 04 45 0F 28 E1 F3 44 0F 59 E1 F3 46 0F 11 A4 3B 98 14 00 00";
// +0xF43CA, in +0xF42D8: the builder's state, image base (+10) plus an image offset (+25), 0x518 a controller.
inline constexpr char kBoomBuilderStateSig[] =
    "49 69 CF 18 05 00 00 48 8D 05 ?? ?? ?? ?? 4C 89 7C 24 60 4D 8B EF 4C 8D B8 ?? ?? ?? ?? 48 8B 85 60 01 00 00 4C 03 "
    "F9 8B 8D 48 01 00 00";
// +0xF48D8, in +0xF42D8: the second pitch (+0x508) where the pilot test (+7) says so, else the look pitch (+0x504).
inline constexpr char kBoomBuilderPitchSig[] =
    "8B 8D 40 01 00 00 E8 ?? ?? ?? ?? 84 C0 74 0B F3 41 0F 10 B7 08 05 00 00 EB 09 F3 41 0F 10 B7 04 05 00 00";
// +0xF91F4 whole: the pilot test. The seat's flags bit 2 and the parent's physics type in 0x94 (its call at +197).
inline constexpr char kBoomPilotSig[] =
    "48 89 5C 24 08 57 48 83 EC 20 8B 15 ?? ?? ?? ?? 45 33 C9 65 48 8B 04 25 58 00 00 00 41 83 CA FF 4C 8B 04 D0 48 63 "
    "C1 48 69 C8 10 01 00 00 BA C0 00 00 00 4A 8B 04 02 8B 94 08 C0 00 00 00 41 3B D2 0F 84 BA 00 00 00 B8 38 00 00 00 "
    "4A 8B 04 00 4C 8B 40 48 0F B7 C2 48 8D 0C 40 49 8B 44 C8 10 44 39 50 10 0F 84 97 00 00 00 0F B7 98 4E 02 00 00 66 "
    "41 3B DA 0F 84 86 00 00 00 0F B7 40 10 41 8D 79 01 48 8D 0C 40 8B C7 49 8B 54 C8 10 8A 8A 96 00 00 00 D3 E0 D1 E8 "
    "40 84 C7 74 64 0F B7 0A 48 8B 05 ?? ?? ?? ?? 4C 8B 1D ?? ?? ?? ?? 44 39 4C C8 04 75 05 45 8B D1 EB 08 8B 44 C8 04 "
    "4D 8D 14 83 8B 0A E8 ?? ?? ?? ?? 8B C8 8B C7 D3 E0 A8 94 74 2E 41 8B 82 5C 02 00 00 49 8B D1 85 C0 74 04 49 8D 14 "
    "83 48 0F BF C3 48 69 C8 D4 00 00 00 45 0F B6 C9 8B 04 11 C1 E8 02 40 84 C7 44 0F 45 CF 48 8B 5C 24 30 41 8A C1 48 "
    "83 C4 20 5F C3";

struct Halo3VehicleBoomBinding
{
    // The module generation the parts below were proven in; 0 = nothing bound. Written last, cleared first.
    std::atomic<uint32_t> generation{0};
    bool seatCamera = false;
    bool limits = false;
    float flatMin = -vehicle_boom::kFlatLookLimit, flatMax = vehicle_boom::kFlatLookLimit;
    bool track = false;
    void* const* globals = nullptr; // the slot holding the game globals tag (its default track)
    const uint8_t* preferences = nullptr; // controller 0's
    bool pilot = false;
};
Halo3VehicleBoomBinding g_halo3VehicleBoom;

namespace
{
size_t Halo3BoomPatternBytes(const char* pattern)
{
    return (strlen(pattern) + 1) / 3;
}

// The function an address lies in, by its unwind entry; 0 when the module has none for it.
uintptr_t Halo3BoomFunctionOf(uintptr_t base, uintptr_t at)
{
    DWORD64 image = 0;
    const RUNTIME_FUNCTION* entry = at ? RtlLookupFunctionEntry(at, &image, nullptr) : nullptr;
    return entry && image == base ? base + entry->BeginAddress : 0;
}

// Some call in the function at `function` lands on `target`.
bool Halo3BoomFunctionCalls(uintptr_t base, uintptr_t function, uintptr_t target)
{
    DWORD64 image = 0;
    const RUNTIME_FUNCTION* entry = function ? RtlLookupFunctionEntry(function, &image, nullptr) : nullptr;
    if (!entry || image != base || base + entry->BeginAddress != function || !target)
        return false;
    const uintptr_t end = base + entry->EndAddress;
    for (uintptr_t at = function; at + 5 <= end; ++at)
        if (*reinterpret_cast<const uint8_t*>(at) == 0xE8 &&
            at + 5 + *reinterpret_cast<const int32_t*>(at + 1) == target)
            return true;
    return false;
}

bool Halo3BoomReadFloat(uintptr_t at, float& out)
{
    __try
    {
        out = *reinterpret_cast<const float*>(at);
        return std::isfinite(out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

// At the Halo 3 install, after the tag tables and the type accessor are resolved (they are cross-checked here).
void BindHalo3VehicleBoom(uintptr_t base, size_t size, uint32_t generation)
{
    Halo3VehicleBoomBinding& boom = g_halo3VehicleBoom;
    boom.generation.store(0, std::memory_order_release);
    boom.seatCamera = boom.limits = boom.track = boom.pilot = false;
    boom.flatMin = -vehicle_boom::kFlatLookLimit;
    boom.flatMax = vehicle_boom::kFlatLookLimit;
    boom.globals = nullptr;
    boom.preferences = nullptr;
    if (!generation)
        return;
    const auto inside = [base, size](uintptr_t at, size_t bytes) { return at >= base && at + bytes <= base + size; };
    const auto rip = [&](uintptr_t at, size_t disp, size_t end) -> uintptr_t {
        return at ? sig::RipTarget(at + disp, at + end) : 0;
    };
    const auto here = [&](uintptr_t at, const char* pattern) {
        const size_t bytes = Halo3BoomPatternBytes(pattern);
        return at && inside(at, bytes) && sig::Find(at, bytes, pattern) == at;
    };
    const auto value = [&](uintptr_t at, float want) {
        float got = 0.0f;
        return at && inside(at, 4) && Halo3BoomReadFloat(at, got) && got == want;
    };
    const uintptr_t tagBase = reinterpret_cast<uintptr_t>(g_halo3TagDataBase);
    const uintptr_t instances = reinterpret_cast<uintptr_t>(g_halo3TagInstanceTable);

    // THE SEAT CAMERA.
    const uintptr_t seatSite = mcc_clock::FindUnique(base, size, kBoomSeatCameraSig);
    const uintptr_t seatFn = Halo3BoomFunctionOf(base, seatSite);
    const char* seatWhy = !seatSite ? "its signature"
                        : !seatFn ? "its function"
                        : !tagBase || !instances || rip(seatSite, 12, 16) != tagBase || rip(seatSite, 22, 26) != instances
                            ? "its tag tables against the mod's"
                            : nullptr;
    boom.seatCamera = !seatWhy;

    // THE LOOK LIMITS.
    const char* limitsWhy = nullptr;
    if (!boom.seatCamera)
        limitsWhy = "the seat camera";
    else
    {
        const uintptr_t head = mcc_clock::FindUnique(base, size, kBoomLookHeadSig);
        const uintptr_t clamp = mcc_clock::FindUnique(base, size, kBoomLookClampSig);
        const uintptr_t update = Halo3BoomFunctionOf(base, head);
        const uintptr_t upper = rip(head, 75, 79), lower = rip(head, 100, 104);
        float hi = 0.0f, lo = 0.0f;
        if (!head || !clamp)
            limitsWhy = "its signatures";
        else if (!update || Halo3BoomFunctionOf(base, clamp) != update)
            limitsWhy = "its two sites in one function";
        else if (rip(head, 105, 109) != seatFn)
            limitsWhy = "its call to the seat camera";
        else if (rip(clamp, 69, 73) != lower || rip(clamp, 88, 92) != lower)
            limitsWhy = "its clamp's lower limit";
        else if (!inside(upper, 4) || !inside(lower, 4) || !Halo3BoomReadFloat(upper, hi) ||
                 !Halo3BoomReadFloat(lower, lo) || !(lo < 0.0f && hi > 0.0f && hi <= vehicle_boom::kTrackPitchLimit &&
                                                     lo >= -vehicle_boom::kTrackPitchLimit))
            limitsWhy = "its two limits";
        else
        {
            boom.flatMin = lo;
            boom.flatMax = hi;
            boom.limits = true;
        }
    }

    // THE TRACK.
    const char* trackWhy = nullptr;
    if (!boom.seatCamera)
        trackWhy = "the seat camera";
    else
    {
        const uintptr_t pitch = mcc_clock::FindUnique(base, size, kBoomFollowPitchSig);
        const uintptr_t offset = mcc_clock::FindUnique(base, size, kBoomFollowOffsetSig);
        const uintptr_t follow = Halo3BoomFunctionOf(base, pitch);
        const uintptr_t solver = rip(pitch, 44, 48), track = rip(pitch, 68, 72);
        const uintptr_t forward = mcc_clock::FindUnique(base, size, kBoomTrackForwardSig);
        const uintptr_t place = mcc_clock::FindUnique(base, size, kBoomTrackPositionSig);
        // A decoded address is read only once its own signature has matched in place inside the module.
        const uintptr_t cubic = rip(forward, 52, 56);
        if (!pitch || !offset || !follow || Halo3BoomFunctionOf(base, offset) != follow)
            trackWhy = "the following camera's two sites";
        else if (!Halo3BoomFunctionCalls(base, follow, seatFn))
            trackWhy = "the following camera's call to the seat camera";
        else if (!here(solver, kBoomSolverSig) || rip(solver, 90, 94) != track || !value(rip(solver, 42, 46), 1.0f) ||
                 !value(rip(solver, 82, 86), vehicle_boom::kSolveHalf) ||
                 !value(rip(solver, 166, 170), vehicle_boom::kSolveEpsilon))
            trackWhy = "the pitch solver";
        else if (!here(track, kBoomTrackSig) || rip(track, 44, 48) != tagBase || rip(track, 166, 170) != instances)
            trackWhy = "the track";
        else if (!forward || !place || Halo3BoomFunctionOf(base, forward) != track ||
                 Halo3BoomFunctionOf(base, place) != track || rip(place, 142, 146) != cubic ||
                 !here(cubic, kBoomCubicSig) || !value(rip(cubic, 84, 88), vehicle_boom::kCubicThree))
            trackWhy = "the track's cubic";
        else if (!here(rip(track, 370, 374), kBoomRotateSig) || !value(rip(rip(track, 370, 374), 190, 194), 1.0f))
            trackWhy = "the track's rotation";
        else if (!inside(rip(track, 139, 143), sizeof(void*)))
            trackWhy = "the game globals' default track";
        else
        {
            boom.globals = reinterpret_cast<void* const*>(rip(track, 139, 143));
            boom.track = true;
        }
    }

    // THE INVERSION.
    const char* invertWhy = nullptr;
    const char* pilotWhy = nullptr;
    {
        const uintptr_t head = mcc_clock::FindUnique(base, size, kBoomPadHeadSig);
        const uintptr_t invert = mcc_clock::FindUnique(base, size, kBoomPadInvertSig);
        const uintptr_t pad = Halo3BoomFunctionOf(base, head);
        const uintptr_t preferences = rip(head, 6, 10);
        const uintptr_t state = mcc_clock::FindUnique(base, size, kBoomBuilderStateSig);
        const uintptr_t choice = mcc_clock::FindUnique(base, size, kBoomBuilderPitchSig);
        const uintptr_t builder = Halo3BoomFunctionOf(base, state);
        if (!head || !invert || !pad || Halo3BoomFunctionOf(base, invert) != pad)
            invertWhy = "the gamepad abstraction's two sites";
        else if (!inside(preferences, kHalo3PadPreferencesStride))
            invertWhy = "the controller preferences";
        else if (!state || !choice || !builder || Halo3BoomFunctionOf(base, choice) != builder ||
                 rip(state, 10, 14) != base ||
                 base + *reinterpret_cast<const uint32_t*>(state + 25) + kHalo3BuilderPitchOffset !=
                     preferences + kHalo3PadPitchOffset)
            invertWhy = "the player-control builder's read of the abstraction's pitch";
        else
        {
            boom.preferences = reinterpret_cast<const uint8_t*>(preferences);
            const uintptr_t pilot = rip(choice, 7, 11);
            if (!here(pilot, kBoomPilotSig))
                pilotWhy = "its signature";
            else if (!g_halo3VehicleTypeAccessor ||
                     rip(pilot, 197, 201) != reinterpret_cast<uintptr_t>(g_halo3VehicleTypeAccessor))
                pilotWhy = "its type accessor against the mod's";
            else
                boom.pilot = true;
        }
    }

    boom.generation.store(generation, std::memory_order_release);
    if (seatWhy)
        LOG("Halo 3 third-person boom: the seat camera (+0x355898) did not prove (%s); the boom orbits the game's "
            "pivot within the flat game's own +-85.5 deg, and is not inverted unless vehicle_boom_invert = 1",
            seatWhy);
    else
        LOG("Halo 3 third-person boom: the seat camera at +0x%llX; look limits %s; camera track %s; look inversion %s",
            static_cast<unsigned long long>(seatFn - base),
            limitsWhy ? "not proven (the flat game's own +-85.5 deg are used)" : "from the seat camera, held to the look "
                                                                                 "update's own (+0xF7DF0)",
            trackWhy ? "not proven: the boom orbits the game's pivot at its own distance" : "proven (+0x213CF4)",
            invertWhy ? "not proven: not inverted unless vehicle_boom_invert = 1"
                      : (pilotWhy ? "from the game's look setting (the pilot test did not prove: a pilot takes it too)"
                                  : "from the game's look and flight settings (+0x1871BC, +0xF91F4)"));
    if (!seatWhy && limitsWhy)
        LOG("Halo 3 third-person boom: the look limits did not prove (%s)", limitsWhy);
    if (!seatWhy && trackWhy)
        LOG("Halo 3 third-person boom: the camera track did not prove (%s)", trackWhy);
    if (invertWhy)
        LOG("Halo 3 third-person boom: the game's look inversion did not prove (%s)", invertWhy);
    else if (pilotWhy)
        LOG("Halo 3 third-person boom: the pilot test did not prove (%s)", pilotWhy);
}

// Before halo3.dll goes. The cold worker's reads check the generation first, and are guarded.
void RetireHalo3VehicleBoom()
{
    g_halo3VehicleBoom.generation.store(0, std::memory_order_release);
}

bool Halo3VehicleBoomBound()
{
    const uint32_t generation = g_halo3VehicleBoom.generation.load(std::memory_order_acquire);
    return generation && generation == g_halo3RuntimeGeneration.load(std::memory_order_relaxed);
}

// WHAT A SEAT'S CAMERA IS, from the loaded tags: the seat's flags, its camera's look range, and its track (the first,
// as the game takes it outside the two states that ask for its second; the second is kept for the log's check).
struct Halo3BoomSeatFacts
{
    bool read = false;
    uint32_t flags = 0;
    float rangeMin = 0.0f, rangeMax = 0.0f; // radians; both 0 = none of its own
    int32_t tracks = 0;                      // tracks the seat camera lists
    bool defaultTrack = false;               // it lists none: the game globals' default
    int32_t points = 0;                      // control points in the first track (0: none read)
    vehicle_boom::Track track{};
    vehicle_boom::Track second{};
    int physicsType = -1;
};

namespace
{
// One track's control points, by its 'trak' tag index. Guarded by the caller.
void Halo3BoomCopyTrack(const unsigned char* tagBase, uint32_t trak, vehicle_boom::Track& out, int32_t* points)
{
    out.count = 0;
    if ((trak & 0xFFFFu) == 0xFFFFu)
        return;
    const unsigned char* definition = Halo3LoadedTagDefinition(trak);
    if (!definition)
        return;
    const int32_t count = *reinterpret_cast<const int32_t*>(definition + kHalo3TrackPointsCountOffset);
    const uint32_t address = *reinterpret_cast<const uint32_t*>(definition + kHalo3TrackPointsAddressOffset);
    if (points)
        *points = count;
    if (count < vehicle_boom::kMinTrackPoints || count > vehicle_boom::kMaxTrackPoints || !address)
        return;
    const unsigned char* point = tagBase + static_cast<size_t>(address) * 4;
    for (int32_t i = 0; i < count; ++i, point += kHalo3TrackPointStride)
    {
        memcpy(out.points[i].position, point, sizeof(out.points[i].position));
        memcpy(out.points[i].orientation, point + kHalo3TrackPointOrientationOffset, sizeof(out.points[i].orientation));
    }
    out.count = count;
}

bool Halo3BoomReadSeatGuarded(uint32_t definitionIndex, int seatIndex, void* const* globals, bool withTrack,
                              Halo3BoomSeatFacts& out)
{
    __try
    {
        const unsigned char* tagBase =
            g_halo3TagDataBase ? static_cast<const unsigned char*>(*g_halo3TagDataBase) : nullptr;
        if (!tagBase || definitionIndex > 0xFFFFu || seatIndex < 0 || seatIndex > kHalo3VehicleSeatMax)
            return false;
        const unsigned char* definition = Halo3LoadedTagDefinition(definitionIndex);
        if (!definition)
            return false;
        const int32_t seats = *reinterpret_cast<const int32_t*>(definition + kHalo3VehicleSeatsBlockOffset);
        const uint32_t seatsAddress = *reinterpret_cast<const uint32_t*>(definition + kHalo3VehicleSeatsBlockOffset + 4);
        if (seats <= 0 || seats > 126 || seatIndex >= seats || !seatsAddress)
            return false;
        const unsigned char* seat =
            tagBase + static_cast<size_t>(seatsAddress) * 4 + static_cast<size_t>(seatIndex) * kHalo3VehicleSeatStride;
        const unsigned char* camera = seat + kHalo3SeatCameraOffset;
        out.flags = *reinterpret_cast<const uint32_t*>(seat);
        out.rangeMin = *reinterpret_cast<const float*>(camera + kHalo3CameraPitchMinOffset);
        out.rangeMax = *reinterpret_cast<const float*>(camera + kHalo3CameraPitchMaxOffset);
        out.tracks = *reinterpret_cast<const int32_t*>(camera + kHalo3CameraTracksCountOffset);
        out.read = true;
        if (!withTrack)
            return true;
        const uint32_t tracksAddress = *reinterpret_cast<const uint32_t*>(camera + kHalo3CameraTracksAddressOffset);
        const unsigned char* tracks =
            out.tracks > 0 && out.tracks <= 16 && tracksAddress ? tagBase + static_cast<size_t>(tracksAddress) * 4 : nullptr;
        uint32_t first = tracks ? *reinterpret_cast<const uint32_t*>(tracks + kHalo3CameraTrackTagOffset) : 0xFFFFFFFFu;
        if (first == 0xFFFFFFFFu && globals && *globals)
        {
            const uint32_t fallback = *reinterpret_cast<const uint32_t*>(
                static_cast<const unsigned char*>(*globals) + kHalo3GlobalsDefaultTrackOffset);
            if (fallback)
            {
                first = *reinterpret_cast<const uint32_t*>(tagBase + static_cast<size_t>(fallback) * 4 +
                                                            kHalo3CameraTrackTagOffset);
                out.defaultTrack = true;
            }
        }
        Halo3BoomCopyTrack(tagBase, first, out.track, &out.points);
        if (tracks && out.tracks >= 2)
            Halo3BoomCopyTrack(tagBase,
                               *reinterpret_cast<const uint32_t*>(tracks + kHalo3CameraTrackStride +
                                                                  kHalo3CameraTrackTagOffset),
                               out.second, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = Halo3BoomSeatFacts{};
        return false;
    }
}

bool Halo3BoomReadPreferences(const uint8_t* preferences, bool& look, bool& flight)
{
    __try
    {
        look = preferences[kHalo3PadLookInvertOffset] != 0;
        flight = preferences[kHalo3PadFlightInvertOffset] != 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

// Cold worker. The seat camera's facts for the seat of `definitionIndex`, `seatIndex`; the track only where the
// following camera's use of it is proven. False: nothing could be read (the boom orbits within +-85.5 deg).
bool Halo3VehicleBoomReadSeat(uint32_t definitionIndex, int seatIndex, Halo3BoomSeatFacts& out)
{
    out = Halo3BoomSeatFacts{};
    if (!Halo3VehicleBoomBound() || !g_halo3VehicleBoom.seatCamera)
        return false;
    if (!Halo3BoomReadSeatGuarded(definitionIndex, seatIndex, g_halo3VehicleBoom.globals, g_halo3VehicleBoom.track, out))
        return false;
    int32_t counts[10]{};
    float jeep[2]{}, tails[3]{}, scoutAccel = 0.0f;
    int32_t scoutSpecific = -1;
    if (Halo3ReadDefinitionFields(definitionIndex, counts, jeep, &scoutSpecific, &scoutAccel, tails))
    {
        out.physicsType = kHalo3VehicleTypeNoneAuthored;
        for (int type = 0; type < 10; ++type)
            if (counts[type] > 0)
            {
                out.physicsType = type;
                break;
            }
    }
    return true;
}

// The look range the flat game holds a seat camera's look to (+0xF7DF0), from its facts. The camera's two range
// fields are the ones the look update's clamp names: without that proof neither is used, only +-85.5 deg.
vehicle_boom::Range Halo3VehicleBoomRange(const Halo3BoomSeatFacts& facts)
{
    const Halo3VehicleBoomBinding& boom = g_halo3VehicleBoom;
    if (!Halo3VehicleBoomBound() || !boom.limits)
        return vehicle_boom::LookRange(0.0f, 0.0f);
    return facts.read ? vehicle_boom::LookRange(facts.rangeMin, facts.rangeMax, boom.flatMin, boom.flatMax)
                      : vehicle_boom::LookRange(0.0f, 0.0f, boom.flatMin, boom.flatMax);
}

// The player pilots an aircraft in this seat, as +0xF91F4 tells: its driver's seat on a flyer.
bool Halo3VehicleBoomPiloting(const Halo3BoomSeatFacts& facts)
{
    return facts.read && (facts.flags & kHalo3SeatDriverBit) != 0 && facts.physicsType >= 0 &&
           facts.physicsType < 32 && (kHalo3VehicleFlyingTypeMask & (1u << facts.physicsType)) != 0;
}

// Cold worker, every poll: the flat game's inversion of the right stick's Y in this seat. False: it could not be
// read, and the caller follows "not inverted".
bool Halo3VehicleBoomGameInverts(bool piloting, bool& inverts)
{
    inverts = false;
    if (!Halo3VehicleBoomBound() || !g_halo3VehicleBoom.preferences)
        return false;
    bool look = false, flight = false;
    if (!Halo3BoomReadPreferences(g_halo3VehicleBoom.preferences, look, flight))
        return false;
    inverts = vehicle_boom::GameInverts(look, flight, piloting && g_halo3VehicleBoom.pilot);
    return true;
}
