#pragma once

#include <d3d11.h>
#include <cstddef>
#include <cstdint>

// Render-thread-only wrapper over NVIDIA NGX Super Resolution.
// Production uses the game's D3D11 device/command stream with a private context
// state around NGX calls, preserving all game bindings. There is no shared-device
// fence handoff or forced per-eye Flush. The old D3D12/NvOF/image-search code is
// dormant; the offline benchmark can explicitly select D3D12 for comparison.
//
// Halo 3 can consume the rendered eye directly from typeless/UAV-capable storage
// with an sRGB render view. Its motion/depth pass reads the engine depth SRV at
// eye end, before the next eye overwrites it. Other paths retain required copies.
// Motion is camera-only: moving/skinned geometry still needs native vectors.
//
// NGX is not thread safe. All calls remain on the game's render thread. Errors
// affect DLSS for that eye/frame only; they never disarm the title or VR session.

struct DlssFeatureDesc
{
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    int quality = 0;        // dlss::Quality
    int preset = 0;         // dlss::kPreset*
    bool depthInverted = false;
};

struct DlssEvalInputs
{
    // D3D11 backend: the resources DLSS reads/writes. D3D12 backend: ignored
    // (the shared textures handed out by Dlss_CreateEyeTextures are used).
    ID3D11Resource* color = nullptr;    // render-size, perceptual (sRGB-encoded) LDR
    ID3D11Resource* depth = nullptr;    // render-size, one channel, raw depth
    ID3D11Resource* motion = nullptr;   // render-size RG16F, previous - current, pixels
    ID3D11Resource* output = nullptr;   // output-size, UAV-capable
    float jitterPixelX = 0.0f;
    float jitterPixelY = 0.0f;
    // This render's jitter minus the previous render's, in render pixels
    // (+x right, +y down): what the optical flow measures on top of the real
    // motion, because both frames were rasterised with their own jitter.
    float jitterDeltaX = 0.0f;
    float jitterDeltaY = 0.0f;
    // True when the mod copied this render's colour into the flow input
    // Dlss_FlowInputIndex named for this eye.
    bool flowInputWritten = false;
    float frameTimeMs = 0.0f;
    bool reset = false;
};

// The per-eye textures DLSS works on. With the D3D12 backend they are shared
// D3D12 resources opened in D3D11 (AddRef'd D3D11 textures the caller owns a
// reference to, exactly like ones it created itself).
struct DlssEyeTextures
{
    ID3D11Texture2D* color = nullptr;   // renderW x renderH, colorFormat (UNORM)
    ID3D11Texture2D* depth = nullptr;   // renderW x renderH, R32_FLOAT, RTV
    // renderW x renderH, R16G16_FLOAT, RTV: the camera-only motion vectors
    // the mod's motion pass writes (previous - current, render pixels).
    ID3D11Texture2D* motion = nullptr;
    ID3D11Texture2D* output = nullptr;  // outputW x outputH, R8G8B8A8_UNORM, UAV
    // Optical-flow input frames, renderW x renderH, B8G8R8A8_UNORM, RTV:
    // the mod copies each render's colour into the one Dlss_FlowInputIndex
    // names. Null when object motion is unavailable (camera-only vectors).
    ID3D11Texture2D* flowInput[2]{};
};

// Initializes NGX for `device` once. Subsequent calls return the cached result
// until Dlss_Shutdown. False means DLSS is unavailable; Dlss_StatusText says
// why in plain language (no NVIDIA GPU, old driver, DLL missing, denied).
bool Dlss_EnsureInitialized(ID3D11Device* device);
bool Dlss_IsAvailable();
const char* Dlss_StatusText();
// True when DLSS runs on the D3D12 backend and the eye textures must come
// from Dlss_CreateEyeTextures.
bool Dlss_UsesSharedTextures();

// D3D12 backend only: (re)creates the shared textures for one eye. Releases
// any previous set for that eye first. `colorFormat` is the UNORM twin of the
// eye image format (what DLSS reads as perceptual LDR).
bool Dlss_CreateEyeTextures(
    int eye, uint32_t renderWidth, uint32_t renderHeight,
    uint32_t outputWidth, uint32_t outputHeight, DXGI_FORMAT colorFormat,
    DlssEyeTextures& out);
void Dlss_ReleaseEyeTextures(int eye);

// Which of the eye's two flow inputs this render's colour belongs in (0 or
// 1); -1 when the external optical-flow path is unused. The GPU image-motion
// path consumes DLSS colour directly and never needs these extra copies.
int Dlss_FlowInputIndex(int eye);
bool Dlss_HasImageMotion(int eye);
// Plain-language state of the object-motion path for the log and F1.
const char* Dlss_ObjectMotionStatus();

// The DLSS-admissible render size range for one output size and quality slot
// (Programming Guide 5.2.8). False when the query itself is not supported.
bool Dlss_QueryRenderRange(
    uint32_t outputWidth, uint32_t outputHeight, int quality,
    uint32_t& outMinWidth, uint32_t& outMinHeight,
    uint32_t& outMaxWidth, uint32_t& outMaxHeight,
    uint32_t& outOptimalWidth, uint32_t& outOptimalHeight);

// Creates (or re-creates, when `desc` changed) the feature for one eye.
// `outRecreated` tells the caller to pass reset=1 on the next evaluation.
bool Dlss_EnsureFeature(
    int eye, ID3D11DeviceContext* context, const DlssFeatureDesc& desc,
    bool& outRecreated, char* reason, size_t reasonBytes);

bool Dlss_Evaluate(int eye, ID3D11DeviceContext* context,
                   const DlssEvalInputs& inputs, uint32_t& outFailureCode);

bool Dlss_GetFeatureDesc(int eye, DlssFeatureDesc& out);

// Makes the D3D11 context wait for the evaluation submitted for `eye` since
// the last call. On the D3D12 backend Dlss_Evaluate submits each eye to a
// compute queue and returns without waiting; this is the one wait per eye,
// taken right before that eye's finished image is read into the headset
// image (the first eye's is normally long complete by then).
void Dlss_SyncD3D11(ID3D11DeviceContext* context, int eye);

// CPU time the render thread spent inside DLSS evaluation calls since the
// last read (microseconds, summed over both eyes and all frames), and the
// number of evaluations. The IQ GPU log line reports it.
void Dlss_TakeCpuTime(double& outMicroseconds, uint32_t& outEvaluations);

struct DlssGpuTiming
{
    double mergeMicroseconds = 0;
    double evaluateMicroseconds = 0;
    uint32_t evaluations = 0;
};
// Completed command lists only; never blocks or flushes for diagnostics.
void Dlss_TakeGpuTime(DlssGpuTiming& out);

// Releases both features (resize, title detach) and, separately, the NGX
// instance (device loss / shutdown). Both are safe to call repeatedly.
void Dlss_ReleaseFeatures();
void Dlss_Shutdown();
