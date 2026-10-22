#pragma once
// THE VEHICLE CAMERAS IN HALO 2'S SEATS: what Halo 2's observer (halo2_observer_6dof.cpp, which builds the frame's
// camera) hands the vehicle cameras (game.cpp, halo2_vehicle_cameras.inl) once a frame of a seat, and what it gets
// back. Every call but Game_Halo2VehicleCameraPlacedSeat is on the observer's thread.
#include <cstdint>

struct Halo2VehicleCameraSeatSample
{
    uint32_t generation = 0;
    uint32_t unit = UINT32_MAX, parent = UINT32_MAX, root = UINT32_MAX; // the seated unit, what it sits in, the top
    int16_t seat = -1;
    // The seated object's render model (halo2_vehicle_identity.h; 0 = not one the mod knows), and the one of the
    // vehicle it rides (the root) where it rides one.
    uint64_t identity = 0, carrierIdentity = 0;
    // The seated object's render model's bounds (x, y, z: low, high each) and node count, where they were read: a
    // vehicle the mod does not know is named by them, and its size is half the bounds' diagonal. The carrier's bounds
    // too, where it rides one.
    bool modelRead = false;
    float bounds[6]{};
    unsigned nodes = 0;
    bool carrierModelRead = false;
    float carrierBounds[6]{};
    // Their sizes, world units (halo2_vehicle_camera::BoundsRadius): the seated object's, and the carrier's where it
    // rides one. 0 = not read.
    float radius = 0.0f, carrierRadius = 0.0f;
    // The occupied seat's own flags (seat_converge_logic.h: bit 2 driver, bit 3 gunner), where they were read.
    bool flagsKnown = false;
    uint32_t seatFlags = 0;
    // The way the game's own camera looks this frame, before the view is built on it (the observer's stock pose): along
    // the aim the game holds for the seat.
    float stockForward[3]{};
};

// Once a frame of a seat the seat view runs in, before the view is built: the seat is published for the cold worker.
// True when a chase camera of the camera file is chosen for exactly this seat; `followHull` is then its tethering's
// rotationTracking yaw, which stands in for vehicle_view_follow in that seat (left as it is otherwise).
bool Game_Halo2VehicleCamerasBegin(const Halo2VehicleCameraSeatSample& seat, bool& followHull) noexcept;
// Once the view is built: `eye` (the view's point, in and out) moved by a firstperson camera's "offset", or stood at a
// chase camera's place. `viewYaw` is the view's heading (radians about +Z). True when it moved.
bool Game_Halo2VehicleCamerasPlace(const Halo2VehicleCameraSeatSample& seat, float viewYaw, float eye[3]) noexcept;
// After the player's head is added to the view: `head` (world units, the head's movement off the eye) held inside the
// leash of the chase camera placed this frame. False: no chase camera was placed, or it sets no leash.
bool Game_Halo2VehicleCameraLeash(float head[3]) noexcept;
// Once the head is added: the ring where the vehicle aims, for a chase camera that shows it, from the view the frame is
// drawn from (`eye`, `forward`, `up`: the tracked centre camera) and the head's orientation in the headset's space it
// was built with ([x, y, z, w]). Null for any of them takes the ring down.
void Game_Halo2VehicleAimMarkerStep(const float eye[3], const float forward[3], const float up[3],
                                    const float headQuat[4]) noexcept;
// A frame of the observer with the player on foot: the seat is published as left.
void Game_Halo2VehicleCamerasOnFoot(uint32_t generation) noexcept;
// Any thread: a chase camera of the camera file placed this seat's view within 100 ms. The seat's view is then the
// mod's own as it is with the first-person seat view (the seat drive writes the seat, and a fire's crosshair line is
// not moved to the view's eye, which stands metres from the seat).
bool Game_Halo2VehicleCameraPlacedSeat(uint32_t parent, int16_t seat) noexcept;
