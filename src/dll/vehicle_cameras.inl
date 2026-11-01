// VEHICLE CAMERAS: the runtime. Included by game.cpp, after the Halo 3 seat snapshot (Halo3ReadSeatSnapshot).
// The file's format, the selection and the geometry are engine-free (src/common/vehicle_camera_*.h); this is
// who runs them and what a game's seat camera asks of them.
//
// THREADS.
//   The cold worker (WaitThread, VehicleCameras_ColdTick) owns the file's store and the selector: it reads the
//     file, follows the seat the player is in, takes up the steps the menu asked for, and publishes the camera
//     in use with the seat it was chosen for. It logs and allocates; nothing else here does.
//   A game's camera thread reads that record once a frame and places its seat view from it
//     (Halo3VehicleCameraPlace), publishing the point for the threads that build the hands and the aim on it.
//   The menu's thread asks for steps (two atomics) and reads the readout line.
//
// OFF (vehicle_cameras = 0, the default): the cold worker publishes no camera, every reader returns false on one
// atomic load, and each game's seat view is exactly what it is without this file.
//
// HALO 3. A chase camera stands in for the seat's authored point in the mod's own seat camera (ApplyHeadLook),
// so what hangs off the rendered eye goes with it, and the hands' origin follows (ControllerWorldPoseEx). The
// camera director is left on its own third-person answer for that seat (halo3_native_seat_patch.inl), so the
// game draws the player's character in the seat and no first-person arms. Nothing the simulation reads is
// touched. The camera's mode turns the view (Halo3ApplySeatYawFollow): rotationTracking yaw follows the hull's
// heading, and pitch with it where the seat takes the hull's pitch. How the seat is steered is a separate
// choice (vehicle_control). A chase camera whose "aimMarker" says so shows a ring where the vehicle aims
// (Halo3VehicleAimMarkerStep), drawn on the headset's compositor.
// Not applied in this game yet: rotationTracking roll, pitch without yaw, the leash, collide, and an origin
// naming a marker (the vehicle's origin stands in). The cold worker says so in the log for a camera that asks
// for one.
//
// THE THIRD-PERSON BOOM (vehicle_boom_logic.h; halo3_vehicle_boom.inl reads the game's side). A camera of origin
// "thirdperson" stands where the game's own third-person camera would stand at a look pitch of the boom's own --
// never the aim's. The cold worker works out the seat's shape (the game's camera track across the flat game's look
// range, or an orbit of the pivot where the track cannot be read) and where the boom starts, and keeps the player's
// angle per seat and camera; the camera thread moves it with the right stick's Y, which the input hook hands over
// only where that axis aims nothing (Motion, the wheel), and puts it back on the hold that re-centres the view.

const char* Halo3VehicleIdName(uint32_t id);
const char* Halo3SeatVehicleName(const Halo3SeatSnapshot& seat, char (&buffer)[kHalo3VehicleNameCapacity]);

// The seat a camera was chosen for, so a game's camera thread never lays one seat's camera onto another.
struct VehicleCameraSeatKey
{
    uint32_t generation = 0;
    int32_t vehicle = -1;
    int32_t seatIndex = -1;
    uint32_t identity = 0;
    bool mounted = false;
    friend bool operator==(const VehicleCameraSeatKey&, const VehicleCameraSeatKey&) = default;
};

// One writer (the cold worker). `chase` is the readers' whole cost while no chase camera is in use, and
// `eyeNudge` / `showHeadlessBody` theirs while no firstperson camera moves the seat's eye / shows the body.
struct VehicleCameraPublished
{
    std::atomic<uint32_t> seq{0}; // even = stable
    std::atomic<uint32_t> chase{0};
    std::atomic<uint32_t> eyeNudge{0};
    std::atomic<uint32_t> showHeadlessBody{0};
    std::atomic<uint32_t> holdExposure{0};
    vehicle_camera_select::ActiveCamera camera{};
    VehicleCameraSeatKey seat{};
};
VehicleCameraPublished g_vehicleCameraPublished;

void VehicleCameraPublish(const vehicle_camera_select::ActiveCamera& camera, const VehicleCameraSeatKey& seat)
{
    VehicleCameraPublished& published = g_vehicleCameraPublished;
    published.seq.fetch_add(1, std::memory_order_acq_rel);
    published.camera = camera;
    published.seat = seat;
    published.chase.store(camera.valid && camera.chase ? 1u : 0u, std::memory_order_relaxed);
    const bool nudged = camera.offsetCm[0] != 0.0f || camera.offsetCm[1] != 0.0f || camera.offsetCm[2] != 0.0f;
    published.eyeNudge.store(camera.valid && !camera.chase && nudged ? 1u : 0u, std::memory_order_relaxed);
    published.showHeadlessBody.store(camera.valid && !camera.chase && camera.showHeadlessBody ? 1u : 0u, std::memory_order_relaxed);
    published.holdExposure.store(camera.valid && camera.holdExposure ? 1u : 0u, std::memory_order_relaxed);
    published.seq.fetch_add(1, std::memory_order_release);
}

// Whether the camera file asks any Halo 3 firstperson camera for "showHeadlessBody" (one writer, the cold worker): the body
// drawing's hooks (halo3_avatar.inl) are installed only while it does.
std::atomic<bool> g_vehicleCameraHeadlessBodyWanted{false};
bool Halo3VehicleCameraHeadlessBodyWanted()
{
    return g_vehicleCameraHeadlessBodyWanted.load(std::memory_order_acquire);
}

// A FIRSTPERSON CAMERA'S "showHeadlessBody" for exactly this seat: the occupant's body drawn without its head. The render
// thread's question once a frame in a seat: one atomic load while no camera in use asks for it. `known` false = the
// camera was being published both times it was read, so the answer is not known this time.
bool Halo3VehicleCameraShowsHeadlessBody(uint32_t generation, int32_t vehicle, int32_t seatIndex, uint32_t identity,
                                 bool mounted, bool& known)
{
    known = true;
    const VehicleCameraPublished& published = g_vehicleCameraPublished;
    if (!published.showHeadlessBody.load(std::memory_order_acquire))
        return false;
    const VehicleCameraSeatKey wanted{generation, vehicle, seatIndex, identity, mounted};
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const vehicle_camera_select::ActiveCamera camera = published.camera;
        const VehicleCameraSeatKey seat = published.seat;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (published.seq.load(std::memory_order_relaxed) != before)
            continue;
        return camera.valid && !camera.chase && camera.showHeadlessBody && seat == wanted;
    }
    known = false;
    return false;
}

// A CAMERA'S "eyeAdjust": false for exactly the seat the player is in (any camera type): the camera pass's question
// once a frame (engine_exposure_lock.inl holds the game's adaptation while it is true). One atomic load while no
// camera in use asks for it; a camera being published both times it is read answers false for that frame.
bool Halo3VehicleCameraHoldsExposure()
{
    const VehicleCameraPublished& published = g_vehicleCameraPublished;
    if (!published.holdExposure.load(std::memory_order_acquire))
        return false;
    Halo3SeatSnapshot seat;
    if (!Halo3ReadSeatSnapshot(seat))
        return false;
    const VehicleCameraSeatKey wanted{g_halo3RuntimeGeneration.load(std::memory_order_relaxed), seat.parentHandle,
                                      seat.seatIndex, seat.identity, seat.mounted};
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
        return hold && chosenFor == wanted;
    }
    return false;
}

// A FIRSTPERSON CAMERA'S OFFSET (vehicle_camera_file.h): how far the seat's eye is moved for this seat, in world
// units [forward, right, up] in the vehicle's own frame. False while the camera in use for exactly this seat is not
// a firstperson camera with an offset. The seat sampler's question, every frame of a seat: one atomic load while
// none is set.
bool Halo3VehicleCameraEyeNudge(uint32_t generation, int32_t vehicle, int32_t seatIndex, uint32_t identity,
                                bool mounted, float outWu[3])
{
    const VehicleCameraPublished& published = g_vehicleCameraPublished;
    if (!published.eyeNudge.load(std::memory_order_acquire))
        return false;
    const VehicleCameraSeatKey wanted{generation, vehicle, seatIndex, identity, mounted};
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const vehicle_camera_select::ActiveCamera camera = published.camera;
        const VehicleCameraSeatKey seat = published.seat;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (published.seq.load(std::memory_order_relaxed) != before)
            continue;
        if (!camera.valid || camera.chase || !(seat == wanted))
            return false;
        for (int axis = 0; axis < 3; ++axis)
        {
            outWu[axis] = camera.offsetCm[axis] * vehicle_camera_frame::kWorldUnitsPerCm;
            if (!std::isfinite(outWu[axis]))
                return false;
        }
        return true;
    }
    return false;
}

bool VehicleCameraRead(vehicle_camera_select::ActiveCamera& camera, VehicleCameraSeatKey& seat)
{
    const VehicleCameraPublished& published = g_vehicleCameraPublished;
    if (!published.chase.load(std::memory_order_acquire))
        return false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        camera = published.camera;
        seat = published.seat;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (published.seq.load(std::memory_order_relaxed) != before)
            continue;
        return camera.valid && camera.chase;
    }
    return false;
}

// The menu's line: which camera is in use, or why none is. One writer (the cold worker).
struct VehicleCameraReadout
{
    std::atomic<uint32_t> seq{0};
    char text[384]{};
};
VehicleCameraReadout g_vehicleCameraReadout;
std::atomic<int> g_vehicleCameraStepCamera{0};
std::atomic<int> g_vehicleCameraStepMode{0};
// The left stick click in a seat (vehicle_seat_buttons.h), posted by the input hook: the next way to control it.
std::atomic<int> g_vehicleControlStep{0};
// The seat's control mode, as vehicle_control's numbers: the player's choice for this seat where he made one,
// else the setting. -1 = not in a seat the vehicle cameras run in, where the setting alone decides.
std::atomic<int> g_vehicleControlEffective{-1};
// The camera buttons are the mod's in this seat (bit 0), and it is a passenger's (bit 1). Read by the input hook.
std::atomic<uint32_t> g_vehicleSeatButtons{0};
inline constexpr uint32_t kVehicleSeatButtonsLive = 1u, kVehicleSeatPassenger = 2u;

// How the seat the player is in is controlled (vehicle_control_logic.h). Any thread.
vehicle_control::Mode VehicleControlModeNow()
{
    const int effective = g_vehicleControlEffective.load(std::memory_order_acquire);
    return vehicle_control::FromConfig(effective >= 0 ? effective : g_config.vehicle_control);
}

// ---- the boom's shared state ----
// The cold worker's: the seat's boom, for the camera thread. One writer; `serial` moves with every publication.
struct VehicleBoomPublished
{
    std::atomic<uint32_t> seq{0}; // even = stable
    std::atomic<uint32_t> serial{0};
    VehicleCameraSeatKey seat{};
    int camera = -1;          // the camera (ActiveCamera::camera) it is for
    uint32_t startSerial = 0; // moved: the boom starts over at `start`
    float start = 0.0f;       // look pitch, radians
    float home = 0.0f;        // the camera's own start, where a re-centre puts it
    vehicle_boom::Shape shape{};
};
VehicleBoomPublished g_vehicleBoomPublished;
// The stick's Y is inverted for the boom in the seat the player is in (cold worker, every poll).
std::atomic<bool> g_vehicleBoomInverted{false};
// The input hook's: the right stick's Y while the boom has it, and when it was handed over.
std::atomic<float> g_vehicleBoomStick{0.0f};
std::atomic<uint64_t> g_vehicleBoomStickMs{0};
inline constexpr uint64_t kVehicleBoomStickFreshMs = 100;
// Re-centre asks: the hold that re-centres the view, and the menu's button.
std::atomic<uint32_t> g_vehicleBoomRecenter{0};
// The camera thread's: when a boom last placed the camera, for the input hook's question.
std::atomic<uint64_t> g_vehicleBoomLiveMs{0};
inline constexpr uint64_t kVehicleBoomLiveMs = 200;
// The camera thread's boom as it stands, and the game's own camera as it stood, for the cold worker to keep, show
// and check. One writer.
struct VehicleBoomCurrent
{
    std::atomic<uint32_t> seq{0};
    uint32_t serial = 0; // the publication it moves (0: the stand-in orbit)
    float pitch = 0.0f;
    bool sampled = false;
    uint64_t ms = 0;  // when
    float stood[3]{}; // the game's camera from its pivot, world units
    float looks[3]{}; // the way it looks
};
VehicleBoomCurrent g_vehicleBoomCurrent;
// For the menu (cold worker): bit 0 a boom is in use, bit 1 from the game's track, bit 2 inverted; its angle and
// the angles it reaches, degrees above level.
std::atomic<uint32_t> g_vehicleBoomShown{0};
std::atomic<float> g_vehicleBoomShownDeg{0.0f};
std::atomic<float> g_vehicleBoomShownLowDeg{0.0f};
std::atomic<float> g_vehicleBoomShownHighDeg{0.0f};

void VehicleCameraSetReadout(const std::string& text)
{
    VehicleCameraReadout& readout = g_vehicleCameraReadout;
    readout.seq.fetch_add(1, std::memory_order_acq_rel);
    const size_t length = text.size() < sizeof(readout.text) - 1 ? text.size() : sizeof(readout.text) - 1;
    memcpy(readout.text, text.data(), length);
    readout.text[length] = '\0';
    readout.seq.fetch_add(1, std::memory_order_release);
}

bool VehicleCameraGetReadout(char* out, size_t capacity)
{
    if (!out || capacity == 0)
        return false;
    out[0] = '\0';
    const VehicleCameraReadout& readout = g_vehicleCameraReadout;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const uint32_t before = readout.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        char text[sizeof(readout.text)];
        memcpy(text, readout.text, sizeof(text));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (readout.seq.load(std::memory_order_relaxed) != before)
            continue;
        text[sizeof(text) - 1] = '\0';
        const size_t length = strlen(text) < capacity - 1 ? strlen(text) : capacity - 1;
        memcpy(out, text, length);
        out[length] = '\0';
        return length != 0;
    }
    return false;
}

void VehicleCameraRequestStep(int cameraDirection, int modeDirection)
{
    if (cameraDirection)
        g_vehicleCameraStepCamera.fetch_add(cameraDirection > 0 ? 1 : -1, std::memory_order_relaxed);
    if (modeDirection)
        g_vehicleCameraStepMode.fetch_add(modeDirection > 0 ? 1 : -1, std::memory_order_relaxed);
}

// ---- Halo 3 ----------------------------------------------------------------------------------------------

// The chase camera's point this frame, before the player's own head movement. One writer (the camera thread).
struct Halo3VehicleCameraPoint
{
    std::atomic<uint32_t> seq{0};
    std::atomic<uint64_t> ms{0};
    float point[3]{};
};
Halo3VehicleCameraPoint g_halo3VehicleCameraPoint;
inline constexpr uint64_t kHalo3VehicleCameraPointFreshMs = 100;
vehicle_camera_place::State g_halo3VehicleCameraState; // camera thread
vehicle_camera_place::LagState g_halo3VehicleCameraLag; // camera thread
LARGE_INTEGER g_halo3VehicleCameraLagAt{};              // camera thread: the last lag step's clock
// Frames placed; those of them placed from the simulation's vehicle frame because the drawn root node did not
// check out; and those placed at the mod's stand-in because the game's own third-person camera could not be
// read. Counted by the camera thread, taken by the cold worker for its log lines.
std::atomic<uint32_t> g_halo3VehicleCameraFrames{0};
std::atomic<uint32_t> g_halo3VehicleCameraTickFrames{0};
std::atomic<uint32_t> g_halo3VehicleCameraStandInFrames{0};
// "collide": frames that asked for it and had a fresh trace, frames held short of something, and frames that
// asked with no trace to go on (the title's trace is not running for this seat).
std::atomic<uint32_t> g_halo3VehicleCameraCollideFrames{0};
std::atomic<uint32_t> g_halo3VehicleCameraHeldFrames{0};
// Frames a third-person camera kept its last rest place while the game's camera could not be read (camera thread).
std::atomic<uint32_t> g_halo3VehicleCameraRestKeptFrames{0};
// The lag follower's own count of starting over (on a leap of its place, on a runaway), as the camera thread last
// read it, and the furthest the camera has trailed its place since the cold worker last took it, in cm.
std::atomic<uint32_t> g_halo3VehicleCameraLagJumps{0};
std::atomic<uint32_t> g_halo3VehicleCameraLagRunaways{0};
std::atomic<uint32_t> g_halo3VehicleCameraLagLimited{0}; // frames the lag's limit held the camera (LagState::limited)
std::atomic<uint32_t> g_halo3VehicleCameraLagTrailCm{0};
std::atomic<uint32_t> g_halo3VehicleCameraNoTraceFrames{0};
// How much of the way out the camera may go, eased (vehicle_camera_place::EaseReach). Camera thread.
float g_halo3VehicleCameraReach = 1.0f;
// This pass's placement, for the leash on the head (Halo3VehicleCameraLeashHead), later in the same pass.
// Camera thread.
// The way a seat's vehicle faces for a view that turns with it: a tuned seat's own node, as its view follows; a seat
// with no tuned point its hull's frame (the node a mod map's vehicle seats its occupant on may be a part that turns
// with the aim). The aim ring takes the same, so it rests where the view comes to rest (Halo3ApplySeatYawFollow).
const float* Halo3VehicleFollowForward(const Halo3SeatSnapshot& seat)
{
    return Halo3SeatFollowsHull(static_cast<Halo3VehicleId>(seat.identity), seat.seatIndex, seat.mounted)
        ? seat.rawFwd : seat.hullFwd;
}

struct Halo3VehicleCameraPass
{
    bool placed = false;
    float reach = 1.0f;
    vehicle_camera_select::ActiveCamera camera{};
    vehicle_camera_place::Placed where{};
    // The seat the camera was placed for, and the way its vehicle faces in the snapshot it was placed from (the
    // frame a camera that turns with the vehicle follows): the aim ring's heading (Halo3VehicleAimMarkerStep).
    VehicleCameraSeatKey seat{};
    float vehicleForward[3]{};
};
Halo3VehicleCameraPass g_halo3VehicleCameraPass;
uint64_t g_halo3VehicleCameraReachAt = 0;
inline constexpr float kHalo3CameraReleasePerSecond = 1.5f;

// A chase camera is chosen for exactly the seat the snapshot shows. Any thread; one atomic load while none is.
bool Halo3VehicleCameraSelected(vehicle_camera_select::ActiveCamera& camera, Halo3SeatSnapshot& seat,
                                VehicleCameraSeatKey& key)
{
    VehicleCameraSeatKey chosenFor;
    if (!VehicleCameraRead(camera, chosenFor))
        return false;
    if (!Halo3ReadSeatSnapshot(seat))
        return false;
    key = VehicleCameraSeatKey{g_halo3RuntimeGeneration.load(std::memory_order_relaxed), seat.parentHandle,
                               seat.seatIndex, seat.identity, seat.mounted};
    return key == chosenFor;
}

// The seat sampler's question: is this seat shown from a chase camera? It is from the moment one is chosen, so
// the game's director is already drawing the seat in third person when the camera is first placed.
bool Halo3VehicleCameraThirdPerson()
{
    vehicle_camera_select::ActiveCamera camera;
    Halo3SeatSnapshot seat;
    VehicleCameraSeatKey key;
    return Halo3VehicleCameraSelected(camera, seat, key);
}

// The camera in use for exactly this seat, a firstperson one included. False while none is, or while it was
// being published both times it was read.
bool VehicleCameraReadForSeat(const VehicleCameraSeatKey& wanted, vehicle_camera_select::ActiveCamera& camera)
{
    const VehicleCameraPublished& published = g_vehicleCameraPublished;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        camera = published.camera;
        const VehicleCameraSeatKey seat = published.seat;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (published.seq.load(std::memory_order_relaxed) != before)
            continue;
        return camera.valid && seat == wanted;
    }
    return false;
}

// How the camera in use turns the view (Halo3ApplySeatYawFollow): its mode's rotationTracking. Yaw follows the
// hull's heading; pitch follows it too where the seat takes the hull's pitch at all, and only with yaw. A chase
// camera always says; a firstperson one says when the file gives it, or any of its modes, "rotationTracking" (as
// Halo Campaign Evolved's cockpit cameras do: "Untethered" [] and "Tethered" ["yaw", ...]). False: the camera does
// not say, and the game's setting (vehicle_view_follow) decides.
bool Halo3VehicleCameraViewTurn(bool& yaw, bool& pitch)
{
    vehicle_camera_select::ActiveCamera camera;
    Halo3SeatSnapshot seat;
    VehicleCameraSeatKey key;
    if (Halo3VehicleCameraSelected(camera, seat, key))
    {
        yaw = camera.rotYaw;
        pitch = camera.rotYaw && camera.rotPitch;
        return true;
    }
    if (!Halo3ReadSeatSnapshot(seat))
        return false;
    key = VehicleCameraSeatKey{g_halo3RuntimeGeneration.load(std::memory_order_relaxed), seat.parentHandle,
                               seat.seatIndex, seat.identity, seat.mounted};
    if (!VehicleCameraReadForSeat(key, camera) || camera.chase || !camera.rotationSet)
        return false;
    yaw = camera.rotYaw;
    pitch = camera.rotYaw && camera.rotPitch;
    return true;
}

bool Halo3VehicleCameraFollowWanted()
{
    bool yaw = false, pitch = false;
    return Halo3VehicleCameraViewTurn(yaw, pitch) && yaw;
}

// The game's own camera for the local player as its observer computed it this frame, before any effect and
// before the mod's seat camera: where it stands, and the point it turns about. For a seat the director draws in
// third person those are the game's chase camera and the seat's camera pivot. `forward`, when asked for, is
// the way it looks: the line a flat player's crosshair marks.
bool Halo3ReadObserverCamera(float position[3], float focus[3], float* forward = nullptr)
{
    Halo3ObserverCapture& cap = g_halo3ObserverCapture;
    if (!cap.valid.load(std::memory_order_acquire))
        return false;
    if (GetTickCount64() - cap.captureMs.load(std::memory_order_relaxed) > 200)
        return false;
    constexpr size_t positionIndex = (kHalo3ObserverPosOffset - kHalo3ObserverCaptureBase) / 4;
    constexpr size_t focusIndex = (kHalo3ObserverFocusOffset - kHalo3ObserverCaptureBase) / 4;
    constexpr size_t forwardIndex = (kHalo3ObserverFwdOffset - kHalo3ObserverCaptureBase) / 4;
    static_assert(positionIndex + 2 < kHalo3ObserverCaptureFloats && focusIndex + 2 < kHalo3ObserverCaptureFloats &&
                      forwardIndex + 2 < kHalo3ObserverCaptureFloats,
                  "the position, focus and forward triplets must sit inside the capture window");
    const uint32_t seq = cap.seq.load(std::memory_order_acquire);
    if (seq & 1u)
        return false;
    const float at[3] = {cap.floats[positionIndex], cap.floats[positionIndex + 1], cap.floats[positionIndex + 2]};
    const float about[3] = {cap.floats[focusIndex], cap.floats[focusIndex + 1], cap.floats[focusIndex + 2]};
    float along[3] = {cap.floats[forwardIndex], cap.floats[forwardIndex + 1], cap.floats[forwardIndex + 2]};
    if (cap.seq.load(std::memory_order_acquire) != seq)
        return false;
    if (!vehicle_camera_place::Finite3(at) || !vehicle_camera_place::Finite3(about))
        return false;
    if (forward)
    {
        if (!vehicle_camera_place::Finite3(along) || !vehicle_camera_frame::Normalize(along))
            return false;
        memcpy(forward, along, sizeof(along));
    }
    memcpy(position, at, sizeof(at));
    memcpy(focus, about, sizeof(about));
    return true;
}

// THE BOOM ON THE CAMERA THREAD: the seat's publication it holds, and its look pitch.
struct Halo3BoomCamera
{
    VehicleCameraSeatKey seat{};
    int camera = -1;
    uint32_t serial = 0;     // the last publication seen
    uint32_t heldSerial = 0; // the one held: another seat's may have been seen since
    uint32_t startSerial = 0;
    bool held = false;   // a publication for this seat and camera
    bool local = false;  // none came in time: the stand-in orbit
    float home = 0.0f;
    float pitch = 0.0f;
    vehicle_boom::Shape shape{};
    uint32_t recenterSeen = 0;
    LARGE_INTEGER at{};
};
Halo3BoomCamera g_halo3BoomCamera;

// Camera thread: the publication for this seat and camera into the boom, when there is a new one. One load while
// nothing changed.
void Halo3BoomTake(const VehicleCameraSeatKey& seat, int camera)
{
    Halo3BoomCamera& boom = g_halo3BoomCamera;
    if (!(boom.seat == seat) || boom.camera != camera)
    {
        boom.seat = seat;
        boom.camera = camera;
        boom.held = boom.local = false;
        boom.serial = 0;
        boom.at.QuadPart = 0;
        boom.recenterSeen = g_vehicleBoomRecenter.load(std::memory_order_relaxed);
    }
    const VehicleBoomPublished& published = g_vehicleBoomPublished;
    const uint32_t serial = published.serial.load(std::memory_order_acquire);
    if (!serial || serial == boom.serial)
        return;
    static vehicle_boom::Shape shape; // camera thread only
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const VehicleCameraSeatKey forSeat = published.seat;
        const int forCamera = published.camera;
        const uint32_t startSerial = published.startSerial;
        const float start = published.start;
        const float home = published.home;
        memcpy(&shape, &published.shape, sizeof(shape));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (published.seq.load(std::memory_order_relaxed) != before ||
            published.serial.load(std::memory_order_relaxed) != serial)
            continue;
        boom.serial = serial;
        if (!(forSeat == seat) || forCamera != camera || !shape.valid)
            return;
        const bool restart = !boom.held || boom.startSerial != startSerial;
        memcpy(&boom.shape, &shape, sizeof(shape));
        boom.home = home;
        boom.pitch = restart ? start : vehicle_boom::Hold(boom.pitch, boom.shape.range);
        boom.startSerial = startSerial;
        boom.heldSerial = serial;
        boom.held = true;
        boom.local = false;
        return;
    }
}

// Camera thread: no publication in time for this seat. The boom orbits the pivot within the flat game's own range,
// from the built-in start, until one comes.
void Halo3BoomStandIn()
{
    static vehicle_boom::Shape orbit; // camera thread only
    if (!orbit.valid)
        vehicle_boom::BuildOrbitShape(vehicle_boom::Range{}, orbit);
    Halo3BoomCamera& boom = g_halo3BoomCamera;
    memcpy(&boom.shape, &orbit, sizeof(orbit));
    boom.home = vehicle_boom::PitchForElevation(orbit, vehicle_boom::DefaultElevation(orbit, vehicle_boom::kBoomPitchUnset));
    boom.pitch = boom.home;
    boom.local = true;
}

// Camera thread: one step of the boom with the stick, and the re-centre.
void Halo3BoomStep()
{
    Halo3BoomCamera& boom = g_halo3BoomCamera;
    static LARGE_INTEGER frequency{};
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const float seconds = boom.at.QuadPart && frequency.QuadPart
        ? static_cast<float>(static_cast<double>(now.QuadPart - boom.at.QuadPart) / static_cast<double>(frequency.QuadPart))
        : 0.0f;
    boom.at = now;
    const uint32_t recenter = g_vehicleBoomRecenter.load(std::memory_order_relaxed);
    if (recenter != boom.recenterSeen)
    {
        boom.recenterSeen = recenter;
        boom.pitch = boom.home;
    }
    const uint64_t stickMs = g_vehicleBoomStickMs.load(std::memory_order_acquire);
    const float stick = stickMs && GetTickCount64() - stickMs <= kVehicleBoomStickFreshMs
        ? g_vehicleBoomStick.load(std::memory_order_relaxed)
        : 0.0f;
    boom.pitch = vehicle_boom::Step(boom.pitch, stick, seconds, g_config.vehicle_boom_rate_deg_s,
                                    g_vehicleBoomInverted.load(std::memory_order_relaxed), boom.shape.range);
}

// Camera thread: the boom and the game's own camera this frame, for the cold worker.
void Halo3BoomPublishCurrent(bool sampled, const float stood[3], const float looks[3])
{
    VehicleBoomCurrent& current = g_vehicleBoomCurrent;
    current.seq.fetch_add(1, std::memory_order_acq_rel);
    current.serial = g_halo3BoomCamera.held ? g_halo3BoomCamera.heldSerial : 0u;
    current.pitch = g_halo3BoomCamera.pitch;
    current.sampled = sampled;
    current.ms = GetTickCount64();
    memcpy(current.stood, stood, sizeof(current.stood));
    memcpy(current.looks, looks, sizeof(current.looks));
    current.seq.fetch_add(1, std::memory_order_release);
}

// WHERE THE GAME'S OWN THIRD-PERSON CAMERA STANDS in the seat ("origin": "thirdperson"). Its pivot is the game's,
// read every frame; where the camera stands from it is the boom's (Halo3BoomTake). The distance the game's camera
// stands off is still sampled: an orbiting boom (no track) stands that far out, and the log says it. It is the
// furthest the game's camera has stood in this seat while looking within kHalo3RestLevelSin of level: the game pulls
// its camera in from walls and through its change of view, and the furthest it stands is the distance it means.
// Until it has looked level, the furthest it has stood at any aim is used. Camera thread.
struct Halo3VehicleCameraRest
{
    VehicleCameraSeatKey seat{};
    float distance = 0.0f; // world units
    float rise = 0.0f;     // of that distance, the height above the pivot along the vehicle's up
    bool level = false;    // the distance and height were taken while the game's camera looked level
    uint32_t waited = 0;   // placements asked for before a distance was known
    // The last place read in this seat: the game's camera's pivot from the vehicle's origin, along the vehicle's
    // forward, left and up (world units). Kept through a frame the game's camera cannot be read in, so the camera
    // does not drop to the stand-in mid-flight and snap back.
    bool placeKept = false;
    float pivotLocal[3]{};
    // The pivot as last read, on the vehicle the same way (unsmoothed), and when: see THE PIVOT, CARRIED ON THE
    // VEHICLE AS DRAWN in Halo3VehicleCameraRestPlace.
    float pivotRead[3]{};
    LARGE_INTEGER pivotAt{};
};
Halo3VehicleCameraRest g_halo3VehicleCameraRest;
// How far the game's camera pivot moved on the vehicle from one camera pass to the next, at most, since the cold
// worker last took it (cm): near 0 for a pivot read on the same beat as the vehicle is drawn.
std::atomic<uint32_t> g_halo3VehicleCameraPivotStepCm{0};
// The pivot's movement on the vehicle is smoothed over this long (seconds).
inline constexpr float kHalo3RestPivotSmoothSeconds = 0.25f;
// How long the mod's panel shows the note after a change (vehicle_camera_note = 2, or 1 when the game's HUD cannot
// take it); the game's HUD keeps its own lines for as long as it keeps them.
inline constexpr float kVehicleCameraNoteSeconds = 3.0f;
// The resting distance in use, world units (0 = none yet), for the cold worker's log line, with the shape of the
// game's camera where it stood furthest: its height above its pivot along the vehicle's up, the pitch it looked
// at, and the pivot's own height above the vehicle's origin.
std::atomic<float> g_halo3VehicleCameraRestWu{0.0f};
std::atomic<float> g_halo3VehicleCameraRestUpWu{0.0f};
std::atomic<float> g_halo3VehicleCameraRestPitchDeg{0.0f};
std::atomic<float> g_halo3VehicleCameraRestPivotUpWu{0.0f};
inline constexpr float kHalo3RestKnownWu = 0.25f; // nearer than this the director is still in the seat's own view
inline constexpr float kHalo3RestFurthestWu = 8.0f;
inline constexpr float kHalo3RestPivotReachWu = 10.0f; // a pivot further than this from the vehicle is not its seat's
inline constexpr uint32_t kHalo3RestWaitFrames = 45;
inline constexpr float kHalo3RestLevelSin = 0.26f; // within about 15 degrees of level

// Fills `in` with the place. False: not known YET, and the seat keeps the view it has for this frame. True with
// in.restValid false: it could not be read at all, and the caller's stand-in applies.
bool Halo3VehicleCameraRestPlace(const VehicleCameraSeatKey& seat, int camera, vehicle_camera_place::Inputs& in)
{
    Halo3VehicleCameraRest& rest = g_halo3VehicleCameraRest;
    if (!(rest.seat == seat))
    {
        rest = {};
        rest.seat = seat;
    }
    // A vehicle larger than a few metres has its camera's pivot and resting place further out in proportion.
    const float radiusWu = std::isfinite(in.vehicleRadiusCm) && in.vehicleRadiusCm > 0.0f
        ? in.vehicleRadiusCm * vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
    const float pivotReachWu = 2.0f * radiusWu > kHalo3RestPivotReachWu ? 2.0f * radiusWu : kHalo3RestPivotReachWu;
    const float furthestWu = 4.0f * radiusWu > kHalo3RestFurthestWu ? 4.0f * radiusWu : kHalo3RestFurthestWu;
    float at[3], about[3], looks[3];
    bool observed = Halo3ReadObserverCamera(at, about, looks);
    if (observed)
    {
        const float fromVehicle[3] = {about[0] - in.vehicleOrigin[0], about[1] - in.vehicleOrigin[1],
                                      about[2] - in.vehicleOrigin[2]};
        observed = vehicle_camera_frame::Dot(fromVehicle, fromVehicle) <= pivotReachWu * pivotReachWu;
    }
    float stood[3]{};
    if (observed)
    {
        for (int axis = 0; axis < 3; ++axis)
            stood[axis] = at[axis] - about[axis];
        const float distance = sqrtf(vehicle_camera_frame::Dot(stood, stood));
        const float lookUp = vehicle_camera_frame::Dot(looks, in.vehicle.up);
        const bool level = std::fabs(lookUp) <= kHalo3RestLevelSin;
        // The first level sample replaces whatever was taken at another aim; after that only a level one that
        // stands further does.
        const bool take = std::isfinite(distance) && distance <= furthestWu &&
                          (level ? (!rest.level || distance > rest.distance) : (!rest.level && distance > rest.distance));
        if (take)
        {
            rest.distance = distance;
            rest.rise = vehicle_camera_frame::Dot(stood, in.vehicle.up);
            rest.level = level;
            const float pivot[3] = {about[0] - in.vehicleOrigin[0], about[1] - in.vehicleOrigin[1],
                                    about[2] - in.vehicleOrigin[2]};
            g_halo3VehicleCameraRestUpWu.store(rest.rise, std::memory_order_relaxed);
            g_halo3VehicleCameraRestPitchDeg.store(
                asinf(lookUp < -1.0f ? -1.0f : (lookUp > 1.0f ? 1.0f : lookUp)) * 57.29578f,
                std::memory_order_relaxed);
            g_halo3VehicleCameraRestPivotUpWu.store(vehicle_camera_frame::Dot(pivot, in.vehicle.up),
                                                    std::memory_order_relaxed);
        }
    }
    // THE BOOM: the publication for this seat, else -- once the wait is over -- the stand-in orbit.
    Halo3BoomTake(seat, camera);
    Halo3BoomCamera& boom = g_halo3BoomCamera;
    if (!boom.held && !boom.local && rest.waited >= kHalo3RestWaitFrames)
        Halo3BoomStandIn();
    if (observed && (boom.held || boom.local))
    {
        Halo3BoomStep();
        Halo3BoomPublishCurrent(true, stood, looks);
        // An orbit stands as far out as the game's camera does, so it waits for that; a track has its own.
        if (boom.shape.track || rest.distance >= kHalo3RestKnownWu)
        {
            in.restValid = true;
            vehicle_boom::Offset(boom.shape, boom.pitch, rest.distance, in.restOffset);
            g_halo3VehicleCameraRestWu.store(rest.distance, std::memory_order_relaxed);
            g_vehicleBoomLiveMs.store(GetTickCount64(), std::memory_order_release);
            // THE PIVOT, CARRIED ON THE VEHICLE AS DRAWN. The game's camera is read here and the vehicle is drawn
            // on a beat of its own; a world point taken from one and an offset laid along the other differ by
            // however far the vehicle moved between the two, which grows with speed and shows as jitter. So the
            // pivot is read as a point ON the vehicle, its movement there smoothed (it is a place on the vehicle,
            // for that seat), and rebuilt each pass from the vehicle as drawn: the camera then moves on one clock.
            const float pivot[3] = {about[0] - in.vehicleOrigin[0], about[1] - in.vehicleOrigin[1],
                                    about[2] - in.vehicleOrigin[2]};
            const float read[3] = {vehicle_camera_frame::Dot(pivot, in.vehicle.forward),
                                   vehicle_camera_frame::Dot(pivot, in.vehicle.left),
                                   vehicle_camera_frame::Dot(pivot, in.vehicle.up)};
            static LARGE_INTEGER frequency{};
            if (!frequency.QuadPart)
                QueryPerformanceFrequency(&frequency);
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const float seconds = rest.pivotAt.QuadPart && frequency.QuadPart
                ? static_cast<float>(static_cast<double>(now.QuadPart - rest.pivotAt.QuadPart) /
                                     static_cast<double>(frequency.QuadPart))
                : -1.0f;
            rest.pivotAt = now;
            if (rest.placeKept && seconds >= 0.0f && seconds <= 0.5f)
            {
                float step2 = 0.0f;
                for (int axis = 0; axis < 3; ++axis)
                    step2 += (read[axis] - rest.pivotRead[axis]) * (read[axis] - rest.pivotRead[axis]);
                const float stepCm = sqrtf(step2) * 304.8f;
                const uint32_t step = std::isfinite(stepCm) ? static_cast<uint32_t>(stepCm < 1.0e8f ? stepCm : 1.0e8f)
                                                            : 0u;
                uint32_t most = g_halo3VehicleCameraPivotStepCm.load(std::memory_order_relaxed);
                while (step > most &&
                       !g_halo3VehicleCameraPivotStepCm.compare_exchange_weak(most, step, std::memory_order_relaxed))
                {
                }
                const float blend = 1.0f - std::exp(-seconds / kHalo3RestPivotSmoothSeconds);
                for (int axis = 0; axis < 3; ++axis)
                    rest.pivotLocal[axis] += (read[axis] - rest.pivotLocal[axis]) * blend;
            }
            else
            {
                memcpy(rest.pivotLocal, read, sizeof(rest.pivotLocal));
            }
            memcpy(rest.pivotRead, read, sizeof(rest.pivotRead));
            rest.placeKept = true;
            for (int axis = 0; axis < 3; ++axis)
                in.rest[axis] = in.vehicleOrigin[axis] + in.vehicle.forward[axis] * rest.pivotLocal[0] +
                                in.vehicle.left[axis] * rest.pivotLocal[1] + in.vehicle.up[axis] * rest.pivotLocal[2];
            return true;
        }
    }
    // The game's camera could not be read this frame, after it had been in this seat: the last place, carried on
    // the vehicle, rather than the stand-in -- which on a large vehicle is inside its hull, and a camera that drops
    // there and back snaps.
    if (rest.placeKept && (boom.held || boom.local))
    {
        in.restValid = true;
        for (int axis = 0; axis < 3; ++axis)
            in.rest[axis] = in.vehicleOrigin[axis] + in.vehicle.forward[axis] * rest.pivotLocal[0] +
                            in.vehicle.left[axis] * rest.pivotLocal[1] + in.vehicle.up[axis] * rest.pivotLocal[2];
        vehicle_boom::Offset(boom.shape, boom.pitch, rest.distance, in.restOffset);
        g_halo3VehicleCameraRestKeptFrames.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    if (rest.waited < kHalo3RestWaitFrames)
    {
        ++rest.waited;
        return false;
    }
    in.restValid = false;
    return true;
}

// Camera thread, once a camera pass. `viewYaw` is the heading the player's room faces. True with `out` the
// world point the seat view is seen from: a chase camera is in use for exactly the seat the snapshot shows.
bool Halo3VehicleCameraPlace(float viewYaw, float out[3])
{
    g_halo3VehicleCameraPass.placed = false;
    vehicle_camera_select::ActiveCamera camera;
    Halo3SeatSnapshot seat;
    VehicleCameraSeatKey key;
    if (!Halo3VehicleCameraSelected(camera, seat, key))
        return false;

    vehicle_camera_place::Inputs in;
    bool drawn = seat.rootValid;
    if (drawn)
    {
        memcpy(in.vehicleOrigin, seat.rootPos, sizeof(in.vehicleOrigin));
        memcpy(in.vehicle.forward, seat.rootFwd, sizeof(in.vehicle.forward));
        memcpy(in.vehicle.left, seat.rootLeft, sizeof(in.vehicle.left));
        memcpy(in.vehicle.up, seat.rootUp, sizeof(in.vehicle.up));
        drawn = vehicle_camera_frame::Usable(in.vehicle);
    }
    if (!drawn)
    {
        if (!vehicle_camera_frame::FromForwardUp(seat.hullFwd, seat.hullUp, in.vehicle))
            return false;
        memcpy(in.vehicleOrigin, seat.hullPos, sizeof(in.vehicleOrigin));
    }
    // A vehicle the mod has no seat point for (one it does not know) has neither of these; a camera measured
    // from one then falls back to the vehicle's origin.
    in.seatValid = seat.anchorBaseValid;
    memcpy(in.seat, seat.anchorBase, sizeof(in.seat));
    in.headValid = seat.anchorValid;
    memcpy(in.head, seat.anchor, sizeof(in.head));
    // The frame of the node the seat is attached to, as drawn (the seat sampler's live node; the object's own frame
    // where it has none): a camera measured from the seat or the head turns with it -- a walker's torso, a turret.
    in.seatFrameValid = vehicle_camera_frame::FromForwardUp(seat.fwd, seat.up, in.seatFrame);
    // The head as a frame: the seat's, turned as the head has turned since it settled ("origin": "playerhead").
    in.headFrameValid = seat.headFrameValid && vehicle_camera_frame::FromForwardUp(seat.headFwd, seat.headUp, in.headFrame);
    in.viewYaw = viewYaw;
    in.vehicleRadiusCm = std::isfinite(seat.boundsRadiusWu) && seat.boundsRadiusWu > 0.0f
        ? seat.boundsRadiusWu / vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
    if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::ThirdPerson) &&
        !Halo3VehicleCameraRestPlace(key, camera.camera, in))
        return false;

    float point[3];
    vehicle_camera_place::Placed placed;
    if (!vehicle_camera_place::Place(camera, in, g_halo3VehicleCameraState, point, &placed))
        return false;
    const float* pivot = placed.pivot;
    float reachUsed = 1.0f;

    // LAG ("lag"): the boom swings to its place on a spring (vehicle_camera_place::FollowLagOnBoom: the offset from
    // the pivot lags, the vehicle's travel never does), on the frame's own clock -- a millisecond tick is a sixth of
    // a frame at 90 Hz. Before the wall check, so a wall still holds the lagged camera short.
    {
        static LARGE_INTEGER frequency{};
        if (!frequency.QuadPart)
            QueryPerformanceFrequency(&frequency);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const float seconds = g_halo3VehicleCameraLagAt.QuadPart && frequency.QuadPart
            ? static_cast<float>(static_cast<double>(now.QuadPart - g_halo3VehicleCameraLagAt.QuadPart) /
                                 static_cast<double>(frequency.QuadPart))
            : -1.0f;
        g_halo3VehicleCameraLagAt = now;
        vehicle_camera_place::FollowLagOnBoom(camera, pivot, point, seconds, g_halo3VehicleCameraLag, point);
        g_halo3VehicleCameraLagJumps.store(g_halo3VehicleCameraLag.jumps, std::memory_order_relaxed);
        g_halo3VehicleCameraLagRunaways.store(g_halo3VehicleCameraLag.runaways, std::memory_order_relaxed);
        g_halo3VehicleCameraLagLimited.store(g_halo3VehicleCameraLag.limited, std::memory_order_relaxed);
        const float trailCm = g_halo3VehicleCameraLag.trailWu * 304.8f;
        const uint32_t trail = std::isfinite(trailCm) && trailCm > 0.0f
            ? static_cast<uint32_t>(trailCm < 1.0e8f ? trailCm : 1.0e8f) : 0u;
        uint32_t furthest = g_halo3VehicleCameraLagTrailCm.load(std::memory_order_relaxed);
        while (trail > furthest &&
               !g_halo3VehicleCameraLagTrailCm.compare_exchange_weak(furthest, trail, std::memory_order_relaxed))
        {
        }
    }

    // HELD SHORT OF A WALL ("collide"): the line from the pivot to the camera's place is traced by the title's own
    // simulation thread beside the aim's ray (legacy_sight_trace.inl), and the camera stops short of what it meets.
    // The trace is a tick or two behind the line it is read against; at the speed a camera moves that is a few
    // centimetres. With no trace to go on the camera goes all the way, as before.
    if (camera.collide)
    {
        const float toward[3] = {point[0] - pivot[0], point[1] - pivot[1], point[2] - pivot[2]};
        const float length = sqrtf(vehicle_camera_frame::Dot(toward, toward));
        float wanted = 1.0f;
        if (std::isfinite(length) && length > 0.05f)
        {
            const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_relaxed);
            SightAuxRay ray{};
            ray.generation = generation;
            for (int axis = 0; axis < 3; ++axis)
            {
                ray.origin[axis] = pivot[axis];
                ray.dir[axis] = toward[axis] / length;
            }
            Sight_PublishCameraRay(GameTitle::Halo3, ray);
            bool hit = false;
            float rangeWu = 0.0f;
            if (Sight_ReadCameraTrace(GameTitle::Halo3, generation, 150, hit, rangeWu))
            {
                wanted = vehicle_camera_place::CollideReach(hit, rangeWu, length, camera.collideMarginCm);
                g_halo3VehicleCameraCollideFrames.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                g_halo3VehicleCameraNoTraceFrames.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const uint64_t nowMs = GetTickCount64();
        const float seconds = g_halo3VehicleCameraReachAt && nowMs > g_halo3VehicleCameraReachAt
            ? static_cast<float>(nowMs - g_halo3VehicleCameraReachAt) * 0.001f
            : 0.0f;
        g_halo3VehicleCameraReachAt = nowMs;
        const float reach = vehicle_camera_place::EaseReach(g_halo3VehicleCameraReach, wanted,
                                                            seconds > 0.1f ? 0.1f : seconds,
                                                            kHalo3CameraReleasePerSecond);
        if (reach < 0.999f)
        {
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = pivot[axis] + toward[axis] * reach;
            g_halo3VehicleCameraHeldFrames.fetch_add(1, std::memory_order_relaxed);
            reachUsed = reach;
        }
    }
    else
    {
        g_halo3VehicleCameraReach = 1.0f;
    }

    g_halo3VehicleCameraPass.placed = true;
    g_halo3VehicleCameraPass.reach = reachUsed;
    g_halo3VehicleCameraPass.camera = camera;
    g_halo3VehicleCameraPass.where = placed;
    g_halo3VehicleCameraPass.seat = key;
    memcpy(g_halo3VehicleCameraPass.vehicleForward, Halo3VehicleFollowForward(seat),
           sizeof(g_halo3VehicleCameraPass.vehicleForward));

    Halo3VehicleCameraPoint& published = g_halo3VehicleCameraPoint;
    published.seq.fetch_add(1, std::memory_order_acq_rel);
    memcpy(published.point, point, sizeof(published.point));
    published.ms.store(GetTickCount64(), std::memory_order_relaxed);
    published.seq.fetch_add(1, std::memory_order_release);
    g_halo3VehicleCameraFrames.fetch_add(1, std::memory_order_relaxed);
    if (!drawn)
        g_halo3VehicleCameraTickFrames.fetch_add(1, std::memory_order_relaxed);
    if (g_halo3VehicleCameraState.standIn)
        g_halo3VehicleCameraStandInFrames.fetch_add(1, std::memory_order_relaxed);
    memcpy(out, point, sizeof(point));
    return true;
}

void Halo3VehicleCameraBeginPass()
{
    g_halo3VehicleCameraPass.placed = false;
}

// Camera thread, after Halo3VehicleCameraPlace in the same pass: the player's head movement `head` (world units,
// added to the camera's point) held inside the camera's leash. False when this pass placed no chase camera or the
// camera sets no leash.
bool Halo3VehicleCameraLeashHead(float head[3])
{
    const Halo3VehicleCameraPass& pass = g_halo3VehicleCameraPass;
    return pass.placed && vehicle_camera_place::LeashHead(pass.camera, pass.where, pass.reach, head);
}

// Any thread. The point the camera thread last placed, while it is fresh.
bool Halo3VehicleCameraPublishedPoint(float out[3])
{
    if (!g_vehicleCameraPublished.chase.load(std::memory_order_acquire))
        return false;
    const Halo3VehicleCameraPoint& published = g_halo3VehicleCameraPoint;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.seq.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        float point[3];
        memcpy(point, published.point, sizeof(point));
        const uint64_t ms = published.ms.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (published.seq.load(std::memory_order_relaxed) != before)
            continue;
        if (GetTickCount64() - ms > kHalo3VehicleCameraPointFreshMs)
            return false;
        memcpy(out, point, sizeof(point));
        return true;
    }
    return false;
}

// THE VEHICLE AIM RING (Halo Campaign Evolved's "aimMarker"; vehicle_aim_marker_logic.h): a ring where the VEHICLE
// aims, beside the crosshair, while a chase camera whose file entry asks for it is up. Placed by the camera thread once
// a camera pass, against that pass's own view: the eye the view is seen from and the way the pass maps the world into
// the headset's space (ApplyHeadLook). Drawn by the frame thread from the stereo centre of the frame it submits. What
// crosses between them is a direction in the headset's space and a distance; the head pose never enters it, so a
// fresher head at submit cannot pull the ring off its point. Its depth is the simulation's trace of the ring's own ray
// (Sight_PublishMarkerRay), a tick or two behind: a range crosses that boundary, never a world point. Nothing the
// simulation reads is written.
struct Halo3VehicleAimMarkerMark
{
    std::atomic<uint32_t> seq{0};
    std::atomic<uint64_t> ms{0}; // when it was placed; 0 = no ring
    float vrYaw = 0.0f, vrPitch = 0.0f, distanceM = 0.0f;
};
Halo3VehicleAimMarkerMark g_halo3VehicleAimMarker;
inline constexpr uint64_t kHalo3VehicleAimMarkerFreshMs = 100;
// The range along the ring's ray, held for the seat it was measured in. Camera thread.
vehicle_aim_marker::HeldRange g_halo3VehicleAimMarkerRange;
VehicleCameraSeatKey g_halo3VehicleAimMarkerSeat{};
// Passes that placed the ring, and those of them with no fresh trace of its ray (drawn at a held depth or the trace's
// far end). Counted by the camera thread, taken by the cold worker for its log line.
std::atomic<uint32_t> g_halo3VehicleAimMarkerFrames{0};
std::atomic<uint32_t> g_halo3VehicleAimMarkerUntracedFrames{0};

void Halo3VehicleAimMarkerClear()
{
    Halo3VehicleAimMarkerMark& mark = g_halo3VehicleAimMarker;
    if (!mark.ms.load(std::memory_order_relaxed))
        return;
    mark.seq.fetch_add(1, std::memory_order_acq_rel);
    mark.ms.store(0, std::memory_order_relaxed);
    mark.seq.fetch_add(1, std::memory_order_release);
}

// Camera thread, at the end of a camera pass, once the view is where it will be drawn from: `eye` that point (world
// units), and `followed` with `hullYaw` / `hullPitch` the vehicle frame the view's rotation was composed in, when it
// was (Halo3ReadRollStableFollow). The ring is placed in a pass that placed a chase camera asking for it, or in the
// seat view of a firstperson camera whose mode asks for it (a Tethered one, as Halo Campaign Evolved's cockpit
// cameras show it), and is taken down in any other.
void Halo3VehicleAimMarkerStep(const float eye[3], bool followed, float hullYaw, float hullPitch)
{
    const Halo3VehicleCameraPass& pass = g_halo3VehicleCameraPass;
    VehicleCameraSeatKey seatKey = pass.seat;
    float vehicleForward[3] = {pass.vehicleForward[0], pass.vehicleForward[1], pass.vehicleForward[2]};
    bool wanted = pass.placed && pass.camera.aimMarker;
    if (!pass.placed && g_config.vehicle_aim_marker && eye)
    {
        Halo3SeatSnapshot seat;
        vehicle_camera_select::ActiveCamera camera;
        if (Halo3ReadSeatSnapshot(seat))
        {
            const VehicleCameraSeatKey key{g_halo3RuntimeGeneration.load(std::memory_order_relaxed),
                                           seat.parentHandle, seat.seatIndex, seat.identity, seat.mounted};
            if (VehicleCameraReadForSeat(key, camera) && !camera.chase && camera.rotationSet && camera.aimMarker)
            {
                wanted = true;
                seatKey = key;
                memcpy(vehicleForward, Halo3VehicleFollowForward(seat), sizeof(vehicleForward));
            }
        }
    }
    if (!g_config.vehicle_aim_marker || !wanted || !eye)
    {
        Halo3VehicleAimMarkerClear();
        return;
    }
    // A seat measures afresh: the range held in the last one says nothing of this one's surroundings.
    if (!(seatKey == g_halo3VehicleAimMarkerSeat))
    {
        g_halo3VehicleAimMarkerSeat = seatKey;
        g_halo3VehicleAimMarkerRange.Reset();
    }
    // The line the vehicle aims along: the vehicle's heading at the game's aim pitch, FROM THE EYE THIS PASS'S VIEW
    // IS DRAWN FROM, so the ring sits on the heading wherever the camera is and however near the trace stops: its
    // place in the view is the heading alone, and the range only sets its depth. Not from the seat's engine eye
    // (g_baseCam): that is a game tick old and the camera can sit metres to its side (a side camera, a chase camera
    // swinging wide in a turn), so a point on its line, seen from here, slid across the view as the vehicle turned,
    // furthest when the trace stopped on the ground a few metres out. Nor from the game's third-person camera, which
    // swings round the vehicle to look along the aim. That camera gives the pitch only.
    float at[3], about[3], looks[3], dir[3];
    if (!Halo3ReadObserverCamera(at, about, looks) || !vehicle_aim_marker::Direction(vehicleForward, looks, dir))
    {
        Halo3VehicleAimMarkerClear();
        return;
    }
    memcpy(at, eye, sizeof(at));
    const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_relaxed);
    SightAuxRay ray{};
    ray.generation = generation;
    for (int axis = 0; axis < 3; ++axis)
    {
        ray.origin[axis] = at[axis];
        ray.dir[axis] = dir[axis];
    }
    Sight_PublishMarkerRay(GameTitle::Halo3, ray);
    bool hit = false;
    float rangeWu = 0.0f;
    const bool traced = Sight_ReadMarkerTrace(GameTitle::Halo3, generation, kSightFreshMs, hit, rangeWu);
    const float range = g_halo3VehicleAimMarkerRange.Use(traced, hit, rangeWu, kSightTraceLengthWu);
    // Pulled back off the surface as the crosshair is, then seen from the eye this pass's view is drawn from.
    const float unitsPerVrMetre = g_worldScale.load(std::memory_order_relaxed);
    const float along = vehicle_aim_marker::PullBack(range, g_config.reticle_surface_offset_m * unitsPerVrMetre);
    float point[3], fromEye[3], lengthWu = 0.0f, vrYaw = 0.0f, vrPitch = 0.0f;
    if (!(unitsPerVrMetre > 0.0f) || !shot_point::PointAlongRay(at, dir, along, point) ||
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
    if (!traced)
        g_halo3VehicleAimMarkerUntracedFrames.fetch_add(1, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Dev: where the ring sits across the view (degrees right of the head's straight ahead), against how fast the
    // vehicle turns (degrees a second, right +), once a second: a ring that slides against the turn shows here.
    {
        struct RingWitness
        {
            uint64_t sinceMs = 0, lastMs = 0;
            float lastHeading = 0.0f;
            bool haveHeading = false;
            uint32_t passes = 0, followedPasses = 0;
            double offsetSum = 0.0, turnSum = 0.0, offsetTimesTurn = 0.0, rangeSum = 0.0;
            float offsetMin = 1.0e9f, offsetMax = -1.0e9f;
            // The hull against the view's yaw reference (constant while the view follows the hull), and the ring's
            // line from the eye against the hull's heading (what the eye's place and the range add).
            float hullRefMin = 1.0e9f, hullRefMax = -1.0e9f, ringHullMin = 1.0e9f, ringHullMax = -1.0e9f;
        };
        static thread_local RingWitness w;
        const uint64_t nowMs = GetTickCount64();
        float hq[4], hp[3];
        const float heading = std::atan2(vehicleForward[1], vehicleForward[0]);
        if (VR_GetHeadPose(hq, hp))
        {
            const float x = hq[0], y = hq[1], z = hq[2], qw = hq[3];
            const float headYaw = std::atan2(-2.0f * (qw * y + x * z), (1.0f - 2.0f * (x * x + y * y)));
            const float offsetDeg = -Halo3WrapPi(vrYaw - headYaw) * 57.2958f;   // right of straight ahead +
            float turnDegS = 0.0f;
            if (w.haveHeading && nowMs > w.lastMs)
                turnDegS = -Halo3WrapPi(heading - w.lastHeading) * 57.2958f * 1000.0f /
                           static_cast<float>(nowMs - w.lastMs);                 // right turn +
            if (!w.sinceMs)
                w.sinceMs = nowMs;
            ++w.passes;
            w.followedPasses += followed ? 1u : 0u;
            w.offsetSum += offsetDeg;
            w.turnSum += turnDegS;
            w.offsetTimesTurn += static_cast<double>(offsetDeg) * turnDegS;
            w.rangeSum += range / unitsPerVrMetre;
            w.offsetMin = offsetDeg < w.offsetMin ? offsetDeg : w.offsetMin;
            w.offsetMax = offsetDeg > w.offsetMax ? offsetDeg : w.offsetMax;
            const float hullRef = Halo3WrapPi(heading - g_gameYawRef) * 57.2958f;
            const float ringHull = Halo3WrapPi(std::atan2(fromEye[1], fromEye[0]) - heading) * 57.2958f;
            w.hullRefMin = hullRef < w.hullRefMin ? hullRef : w.hullRefMin;
            w.hullRefMax = hullRef > w.hullRefMax ? hullRef : w.hullRefMax;
            w.ringHullMin = ringHull < w.ringHullMin ? ringHull : w.ringHullMin;
            w.ringHullMax = ringHull > w.ringHullMax ? ringHull : w.ringHullMax;
        }
        if (nowMs != w.lastMs)
        {
            w.lastHeading = heading;
            w.lastMs = nowMs;
            w.haveHeading = true;
        }
        if (w.passes && nowMs - w.sinceMs >= 1000)
        {
            const double n = static_cast<double>(w.passes);
            LOG("Vehicle cameras (dev): aim ring %s, %u passes (%u followed): across the view mean %+.2f deg (%+.2f .. "
                "%+.2f), turning mean %+.1f deg/s, offset x turn %+.1f, range mean %.1f m; hull - view ref %+.1f .. "
                "%+.1f deg; ring line - hull %+.1f .. %+.1f deg",
                pass.placed ? "chase" : "seat view", w.passes, w.followedPasses, w.offsetSum / n, w.offsetMin,
                w.offsetMax, w.turnSum / n, w.offsetTimesTurn / n, w.rangeSum / n, w.hullRefMin, w.hullRefMax,
                w.ringHullMin, w.ringHullMax);
            const RingWitness kept = w;
            w = RingWitness{};
            w.lastMs = kept.lastMs;
            w.lastHeading = kept.lastHeading;
            w.haveHeading = kept.haveHeading;
        }
    }
#endif
}

// Any thread (the frame thread draws it). The ring's direction in the headset's space (yaw about +Y, pitch up, the
// reticle's convention) and its distance from the eye in metres, while the camera thread keeps placing it.
bool Halo3VehicleAimMarkerRead(float& vrYaw, float& vrPitch, float& distanceM)
{
    const Halo3VehicleAimMarkerMark& mark = g_halo3VehicleAimMarker;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = mark.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const uint64_t ms = mark.ms.load(std::memory_order_relaxed);
        const float yaw = mark.vrYaw, pitch = mark.vrPitch, distance = mark.distanceM;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (mark.seq.load(std::memory_order_relaxed) != before)
            continue;
        const uint64_t nowMs = GetTickCount64();
        if (!ms || nowMs < ms || nowMs - ms > kHalo3VehicleAimMarkerFreshMs)
            return false;
        if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(distance) || distance <= 0.0f)
            return false;
        vrYaw = yaw;
        vrPitch = pitch;
        distanceM = distance;
        return true;
    }
    return false;
}

// The cold worker's reading of the Halo 3 seat. Unknown: seated, but this moment's snapshot could not be read;
// the answer of the last tick stands. A vehicle the mod does not know is a seat like any other here: its name
// is its own (Halo3SeatVehicleName), and the file's entry whose "match" is found in it, else "default", is the one
// it gets.
// `seatView`: the mod has a first-person seat point for this seat, so the seat view is a view of its own; a
// vehicle it does not know (a custom one) has none and starts in third person. `wheelSeat`: the driver's seat of
// a look-steered ground vehicle, where the wheel can be taken.
enum class VehicleCameraSeatRead { OnFoot, Seated, Unknown };
VehicleCameraSeatRead Halo3VehicleCameraSeat(VehicleCameraSeatKey& key, const char*& name, uint8_t& seatBits,
                                             bool& seatView, bool& wheelSeat, uint32_t& definitionIndex)
{
    if (!Halo3VehicleFpActive())
        return VehicleCameraSeatRead::OnFoot;
    Halo3SeatSnapshot seat;
    if (!Halo3ReadSeatSnapshot(seat))
        return VehicleCameraSeatRead::Unknown;
    const auto id = static_cast<Halo3VehicleId>(seat.identity);
    const bool turret = id == Halo3VehicleId::StationaryTurret;
    key = VehicleCameraSeatKey{g_halo3RuntimeGeneration.load(std::memory_order_relaxed), seat.parentHandle,
                               seat.seatIndex, seat.identity, seat.mounted};
    // A gunner on a vehicle's own gun is named by the vehicle that carries it; a turret the player walks up to
    // is driven and fired from its one seat.
    // A vehicle the mod does not know is named by its tag ("sparrowhawk"), or while the map's tag names are not
    // read by its tags' fingerprint ("custom_..."), so each custom vehicle has its own entry match, seat memory and
    // note. Cold worker only: the name lives in this buffer.
    static char customName[kHalo3VehicleNameCapacity];
    name = turret ? "turret" : Halo3SeatVehicleName(seat, customName);
    // A vehicle the mod cannot name has its driver's seat named by the seat's own tag flag (Halo3SeatRoleOf), so
    // its pilot reads as a driver, not as a passenger.
    const Halo3SeatRole role = Halo3SeatRoleOf(id, static_cast<Halo3VehicleId>(seat.controlClass), seat.seatIndex,
                                               seat.mounted, Halo3OccupiedSeatFlags());
    const bool driver = turret || role.driver;
    seatBits = vehicle_camera_file::SeatBits(driver, turret || seat.mounted);
    seatView = Halo3FindSeatPoint(id, seat.seatIndex, seat.mounted) != nullptr;
    wheelSeat = Halo3SeatRoleAuthorsSteering(role, true) && Halo3SeatRoleUsesWheel(role);
    definitionIndex = seat.definitionIndex;
    return VehicleCameraSeatRead::Seated;
}

// What a selected camera asks for that Halo 3 does not do yet, for the log; empty when it asks for nothing of
// the kind.
std::string Halo3VehicleCameraNotApplied(const vehicle_camera_select::ActiveCamera& camera)
{
    if (!camera.valid || !camera.chase)
        return {};
    std::string list;
    const auto add = [&list](const char* what) {
        if (!list.empty())
            list += "; ";
        list += what;
    };
    if (camera.rotRoll)
        add("rotationTracking roll (the horizon is never rolled)");
    if (camera.rotPitch && !camera.rotYaw)
        add("rotationTracking pitch without yaw");
    if (camera.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::Marker))
        add("an origin naming a marker (the vehicle's origin is used)");
    return list;
}

// ---- The cold worker --------------------------------------------------------------------------------------

// THE SEAT MEMORY ON DISK (Selector::MemoryText): halomccvr_vehicle_cameras_last.txt beside the camera file,
// the player's own, written by the mod and never shipped. Replaced whole through a temporary file, so a reader
// never finds half of one.
inline constexpr wchar_t kVehicleCameraMemoryFile[] = L"halomccvr_vehicle_cameras_last.txt";
// THE PLAYER'S LAG TUNING (vehicle_camera_tuning.h), from F1 > Vehicles: a file of its own beside the camera file.
inline constexpr wchar_t kVehicleCameraTuningFile[] = L"halomccvr_vehicle_cameras_tuning.txt";
inline constexpr uint64_t kVehicleCameraTuningSaveMs = 1000;
// The menu's lag sliders. Render thread to the cold worker: an edit (1 = set the camera in use to these values,
// 2 = back to the camera file's), counted so the worker takes each once. Cold worker to the menu: the camera in
// use's lag as it applies, and whether it is a chase camera and tuned.
std::atomic<uint32_t> g_vehicleCameraLagEditSerial{0};
std::atomic<uint32_t> g_vehicleCameraLagEditAction{0};
std::atomic<float> g_vehicleCameraLagEditSeconds{0.0f};
std::atomic<float> g_vehicleCameraLagEditDamping{1.0f};
std::atomic<float> g_vehicleCameraLagShownSeconds{0.0f};
std::atomic<float> g_vehicleCameraLagShownDamping{1.0f};
std::atomic<uint32_t> g_vehicleCameraLagShownState{0}; // bit 0: a chase camera is in use; bit 1: it is tuned
inline constexpr uint64_t kVehicleCameraMemorySaveMs = 2000;

bool VehicleCameraReadText(const std::wstring& path, std::string& out)
{
    out.clear();
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
        return false;
    char block[4096];
    size_t got = 0;
    while ((got = fread(block, 1, sizeof(block), file)) > 0 && out.size() < (1u << 20))
        out.append(block, got);
    fclose(file);
    return true;
}

bool VehicleCameraWriteText(const std::wstring& path, const std::string& text)
{
    const std::wstring temporary = path + L".tmp";
    FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0 || !file)
        return false;
    const bool whole = fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = fclose(file) == 0;
    if (!whole || !closed || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

// The parts of each vehicle sat in, and the regions a camera hides (its "hideRegions").
#include "halo3_vehicle_parts.inl"

struct VehicleCameraRuntime;
void VehicleCameraAutoAdd(VehicleCameraRuntime& r, const char* gameKey, const char* name, uint8_t seatBits);

struct VehicleCameraRuntime
{
    VehicleCameraRuntime(std::wstring path, std::wstring memory, std::wstring tuningFile)
        : store(std::move(path)), memoryPath(std::move(memory)), tuningPath(std::move(tuningFile))
    {
    }
    vehicle_camera_store::Store store;
    vehicle_camera_select::Selector selector;
    std::wstring memoryPath;
    vehicle_camera_tuning::Overlay tuning;
    std::wstring tuningPath;
    uint64_t nextTuningSaveMs = 0;
    uint32_t tuningEditSeen = 0;
    bool tuningFailureLogged = false;
    uint64_t nextMemorySaveMs = 0;
    bool memoryFailureLogged = false;
    bool wheelSeat = false;
    uint8_t seatBits = 0;
    uint64_t nextFilePollMs = 0;
    // The seats (game, vehicle, seat bits) the camera file has been offered entries for this session: each once.
    std::vector<std::string> autoAddTried;
    VehicleCameraSeatKey seat{};
    vehicle_camera_select::ActiveCamera publishedCamera{};
    VehicleCameraSeatKey publishedSeat{};
    std::string loggedLine;
    std::string readout;
    int frameSourceLogged = -1; // 0 = the drawn root node, 1 = the simulation's frame
    bool standInLogged = false;
    bool noTraceLogged = false;
    bool markerNoTraceLogged = false;
    float restLoggedWu = 0.0f;
    float restSeenWu = 0.0f;
    uint32_t heldFramesStint = 0;
    uint32_t restKeptStint = 0;
    uint32_t pivotStepCmStint = 0;
    // The lag follower in this seat: its counts as last read, how often it started over, and how far it trailed.
    uint32_t lagJumpsSeen = 0;
    uint32_t lagRunawaysSeen = 0;
    uint32_t lagLimitedSeen = 0;
    uint32_t lagLimitedStint = 0;
    uint32_t lagJumpsStint = 0;
    uint32_t lagRunawaysStint = 0;
    uint32_t lagTrailCmStint = 0;
    // THE BOOM, as the cold worker last published it.
    struct Boom
    {
        bool published = false;
        VehicleCameraSeatKey seat{};
        int camera = -1;
        uint32_t definitionIndex = 0xFFFFFFFFu;
        uint32_t generation = 0; // the game side's (halo3_vehicle_boom.inl)
        float fileDegrees = vehicle_camera_file::kBoomPitchUnset;
        uint32_t serial = 0, startSerial = 0;
        float start = 0.0f, home = 0.0f;
        vehicle_boom::Shape shape{};
        Halo3BoomSeatFacts facts{};
        bool piloting = false;
        std::string seatName;
        // The log's check of the track against the game's own camera.
        uint64_t publishedMs = 0;
        bool checkLogged = false;
        uint32_t checkSamples = 0;
        float bestApart = 1.0e9f, bestApartSecond = 1.0e9f, bestPitch = 0.0f, bestStood = 0.0f, bestTrack = 0.0f;
    } boom;
    int boomInversionLogged = -100;
};
inline constexpr uint64_t kVehicleCameraFilePollMs = 2000;

// ---- the boom on the cold worker ----

// The log's check of the track: where the game's own camera stood from its pivot (world units) and the way it
// looked, against where the track puts it at that look pitch. Centimetres apart; false when there is nothing to
// compare.
bool VehicleBoomTrackApart(const vehicle_boom::Track& track, const float stood[3], const float looks[3], float& apart,
                           float& pitch, float& stoodCm, float& trackCm)
{
    if (!vehicle_boom::Usable(track))
        return false;
    float heading[2] = {looks[0], looks[1]};
    const float level = std::sqrt(heading[0] * heading[0] + heading[1] * heading[1]);
    if (!std::isfinite(level) || level < 0.05f)
        return false;
    heading[0] /= level;
    heading[1] /= level;
    pitch = std::asin(looks[2] < -1.0f ? -1.0f : (looks[2] > 1.0f ? 1.0f : looks[2]));
    const float measured[3] = {stood[0] * heading[0] + stood[1] * heading[1],
                               -stood[0] * heading[1] + stood[1] * heading[0], stood[2]};
    float place[3];
    if (!vehicle_boom::TrackOffset(track, pitch, place))
        return false;
    const float d[3] = {measured[0] - place[0], measured[1] - place[1], measured[2] - place[2]};
    apart = std::sqrt(vehicle_camera_frame::Dot(d, d)) / vehicle_camera_frame::kWorldUnitsPerCm;
    stoodCm = std::sqrt(vehicle_camera_frame::Dot(measured, measured)) / vehicle_camera_frame::kWorldUnitsPerCm;
    trackCm = std::sqrt(vehicle_camera_frame::Dot(place, place)) / vehicle_camera_frame::kWorldUnitsPerCm;
    return std::isfinite(apart);
}

// A VEHICLE OF ITS OWN IN THE FILE (vehicle_camera_autoadd.h): a seat riding on the file's "default" entry has
// entries for its vehicle added to the file, once per game, vehicle and seat a session (a file that cannot take
// them is tried once). The store reads the file again on the next poll, which is made at once.
void VehicleCameraAutoAdd(VehicleCameraRuntime& r, const char* gameKey, const char* name, uint8_t seatBits)
{
    if (!g_config.vehicle_camera_autoadd || !r.store.FromFile() || !name || !name[0])
        return;
    const std::string seatKey = std::string(gameKey) + '\t' + name + '\t' + std::to_string(seatBits);
    for (const std::string& tried : r.autoAddTried)
        if (tried == seatKey)
            return;
    r.autoAddTried.push_back(seatKey);
    static const vehicle_camera_file::Table builtIn = vehicle_camera_file::DefaultTable();
    const vehicle_camera_autoadd::Addition addition =
        vehicle_camera_autoadd::For(r.store.Current(), builtIn, gameKey, name, seatBits);
    if (addition.entries.empty())
        return;
    std::string names;
    for (const vehicle_camera_file::Vehicle& entry : addition.entries)
        names += (names.empty() ? "\"" : ", \"") + entry.name + "\"";
    std::string why;
    if (!r.store.Add(addition, gameKey, name, seatBits, why))
    {
        LOG("Vehicle cameras: %s rides on the camera file's \"default\" entry, and %s could not be added to %s: %s",
            name, names.c_str(), vehicle_camera_store::kFileNameText, why.c_str());
        return;
    }
    LOG("Vehicle cameras: %s rode on the camera file's \"default\" entry, so %s %s added to %s (section \"%s\", %s); "
        "the rest of the file is as it was, and the file as it first was is kept as %s%s",
        name, names.c_str(), addition.entries.size() == 1 ? "was" : "were", vehicle_camera_store::kFileNameText,
        addition.sectionKey.c_str(),
        addition.fromBuiltIn ? "the built-in entries for it" : "the default entry's cameras under its own name",
        vehicle_camera_store::kFileNameText, vehicle_camera_store::kBackupSuffixText);
    r.nextFilePollMs = 0;
}

void VehicleBoomCheckLog(VehicleCameraRuntime& r)
{
    VehicleCameraRuntime::Boom& b = r.boom;
    if (b.checkLogged || !b.checkSamples || !b.shape.track)
        return;
    b.checkLogged = true;
    char second[96] = "";
    if (b.bestApartSecond < 1.0e8f)
        std::snprintf(second, sizeof(second), "; by the seat's second track, %.1f cm",
                      static_cast<double>(b.bestApartSecond));
    LOG("Vehicle cameras (Halo 3): the boom's track checked against the game's own camera in %s: of %u samples the "
        "closest stood %.0f cm from its pivot looking %.1f deg, where the track puts it %.0f cm out, %.1f cm apart%s",
        b.seatName.c_str(), b.checkSamples, static_cast<double>(b.bestStood),
        static_cast<double>(b.bestPitch / vehicle_boom::kRadiansPerDegree), static_cast<double>(b.bestTrack),
        static_cast<double>(b.bestApart), second);
}

// Cold worker, every poll. `wanted`: a camera of origin "thirdperson" is in use in a Halo 3 seat; `seated`: this
// poll's seat could be read (an unreadable poll keeps the boom as it is).
void VehicleBoomColdTick(VehicleCameraRuntime& r, const vehicle_camera_select::ActiveCamera& active, bool wanted,
                         bool seated, uint32_t definitionIndex, const char* name, uint64_t nowMs)
{
    VehicleCameraRuntime::Boom& b = r.boom;
    const auto publish = [&]() {
        VehicleBoomPublished& published = g_vehicleBoomPublished;
        published.seq.fetch_add(1, std::memory_order_acq_rel);
        published.seat = b.seat;
        published.camera = b.camera;
        published.startSerial = b.startSerial;
        published.start = b.start;
        published.home = b.home;
        memcpy(&published.shape, &b.shape, sizeof(published.shape));
        published.serial.store(++b.serial, std::memory_order_relaxed);
        published.seq.fetch_add(1, std::memory_order_release);
    };
    if (!wanted)
    {
        if (b.published)
        {
            VehicleBoomCheckLog(r);
            b.published = false;
            b.shape = vehicle_boom::Shape{};
            publish();
            g_vehicleBoomShown.store(0, std::memory_order_release);
        }
        return;
    }
    if (!seated)
        return;

    const uint32_t generation = g_halo3VehicleBoom.generation.load(std::memory_order_acquire);
    const bool newCamera = !b.published || !(b.seat == r.seat) || b.camera != active.camera ||
                           b.definitionIndex != definitionIndex || b.generation != generation;
    const bool newStart = newCamera || b.fileDegrees != active.boomPitchDeg;
    if (newStart)
    {
        if (newCamera)
        {
            VehicleBoomCheckLog(r);
            const uint32_t serial = b.serial, startSerial = b.startSerial;
            b = VehicleCameraRuntime::Boom{};
            b.serial = serial;
            b.startSerial = startSerial;
            b.seat = r.seat;
            b.camera = active.camera;
            b.definitionIndex = definitionIndex;
            b.generation = generation;
            b.seatName = name && name[0] ? name : "this seat";
            const bool read = Halo3VehicleBoomReadSeat(definitionIndex, r.seat.seatIndex, b.facts);
            const vehicle_boom::Range range = Halo3VehicleBoomRange(b.facts);
            if (!read || !vehicle_boom::BuildTrackShape(b.facts.track, range, b.shape))
                vehicle_boom::BuildOrbitShape(range, b.shape);
            b.piloting = Halo3VehicleBoomPiloting(b.facts);
        }
        b.fileDegrees = active.boomPitchDeg;
        b.home = vehicle_boom::PitchForElevation(b.shape, vehicle_boom::DefaultElevation(b.shape, b.fileDegrees));
        float kept = 0.0f;
        const bool remembered = r.selector.BoomPitch(kept);
        // A new camera starts where the player left it, else at its own start. A camera file edited while seated
        // moves the start only of a boom the player has not moved.
        if (newCamera || !remembered)
        {
            b.start = remembered ? vehicle_boom::PitchForElevation(b.shape, kept * vehicle_boom::kRadiansPerDegree) : b.home;
            ++b.startSerial;
        }
        b.published = true;
        b.publishedMs = nowMs;
        publish();

        const float deg = 1.0f / vehicle_boom::kRadiansPerDegree;
        const float startDeg = vehicle_boom::ElevationAt(b.shape, b.start) * deg;
        const float highDeg = vehicle_boom::ElevationAt(b.shape, b.shape.range.min) * deg;
        const float lowDeg = vehicle_boom::ElevationAt(b.shape, b.shape.range.max) * deg;
        float level[3];
        vehicle_boom::OffsetAt(b.shape, 0.0f, level);
        const float levelCm = std::sqrt(vehicle_camera_frame::Dot(level, level)) / vehicle_camera_frame::kWorldUnitsPerCm;
        const char* from = remembered ? "your angle, kept" : (vehicle_camera_file::BoomPitchSet(b.fileDegrees)
                                                                  ? "the camera file's boomPitch"
                                                                  : "the game's level view, a little higher");
        if (b.shape.track)
            LOG("Vehicle cameras (Halo 3): the boom in %s stands where the game's own third-person camera would at a "
                "look pitch of its own: the seat's camera track (%d points%s) across the flat game's look range %.1f to "
                "%.1f deg, from %.1f deg above level looking down to %.1f deg looking up; looking level the game's "
                "camera stands %.1f deg above level, %.0f cm out. It starts at %.1f deg (%s)",
                b.seatName.c_str(), b.facts.track.count, b.facts.defaultTrack ? ", the game's default" : "",
                static_cast<double>(b.shape.range.min * deg), static_cast<double>(b.shape.range.max * deg),
                static_cast<double>(highDeg), static_cast<double>(lowDeg),
                static_cast<double>(vehicle_boom::ElevationAt(b.shape, 0.0f) * deg),
                static_cast<double>(levelCm), static_cast<double>(startDeg), from);
        else
        {
            const char* why = !Halo3VehicleBoomBound() || !g_halo3VehicleBoom.seatCamera ? "the game's seat camera is not bound"
                            : !b.facts.read                                             ? "the seat's tags could not be read"
                            : !g_halo3VehicleBoom.track                                 ? "the game's camera track is not bound"
                            : b.facts.points < vehicle_boom::kMinTrackPoints            ? "the seat camera has no track of four points"
                            : b.facts.points > vehicle_boom::kMaxTrackPoints            ? "its track has more points than the mod reads"
                                                                                        : "its track did not check out";
            LOG("Vehicle cameras (Halo 3): the boom in %s orbits the game's pivot at the game's own distance (%s); look "
                "range %.1f to %.1f deg, so from %.1f to %.1f deg above level. It starts at %.1f deg (%s)",
                b.seatName.c_str(), why, static_cast<double>(b.shape.range.min * deg),
                static_cast<double>(b.shape.range.max * deg), static_cast<double>(lowDeg), static_cast<double>(highDeg),
                static_cast<double>(startDeg), from);
        }
    }

    // THE INVERSION, every poll: the game's setting can change in its pause menu.
    bool gameInverts = false;
    const bool known = Halo3VehicleBoomGameInverts(b.piloting, gameInverts);
    const int setting = g_config.vehicle_boom_invert;
    const bool inverted = vehicle_boom::Inverted(setting, known, gameInverts);
    g_vehicleBoomInverted.store(inverted, std::memory_order_relaxed);
    const int said = setting == 0 || setting == 1 ? 10 + setting : (known ? (gameInverts ? 1 : 0) + (b.piloting ? 2 : 0) : -1);
    if (said != r.boomInversionLogged)
    {
        r.boomInversionLogged = said;
        if (setting == 0 || setting == 1)
            LOG("Vehicle cameras (Halo 3): the boom's stick is %s (vehicle_boom_invert = %d)",
                setting ? "inverted" : "not inverted", setting);
        else if (known)
            LOG("Vehicle cameras (Halo 3): the boom's stick follows the game's %s inversion, which is %s",
                b.piloting && g_halo3VehicleBoom.pilot ? "flight" : "look", gameInverts ? "on" : "off");
        else
            LOG("Vehicle cameras (Halo 3): the game's look inversion could not be read, so the boom's stick is not "
                "inverted (vehicle_boom_invert = 1 inverts it)");
    }

    // THE PLAYER'S ANGLE, kept for this seat and camera, and the track's check.
    VehicleBoomCurrent current{};
    bool fresh = false;
    for (int attempt = 0; attempt < 3 && !fresh; ++attempt)
    {
        const uint32_t before = g_vehicleBoomCurrent.seq.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        current.serial = g_vehicleBoomCurrent.serial;
        current.pitch = g_vehicleBoomCurrent.pitch;
        current.sampled = g_vehicleBoomCurrent.sampled;
        current.ms = g_vehicleBoomCurrent.ms;
        memcpy(current.stood, g_vehicleBoomCurrent.stood, sizeof(current.stood));
        memcpy(current.looks, g_vehicleBoomCurrent.looks, sizeof(current.looks));
        std::atomic_thread_fence(std::memory_order_acquire);
        fresh = g_vehicleBoomCurrent.seq.load(std::memory_order_relaxed) == before;
    }
    float pitch = b.start;
    if (fresh && current.serial == b.serial && std::isfinite(current.pitch))
    {
        pitch = vehicle_boom::Hold(current.pitch, b.shape.range);
        const bool atHome = std::fabs(pitch - b.home) < 0.25f * vehicle_boom::kRadiansPerDegree;
        r.selector.SetBoomPitch(vehicle_boom::ElevationAt(b.shape, pitch) / vehicle_boom::kRadiansPerDegree, atHome);
        if (current.sampled && b.shape.track && nowMs <= current.ms + 200)
        {
            float apart = 0.0f, lookPitch = 0.0f, stoodCm = 0.0f, trackCm = 0.0f;
            if (VehicleBoomTrackApart(b.facts.track, current.stood, current.looks, apart, lookPitch, stoodCm, trackCm))
            {
                ++b.checkSamples;
                if (apart < b.bestApart)
                {
                    b.bestApart = apart;
                    b.bestPitch = lookPitch;
                    b.bestStood = stoodCm;
                    b.bestTrack = trackCm;
                }
                float second = 0.0f;
                if (VehicleBoomTrackApart(b.facts.second, current.stood, current.looks, second, lookPitch, stoodCm, trackCm) &&
                    second < b.bestApartSecond)
                    b.bestApartSecond = second;
            }
        }
    }
    if (!b.checkLogged && nowMs - b.publishedMs >= 3000)
        VehicleBoomCheckLog(r);
    const float deg = 1.0f / vehicle_boom::kRadiansPerDegree;
    g_vehicleBoomShownDeg.store(vehicle_boom::ElevationAt(b.shape, pitch) * deg, std::memory_order_relaxed);
    g_vehicleBoomShownLowDeg.store(vehicle_boom::ElevationAt(b.shape, b.shape.range.max) * deg, std::memory_order_relaxed);
    g_vehicleBoomShownHighDeg.store(vehicle_boom::ElevationAt(b.shape, b.shape.range.min) * deg, std::memory_order_relaxed);
    g_vehicleBoomShown.store(1u | (b.shape.track ? 2u : 0u) | (inverted ? 4u : 0u), std::memory_order_release);
}

// WaitThread, every poll. Everything slow about the feature happens here.
void VehicleCameras_ColdTick(uint64_t nowMs)
{
    static std::unique_ptr<VehicleCameraRuntime> runtime;
    static bool wasOn = false;
    Halo3VehicleNoteTick();
    if (!g_config.vehicle_cameras)
    {
        if (wasOn)
        {
            wasOn = false;
            VehicleCameraPublish({}, {});
            VehicleCameraSetReadout({});
            g_vehicleCameraStepCamera.store(0, std::memory_order_relaxed);
            g_vehicleCameraStepMode.store(0, std::memory_order_relaxed);
            g_vehicleControlStep.store(0, std::memory_order_relaxed);
            g_vehicleControlEffective.store(-1, std::memory_order_release);
            g_vehicleSeatButtons.store(0, std::memory_order_release);
            if (runtime)
            {
                runtime->selector.Update("", false, "", 0);
                VehicleBoomColdTick(*runtime, {}, false, false, 0xFFFFFFFFu, "", nowMs);
                runtime->publishedCamera = {};
                runtime->publishedSeat = {};
                runtime->loggedLine.clear();
                runtime->frameSourceLogged = -1;
                runtime->standInLogged = false;
                runtime->noTraceLogged = false;
                runtime->markerNoTraceLogged = false;
            }
            g_vehicleCameraHeadlessBodyWanted.store(false, std::memory_order_release);
            g_halo3VehicleHideWanted.store(false, std::memory_order_release);
            g_halo3VehiclePartsWanted.store(false, std::memory_order_release);
            g_halo3VehicleFadeWanted.store(false, std::memory_order_release);
            Halo3VehicleParts_ColdTick({}, {}, false, 0xFFFFFFFFu, nowMs);
            LOG("Vehicle cameras: off (vehicle_cameras = 0); every seat keeps the view it has without them");
        }
        return;
    }
    if (!runtime)
    {
        std::wstring path = LogDirectory();
        if (path.empty())
            return;
        std::wstring memory = path + kVehicleCameraMemoryFile;
        std::wstring tuningFile = path + kVehicleCameraTuningFile;
        path += vehicle_camera_store::kFileName;
        runtime = std::make_unique<VehicleCameraRuntime>(std::move(path), std::move(memory), std::move(tuningFile));
        std::string kept;
        if (VehicleCameraReadText(runtime->memoryPath, kept))
        {
            const int seats = runtime->selector.LoadMemory(kept);
            LOG("Vehicle cameras: your last camera, tethering mode and controls kept for %d seat(s), and your "
                "third-person boom for %d camera(s), from halomccvr_vehicle_cameras_last.txt",
                seats, runtime->selector.BoomKept());
        }
        std::string tuned;
        if (VehicleCameraReadText(runtime->tuningPath, tuned))
            LOG("Vehicle cameras: your lag tuning for %d camera(s), from halomccvr_vehicle_cameras_tuning.txt",
                runtime->tuning.Load(tuned));
        runtime->tuningEditSeen = g_vehicleCameraLagEditSerial.load(std::memory_order_acquire);
    }
    VehicleCameraRuntime& r = *runtime;
    if (!wasOn)
    {
        wasOn = true;
        r.nextFilePollMs = 0;
        LOG("Vehicle cameras: on. The cameras of each seat are read from %s in the mod's folder; F1 > Vehicles "
            "steps through them. Halo 3 seats for now, and only while vehicle_first_person is on",
            vehicle_camera_store::kFileNameText);
    }

    if (nowMs >= r.nextFilePollMs)
    {
        r.nextFilePollMs = nowMs + kVehicleCameraFilePollMs;
        const vehicle_camera_store::PollResult polled = r.store.Poll();
        for (const std::string& note : polled.notes)
            LOG("%s", note.c_str());
        if (polled.changed)
        {
            r.selector.SetTable(r.store.Current());
            g_vehicleCameraHeadlessBodyWanted.store(vehicle_camera_file::AnyShowsHeadlessBody(r.store.Current(), "halo3"),
                                                 std::memory_order_release);
            g_halo3VehicleHideWanted.store(vehicle_camera_file::AnyHidesRegions(r.store.Current(), "halo3"),
                                           std::memory_order_release);
            g_halo3VehiclePartsWanted.store(vehicle_camera_file::AnyHidesParts(r.store.Current(), "halo3"),
                                            std::memory_order_release);
            g_halo3VehicleFadeWanted.store(vehicle_camera_file::AnyFadesParts(r.store.Current(), "halo3"),
                                           std::memory_order_release);
        }
    }

    VehicleCameraSeatRead read = VehicleCameraSeatRead::OnFoot;
    VehicleCameraSeatKey key{};
    const char* name = "";
    uint8_t seatBits = 0;
    const char* gameKey = "";
    bool seatView = true, wheelSeat = false;
    uint32_t definitionIndex = 0xFFFFFFFFu;
    const bool halo3 = TitleAdapter_GetActiveTitle() == GameTitle::Halo3;
    if (halo3)
    {
        gameKey = "halo3";
        read = Halo3VehicleCameraSeat(key, name, seatBits, seatView, wheelSeat, definitionIndex);
    }
    if (read == VehicleCameraSeatRead::Seated)
    {
        r.selector.Update(gameKey, true, name, seatBits, seatView);
        r.seat = key;
        r.seatBits = seatBits;
        r.wheelSeat = wheelSeat;
        VehicleCameraAutoAdd(r, gameKey, name, seatBits);
    }
    else if (read == VehicleCameraSeatRead::OnFoot)
    {
        r.selector.Update(gameKey, false, "", 0);
        r.seat = {};
        r.seatBits = 0;
        r.wheelSeat = false;
    }

    // A step asked for on foot is dropped with the rest: the selector has nothing to step.
    const int stepCamera = g_vehicleCameraStepCamera.exchange(0, std::memory_order_relaxed);
    const int stepMode = g_vehicleCameraStepMode.exchange(0, std::memory_order_relaxed);
    if (stepCamera)
        r.selector.StepCamera(stepCamera);
    if (stepMode)
        r.selector.StepMode(stepMode);

    // HOW THE SEAT IS CONTROLLED: the player's choice for it where he made one (the left stick click, kept per
    // vehicle and seat), else the setting.
    const bool inSeat = r.selector.Active().valid;
    const int base = static_cast<int>(vehicle_control::FromConfig(g_config.vehicle_control));
    int control = base;
    if (inSeat)
    {
        const int choice = r.selector.ControlChoice(base);
        if (choice >= 0)
            control = choice;
        if (g_vehicleControlStep.exchange(0, std::memory_order_relaxed) != 0)
        {
            const vehicle_control::Mode next =
                vehicle_control::Next(vehicle_control::FromConfig(control), r.wheelSeat);
            control = static_cast<int>(next);
            r.selector.SetControlChoice(control, base);
            LOG("Vehicle cameras: %s is now controlled by %s (left stick click)", name[0] ? name : "this seat",
                vehicle_control::Name(next));
        }
    }
    else
    {
        g_vehicleControlStep.store(0, std::memory_order_relaxed);
    }
    g_vehicleControlEffective.store(inSeat ? control : -1, std::memory_order_release);
    g_vehicleSeatButtons.store(
        inSeat && halo3 ? kVehicleSeatButtonsLive |
                              ((r.seatBits & vehicle_camera_file::kSeatPassenger) ? kVehicleSeatPassenger : 0u)
                        : 0u,
        std::memory_order_release);

    if (r.selector.MemoryDirty() && nowMs >= r.nextMemorySaveMs)
    {
        r.nextMemorySaveMs = nowMs + kVehicleCameraMemorySaveMs;
        if (VehicleCameraWriteText(r.memoryPath, r.selector.MemoryText()))
        {
            r.selector.MemorySaved();
            r.memoryFailureLogged = false;
        }
        else if (!r.memoryFailureLogged)
        {
            r.memoryFailureLogged = true;
            LOG("Vehicle cameras: halomccvr_vehicle_cameras_last.txt could not be written (%lu); your choices are "
                "kept for this session and tried again",
                GetLastError());
        }
    }

    // THE PLAYER'S LAG TUNING: an edit from the menu sliders applies to the camera in use; the camera published to
    // the camera thread carries the tuned lag in place of the file's.
    const std::string tuningKey = r.selector.Active().chase ? r.selector.TuningKey(gameKey) : std::string();
    const uint32_t tuningEdit = g_vehicleCameraLagEditSerial.load(std::memory_order_acquire);
    if (tuningEdit != r.tuningEditSeen)
    {
        r.tuningEditSeen = tuningEdit;
        if (!tuningKey.empty())
        {
            if (g_vehicleCameraLagEditAction.load(std::memory_order_relaxed) == 2)
            {
                if (r.tuning.Clear(tuningKey))
                    LOG("Vehicle cameras: %s is back to the camera file's lag", r.selector.Describe().c_str());
            }
            else
            {
                r.tuning.Set(tuningKey, vehicle_camera_tuning::Tuning{
                                            g_vehicleCameraLagEditSeconds.load(std::memory_order_relaxed),
                                            g_vehicleCameraLagEditDamping.load(std::memory_order_relaxed)});
            }
        }
    }
    if (r.tuning.Dirty() && nowMs >= r.nextTuningSaveMs)
    {
        r.nextTuningSaveMs = nowMs + kVehicleCameraTuningSaveMs;
        if (VehicleCameraWriteText(r.tuningPath, r.tuning.Text()))
        {
            r.tuning.Saved();
            r.tuningFailureLogged = false;
        }
        else if (!r.tuningFailureLogged)
        {
            r.tuningFailureLogged = true;
            LOG("Vehicle cameras: halomccvr_vehicle_cameras_tuning.txt could not be written (%lu); your lag tuning "
                "is kept for this session and tried again",
                GetLastError());
        }
    }
    vehicle_camera_select::ActiveCamera active = r.selector.Active();
    vehicle_camera_tuning::Tuning tuned;
    const bool isTuned = !tuningKey.empty() && r.tuning.Find(tuningKey, tuned);
    if (isTuned)
    {
        active.lagSeconds = tuned.lag;
        active.lagDamping = tuned.damping;
    }
    g_vehicleCameraLagShownSeconds.store(active.lagSeconds, std::memory_order_relaxed);
    g_vehicleCameraLagShownDamping.store(active.lagDamping, std::memory_order_relaxed);
    g_vehicleCameraLagShownState.store((active.valid && active.chase ? 1u : 0u) | (isTuned ? 2u : 0u),
                                       std::memory_order_release);
    if (!(active == r.publishedCamera) || !(r.seat == r.publishedSeat))
    {
        VehicleCameraPublish(active, r.seat);
        r.publishedCamera = active;
        r.publishedSeat = r.seat;
    }
    // An unknown read (the seat snapshot a moment stale) keeps what was published.
    if (halo3 && read != VehicleCameraSeatRead::Unknown)
        Halo3VehicleParts_ColdTick(active, r.seat, read == VehicleCameraSeatRead::Seated, definitionIndex, nowMs);
    VehicleBoomColdTick(r, active,
                        halo3 && read != VehicleCameraSeatRead::OnFoot && active.valid && active.chase &&
                            active.origin == static_cast<uint8_t>(vehicle_camera_file::Origin::ThirdPerson),
                        read == VehicleCameraSeatRead::Seated, definitionIndex, name, nowMs);

    const bool onFoot = !active.valid && r.selector.StandDownReason() == vehicle_camera_select::kOnFoot;
    std::string line = r.selector.Describe();
    if (line.empty())
    {
        // Not in a seat this feature reaches: say which of the reasons it is.
        if (!onFoot)
            line = "no camera: " + r.selector.StandDownReason();
        else if (!halo3)
            line = "no camera: vehicle cameras are in Halo 3 only for now";
        else if (!g_config.vehicle_first_person)
            line = "no camera: first-person vehicles are off, so a seat keeps the game's own camera";
        else
            line = "no camera: on foot";
    }
    else
    {
        if (isTuned)
        {
            char tunedText[96];
            std::snprintf(tunedText, sizeof(tunedText), "; your tuning: lag %.2f s, damping %.2f",
                          static_cast<double>(tuned.lag), static_cast<double>(tuned.damping));
            line += tunedText;
        }
        line += std::string("; controls: ") + vehicle_control::Name(vehicle_control::FromConfig(control));
        if (const std::string missing = Halo3VehicleCameraNotApplied(active); !missing.empty())
            line += ". Not applied in Halo 3 yet: " + missing;
    }
    if (line != r.readout)
    {
        r.readout = line;
        VehicleCameraSetReadout(line);
    }
    // Getting out is not news; a seat's camera, a step, and a seat the file gives no camera are.
    if (!onFoot && line != r.loggedLine)
    {
        if (active.valid && g_config.vehicle_camera_note)
        {
            std::wstring note = r.selector.Note();
            note += L" \u00B7 ";
            // The control's name without its explanation: "Motion (your hand)" is "Motion" here.
            for (const char* c = vehicle_control::Name(vehicle_control::FromConfig(control)); *c && *c != '('; ++c)
                note.push_back(static_cast<wchar_t>(*c));
            while (!note.empty() && note.back() == L' ')
                note.pop_back();
            Halo3VehicleNoteShow(note, kVehicleCameraNoteSeconds);
        }
        r.loggedLine = line;
        LOG("Vehicle cameras: %s", line.c_str());
    }
    {
        const uint32_t jumps = g_halo3VehicleCameraLagJumps.load(std::memory_order_relaxed);
        const uint32_t runaways = g_halo3VehicleCameraLagRunaways.load(std::memory_order_relaxed);
        r.lagJumpsStint += jumps - r.lagJumpsSeen;
        r.lagRunawaysStint += runaways - r.lagRunawaysSeen;
        r.lagJumpsSeen = jumps;
        r.lagRunawaysSeen = runaways;
        const uint32_t limited = g_halo3VehicleCameraLagLimited.load(std::memory_order_relaxed);
        r.lagLimitedStint += limited - r.lagLimitedSeen;
        r.lagLimitedSeen = limited;
        const uint32_t trail = g_halo3VehicleCameraLagTrailCm.exchange(0, std::memory_order_relaxed);
        r.lagTrailCmStint = trail > r.lagTrailCmStint ? trail : r.lagTrailCmStint;
    }
    if (onFoot)
    {
        // A lagged camera trails its place in a turn or a speed change, and starts over only on a leap of its place
        // (a respawn, a seat change) or a runaway: never for going fast.
        if (r.lagTrailCmStint || r.lagJumpsStint || r.lagRunawaysStint || r.lagLimitedStint)
            LOG("Vehicle cameras (Halo 3): in that seat the lagged camera trailed its place by at most %u cm, was "
                "held at its lagLimit on %u frames, and started over %u time(s) on a leap of its place and %u on a "
                "runaway",
                r.lagTrailCmStint, r.lagLimitedStint, r.lagJumpsStint, r.lagRunawaysStint);
        r.lagJumpsStint = r.lagRunawaysStint = r.lagTrailCmStint = r.lagLimitedStint = 0;
        r.loggedLine.clear();
        r.frameSourceLogged = -1;
        r.standInLogged = false;
        r.noTraceLogged = false;
        r.markerNoTraceLogged = false;
        if (r.heldFramesStint)
            LOG("Vehicle cameras (Halo 3): in that seat the camera was held short of something on %u frames",
                r.heldFramesStint);
        r.heldFramesStint = 0;
        if (r.restKeptStint)
            LOG("Vehicle cameras (Halo 3): in that seat the game's own camera could not be read on %u frames, and the "
                "third-person camera kept its last place on the vehicle through them", r.restKeptStint);
        r.restKeptStint = 0;
        if (r.pivotStepCmStint)
            LOG("Vehicle cameras (Halo 3): in that seat the game's camera pivot moved on the vehicle by up to %u cm "
                "from one frame to the next (the third-person camera carries it on the vehicle as drawn, smoothed "
                "over %.2f s)",
                r.pivotStepCmStint, static_cast<double>(kHalo3RestPivotSmoothSeconds));
        r.pivotStepCmStint = 0;
        r.restLoggedWu = 0.0f;
        r.restSeenWu = 0.0f;
        g_halo3VehicleCameraRestWu.store(0.0f, std::memory_order_relaxed);
    }

    const uint32_t frames = g_halo3VehicleCameraFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t tickFrames = g_halo3VehicleCameraTickFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t standInFrames = g_halo3VehicleCameraStandInFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t collideFrames = g_halo3VehicleCameraCollideFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t noTraceFrames = g_halo3VehicleCameraNoTraceFrames.exchange(0, std::memory_order_relaxed);
    r.heldFramesStint += g_halo3VehicleCameraHeldFrames.exchange(0, std::memory_order_relaxed);
    r.restKeptStint += g_halo3VehicleCameraRestKeptFrames.exchange(0, std::memory_order_relaxed);
    {
        const uint32_t pivotStep = g_halo3VehicleCameraPivotStepCm.exchange(0, std::memory_order_relaxed);
        r.pivotStepCmStint = pivotStep > r.pivotStepCmStint ? pivotStep : r.pivotStepCmStint;
    }
    // The game's own third-person camera's resting distance: said once it has settled in a seat (the same on two
    // polls running; it grows over the first half second as the game's camera eases out), and again whenever it
    // has settled a tenth further out (the distance is the furthest the game's camera has stood).
    const float restWu = g_halo3VehicleCameraRestWu.load(std::memory_order_relaxed);
    const bool restSettled = restWu == r.restSeenWu;
    r.restSeenWu = restWu;
    if (restWu > 0.0f && restSettled && restWu > r.restLoggedWu * 1.1f)
    {
        r.restLoggedWu = restWu;
        const float upWu = g_halo3VehicleCameraRestUpWu.load(std::memory_order_relaxed);
        const float pivotUpWu = g_halo3VehicleCameraRestPivotUpWu.load(std::memory_order_relaxed);
        LOG("Vehicle cameras (Halo 3): the game's own third-person camera rests %.2f world units (%.0f cm) from "
            "its pivot in this seat, %.0f cm of that above the pivot (it looked %.1f deg up), "
            "and the pivot is %.0f cm above the vehicle's origin",
            static_cast<double>(restWu), static_cast<double>(restWu / vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(upWu / vehicle_camera_frame::kWorldUnitsPerCm),
            static_cast<double>(g_halo3VehicleCameraRestPitchDeg.load(std::memory_order_relaxed)),
            static_cast<double>(pivotUpWu / vehicle_camera_frame::kWorldUnitsPerCm));
    }
    if (noTraceFrames > 60 && collideFrames == 0 && !r.noTraceLogged)
    {
        r.noTraceLogged = true;
        LOG("Vehicle cameras (Halo 3): this camera asks to be kept out of walls (\"collide\"), but the game's trace is "
            "not running for this seat, so it goes all the way out");
    }
    const uint32_t markerFrames = g_halo3VehicleAimMarkerFrames.exchange(0, std::memory_order_relaxed);
    const uint32_t markerUntracedFrames = g_halo3VehicleAimMarkerUntracedFrames.exchange(0, std::memory_order_relaxed);
    if (markerFrames > 60 && markerUntracedFrames == markerFrames && !r.markerNoTraceLogged)
    {
        r.markerNoTraceLogged = true;
        LOG("Vehicle cameras (Halo 3): the ring where the vehicle aims is shown, but the game's trace is not running "
            "for this seat, so it is drawn at the last depth measured, else far out along its line");
    }
    if (standInFrames && !r.standInLogged)
    {
        r.standInLogged = true;
        LOG("Vehicle cameras (Halo 3): where the game's own third-person camera rests could not be read for "
            "this seat, so the camera stands %.0f cm behind and %.0f cm above the vehicle's origin instead",
            static_cast<double>(vehicle_camera_place::kStandInBehindCm),
            static_cast<double>(vehicle_camera_place::kStandInAboveCm));
    }
    if (frames)
    {
        const int source = tickFrames * 2 > frames ? 1 : 0;
        if (source != r.frameSourceLogged)
        {
            r.frameSourceLogged = source;
            if (source == 0)
                LOG("Vehicle cameras (Halo 3): the camera is placed from the vehicle's root node as it is drawn");
            else
                LOG("Vehicle cameras (Halo 3): the vehicle's drawn root node did not check out against its own "
                    "frame, so the camera is placed from the simulation's frame; it can shake against the vehicle "
                    "at speed");
        }
    }
}
