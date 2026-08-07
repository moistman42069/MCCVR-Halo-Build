cmake_minimum_required(VERSION 3.24)
project(halomccvr LANGUAGES C CXX RC)

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
message(STATUS
    "Halo MCC VR build identity: ${HALOMCCVR_BUILD_COMMIT}, ODST=${HALOMCCVR_BUILD_ODST}, Reach=${HALOMCCVR_BUILD_REACH}, ReachRender=${HALOMCCVR_BUILD_REACH_RENDER}")

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
    src/dll/d3d11_hook.cpp
    src/dll/vr.cpp
    src/dll/menu.cpp
    src/dll/sigscan.cpp
    src/dll/game.cpp
    src/dll/ik.cpp
    src/dll/input.cpp
    src/dll/title_adapter.cpp
    src/dll/reach_adapter.cpp
    src/dll/halo4_adapter.cpp
    src/dll/halo4_cold_observation.cpp)
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
    HALOMCCVR_BUILD_COMMIT="${HALOMCCVR_BUILD_COMMIT}"
    HALOMCCVR_BUILD_ODST="${HALOMCCVR_BUILD_ODST}"
    HALOMCCVR_BUILD_REACH="${HALOMCCVR_BUILD_REACH}"
    HALOMCCVR_BUILD_REACH_RENDER="${HALOMCCVR_BUILD_REACH_RENDER}")
target_link_libraries(halo3xr PRIVATE
    openxr_loader minhook imgui_lib
    d3d11 dxgi d3dcompiler)
if(HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE)
    target_link_libraries(halo3xr PRIVATE bcrypt)
endif()

add_executable(halo3xr_launcher WIN32 src/launcher/launcher.cpp)
target_compile_definitions(halo3xr_launcher PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# IApplicationActivationManager: the Microsoft Store edition is a packaged app
# and can only be started through packaged activation.
target_link_libraries(halo3xr_launcher PRIVATE ole32)

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
    add_executable(halomccvr_core_tests
        tests/core_tests.cpp
        src/common/input_logic.cpp
        src/common/scope_logic.cpp
        src/common/title_registry.cpp
        src/common/title_runtime_state.cpp
        src/common/config.cpp
        src/common/log.cpp
        src/dll/sigscan.cpp
        src/dll/reach_adapter.cpp
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
        HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE=$<BOOL:${HALOMCCVR_EXPERIMENTAL_REACH_RENDER_CANDIDATE}>)
    add_test(NAME halomccvr_core_tests COMMAND halomccvr_core_tests)
endif()

# Stage an unaccepted local candidate only. Release-specific hashes and metadata
# must never be copied into a newly rebuilt package.
install(TARGETS halo3xr halo3xr_launcher
    RUNTIME DESTINATION .
    COMPONENT dist)
install(FILES
    LICENSE
    installer/MANUAL-README.txt
    DESTINATION .
    COMPONENT dist)
