#include "dlss.h"

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>

#include "../common/dlss_logic.h"
#include "../common/log.h"
#include "nvof.h"
#include "dlss_image_motion.h"
#include "gpu_eye_timing.h"
#ifdef HALOMCCVR_DLSS_BACKEND_BENCH
extern bool g_benchmarkUseD3D11;
extern bool g_benchmarkNoInputFlush;
#endif

#ifndef HALOMCCVR_BUILD_COMMIT
#define HALOMCCVR_BUILD_COMMIT "unknown"
#endif

namespace
{
    // NGX requires a GUID-like project identifier for engines it does not
    // know. This one belongs to Halo MCC VR alone; never reuse another
    // application's NVIDIA application id.
    constexpr const char* kProjectId = "6f1a3d0c-9b2e-4c7a-8d5f-2e4b7c9a1d63";

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object)
        {
            object->Release();
            object = nullptr;
        }
    }

    struct Feature
    {
        NVSDK_NGX_Handle* handle = nullptr;
        NVSDK_NGX_Parameter* params = nullptr;
        DlssFeatureDesc desc{};
        bool created = false;
    };

    // One eye's shared textures: owned on the D3D12 heap, opened in D3D11.
    struct SharedEye
    {
        ID3D12Resource* color = nullptr;
        ID3D12Resource* depth = nullptr;
        ID3D12Resource* motion = nullptr;      // the vectors DLSS reads
        ID3D12Resource* output = nullptr;
        ID3D11Texture2D* color11 = nullptr;
        ID3D11Texture2D* depth11 = nullptr;
        ID3D11Texture2D* motion11 = nullptr;
        ID3D11Texture2D* output11 = nullptr;
        // Object motion. With it, the mod's motion pass writes camera-only
        // vectors into motionCam and the merge pass on the compute queue
        // writes the vectors DLSS reads into motion. Without it (engine
        // unavailable) the mod writes motion directly.
        ID3D12Resource* motionCam = nullptr;
        ID3D11Texture2D* motionCam11 = nullptr;
        ID3D12Resource* flowInput[2]{};        // BGRA8 eye colour, this and the previous render
        ID3D11Texture2D* flowInput11[2]{};
        ID3D12Resource* flow = nullptr;        // the engine's output, one vector per block
        NvOfSession* ofSession = nullptr;
        ID3D12Fence* flowDoneFence = nullptr;
        ID3D12Fence* flowGateFence = nullptr;
        UINT64 flowDoneValue = 0;
        void* flowInputHandle[2]{};
        void* flowHandle = nullptr;
        int flowCursor = 0;           // which flowInput the NEXT render is copied into
        bool flowPrevValid = false;   // the other one holds the previous render
        bool flowHintsValid = false;  // the engine's last result belongs to this sequence
        bool imageMotion = false;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
    };

    // A command allocator + the queue fence value that retires it.
    constexpr int kCommandRing = 6; // two eyes x three frames in flight
    struct CommandSlot
    {
        ID3D12CommandAllocator* allocator = nullptr;
        UINT64 retireValue = 0;
        bool timingPending = false;
    };

    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext1* g_nativeContext = nullptr;
    ID3DDeviceContextState* g_nativeState = nullptr;
    GpuEyeTiming g_nativeTiming;

    // NGX may change any compute/graphics binding. A device context state
    // preserves the game's complete pipeline without a per-slot COM census.
    struct NativeScope
    {
        ID3DDeviceContextState* previous = nullptr;
        NativeScope() { if (g_nativeContext && g_nativeState)
            g_nativeContext->SwapDeviceContextState(g_nativeState, &previous); }
        ~NativeScope() { if (previous) {
            // Do not pin NGX's last input/output in an inactive state object.
            g_nativeContext->ClearState();
            g_nativeContext->SwapDeviceContextState(previous, nullptr);
            previous->Release();
        } }
    };
    bool g_initTried = false;
    bool g_initOk = false;
    bool g_backendD3D12 = false;
    NVSDK_NGX_Parameter* g_caps = nullptr;
    Feature g_features[2];
    SharedEye g_shared[2];
    DlssImageMotion g_imageMotion;
    bool g_imageMotionReady = false;
    wchar_t g_modDir[MAX_PATH]{};
    const wchar_t* g_pathList[1]{};
    char g_status[256] = "off";
    char g_dllVersion[48] = "unknown";
    uint64_t g_evalFailuresLogged = 0;

    // One D3D12 queue with its command list, allocator ring and retire fence.
    struct Queue
    {
        ID3D12CommandQueue* queue = nullptr;
        ID3D12GraphicsCommandList* list = nullptr;
        CommandSlot ring[kCommandRing]{};
        int cursor = 0;
        ID3D12Fence* done = nullptr;   // signalled after every submit; CPU reads it
        UINT64 doneValue = 0;
    };

    // Dormant shared-device backend, retained for explicit offline comparison.
    // A compute queue alone does not prove useful overlap with game raster;
    // current-runtime measurements favor the native D3D11 stream.
    ID3D12Device* g_device12 = nullptr;
    ID3D11DeviceContext* g_evalContext = nullptr;
    ID3D11DeviceContext4* g_evalContext4 = nullptr;
    Queue g_direct;
    Queue g_compute;
    ID3D12QueryHeap* g_gpuQueries = nullptr;
    ID3D12Resource* g_gpuReadback = nullptr;
    const UINT64* g_gpuMapped = nullptr;
    UINT64 g_gpuFrequency = 0;
    DlssGpuTiming g_gpuTiming{};

    void CollectGpuTime()
    {
        if (!g_gpuMapped || !g_gpuFrequency || !g_compute.done) return;
        const UINT64 completed = g_compute.done->GetCompletedValue();
        if (completed == UINT64_MAX) return; // device removed
        for (int i = 0; i < kCommandRing; ++i)
        {
            auto& slot = g_compute.ring[i];
            if (!slot.timingPending || completed < slot.retireValue) continue;
            slot.timingPending = false;
            const UINT64* t = g_gpuMapped + i * 3;
            if (t[1] < t[0] || t[2] < t[1]) continue;
            g_gpuTiming.mergeMicroseconds += (t[1] - t[0]) * 1e6 / double(g_gpuFrequency);
            g_gpuTiming.evaluateMicroseconds += (t[2] - t[1]) * 1e6 / double(g_gpuFrequency);
            ++g_gpuTiming.evaluations;
        }
    }

    void InitGpuTime()
    {
        D3D12_QUERY_HEAP_DESC query{};
        query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; query.Count = kCommandRing * 3;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kCommandRing * 3 * sizeof(UINT64); desc.Height = 1;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(g_device12->CreateQueryHeap(&query, IID_PPV_ARGS(&g_gpuQueries))) ||
            FAILED(g_device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_gpuReadback))) ||
            FAILED(g_compute.queue->GetTimestampFrequency(&g_gpuFrequency))) return;
        void* mapped = nullptr;
        D3D12_RANGE range{0, static_cast<SIZE_T>(desc.Width)};
        if (SUCCEEDED(g_gpuReadback->Map(0, &range, &mapped))) g_gpuMapped = static_cast<const UINT64*>(mapped);
    }
    ID3D12Fence* g_toD3D12 = nullptr;     // D3D11 signals, D3D12 waits
    ID3D12Fence* g_toD3D11 = nullptr;     // D3D12 signals, D3D11 waits
    ID3D11Fence* g_toD3D12_11 = nullptr;
    ID3D11Fence* g_toD3D11_11 = nullptr;
    UINT64 g_toD3D12Value = 0;
    UINT64 g_toD3D11Value = 0;
    // Per eye: the highest queue signal the D3D11 context has not waited for.
    UINT64 g_pendingD3D11Wait[2]{};

    // Object motion (nvof.h) on the same D3D12 device. The engine waits on
    // g_toD3D12 (the frames are written) and signals g_ofDone (the flow is
    // written); the compute queue waits on g_ofDone before the merge pass.
    // g_ofGate is signalled from the CPU and only gates registrations.
    constexpr UINT kMergeDescriptors = 5; // t0 camera vectors, t1 flow, t2/t3 inputs, u0 merged
    ID3D12Fence* g_ofDone = nullptr;
    ID3D12Fence* g_ofGate = nullptr;
    UINT64 g_ofDoneValue = 0;
    UINT64 g_ofGateValue = 0;
    ID3D12RootSignature* g_mergeRootSignature = nullptr;
    ID3D12PipelineState* g_mergePso = nullptr;
    ID3D12DescriptorHeap* g_descriptorHeap = nullptr;
    UINT g_descriptorSize = 0;
    bool g_mergeReady = false;
    bool g_objectMotionLogged = false;
    char g_objectMotionStatus[256] = "off";

    // Root constants of the merge pass (12 dwords), mirrored in the shader.
    struct MergeParams
    {
        uint32_t width, height, curIndex, grid;
        float invW, invH, flowScale, margin;
        float jitterDx, jitterDy, minSwitch, pad;
    };
    static_assert(sizeof(MergeParams) == 12 * 4);
    HANDLE g_retireEvent = nullptr;

    // CPU time inside evaluation calls, reported through Dlss_TakeCpuTime.
    double g_evalCpuUs = 0.0;
    uint32_t g_evalCount = 0;
    LARGE_INTEGER g_qpcFrequency{};

    // NGX writes one or two informational lines per evaluation on the D3D11
    // backend (its resource-cache housekeeping); while g_ngxQuiet is set the
    // callback counts instead of writing. The first such line is kept.
    bool g_ngxQuiet = false;
    uint64_t g_ngxSuppressed = 0;
    bool g_ngxSuppressedSampleLogged = false;

    void SetStatus(const char* text)
    {
        snprintf(g_status, sizeof(g_status), "%s", text);
    }

    void NVSDK_CONV NgxLogCallback(const char* message,
                                   NVSDK_NGX_Logging_Level,
                                   NVSDK_NGX_Feature)
    {
        if (!message)
            return;
        if (g_ngxQuiet)
        {
            ++g_ngxSuppressed;
            if (g_ngxSuppressedSampleLogged)
                return;
            g_ngxSuppressedSampleLogged = true;
        }
        char line[512];
        snprintf(line, sizeof(line), "%s", message);
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (!length)
            return;
        if (g_ngxQuiet)
            LOG("NGX (per-frame line; further copies are counted, not written): %s", line);
        else
            LOG("NGX: %s", line);
    }

    void ReadRuntimeDllVersion()
    {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\nvngx_dlss.dll", g_modDir);
        const DWORD size = GetFileVersionInfoSizeW(path, nullptr);
        if (!size)
        {
            snprintf(g_dllVersion, sizeof(g_dllVersion), "not beside the mod");
            return;
        }
        void* block = HeapAlloc(GetProcessHeap(), 0, size);
        if (!block)
            return;
        VS_FIXEDFILEINFO* info = nullptr;
        UINT infoLength = 0;
        if (GetFileVersionInfoW(path, 0, size, block) &&
            VerQueryValueW(block, L"\\", reinterpret_cast<void**>(&info),
                           &infoLength) &&
            info && infoLength >= sizeof(VS_FIXEDFILEINFO))
        {
            snprintf(g_dllVersion, sizeof(g_dllVersion), "%u.%u.%u.%u",
                     HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                     HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
        }
        HeapFree(GetProcessHeap(), 0, block);
    }

    NVSDK_NGX_PerfQuality_Value ToNgxQuality(int quality)
    {
        switch (static_cast<dlss::Quality>(quality))
        {
        case dlss::Quality::Dlaa: return NVSDK_NGX_PerfQuality_Value_DLAA;
        case dlss::Quality::MaxQuality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case dlss::Quality::Balanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
        case dlss::Quality::MaxPerformance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case dlss::Quality::UltraPerformance:
            return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        default: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        }
    }

    NVSDK_NGX_DLSS_Hint_Render_Preset ToNgxPreset(int preset)
    {
        switch (preset)
        {
        case dlss::kPresetFastCnn: return NVSDK_NGX_DLSS_Hint_Render_Preset_F;
        case dlss::kPresetCnn: return NVSDK_NGX_DLSS_Hint_Render_Preset_E;
        case dlss::kPresetTransformerJ: return NVSDK_NGX_DLSS_Hint_Render_Preset_J;
        case dlss::kPresetTransformerK: return NVSDK_NGX_DLSS_Hint_Render_Preset_K;
        case dlss::kPresetTransformerL: return NVSDK_NGX_DLSS_Hint_Render_Preset_L;
        case dlss::kPresetTransformerM: return NVSDK_NGX_DLSS_Hint_Render_Preset_M;
        default: return NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
        }
    }

    void ReleaseFeature(Feature& feature)
    {
        NativeScope scope;
        if (feature.handle)
        {
            if (g_backendD3D12)
                NVSDK_NGX_D3D12_ReleaseFeature(feature.handle);
            else
                NVSDK_NGX_D3D11_ReleaseFeature(feature.handle);
            feature.handle = nullptr;
        }
        if (feature.params)
        {
            if (g_backendD3D12)
                NVSDK_NGX_D3D12_DestroyParameters(feature.params);
            else
                NVSDK_NGX_D3D11_DestroyParameters(feature.params);
            feature.params = nullptr;
        }
        feature.created = false;
        feature.desc = {};
    }

    bool SameDesc(const DlssFeatureDesc& a, const DlssFeatureDesc& b)
    {
        return a.renderWidth == b.renderWidth && a.renderHeight == b.renderHeight &&
            a.outputWidth == b.outputWidth && a.outputHeight == b.outputHeight &&
            a.quality == b.quality && a.preset == b.preset &&
            a.depthInverted == b.depthInverted;
    }

    // --- D3D12 backend ----------------------------------------------------

    // Waits (CPU) until the queue has passed `value`; only used to retire a
    // command allocator when every slot is still in flight, once after a
    // feature is created, and before textures are released.
    void WaitQueue(Queue& q, UINT64 value)
    {
        if (!q.done || q.done->GetCompletedValue() >= value)
            return;
        if (SUCCEEDED(q.done->SetEventOnCompletion(value, g_retireEvent)))
            WaitForSingleObject(g_retireEvent, 5000);
    }

    void WaitAllQueues()
    {
        if (g_direct.queue && g_direct.done)
            WaitQueue(g_direct, g_direct.doneValue);
        if (g_compute.queue && g_compute.done)
            WaitQueue(g_compute, g_compute.doneValue);
    }

    // CPU wait for any fence (the optical-flow engine's, before its
    // textures are unregistered).
    void WaitFenceCpu(ID3D12Fence* fence, UINT64 value)
    {
        if (!fence || !value || !g_retireEvent || fence->GetCompletedValue() >= value)
            return;
        if (SUCCEEDED(fence->SetEventOnCompletion(value, g_retireEvent)))
            WaitForSingleObject(g_retireEvent, 5000);
    }

    // Opens a fresh command list on the next allocator of the queue's ring.
    bool BeginCommands(Queue& q)
    {
        CommandSlot& slot = q.ring[q.cursor];
        // The steady-state eye path must not block the render thread when
        // all allocators are busy. Drop this optional evaluation and reset
        // its history next time; cold feature creation may still wait.
        if (&q == &g_compute && q.done->GetCompletedValue() < slot.retireValue)
            return false;
        WaitQueue(q, slot.retireValue);
        if (FAILED(slot.allocator->Reset()))
            return false;
        if (FAILED(q.list->Reset(slot.allocator, nullptr)))
            return false;
        return true;
    }

    // Closes and executes the list, marks the allocator's retirement.
    bool EndCommands(Queue& q)
    {
        if (FAILED(q.list->Close()))
            return false;
        ID3D12CommandList* lists[1] = {q.list};
        q.queue->ExecuteCommandLists(1, lists);
        ++q.doneValue;
        q.queue->Signal(q.done, q.doneValue);
        q.ring[q.cursor].retireValue = q.doneValue;
        q.cursor = (q.cursor + 1) % kCommandRing;
        return true;
    }

    void ReleaseObjectMotion(SharedEye& eye)
    {
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
        LOG("OF cleanup %p wait", eye.ofSession);
#endif
        // The engine may still be writing the last flow.
        WaitFenceCpu(eye.flowDoneFence, eye.flowDoneValue);
        for (int i = 0; i < 2; ++i)
        {
            NvOf_Unregister(eye.flowInputHandle[i]);
            eye.flowInputHandle[i] = nullptr;
        }
        NvOf_Unregister(eye.flowHandle);
        eye.flowHandle = nullptr;
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
        LOG("OF cleanup motionCam11");
#endif
        SafeRelease(eye.motionCam11);
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
        LOG("OF cleanup motionCam12");
#endif
        SafeRelease(eye.motionCam);
        for (int i = 0; i < 2; ++i)
        {
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
            LOG("OF cleanup flow input %d D3D11", i);
#endif
            SafeRelease(eye.flowInput11[i]);
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
            LOG("OF cleanup flow input %d D3D12", i);
#endif
            SafeRelease(eye.flowInput[i]);
        }
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
        LOG("OF cleanup flow output");
#endif
        SafeRelease(eye.flow);
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
        LOG("OF cleanup destroy session");
#endif
        // NVIDIA's D3D12 cleanup order: unregister, free resources, then
        // destroy the session. This external engine path is dormant.
        NvOf_DestroySession(eye.ofSession);
        eye.ofSession = nullptr;
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
        LOG("OF cleanup release fences");
#endif
        SafeRelease(eye.flowDoneFence);
        SafeRelease(eye.flowGateFence);
        eye.flowDoneValue = 0;
        eye.flowCursor = 0;
        eye.flowPrevValid = false;
        eye.flowHintsValid = false;
    }

    void ReleaseSharedEye(SharedEye& eye)
    {
        g_imageMotion.ReleaseEye(static_cast<int>(&eye - g_shared));
        eye.imageMotion = false;
        ReleaseObjectMotion(eye);
        SafeRelease(eye.color11);
        SafeRelease(eye.depth11);
        SafeRelease(eye.motion11);
        SafeRelease(eye.output11);
        SafeRelease(eye.color);
        SafeRelease(eye.depth);
        SafeRelease(eye.motion);
        SafeRelease(eye.output);
        eye.renderWidth = eye.renderHeight = 0;
    }

    void ReleaseQueue(Queue& q)
    {
        SafeRelease(q.done);
        SafeRelease(q.list);
        for (CommandSlot& slot : q.ring)
        {
            SafeRelease(slot.allocator);
            slot.retireValue = 0;
        }
        SafeRelease(q.queue);
        q.doneValue = 0;
        q.cursor = 0;
    }

    void ShutdownD3D12()
    {
        WaitAllQueues();
        if (g_gpuMapped && g_gpuReadback) { D3D12_RANGE none{}; g_gpuReadback->Unmap(0, &none); }
        g_gpuMapped = nullptr; g_gpuFrequency = 0; g_gpuTiming = {};
        SafeRelease(g_gpuReadback); SafeRelease(g_gpuQueries);
        for (SharedEye& eye : g_shared)
            ReleaseSharedEye(eye);
        g_imageMotion.Shutdown(); g_imageMotionReady = false;
        SafeRelease(g_toD3D12_11);
        SafeRelease(g_evalContext4); SafeRelease(g_evalContext);
        SafeRelease(g_toD3D11_11);
        SafeRelease(g_toD3D12);
        SafeRelease(g_toD3D11);
        SafeRelease(g_mergePso);
        SafeRelease(g_mergeRootSignature);
        SafeRelease(g_descriptorHeap);
        SafeRelease(g_ofDone);
        SafeRelease(g_ofGate);
        g_mergeReady = false;
        g_ofDoneValue = g_ofGateValue = 0;
        ReleaseQueue(g_compute);
        ReleaseQueue(g_direct);
        SafeRelease(g_device12);
        if (g_retireEvent)
        {
            CloseHandle(g_retireEvent);
            g_retireEvent = nullptr;
        }
        g_toD3D12Value = g_toD3D11Value = 0;
        g_pendingD3D11Wait[0] = g_pendingD3D11Wait[1] = 0;
    }

    // A shared fence created on D3D12 and opened on D3D11.
    bool CreateSharedFence(ID3D11Device5* device11, ID3D12Fence*& fence12,
                           ID3D11Fence*& fence11)
    {
        if (FAILED(g_device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
                                           IID_PPV_ARGS(&fence12))))
            return false;
        HANDLE handle = nullptr;
        if (FAILED(g_device12->CreateSharedHandle(fence12, nullptr, GENERIC_ALL,
                                                  nullptr, &handle)))
            return false;
        const HRESULT opened =
            device11->OpenSharedFence(handle, IID_PPV_ARGS(&fence11));
        CloseHandle(handle);
        return SUCCEEDED(opened);
    }

    bool CreateQueue(Queue& q, D3D12_COMMAND_LIST_TYPE type)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = type;
        if (FAILED(g_device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&q.queue))))
            return false;
        for (CommandSlot& slot : q.ring)
        {
            if (FAILED(g_device12->CreateCommandAllocator(type, IID_PPV_ARGS(&slot.allocator))))
                return false;
        }
        if (FAILED(g_device12->CreateCommandList(0, type, q.ring[0].allocator, nullptr,
                                                 IID_PPV_ARGS(&q.list))) ||
            FAILED(q.list->Close()))
            return false;
        return SUCCEEDED(g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                 IID_PPV_ARGS(&q.done)));
    }

    // The merge pass: per pixel, the camera-only vector unless the previous
    // frame proves it wrong and the measured flow proves itself right.
    // Evidence is colour: the vector that lands the previous frame's colour
    // inside this pixel's 3x3 neighbourhood (the box jitter and filtering
    // can legitimately move it within) explains the pixel. The static
    // world passes with the camera vector and keeps its exact value; a
    // character's body fails it (the camera vector lands on another spot)
    // and takes the flow, which passes; a surface uncovered this frame fails
    // both and keeps the camera vector, which is right for it. Flow is
    // measured between two jittered renders, so the jitter difference is
    // taken back out before use.
    constexpr const char* kMergeShader = R"(
Texture2D<float2> camTex : register(t0);
Texture2D<int2> flowTex : register(t1);
Texture2D<float4> in0Tex : register(t2);
Texture2D<float4> in1Tex : register(t3);
RWTexture2D<float2> outTex : register(u0);
SamplerState smp : register(s0);
cbuffer Params : register(b0)
{
    uint width, height, curIndex, grid;
    float invW, invH, flowScale, margin;
    float jitterDx, jitterDy, minSwitch, pad;
};
float3 Prev(float2 uv)
{
    return curIndex == 0 ? in1Tex.SampleLevel(smp, uv, 0).rgb : in0Tex.SampleLevel(smp, uv, 0).rgb;
}
float3 Cur(int2 p)
{
    return curIndex == 0 ? in0Tex.Load(int3(p, 0)).rgb : in1Tex.Load(int3(p, 0)).rgb;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    int2 px = int2(id.xy);
    float2 cam = camTex.Load(int3(px, 0));
    float2 f = flowTex.Load(int3(px / (int)grid, 0)) * flowScale + float2(jitterDx, jitterDy);
    float2 result = cam;
    float2 d = f - cam;
    if (all(abs(f) < 256.0) && dot(d, d) > minSwitch * minSwitch)
    {
        int2 last = int2(width - 1, height - 1);
        float2 inv = float2(invW, invH);
        // The colour inputs contain raster jitter. Compare them with raw
        // image displacement; DLSS receives the unjittered displacement.
        float2 rawCam = cam - float2(jitterDx, jitterDy);
        float2 rawFlow = f - float2(jitterDx, jitterDy);
        float2 base = (float2(px) + 0.5) * inv;
        float2 flowUv = base + rawFlow * inv;
        if (any(flowUv < 0.0) || any(flowUv > 1.0))
        { outTex[px] = cam; return; }
        float camError = 0.0, flowError = 0.0;
        [unroll] for (int dy = -1; dy <= 1; ++dy)
        [unroll] for (int dx = -1; dx <= 1; ++dx)
        {
            int2 q = clamp(px + int2(dx, dy), int2(0, 0), last);
            float3 c = Cur(q);
            float2 uv = (float2(q) + 0.5) * inv;
            float3 a = c - Prev(uv + rawCam * inv);
            float3 b = c - Prev(uv + rawFlow * inv);
            camError += dot(a, a);
            flowError += dot(b, b);
        }
        // A colour-range test loses spatial correspondence: unrelated
        // texels anywhere in a textured body pass it. Compare aligned
        // patches instead and require a meaningful improvement over camera.
        if (flowError + 27.0 * margin * margin < camError * 0.75)
            result = f;
    }
    outTex[px] = result;
}
)";

    D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptor(UINT index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = g_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(index) * g_descriptorSize;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptor(UINT index)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = g_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<UINT64>(index) * g_descriptorSize;
        return h;
    }

    // Root signature, compute PSO and descriptor heap of the merge pass.
    // Failure leaves object motion off (camera-only vectors) and DLSS on.
    bool EnsureMergePipeline(char* why, size_t whyBytes)
    {
        if (g_mergeReady)
            return true;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 4;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 4;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = sizeof(MergeParams) / 4;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 2;
        params[1].DescriptorTable.pDescriptorRanges = ranges;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 2;
        desc.pParameters = params;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers = &sampler;
        ID3DBlob* blob = nullptr;
        ID3DBlob* error = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
        if (FAILED(hr) || !blob)
        {
            snprintf(why, whyBytes, "merge root signature could not be serialised (0x%08X)",
                     static_cast<unsigned>(hr));
            SafeRelease(error);
            return false;
        }
        hr = g_device12->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                             IID_PPV_ARGS(&g_mergeRootSignature));
        blob->Release();
        if (FAILED(hr))
        {
            snprintf(why, whyBytes, "merge root signature could not be created (0x%08X)",
                     static_cast<unsigned>(hr));
            return false;
        }
        ID3DBlob* cs = nullptr;
        hr = D3DCompile(kMergeShader, strlen(kMergeShader), nullptr, nullptr, nullptr, "main",
                        "cs_5_0", 0, 0, &cs, &error);
        if (FAILED(hr) || !cs)
        {
            snprintf(why, whyBytes, "merge shader failed to compile: %s",
                     error ? static_cast<const char*>(error->GetBufferPointer()) : "?");
            SafeRelease(error);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = g_mergeRootSignature;
        pso.CS.pShaderBytecode = cs->GetBufferPointer();
        pso.CS.BytecodeLength = cs->GetBufferSize();
        hr = g_device12->CreateComputePipelineState(&pso, IID_PPV_ARGS(&g_mergePso));
        cs->Release();
        if (FAILED(hr))
        {
            snprintf(why, whyBytes, "merge pipeline could not be created (0x%08X)",
                     static_cast<unsigned>(hr));
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 2 * kMergeDescriptors;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = g_device12->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&g_descriptorHeap));
        if (FAILED(hr))
        {
            snprintf(why, whyBytes, "merge descriptor heap could not be created (0x%08X)",
                     static_cast<unsigned>(hr));
            return false;
        }
        g_descriptorSize = g_device12->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_mergeReady = true;
        return true;
    }

    // The D3D12 device on the game's adapter, its two queues and the shared
    // fences. Any failure is reported and the D3D11 backend is used instead.
    bool InitD3D12(char* why, size_t whyBytes)
    {
        auto fail = [&](const char* text) {
            snprintf(why, whyBytes, "%s", text);
            ShutdownD3D12();
            return false;
        };
        ID3D11Device5* device11 = nullptr;
        if (FAILED(g_device->QueryInterface(IID_PPV_ARGS(&device11))) || !device11)
            return fail("the game's D3D11 device is not D3D11.4 (no shared fences)");
        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        if (FAILED(g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
            FAILED(dxgiDevice->GetAdapter(&adapter)))
        {
            SafeRelease(dxgiDevice);
            device11->Release();
            return fail("the game's adapter could not be queried");
        }
        const HRESULT created = D3D12CreateDevice(
            adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device12));
        adapter->Release();
        dxgiDevice->Release();
        if (FAILED(created) || !g_device12)
        {
            device11->Release();
            return fail("D3D12 is not available on the game's GPU");
        }
        if (!CreateQueue(g_direct, D3D12_COMMAND_LIST_TYPE_DIRECT))
        {
            device11->Release();
            return fail("D3D12 direct queue creation failed");
        }
        if (!CreateQueue(g_compute, D3D12_COMMAND_LIST_TYPE_COMPUTE))
        {
            device11->Release();
            return fail("D3D12 compute queue creation failed");
        }
        g_retireEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_retireEvent ||
            !CreateSharedFence(device11, g_toD3D12, g_toD3D12_11) ||
            !CreateSharedFence(device11, g_toD3D11, g_toD3D11_11))
        {
            device11->Release();
            return fail("shared D3D11/D3D12 fences could not be created");
        }
        device11->Release();
        g_device->GetImmediateContext(&g_evalContext);
        if (!g_evalContext || FAILED(g_evalContext->QueryInterface(IID_PPV_ARGS(&g_evalContext4))))
            return fail("the immediate context has no D3D11.4 fence interface");
        QueryPerformanceFrequency(&g_qpcFrequency);
        InitGpuTime();

        // Object motion: fail-open, DLSS itself does not depend on it.
        char objectWhy[200] = "";
        if (!dlss::kEnableDlss19ImageMotion)
        {
            snprintf(g_objectMotionStatus,sizeof(g_objectMotionStatus),
                "disabled: DLSS-19 image search rejected for cost, loading stalls and remaining smear");
            LOG("DLSS: object motion %s; camera vectors retained",g_objectMotionStatus);
            return true;
        }
        // Retain the external-engine implementation dormant. Motion now
        // records on the DLSS list, eliminating the BGRA conversion and
        // external engine submissions/fences from the active path.
        constexpr bool kUseExternalFlow = false;
        if (!kUseExternalFlow)
        {
            g_imageMotionReady = g_imageMotion.Init(g_device12, objectWhy, sizeof(objectWhy));
            snprintf(g_objectMotionStatus, sizeof(g_objectMotionStatus), "%s%s",
                g_imageMotionReady ? "ready: GPU pyramid motion on the DLSS queue" : "unavailable: ",
                g_imageMotionReady ? "" : objectWhy);
            LOG("DLSS: object motion %s", g_objectMotionStatus);
            return true;
        }
        if (FAILED(g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_ofDone))) ||
            FAILED(g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_ofGate))))
        {
            snprintf(g_objectMotionStatus, sizeof(g_objectMotionStatus),
                     "unavailable: optical-flow fences could not be created");
        }
        else if (!EnsureMergePipeline(objectWhy, sizeof(objectWhy)))
        {
            snprintf(g_objectMotionStatus, sizeof(g_objectMotionStatus),
                     "unavailable: %s", objectWhy);
        }
        else if (NvOf_Available(g_device12))
        {
            snprintf(g_objectMotionStatus, sizeof(g_objectMotionStatus), "%s", NvOf_StatusText());
        }
        else
        {
            snprintf(g_objectMotionStatus, sizeof(g_objectMotionStatus), "%s", NvOf_StatusText());
        }
        return true;
    }

    // One shared texture: D3D12 committed resource on a shared heap, opened
    // in D3D11 as a texture the mod can bind exactly like its own.
    constexpr D3D12_RESOURCE_FLAGS kSharedFlags =
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    bool CreateSharedTexture(uint32_t width, uint32_t height, DXGI_FORMAT format,
                             ID3D12Resource*& out12, ID3D11Texture2D*& out11,
                             D3D12_RESOURCE_FLAGS flags = kSharedFlags)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        // ALLOW_SIMULTANEOUS_ACCESS is what lets D3D11 open the texture at
        // all: without it OpenSharedResource1 returns E_INVALIDARG for every
        // format and bind combination (DLSS-9 headset log, and the
        // standalone probe in docs/DLSS-UPSCALE-EVIDENCE.md, DLSS-10). A
        // texture D3D11 creates with SHARED_NTHANDLE carries the same flag
        // when D3D12 opens it.
        desc.Flags = flags;
        HRESULT hr = g_device12->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&out12));
        if (FAILED(hr))
        {
            LOG("DLSS ERROR: D3D12 shared texture %ux%u fmt %d: create 0x%08X",
                width, height, static_cast<int>(format), static_cast<unsigned>(hr));
            return false;
        }
        HANDLE handle = nullptr;
        hr = g_device12->CreateSharedHandle(out12, nullptr, GENERIC_ALL, nullptr, &handle);
        if (FAILED(hr))
        {
            LOG("DLSS ERROR: D3D12 shared texture %ux%u fmt %d: shared handle 0x%08X",
                width, height, static_cast<int>(format), static_cast<unsigned>(hr));
            return false;
        }
        ID3D11Device1* device1 = nullptr;
        HRESULT opened = E_NOINTERFACE;
        if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&device1))) && device1)
        {
            opened = device1->OpenSharedResource1(handle, IID_PPV_ARGS(&out11));
            device1->Release();
        }
        CloseHandle(handle);
        if (FAILED(opened) || !out11)
        {
            LOG("DLSS ERROR: D3D12 shared texture %ux%u fmt %d: D3D11 open 0x%08X",
                width, height, static_cast<int>(format), static_cast<unsigned>(opened));
            return false;
        }
        return true;
    }

    void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                 D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
    }

    void SetPresetHints(NVSDK_NGX_Parameter* params, int preset)
    {
        const unsigned value = static_cast<unsigned>(ToNgxPreset(preset));
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, value);
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, value);
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, value);
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, value);
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, value);
    }

    NVSDK_NGX_DLSS_Create_Params CreateParams(const DlssFeatureDesc& desc)
    {
        NVSDK_NGX_DLSS_Create_Params create{};
        create.Feature.InWidth = desc.renderWidth;
        create.Feature.InHeight = desc.renderHeight;
        create.Feature.InTargetWidth = desc.outputWidth;
        create.Feature.InTargetHeight = desc.outputHeight;
        create.Feature.InPerfQualityValue = ToNgxQuality(desc.quality);
        int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        if (desc.depthInverted)
            flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        create.InFeatureCreateFlags = flags;
        create.InEnableOutputSubrects = false;
        return create;
    }

    bool InitNgxCommon(NVSDK_NGX_Result init, NVSDK_NGX_Result capsResult)
    {
        if (NVSDK_NGX_FAILED(init))
        {
            char text[256];
            snprintf(text, sizeof(text),
                     "unavailable: NGX would not start (0x%08X). DLSS needs an "
                     "NVIDIA RTX GPU with a current driver; the mod's own filter "
                     "chain is used instead",
                     static_cast<unsigned>(init));
            SetStatus(text);
            LOG("DLSS: %s (nvngx_dlss.dll %s)", g_status, g_dllVersion);
            return false;
        }
        if (NVSDK_NGX_FAILED(capsResult) || !g_caps)
        {
            g_caps = nullptr;
            char text[256];
            snprintf(text, sizeof(text),
                     "unavailable: NGX capability query failed (0x%08X); the "
                     "NVIDIA driver is too old for this DLSS SDK",
                     static_cast<unsigned>(capsResult));
            SetStatus(text);
            LOG("DLSS: %s", g_status);
            return false;
        }
        int needsUpdatedDriver = 0;
        unsigned minMajor = 0, minMinor = 0;
        int available = 0;
        int initResult = 0;
        NVSDK_NGX_Parameter_GetI(g_caps, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
                                 &needsUpdatedDriver);
        NVSDK_NGX_Parameter_GetUI(g_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
                                  &minMajor);
        NVSDK_NGX_Parameter_GetUI(g_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,
                                  &minMinor);
        const NVSDK_NGX_Result availableResult = NVSDK_NGX_Parameter_GetI(
            g_caps, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
        const NVSDK_NGX_Result initResultQuery = NVSDK_NGX_Parameter_GetI(
            g_caps, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
        char text[256];
        if (needsUpdatedDriver)
        {
            snprintf(text, sizeof(text),
                     "unavailable: the NVIDIA driver is too old for DLSS "
                     "(needs %u.%u or newer)", minMajor, minMinor);
        }
        else if (NVSDK_NGX_FAILED(availableResult) || !available)
        {
            snprintf(text, sizeof(text),
                     "unavailable: DLSS is not supported on this GPU");
        }
        else if (NVSDK_NGX_FAILED(initResultQuery) || !initResult)
        {
            snprintf(text, sizeof(text),
                     "unavailable: DLSS denied for this application (0x%08X); "
                     "is nvngx_dlss.dll beside HaloMCCVR.dll?",
                     static_cast<unsigned>(initResult));
        }
        else
        {
            snprintf(text, sizeof(text), "ready (nvngx_dlss.dll %s, %s)",
                     g_dllVersion, g_backendD3D12 ? "D3D12 backend" : "D3D11 backend");
            SetStatus(text);
            return true;
        }
        SetStatus(text);
        return false;
    }
}

bool Dlss_EnsureInitialized(ID3D11Device* device)
{
    if (!device)
        return false;
    if (g_initTried && g_device == device)
        return g_initOk;
    if (g_initTried)
        Dlss_Shutdown();
    g_initTried = true;
    g_device = device;
    QueryPerformanceFrequency(&g_qpcFrequency);

    const wchar_t* logDir = LogDirectory();
    swprintf_s(g_modDir, L"%s", logDir ? logDir : L".");
    size_t length = wcslen(g_modDir);
    while (length > 1 && (g_modDir[length - 1] == L'\\' || g_modDir[length - 1] == L'/'))
        g_modDir[--length] = L'\0';
    g_pathList[0] = g_modDir;
    // DLSS is a player-supplied optional feature. Do not create a context
    // state, allocate queries, or ask NGX/driver caches to load a substitute
    // when its runtime is absent from the mod folder.
    wchar_t runtimePath[MAX_PATH]{};
    swprintf_s(runtimePath, L"%s\\nvngx_dlss.dll", g_modDir);
    const DWORD runtimeAttributes = GetFileAttributesW(runtimePath);
    if (runtimeAttributes == INVALID_FILE_ATTRIBUTES ||
        (runtimeAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        SetStatus("unavailable: optional nvngx_dlss.dll missing; normal VR resolve");
        LOG("DLSS: %s", g_status);
        return false;
    }
    ReadRuntimeDllVersion();

    NVSDK_NGX_FeatureCommonInfo common{};
    common.PathListInfo.Path = g_pathList;
    common.PathListInfo.Length = 1;
    common.LoggingInfo.LoggingCallback = &NgxLogCallback;
    common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    common.LoggingInfo.DisableOtherLoggingSinks = true;

    // Use the game's own device and command stream. Current-runtime tests
    // with queued GPU work disprove the old unconditional CPU-stall claim.
    // Keep the shared-device implementation dormant for comparison.
    char why[160] = "";
#ifdef HALOMCCVR_DLSS_BACKEND_BENCH
    g_backendD3D12 = !g_benchmarkUseD3D11 && InitD3D12(why, sizeof(why));
#else
    g_backendD3D12 = false;
#endif
    if (!g_backendD3D12)
    {
        ID3D11Device1* device1 = nullptr;
        ID3D11DeviceContext* immediate = nullptr;
        device->GetImmediateContext(&immediate);
        const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
        const UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
            ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        bool ready = SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1))) && immediate &&
            SUCCEEDED(immediate->QueryInterface(IID_PPV_ARGS(&g_nativeContext))) &&
            SUCCEEDED(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
                __uuidof(ID3D11Device), nullptr, &g_nativeState));
        SafeRelease(device1); SafeRelease(immediate);
        if (!ready) { SetStatus("D3D11 context isolation unavailable"); return false; }
        g_nativeTiming.Init(device);
        snprintf(g_objectMotionStatus, sizeof(g_objectMotionStatus),
            "camera-only; native object vectors not implemented");
        LOG("DLSS: native D3D11 command stream; shared-device handoffs disabled");
    }

    NVSDK_NGX_Result init;
    NVSDK_NGX_Result capsResult;
    if (g_backendD3D12)
    {
        init = NVSDK_NGX_D3D12_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, HALOMCCVR_BUILD_COMMIT,
            g_modDir, g_device12, &common, NVSDK_NGX_Version_API);
        capsResult = NVSDK_NGX_FAILED(init)
            ? init : NVSDK_NGX_D3D12_GetCapabilityParameters(&g_caps);
    }
    else
    {
        NativeScope scope;
        init = NVSDK_NGX_D3D11_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, HALOMCCVR_BUILD_COMMIT,
            g_modDir, device, &common, NVSDK_NGX_Version_API);
        capsResult = NVSDK_NGX_FAILED(init)
            ? init : NVSDK_NGX_D3D11_GetCapabilityParameters(&g_caps);
    }
    g_initOk = InitNgxCommon(init, capsResult);
    LOG("DLSS: %s; NGX data/search path %ls", g_status, g_modDir);
    if (!g_initOk)
    {
        if (g_caps)
        {
            if (g_backendD3D12)
                NVSDK_NGX_D3D12_DestroyParameters(g_caps);
            else
                NVSDK_NGX_D3D11_DestroyParameters(g_caps);
            g_caps = nullptr;
        }
        if (!NVSDK_NGX_FAILED(init))
        {
            if (g_backendD3D12)
                NVSDK_NGX_D3D12_Shutdown1(g_device12);
            else
                NVSDK_NGX_D3D11_Shutdown1(device);
        }
        ShutdownD3D12();
        g_backendD3D12 = false;
    }
    return g_initOk;
}

bool Dlss_IsAvailable()
{
    return g_initOk;
}

const char* Dlss_StatusText()
{
    return g_status;
}

bool Dlss_UsesSharedTextures()
{
    return g_initOk && g_backendD3D12;
}

namespace
{
    // A D3D12-only texture (the engine's flow output: nothing in D3D11
    // reads it).
    bool CreatePlainTexture(uint32_t width, uint32_t height, DXGI_FORMAT format,
                            ID3D12Resource*& out)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        const HRESULT hr = g_device12->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&out));
        if (FAILED(hr))
            LOG("DLSS ERROR: D3D12 texture %ux%u fmt %d: create 0x%08X",
                width, height, static_cast<int>(format), static_cast<unsigned>(hr));
        return SUCCEEDED(hr);
    }

    // Registers one texture with the engine: it waits for the CPU-signalled
    // gate (already reached) and signals g_ofDone when its own work is done.
    bool RegisterWithEngine(SharedEye& eye, ID3D12Resource* resource, void*& handle)
    {
        eye.flowGateFence->Signal(1);
        if (!NvOf_Register(eye.ofSession, resource, eye.flowGateFence, 1,
                           eye.flowDoneFence, eye.flowDoneValue + 1, &handle))
            return false;
        ++eye.flowDoneValue;
        return true;
    }

    // The engine session, its textures and the merge descriptors for one
    // eye. False (logged once) leaves the eye on camera-only vectors.
    bool SetUpObjectMotion(int eye, SharedEye& shared, uint32_t width, uint32_t height)
    {
        if (g_imageMotionReady)
        {
            bool ok = CreateSharedTexture(width,height,DXGI_FORMAT_R16G16_FLOAT,
                shared.motionCam,shared.motionCam11) &&
                g_imageMotion.CreateEye(eye,width,height,shared.color,shared.motionCam,shared.motion);
            if (!ok)
            {
                g_imageMotion.ReleaseEye(eye);
                SafeRelease(shared.motionCam11); SafeRelease(shared.motionCam);
                LOG("DLSS: eye %d GPU image-motion allocation failed; camera-only vectors stay", eye);
                return false;
            }
            shared.imageMotion = true;
            LOG("DLSS: eye %d image motion active at %ux%u: one compute queue, no external optical-flow submission",eye,width,height);
            return true;
        }
        if (!g_mergeReady || !g_ofDone || !NvOf_Available(g_device12))
            return false;
        char why[200] = "";
        bool ok = CreateSharedTexture(width, height, DXGI_FORMAT_R16G16_FLOAT,
                                      shared.motionCam, shared.motionCam11);
        if (ok) ok = SUCCEEDED(g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&shared.flowDoneFence))) &&
            SUCCEEDED(g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&shared.flowGateFence)));
        for (int i = 0; i < 2 && ok; ++i)
        {
            ok = CreateSharedTexture(width, height, NvOf_InputFormat(),
                                     shared.flowInput[i], shared.flowInput11[i],
                                     D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
        }
        const uint32_t grid = NvOf_Grid();
        if (ok)
            ok = CreatePlainTexture((width + grid - 1) / grid, (height + grid - 1) / grid,
                                    NvOf_OutputFormat(), shared.flow);
        if (ok)
        {
            shared.ofSession = NvOf_CreateSession(width, height, why, sizeof(why));
            ok = shared.ofSession != nullptr;
        }
        for (int i = 0; i < 2 && ok; ++i)
            ok = RegisterWithEngine(shared, shared.flowInput[i], shared.flowInputHandle[i]);
        if (ok)
            ok = RegisterWithEngine(shared, shared.flow, shared.flowHandle);
        if (ok)
        {
            const UINT base = static_cast<UINT>(eye) * kMergeDescriptors;
            g_device12->CreateShaderResourceView(shared.motionCam, nullptr, CpuDescriptor(base + 0));
            g_device12->CreateShaderResourceView(shared.flow, nullptr, CpuDescriptor(base + 1));
            g_device12->CreateShaderResourceView(shared.flowInput[0], nullptr, CpuDescriptor(base + 2));
            g_device12->CreateShaderResourceView(shared.flowInput[1], nullptr, CpuDescriptor(base + 3));
            g_device12->CreateUnorderedAccessView(shared.motion, nullptr, nullptr, CpuDescriptor(base + 4));
        }
        if (!ok)
        {
            if (!g_objectMotionLogged)
            {
                g_objectMotionLogged = true;
                LOG("DLSS: eye %d object motion (optical flow) could not be set up%s%s; "
                    "camera-only motion vectors stay for this eye",
                    eye, why[0] ? ": " : "", why);
            }
            snprintf(g_objectMotionStatus, sizeof(g_objectMotionStatus),
                     "unavailable: %s", why[0] ? why : "its textures could not be created");
            ReleaseObjectMotion(shared);
            return false;
        }
        shared.flowCursor = 0;
        shared.flowPrevValid = false;
        shared.flowHintsValid = false;
        if (!g_objectMotionLogged)
        {
            g_objectMotionLogged = true;
            LOG("DLSS: object motion active: the optical-flow engine measures each eye's "
                "%ux%u render against the previous one (fenced on the DLSS device); "
                "the merge pass keeps camera vectors where they explain the pixel",
                width, height);
        }
        return true;
    }
}

bool Dlss_CreateEyeTextures(
    int eye, uint32_t renderWidth, uint32_t renderHeight,
    uint32_t outputWidth, uint32_t outputHeight, DXGI_FORMAT colorFormat,
    DlssEyeTextures& out)
{
    out = {};
    if (!Dlss_UsesSharedTextures() || eye < 0 || eye > 1 || !renderWidth ||
        !renderHeight || !outputWidth || !outputHeight)
        return false;
    Dlss_ReleaseEyeTextures(eye);
    SharedEye& shared = g_shared[eye];
    if (!CreateSharedTexture(renderWidth, renderHeight, colorFormat,
                             shared.color, shared.color11) ||
        !CreateSharedTexture(renderWidth, renderHeight, DXGI_FORMAT_R32_FLOAT,
                             shared.depth, shared.depth11) ||
        !CreateSharedTexture(renderWidth, renderHeight, DXGI_FORMAT_R16G16_FLOAT,
                             shared.motion, shared.motion11) ||
        !CreateSharedTexture(outputWidth, outputHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
                             shared.output, shared.output11))
    {
        LOG("DLSS ERROR: eye %d shared D3D12/D3D11 textures could not be created "
            "(%ux%u -> %ux%u)", eye, renderWidth, renderHeight, outputWidth, outputHeight);
        ReleaseSharedEye(shared);
        return false;
    }
    shared.renderWidth = renderWidth;
    shared.renderHeight = renderHeight;
    const bool objectMotion = SetUpObjectMotion(eye, shared, renderWidth, renderHeight);
    // The caller owns one reference to each D3D11 texture, like its own.
    shared.color11->AddRef();
    shared.depth11->AddRef();
    shared.output11->AddRef();
    out.color = shared.color11;
    out.depth = shared.depth11;
    out.output = shared.output11;
    if (objectMotion)
    {
        shared.motionCam11->AddRef();
        out.motion = shared.motionCam11;
        for (int i = 0; i < 2; ++i)
        {
            if (shared.flowInput11[i]) shared.flowInput11[i]->AddRef();
            out.flowInput[i] = shared.flowInput11[i];
        }
    }
    else
    {
        shared.motion11->AddRef();
        out.motion = shared.motion11;
    }
    return true;
}

void Dlss_ReleaseEyeTextures(int eye)
{
    if (eye < 0 || eye > 1)
        return;
    // The GPU may still be reading them from the last evaluation.
    WaitAllQueues();
    ReleaseSharedEye(g_shared[eye]);
}

int Dlss_FlowInputIndex(int eye)
{
    if (eye < 0 || eye > 1 || !g_backendD3D12)
        return -1;
    const SharedEye& shared = g_shared[eye];
    if (!shared.ofSession || !shared.flowHandle)
        return -1;
    return shared.flowCursor;
}

const char* Dlss_ObjectMotionStatus()
{
    return g_objectMotionStatus;
}

bool Dlss_HasImageMotion(int eye)
{
    return eye>=0 && eye<2 && g_shared[eye].imageMotion;
}

bool Dlss_QueryRenderRange(
    uint32_t outputWidth, uint32_t outputHeight, int quality,
    uint32_t& outMinWidth, uint32_t& outMinHeight,
    uint32_t& outMaxWidth, uint32_t& outMaxHeight,
    uint32_t& outOptimalWidth, uint32_t& outOptimalHeight)
{
    outMinWidth = outMinHeight = outMaxWidth = outMaxHeight = 0;
    outOptimalWidth = outOptimalHeight = 0;
    if (!g_initOk || !g_caps || !outputWidth || !outputHeight)
        return false;
    float sharpness = 0.0f;
    const NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
        g_caps, outputWidth, outputHeight, ToNgxQuality(quality),
        &outOptimalWidth, &outOptimalHeight, &outMaxWidth, &outMaxHeight,
        &outMinWidth, &outMinHeight, &sharpness);
    if (NVSDK_NGX_FAILED(result))
        return false;
    return outOptimalWidth != 0 && outOptimalHeight != 0 &&
        outMaxWidth != 0 && outMaxHeight != 0;
}

bool Dlss_EnsureFeature(
    int eye, ID3D11DeviceContext* context, const DlssFeatureDesc& desc,
    bool& outRecreated, char* reason, size_t reasonBytes)
{
    outRecreated = false;
    auto fail = [&](const char* text) {
        if (reason && reasonBytes)
            snprintf(reason, reasonBytes, "%s", text);
        return false;
    };
    if (eye < 0 || eye > 1 || !context)
        return fail("bad eye or context");
    if (!g_initOk)
        return fail("NGX is not initialized");
    Feature& feature = g_features[eye];
    if (feature.created && SameDesc(feature.desc, desc))
        return true;
    ReleaseFeature(feature);

    const NVSDK_NGX_Result allocated = g_backendD3D12
        ? NVSDK_NGX_D3D12_AllocateParameters(&feature.params)
        : NVSDK_NGX_D3D11_AllocateParameters(&feature.params);
    if (NVSDK_NGX_FAILED(allocated) || !feature.params)
    {
        feature.params = nullptr;
        char text[160];
        snprintf(text, sizeof(text),
                 "NGX parameter allocation failed (0x%08X)",
                 static_cast<unsigned>(allocated));
        return fail(text);
    }
    SetPresetHints(feature.params, desc.preset);
    NVSDK_NGX_DLSS_Create_Params create = CreateParams(desc);

    NVSDK_NGX_Result created;
    if (g_backendD3D12)
    {
        if (!BeginCommands(g_direct))
            return fail("D3D12 command list could not be opened for feature creation");
        created = NGX_D3D12_CREATE_DLSS_EXT(g_direct.list, 1, 1, &feature.handle,
                                            feature.params, &create);
        if (!EndCommands(g_direct))
            return fail("D3D12 feature creation could not be submitted");
        // One-time: creation must have run before the first evaluation.
        WaitQueue(g_direct, g_direct.doneValue);
    }
    else
    {
        NativeScope scope;
        created = NGX_D3D11_CREATE_DLSS_EXT(context, &feature.handle,
                                            feature.params, &create);
    }
    if (NVSDK_NGX_FAILED(created) || !feature.handle)
    {
        feature.handle = nullptr;
        ReleaseFeature(feature);
        char text[160];
        snprintf(text, sizeof(text),
                 "DLSS feature creation failed (0x%08X) for %ux%u -> %ux%u",
                 static_cast<unsigned>(created), desc.renderWidth,
                 desc.renderHeight, desc.outputWidth, desc.outputHeight);
        return fail(text);
    }
    feature.created = true;
    feature.desc = desc;
    outRecreated = true;
    LOG("DLSS: eye %d feature created (%s): render %ux%u -> output %ux%u, slot %s, "
        "preset %s, depth %s, flags 0x%X",
        eye, g_backendD3D12 ? "D3D12" : "D3D11",
        desc.renderWidth, desc.renderHeight, desc.outputWidth,
        desc.outputHeight, dlss::QualityName(static_cast<dlss::Quality>(desc.quality)),
        dlss::PresetName(desc.preset),
        desc.depthInverted ? "reversed (far=0)" : "standard (far=1)",
        static_cast<unsigned>(create.InFeatureCreateFlags));
    return true;
}

bool Dlss_Evaluate(int eye, ID3D11DeviceContext* context,
                   const DlssEvalInputs& inputs, uint32_t& outFailureCode)
{
    outFailureCode = 0;
    if (eye < 0 || eye > 1 || !context || !g_initOk)
        return false;
    Feature& feature = g_features[eye];
    if (!feature.created || !feature.handle || !feature.params)
        return false;

    LARGE_INTEGER start{}, end{};
    QueryPerformanceCounter(&start);
    NVSDK_NGX_Result result;
    if (g_backendD3D12)
    {
        SharedEye& shared = g_shared[eye];
        if (!shared.color || !shared.depth || !shared.motion || !shared.output)
            return false;
        ID3D11DeviceContext4* context4 = g_evalContext4;
        if (!context4 || context != g_evalContext)
            return false;

        // 1. Everything D3D11 recorded for this eye (its render, the colour
        //    copy, the flow input, the camera-motion pass) lands before the
        //    D3D12 side reads it: GPU-side.
        ++g_toD3D12Value;
        const UINT64 ready = g_toD3D12Value;
        const HRESULT inputSignal = context4->Signal(g_toD3D12_11, ready);
        if (FAILED(inputSignal)) { outFailureCode=static_cast<uint32_t>(inputSignal); return false; }
#ifdef HALOMCCVR_DLSS_BACKEND_BENCH
        if (!g_benchmarkNoInputFlush)
#endif
            context4->Flush();

        // 2. Object motion: the engine compares this render's colour with
        //    the previous render's, waiting on the same fence. Nothing on
        //    the graphics queue waits for it.
        const bool objectMotion = shared.ofSession != nullptr && shared.flowHandle != nullptr;
        bool flow = false;
        UINT64 flowDone = 0;
        int cur = shared.flowCursor;
        if (objectMotion)
        {
            const int prev = cur ^ 1;
            if (inputs.reset)
                shared.flowPrevValid = false; // the previous render belongs to another history
            if (inputs.flowInputWritten && shared.flowPrevValid)
            {
                flowDone = shared.flowDoneValue + 1;
                if (NvOf_Execute(shared.ofSession, shared.flowInputHandle[cur],
                                 shared.flowInputHandle[prev], shared.flowHandle,
                                 shared.flowHintsValid, g_toD3D12, ready, shared.flowDoneFence, flowDone))
                {
                    ++shared.flowDoneValue;
                    flow = true;
                    shared.flowHintsValid = true;
                }
                else
                {
                    shared.flowHintsValid = false;
                }
            }
            else
            {
                shared.flowHintsValid = false;
            }
            shared.flowPrevValid = inputs.flowInputWritten;
            if (inputs.flowInputWritten)
                shared.flowCursor = prev; // the next render overwrites the frame just used as reference
        }

        // 3. The compute queue waits for D3D11 and, when a flow was asked
        //    for, for the engine; then the merge pass and DLSS. This is
        //    called at the END of the eye's render, so the game's raster of
        //    the other eye and this eye's upscale run on the GPU at once.
        const HRESULT inputWait = g_compute.queue->Wait(g_toD3D12, ready);
        if (FAILED(inputWait)) { outFailureCode=static_cast<uint32_t>(inputWait); return false; }
        if (flow)
            g_compute.queue->Wait(shared.flowDoneFence, flowDone);
        if (!BeginCommands(g_compute))
        {
            return false;
        }
        ID3D12GraphicsCommandList* list = g_compute.list;
        CollectGpuTime();
        const UINT queryBase = g_compute.cursor * 3;
        const bool timed = g_gpuMapped != nullptr;
        if (timed) list->EndQuery(g_gpuQueries, D3D12_QUERY_TYPE_TIMESTAMP, queryBase);
        const D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Barrier(list, shared.color, D3D12_RESOURCE_STATE_COMMON, kRead);
        Barrier(list, shared.depth, D3D12_RESOURCE_STATE_COMMON, kRead);
        Barrier(list, shared.output, D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12Resource* motionForDlss = shared.motion;
        if (shared.imageMotion)
        {
            Barrier(list, shared.motionCam, D3D12_RESOURCE_STATE_COMMON, kRead);
            Barrier(list, shared.motion, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            g_imageMotion.Record(eye,list,shared.color,shared.motion,
                inputs.jitterDeltaX,inputs.jitterDeltaY,inputs.reset);
        }
        else if (objectMotion)
        {
            Barrier(list, shared.motionCam, D3D12_RESOURCE_STATE_COMMON, kRead);
            if (flow)
            {
                Barrier(list, shared.flow, D3D12_RESOURCE_STATE_COMMON, kRead);
                Barrier(list, shared.flowInput[0], D3D12_RESOURCE_STATE_COMMON, kRead);
                Barrier(list, shared.flowInput[1], D3D12_RESOURCE_STATE_COMMON, kRead);
                Barrier(list, shared.motion, D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                MergeParams p{};
                p.width = shared.renderWidth;
                p.height = shared.renderHeight;
                p.curIndex = static_cast<uint32_t>(cur);
                p.grid = NvOf_Grid();
                p.invW = 1.0f / static_cast<float>(shared.renderWidth);
                p.invH = 1.0f / static_cast<float>(shared.renderHeight);
                p.flowScale = NvOf_FlowToPixels();
                p.margin = 2.0f / 255.0f; // noise floor for the patch error comparison
                p.jitterDx = inputs.jitterDeltaX;
                p.jitterDy = inputs.jitterDeltaY;
                p.minSwitch = 0.75f;   // flow within this of the camera vector is the camera vector
                ID3D12DescriptorHeap* heaps[1] = {g_descriptorHeap};
                list->SetDescriptorHeaps(1, heaps);
                list->SetComputeRootSignature(g_mergeRootSignature);
                list->SetPipelineState(g_mergePso);
                list->SetComputeRoot32BitConstants(0, sizeof(MergeParams) / 4, &p, 0);
                list->SetComputeRootDescriptorTable(
                    1, GpuDescriptor(static_cast<UINT>(eye) * kMergeDescriptors));
                list->Dispatch((shared.renderWidth + 7) / 8, (shared.renderHeight + 7) / 8, 1);
                D3D12_RESOURCE_BARRIER uav{};
                uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                uav.UAV.pResource = shared.motion;
                list->ResourceBarrier(1, &uav);
                Barrier(list, shared.motion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kRead);
            }
            else
            {
                motionForDlss = shared.motionCam;
            }
        }
        else
        {
            Barrier(list, shared.motion, D3D12_RESOURCE_STATE_COMMON, kRead);
        }
        NVSDK_NGX_D3D12_DLSS_Eval_Params eval{};
        eval.Feature.pInColor = shared.color;
        eval.Feature.pInOutput = shared.output;
        eval.Feature.InSharpness = 0.0f;
        eval.pInDepth = shared.depth;
        eval.pInMotionVectors = motionForDlss;
        eval.InJitterOffsetX = inputs.jitterPixelX;
        eval.InJitterOffsetY = inputs.jitterPixelY;
        eval.InRenderSubrectDimensions.Width = feature.desc.renderWidth;
        eval.InRenderSubrectDimensions.Height = feature.desc.renderHeight;
        eval.InReset = inputs.reset ? 1 : 0;
        eval.InMVScaleX = 1.0f;
        eval.InMVScaleY = 1.0f;
        eval.InFrameTimeDeltaInMsec = inputs.frameTimeMs;
        if (timed) list->EndQuery(g_gpuQueries, D3D12_QUERY_TYPE_TIMESTAMP, queryBase + 1);
        result = NGX_D3D12_EVALUATE_DLSS_EXT(list, feature.handle, feature.params, &eval);
        if (timed)
        {
            list->EndQuery(g_gpuQueries, D3D12_QUERY_TYPE_TIMESTAMP, queryBase + 2);
            list->ResolveQueryData(g_gpuQueries, D3D12_QUERY_TYPE_TIMESTAMP, queryBase, 3,
                g_gpuReadback, queryBase * sizeof(UINT64));
        }
        Barrier(list, shared.color, kRead, D3D12_RESOURCE_STATE_COMMON);
        Barrier(list, shared.depth, kRead, D3D12_RESOURCE_STATE_COMMON);
        Barrier(list, shared.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COMMON);
        if (shared.imageMotion)
        {
            Barrier(list, shared.motionCam, kRead, D3D12_RESOURCE_STATE_COMMON);
            Barrier(list, shared.motion, kRead, D3D12_RESOURCE_STATE_COMMON);
        }
        else if (objectMotion)
        {
            Barrier(list, shared.motionCam, kRead, D3D12_RESOURCE_STATE_COMMON);
            if (flow)
            {
                Barrier(list, shared.flow, kRead, D3D12_RESOURCE_STATE_COMMON);
                Barrier(list, shared.flowInput[0], kRead, D3D12_RESOURCE_STATE_COMMON);
                Barrier(list, shared.flowInput[1], kRead, D3D12_RESOURCE_STATE_COMMON);
                Barrier(list, shared.motion, kRead, D3D12_RESOURCE_STATE_COMMON);
            }
        }
        else
        {
            Barrier(list, shared.motion, kRead, D3D12_RESOURCE_STATE_COMMON);
        }
        const int timingSlot = g_compute.cursor;
        const bool submitted = EndCommands(g_compute);
        g_compute.ring[timingSlot].timingPending = timed && submitted;

        // 4. The queue signals when the output is written. D3D11 does not
        //    wait here; Dlss_SyncD3D11 takes one wait per eye right before
        //    that eye's finished image is read into the headset image.
        ++g_toD3D11Value;
        g_compute.queue->Signal(g_toD3D11, g_toD3D11Value);
        g_pendingD3D11Wait[eye] = g_toD3D11Value;
        if (!submitted)
            result = NVSDK_NGX_Result_Fail;
    }
    else
    {
        if (!inputs.color || !inputs.depth || !inputs.motion || !inputs.output)
            return false;
        if (context != g_nativeContext) return false;
        NativeScope scope;
        g_nativeTiming.Begin(context, true, feature.desc.renderWidth, feature.desc.renderHeight);
        g_nativeTiming.EndRaster(context);
        NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};
        eval.Feature.pInColor = inputs.color;
        eval.Feature.pInOutput = inputs.output;
        eval.Feature.InSharpness = 0.0f;
        eval.pInDepth = inputs.depth;
        eval.pInMotionVectors = inputs.motion;
        eval.InJitterOffsetX = inputs.jitterPixelX;
        eval.InJitterOffsetY = inputs.jitterPixelY;
        eval.InRenderSubrectDimensions.Width = feature.desc.renderWidth;
        eval.InRenderSubrectDimensions.Height = feature.desc.renderHeight;
        eval.InReset = inputs.reset ? 1 : 0;
        eval.InMVScaleX = 1.0f;
        eval.InMVScaleY = 1.0f;
        eval.InFrameTimeDeltaInMsec = inputs.frameTimeMs;
        g_ngxQuiet = true;
        result = NGX_D3D11_EVALUATE_DLSS_EXT(context, feature.handle, feature.params, &eval);
        g_ngxQuiet = false;
        g_nativeTiming.End(context);
    }
    QueryPerformanceCounter(&end);
    if (g_qpcFrequency.QuadPart)
    {
        g_evalCpuUs += static_cast<double>(end.QuadPart - start.QuadPart) * 1e6 /
            static_cast<double>(g_qpcFrequency.QuadPart);
    }
    ++g_evalCount;

    if (NVSDK_NGX_FAILED(result))
    {
        // A failed NGX evaluation can still have submitted the preparation
        // list. Keep the next D3D11 write behind that GPU work even though
        // this frame takes the ordinary resolve and never calls FinishEye.
        Dlss_SyncD3D11(context, eye);
        outFailureCode = static_cast<uint32_t>(result);
        if (g_evalFailuresLogged++ % 600 == 0)
        {
            LOG("DLSS: eye %d evaluation failed (0x%08X); this frame uses the "
                "mod's own resolve", eye, static_cast<unsigned>(result));
        }
        return false;
    }
    return true;
}

bool Dlss_GetFeatureDesc(int eye, DlssFeatureDesc& out)
{
    if (eye < 0 || eye > 1 || !g_features[eye].created)
        return false;
    out = g_features[eye].desc;
    return true;
}

void Dlss_SyncD3D11(ID3D11DeviceContext* context, int eye)
{
    if (!g_backendD3D12 || eye < 0 || eye > 1 || !g_pendingD3D11Wait[eye] || !context)
        return;
    ID3D11DeviceContext4* context4 = g_evalContext4;
    if (context4 && context == g_evalContext)
    {
        context4->Wait(g_toD3D11_11, g_pendingD3D11Wait[eye]);
    }
    g_pendingD3D11Wait[eye] = 0;
}

void Dlss_TakeCpuTime(double& outMicroseconds, uint32_t& outEvaluations)
{
    outMicroseconds = g_evalCpuUs;
    outEvaluations = g_evalCount;
    g_evalCpuUs = 0.0;
    g_evalCount = 0;
}

void Dlss_TakeGpuTime(DlssGpuTiming& out)
{
    CollectGpuTime();
    if (!g_backendD3D12 && g_nativeContext && g_features[0].created)
    {
        const auto& shape = g_features[0].desc;
        const auto completed = g_nativeTiming.Collect(g_nativeContext, true, shape.renderWidth, shape.renderHeight);
        g_gpuTiming.evaluateMicroseconds += completed.prepareUs;
        g_gpuTiming.evaluations += completed.eyes;
    }
    out = g_gpuTiming;
    g_gpuTiming = {};
}

void Dlss_ReleaseFeatures()
{
    if (g_backendD3D12)
        WaitAllQueues();
    for (Feature& feature : g_features)
    {
        if (feature.created)
            LOG("DLSS: releasing eye feature (%ux%u -> %ux%u)",
                feature.desc.renderWidth, feature.desc.renderHeight,
                feature.desc.outputWidth, feature.desc.outputHeight);
        ReleaseFeature(feature);
    }
    if (g_ngxSuppressed)
    {
        LOG("NGX: %llu per-frame lines were counted but not written while DLSS ran",
            static_cast<unsigned long long>(g_ngxSuppressed));
        g_ngxSuppressed = 0;
        g_ngxSuppressedSampleLogged = false;
    }
}

void Dlss_Shutdown()
{
    Dlss_ReleaseFeatures();
    for (int eye = 0; eye < 2; ++eye)
        Dlss_ReleaseEyeTextures(eye);
    if (g_caps)
    {
        if (g_backendD3D12)
            NVSDK_NGX_D3D12_DestroyParameters(g_caps);
        else
            NVSDK_NGX_D3D11_DestroyParameters(g_caps);
        g_caps = nullptr;
    }
    if (g_initOk)
    {
#ifdef HALOMCCVR_DLSS_PIPELINE_PROBE
        LOG("cleanup NGX shutdown begin");
#endif
        if (g_backendD3D12 && g_device12)
            NVSDK_NGX_D3D12_Shutdown1(g_device12);
        else if (g_device)
            NVSDK_NGX_D3D11_Shutdown1(g_device);
        LOG("DLSS: NGX shut down");
    }
    ShutdownD3D12();
    g_nativeTiming.Release();
    SafeRelease(g_nativeState);
    SafeRelease(g_nativeContext);
    g_backendD3D12 = false;
    g_initOk = false;
    g_initTried = false;
    g_device = nullptr;
    SetStatus("off");
}
