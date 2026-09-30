#pragma once

#include <cstdint>

// Reach admitted barrel-fire -> selected effect -> native rumble dispatch.
// Discovery is from the pinned HREK; retail bytes only verify homologs.
namespace reach_haptic_contract {
inline constexpr uint32_t admitted_fire_handler = 0x4C0554;
inline constexpr uint32_t selected_effect_builder = 0x4C0F70;
inline constexpr uint32_t admitted_effect_source = 0x4C1648;
inline constexpr uint32_t authored_effect_wrapper = 0x493720;
// Distinct trigger/charge effects. HREK DF64E0 calls the two selected effect
// sites; retail 4B70AC is entered by the trigger-state worker 4B8778.
inline constexpr uint32_t trigger_effect_source = 0x4B70AC;
inline constexpr uint32_t trigger_state_worker = 0x4B8778;
inline constexpr uint32_t player_effect_wrapper = 0x162654;
inline constexpr uint32_t player_effect_dispatcher = 0x1626B8;
inline constexpr uint32_t effect_worker = 0x1628D0;
inline constexpr uint32_t queue_writer = 0x0DAEFC;
inline constexpr uint32_t haptic_update = 0x0DACD8;
inline constexpr uint32_t haptic_evaluator = 0x0DB19C;
inline constexpr uint32_t haptic_descriptor_advance = 0x163DA4;
inline constexpr uint32_t haptic_curve_input = 0x174ED8;
inline constexpr uint32_t haptic_curve_output = 0x175524;
inline constexpr uint32_t tag_table_root = 0x0C1A600;
inline constexpr uint32_t tag_group_root = 0x4E39F20;

inline constexpr uint32_t success_result_call = 0x4C06E3;
inline constexpr uint32_t selected_effect_call = 0x4C09EC;
inline constexpr uint32_t admitted_effect_call = 0x4C0A03;
inline constexpr uint32_t primary_effect_call = 0x4C169E;
inline constexpr uint32_t secondary_effect_call = 0x4C1797;
inline constexpr uint32_t dispatcher_wrapper_call = 0x493908;
inline constexpr uint32_t player_dispatch_call = 0x1626AC;
inline constexpr uint32_t local_queue_call = 0x162BB9;
inline constexpr uint32_t trigger_worker_source_call = 0x4B8BA1;
inline constexpr uint32_t trigger_primary_effect_call = 0x4B73FB;
inline constexpr uint32_t trigger_secondary_effect_call = 0x4B76C6;

struct Binding { uint32_t rva; const char* pattern; };
inline constexpr Binding entries[]{
    {admitted_fire_handler,
     "48 8B C4 48 89 58 18 66 89 50 10 55 56 57 41 54 41 55 41 56 41 57 48 81 EC B0 00 00 00 44 8B 0D A0 75 75 00"},
    {selected_effect_builder,
     "48 89 5C 24 08 44 88 4C 24 20 44 88 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 10 8B 05 84 6B 75 00"},
    {admitted_effect_source,
     "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 56 48 83 EC 40 49 8B D8 48 63 EA 8B F9 E8 4B CA FF FF"},
    {authored_effect_wrapper,
     "48 8B C4 55 53 56 57 41 56 48 8D A8 18 FE FF FF 48 81 EC C0 02 00 00 0F 29 70 C8 48 8B 05 CE 68 66 00"},
    {player_effect_wrapper,
     "48 83 EC 68 48 83 64 24 58 00 83 C8 FF F3 0F 10 84 24 A8 00 00 00 F3 0F 10 8C 24 A0 00 00 00 89 44 24 50"},
    {player_effect_dispatcher,
     "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 4C 89 48 20 57 41 54 41 55 41 56 41 57 48 81 EC 90 00 00 00"},
    {effect_worker,
     "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 B9 48 81 EC C8 00 00 00 0F 29 70 A8 48 8B 05 1B 77 99 00"},
    {queue_writer,
     "48 89 7C 24 08 83 CF FF 0F 28 DA 44 8B DA 3B D7 0F 84 16 01 00 00 44 8B 05 FF CB B3 00 65 48 8B 04 25 58 00 00 00"},
    {haptic_update,
     "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 57 41 54 41 55 41 56 41 57 48 83 EC 40 45 33 D2 0F 29 70 C8"},
    {haptic_evaluator,
     "48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 57 41 54 41 55 41 56 41 57 48 83 EC 50 F3 0F 10 81 D0 01 00 00"},
    {haptic_descriptor_advance,
     "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 55 41 56 48 8D 68 A1 48 81 EC C0 00 00 00 83 39 FF"},
    {haptic_curve_input,
     "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 57 48 81 EC 90 00 00 00 8B 59 0C 4C 8D 3D"},
    {haptic_curve_output,
     "8B 51 0C 48 8D 0D F2 49 CC 04 8B C2 48 C1 E8 1C 48 8B 0C C1 80 7C 91 02 00 75 43 F6 44 91 01 04 F3 0F 10"},
    {trigger_effect_source,
     "48 8B C4 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC B8 00 00 00 44 8B 05 4F 0A 76 00 45 33 C9 0F 29 78 A8"},
    {trigger_state_worker,
     "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 30 8B 15 7E F3 75 00 33 FF"},
};

struct Edge { uint32_t call, target; };
inline constexpr Edge edges[]{
    {success_result_call, 0x4C0B9C},
    {selected_effect_call, selected_effect_builder},
    {admitted_effect_call, admitted_effect_source},
    {primary_effect_call, authored_effect_wrapper},
    {secondary_effect_call, authored_effect_wrapper},
    {dispatcher_wrapper_call, player_effect_wrapper},
    {player_dispatch_call, player_effect_dispatcher},
    {local_queue_call, queue_writer},
    {0x0DADB1, haptic_descriptor_advance},
    {0x0DADE8, haptic_evaluator},
    {0x0DB268, haptic_curve_input},
    {0x0DB273, haptic_curve_output},
    {trigger_worker_source_call, trigger_effect_source},
    {trigger_primary_effect_call, player_effect_wrapper},
    {trigger_secondary_effect_call, player_effect_wrapper},
};

struct Witness { uint32_t rva; const char* pattern; };
inline constexpr Witness witnesses[]{
    // A zero result from the barrel admission helper bypasses the selected
    // effect source; nonzero reaches the selected effect builder and source.
    {0x4C09F4,
     "45 84 FF 74 0F 4C 8D 44 24 60 41 8B D4 8B CB E8 40 0C 00 00"},
    // Primary and secondary selected effects have separate call returns.
    {0x4C169E, "E8 7D 20 FD FF"},
    {0x4C1797, "E8 84 1F FD FF"},
    // The local user's worker passes through the native queue writer.
    {0x162BB9, "E8 3E 83 F7 FF"},
    // The trigger-state worker owns both primary and secondary charging/held
    // effect paths; the hand is resolved from the actual salted weapon datum.
    {0x4B8BA1, "E8 06 E5 FF FF"},
    {0x4B73FB, "E8 54 B2 CA FF"},
    {0x4B76C6, "E8 89 AF CA FF"},
};

struct DataReference { uint32_t instruction, size, displacement, target; };
inline constexpr DataReference data_references[]{
    {0x0DB1D3, 7, 3, tag_table_root},
    {0x0DB216, 7, 3, tag_group_root},
};
} // namespace reach_haptic_contract
