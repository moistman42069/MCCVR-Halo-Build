// HaloMCCFlatProbe.dll -- THE FLAT CONTROL ARM for the AS SEEN measurement (dev tooling; never packaged).
//
// THE QUESTION: strafing with the assault rifle in Halo 3, the impact marks land at the reticle in flat; in VR they
// can trail it by about half the reticle. The VR dev build measures what the headset shows
// (halo3_impact_witness.inl, AS SEEN). This DLL measures the SAME thing in stock flat Halo 3, with the same code
// (src/common/as_seen_logic.h), so the two numbers can be compared bin for bin.
//
// HALO 2: the same question for Halo 2 (shots that seem to trail a strafe in Halo 2 Anniversary, flat parity on).
// The probe measures stock flat Halo 2 too, in BOTH renderers (MCC switches
// with Tab / the pad's Back), each frame tagged by the renderer that drew it, with the simulation's own tick counter
// and the camera's interpolation fraction, so the time decomposition is in ticks as well as milliseconds.
//
// OBSERVE ONLY. It is loaded without the VR mod (HaloMCCVRLauncher.exe --flat-probe, dev builds) and touches nothing:
// no OpenXR, no stereo, no swapchain or presentation change, no aim or record write, no input hook, no camera or seat
// patch. Its hooks forward every argument and the return value exactly and only READ:
//   Halo 3 (halo3.dll):
//   * the camera copy (the VR mod's kCamCopySig, halo3.dll; the same prototype as its audited CamCopyHook): after the
//     native copy, the render camera's position, forward, up and projection tangents;
//   * object_new (the impact witness's anchors): the fire routine's projectile creation, by its return address --
//     the placement's position and forward;
//   * the collision scheduler (the VR mod's kHalo3CollisionVectorSignature, its 8-argument prototype): after the
//     native query, a tracked round's own query and, when it hits, the hit point;
//   Halo 2 (halo2.dll; every anchor decoded from code below, each signature unique in the module):
//   * object_new 0x8D79D0 (one argument, the placement block): only the call from the fire routine 0x8E4940 (its
//     `lea rcx,[rbp+60]; call object_new; mov esi,eax; cmp eax,-1` at 0x8E5A6B), the placement's position (+0x1C),
//     forward (+0x28) and velocity (+0x40) -- offsets decoded from object_new's own copy into the object
//     (0x8D7C3E: +0x1C -> +0x64, +0x28 -> +0x70, +0x34 -> +0x7C, +0x40 -> +0x88);
//   * collision_test_vector 0x75B850 (the VR mod's world-collision anchor; six arguments, result: fraction +4, point
//     +8): every projectile query funnels through it (collision_test_line 0x75B2C0 calls it too);
//   * CLASSIC: render_player_window 0x7E2130, only its ordinary call from the render frame (0x7E1706): the window's
//     render camera (+0x0C: position, forward +0xC, up +0x18, vertical FOV +0x28) -- on the main thread, after the
//     observer update, so the tick counter and the interpolation fraction read there are this frame's;
//   * ANNIVERSARY (Saber): the scene render 0x2DF190 (the Saber render thread), main views only: the camera matrix
//     the engine latched into the view record (record+0x20), turned back into Blam units with the engine's own
//     constants (read from the module globals the camera bridge 0x5F510 reads; Halo2SaberViewMatrixToCamera); and the
//     camera bridge itself (main thread, user 0): the Saber camera matrix it built and the tick and interpolation
//     fraction at that moment, matched to the view record bit for bit;
//   * the game tick counter: game_time_globals (the slot read by the tick increment 0x7067F0, `48 8B 05 .. FF 40 08
//     C3`; +8 the current tick, +4 seconds per tick, +2 ticks per second) and the frame interpolator's blend factor
//     (0x164B300, written once a frame by 0x723580 from the game-time tick fraction -- the camera is drawn at
//     lerp(previous tick, newest tick, that factor); see the Halo 2 notes below);
//   * IDXGISwapChain::Present / Present1: the frame boundary and its time.
// Every signature must match exactly once or that hook is not installed (and the probe says so); every read of game
// memory is under SEH. ADDR-HYGIENE: dev-only (a measurement tool; it decides nothing and writes nothing).
//
// A round is the local player's when it spawns within 1.5 m of the render camera and its forward is within 30 deg of
// the camera's (campaign: an ally standing that close firing that way is rare; counted rounds are logged). Its
// collision queries are matched by geometry (as_seen::QueryOnRound): they start on its spawn line and run along its
// forward (no object read is needed). Its first presented frame is the first Present whose frame's camera was taken
// after the hit.
//
// HALO 2's camera (static analysis of the pinned halo2.dll): the unit camera position 0x927170 RECORDS the
// tick's camera into MCC's frame interpolator while a tick runs (0x7231C0, flag 0x164B304 set by the tick begin
// 0x723520) and, outside a tick, READS BACK lerp(previous tick, newest tick, factor 0x164B300) (0x7227A0 -> 0x6FDC80);
// the factor is clamp(game_time+0x10, 0, 1), written after the frame's ticks (0x67A220+0x618) just before the observer
// update (+0x628). So every camera either renderer draws is up to a tick BEHIND the simulation's newest eye, and the
// probe's census measures exactly that (ticks per frame, did the eye move on tick-less frames, the mean factor).
//
// HALO REACH and HALO 4: both run MCC's frame interpolator the way Halo 2 does (static analysis of the
// pinned modules: as_seen_logic.h section 8) -- the first-person camera is built after the frame's ticks from the unit's
// origin read back as lerp(previous tick, newest tick, factor) -- so their flat parity has to be measured against
// stock flat, and a one-tick carry for Halo 4's fire eye is short by that interpolation. The probe measures stock
// flat Reach and Halo 4 the same way, with MCC's frame clock (the tick counter and the blend factor, mcc_frame_clock.h,
// decoded from the engine's own code) on every camera, spawn and hit:
//   Reach (haloreach.dll; each anchor unique and decoded):
//   * object_new 0x46D6E0, only its two calls from the projectile transaction 0x4C2710 (returns 0x4C3CE6 / 0x4C3CF3):
//     the placement's position (+0x1C), forward (+0x28) and velocity (+0x40), decoded from object_new's own copy;
//   * collision_test_vector 0x12969C (the VR mod's anchor; eight arguments): a tracked round's query, the hit point
//     start + fraction (result +4) x vector;
//   * render_camera_from_observer 0x287DFC, only its world-render call (return 0x26C2DE): the frame's render camera
//     after the native copy (position +0, forward +0xC, up +0x18, vertical FOV +0x28);
//   Halo 4 (halo4.dll):
//   * object_new 0x5D2034, only its two calls from the fire routine 0x6176B8 (returns 0x618C88 / 0x618C92), the same
//     placement layout decoded from its own copy;
//   * PhysicsRayCast 0x1C1D4C (the VR mod's anchor; Halo 4 collides with Havok rays): a tracked round's ray, the hit
//     point start + fraction (result +4) x (end - start);
//   * the player-view setup 0x374C84 (the VR camera core's anchor): the observer it is handed (its sixth argument;
//     position +0, forward +0x28, up +0x34, vertical FOV +0x78) -- the frame's camera, before the native call.
//
// Lifetime: a worker thread waits for halo3.dll / halo2.dll / haloreach.dll / halo4.dll, installs, and retires the hooks
// (no-write retirement, minhook_lifecycle) if MCC unloads that module, installing again when it comes back. Log:
// HaloMCCFlatProbe.log next to this DLL, one AS SEEN line per ~2 s window with impacts (per renderer / title), plus the
// camera census.

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include <MinHook.h>

#include "../common/as_seen_logic.h"
#include "../common/halo2_render_logic.h"
#include "../common/log.h"
#include "../common/minhook_lifecycle.h"
#include "../dll/mcc_frame_clock.h"
#include "../dll/sigscan.h"

namespace
{
// ---- the Halo 3 anchors (copied from the VR mod, which proves and uses each; each must match exactly once here) ----
// game.cpp kCamCopySig: mov [rsp+8],rbx; push rdi; sub rsp,0x30; movaps [rsp+0x20],xmm6; mov rdi,rdx; mov rbx,rcx; ...
constexpr char kCamCopySig[] =
    "48 89 5C 24 08 57 48 83 EC 30 0F 29 74 24 20 48 8B FA 48 8B D9 48 85 D2 74 ?? F3 0F 10 1D ?? ?? ?? ??";
// halo3_impact_witness.inl: object_new's prologue, its placement copy, the fire routine's call to it.
constexpr char kObjectNewSig[] =
    "48 8B C4 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 78 83 CD FF 45 33 E4 F6 41 18 10 48 8B F1 8B FD 44 88 "
    "60 10";
constexpr char kPlacementCopySig[] =
    "F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? F2 0F 10 46 ?? "
    "F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ??";
constexpr char kFireCallSig[] =
    "48 8D 8D ?? ?? 00 00 E8 ?? ?? ?? ?? F3 44 0F 10 4C 24 ?? 41 83 C9 FF 89 44 24 ?? 41 3B C1 0F 84";
// game.cpp kHalo3CollisionVectorSignature: the active collision scheduler (the VR mod's Halo3CollisionResolveDetour).
constexpr char kCollisionSig[] =
    "48 8B C4 4C 89 40 18 88 50 10 48 89 48 08 55 53 56 57 41 54 "
    "41 55 41 56 41 57 48 8D A8 18 BB FF FF B8 A8 45 00 00 E8 ?? ?? ?? ?? 48 2B E0";
// The camera copy's source record (game.cpp kSrcPos / kSrcFwd / kSrcUp / kSrcProjX / kSrcProjY).
// ADDR-HYGIENE: dev-only (the VR mod reads the same fields every frame; here they are only read).
constexpr size_t kSrcPos = 0x00, kSrcFwd = 0x28, kSrcUp = 0x34, kSrcProjX = 0x68, kSrcProjY = 0x6C;

// ---- the Halo 2 anchors (pinned halo2.dll 1.3528.0.0, read with capstone; each unique in the module) -------------
// object_new 0x8D79D0: push rbx..r15; sub rsp,0xF8; mov [rsp+40],-2; mov rsi,rcx; mov edi,-1; ...; test [rcx+18],10.
constexpr char kH2ObjectNewSig[] =
    "40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC F8 00 00 00 48 C7 44 24 40 FE FF FF FF 48 8B F1 BF FF FF FF FF "
    "C6 84 24 48 01 00 00 00 F6 41 18 10";
// Its copy of the placement block into the object (0x8D7C3E): position, forward, up, then the velocity pair.
constexpr char kH2PlacementCopySig[] =
    "F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 43 ?? "
    "F2 0F 10 46 ?? F2 0F 11 43 ?? 8B 46 ?? 89 83 ?? ?? 00 00 F2 0F 10 46 ?? F2 0F 11 83 ?? ?? 00 00 8B 46 ?? 89 83";
// The fire routine 0x8E4940 (its entry, as the VR mod's dual-aim lane pins it) and its call of object_new (0x8E5A6B).
constexpr char kH2FireEntrySig[] =
    "44 88 4C 24 20 44 89 44 24 18 66 89 54 24 10 89 4C 24 08 55 53 41 55 41 56 48 8D AC 24 A8 E1 FF FF B8 58 1F 00 00 "
    "E8";
constexpr char kH2FireCallSig[] = "48 8D 4D ?? E8 ?? ?? ?? ?? 8B F0 83 F8 FF 0F 84";
// collision_test_vector 0x75B850 (halo2_observer_6dof.cpp kCollisionPattern).
constexpr char kH2CollisionSig[] =
    "48 8B C4 44 89 48 20 55 53 41 56 48 8D A8 A8 FB FF FF 48 81 EC 40 05 00 00 48 8B 9D 88 04 00 00 45 32 F6";
// The tick increment 0x7067F0: mov rax,[game_time_globals]; inc dword [rax+8]; ret.
constexpr char kH2TickIncrementSig[] = "48 8B 05 ?? ?? ?? ?? FF 40 08 C3";
// The interpolator's factor writer 0x723580: sub rsp,28; call <fraction>; movss [factor],xmm0; add rsp,28; ret. The
// call's target 0x706F80 must read the same game_time slot's +0x10 (checked below).
constexpr char kH2InterpFactorSig[] = "48 83 EC 28 E8 ?? ?? ?? ?? F3 0F 11 05 ?? ?? ?? ?? 48 83 C4 28 C3";
constexpr char kH2FractionReadSig[] = "48 8B 05 ?? ?? ?? ?? 0F 57 C0 F3 0F 10 48 10";
// CLASSIC: render_player_window 0x7E2130 and the render frame's ordinary call (0x7E16F9: mov ecx,[rbx+4]; call;
// xor edx,edx; mov rcx,rbx; call render_player_window; call ...).
constexpr char kH2PlayerWindowSig[] =
    "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 00 02 00 00 48 8B 05 ?? ?? ?? ?? 4C 8D 3D";
constexpr char kH2PlayerWindowCallSig[] = "8B 4B 04 E8 ?? ?? ?? ?? 33 D2 48 8B CB E8 ?? ?? ?? ?? E8";
// ANNIVERSARY: the Saber scene render 0x2DF190 and its view record arithmetic (0x2DF2C5: record =
// *slot + 0x150 + view * 0x758; the camera at record + 0x20).
constexpr char kH2SceneSig[] =
    "40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 70 FD FF FF 48 81 EC 90 03 00 00 48 C7 45 C0 FE FF FF FF";
constexpr char kH2SceneRecordSig[] =
    "48 8B 05 ?? ?? ?? ?? 48 69 CB ?? ?? 00 00 48 89 4D ?? 48 8D 98 ?? ?? 00 00 48 03 D9 4D 89 AE ?? ?? ?? ?? 48 8D 43 "
    "??";
// The classic render driver 0x95FEC0 tests the renderer gate byte first (`cmp byte [gate],0; jne <skip>`): 0 = the
// classic tree renders, otherwise the remastered (Saber) renderer owns the frame (E-H2 evidence: 0xE70CF8).
constexpr char kH2ClassicDriverSig[] = "40 53 48 83 EC 20 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? E8";
// The Blam -> Saber camera bridge 0x5F510 and, inside it, the scene slot and the four engine constants it reads.
constexpr char kH2BridgeSig[] = "48 89 5C 24 08 57 48 81 EC F0 00 00 00 48 63 D9 0F B6 FA 8B CB E8";
constexpr char kH2BridgeSceneSig[] = "48 8B 05 ?? ?? ?? ?? 3B 98 ?? ?? 00 00 0F 8D";
constexpr char kH2BridgeConstSig[] =
    "F3 0F 10 25 ?? ?? ?? ?? 33 C0 F2 0F 10 15 ?? ?? ?? ?? 48 8B CB F3 0F 10 1D ?? ?? ?? ?? F3 0F 10 4C 24 40";
constexpr char kH2BridgeOffsetXSig[] = "F3 0F 58 35 ?? ?? ?? ??";
constexpr char kH2BridgeOffsetZSig[] = "F3 0F 58 2D ?? ?? ?? ??";
// Layouts the code above confirms (E-H2-3 / E-H2-21): the window's render camera at +0x0C (position +0, forward
// +0xC, up +0x18, vertical FOV +0x28); the view record's flags at +0 (main view: (flags & 0x4C) == 0), the
// collection's view count at +0x148; the Saber camera's horizontal FOV (degrees) at +0x150 of the camera copy.
// ADDR-HYGIENE: dev-only.
constexpr size_t kH2WindowCamera = 0x0C, kH2CamPos = 0x00, kH2CamFwd = 0x0C, kH2CamUp = 0x18, kH2CamVFov = 0x28;
constexpr size_t kH2ViewCount = 0x148, kH2SaberHFov = 0x150;
constexpr uint32_t kH2NonMainViewFlags = 0x4C;

// ---- the Halo Reach anchors (pinned haloreach.dll; each unique in the module) ----------------------------------------
// object_new 0x46D6E0: push rbx..r15; sub rsp,0xA8; mov eax,[rcx+18]; or r12,-1; xor r8d,r8d; shr eax,4.
constexpr char kReachObjectNewSig[] =
    "40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC A8 00 00 00 8B 41 18 49 83 CC FF 45 33 C0 C1 E8 04";
// Its copy of the placement block into the object (0x46D9A6): position, forward, up, velocity (movsd + mov each).
constexpr char kReachPlacementCopySig[] =
    "F2 41 0F 10 46 ?? F2 0F 11 47 ?? 41 8B 46 ?? 89 47 ?? F2 41 0F 10 46 ?? F2 0F 11 47 ?? 41 8B 46 ?? 89 47 ?? F2 41 "
    "0F 10 46 ?? F2 0F 11 47 ?? 41 8B 46 ?? 89 47 ?? F2 41 0F 10 46 ?? F2 0F 11 47 ?? 41 8B 46 ?? 89 47";
// The projectile transaction's two calls of object_new (0x4C3CDA: lea rcx,[rbp+disp]; call; xor r11d,r11d; jmp; call).
constexpr char kReachFireCallsSig[] =
    "48 8D 8D ?? ?? 00 00 E8 ?? ?? ?? ?? 45 33 DB E9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B F8 3B C3 0F 84";
// collision_test_vector 0x12969C (game.cpp kReachCollisionVectorSignature).
constexpr char kReachCollisionSig[] =
    "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 F0 97 FE FF B8 10 69 01 00 E8 ?? ?? ?? ?? 48 2B E0 0F "
    "29 B4 24 00 69 01 00 0F 29 BC 24 F0 68 01 00";
// render_camera_from_observer 0x287DFC (reach_render_logic.h), its copy map (0x287E49) and the world render's call
// (0x26C2CC: mov [rdi+3A4],eax; mov [rdi+3A8],r12d; call; test rbx,rbx; je).
constexpr char kReachCameraSig[] =
    "48 89 5C 24 08 57 48 83 EC 30 48 8B D9 0F 29 74 24 20 B1 01 48 8B FA E8 ?? ?? ?? ?? 0F 28 D8 48 85 FF 74";
constexpr char kReachCameraCopySig[] =
    "F2 0F 10 47 28 F2 0F 11 43 0C 8B 47 30 89 43 14 F2 0F 10 47 34 F2 0F 11 43 18 8B 47 3C 89 43 20 F3 0F 10 47 6C F3 "
    "0F 11 43 28";
constexpr char kReachWorldCameraCallSig[] = "89 87 A4 03 00 00 44 89 A7 A8 03 00 00 E8 ?? ?? ?? ?? 48 85 DB 74";

// ---- the Halo 4 anchors (pinned halo4.dll; each unique in the module) ------------------------------------------------
constexpr char kH4ObjectNewSig[] =
    "48 8B C4 48 89 48 08 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC C8 00 00 00 0F 29 70 A8 4C 8D 69 18 41 8B 45 00";
constexpr char kH4PlacementCopySig[] =
    "F2 41 0F 10 46 ?? F2 0F 11 46 ?? 41 8B 46 ?? 89 46 ?? F2 41 0F 10 46 ?? F2 0F 11 46 ?? 41 8B 46 ?? 89 46 ?? F2 41 "
    "0F 10 46 ?? F2 0F 11 46 ?? 41 8B 46 ?? 89 86 ?? ?? 00 00 F2 41 0F 10 46 ?? F2 0F 11 86 ?? ?? 00 00 41 8B 46 ?? 89 "
    "86";
constexpr char kH4FireCallsSig[] =
    "48 8D 8D ?? ?? 00 00 E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B E0 3B C3 0F 84";
// PhysicsRayCast 0x1C1D4C (game.cpp kRayCastSignature).
constexpr char kH4RayCastSig[] =
    "48 8B C4 55 56 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 10 0A 00 00 48 C7 44 24 30 FE FF FF FF 48 89 58 18 48 8B 05 ?? ?? "
    "?? ?? 48 33 C4 48 89 85 00 09 00 00 48 8B F2 48 8B F9";
// The player-view setup 0x374C84 (halo4_render_logic.h "player-view-setup-entry") and the converter's copy map that
// proves the observer layout (0x38F074).
constexpr char kH4SetupSig[] =
    "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B D9 0F 29 74 24 40 4C 8D 2D ?? ?? ?? ?? 49 63 E8";
constexpr char kH4ObserverCopySig[] =
    "F2 0F 10 42 28 F2 0F 11 43 0C 8B 42 30 89 43 14 F2 0F 10 42 34 F2 0F 11 43 18 8B 42 3C 89 43 20 F3 0F 10 42 78 F3 "
    "0F 11 43 28 F3 0F 10 72 7C F3 0F 11 73 2C";
// Layouts the copy maps above prove. ADDR-HYGIENE: dev-only.
constexpr size_t kReachCamPos = 0x00, kReachCamFwd = 0x0C, kReachCamUp = 0x18, kReachCamVFov = 0x28;
constexpr size_t kH4ObsPos = 0x00, kH4ObsFwd = 0x28, kH4ObsUp = 0x34, kH4ObsVFov = 0x78;
// Halo 4's ray input: start +0x0C, end +0x18 (game.cpp Halo4PhysicsRayCastInput); its result: fraction +4.
constexpr size_t kH4RayStart = 0x0C, kH4RayEnd = 0x18;

using CamCopyFn = void*(__fastcall*)(void*, void*);
using ObjectNewFn = int32_t(__fastcall*)(void*);
using CollisionFn = uint8_t(__fastcall*)(uint64_t, int32_t, const float*, const float*, int32_t, int32_t, int32_t,
                                          void*);
using H2CollisionFn = uint8_t(__fastcall*)(uint32_t, const float*, const float*, int32_t, int32_t, void*);
using H2PlayerWindowFn = void(__fastcall*)(void*, uint8_t);
using H2SceneFn = void(__fastcall*)(void*, void*, uint32_t);
using H2BridgeFn = void(__fastcall*)(int32_t, uint8_t);
using ReachCameraFn = void(__fastcall*)(void*, const void*);
using H4RayCastFn = bool(__fastcall*)(void*, void*);
using H4SetupFn = void(__fastcall*)(uintptr_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);

uint64_t NowUs()
{
    static LARGE_INTEGER frequency{};
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart / frequency.QuadPart * 1000000 +
                                 now.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart);
}

// ---- state ---------------------------------------------------------------------------------------------------------
enum Renderer : uint8_t { kHalo3 = 0, kH2Classic = 1, kH2Anniversary = 2, kReach = 3, kHalo4 = 4, kRenderers = 5 };
const char* RendererName(int r)
{
    return r == kH2Classic ? "Halo 2 CLASSIC FLAT" : r == kH2Anniversary ? "Halo 2 ANNIVERSARY FLAT"
         : r == kReach ? "Halo Reach FLAT" : r == kHalo4 ? "Halo 4 FLAT" : "Halo 3 FLAT";
}

struct Camera
{
    bool valid = false;
    uint64_t us = 0;
    float pos[3]{}, fwd[3]{}, up[3]{};
    float tanX = 0.0f, tanY = 0.0f;
    uint8_t renderer = kHalo3;
    int64_t tick = -1;     // the simulation tick counter when this camera was built (Halo 2; -1 unknown)
    float alpha = NAN;     // the interpolation factor it was drawn at (Halo 2)
    DWORD thread = 0;
};

struct Round
{
    bool used = false;
    uint64_t fireUs = 0;
    float spawn[3]{}, fwd[3]{};
    Camera aim{};             // the newest camera taken before the fire: the crosshair the player fired through
    bool velValid = false;
    float vel[3]{};           // the camera eye's velocity at the fire (wu/s)
    uint32_t queries = 0;
    int64_t fireTick = -1;
};

struct Pending
{
    bool used = false;
    float impact[3]{};
    uint64_t impactUs = 0;
    int64_t impactTick = -1;
    Round round{};
};

// One Saber camera bridge push (main thread): the matrix it built, and the tick / factor at that moment.
struct BridgePush
{
    bool used = false;
    float m[16]{};
    int64_t tick = -1;
    float alpha = NAN;
    uint64_t us = 0;
};

struct Halo2Hooks
{
    HMODULE module = nullptr;
    uintptr_t base = 0;
    void* newTarget = nullptr;
    void* colTarget = nullptr;
    void* windowTarget = nullptr;
    void* sceneTarget = nullptr;
    void* bridgeTarget = nullptr;
    ObjectNewFn newOriginal = nullptr;
    H2CollisionFn colOriginal = nullptr;
    H2PlayerWindowFn windowOriginal = nullptr;
    H2SceneFn sceneOriginal = nullptr;
    H2BridgeFn bridgeOriginal = nullptr;
    uintptr_t fireReturn = 0, windowReturn = 0;
    uint32_t placePos = 0, placeFwd = 0, placeVel = 0;
    uintptr_t gameTimeSlot = 0, factorGlobal = 0, rendererGate = 0;
    uintptr_t viewCollectionSlot = 0, saberSceneSlot = 0;
    uint32_t viewStride = 0, viewArray = 0, viewCamera = 0;
    uint32_t saberCameraList = 0x100;   // the scene's camera list (+0x100) and count (+0x108; decoded below)
    uint32_t saberCameraCount = 0;
    Halo2SaberCameraConstants constants{};
    uintptr_t constantAddress[4]{};      // offset X, Z, Y, forward scale: read each frame (module globals)
};

// Halo Reach and Halo 4: object_new from the fire, the collision query, the frame's camera, and MCC's frame clock.
struct ClockedHooks
{
    HMODULE module = nullptr;
    uintptr_t base = 0;
    void* newTarget = nullptr;
    void* colTarget = nullptr;
    void* camTarget = nullptr;
    ObjectNewFn newOriginal = nullptr;
    CollisionFn reachColOriginal = nullptr;   // Reach
    H4RayCastFn rayOriginal = nullptr;        // Halo 4
    ReachCameraFn reachCamOriginal = nullptr; // Reach
    H4SetupFn setupOriginal = nullptr;        // Halo 4
    uintptr_t fireReturnA = 0, fireReturnB = 0, worldCameraReturn = 0;
    uint32_t placePos = 0, placeFwd = 0, placeVel = 0;
    uint32_t cameras = 0, camerasOtherSite = 0;   // under the probe lock
};

struct Probe
{
    SRWLOCK lock = SRWLOCK_INIT;
    // Halo 3 hooks.
    void* camTarget = nullptr;
    void* newTarget = nullptr;
    void* colTarget = nullptr;
    CamCopyFn camOriginal = nullptr;
    ObjectNewFn newOriginal = nullptr;
    CollisionFn colOriginal = nullptr;
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> ready{false};
    HMODULE module = nullptr;
    uintptr_t fireReturn = 0;
    uint32_t placePos = 0, placeFwd = 0;
    // Halo 2 hooks.
    Halo2Hooks h2{};
    std::atomic<bool> h2Ready{false};
    // Halo Reach and Halo 4 hooks, and their frame clocks.
    ClockedHooks reach{}, h4{};
    std::atomic<bool> reachReady{false}, h4Ready{false};
    mcc_clock::Clock reachClock{}, h4Clock{};
    // Present (dxgi.dll: installed once, never retired).
    PresentFn presentOriginal = nullptr;
    Present1Fn present1Original = nullptr;
    // Under `lock`.
    Camera latest{};                  // the newest camera
    Camera frame{};                   // the first camera taken since the last Present: this frame's
    uint32_t framePasses = 0;         // cameras taken in this frame
    float frameSpreadMm = 0.0f;       // the largest distance of another pass's camera from the frame's
    static constexpr int kRounds = 16, kPending = 16, kPushes = 16;
    Round rounds[kRounds]{};
    Pending pending[kPending]{};
    std::atomic<uint32_t> live{0};    // rounds + pending in use (the collision hook takes the lock only while any)
    BridgePush pushes[kPushes]{};
    uint32_t pushNext = 0;
    as_seen::Window window[kRenderers]{};
    as_seen::CameraCensus census[kRenderers]{};   // per presented frame (also the source of the eye's velocity)
    // Window counters.
    uint32_t presents = 0, cameraCopies = 0, spawnsAtCall = 0, spawnsLocal = 0, spawnsForeign = 0, queriesMatched = 0,
             queriesAmbiguous = 0, roundsLost = 0, roundsEvicted = 0, faults = 0;
    uint32_t framesBy[kRenderers]{};
    uint32_t presentsOnCameraThread = 0, presentsWithFrame = 0;
    uint32_t sceneViews = 0, sceneMain = 0, sceneUnreadable = 0, pushMatched = 0, pushUnmatched = 0;
    uint32_t windowOther = 0, viewsWrongRenderer = 0;
    uint32_t speedN = 0;
    double speedSum = 0.0, speedMax = 0.0;
    double passesSum = 0.0, spreadMaxMm = 0.0, tanXSum = 0.0, fovSum = 0.0;
    uint32_t tanXN = 0, fovN = 0;
    uint64_t firstPresentUs = 0, lastPresentUs = 0;
};
Probe g_probe;
float g_vrHudWidth = 5242.0f;   // the VR HUD raster width the art fraction refers to (HaloMCCFlatProbe.cfg)

bool Read3(const void* at, float out[3])
{
    __try
    {
        std::memcpy(out, at, sizeof(float) * 3);
        return as_seen::Finite3(out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
bool Read1(const void* at, float& out)
{
    __try
    {
        std::memcpy(&out, at, sizeof(float));
        return std::isfinite(out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
bool ReadBytes(const void* at, void* out, size_t size)
{
    __try
    {
        std::memcpy(out, at, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Halo 2's game tick and the interpolator's factor, read now (-1 / NaN when unreadable). The game_time object is the
// slot's pointee; +0 initialized (exactly 1 while a level runs), +8 the current tick.
void Halo2Clock(int64_t& tick, float& alpha)
{
    tick = -1;
    alpha = NAN;
    const auto& h = g_probe.h2;
    if (!h.gameTimeSlot)
        return;
    __try
    {
        const uint8_t* object = *reinterpret_cast<const uint8_t* const*>(h.gameTimeSlot);
        if (object && object[0] == 1)
            tick = *reinterpret_cast<const uint32_t*>(object + 8);
        if (h.factorGlobal)
        {
            const float f = *reinterpret_cast<const float*>(h.factorGlobal);
            if (std::isfinite(f) && f >= 0.0f && f <= 1.0f)
                alpha = f;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        tick = -1;
        alpha = NAN;
    }
}

// Halo 2's renderer now: 1 Anniversary (the gate byte set: the classic tree is skipped), 0 Classic, -1 unreadable.
int Halo2RemasteredNow()
{
    const auto& h = g_probe.h2;
    if (!h.rendererGate)
        return -1;
    __try
    {
        return *reinterpret_cast<const volatile uint8_t*>(h.rendererGate) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The camera eye's velocity from a census history (40-200 ms back), under the lock.
bool EyeVelocity(const as_seen::CameraCensus& c, const float pos[3], uint64_t us, float out[3])
{
    for (int k = 1; k <= c.count; ++k)
    {
        const int i = (c.head + as_seen::CameraCensus::kHistory - k) % as_seen::CameraCensus::kHistory;
        if (c.us[i] >= us)
            continue;
        const uint64_t dt = us - c.us[i];
        if (dt < 40000)
            continue;
        if (dt > 200000)
            return false;
        for (int axis = 0; axis < 3; ++axis)
            out[axis] = (pos[axis] - c.pos[i][axis]) / (float(dt) * 1.0e-6f);
        return as_seen::Finite3(out);
    }
    return false;
}

// A camera this frame was drawn from (under the lock): the first one since the last Present is the frame's.
void NoteCamera(const Camera& c)
{
    auto& p = g_probe;
    ++p.cameraCopies;
    p.latest = c;
    if (!p.frame.valid)
    {
        p.frame = c;
        p.framePasses = 1;
        if (c.tanX > 0.2f && c.tanX < 3.0f)
        {
            p.tanXSum += c.tanX;
            ++p.tanXN;
        }
    }
    else
    {
        ++p.framePasses;
        const float d[3]{c.pos[0] - p.frame.pos[0], c.pos[1] - p.frame.pos[1], c.pos[2] - p.frame.pos[2]};
        const float mm = as_seen::Length(d) * as_seen::kMmPerWu;
        p.frameSpreadMm = mm > p.frameSpreadMm ? mm : p.frameSpreadMm;
    }
}

// A presented frame (under the lock): the census, and every impact this frame is the first to show.
void FinishFrame(uint64_t presentUs)
{
    auto& p = g_probe;
    ++p.presents;
    if (!p.firstPresentUs)
        p.firstPresentUs = presentUs;
    p.lastPresentUs = presentUs;
    if (!p.frame.valid)
        return;
    const int r = p.frame.renderer < kRenderers ? p.frame.renderer : kHalo3;
    ++p.framesBy[r];
    ++p.presentsWithFrame;
    if (p.frame.thread == GetCurrentThreadId())
        ++p.presentsOnCameraThread;
    p.passesSum += p.framePasses;
    p.spreadMaxMm = p.frameSpreadMm > p.spreadMaxMm ? p.frameSpreadMm : p.spreadMaxMm;
    float vel[3]{};
    const bool velKnown = EyeVelocity(p.census[r], p.frame.pos, p.frame.us, vel);
    p.census[r].Add(p.frame.pos, p.frame.us, p.frame.tick, p.frame.alpha);
    for (auto& q : p.pending)
    {
        // Does this frame show the hit? Its world is the state its camera was built in: past the hit's tick when both
        // ticks are known (Halo 2: the counter steps at the end of each tick, so a hit reads one less than any later
        // camera; the Saber thread can draw an older main frame after a newer one simulated the hit), else by time.
        const bool shows = p.frame.tick >= 0 && q.impactTick >= 0 ? p.frame.tick > q.impactTick
                                                                   : q.impactUs <= p.frame.us;
        if (!q.used || !shows)
            continue;
        const float* v = velKnown ? vel : (q.round.velValid ? q.round.vel : nullptr);
        as_seen::Sample s{};
        s.seen = as_seen::Measure(q.impact, p.frame.pos, p.frame.fwd, v);
        s.stockValid = true;   // in flat the crosshair on screen IS the stock one
        s.stock = s.seen;
        s.atFireValid = q.round.aim.valid;
        if (s.atFireValid)
            s.atFire = as_seen::Measure(q.impact, q.round.aim.pos, q.round.aim.fwd, v);
        s.aimCamUs = q.round.aim.us;
        s.fireUs = q.round.fireUs;
        s.impactUs = q.impactUs;
        s.frameCamUs = p.frame.us;
        s.presentUs = presentUs;
        s.ticksFireToImpact = q.round.queries ? static_cast<int32_t>(q.round.queries - 1) : -1;
        if (p.frame.tick >= 0 && q.round.aim.tick >= 0 && p.frame.tick >= q.round.aim.tick)
            s.ticksAimToFrame = static_cast<int32_t>(p.frame.tick - q.round.aim.tick);
        if (p.frame.tick >= 0 && q.impactTick >= 0 && p.frame.tick >= q.impactTick)
            s.ticksImpactToFrame = static_cast<int32_t>(p.frame.tick - q.impactTick);
        s.frameAlpha = p.frame.alpha;
        if (q.round.aim.valid)
        {
            const float moved[3]{p.frame.pos[0] - q.round.aim.pos[0], p.frame.pos[1] - q.round.aim.pos[1],
                                 p.frame.pos[2] - q.round.aim.pos[2]};
            s.eyeMovedAlongMm = as_seen::AlongTravelMm(moved, p.frame.fwd, v, &s.eyeMovedKnown);
        }
        // Radii only for Halo 3, whose VR reticle art geometry the flat art fraction refers to.
        const float radius = r == kHalo3 ? as_seen::FlatArtHalfAngleDeg(g_vrHudWidth, p.frame.tanX) : 0.0f;
        p.window[r].Add(s, std::isfinite(radius) ? radius : 0.0f);
        q = Pending{};
        p.live.fetch_sub(1, std::memory_order_acq_rel);
    }
    p.frame = Camera{};
    p.framePasses = 0;
    p.frameSpreadMm = 0.0f;
}

// A projectile the fire routine created (under no lock): track it when it is the local player's.
void NoteSpawn(const float spawn[3], const float fwd[3], const float* velocity, int64_t tick)
{
    auto& p = g_probe;
    const uint64_t nowUs = NowUs();
    AcquireSRWLockExclusive(&p.lock);
    ++p.spawnsAtCall;
    const Camera cam = p.latest;
    bool local = false;
    if (cam.valid && nowUs - cam.us < 100000)
    {
        const float d[3]{spawn[0] - cam.pos[0], spawn[1] - cam.pos[1], spawn[2] - cam.pos[2]};
        const float deg = shot_lag::Length(d) * as_seen::kMetresPerWu < 1.5f
            ? std::acos(std::fmax(-1.0f, std::fmin(1.0f, as_seen::Dot(fwd, cam.fwd) /
                                                      (as_seen::Length(fwd) * as_seen::Length(cam.fwd) +
                                                       1.0e-9f)))) * as_seen::kRadToDeg
            : 180.0f;
        local = deg < 30.0f;
    }
    if (!local)
        ++p.spawnsForeign;
    else
    {
        ++p.spawnsLocal;
        if (velocity && as_seen::Finite3(velocity))
        {
            const double mps = double(as_seen::Length(velocity)) * as_seen::kMetresPerWu;
            ++p.speedN;
            p.speedSum += mps;
            p.speedMax = mps > p.speedMax ? mps : p.speedMax;
        }
        Round* slot = nullptr;
        for (auto& r : p.rounds)
            if (!r.used)
            {
                slot = &r;
                break;
            }
        if (!slot)
        {
            slot = &p.rounds[0];
            for (auto& r : p.rounds)
                if (r.fireUs < slot->fireUs)
                    slot = &r;
            ++p.roundsEvicted;
        }
        else
            p.live.fetch_add(1, std::memory_order_acq_rel);
        *slot = Round{};
        slot->used = true;
        slot->fireUs = nowUs;
        slot->fireTick = tick;
        std::memcpy(slot->spawn, spawn, sizeof(slot->spawn));
        std::memcpy(slot->fwd, fwd, sizeof(slot->fwd));
        slot->aim = cam;
        const int r = cam.renderer < kRenderers ? cam.renderer : kHalo3;
        slot->velValid = EyeVelocity(p.census[r], cam.pos, cam.us, slot->vel);
    }
    ReleaseSRWLockExclusive(&p.lock);
}

// ---- the Halo 3 detours (forward exactly; read only) ----------------------------------------------------------------
__declspec(noinline) void* __fastcall CamCopyDetour(void* dst, void* src)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    void* result = nullptr;
    __try
    {
        result = p.camOriginal ? p.camOriginal(dst, src) : nullptr;
        if (src && p.ready.load(std::memory_order_acquire))
        {
            const auto* bytes = static_cast<const uint8_t*>(src);
            Camera c{};
            c.us = NowUs();
            c.renderer = kHalo3;
            c.thread = GetCurrentThreadId();
            c.valid = Read3(bytes + kSrcPos, c.pos) && Read3(bytes + kSrcFwd, c.fwd) && Read3(bytes + kSrcUp, c.up) &&
                      Read1(bytes + kSrcProjX, c.tanX) && Read1(bytes + kSrcProjY, c.tanY);
            AcquireSRWLockExclusive(&p.lock);
            if (c.valid)
                NoteCamera(c);
            else
                ++p.faults;
            ReleaseSRWLockExclusive(&p.lock);
        }
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

__declspec(noinline) int32_t __fastcall ObjectNewDetour(void* placement)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int32_t result = -1;
    __try
    {
        const bool ours = reinterpret_cast<uintptr_t>(_ReturnAddress()) == p.fireReturn && placement &&
                          p.ready.load(std::memory_order_acquire);
        float spawn[3]{}, fwd[3]{};
        const bool placed = ours && Read3(static_cast<const uint8_t*>(placement) + p.placePos, spawn) &&
                            Read3(static_cast<const uint8_t*>(placement) + p.placeFwd, fwd);
        if (p.newOriginal)
            result = p.newOriginal(placement);
        if (placed && result != -1)
            NoteSpawn(spawn, fwd, nullptr, -1);
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// A query that starts on a tracked round's spawn line and runs along its forward (as_seen::QueryOnRound) is that
// round's. The closest line wins; two within 1 cm of each other go to the older round.
void NoteQueryCore(const float s[3], const float v[3], bool landed, const float point[3], int64_t tick);
void NoteQuery(const float* start, const float* vector, const void* result, uint8_t hit, int64_t tick)
{
    auto& p = g_probe;
    if (!p.live.load(std::memory_order_acquire))
        return;
    float s[3]{}, v[3]{};
    if (!Read3(start, s) || !Read3(vector, v) || !(as_seen::Length(v) > 1.0e-6f))
        return;
    float point[3]{}, fraction = NAN;
    bool landed = false;
    if (hit && result)
    {
        landed = Read1(static_cast<const uint8_t*>(result) + 4, fraction) &&
                 Read3(static_cast<const uint8_t*>(result) + 8, point) && fraction >= 0.0f && fraction <= 1.0f;
    }
    NoteQueryCore(s, v, landed, point, tick);
}
// A query whose hit point is start + fraction x vector (Reach's collision_test_vector, Halo 4's ray: the fraction at
// result +4 is the layout both VR lanes already read; no point field is assumed).
void NoteQueryFraction(const float* start, const float* vector, const void* result, bool hit, int64_t tick)
{
    auto& p = g_probe;
    if (!p.live.load(std::memory_order_acquire))
        return;
    float s[3]{}, v[3]{};
    if (!Read3(start, s) || !Read3(vector, v) || !(as_seen::Length(v) > 1.0e-6f))
        return;
    float point[3]{}, fraction = NAN;
    bool landed = false;
    if (hit && result && Read1(static_cast<const uint8_t*>(result) + 4, fraction) && fraction >= 0.0f &&
        fraction <= 1.0f)
    {
        for (int axis = 0; axis < 3; ++axis)
            point[axis] = s[axis] + v[axis] * fraction;
        landed = as_seen::Finite3(point);
    }
    NoteQueryCore(s, v, landed, point, tick);
}
void NoteQueryCore(const float s[3], const float v[3], bool landed, const float point[3], int64_t tick)
{
    auto& p = g_probe;
    const uint64_t nowUs = NowUs();
    AcquireSRWLockExclusive(&p.lock);
    Round* best = nullptr;
    float bestMiss = 0.0f;
    bool ambiguous = false;
    for (auto& r : p.rounds)
    {
        if (!r.used)
            continue;
        if (nowUs - r.fireUs > 3000000)
        {
            ++p.roundsLost;
            r = Round{};
            p.live.fetch_sub(1, std::memory_order_acq_rel);
            continue;
        }
        float miss = 0.0f;
        if (!as_seen::QueryOnRound(r.spawn, r.fwd, s, v, &miss))
            continue;
        if (best && std::fabs(miss - bestMiss) < 0.01f)
        {
            ambiguous = true;
            if (r.fireUs < best->fireUs)
            {
                best = &r;
                bestMiss = miss;
            }
        }
        else if (!best || miss < bestMiss)
        {
            best = &r;
            bestMiss = miss;
        }
    }
    if (best)
    {
        ++p.queriesMatched;
        if (ambiguous)
            ++p.queriesAmbiguous;
        ++best->queries;
        if (landed)
        {
            Pending* slot = nullptr;
            for (auto& q : p.pending)
                if (!q.used)
                {
                    slot = &q;
                    break;
                }
            if (!slot)
            {
                slot = &p.pending[0];
                for (auto& q : p.pending)
                    if (q.impactUs < slot->impactUs)
                        slot = &q;
                ++p.window[slot->round.aim.renderer < kRenderers ? slot->round.aim.renderer : kHalo3].unframed;
                p.live.fetch_sub(1, std::memory_order_acq_rel);   // the evicted pending's
            }
            *slot = Pending{};
            slot->used = true;
            std::memcpy(slot->impact, point, sizeof(point));
            slot->impactUs = nowUs;
            slot->impactTick = tick;
            slot->round = *best;
            *best = Round{};   // the round's count moves to its pending: `live` unchanged
        }
    }
    ReleaseSRWLockExclusive(&p.lock);
}

__declspec(noinline) uint8_t __fastcall CollisionDetour(uint64_t flags, int32_t mode, const float* start,
                                                        const float* desired, int32_t ignoredA, int32_t ignoredB,
                                                        int32_t ignoredC, void* result)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint8_t native = 0;
    __try
    {
        if (p.colOriginal)
            native = p.colOriginal(flags, mode, start, desired, ignoredA, ignoredB, ignoredC, result);
        if (p.ready.load(std::memory_order_acquire) && start && desired)
            NoteQuery(start, desired, result, native, -1);
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return native;
}

// ---- the Halo 2 detours (forward exactly; read only) ----------------------------------------------------------------
__declspec(noinline) int32_t __fastcall H2ObjectNewDetour(void* placement)
{
    auto& p = g_probe;
    auto& h = p.h2;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int32_t result = -1;
    __try
    {
        const bool ours = reinterpret_cast<uintptr_t>(_ReturnAddress()) == h.fireReturn && placement &&
                          p.h2Ready.load(std::memory_order_acquire);
        float spawn[3]{}, fwd[3]{}, vel[3]{};
        const bool placed = ours && Read3(static_cast<const uint8_t*>(placement) + h.placePos, spawn) &&
                            Read3(static_cast<const uint8_t*>(placement) + h.placeFwd, fwd);
        const bool velRead = placed && Read3(static_cast<const uint8_t*>(placement) + h.placeVel, vel);
        if (h.newOriginal)
            result = h.newOriginal(placement);
        if (placed && result != -1)
        {
            int64_t tick = -1;
            float alpha = NAN;
            Halo2Clock(tick, alpha);
            NoteSpawn(spawn, fwd, velRead ? vel : nullptr, tick);
        }
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

__declspec(noinline) uint8_t __fastcall H2CollisionDetour(uint32_t flags, const float* start, const float* vector,
                                                          int32_t ignoredA, int32_t ignoredB, void* result)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint8_t native = 0;
    __try
    {
        if (p.h2.colOriginal)
            native = p.h2.colOriginal(flags, start, vector, ignoredA, ignoredB, result);
        if (p.h2Ready.load(std::memory_order_acquire) && start && vector && p.live.load(std::memory_order_acquire))
        {
            int64_t tick = -1;
            float alpha = NAN;
            Halo2Clock(tick, alpha);
            NoteQuery(start, vector, result, native, tick);
        }
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return native;
}

// CLASSIC: the window's render camera, read before the native render (this frame's, built from the observer this
// frame on this main thread), then the render runs untouched.
__declspec(noinline) void __fastcall H2PlayerWindowDetour(void* window, uint8_t flag)
{
    auto& p = g_probe;
    auto& h = p.h2;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (window && p.h2Ready.load(std::memory_order_acquire))
        {
            const bool remastered = Halo2RemasteredNow() == 1;
            if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != h.windowReturn || remastered)
            {
                AcquireSRWLockExclusive(&p.lock);
                ++(remastered ? p.viewsWrongRenderer : p.windowOther);
                ReleaseSRWLockExclusive(&p.lock);
            }
            else
            {
                const auto* camera = static_cast<const uint8_t*>(window) + kH2WindowCamera;
                Camera c{};
                c.us = NowUs();
                c.renderer = kH2Classic;
                c.thread = GetCurrentThreadId();
                float vfov = 0.0f;
                c.valid = Read3(camera + kH2CamPos, c.pos) && Read3(camera + kH2CamFwd, c.fwd) &&
                          Read3(camera + kH2CamUp, c.up) && Read1(camera + kH2CamVFov, vfov);
                Halo2Clock(c.tick, c.alpha);
                AcquireSRWLockExclusive(&p.lock);
                if (c.valid)
                {
                    if (vfov > 0.1f && vfov < 3.0f)
                    {
                        p.fovSum += vfov * as_seen::kRadToDeg;
                        ++p.fovN;
                    }
                    NoteCamera(c);
                }
                else
                    ++p.faults;
                ReleaseSRWLockExclusive(&p.lock);
            }
        }
        if (h.windowOriginal)
            h.windowOriginal(window, flag);
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// The Saber constants (module globals the bridge reads), read now.
bool ReadSaberConstants(Halo2SaberCameraConstants& out)
{
    const auto& h = g_probe.h2;
    float v[4]{};
    for (int i = 0; i < 4; ++i)
        if (!h.constantAddress[i] || !Read1(reinterpret_cast<const void*>(h.constantAddress[i]), v[i]))
            return false;
    out.offsetX = v[0];
    out.offsetZ = v[1];
    out.offsetY = v[2];
    out.forwardScale = v[3];
    return true;
}

// ANNIVERSARY: the main view's latched camera (record+0x20), before the native scene render reads it.
__declspec(noinline) void __fastcall H2SceneDetour(void* context, void* unused, uint32_t view)
{
    auto& p = g_probe;
    auto& h = p.h2;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        const int remastered = Halo2RemasteredNow();
        if (remastered == 0 && p.h2Ready.load(std::memory_order_acquire))
        {
            AcquireSRWLockExclusive(&p.lock);
            ++p.viewsWrongRenderer;   // a Saber view while the gate says Classic: not this frame's camera
            ReleaseSRWLockExclusive(&p.lock);
        }
        else if (p.h2Ready.load(std::memory_order_acquire) && h.viewCollectionSlot)
        {
            uintptr_t collection = 0;
            int32_t count = 0;
            uint32_t flags = 0;
            float m[16]{};
            float hfov = 0.0f;
            const uint64_t nowUs = NowUs();
            bool read = ReadBytes(reinterpret_cast<const void*>(h.viewCollectionSlot), &collection,
                                  sizeof(collection)) &&
                        collection &&
                        ReadBytes(reinterpret_cast<const void*>(collection + kH2ViewCount), &count, sizeof(count)) &&
                        count > 0 && count <= 0x50 && view < static_cast<uint32_t>(count);
            const uintptr_t record = read ? collection + h.viewArray + uintptr_t(view) * h.viewStride : 0;
            read = read && ReadBytes(reinterpret_cast<const void*>(record), &flags, sizeof(flags));
            const bool main = read && (flags & kH2NonMainViewFlags) == 0;
            Halo2SaberCameraConstants constants{};
            Halo2CameraBasis basis{};
            const bool camera = main &&
                ReadBytes(reinterpret_cast<const void*>(record + h.viewCamera), m, sizeof(m)) &&
                ReadSaberConstants(constants) && Halo2SaberViewMatrixToCamera(m, constants, basis);
            if (camera)
                (void)Read1(reinterpret_cast<const void*>(record + h.viewCamera + kH2SaberHFov), hfov);
            AcquireSRWLockExclusive(&p.lock);
            ++p.sceneViews;
            if (!read)
                ++p.sceneUnreadable;
            if (main)
                ++p.sceneMain;
            if (camera)
            {
                Camera c{};
                c.valid = true;
                c.us = nowUs;
                c.renderer = kH2Anniversary;
                c.thread = GetCurrentThreadId();
                std::memcpy(c.pos, basis.position, sizeof(c.pos));
                std::memcpy(c.fwd, basis.forward, sizeof(c.fwd));
                std::memcpy(c.up, basis.up, sizeof(c.up));
                if (hfov > 5.0f && hfov < 170.0f)
                {
                    c.tanX = std::tan(hfov * 0.5f / as_seen::kRadToDeg);
                    p.fovSum += hfov;
                    ++p.fovN;
                }
                // The bridge push that built this matrix: its tick and factor (bit-exact match, newest first).
                const BridgePush* match = nullptr;
                for (uint32_t k = 1; k <= Probe::kPushes && !match; ++k)
                {
                    const BridgePush& push = p.pushes[(p.pushNext + Probe::kPushes - k) % Probe::kPushes];
                    if (push.used && !std::memcmp(push.m, m, sizeof(m)))
                        match = &push;
                }
                if (match)
                {
                    ++p.pushMatched;
                    c.tick = match->tick;
                    c.alpha = match->alpha;
                }
                else
                    ++p.pushUnmatched;
                NoteCamera(c);
            }
            else if (main)
                ++p.faults;
            ReleaseSRWLockExclusive(&p.lock);
        }
        if (h.sceneOriginal)
            h.sceneOriginal(context, unused, view);
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// ANNIVERSARY: the bridge built user 0's Saber camera from the observer: keep its matrix with the clock read now.
__declspec(noinline) void __fastcall H2BridgeDetour(int32_t user, uint8_t flag)
{
    auto& p = g_probe;
    auto& h = p.h2;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (h.bridgeOriginal)
            h.bridgeOriginal(user, flag);
        if (user == 0 && p.h2Ready.load(std::memory_order_acquire) && h.saberSceneSlot)
        {
            uintptr_t scene = 0, list = 0, camera = 0;
            int32_t count = 0;
            BridgePush push{};
            const bool read =
                ReadBytes(reinterpret_cast<const void*>(h.saberSceneSlot), &scene, sizeof(scene)) && scene &&
                ReadBytes(reinterpret_cast<const void*>(scene + h.saberCameraCount), &count, sizeof(count)) &&
                count > 0 && count <= 8 &&
                ReadBytes(reinterpret_cast<const void*>(scene + h.saberCameraList), &list, sizeof(list)) && list &&
                ReadBytes(reinterpret_cast<const void*>(list), &camera, sizeof(camera)) && camera &&
                ReadBytes(reinterpret_cast<const void*>(camera), push.m, sizeof(push.m));
            if (read)
            {
                push.used = true;
                push.us = NowUs();
                Halo2Clock(push.tick, push.alpha);
                AcquireSRWLockExclusive(&p.lock);
                p.pushes[p.pushNext++ % Probe::kPushes] = push;
                ReleaseSRWLockExclusive(&p.lock);
            }
        }
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void NotePresent()
{
    const uint64_t nowUs = NowUs();
    AcquireSRWLockExclusive(&g_probe.lock);
    FinishFrame(nowUs);
    ReleaseSRWLockExclusive(&g_probe.lock);
}

HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* chain, UINT sync, UINT flags)
{
    if (!(flags & DXGI_PRESENT_TEST))
        NotePresent();
    return g_probe.presentOriginal(chain, sync, flags);
}

HRESULT STDMETHODCALLTYPE Present1Detour(IDXGISwapChain1* chain, UINT sync, UINT flags,
                                         const DXGI_PRESENT_PARAMETERS* parameters)
{
    if (!(flags & DXGI_PRESENT_TEST))
        NotePresent();
    return g_probe.present1Original(chain, sync, flags, parameters);
}

// ---- the Reach and Halo 4 detours (forward exactly; read only) -------------------------------------------------------
// MCC's frame clock now (the game thread's TLS pointers captured when this thread has them).
void ClockNow(mcc_clock::Clock& clock, int64_t& tick, float& alpha)
{
    (void)clock.Capture();
    clock.Read(tick, alpha);
}

// object_new from the fire routine: track the round when it is the local player's (NoteSpawn's geometry).
int32_t ClockedObjectNew(ClockedHooks& h, mcc_clock::Clock& clock, std::atomic<bool>& ready, void* placement,
                         uintptr_t caller)
{
    const bool ours = (caller == h.fireReturnA || caller == h.fireReturnB) && placement &&
                      ready.load(std::memory_order_acquire);
    float spawn[3]{}, fwd[3]{}, vel[3]{};
    const bool placed = ours && Read3(static_cast<const uint8_t*>(placement) + h.placePos, spawn) &&
                        Read3(static_cast<const uint8_t*>(placement) + h.placeFwd, fwd);
    const bool velRead = placed && Read3(static_cast<const uint8_t*>(placement) + h.placeVel, vel);
    const int32_t result = h.newOriginal ? h.newOriginal(placement) : -1;
    if (placed && result != -1)
    {
        int64_t tick = -1;
        float alpha = NAN;
        ClockNow(clock, tick, alpha);
        NoteSpawn(spawn, fwd, velRead ? vel : nullptr, tick);
    }
    return result;
}

__declspec(noinline) int32_t __fastcall ReachObjectNewDetour(void* placement)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int32_t result = -1;
    __try
    {
        result = ClockedObjectNew(p.reach, p.reachClock, p.reachReady, placement,
                                  reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

__declspec(noinline) int32_t __fastcall H4ObjectNewDetour(void* placement)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int32_t result = -1;
    __try
    {
        result = ClockedObjectNew(p.h4, p.h4Clock, p.h4Ready, placement,
                                  reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

__declspec(noinline) uint8_t __fastcall ReachCollisionDetour(uint64_t flags, int32_t mode, const float* start,
                                                             const float* vector, int32_t ignoredA, int32_t ignoredB,
                                                             int32_t ignoredC, void* result)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint8_t native = 0;
    __try
    {
        if (p.reach.reachColOriginal)
            native = p.reach.reachColOriginal(flags, mode, start, vector, ignoredA, ignoredB, ignoredC, result);
        if (p.reachReady.load(std::memory_order_acquire) && start && vector && p.live.load(std::memory_order_acquire))
        {
            int64_t tick = -1;
            float alpha = NAN;
            ClockNow(p.reachClock, tick, alpha);
            NoteQueryFraction(start, vector, result, native != 0, tick);
        }
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return native;
}

__declspec(noinline) bool __fastcall H4RayCastDetour(void* input, void* output)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool native = false;
    __try
    {
        if (p.h4.rayOriginal)
            native = p.h4.rayOriginal(input, output);
        if (p.h4Ready.load(std::memory_order_acquire) && input && p.live.load(std::memory_order_acquire))
        {
            float s[3]{}, e[3]{};
            if (Read3(static_cast<const uint8_t*>(input) + kH4RayStart, s) &&
                Read3(static_cast<const uint8_t*>(input) + kH4RayEnd, e))
            {
                const float v[3]{e[0] - s[0], e[1] - s[1], e[2] - s[2]};
                int64_t tick = -1;
                float alpha = NAN;
                ClockNow(p.h4Clock, tick, alpha);
                NoteQueryFraction(s, v, output, native, tick);
            }
        }
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return native;
}

// A clocked title's frame camera (under no lock): the census and the frame take it.
void NoteClockedCamera(ClockedHooks& h, mcc_clock::Clock& clock, uint8_t renderer, const uint8_t* camera,
                       size_t pos, size_t fwd, size_t up, size_t vfov)
{
    auto& p = g_probe;
    Camera c{};
    c.us = NowUs();
    c.renderer = renderer;
    c.thread = GetCurrentThreadId();
    float fov = 0.0f;
    c.valid = Read3(camera + pos, c.pos) && Read3(camera + fwd, c.fwd) && Read3(camera + up, c.up) &&
              Read1(camera + vfov, fov);
    ClockNow(clock, c.tick, c.alpha);
    AcquireSRWLockExclusive(&p.lock);
    ++h.cameras;
    if (c.valid)
    {
        if (fov > 0.1f && fov < 3.0f)
        {
            p.fovSum += fov * as_seen::kRadToDeg;
            ++p.fovN;
        }
        NoteCamera(c);
    }
    else
        ++p.faults;
    ReleaseSRWLockExclusive(&p.lock);
}

// REACH: the world render's camera, after the native copy built it from the observer.
__declspec(noinline) void __fastcall ReachCameraDetour(void* dst, const void* src)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (p.reach.reachCamOriginal)
            p.reach.reachCamOriginal(dst, src);
        if (dst && p.reachReady.load(std::memory_order_acquire))
        {
            if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == p.reach.worldCameraReturn)
                NoteClockedCamera(p.reach, p.reachClock, kReach, static_cast<const uint8_t*>(dst), kReachCamPos,
                                  kReachCamFwd, kReachCamUp, kReachCamVFov);
            else
            {
                AcquireSRWLockExclusive(&p.lock);
                ++p.reach.camerasOtherSite;
                ReleaseSRWLockExclusive(&p.lock);
            }
        }
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// HALO 4: the observer the player-view setup is handed (read before the native call; setup does not write it).
__declspec(noinline) void __fastcall H4SetupDetour(uintptr_t view, uint32_t window, uint32_t count, uint32_t mode,
                                                   uint32_t user, uintptr_t observer)
{
    auto& p = g_probe;
    p.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (observer && p.h4Ready.load(std::memory_order_acquire))
        {
            if (user == 0)
                NoteClockedCamera(p.h4, p.h4Clock, kHalo4, reinterpret_cast<const uint8_t*>(observer), kH4ObsPos,
                                  kH4ObsFwd, kH4ObsUp, kH4ObsVFov);
            else
            {
                AcquireSRWLockExclusive(&p.lock);
                ++p.h4.camerasOtherSite;
                ReleaseSRWLockExclusive(&p.lock);
            }
        }
        if (p.h4.setupOriginal)
            p.h4.setupOriginal(view, window, count, mode, user, observer);
    }
    __finally
    {
        p.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// ---- install / retire --------------------------------------------------------------------------------------------
uintptr_t Unique(uintptr_t base, size_t size, const char* pattern)
{
    const uintptr_t hit = sig::Find(base, size, pattern);
    if (!hit || sig::Find(hit + 1, base + size - hit - 1, pattern))
        return 0;
    return hit;
}

uintptr_t FunctionOf(uintptr_t address)
{
    DWORD64 imageBase = 0;
    const auto* entry = RtlLookupFunctionEntry(static_cast<DWORD64>(address), &imageBase, nullptr);
    return entry ? static_cast<uintptr_t>(imageBase) + entry->BeginAddress : 0;
}

// The entry of the function containing `address`, following chained unwind info (UNW_FLAG_CHAININFO) to the
// primary chunk: a large LTCG function is split into several .pdata chunks.
uintptr_t FunctionRoot(uintptr_t address)
{
    DWORD64 imageBase = 0;
    const RUNTIME_FUNCTION* entry = RtlLookupFunctionEntry(static_cast<DWORD64>(address), &imageBase, nullptr);
    if (!entry)
        return 0;
    __try
    {
        DWORD begin = entry->BeginAddress, unwind = entry->UnwindData;
        for (int i = 0; i < 32; ++i)
        {
            const auto* info = reinterpret_cast<const uint8_t*>(imageBase + (unwind & ~3u));
            if (!((info[0] >> 3) & 4))   // UNW_FLAG_CHAININFO
                return static_cast<uintptr_t>(imageBase) + begin;
            const uint8_t codes = info[2];
            const auto* chained = reinterpret_cast<const RUNTIME_FUNCTION*>(info + 4 + ((codes + 1) & ~1) * 2);
            begin = chained->BeginAddress;
            unwind = chained->UnwindData;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return 0;
}

int32_t Rel32(uintptr_t at)
{
    int32_t rel = 0;
    std::memcpy(&rel, reinterpret_cast<const void*>(at), sizeof(rel));
    return rel;
}
uint32_t U32(uintptr_t at)
{
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(value));
    return value;
}

bool Hook(void* target, void* detour, void** original, const char* name)
{
    if (MH_CreateHook(target, detour, original) != MH_OK)
    {
        LOG("probe: %s: the hook was not created", name);
        *original = nullptr;
        return false;
    }
    if (MH_EnableHook(target) != MH_OK)
    {
        LOG("probe: %s: the hook did not enable", name);
        MH_RemoveHook(target);
        *original = nullptr;
        return false;
    }
    return true;
}

void ClearTracking()
{
    auto& p = g_probe;
    AcquireSRWLockExclusive(&p.lock);
    for (auto& r : p.rounds)
        r = Round{};
    for (auto& q : p.pending)
        q = Pending{};
    for (auto& push : p.pushes)
        push = BridgePush{};
    p.live.store(0, std::memory_order_release);
    p.latest = p.frame = Camera{};
    ReleaseSRWLockExclusive(&p.lock);
}

// Disable `targets`, wait for every detour to drain, then remove them. False when a detour stayed busy (the hooks stay
// disabled and are never removed: no-write retirement).
bool RetireTargets(void* const* targets, size_t count, const char* why)
{
    auto& p = g_probe;
    for (size_t i = 0; i < count; ++i)
        if (targets[i])
            MCCVR_DisableHookForRetirement(targets[i]);
    // Quiescence: nothing is inside a detour any more (they are reached only through the patches just disabled).
    for (int i = 0; i < 200 && p.callbacks.load(std::memory_order_acquire); ++i)
        Sleep(10);
    if (p.callbacks.load(std::memory_order_acquire))
    {
        LOG("probe: retiring (%s): a detour is still busy; the hooks stay disabled and are never removed", why);
        return false;
    }
    for (size_t i = 0; i < count; ++i)
        if (targets[i])
            MH_RemoveHook(targets[i]);
    return true;
}

void Retire(const char* why)
{
    auto& p = g_probe;
    p.ready.store(false, std::memory_order_release);
    void* const targets[]{p.camTarget, p.newTarget, p.colTarget};
    const bool removed = RetireTargets(targets, 3, why);
    p.camTarget = p.newTarget = p.colTarget = nullptr;
    if (!removed)
        return;
    p.camOriginal = nullptr;
    p.newOriginal = nullptr;
    p.colOriginal = nullptr;
    p.module = nullptr;
    ClearTracking();
    LOG("probe: Halo 3 hooks retired (%s)", why);
}

void RetireHalo2(const char* why)
{
    auto& p = g_probe;
    auto& h = p.h2;
    p.h2Ready.store(false, std::memory_order_release);
    void* const targets[]{h.newTarget, h.colTarget, h.windowTarget, h.sceneTarget, h.bridgeTarget};
    const bool removed = RetireTargets(targets, 5, why);
    h.newTarget = h.colTarget = h.windowTarget = h.sceneTarget = h.bridgeTarget = nullptr;
    if (!removed)
        return;
    h = Halo2Hooks{};
    ClearTracking();
    LOG("probe: Halo 2 hooks retired (%s)", why);
}

bool InstallHalo3(HMODULE module)
{
    auto& p = g_probe;
    uintptr_t base = 0;
    size_t size = 0;
    if (!sig::ModuleRange(L"halo3.dll", base, size) || base != reinterpret_cast<uintptr_t>(module))
        return false;
    const uintptr_t cam = Unique(base, size, kCamCopySig);
    const uintptr_t entry = Unique(base, size, kObjectNewSig);
    const uintptr_t copy = Unique(base, size, kPlacementCopySig);
    const uintptr_t call = Unique(base, size, kFireCallSig);
    const uintptr_t col = Unique(base, size, kCollisionSig);
    LOG("probe: halo3.dll at %p (%zu bytes): camera copy %s, object_new %s, its placement copy %s, the fire's call "
        "%s, collision scheduler %s", module, size, cam ? "unique" : "MISSING/AMBIGUOUS",
        entry ? "unique" : "MISSING/AMBIGUOUS", copy ? "unique" : "MISSING/AMBIGUOUS",
        call ? "unique" : "MISSING/AMBIGUOUS", col ? "unique" : "MISSING/AMBIGUOUS");
    if (!cam || !entry || !copy || !call || !col)
    {
        LOG("probe: an anchor did not prove; nothing is hooked in this halo3.dll (stock game untouched)");
        return false;
    }
    const int32_t rel = Rel32(call + 8);
    const auto* c = reinterpret_cast<const uint8_t*>(copy);
    const uint32_t placePos = c[4], objPos = c[9], placePosZ = c[0x0C], objPosZ = c[0x0F];
    const uint32_t placeFwd = c[0x14], objFwd = c[0x19];
    const bool coherent = placePosZ == placePos + 8 && objPosZ == objPos + 8 && placeFwd == placePos + 0xC &&
                          objFwd == objPos + 0xC;
    const bool inObjectNew = FunctionOf(copy) == entry;
    const bool callsObjectNew = call + 12 + static_cast<intptr_t>(rel) == entry;
    if (!coherent || !inObjectNew || !callsObjectNew)
    {
        LOG("probe: the decoded layout did not prove (placement copy coherent %d, inside object_new %d, the fire's "
            "call targets it %d); nothing is hooked", coherent ? 1 : 0, inObjectNew ? 1 : 0, callsObjectNew ? 1 : 0);
        return false;
    }
    p.fireReturn = call + 12;
    p.placePos = placePos;
    p.placeFwd = placeFwd;
    p.module = module;
    if (!Hook(reinterpret_cast<void*>(cam), reinterpret_cast<void*>(&CamCopyDetour),
              reinterpret_cast<void**>(&p.camOriginal), "camera copy"))
        return false;
    p.camTarget = reinterpret_cast<void*>(cam);
    if (!Hook(reinterpret_cast<void*>(entry), reinterpret_cast<void*>(&ObjectNewDetour),
              reinterpret_cast<void**>(&p.newOriginal), "object_new"))
    {
        Retire("object_new failed");
        return false;
    }
    p.newTarget = reinterpret_cast<void*>(entry);
    if (!Hook(reinterpret_cast<void*>(col), reinterpret_cast<void*>(&CollisionDetour),
              reinterpret_cast<void**>(&p.colOriginal), "collision scheduler"))
    {
        Retire("collision failed");
        return false;
    }
    p.colTarget = reinterpret_cast<void*>(col);
    p.ready.store(true, std::memory_order_release);
    LOG("probe: OBSERVING Halo 3 -- camera copy +0x%llX, object_new +0x%llX (fire call return +0x%llX, placement "
        "position +0x%X forward +0x%X), collision scheduler +0x%llX",
        static_cast<unsigned long long>(cam - base), static_cast<unsigned long long>(entry - base),
        static_cast<unsigned long long>(p.fireReturn - base), placePos, placeFwd,
        static_cast<unsigned long long>(col - base));
    return true;
}

// The first match of `pattern` in [from, from + span) (0 when absent).
uintptr_t FindIn(uintptr_t from, size_t span, const char* pattern) { return sig::Find(from, span, pattern); }

bool InstallHalo2(HMODULE module)
{
    auto& p = g_probe;
    auto& h = p.h2;
    uintptr_t base = 0;
    size_t size = 0;
    if (!sig::ModuleRange(L"halo2.dll", base, size) || base != reinterpret_cast<uintptr_t>(module))
        return false;
    struct Anchor { const char* name; const char* pattern; uintptr_t hit; };
    Anchor a[]{{"object_new", kH2ObjectNewSig, 0},           {"its placement copy", kH2PlacementCopySig, 0},
               {"the fire routine", kH2FireEntrySig, 0},       {"the fire's call of object_new", kH2FireCallSig, 0},
               {"collision_test_vector", kH2CollisionSig, 0},  {"the tick increment", kH2TickIncrementSig, 0},
               {"the interpolator factor writer", kH2InterpFactorSig, 0},
               {"render_player_window", kH2PlayerWindowSig, 0}, {"its ordinary call", kH2PlayerWindowCallSig, 0},
               {"the Saber scene render", kH2SceneSig, 0},     {"its view record", kH2SceneRecordSig, 0},
               {"the Saber camera bridge", kH2BridgeSig, 0},   {"the bridge's constants", kH2BridgeConstSig, 0},
               {"the classic render driver", kH2ClassicDriverSig, 0}};
    bool all = true;
    for (auto& anchor : a)
    {
        anchor.hit = Unique(base, size, anchor.pattern);
        all = all && anchor.hit;
    }
    if (!all)
    {
        for (const auto& anchor : a)
            if (!anchor.hit)
                LOG("probe: halo2.dll: %s is MISSING or AMBIGUOUS", anchor.name);
        LOG("probe: an anchor did not prove; nothing is hooked in this halo2.dll (stock game untouched)");
        return false;
    }
    const uintptr_t objectNew = a[0].hit, copy = a[1].hit, fire = a[2].hit, call = a[3].hit, col = a[4].hit;
    const uintptr_t tickInc = a[5].hit, factorWriter = a[6].hit, window = a[7].hit, windowCall = a[8].hit;
    const uintptr_t scene = a[9].hit, record = a[10].hit, bridge = a[11].hit, constLoads = a[12].hit;
    const uintptr_t driver = a[13].hit;
    const uintptr_t rendererGate = driver + 13 + static_cast<intptr_t>(Rel32(driver + 8));

    // object_new's copy: position, forward, up, velocity, each pair coherent (+8 for the third float, +0xC between).
    const auto* c = reinterpret_cast<const uint8_t*>(copy);
    const uint32_t placePos = c[4], objPos = c[9], placeFwd = c[20], objFwd = c[25], placeUp = c[36];
    const uint32_t placeVel = c[55], objVel = U32(copy + 60), objUp = c[41], objUpZ = U32(copy + 47);
    const bool copyCoherent = c[12] == placePos + 8 && c[15] == objPos + 8 && placeFwd == placePos + 0xC &&
                              objFwd == objPos + 0xC && c[28] == placeFwd + 8 && c[31] == objFwd + 8 &&
                              placeUp == placeFwd + 0xC && objUp == objFwd + 0xC && c[44] == placeUp + 8 &&
                              objUpZ == objUp + 8 && placeVel == placeUp + 0xC && objVel == objUp + 0xC &&
                              c[66] == placeVel + 8;
    const bool copyInObjectNew = FunctionRoot(copy) == objectNew;
    const bool callInFire = FunctionRoot(call) == fire;
    const bool callsObjectNew = call + 9 + static_cast<intptr_t>(Rel32(call + 5)) == objectNew;
    // The game clock: the tick increment's slot, and the factor writer's call into the fraction read of that slot.
    const uintptr_t gameTimeSlot = tickInc + 7 + static_cast<intptr_t>(Rel32(tickInc + 3));
    const uintptr_t fractionRead = factorWriter + 9 + static_cast<intptr_t>(Rel32(factorWriter + 5));
    const uintptr_t factorGlobal = factorWriter + 17 + static_cast<intptr_t>(Rel32(factorWriter + 13));
    bool fractionOfSlot = false;
    __try
    {
        fractionOfSlot = sig::Find(fractionRead, 15, kH2FractionReadSig) == fractionRead &&
                         fractionRead + 7 + static_cast<intptr_t>(Rel32(fractionRead + 3)) == gameTimeSlot;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        fractionOfSlot = false;
    }
    // CLASSIC: the ordinary call's second call targets render_player_window; the ordinary return follows it.
    const bool windowCallTargets = windowCall + 18 + static_cast<intptr_t>(Rel32(windowCall + 14)) == window;
    // ANNIVERSARY: the record arithmetic lives in the scene render; the bridge's loads live in the bridge.
    const auto* r = reinterpret_cast<const uint8_t*>(record);
    const uint32_t viewStride = U32(record + 10), viewArray = U32(record + 21), viewCamera = r[38];
    const bool recordInScene = FunctionRoot(record) == scene;
    const uintptr_t viewSlot = record + 7 + static_cast<intptr_t>(Rel32(record + 3));
    const uintptr_t sceneLoad = FindIn(bridge, 0x320, kH2BridgeSceneSig);
    const uintptr_t offX = FindIn(bridge, 0x320, kH2BridgeOffsetXSig);
    const uintptr_t offZ = FindIn(bridge, 0x320, kH2BridgeOffsetZSig);
    const bool bridgeLoads = sceneLoad && offX && offZ && FunctionRoot(constLoads) == bridge &&
                             FunctionRoot(sceneLoad) == bridge && FunctionRoot(offX) == bridge &&
                             FunctionRoot(offZ) == bridge;
    const uintptr_t saberScene = sceneLoad ? sceneLoad + 7 + static_cast<intptr_t>(Rel32(sceneLoad + 3)) : 0;
    const uint32_t cameraCount = sceneLoad ? U32(sceneLoad + 9) : 0;
    const uintptr_t constY = constLoads + 8 + static_cast<intptr_t>(Rel32(constLoads + 4));
    const uintptr_t constScale = constLoads + 29 + static_cast<intptr_t>(Rel32(constLoads + 25));
    const uintptr_t constX = offX ? offX + 8 + static_cast<intptr_t>(Rel32(offX + 4)) : 0;
    const uintptr_t constZ = offZ ? offZ + 8 + static_cast<intptr_t>(Rel32(offZ + 4)) : 0;
    // The engine keeps the four constants together (offset X, Z, Y, then the forward scale 0x34 on).
    const bool constantsTogether = constX && constZ == constX + 4 && constY == constX + 8 &&
                                   constScale == constX + 0x34;
    const bool proven = copyCoherent && copyInObjectNew && callInFire && callsObjectNew && fractionOfSlot &&
                        windowCallTargets && recordInScene && viewStride && viewArray && viewCamera &&
                        bridgeLoads && cameraCount == 0x108 && constantsTogether;
    LOG("probe: halo2.dll at %p (%zu bytes): every anchor unique; object_new +0x%llX copy coherent %d (placement "
        "position +0x%X forward +0x%X velocity +0x%X -> object +0x%X/+0x%X/+0x%X) in object_new %d; the fire's call "
        "+0x%llX in the fire routine +0x%llX %d, targets object_new %d; game_time slot +0x%llX, factor +0x%llX read "
        "from that slot's fraction %d; render_player_window +0x%llX called at +0x%llX %d; scene render +0x%llX, view "
        "record slot +0x%llX + 0x%X + view x 0x%X, camera +0x%X, in the scene render %d; camera bridge +0x%llX: "
        "scene slot +0x%llX (count +0x%X), constants +0x%llX..+0x%llX together %d",
        module, size, static_cast<unsigned long long>(objectNew - base), copyCoherent ? 1 : 0, placePos, placeFwd,
        placeVel, objPos, objFwd, objVel, copyInObjectNew ? 1 : 0, static_cast<unsigned long long>(call - base),
        static_cast<unsigned long long>(fire - base), callInFire ? 1 : 0, callsObjectNew ? 1 : 0,
        static_cast<unsigned long long>(gameTimeSlot - base), static_cast<unsigned long long>(factorGlobal - base),
        fractionOfSlot ? 1 : 0, static_cast<unsigned long long>(window - base),
        static_cast<unsigned long long>(windowCall + 13 - base), windowCallTargets ? 1 : 0,
        static_cast<unsigned long long>(scene - base), static_cast<unsigned long long>(viewSlot - base), viewArray,
        viewStride, viewCamera, recordInScene ? 1 : 0, static_cast<unsigned long long>(bridge - base),
        static_cast<unsigned long long>(saberScene ? saberScene - base : 0), cameraCount,
        static_cast<unsigned long long>(constX ? constX - base : 0),
        static_cast<unsigned long long>(constScale - base), constantsTogether ? 1 : 0);
    if (!proven)
    {
        LOG("probe: the decoded Halo 2 layout did not prove; nothing is hooked in this halo2.dll (stock game "
            "untouched)");
        return false;
    }
    h = Halo2Hooks{};
    h.module = module;
    h.base = base;
    h.fireReturn = call + 9;
    h.windowReturn = windowCall + 18;
    h.placePos = placePos;
    h.placeFwd = placeFwd;
    h.placeVel = placeVel;
    h.gameTimeSlot = gameTimeSlot;
    h.factorGlobal = factorGlobal;
    h.rendererGate = rendererGate;
    h.viewCollectionSlot = viewSlot;
    h.viewStride = viewStride;
    h.viewArray = viewArray;
    h.viewCamera = viewCamera;
    h.saberSceneSlot = saberScene;
    h.saberCameraCount = cameraCount;
    h.constantAddress[0] = constX;
    h.constantAddress[1] = constZ;
    h.constantAddress[2] = constY;
    h.constantAddress[3] = constScale;
    struct Target { uintptr_t at; void* detour; void** original; void** target; const char* name; };
    const Target targets[]{
        {objectNew, reinterpret_cast<void*>(&H2ObjectNewDetour), reinterpret_cast<void**>(&h.newOriginal),
         &h.newTarget, "object_new"},
        {col, reinterpret_cast<void*>(&H2CollisionDetour), reinterpret_cast<void**>(&h.colOriginal), &h.colTarget,
         "collision_test_vector"},
        {window, reinterpret_cast<void*>(&H2PlayerWindowDetour), reinterpret_cast<void**>(&h.windowOriginal),
         &h.windowTarget, "render_player_window"},
        {scene, reinterpret_cast<void*>(&H2SceneDetour), reinterpret_cast<void**>(&h.sceneOriginal), &h.sceneTarget,
         "the Saber scene render"},
        {bridge, reinterpret_cast<void*>(&H2BridgeDetour), reinterpret_cast<void**>(&h.bridgeOriginal),
         &h.bridgeTarget, "the Saber camera bridge"}};
    for (const auto& t : targets)
    {
        if (!Hook(reinterpret_cast<void*>(t.at), t.detour, t.original, t.name))
        {
            RetireHalo2("a Halo 2 hook failed");
            return false;
        }
        *t.target = reinterpret_cast<void*>(t.at);
    }
    p.h2Ready.store(true, std::memory_order_release);
    int64_t tick = -1;
    float alpha = NAN;
    Halo2Clock(tick, alpha);
    LOG("probe: OBSERVING Halo 2 (both renderers; each frame is tagged by the renderer that drew it) -- tick now %lld, "
        "interpolation factor now %.3f; renderer gate +0x%llX now %s", static_cast<long long>(tick),
        static_cast<double>(alpha), static_cast<unsigned long long>(rendererGate - base),
        Halo2RemasteredNow() == 1 ? "ANNIVERSARY" : Halo2RemasteredNow() == 0 ? "CLASSIC" : "unreadable");
    return true;
}

void RetireClocked(ClockedHooks& h, std::atomic<bool>& ready, mcc_clock::Clock& clock, const char* title,
                   const char* why)
{
    ready.store(false, std::memory_order_release);
    void* const targets[]{h.newTarget, h.colTarget, h.camTarget};
    const bool removed = RetireTargets(targets, 3, why);
    h.newTarget = h.colTarget = h.camTarget = nullptr;
    if (!removed)
        return;
    h = ClockedHooks{};
    clock.Reset();
    ClearTracking();
    LOG("probe: %s hooks retired (%s)", title, why);
}

// object_new + its placement copy + the fire's two calls of it, decoded and proven (both titles' shapes: the copy's
// four placement offsets 0xC apart; the calls at +7 and `secondCall`).
bool ProveSpawnAnchors(uintptr_t objectNew, uintptr_t copy, uintptr_t calls, size_t secondCall, size_t velIndex,
                       ClockedHooks& h)
{
    const auto* c = reinterpret_cast<const uint8_t*>(copy);
    const uint32_t placePos = c[5], placeFwd = c[23], placeUp = c[41], placeVel = c[velIndex];
    const bool coherent = c[14] == placePos + 8 && placeFwd == placePos + 0xC && c[32] == placeFwd + 8 &&
                          placeUp == placeFwd + 0xC && c[50] == placeUp + 8 && placeVel == placeUp + 0xC;
    const uintptr_t callA = calls + 7, callB = calls + secondCall;
    const bool callsObjectNew = reinterpret_cast<const uint8_t*>(callA)[0] == 0xE8 &&
                                reinterpret_cast<const uint8_t*>(callB)[0] == 0xE8 &&
                                callA + 5 + static_cast<intptr_t>(Rel32(callA + 1)) == objectNew &&
                                callB + 5 + static_cast<intptr_t>(Rel32(callB + 1)) == objectNew;
    const bool copyInObjectNew = FunctionRoot(copy) == objectNew;
    if (!coherent || !callsObjectNew || !copyInObjectNew)
    {
        LOG("probe: the decoded spawn layout did not prove (placement copy coherent %d, inside object_new %d, the "
            "fire's calls target it %d)", coherent ? 1 : 0, copyInObjectNew ? 1 : 0, callsObjectNew ? 1 : 0);
        return false;
    }
    h.fireReturnA = callA + 5;
    h.fireReturnB = callB + 5;
    h.placePos = placePos;
    h.placeFwd = placeFwd;
    h.placeVel = placeVel;
    return true;
}

bool InstallReach(HMODULE module)
{
    auto& p = g_probe;
    auto& h = p.reach;
    uintptr_t base = 0;
    size_t size = 0;
    if (!sig::ModuleRange(L"haloreach.dll", base, size) || base != reinterpret_cast<uintptr_t>(module))
        return false;
    struct Anchor { const char* name; const char* pattern; uintptr_t hit; };
    Anchor a[]{{"object_new", kReachObjectNewSig, 0},          {"its placement copy", kReachPlacementCopySig, 0},
               {"the transaction's calls", kReachFireCallsSig, 0}, {"collision_test_vector", kReachCollisionSig, 0},
               {"render_camera_from_observer", kReachCameraSig, 0}, {"its copy map", kReachCameraCopySig, 0},
               {"the world render's call", kReachWorldCameraCallSig, 0}};
    bool all = true;
    for (auto& anchor : a)
    {
        anchor.hit = Unique(base, size, anchor.pattern);
        all = all && anchor.hit;
    }
    if (!all)
    {
        for (const auto& anchor : a)
            if (!anchor.hit)
                LOG("probe: haloreach.dll: %s is MISSING or AMBIGUOUS", anchor.name);
        LOG("probe: an anchor did not prove; nothing is hooked in this haloreach.dll (stock game untouched)");
        return false;
    }
    ClockedHooks fresh{};
    if (!ProveSpawnAnchors(a[0].hit, a[1].hit, a[2].hit, 20, 59, fresh))
        return false;
    const uintptr_t camera = a[4].hit, worldCall = a[6].hit + 13;
    const bool copyInCamera = FunctionRoot(a[5].hit) == camera;
    const bool worldCallsCamera = worldCall + 5 + static_cast<intptr_t>(Rel32(worldCall + 1)) == camera;
    if (!copyInCamera || !worldCallsCamera)
    {
        LOG("probe: the Reach camera did not prove (copy map in it %d, the world render calls it %d); nothing is "
            "hooked", copyInCamera ? 1 : 0, worldCallsCamera ? 1 : 0);
        return false;
    }
    const char* clockWhy = nullptr;
    if (!p.reachClock.Resolve(base, size, mcc_clock::kReach, &clockWhy))
        LOG("probe: Reach's frame clock did not resolve (%s): frames are matched by time, the census has no ticks",
            clockWhy ? clockWhy : "?");
    h = fresh;
    h.module = module;
    h.base = base;
    h.worldCameraReturn = worldCall + 5;
    struct Target { uintptr_t at; void* detour; void** original; void** target; const char* name; };
    const Target targets[]{
        {a[0].hit, reinterpret_cast<void*>(&ReachObjectNewDetour), reinterpret_cast<void**>(&h.newOriginal),
         &h.newTarget, "object_new"},
        {a[3].hit, reinterpret_cast<void*>(&ReachCollisionDetour), reinterpret_cast<void**>(&h.reachColOriginal),
         &h.colTarget, "collision_test_vector"},
        {camera, reinterpret_cast<void*>(&ReachCameraDetour), reinterpret_cast<void**>(&h.reachCamOriginal),
         &h.camTarget, "render_camera_from_observer"}};
    for (const auto& t : targets)
    {
        if (!Hook(reinterpret_cast<void*>(t.at), t.detour, t.original, t.name))
        {
            RetireClocked(h, p.reachReady, p.reachClock, "Reach", "a Reach hook failed");
            return false;
        }
        *t.target = reinterpret_cast<void*>(t.at);
    }
    p.reachReady.store(true, std::memory_order_release);
    const auto& l = p.reachClock.layout;
    LOG("probe: OBSERVING Halo Reach -- object_new +0x%llX (fire returns +0x%llX/+0x%llX, placement position +0x%X "
        "forward +0x%X velocity +0x%X), collision_test_vector +0x%llX, render camera +0x%llX (world call returns "
        "+0x%llX); frame clock %s (TLS index +0x%X, game_time TLS+0x%X tick +0x%X seconds/tick +0x%X, interpolator "
        "TLS+0x%X factor +0x%X)",
        static_cast<unsigned long long>(a[0].hit - base), static_cast<unsigned long long>(h.fireReturnA - base),
        static_cast<unsigned long long>(h.fireReturnB - base), h.placePos, h.placeFwd, h.placeVel,
        static_cast<unsigned long long>(a[3].hit - base), static_cast<unsigned long long>(camera - base),
        static_cast<unsigned long long>(h.worldCameraReturn - base), l.valid ? "decoded" : "UNRESOLVED",
        l.tlsIndexRva, l.gameTimeSlot, l.tickOffset, l.secondsPerTickOffset, l.interpSlot, l.factorOffset);
    return true;
}

bool InstallHalo4(HMODULE module)
{
    auto& p = g_probe;
    auto& h = p.h4;
    uintptr_t base = 0;
    size_t size = 0;
    if (!sig::ModuleRange(L"halo4.dll", base, size) || base != reinterpret_cast<uintptr_t>(module))
        return false;
    struct Anchor { const char* name; const char* pattern; uintptr_t hit; };
    Anchor a[]{{"object_new", kH4ObjectNewSig, 0},     {"its placement copy", kH4PlacementCopySig, 0},
               {"the fire's calls", kH4FireCallsSig, 0}, {"PhysicsRayCast", kH4RayCastSig, 0},
               {"the player-view setup", kH4SetupSig, 0}, {"the observer copy map", kH4ObserverCopySig, 0}};
    bool all = true;
    for (auto& anchor : a)
    {
        anchor.hit = Unique(base, size, anchor.pattern);
        all = all && anchor.hit;
    }
    if (!all)
    {
        for (const auto& anchor : a)
            if (!anchor.hit)
                LOG("probe: halo4.dll: %s is MISSING or AMBIGUOUS", anchor.name);
        LOG("probe: an anchor did not prove; nothing is hooked in this halo4.dll (stock game untouched)");
        return false;
    }
    ClockedHooks fresh{};
    if (!ProveSpawnAnchors(a[0].hit, a[1].hit, a[2].hit, 17, 62, fresh))
        return false;
    const char* clockWhy = nullptr;
    if (!p.h4Clock.Resolve(base, size, mcc_clock::kHalo4, &clockWhy))
        LOG("probe: Halo 4's frame clock did not resolve (%s): frames are matched by time, the census has no ticks",
            clockWhy ? clockWhy : "?");
    h = fresh;
    h.module = module;
    h.base = base;
    struct Target { uintptr_t at; void* detour; void** original; void** target; const char* name; };
    const Target targets[]{
        {a[0].hit, reinterpret_cast<void*>(&H4ObjectNewDetour), reinterpret_cast<void**>(&h.newOriginal),
         &h.newTarget, "object_new"},
        {a[3].hit, reinterpret_cast<void*>(&H4RayCastDetour), reinterpret_cast<void**>(&h.rayOriginal), &h.colTarget,
         "PhysicsRayCast"},
        {a[4].hit, reinterpret_cast<void*>(&H4SetupDetour), reinterpret_cast<void**>(&h.setupOriginal), &h.camTarget,
         "the player-view setup"}};
    for (const auto& t : targets)
    {
        if (!Hook(reinterpret_cast<void*>(t.at), t.detour, t.original, t.name))
        {
            RetireClocked(h, p.h4Ready, p.h4Clock, "Halo 4", "a Halo 4 hook failed");
            return false;
        }
        *t.target = reinterpret_cast<void*>(t.at);
    }
    p.h4Ready.store(true, std::memory_order_release);
    const auto& l = p.h4Clock.layout;
    LOG("probe: OBSERVING Halo 4 -- object_new +0x%llX (fire returns +0x%llX/+0x%llX, placement position +0x%X forward "
        "+0x%X velocity +0x%X), PhysicsRayCast +0x%llX, player-view setup +0x%llX (observer copy map +0x%llX); frame "
        "clock %s (TLS index +0x%X, game_time TLS+0x%X tick +0x%X seconds/tick +0x%X, interpolator TLS+0x%X factor "
        "+0x%X)",
        static_cast<unsigned long long>(a[0].hit - base), static_cast<unsigned long long>(h.fireReturnA - base),
        static_cast<unsigned long long>(h.fireReturnB - base), h.placePos, h.placeFwd, h.placeVel,
        static_cast<unsigned long long>(a[3].hit - base), static_cast<unsigned long long>(a[4].hit - base),
        static_cast<unsigned long long>(a[5].hit - base), l.valid ? "decoded" : "UNRESOLVED", l.tlsIndexRva,
        l.gameTimeSlot, l.tickOffset, l.secondsPerTickOffset, l.interpSlot, l.factorOffset);
    return true;
}

// Present / Present1 of a throwaway swapchain's vtable (dxgi.dll's own code, shared by MCC's chain). Installed once.
bool InstallPresent()
{
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"halomcc_flat_probe_dummy";
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr,
                                  wc.hInstance, nullptr);
    if (!window)
        return false;
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 64;
    desc.BufferDesc.Height = 64;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                                     D3D11_SDK_VERSION, &desc, &chain, &device, &level, &context);
    bool ok = false;
    if (SUCCEEDED(hr) && chain)
    {
        void** table = *reinterpret_cast<void***>(chain);
        ok = Hook(table[8], reinterpret_cast<void*>(&PresentDetour),
                  reinterpret_cast<void**>(&g_probe.presentOriginal), "Present");
        IDXGISwapChain1* chain1 = nullptr;
        if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&chain1))) && chain1)
        {
            void** table1 = *reinterpret_cast<void***>(chain1);
            if (!Hook(table1[22], reinterpret_cast<void*>(&Present1Detour),
                      reinterpret_cast<void**>(&g_probe.present1Original), "Present1"))
                LOG("probe: Present1 not hooked (Present alone marks frames)");
            chain1->Release();
        }
    }
    else
        LOG("probe: the throwaway D3D11 device was not created (hr 0x%08X)", static_cast<unsigned>(hr));
    if (context)
        context->Release();
    if (device)
        device->Release();
    if (chain)
        chain->Release();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

void ReportClocked(const char* title, mcc_clock::Clock& clock, uint32_t presents, double seconds, uint32_t cameras,
                   uint32_t camerasOther, uint32_t atCall, uint32_t local, double speed, double speedMax,
                   uint32_t foreign, uint32_t matched, uint32_t ambiguous, uint32_t lost, uint32_t evicted,
                   uint32_t faults, double fov);

void Report()
{
    auto& p = g_probe;
    AcquireSRWLockExclusive(&p.lock);
    // An impact no frame showed within half a second is dropped (counted).
    const uint64_t nowUs = NowUs();
    for (auto& q : p.pending)
        if (q.used && nowUs > q.impactUs + 500000)
        {
            ++p.window[q.round.aim.renderer < kRenderers ? q.round.aim.renderer : kHalo3].unframed;
            q = Pending{};
            p.live.fetch_sub(1, std::memory_order_acq_rel);
        }
    as_seen::Window windows[kRenderers]{};
    char census[kRenderers][1024]{};
    bool censusAny[kRenderers]{};
    for (int r = 0; r < kRenderers; ++r)
    {
        windows[r] = p.window[r];
        p.window[r] = as_seen::Window{};
        censusAny[r] = p.census[r].Format(census[r], sizeof(census[r]));
        p.census[r].ResetCounts();
    }
    uint32_t framesBy[kRenderers]{};
    std::memcpy(framesBy, p.framesBy, sizeof(framesBy));
    std::memset(p.framesBy, 0, sizeof(p.framesBy));
    const uint32_t presents = p.presents, atCall = p.spawnsAtCall, local = p.spawnsLocal, foreign = p.spawnsForeign,
                   matched = p.queriesMatched, ambiguous = p.queriesAmbiguous, lost = p.roundsLost,
                   evicted = p.roundsEvicted, faults = p.faults, tanXN = p.tanXN, onThread = p.presentsOnCameraThread,
                   withFrame = p.presentsWithFrame, sceneViews = p.sceneViews, sceneMain = p.sceneMain,
                   sceneUnreadable = p.sceneUnreadable, pushMatched = p.pushMatched, pushUnmatched = p.pushUnmatched,
                   windowOther = p.windowOther, speedN = p.speedN, fovN = p.fovN,
                   wrongRenderer = p.viewsWrongRenderer;
    const double passes = presents ? p.passesSum / presents : 0.0, spread = p.spreadMaxMm;
    const double tanX = tanXN ? p.tanXSum / tanXN : 0.0, fov = fovN ? p.fovSum / fovN : 0.0;
    const double speed = speedN ? p.speedSum / speedN : 0.0, speedMax = p.speedMax;
    const double seconds = p.lastPresentUs > p.firstPresentUs ? double(p.lastPresentUs - p.firstPresentUs) * 1e-6 : 0;
    p.presents = p.cameraCopies = p.spawnsAtCall = p.spawnsLocal = p.spawnsForeign = p.queriesMatched =
        p.queriesAmbiguous = p.roundsLost = p.roundsEvicted = p.faults = p.tanXN = p.presentsOnCameraThread =
            p.presentsWithFrame = p.sceneViews = p.sceneMain = p.sceneUnreadable = p.pushMatched = p.pushUnmatched =
                p.windowOther = p.speedN = p.fovN = p.viewsWrongRenderer = 0;
    p.passesSum = p.spreadMaxMm = p.tanXSum = p.fovSum = p.speedSum = p.speedMax = 0.0;
    p.firstPresentUs = p.lastPresentUs = 0;
    const uint32_t reachCameras = p.reach.cameras, reachOther = p.reach.camerasOtherSite, h4Cameras = p.h4.cameras,
                   h4Other = p.h4.camerasOtherSite;
    p.reach.cameras = p.reach.camerasOtherSite = p.h4.cameras = p.h4.camerasOtherSite = 0;
    ReleaseSRWLockExclusive(&p.lock);
    const bool h3 = p.ready.load(std::memory_order_acquire), h2 = p.h2Ready.load(std::memory_order_acquire);
    const bool reach = p.reachReady.load(std::memory_order_acquire), h4 = p.h4Ready.load(std::memory_order_acquire);
    if (!h3 && !h2 && !reach && !h4 && !presents)
        return;
    static char line[4096];
    for (int r = 0; r < kRenderers; ++r)
    {
        char label[320];
        if (r == kHalo3)
            std::snprintf(label, sizeof(label),
                          "Halo 3 FLAT [vs the stock crosshair: the camera of the first presented frame after the "
                          "impact, along its forward; radii = the VR reticle's art in this view, %.2f deg]",
                          static_cast<double>(as_seen::FlatArtHalfAngleDeg(g_vrHudWidth, static_cast<float>(tanX))));
        else if (r == kReach || r == kHalo4)
            std::snprintf(label, sizeof(label),
                          "%s [vs the stock crosshair: the frame's camera (%s) of the first presented frame after the "
                          "impact, along its forward; ticks and alpha from MCC's frame clock]", RendererName(r),
                          r == kReach ? "the world render's, from the observer" : "the observer the view setup was handed");
        else
            std::snprintf(label, sizeof(label),
                          "%s [vs the stock crosshair: the camera the renderer drew the first presented frame after "
                          "the impact from, along its forward; ticks from game_time]", RendererName(r));
        if (windows[r].Format(label, line, sizeof(line)))
            LOG("%s", line);
        if (censusAny[r])
            LOG("AS SEEN %s camera (dev; per presented frame): %s", RendererName(r), census[r]);
    }
    if (h3 && (atCall || presents))
        LOG("AS SEEN Halo 3 FLAT probe (dev): %u presents (%.0f fps), %.1f camera copies a frame (other passes up to "
            "%.0f mm off the frame's), horizontal projection tangent %.3f (%.1f deg FOV); rounds from the fire "
            "routine %u: ours %u, not ours %u; their queries matched %u (%u ambiguous), lost %u, evicted %u; read "
            "faults %u", presents, seconds > 0 ? (presents - 1) / seconds : 0.0, passes, spread, tanX,
            tanX > 0 ? 2.0 * std::atan(tanX) * as_seen::kRadToDeg : 0.0, atCall, local, foreign, matched, ambiguous,
            lost, evicted, faults);
    if (reach && (atCall || presents))
        ReportClocked("Halo Reach", p.reachClock, presents, seconds, reachCameras, reachOther, atCall, local, speed,
                      speedMax, foreign, matched, ambiguous, lost, evicted, faults, fov);
    if (h4 && (atCall || presents))
        ReportClocked("Halo 4", p.h4Clock, presents, seconds, h4Cameras, h4Other, atCall, local, speed, speedMax,
                      foreign, matched, ambiguous, lost, evicted, faults, fov);
    if (h2 && (atCall || presents))
    {
        int64_t tick = -1;
        float alpha = NAN;
        Halo2Clock(tick, alpha);
        float secondsPerTick = NAN;
        int16_t ticksPerSecond = 0;
        __try
        {
            const uint8_t* object = *reinterpret_cast<const uint8_t* const*>(p.h2.gameTimeSlot);
            if (object)
            {
                std::memcpy(&ticksPerSecond, object + 2, sizeof(ticksPerSecond));
                std::memcpy(&secondsPerTick, object + 4, sizeof(secondsPerTick));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        LOG("AS SEEN Halo 2 FLAT probe (dev): %u presents (%.0f fps): drawn by CLASSIC %u, ANNIVERSARY %u, no frame "
            "camera %u; the frame camera was taken on the presenting thread for %u of %u; %.1f cameras a frame (other "
            "passes up to %.0f mm off the frame's); FOV %.1f deg (classic: vertical; anniversary: horizontal); Saber "
            "scene renders %u (main views %u, unreadable %u), bridge pushes matched %u, unmatched %u; render windows "
            "from another caller %u; cameras from the renderer the gate says is off %u (renderer now %s); game_time %d ticks/s, %.2f ms a tick, tick now %lld; rounds from the fire routine "
            "%u: ours %u (mean speed %.0f m/s, max %.0f), not ours %u; their queries matched %u (%u ambiguous), lost "
            "%u, evicted %u; read faults %u",
            presents, seconds > 0 ? (presents - 1) / seconds : 0.0, framesBy[kH2Classic], framesBy[kH2Anniversary],
            presents - framesBy[kH2Classic] - framesBy[kH2Anniversary] - framesBy[kHalo3], onThread, withFrame,
            passes, spread, fov, sceneViews, sceneMain, sceneUnreadable, pushMatched, pushUnmatched, windowOther,
            wrongRenderer,
            Halo2RemasteredNow() == 1 ? "ANNIVERSARY" : Halo2RemasteredNow() == 0 ? "CLASSIC" : "unreadable",
            static_cast<int>(ticksPerSecond), static_cast<double>(secondsPerTick) * 1000.0,
            static_cast<long long>(tick), atCall, local, speed, speedMax, foreign, matched, ambiguous, lost, evicted,
            faults);
    }
}

// The Reach / Halo 4 status line: presents, cameras, rounds and the frame clock now.
void ReportClocked(const char* title, mcc_clock::Clock& clock, uint32_t presents, double seconds, uint32_t cameras,
                   uint32_t camerasOther, uint32_t atCall, uint32_t local, double speed, double speedMax,
                   uint32_t foreign, uint32_t matched, uint32_t ambiguous, uint32_t lost, uint32_t evicted,
                   uint32_t faults, double fov)
{
    int64_t tick = -1;
    float alpha = NAN, tickMs = NAN;
    clock.Read(tick, alpha, &tickMs);
    LOG("AS SEEN %s FLAT probe (dev): %u presents (%.0f fps), frame cameras %u (other sites / users %u), vertical FOV "
        "%.1f deg; MCC frame clock: tick now %lld, interpolation factor now %.3f, game tick %.2f ms (captures %u); "
        "rounds from the fire routine %u: ours %u (mean speed %.0f m/s, max %.0f), not ours %u; their queries matched "
        "%u (%u ambiguous), lost %u, evicted %u; read faults %u",
        title, presents, seconds > 0 ? (presents - 1) / seconds : 0.0, cameras, camerasOther, fov,
        static_cast<long long>(tick), static_cast<double>(alpha), static_cast<double>(tickMs),
        clock.captures.load(std::memory_order_relaxed), atCall, local, speed, speedMax, foreign, matched, ambiguous,
        lost, evicted, faults);
}

void ReadConfig(const wchar_t* directory)
{
    wchar_t path[MAX_PATH]{};
    std::swprintf(path, MAX_PATH, L"%lsHaloMCCFlatProbe.cfg", directory);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"rt") || !file)
        return;
    char text[256];
    while (std::fgets(text, sizeof(text), file))
    {
        float value = 0.0f;
        if (std::sscanf(text, " vr_hud_width = %f", &value) == 1 && std::isfinite(value) && value > 100.0f)
            g_vrHudWidth = value;
    }
    std::fclose(file);
}

DWORD WINAPI Worker(void*)
{
    wchar_t self[MAX_PATH]{};
    HMODULE me = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Worker), &me);
    GetModuleFileNameW(me, self, MAX_PATH);
    wchar_t* slash = std::wcsrchr(self, L'\\');
    if (slash)
        slash[1] = L'\0';
    wchar_t logPath[MAX_PATH]{};
    std::swprintf(logPath, MAX_PATH, L"%lsHaloMCCFlatProbe.log", self);
    LogInit(logPath);
    ReadConfig(self);
    LOG("HaloMCCFlatProbe loaded into pid %lu (source %s, compiled %s %s): OBSERVE ONLY -- no VR, no writes; the AS "
        "SEEN control arm for stock flat Halo 3, Halo 2 (both renderers), Halo Reach and Halo 4. vr_hud_width %.0f "
        "(HaloMCCFlatProbe.cfg: "
        "the VR HUD raster width the Halo 3 reticle radius refers to)", GetCurrentProcessId(), HALOMCCVR_BUILD_COMMIT,
        __DATE__, __TIME__, static_cast<double>(g_vrHudWidth));
    if (MH_Initialize() != MH_OK)
    {
        LOG("probe: MinHook did not initialise; nothing is hooked");
        return 0;
    }
    if (!InstallPresent())
        LOG("probe: Present is not hooked; impacts cannot be placed in frames (nothing else is hooked either)");
    else
        LOG("probe: Present hooked (frame boundaries); waiting for halo3.dll, halo2.dll, haloreach.dll or halo4.dll");
    HMODULE failedFor3 = nullptr, failedFor2 = nullptr, failedForReach = nullptr, failedFor4 = nullptr;
    uint64_t nextReportUs = NowUs() + 2000000;
    for (;;)
    {
        Sleep(250);
        if (!g_probe.presentOriginal)
            continue;
        const HMODULE module3 = GetModuleHandleW(L"halo3.dll");
        if (g_probe.module && module3 != g_probe.module)
            Retire(module3 ? "halo3.dll was reloaded" : "halo3.dll was unloaded");
        if (!g_probe.module && module3 && module3 != failedFor3)
        {
            if (!InstallHalo3(module3))
                failedFor3 = module3;
        }
        if (!module3)
            failedFor3 = nullptr;
        const HMODULE module2 = GetModuleHandleW(L"halo2.dll");
        if (g_probe.h2.module && module2 != g_probe.h2.module)
            RetireHalo2(module2 ? "halo2.dll was reloaded" : "halo2.dll was unloaded");
        if (!g_probe.h2.module && module2 && module2 != failedFor2)
        {
            if (!InstallHalo2(module2))
                failedFor2 = module2;
        }
        if (!module2)
            failedFor2 = nullptr;
        const HMODULE moduleReach = GetModuleHandleW(L"haloreach.dll");
        if (g_probe.reach.module && moduleReach != g_probe.reach.module)
            RetireClocked(g_probe.reach, g_probe.reachReady, g_probe.reachClock, "Halo Reach",
                          moduleReach ? "haloreach.dll was reloaded" : "haloreach.dll was unloaded");
        if (!g_probe.reach.module && moduleReach && moduleReach != failedForReach)
        {
            if (!InstallReach(moduleReach))
                failedForReach = moduleReach;
        }
        if (!moduleReach)
            failedForReach = nullptr;
        const HMODULE module4 = GetModuleHandleW(L"halo4.dll");
        if (g_probe.h4.module && module4 != g_probe.h4.module)
            RetireClocked(g_probe.h4, g_probe.h4Ready, g_probe.h4Clock, "Halo 4",
                          module4 ? "halo4.dll was reloaded" : "halo4.dll was unloaded");
        if (!g_probe.h4.module && module4 && module4 != failedFor4)
        {
            if (!InstallHalo4(module4))
                failedFor4 = module4;
        }
        if (!module4)
            failedFor4 = nullptr;
        const uint64_t nowUs = NowUs();
        if (nowUs >= nextReportUs)
        {
            nextReportUs = nowUs + 2000000;
            Report();
        }
    }
}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        // Pin: hooks point into this module for the life of the process.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        if (HANDLE thread = CreateThread(nullptr, 0, &Worker, nullptr, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}
