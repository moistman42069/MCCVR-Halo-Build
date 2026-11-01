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
# Development diagnostics and headless-test hooks. A config flag is not a sufficient guard, so anything
# that answers a question rather than playing the game is compiled out of a player build. OFF = shipping; the "dev" preset turns it on; package-candidate.ps1 refuses it.
option(HALOMCCVR_DEV_DIAGNOSTICS
    "Compile in development diagnostics and headless-test hooks (never packaged)" OFF)
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

# vr.h exposes the sampled OpenXR head pose in aim receipts. Keep that public
# header usable by the DLL's focused fixtures as well as the runtime target.
include_directories("${openxr_SOURCE_DIR}/include")

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
include(cmake/MenuAssets.cmake)
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

add_executable(menu_style_preview EXCLUDE_FROM_ALL tools/menu-style-preview.cpp)
target_include_directories(menu_style_preview PRIVATE src/dll "${HALOMCCVR_GENERATED_DIR}")
target_compile_definitions(menu_style_preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(menu_style_preview PRIVATE imgui_lib d3d11 dxgi d3dcompiler)

add_library(halo3xr SHARED
    ${HALOMCCVR_GENERATED_DIR}/smaa_resources.rc
    src/common/log.cpp
    src/common/config.cpp
    src/common/input_logic.cpp
    src/common/scope_logic.cpp
    src/common/title_registry.cpp
    src/common/title_runtime_state.cpp
    src/common/vehicle_camera_autoadd.cpp
    src/common/vehicle_camera_file.cpp
    src/common/vehicle_camera_select.cpp
    src/common/vehicle_camera_store.cpp
    src/dll/dllmain.cpp
    src/dll/native_fault_probe.cpp
    src/dll/telemetry_recorder.cpp
    src/dll/d3d11_hook.cpp
    src/dll/vr.cpp
    src/dll/weapon_accessory_renderer.cpp
    src/dll/native_reload_policy.cpp
    src/dll/host_unload_gate.cpp
    src/dll/native_vehicle_first_person.cpp
    src/dll/menu.cpp
    src/dll/native_menu_pointer.cpp
    src/dll/sigscan.cpp
    src/dll/game.cpp
    src/dll/ik.cpp
    src/dll/input.cpp
    src/dll/seat_exit_witness.cpp
    src/dll/roomscale.cpp
    src/dll/sight_layer.cpp
    src/dll/dev_pad.cpp
    src/dll/title_adapter.cpp
    src/dll/title_image_guard.cpp
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
    src/dll/haloce_head_relative.cpp
    src/dll/haloce_unit_control.cpp
    src/dll/haloce_network_input.cpp
    src/dll/halo2_anniversary_stereo.cpp
    src/dll/halo4_adapter.cpp
    src/dll/halo4_cold_observation.cpp)
if(HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA)
    target_sources(halo3xr PRIVATE src/dll/halo4_restoration.asm)
endif()
# Halo 3 / ODST / Halo 4 direct drive (legacy_direct_drive): the control-update detours.
target_sources(halo3xr PRIVATE src/dll/legacy_player_control_hook.asm)
# Where the direct drives' code is in the loaded game DLL (src/common/drive_sites.h, drive_resolve.h): the DLL's
# side of the resolution, and the address-verification library it uses (the TLS directory read, the watchdog, the
# reporter).
target_sources(halo3xr PRIVATE
    src/dll/drive_cascade.cpp
    src/common/addrcascade/AddressCascade.cpp)
# A title install's searches, saved per build of the game DLL for the next launch (install_search_cache.h).
target_sources(halo3xr PRIVATE src/dll/install_search_cache.cpp)
# Halo 2 direct drive (halo2_direct_drive.inl): its control-update detour, with the observer it reads.
if(HALOMCCVR_HALO2_OBSERVER_6DOF)
    target_sources(halo3xr PRIVATE src/dll/halo2_player_control_hook.asm)
endif()
# CE's saber bone-convert hook enters through a register-preserving thunk (haloce_first_person.cpp).
target_sources(halo3xr PRIVATE src/dll/haloce_skin_convert_thunk.asm)
set_target_properties(halo3xr PROPERTIES OUTPUT_NAME "HaloMCCVR")
target_include_directories(halo3xr PRIVATE
    ${HALOMCCVR_GENERATED_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/Textures)
if(HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE)
    target_sources(halo3xr PRIVATE
        src/dll/reach_render_candidate.cpp
        src/dll/reach_render_preflight.cpp
        src/dll/reach_aim_provider.cpp
        src/dll/reach_player_control_hook.asm
        src/dll/reach_render_gate_thunk.asm)
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
    HALOMCCVR_DEV_DIAGNOSTICS=$<BOOL:${HALOMCCVR_DEV_DIAGNOSTICS}>
    # Fault injection for the direct drives' address resolution (config key drive_fault): dev builds only. A player
    # build leaves it undefined, and the forcing code is not compiled (addrcascade::fault, drive_resolve::Fault).
    $<$<BOOL:${HALOMCCVR_DEV_DIAGNOSTICS}>:ADDRCASCADE_FAULTS=1>
    HALOMCCVR_HALO2_ANNIVERSARY_STEREO=$<BOOL:${HALOMCCVR_HALO2_ANNIVERSARY_STEREO}>
    HALOMCCVR_BUILD_COMMIT="${HALOMCCVR_BUILD_COMMIT}"
    HALOMCCVR_BUILD_ODST="${HALOMCCVR_BUILD_ODST}"
    HALOMCCVR_BUILD_REACH="${HALOMCCVR_BUILD_REACH}"
    HALOMCCVR_BUILD_REACH_RENDER="${HALOMCCVR_BUILD_REACH_RENDER}"
    HALOMCCVR_BUILD_HALO4="${HALOMCCVR_BUILD_HALO4}"
    HALOMCCVR_BUILD_HALO2="${HALOMCCVR_BUILD_HALO2}")
target_link_libraries(halo3xr PRIVATE
    openxr_loader minhook imgui_lib
    d3d11 dxgi d3dcompiler gdi32 advapi32 shell32 ole32)
target_sources(halo3xr PRIVATE src/dll/native_subtitles.cpp)
target_sources(halo3xr PRIVATE src/dll/physical_crouch_camera.cpp)
target_sources(halo3xr PRIVATE src/dll/native_vr_actions.cpp)
# The bridge's native readers are entered through register-preserving thunks (LTCG callers keep
# volatile registers live across those leaf calls; see the .asm).
target_sources(halo3xr PRIVATE src/dll/native_vr_reader_thunks.asm)
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
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    # --headless, the unattended launch, is a headless-test hook: dev builds only.
    HALOMCCVR_DEV_DIAGNOSTICS=$<BOOL:${HALOMCCVR_DEV_DIAGNOSTICS}>)
# IApplicationActivationManager: the Microsoft Store edition is a packaged app
# and can only be started through packaged activation.
target_link_libraries(halo3xr_launcher PRIVATE ole32 bcrypt advapi32 winhttp crypt32 shell32 uxtheme)
# The launcher runs with the user's own rights. The game it starts inherits its token, and in an elevated game
# the OpenXR loader ignores XR_RUNTIME_JSON: a headset runtime chosen with that variable is replaced by the
# machine's registered one and VR never starts. An install into a folder the user cannot write to says so and
# asks for the launcher to be run as administrator for that install (installer.cpp).
if(MSVC)
    target_link_options(halo3xr_launcher PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
endif()

# THE PROJECT RECLAIMER RELAY (experimental; packaged beside HaloMCCVR.dll by the dist install below).
# Project Reclaimer runs MCC's halo3.dll in a worker process two generations below the one a launcher can start,
# so HaloMCCVRReclaimerRelay.dll rides its process starts and loads HaloMCCVR.dll into the engine worker before
# that worker runs (src/relay/reclaimer_relay.cpp). The launcher uses it when it sits beside a Project Reclaimer
# client and no MCC install is around it.
add_library(halomccvr_reclaimer_relay SHARED src/relay/reclaimer_relay.cpp)
set_target_properties(halomccvr_reclaimer_relay PROPERTIES OUTPUT_NAME "HaloMCCVRReclaimerRelay")
target_include_directories(halomccvr_reclaimer_relay PRIVATE src/common "${minhook_SOURCE_DIR}/include")
target_compile_definitions(halomccvr_reclaimer_relay PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(halomccvr_reclaimer_relay PRIVATE minhook)

# The launcher for a Project Reclaimer folder: the same sources WITHOUT the administrator manifest. The MCC
# launcher asks for administrator to install into the game's folder; a game it starts is then elevated, and the
# OpenXR loader ignores XR_RUNTIME_JSON in an elevated process, so a player who selects their runtime with that
# variable got the machine's registered one instead (seen: Oculus with no headset, SteamVR running). Project
# Reclaimer needs no install step, so its launcher runs as the player. It never shows the MCC menu.
add_executable(halo3xr_reclaimer_launcher WIN32 src/launcher/launcher.cpp
    src/launcher/installer.cpp src/launcher/menu.cpp src/launcher/updater.cpp)
set_target_properties(halo3xr_reclaimer_launcher PROPERTIES OUTPUT_NAME "HaloMCCVRReclaimerLauncher")
target_compile_definitions(halo3xr_reclaimer_launcher PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    HALOMCCVR_RECLAIMER_LAUNCHER=1
    HALOMCCVR_DEV_DIAGNOSTICS=$<BOOL:${HALOMCCVR_DEV_DIAGNOSTICS}>)
target_link_libraries(halo3xr_reclaimer_launcher PRIVATE ole32 bcrypt advapi32 winhttp crypt32 shell32 uxtheme)

# THE FLAT PROBE (dev preset only; never packaged -- package-candidate.ps1 refuses a dev cache and copies named
# files only). HaloMCCFlatProbe.dll is the AS SEEN control arm: stock flat Halo 3 measured with the same code as the
# VR witness (src/common/as_seen_logic.h). OBSERVE ONLY: no OpenXR, no stereo, no presentation change, no writes.
# Loaded alone by the dev launcher's --flat-probe (tools/Install-FlatProbe.ps1 stages it).
if(HALOMCCVR_DEV_DIAGNOSTICS)
    add_library(halomcc_flat_probe SHARED
        src/probe/flat_probe.cpp
        src/common/log.cpp
        src/dll/sigscan.cpp)
    set_target_properties(halomcc_flat_probe PROPERTIES OUTPUT_NAME "HaloMCCFlatProbe")
    # minhook's own sources carry minhook_lifecycle.c (MCCVR_DisableHookForRetirement), see above.
    target_include_directories(halomcc_flat_probe PRIVATE src/common src/dll "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomcc_flat_probe PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_BUILD_COMMIT="${HALOMCCVR_BUILD_COMMIT}")
    target_link_libraries(halomcc_flat_probe PRIVATE minhook d3d11 dxgi)
endif()

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
    add_executable(authored_grab_zone_tests tests/authored_grab_zone_tests.cpp)
    add_test(NAME authored_grab_zone_tests COMMAND authored_grab_zone_tests)
    add_executable(halo3_live_hud_tests tests/halo3_live_hud_tests.cpp)
    add_test(NAME halo3_live_hud_tests COMMAND halo3_live_hud_tests)
    add_executable(halomccvr_post_effect_policy_tests tests/post_effect_policy_tests.cpp)
    add_test(NAME halomccvr_post_effect_policy_tests COMMAND halomccvr_post_effect_policy_tests)
    add_executable(halomccvr_bloom_override_tests tests/bloom_override_tests.cpp src/dll/bloom_override.cpp src/common/config.cpp)
    target_compile_definitions(halomccvr_bloom_override_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_bloom_override_tests PRIVATE d3d11)
    add_test(NAME halomccvr_bloom_override_tests COMMAND halomccvr_bloom_override_tests)
    add_executable(halomccvr_muzzle_suppression_tests tests/muzzle_suppression_tests.cpp)
    target_compile_definitions(halomccvr_muzzle_suppression_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_muzzle_suppression_tests COMMAND halomccvr_muzzle_suppression_tests)
    add_executable(halomccvr_physical_crouch_tests tests/physical_crouch_tests.cpp)
    add_executable(halomccvr_hud_reveal_tests tests/hud_reveal_tests.cpp)
    add_test(NAME halomccvr_hud_reveal_tests COMMAND halomccvr_hud_reveal_tests)
    add_executable(body_visibility_switch_tests tests/body_visibility_switch_tests.cpp)
    add_test(NAME body_visibility_switch COMMAND body_visibility_switch_tests)
    add_executable(halomccvr_halo4_arm_ik_tests tests/halo4_arm_ik_tests.cpp src/dll/ik.cpp)
    target_compile_definitions(halomccvr_halo4_arm_ik_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_arm_ik_tests COMMAND halomccvr_halo4_arm_ik_tests)
    add_executable(halomccvr_halo4_body_ik_tests tests/halo4_body_ik_logic_tests.cpp)
    add_executable(finger_joint_identity_tests tests/finger_joint_identity_tests.cpp)
    target_compile_features(finger_joint_identity_tests PRIVATE cxx_std_20)
    add_test(NAME finger_joint_identity COMMAND finger_joint_identity_tests)
    add_executable(window_focus_policy_tests tests/window_focus_policy_tests.cpp)
    target_compile_definitions(window_focus_policy_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME window_focus_policy COMMAND window_focus_policy_tests)
    target_compile_definitions(halomccvr_halo4_body_ik_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_body_ik_tests COMMAND halomccvr_halo4_body_ik_tests)
    add_executable(reach_contact_backend_tests tests/reach_contact_backend_tests.cpp)
    target_compile_features(reach_contact_backend_tests PRIVATE cxx_std_20)
    target_compile_options(reach_contact_backend_tests PRIVATE /W4 /EHsc)
    add_test(NAME reach_contact_backend COMMAND reach_contact_backend_tests)
    add_executable(halo4_contact_backend_tests tests/halo4_contact_backend_tests.cpp)
    target_compile_features(halo4_contact_backend_tests PRIVATE cxx_std_20)
    target_compile_options(halo4_contact_backend_tests PRIVATE /W4 /EHsc)
    add_test(NAME halo4_contact_backend COMMAND halo4_contact_backend_tests)
    add_executable(native_vr_actions_runtime_tests tests/native_vr_actions_runtime_tests.cpp
        src/dll/native_vr_reader_thunks.asm)
    target_include_directories(native_vr_actions_runtime_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(native_vr_actions_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME native_vr_actions_runtime COMMAND native_vr_actions_runtime_tests)
    # The reader thunks must keep every volatile register but rax across the C++ hook (an asm probe
    # plays the LTCG caller; the hook clobbers everything).
    add_executable(native_vr_reader_thunk_tests tests/native_vr_reader_thunk_tests.cpp
        tests/native_vr_reader_thunk_probe.asm src/dll/native_vr_reader_thunks.asm)
    add_test(NAME native_vr_reader_thunks COMMAND native_vr_reader_thunk_tests)
    # HaloMCCVR.log keeps the previous launch as .prev even while another process holds the old log.
    add_executable(halomccvr_log_rotation_tests tests/log_rotation_tests.cpp src/common/log.cpp)
    add_test(NAME halomccvr_log_rotation COMMAND halomccvr_log_rotation_tests)
    add_test(NAME halomccvr_physical_crouch_tests COMMAND halomccvr_physical_crouch_tests)
    add_executable(halomccvr_hand_smoothing_tests tests/hand_smoothing_tests.cpp src/common/config.cpp)
    target_compile_definitions(halomccvr_hand_smoothing_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_hand_smoothing_tests COMMAND halomccvr_hand_smoothing_tests)
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
    # A byte-pattern proof must read the GAME's code where this mod has hooked it (sigscan.h): real MinHook
    # hooks on synthetic code, the production scanner, and the control that shows a hooked head reads our jump.
    add_executable(halomccvr_proof_after_hook_tests tests/proof_after_hook_tests.cpp src/dll/sigscan.cpp)
    target_compile_definitions(halomccvr_proof_after_hook_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_proof_after_hook_tests PRIVATE minhook)
    add_test(NAME halomccvr_proof_after_hook_tests COMMAND halomccvr_proof_after_hook_tests)
    # The scanner's memory of one loaded image (sigscan.h, ImageMemoScope): the same answers as the search.
    add_executable(halomccvr_sigscan_memo_tests tests/sigscan_memo_tests.cpp src/dll/sigscan.cpp)
    target_compile_definitions(halomccvr_sigscan_memo_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_sigscan_memo_tests COMMAND halomccvr_sigscan_memo_tests)
    # The install's searches saved for the next launch (install_search_cache.h): the file, the build identity, and
    # every saved answer checked as the scanner checks one it remembers, on synthetic images and the test's own.
    add_executable(halomccvr_install_search_cache_tests tests/install_search_cache_tests.cpp
        src/dll/install_search_cache.cpp src/dll/sigscan.cpp)
    target_compile_definitions(halomccvr_install_search_cache_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_install_search_cache_tests PRIVATE minhook)
    add_test(NAME halomccvr_install_search_cache_tests COMMAND halomccvr_install_search_cache_tests)
    # A per-frame reader of a title's image stops at the title's unload (title_image_guard.h): a real DLL is
    # loaded and freed under a reader thread.
    add_executable(halomccvr_title_image_guard_tests tests/title_image_guard_tests.cpp src/dll/title_image_guard.cpp)
    target_compile_definitions(halomccvr_title_image_guard_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_title_image_guard_tests COMMAND halomccvr_title_image_guard_tests)
    add_executable(halomccvr_host_unload_gate_tests tests/host_unload_gate_tests.cpp)
    add_test(NAME halomccvr_host_unload_gate_tests COMMAND halomccvr_host_unload_gate_tests)
    add_executable(halomccvr_title_build_identity_tests tests/title_build_identity_tests.cpp)
    add_test(NAME halomccvr_title_build_identity_tests COMMAND halomccvr_title_build_identity_tests)
    add_executable(halomccvr_reclaimer_host_logic_tests tests/reclaimer_host_logic_tests.cpp)
    add_test(NAME halomccvr_reclaimer_host_logic_tests COMMAND halomccvr_reclaimer_host_logic_tests)
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
        tests/roomscale_recenter_tests.cpp
        tests/roomscale_admission_tests.cpp
        tests/director_camera_tests.cpp
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
    # Merged donor test content: tests/core_tests.cpp's ReachRollbackGate
    # reach-cleanup block compiles to a ~1.05 MiB single frame (the donor's own
    # suite compiled the same block at ~850 KiB), which overflows the 1 MiB
    # MSVC default reserve before any test runs. Give the harness explicit
    # headroom; no assertions change.
    if(MSVC)
        target_link_options(halomccvr_core_tests PRIVATE "/STACK:4194304")
    endif()
    add_test(NAME halomccvr_core_tests COMMAND halomccvr_core_tests)
    # Exercise shipping XInput/OpenXR haptic functions with captured XR output.
    # This also checks the real title masks, ownership and short-pulse retention.
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(HALOMCCVR_PACKAGE_PYTHON "${Python3_EXECUTABLE}"
        CACHE FILEPATH "Python interpreter for package analyser validation" FORCE)
    add_executable(halomccvr_virtual_stock_tests tests/virtual_stock_tests.cpp)
    target_include_directories(halomccvr_virtual_stock_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_virtual_stock_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_virtual_stock_tests COMMAND halomccvr_virtual_stock_tests)
    # Two-Handed Lab tranche 1: dependency-light PURE anchor/authority/damping
    # logic. Header-only; no runtime integration yet.
    add_executable(halomccvr_two_hand_lab_tests tests/two_hand_lab_logic_tests.cpp)
    target_include_directories(halomccvr_two_hand_lab_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_two_hand_lab_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_two_hand_lab_tests COMMAND halomccvr_two_hand_lab_tests)
    # Persistent support grip (T4 foundation): pure policy headers only, no
    # runtime wiring yet. Title-aware owner identity, tri-state evidence,
    # durable relationship/writer policy, pending-acquisition freshness and
    # the presented-ray qualification rules. The suite links the existing
    # header-only hold/admission helpers so the acquisition-order case steps
    # the production equation.
    add_executable(halomccvr_support_grip_logic_tests
        tests/support_grip_logic_tests.cpp src/common/input_logic.cpp)
    target_include_directories(halomccvr_support_grip_logic_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_support_grip_logic_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_support_grip_logic_tests COMMAND halomccvr_support_grip_logic_tests)
    # Support-grab acquisition predicate: acquisition zone independent of the
    # support wrist orientation.
    add_executable(halomccvr_support_grab_tests tests/support_grab_tests.cpp)
    target_include_directories(halomccvr_support_grab_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_support_grab_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_support_grab_tests COMMAND halomccvr_support_grab_tests)
    # Halo 4 on-foot owner-evidence fallback: stage decision plus the per-pair
    # finalization policy (A003).
    add_executable(halomccvr_halo4_owner_evidence_tests tests/halo4_owner_evidence_tests.cpp)
    target_compile_definitions(halomccvr_halo4_owner_evidence_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_owner_evidence_tests COMMAND halomccvr_halo4_owner_evidence_tests)
    # Pure grab/release aim-continuity state machine (fixed 200 ms smoothstep
    # for both directions) shared by Standard and Plus Virtual Stock.
    # Header-only and dependency-free, so it gets its own deterministic
    # standalone suite.
    add_executable(halomccvr_virtual_stock_aim_continuity_tests
        tests/virtual_stock_aim_continuity_tests.cpp)
    target_include_directories(halomccvr_virtual_stock_aim_continuity_tests
        PRIVATE src/common)
    target_compile_definitions(halomccvr_virtual_stock_aim_continuity_tests
        PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_virtual_stock_aim_continuity_tests
        COMMAND halomccvr_virtual_stock_aim_continuity_tests)
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
    # End-to-end live seam for grab/release aim continuity: the test includes
    # the production runtime fragment against the generated production aim
    # solver, so the asserted latch-frame anchor contract, the 200 ms ease and
    # the release continuity are the shipping code, not a model of it. It
    # builds both Standard (rear reference 0/1) and Plus (rear reference 3)
    # inputs.
    add_executable(halomccvr_virtual_stock_aim_continuity_live_tests
        tests/virtual_stock_aim_continuity_live_tests.cpp
        "${HALOMCCVR_STOCK_FIXTURE}")
    target_include_directories(
        halomccvr_virtual_stock_aim_continuity_live_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include")
    add_test(NAME halomccvr_virtual_stock_aim_continuity_live_tests
        COMMAND halomccvr_virtual_stock_aim_continuity_live_tests)
    # Source-consistency guard for the aim-continuity invalidation topology:
    # the layer must be invalidated on exactly its nine exceptional lifecycle
    # paths (definition plus nine call sites) and never by the routine
    # prepared-frame retire (ResetPreparedFrame). Python stdlib only; the
    # --self-test mode exercises the checker logic on synthetic sources.
    add_test(NAME halomccvr_aim_continuity_lifecycle_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_aim_continuity_lifecycle.py"
            --self-test)
    # Source-consistency guard for the Two-Handed Lab patch wiring: the Lab is
    # hidden from normal sidebar navigation exactly like the Virtual Stock Lab
    # (no Advanced doorway, no Back button) while its page block and test
    # suites stay registered, presets are complete deterministic builder
    # states, the global reset also resets the Lab while the Virtual Stock
    # reset never touches it, and the status line uses numerical wording.
    # Python stdlib only; the --self-test mode exercises the checker logic on
    # synthetic sources and then checks the real tree.
    add_test(NAME halomccvr_two_hand_lab_patch_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_two_hand_lab_patch.py"
            --self-test)
    # Source-consistency guard for the persistent support grip port: the
    # config key stays default-off with its parse/save branches, the
    # applicability predicate is consulted by both the durable writer and the
    # assembly qualification, the PG-off latch branch keeps its base stores
    # verbatim, the reticle reseed and the owner-break invalidation pair stay
    # single, no donor leftover or retired sway symbol enters the new paths,
    # and the acquisition predicate stays gated behind the feature. Python
    # stdlib only; the --self-test mode exercises the checker logic on
    # synthetic sources and then checks the real tree.
    add_test(NAME halomccvr_persistent_support_grip_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_persistent_support_grip.py"
            --self-test)
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
    # SEATEXIT witness (dev-only vehicle-exit diagnostic): ring compression, the Use/Exit verdict, the drive
    # summary, the native Use record span, vehicle kinematics and the edge-log limiter are pure logic.
    add_executable(halomccvr_seat_exit_witness_tests tests/seat_exit_witness_tests.cpp)
    target_compile_features(halomccvr_seat_exit_witness_tests PRIVATE cxx_std_20)
    target_compile_definitions(halomccvr_seat_exit_witness_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_seat_exit_witness_tests COMMAND halomccvr_seat_exit_witness_tests)
    add_executable(halomccvr_weapon_interaction_tests tests/weapon_interaction_tests.cpp)
    add_executable(halomccvr_weapon_defaults_tests tests/weapon_defaults_tests.cpp)
    add_test(NAME halomccvr_weapon_defaults_tests COMMAND halomccvr_weapon_defaults_tests)
    add_executable(halomccvr_native_reload_tests tests/native_reload_policy_tests.cpp src/common/config.cpp)
    add_executable(halomccvr_native_vehicle_fp_tests tests/native_vehicle_first_person_tests.cpp src/common/config.cpp)
    target_include_directories(halomccvr_native_vehicle_fp_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_native_vehicle_fp_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_native_vehicle_fp_tests COMMAND halomccvr_native_vehicle_fp_tests)
    # The same production detours with the dev-only seat-eye and first-person witnesses compiled in (the
    # target above is the player-build flavour).
    add_executable(halomccvr_native_vehicle_fp_dev_tests tests/native_vehicle_first_person_tests.cpp src/common/config.cpp)
    target_include_directories(halomccvr_native_vehicle_fp_dev_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_native_vehicle_fp_dev_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_DEV_DIAGNOSTICS=1)
    add_test(NAME halomccvr_native_vehicle_fp_dev_tests COMMAND halomccvr_native_vehicle_fp_dev_tests)
    add_executable(halomccvr_odst_seat_lifetime_tests tests/odst_seat_lifetime_tests.cpp)
    target_compile_definitions(halomccvr_odst_seat_lifetime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_odst_seat_lifetime_tests COMMAND halomccvr_odst_seat_lifetime_tests)
    add_executable(halomccvr_halo3_seat_lifetime_tests tests/halo3_seat_lifetime_tests.cpp)
    target_compile_definitions(halomccvr_halo3_seat_lifetime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_seat_lifetime_tests COMMAND halomccvr_halo3_seat_lifetime_tests)
    add_executable(halomccvr_seat_view_tests tests/seat_view_tests.cpp)
    target_compile_definitions(halomccvr_seat_view_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_seat_view_tests COMMAND halomccvr_seat_view_tests)
    # The production install / sync / teardown of the Halo 3 seat view (src/dll/halo3_seat_view.inl) over a
    # fixture module whose director call sites are run. Player and dev flavour (the dev A/B key).
    add_executable(halomccvr_halo3_seat_view_tests tests/halo3_seat_view_tests.cpp)
    target_compile_definitions(halomccvr_halo3_seat_view_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_seat_view_tests COMMAND halomccvr_halo3_seat_view_tests)
    add_executable(halomccvr_halo3_seat_view_dev_tests tests/halo3_seat_view_tests.cpp)
    target_compile_definitions(halomccvr_halo3_seat_view_dev_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_DEV_DIAGNOSTICS=1)
    add_test(NAME halomccvr_halo3_seat_view_dev_tests COMMAND halomccvr_halo3_seat_view_dev_tests)
    # Vehicle cameras: the small JSON reader for hand-edited files (src/common/json_lite.h) and the camera
    # file itself -- model, reader, writer and vehicle matching (src/common/vehicle_camera_file.h). Pure logic.
    add_executable(halomccvr_json_lite_tests tests/json_lite_tests.cpp)
    target_compile_options(halomccvr_json_lite_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_json_lite_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_json_lite_tests COMMAND halomccvr_json_lite_tests)
    add_executable(halomccvr_vehicle_camera_file_tests tests/vehicle_camera_file_tests.cpp
        src/common/vehicle_camera_file.cpp)
    target_compile_options(halomccvr_vehicle_camera_file_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_camera_file_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_camera_file_tests COMMAND halomccvr_vehicle_camera_file_tests)
    # Where a camera sits: the offset, the tracked rotations and the leash in the games' space
    # (src/common/vehicle_camera_frame_logic.h).
    add_executable(halomccvr_vehicle_camera_frame_tests tests/vehicle_camera_frame_tests.cpp)
    target_compile_options(halomccvr_vehicle_camera_frame_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_camera_frame_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_camera_frame_tests COMMAND halomccvr_vehicle_camera_frame_tests)
    # Which camera is in use: the entry for a seat, stepping, what a seat remembers, a reloaded file
    # (src/common/vehicle_camera_select.h).
    add_executable(halomccvr_vehicle_camera_select_tests tests/vehicle_camera_select_tests.cpp
        src/common/vehicle_camera_select.cpp src/common/vehicle_camera_file.cpp)
    target_compile_options(halomccvr_vehicle_camera_select_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_camera_select_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_camera_select_tests COMMAND halomccvr_vehicle_camera_select_tests)
    # A vehicle of its own in the player's camera file: what is added, and the text it is spliced into
    # (src/common/vehicle_camera_autoadd.h). Pure logic.
    add_executable(halomccvr_vehicle_camera_autoadd_tests tests/vehicle_camera_autoadd_tests.cpp
        src/common/vehicle_camera_autoadd.cpp src/common/vehicle_camera_file.cpp)
    target_compile_options(halomccvr_vehicle_camera_autoadd_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_camera_autoadd_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_camera_autoadd_tests COMMAND halomccvr_vehicle_camera_autoadd_tests)
    # The camera file on disk: written once when missing, added to and never rewritten, read again when edited
    # (src/common/vehicle_camera_store.h). Works in a folder of its own under the temp folder.
    add_executable(halomccvr_vehicle_camera_store_tests tests/vehicle_camera_store_tests.cpp
        src/common/vehicle_camera_store.cpp src/common/vehicle_camera_autoadd.cpp src/common/vehicle_camera_file.cpp)
    target_compile_options(halomccvr_vehicle_camera_store_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_camera_store_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_camera_store_tests COMMAND halomccvr_vehicle_camera_store_tests)
    # Where the camera in use is this frame: the selected camera laid onto the vehicle a game reports
    # (src/common/vehicle_camera_place_logic.h).
    add_executable(halomccvr_vehicle_camera_place_tests tests/vehicle_camera_place_tests.cpp
        src/common/vehicle_camera_select.cpp src/common/vehicle_camera_file.cpp)
    target_compile_options(halomccvr_vehicle_camera_place_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_camera_place_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_camera_place_tests COMMAND halomccvr_vehicle_camera_place_tests)
    # How a vehicle seat is controlled, apart from how the view turns: hand, stick or wheel, and the left
    # stick click's cycle (src/common/vehicle_control_logic.h).
    add_executable(halomccvr_vehicle_control_tests tests/vehicle_control_tests.cpp)
    target_compile_options(halomccvr_vehicle_control_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_control_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_control_tests COMMAND halomccvr_vehicle_control_tests)
    # A look-steered driver's hand aim refused behind and near the vehicle, in its own ground plane
    # (src/common/vehicle_aim_reject_logic.h).
    add_executable(halomccvr_vehicle_aim_reject_tests tests/vehicle_aim_reject_tests.cpp)
    target_compile_options(halomccvr_vehicle_aim_reject_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_aim_reject_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_aim_reject_tests COMMAND halomccvr_vehicle_aim_reject_tests)
    # Which present carries the game's picture: not a host's token surface, not another device than the
    # headset session's (src/common/present_owner_logic.h).
    add_executable(halomccvr_present_owner_tests tests/present_owner_tests.cpp)
    target_compile_options(halomccvr_present_owner_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_present_owner_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_present_owner_tests COMMAND halomccvr_present_owner_tests)
    # Which OpenXR API layers the loader put between the mod and the runtime, and the ones known to break it
    # (src/common/openxr_layer_report_logic.h).
    add_executable(halomccvr_openxr_layer_report_tests tests/openxr_layer_report_tests.cpp)
    target_compile_options(halomccvr_openxr_layer_report_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_openxr_layer_report_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_openxr_layer_report_tests COMMAND halomccvr_openxr_layer_report_tests)
    # The third-person boom: Halo 3's camera track as the game evaluates it, the flat game's look range, the
    # boom's angle from the stick and where it starts (src/common/vehicle_boom_logic.h).
    add_executable(halomccvr_vehicle_boom_tests tests/vehicle_boom_tests.cpp)
    target_compile_options(halomccvr_vehicle_boom_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_boom_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_boom_tests COMMAND halomccvr_vehicle_boom_tests)
    # The player's lag tuning laid over the camera file (src/common/vehicle_camera_tuning.h).
    add_executable(halomccvr_vehicle_camera_tuning_tests tests/vehicle_camera_tuning_tests.cpp)
    target_compile_options(halomccvr_vehicle_camera_tuning_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_camera_tuning_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_camera_tuning_tests COMMAND halomccvr_vehicle_camera_tuning_tests)
    # The vehicle camera note in the game's own HUD messages (src/common/hud_note_logic.h).
    add_executable(halomccvr_hud_note_tests tests/hud_note_tests.cpp)
    target_compile_options(halomccvr_hud_note_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_hud_note_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_hud_note_tests COMMAND halomccvr_hud_note_tests)
    # The vehicle camera buttons in a seat: taps, holds, the left stick click, and what reaches the game
    # (src/common/vehicle_seat_buttons.h).
    add_executable(halomccvr_vehicle_seat_buttons_tests tests/vehicle_seat_buttons_tests.cpp)
    target_compile_options(halomccvr_vehicle_seat_buttons_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_seat_buttons_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_seat_buttons_tests COMMAND halomccvr_vehicle_seat_buttons_tests)
    # The ring where the vehicle aims, beside the crosshair: its picture, colour, direction, range and size
    # (src/common/vehicle_aim_marker_logic.h).
    add_executable(halomccvr_vehicle_aim_marker_tests tests/vehicle_aim_marker_tests.cpp)
    target_compile_options(halomccvr_vehicle_aim_marker_tests PRIVATE /W4 /WX)
    target_compile_definitions(halomccvr_vehicle_aim_marker_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_aim_marker_tests COMMAND halomccvr_vehicle_aim_marker_tests)
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
    set(HALOMCCVR_GRIP_POSE_CAPTURE_FIXTURE "${HALOMCCVR_GENERATED_DIR}/grip_pose_capture_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_GRIP_POSE_CAPTURE_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_grip_pose_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_GRIP_POSE_CAPTURE_FIXTURE}"
        DEPENDS tools/generate_grip_pose_fixture.py tools/generate_haptics_runtime_fixture.py src/dll/vr.cpp
        VERBATIM)
    add_executable(halomccvr_grip_pose_capture_tests tests/grip_pose_capture_tests.cpp
        "${HALOMCCVR_GRIP_POSE_CAPTURE_FIXTURE}")
    target_include_directories(halomccvr_grip_pose_capture_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_grip_pose_capture_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_grip_pose_capture_tests COMMAND halomccvr_grip_pose_capture_tests)
    set(HALOMCCVR_AIM_POSE_FIXTURE "${HALOMCCVR_GENERATED_DIR}/aim_pose_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_AIM_POSE_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_aim_pose_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_AIM_POSE_FIXTURE}"
        DEPENDS tools/generate_aim_pose_fixture.py tools/generate_haptics_runtime_fixture.py
            src/common/virtual_stock_logic.h src/common/virtual_stock_diagnostic.h
            src/common/virtual_stock_settings.h src/common/virtual_stock_test_profiles.h
            src/common/virtual_stock_test_profiles_experiments.inc
            src/common/config.h src/dll/aim_pose_trace.h src/dll/vr.cpp
            src/dll/virtual_stock_aim.inl
        VERBATIM)
    add_executable(halomccvr_aim_pose_tests tests/aim_pose_tests.cpp
        "${HALOMCCVR_AIM_POSE_FIXTURE}")
    target_include_directories(halomccvr_aim_pose_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_aim_pose_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_aim_pose_tests COMMAND halomccvr_aim_pose_tests)
    # The XInput coherent-sample tests compile the same atomic packet,
    # seqlock reader, identity/freshness selector and solver-input mapping that
    # the shipping VR_GetAimPoseWithSupportProvenance path calls.
    add_executable(halomccvr_committed_aim_sample_tests
        tests/committed_aim_sample_tests.cpp "${HALOMCCVR_AIM_POSE_FIXTURE}")
    target_include_directories(halomccvr_committed_aim_sample_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_committed_aim_sample_tests PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_committed_aim_sample_tests
        COMMAND halomccvr_committed_aim_sample_tests)
    add_executable(halomccvr_aim_servo_identity_tests
        tests/aim_servo_identity_tests.cpp)
    target_include_directories(halomccvr_aim_servo_identity_tests PRIVATE
        src/common)
    target_compile_definitions(halomccvr_aim_servo_identity_tests PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_aim_servo_identity_tests
        COMMAND halomccvr_aim_servo_identity_tests)
    # Two-Handed Lab tranche 2A: live VS-off solver integration (runtime
    # settings snapshot, anchor matrix, offhand influence, soft agreement,
    # single-source D/S, diagnostics) against the same generated aim fixture.
    add_executable(halomccvr_two_hand_lab_solver_tests
        tests/two_hand_lab_solver_tests.cpp "${HALOMCCVR_AIM_POSE_FIXTURE}")
    target_include_directories(halomccvr_two_hand_lab_solver_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_two_hand_lab_solver_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_two_hand_lab_solver_tests COMMAND halomccvr_two_hand_lab_solver_tests)
    # Two-Handed Lab tranche 2B: live temporal experiment modes (production
    # fragment against the generated aim fixture) — Continuity200ms latch
    # law, constant/adaptive damping, mode exclusivity, resets, fail-open.
    add_executable(halomccvr_two_hand_lab_temporal_tests
        tests/two_hand_lab_temporal_tests.cpp "${HALOMCCVR_AIM_POSE_FIXTURE}")
    target_include_directories(halomccvr_two_hand_lab_temporal_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_two_hand_lab_temporal_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_two_hand_lab_temporal_tests COMMAND halomccvr_two_hand_lab_temporal_tests)
    set(HALOMCCVR_TELEMETRY_TEST_JSONL
        "${HALOMCCVR_GENERATED_DIR}/telemetry_recorder_test.jsonl")
    add_executable(halomccvr_telemetry_tests
        tests/telemetry_recorder_tests.cpp
        "${HALOMCCVR_AIM_POSE_FIXTURE}"
        src/dll/telemetry_recorder.cpp
        src/common/log.cpp)
    set(HALOMCCVR_TELEMETRY_ANALYSER_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/telemetry_analyser/analyse_mccvr_telemetry.py")
    set(HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_DIR
        "${HALOMCCVR_GENERATED_DIR}/telemetry_analyser_smoke")
    set(HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_SCRIPT
        "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_DIR}/TelemetryAnalyser/analyse_mccvr_telemetry.py")
    add_custom_command(
        OUTPUT "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_SCRIPT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_DIR}/TelemetryAnalyser"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${HALOMCCVR_TELEMETRY_ANALYSER_SOURCE}"
            "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_SCRIPT}"
        DEPENDS "${HALOMCCVR_TELEMETRY_ANALYSER_SOURCE}"
        VERBATIM)
    add_custom_target(halomccvr_telemetry_analyser_smoke_stage
        DEPENDS "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_SCRIPT}")
    add_dependencies(halomccvr_telemetry_tests
        halomccvr_telemetry_analyser_smoke_stage)
    target_include_directories(halomccvr_telemetry_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common src/dll)
    target_compile_definitions(halomccvr_telemetry_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_TELEMETRY_TESTING
        HALOMCCVR_BUILD_COMMIT="${HALOMCCVR_BUILD_COMMIT}")
    add_test(NAME halomccvr_telemetry_tests
        COMMAND halomccvr_telemetry_tests "${HALOMCCVR_TELEMETRY_TEST_JSONL}")
    set_tests_properties(halomccvr_telemetry_tests PROPERTIES
        FIXTURES_SETUP halomccvr_telemetry_jsonl)
    add_test(NAME halomccvr_telemetry_jsonl_tests
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_telemetry_jsonl.py"
            "${HALOMCCVR_TELEMETRY_TEST_JSONL}"
            --expect-fixture --self-test)
    set_tests_properties(halomccvr_telemetry_jsonl_tests PROPERTIES
        FIXTURES_REQUIRED halomccvr_telemetry_jsonl)
    add_test(NAME halomccvr_telemetry_analyser_smoke
        COMMAND halomccvr_telemetry_tests --analyser-smoke
            "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_DIR}"
            "${HALOMCCVR_TELEMETRY_TEST_JSONL}")
    set_tests_properties(halomccvr_telemetry_analyser_smoke PROPERTIES
        FIXTURES_REQUIRED halomccvr_telemetry_jsonl)
    add_executable(halomccvr_left_hand_h2_tests tests/left_hand_h2_tests.cpp)
    target_include_directories(halomccvr_left_hand_h2_tests PRIVATE src/common)
    add_test(NAME halomccvr_left_hand_h2_tests COMMAND halomccvr_left_hand_h2_tests)
    add_executable(halomccvr_left_hand_shared_tests tests/left_hand_shared_tests.cpp)
    target_include_directories(halomccvr_left_hand_shared_tests PRIVATE src/common)
    add_test(NAME halomccvr_left_hand_shared_tests COMMAND halomccvr_left_hand_shared_tests)
    add_executable(halomccvr_aim_solve_tests tests/aim_solve_tests.cpp)
    target_include_directories(halomccvr_aim_solve_tests PRIVATE src/common)
    add_test(NAME halomccvr_aim_solve_tests COMMAND halomccvr_aim_solve_tests)
    add_executable(halomccvr_seat_drive_policy_tests tests/seat_drive_policy_tests.cpp)
    target_include_directories(halomccvr_seat_drive_policy_tests PRIVATE src/common)
    add_test(NAME halomccvr_seat_drive_policy_tests COMMAND halomccvr_seat_drive_policy_tests)
    # Seat convergence for Halo CE and Halo 2 (src/common/seat_converge_logic.h): the seat's role from its flags
    # word or the vehicle's occupant handles, the verdict on the engine's camera answer, the stance key, the
    # line-versus-point measure and the engine's aiming-bounds clamp.
    add_executable(halomccvr_seat_converge_tests tests/seat_converge_tests.cpp)
    target_include_directories(halomccvr_seat_converge_tests PRIVATE src/common)
    add_test(NAME halomccvr_seat_converge_tests COMMAND halomccvr_seat_converge_tests)
    # Aiming a seat's gun through the point (src/common/seat_barrel_logic.h): the barrel's geometry learned from
    # the lines of the seat's own fires, and the aim for which the barrel's line runs through a point.
    add_executable(halomccvr_seat_barrel_tests tests/seat_barrel_tests.cpp)
    target_include_directories(halomccvr_seat_barrel_tests PRIVATE src/common)
    add_test(NAME halomccvr_seat_barrel_tests COMMAND halomccvr_seat_barrel_tests)
    # Reach's motion tracker follows the head (src/common/reach_head_relative_logic.h): the decode of the game's
    # own heading store and its two readers, and the two headset checks on the game's own placement arithmetic.
    add_executable(halomccvr_reach_head_relative_tests tests/reach_head_relative_tests.cpp)
    target_include_directories(halomccvr_reach_head_relative_tests PRIVATE src/common)
    add_test(NAME halomccvr_reach_head_relative_tests COMMAND halomccvr_reach_head_relative_tests)
    # Halo 3's player tags (src/common/halo3_player_tags_logic.h): which navpoints are teammates' tags, what each
    # setting does to one, and the positions named beside its signatures.
    add_executable(halomccvr_halo3_player_tags_tests tests/halo3_player_tags_tests.cpp)
    target_include_directories(halomccvr_halo3_player_tags_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo3_player_tags_tests COMMAND halomccvr_halo3_player_tags_tests)
    # Halo 4's motion tracker follows the head (src/common/halo4_head_relative_logic.h; a retail-derived lead, not
    # H4EK-verified): the decode of the three sites and its refusals, the proof the hook takes at run time before
    # it changes anything (agree, disagree, a reference that did not move, withdrawal), and the two headset checks
    # on the game's own placement arithmetic.
    add_executable(halomccvr_halo4_head_relative_tests tests/halo4_head_relative_tests.cpp)
    target_include_directories(halomccvr_halo4_head_relative_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo4_head_relative_tests COMMAND halomccvr_halo4_head_relative_tests)
    # Halo 4's sound listener follows the head (src/common/halo4_sound_listener_logic.h; a retail-derived lead, not
    # H4EK-verified): the decode of the four sites and its refusals, the value agreement and the proof the hook
    # takes at run time before it changes anything, what the listener is given on the game's own builder,
    # transform and mirror, every refusal, and the measurement the dev report prints.
    add_executable(halomccvr_halo4_sound_listener_tests tests/halo4_sound_listener_tests.cpp)
    target_include_directories(halomccvr_halo4_sound_listener_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo4_sound_listener_tests COMMAND halomccvr_halo4_sound_listener_tests)
    # Reach's navigation markers, the pure half (src/common/reach_nav_marker_logic.h). Geometry on the header's
    # own MODEL of the game's projection, not on the game: with the eye's field of view the model's marker is
    # on its point's pixel for each eye with the head turned and the gun swept; with the game's it is off by
    # the magnification; the field-of-view verdicts; the decoding of the projection's sites from byte patterns.
    add_executable(halomccvr_reach_nav_marker_tests tests/reach_nav_marker_tests.cpp)
    target_include_directories(halomccvr_reach_nav_marker_tests PRIVATE src/common)
    add_test(NAME halomccvr_reach_nav_marker_tests COMMAND halomccvr_reach_nav_marker_tests)
    # Head-relative HUD and sound, the shared pure half (src/common/head_relative_logic.h): the heading of a
    # forward vector, the game's forward with the head's yaw, the head sample's verdict, and the player's two
    # headset checks (gun swept: contacts stay put; head turned: contacts turn) as geometry.
    add_executable(halomccvr_head_relative_tests tests/head_relative_tests.cpp)
    target_include_directories(halomccvr_head_relative_tests PRIVATE src/common)
    add_test(NAME halomccvr_head_relative_tests COMMAND halomccvr_head_relative_tests)
    # Head-relative motion tracker, Halo CE (src/common/haloce_head_relative_logic.h; a retail-derived lead, not
    # HCEEK-verified): the decode of the nine sites and every refusal of it, the exact guard on the stored heading,
    # the two-stage proof taken at run time (provisional at rest, confirmed by a turn, withdrawal, no second
    # provisional pass), the head built from the frame's own context, and the headset check as geometry.
    add_executable(halomccvr_ce_head_relative_tests tests/haloce_head_relative_tests.cpp)
    target_include_directories(halomccvr_ce_head_relative_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_head_relative_tests COMMAND halomccvr_ce_head_relative_tests)
    # The same lane's PRODUCTION body (src/dll/haloce_head_relative.cpp) over a synthetic image built from the
    # header's patterns at addresses that are not retail's: the install proof through sig::Find and the function
    # table, the detour and its one write, every report line as formatted, the retirement and what a reinstall
    # keeps. Player flavour, then dev flavour (the 2-second line the first run is judged by).
    add_executable(halomccvr_ce_head_relative_runtime_tests tests/haloce_head_relative_runtime_tests.cpp
        src/dll/sigscan.cpp src/common/config.cpp)
    target_include_directories(halomccvr_ce_head_relative_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_head_relative_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_head_relative_runtime_tests PRIVATE minhook psapi)
    add_test(NAME halomccvr_ce_head_relative_runtime_tests COMMAND halomccvr_ce_head_relative_runtime_tests)
    add_executable(halomccvr_ce_head_relative_runtime_dev_tests tests/haloce_head_relative_runtime_tests.cpp
        src/dll/sigscan.cpp src/common/config.cpp)
    target_include_directories(halomccvr_ce_head_relative_runtime_dev_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_head_relative_runtime_dev_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_DEV_DIAGNOSTICS=1)
    target_link_libraries(halomccvr_ce_head_relative_runtime_dev_tests PRIVATE minhook psapi)
    add_test(NAME halomccvr_ce_head_relative_runtime_dev_tests COMMAND halomccvr_ce_head_relative_runtime_dev_tests)
    # The F8/F9 pitch trim (src/common/pitch_trim_logic.h): applied with stereo off, zero in stereo VR, and a key
    # press in stereo changes nothing (two stray F9 presses once put the reticle 4 degrees above every shot).
    add_executable(halomccvr_pitch_trim_tests tests/pitch_trim_tests.cpp)
    target_include_directories(halomccvr_pitch_trim_tests PRIVATE src/common)
    add_test(NAME halomccvr_pitch_trim_tests COMMAND halomccvr_pitch_trim_tests)
    # Mod hotkeys are bare keys (src/common/hotkey_logic.h): a key with Ctrl, Alt or a Windows key held is
    # another tool's chord (a replay-capture Ctrl+F9 once trimmed the view two degrees per clip).
    add_executable(halomccvr_hotkey_tests tests/hotkey_tests.cpp)
    target_include_directories(halomccvr_hotkey_tests PRIVATE src/common)
    add_test(NAME halomccvr_hotkey_tests COMMAND halomccvr_hotkey_tests)
    # Head-relative motion tracker, Halo 3 / ODST (src/common/halo3_head_relative_logic.h): the game's placement
    # arithmetic as read from halo3.dll, the head's heading substituted into it, every refusal, the signatures'
    # decode offsets, and the two headset checks as geometry.
    add_executable(halomccvr_halo3_head_relative_tests tests/halo3_head_relative_tests.cpp)
    target_include_directories(halomccvr_halo3_head_relative_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo3_head_relative_tests COMMAND halomccvr_halo3_head_relative_tests)
    # Halo 3 tag names (src/common/halo3_tag_names.h): the cache header as halo3.dll checks it, the name table in
    # both address layouts on synthetic files, every refusal of a name, the short name an unknown vehicle goes by,
    # the decode of the two signatures on images built from their own patterns, and the stock vehicles by tag path
    # (src/common/halo3_vehicle_logic.h) with what the rule makes of vehicles read offline from Sandtrap and HugeGrass.
    add_executable(halomccvr_halo3_tag_names_tests tests/halo3_tag_names_tests.cpp)
    target_include_directories(halomccvr_halo3_tag_names_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo3_tag_names_tests COMMAND halomccvr_halo3_tag_names_tests)
    # Halo 3 tag names at runtime (src/dll/halo3_tag_names.inl) on a synthetic halo3.dll image: the install's decode,
    # the cold worker reading each map once and proving it, a new map and a map ending under one generation, every
    # refusal logged once with backoff, the camera thread's verdicts, and the removal.
    add_executable(halomccvr_halo3_tag_names_runtime_tests tests/halo3_tag_names_runtime_tests.cpp)
    target_include_directories(halomccvr_halo3_tag_names_runtime_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo3_tag_names_runtime_tests COMMAND halomccvr_halo3_tag_names_runtime_tests)
    # Halo 3 string ids (src/common/halo3_string_ids.h): a map's strings on a synthetic table and buffer laid out as
    # retail's, every refusal, the text of global, map and unknown ids, and the decode of the three signatures.
    add_executable(halomccvr_halo3_string_ids_tests tests/halo3_string_ids_tests.cpp)
    target_include_directories(halomccvr_halo3_string_ids_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo3_string_ids_tests COMMAND halomccvr_halo3_string_ids_tests)
    # Halo 3 glass fade (src/common/halo3_glass_fade.h): which parameters a camera's fadeParts touches per blend mode,
    # read against the stock glass shaders, and what it does to each.
    add_executable(halomccvr_foreign_detour_tests tests/foreign_detour_tests.cpp)
    target_include_directories(halomccvr_foreign_detour_tests PRIVATE src/common)
    add_test(NAME halomccvr_foreign_detour_tests COMMAND halomccvr_foreign_detour_tests)
    add_executable(halomccvr_halo3_glass_fade_tests tests/halo3_glass_fade_tests.cpp)
    target_include_directories(halomccvr_halo3_glass_fade_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo3_glass_fade_tests COMMAND halomccvr_halo3_glass_fade_tests)
    # Halo 3: the motion tracker's blips move with the HUD height (src/common/halo3_tracker_hud_height_logic.h;
    # retail-derived and run): the decode of the five sites on an image built from the header's patterns and
    # every way it refuses (a missing site, a call elsewhere, a neighbouring field, a scale pair not beside its
    # origin), the positions inside each pattern, and the move itself with its refusals and its restore rule.
    add_executable(halomccvr_halo3_tracker_hud_height_tests tests/halo3_tracker_hud_height_tests.cpp)
    target_include_directories(halomccvr_halo3_tracker_hud_height_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo3_tracker_hud_height_tests COMMAND halomccvr_halo3_tracker_hud_height_tests)
    # Halo 2's motion tracker follows the head (src/common/halo2_head_relative_logic.h; a retail-derived lead, not
    # H2EK-verified): the decode of the four sites and its refusals, the proof the hook takes at run time before
    # it changes anything (agree, disagree, a reference that did not move, withdrawal), the game's placement
    # arithmetic as read from halo2.dll, and the two headset checks on it.
    add_executable(halomccvr_halo2_head_relative_tests tests/halo2_head_relative_tests.cpp)
    target_include_directories(halomccvr_halo2_head_relative_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo2_head_relative_tests COMMAND halomccvr_halo2_head_relative_tests)
    # The dev sound listener witness, the pure half (src/common/sound_listener_witness_logic.h, and per title
    # halo2_sound_listener_logic.h -- a retail-derived lead, not H2EK-verified -- and haloce_sound_listener_logic.h):
    # the angle between two directions, the window and its verdict (head / aim / together / the game's camera /
    # neither), the decode of Halo 2's listener sites and record with every refusal, and Halo CE's packet held
    # against the head the listener hook builds. The runtime that uses them exists in dev builds only.
    add_executable(halomccvr_sound_listener_witness_tests tests/sound_listener_witness_tests.cpp src/dll/ik.cpp)
    target_include_directories(halomccvr_sound_listener_witness_tests PRIVATE src/common)
    add_test(NAME halomccvr_sound_listener_witness_tests COMMAND halomccvr_sound_listener_witness_tests)
    # Gun-slaved seats (src/common/gun_slaved_seat_logic.h): seat flag bit 7, the occupant-aim binding, the servo's
    # feedback and lag gate, the gun-settle gate of the crosshair witness and the nod witness.
    add_executable(halomccvr_gun_slaved_seat_tests tests/gun_slaved_seat_tests.cpp)
    target_include_directories(halomccvr_gun_slaved_seat_tests PRIVATE src/common)
    add_test(NAME halomccvr_gun_slaved_seat_tests COMMAND halomccvr_gun_slaved_seat_tests)
    # The free camera with no player unit (src/common/aim_no_unit_logic.h): the death/respawn debounce, its
    # fail-open rules and the frame the aim servo composes while the player has no unit.
    add_executable(halomccvr_aim_no_unit_tests tests/aim_no_unit_tests.cpp)
    target_include_directories(halomccvr_aim_no_unit_tests PRIVATE src/common)
    add_test(NAME halomccvr_aim_no_unit_tests COMMAND halomccvr_aim_no_unit_tests)
    add_executable(halomccvr_seat_aim_frame_tests tests/seat_aim_frame_tests.cpp)
    target_include_directories(halomccvr_seat_aim_frame_tests PRIVATE src/common)
    add_test(NAME halomccvr_seat_aim_frame_tests COMMAND halomccvr_seat_aim_frame_tests)
    # Seat frame conversion (src/common/seat_frame_logic.h): every title's vehicle-up rebase R(u) and its inverse.
    add_executable(halomccvr_seat_frame_logic_tests tests/seat_frame_logic_tests.cpp)
    target_include_directories(halomccvr_seat_frame_logic_tests PRIVATE src/common)
    add_test(NAME halomccvr_seat_frame_logic_tests COMMAND halomccvr_seat_frame_logic_tests)
    # Seat frame readers (src/common/seat_frame_sites.h, src/dll/seat_frame_reader.h): the read over a synthetic
    # world per title, the case decision, and the live Bind over an executable fixture.
    add_executable(halomccvr_seat_frame_sites_tests tests/seat_frame_sites_tests.cpp src/dll/sigscan.cpp)
    target_compile_definitions(halomccvr_seat_frame_sites_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_seat_frame_sites_tests PRIVATE psapi)
    add_test(NAME halomccvr_seat_frame_sites_tests COMMAND halomccvr_seat_frame_sites_tests)
    # The production install / sync / teardown of the seat aim rebase skip (src/dll/seat_aim_world_frame.inl)
    # over a VirtualAlloc'd executable fixture: player flavour, then dev flavour (the A/B key and the witness).
    add_executable(halomccvr_seat_aim_world_frame_tests tests/seat_aim_world_frame_tests.cpp src/dll/sigscan.cpp)
    target_compile_definitions(halomccvr_seat_aim_world_frame_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_seat_aim_world_frame_tests PRIVATE psapi)
    add_test(NAME halomccvr_seat_aim_world_frame_tests COMMAND halomccvr_seat_aim_world_frame_tests)
    add_executable(halomccvr_seat_aim_world_frame_dev_tests tests/seat_aim_world_frame_tests.cpp src/dll/sigscan.cpp)
    target_compile_definitions(halomccvr_seat_aim_world_frame_dev_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_DEV_DIAGNOSTICS=1)
    target_link_libraries(halomccvr_seat_aim_world_frame_dev_tests PRIVATE psapi)
    add_test(NAME halomccvr_seat_aim_world_frame_dev_tests COMMAND halomccvr_seat_aim_world_frame_dev_tests)
    add_executable(halomccvr_converge_policy_tests tests/converge_policy_tests.cpp)
    target_include_directories(halomccvr_converge_policy_tests PRIVATE src/common)
    add_test(NAME halomccvr_converge_policy_tests COMMAND halomccvr_converge_policy_tests)
    # The seat limit reticle (src/common/seat_limit_reticle_logic.h): a seat's angular stops in the record's frame,
    # the clamp of the hand's aim through that frame and back, and the edge hysteresis.
    add_executable(halomccvr_seat_limit_reticle_tests tests/seat_limit_reticle_tests.cpp)
    target_include_directories(halomccvr_seat_limit_reticle_tests PRIVATE src/common)
    add_test(NAME halomccvr_seat_limit_reticle_tests COMMAND halomccvr_seat_limit_reticle_tests)
    # A look-steered land driver's gun band (src/common/vehicle_gun_band_logic.h): the barrels learned in
    # the hull frame (animated barrels refused), the firing assist's cap, and the clamp of the shot's elevation.
    add_executable(halomccvr_vehicle_gun_band_tests tests/vehicle_gun_band_tests.cpp)
    target_include_directories(halomccvr_vehicle_gun_band_tests PRIVATE src/common)
    add_test(NAME halomccvr_vehicle_gun_band_tests COMMAND halomccvr_vehicle_gun_band_tests)
    add_executable(halomccvr_assess_reticle_tests tests/assess_reticle_tests.cpp)
    target_include_directories(halomccvr_assess_reticle_tests PRIVATE src/common)
    add_test(NAME halomccvr_assess_reticle_tests COMMAND halomccvr_assess_reticle_tests)
    add_executable(halomccvr_shot_lag_tests tests/shot_lag_tests.cpp)
    target_include_directories(halomccvr_shot_lag_tests PRIVATE src/common)
    add_test(NAME halomccvr_shot_lag_tests COMMAND halomccvr_shot_lag_tests)
    # The written aim's lead (src/common/aim_lead_logic.h): a steady sweep written where it will be one lead
    # time on; a step, a gap, a stop and an aim at rest written unchanged; the cap; and the stated noise bound.
    add_executable(halomccvr_aim_lead_tests tests/aim_lead_tests.cpp)
    target_include_directories(halomccvr_aim_lead_tests PRIVATE src/common)
    add_test(NAME halomccvr_aim_lead_tests COMMAND halomccvr_aim_lead_tests)
    # The seated record upstream of replication (src/common/seat_upstream_logic.h): the stock query model
    # and its exact inverse, the learned velocity gain, the write in effect at a tick, the seated recomposition's
    # fail-closed rules and the stock-line witness.
    add_executable(halomccvr_seat_upstream_tests tests/seat_upstream_tests.cpp)
    target_include_directories(halomccvr_seat_upstream_tests PRIVATE src/common)
    add_test(NAME halomccvr_seat_upstream_tests COMMAND halomccvr_seat_upstream_tests)
    # The shot impact witness (src/common/shot_impact_logic.h, dev): where a round actually goes -- the
    # spawned object's proof, the native's turn, inherited velocity, the round's line and impact against P, the
    # reticle's map to the world, the trigger latch and the report lines.
    add_executable(halomccvr_shot_impact_tests tests/shot_impact_tests.cpp)
    target_include_directories(halomccvr_shot_impact_tests PRIVATE src/common)
    add_test(NAME halomccvr_shot_impact_tests COMMAND halomccvr_shot_impact_tests)
    # As seen (src/common/as_seen_logic.h, dev): an impact against the crosshair drawn in the first frame
    # that shows it, binned by strafe speed and range, with the clocks between -- shared by the VR witness and the flat
    # probe (HaloMCCFlatProbe.dll), so both hosts compute it identically.
    add_executable(halomccvr_as_seen_tests tests/as_seen_tests.cpp)
    target_include_directories(halomccvr_as_seen_tests PRIVATE src/common)
    add_test(NAME halomccvr_as_seen_tests COMMAND halomccvr_as_seen_tests)
    # Source-consistency guard for the seated converged claim: game.cpp assigns the claim from
    # converge::SeatedClaimDecide and nothing else, never clears sightSeatedConverged, and reads no natural-cause
    # symbol into the claim's facts. No behavioural test compiles game.cpp, so a natural-cause switch-off added
    # at the call site would otherwise pass every test. Python stdlib only; --self-test checks the checker.
    add_test(NAME halomccvr_seated_claim_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_seated_claim.py"
            --self-test)
    # The sight layer's one clock: sight_layer.cpp reads only Sight_NowMs()/Sight_NowUs(), its ray
    # publications stamp themselves (never a caller's sampleMs), and every trace driver passes Sight_NowMs() to
    # Sight_TraceDue. A GetTickCount64 stamp aged against QPC lost its window to the clocks' offset (the seat limit
    # ray). Python stdlib only; --self-test checks the checker.
    add_test(NAME halomccvr_sight_clock_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_sight_clock.py"
            --self-test)
    # Isolation guard for the dev assessment marker (honest_reticle_assess): it is a SECOND marker,
    # never the player's reticle. Its code sits in [assess-only] regions inside #if HALOMCCVR_DEV_DIAGNOSTICS;
    # nothing outside them reads the key, Game_GetClampedAimDirection or the publication, and nothing inside them
    # writes the reticle, a mark, a shot, the aim or the drive. --self-test proves each rule fails on a misuse.
    add_test(NAME halomccvr_assess_isolation_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_assess_isolation.py"
            --self-test)
    # Every LOG(...) passes as many arguments as its format converts. Logf is a plain variadic, so
    # nothing else checks it, and no test runs game.cpp's install path: a LOG one argument short handed "%s" a
    # TLS offset and killed MCC at every Reach and Halo 4 level load. Counts only (not types). Python stdlib.
    add_test(NAME halomccvr_log_format_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_log_formats.py"
            --self-test "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_executable(halomccvr_aim_caller_tests tests/aim_caller_tests.cpp)
    target_include_directories(halomccvr_aim_caller_tests PRIVATE src/common)
    add_test(NAME halomccvr_aim_caller_tests COMMAND halomccvr_aim_caller_tests)
    add_executable(halomccvr_halo4_seat_aim_logic_tests tests/halo4_seat_aim_logic_tests.cpp)
    target_include_directories(halomccvr_halo4_seat_aim_logic_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo4_seat_aim_logic_tests COMMAND halomccvr_halo4_seat_aim_logic_tests)
    # Halo 4 seated convergence (src/common/halo4_seat_converge_logic.h): the solve's start and its carry, the
    # fixed point over a model of the game's following camera and the rate it is reached at, the start held
    # across a gap in the samples, the reasons a poll does not solve, and the witness's line-against-point measure.
    add_executable(halomccvr_halo4_seat_converge_tests tests/halo4_seat_converge_tests.cpp)
    target_include_directories(halomccvr_halo4_seat_converge_tests PRIVATE src/common)
    add_test(NAME halomccvr_halo4_seat_converge_tests COMMAND halomccvr_halo4_seat_converge_tests)
    add_executable(halomccvr_type13_flight_log_tests tests/type13_flight_log_tests.cpp)
    target_include_directories(halomccvr_type13_flight_log_tests PRIVATE src/common)
    add_test(NAME halomccvr_type13_flight_log_tests COMMAND halomccvr_type13_flight_log_tests)
    # addrcascade (src/common/addrcascade): the address-verification library the direct drives resolve through.
    # Its two source files are unmodified copies, so its tests compile them ALONE: of this project's folders only
    # its own is on the include path, at /W4 /WX, with a consumer that includes nothing else of this project. One
    # convenient #include of a project header would never fail in the DLL's build, where they all are.
    # Built twice: as a player build sees it (ADDRCASCADE_FAULTS undefined, fault() a constant false) and as a dev
    # build does (ADDRCASCADE_FAULTS=1).
    foreach(halomccvr_addrcascade_flavour "" "_faults")
        set(halomccvr_addrcascade_target "halomccvr_addrcascade${halomccvr_addrcascade_flavour}_tests")
        add_executable(${halomccvr_addrcascade_target}
            tests/addrcascade_tests.cpp src/common/addrcascade/AddressCascade.cpp)
        target_include_directories(${halomccvr_addrcascade_target} PRIVATE src/common/addrcascade)
        if(MSVC)
            target_compile_options(${halomccvr_addrcascade_target} PRIVATE /W4 /WX)
        endif()
        add_test(NAME ${halomccvr_addrcascade_target} COMMAND ${halomccvr_addrcascade_target})
    endforeach()
    target_compile_definitions(halomccvr_addrcascade_faults_tests PRIVATE ADDRCASCADE_FAULTS=1)
    # Drive address resolution (src/common/drive_resolve.h) over synthetic images: found as recorded, followed
    # when the code moved, refused when ambiguous or missing, told apart by the recorded address, resolved inside
    # its function, read through this mod's own hooks, and every forced fault.
    add_executable(halomccvr_drive_resolve_tests tests/drive_resolve_tests.cpp)
    target_include_directories(halomccvr_drive_resolve_tests PRIVATE src/common)
    add_test(NAME halomccvr_drive_resolve_tests COMMAND halomccvr_drive_resolve_tests)
    # EVERY REAL PLAN of src/common/drive_sites.h -- the code that decides what each drive hooks -- run on an image
    # synthesised from the tables themselves (each row's bytes at its recorded RVA, inside function extents
    # generated from the rows, with a matching .pdata, calls and rip loads filled as the plan requires): as
    # recorded, moved, and with one row broken at a time; then the proofs that are not rows. No game file.
    add_executable(halomccvr_drive_sites_tests tests/drive_sites_tests.cpp)
    target_include_directories(halomccvr_drive_sites_tests PRIVATE src/common)
    add_test(NAME halomccvr_drive_sites_tests COMMAND halomccvr_drive_sites_tests)
    # The DLL's side of that resolution (src/dll/drive_cascade.cpp), as shipped, on a module that is really loaded
    # (the test's own executable): opened through VirtualQuery, its .pdata compared with the operating system's own
    # lookup, its TLS index taken from its PE TLS directory, a signature read through a hook patch, the address
    # cache's file written, verified from, replaced, refused and left whole by a failed write on disk, the line
    # that says which rung won, and the hook watchdog as the cold worker steps it. Player flavour (the dev fault
    # key must do nothing), then dev flavour (each forced fault must show).
    foreach(halomccvr_drive_cascade_flavour "" "_dev")
        set(halomccvr_drive_cascade_target "halomccvr_drive_cascade${halomccvr_drive_cascade_flavour}_tests")
        add_executable(${halomccvr_drive_cascade_target} tests/drive_cascade_tests.cpp
            src/dll/drive_cascade.cpp src/dll/sigscan.cpp src/common/config.cpp
            src/common/addrcascade/AddressCascade.cpp)
        target_include_directories(${halomccvr_drive_cascade_target} PRIVATE src/common src/dll)
        target_compile_definitions(${halomccvr_drive_cascade_target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(${halomccvr_drive_cascade_target} PRIVATE psapi)
        add_test(NAME ${halomccvr_drive_cascade_target} COMMAND ${halomccvr_drive_cascade_target})
    endforeach()
    target_compile_definitions(halomccvr_drive_cascade_dev_tests PRIVATE
        HALOMCCVR_DEV_DIAGNOSTICS=1 ADDRCASCADE_FAULTS=1)
    # THE OFFLINE CHECK of the direct drives' addresses against the game's own DLL files
    # (tools/drive_address_check.cpp): each DLL laid out as an image, every drive's own resolution run on it from
    # the same tables the DLL uses (src/common/drive_sites.h), then again on a copy with every section moved, and
    # again with each fault forced. It needs the MCC install folder (--mcc <folder>, or the HALOMCCVR_MCC_DIR
    # environment variable) and only reads the DLLs. With neither it exits 77, which ctest reports as skipped: a
    # machine without the game does not fail its tests.
    # With --loaded <scratch folder> it also maps a copy of each DLL as the Windows loader does (its code not run)
    # and resolves every drive on it through the DLL's own side of the resolution (src/dll/drive_cascade.cpp).
    # That side is built here as a dev build builds it, so the tool can run the search beside the verified addresses
    # (the self-test bit of drive_fault) on each mapped copy. With --loaded it also makes each install's own
    # decision on the mapped copy, says the line the install logs, and puts the address cache through a real
    # file in that folder. Everything is done for the player build's plans and for the dev build's.
    # The test below never passes --loaded, and where HALOMCCVR_MCC_DIR is not set it is SKIPPED, which ctest
    # counts among the tests that passed: a green run there has read no game DLL.
    add_executable(halomccvr_drive_address_check tools/drive_address_check.cpp
        src/dll/drive_cascade.cpp src/dll/sigscan.cpp src/common/config.cpp
        src/common/addrcascade/AddressCascade.cpp)
    target_include_directories(halomccvr_drive_address_check PRIVATE src/common)
    target_compile_definitions(halomccvr_drive_address_check PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN ADDRCASCADE_FAULTS=1 HALOMCCVR_DEV_DIAGNOSTICS=1)
    target_link_libraries(halomccvr_drive_address_check PRIVATE psapi)
    add_test(NAME halomccvr_drive_address_check COMMAND halomccvr_drive_address_check --moved --faults --brief)
    set_tests_properties(halomccvr_drive_address_check PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(halomccvr_dev_pad_script_tests tests/dev_pad_script_tests.cpp)
    target_include_directories(halomccvr_dev_pad_script_tests PRIVATE src/common)
    add_test(NAME halomccvr_dev_pad_script_tests COMMAND halomccvr_dev_pad_script_tests)
    add_executable(halomccvr_sight_range_tests tests/sight_range_tests.cpp)
    target_include_directories(halomccvr_sight_range_tests PRIVATE src/common)
    add_test(NAME halomccvr_sight_range_tests COMMAND halomccvr_sight_range_tests)
    # What the intent ray can hit: each title's trace flags decoded with that title's own bit layout
    # must include bipeds and vehicles (and each engine's line-of-sight flags must be seen to leave bipeds out);
    # the class of a trace result; the seated look-past keeps a boarder; Halo 2's projectile-flags agreement.
    add_executable(halomccvr_sight_hit_class_tests tests/sight_hit_class_tests.cpp)
    target_include_directories(halomccvr_sight_hit_class_tests PRIVATE src/common)
    add_test(NAME halomccvr_sight_hit_class_tests COMMAND halomccvr_sight_hit_class_tests)
    add_executable(halomccvr_native_forward_tests tests/native_forward_tests.cpp)
    add_test(NAME halomccvr_native_forward_tests COMMAND halomccvr_native_forward_tests)
    add_executable(halomccvr_sight_scope_tests tests/sight_scope_tests.cpp src/dll/sight_layer.cpp)
    target_include_directories(halomccvr_sight_scope_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_sight_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_sight_scope_tests COMMAND halomccvr_sight_scope_tests)
    add_executable(halomccvr_ce_render_tests tests/haloce_render_tests.cpp)
    target_include_directories(halomccvr_ce_render_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_render_tests COMMAND halomccvr_ce_render_tests)
    add_executable(halomccvr_ce_view_pair_tests tests/haloce_view_pair_tests.cpp)
    target_include_directories(halomccvr_ce_view_pair_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_view_pair_tests COMMAND halomccvr_ce_view_pair_tests)
    add_executable(halomccvr_ce_classic_view_pair_tests tests/haloce_classic_view_pair_tests.cpp)
    target_include_directories(halomccvr_ce_classic_view_pair_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_classic_view_pair_tests COMMAND halomccvr_ce_classic_view_pair_tests)
    add_executable(halomccvr_weapon_animation_motion_tests tests/weapon_animation_motion_tests.cpp)
    add_test(NAME halomccvr_weapon_animation_motion_tests COMMAND halomccvr_weapon_animation_motion_tests)
    add_executable(halomccvr_halo3_secondary_motion_tests tests/halo3_secondary_motion_tests.cpp)
    add_test(NAME halomccvr_halo3_secondary_motion_tests COMMAND halomccvr_halo3_secondary_motion_tests)
    add_executable(halomccvr_weapon_alignment_tests tests/weapon_alignment_tests.cpp)
    target_include_directories(halomccvr_weapon_alignment_tests PRIVATE src/common)
    add_test(NAME halomccvr_weapon_alignment_tests COMMAND halomccvr_weapon_alignment_tests)
    add_executable(halomccvr_ce_first_person_tests tests/haloce_first_person_tests.cpp src/dll/ik.cpp)
    target_include_directories(halomccvr_ce_first_person_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_first_person_tests COMMAND halomccvr_ce_first_person_tests)
    add_executable(halomccvr_ce_contact_tests tests/haloce_contact_tests.cpp src/dll/ik.cpp)
    target_include_directories(halomccvr_ce_contact_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_contact_tests COMMAND halomccvr_ce_contact_tests)
    add_executable(halomccvr_ce_contact_runtime_tests tests/haloce_contact_runtime_tests.cpp
        src/dll/haloce_native_bindings.cpp src/dll/ik.cpp src/dll/sight_layer.cpp)
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
    add_executable(halomccvr_reach_reticle_alpha_pixels_tests tests/reach_reticle_alpha_pixels_tests.cpp)
    target_compile_definitions(halomccvr_reach_reticle_alpha_pixels_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_reach_reticle_alpha_pixels_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_reach_reticle_alpha_pixels_tests COMMAND halomccvr_reach_reticle_alpha_pixels_tests)
    add_executable(halomccvr_reticle_tint_pixels_tests tests/reticle_tint_pixels_tests.cpp)
    target_compile_definitions(halomccvr_reticle_tint_pixels_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_reticle_tint_pixels_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_reticle_tint_pixels_tests COMMAND halomccvr_reticle_tint_pixels_tests)
    add_executable(halomccvr_mono_view_tests tests/mono_view_tests.cpp)
    add_test(NAME halomccvr_mono_view_tests COMMAND halomccvr_mono_view_tests)
    # A player build reads every key it writes: compiled without the development flag whatever this build is.
    add_executable(halomccvr_config_player_roundtrip_tests tests/config_player_roundtrip_tests.cpp
        src/common/config.cpp)
    target_compile_definitions(halomccvr_config_player_roundtrip_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_config_player_roundtrip_tests COMMAND halomccvr_config_player_roundtrip_tests)
    add_executable(halomccvr_hook_cost_tests tests/hook_cost_tests.cpp)
    add_test(NAME halomccvr_hook_cost_tests COMMAND halomccvr_hook_cost_tests)
    add_executable(halomccvr_host_ui_panel_pixels_tests tests/host_ui_panel_pixels_tests.cpp)
    target_compile_definitions(halomccvr_host_ui_panel_pixels_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_host_ui_panel_pixels_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_host_ui_panel_pixels_tests COMMAND halomccvr_host_ui_panel_pixels_tests)
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
    # The same production body with the dev sound listener witness compiled in (it samples inside the listener
    # detour): every check above must still hold -- the witness changes nothing that is handed on -- and the
    # witness must have measured the game's own packet and the handed one against the head.
    add_executable(halomccvr_ce_orientation_runtime_dev_tests tests/haloce_orientation_runtime_tests.cpp
        src/dll/haloce_native_bindings.cpp src/dll/ik.cpp)
    target_include_directories(halomccvr_ce_orientation_runtime_dev_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_orientation_runtime_dev_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_DEV_DIAGNOSTICS=1)
    target_link_libraries(halomccvr_ce_orientation_runtime_dev_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_orientation_runtime_dev_tests COMMAND halomccvr_ce_orientation_runtime_dev_tests)
    add_executable(halomccvr_ce_unit_control_runtime_tests tests/haloce_unit_control_runtime_tests.cpp
        src/dll/sight_layer.cpp)
    target_include_directories(halomccvr_ce_unit_control_runtime_tests PRIVATE src/common src/dll "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_ce_unit_control_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_ce_unit_control_runtime_tests COMMAND halomccvr_ce_unit_control_runtime_tests)
    add_executable(halomccvr_ce_network_input_tests tests/haloce_network_input_tests.cpp)
    target_include_directories(halomccvr_ce_network_input_tests PRIVATE src/common src/dll "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_ce_network_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_ce_network_input_tests COMMAND halomccvr_ce_network_input_tests)
    add_executable(halomccvr_ce_first_person_runtime_tests tests/haloce_first_person_runtime_tests.cpp
        src/dll/haloce_native_bindings.cpp src/dll/ik.cpp src/dll/sight_layer.cpp
        src/dll/haloce_skin_convert_thunk.asm)
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
    add_executable(halomccvr_halo3_weapon_haptics_tests tests/halo3_weapon_haptics_tests.cpp)
    add_executable(halo3_avatar_logic_tests tests/halo3_avatar_logic_tests.cpp)
    target_compile_definitions(halo3_avatar_logic_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halo3_avatar_logic COMMAND halo3_avatar_logic_tests)
    add_executable(halo3_avatar_runtime_tests tests/halo3_avatar_runtime_tests.cpp)
    target_compile_definitions(halo3_avatar_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(halo3_avatar_runtime_tests PRIVATE /W4 /EHsc)
    add_test(NAME halo3_avatar_runtime COMMAND halo3_avatar_runtime_tests)
    add_executable(odst_finger_pose_tests tests/odst_finger_pose_tests.cpp)
    target_compile_definitions(odst_finger_pose_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(odst_finger_pose_tests PRIVATE /W4 /EHsc)
    add_test(NAME odst_finger_pose COMMAND odst_finger_pose_tests)
    add_test(NAME halomccvr_halo3_weapon_haptics_tests COMMAND halomccvr_halo3_weapon_haptics_tests)
    add_executable(halomccvr_halo3_weapon_haptics_backend_tests tests/halo3_weapon_haptics_backend_tests.cpp)
    target_compile_definitions(halomccvr_halo3_weapon_haptics_backend_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(halomccvr_halo3_weapon_haptics_backend_tests PRIVATE /W4 /EHsc)
    add_test(NAME halomccvr_halo3_weapon_haptics_backend COMMAND halomccvr_halo3_weapon_haptics_backend_tests)
    add_executable(halomccvr_halo3_collision_dispatch_tests tests/halo3_collision_dispatch_tests.cpp)
    add_executable(halomccvr_native_fault_probe_tests tests/native_fault_probe_tests.cpp)
    target_compile_definitions(halomccvr_halo3_collision_dispatch_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(halomccvr_native_fault_probe_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_collision_dispatch_tests COMMAND halomccvr_halo3_collision_dispatch_tests)
    add_test(NAME halomccvr_native_fault_probe_tests COMMAND halomccvr_native_fault_probe_tests)
    add_executable(halomccvr_own_image_fault_filter_tests tests/own_image_fault_filter_tests.cpp)
    target_compile_definitions(halomccvr_own_image_fault_filter_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(halomccvr_own_image_fault_filter_tests PRIVATE /W4)
    add_test(NAME halomccvr_own_image_fault_filter_tests COMMAND halomccvr_own_image_fault_filter_tests)
    target_include_directories(halomccvr_halo3_independent_shots_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo3_independent_shots_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_options(halomccvr_halo3_independent_shots_tests PRIVATE /MAP)
    add_test(NAME halomccvr_halo3_independent_shots_tests COMMAND halomccvr_halo3_independent_shots_tests)
    add_executable(halomccvr_halo2_independent_shots_tests tests/halo2_independent_shots_tests.cpp)
    target_include_directories(halomccvr_halo2_independent_shots_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo2_independent_shots_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_independent_shots_tests COMMAND halomccvr_halo2_independent_shots_tests)
    add_executable(halomccvr_halo2_haptic_runtime_tests tests/halo2_haptic_runtime_tests.cpp)
    target_compile_definitions(halomccvr_halo2_haptic_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_haptic_runtime_tests COMMAND halomccvr_halo2_haptic_runtime_tests)
    add_executable(halo4_weapon_haptics_tests tests/halo4_weapon_haptics_tests.cpp)
    target_compile_definitions(halo4_weapon_haptics_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(halo4_weapon_haptics_tests PRIVATE /W4 /EHsc)
    add_test(NAME halo4_weapon_haptics COMMAND halo4_weapon_haptics_tests)
    add_executable(reach_weapon_haptics_logic_tests tests/reach_weapon_haptics_logic_tests.cpp)
    add_test(NAME reach_weapon_haptics_logic COMMAND reach_weapon_haptics_logic_tests)
    add_executable(dual_wield_shake_tests tests/dual_wield_shake_tests.cpp)
    target_compile_features(dual_wield_shake_tests PRIVATE cxx_std_20)
    add_test(NAME dual_wield_shake COMMAND dual_wield_shake_tests)
    add_executable(odst_fp_map_probe_tests tests/odst_fp_map_probe_tests.cpp)
    target_compile_features(odst_fp_map_probe_tests PRIVATE cxx_std_20)
    add_test(NAME odst_fp_map_probe COMMAND odst_fp_map_probe_tests)
    add_executable(halo3_dual_shake_tests tests/halo3_dual_shake_tests.cpp)
    target_compile_features(halo3_dual_shake_tests PRIVATE cxx_std_20)
    add_test(NAME halo3_dual_shake COMMAND halo3_dual_shake_tests)
    set(HALOMCCVR_ODST_MODDED_ASSETS "${HALOMCCVR_GENERATED_DIR}/odst_modded_fp_assets.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_ODST_MODDED_ASSETS}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_odst_modded_fp_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_ODST_MODDED_ASSETS}"
        DEPENDS tools/generate_odst_modded_fp_fixture.py
            docs/ODST-MODDED-WEAPONS-MAP-EVIDENCE.json
            docs/ODST-MYTHIC-FP-MAP-EVIDENCE.json VERBATIM)
    add_executable(odst_modded_fp_tests tests/odst_modded_fp_tests.cpp "${HALOMCCVR_ODST_MODDED_ASSETS}")
    target_include_directories(odst_modded_fp_tests PRIVATE "${HALOMCCVR_GENERATED_DIR}")
    target_compile_features(odst_modded_fp_tests PRIVATE cxx_std_20)
    add_test(NAME odst_modded_fp COMMAND odst_modded_fp_tests)
    set(HALOMCCVR_HALO3_MODDED_FIXTURE "${HALOMCCVR_GENERATED_DIR}/halo3_modded_fp_fixture.inl")
    set(HALOMCCVR_HALO3_MODDED_ASSETS "${HALOMCCVR_GENERATED_DIR}/halo3_modded_fp_assets.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_HALO3_MODDED_FIXTURE}" "${HALOMCCVR_HALO3_MODDED_ASSETS}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_halo3_modded_fp_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_HALO3_MODDED_FIXTURE}"
        DEPENDS tools/generate_halo3_modded_fp_fixture.py tools/generate_haptics_runtime_fixture.py
            src/dll/game.cpp src/dll/legacy_runtime_weapon_bounds.inl
            docs/HALO3-MODDED-WEAPONS-MAP-EVIDENCE.json VERBATIM)
    add_executable(halo3_modded_fp_tests tests/halo3_modded_fp_tests.cpp
        "${HALOMCCVR_HALO3_MODDED_FIXTURE}" "${HALOMCCVR_HALO3_MODDED_ASSETS}")
    target_include_directories(halo3_modded_fp_tests PRIVATE "${HALOMCCVR_GENERATED_DIR}")
    target_compile_features(halo3_modded_fp_tests PRIVATE cxx_std_20)
    add_test(NAME halo3_modded_fp COMMAND halo3_modded_fp_tests)
    set(HALOMCCVR_REACH_MODDED_FIXTURE "${HALOMCCVR_GENERATED_DIR}/reach_modded_fp_fixture.inl")
    set(HALOMCCVR_REACH_MODDED_ASSETS "${HALOMCCVR_GENERATED_DIR}/reach_modded_fp_assets.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_REACH_MODDED_FIXTURE}" "${HALOMCCVR_REACH_MODDED_ASSETS}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_reach_modded_fp_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_REACH_MODDED_FIXTURE}"
        DEPENDS tools/generate_reach_modded_fp_fixture.py tools/generate_haptics_runtime_fixture.py
            src/dll/game.cpp src/dll/legacy_runtime_weapon_bounds.inl
            docs/REACH-MODDED-WEAPONS-M20-EVIDENCE.json VERBATIM)
    add_executable(reach_modded_fp_layout_tests tests/reach_modded_fp_layout_tests.cpp
        "${HALOMCCVR_REACH_MODDED_FIXTURE}" "${HALOMCCVR_REACH_MODDED_ASSETS}")
    target_include_directories(reach_modded_fp_layout_tests PRIVATE "${HALOMCCVR_GENERATED_DIR}")
    target_compile_features(reach_modded_fp_layout_tests PRIVATE cxx_std_20)
    add_test(NAME reach_modded_fp_layout COMMAND reach_modded_fp_layout_tests)
    add_executable(halomccvr_reach_finger_pose_tests tests/reach_finger_pose_tests.cpp)
    target_compile_features(halomccvr_reach_finger_pose_tests PRIVATE cxx_std_20)
    add_test(NAME halomccvr_reach_finger_pose_tests COMMAND halomccvr_reach_finger_pose_tests)
    add_executable(halo4_body_packet_backend_tests tests/halo4_body_packet_backend_tests.cpp)
    target_compile_features(halo4_body_packet_backend_tests PRIVATE cxx_std_20)
    add_test(NAME halo4_body_packet_backend_tests COMMAND halo4_body_packet_backend_tests)
    add_executable(odst_weapon_haptics_tests tests/odst_weapon_haptics_tests.cpp)
    target_compile_definitions(odst_weapon_haptics_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(odst_weapon_haptics_tests PRIVATE /W4 /EHsc)
    add_test(NAME odst_weapon_haptics COMMAND odst_weapon_haptics_tests)
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
install(TARGETS halo3xr
    RUNTIME DESTINATION .
    COMPONENT dist)
install(TARGETS halo3xr_launcher
    RUNTIME DESTINATION .
    COMPONENT launcher)
# The Project Reclaimer relay rides beside HaloMCCVR.dll, so the launcher's Project Reclaimer section can start it
# from an installed Halo_MCC_VR folder.
install(TARGETS halomccvr_reclaimer_relay
    RUNTIME DESTINATION .
    COMPONENT dist)
install(FILES
    LICENSE
    installer/MANUAL-README.txt
    installer/THIRD-PARTY-LICENSES.txt
    DESTINATION .
    COMPONENT dist)
install(DIRECTORY src/launcher/assets/ DESTINATION assets COMPONENT dist)
install(FILES
    tools/telemetry_analyser/analyse_mccvr_telemetry.py
    DESTINATION TelemetryAnalyser
    COMPONENT dist)
