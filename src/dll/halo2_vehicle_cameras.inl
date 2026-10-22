// VEHICLE CAMERAS IN HALO 2: Halo 2's side of vehicle_cameras.inl. Included by game.cpp right after it; the entry
// points Halo 2's observer calls (halo2_vehicle_cameras.h) are defined at the end of game.cpp, outside its namespace.
//
// THREADS.
//   Halo 2's observer (halo2_observer_6dof.cpp, ApplyHeadPose: the frame's camera, on the game's main thread) builds
//     the seat's view once a frame. It hands the seat to Halo2VehicleCamerasBegin, which publishes it for the cold
//     worker and answers how a chosen chase camera turns the view, and, once the view's heading is built, to
//     Halo2VehicleCamerasPlace, which moves the view's eye: a firstperson camera's "offset", or a chase camera's
//     place. The hands are built on that eye (the seat's vehicle reference), so they go with it. The leash holds the
//     head once it is added (Halo2VehicleCameraLeash). It reads the cold worker's published camera with two atomic
//     loads while none is chosen.
//   The cold worker (vehicle_cameras.inl, VehicleCameras_ColdTick) reads the published seat through
//     Halo2VehicleCameraSeat: the vehicle's name, the seat's role, and the key every reader matches a camera to.
//
// WHAT A CHASE CAMERA CHANGES. The first-person seat override (native_vehicle_first_person.cpp) stands down for the
// seat (Game_Halo2VehicleCameraChase), so the game's own answer for it stands: a seat the game shows in third person
// draws its occupant and no first-person view, as the flat game does, and the game's own camera is its chase camera
// again -- the one the flat game aims and fires from (the seat drive writes no seat whose first-person view it does
// not own; the stick servo aims it, from the hand, as with vehicle_first_person off). The view's eye stands at the
// chase camera's place. The camera's tethering turns the view: its rotationTracking yaw stands in for
// vehicle_view_follow in that seat. Its leash holds the head, and its "collide" is traced by Halo 2's sight trace on
// the simulation thread (halo2_observer_6dof.cpp, Halo2SightAuxTraces). Nothing the simulation reads is written.
//
// "thirdperson": the game's own third-person camera is the observer's stock camera while the override stands down.
// Its pivot is the point the game's own first-person evaluator answers for the seat -- the unit camera position, which
// for a seated unit is its seat's camera marker -- read while the seat view is first person (every seat starts in it)
// and kept, as a point on the seat's own object, for that kind of seat; until it has been read, the occupant's head
// where it settled in the seat stands in. Where the game's camera stands from the pivot gives the distance; the camera
// then orbits the pivot at the shared boom's elevation (vehicle_boom_logic.h's orbit shape: Halo 2's camera track is
// not read).
//
// "seat" and "playerhead": the occupant's head marker (as the first-person seat camera places it, carried onto the
// vehicle as drawn). "seat" is that head where it settled in the seat, held on the seat's own object, so the
// occupant's animation does not shake the camera; "playerhead" is the head as it is, its frame turned as far as the
// head has turned since it settled.

// ---- the seat, published by the observer for the cold worker ---------------------------------------------------

struct Halo2VehicleCameraSeatRecord
{
    std::atomic<uint32_t> seq{0}; // even = stable
    std::atomic<uint64_t> ms{0};  // the last publication; 0 = never
    bool seated = false;
    uint32_t generation = 0;
    uint32_t parent = UINT32_MAX;
    uint32_t root = UINT32_MAX;
    int16_t seat = -1;
    uint64_t identity = 0;
    uint64_t carrierIdentity = 0;
    bool onCarrier = false;
    bool modelRead = false;
    float bounds[6]{};
    unsigned nodes = 0;
    float radius = 0.0f;
    bool flagsKnown = false;
    uint32_t seatFlags = 0;
};
Halo2VehicleCameraSeatRecord g_halo2VehicleCameraSeat;
inline constexpr uint64_t kHalo2VehicleCameraSeatFreshMs = 250;

bool Halo2VehicleCameraOnCarrier(const Halo2VehicleCameraSeatSample& seat)
{
    return seat.root != UINT32_MAX && seat.root != seat.parent;
}

// The seat's key as every reader of the published camera matches it (VehicleCameraSeatKey).
VehicleCameraSeatKey Halo2VehicleCameraKey(const Halo2VehicleCameraSeatSample& seat)
{
    return VehicleCameraSeatKey{seat.generation, static_cast<int32_t>(seat.parent), seat.seat,
                                halo2_vehicle_camera::KeyIdentity(seat.identity), Halo2VehicleCameraOnCarrier(seat)};
}

void Halo2VehicleCameraPublishSeat(const Halo2VehicleCameraSeatSample* seat, uint32_t generation)
{
    Halo2VehicleCameraSeatRecord& record = g_halo2VehicleCameraSeat;
    record.seq.fetch_add(1, std::memory_order_acq_rel);
    record.seated = seat != nullptr;
    record.generation = generation;
    if (seat)
    {
        record.parent = seat->parent;
        record.root = seat->root;
        record.seat = seat->seat;
        record.identity = seat->identity;
        record.carrierIdentity = seat->carrierIdentity;
        record.onCarrier = Halo2VehicleCameraOnCarrier(*seat);
        record.modelRead = seat->modelRead;
        memcpy(record.bounds, seat->bounds, sizeof(record.bounds));
        record.nodes = seat->nodes;
        record.radius = seat->radius;
        record.flagsKnown = seat->flagsKnown;
        record.seatFlags = seat->seatFlags;
    }
    record.ms.store(GetTickCount64(), std::memory_order_relaxed);
    record.seq.fetch_add(1, std::memory_order_release);
}

// Cold worker. False: never published, torn twice, or a moment stale (the observer did not run: a pause, a load).
bool Halo2VehicleCameraReadSeat(Halo2VehicleCameraSeatRecord& out)
{
    const Halo2VehicleCameraSeatRecord& record = g_halo2VehicleCameraSeat;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const uint32_t before = record.seq.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        const uint64_t ms = record.ms.load(std::memory_order_relaxed);
        out.seated = record.seated;
        out.generation = record.generation;
        out.parent = record.parent;
        out.root = record.root;
        out.seat = record.seat;
        out.identity = record.identity;
        out.carrierIdentity = record.carrierIdentity;
        out.onCarrier = record.onCarrier;
        out.modelRead = record.modelRead;
        memcpy(out.bounds, record.bounds, sizeof(out.bounds));
        out.nodes = record.nodes;
        out.radius = record.radius;
        out.flagsKnown = record.flagsKnown;
        out.seatFlags = record.seatFlags;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (record.seq.load(std::memory_order_relaxed) != before)
            continue;
        const uint64_t now = GetTickCount64();
        return ms && now >= ms && now - ms <= kHalo2VehicleCameraSeatFreshMs;
    }
    return false;
}

// ---- the camera chosen for this seat ------------------------------------------------------------------------------

// A chase camera of the file is chosen for exactly the seat `key` names. Any thread; one atomic load while none is.
bool Halo2VehicleCameraChaseFor(const VehicleCameraSeatKey& key, vehicle_camera_select::ActiveCamera* out = nullptr)
{
    vehicle_camera_select::ActiveCamera camera;
    VehicleCameraSeatKey chosenFor;
    if (!VehicleCameraRead(camera, chosenFor) || !(chosenFor == key))
        return false;
    if (out)
        *out = camera;
    return true;
}

// ---- where a camera stands ----------------------------------------------------------------------------------------

// Counted by the observer, taken by the cold worker for its log lines (Halo2VehicleCamerasColdLog).
std::atomic<uint32_t> g_halo2VehicleCameraFrames{0};        // chase placements
std::atomic<uint32_t> g_halo2VehicleCameraStandInFrames{0}; // of them, "thirdperson" at the mod's stand-in
std::atomic<uint32_t> g_halo2VehicleCameraNudgeFrames{0};   // firstperson eyes moved by an offset
std::atomic<uint32_t> g_halo2VehicleCameraNudgeRefused{0};  // ...and offsets past the vehicle's bounds, not applied
std::atomic<uint32_t> g_halo2VehicleCameraHeadMisses{0};    // the occupant's head not read
std::atomic<uint32_t> g_halo2VehicleCameraFrameMisses{0};   // the vehicle's drawn root not read: the eye stays
std::atomic<uint32_t> g_halo2VehicleCameraLagTrailCm{0};
std::atomic<uint32_t> g_halo2VehicleCameraPivotSource{0};   // 1 the game's own pivot, 2 the head stood in for it
std::atomic<float> g_halo2VehicleCameraRestWu{0.0f};        // the game's camera's resting distance in use
std::atomic<float> g_halo2VehicleCameraRestRiseWu{0.0f};    // of it, the height above its pivot
std::atomic<float> g_halo2VehicleCameraRestPitchDeg{0.0f};
std::atomic<float> g_halo2VehicleCameraPivotUpWu{0.0f};     // the pivot's height above the vehicle's origin
std::atomic<float> g_halo2VehicleCameraPivotHeadCm{0.0f};   // and its distance from the occupant's head
std::atomic<float> g_halo2VehicleCameraRadiusCm{0.0f};      // the size a stand-in was laid out by

// Where the game's own camera stands looking level, as an elevation above level from its pivot in whole degrees, for
// the boom's start, with the seat it was measured in (Reach's ReachVehicleCameraLevel, for the same reader).
struct Halo2VehicleCameraLevel
{
    std::atomic<uint32_t> seq{0}; // even = stable
    VehicleCameraSeatKey seat{};
    float deg = vehicle_boom::kBoomPitchUnset;
};
Halo2VehicleCameraLevel g_halo2VehicleCameraLevel;

void Halo2VehicleCameraLevelStore(const VehicleCameraSeatKey& seat, float deg)
{
    Halo2VehicleCameraLevel& level = g_halo2VehicleCameraLevel;
    level.seq.fetch_add(1, std::memory_order_acq_rel);
    level.seat = seat;
    level.deg = deg;
    level.seq.fetch_add(1, std::memory_order_release);
}

bool Halo2VehicleCameraLevelElevationDeg(const VehicleCameraSeatKey& seat, float& deg)
{
    const Halo2VehicleCameraLevel& level = g_halo2VehicleCameraLevel;
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

// The orbit's check (observer writes, cold worker takes on leaving the seat): the nearest and furthest the game's
// camera stood from its pivot, in cm, and the lowest and highest it looked, in degrees.
struct Halo2VehicleCameraOrbitCheck
{
    std::atomic<uint32_t> seq{0};
    uint32_t samples = 0;
    float nearCm = 0.0f, farCm = 0.0f, lowDeg = 0.0f, highDeg = 0.0f;
};
Halo2VehicleCameraOrbitCheck g_halo2VehicleCameraOrbit;

void Halo2VehicleCameraOrbitNote(float distanceWu, float lookDeg)
{
    Halo2VehicleCameraOrbitCheck& check = g_halo2VehicleCameraOrbit;
    const float cm = distanceWu / vehicle_camera_frame::kWorldUnitsPerCm;
    check.seq.fetch_add(1, std::memory_order_acq_rel);
    if (!check.samples)
    {
        check.nearCm = check.farCm = cm;
        check.lowDeg = check.highDeg = lookDeg;
    }
    else
    {
        check.nearCm = cm < check.nearCm ? cm : check.nearCm;
        check.farCm = cm > check.farCm ? cm : check.farCm;
        check.lowDeg = lookDeg < check.lowDeg ? lookDeg : check.lowDeg;
        check.highDeg = lookDeg > check.highDeg ? lookDeg : check.highDeg;
    }
    ++check.samples;
    check.seq.fetch_add(1, std::memory_order_release);
}

float Halo2VehicleCameraSecondsSince(LARGE_INTEGER& at)
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

// An object's root node as drawn this frame (native_vehicle_first_person.cpp's query): its position and its frame.
bool Halo2VehicleCameraRoot(uint32_t object, float position[3], vehicle_camera_frame::Basis& basis)
{
    float m[13]{};
    if (object == UINT32_MAX || !NativeVehicleFirstPerson_Halo2Root(object, true, m))
        return false;
    vehicle_camera_frame::Basis b{};
    memcpy(b.forward, m + 1, sizeof(b.forward));
    memcpy(b.left, m + 4, sizeof(b.left));
    memcpy(b.up, m + 7, sizeof(b.up));
    if (!vehicle_camera_frame::Usable(b) || !vehicle_camera_place::Finite3(m + 10))
        return false;
    basis = b;
    memcpy(position, m + 10, 3 * sizeof(float));
    return true;
}

// A point in a frame's own terms (forward, left, up from its origin), and back.
void Halo2VehicleCameraToLocal(const float origin[3], const vehicle_camera_frame::Basis& basis, const float point[3],
                               float local[3])
{
    const float d[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    local[0] = vehicle_camera_frame::Dot(d, basis.forward);
    local[1] = vehicle_camera_frame::Dot(d, basis.left);
    local[2] = vehicle_camera_frame::Dot(d, basis.up);
}
void Halo2VehicleCameraToWorld(const float origin[3], const vehicle_camera_frame::Basis& basis, const float local[3],
                               float point[3])
{
    for (int axis = 0; axis < 3; ++axis)
        point[axis] = origin[axis] + basis.forward[axis] * local[0] + basis.left[axis] * local[1] +
                      basis.up[axis] * local[2];
}

// THE SEAT'S POINT: the occupant's head where it settled, as a point on the seat's own object (observer thread). Read
// for kHalo2SeatSettleFrames frames of the seat before it is held; until then the head as it is stands in.
struct Halo2VehicleCameraSeatEye
{
    VehicleCameraSeatKey seat{};
    uint32_t frames = 0;
    bool held = false;
    float local[3]{};
    // The head's frame in the seat's when it settled ("playerhead" turns with the head from there).
    bool restValid = false;
    vehicle_camera_frame::Basis rest{};
};
Halo2VehicleCameraSeatEye g_halo2VehicleCameraSeatEye;
inline constexpr uint32_t kHalo2SeatSettleFrames = 45;

// THE GAME'S OWN PIVOT for a kind of seat (observer thread): the point the first-person evaluator answers for it, as a
// point on the seat's own object -- kept for the session by the seat's vehicle model, seat and carriage, so a seat
// boarded straight into a chase camera (the seat memory) has it from an earlier ride.
struct Halo2VehicleCameraPivot
{
    bool valid = false;
    uint32_t identity = 0;
    int16_t seat = -1;
    bool mounted = false;
    uint32_t parent = UINT32_MAX; // for a vehicle the mod does not know (identity 0): this one only
    float local[3]{};
};
Halo2VehicleCameraPivot g_halo2VehicleCameraPivots[16];
uint32_t g_halo2VehicleCameraPivotNext = 0;
uint64_t g_halo2VehicleCameraPivotSeenUs = 0;

Halo2VehicleCameraPivot* Halo2VehicleCameraFindPivot(const VehicleCameraSeatKey& key, bool create)
{
    for (Halo2VehicleCameraPivot& pivot : g_halo2VehicleCameraPivots)
        if (pivot.valid && pivot.identity == key.identity && pivot.seat == key.seatIndex && pivot.mounted == key.mounted &&
            (key.identity != 0 || pivot.parent == static_cast<uint32_t>(key.vehicle)))
            return &pivot;
    if (!create)
        return nullptr;
    Halo2VehicleCameraPivot& slot = g_halo2VehicleCameraPivots[g_halo2VehicleCameraPivotNext++ % 16u];
    slot = Halo2VehicleCameraPivot{};
    slot.identity = key.identity;
    slot.seat = static_cast<int16_t>(key.seatIndex);
    slot.mounted = key.mounted;
    slot.parent = static_cast<uint32_t>(key.vehicle);
    return &slot;
}

// The game's own pivot point, newly answered by the first-person evaluator for this seat, onto the seat's object.
void Halo2VehicleCameraNotePivot(const Halo2VehicleCameraSeatSample& sample, const VehicleCameraSeatKey& key,
                                 const float seatOrigin[3], const vehicle_camera_frame::Basis& seatFrame)
{
    NativeVehicleSeatPoint native{};
    if (!NativeVehicleFirstPerson_Halo2NativePoint(native) || native.us == g_halo2VehicleCameraPivotSeenUs ||
        native.generation != sample.generation || native.parent != sample.parent || native.seat != sample.seat ||
        !vehicle_camera_place::Finite3(native.point))
        return;
    g_halo2VehicleCameraPivotSeenUs = native.us;
    float local[3];
    Halo2VehicleCameraToLocal(seatOrigin, seatFrame, native.point, local);
    // A point further from the seat's object than its own size and a margin is not its seat's camera marker.
    const float reach = (sample.radius > 0.0f ? sample.radius : 1.0f) + 2.0f;
    if (!vehicle_camera_place::Finite3(local) || vehicle_camera_frame::Dot(local, local) > reach * reach)
        return;
    // Eased in: the marker is fixed on its object, so a sample asked at another moment of the tick than the drawn
    // frame's (a fire's own ask is inside a tick) only wobbles about the point, and the mean is the point.
    if (Halo2VehicleCameraPivot* pivot = Halo2VehicleCameraFindPivot(key, true))
    {
        for (int axis = 0; axis < 3; ++axis)
            pivot->local[axis] = pivot->valid ? pivot->local[axis] + (local[axis] - pivot->local[axis]) * 0.1f
                                              : local[axis];
        pivot->valid = true;
    }
}

// THE GAME'S OWN THIRD-PERSON CAMERA in the seat, as last measured (observer thread): its distance from its pivot and
// its height above it, taken while it looked within about 15 degrees of level where it has (else at any look), as the
// furthest it stood -- the game pulls its camera in from walls and through its change of view. Reach's measurement
// (ReachVehicleCameraRest), on Halo 2's pivot.
struct Halo2VehicleCameraRest
{
    VehicleCameraSeatKey seat{};
    float distance = 0.0f;
    float rise = 0.0f;
    bool level = false;
    uint32_t waited = 0;
};
Halo2VehicleCameraRest g_halo2VehicleCameraRest;
inline constexpr float kHalo2RestKnownWu = 0.25f; // nearer than this the game's camera is still the seat's own view
inline constexpr float kHalo2RestFurthestWu = 8.0f;
inline constexpr uint32_t kHalo2RestWaitFrames = 45;
inline constexpr float kHalo2RestLevelSin = 0.26f;

vehicle_camera_place::State g_halo2VehicleCameraState;  // observer thread
vehicle_camera_place::LagState g_halo2VehicleCameraLag; // observer thread
LARGE_INTEGER g_halo2VehicleCameraLagAt{};              // observer thread

// This frame's placement, for the leash on the head and the aim ring later in the same frame on the same thread.
struct Halo2VehicleCameraPassRecord
{
    bool placed = false;
    float reach = 1.0f;
    vehicle_camera_select::ActiveCamera camera{};
    vehicle_camera_place::Placed where{};
    // For the aim ring (Halo2VehicleAimMarkerStep): the seat the camera was placed for, the seat's own point (where
    // the seat's aim is solved from, before the camera moved the view), the way the vehicle faces, and the way the
    // game's own camera looked this frame (its aim pitch).
    VehicleCameraSeatKey seat{};
    bool seatEyeValid = false;
    float seatEye[3]{};
    float vehicleForward[3]{};
    bool looksValid = false;
    float looks[3]{};
};
Halo2VehicleCameraPassRecord g_halo2VehicleCameraPass; // observer thread
// The seat a chase camera last placed the view in, and when (observer writes, any thread reads).
std::atomic<uint64_t> g_halo2VehicleCameraPlacedMs{0};
std::atomic<uint32_t> g_halo2VehicleCameraPlacedParent{UINT32_MAX};
std::atomic<int> g_halo2VehicleCameraPlacedSeat{-1};

// "collide": frames with a fresh trace, frames held short of something, frames with no trace to go on.
std::atomic<uint32_t> g_halo2VehicleCameraCollideFrames{0};
std::atomic<uint32_t> g_halo2VehicleCameraHeldFrames{0};
std::atomic<uint32_t> g_halo2VehicleCameraNoTraceFrames{0};
float g_halo2VehicleCameraReach = 1.0f;      // observer thread
LARGE_INTEGER g_halo2VehicleCameraReachAt{}; // observer thread
inline constexpr float kHalo2CameraReleasePerSecond = 1.5f;

// Fills `in` with the game's camera's place for a "thirdperson" camera. False: not known YET (the frame keeps the
// seat's eye). True with in.restValid false: it could not be read, and the mod's stand-in applies.
bool Halo2VehicleCameraRestPlace(const Halo2VehicleCameraSeatSample& sample, const VehicleCameraSeatKey& key,
                                 const vehicle_camera_select::ActiveCamera& camera, const float pivot[3],
                                 vehicle_camera_place::Inputs& in)
{
    Halo2VehicleCameraRest& rest = g_halo2VehicleCameraRest;
    if (!(rest.seat == key))
    {
        rest = {};
        rest.seat = key;
        Halo2VehicleCameraLevelStore(key, vehicle_boom::kBoomPitchUnset);
        Halo2VehicleCameraOrbitCheck& check = g_halo2VehicleCameraOrbit;
        check.seq.fetch_add(1, std::memory_order_acq_rel);
        check.samples = 0;
        check.seq.fetch_add(1, std::memory_order_release);
    }
    const float radiusWu = std::isfinite(in.vehicleRadiusCm) && in.vehicleRadiusCm > 0.0f
        ? in.vehicleRadiusCm * vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
    const float furthestWu = 4.0f * radiusWu > kHalo2RestFurthestWu ? 4.0f * radiusWu : kHalo2RestFurthestWu;
    // The game's own camera is the stock one only while the first-person override has stood down for the seat.
    float at[3]{}, looks[3]{};
    memcpy(at, sample.stockPosition, sizeof(at));
    memcpy(looks, sample.stockForward, sizeof(looks));
    const bool observed = pivot && vehicle_camera_place::Finite3(at) && vehicle_camera_place::Finite3(looks) &&
                          vehicle_camera_frame::Normalize(looks) &&
                          !NativeVehicleFirstPerson_SeatViewOwned(GameTitle::Halo2, sample.parent, sample.seat);
    if (observed)
    {
        const float stood[3] = {at[0] - pivot[0], at[1] - pivot[1], at[2] - pivot[2]};
        const float distance = sqrtf(vehicle_camera_frame::Dot(stood, stood));
        const float lookUp = vehicle_camera_frame::Dot(looks, in.vehicle.up);
        const float lookDeg =
            std::isfinite(lookUp) ? asinf(lookUp < -1.0f ? -1.0f : (lookUp > 1.0f ? 1.0f : lookUp)) * 57.29578f : 0.0f;
        const bool usable = std::isfinite(distance) && distance >= kHalo2RestKnownWu && distance <= furthestWu &&
                            std::isfinite(lookUp);
        // THE ORBIT'S CHECK, for the log on leaving the seat: how near and how far the game's camera stood from the
        // pivot over the looks it was seen at. An orbit of that pivot keeps the two close.
        if (usable)
            Halo2VehicleCameraOrbitNote(distance, lookDeg);
        const bool level = std::fabs(lookUp) <= kHalo2RestLevelSin;
        const bool take =
            usable && (level ? (!rest.level || distance > rest.distance) : (!rest.level && distance > rest.distance));
        if (take)
        {
            rest.distance = distance;
            rest.rise = vehicle_camera_frame::Dot(stood, in.vehicle.up);
            rest.level = level;
            if (level)
            {
                const float levelDeg = halo2_vehicle_camera::OrbitLevelElevationDeg(rest.rise, distance, lookDeg);
                // A degree either way is the measurement's own wobble: only a move of two restarts the boom.
                const float published = g_halo2VehicleCameraLevel.deg;
                if (vehicle_boom::BoomPitchSet(levelDeg) &&
                    (!vehicle_boom::BoomPitchSet(published) || std::fabs(levelDeg - published) >= 2.0f))
                    Halo2VehicleCameraLevelStore(key, levelDeg);
            }
            g_halo2VehicleCameraRestRiseWu.store(rest.rise, std::memory_order_relaxed);
            g_halo2VehicleCameraRestPitchDeg.store(lookDeg, std::memory_order_relaxed);
        }
    }
    // THE BOOM (vehicle_cameras.inl's shared boom, which this thread runs in Halo 2 as Halo 3's camera thread does
    // there): the cold worker's publication for this seat and camera -- an orbit of the pivot over Halo 2's own range
    // (halo2_vehicle_camera::BoomRange), starting at the player's kept angle, else the file's "boomPitch", else the
    // game's level view a little higher -- or, until one comes, the stand-in orbit over the same range. The right
    // stick raises and lowers it.
    Halo3BoomTake(key, camera.camera);
    Halo3BoomCamera& boom = g_halo3BoomCamera;
    if (!boom.held && !boom.local && rest.waited >= kHalo2RestWaitFrames)
        Halo3BoomStandIn(halo2_vehicle_camera::BoomRange());
    if (pivot && rest.distance >= kHalo2RestKnownWu && (boom.held || boom.local))
    {
        Halo3BoomStep();
        const float stood[3] = {0.0f, 0.0f, 0.0f}, unused[3] = {0.0f, 0.0f, 0.0f};
        Halo3BoomPublishCurrent(false, stood, unused);
        vehicle_boom::Offset(boom.shape, boom.pitch, rest.distance, in.restOffset);
        g_vehicleBoomLiveMs.store(GetTickCount64(), std::memory_order_release);
        in.restValid = true;
        memcpy(in.rest, pivot, sizeof(in.rest));
        g_halo2VehicleCameraRestWu.store(rest.distance, std::memory_order_relaxed);
        return true;
    }
    if (rest.waited < kHalo2RestWaitFrames)
    {
        ++rest.waited;
        return false;
    }
    in.restValid = false;
    return true;
}

// Observer thread, once a frame of a seat: where the chosen chase camera stands. False: none is chosen for this seat,
// or its place is not known yet; the seat's eye stays.
bool Halo2VehicleCameraPlace(const Halo2VehicleCameraSeatSample& sample, const VehicleCameraSeatKey& key,
                             const vehicle_camera_select::ActiveCamera& camera, float viewYaw, const float seatOrigin[3],
                             const vehicle_camera_frame::Basis& seatFrame, const float headPoint[3],
                             const vehicle_camera_frame::Basis* headBasis, float out[3])
{
    vehicle_camera_place::Inputs in;
    // The vehicle: the carrier a gun rides, else the seated object itself, as drawn this frame.
    const bool onCarrier = Halo2VehicleCameraOnCarrier(sample);
    if (onCarrier)
    {
        if (!Halo2VehicleCameraRoot(sample.root, in.vehicleOrigin, in.vehicle))
        {
            g_halo2VehicleCameraFrameMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }
    else
    {
        memcpy(in.vehicleOrigin, seatOrigin, sizeof(in.vehicleOrigin));
        in.vehicle = seatFrame;
    }
    // The vehicle's size: the carrier's where a gun rides one, so a stand-in stands off the carrier, not the gun.
    const float radiusWu = onCarrier && sample.carrierRadius > 0.0f ? sample.carrierRadius : sample.radius;
    in.vehicleRadiusCm = radiusWu > 0.0f ? radiusWu / vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
    g_halo2VehicleCameraRadiusCm.store(in.vehicleRadiusCm, std::memory_order_relaxed);

    // The seat: the occupant's head where it settled, held on the seat's own object.
    Halo2VehicleCameraSeatEye& eye = g_halo2VehicleCameraSeatEye;
    in.seatFrameValid = true;
    in.seatFrame = seatFrame;
    if (eye.held)
    {
        in.seatValid = true;
        Halo2VehicleCameraToWorld(seatOrigin, seatFrame, eye.local, in.seat);
    }
    else if (headPoint)
    {
        in.seatValid = true;
        memcpy(in.seat, headPoint, sizeof(in.seat));
    }
    // The head ("playerhead"): the marker as it is, its frame turned as far as the head has turned since it settled.
    if (headPoint)
    {
        in.headValid = true;
        memcpy(in.head, headPoint, sizeof(in.head));
        if (headBasis && eye.restValid)
        {
            const vehicle_camera_frame::Basis inSeat = vehicle_camera_frame::InFrame(seatFrame, *headBasis);
            in.headFrame = vehicle_camera_frame::TurnedSince(seatFrame, inSeat, eye.rest);
            in.headFrameValid = vehicle_camera_frame::Usable(in.headFrame);
        }
    }
    else if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Head))
    {
        g_halo2VehicleCameraHeadMisses.fetch_add(1, std::memory_order_relaxed);
    }
    in.viewYaw = viewYaw;

    if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::ThirdPerson))
    {
        // THE PIVOT: the game's own, kept for this kind of seat, on the seat's object as drawn; else the seat's point.
        float pivot[3]{};
        const float* pivotUsed = nullptr;
        if (const Halo2VehicleCameraPivot* kept = Halo2VehicleCameraFindPivot(key, false))
        {
            Halo2VehicleCameraToWorld(seatOrigin, seatFrame, kept->local, pivot);
            pivotUsed = pivot;
            g_halo2VehicleCameraPivotSource.store(1, std::memory_order_relaxed);
        }
        else if (in.seatValid)
        {
            memcpy(pivot, in.seat, sizeof(pivot));
            pivotUsed = pivot;
            g_halo2VehicleCameraPivotSource.store(2, std::memory_order_relaxed);
        }
        if (pivotUsed)
        {
            float fromVehicle[3];
            Halo2VehicleCameraToLocal(in.vehicleOrigin, in.vehicle, pivotUsed, fromVehicle);
            g_halo2VehicleCameraPivotUpWu.store(fromVehicle[2], std::memory_order_relaxed);
            if (headPoint)
            {
                const float apart[3]{pivotUsed[0] - headPoint[0], pivotUsed[1] - headPoint[1],
                                     pivotUsed[2] - headPoint[2]};
                g_halo2VehicleCameraPivotHeadCm.store(sqrtf(vehicle_camera_frame::Dot(apart, apart)) /
                                                          vehicle_camera_frame::kWorldUnitsPerCm,
                                                      std::memory_order_relaxed);
            }
        }
        if (!Halo2VehicleCameraRestPlace(sample, key, camera, pivotUsed, in))
            return false;
    }

    float point[3];
    vehicle_camera_place::Placed placed;
    if (!vehicle_camera_place::Place(camera, in, g_halo2VehicleCameraState, point, &placed))
        return false;

    // LAG: the boom from its pivot swings on a spring; the vehicle's travel never lags.
    const float seconds = Halo2VehicleCameraSecondsSince(g_halo2VehicleCameraLagAt);
    vehicle_camera_place::FollowLagOnBoom(camera, placed.pivot, point, seconds, g_halo2VehicleCameraLag, point);
    const float trailCm = g_halo2VehicleCameraLag.trailWu * 304.8f;
    const uint32_t trail =
        std::isfinite(trailCm) && trailCm > 0.0f ? static_cast<uint32_t>(trailCm < 1.0e8f ? trailCm : 1.0e8f) : 0u;
    uint32_t furthest = g_halo2VehicleCameraLagTrailCm.load(std::memory_order_relaxed);
    while (trail > furthest &&
           !g_halo2VehicleCameraLagTrailCm.compare_exchange_weak(furthest, trail, std::memory_order_relaxed))
    {
    }
    if (!vehicle_camera_place::Finite3(point))
        return false;

    // HELD SHORT OF A WALL ("collide"): the line from the pivot to the camera's place is traced by Halo 2's sight trace
    // on its simulation thread, and the camera stops short of what it meets. The trace is a tick or two behind; with
    // none to go on the camera goes all the way.
    float reachUsed = 1.0f;
    if (camera.collide)
    {
        const float toward[3] = {point[0] - placed.pivot[0], point[1] - placed.pivot[1], point[2] - placed.pivot[2]};
        const float length = sqrtf(vehicle_camera_frame::Dot(toward, toward));
        float wanted = 1.0f;
        if (std::isfinite(length) && length > 0.05f)
        {
            SightAuxRay ray{};
            ray.generation = sample.generation;
            for (int axis = 0; axis < 3; ++axis)
            {
                ray.origin[axis] = placed.pivot[axis];
                ray.dir[axis] = toward[axis] / length;
            }
            Sight_PublishCameraRay(GameTitle::Halo2, ray);
            bool hit = false;
            float rangeWu = 0.0f;
            if (Sight_ReadCameraTrace(GameTitle::Halo2, sample.generation, 150, hit, rangeWu))
            {
                wanted = vehicle_camera_place::CollideReach(hit, rangeWu, length, camera.collideMarginCm);
                g_halo2VehicleCameraCollideFrames.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                g_halo2VehicleCameraNoTraceFrames.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const float since = Halo2VehicleCameraSecondsSince(g_halo2VehicleCameraReachAt);
        const float reach = vehicle_camera_place::EaseReach(g_halo2VehicleCameraReach, wanted,
                                                            since < 0.0f ? 0.0f : (since > 0.1f ? 0.1f : since),
                                                            kHalo2CameraReleasePerSecond);
        if (reach < 0.999f)
        {
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = placed.pivot[axis] + toward[axis] * reach;
            g_halo2VehicleCameraHeldFrames.fetch_add(1, std::memory_order_relaxed);
            reachUsed = reach;
        }
    }
    else
    {
        g_halo2VehicleCameraReach = 1.0f;
    }
    Halo2VehicleCameraPassRecord& pass = g_halo2VehicleCameraPass;
    pass.reach = reachUsed;
    pass.placed = true;
    pass.camera = camera;
    pass.where = placed;
    pass.seat = key;
    pass.seatEyeValid = in.seatValid && vehicle_camera_place::Finite3(in.seat);
    memcpy(pass.seatEye, in.seat, sizeof(pass.seatEye));
    memcpy(pass.vehicleForward, in.vehicle.forward, sizeof(pass.vehicleForward));
    memcpy(pass.looks, sample.stockForward, sizeof(pass.looks));
    pass.looksValid = vehicle_camera_place::Finite3(pass.looks) && vehicle_camera_frame::Normalize(pass.looks);
    g_halo2VehicleCameraFrames.fetch_add(1, std::memory_order_relaxed);
    if (g_halo2VehicleCameraState.standIn)
        g_halo2VehicleCameraStandInFrames.fetch_add(1, std::memory_order_relaxed);
    memcpy(out, point, sizeof(point));
    return true;
}

// Observer thread, before the seat's view is built. False: no chase camera is chosen for this seat.
bool Halo2VehicleCamerasBegin(const Halo2VehicleCameraSeatSample& sample, bool& followHull)
{
    g_halo2VehicleCameraPass.placed = false;
    // The vehicle cameras run in a seat only while it has its first-person seat view, as in Halo 3 and Reach: with
    // vehicle_first_person off the seat is the game's own chase camera, and is published as no seat of theirs.
    if (!g_config.vehicle_first_person)
    {
        Halo2VehicleCameraPublishSeat(nullptr, sample.generation);
        return false;
    }
    Halo2VehicleCameraPublishSeat(&sample, sample.generation);
    vehicle_camera_select::ActiveCamera camera;
    if (!Halo2VehicleCameraChaseFor(Halo2VehicleCameraKey(sample), &camera))
        return false;
    followHull = camera.rotYaw;
    return true;
}

// Observer thread, once the seat's view is built: the eye moved or stood at a chase camera's place.
bool Halo2VehicleCamerasPlace(const Halo2VehicleCameraSeatSample& sample, float viewYaw, float eye[3])
{
    g_halo2VehicleCameraPass.placed = false;
    if (!g_config.vehicle_cameras || !g_config.vehicle_first_person || !eye)
        return false;
    const VehicleCameraSeatKey key = Halo2VehicleCameraKey(sample);
    // The seat's own object as drawn this frame: every camera of this seat is laid out on it.
    float seatOrigin[3]{};
    vehicle_camera_frame::Basis seatFrame{};
    if (!Halo2VehicleCameraRoot(sample.parent, seatOrigin, seatFrame))
    {
        g_halo2VehicleCameraFrameMisses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    // The game's own pivot, while the first-person evaluator answers it (the seat view is first person).
    Halo2VehicleCameraNotePivot(sample, key, seatOrigin, seatFrame);

    // The occupant's head, and the seat's point it settles into.
    NativeVehicleCameraOwner owner{sample.generation, sample.unit, sample.parent, sample.seat, 0, sample.identity,
                                   sample.root};
    float head[3]{}, headForward[3]{}, headUp[3]{};
    const bool headRead = NativeVehicleFirstPerson_Halo2Head(owner, head, headForward, headUp);
    vehicle_camera_frame::Basis headBasis{};
    const bool headBasisRead = headRead && vehicle_camera_frame::FromForwardUp(headForward, headUp, headBasis) &&
                               vehicle_camera_frame::Usable(headBasis);
    Halo2VehicleCameraSeatEye& seatEye = g_halo2VehicleCameraSeatEye;
    if (!(seatEye.seat == key))
        seatEye = Halo2VehicleCameraSeatEye{key};
    if (headRead && !seatEye.held && ++seatEye.frames >= kHalo2SeatSettleFrames)
    {
        Halo2VehicleCameraToLocal(seatOrigin, seatFrame, head, seatEye.local);
        seatEye.held = vehicle_camera_place::Finite3(seatEye.local);
        if (headBasisRead)
        {
            seatEye.rest = vehicle_camera_frame::InFrame(seatFrame, headBasis);
            seatEye.restValid = vehicle_camera_frame::Usable(seatEye.rest);
        }
    }

    // A FIRSTPERSON CAMERA'S "offset": the seat's eye moved along the seated object's own frame -- kept within its
    // bounds and a margin, as Halo 3 keeps its own (Halo3AnchorWithinBounds, 2 world units).
    float nudge[3];
    if (Halo3VehicleCameraEyeNudge(key.generation, key.vehicle, key.seatIndex, key.identity, key.mounted, nudge))
    {
        constexpr float kNudgeMarginWu = 2.0f;
        const float reachWu = (sample.radius > 0.0f ? sample.radius : 0.0f) + kNudgeMarginWu;
        float moved[3], fromRoot[3];
        bool finite = true;
        for (int axis = 0; axis < 3; ++axis)
        {
            moved[axis] = eye[axis] + seatFrame.forward[axis] * nudge[0] - seatFrame.left[axis] * nudge[1] +
                          seatFrame.up[axis] * nudge[2];
            fromRoot[axis] = moved[axis] - seatOrigin[axis];
            finite = finite && std::isfinite(moved[axis]);
        }
        if (finite && vehicle_camera_frame::Dot(fromRoot, fromRoot) <= reachWu * reachWu)
        {
            memcpy(eye, moved, sizeof(moved));
            g_halo2VehicleCameraNudgeFrames.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        g_halo2VehicleCameraNudgeRefused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // A CHASE CAMERA: the eye stands at its place.
    vehicle_camera_select::ActiveCamera camera;
    if (!Halo2VehicleCameraChaseFor(key, &camera))
        return false;
    float point[3];
    if (!Halo2VehicleCameraPlace(sample, key, camera, viewYaw, seatOrigin, seatFrame, headRead ? head : nullptr,
                                 headBasisRead ? &headBasis : nullptr, point))
        return false;
    memcpy(eye, point, sizeof(point));
    g_halo2VehicleCameraPlacedParent.store(sample.parent, std::memory_order_relaxed);
    g_halo2VehicleCameraPlacedSeat.store(sample.seat, std::memory_order_relaxed);
    g_halo2VehicleCameraPlacedMs.store(GetTickCount64(), std::memory_order_release);
    return true;
}

// The ring's frames in Halo 2, and those with a fresh trace of its ray, for the line on leaving the seat.
std::atomic<uint32_t> g_halo2VehicleAimMarkerFrames{0};
std::atomic<uint32_t> g_halo2VehicleAimMarkerTracedFrames{0};

// THE VEHICLE AIM RING ("aimMarker", vehicle_aim_marker_logic.h), as Halo 3 and Reach place it: observer thread, once
// the view is built from this frame's chase camera. `eye`, `forward` and `up` are the view the frame is drawn from
// (the tracked centre camera), `headQuat` the head's orientation in the headset's space that view was built with
// ([x, y, z, w]). The line the vehicle aims along is its heading at the game's own aim pitch, from the seat's own
// point; its depth is Halo 2's sight trace of that ray (Halo2SightAuxTraces); its direction in the headset's space is
// the view's: the direction from the eye in the view's own axes (right, up, back), turned by the head. The frame
// thread draws it (Game_GetVehicleAimMarker). Shares Halo 3's ring record: one title runs at a time.
void Halo2VehicleAimMarkerStep(const float eye[3], const float forward[3], const float up[3], const float headQuat[4])
{
    const Halo2VehicleCameraPassRecord& pass = g_halo2VehicleCameraPass;
    float dir[3];
    if (!g_config.vehicle_aim_marker || !eye || !forward || !up || !headQuat || !pass.placed || !pass.camera.aimMarker ||
        !pass.seatEyeValid || !pass.looksValid || !vehicle_aim_marker::Direction(pass.vehicleForward, pass.looks, dir))
    {
        Halo3VehicleAimMarkerClear();
        return;
    }
    if (!(pass.seat == g_halo3VehicleAimMarkerSeat))
    {
        g_halo3VehicleAimMarkerSeat = pass.seat;
        g_halo3VehicleAimMarkerRange.Reset();
    }
    SightAuxRay ray{};
    ray.generation = pass.seat.generation;
    for (int axis = 0; axis < 3; ++axis)
    {
        ray.origin[axis] = pass.seatEye[axis];
        ray.dir[axis] = dir[axis];
    }
    Sight_PublishMarkerRay(GameTitle::Halo2, ray);
    bool hit = false;
    float rangeWu = 0.0f;
    const bool traced = Sight_ReadMarkerTrace(GameTitle::Halo2, pass.seat.generation, kSightFreshMs, hit, rangeWu);
    const float range = g_halo3VehicleAimMarkerRange.Use(traced, hit, rangeWu, kSightTraceLengthWu);
    const float unitsPerVrMetre = Game_GetWorldScale();
    const float along = vehicle_aim_marker::PullBack(range, g_config.reticle_surface_offset_m * unitsPerVrMetre);
    float point[3], fromEye[3], lengthWu = 0.0f;
    vehicle_camera_frame::Basis view{};
    if (!(unitsPerVrMetre > 0.0f) || !shot_point::PointAlongRay(pass.seatEye, dir, along, point) ||
        !shot_point::FromHead(eye, point, fromEye, lengthWu) || !(lengthWu > 0.0f) ||
        !vehicle_camera_frame::FromForwardUp(forward, up, view))
    {
        Halo3VehicleAimMarkerClear();
        return;
    }
    // The view's own axes as the headset's are: x right (minus left), y up, z back.
    const float local[3]{-vehicle_camera_frame::Dot(fromEye, view.left), vehicle_camera_frame::Dot(fromEye, view.up),
                         -vehicle_camera_frame::Dot(fromEye, view.forward)};
    float tracking[3]{};
    {
        const float qx = headQuat[0], qy = headQuat[1], qz = headQuat[2], qw = headQuat[3];
        const float tx = 2.0f * (qy * local[2] - qz * local[1]);
        const float ty = 2.0f * (qz * local[0] - qx * local[2]);
        const float tz = 2.0f * (qx * local[1] - qy * local[0]);
        tracking[0] = local[0] + qw * tx + (qy * tz - qz * ty);
        tracking[1] = local[1] + qw * ty + (qz * tx - qx * tz);
        tracking[2] = local[2] + qw * tz + (qx * ty - qy * tx);
    }
    if (!vehicle_camera_place::Finite3(tracking) || !vehicle_camera_frame::Normalize(tracking))
    {
        Halo3VehicleAimMarkerClear();
        return;
    }
    Halo3VehicleAimMarkerMark& mark = g_halo3VehicleAimMarker;
    mark.seq.fetch_add(1, std::memory_order_acq_rel);
    mark.vrYaw = atan2f(tracking[0], -tracking[2]);
    mark.vrPitch = asinf(tracking[1] < -1.0f ? -1.0f : (tracking[1] > 1.0f ? 1.0f : tracking[1]));
    mark.distanceM = lengthWu / unitsPerVrMetre;
    mark.ms.store(GetTickCount64(), std::memory_order_relaxed);
    mark.seq.fetch_add(1, std::memory_order_release);
    g_halo3VehicleAimMarkerFrames.fetch_add(1, std::memory_order_relaxed);
    g_halo2VehicleAimMarkerFrames.fetch_add(1, std::memory_order_relaxed);
    if (!traced)
        g_halo3VehicleAimMarkerUntracedFrames.fetch_add(1, std::memory_order_relaxed);
    else
        g_halo2VehicleAimMarkerTracedFrames.fetch_add(1, std::memory_order_relaxed);
}

// ---- the cold worker's side ---------------------------------------------------------------------------------------

// The seat the player is in, for the cold worker. OnFoot: not in a seat the Halo 2 seat view runs in. Unknown: the
// observer's record is a moment stale or torn (the published camera is kept).
VehicleCameraSeatRead Halo2VehicleCameraSeat(VehicleCameraSeatKey& key, const char*& name, uint8_t& seatBits,
                                            bool& seatView, bool& wheelSeat, uint32_t& definitionIndex)
{
    Halo2VehicleCameraSeatRecord seat;
    if (!Halo2VehicleCameraReadSeat(seat))
        return VehicleCameraSeatRead::Unknown;
    if (!seat.seated || seat.generation != TitleAdapter_GetGeneration(GameTitle::Halo2))
        return VehicleCameraSeatRead::OnFoot;
    key = VehicleCameraSeatKey{seat.generation, static_cast<int32_t>(seat.parent), seat.seat,
                               halo2_vehicle_camera::KeyIdentity(seat.identity), seat.onCarrier};
    static char buffer[halo2_vehicle_camera::kNameCapacity];
    const char* model = halo2_vehicle_identity::Name(seat.identity);
    const char* carrier = seat.onCarrier ? halo2_vehicle_identity::Name(seat.carrierIdentity) : nullptr;
    name = halo2_vehicle_camera::SeatVehicleName(model, carrier, seat.onCarrier, seat.modelRead ? seat.bounds : nullptr,
                                                 seat.modelRead ? seat.nodes : 0u, buffer);
    seatBits = halo2_vehicle_camera::SeatBits(seat.flagsKnown, seat.seatFlags, seat.onCarrier, seat.seat);
    seatView = true; // every seat the Halo 2 seat view runs in has an eye
    wheelSeat = false; // Halo 2 has no wheel
    definitionIndex = halo2_vehicle_camera::KeyIdentity(seat.identity);

    // Once per seat: what the seat is, and the flags its role was read from.
    static VehicleCameraSeatKey logged{};
    if (!(logged == key))
    {
        logged = key;
        LOG("Vehicle cameras (Halo 2): %s, seat %d (%s): model %016llx%s, seat flags %s%08X, size %.3f wu%s", name,
            static_cast<int>(seat.seat), vehicle_camera_file::SeatText(seatBits).c_str(),
            static_cast<unsigned long long>(seat.identity), model ? "" : " (not one the mod knows)",
            seat.flagsKnown ? "" : "not read, ", seat.seatFlags, static_cast<double>(seat.radius),
            seat.onCarrier ? (carrier ? ", riding a carrier the mod knows" : ", riding a carrier") : "");
    }
    return VehicleCameraSeatRead::Seated;
}

// What a selected camera asks for that Halo 2 does not do yet, for the log and the readout; empty when nothing.
std::string Halo2VehicleCameraNotApplied(const vehicle_camera_select::ActiveCamera& camera)
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
            add("rotationTracking pitch (Halo 2's seat view keeps its own level heading)");
        if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Marker))
            add("an origin naming a marker (the vehicle's origin is used)");
    }
    if (camera.holdExposure)
        add("eyeAdjust");
    if (camera.hideRegions[0])
        add("hideRegions");
    if (camera.hideParts[0])
        add("hideParts");
    if (camera.fadeParts[0])
        add("fadeParts");
    return list;
}

// Cold worker, every poll while the vehicle cameras are on in Halo 2: the counts the observer keeps, as log lines
// when there is something to say. `seated` false = the player has left the seat.
void Halo2VehicleCamerasColdLog(bool seated)
{
    static bool standInLogged = false, noTraceLogged = false, nudgeRefusedLogged = false, frameMissLogged = false;
    static uint32_t pivotSourceLogged = 0;
    static float restLoggedWu = 0.0f, restSeenWu = 0.0f;
    static uint32_t trailStint = 0, headMissStint = 0, heldStint = 0, tracedStint = 0;
    static uint32_t ringStint = 0, ringTracedStint = 0;
    ringStint += g_halo2VehicleAimMarkerFrames.exchange(0, std::memory_order_relaxed);
    ringTracedStint += g_halo2VehicleAimMarkerTracedFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t collideFrames = g_halo2VehicleCameraCollideFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t noTraceFrames = g_halo2VehicleCameraNoTraceFrames.exchange(0, std::memory_order_relaxed);
    heldStint += g_halo2VehicleCameraHeldFrames.exchange(0, std::memory_order_relaxed);
    tracedStint += collideFrames;
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
    if (noTraceFrames > 60 && collideFrames == 0 && !noTraceLogged)
    {
        noTraceLogged = true;
        LOG("Vehicle cameras (Halo 2): this camera asks to be kept out of walls (\"collide\"), but Halo 2's sight "
            "trace is not running for this seat, so it goes all the way out");
    }
    const uint32_t frames = g_halo2VehicleCameraFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t standIn = g_halo2VehicleCameraStandInFrames.exchange(0, std::memory_order_relaxed);
    g_halo2VehicleCameraNudgeFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t nudgeRefused = g_halo2VehicleCameraNudgeRefused.exchange(0, std::memory_order_relaxed);
    const uint32_t frameMisses = g_halo2VehicleCameraFrameMisses.exchange(0, std::memory_order_relaxed);
    headMissStint += g_halo2VehicleCameraHeadMisses.exchange(0, std::memory_order_relaxed);
    const uint32_t trail = g_halo2VehicleCameraLagTrailCm.exchange(0, std::memory_order_relaxed);
    trailStint = trail > trailStint ? trail : trailStint;
    if (nudgeRefused && !nudgeRefusedLogged)
    {
        nudgeRefusedLogged = true;
        LOG("Vehicle cameras (Halo 2): this firstperson camera's offset would put the eye outside the vehicle's bounds, "
            "so it is not applied");
    }
    if (frameMisses > 30 && !frameMissLogged)
    {
        frameMissLogged = true;
        LOG("Vehicle cameras (Halo 2): the vehicle's drawn frame could not be read for this seat (the first-person seat "
            "view is not installed, or its marker query refused), so the seat keeps its own eye");
    }
    if (!seated)
    {
        if (trailStint)
            LOG("Vehicle cameras (Halo 2): in that seat the lagged camera trailed its place by at most %u cm", trailStint);
        if (headMissStint)
            LOG("Vehicle cameras (Halo 2): in that seat the occupant's head could not be read on %u frames, and the "
                "seat's point stood in for it",
                headMissStint);
        if (tracedStint)
            LOG("Vehicle cameras (Halo 2): in that seat the camera's line was traced for walls on %u frames and held "
                "short of something on %u of them",
                tracedStint, heldStint);
        if (ringStint)
            LOG("Vehicle cameras (Halo 2): in that seat the ring where the vehicle aims was placed on %u frames, %u of "
                "them at a freshly traced depth",
                ringStint, ringTracedStint);
        ringStint = ringTracedStint = 0;
        if (boomSeen)
            LOG("Vehicle cameras (Halo 2): in that seat the third-person boom went from %.1f to %.1f deg above level "
                "and was left at %.1f deg (kept for that seat and camera)",
                static_cast<double>(boomLowDeg), static_cast<double>(boomHighDeg), static_cast<double>(boomLastDeg));
        boomSeen = false;
        // The orbit's check: a camera that orbits the pivot stands about as far from it at every look.
        const Halo2VehicleCameraOrbitCheck& check = g_halo2VehicleCameraOrbit;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const uint32_t before = check.seq.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            const uint32_t samples = check.samples;
            const float nearCm = check.nearCm, farCm = check.farCm, lowDeg = check.lowDeg, highDeg = check.highDeg;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (check.seq.load(std::memory_order_relaxed) != before)
                continue;
            static uint32_t loggedSamples = 0;
            if (samples && samples != loggedSamples)
            {
                loggedSamples = samples;
                LOG("Vehicle cameras (Halo 2): in that seat the game's own camera stood %.0f to %.0f cm from its "
                    "pivot over %u frames, looking from %.1f to %.1f deg (an orbit of the pivot keeps the two close)",
                    static_cast<double>(nearCm), static_cast<double>(farCm), samples, static_cast<double>(lowDeg),
                    static_cast<double>(highDeg));
            }
            break;
        }
        trailStint = headMissStint = heldStint = tracedStint = 0;
        standInLogged = noTraceLogged = nudgeRefusedLogged = frameMissLogged = false;
        pivotSourceLogged = 0;
        restLoggedWu = restSeenWu = 0.0f;
        g_halo2VehicleCameraRestWu.store(0.0f, std::memory_order_relaxed);
        return;
    }
    if (standIn && !standInLogged)
    {
        standInLogged = true;
        float behindCm = 0.0f, aboveCm = 0.0f;
        vehicle_camera_place::StandInOffsetCm(g_halo2VehicleCameraRadiusCm.load(std::memory_order_relaxed), behindCm,
                                              aboveCm);
        LOG("Vehicle cameras (Halo 2): where the game's own third-person camera rests could not be read for this seat, "
            "so the camera stands %.0f cm behind and %.0f cm above the vehicle's origin instead",
            static_cast<double>(behindCm), static_cast<double>(aboveCm));
    }
    const uint32_t pivotSource = g_halo2VehicleCameraPivotSource.load(std::memory_order_relaxed);
    if (frames && pivotSource && pivotSource != pivotSourceLogged)
    {
        pivotSourceLogged = pivotSource;
        if (pivotSource == 1)
            LOG("Vehicle cameras (Halo 2): the third-person camera turns about the game's own pivot for this seat (its "
                "camera marker, read in the seat view), %.0f cm from the occupant's head and %.0f cm above the "
                "vehicle's origin",
                static_cast<double>(g_halo2VehicleCameraPivotHeadCm.load(std::memory_order_relaxed)),
                static_cast<double>(g_halo2VehicleCameraPivotUpWu.load(std::memory_order_relaxed) /
                                    vehicle_camera_frame::kWorldUnitsPerCm));
        else
            LOG("Vehicle cameras (Halo 2): the game's own pivot for this seat has not been read yet (it is, in the seat "
                "view), so the third-person camera turns about the occupant's head for now");
    }
    const float restWu = g_halo2VehicleCameraRestWu.load(std::memory_order_relaxed);
    const bool settled = restWu == restSeenWu;
    restSeenWu = restWu;
    if (frames && restWu > 0.0f && settled && restWu > restLoggedWu * 1.1f)
    {
        restLoggedWu = restWu;
        LOG("Vehicle cameras (Halo 2): the game's own third-person camera rests %.2f world units (%.0f cm) from its "
            "pivot in this seat, %.0f cm of that above the pivot (it looked %.1f deg up), and the pivot is %.0f cm "
            "above the vehicle's origin",
            static_cast<double>(restWu), static_cast<double>(restWu / vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_halo2VehicleCameraRestRiseWu.load(std::memory_order_relaxed) /
                                vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_halo2VehicleCameraRestPitchDeg.load(std::memory_order_relaxed)),
            static_cast<double>(g_halo2VehicleCameraPivotUpWu.load(std::memory_order_relaxed) /
                                vehicle_camera_frame::kWorldUnitsPerCm));
    }
}
