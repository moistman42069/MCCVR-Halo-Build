#pragma once

#include "haloce_contracts.generated.h"

// Pinned Halo 3 retail bindings, independently matched to H3EK. The common
// contract record types live in haloce_contracts.generated.h; this namespace
// contains only Halo 3 addresses and evidence.
namespace halo3_haptic_contract {
using halo_ce::contract::Entry;
using halo_ce::contract::Relative;
using halo_ce::contract::Witness;

inline constexpr uint32_t successful_weapon_fire = 0x366858;
inline constexpr uint32_t weapon_trigger_update = 0x35F828;
inline constexpr uint32_t damage_effect_wrapper = 0x35C864;
inline constexpr uint32_t player_effect = 0x1B370C;
inline constexpr uint32_t effect_worker = 0x1B38B4;
inline constexpr uint32_t haptic_update = 0x18EB28;
inline constexpr uint32_t haptic_evaluator = 0x18ED28;
inline constexpr uint32_t haptic_curve = 0x24FA50;

inline constexpr uint32_t admitted_event_call = 0x914A3;
inline constexpr uint32_t admitted_trigger_call = 0x361533;
inline constexpr uint32_t admitted_event_return = admitted_event_call + 5;
inline constexpr uint32_t admitted_trigger_return = admitted_trigger_call + 5;
inline constexpr uint32_t charge_effect_call_initial = 0x36076D;
inline constexpr uint32_t charge_effect_call_repeat = 0x360913;
inline constexpr uint32_t charge_effect_return_initial = charge_effect_call_initial + 5;
inline constexpr uint32_t charge_effect_return_repeat = charge_effect_call_repeat + 5;
inline constexpr uint32_t owner_damage_wrapper_call = 0x3673FF;
inline constexpr uint32_t owner_damage_wrapper_return = owner_damage_wrapper_call + 5;
inline constexpr uint32_t passenger_damage_wrapper_call = 0x3674CA;
inline constexpr uint32_t passenger_damage_wrapper_return = passenger_damage_wrapper_call + 5;
inline constexpr uint32_t wrapper_player_effect_call = 0x35C9FC;
inline constexpr uint32_t wrapper_player_effect_return = wrapper_player_effect_call + 5;
inline constexpr uint32_t worker_call_local_list = 0x1B384A;
inline constexpr uint32_t worker_call_specific_user = 0x1B388F;
inline constexpr uint32_t evaluator_curve_call = 0x18EE21;

inline constexpr std::array<Entry, 8> entries{{
    {"h3_admitted_weapon_fire", successful_weapon_fire,
     "48 8B C4 48 89 58 18 66 89 50 10 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 10 01 00 00", true},
    {"h3_weapon_trigger_update", weapon_trigger_update,
     "48 8B C4 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC E8 00 00 00 8B 15 51 A7 6D 00", true},
    {"h3_damage_effect_wrapper", damage_effect_wrapper,
     "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 70 20 55 48 8D 68 D8 48 81 EC 20 01 00 00 44 8B 05 12 D7 6D 00", true},
    {"h3_player_effect_dispatch", player_effect,
     "48 8B C4 48 89 58 08 48 89 68 10 4C 89 48 20 56 57 41 56 48 83 EC 70 0F 29 70 D8 44 8B D2 48 8B 15 E7 58 89 00 8B F1 0F", true},
    {"h3_player_effect_worker", effect_worker,
     "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 89 50 10 57 41 54 41 55 41 56 41 57 48 83 EC 50 4C 8B 3D EE BB E1 01 0F 29", true},
    {"h3_haptic_update", haptic_update,
     "48 89 5C 24 08 55 56 57 41 56 41 57 48 83 EC 40 45 33 FF 0F 29 74 24 30 44 38 3D 41 DF 8A 00 0F 28 F0 44 89 7C 24 78 0F", true},
    {"h3_haptic_evaluator", haptic_evaluator,
     "48 8B C4 48 89 58 18 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 F3 0F 10 81 90 00 00 00", true},
    {"h3_haptic_curve", haptic_curve,
     "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 56 41 57 48 81 EC 90 00 00 00 8B 71 0C 33 FF 4C 8B 35 51 FA D7 01 0F", true},
}};

inline constexpr std::array<Witness, 2> witnesses{{
    // Exact inline native eight-slot queue writes in the effect worker.
    {0x1B3B77, "F3 41 0F 11 74 80 08 45 89 24 80 41 89 6C 80 04 47 89 5C 90 70"},
    // Native haptic evaluator rejects the integer NONE datum before row lookup.
    {0x18EDA0, "83 7B F8 FF 0F 84 9C 00 00 00 48 63 4B FC 83 F9 FF 0F 84 8F 00 00 00 0F B7 43 F8"},
}};

inline constexpr std::array<Relative, 10> relatives{{
    {admitted_event_call, 5, 1, successful_weapon_fire},
    {admitted_trigger_call, 5, 1, successful_weapon_fire},
    {charge_effect_call_initial, 5, 1, player_effect},
    {charge_effect_call_repeat, 5, 1, player_effect},
    {owner_damage_wrapper_call, 5, 1, damage_effect_wrapper},
    {passenger_damage_wrapper_call, 5, 1, damage_effect_wrapper},
    {wrapper_player_effect_call, 5, 1, player_effect},
    {worker_call_local_list, 5, 1, effect_worker},
    {worker_call_specific_user, 5, 1, effect_worker},
    {evaluator_curve_call, 5, 1, haptic_curve},
}};
} // namespace halo3_haptic_contract
