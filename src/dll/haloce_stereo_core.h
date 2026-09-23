#pragma once
#include "haloce_eye_cache.h"
#include "../common/haloce_frame_context.h"

// CE Anniversary builds two views before culling. No whole-frame replay.
// Classic, weapon IK, melee, collision and HUD extraction remain separate.
bool HaloCE_Poll(uintptr_t base,size_t size,uint32_t generation,bool active,
    bool allowInitialInstall) noexcept;
// Management worker only: permits a failed install to retry; healthy CE hooks,
// renderer resources, HUD and tracking implementation remain untouched.
void HaloCE_RequestRecovery(uint32_t generation) noexcept;
// Cold optional proof captured before the core detours overlapping native
// bodies. Revoked on retirement and valid only for the exact retained module.
bool HaloCE_HudTargetBindingsVerified(uintptr_t base,size_t size,uint32_t generation) noexcept;
// Ordinary native HUD callbacks must also wait for a managed renderer reset
// to restore its effects. This check does not depend on camera heartbeats.
bool HaloCE_NativeHudResourcesReady(uintptr_t expectedBase,uint32_t expectedGeneration) noexcept;
bool HaloCE_Armed() noexcept;
void HaloCE_Recenter() noexcept;
void HaloCE_PublishTracking(const halo_ce::Tracking& tracking,bool enabled) noexcept;
void HaloCE_PresentResources(ID3D11Device* device,ID3D11DeviceContext* context) noexcept;
void HaloCE_SetDlssRequested(bool enabled) noexcept;
void HaloCE_CaptureClassicDlssDepth(uintptr_t caller) noexcept;
bool HaloCE_AcquirePair(ID3D11DeviceContext* context,uint64_t currentSerial,
    uint64_t spaceEpoch,halo_ce::EyeCache::Completed& pair) noexcept;
void HaloCE_ReleasePair(uint64_t borrowId) noexcept;
bool HaloCE_OwnsPresentation() noexcept;
// Optional native features receive one coherent tracking/reference/context.
// Native first-person callers supply the current STOCK center camera before
// their camera adaptation; Classic's active eye scope substitutes its source.
bool HaloCE_GetRenderContext(const halo_ce::Camera& stockCamera,
    halo_ce::RenderContext& context) noexcept;
// Projection changes require the verified primary Classic BBCF30 consumer,
// not merely a prepared frame or a recent first-person palette. Auxiliary
// views and work outside the primary draw interval cannot borrow this scope.
bool HaloCE_GetClassicPrimaryEyeContext(halo_ce::RenderContext& context) noexcept;
// Gameplay hooks never read the renderer's temporary camera globals. This
// receipt retains a verified stock center and pairs it with fresh XR input.
bool HaloCE_GetGameplayContext(halo_ce::RenderContext& context) noexcept;
bool HaloCE_RenderContextCurrent(const halo_ce::RenderContext& context) noexcept;
// The ordinary late Anniversary HUD callback owns the native setup. Its
// gameplay draw can frame the two already rendered packed eye regions; it
// never invokes that callback or changes the native target stack itself.
bool HaloCE_BeginAnniversaryHudGameplay(ID3D11DeviceContext* context,
    UINT& packedWidth,UINT& packedHeight) noexcept;
void HaloCE_EndAnniversaryHudGameplay(bool complete) noexcept;
// Native per-eye post effects may use the exact rendered frame's immutable
// settings only while its primary camera still owns the current scene.
bool HaloCE_GetAnniversaryEyeTracking(const halo_ce::SaberCamera* camera,
    halo_ce::Tracking& tracking) noexcept;
// Material writers run during depth/scene/shading, before all three stages
// complete. Only the currently selected, verified primary camera may lend its
// frozen eye settings; auxiliary uploads and per-eye output revoke this scope.
bool HaloCE_GetAnniversaryPrimaryEyeTracking(halo_ce::Tracking& tracking) noexcept;
// Pre-culling/material workers use the explicitly published native list,
// before render-eye TLS exists. Source/copied-list identity and its current
// ticket, both cameras, source player, reference and XR lifetime must match.
bool HaloCE_GetAnniversaryPreparedListTracking(uintptr_t list,halo_ce::Tracking& tracking) noexcept;
void HaloCE_RecordAnniversaryVisibilitySubmission(uintptr_t list,int32_t phase,uintptr_t caller) noexcept;
// Successful creation owns this texture, even before CE loads. Metadata only.
void HaloCE_RecordTextureCreated(ID3D11Texture2D* texture,
    const D3D11_TEXTURE2D_DESC& descriptor) noexcept;
// Cold presentation owns the GetBuffer reference, including buffers created
// before CE hooks existed. Repeated observations preserve the resource revision.
void HaloCE_RecordPresentationTexture(ID3D11Texture2D* texture,
    const D3D11_TEXTURE2D_DESC& descriptor) noexcept;
void HaloCE_ForgetPresentationTexture() noexcept;
