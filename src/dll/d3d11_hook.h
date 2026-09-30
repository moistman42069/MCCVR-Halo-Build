#pragma once

#include <cstdint>

// Hooks IDXGISwapChain::Present / Present1 / ResizeBuffers process-wide.
// Present is the "here's a finished frame" call every D3D11 game makes each
// frame — our hook is where all VR work happens.

bool InstallD3D11Hooks();

// True only after Halo 2's identified native crosshair shader has completed a
// capture for the currently loaded halo2.dll generation. Until then the shared
// compositor retains its procedural fallback, so a shader mismatch cannot
// leave the player without any aiming marker.
bool D3D_Halo2NativeCrosshairCaptured();
bool D3D_Halo2HudShaderPathAvailable();
// Stage 3BH/3BR's Draw/DrawIndexed framing pin is required before Halo 4's
// optional authored-reticle hooks may hide the native flat CUI copy.
bool D3D_Halo4AuthoredReticleDrawPathAvailable();
// Exact known-good V6 pixel-shader path for Halo 4's visor/framing overlay.
// The feature remains stock unless both shader creation and PS binding hooks
// are live; the default-visible path forwards every binding byte-for-byte.
bool D3D_Halo4HelmetShaderPathAvailable();
void D3D_GetHalo4HelmetTelemetry(
    uint64_t& shadersRegistered, uint64_t& suppressions);
// Exact H4EK/retail `screen\motion_suck` pixel shader. This optional bridge is
// independently gated and suppresses only the incompatible full-screen draw
// while Halo 4 stereo is active.
bool D3D_Halo4ScreenEffectShaderPathAvailable();
void D3D_GetHalo4ScreenEffectTelemetry(
    uint64_t& shadersRegistered, uint64_t& suppressions);
void D3D_GetHalo2HudTelemetry(
    uint64_t& gameplayShadersRegistered,
    uint64_t& crosshairShadersRegistered,
    uint64_t& rasterDraws,
    uint64_t& nativeCrosshairDraws,
    uint64_t& stateFailures);

// --- Desktop-window fit (config.fit_desktop_window) -------------------------
// The forced render size (0,0 before initialization or if required hooks fail).
// menu.cpp uses it while rewriting MCC's WM_SIZE transaction. This is separate
// from the optional physical desktop-window fit.
void D3D_GetForcedRenderSize(unsigned& width, unsigned& height);

// Window fitting remains optional; headset render sizing works independently.
bool D3D_FitActive();
bool D3D_CanLiveResize();

// menu.cpp brackets the game's own WM_SIZE handling with this. While set, our
// GetClientRect hook returns the full render size to the game's resize code on
// THAT call stack only (it is thread-local), so MCC keeps sizing its render to
// the full backbuffer. DXGI/DWM present-time client queries -- on the render
// thread -- never see it, so they still downscale the full frame into the real,
// smaller window instead of clipping it to a corner.
void D3D_SetForcedClientLie(bool on);

// --- Dormant co-op drop probe (config.coop_probe) ---------------------------
// The lifecycle call remains wired for targeted diagnostic rebuilds. Production
// builds compile out both Present sampling and this dump; reason names the mode
// a diagnostic rebuild fell out of.
void CoopProbe_DumpRunUp(const char* reason);

// The current render plan, initially seeded from the launcher and updated by
// live resolution changes: the size the game renders, the headset picture size
// (native x resolution_scale) that the DLSS output must have, whether the
// render was shrunk for DLSS, and whether nvngx_dlss.dll was found beside the
// mod. Zero sizes before InstallD3D11Hooks.
void D3D_GetRenderPlan(unsigned& renderW, unsigned& renderH,
                       unsigned& outputW, unsigned& outputH,
                       bool& dlss, bool& dlssRuntimePresent);

// Live render-size change (no restart). Stores the new plan and, when the
// render hooks are available and the render size differs, queues the requested
// backbuffer size so MCC's own resize path (its WM_SIZE handling ->
// ResizeBuffers, which the fit already intercepts) re-sizes every render
// target. Returns Requested when the caller must now send the game window a
// message to the UI thread (menu.cpp posts kLiveResizeMsg for that),
// Unchanged when nothing needs to happen (same size, a request already
// pending, or the same size already failed), RestartNeeded when a required
// hook was unavailable at initialization. Desktop fitting is independent.
// Deferred means a title is loading: no published dimensions change. The UI
// handler repeats that admission check before publishing and sending WM_SIZE.
enum class D3DRenderPlanResult { Unchanged, Requested, RestartNeeded, Deferred };
D3DRenderPlanResult D3D_RequestRenderPlan(
    unsigned renderW, unsigned renderH, unsigned outputW, unsigned outputH,
    bool dlss);
// UI thread: recheck loading admission and publish the queued dimensions.
// False means no notification or physical fit may be issued for this message.
bool D3D_BeginLiveResize(unsigned& renderW, unsigned& renderH);
void D3D_CancelQueuedLiveResize();
// Explicit settings changes and title-generation changes permit a new attempt.
void D3D_RetryLiveResize();
// 0 idle, 1 a live resize is pending, 2 the last one completed, 3 the last
// one failed (MCC never re-sized; the forced size was reverted to the real
// backbuffer, reported in actualW/H).
int D3D_LiveResizeState(unsigned& actualW, unsigned& actualH);
HWND D3D_GameWindow();


// DLSS texture mip bias (Programming Guide 3.5). While `bias` is non-zero,
// every pixel-shader sampler the game binds on the calling thread is
// replaced by a copy with that much added to its MipLODBias, so textures
// are sampled for the headset picture rather than the smaller render. Set
// at the start of an eye render, cleared (0) at its end, before the mod's
// own passes. Render thread only.
void D3D_SetEyeSamplerBias(float bias);
void D3D_ReportSamplerCache(); // worker-only fallback diagnostics
