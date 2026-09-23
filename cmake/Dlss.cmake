# Optional runtime, mandatory source dependency. Runtime packaging is governed
# by third_party/dlss/LICENSE.txt; the ordinary VR resolve remains available
# when the runtime is absent or the current hardware/title is unsupported.
include(FetchContent)
FetchContent_Declare(dlss_sdk
    GIT_REPOSITORY https://github.com/NVIDIA/DLSS.git
    GIT_TAG a291cc7d2cc642a51566f3dfd5376f635cd1b284
    GIT_SHALLOW FALSE
    GIT_SUBMODULES "")
FetchContent_MakeAvailable(dlss_sdk)
set(HALOMCCVR_DLSS_NGX_LIB
    "${dlss_sdk_SOURCE_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
if(NOT EXISTS "${HALOMCCVR_DLSS_NGX_LIB}")
    message(FATAL_ERROR "Pinned NVIDIA NGX static loader is missing")
endif()
target_sources(halo3xr PRIVATE src/dll/dlss.cpp src/dll/nvof.cpp)
target_include_directories(halo3xr PRIVATE "${dlss_sdk_SOURCE_DIR}/include")
target_link_libraries(halo3xr PRIVATE
    "${HALOMCCVR_DLSS_NGX_LIB}" d3d12 version)

# Candidate runtime is the signed release binary from this exact SDK revision.
# Never substitute the development/watermarked runtime or a local download.
set(HALOMCCVR_DLSS_RUNTIME "${dlss_sdk_SOURCE_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll")
file(SHA256 "${HALOMCCVR_DLSS_RUNTIME}" HALOMCCVR_DLSS_RUNTIME_SHA256)
if(NOT HALOMCCVR_DLSS_RUNTIME_SHA256 STREQUAL
    "be6e434a94ca32499515eb62ca0e6c274526055d568d0426e4c652dcdfb6ee6e")
    message(FATAL_ERROR "Pinned NVIDIA DLSS release runtime hash mismatch")
endif()
install(FILES "${HALOMCCVR_DLSS_RUNTIME}" DESTINATION . COMPONENT dist)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dlss/LICENSE.txt"
              "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dlss/NOTICE.txt"
    DESTINATION licenses/NVIDIA-DLSS COMPONENT dist)

function(halomccvr_compile_dlss_shader entry symbol filename)
    execute_process(COMMAND "${HALOMCCVR_FXC}" /nologo /Ges /WX /O3
        /T ps_5_0 /E "${entry}"
        /Fh "${HALOMCCVR_GENERATED_DIR}/${filename}" /Vn "${symbol}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dll/dlss_camera.hlsl"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "DLSS shader ${entry} failed: ${output}${error}")
    endif()
endfunction()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dll/dlss_camera.hlsl")
halomccvr_compile_dlss_shader(ps_motion g_dlssCameraPs dlss_camera_ps.h)
halomccvr_compile_dlss_shader(ps_debug g_dlssDebugPsCode dlss_debug_ps.h)
halomccvr_compile_dlss_shader(ps_copy g_dlssCopyPsCode dlss_copy_ps.h)

if(BUILD_TESTING)
    add_executable(halo4_vehicle_identity_tests tests/halo4_vehicle_identity_tests.cpp)
    target_compile_definitions(halo4_vehicle_identity_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halo4_vehicle_identity COMMAND halo4_vehicle_identity_tests)
    add_executable(halo2_vehicle_identity_tests tests/halo2_vehicle_identity_tests.cpp)
    target_compile_definitions(halo2_vehicle_identity_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halo2_vehicle_identity COMMAND halo2_vehicle_identity_tests)
    add_executable(ce_dlss_camera_tests tests/ce_dlss_camera_tests.cpp)
    add_test(NAME ce_dlss_camera COMMAND ce_dlss_camera_tests)
    add_executable(ce_dlss_depth_tests tests/ce_dlss_depth_tests.cpp src/dll/haloce_eye_cache.cpp)
    target_compile_definitions(ce_dlss_depth_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ce_dlss_depth_tests PRIVATE d3d11)
    add_test(NAME ce_dlss_depth COMMAND ce_dlss_depth_tests)
    add_executable(dual_reticle_tests tests/dual_reticle_tests.cpp)
    add_test(NAME dual_reticle COMMAND dual_reticle_tests)
    add_executable(dlss_contract_tests tests/dlss_contract_tests.cpp)
    add_test(NAME dlss_contract COMMAND dlss_contract_tests)
    add_executable(subtitle_contract_tests tests/subtitle_contract_tests.cpp)
    add_test(NAME subtitle_contract COMMAND subtitle_contract_tests)
    add_executable(native_subtitle_interface_tests tests/native_subtitle_interface_tests.cpp)
    target_compile_definitions(native_subtitle_interface_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME native_subtitle_interface COMMAND native_subtitle_interface_tests)
endif()
