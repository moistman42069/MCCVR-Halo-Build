// VEHICLE CAMERAS IN HALO CE: CE's side of vehicle_cameras.inl. Included by game.cpp right after it.
//
// THREADS.
//   The cold worker (VehicleCameras_ColdTick) reads the seat the player sits in (HaloCEVehicleCameraSeat, from
//     haloce_controls.cpp's seat facts): the vehicle's name by its tag path, the seat's role, and the key every reader
//     matches a camera to.
//   The game's own camera code, on the game's thread, asks two things of the published camera: the first-person
//     override (native_vehicle_first_person.cpp) whether a chase camera is chosen for its seat and how far a
//     firstperson camera's "offset" moves the eye; the following camera's hook (haloce_vehicle_cameras.cpp) where a
//     chase camera stands (HaloCEVehicleCameraPlace). One atomic load each while no camera of the kind is chosen.
//
// WHAT A CHASE CAMERA CHANGES. The seat is left on the third-person camera the game gives it, so the game draws the
// player's character in the seat and no first-person weapon, as the flat game does; the observer is then told to
// stand that camera at the chosen place. Everything the view is built from in either graphics mode (the observer's
// camera) moves with it; the simulation's own camera (director_camera_deterministic, the line the game aims and fires
// along) is the game's, as it is for a flat player in that seat. The camera's mode turns the view: its rotationTracking
// yaw stands in for vehicle_view_follow in that seat (haloce_controls.cpp's turn). "collide" is traced with the sight
// trace's own query on the game's thread (HaloCEContact_TraceSegment). The leash is published with each placement and
// held by the render paths in the frame's tracking reference (haloce_vehicle_leash_logic.h).
//
// "thirdperson": the game's own camera is the following camera's command, before the hook moves it: the pivot it hangs
// from (the seat camera's marker, as drawn) and where it stands from that pivot (haloce_vehicle_camera_logic.h
// GameCameraFromPivot). The boom stands the camera where the game's would stand at a look pitch of its own, from the
// seat camera's track (haloce_vehicle_camera::TrackShape), as Halo 3's does; a seat whose track cannot be read has the
// boom orbit the pivot at the game's own distance instead, as Reach's does.
//
// Not in CE: the aim ring, the glass and body keys, eyeAdjust (classic CE has no eye adaptation to hold), pitch follow
// and a marker origin. The readout says so for a camera that asks.

// ---- the camera chosen for a seat ----------------------------------------------------------------------------------

// A chase camera is chosen for exactly this CE seat. Any thread; one atomic load while none is.
bool HaloCEVehicleCameraChosen(uint32_t generation, uint32_t parent, int16_t seat,
                               vehicle_camera_select::ActiveCamera& camera, VehicleCameraSeatKey& key)
{
    if (!VehicleCameraRead(camera, key))
        return false;
    return TitleAdapter_GetActiveTitle() == GameTitle::HaloCE && key.generation == generation &&
           key.vehicle == static_cast<int32_t>(parent) && key.seatIndex == seat;
}

// The vehicle's root node and size, for a firstperson offset: read again on the game's thread, as drawn.
// A FIRSTPERSON CAMERA'S "offset": the seat's eye moved along the vehicle's own frame, kept within the vehicle's
// bounds and a margin, as Halo 3 keeps its own (Halo3AnchorWithinBounds, 2 world units). False (`point` unchanged)
// while the camera in use for exactly this seat is not a firstperson camera with an offset.
std::atomic<uint32_t> g_haloceVehicleCameraNudgeFrames{0};
bool HaloCEVehicleCameraEyeNudge(uint32_t generation, uint32_t parent, int16_t seat, float point[3])
{
    const VehicleCameraPublished& published = g_vehicleCameraPublished;
    if (!published.eyeNudge.load(std::memory_order_acquire) || TitleAdapter_GetActiveTitle() != GameTitle::HaloCE)
        return false;
    vehicle_camera_select::ActiveCamera camera{};
    VehicleCameraSeatKey chosenFor{};
    bool read = false;
    for (int attempt = 0; attempt < 2 && !read; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        camera = published.camera;
        chosenFor = published.seat;
        std::atomic_thread_fence(std::memory_order_acquire);
        read = published.seq.load(std::memory_order_relaxed) == before;
    }
    if (!read || !camera.valid || camera.chase || chosenFor.generation != generation ||
        chosenFor.vehicle != static_cast<int32_t>(parent) || chosenFor.seatIndex != seat)
        return false;
    HaloCEVehicleSeatNow now{};
    float origin[3], forward[3], up[3];
    vehicle_camera_frame::Basis basis{};
    if (!HaloCEControls_ReadVehicleSeatNow(now) || now.generation != generation || now.parent != parent ||
        now.seat != seat || !HaloCEVehicleCameras_RootFrame(now.root, origin, forward, up) ||
        !vehicle_camera_frame::FromForwardUp(forward, up, basis))
        return false;
    constexpr float kNudgeMarginWu = 2.0f;
    const float reachWu = (now.boundsRadius > 0.0f ? now.boundsRadius : 0.0f) + kNudgeMarginWu;
    float moved[3], fromRoot[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        const float wu[3] = {camera.offsetCm[0] * vehicle_camera_frame::kWorldUnitsPerCm,
                             camera.offsetCm[1] * vehicle_camera_frame::kWorldUnitsPerCm,
                             camera.offsetCm[2] * vehicle_camera_frame::kWorldUnitsPerCm};
        moved[axis] = point[axis] + basis.forward[axis] * wu[0] - basis.left[axis] * wu[1] + basis.up[axis] * wu[2];
        fromRoot[axis] = moved[axis] - origin[axis];
    }
    if (!vehicle_camera_place::Finite3(moved) || vehicle_camera_frame::Dot(fromRoot, fromRoot) > reachWu * reachWu)
        return false;
    memcpy(point, moved, sizeof(moved));
    g_haloceVehicleCameraNudgeFrames.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ---- where a chase camera stands ----------------------------------------------------------------------------------

// Counted by the game's thread, taken by the cold worker for its log lines (HaloCEVehicleCamerasColdLog).
std::atomic<uint32_t> g_haloceVehicleCameraFrames{0};        // chase placements
std::atomic<uint32_t> g_haloceVehicleCameraStandInFrames{0}; // of them, "thirdperson" at the mod's stand-in
std::atomic<uint32_t> g_haloceVehicleCameraHeadMisses{0};    // "playerhead" with no head marker read
std::atomic<uint32_t> g_haloceVehicleCameraSeatFrameRefused{0}; // the occupant's root not taken for the seat's frame
// Where the seat's point, the head and the game's pivot sit on the vehicle (world units along its forward, left, up),
// as the last placement read them: for the log line a seat gets once (the presets' offsets are laid from these).
struct HaloCEVehicleCameraLayout
{
    std::atomic<uint32_t> seq{0};
    VehicleCameraSeatKey seat{};
    bool seatValid = false, headValid = false, pivotValid = false, seatFrame = false;
    float seat3[3]{}, head3[3]{}, pivot3[3]{};
};
HaloCEVehicleCameraLayout g_haloceVehicleCameraLayout;
std::atomic<uint32_t> g_haloceVehicleCameraLagTrailCm{0};
std::atomic<uint32_t> g_haloceVehicleCameraCollideFrames{0};
std::atomic<uint32_t> g_haloceVehicleCameraHeldFrames{0};
std::atomic<uint32_t> g_haloceVehicleCameraNoTraceFrames{0};
std::atomic<float> g_haloceVehicleCameraRestWu{0.0f};
std::atomic<float> g_haloceVehicleCameraRestRiseWu{0.0f};
std::atomic<float> g_haloceVehicleCameraRestPitchDeg{0.0f};
std::atomic<float> g_haloceVehicleCameraPivotUpWu{0.0f};
std::atomic<float> g_haloceVehicleCameraRadiusCm{0.0f};

// Where the game's own camera stands looking level, as an elevation above level from its pivot in whole degrees, for
// the boom's start (VehicleBoomColdTick), with the seat it was measured in. The game's thread writes (the only
// writer), the cold worker reads through the sequence.
struct HaloCEVehicleCameraLevel
{
    std::atomic<uint32_t> seq{0}; // even = stable
    VehicleCameraSeatKey seat{};
    float deg = vehicle_boom::kBoomPitchUnset;
};
HaloCEVehicleCameraLevel g_haloceVehicleCameraLevel;

void HaloCEVehicleCameraLevelStore(const VehicleCameraSeatKey& seat, float deg)
{
    HaloCEVehicleCameraLevel& level = g_haloceVehicleCameraLevel;
    level.seq.fetch_add(1, std::memory_order_acq_rel);
    level.seat = seat;
    level.deg = deg;
    level.seq.fetch_add(1, std::memory_order_release);
}

// Cold worker: the level view measured in `seat` (CE's key compares generation, vehicle and seat); unset when the
// measurement in hand is another seat's. False: the record was being written both times it was read.
bool HaloCEVehicleCameraLevelElevationDeg(const VehicleCameraSeatKey& seat, float& deg)
{
    const HaloCEVehicleCameraLevel& level = g_haloceVehicleCameraLevel;
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

// THE LEASH of the camera last placed (Game_HaloCEVehicleCameraLeash): the game's thread writes it with every
// placement, the render paths read it once a frame through the sequence.
struct HaloCEVehicleCameraLeashRecord
{
    std::atomic<uint32_t> seq{0}; // even = stable
    std::atomic<uint64_t> ms{0};
    bool set = false;
    uint32_t generation = 0;
    vehicle_camera_frame::Basis carrier{};
    vehicle_camera_frame::LeashBox box{};
};
HaloCEVehicleCameraLeashRecord g_haloceVehicleCameraLeash;
inline constexpr uint64_t kHaloCEVehicleCameraLeashFreshMs = 200;

void HaloCEVehicleCameraLeashPublish(bool set, uint32_t generation, const vehicle_camera_frame::Basis& carrier,
                                     const vehicle_camera_frame::LeashBox& box)
{
    HaloCEVehicleCameraLeashRecord& record = g_haloceVehicleCameraLeash;
    if (!set && !record.set)
        return; // nothing to take back: one load while no camera sets a leash
    record.seq.fetch_add(1, std::memory_order_acq_rel);
    record.set = set;
    record.generation = generation;
    record.carrier = carrier;
    record.box = box;
    record.ms.store(GetTickCount64(), std::memory_order_relaxed);
    record.seq.fetch_add(1, std::memory_order_release);
}

bool HaloCEVehicleCameraLeashRead(uint32_t generation, vehicle_camera_frame::Basis& carrier,
                                  vehicle_camera_frame::LeashBox& box)
{
    const HaloCEVehicleCameraLeashRecord& record = g_haloceVehicleCameraLeash;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = record.seq.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        const bool set = record.set;
        const uint32_t at = record.generation;
        const uint64_t ms = record.ms.load(std::memory_order_relaxed);
        const vehicle_camera_frame::Basis c = record.carrier;
        const vehicle_camera_frame::LeashBox b = record.box;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (record.seq.load(std::memory_order_relaxed) != before)
            continue;
        const uint64_t now = GetTickCount64();
        if (!set || at != generation || !ms || now < ms || now - ms > kHaloCEVehicleCameraLeashFreshMs)
            return false;
        carrier = c;
        box = b;
        return true;
    }
    return false;
}

vehicle_camera_place::State g_haloceVehicleCameraState;  // game's thread
vehicle_camera_place::LagState g_haloceVehicleCameraLag; // game's thread
LARGE_INTEGER g_haloceVehicleCameraLagAt{};              // game's thread
float g_haloceVehicleCameraReach = 1.0f;                 // game's thread
LARGE_INTEGER g_haloceVehicleCameraReachAt{};            // game's thread
inline constexpr float kHaloCECameraReleasePerSecond = 1.5f;

float HaloCEVehicleCameraSecondsSince(LARGE_INTEGER& at)
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

// THE GAME'S OWN THIRD-PERSON CAMERA in the seat, as last measured (game's thread): its distance from its pivot and its
// height above it, taken while it looked within about 15 degrees of level where it has (else at any look), as the
// furthest it stood. The pivot is kept as a point on the vehicle, its movement there smoothed, and rebuilt every frame
// from the vehicle as drawn.
struct HaloCEVehicleCameraRest
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
HaloCEVehicleCameraRest g_haloceVehicleCameraRest;
inline constexpr float kHaloCERestKnownWu = 0.25f; // nearer than this the game's camera is still in the seat's own view
inline constexpr float kHaloCERestFurthestWu = 8.0f;
inline constexpr float kHaloCERestPivotReachWu = 10.0f;
inline constexpr uint32_t kHaloCERestWaitFrames = 45;
inline constexpr float kHaloCERestLevelSin = 0.26f;
inline constexpr float kHaloCERestPivotSmoothSeconds = 0.25f;

// Fills `in` with the game's camera's place for a "thirdperson" camera. False: not known YET (the frame keeps the
// game's own camera). True with in.restValid false: it could not be read, and the mod's stand-in applies.
bool HaloCEVehicleCameraRestPlace(const HaloCEChaseFrame& frame, const VehicleCameraSeatKey& seat,
                                  const vehicle_camera_select::ActiveCamera& camera, vehicle_camera_place::Inputs& in)
{
    HaloCEVehicleCameraRest& rest = g_haloceVehicleCameraRest;
    if (!(rest.seat == seat))
    {
        rest = {};
        rest.seat = seat;
        HaloCEVehicleCameraLevelStore(seat, vehicle_boom::kBoomPitchUnset);
    }
    const float radiusWu = std::isfinite(in.vehicleRadiusCm) && in.vehicleRadiusCm > 0.0f
        ? in.vehicleRadiusCm * vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
    const float pivotReachWu = 2.0f * radiusWu > kHaloCERestPivotReachWu ? 2.0f * radiusWu : kHaloCERestPivotReachWu;
    const float furthestWu = 4.0f * radiusWu > kHaloCERestFurthestWu ? 4.0f * radiusWu : kHaloCERestFurthestWu;
    float looks[3] = {frame.looks[0], frame.looks[1], frame.looks[2]};
    const float fromVehicle[3] = {frame.pivot[0] - in.vehicleOrigin[0], frame.pivot[1] - in.vehicleOrigin[1],
                                  frame.pivot[2] - in.vehicleOrigin[2]};
    const bool observed = frame.gameValid && vehicle_camera_frame::Normalize(looks) &&
                          vehicle_camera_frame::Dot(fromVehicle, fromVehicle) <= pivotReachWu * pivotReachWu;
    if (observed)
    {
        const float* stood = frame.gameFromPivot;
        const float distance = sqrtf(vehicle_camera_frame::Dot(stood, stood));
        const float lookUp = vehicle_camera_frame::Dot(looks, in.vehicle.up);
        const float lookDeg = asinf(lookUp < -1.0f ? -1.0f : (lookUp > 1.0f ? 1.0f : lookUp)) * 57.29578f;
        const bool level = std::fabs(lookUp) <= kHaloCERestLevelSin;
        const bool take = std::isfinite(distance) && distance >= kHaloCERestKnownWu && distance <= furthestWu &&
                          (level ? (!rest.level || distance > rest.distance) : (!rest.level && distance > rest.distance));
        if (take)
        {
            rest.distance = distance;
            rest.rise = vehicle_camera_frame::Dot(stood, in.vehicle.up);
            rest.level = level;
            if (level)
            {
                const float levelDeg = haloce_vehicle_camera::LevelElevationDeg(rest.rise, distance, lookDeg);
                // A degree either way is the measurement's own wobble; each change restarts a boom the player has not
                // moved, so only a move of two counts (as in Reach).
                const float published = g_haloceVehicleCameraLevel.deg;
                if (vehicle_boom::BoomPitchSet(levelDeg) &&
                    (!vehicle_boom::BoomPitchSet(published) || std::fabs(levelDeg - published) >= 2.0f))
                    HaloCEVehicleCameraLevelStore(seat, levelDeg);
            }
            g_haloceVehicleCameraRestRiseWu.store(rest.rise, std::memory_order_relaxed);
            g_haloceVehicleCameraRestPitchDeg.store(lookDeg, std::memory_order_relaxed);
        }
        // The pivot as a point on the vehicle.
        const float read[3] = {vehicle_camera_frame::Dot(fromVehicle, in.vehicle.forward),
                               vehicle_camera_frame::Dot(fromVehicle, in.vehicle.left),
                               vehicle_camera_frame::Dot(fromVehicle, in.vehicle.up)};
        const float seconds = HaloCEVehicleCameraSecondsSince(rest.pivotAt);
        if (rest.placeKept && seconds >= 0.0f && seconds <= 0.5f)
        {
            const float blend = 1.0f - std::exp(-seconds / kHaloCERestPivotSmoothSeconds);
            for (int axis = 0; axis < 3; ++axis)
                rest.pivotLocal[axis] += (read[axis] - rest.pivotLocal[axis]) * blend;
        }
        else
        {
            memcpy(rest.pivotLocal, read, sizeof(rest.pivotLocal));
        }
        rest.placeKept = true;
        g_haloceVehicleCameraPivotUpWu.store(read[2], std::memory_order_relaxed);
    }
    // THE BOOM (vehicle_cameras.inl's shared boom, run here on the game's thread as Halo 3's camera thread runs it): the
    // cold worker's publication for this seat and camera -- the seat camera's track, else an orbit of the pivot over
    // CE's range (haloce_vehicle_camera::BoomRange), starting at the player's kept angle, else the file's "boomPitch",
    // else the game's level view a little higher -- or, until one comes, the stand-in orbit over that range. Each frame
    // the game's own camera is handed back with it, for the cold worker's check of the track.
    Halo3BoomTake(seat, camera.camera);
    Halo3BoomCamera& boom = g_halo3BoomCamera;
    if (!boom.held && !boom.local && rest.waited >= kHaloCERestWaitFrames)
        Halo3BoomStandIn(haloce_vehicle_camera::BoomRange());
    // An orbit stands as far out as the game's camera does, so it waits for that; a track has its own.
    if (rest.placeKept && (boom.shape.track || rest.distance >= kHaloCERestKnownWu) && (boom.held || boom.local))
    {
        Halo3BoomStep();
        static const float kNone[3] = {0.0f, 0.0f, 0.0f};
        Halo3BoomPublishCurrent(observed && boom.shape.track, observed ? frame.gameFromPivot : kNone,
                                observed ? looks : kNone);
        vehicle_boom::Offset(boom.shape, boom.pitch, rest.distance, in.restOffset);
        g_vehicleBoomLiveMs.store(GetTickCount64(), std::memory_order_release);
        in.restValid = true;
        for (int axis = 0; axis < 3; ++axis)
            in.rest[axis] = in.vehicleOrigin[axis] + in.vehicle.forward[axis] * rest.pivotLocal[0] +
                            in.vehicle.left[axis] * rest.pivotLocal[1] + in.vehicle.up[axis] * rest.pivotLocal[2];
        g_haloceVehicleCameraRestWu.store(rest.distance, std::memory_order_relaxed);
        return true;
    }
    if (rest.waited < kHaloCERestWaitFrames)
    {
        ++rest.waited;
        return false;
    }
    in.restValid = false;
    return true;
}

// The game's thread, from the following camera's hook: where the chosen chase camera stands this frame. False: none is
// chosen for this seat, or its place is not known yet; the game's own camera stays.
bool HaloCEVehicleCameraPlace(const HaloCEChaseFrame& frame, float out[3])
{
    vehicle_camera_select::ActiveCamera camera;
    VehicleCameraSeatKey chosenFor;
    if (!HaloCEVehicleCameraChosen(frame.generation, frame.parent, frame.seat, camera, chosenFor))
        return false;
    const VehicleCameraSeatKey& key = chosenFor;

    vehicle_camera_place::Inputs in;
    if (!vehicle_camera_frame::FromForwardUp(frame.vehicleForward, frame.vehicleUp, in.vehicle) ||
        !vehicle_camera_frame::Usable(in.vehicle))
        return false;
    memcpy(in.vehicleOrigin, frame.vehiclePos, sizeof(in.vehicleOrigin));
    in.vehicleRadiusCm = frame.boundsRadius > 0.0f ? frame.boundsRadius / vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
    g_haloceVehicleCameraRadiusCm.store(in.vehicleRadiusCm, std::memory_order_relaxed);
    // The seat: the occupant's root node, and the frame of what it rides (the hull, or a gun that turns). A root node
    // whose up does not stand within about 30 degrees of the vehicle's is not taken for the seat's frame (a model may
    // lay its root's axes another way); the vehicle's frame serves.
    in.seatValid = frame.seatValid;
    if (in.seatValid)
    {
        memcpy(in.seat, frame.seatPos, sizeof(in.seat));
        in.seatFrameValid = vehicle_camera_frame::FromForwardUp(frame.seatForward, frame.seatUp, in.seatFrame) &&
                            vehicle_camera_frame::Usable(in.seatFrame) &&
                            vehicle_camera_frame::Dot(in.seatFrame.up, in.vehicle.up) > 0.85f;
        if (!in.seatFrameValid)
            g_haloceVehicleCameraSeatFrameRefused.fetch_add(1, std::memory_order_relaxed);
    }
    // The occupant's head, read only for a camera measured from it.
    in.headValid = false;
    if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Head))
    {
        if (frame.headValid)
        {
            in.headValid = true;
            memcpy(in.head, frame.headPos, sizeof(in.head));
            // THE HEAD AS A FRAME ("playerhead", as Halo 3 lays it out): the seat's frame turned as far as the head has
            // turned in it since it settled, so the marker's own axis convention never matters.
            static constexpr uint32_t kHeadSettleFrames = 45;
            struct HeadReference
            {
                VehicleCameraSeatKey seat{};
                uint32_t frames = 0;
                bool restValid = false;
                vehicle_camera_frame::Basis rest{};
            };
            static HeadReference headRef; // game's thread
            if (!(headRef.seat == key))
                headRef = HeadReference{key};
            vehicle_camera_frame::Basis headBasis{};
            if (in.seatFrameValid && vehicle_camera_frame::FromForwardUp(frame.headForward, frame.headUp, headBasis) &&
                vehicle_camera_frame::Usable(headBasis))
            {
                const vehicle_camera_frame::Basis inSeat = vehicle_camera_frame::InFrame(in.seatFrame, headBasis);
                if (!headRef.restValid && ++headRef.frames >= kHeadSettleFrames)
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
            g_haloceVehicleCameraHeadMisses.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // The layout on the vehicle, for the seat's log line.
    {
        const auto local = [&in](const float point[3], float out3[3]) {
            const float d[3] = {point[0] - in.vehicleOrigin[0], point[1] - in.vehicleOrigin[1],
                                point[2] - in.vehicleOrigin[2]};
            out3[0] = vehicle_camera_frame::Dot(d, in.vehicle.forward);
            out3[1] = vehicle_camera_frame::Dot(d, in.vehicle.left);
            out3[2] = vehicle_camera_frame::Dot(d, in.vehicle.up);
        };
        HaloCEVehicleCameraLayout& layout = g_haloceVehicleCameraLayout;
        layout.seq.fetch_add(1, std::memory_order_acq_rel);
        layout.seat = key;
        layout.seatValid = frame.seatValid;
        layout.seatFrame = in.seatFrameValid;
        layout.headValid = frame.headValid;
        layout.pivotValid = frame.gameValid;
        local(frame.seatPos, layout.seat3);
        local(frame.headPos, layout.head3);
        local(frame.pivot, layout.pivot3);
        layout.seq.fetch_add(1, std::memory_order_release);
    }
    // The view's heading (an orbiting camera rides it): the seat's control yaw, which the turn stick moves and the hand
    // never does; the vehicle's where it cannot be read.
    halo_ce::Vec3 heading{};
    in.viewYaw = HaloCEControls_SeatHeading(frame.generation, heading) ? std::atan2(heading.y, heading.x)
                                                                         : vehicle_camera_frame::Heading(in.vehicle);
    if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::ThirdPerson) &&
        !HaloCEVehicleCameraRestPlace(frame, key, camera, in))
        return false;

    float point[3];
    vehicle_camera_place::Placed placed;
    if (!vehicle_camera_place::Place(camera, in, g_haloceVehicleCameraState, point, &placed))
        return false;

    // LAG: the boom from its pivot swings on a spring; the vehicle's travel never lags.
    const float seconds = HaloCEVehicleCameraSecondsSince(g_haloceVehicleCameraLagAt);
    vehicle_camera_place::FollowLagOnBoom(camera, placed.pivot, point, seconds, g_haloceVehicleCameraLag, point);
    const float trailCm = g_haloceVehicleCameraLag.trailWu / vehicle_camera_frame::kWorldUnitsPerCm;
    const uint32_t trail =
        std::isfinite(trailCm) && trailCm > 0.0f ? static_cast<uint32_t>(trailCm < 1.0e8f ? trailCm : 1.0e8f) : 0u;
    uint32_t furthest = g_haloceVehicleCameraLagTrailCm.load(std::memory_order_relaxed);
    while (trail > furthest &&
           !g_haloceVehicleCameraLagTrailCm.compare_exchange_weak(furthest, trail, std::memory_order_relaxed))
    {
    }
    if (!vehicle_camera_place::Finite3(point))
        return false;

    // HELD SHORT OF A WALL ("collide"): the line from the pivot to the camera's place, traced on this thread with the
    // sight trace's own query and the vehicle hierarchy ignored, as the game's own crosshair test ignores it.
    float reachUsed = 1.0f;
    if (camera.collide)
    {
        const float toward[3] = {point[0] - placed.pivot[0], point[1] - placed.pivot[1], point[2] - placed.pivot[2]};
        const float length = sqrtf(vehicle_camera_frame::Dot(toward, toward));
        float wanted = 1.0f;
        if (std::isfinite(length) && length > 0.05f)
        {
            bool hit = false;
            float fraction = 1.0f;
            if (HaloCEContact_TraceSegment(placed.pivot, toward, frame.root, hit, fraction))
            {
                wanted = vehicle_camera_place::CollideReach(hit, fraction * length, length, camera.collideMarginCm);
                g_haloceVehicleCameraCollideFrames.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                g_haloceVehicleCameraNoTraceFrames.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const float since = HaloCEVehicleCameraSecondsSince(g_haloceVehicleCameraReachAt);
        const float reach = vehicle_camera_place::EaseReach(g_haloceVehicleCameraReach, wanted,
                                                            since < 0.0f ? 0.0f : (since > 0.1f ? 0.1f : since),
                                                            kHaloCECameraReleasePerSecond);
        if (reach < 0.999f)
        {
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = placed.pivot[axis] + toward[axis] * reach;
            g_haloceVehicleCameraHeldFrames.fetch_add(1, std::memory_order_relaxed);
            reachUsed = reach;
        }
    }
    else
    {
        g_haloceVehicleCameraReach = 1.0f;
    }
    // THE LEASH: the box the head stays inside, around the camera's point as it stands (a camera pulled in from a wall
    // keeps its limits where the file puts them), for the render paths' reference this frame.
    {
        const bool leashed =
            vehicle_camera_file::LeashSet(camera.leashMinCm) || vehicle_camera_file::LeashSet(camera.leashMaxCm);
        const float cameraCm[3] = {placed.offsetCm[0] * reachUsed, placed.offsetCm[1] * reachUsed,
                                   placed.offsetCm[2] * reachUsed};
        HaloCEVehicleCameraLeashPublish(leashed, frame.generation, placed.carrier,
                                        vehicle_camera_frame::LeashToBasis(camera.leashMinCm, camera.leashMaxCm,
                                                                           cameraCm));
    }
    g_haloceVehicleCameraFrames.fetch_add(1, std::memory_order_relaxed);
    if (g_haloceVehicleCameraState.standIn)
        g_haloceVehicleCameraStandInFrames.fetch_add(1, std::memory_order_relaxed);
    memcpy(out, point, sizeof(point));
    return true;
}

// ---- the cold worker's side ---------------------------------------------------------------------------------------

// The seat camera's track in `seat`, into the boom's seat facts (VehicleBoomColdTick): read, the tracks the seat lists,
// whether it took the game's default, and the control points. False (facts cleared) when the seat cannot be read now.
bool HaloCEVehicleCameraReadTrack(const VehicleCameraSeatKey& seat, Halo3BoomSeatFacts& facts)
{
    facts = Halo3BoomSeatFacts{};
    HaloCEVehicleSeatTrack track{};
    if (!HaloCEControls_ReadVehicleSeatTrack(seat.generation, static_cast<uint32_t>(seat.vehicle), seat.seatIndex, track))
        return false;
    facts.read = true;
    facts.tracks = track.tracks;
    facts.defaultTrack = track.defaultTrack;
    facts.points = track.points;
    facts.track = track.track;
    return true;
}

// The seat the player is in, for the cold worker. OnFoot: not in a seat the vehicle cameras run in (the first-person
// seat view is off or not installed counts as that, as in Halo 3). Unknown: not readable now (the published camera is
// kept).
VehicleCameraSeatRead HaloCEVehicleCameraSeat(VehicleCameraSeatKey& key, const char*& name, uint8_t& seatBits,
                                              bool& seatView, bool& wheelSeat, uint32_t& definitionIndex)
{
    if (!g_config.vehicle_first_person || !NativeVehicleFirstPerson_Requested(GameTitle::HaloCE))
        return VehicleCameraSeatRead::OnFoot;
    HaloCEVehicleSeatFacts facts{};
    const HaloCEVehicleSeatRead read = HaloCEControls_ReadVehicleSeatFacts(facts);
    if (read == HaloCEVehicleSeatRead::OnFoot)
        return VehicleCameraSeatRead::OnFoot;
    if (read != HaloCEVehicleSeatRead::Seated)
        return VehicleCameraSeatRead::Unknown;
    const bool mounted = facts.root != UINT32_MAX && facts.root != facts.parent;
    key = VehicleCameraSeatKey{facts.generation, static_cast<int32_t>(facts.parent), facts.seat, facts.definition,
                               mounted};
    static char buffer[haloce_vehicle_camera::kNameCapacity]; // cold worker only
    if (!haloce_vehicle_camera::NameFromTagPath(facts.tagPath, buffer))
        haloce_vehicle_camera::CustomName(facts.modelIdentity, buffer);
    name = buffer;
    seatBits = haloce_vehicle_camera::SeatBits(facts.seatFlagsRead, facts.seatFlags, facts.drivesIt, facts.aimsIt);
    seatView = true; // every CE seat the first-person view runs in has an eye: the occupant's head
    wheelSeat = false; // CE seats are steered by the hand (its own vehicle controls), never a wheel
    definitionIndex = facts.definition;

    // Once per seat: what the seat is, and what its role and name were read from.
    static VehicleCameraSeatKey logged{};
    if (!(logged == key))
    {
        logged = key;
        LOG("Vehicle cameras (Halo CE): %s, seat %d (%s): tag \"%s\", seat flags %08X (%s), definition %08X, bounds "
            "radius %.3f wu, occupant slots: %s%s%s",
            name, static_cast<int>(facts.seat), vehicle_camera_file::SeatText(seatBits).c_str(),
            facts.tagPath[0] ? facts.tagPath : "(not read)", facts.seatFlags,
            facts.seatFlagsRead ? "the role read from them" : "not read; the role from the occupant slots",
            facts.definition, static_cast<double>(facts.boundsRadius),
            facts.drivesIt ? "drives it" : "does not drive it", facts.aimsIt ? ", aims it" : ", does not aim it",
            mounted ? ", riding a carrier" : "");
    }
    return VehicleCameraSeatRead::Seated;
}

// What a selected camera asks for that CE does not do yet, for the log and the readout; empty when nothing.
std::string HaloCEVehicleCameraNotApplied(const vehicle_camera_select::ActiveCamera& camera)
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
            add("rotationTracking pitch (CE's view turns with the hull's heading only)");
        if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Marker))
            add("an origin naming a marker (the vehicle's origin is used)");
    }
    if (camera.hideRegions[0])
        add("hideRegions");
    if (camera.hideParts[0])
        add("hideParts");
    if (camera.fadeParts[0])
        add("fadeParts");
    if (camera.holdExposure)
        add("eyeAdjust");
    return list;
}

// Cold worker, every poll while the vehicle cameras are on in CE: the counts the game's thread keeps, as log lines
// when there is something to say. `seated` false = the player has left the seat.
void HaloCEVehicleCamerasColdLog(bool seated)
{
    static bool standInLogged = false, noTraceLogged = false;
    static float restLoggedWu = 0.0f, restSeenWu = 0.0f;
    static uint32_t trailStint = 0, headMissStint = 0, heldStint = 0, tracedStint = 0, placedStint = 0, nudgeStint = 0;
    const uint32_t collideFrames = g_haloceVehicleCameraCollideFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t noTraceFrames = g_haloceVehicleCameraNoTraceFrames.exchange(0, std::memory_order_relaxed);
    heldStint += g_haloceVehicleCameraHeldFrames.exchange(0, std::memory_order_relaxed);
    tracedStint += collideFrames;
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
        LOG("Vehicle cameras (Halo CE): this camera asks to be kept out of walls (\"collide\"), but CE's sight trace is "
            "not available, so it goes all the way out");
    }
    const uint32_t frames = g_haloceVehicleCameraFrames.exchange(0, std::memory_order_relaxed);
    placedStint += frames;
    nudgeStint += g_haloceVehicleCameraNudgeFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t standIn = g_haloceVehicleCameraStandInFrames.exchange(0, std::memory_order_relaxed);
    headMissStint += g_haloceVehicleCameraHeadMisses.exchange(0, std::memory_order_relaxed);
    static uint32_t seatFrameStint = 0;
    seatFrameStint += g_haloceVehicleCameraSeatFrameRefused.exchange(0, std::memory_order_relaxed);
    const uint32_t trail = g_haloceVehicleCameraLagTrailCm.exchange(0, std::memory_order_relaxed);
    trailStint = trail > trailStint ? trail : trailStint;
    if (!seated)
    {
        if (seatFrameStint)
            LOG("Vehicle cameras (Halo CE): in that seat the occupant's root node did not stand upright on the vehicle "
                "on %u frames, and the vehicle's frame served the seat's",
                seatFrameStint);
        seatFrameStint = 0;
        if (placedStint || nudgeStint)
            LOG("Vehicle cameras (Halo CE): in that seat a chase camera was placed on %u frames and a firstperson "
                "camera's offset moved the eye on %u",
                placedStint, nudgeStint);
        if (trailStint)
            LOG("Vehicle cameras (Halo CE): in that seat the lagged camera trailed its place by at most %u cm",
                trailStint);
        if (headMissStint)
            LOG("Vehicle cameras (Halo CE): in that seat the occupant's head could not be read on %u frames, and the "
                "seat stood in for it",
                headMissStint);
        if (tracedStint)
            LOG("Vehicle cameras (Halo CE): in that seat the camera's line was traced for walls on %u frames and held "
                "short of something on %u of them",
                tracedStint, heldStint);
        if (boomSeen)
            LOG("Vehicle cameras (Halo CE): in that seat the third-person boom went from %.1f to %.1f deg above level "
                "and was left at %.1f deg (kept for that seat and camera)",
                static_cast<double>(boomLowDeg), static_cast<double>(boomHighDeg), static_cast<double>(boomLastDeg));
        boomSeen = false;
        trailStint = headMissStint = heldStint = tracedStint = placedStint = nudgeStint = 0;
        standInLogged = noTraceLogged = false;
        restLoggedWu = restSeenWu = 0.0f;
        g_haloceVehicleCameraRestWu.store(0.0f, std::memory_order_relaxed);
        return;
    }
    // Once per seat a chase camera was placed in: where the seat, the head and the game's pivot sit on the vehicle.
    {
        static VehicleCameraSeatKey layoutLogged{};
        const HaloCEVehicleCameraLayout& layout = g_haloceVehicleCameraLayout;
        const uint32_t before = layout.seq.load(std::memory_order_acquire);
        if (before && !(before & 1u) && !(layout.seat == layoutLogged))
        {
            const VehicleCameraSeatKey seat = layout.seat;
            const bool seatValid = layout.seatValid, headValid = layout.headValid, pivotValid = layout.pivotValid,
                       seatFrame = layout.seatFrame;
            float s3[3], h3[3], p3[3];
            memcpy(s3, layout.seat3, sizeof(s3));
            memcpy(h3, layout.head3, sizeof(h3));
            memcpy(p3, layout.pivot3, sizeof(p3));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (layout.seq.load(std::memory_order_relaxed) == before)
            {
                layoutLogged = seat;
                constexpr double cm = 1.0 / vehicle_camera_frame::kWorldUnitsPerCm;
                LOG("Vehicle cameras (Halo CE): seat %d's layout on its vehicle, cm forward/left/up from the vehicle's "
                    "root node: the seat %s (%.0f %.0f %.0f; its frame %s), the head %s (%.0f %.0f %.0f), the game's "
                    "third-person pivot %s (%.0f %.0f %.0f)",
                    seat.seatIndex, seatValid ? "read" : "NOT read", s3[0] * cm, s3[1] * cm, s3[2] * cm,
                    seatFrame ? "taken" : "not taken, the vehicle's serves", headValid ? "read" : "NOT read",
                    h3[0] * cm, h3[1] * cm, h3[2] * cm, pivotValid ? "read" : "NOT read", p3[0] * cm, p3[1] * cm,
                    p3[2] * cm);
            }
        }
    }
    if (standIn && !standInLogged)
    {
        standInLogged = true;
        float behindCm = 0.0f, aboveCm = 0.0f;
        vehicle_camera_place::StandInOffsetCm(g_haloceVehicleCameraRadiusCm.load(std::memory_order_relaxed), behindCm,
                                              aboveCm);
        LOG("Vehicle cameras (Halo CE): where the game's own third-person camera rests could not be read for this "
            "seat, so the camera stands %.0f cm behind and %.0f cm above the vehicle's origin instead",
            static_cast<double>(behindCm), static_cast<double>(aboveCm));
    }
    const float restWu = g_haloceVehicleCameraRestWu.load(std::memory_order_relaxed);
    const bool settled = restWu == restSeenWu;
    restSeenWu = restWu;
    if (frames && restWu > 0.0f && settled && restWu > restLoggedWu * 1.1f)
    {
        restLoggedWu = restWu;
        LOG("Vehicle cameras (Halo CE): the game's own third-person camera rests %.2f world units (%.0f cm) from its "
            "pivot in this seat, %.0f cm of that above the pivot (it looked %.1f deg up), and the pivot is %.0f cm "
            "above the vehicle's origin",
            static_cast<double>(restWu), static_cast<double>(restWu / vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_haloceVehicleCameraRestRiseWu.load(std::memory_order_relaxed) /
                                vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_haloceVehicleCameraRestPitchDeg.load(std::memory_order_relaxed)),
            static_cast<double>(g_haloceVehicleCameraPivotUpWu.load(std::memory_order_relaxed) /
                                vehicle_camera_frame::kWorldUnitsPerCm));
    }
}
