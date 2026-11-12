#pragma once

// Reach's aim provider -- the FIRST per-game implementation of aim_solve::IAimProvider
// (src/common/aim_provider.h). Reach is closest to CampE, so it leads.
//
// ROUTING (input.cpp, experimental Reach builds): the provider owns Reach's aim stick when
// `aim_provider` is on, and always when `reach_direct_drive=3`, because its WriteState rung is the only
// thing that actuates the direct drive. Both default off, leaving the shipped call untouched.
//
// RUNGS. The core picks from caps() every poll:
//   WriteState -- direct drive into player_control.desired_angles (hook on haloreach+0x6375C). Offered
//                 only with reach_direct_drive=3, the arm-gate armed, and on foot or in a seat the hand
//                 aims (reach_vehicle_aim). The aim stick is held neutral while it drives.
//   StickLoop  -- Game_ComputeAimStick, the shipped closed loop, byte-for-byte: a seat whose stick
//                 steers, cinematics, an unarmed gate, and the default configuration all aim as before.
// The servo runs FIRST every poll -- it records the desired aim the core takes as its intent and
// computes the fallback stick -- so dropping from WriteState to StickLoop is immediate, never a gap.
//
// NOT YET CLAIMED: a replicated write. Co-op host-follow is unverified, so actuate() reports
// wrote_replicated_state=false even while the direct drive runs.

// Runs the provider solve and produces the aim right-stick for this poll. Returns true and fills
// outRx/outRy when the stick is driven (zeros while the state write owns aim), false when aim is not
// steering (matches Game_ComputeAimStick's contract).
bool ReachAimProvider_ProduceStick(float& outRx, float& outRy);

// The provider's 2 s status line. Called from the cold worker, never the input hook (the provider only
// counts into atomics there, keeping file I/O and the log lock out of the game's XInput path).
void ReachAimProvider_Report();
