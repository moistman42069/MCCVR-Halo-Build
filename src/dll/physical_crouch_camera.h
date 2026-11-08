#pragma once
#include "../common/physical_crouch_camera_logic.h"

void PhysicalCrouchCamera_Publish(GameTitle title,uint32_t generation,
    uint64_t trackingEpoch,uint64_t nowMs,bool available,bool requested) noexcept;
void PhysicalCrouchCamera_Invalidate() noexcept;
bool PhysicalCrouchCamera_Read(PhysicalCrouchCameraRequest& request) noexcept;

// DEV BUILD ONLY: the game's OWN crouch camera drop for a title (world units), as that title's camera
// correction just read it from the unit, so an automated test judges a physical duck by the game's state
// (the CROUCH line's nativeDrop/unitCrouched). A player build compiles both to nothing.
// When a title's reader cannot read it, NoteNativeMiss records why (bits, per title: 0x40 = no crouch
// request published by the input side, 0x80 = the unit/crouch read failed; lower bits are the title's
// own gates), so a stale witness says where the chain stopped. NoteApplied records the correction the
// title's camera actually added to the view this frame (0 = none), the end of the chain.
#if HALOMCCVR_DEV_DIAGNOSTICS
void PhysicalCrouchCamera_NoteNativeDrop(GameTitle title,float dropWu) noexcept;
bool PhysicalCrouchCamera_NativeDrop(GameTitle title,uint64_t maxAgeMs,float& dropWu) noexcept;
void PhysicalCrouchCamera_NoteNativeMiss(GameTitle title,uint32_t why) noexcept;
uint32_t PhysicalCrouchCamera_NativeMiss(GameTitle title,uint64_t maxAgeMs) noexcept;
void PhysicalCrouchCamera_NoteApplied(GameTitle title,float correctionWu) noexcept;
bool PhysicalCrouchCamera_Applied(GameTitle title,uint64_t maxAgeMs,float& correctionWu) noexcept;
#else
inline void PhysicalCrouchCamera_NoteNativeDrop(GameTitle,float) noexcept {}
inline bool PhysicalCrouchCamera_NativeDrop(GameTitle,uint64_t,float& dropWu) noexcept {dropWu=0;return false;}
inline void PhysicalCrouchCamera_NoteNativeMiss(GameTitle,uint32_t) noexcept {}
inline uint32_t PhysicalCrouchCamera_NativeMiss(GameTitle,uint64_t) noexcept {return 0;}
inline void PhysicalCrouchCamera_NoteApplied(GameTitle,float) noexcept {}
inline bool PhysicalCrouchCamera_Applied(GameTitle,uint64_t,float& correctionWu) noexcept {correctionWu=0;return false;}
#endif
