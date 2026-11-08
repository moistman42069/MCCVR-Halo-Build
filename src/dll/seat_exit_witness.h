#pragma once
// SEATEXIT witness (DEV BUILDS ONLY): why does Halo 3 / ODST drop the player out of a vehicle seat?
// Four recorders, all lock-, log- and allocation-free, feed fixed rings:
//  * the XInput merge: what our code handed the game each poll (input.cpp);
//  * the seat direct drive: every accumulator call, what it wrote against the game's aim
//    (legacy_direct_drive.inl, sim thread);
//  * the native player-control builder's reads of the Use record, through the action bridge's proven
//    reader hook (native_vr_actions_h3/odst.inl): the GAME's own view of the Use/Exit button;
//  * the Halo 3 vehicle sampler: the raw seated edge and the vehicle's proven object frame (camera thread).
// The cold worker prints one block per raw seated edge, and per vehicle->gameplay runtime-mode edge that
// no raw edge explained. A player build compiles all of it to the empty inline stubs below.
#include <cstdint>
#include "../common/runtime_types.h"

#if HALOMCCVR_DEV_DIAGNOSTICS
#include "../common/seat_exit_witness_logic.h"
// One clock for every recorder (QPC milliseconds).
uint64_t SeatWitness_NowMs() noexcept;
// XInput poll thread(s): stores the sample only when its state changed (or as a 100 ms heartbeat).
void SeatWitness_Record(const seat_witness::Sample& sample) noexcept;
// Sim thread: one direct-drive accumulator call.
void SeatWitness_RecordDrive(const seat_witness::DriveSample& sample) noexcept;
// Native player-control builder read of a watched action record (Counters: frames > 0 = held).
void SeatWitness_NoteNativeAction(GameTitle title, unsigned action, bool present, uint8_t frames,
    uint16_t heldMs) noexcept;
// Vehicle sampler (engine camera thread): raw seated state every sample; an edge is queued.
void SeatWitness_NoteSeat(GameTitle title, uint32_t generation, bool seated, int unit, int seat,
    int parent, int vehicleType, uint32_t defIndex, unsigned nativeInVehicle) noexcept;
// Vehicle sampler: the seated vehicle's validated object frame (world units).
void SeatWitness_NoteVehicleFrame(GameTitle title, const float pos[3], const float fwd[3],
    const float up[3]) noexcept;
// TitleAdapter_SetRuntimeMode: a vehicle/turret -> gameplay edge is queued.
void SeatWitness_NoteRuntimeMode(GameTitle title, uint8_t previous, uint8_t next) noexcept;
// Cold worker: prints the block for each queued edge once its +250 ms tail has been recorded.
void SeatWitness_Poll() noexcept;
#else
inline void SeatWitness_NoteSeat(GameTitle, uint32_t, bool, int, int, int, int, uint32_t, unsigned) noexcept {}
inline void SeatWitness_Poll() noexcept {}
#endif
