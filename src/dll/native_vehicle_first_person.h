#pragma once
#include <cstdint>

enum class GameTitle : uint8_t;
struct NativeVehicleCameraOwner
{
    uint32_t generation{},unit{UINT32_MAX},parent{UINT32_MAX};
    int16_t seat{-1};
    uintptr_t object{};
    uint64_t vehicleIdentity{}; // stable authored identity; never a live object datum
    bool operator==(const NativeVehicleCameraOwner&) const = default;
};

// Optional CE/H2/H4 camera feature. Existing H3/ODST/Reach paths are independent.
void NativeVehicleFirstPerson_Poll();
bool NativeVehicleFirstPerson_TrimTarget(GameTitle title,uint64_t& identity,int& seat) noexcept;
bool Game_ReadVehicleCameraOwner(GameTitle title, NativeVehicleCameraOwner& owner) noexcept;
bool HaloCEControls_ReadVehicleCameraOwner(NativeVehicleCameraOwner& owner) noexcept;
bool Halo2Observer6Dof_ReadVehicleCameraOwner(NativeVehicleCameraOwner& owner) noexcept;
