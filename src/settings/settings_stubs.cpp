// The game's side of menu.cpp, for the desktop settings window: no game is running, so every question has the answer it
// has at the MCC shell, and every action that would touch a game does nothing. Settings themselves are g_config's.
#include <windows.h>
#include <d3d11.h>
#include <cstring>
#include "../dll/menu.h"
#include "../dll/vr.h"
#include "../dll/game.h"
#include "../dll/title_adapter.h"
#include "../dll/d3d11_hook.h"
#include "../dll/telemetry_recorder.h"
#include "../dll/two_hand_lab_runtime.h"
#include "../dll/native_menu_pointer.h"
#include "../dll/host_unload_gate.h"
#include "../dll/native_vehicle_first_person.h"
#include "../dll/lifted_engine.h"
#include "../common/config.h"

static void Blank(char* out, size_t size)
{
    if (out && size) out[0] = 0;
}

bool D3D_BeginLiveResize(unsigned&, unsigned&) { return false; }
bool D3D_CanLiveResize() { return false; }
bool D3D_FitActive() { return false; }
intptr_t D3D_MapMouseMessageToRender(unsigned, intptr_t lp) { return lp; }
bool D3D_HostNativeRenderAnswer(unsigned, unsigned, unsigned&, unsigned&, int&) { return false; }
int D3D_LiveResizeState(unsigned& width, unsigned& height) { width = height = 0; return 0; }
HWND D3D_GameWindow() { return nullptr; }
void D3D_CancelQueuedLiveResize() {}
void D3D_GetForcedRenderSize(unsigned& width, unsigned& height) { width = height = 0; }
void D3D_GetRenderPlan(unsigned& a, unsigned& b, unsigned& c, unsigned& d, bool& e, bool& f)
{
    a = b = c = d = 0;
    e = f = false;
}
void D3D_RetryLiveResize() {}
void D3D_SetForcedClientLie(bool) {}

bool Game_CanToggleImmersiveView() { return false; }
bool Game_GetWriteUp() { return false; }
bool Game_ReadZoomLevel(int&) { return false; }
bool Game_IsHeadTrackingApplied() { return false; }
bool Game_IsStereoGeometryOnlyBringup() { return false; }
bool Game_ReadVrActionBindings(vr_mapping::Transports&, uint64_t, unsigned) { return false; }
bool Game_ReadVrExtraBindings(vr_mapping::ExtraTransports&, uint64_t) { return false; }
bool Game_UsesHalo2NativeHudLayout() { return false; }
bool Game_VehicleBoom(float&, float&, float&, bool&, bool&) { return false; }
bool Game_VehicleCameraLag(float&, float&, bool&) { return false; }
bool Game_VehicleCameraReadout(char* out, size_t size) { Blank(out, size); return false; }
const char* Game_ManualVrRecoveryStatus() { return "No game is running."; }
const char* Game_ReclaimerFlightStatus() { return ""; }
const char* Game_VehicleSeatTrimName(int, VehicleTrimBank) { return ""; }
float Game_GetPitchSign() { return 1.0f; }
float Game_GetWorldScale() { return g_config.world_scale; }
float Game_GetYawSign() { return 1.0f; }
int Game_VehicleSeatTrimSlotEx(VehicleTrimBank*) { return -1; }
void Game_FlipPitch() {}
void Game_FlipYaw() {}
void Game_GunScale(int) {}
void Game_LeanScale(int) {}
void Game_PitchTrim(int) {}
void Game_RecenterManual() {}
void Game_RequestManualVrRecovery() {}
void Game_SetWorldScale(float scale) { g_config.world_scale = scale; }
void Game_ToggleHeadTracking() {}
void Game_TogglePositional() {}
void Game_ToggleUp() {}
void Game_ToggleVrAim() {}
void Game_VehicleBoomRecenter() {}
void Game_VehicleCameraResetLag() {}
void Game_VehicleCameraSetLag(float, float) {}
void Game_VehicleCameraStep(int, int) {}

bool Input_PhysicalCrouchRiseCapped() { return false; }
void Input_RequestPauseToggle() {}
bool NativeMenuPointer_Message(UINT, LPARAM) { return false; }
void NativeMenuPointer_Init(HWND) {}
bool NativeVehicleFirstPerson_TrimTarget(GameTitle, uint64_t&, int&) noexcept { return false; }
void HostUnloadGate_HostClosing(bool) noexcept {}

const char* RuntimeModeName(RuntimeMode) { return "no game running"; }
GameTitle TitleAdapter_GetActiveTitle() { return GameTitle::None; }
RuntimeMode TitleAdapter_GetRuntimeMode() { return RuntimeMode{}; }
const TitleDescriptor* TitleAdapter_GetActive() { return nullptr; }

const char* Telemetry_ErrorName(TelemetryErrorCode) noexcept { return ""; }
const char* Telemetry_StateName(TelemetryRecorderState) noexcept { return "not running"; }
TelemetryStatusSnapshot Telemetry_GetStatus() noexcept { return {}; }
void Telemetry_RequestStart() noexcept {}
void Telemetry_RequestStop() noexcept {}

bool VR_GetDlssRenderPlan(VrDlssRenderPlan&) { return false; }
bool VR_HolsterCaptureArmed() noexcept { return false; }
bool VR_IsPausePresentation() { return false; }
bool VR_IsScopeActive() { return false; }
bool VR_IsStereoEnabled() { return false; }
bool VR_IsTwoHandAiming() { return false; }
const char* VR_ControllerProfileName(bool) { return "none: no headset session"; }
uint32_t VR_HolsterCaptureCount() noexcept { return 0; }
uint32_t VR_HolsterCaptureTimedOut() noexcept { return 0; }
void VR_GetDlssStatus(char* out, size_t size) { Blank(out, size); }
void VR_GetStatus(VrStatus& status) { status = VrStatus{}; }
void VR_HolsterCaptureArm(bool) noexcept {}
void VR_RecalibratePhysicalCrouch() noexcept {}
void VR_SetGameHaptics(float) {}
void VR_SetScopeActive(bool) {}
void VR_ToggleScreenFollow() {}
void VR_ToggleStereo() {}

namespace lifted
{
bool Active() { return false; }
}

namespace two_hand_lab_runtime
{
static two_hand_lab::Settings g_settings{};
bool GetLastDiagnostics(two_hand_lab::Diagnostics&) noexcept { return false; }
two_hand_lab::Settings GetSettings() noexcept { return g_settings; }
void ResetSettings() noexcept { g_settings = two_hand_lab::Settings{}; }
void SetSettings(const two_hand_lab::Settings& settings) noexcept { g_settings = settings; }
}
