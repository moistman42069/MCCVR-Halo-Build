#pragma once
#include <cstddef>
#include <cstdint>
#include "../common/vehicle_camera_frame_logic.h"

// THE VEHICLE CAMERAS IN HALO CE'S SEATS: the engine side (haloce_vehicle_cameras.cpp). A chase camera of the vehicle
// camera file is placed in the game's own third-person camera: the director's following camera
// (following_camera_update) runs as built, and its command to the observer then stands the camera at the chosen place
// (its pivot moved there, its offset and distance taken off, so the observer neither swings nor pulls it in). Only
// the frame's camera is placed: the simulation's own camera (director_camera_deterministic) is the game's third-person
// one -- in a seat whose own camera is first person, only in solo (native_vehicle_first_person.cpp ChaseSeat), and in a
// shared simulation the game's first-person one. The placement itself is game.cpp's (haloce_vehicle_cameras.inl), with
// the shared camera runtime.
//
// The hook is installed only while the vehicle cameras are on in CE, and retired with the title like every CE feature.

bool HaloCEVehicleCameras_Poll(uintptr_t base,size_t size,uint32_t generation,bool active) noexcept;
// The following-camera hook is installed and live for the active CE generation: a seat whose chosen camera is a chase
// camera can be shown from it (the selector gives such a seat to the game's following camera only then).
bool HaloCEVehicleCameras_FollowingLive() noexcept;
// An object's root node as the game draws it this frame (marker query with an empty name): position, forward, up.
// Any thread the game's own camera code runs on. False when not installed, the object is gone, or the frame is not
// usable.
bool HaloCEVehicleCameras_RootFrame(uint32_t object,float position[3],float forward[3],float up[3]) noexcept;

// What the following camera's hook hands the placement (game.cpp Game_HaloCEChasePlace), all as drawn this frame.
struct HaloCEChaseFrame
{
    uint32_t generation{},unit{0xffffffffu},parent{0xffffffffu},root{0xffffffffu},definition{0xffffffffu};
    int16_t seat{-1};
    float boundsRadius{}; // world units; 0 = not read
    // The vehicle (the hierarchy's top): its root node.
    float vehiclePos[3]{},vehicleForward[3]{},vehicleUp[3]{};
    // The occupant's root node: the seat's point, and the frame of what the seat rides.
    bool seatValid{};
    float seatPos[3]{},seatForward[3]{},seatUp[3]{};
    // The occupant's head marker.
    bool headValid{};
    float headPos[3]{},headForward[3]{},headUp[3]{};
    // The game's own third-person camera this frame, from its command: the pivot it hangs from, where it stands from
    // that pivot, and the way it looks.
    bool gameValid{};
    float pivot[3]{},gameFromPivot[3]{},looks[3]{};
};

// game.cpp (haloce_vehicle_cameras.inl): what the shared camera runtime answers for a CE seat. Any thread the game's
// camera code runs on; one atomic load each while no camera of the kind is chosen.
//   ChaseAny: some chase camera is chosen now, in CE.
//   ChaseSeat: `unit` is the local player's, seated, a chase camera is chosen for exactly its seat, and the following
//     camera's hook is live to place it.
//   EyeNudgeWorld: a firstperson camera's "offset" for this seat moves `point` (the seat's eye, world units).
//   ChasePlace: where the chosen chase camera stands this frame.
//   VehicleCameraFollow: a chase camera is chosen for this seat; `yaw` = its mode turns the view with the hull.
bool Game_HaloCEChaseAny() noexcept;
bool Game_HaloCEChaseSeat(uint32_t unit) noexcept;
bool Game_HaloCEEyeNudgeWorld(uint32_t generation,uint32_t parent,int16_t seat,float point[3]) noexcept;
bool Game_HaloCEChasePlace(const HaloCEChaseFrame& frame,float out[3]) noexcept;
bool Game_HaloCEVehicleCameraFollow(uint32_t generation,uint32_t parent,int16_t seat,bool& yaw) noexcept;
// THE LEASH of the chase camera placed in the last 200 ms for this generation (haloce_vehicle_leash_logic.h): the frame
// its offset is laid out in and the box the head stays inside, in world units around the camera's point. False while
// no placed camera sets one. Any thread (the render paths read it once a frame).
bool Game_HaloCEVehicleCameraLeash(uint32_t generation,vehicle_camera_frame::Basis& carrier,
    vehicle_camera_frame::LeashBox& box) noexcept;
