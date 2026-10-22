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
// keeps its own third-person camera word (ReachEnsureNativeFirstPersonSeat gives the first-person lease back). The
// camera's mode turns the view: its rotationTracking yaw stands in for vehicle_view_follow in that seat
// (ReachApplySeatYawFollow). Nothing the simulation reads is written.
//
// "thirdperson": the game's own camera for the seat is the stock camera of the frame (the render view's compact
// camera before the mod moves it); its pivot is the point unit_get_camera_info answers for the seat. Where the game's
// camera stands from that pivot, looking level, gives the distance; the camera then orbits the pivot at the boom's
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

vehicle_camera_place::State g_reachVehicleCameraState;  // render thread
vehicle_camera_place::LagState g_reachVehicleCameraLag; // render thread
LARGE_INTEGER g_reachVehicleCameraLagAt{};              // render thread

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
        const bool level = std::fabs(lookUp) <= kReachRestLevelSin;
        const bool take = std::isfinite(distance) && distance >= kReachRestKnownWu && distance <= furthestWu &&
                          (level ? (!rest.level || distance > rest.distance) : (!rest.level && distance > rest.distance));
        if (take)
        {
            rest.distance = distance;
            rest.rise = vehicle_camera_frame::Dot(stood, in.vehicle.up);
            rest.level = level;
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
    if (rest.placeKept && rest.distance >= kReachRestKnownWu)
    {
        // THE BOOM: an orbit of the pivot over the flat game's look range, at the file's "boomPitch", else where the
        // game's camera stood looking level, a little higher.
        static vehicle_boom::Shape orbit{};
        if (!orbit.valid)
            vehicle_boom::BuildOrbitShape(vehicle_boom::Range{}, orbit);
        float elevation = 0.0f;
        if (vehicle_boom::BoomPitchSet(camera.boomPitchDeg))
            elevation = camera.boomPitchDeg * vehicle_boom::kRadiansPerDegree;
        else if (rest.level && rest.distance > 1.0e-3f)
            elevation = asinf(std::fabs(rest.rise) <= rest.distance ? rest.rise / rest.distance : 0.0f) +
                        vehicle_boom::kDefaultRaiseDeg * vehicle_boom::kRadiansPerDegree;
        else
            elevation = vehicle_boom::DefaultElevation(orbit, vehicle_boom::kBoomPitchUnset);
        const float pitch = vehicle_boom::PitchForElevation(orbit, elevation);
        vehicle_boom::Offset(orbit, pitch, rest.distance, in.restOffset);
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
            memcpy(in.head, head.matrix.position, sizeof(in.head));
        else
            g_reachVehicleCameraHeadMisses.fetch_add(1, std::memory_order_relaxed);
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
    g_reachVehicleCameraFrames.fetch_add(1, std::memory_order_relaxed);
    if (g_reachVehicleCameraState.standIn)
        g_reachVehicleCameraStandInFrames.fetch_add(1, std::memory_order_relaxed);
    memcpy(out, point, sizeof(point));
    return true;
}

// Render thread, every main render view: after the seat is sampled and its yaw followed, before the head-centre
// camera is built from it. `stockCompact` is the frame's camera as the game computed it.
void ReachVehicleCamerasPass(ReachVehicleFrame& frame, const unsigned char* stockCompact)
{
    ReachVehicleCameraPublishSeat(frame);
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
        if (camera.collide)
            add("collide (the camera is not held short of walls)");
        if (vehicle_camera_file::LeashSet(camera.leashMinCm) || vehicle_camera_file::LeashSet(camera.leashMaxCm))
            add("the leash");
        if (camera.aimMarker)
            add("the aim ring");
    }
    else if (camera.showHeadlessBody)
    {
        add("showHeadlessBody");
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

// Cold worker, every poll while the vehicle cameras are on in Reach: the counts the render thread keeps, as log
// lines when there is something to say. `seated` false = the player has left the seat.
void ReachVehicleCamerasColdLog(bool seated)
{
    static bool standInLogged = false;
    static float restLoggedWu = 0.0f, restSeenWu = 0.0f;
    static uint32_t trailStint = 0, headMissStint = 0;
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
        trailStint = headMissStint = 0;
        standInLogged = false;
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
