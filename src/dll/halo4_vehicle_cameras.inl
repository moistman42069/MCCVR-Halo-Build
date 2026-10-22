// VEHICLE CAMERAS IN HALO 4: Halo 4's side of vehicle_cameras.inl. Included by game.cpp inside the Halo 4 camera core,
// after the head-pose pieces it reads and before the stereo transaction that calls it.
//
// THREADS.
//   The stereo transaction (an engine thread: the game's own TLS is readable there) samples the seat once a frame,
//     publishes it for the cold worker, and, while a camera of the file is chosen for exactly that seat, moves the
//     eye the frame is drawn from before the head pose is composed onto it: a firstperson camera's "offset", or a
//     chase camera's place (Halo4VehicleCamerasPass). Its only reads of the camera in use are two atomic loads while
//     none is chosen.
//   The cold worker (vehicle_cameras.inl, VehicleCameras_ColdTick) reads the published seat (Halo4VehicleCameraSeat):
//     the vehicle's name, the seat's role, and the key every reader matches a camera to.
//   The first-person seat override (native_vehicle_first_person.cpp) asks Game_VehicleCameraChaseSeat for each seat
//     it would take over.
//
// WHAT A CHASE CAMERA CHANGES. The first-person seat override stands down for that seat, so the game's camera
// director gives the seat its own third-person camera and draws the occupant in it, as the flat game does. The eye
// the frame is drawn from stands at the camera's place; the head pose turns it and moves it inside the camera's
// leash, and its direction is the player's, as in every seat. The aim keeps working as in the seat view (the seat's
// view is the mod's: Halo4SeatViewOwned), and its intent ray starts at the eye, where the player sees from. The
// camera's mode turns the view: its rotationTracking yaw turns the heading the head-owned view is built on with the
// hull (Halo 4 has no view follow of its own). Its "collide" is traced by the seated trace (halo4_seat_converge.inl).
// Nothing else the game reads is written.
//
// "thirdperson": the game's own camera for the seat is its following camera, standing at the seat's pivot less its
// distance along the aim (halo4_seat_converge_logic.h). The pivot is not read: it is found where the camera's lines
// meet in the vehicle's frame (halo4_vehicle_camera::PivotFinder), and the camera stands there at the distance the
// game's camera keeps, orbiting it at the shared boom's elevation along the vehicle's heading (vehicle_boom_logic.h's
// orbit shape; Halo 4's camera track is not read).

// ---- the seat, sampled on the stereo transaction ----------------------------------------------------------------

bool Halo4SeatFlagsRead(uint32_t unit, uint32_t& flags); // after legacy_direct_drive.inl: the seat frame reader's read

struct Halo4VehicleCameraFrame
{
    bool active = false; // seated, and the seated object's root read this frame
    uint32_t generation = 0;
    uint32_t unit = UINT32_MAX, parent = UINT32_MAX, carrier = UINT32_MAX;
    int seatIndex = -1;
    uint64_t identity = 0, carrierIdentity = 0;
    uint32_t definition = 0xFFFFFFFFu;
    bool flagsKnown = false;
    uint32_t seatFlags = 0;
    // The seated object's root node as drawn, and the carrier's where a gun rides one.
    float rootPos[3]{}, rootForward[3]{}, rootLeft[3]{}, rootUp[3]{};
    bool carrierRootValid = false;
    float carrierPos[3]{}, carrierForward[3]{}, carrierLeft[3]{}, carrierUp[3]{};
    // The occupant's head marker as drawn, and the seat's point: where the head has settled in the seated object.
    bool headValid = false;
    float head[3]{}, headForward[3]{}, headUp[3]{};
    bool seatPointValid = false;
    float seatPoint[3]{};
};

// What does not change while the player sits in one seat, read once per seat (render thread): the vehicle's and its
// carrier's identities, its definition, and the seat's flags (tried again on later frames until they read).
struct Halo4VehicleCameraSeatCache
{
    uint32_t generation = 0, unit = UINT32_MAX, parent = UINT32_MAX;
    int seatIndex = -1;
    uint64_t identity = 0, carrierIdentity = 0;
    uint32_t carrier = UINT32_MAX, definition = 0xFFFFFFFFu;
    uint32_t identityTries = 0, flagTries = 0;
    bool flagsKnown = false;
    uint32_t seatFlags = 0;
    // The head in the seated object's own frame, eased: the seat's point.
    bool seatLocalValid = false;
    float seatLocal[3]{};
    LARGE_INTEGER seatAt{};
};
Halo4VehicleCameraSeatCache g_halo4VehicleCameraSeatCache;
inline constexpr uint32_t kHalo4SeatReadTries = 30;
inline constexpr float kHalo4SeatPointSmoothSeconds = 0.5f;
inline constexpr uint32_t kHalo4HeadMarker = 0x122; // the seat camera's own head query (native_vehicle_first_person)

float Halo4VehicleCameraSecondsSince(LARGE_INTEGER& at)
{
    static LARGE_INTEGER frequency{};
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const float seconds = at.QuadPart && frequency.QuadPart
        ? static_cast<float>(static_cast<double>(now.QuadPart - at.QuadPart) / static_cast<double>(frequency.QuadPart))
        : -1.0f;
    at = now;
    return seconds;
}

// The object data of `handle` among `kinds` (Halo4VehicleObject's mask), through this thread's engine TLS. Null when
// any link is missing. The caller guards the reads.
const uint8_t* Halo4VehicleCameraObject(uint32_t handle, uint32_t kinds)
{
    if (!g_halo4EngineTlsIndex || *g_halo4EngineTlsIndex >= 1088 || handle == UINT32_MAX)
        return nullptr;
    auto** slots = reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
    const uint8_t* tls = slots ? slots[*g_halo4EngineTlsIndex] : nullptr;
    if (!tls)
        return nullptr;
    return Halo4VehicleObject(Halo4VehicleRead<const uint8_t*>(tls, 0x18), handle, kinds);
}

// A node frame (scale, forward, left, up, position) into a frame's fields.
void Halo4VehicleCameraTakeFrame(const float m[13], float pos[3], float forward[3], float left[3], float up[3])
{
    memcpy(forward, m + 1, 12);
    memcpy(left, m + 4, 12);
    memcpy(up, m + 7, 12);
    memcpy(pos, m + 10, 12);
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// DEV: the vehicle's root as this thread's query draws it against as simulated: a moving vehicle shows the two apart
// when the query is interpolated here, as the camera's frame must be (the hull on screen is the drawn one). Counted
// by the render thread, reported by the cold worker.
std::atomic<uint32_t> g_halo4VehicleCameraDrawnSamples{0}, g_halo4VehicleCameraDrawnApart{0};
std::atomic<uint32_t> g_halo4VehicleCameraDrawnGapMaxMm{0};
void Halo4VehicleCameraNoteDrawn(uint32_t parent, const float drawn[13])
{
    float simulated[13]{};
    if (!NativeVehicleFirstPerson_Halo4MarkerFrame(parent, 0, false, simulated))
        return;
    const float d[3] = {drawn[10] - simulated[10], drawn[11] - simulated[11], drawn[12] - simulated[12]};
    const float gapMm = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * 304.8f;
    g_halo4VehicleCameraDrawnSamples.fetch_add(1, std::memory_order_relaxed);
    if (gapMm > 0.1f)
        g_halo4VehicleCameraDrawnApart.fetch_add(1, std::memory_order_relaxed);
    const uint32_t mm = std::isfinite(gapMm) ? static_cast<uint32_t>(gapMm < 1.0e8f ? gapMm : 1.0e8f) : 0u;
    if (mm > g_halo4VehicleCameraDrawnGapMaxMm.load(std::memory_order_relaxed))
        g_halo4VehicleCameraDrawnGapMaxMm.store(mm, std::memory_order_relaxed);
}
#endif

// Render thread, once a frame of a seat: everything the vehicle cameras read of it. `seat` is the frame's own seat
// read (the stereo transaction's).
bool Halo4SampleVehicleCameraFrame(const Halo4VehicleInputState& seat, Halo4VehicleCameraFrame& frame)
{
    frame = Halo4VehicleCameraFrame{};
    if (!seat.seated || seat.parent == UINT32_MAX || seat.seat < 0)
        return false;
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
    Halo4VehicleCameraSeatCache& cache = g_halo4VehicleCameraSeatCache;
    if (cache.generation != generation || cache.unit != seat.unit || cache.parent != seat.parent ||
        cache.seatIndex != seat.seat)
    {
        cache = Halo4VehicleCameraSeatCache{};
        cache.generation = generation;
        cache.unit = seat.unit;
        cache.parent = seat.parent;
        cache.seatIndex = seat.seat;
    }
    if (!cache.identity && cache.identityTries < kHalo4SeatReadTries)
    {
        ++cache.identityTries;
        __try
        {
            const uint8_t* parent = Halo4VehicleCameraObject(seat.parent, 0x2003);
            if (parent)
            {
                cache.identity = Halo4ReadVehicleIdentity(parent);
                cache.definition = Halo4VehicleRead<uint16_t>(parent, 0x8);
                const uint32_t carrier = Halo4VehicleRead<uint32_t>(parent, 0x24);
                const uint8_t* carrierData = carrier != UINT32_MAX ? Halo4VehicleCameraObject(carrier, 0x2003) : nullptr;
                cache.carrier = carrierData ? carrier : UINT32_MAX;
                cache.carrierIdentity = carrierData ? Halo4ReadVehicleIdentity(carrierData) : 0;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            cache.identity = 0;
        }
    }
    if (!cache.flagsKnown && cache.flagTries < kHalo4SeatReadTries)
    {
        ++cache.flagTries;
        uint32_t flags = 0;
        if (Halo4SeatFlagsRead(seat.unit, flags))
        {
            cache.flagsKnown = true;
            cache.seatFlags = flags;
        }
    }
    float root[13]{};
    if (!NativeVehicleFirstPerson_Halo4MarkerFrame(seat.parent, 0, true, root))
        return false;
#if HALOMCCVR_DEV_DIAGNOSTICS
    Halo4VehicleCameraNoteDrawn(seat.parent, root);
#endif
    frame.generation = generation;
    frame.unit = seat.unit;
    frame.parent = seat.parent;
    frame.carrier = cache.carrier;
    frame.seatIndex = seat.seat;
    frame.identity = cache.identity;
    frame.carrierIdentity = cache.carrierIdentity;
    frame.definition = cache.definition;
    frame.flagsKnown = cache.flagsKnown;
    frame.seatFlags = cache.seatFlags;
    Halo4VehicleCameraTakeFrame(root, frame.rootPos, frame.rootForward, frame.rootLeft, frame.rootUp);
    float carrierRoot[13]{};
    if (cache.carrier != UINT32_MAX && NativeVehicleFirstPerson_Halo4MarkerFrame(cache.carrier, 0, true, carrierRoot))
    {
        frame.carrierRootValid = true;
        Halo4VehicleCameraTakeFrame(carrierRoot, frame.carrierPos, frame.carrierForward, frame.carrierLeft,
                                    frame.carrierUp);
    }
    float head[13]{}, headLeft[3]{};
    if (NativeVehicleFirstPerson_Halo4MarkerFrame(seat.unit, kHalo4HeadMarker, true, head))
    {
        frame.headValid = true;
        Halo4VehicleCameraTakeFrame(head, frame.head, frame.headForward, headLeft, frame.headUp);
        // THE SEAT'S POINT: the head as it settles in the seated object's frame, eased, so a boarding animation or
        // a bounce does not carry a camera measured from the seat with it.
        vehicle_camera_frame::Basis seated{};
        if (vehicle_camera_frame::FromForwardUp(frame.rootForward, frame.rootUp, seated))
        {
            const float d[3] = {frame.head[0] - frame.rootPos[0], frame.head[1] - frame.rootPos[1],
                                frame.head[2] - frame.rootPos[2]};
            const float local[3] = {vehicle_camera_frame::Dot(d, seated.forward), vehicle_camera_frame::Dot(d, seated.left),
                                    vehicle_camera_frame::Dot(d, seated.up)};
            const float seconds = Halo4VehicleCameraSecondsSince(cache.seatAt);
            if (cache.seatLocalValid && seconds >= 0.0f && seconds <= 0.5f)
            {
                const float blend = 1.0f - std::exp(-seconds / kHalo4SeatPointSmoothSeconds);
                for (int axis = 0; axis < 3; ++axis)
                    cache.seatLocal[axis] += (local[axis] - cache.seatLocal[axis]) * blend;
            }
            else
            {
                memcpy(cache.seatLocal, local, sizeof(local));
            }
            cache.seatLocalValid = vehicle_camera_place::Finite3(cache.seatLocal);
        }
    }
    if (cache.seatLocalValid)
    {
        vehicle_camera_frame::Basis seated{};
        if (vehicle_camera_frame::FromForwardUp(frame.rootForward, frame.rootUp, seated))
        {
            for (int axis = 0; axis < 3; ++axis)
                frame.seatPoint[axis] = frame.rootPos[axis] + seated.forward[axis] * cache.seatLocal[0] +
                                        seated.left[axis] * cache.seatLocal[1] + seated.up[axis] * cache.seatLocal[2];
            frame.seatPointValid = vehicle_camera_place::Finite3(frame.seatPoint);
        }
    }
    frame.active = true;
    return true;
}

// ---- the seat, published for the cold worker --------------------------------------------------------------------

struct Halo4VehicleCameraSeatRecord
{
    std::atomic<uint32_t> seq{0}; // even = stable
    std::atomic<uint64_t> ms{0};  // the last publication; 0 = never
    bool seated = false;
    uint32_t generation = 0;
    uint32_t parent = UINT32_MAX;
    int32_t seatIndex = -1;
    uint64_t identity = 0, carrierIdentity = 0;
    bool onCarrier = false;
    bool flagsKnown = false;
    uint32_t seatFlags = 0;
    uint32_t definition = 0xFFFFFFFFu;
    bool nativeChase = false; // the game's own camera for the seat is its following camera
    bool identityPending = false; // the vehicle's identity has not read yet, and will be tried again
};
Halo4VehicleCameraSeatRecord g_halo4VehicleCameraSeat;
inline constexpr uint64_t kHalo4VehicleCameraSeatFreshMs = 250;
// A record this old says nothing at all: the transaction has not run (a menu, a load, a teardown) and the cold worker
// takes the player as out of the seat.
inline constexpr uint64_t kHalo4VehicleCameraSeatGoneMs = 2000;

// The seat's key as every reader of the published camera matches it (VehicleCameraSeatKey): the render model's
// checksum stands for the identity.
VehicleCameraSeatKey Halo4VehicleCameraKey(uint32_t generation, uint32_t parent, int seatIndex, uint64_t identity,
                                           bool onCarrier)
{
    return VehicleCameraSeatKey{generation, static_cast<int32_t>(parent), seatIndex,
                                static_cast<uint32_t>(identity & 0xFFFFFFFFull), onCarrier};
}
VehicleCameraSeatKey Halo4VehicleCameraKey(const Halo4VehicleCameraFrame& frame)
{
    return Halo4VehicleCameraKey(frame.generation, frame.parent, frame.seatIndex, frame.identity,
                                 frame.carrier != UINT32_MAX);
}

void Halo4VehicleCameraPublishSeat(bool seatRead, const Halo4VehicleCameraFrame& frame, bool nativeChase)
{
    if (!seatRead)
        return; // an unread frame says nothing: the cold worker sees the record age
    Halo4VehicleCameraSeatRecord& record = g_halo4VehicleCameraSeat;
    record.seq.fetch_add(1, std::memory_order_acq_rel);
    record.seated = frame.active;
    record.generation = frame.active ? frame.generation : TitleAdapter_GetGeneration(GameTitle::Halo4);
    record.parent = frame.parent;
    record.seatIndex = frame.seatIndex;
    record.identity = frame.identity;
    record.carrierIdentity = frame.carrierIdentity;
    record.onCarrier = frame.carrier != UINT32_MAX;
    record.flagsKnown = frame.flagsKnown;
    record.seatFlags = frame.seatFlags;
    record.definition = frame.definition;
    record.nativeChase = nativeChase;
    record.identityPending = frame.active && !frame.identity &&
                             g_halo4VehicleCameraSeatCache.identityTries < kHalo4SeatReadTries;
    record.ms.store(GetTickCount64(), std::memory_order_relaxed);
    record.seq.fetch_add(1, std::memory_order_release);
}

// False: the record was being written both times, or has never been written. `ageMs` how old it is.
bool Halo4VehicleCameraReadSeat(Halo4VehicleCameraSeatRecord& out, uint64_t& ageMs)
{
    const Halo4VehicleCameraSeatRecord& record = g_halo4VehicleCameraSeat;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const uint32_t before = record.seq.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        const uint64_t ms = record.ms.load(std::memory_order_relaxed);
        out.seated = record.seated;
        out.generation = record.generation;
        out.parent = record.parent;
        out.seatIndex = record.seatIndex;
        out.identity = record.identity;
        out.carrierIdentity = record.carrierIdentity;
        out.onCarrier = record.onCarrier;
        out.flagsKnown = record.flagsKnown;
        out.seatFlags = record.seatFlags;
        out.definition = record.definition;
        out.nativeChase = record.nativeChase;
        out.identityPending = record.identityPending;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (record.seq.load(std::memory_order_relaxed) != before)
            continue;
        const uint64_t now = GetTickCount64();
        if (!ms || now < ms)
            return false;
        ageMs = now - ms;
        return true;
    }
    return false;
}

// ---- the camera chosen for this seat -----------------------------------------------------------------------------

// A chase camera is chosen for the seat (generation, vehicle, seat): one atomic load while none is. Any thread.
bool Halo4VehicleCameraChaseChosen(uint32_t generation, uint32_t parent, int seatIndex)
{
    vehicle_camera_select::ActiveCamera camera;
    VehicleCameraSeatKey chosenFor;
    return VehicleCameraRead(camera, chosenFor) && chosenFor.generation == generation &&
           chosenFor.vehicle == static_cast<int32_t>(parent) && chosenFor.seatIndex == seatIndex;
}

// The stereo transaction stood the eye at a chase camera's place in this seat (render thread writes, any thread
// reads): the seat's view is the mod's, as in the first-person seat view (Halo4SeatViewOwned).
std::atomic<uint64_t> g_halo4VehicleCameraPlacedMs{0};
std::atomic<uint32_t> g_halo4VehicleCameraPlacedParent{UINT32_MAX};
std::atomic<int32_t> g_halo4VehicleCameraPlacedSeat{-1};
inline constexpr uint64_t kHalo4VehicleCameraPlacedFreshMs = 250;

bool Halo4VehicleCameraOwnsSeatView(uint32_t parent, int seat) noexcept
{
    const uint64_t at = g_halo4VehicleCameraPlacedMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    return at && now >= at && now - at <= kHalo4VehicleCameraPlacedFreshMs &&
           g_halo4VehicleCameraPlacedParent.load(std::memory_order_relaxed) == parent &&
           g_halo4VehicleCameraPlacedSeat.load(std::memory_order_relaxed) == seat;
}

// ---- where a chase camera stands ---------------------------------------------------------------------------------

// Counted by the render thread, taken by the cold worker for its log lines (Halo4VehicleCamerasColdLog).
std::atomic<uint32_t> g_halo4VehicleCameraFrames{0};        // chase placements
std::atomic<uint32_t> g_halo4VehicleCameraStandInFrames{0}; // of them, "thirdperson" at the mod's stand-in
std::atomic<uint32_t> g_halo4VehicleCameraNudgeFrames{0};   // firstperson eyes moved by an offset
std::atomic<uint32_t> g_halo4VehicleCameraHeadMisses{0};    // "playerhead" with no head marker read
std::atomic<uint32_t> g_halo4VehicleCameraNoRootFrames{0};  // a chosen camera with the vehicle's root unread
std::atomic<uint32_t> g_halo4VehicleCameraLagTrailCm{0};
std::atomic<uint32_t> g_halo4VehicleCameraTurnFrames{0};    // frames the view's heading turned with the hull
std::atomic<float> g_halo4VehicleCameraRestWu{0.0f};    // the game's camera's resting distance in use (0 = none yet)
std::atomic<float> g_halo4VehicleCameraRestRiseWu{0.0f}; // of it, the height above its pivot
std::atomic<float> g_halo4VehicleCameraRestPitchDeg{0.0f};
std::atomic<uint32_t> g_halo4VehicleCameraPivotSolved{0}; // 1: the pivot is where the game's camera lines met
std::atomic<float> g_halo4VehicleCameraPivot[3]{};        // the pivot, in the vehicle's frame (world units)
std::atomic<float> g_halo4VehicleCameraPivotResidualWu{0.0f};
std::atomic<uint32_t> g_halo4VehicleCameraPivotSamples{0};
// Where the game's own camera stands looking level, as an elevation above level from its pivot in whole degrees, for
// the boom's start (vehicle_cameras.inl's VehicleBoomColdTick), with the seat it was measured in. Render thread
// writes (the only writer), cold worker reads through the sequence.
struct Halo4VehicleCameraLevel
{
    std::atomic<uint32_t> seq{0}; // even = stable
    VehicleCameraSeatKey seat{};
    float deg = vehicle_boom::kBoomPitchUnset;
};
Halo4VehicleCameraLevel g_halo4VehicleCameraLevel;

void Halo4VehicleCameraLevelStore(const VehicleCameraSeatKey& seat, float deg)
{
    Halo4VehicleCameraLevel& level = g_halo4VehicleCameraLevel;
    level.seq.fetch_add(1, std::memory_order_acq_rel);
    level.seat = seat;
    level.deg = deg;
    level.seq.fetch_add(1, std::memory_order_release);
}

// Cold worker: the level view measured in `seat`; unset when the measurement in hand is another seat's. False: the
// record was being written both times it was read.
bool Halo4VehicleCameraLevelElevationDeg(const VehicleCameraSeatKey& seat, float& deg)
{
    const Halo4VehicleCameraLevel& level = g_halo4VehicleCameraLevel;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const uint32_t before = level.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const VehicleCameraSeatKey measuredIn = level.seat;
        const float measured = level.deg;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (level.seq.load(std::memory_order_relaxed) != before)
            continue;
        deg = measuredIn == seat ? measured : vehicle_boom::kBoomPitchUnset;
        return true;
    }
    return false;
}

vehicle_camera_place::State g_halo4VehicleCameraState;  // render thread
vehicle_camera_place::LagState g_halo4VehicleCameraLag; // render thread
LARGE_INTEGER g_halo4VehicleCameraLagAt{};              // render thread

// This frame's placement, for the leash on the head (Halo4VehicleCameraLeashHead), later in the same transaction.
struct Halo4VehicleCameraPassRecord
{
    bool placed = false;
    float reach = 1.0f; // how much of the way out the wall let the camera go
    vehicle_camera_select::ActiveCamera camera{};
    vehicle_camera_place::Placed where{};
    // For the aim ring (Halo4VehicleAimMarkerStep): the seat the camera was placed for, the seat's own eye (the
    // occupant's head: where a point the hand can aim at lies), the way the vehicle faces, and the way the game's own
    // camera looked this frame (its aim pitch).
    VehicleCameraSeatKey seat{};
    float seatEye[3]{};
    float vehicleForward[3]{};
    bool looksValid = false;
    float looks[3]{};
};
Halo4VehicleCameraPassRecord g_halo4VehicleCameraPass; // render thread

// "collide": frames with a fresh trace, frames held short, frames with no trace to go on; and how much of the way out
// the camera may go, eased.
std::atomic<uint32_t> g_halo4VehicleCameraCollideFrames{0};
std::atomic<uint32_t> g_halo4VehicleCameraHeldFrames{0};
std::atomic<uint32_t> g_halo4VehicleCameraNoTraceFrames{0};
float g_halo4VehicleCameraReach = 1.0f;      // render thread
LARGE_INTEGER g_halo4VehicleCameraReachAt{}; // render thread
inline constexpr float kHalo4CameraReleasePerSecond = 1.5f;

// Render thread, in the stereo transaction once the head pose has moved the eye from `before` to `after`: that
// movement held inside the chase camera's leash. Nothing when this frame placed no chase camera or it sets no leash.
void Halo4VehicleCameraLeashHead(const float before[3], float after[3])
{
    const Halo4VehicleCameraPassRecord& pass = g_halo4VehicleCameraPass;
    if (!pass.placed)
        return;
    float head[3] = {after[0] - before[0], after[1] - before[1], after[2] - before[2]};
    if (vehicle_camera_place::LeashHead(pass.camera, pass.where, pass.reach, head))
        for (int axis = 0; axis < 3; ++axis)
            after[axis] = before[axis] + head[axis];
}

// THE GAME'S OWN THIRD-PERSON CAMERA in the seat (render thread): the point it turns about, found from its lines in
// the vehicle's frame, and its distance from there and height above it, taken while it looked within about 15 degrees
// of level where it has (else at any look), as the furthest it stood -- the game pulls its camera in from walls.
struct Halo4VehicleCameraRest
{
    VehicleCameraSeatKey seat{};
    halo4_vehicle_camera::PivotFinder finder{};
    bool pivotKnown = false;
    float pivotLocal[3]{};
    uint32_t sinceSolve = 0;
    float distance = 0.0f;
    float rise = 0.0f;
    bool level = false;
    bool measuredFromPivot = false; // the distance was taken from the found pivot, not the stand-in point
    // Until the lines cross: the height, over the vehicle's origin, at which the game's camera line passes it.
    float standInHeight = 0.0f;
    uint32_t seen = 0;   // frames the game's following camera was seen in this seat
    uint32_t waited = 0; // frames placed while it could not be read
};
Halo4VehicleCameraRest g_halo4VehicleCameraRest;
inline constexpr float kHalo4RestKnownWu = 0.25f;     // nearer than this the game's camera is not its following one
inline constexpr float kHalo4RestFurthestWu = 8.0f;
inline constexpr uint32_t kHalo4RestSettleFrames = 30; // the director's own change of view, after the camera is chosen
inline constexpr uint32_t kHalo4RestWaitFrames = 45;
inline constexpr uint32_t kHalo4RestSolveEvery = 15;
inline constexpr float kHalo4RestLevelSin = 0.26f;

// Fills `in` with the game's camera's place for a "thirdperson" camera. False: not known YET (the frame keeps the
// seat's own eye). True with in.restValid false: it could not be read, and the mod's stand-in applies. `gameCamera` is
// the frame's observer before the mod moved anything: the game's own camera for the seat; `following` whether that is
// its following camera (the override stood down and the game chose it).
bool Halo4VehicleCameraRestPlace(const VehicleCameraSeatKey& seat, const vehicle_camera_select::ActiveCamera& camera,
                                 const Halo4CameraBasis& gameCamera, bool following, vehicle_camera_place::Inputs& in)
{
    Halo4VehicleCameraRest& rest = g_halo4VehicleCameraRest;
    if (!(rest.seat == seat))
    {
        rest = Halo4VehicleCameraRest{};
        rest.seat = seat;
        Halo4VehicleCameraLevelStore(seat, vehicle_boom::kBoomPitchUnset);
        g_halo4VehicleCameraPivotSolved.store(0, std::memory_order_relaxed);
    }
    const vehicle_camera_frame::Basis& v = in.vehicle;
    if (following)
    {
        // The camera and its look in the vehicle's own frame.
        const float d[3] = {gameCamera.position[0] - in.vehicleOrigin[0], gameCamera.position[1] - in.vehicleOrigin[1],
                            gameCamera.position[2] - in.vehicleOrigin[2]};
        const float o[3] = {vehicle_camera_frame::Dot(d, v.forward), vehicle_camera_frame::Dot(d, v.left),
                            vehicle_camera_frame::Dot(d, v.up)};
        float look[3] = {vehicle_camera_frame::Dot(gameCamera.forward, v.forward),
                         vehicle_camera_frame::Dot(gameCamera.forward, v.left),
                         vehicle_camera_frame::Dot(gameCamera.forward, v.up)};
        if (vehicle_camera_place::Finite3(o) && vehicle_camera_frame::Normalize(look) &&
            ++rest.seen > kHalo4RestSettleFrames)
        {
            rest.finder.Add(o, look);
            if (++rest.sinceSolve >= kHalo4RestSolveEvery || !rest.pivotKnown)
            {
                rest.sinceSolve = 0;
                float found[3]{}, residual = 0.0f;
                if (rest.finder.Solve(found, &residual))
                {
                    if (!rest.pivotKnown)
                    {
                        // Distances taken from the stand-in point are not the pivot's: measure again from here.
                        rest.distance = 0.0f;
                        rest.level = false;
                    }
                    rest.pivotKnown = true;
                    memcpy(rest.pivotLocal, found, sizeof(found));
                    for (int axis = 0; axis < 3; ++axis)
                        g_halo4VehicleCameraPivot[axis].store(found[axis], std::memory_order_relaxed);
                    g_halo4VehicleCameraPivotResidualWu.store(residual, std::memory_order_relaxed);
                    g_halo4VehicleCameraPivotSamples.store(rest.finder.samples, std::memory_order_relaxed);
                    g_halo4VehicleCameraPivotSolved.store(1, std::memory_order_release);
                }
            }
            // Until the lines have crossed, the point of this line nearest the vehicle's origin stands in.
            float pivot[3]{};
            if (rest.pivotKnown)
                memcpy(pivot, rest.pivotLocal, sizeof(pivot));
            else
            {
                const float origin[3] = {0.0f, 0.0f, 0.0f};
                halo4_vehicle_camera::NearestOnLine(o, look, origin, pivot);
                rest.standInHeight = pivot[2];
            }
            const float stood[3] = {o[0] - pivot[0], o[1] - pivot[1], o[2] - pivot[2]};
            const float distance = sqrtf(vehicle_camera_frame::Dot(stood, stood));
            const float lookUp = look[2];
            const bool level = std::fabs(lookUp) <= kHalo4RestLevelSin;
            const bool take = std::isfinite(distance) && distance >= kHalo4RestKnownWu &&
                              distance <= kHalo4RestFurthestWu &&
                              (level ? (!rest.level || distance > rest.distance)
                                     : (!rest.level && distance > rest.distance));
            if (take)
            {
                rest.distance = distance;
                rest.rise = stood[2];
                rest.level = level;
                rest.measuredFromPivot = rest.pivotKnown;
                const float lookDeg = asinf(lookUp < -1.0f ? -1.0f : (lookUp > 1.0f ? 1.0f : lookUp)) * 57.29578f;
                if (level && rest.pivotKnown)
                {
                    const float levelDeg = halo4_vehicle_camera::OrbitLevelElevationDeg(rest.rise, distance, lookDeg);
                    // A degree either way is the measurement's own wobble: only a move of two restarts the boom.
                    const float published = g_halo4VehicleCameraLevel.deg;
                    if (vehicle_boom::BoomPitchSet(levelDeg) &&
                        (!vehicle_boom::BoomPitchSet(published) || std::fabs(levelDeg - published) >= 2.0f))
                        Halo4VehicleCameraLevelStore(seat, levelDeg);
                }
                g_halo4VehicleCameraRestRiseWu.store(rest.rise, std::memory_order_relaxed);
                g_halo4VehicleCameraRestPitchDeg.store(lookDeg, std::memory_order_relaxed);
            }
        }
    }
    // THE BOOM (vehicle_cameras.inl's shared boom, which this thread runs in Halo 4 as Halo 3's camera thread does
    // there): the cold worker's publication for this seat and camera -- an orbit of the pivot over Halo 4's range
    // (halo4_vehicle_camera::BoomRange), starting at the player's kept angle, else the file's "boomPitch", else the
    // game's level view a little higher -- or, until one comes, the stand-in orbit over the same range.
    Halo3BoomTake(seat, camera.camera);
    Halo3BoomCamera& boom = g_halo3BoomCamera;
    if (!boom.held && !boom.local && (rest.distance >= kHalo4RestKnownWu || rest.waited >= kHalo4RestWaitFrames))
        Halo3BoomStandIn(halo4_vehicle_camera::BoomRange());
    if (rest.distance >= kHalo4RestKnownWu && (boom.held || boom.local))
    {
        Halo3BoomStep();
        const float none[3] = {0.0f, 0.0f, 0.0f};
        Halo3BoomPublishCurrent(false, none, none);
        vehicle_boom::Offset(boom.shape, boom.pitch, rest.distance, in.restOffset);
        g_vehicleBoomLiveMs.store(GetTickCount64(), std::memory_order_release);
        // Before the lines cross the camera orbits the point over the vehicle's origin at the height its line passes.
        float pivot[3] = {0.0f, 0.0f, rest.standInHeight};
        if (rest.pivotKnown)
            memcpy(pivot, rest.pivotLocal, sizeof(pivot));
        in.restValid = true;
        for (int axis = 0; axis < 3; ++axis)
            in.rest[axis] = in.vehicleOrigin[axis] + v.forward[axis] * pivot[0] + v.left[axis] * pivot[1] +
                            v.up[axis] * pivot[2];
        g_halo4VehicleCameraRestWu.store(rest.distance, std::memory_order_relaxed);
        return true;
    }
    // While the game's following camera is up its distance is coming (the director's own change of view settles
    // first), and meanwhile the frame shows that camera itself, the flat game's view. Only a seat whose game camera
    // is not its following one waits out kHalo4RestWaitFrames for the stand-in.
    if (following || rest.waited < kHalo4RestWaitFrames)
    {
        if (!following)
            ++rest.waited;
        return false;
    }
    in.restValid = false;
    return true;
}

// Render thread, once a frame of a seat: where the chosen chase camera stands. False: none is chosen for this seat,
// or its place is not known yet; the seat's own eye stays.
bool Halo4VehicleCameraPlace(const Halo4VehicleCameraFrame& frame, const VehicleCameraSeatKey& key,
                             const vehicle_camera_select::ActiveCamera& camera, const Halo4CameraBasis& gameCamera,
                             bool following, const float seatEye[3], float out[3])
{
    vehicle_camera_place::Inputs in;
    // The vehicle: the carrier a gun rides, else the seated object itself. The seat's own frame is the seated
    // object's root, so a camera measured from the seat of a gun turns with the gun.
    const bool carried = frame.carrierRootValid;
    if (!vehicle_camera_frame::FromForwardUp(carried ? frame.carrierForward : frame.rootForward,
                                             carried ? frame.carrierUp : frame.rootUp, in.vehicle) ||
        !vehicle_camera_frame::Usable(in.vehicle))
        return false;
    memcpy(in.vehicleOrigin, carried ? frame.carrierPos : frame.rootPos, sizeof(in.vehicleOrigin));
    in.vehicleRadiusCm = 0.0f; // Halo 4's bounds are not read: a stand-in stands at the fixed distance
    in.seatValid = frame.seatPointValid;
    if (in.seatValid)
        memcpy(in.seat, frame.seatPoint, sizeof(in.seat));
    else
    {
        in.seatValid = true;
        memcpy(in.seat, seatEye, sizeof(in.seat));
    }
    in.seatFrameValid = vehicle_camera_frame::FromForwardUp(frame.rootForward, frame.rootUp, in.seatFrame) &&
                        vehicle_camera_frame::Usable(in.seatFrame);
    // The occupant's head, and for a camera measured from it, the head as a frame: the seat's frame turned as far as
    // the head has turned in it since it settled (Reach's and Halo 3's "playerhead").
    in.headValid = frame.headValid;
    if (in.headValid)
    {
        memcpy(in.head, frame.head, sizeof(in.head));
        if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Head))
        {
            static constexpr uint32_t kHalo4HeadSettleFrames = 45;
            struct HeadReference
            {
                VehicleCameraSeatKey seat{};
                uint32_t frames = 0;
                bool restValid = false;
                vehicle_camera_frame::Basis rest{};
            };
            static HeadReference headRef; // render thread
            if (!(headRef.seat == key))
                headRef = HeadReference{key};
            vehicle_camera_frame::Basis headBasis{};
            if (in.seatFrameValid && vehicle_camera_frame::FromForwardUp(frame.headForward, frame.headUp, headBasis))
            {
                const vehicle_camera_frame::Basis inSeat = vehicle_camera_frame::InFrame(in.seatFrame, headBasis);
                if (!headRef.restValid && ++headRef.frames >= kHalo4HeadSettleFrames)
                {
                    headRef.rest = inSeat;
                    headRef.restValid = true;
                }
                if (headRef.restValid)
                {
                    in.headFrame = vehicle_camera_frame::TurnedSince(in.seatFrame, inSeat, headRef.rest);
                    in.headFrameValid = vehicle_camera_frame::Usable(in.headFrame);
                }
            }
        }
    }
    else
    {
        if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Head))
            g_halo4VehicleCameraHeadMisses.fetch_add(1, std::memory_order_relaxed);
        // The seat's own eye stands in for the head.
        in.headValid = true;
        memcpy(in.head, seatEye, sizeof(in.head));
    }
    in.viewYaw = g_halo4Camera.gameYawReference.load(std::memory_order_relaxed);
    if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::ThirdPerson) &&
        !Halo4VehicleCameraRestPlace(key, camera, gameCamera, following, in))
        return false;

    float point[3];
    vehicle_camera_place::Placed placed;
    if (!vehicle_camera_place::Place(camera, in, g_halo4VehicleCameraState, point, &placed))
        return false;

    // LAG: the boom from its pivot swings on a spring; the vehicle's travel never lags.
    const float seconds = Halo4VehicleCameraSecondsSince(g_halo4VehicleCameraLagAt);
    vehicle_camera_place::FollowLagOnBoom(camera, placed.pivot, point, seconds, g_halo4VehicleCameraLag, point);
    const float trailCm = g_halo4VehicleCameraLag.trailWu * 304.8f;
    const uint32_t trail =
        std::isfinite(trailCm) && trailCm > 0.0f ? static_cast<uint32_t>(trailCm < 1.0e8f ? trailCm : 1.0e8f) : 0u;
    uint32_t furthest = g_halo4VehicleCameraLagTrailCm.load(std::memory_order_relaxed);
    while (trail > furthest &&
           !g_halo4VehicleCameraLagTrailCm.compare_exchange_weak(furthest, trail, std::memory_order_relaxed))
    {
    }
    if (!vehicle_camera_place::Finite3(point))
        return false;

    // HELD SHORT OF A WALL ("collide"): the line from the pivot to the camera's place is traced by the seated trace
    // (halo4_seat_converge.inl) in the game's control pass, and the camera stops short of what it meets. The trace is
    // a frame or two behind; with none to go on the camera goes all the way.
    float reachUsed = 1.0f;
    if (camera.collide)
    {
        const float toward[3] = {point[0] - placed.pivot[0], point[1] - placed.pivot[1], point[2] - placed.pivot[2]};
        const float length = sqrtf(vehicle_camera_frame::Dot(toward, toward));
        float wanted = 1.0f;
        if (std::isfinite(length) && length > 0.05f)
        {
            const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
            SightAuxRay ray{};
            ray.generation = generation;
            for (int axis = 0; axis < 3; ++axis)
            {
                ray.origin[axis] = placed.pivot[axis];
                ray.dir[axis] = toward[axis] / length;
            }
            Sight_PublishCameraRay(GameTitle::Halo4, ray);
            bool hit = false;
            float rangeWu = 0.0f;
            if (Sight_ReadCameraTrace(GameTitle::Halo4, generation, 150, hit, rangeWu))
            {
                wanted = vehicle_camera_place::CollideReach(hit, rangeWu, length, camera.collideMarginCm);
                g_halo4VehicleCameraCollideFrames.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                g_halo4VehicleCameraNoTraceFrames.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const float since = Halo4VehicleCameraSecondsSince(g_halo4VehicleCameraReachAt);
        const float reach = vehicle_camera_place::EaseReach(g_halo4VehicleCameraReach, wanted,
                                                            since < 0.0f ? 0.0f : (since > 0.1f ? 0.1f : since),
                                                            kHalo4CameraReleasePerSecond);
        if (reach < 0.999f)
        {
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = placed.pivot[axis] + toward[axis] * reach;
            g_halo4VehicleCameraHeldFrames.fetch_add(1, std::memory_order_relaxed);
            reachUsed = reach;
        }
    }
    else
    {
        g_halo4VehicleCameraReach = 1.0f;
    }
    Halo4VehicleCameraPassRecord& pass = g_halo4VehicleCameraPass;
    pass.reach = reachUsed;
    pass.placed = true;
    pass.camera = camera;
    pass.where = placed;
    g_halo4VehicleCameraFrames.fetch_add(1, std::memory_order_relaxed);
    if (g_halo4VehicleCameraState.standIn)
        g_halo4VehicleCameraStandInFrames.fetch_add(1, std::memory_order_relaxed);
    memcpy(out, point, sizeof(point));
    return true;
}

// TETHERING: a chosen chase camera whose mode tracks yaw turns the heading the head-owned view is built on
// (g_halo4Camera.gameYawReference) as far as the hull has turned since the last frame, so the view turns with the
// vehicle. The hand's aim is laid on the same heading and turns with it, as the view follow does in Halo 3 and
// Reach. Render thread; the VR turn on the input thread turns the same heading, so the turn is added, never stored.
struct Halo4VehicleCameraTurn
{
    VehicleCameraSeatKey seat{};
    bool known = false;
    float heading = 0.0f;
    LARGE_INTEGER at{};
};
Halo4VehicleCameraTurn g_halo4VehicleCameraTurn; // render thread

void Halo4VehicleCameraTurnView(const VehicleCameraSeatKey& key, bool turns, const float hullForward[3])
{
    Halo4VehicleCameraTurn& turn = g_halo4VehicleCameraTurn;
    const float seconds = Halo4VehicleCameraSecondsSince(turn.at);
    const float heading = std::atan2(hullForward[1], hullForward[0]);
    if (!turns || !std::isfinite(heading) || !Halo4ControllerAimActive())
    {
        turn.known = false;
        return;
    }
    if (!(turn.seat == key) || !turn.known || seconds < 0.0f || seconds > 0.25f)
    {
        turn.seat = key;
        turn.known = true;
        turn.heading = heading;
        return;
    }
    const float delta = vehicle_camera_frame::YawDelta(heading, turn.heading);
    turn.heading = heading;
    if (!std::isfinite(delta) || delta == 0.0f)
        return;
    float reference = g_halo4Camera.gameYawReference.load(std::memory_order_relaxed);
    while (!g_halo4Camera.gameYawReference.compare_exchange_weak(reference, WrapPi(reference + delta),
                                                                 std::memory_order_relaxed))
    {
    }
    g_halo4VehicleCameraTurnFrames.fetch_add(1, std::memory_order_relaxed);
}

bool Halo4VehicleCameraHoldsExposure(const VehicleCameraSeatKey& key); // below

// The stereo transaction, every frame of a player-controlled seat with the head tracked: before the head pose is
// composed onto `eye` (the frame's camera, from the game's observer) and `hands` (the camera the hands are placed
// from, the observer as the game made it). `seatRead`/`seat` are the frame's own seat read.
void Halo4VehicleCamerasPass(bool seatRead, const Halo4VehicleInputState& seat, Halo4CameraBasis& hands,
                             Halo4CameraBasis& eye)
{
    g_halo4VehicleCameraPass.placed = false;
    if (!g_config.vehicle_cameras)
    {
        EngineExposureLockApply(g_halo4Exposure, false);
        return;
    }
    Halo4VehicleCameraFrame frame;
    const bool sampled = seatRead && Halo4SampleVehicleCameraFrame(seat, frame);
    // The game's own answer for this seat, before the override: its following camera, or its first-person eye.
    bool nativeChase = false;
    const bool nativeKnown = frame.active &&
                             NativeVehicleFirstPerson_Halo4NativeChase(frame.parent, frame.seatIndex, nativeChase);
    if (seatRead && (!seat.seated || sampled))
        Halo4VehicleCameraPublishSeat(true, frame, nativeKnown && nativeChase);
    // "eyeAdjust": false holds Halo 4's eye adaptation (render_exposure_lock, engine_exposure_lock.inl) while the
    // camera in use for this seat says so; every other frame gives it back to the engine, as does a teardown on its
    // way. A compare a frame. Frames that never reach this pass give it back in the transaction.
    EngineExposureLockApply(g_halo4Exposure, frame.active &&
                                                 !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
                                                 Halo4VehicleCameraHoldsExposure(Halo4VehicleCameraKey(frame)));
    if (!frame.active)
    {
        if (seatRead && seat.seated && Halo4VehicleCameraChaseChosen(TitleAdapter_GetGeneration(GameTitle::Halo4),
                                                                     seat.parent, seat.seat))
            g_halo4VehicleCameraNoRootFrames.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const VehicleCameraSeatKey key = Halo4VehicleCameraKey(frame);

    // A FIRSTPERSON CAMERA'S "offset": the seat's eye, and the hands' camera with it, moved along the seated object's
    // own frame -- by no more than two world units, as Halo 3 and Reach keep theirs within the vehicle's reach.
    float nudge[3];
    if (Halo3VehicleCameraEyeNudge(key.generation, key.vehicle, key.seatIndex, key.identity, key.mounted, nudge))
    {
        vehicle_camera_frame::Basis basis{};
        constexpr float kNudgeMostWu = 2.0f;
        if (vehicle_camera_frame::FromForwardUp(frame.rootForward, frame.rootUp, basis) &&
            vehicle_camera_frame::Dot(nudge, nudge) <= kNudgeMostWu * kNudgeMostWu)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                const float by = basis.forward[axis] * nudge[0] - basis.left[axis] * nudge[1] + basis.up[axis] * nudge[2];
                eye.position[axis] += by;
                hands.position[axis] += by;
            }
            g_halo4VehicleCameraNudgeFrames.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    // A CHASE CAMERA: the eye stands at its place, and its mode turns the view.
    vehicle_camera_select::ActiveCamera camera;
    VehicleCameraSeatKey chosenFor;
    if (!VehicleCameraRead(camera, chosenFor) || !(chosenFor == key))
    {
        Halo4VehicleCameraTurnView(key, false, frame.rootForward);
        return;
    }
    Halo4VehicleCameraTurnView(key, camera.rotYaw, frame.carrierRootValid ? frame.carrierForward : frame.rootForward);
    // The game's camera is its following camera when the override has stood down for this seat and the game itself
    // chose it (a seat the game shows in first person keeps its eye there).
    const bool following = nativeKnown && nativeChase &&
                           !NativeVehicleFirstPerson_SeatViewOwned(GameTitle::Halo4, frame.parent, frame.seatIndex);
    float point[3];
    if (Halo4VehicleCameraPlace(frame, key, camera, hands, following, eye.position, point))
    {
        Halo4VehicleCameraPassRecord& pass = g_halo4VehicleCameraPass;
        pass.seat = key;
        memcpy(pass.seatEye, frame.headValid ? frame.head : eye.position, sizeof(pass.seatEye));
        memcpy(pass.vehicleForward, frame.carrierRootValid ? frame.carrierForward : frame.rootForward,
               sizeof(pass.vehicleForward));
        memcpy(pass.looks, hands.forward, sizeof(pass.looks));
        pass.looksValid = vehicle_camera_place::Finite3(pass.looks) && vehicle_camera_frame::Normalize(pass.looks);
        memcpy(eye.position, point, sizeof(point));
        g_halo4VehicleCameraPlacedParent.store(frame.parent, std::memory_order_relaxed);
        g_halo4VehicleCameraPlacedSeat.store(frame.seatIndex, std::memory_order_relaxed);
        g_halo4VehicleCameraPlacedMs.store(GetTickCount64(), std::memory_order_release);
    }
}

// A CAMERA'S "eyeAdjust": false for exactly this seat (any camera type). One atomic load while no camera in use asks
// for it; a camera being published both times it is read answers false for that frame.
// The ring's frames in Halo 4, and those with a fresh trace of its ray, for the line on leaving the seat.
std::atomic<uint32_t> g_halo4VehicleAimMarkerFrames{0};
std::atomic<uint32_t> g_halo4VehicleAimMarkerTracedFrames{0};

// THE VEHICLE AIM RING ("aimMarker", vehicle_aim_marker_logic.h), as Halo 3 and Reach place it: the stereo
// transaction, once the head pose has put the eye where the frame is drawn from (`eye`). The line the vehicle aims
// along is its heading at the game's own aim pitch, from the occupant's head; its depth is the seated trace of that
// ray (halo4_seat_converge.inl); its direction in the headset's space is the head-owned view's own mapping (the
// heading reference the view is built on, Halo4ApplyHeadPose). The frame thread draws it (Game_GetVehicleAimMarker).
// Shares Halo 3's ring record: one title runs at a time.
void Halo4VehicleAimMarkerStep(const float eye[3])
{
    const Halo4VehicleCameraPassRecord& pass = g_halo4VehicleCameraPass;
    float dir[3];
    if (!g_config.vehicle_aim_marker || !pass.placed || !pass.camera.aimMarker || !eye || !pass.looksValid ||
        !Halo4ControllerAimActive() || !vehicle_aim_marker::Direction(pass.vehicleForward, pass.looks, dir))
    {
        Halo3VehicleAimMarkerClear();
        return;
    }
    if (!(pass.seat == g_halo3VehicleAimMarkerSeat))
    {
        g_halo3VehicleAimMarkerSeat = pass.seat;
        g_halo3VehicleAimMarkerRange.Reset();
    }
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
    SightAuxRay ray{};
    ray.generation = generation;
    for (int axis = 0; axis < 3; ++axis)
    {
        ray.origin[axis] = pass.seatEye[axis];
        ray.dir[axis] = dir[axis];
    }
    Sight_PublishMarkerRay(GameTitle::Halo4, ray);
    bool hit = false;
    float rangeWu = 0.0f;
    const bool traced = Sight_ReadMarkerTrace(GameTitle::Halo4, generation, kSightFreshMs, hit, rangeWu);
    const float range = g_halo3VehicleAimMarkerRange.Use(traced, hit, rangeWu, kSightTraceLengthWu);
    const float unitsPerVrMetre = g_worldScale.load(std::memory_order_relaxed);
    const float along = vehicle_aim_marker::PullBack(range, g_config.reticle_surface_offset_m * unitsPerVrMetre);
    float point[3], fromEye[3], lengthWu = 0.0f, vrYaw = 0.0f, vrPitch = 0.0f;
    if (!(unitsPerVrMetre > 0.0f) || !shot_point::PointAlongRay(pass.seatEye, dir, along, point) ||
        !shot_point::FromHead(eye, point, fromEye, lengthWu) ||
        !Halo3SeatAimToVrReticle(fromEye, false, 0.0f, 0.0f,
                                 g_halo4Camera.gameYawReference.load(std::memory_order_relaxed),
                                 g_halo4Camera.headYawReference.load(std::memory_order_relaxed), g_yawSign.load(),
                                 g_pitchSign.load(), vrYaw, vrPitch))
    {
        Halo3VehicleAimMarkerClear();
        return;
    }
    Halo3VehicleAimMarkerMark& mark = g_halo3VehicleAimMarker;
    mark.seq.fetch_add(1, std::memory_order_acq_rel);
    mark.vrYaw = vrYaw;
    mark.vrPitch = vrPitch;
    mark.distanceM = lengthWu / unitsPerVrMetre;
    mark.ms.store(GetTickCount64(), std::memory_order_relaxed);
    mark.seq.fetch_add(1, std::memory_order_release);
    g_halo3VehicleAimMarkerFrames.fetch_add(1, std::memory_order_relaxed);
    g_halo4VehicleAimMarkerFrames.fetch_add(1, std::memory_order_relaxed);
    if (!traced)
        g_halo3VehicleAimMarkerUntracedFrames.fetch_add(1, std::memory_order_relaxed);
    else
        g_halo4VehicleAimMarkerTracedFrames.fetch_add(1, std::memory_order_relaxed);
}

bool Halo4VehicleCameraHoldsExposure(const VehicleCameraSeatKey& key)
{
    const VehicleCameraPublished& published = g_vehicleCameraPublished;
    if (!published.holdExposure.load(std::memory_order_acquire))
        return false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const bool hold = published.camera.valid && published.camera.holdExposure;
        const VehicleCameraSeatKey chosenFor = published.seat;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (published.seq.load(std::memory_order_relaxed) != before)
            continue;
        return hold && chosenFor == key;
    }
    return false;
}

// ---- the cold worker's side ---------------------------------------------------------------------------------------

// The seat the player is in, for the cold worker. OnFoot: not in a seat (or first-person vehicles are off, or the
// stereo transaction has not run for a while). Unknown: the record is a moment stale or torn (the published camera is
// kept).
VehicleCameraSeatRead Halo4VehicleCameraSeat(VehicleCameraSeatKey& key, const char*& name, uint8_t& seatBits,
                                            bool& seatView, bool& wheelSeat, uint32_t& definitionIndex)
{
    if (!g_config.vehicle_first_person)
        return VehicleCameraSeatRead::OnFoot;
    Halo4VehicleCameraSeatRecord seat;
    uint64_t ageMs = 0;
    if (!Halo4VehicleCameraReadSeat(seat, ageMs))
        return VehicleCameraSeatRead::Unknown;
    if (ageMs > kHalo4VehicleCameraSeatGoneMs)
        return VehicleCameraSeatRead::OnFoot;
    if (ageMs > kHalo4VehicleCameraSeatFreshMs || seat.generation != TitleAdapter_GetGeneration(GameTitle::Halo4))
        return VehicleCameraSeatRead::Unknown;
    if (!seat.seated)
        return VehicleCameraSeatRead::OnFoot;
    // A vehicle whose identity has not read yet is not named custom_ for a moment: the next frames read it.
    if (seat.identityPending)
        return VehicleCameraSeatRead::Unknown;
    key = Halo4VehicleCameraKey(seat.generation, seat.parent, seat.seatIndex, seat.identity, seat.onCarrier);
    static char buffer[halo4_vehicle_camera::kNameCapacity];
    name = halo4_vehicle_camera::SeatVehicleName(seat.identity, seat.onCarrier, seat.carrierIdentity, buffer);
    seatBits = halo4_vehicle_camera::SeatBits(seat.flagsKnown, seat.seatFlags, seat.onCarrier, seat.seatIndex);
    seatView = true; // every Halo 4 seat has the first-person seat view
    wheelSeat = false; // Halo 4's drivers steer by the look; there is no wheel
    definitionIndex = seat.definition;

    // Once per seat: what the seat is, and the facts its name and role were read from.
    static VehicleCameraSeatKey logged{};
    static uint32_t loggedFlags = 0xFFFFFFFFu;
    const uint32_t flagsSeen = seat.flagsKnown ? seat.seatFlags : 0xFFFFFFFFu;
    if (!(logged == key) || loggedFlags != flagsSeen)
    {
        logged = key;
        loggedFlags = flagsSeen;
        char flagsText[24];
        if (seat.flagsKnown)
            std::snprintf(flagsText, sizeof(flagsText), "%08X", seat.seatFlags);
        else
            std::snprintf(flagsText, sizeof(flagsText), "not read");
        LOG("Vehicle cameras (Halo 4): %s, seat %d (%s): model %016llx%s, seat flags %s, definition %04X%s; the game's "
            "own camera here is %s",
            name, seat.seatIndex, vehicle_camera_file::SeatText(seatBits).c_str(),
            static_cast<unsigned long long>(seat.identity),
            halo4_vehicle_camera::IdentityName(seat.identity) ? "" : " (not one the mod has measured)", flagsText,
            seat.definition & 0xFFFFu,
            seat.onCarrier ? (halo4_vehicle_camera::IdentityName(seat.carrierIdentity) ? ", riding a carrier the mod knows"
                                                                                         : ", riding a carrier")
                           : "",
            seat.nativeChase ? "its third-person following camera" : "first person (or not yet seen)");
        if (seat.onCarrier)
            LOG("Vehicle cameras (Halo 4): that seat's carrier is model %016llx",
                static_cast<unsigned long long>(seat.carrierIdentity));
    }
    return VehicleCameraSeatRead::Seated;
}

// What a selected camera asks for that Halo 4 does not do yet, for the log and the readout; empty when nothing.
std::string Halo4VehicleCameraNotApplied(const vehicle_camera_select::ActiveCamera& camera)
{
    if (!camera.valid)
        return {};
    std::string list;
    const auto add = [&list](const char* what) {
        if (!list.empty())
            list += "; ";
        list += what;
    };
    if (camera.chase)
    {
        if (camera.rotRoll)
            add("rotationTracking roll (the horizon is never rolled)");
        if (camera.rotPitch)
            add("rotationTracking pitch (the view turns with the hull's heading only)");
        if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Marker))
            add("an origin naming a marker (the vehicle's origin is used)");
    }
    if (camera.hideRegions[0])
        add("hideRegions");
    if (camera.hideParts[0])
        add("hideParts");
    if (camera.fadeParts[0])
        add("fadeParts");
    return list;
}

// Cold worker, every poll while the vehicle cameras are on in Halo 4: the counts the render thread keeps, as log lines
// when there is something to say. `seated` false = the player has left the seat.
void Halo4VehicleCamerasColdLog(bool seated)
{
    static bool standInLogged = false, noTraceLogged = false, noRootLogged = false;
    static float restLoggedWu = 0.0f, restSeenWu = 0.0f;
    static uint32_t trailStint = 0, headMissStint = 0, heldStint = 0, tracedStint = 0, turnStint = 0, nudgeStint = 0;
    static uint32_t pivotLoggedSamples = 0;
    const uint32_t collideFrames = g_halo4VehicleCameraCollideFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t noTraceFrames = g_halo4VehicleCameraNoTraceFrames.exchange(0, std::memory_order_relaxed);
    heldStint += g_halo4VehicleCameraHeldFrames.exchange(0, std::memory_order_relaxed);
    tracedStint += collideFrames;
    turnStint += g_halo4VehicleCameraTurnFrames.exchange(0, std::memory_order_relaxed);
    static uint32_t ringStint = 0, ringTracedStint = 0;
    ringStint += g_halo4VehicleAimMarkerFrames.exchange(0, std::memory_order_relaxed);
    ringTracedStint += g_halo4VehicleAimMarkerTracedFrames.exchange(0, std::memory_order_relaxed);
    nudgeStint += g_halo4VehicleCameraNudgeFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t noRoot = g_halo4VehicleCameraNoRootFrames.exchange(0, std::memory_order_relaxed);
    // The boom's angle while one is in use, and the range it went through, for the line on leaving the seat.
    static bool boomSeen = false;
    static float boomLastDeg = 0.0f, boomLowDeg = 0.0f, boomHighDeg = 0.0f;
    if (seated && (g_vehicleBoomShown.load(std::memory_order_acquire) & 1u))
    {
        const float deg = g_vehicleBoomShownDeg.load(std::memory_order_relaxed);
        boomLowDeg = boomSeen && boomLowDeg < deg ? boomLowDeg : deg;
        boomHighDeg = boomSeen && boomHighDeg > deg ? boomHighDeg : deg;
        boomLastDeg = deg;
        boomSeen = true;
    }
    if (noRoot > 60 && !noRootLogged)
    {
        noRootLogged = true;
        LOG("Vehicle cameras (Halo 4): a camera is chosen for this seat, but the vehicle's place could not be read, so "
            "the game's own camera is shown");
    }
    if (noTraceFrames > 60 && collideFrames == 0 && !noTraceLogged)
    {
        noTraceLogged = true;
        LOG("Vehicle cameras (Halo 4): this camera asks to be kept out of walls (\"collide\"), but the seated trace is "
            "not running for this seat (halo4_seat_converge = 0, or the seat does not converge), so it goes all the "
            "way out");
    }
    const uint32_t frames = g_halo4VehicleCameraFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t standIn = g_halo4VehicleCameraStandInFrames.exchange(0, std::memory_order_relaxed);
    headMissStint += g_halo4VehicleCameraHeadMisses.exchange(0, std::memory_order_relaxed);
    const uint32_t trail = g_halo4VehicleCameraLagTrailCm.exchange(0, std::memory_order_relaxed);
    trailStint = trail > trailStint ? trail : trailStint;
    // The pivot the game's camera turns about, each time it is found again with more lines behind it (in the seat:
    // the flag stays up after it, until the next seat's first frame).
    if (seated && g_halo4VehicleCameraPivotSolved.load(std::memory_order_acquire))
    {
        const uint32_t samples = g_halo4VehicleCameraPivotSamples.load(std::memory_order_relaxed);
        if (samples && (!pivotLoggedSamples || samples >= 4 * pivotLoggedSamples))
        {
            pivotLoggedSamples = samples;
            LOG("Vehicle cameras (Halo 4): the game's own third-person camera turns about a point %.0f cm ahead of, "
                "%.0f cm left of and %.0f cm above the vehicle's origin (its lines meet there within %.0f cm, over %u "
                "frames)",
                static_cast<double>(g_halo4VehicleCameraPivot[0].load(std::memory_order_relaxed) /
                                    vehicle_camera_frame::kWorldUnitsPerCm),
                static_cast<double>(g_halo4VehicleCameraPivot[1].load(std::memory_order_relaxed) /
                                    vehicle_camera_frame::kWorldUnitsPerCm),
                static_cast<double>(g_halo4VehicleCameraPivot[2].load(std::memory_order_relaxed) /
                                    vehicle_camera_frame::kWorldUnitsPerCm),
                static_cast<double>(g_halo4VehicleCameraPivotResidualWu.load(std::memory_order_relaxed) /
                                    vehicle_camera_frame::kWorldUnitsPerCm),
                samples);
        }
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        static uint64_t drawnSaidMs = 0;
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - drawnSaidMs >= 5000)
        {
            drawnSaidMs = nowMs;
            const uint32_t samples = g_halo4VehicleCameraDrawnSamples.exchange(0, std::memory_order_relaxed);
            const uint32_t apart = g_halo4VehicleCameraDrawnApart.exchange(0, std::memory_order_relaxed);
            const uint32_t gapMm = g_halo4VehicleCameraDrawnGapMaxMm.exchange(0, std::memory_order_relaxed);
            if (samples)
                LOG("Vehicle cameras (Halo 4, dev): the vehicle's root as drawn on the stereo transaction stood apart "
                    "from it as simulated on %u of %u frames, by at most %.1f cm (0 of N while moving: not "
                    "interpolated there)",
                    apart, samples, static_cast<double>(gapMm) * 0.1);
        }
    }
#endif
    if (!seated)
    {
        if (trailStint)
            LOG("Vehicle cameras (Halo 4): in that seat the lagged camera trailed its place by at most %u cm", trailStint);
        if (headMissStint)
            LOG("Vehicle cameras (Halo 4): in that seat the occupant's head could not be read on %u frames, and the "
                "seat's eye stood in for it",
                headMissStint);
        if (tracedStint)
            LOG("Vehicle cameras (Halo 4): in that seat the camera's line was traced for walls on %u frames and held "
                "short of something on %u of them",
                tracedStint, heldStint);
        if (turnStint)
            LOG("Vehicle cameras (Halo 4): in that seat the view's heading turned with the hull on %u frames",
                turnStint);
        if (ringStint)
            LOG("Vehicle cameras (Halo 4): in that seat the ring where the vehicle aims was placed on %u frames, %u of "
                "them at a freshly traced depth",
                ringStint, ringTracedStint);
        ringStint = ringTracedStint = 0;
        if (nudgeStint)
            LOG("Vehicle cameras (Halo 4): in that seat the seat's eye was moved by its camera's offset on %u frames",
                nudgeStint);
        if (boomSeen)
            LOG("Vehicle cameras (Halo 4): in that seat the third-person boom went from %.1f to %.1f deg above level "
                "and was left at %.1f deg (kept for that seat and camera)",
                static_cast<double>(boomLowDeg), static_cast<double>(boomHighDeg), static_cast<double>(boomLastDeg));
        boomSeen = false;
        trailStint = headMissStint = heldStint = tracedStint = turnStint = nudgeStint = 0;
        standInLogged = noTraceLogged = noRootLogged = false;
        restLoggedWu = restSeenWu = 0.0f;
        pivotLoggedSamples = 0;
        g_halo4VehicleCameraRestWu.store(0.0f, std::memory_order_relaxed);
        return;
    }
    if (standIn && !standInLogged)
    {
        standInLogged = true;
        float behindCm = 0.0f, aboveCm = 0.0f;
        vehicle_camera_place::StandInOffsetCm(0.0f, behindCm, aboveCm);
        LOG("Vehicle cameras (Halo 4): where the game's own third-person camera rests could not be read for this seat "
            "(the game's own camera there is not its following camera), so the camera stands %.0f cm behind and %.0f "
            "cm above the vehicle's origin instead",
            static_cast<double>(behindCm), static_cast<double>(aboveCm));
    }
    const float restWu = g_halo4VehicleCameraRestWu.load(std::memory_order_relaxed);
    const bool settled = restWu == restSeenWu;
    restSeenWu = restWu;
    if (frames && restWu > 0.0f && settled && restWu > restLoggedWu * 1.1f)
    {
        restLoggedWu = restWu;
        LOG("Vehicle cameras (Halo 4): the game's own third-person camera rests %.2f world units (%.0f cm) from its "
            "pivot in this seat, %.0f cm of that above the pivot (it looked %.1f deg up)%s",
            static_cast<double>(restWu), static_cast<double>(restWu / vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_halo4VehicleCameraRestRiseWu.load(std::memory_order_relaxed) /
                                vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_halo4VehicleCameraRestPitchDeg.load(std::memory_order_relaxed)),
            g_halo4VehicleCameraPivotSolved.load(std::memory_order_acquire)
                ? ""
                : "; the pivot is not found yet (the aim has not turned enough), so the point of its line nearest the "
                  "vehicle's origin stands in");
    }
}
