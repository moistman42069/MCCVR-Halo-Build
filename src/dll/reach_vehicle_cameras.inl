// VEHICLE CAMERAS IN REACH: Reach's side of vehicle_cameras.inl. Included by game.cpp inside the Reach render
// candidate, after the seat sampler (ReachSampleVehicleCameraFrame) and the seat's yaw follow, before the main render
// view that calls both.
//
// THREADS.
//   The render thread's main render view samples the seat and hands the sample to ReachVehicleCamerasPass, which
//     publishes the seat for the cold worker and, while a camera of the file is chosen for exactly that seat, moves
//     the seat's eye: a firstperson camera's "offset", or a chase camera's place. It reads the cold worker's
//     published camera with two atomic loads while none is chosen.
//   The cold worker (vehicle_cameras.inl, VehicleCameras_ColdTick) reads the published seat through
//     ReachVehicleCameraSeat: the vehicle's name, the seat's role, and the key every reader matches a camera to.
//
// WHAT A CHASE CAMERA CHANGES. The eye the seat view is rendered from (cameraBase), and with it the hands' origin
// (handsBase), stands at the camera's place. The occupant's body is drawn, as the flat game draws it: the hide of
// the player's own unit from the seat camera (ReachBeginUnitCameraPlayerHide) is skipped, and a passenger's seat
// keeps its own third-person camera word (ReachEnsureNativeFirstPersonSeat gives the first-person lease back, which is
// the seat's stock state). The camera's mode turns the view: its rotationTracking yaw stands in for
// vehicle_view_follow in that seat (ReachApplySeatYawFollow). Its leash holds the head (ReachApplyHeadLook), its
// "collide" and its aim ring are traced by Reach's sight trace (reach_sight_trace.inl), and a camera's "eyeAdjust":
// false holds the engine's exposure (engine_exposure_lock.inl). Nothing else the game reads is written.
//
// "thirdperson": the game's own camera for the seat is the stock camera of the frame (the render view's compact
// camera before the mod moves it); its pivot is the point unit_get_camera_info answers for the seat (measured: the
// game's camera orbits it at a constant distance, its elevation about 12 deg less the look in a Mongoose). Where the
// game's camera stands from that pivot gives the distance; the camera then orbits the pivot at the shared boom's
// elevation along the vehicle's heading (vehicle_boom_logic.h's orbit shape: Reach's camera track is not read).

// ---- the seat, published by the render thread for the cold worker ----------------------------------------------

struct ReachVehicleCameraSeatRecord
{
    std::atomic<uint32_t> seq{0}; // even = stable
    std::atomic<uint64_t> ms{0};  // the last publication; 0 = never
    bool seated = false;
    uint32_t generation = 0;
    int32_t vehicle = -1;
    int32_t seatIndex = -1;
    uint8_t identity = 0;
    uint8_t carrierIdentity = 0;
    bool onCarrier = false;
    uint32_t seatFlags = 0;
    uint32_t definitionDatum = 0xFFFFFFFFu;
    ReachVehicleFingerprint fingerprint{};
    int32_t physicsType = -1;
    float boundsRadius = 0.0f;
};
ReachVehicleCameraSeatRecord g_reachVehicleCameraSeat;
inline constexpr uint64_t kReachVehicleCameraSeatFreshMs = 250;

// The carrier's identity, kept per carrier object for the run (render thread).
struct ReachVehicleCameraCarrierCache
{
    uint32_t generation = 0;
    int32_t carrier = -1;
    ReachVehicleId identity = ReachVehicleId::Unknown;
};
ReachVehicleCameraCarrierCache g_reachVehicleCameraCarrier;

// The seat's key as every reader of the published camera matches it (VehicleCameraSeatKey).
VehicleCameraSeatKey ReachVehicleCameraKey(const ReachVehicleFrame& frame)
{
    return VehicleCameraSeatKey{g_reachCamera.generation.load(std::memory_order_acquire), frame.directParent,
                                frame.seatIndex, static_cast<uint32_t>(frame.identity),
                                frame.carrier != frame.directParent};
}

// Render thread. The identity of the object a gun rides, resolved once per carrier object.
ReachVehicleId ReachVehicleCameraCarrierIdentity(const ReachVehicleFrame& frame)
{
    if (frame.carrier == frame.directParent || frame.carrier == -1 || !g_reachCamera.tagGet)
        return ReachVehicleId::Unknown;
    const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
    ReachVehicleCameraCarrierCache& cache = g_reachVehicleCameraCarrier;
    if (cache.generation == generation && cache.carrier == frame.carrier)
        return cache.identity;
    ReachVehicleId identity = ReachVehicleId::Unknown;
    __try
    {
        uint8_t kind = 0;
        unsigned char* data = ReachVehicleObjectData(frame.carrier, kind);
        if (data && kind == kReachObjectKindVehicle)
        {
            const uint32_t datum = *reinterpret_cast<const uint32_t*>(data + kReachObjectDefinitionDatumOffset);
            const auto* definition =
                static_cast<const unsigned char*>(g_reachCamera.tagGet(kReachVehicleGroupTag, datum));
            if (definition)
                identity = ReachResolveVehicleDefinition(definition, datum);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        identity = ReachVehicleId::Unknown;
    }
    cache.generation = generation;
    cache.carrier = frame.carrier;
    cache.identity = identity;
    return identity;
}

void ReachVehicleCameraPublishSeat(const ReachVehicleFrame& frame)
{
    ReachVehicleCameraSeatRecord& record = g_reachVehicleCameraSeat;
    const ReachVehicleId carrier = frame.active ? ReachVehicleCameraCarrierIdentity(frame) : ReachVehicleId::Unknown;
    record.seq.fetch_add(1, std::memory_order_acq_rel);
    record.seated = frame.active;
    record.generation = g_reachCamera.generation.load(std::memory_order_acquire);
    record.vehicle = frame.directParent;
    record.seatIndex = frame.seatIndex;
    record.identity = static_cast<uint8_t>(frame.identity);
    record.carrierIdentity = static_cast<uint8_t>(carrier);
    record.onCarrier = frame.carrier != frame.directParent;
    record.seatFlags = frame.seatFlags;
    record.definitionDatum = frame.definitionDatum;
    record.fingerprint = frame.fingerprint;
    record.physicsType = frame.physicsType;
    record.boundsRadius = frame.boundsRadius;
    record.ms.store(GetTickCount64(), std::memory_order_relaxed);
    record.seq.fetch_add(1, std::memory_order_release);
}

bool ReachVehicleCameraReadSeat(ReachVehicleCameraSeatRecord& out)
{
    const ReachVehicleCameraSeatRecord& record = g_reachVehicleCameraSeat;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const uint32_t before = record.seq.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        const uint64_t ms = record.ms.load(std::memory_order_relaxed);
        out.seated = record.seated;
        out.generation = record.generation;
        out.vehicle = record.vehicle;
        out.seatIndex = record.seatIndex;
        out.identity = record.identity;
        out.carrierIdentity = record.carrierIdentity;
        out.onCarrier = record.onCarrier;
        out.seatFlags = record.seatFlags;
        out.definitionDatum = record.definitionDatum;
        out.fingerprint = record.fingerprint;
        out.physicsType = record.physicsType;
        out.boundsRadius = record.boundsRadius;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (record.seq.load(std::memory_order_relaxed) != before)
            continue;
        const uint64_t now = GetTickCount64();
        return ms && now >= ms && now - ms <= kReachVehicleCameraSeatFreshMs;
    }
    return false;
}

// ---- the camera chosen for this seat ------------------------------------------------------------------------------

// A chase camera is chosen for exactly the seat this sample shows. Render thread; one atomic load while none is.
bool ReachVehicleCameraChaseFor(const ReachVehicleFrame& frame)
{
    if (!frame.active)
        return false;
    vehicle_camera_select::ActiveCamera camera;
    VehicleCameraSeatKey chosenFor;
    return VehicleCameraRead(camera, chosenFor) && chosenFor == ReachVehicleCameraKey(frame);
}

// How a chosen chase camera turns the view in this seat: its mode's rotationTracking. Yaw follows the hull's
// heading; pitch follows it too where the seat takes the hull's pitch at all, and only with yaw (as in Halo 3).
// False: no chase camera is chosen for it, and vehicle_view_follow decides.
bool ReachVehicleCameraViewTurn(const ReachVehicleFrame& frame, bool& yaw, bool& pitch)
{
    if (!frame.active)
        return false;
    vehicle_camera_select::ActiveCamera camera;
    VehicleCameraSeatKey chosenFor;
    if (!VehicleCameraRead(camera, chosenFor) || !(chosenFor == ReachVehicleCameraKey(frame)))
        return false;
    yaw = camera.rotYaw;
    pitch = camera.rotYaw && camera.rotPitch;
    return true;
}

// The last frame a chosen Reach chase camera turned the view with the hull (the shared follow reader's master gate,
// Halo3VehicleCameraFollowWanted). Render thread writes; any thread reads.
std::atomic<uint64_t> g_reachVehicleCameraFollowMs{0};
inline constexpr uint64_t kReachVehicleCameraFollowFreshMs = 200;

bool ReachVehicleCameraFollowWanted()
{
    const uint64_t ms = g_reachVehicleCameraFollowMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    return ms && now >= ms && now - ms <= kReachVehicleCameraFollowFreshMs;
}

// ---- where a chase camera stands ----------------------------------------------------------------------------------

// Counted by the render thread, taken by the cold worker for its log lines (ReachVehicleCamerasColdLog).
std::atomic<uint32_t> g_reachVehicleCameraFrames{0};        // chase placements
std::atomic<uint32_t> g_reachVehicleCameraStandInFrames{0}; // of them, "thirdperson" at the mod's stand-in
std::atomic<uint32_t> g_reachVehicleCameraNudgeFrames{0};   // firstperson eyes moved by an offset
std::atomic<uint32_t> g_reachVehicleCameraHeadMisses{0};    // "playerhead" with no head marker read
std::atomic<uint32_t> g_reachVehicleCameraLagTrailCm{0};
std::atomic<float> g_reachVehicleCameraRestWu{0.0f};    // the game's camera's resting distance in use (0 = none yet)
std::atomic<float> g_reachVehicleCameraRestRiseWu{0.0f}; // of it, the height above its pivot
std::atomic<float> g_reachVehicleCameraRestPitchDeg{0.0f};
std::atomic<float> g_reachVehicleCameraPivotUpWu{0.0f}; // the pivot's height above the vehicle's origin
// Where the game's own camera stands looking level, as an elevation above level from its pivot in whole degrees, for
// the boom's start (vehicle_cameras.inl's VehicleBoomColdTick), with the seat it was measured in: unset until that
// seat's camera has been seen looking level. Render thread writes (the only writer, so it reads its own fields
// plainly), cold worker reads through the sequence.
struct ReachVehicleCameraLevel
{
    std::atomic<uint32_t> seq{0}; // even = stable
    VehicleCameraSeatKey seat{};
    float deg = vehicle_boom::kBoomPitchUnset;
};
ReachVehicleCameraLevel g_reachVehicleCameraLevel;

void ReachVehicleCameraLevelStore(const VehicleCameraSeatKey& seat, float deg)
{
    ReachVehicleCameraLevel& level = g_reachVehicleCameraLevel;
    level.seq.fetch_add(1, std::memory_order_acq_rel);
    level.seat = seat;
    level.deg = deg;
    level.seq.fetch_add(1, std::memory_order_release);
}

// Cold worker: the level view measured in `seat`; unset when the measurement in hand is another seat's (the render
// thread has not yet placed a camera in this one). False: the record was being written both times it was read.
bool ReachVehicleCameraLevelElevationDeg(const VehicleCameraSeatKey& seat, float& deg)
{
    const ReachVehicleCameraLevel& level = g_reachVehicleCameraLevel;
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

vehicle_camera_place::State g_reachVehicleCameraState;  // render thread
vehicle_camera_place::LagState g_reachVehicleCameraLag; // render thread
LARGE_INTEGER g_reachVehicleCameraLagAt{};              // render thread

// This frame's placement, for the leash on the head (ReachVehicleCameraLeashHead), later in the same frame on the
// same thread: ReachApplyHeadLook builds the head-centre camera after the pass.
struct ReachVehicleCameraPassRecord
{
    bool placed = false;
    float reach = 1.0f; // how much of the way out the wall let the camera go
    vehicle_camera_select::ActiveCamera camera{};
    vehicle_camera_place::Placed where{};
    // For the aim ring (ReachVehicleAimMarkerStep): the seat the camera was placed for, the seat's own eye before the
    // camera moved it (the eye the seat's steering and aim are solved from), the way the vehicle faces, and the way
    // the game's own camera looked this frame (its aim pitch).
    VehicleCameraSeatKey seat{};
    float seatEye[3]{};
    float vehicleForward[3]{};
    bool looksValid = false;
    float looks[3]{};
};
ReachVehicleCameraPassRecord g_reachVehicleCameraPass; // render thread

// "collide": frames with a fresh trace, frames held short of something, frames with no trace to go on (counted by
// the render thread, taken by the cold worker); and how much of the way out the camera may go, eased.
std::atomic<uint32_t> g_reachVehicleCameraCollideFrames{0};
std::atomic<uint32_t> g_reachVehicleCameraHeldFrames{0};
std::atomic<uint32_t> g_reachVehicleCameraNoTraceFrames{0};
float g_reachVehicleCameraReach = 1.0f;      // render thread
LARGE_INTEGER g_reachVehicleCameraReachAt{}; // render thread
inline constexpr float kReachCameraReleasePerSecond = 1.5f;

// Render thread, in ReachApplyHeadLook: the player's head movement `head` (world units, added to the eye) held
// inside the chase camera's leash. False when this frame placed no chase camera or the camera sets no leash.
bool ReachVehicleCameraLeashHead(float head[3])
{
    const ReachVehicleCameraPassRecord& pass = g_reachVehicleCameraPass;
    return pass.placed && vehicle_camera_place::LeashHead(pass.camera, pass.where, pass.reach, head);
}

// THE GAME'S OWN THIRD-PERSON CAMERA in the seat, as last measured (render thread): its distance from its pivot and
// its height above it, taken while it looked within about 15 degrees of level where it has (else at any look), as
// the furthest it stood -- the game pulls its camera in from walls and through a change of view. The pivot is kept
// as a point on the vehicle, its movement there smoothed, and rebuilt every frame from the vehicle as drawn, so the
// camera moves on one clock.
struct ReachVehicleCameraRest
{
    VehicleCameraSeatKey seat{};
    float distance = 0.0f;
    float rise = 0.0f;
    bool level = false;
    uint32_t waited = 0;
    bool placeKept = false;
    float pivotLocal[3]{};
    LARGE_INTEGER pivotAt{};
};
ReachVehicleCameraRest g_reachVehicleCameraRest;
inline constexpr float kReachRestKnownWu = 0.25f;  // nearer than this the game's camera is still the seat's own view
inline constexpr float kReachRestFurthestWu = 8.0f;
inline constexpr float kReachRestPivotReachWu = 10.0f;
inline constexpr uint32_t kReachRestWaitFrames = 45;
inline constexpr float kReachRestLevelSin = 0.26f;
inline constexpr float kReachRestPivotSmoothSeconds = 0.25f;

// The orbit's check (render thread writes, cold worker takes on leaving the seat): the nearest and furthest the game's
// camera stood from its pivot, in cm, and the lowest and highest it looked, in degrees.
struct ReachVehicleCameraOrbitCheck
{
    std::atomic<uint32_t> seq{0};
    uint32_t samples = 0;
    float nearCm = 0.0f, farCm = 0.0f, lowDeg = 0.0f, highDeg = 0.0f;
};
ReachVehicleCameraOrbitCheck g_reachVehicleCameraOrbit;

void ReachVehicleCameraOrbitNote(float distanceWu, float lookDeg)
{
    ReachVehicleCameraOrbitCheck& check = g_reachVehicleCameraOrbit;
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

float ReachVehicleCameraSecondsSince(LARGE_INTEGER& at)
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

// Fills `in` with the game's camera's place for a "thirdperson" camera. False: not known YET (the frame keeps the
// seat's own eye). True with in.restValid false: it could not be read, and the mod's stand-in applies.
bool ReachVehicleCameraRestPlace(const ReachVehicleFrame& frame, const VehicleCameraSeatKey& seat,
                                 const vehicle_camera_select::ActiveCamera& camera, const unsigned char* stockCompact,
                                 vehicle_camera_place::Inputs& in)
{
    ReachVehicleCameraRest& rest = g_reachVehicleCameraRest;
    if (!(rest.seat == seat))
    {
        rest = {};
        rest.seat = seat;
        ReachVehicleCameraLevelStore(seat, vehicle_boom::kBoomPitchUnset);
        ReachVehicleCameraOrbitCheck& check = g_reachVehicleCameraOrbit;
        check.seq.fetch_add(1, std::memory_order_acq_rel);
        check.samples = 0;
        check.seq.fetch_add(1, std::memory_order_release);
    }
    const float radiusWu = std::isfinite(in.vehicleRadiusCm) && in.vehicleRadiusCm > 0.0f
        ? in.vehicleRadiusCm * vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
    const float pivotReachWu = 2.0f * radiusWu > kReachRestPivotReachWu ? 2.0f * radiusWu : kReachRestPivotReachWu;
    const float furthestWu = 4.0f * radiusWu > kReachRestFurthestWu ? 4.0f * radiusWu : kReachRestFurthestWu;
    float at[3]{}, looks[3]{};
    bool observed = frame.nativeCameraValid && stockCompact != nullptr;
    if (observed)
    {
        memcpy(at, stockCompact + 0x00, sizeof(at));
        memcpy(looks, stockCompact + 0x0C, sizeof(looks));
        const float* pivot = frame.nativeCameraPoint;
        const float fromVehicle[3] = {pivot[0] - in.vehicleOrigin[0], pivot[1] - in.vehicleOrigin[1],
                                      pivot[2] - in.vehicleOrigin[2]};
        observed = vehicle_camera_place::Finite3(at) && vehicle_camera_place::Finite3(looks) &&
                   vehicle_camera_frame::Normalize(looks) &&
                   vehicle_camera_frame::Dot(fromVehicle, fromVehicle) <= pivotReachWu * pivotReachWu;
    }
    if (observed)
    {
        const float stood[3] = {at[0] - frame.nativeCameraPoint[0], at[1] - frame.nativeCameraPoint[1],
                                at[2] - frame.nativeCameraPoint[2]};
        const float distance = sqrtf(vehicle_camera_frame::Dot(stood, stood));
        const float lookUp = vehicle_camera_frame::Dot(looks, in.vehicle.up);
        // THE ORBIT'S CHECK, for the log on leaving the seat: how near and how far the game's camera stood from the
        // pivot over the looks it was seen at, beyond the seat's own view. An orbit of that pivot keeps the two close.
        if (std::isfinite(distance) && distance >= kReachRestKnownWu && distance <= furthestWu && std::isfinite(lookUp))
        {
            const float lookDeg = asinf(lookUp < -1.0f ? -1.0f : (lookUp > 1.0f ? 1.0f : lookUp)) * 57.29578f;
            ReachVehicleCameraOrbitNote(distance, lookDeg);
        }
        const bool level = std::fabs(lookUp) <= kReachRestLevelSin;
        const bool take = std::isfinite(distance) && distance >= kReachRestKnownWu && distance <= furthestWu &&
                          (level ? (!rest.level || distance > rest.distance) : (!rest.level && distance > rest.distance));
        if (take)
        {
            rest.distance = distance;
            rest.rise = vehicle_camera_frame::Dot(stood, in.vehicle.up);
            rest.level = level;
            // The game's camera ORBITS its pivot, so where it stands looking level is the elevation it stood at plus
            // the look it had then (reach_vehicle_camera::OrbitLevelElevationDeg).
            if (level)
            {
                const float lookDeg = asinf(lookUp < -1.0f ? -1.0f : (lookUp > 1.0f ? 1.0f : lookUp)) * 57.29578f;
                const float levelDeg = reach_vehicle_camera::OrbitLevelElevationDeg(rest.rise, distance, lookDeg);
                // A degree either way is the measurement's own wobble (seen in game: 12, then 11, as the camera
                // eased out), and each change restarts a boom the player has not moved: only a move of two counts.
                const float published = g_reachVehicleCameraLevel.deg;
                if (vehicle_boom::BoomPitchSet(levelDeg) &&
                    (!vehicle_boom::BoomPitchSet(published) || std::fabs(levelDeg - published) >= 2.0f))
                    ReachVehicleCameraLevelStore(seat, levelDeg);
            }
            g_reachVehicleCameraRestRiseWu.store(rest.rise, std::memory_order_relaxed);
            g_reachVehicleCameraRestPitchDeg.store(
                asinf(lookUp < -1.0f ? -1.0f : (lookUp > 1.0f ? 1.0f : lookUp)) * 57.29578f, std::memory_order_relaxed);
        }
        // The pivot as a point on the vehicle.
        const float pivot[3] = {frame.nativeCameraPoint[0] - in.vehicleOrigin[0],
                                frame.nativeCameraPoint[1] - in.vehicleOrigin[1],
                                frame.nativeCameraPoint[2] - in.vehicleOrigin[2]};
        const float read[3] = {vehicle_camera_frame::Dot(pivot, in.vehicle.forward),
                               vehicle_camera_frame::Dot(pivot, in.vehicle.left),
                               vehicle_camera_frame::Dot(pivot, in.vehicle.up)};
        const float seconds = ReachVehicleCameraSecondsSince(rest.pivotAt);
        if (rest.placeKept && seconds >= 0.0f && seconds <= 0.5f)
        {
            const float blend = 1.0f - std::exp(-seconds / kReachRestPivotSmoothSeconds);
            for (int axis = 0; axis < 3; ++axis)
                rest.pivotLocal[axis] += (read[axis] - rest.pivotLocal[axis]) * blend;
        }
        else
        {
            memcpy(rest.pivotLocal, read, sizeof(rest.pivotLocal));
        }
        rest.placeKept = true;
        g_reachVehicleCameraPivotUpWu.store(read[2], std::memory_order_relaxed);
    }
    // THE BOOM (vehicle_cameras.inl's shared boom, which this thread runs in Reach as Halo 3's camera thread does
    // there): the cold worker's publication for this seat and camera -- an orbit of the pivot over the flat game's
    // look range, starting at the player's kept angle, else the file's "boomPitch", else the game's level view a
    // little higher -- or, until one comes, the stand-in orbit. The right stick raises and lowers it.
    Halo3BoomTake(seat, camera.camera);
    Halo3BoomCamera& boom = g_halo3BoomCamera;
    if (!boom.held && !boom.local && rest.waited >= kReachRestWaitFrames)
        Halo3BoomStandIn();
    if (rest.placeKept && rest.distance >= kReachRestKnownWu && (boom.held || boom.local))
    {
        Halo3BoomStep();
        const float stood[3] = {0.0f, 0.0f, 0.0f}, looks[3] = {0.0f, 0.0f, 0.0f};
        Halo3BoomPublishCurrent(false, stood, looks);
        vehicle_boom::Offset(boom.shape, boom.pitch, rest.distance, in.restOffset);
        g_vehicleBoomLiveMs.store(GetTickCount64(), std::memory_order_release);
        in.restValid = true;
        for (int axis = 0; axis < 3; ++axis)
            in.rest[axis] = in.vehicleOrigin[axis] + in.vehicle.forward[axis] * rest.pivotLocal[0] +
                            in.vehicle.left[axis] * rest.pivotLocal[1] + in.vehicle.up[axis] * rest.pivotLocal[2];
        g_reachVehicleCameraRestWu.store(rest.distance, std::memory_order_relaxed);
        return true;
    }
    if (rest.waited < kReachRestWaitFrames)
    {
        ++rest.waited;
        return false;
    }
    in.restValid = false;
    return true;
}

// The basis of a root node as Reach draws it, made orthonormal.
bool ReachVehicleCameraBasis(const float forward[3], const float up[3], vehicle_camera_frame::Basis& out)
{
    return vehicle_camera_frame::FromForwardUp(forward, up, out) && vehicle_camera_frame::Usable(out);
}

// Render thread, once a frame of a seat: where the chosen chase camera stands. False: none is chosen for this seat,
// or its place is not known yet; the seat's own eye stays.
bool ReachVehicleCameraPlace(const ReachVehicleFrame& frame, const VehicleCameraSeatKey& key,
                             const unsigned char* stockCompact, float out[3])
{
    vehicle_camera_select::ActiveCamera camera;
    VehicleCameraSeatKey chosenFor;
    if (!VehicleCameraRead(camera, chosenFor) || !(chosenFor == key) || !frame.rootValid)
        return false;

    vehicle_camera_place::Inputs in;
    // The vehicle: the carrier a gun rides, else the seated object itself. The seat's own frame is the seated
    // object's root, so a camera measured from the seat of a gun turns with the gun.
    const bool carried = frame.carrierRootValid && frame.carrier != frame.directParent;
    if (carried)
    {
        if (!ReachVehicleCameraBasis(frame.carrierForward, frame.carrierUp, in.vehicle))
            return false;
        memcpy(in.vehicleOrigin, frame.carrierPos, sizeof(in.vehicleOrigin));
    }
    else
    {
        if (!ReachVehicleCameraBasis(frame.rootForward, frame.rootUp, in.vehicle))
            return false;
        memcpy(in.vehicleOrigin, frame.rootPos, sizeof(in.vehicleOrigin));
    }
    in.vehicleRadiusCm = frame.boundsRadius > 0.0f ? frame.boundsRadius / vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
    in.seatValid = true;
    memcpy(in.seat, frame.seatPoint, sizeof(in.seat));
    in.seatFrameValid = ReachVehicleCameraBasis(frame.rootForward, frame.rootUp, in.seatFrame);
    // The occupant's head (marker 0xC2, as the seat sampler resolves it), read only for a camera measured from it.
    in.headValid = false;
    if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Head) && g_reachCamera.objectMarkers)
    {
        Halo3ObjectMarker head{};
        __try
        {
            in.headValid = g_reachCamera.objectMarkers(frame.unitHandle,
                                                       static_cast<int32_t>(kReachOccupantHeadMarkerStringId), &head,
                                                       1, false, g_config.vehicle_cam_smoothing) > 0 &&
                           Halo3MatrixValid(head.matrix);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            in.headValid = false;
        }
        if (in.headValid)
        {
            memcpy(in.head, head.matrix.position, sizeof(in.head));
            // THE HEAD AS A FRAME ("playerhead", as Halo 3 lays it out): the seat's frame turned as far as the head
            // has turned in it since it settled, so the camera turns and nods with the head and the marker's own axis
            // convention never matters. Settled = read in this seat for kReachHeadSettleFrames frames.
            static constexpr uint32_t kReachHeadSettleFrames = 45;
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
            if (in.seatFrameValid && ReachVehicleCameraBasis(head.matrix.forward, head.matrix.up, headBasis))
            {
                const vehicle_camera_frame::Basis inSeat = vehicle_camera_frame::InFrame(in.seatFrame, headBasis);
                if (!headRef.restValid && ++headRef.frames >= kReachHeadSettleFrames)
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
        else
        {
            g_reachVehicleCameraHeadMisses.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (!in.headValid)
    {
        // The seat's own eye stands in for the head.
        in.headValid = true;
        memcpy(in.head, frame.cameraBase, sizeof(in.head));
    }
    in.viewYaw = g_gameYawRef;
    if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::ThirdPerson) &&
        !ReachVehicleCameraRestPlace(frame, key, camera, stockCompact, in))
        return false;

    float point[3];
    vehicle_camera_place::Placed placed;
    if (!vehicle_camera_place::Place(camera, in, g_reachVehicleCameraState, point, &placed))
        return false;

    // LAG: the boom from its pivot swings on a spring; the vehicle's travel never lags.
    const float seconds = ReachVehicleCameraSecondsSince(g_reachVehicleCameraLagAt);
    vehicle_camera_place::FollowLagOnBoom(camera, placed.pivot, point, seconds, g_reachVehicleCameraLag, point);
    const float trailCm = g_reachVehicleCameraLag.trailWu * 304.8f;
    const uint32_t trail =
        std::isfinite(trailCm) && trailCm > 0.0f ? static_cast<uint32_t>(trailCm < 1.0e8f ? trailCm : 1.0e8f) : 0u;
    uint32_t furthest = g_reachVehicleCameraLagTrailCm.load(std::memory_order_relaxed);
    while (trail > furthest &&
           !g_reachVehicleCameraLagTrailCm.compare_exchange_weak(furthest, trail, std::memory_order_relaxed))
    {
    }
    if (!vehicle_camera_place::Finite3(point))
        return false;

    // HELD SHORT OF A WALL ("collide"): the line from the pivot to the camera's place is traced by Reach's own sight
    // trace on its simulation thread (reach_sight_trace.inl), and the camera stops short of what it meets. The trace
    // is a tick or two behind; with none to go on the camera goes all the way.
    float reachUsed = 1.0f;
    if (camera.collide)
    {
        const float toward[3] = {point[0] - placed.pivot[0], point[1] - placed.pivot[1], point[2] - placed.pivot[2]};
        const float length = sqrtf(vehicle_camera_frame::Dot(toward, toward));
        float wanted = 1.0f;
        if (std::isfinite(length) && length > 0.05f)
        {
            const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::HaloReach);
            SightAuxRay ray{};
            ray.generation = generation;
            for (int axis = 0; axis < 3; ++axis)
            {
                ray.origin[axis] = placed.pivot[axis];
                ray.dir[axis] = toward[axis] / length;
            }
            Sight_PublishCameraRay(GameTitle::HaloReach, ray);
            bool hit = false;
            float rangeWu = 0.0f;
            if (Sight_ReadCameraTrace(GameTitle::HaloReach, generation, 150, hit, rangeWu))
            {
                wanted = vehicle_camera_place::CollideReach(hit, rangeWu, length, camera.collideMarginCm);
                g_reachVehicleCameraCollideFrames.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                g_reachVehicleCameraNoTraceFrames.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const float since = ReachVehicleCameraSecondsSince(g_reachVehicleCameraReachAt);
        const float reach = vehicle_camera_place::EaseReach(g_reachVehicleCameraReach, wanted,
                                                            since < 0.0f ? 0.0f : (since > 0.1f ? 0.1f : since),
                                                            kReachCameraReleasePerSecond);
        if (reach < 0.999f)
        {
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = placed.pivot[axis] + toward[axis] * reach;
            g_reachVehicleCameraHeldFrames.fetch_add(1, std::memory_order_relaxed);
            reachUsed = reach;
        }
    }
    else
    {
        g_reachVehicleCameraReach = 1.0f;
    }
    ReachVehicleCameraPassRecord& pass = g_reachVehicleCameraPass;
    pass.reach = reachUsed;
    pass.placed = true;
    pass.camera = camera;
    pass.where = placed;
    pass.seat = key;
    memcpy(pass.seatEye, frame.cameraBase, sizeof(pass.seatEye));
    memcpy(pass.vehicleForward, frame.rawCarrierForward, sizeof(pass.vehicleForward));
    pass.looksValid = false;
    if (stockCompact)
    {
        memcpy(pass.looks, stockCompact + 0x0C, sizeof(pass.looks));
        pass.looksValid = vehicle_camera_place::Finite3(pass.looks) && vehicle_camera_frame::Normalize(pass.looks);
    }
    g_reachVehicleCameraFrames.fetch_add(1, std::memory_order_relaxed);
    if (g_reachVehicleCameraState.standIn)
        g_reachVehicleCameraStandInFrames.fetch_add(1, std::memory_order_relaxed);
    memcpy(out, point, sizeof(point));
    return true;
}

// Render thread, every main render view: after the seat is sampled and its yaw followed, before the head-centre
// camera is built from it. `stockCompact` is the frame's camera as the game computed it.
// A CAMERA'S "eyeAdjust": false for exactly this seat (any camera type). One atomic load while no camera in use asks
// for it; a camera being published both times it is read answers false for that frame.
bool ReachVehicleCameraHoldsExposure(const VehicleCameraSeatKey& key)
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

void ReachVehicleCamerasPass(ReachVehicleFrame& frame, const unsigned char* stockCompact)
{
    g_reachVehicleCameraPass.placed = false;
    ReachVehicleCameraPublishSeat(frame);
    // "eyeAdjust": false holds Reach's eye adaptation (render_exposure_lock, engine_exposure_lock.inl) while the
    // camera in use for this seat says so; every other frame gives it back to the engine. A compare a frame.
    EngineExposureLockApply(g_reachExposure, frame.active && ReachVehicleCameraHoldsExposure(ReachVehicleCameraKey(frame)));
    if (!frame.active)
        return;
    const VehicleCameraSeatKey key = ReachVehicleCameraKey(frame);

    // A FIRSTPERSON CAMERA'S "offset": the seat's eye moved along the seated object's own frame.
    float nudge[3];
    if (Halo3VehicleCameraEyeNudge(key.generation, key.vehicle, key.seatIndex, key.identity, key.mounted, nudge))
    {
        vehicle_camera_frame::Basis basis{};
        if (frame.rootValid && ReachVehicleCameraBasis(frame.rootForward, frame.rootUp, basis))
        {
            float moved[3], hands[3];
            bool finite = true;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float by = basis.forward[axis] * nudge[0] - basis.left[axis] * nudge[1] + basis.up[axis] * nudge[2];
                moved[axis] = frame.cameraBase[axis] + by;
                hands[axis] = frame.handsBase[axis] + by;
                finite = finite && std::isfinite(moved[axis]) && std::isfinite(hands[axis]);
            }
            if (finite)
            {
                memcpy(frame.cameraBase, moved, sizeof(moved));
                memcpy(frame.handsBase, hands, sizeof(hands));
                g_reachVehicleCameraNudgeFrames.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return;
    }

    // A CHASE CAMERA: the eye, and the hands' origin with it, stand at its place.
    float point[3];
    if (ReachVehicleCameraPlace(frame, key, stockCompact, point))
    {
        memcpy(frame.cameraBase, point, sizeof(point));
        memcpy(frame.handsBase, point, sizeof(point));
    }
}

// The ring's frames in Reach, and those with a fresh trace of its ray, for the line on leaving the seat.
std::atomic<uint32_t> g_reachVehicleAimMarkerFrames{0};
std::atomic<uint32_t> g_reachVehicleAimMarkerTracedFrames{0};

// THE VEHICLE AIM RING ("aimMarker", vehicle_aim_marker_logic.h), as Halo 3 places it: render thread, once the
// head-centre camera is built from this frame's chase camera, `eye` the point the view is drawn from. The line the
// vehicle aims along is its heading at the game's own aim pitch, from the seat's own eye (where a point the hand can
// aim at lies); its depth is Reach's sight trace of that ray (reach_sight_trace.inl); its direction in the headset's
// space as the head-centre camera mapped the world this frame. The frame thread draws it (Game_GetVehicleAimMarker).
// Shares Halo 3's ring record: one title runs at a time.
void ReachVehicleAimMarkerStep(const float eye[3])
{
    const ReachVehicleCameraPassRecord& pass = g_reachVehicleCameraPass;
    float dir[3];
    if (!g_config.vehicle_aim_marker || !pass.placed || !pass.camera.aimMarker || !eye || !pass.looksValid ||
        !vehicle_aim_marker::Direction(pass.vehicleForward, pass.looks, dir))
    {
        Halo3VehicleAimMarkerClear();
        return;
    }
    if (!(pass.seat == g_halo3VehicleAimMarkerSeat))
    {
        g_halo3VehicleAimMarkerSeat = pass.seat;
        g_halo3VehicleAimMarkerRange.Reset();
    }
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::HaloReach);
    SightAuxRay ray{};
    ray.generation = generation;
    for (int axis = 0; axis < 3; ++axis)
    {
        ray.origin[axis] = pass.seatEye[axis];
        ray.dir[axis] = dir[axis];
    }
    Sight_PublishMarkerRay(GameTitle::HaloReach, ray);
    bool hit = false;
    float rangeWu = 0.0f;
    const bool traced = Sight_ReadMarkerTrace(GameTitle::HaloReach, generation, kSightFreshMs, hit, rangeWu);
    const float range = g_halo3VehicleAimMarkerRange.Use(traced, hit, rangeWu, kSightTraceLengthWu);
    const float unitsPerVrMetre = g_worldScale.load(std::memory_order_relaxed);
    const float along = vehicle_aim_marker::PullBack(range, g_config.reticle_surface_offset_m * unitsPerVrMetre);
    float hullYaw = 0.0f, hullPitch = 0.0f;
    const bool followed = Halo3ReadRollStableFollow(hullYaw, hullPitch);
    float point[3], fromEye[3], lengthWu = 0.0f, vrYaw = 0.0f, vrPitch = 0.0f;
    if (!(unitsPerVrMetre > 0.0f) || !shot_point::PointAlongRay(pass.seatEye, dir, along, point) ||
        !shot_point::FromHead(eye, point, fromEye, lengthWu) ||
        !Halo3SeatAimToVrReticle(fromEye, followed, hullYaw, hullPitch, g_gameYawRef, g_headYawRef, g_yawSign.load(),
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
    g_reachVehicleAimMarkerFrames.fetch_add(1, std::memory_order_relaxed);
    if (!traced)
        g_halo3VehicleAimMarkerUntracedFrames.fetch_add(1, std::memory_order_relaxed);
    else
        g_reachVehicleAimMarkerTracedFrames.fetch_add(1, std::memory_order_relaxed);
}

// ---- the cold worker's side ---------------------------------------------------------------------------------------

// The seat the player is in, for the cold worker. OnFoot: not in a seat the Reach seat view runs in. Unknown: the
// render thread's record is a moment stale or torn (the published camera is kept).
VehicleCameraSeatRead ReachVehicleCameraSeat(VehicleCameraSeatKey& key, const char*& name, uint8_t& seatBits,
                                            bool& seatView, bool& wheelSeat, uint32_t& definitionIndex)
{
    if (!ReachVehicleFpActive())
        return VehicleCameraSeatRead::OnFoot;
    ReachVehicleCameraSeatRecord seat;
    if (!ReachVehicleCameraReadSeat(seat) || !seat.seated ||
        seat.generation != g_reachCamera.generation.load(std::memory_order_acquire))
        return VehicleCameraSeatRead::Unknown;
    const auto identity = static_cast<ReachVehicleId>(seat.identity);
    key = VehicleCameraSeatKey{seat.generation, seat.vehicle, seat.seatIndex, seat.identity, seat.onCarrier};
    static char buffer[reach_vehicle_camera::kNameCapacity];
    name = reach_vehicle_camera::SeatVehicleName(identity, seat.onCarrier,
                                                 static_cast<ReachVehicleId>(seat.carrierIdentity), seat.fingerprint,
                                                 seat.physicsType, buffer);
    seatBits = reach_vehicle_camera::SeatBits(identity, seat.seatIndex, seat.onCarrier, seat.seatFlags);
    seatView = true; // every Reach seat the seat view runs in has an eye, a vehicle the mod does not know included
    wheelSeat = reach_vehicle_camera::WheelSeat(identity, seat.seatIndex);
    definitionIndex = seat.definitionDatum;

    // Once per seat: what the seat is, and the flags its role was read from where the mod does not know it.
    static VehicleCameraSeatKey logged{};
    if (!(logged == key))
    {
        logged = key;
        LOG("Vehicle cameras (Reach): %s, seat %d (%s): identity %u%s, seat flags %08X, definition %08X, bounds "
            "radius %.3f wu%s",
            name, seat.seatIndex, vehicle_camera_file::SeatText(seatBits).c_str(), static_cast<unsigned>(seat.identity),
            identity == ReachVehicleId::Unknown ? " (not one the mod knows: its role is read from those flags)" : "",
            seat.seatFlags, seat.definitionDatum, static_cast<double>(seat.boundsRadius),
            seat.onCarrier ? (seat.carrierIdentity ? ", riding a carrier the mod knows" : ", riding a carrier") : "");
    }
    return VehicleCameraSeatRead::Seated;
}

// What a selected camera asks for that Reach does not do yet, for the log and the readout; empty when nothing.
std::string ReachVehicleCameraNotApplied(const vehicle_camera_select::ActiveCamera& camera)
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
            add("rotationTracking pitch");
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

// Cold worker, every poll while the vehicle cameras are on in Reach: the counts the render thread keeps, as log
// lines when there is something to say. `seated` false = the player has left the seat.
void ReachVehicleCamerasColdLog(bool seated)
{
    static bool standInLogged = false, noTraceLogged = false;
    static float restLoggedWu = 0.0f, restSeenWu = 0.0f;
    static uint32_t trailStint = 0, headMissStint = 0, heldStint = 0, tracedStint = 0;
    const uint32_t collideFrames = g_reachVehicleCameraCollideFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t noTraceFrames = g_reachVehicleCameraNoTraceFrames.exchange(0, std::memory_order_relaxed);
    heldStint += g_reachVehicleCameraHeldFrames.exchange(0, std::memory_order_relaxed);
    tracedStint += collideFrames;
    static uint32_t ringStint = 0, ringTracedStint = 0;
    ringStint += g_reachVehicleAimMarkerFrames.exchange(0, std::memory_order_relaxed);
    ringTracedStint += g_reachVehicleAimMarkerTracedFrames.exchange(0, std::memory_order_relaxed);
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
        LOG("Vehicle cameras (Reach): this camera asks to be kept out of walls (\"collide\"), but Reach's sight trace "
            "is not running for this seat, so it goes all the way out");
    }
    const uint32_t frames = g_reachVehicleCameraFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t standIn = g_reachVehicleCameraStandInFrames.exchange(0, std::memory_order_relaxed);
    g_reachVehicleCameraNudgeFrames.exchange(0, std::memory_order_relaxed);
    headMissStint += g_reachVehicleCameraHeadMisses.exchange(0, std::memory_order_relaxed);
    const uint32_t trail = g_reachVehicleCameraLagTrailCm.exchange(0, std::memory_order_relaxed);
    trailStint = trail > trailStint ? trail : trailStint;
    if (!seated)
    {
        if (trailStint)
            LOG("Vehicle cameras (Reach): in that seat the lagged camera trailed its place by at most %u cm", trailStint);
        if (headMissStint)
            LOG("Vehicle cameras (Reach): in that seat the occupant's head could not be read on %u frames, and the "
                "seat's eye stood in for it",
                headMissStint);
        if (tracedStint)
            LOG("Vehicle cameras (Reach): in that seat the camera's line was traced for walls on %u frames and held "
                "short of something on %u of them",
                tracedStint, heldStint);
        if (ringStint)
            LOG("Vehicle cameras (Reach): in that seat the ring where the vehicle aims was placed on %u frames, %u of "
                "them at a freshly traced depth",
                ringStint, ringTracedStint);
        ringStint = ringTracedStint = 0;
        if (boomSeen)
            LOG("Vehicle cameras (Reach): in that seat the third-person boom went from %.1f to %.1f deg above level and "
                "was left at %.1f deg (kept for that seat and camera)",
                static_cast<double>(boomLowDeg), static_cast<double>(boomHighDeg), static_cast<double>(boomLastDeg));
        boomSeen = false;
        // The orbit's check: a camera that orbits the pivot stands about as far from it at every look.
        const ReachVehicleCameraOrbitCheck& check = g_reachVehicleCameraOrbit;
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
                LOG("Vehicle cameras (Reach): in that seat the game's own camera stood %.0f to %.0f cm from its pivot "
                    "over %u frames, looking from %.1f to %.1f deg (an orbit of the pivot keeps the two close)",
                    static_cast<double>(nearCm), static_cast<double>(farCm), samples, static_cast<double>(lowDeg),
                    static_cast<double>(highDeg));
            }
            break;
        }
        trailStint = headMissStint = heldStint = tracedStint = 0;
        standInLogged = noTraceLogged = false;
        restLoggedWu = restSeenWu = 0.0f;
        g_reachVehicleCameraRestWu.store(0.0f, std::memory_order_relaxed);
        return;
    }
    if (standIn && !standInLogged)
    {
        standInLogged = true;
        LOG("Vehicle cameras (Reach): where the game's own third-person camera rests could not be read for this seat, "
            "so the camera stands %.0f cm behind and %.0f cm above the vehicle's origin instead",
            static_cast<double>(vehicle_camera_place::kStandInBehindCm),
            static_cast<double>(vehicle_camera_place::kStandInAboveCm));
    }
    const float restWu = g_reachVehicleCameraRestWu.load(std::memory_order_relaxed);
    const bool settled = restWu == restSeenWu;
    restSeenWu = restWu;
    if (frames && restWu > 0.0f && settled && restWu > restLoggedWu * 1.1f)
    {
        restLoggedWu = restWu;
        LOG("Vehicle cameras (Reach): the game's own third-person camera rests %.2f world units (%.0f cm) from its "
            "pivot in this seat, %.0f cm of that above the pivot (it looked %.1f deg up), and the pivot is %.0f cm "
            "above the vehicle's origin",
            static_cast<double>(restWu), static_cast<double>(restWu / vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_reachVehicleCameraRestRiseWu.load(std::memory_order_relaxed) /
                                vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_reachVehicleCameraRestPitchDeg.load(std::memory_order_relaxed)),
            static_cast<double>(g_reachVehicleCameraPivotUpWu.load(std::memory_order_relaxed) /
                                vehicle_camera_frame::kWorldUnitsPerCm));
    }
}
