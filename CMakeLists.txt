cmake_minimum_required(VERSION 3.24)
project(halomccvr LANGUAGES C CXX RC)

if(MSVC)
    enable_language(ASM_MASM)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT so users do not need a separate Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
include(CTest)

# Put a source identity in the first log line. The package command always
# reconfigures from a clean commit, so changing commits recompiles dllmain.cpp.
find_package(Git QUIET)
set(HALOMCCVR_BUILD_COMMIT "unknown")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
            rev-parse HEAD
        OUTPUT_VARIABLE HALOMCCVR_BUILD_COMMIT
        RESULT_VARIABLE HALOMCCVR_GIT_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT HALOMCCVR_GIT_RESULT EQUAL 0)
        set(HALOMCCVR_BUILD_COMMIT "unknown")
    else()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                status --porcelain --untracked-files=normal
            OUTPUT_VARIABLE HALOMCCVR_GIT_STATUS
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT HALOMCCVR_GIT_STATUS STREQUAL "")
            string(APPEND HALOMCCVR_BUILD_COMMIT "-dirty")
        endif()
    endif()
endif()

# The accepted 0.3.0 product is one cumulative Halo 3 + ODST + Reach build. OFF
# remains available only for a deliberate Halo 3 isolation build.
option(HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    "Enable the cumulative Halo 3 + ODST runtime" ON)

if(HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP)
    set(HALOMCCVR_BUILD_ODST "ON")
    message(STATUS "Halo MCC VR: cumulative Halo 3 + ODST runtime enabled")
else()
    set(HALOMCCVR_BUILD_ODST "OFF")
    message(WARNING
        "Halo MCC VR: ODST is disabled; this does not match release 0.3.0")
endif()

# Halo: Reach is a permanent title in the one cumulative build, exactly like
# Halo 3 and ODST. There is no experimental Reach flag to toggle on or off.
# Reach installs and arms its VR camera/stereo path only after the complete
# loaded-image proof passes on the live module. Before ownership, a failed proof
# installs nothing; after ownership, a mandatory-path failure rejects the whole
# transaction and enters teardown without a flat rerender. This all-or-nothing
# title isolation preserves Halo 3 and ODST without an on/off compile gate.
set(HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP ON)
set(HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE ON)
set(HALOMCCVR_BUILD_REACH "ON")
set(HALOMCCVR_BUILD_REACH_RENDER "ON")
message(STATUS
    "Halo MCC VR: Halo: Reach is permanent with all-or-nothing gated ownership")

# Halo 4 bring-up C-H4-7: stock-projection stereo geometry on the sustained
# per-window transaction. Hook installation is atomic and still requires
# C-H4-2's cold observation plus every unique pinned camera anchor. Runtime
# frame failures drop only that frame and retry with the core armed. The compile
# flag isolates this unaccepted title line; OFF returns Halo 4 to C-H4-2's
# recognised, gamepad-enabled flat behavior.
option(HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA
    "Enable Halo 4 stock-projection stereo geometry (C-H4-7)" ON)
if(HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA)
    set(HALOMCCVR_BUILD_HALO4 "ON")
    message(STATUS
        "Halo MCC VR: Halo 4 stock-projection stereo geometry enabled (C-H4-7)")
else()
    set(HALOMCCVR_BUILD_HALO4 "OFF")
endif()

# Halo 2 C-H2-1 is the independently isolatable read-only identity/liveness
# prerequisite. Rejected C-H2-2 remains dormant behind its own gate. C-H2-6
# separately owns same-frame stereo and headset 6DOF. An unclaimed frame keeps
# stock screen presentation; a partially claimed failed frame is dropped and
# the title core is quarantined for that module generation.
# Turning both behavior gates off returns to C-H2-1.
option(HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION
    "Enable Halo 2 read-only cold observation (C-H2-1)" ON)
option(HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO
    "Enable rejected Halo 2 temporal position stereo (C-H2-2, dormant)" OFF)
option(HALOMCCVR_HALO2_STEREO6DOF
    "Enable Halo 2 same-frame stereo and headset 6DOF (C-H2-6)" ON)
option(HALOMCCVR_HALO2_OBSERVER_6DOF
    "Enable Halo 2 headset 6DOF through the shared observer camera (C-H2-8)" ON)
option(HALOMCCVR_HALO2_ANNIVERSARY_STEREO
    "Enable Halo 2 Anniversary per-eye stereo (C-H2-10)" ON)
option(HALOMCCVR_HALO2_RENDER_PROBE
    "Enable the Halo 2 read-only render-topology census (D-H2-1, diagnostic)" OFF)
if(HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO AND
   NOT HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION)
    message(FATAL_ERROR
        "Halo 2 C-H2-2 requires C-H2-1 cold observation")
endif()
if(HALOMCCVR_HALO2_STEREO6DOF AND
   NOT HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION)
    message(FATAL_ERROR
        "Halo 2 C-H2-6 requires C-H2-1 cold observation")
endif()
if(HALOMCCVR_HALO2_OBSERVER_6DOF AND
   NOT HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION)
    message(FATAL_ERROR
        "Halo 2 C-H2-8 requires C-H2-1 cold observation")
endif()
if(HALOMCCVR_HALO2_STEREO6DOF AND
   HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO)
    message(FATAL_ERROR
        "Halo 2 same-frame and rejected temporal stereo cannot both be enabled")
endif()
if(HALOMCCVR_HALO2_OBSERVER_6DOF)
    set(HALOMCCVR_BUILD_HALO2 "OBSERVER_6DOF_BOTH_RENDERERS")
    message(STATUS
        "Halo MCC VR: Halo 2 observer 6DOF enabled (C-H2-8)")
elseif(HALOMCCVR_HALO2_STEREO6DOF)
    set(HALOMCCVR_BUILD_HALO2 "SAME_FRAME_6DOF_SESSION_FIX")
    message(STATUS
        "Halo MCC VR: Halo 2 same-frame stereo + 6DOF enabled (C-H2-6)")
elseif(HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO)
    set(HALOMCCVR_BUILD_HALO2 "TEMPORAL")
    message(STATUS
        "Halo MCC VR: Halo 2 temporal position stereo enabled (C-H2-2)")
elseif(HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION)
    set(HALOMCCVR_BUILD_HALO2 "COLD")
    message(STATUS
        "Halo MCC VR: Halo 2 read-only cold observation enabled (C-H2-1)")
else()
    set(HALOMCCVR_BUILD_HALO2 "OFF")
endif()

message(STATUS
    "Halo MCC VR build identity: ${HALOMCCVR_BUILD_COMMIT}, ODST=${HALOMCCVR_BUILD_ODST}, Reach=${HALOMCCVR_BUILD_REACH}, ReachRender=${HALOMCCVR_BUILD_REACH_RENDER}, Halo4=${HALOMCCVR_BUILD_HALO4}, Halo2=${HALOMCCVR_BUILD_HALO2}")

# Exact dependency commits used by the accepted release source. Shallow clones
# avoid pulling entire third-party histories into the ignored build tree.
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG 5267613edf3d937e3d77556a106a65c2f82b25c6
    GIT_SHALLOW TRUE)

FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
    GIT_SHALLOW TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 8936b58fe26e8c3da834b8f60b06511d537b4c63
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(openxr minhook imgui)

# Keep upstream pinned and untouched on disk. The local translation unit adds
# a no-write retirement path for title DLLs MCC has already unmapped.
get_target_property(mccvr_minhook_sources minhook SOURCES)
list(FILTER mccvr_minhook_sources EXCLUDE REGEX "(^|/)hook\\.c$")
set_property(TARGET minhook PROPERTY SOURCES ${mccvr_minhook_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/minhook_lifecycle.c")

# Generate, validate, and embed the pinned official SMAA shaders. Bytecode is
# compiled at configure time so selecting SMAA cannot invoke the HLSL optimizer
# on the VR render thread. The installed mod has no loose shader dependency.
set(HALOMCCVR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HALOMCCVR_GENERATED_DIR}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl"
    HALOMCCVR_SMAA_UPSTREAM_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dll/smaa_runtime.hlsl.in"
    "${HALOMCCVR_GENERATED_DIR}/smaa_runtime.hlsl"
    @ONLY)

get_filename_component(HALOMCCVR_WINDOWS_SDK_BIN "${CMAKE_RC_COMPILER}" DIRECTORY)
find_program(HALOMCCVR_FXC NAMES fxc.exe fxc
    HINTS
        "${HALOMCCVR_WINDOWS_SDK_BIN}"
        "$ENV{WindowsSdkVerBinPath}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
if(NOT HALOMCCVR_FXC)
    file(GLOB HALOMCCVR_FXC_CANDIDATES
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
    list(SORT HALOMCCVR_FXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    if(HALOMCCVR_FXC_CANDIDATES)
        list(GET HALOMCCVR_FXC_CANDIDATES 0 HALOMCCVR_FXC)
    endif()
endif()
if(NOT HALOMCCVR_FXC)
    message(FATAL_ERROR "fxc is required to build the embedded SMAA shaders")
endif()

function(halomccvr_compile_smaa entry profile output_name)
    execute_process(
        COMMAND "${HALOMCCVR_FXC}" /nologo /Ges /WX /O3
            /T "${profile}" /E "${entry}"
            /Fo "${HALOMCCVR_GENERATED_DIR}/${output_name}"
            "${HALOMCCVR_GENERATED_DIR}/smaa_runtime.hlsl"
        RESULT_VARIABLE HALOMCCVR_FXC_RESULT
        OUTPUT_VARIABLE HALOMCCVR_FXC_OUTPUT
        ERROR_VARIABLE HALOMCCVR_FXC_ERROR)
    if(NOT HALOMCCVR_FXC_RESULT EQUAL 0)
        message(FATAL_ERROR
            "SMAA shader ${entry} failed to compile:\n"
            "${HALOMCCVR_FXC_OUTPUT}${HALOMCCVR_FXC_ERROR}")
    endif()
endfunction()

halomccvr_compile_smaa(vs_smaa_edges vs_5_0 smaa_edges_vs.cso)
halomccvr_compile_smaa(vs_smaa_weights vs_5_0 smaa_weights_vs.cso)
halomccvr_compile_smaa(vs_smaa_neighborhood vs_5_0 smaa_neighborhood_vs.cso)
halomccvr_compile_smaa(ps_smaa_edges ps_5_0 smaa_edges_ps.cso)
halomccvr_compile_smaa(ps_smaa_weights ps_5_0 smaa_weights_ps.cso)
halomccvr_compile_smaa(ps_smaa_neighborhood ps_5_0 smaa_neighborhood_ps.cso)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dll/smaa_resources.rc.in"
    "${HALOMCCVR_GENERATED_DIR}/smaa_resources.rc"
    @ONLY)

# OpenXR requests the dynamic CRT for its static loader; keep it consistent with
# the mod's static-CRT build.
set_target_properties(openxr_loader PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)

add_library(halo3xr SHARED
    ${HALOMCCVR_GENERATED_DIR}/smaa_resources.rc
    src/common/log.cpp
    src/common/config.cpp
    src/common/input_logic.cpp
    src/common/scope_logic.cpp
    src/common/title_registry.cpp
    src/common/title_runtime_state.cpp
    src/dll/dllmain.cpp
    src/dll/native_fault_probe.cpp
    src/dll/d3d11_hook.cpp
    src/dll/vr.cpp
    src/dll/weapon_accessory_renderer.cpp
    src/dll/native_reload_policy.cpp
    src/dll/native_vehicle_first_person.cpp
    src/dll/menu.cpp
    src/dll/native_menu_pointer.cpp
    src/dll/sigscan.cpp
    src/dll/game.cpp
    src/dll/ik.cpp
    src/dll/input.cpp
    src/dll/roomscale.cpp
    src/dll/title_adapter.cpp
    src/dll/title_reentry_probe.cpp
    src/dll/reach_adapter.cpp
    src/dll/halo2_adapter.cpp
    src/dll/halo2_cold_observation.cpp
    src/dll/halo2_temporal_stereo.cpp
    src/dll/halo2_stereo_core.cpp
    src/dll/halo2_observer_6dof.cpp
    src/dll/halo2_render_mode_guard.cpp
    src/dll/halo2_render_probe.cpp
    src/dll/halo2_saber_camera.cpp
    src/dll/haloce_native_bindings.cpp
    src/dll/haloce_eye_cache.cpp
    src/dll/haloce_stereo_core.cpp
    src/dll/haloce_first_person.cpp
    src/dll/haloce_contact.cpp
    src/dll/haloce_hud.cpp
    src/dll/haloce_hud_layout.cpp
    src/dll/haloce_hud_target.cpp
    src/dll/haloce_controls.cpp
    src/dll/haloce_comfort.cpp
    src/dll/haloce_orientation.cpp
    src/dll/haloce_unit_control.cpp
    src/dll/haloce_network_input.cpp
    src/dll/halo2_anniversary_stereo.cpp
    src/dll/halo4_adapter.cpp
    src/dll/halo4_cold_observation.cpp)
if(HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA)
    target_sources(halo3xr PRIVATE src/dll/halo4_restoration.asm)
endif()
set_target_properties(halo3xr PROPERTIES OUTPUT_NAME "HaloMCCVR")
target_include_directories(halo3xr PRIVATE
    ${HALOMCCVR_GENERATED_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/Textures)
if(HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE)
    target_sources(halo3xr PRIVATE
        src/dll/reach_render_candidate.cpp
        src/dll/reach_render_preflight.cpp)
endif()
target_compile_definitions(halo3xr PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    XR_USE_GRAPHICS_API_D3D11 XR_USE_PLATFORM_WIN32
    HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP=$<BOOL:${HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP}>
    HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP=$<BOOL:${HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP}>
    HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE=$<BOOL:${HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE}>
    HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA=$<BOOL:${HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA}>
    HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION=$<BOOL:${HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION}>
    HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO=$<BOOL:${HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO}>
    HALOMCCVR_HALO2_STEREO6DOF=$<BOOL:${HALOMCCVR_HALO2_STEREO6DOF}>
    HALOMCCVR_HALO2_OBSERVER_6DOF=$<BOOL:${HALOMCCVR_HALO2_OBSERVER_6DOF}>
    HALOMCCVR_HALO2_RENDER_PROBE=$<BOOL:${HALOMCCVR_HALO2_RENDER_PROBE}>
    HALOMCCVR_HALO2_ANNIVERSARY_STEREO=$<BOOL:${HALOMCCVR_HALO2_ANNIVERSARY_STEREO}>
    HALOMCCVR_BUILD_COMMIT="${HALOMCCVR_BUILD_COMMIT}"
    HALOMCCVR_BUILD_ODST="${HALOMCCVR_BUILD_ODST}"
    HALOMCCVR_BUILD_REACH="${HALOMCCVR_BUILD_REACH}"
    HALOMCCVR_BUILD_REACH_RENDER="${HALOMCCVR_BUILD_REACH_RENDER}"
    HALOMCCVR_BUILD_HALO4="${HALOMCCVR_BUILD_HALO4}"
    HALOMCCVR_BUILD_HALO2="${HALOMCCVR_BUILD_HALO2}")
target_link_libraries(halo3xr PRIVATE
    openxr_loader minhook imgui_lib
    d3d11 dxgi d3dcompiler gdi32)
target_sources(halo3xr PRIVATE src/dll/native_subtitles.cpp)
target_sources(halo3xr PRIVATE src/dll/physical_crouch_camera.cpp)
target_sources(halo3xr PRIVATE src/dll/bloom_override.cpp)
if(HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE)
    target_link_libraries(halo3xr PRIVATE bcrypt)
endif()

include(cmake/Dlss.cmake)

add_executable(halo3xr_launcher WIN32 src/launcher/launcher.cpp
    src/launcher/installer.cpp src/launcher/menu.cpp src/launcher/updater.cpp)
set_target_properties(halo3xr_launcher PROPERTIES
    OUTPUT_NAME "HaloMCCVRLauncher")
target_compile_definitions(halo3xr_launcher PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# IApplicationActivationManager: the Microsoft Store edition is a packaged app
# and can only be started through packaged activation.
target_link_libraries(halo3xr_launcher PRIVATE ole32 bcrypt advapi32 winhttp crypt32 shell32 uxtheme)

# Opt-in reverse-engineering helper; never shipped or built by the default
# preset. Some diagnostic modes write to a running game process.
add_executable(camscan EXCLUDE_FROM_ALL tools/camscan.cpp)
target_compile_definitions(camscan PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(camscan PRIVATE psapi)

# Opt-in, external Reach observer. It opens MCC with query/read rights only and
# is deliberately excluded from product/default builds.
add_executable(reach_runtime_observer EXCLUDE_FROM_ALL
    tools/reach_runtime_observer.cpp)
target_include_directories(reach_runtime_observer PRIVATE src/common)
target_compile_definitions(reach_runtime_observer PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    HALOMCCVR_BUILD_COMMIT="${HALOMCCVR_BUILD_COMMIT}")
target_link_libraries(reach_runtime_observer PRIVATE bcrypt ntdll)
set_target_properties(reach_runtime_observer PROPERTIES
    OUTPUT_NAME "reach-runtime-observer")

if(BUILD_TESTING)
    add_executable(halomccvr_bloom_override_tests tests/bloom_override_tests.cpp src/dll/bloom_override.cpp src/common/config.cpp)
    target_compile_definitions(halomccvr_bloom_override_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_bloom_override_tests PRIVATE d3d11)
    add_test(NAME halomccvr_bloom_override_tests COMMAND halomccvr_bloom_override_tests)
    add_executable(halomccvr_muzzle_suppression_tests tests/muzzle_suppression_tests.cpp)
    target_compile_definitions(halomccvr_muzzle_suppression_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_muzzle_suppression_tests COMMAND halomccvr_muzzle_suppression_tests)
    add_executable(halomccvr_physical_crouch_tests tests/physical_crouch_tests.cpp)
    add_test(NAME halomccvr_physical_crouch_tests COMMAND halomccvr_physical_crouch_tests)
    add_executable(halomccvr_physical_crouch_camera_tests tests/physical_crouch_camera_tests.cpp src/dll/physical_crouch_camera.cpp)
    add_test(NAME halomccvr_physical_crouch_camera_tests COMMAND halomccvr_physical_crouch_camera_tests)
    add_executable(halomccvr_dlss_sampler_tests tests/dlss_sampler_cache_tests.cpp)
    target_compile_definitions(halomccvr_dlss_sampler_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_dlss_sampler_tests PRIVATE d3d11)
    add_test(NAME halomccvr_dlss_sampler_tests COMMAND halomccvr_dlss_sampler_tests)
    add_executable(halomccvr_gesture_binding_reader_tests tests/gesture_binding_reader_tests.cpp)
    target_compile_definitions(halomccvr_gesture_binding_reader_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_gesture_binding_reader_tests COMMAND halomccvr_gesture_binding_reader_tests)
    add_executable(halomccvr_updater_archive_tests tests/updater_archive_tests.cpp
        src/launcher/installer.cpp src/launcher/updater.cpp)
    target_compile_definitions(halomccvr_updater_archive_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(halomccvr_updater_archive_tests PRIVATE ole32 bcrypt advapi32 winhttp crypt32 shell32)
    add_test(NAME halomccvr_updater_archive_tests COMMAND halomccvr_updater_archive_tests)
    add_executable(halomccvr_installer_tests tests/installer_tests.cpp src/launcher/installer.cpp)
    target_compile_definitions(halomccvr_installer_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(halomccvr_installer_tests PRIVATE bcrypt advapi32)
    add_test(NAME halomccvr_installer_tests COMMAND halomccvr_installer_tests)
    add_executable(halomccvr_vr_mapping_tests tests/vr_action_mapping_tests.cpp src/common/config.cpp src/common/log.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_vr_mapping_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vr_mapping_tests COMMAND halomccvr_vr_mapping_tests)
    add_executable(halomccvr_halo4_pause_lifetime_tests tests/halo4_pause_lifetime_tests.cpp)
    target_compile_definitions(halomccvr_halo4_pause_lifetime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_pause_lifetime_tests COMMAND halomccvr_halo4_pause_lifetime_tests)
    add_executable(halomccvr_reach_hud_height_tests tests/reach_hud_height_tests.cpp)
    target_compile_definitions(halomccvr_reach_hud_height_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_reach_hud_height_tests COMMAND halomccvr_reach_hud_height_tests)
    add_executable(halomccvr_hook_lifecycle_tests tests/minhook_lifecycle_tests.cpp)
    target_link_libraries(halomccvr_hook_lifecycle_tests PRIVATE minhook)
    add_test(NAME halomccvr_hook_lifecycle_tests COMMAND halomccvr_hook_lifecycle_tests)
    # Packaging runs this tiny program against a nonexistent path to generate
    # the current source tree's exact default config. It is intentionally not
    # installed by CMake: package-candidate stages and hashes its output while
    # install-candidate preserves every already-existing live config.
    add_executable(halomccvr_config_defaults
        tools/config_defaults.cpp
        src/common/config.cpp
        src/common/log.cpp)
    target_include_directories(halomccvr_config_defaults PRIVATE src/common)
    target_compile_definitions(halomccvr_config_defaults PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(halomccvr_config_defaults PROPERTIES
        OUTPUT_NAME "halomccvr-config-defaults")

    add_executable(halomccvr_core_tests
        tests/core_tests.cpp
        tests/roomscale_input_tests.cpp
        src/common/input_logic.cpp
        src/common/scope_logic.cpp
        src/common/title_registry.cpp
        src/common/title_runtime_state.cpp
        src/common/config.cpp
        src/common/log.cpp
        src/dll/sigscan.cpp
        src/dll/reach_adapter.cpp
        src/dll/halo2_adapter.cpp
        src/dll/halo4_adapter.cpp)
    if(HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE)
        target_sources(halomccvr_core_tests PRIVATE
            src/dll/reach_render_candidate.cpp
            src/dll/reach_render_preflight.cpp)
        target_link_libraries(halomccvr_core_tests PRIVATE bcrypt)
    endif()
    target_include_directories(halomccvr_core_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_core_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP=$<BOOL:${HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP}>
        HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP=$<BOOL:${HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP}>
        HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE=$<BOOL:${HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE}>
        HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA=$<BOOL:${HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA}>
        HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION=$<BOOL:${HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION}>
        HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO=$<BOOL:${HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO}>
        HALOMCCVR_HALO2_STEREO6DOF=$<BOOL:${HALOMCCVR_HALO2_STEREO6DOF}>
        HALOMCCVR_HALO2_OBSERVER_6DOF=$<BOOL:${HALOMCCVR_HALO2_OBSERVER_6DOF}>
        HALOMCCVR_HALO2_RENDER_PROBE=$<BOOL:${HALOMCCVR_HALO2_RENDER_PROBE}>
        HALOMCCVR_HALO2_ANNIVERSARY_STEREO=$<BOOL:${HALOMCCVR_HALO2_ANNIVERSARY_STEREO}>)
    add_test(NAME halomccvr_core_tests COMMAND halomccvr_core_tests)
    # Exercise shipping XInput/OpenXR haptic functions with captured XR output.
    # This also checks the real title masks, ownership and short-pulse retention.
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_executable(halomccvr_virtual_stock_tests tests/virtual_stock_tests.cpp)
    target_include_directories(halomccvr_virtual_stock_tests PRIVATE src/common)
    add_test(NAME halomccvr_virtual_stock_tests COMMAND halomccvr_virtual_stock_tests)
    set(HALOMCCVR_STOCK_FIXTURE "${HALOMCCVR_GENERATED_DIR}/virtual_stock_fixture.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_STOCK_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_virtual_stock_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_STOCK_FIXTURE}"
        DEPENDS tools/generate_virtual_stock_fixture.py tools/generate_haptics_runtime_fixture.py
            src/dll/vr.cpp src/dll/virtual_stock_aim.inl VERBATIM)
    add_executable(halomccvr_virtual_stock_aim_tests tests/virtual_stock_aim_tests.cpp "${HALOMCCVR_STOCK_FIXTURE}")
    target_include_directories(halomccvr_virtual_stock_aim_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include")
    add_test(NAME halomccvr_virtual_stock_aim_tests COMMAND halomccvr_virtual_stock_aim_tests)
    set(HALOMCCVR_HAPTICS_FIXTURE "${HALOMCCVR_GENERATED_DIR}/haptics_runtime_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_HAPTICS_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_haptics_runtime_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_HAPTICS_FIXTURE}"
        DEPENDS tools/generate_haptics_runtime_fixture.py src/dll/input.cpp src/dll/vr.cpp
            src/dll/game.cpp src/dll/haloce_stereo_core.cpp
        VERBATIM)
    add_executable(halomccvr_haptics_runtime_tests tests/haptics_runtime_tests.cpp
        "${HALOMCCVR_HAPTICS_FIXTURE}" src/common/input_logic.cpp
        src/common/title_registry.cpp src/common/title_runtime_state.cpp
        src/common/config.cpp src/common/log.cpp)
    target_include_directories(halomccvr_haptics_runtime_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_haptics_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_haptics_runtime_tests COMMAND halomccvr_haptics_runtime_tests)
    add_executable(halomccvr_dpad_input_tests tests/dpad_input_tests.cpp src/common/input_logic.cpp)
    target_include_directories(halomccvr_dpad_input_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_dpad_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_dpad_input_tests COMMAND halomccvr_dpad_input_tests)
    add_executable(halomccvr_weapon_interaction_tests tests/weapon_interaction_tests.cpp)
    add_executable(halomccvr_native_reload_tests tests/native_reload_policy_tests.cpp src/common/config.cpp)
    add_executable(halomccvr_native_vehicle_fp_tests tests/native_vehicle_first_person_tests.cpp src/common/config.cpp)
    target_include_directories(halomccvr_native_vehicle_fp_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_native_vehicle_fp_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_native_vehicle_fp_tests COMMAND halomccvr_native_vehicle_fp_tests)
    add_executable(halomccvr_odst_seat_lifetime_tests tests/odst_seat_lifetime_tests.cpp)
    target_compile_definitions(halomccvr_odst_seat_lifetime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_odst_seat_lifetime_tests COMMAND halomccvr_odst_seat_lifetime_tests)
    add_executable(halomccvr_halo3_seat_lifetime_tests tests/halo3_seat_lifetime_tests.cpp)
    target_compile_definitions(halomccvr_halo3_seat_lifetime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_seat_lifetime_tests COMMAND halomccvr_halo3_seat_lifetime_tests)
    add_executable(halomccvr_halo4_vehicle_input_tests tests/halo4_vehicle_input_tests.cpp)
    target_compile_definitions(halomccvr_halo4_vehicle_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_vehicle_input_tests COMMAND halomccvr_halo4_vehicle_input_tests)
    target_include_directories(halomccvr_native_reload_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_native_reload_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_native_reload_tests COMMAND halomccvr_native_reload_tests)
    add_test(NAME halomccvr_weapon_interaction_tests COMMAND halomccvr_weapon_interaction_tests)
    add_executable(halomccvr_weapon_accessory_tests tests/weapon_accessory_tests.cpp src/dll/weapon_accessory_renderer.cpp)
    target_compile_definitions(halomccvr_weapon_accessory_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_weapon_accessory_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_weapon_accessory_tests COMMAND halomccvr_weapon_accessory_tests)
    set(HALOMCCVR_RELOAD_IDENTITY_FIXTURE "${HALOMCCVR_GENERATED_DIR}/reload_identity_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_RELOAD_IDENTITY_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_reload_identity_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_RELOAD_IDENTITY_FIXTURE}"
        DEPENDS tools/generate_reload_identity_fixture.py tools/generate_haptics_runtime_fixture.py
            src/dll/game.cpp src/dll/halo2_observer_6dof.cpp VERBATIM)
    add_executable(halomccvr_reload_identity_tests tests/reload_identity_tests.cpp "${HALOMCCVR_RELOAD_IDENTITY_FIXTURE}")
    target_include_directories(halomccvr_reload_identity_tests PRIVATE "${HALOMCCVR_GENERATED_DIR}")
    target_compile_definitions(halomccvr_reload_identity_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_reload_identity_tests COMMAND halomccvr_reload_identity_tests)
    set(HALOMCCVR_DPAD_ACTION_FIXTURE "${HALOMCCVR_GENERATED_DIR}/dpad_action_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_DPAD_ACTION_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dpad_action_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_DPAD_ACTION_FIXTURE}"
        DEPENDS tools/generate_dpad_action_fixture.py tools/generate_haptics_runtime_fixture.py src/dll/vr.cpp
        VERBATIM)
    add_executable(halomccvr_dpad_action_tests tests/dpad_action_tests.cpp "${HALOMCCVR_DPAD_ACTION_FIXTURE}"
        src/common/config.cpp src/common/log.cpp)
    target_include_directories(halomccvr_dpad_action_tests PRIVATE "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_dpad_action_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_dpad_action_tests COMMAND halomccvr_dpad_action_tests)
    add_executable(halomccvr_left_hand_h2_tests tests/left_hand_h2_tests.cpp)
    target_include_directories(halomccvr_left_hand_h2_tests PRIVATE src/common)
    add_test(NAME halomccvr_left_hand_h2_tests COMMAND halomccvr_left_hand_h2_tests)
    add_executable(halomccvr_left_hand_shared_tests tests/left_hand_shared_tests.cpp)
    target_include_directories(halomccvr_left_hand_shared_tests PRIVATE src/common)
    add_test(NAME halomccvr_left_hand_shared_tests COMMAND halomccvr_left_hand_shared_tests)
    add_executable(halomccvr_ce_render_tests tests/haloce_render_tests.cpp)
    target_include_directories(halomccvr_ce_render_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_render_tests COMMAND halomccvr_ce_render_tests)
    add_executable(halomccvr_ce_view_pair_tests tests/haloce_view_pair_tests.cpp)
    target_include_directories(halomccvr_ce_view_pair_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_view_pair_tests COMMAND halomccvr_ce_view_pair_tests)
    add_executable(halomccvr_ce_classic_view_pair_tests tests/haloce_classic_view_pair_tests.cpp)
    target_include_directories(halomccvr_ce_classic_view_pair_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_classic_view_pair_tests COMMAND halomccvr_ce_classic_view_pair_tests)
    add_executable(halomccvr_ce_first_person_tests tests/haloce_first_person_tests.cpp src/dll/ik.cpp)
    target_include_directories(halomccvr_ce_first_person_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_first_person_tests COMMAND halomccvr_ce_first_person_tests)
    add_executable(halomccvr_ce_contact_tests tests/haloce_contact_tests.cpp src/dll/ik.cpp)
    target_include_directories(halomccvr_ce_contact_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_contact_tests COMMAND halomccvr_ce_contact_tests)
    add_executable(halomccvr_ce_contact_runtime_tests tests/haloce_contact_runtime_tests.cpp
        src/dll/haloce_native_bindings.cpp src/dll/ik.cpp)
    target_include_directories(halomccvr_ce_contact_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_contact_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_contact_runtime_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_contact_runtime_tests COMMAND halomccvr_ce_contact_runtime_tests)
    # Runs through tools/re/test_ce_fp_shader_native.py with locally extracted,
    # hash-pinned CE shader bytecode. Proprietary fixtures are not in the repo.
    add_executable(halomccvr_ce_first_person_shader_tests tests/haloce_first_person_shader_tests.cpp)
    target_compile_definitions(halomccvr_ce_first_person_shader_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_first_person_shader_tests PRIVATE d3d11 d3dcompiler)
    # External, pinned mesh/sprite particle shaders; see CE muzzle evidence.
    add_executable(halomccvr_ce_particle_shader_tests tests/haloce_particle_shader_tests.cpp)
    target_compile_definitions(halomccvr_ce_particle_shader_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_particle_shader_tests PRIVATE d3d11 d3dcompiler)
    add_executable(halomccvr_ce_bindings_tests
        tests/haloce_bindings_tests.cpp src/dll/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_bindings_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_bindings_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_bindings_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_bindings_tests COMMAND halomccvr_ce_bindings_tests)
    add_executable(halomccvr_ce_eye_cache_tests
        tests/haloce_eye_cache_tests.cpp src/dll/haloce_eye_cache.cpp)
    target_include_directories(halomccvr_ce_eye_cache_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_eye_cache_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_eye_cache_tests PRIVATE d3d11)
    add_test(NAME halomccvr_ce_eye_cache_tests COMMAND halomccvr_ce_eye_cache_tests)
    add_executable(halomccvr_ce_desktop_mirror_tests tests/haloce_desktop_mirror_tests.cpp)
    target_compile_definitions(halomccvr_ce_desktop_mirror_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_desktop_mirror_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_ce_desktop_mirror_tests COMMAND halomccvr_ce_desktop_mirror_tests)
    add_executable(halomccvr_ce_runtime_tests tests/haloce_runtime_tests.cpp
        src/dll/haloce_native_bindings.cpp src/dll/haloce_eye_cache.cpp src/dll/haloce_hud_target.cpp tests/haloce_hud_layout_runtime_fixture.cpp)
    target_include_directories(halomccvr_ce_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_runtime_tests PRIVATE d3d11 d3dcompiler minhook)
    add_test(NAME halomccvr_ce_runtime_tests COMMAND halomccvr_ce_runtime_tests)
    add_executable(halomccvr_ce_classic_runtime_tests tests/haloce_classic_runtime_tests.cpp
        src/dll/haloce_native_bindings.cpp src/dll/haloce_eye_cache.cpp src/dll/haloce_hud_target.cpp src/dll/haloce_hud_layout.cpp)
    target_include_directories(halomccvr_ce_classic_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_classic_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_classic_runtime_tests PRIVATE d3d11 minhook)
    add_test(NAME halomccvr_ce_classic_runtime_tests COMMAND halomccvr_ce_classic_runtime_tests)
    add_executable(halomccvr_ce_hud_tests tests/haloce_hud_tests.cpp src/dll/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_hud_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_hud_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_hud_tests PRIVATE minhook d3d11)
    add_test(NAME halomccvr_ce_hud_tests COMMAND halomccvr_ce_hud_tests)
    add_executable(halomccvr_ce_hud_layout_tests tests/haloce_hud_layout_tests.cpp src/dll/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_hud_layout_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_hud_layout_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_hud_layout_tests PRIVATE minhook d3d11)
    add_test(NAME halomccvr_ce_hud_layout_tests COMMAND halomccvr_ce_hud_layout_tests)
    add_executable(halomccvr_ce_reticle_redirect_tests tests/haloce_reticle_redirect_tests.cpp)
    target_compile_definitions(halomccvr_ce_reticle_redirect_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_reticle_redirect_tests PRIVATE d3d11)
    add_test(NAME halomccvr_ce_reticle_redirect_tests COMMAND halomccvr_ce_reticle_redirect_tests)
    add_executable(halomccvr_ce_reticle_pixels_tests tests/haloce_reticle_pixels_tests.cpp)
    target_compile_definitions(halomccvr_ce_reticle_pixels_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_reticle_pixels_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_ce_reticle_pixels_tests COMMAND halomccvr_ce_reticle_pixels_tests)
    add_executable(halomccvr_ce_hud_target_tests tests/haloce_hud_target_tests.cpp)
    target_include_directories(halomccvr_ce_hud_target_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_hud_target_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_hud_target_tests PRIVATE d3d11)
    add_test(NAME halomccvr_ce_hud_target_tests COMMAND halomccvr_ce_hud_target_tests)
    add_executable(halomccvr_ce_controls_tests tests/haloce_controls_tests.cpp)
    target_include_directories(halomccvr_ce_controls_tests PRIVATE src)
    add_test(NAME halomccvr_ce_controls_tests COMMAND halomccvr_ce_controls_tests)
    add_executable(halomccvr_ce_controls_runtime_tests tests/haloce_controls_runtime_tests.cpp
        src/dll/physical_crouch_camera.cpp src/dll/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_controls_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_controls_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_controls_runtime_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_controls_runtime_tests COMMAND halomccvr_ce_controls_runtime_tests)
    add_executable(halomccvr_ce_comfort_tests tests/haloce_comfort_tests.cpp src/dll/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_comfort_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_comfort_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_comfort_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_comfort_tests COMMAND halomccvr_ce_comfort_tests)
    add_executable(halomccvr_ce_orientation_runtime_tests tests/haloce_orientation_runtime_tests.cpp
        src/dll/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_orientation_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_orientation_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_orientation_runtime_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_orientation_runtime_tests COMMAND halomccvr_ce_orientation_runtime_tests)
    add_executable(halomccvr_ce_unit_control_runtime_tests tests/haloce_unit_control_runtime_tests.cpp)
    target_include_directories(halomccvr_ce_unit_control_runtime_tests PRIVATE src/common src/dll "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_ce_unit_control_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_ce_unit_control_runtime_tests COMMAND halomccvr_ce_unit_control_runtime_tests)
    add_executable(halomccvr_ce_network_input_tests tests/haloce_network_input_tests.cpp)
    target_include_directories(halomccvr_ce_network_input_tests PRIVATE src/common src/dll "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_ce_network_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_ce_network_input_tests COMMAND halomccvr_ce_network_input_tests)
    add_executable(halomccvr_ce_first_person_runtime_tests tests/haloce_first_person_runtime_tests.cpp
        src/dll/haloce_native_bindings.cpp src/dll/ik.cpp)
    target_include_directories(halomccvr_ce_first_person_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_first_person_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_first_person_runtime_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_first_person_runtime_tests COMMAND halomccvr_ce_first_person_runtime_tests)
    add_executable(halomccvr_ce_muzzle_lifecycle_tests tests/haloce_muzzle_lifecycle_tests.cpp)
    target_include_directories(halomccvr_ce_muzzle_lifecycle_tests PRIVATE ${minhook_SOURCE_DIR}/include)
    target_compile_definitions(halomccvr_ce_muzzle_lifecycle_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_ce_muzzle_lifecycle_tests COMMAND halomccvr_ce_muzzle_lifecycle_tests)
    add_executable(halomccvr_reach_display_admission_tests tests/reach_display_admission_tests.cpp)
    add_test(NAME halomccvr_reach_display_admission_tests COMMAND halomccvr_reach_display_admission_tests)
    add_executable(manual_vr_recovery_tests tests/manual_vr_recovery_tests.cpp)
    target_compile_features(manual_vr_recovery_tests PRIVATE cxx_std_20)
    add_test(NAME manual_vr_recovery COMMAND manual_vr_recovery_tests)
    add_executable(halomccvr_title_reentry_probe_tests tests/title_reentry_probe_tests.cpp src/common/title_registry.cpp)
    target_compile_definitions(halomccvr_title_reentry_probe_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_title_reentry_probe_tests COMMAND halomccvr_title_reentry_probe_tests)
    add_executable(halomccvr_halo2_anniversary_cleanup_tests tests/halo2_anniversary_cleanup_tests.cpp)
    target_link_libraries(halomccvr_halo2_anniversary_cleanup_tests PRIVATE minhook)
    target_compile_definitions(halomccvr_halo2_anniversary_cleanup_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_anniversary_cleanup_tests COMMAND halomccvr_halo2_anniversary_cleanup_tests)
    add_executable(halomccvr_halo2_observer_cleanup_tests tests/halo2_observer_cleanup_tests.cpp)
    target_include_directories(halomccvr_halo2_observer_cleanup_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo2_observer_cleanup_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_observer_cleanup_tests COMMAND halomccvr_halo2_observer_cleanup_tests)
    add_executable(halomccvr_hud_extra_draws_tests tests/hud_extra_draws_tests.cpp)
    target_include_directories(halomccvr_hud_extra_draws_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_hud_extra_draws_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_hud_extra_draws_tests COMMAND halomccvr_hud_extra_draws_tests)

    add_executable(halomccvr_halo3_independent_shots_tests tests/halo3_independent_shots_tests.cpp)
    add_executable(halomccvr_halo3_collision_dispatch_tests tests/halo3_collision_dispatch_tests.cpp)
    add_executable(halomccvr_native_fault_probe_tests tests/native_fault_probe_tests.cpp)
    target_compile_definitions(halomccvr_halo3_collision_dispatch_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(halomccvr_native_fault_probe_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_collision_dispatch_tests COMMAND halomccvr_halo3_collision_dispatch_tests)
    add_test(NAME halomccvr_native_fault_probe_tests COMMAND halomccvr_native_fault_probe_tests)
    target_include_directories(halomccvr_halo3_independent_shots_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo3_independent_shots_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_options(halomccvr_halo3_independent_shots_tests PRIVATE /MAP)
    add_test(NAME halomccvr_halo3_independent_shots_tests COMMAND halomccvr_halo3_independent_shots_tests)
    add_executable(halomccvr_halo2_independent_shots_tests tests/halo2_independent_shots_tests.cpp)
    target_include_directories(halomccvr_halo2_independent_shots_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo2_independent_shots_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_independent_shots_tests COMMAND halomccvr_halo2_independent_shots_tests)
    add_executable(halomccvr_weapon_muzzle_tests tests/weapon_muzzle_tests.cpp)
    add_test(NAME halomccvr_weapon_muzzle_tests COMMAND halomccvr_weapon_muzzle_tests)
    add_executable(halomccvr_odst_muzzle_tests tests/odst_muzzle_tests.cpp)
    add_executable(halomccvr_odst_scope_tests tests/odst_scope_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_odst_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_odst_scope_tests COMMAND halomccvr_odst_scope_tests)
    add_executable(halomccvr_reach_scope_tests tests/reach_scope_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_reach_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_reach_scope_tests COMMAND halomccvr_reach_scope_tests)
    add_executable(halomccvr_halo4_scope_tests tests/halo4_scope_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_halo4_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_scope_tests COMMAND halomccvr_halo4_scope_tests)
    add_executable(halomccvr_halo2_scope_tests tests/halo2_scope_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_halo2_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_scope_tests COMMAND halomccvr_halo2_scope_tests)
    add_executable(halomccvr_reach_muzzle_tests tests/reach_muzzle_tests.cpp)
    add_executable(halomccvr_halo4_muzzle_tests tests/halo4_muzzle_tests.cpp)
    target_compile_definitions(halomccvr_halo4_muzzle_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(halomccvr_halo4_muzzle_tests PRIVATE ${minhook_SOURCE_DIR}/include)
    add_test(NAME halomccvr_halo4_muzzle_tests COMMAND halomccvr_halo4_muzzle_tests)
    add_executable(halomccvr_visual_hand_offset_tests tests/visual_hand_offset_tests.cpp src/common/config.cpp src/common/log.cpp src/dll/ik.cpp)
    target_compile_definitions(halomccvr_visual_hand_offset_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_visual_hand_offset_tests COMMAND halomccvr_visual_hand_offset_tests)
    add_executable(halomccvr_vehicle_turn_tests tests/vehicle_turn_tests.cpp src/common/config.cpp src/common/log.cpp)
    target_compile_definitions(halomccvr_vehicle_turn_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_turn_tests COMMAND halomccvr_vehicle_turn_tests)
    target_compile_definitions(halomccvr_reach_muzzle_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(halomccvr_reach_muzzle_tests PRIVATE ${minhook_SOURCE_DIR}/include)
    add_test(NAME halomccvr_reach_muzzle_tests COMMAND halomccvr_reach_muzzle_tests)
    target_compile_definitions(halomccvr_odst_muzzle_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(halomccvr_odst_muzzle_tests PRIVATE ${minhook_SOURCE_DIR}/include)
    add_test(NAME halomccvr_odst_muzzle_tests COMMAND halomccvr_odst_muzzle_tests)
    add_executable(halomccvr_halo3_muzzle_publication_tests tests/halo3_muzzle_publication_tests.cpp)
    target_compile_definitions(halomccvr_halo3_muzzle_publication_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_muzzle_publication_tests COMMAND halomccvr_halo3_muzzle_publication_tests)
    add_executable(halomccvr_menu_slider_tests tests/menu_slider_tests.cpp)
    target_link_libraries(halomccvr_menu_slider_tests PRIVATE imgui_lib)
    add_test(NAME halomccvr_menu_slider_tests COMMAND halomccvr_menu_slider_tests)
    add_executable(halomccvr_game_menu_pointer_tests tests/game_menu_pointer_tests.cpp src/common/config.cpp)
    target_include_directories(halomccvr_game_menu_pointer_tests PRIVATE src/common src/dll "${openxr_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_game_menu_pointer_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_game_menu_pointer_tests COMMAND halomccvr_game_menu_pointer_tests)
endif()

# Stage an unaccepted local candidate only. Release-specific hashes and metadata
# must never be copied into a newly rebuilt package.
install(TARGETS halo3xr halo3xr_launcher
    RUNTIME DESTINATION .
    COMPONENT dist)
install(FILES
    LICENSE
    installer/MANUAL-README.txt
    installer/THIRD-PARTY-LICENSES.txt
    DESTINATION .
    COMPONENT dist)
install(DIRECTORY src/launcher/assets/ DESTINATION assets COMPONENT dist)
