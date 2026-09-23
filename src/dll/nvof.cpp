#include "nvof.h"

#include <cstdio>
#include <cstring>

#include "../common/log.h"
#include "../../third_party/nvof/nvOpticalFlowD3D12.h"

namespace
{
    // API 3.0: the first version with the DirectX 12 interface; the driver
    // serves it with the 3.0 parameter layouts the header reproduces
    // (tools/probes/nvof_d3d12_probe.cpp verifies every call).
    constexpr uint32_t kApiVersion = NV_OF_D3D12_API_VERSION;
    constexpr uint32_t kGrid = 4;

    HMODULE g_module = nullptr;
    NV_OF_D3D12_API_FUNCTION_LIST g_api{};
    ID3D12Device* g_device = nullptr;
    bool g_tried = false;
    bool g_available = false;
    char g_status[256] = "not initialised";
    DXGI_FORMAT g_inputFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT g_outputFormat = DXGI_FORMAT_UNKNOWN;
    uint64_t g_executeFailuresLogged = 0;

    const char* StatusName(NV_OF_STATUS s)
    {
        switch (s)
        {
        case NV_OF_SUCCESS: return "success";
        case NV_OF_ERR_OF_NOT_AVAILABLE: return "optical flow not available on this GPU";
        case NV_OF_ERR_UNSUPPORTED_DEVICE: return "unsupported device";
        case NV_OF_ERR_DEVICE_DOES_NOT_EXIST: return "device does not exist";
        case NV_OF_ERR_INVALID_PTR: return "invalid pointer";
        case NV_OF_ERR_INVALID_PARAM: return "invalid parameter";
        case NV_OF_ERR_INVALID_CALL: return "invalid call";
        case NV_OF_ERR_INVALID_VERSION: return "invalid version";
        case NV_OF_ERR_OUT_OF_MEMORY: return "out of memory";
        case NV_OF_ERR_NOT_INITIALIZED: return "not initialised";
        case NV_OF_ERR_UNSUPPORTED_FEATURE: return "unsupported feature";
        case NV_OF_ERR_GENERIC: return "generic failure";
        default: return "unknown status";
        }
    }

    // Picks the formats the mod can feed from its own textures: BGRA8 input
    // (the driver's "ABGR8" is DXGI B8G8R8A8), RG16 signed-integer output.
    bool ChooseFormats(NvOFHandle probe, char* why, size_t whyBytes)
    {
        auto has = [&](NV_OF_BUFFER_USAGE usage, DXGI_FORMAT want) {
            uint32_t count = 0;
            if (g_api.nvOFGetSurfaceFormatCountD3D12(probe, usage, NV_OF_MODE_OPTICALFLOW, &count) !=
                    NV_OF_SUCCESS || !count || count > 16)
                return false;
            DXGI_FORMAT formats[16]{};
            if (g_api.nvOFGetSurfaceFormatD3D12(probe, usage, NV_OF_MODE_OPTICALFLOW, formats) !=
                NV_OF_SUCCESS)
                return false;
            for (uint32_t i = 0; i < count; ++i)
                if (formats[i] == want)
                    return true;
            return false;
        };
        if (!has(NV_OF_BUFFER_USAGE_INPUT, DXGI_FORMAT_B8G8R8A8_UNORM))
        {
            snprintf(why, whyBytes, "the driver does not accept BGRA8 input frames");
            return false;
        }
        if (!has(NV_OF_BUFFER_USAGE_OUTPUT, DXGI_FORMAT_R16G16_SINT))
        {
            snprintf(why, whyBytes, "the driver does not produce RG16 flow output");
            return false;
        }
        g_inputFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        g_outputFormat = DXGI_FORMAT_R16G16_SINT;
        return true;
    }
}

struct NvOfSession
{
    NvOFHandle handle = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

bool NvOf_Available(ID3D12Device* device)
{
    if (g_tried)
        return g_available && device == g_device;
    g_tried = true;
    g_device = device;
    auto fail = [&](const char* text) {
        snprintf(g_status, sizeof(g_status), "unavailable: %s", text);
        LOG("DLSS: object motion (optical flow) %s; camera-only motion vectors stay", g_status);
        return false;
    };
    if (!device)
        return fail("no D3D12 device (DLSS is on the D3D11 backend)");
    g_module = LoadLibraryW(L"nvofapi64.dll");
    if (!g_module)
        return fail("nvofapi64.dll is not present (NVIDIA driver with optical flow needed)");
    auto createInstance = reinterpret_cast<PFNNVOFAPICREATEINSTANCED3D12>(
        GetProcAddress(g_module, "NvOFAPICreateInstanceD3D12"));
    auto maxVersion = reinterpret_cast<NV_OF_STATUS(NVOFAPI*)(uint32_t*)>(
        GetProcAddress(g_module, "NvOFGetMaxSupportedApiVersion"));
    if (!createInstance)
        return fail("nvofapi64.dll has no DirectX 12 entry point");
    uint32_t maxVer = 0;
    if (maxVersion)
        maxVersion(&maxVer);
    if (maxVer && maxVer < kApiVersion)
        return fail("the driver's optical flow is older than API 3.0");
    NV_OF_STATUS status = createInstance(kApiVersion, &g_api);
    if (status != NV_OF_SUCCESS)
    {
        char text[128];
        snprintf(text, sizeof(text), "API instance: %s", StatusName(status));
        return fail(text);
    }
    // A throwaway session to query formats and to prove the engine exists.
    NvOFHandle probe = nullptr;
    status = g_api.nvCreateOpticalFlowD3D12(device, &probe);
    if (status != NV_OF_SUCCESS || !probe)
    {
        char text[128];
        snprintf(text, sizeof(text), "session: %s", StatusName(status));
        return fail(text);
    }
    char why[160] = "";
    const bool formats = ChooseFormats(probe, why, sizeof(why));
    g_api.nvOFDestroy(probe);
    if (!formats)
        return fail(why);
    g_available = true;
    snprintf(g_status, sizeof(g_status),
             "ready (nvofapi64.dll, DirectX 12 API %u.%u with fences, driver max %u.%u, %ux%u blocks)",
             kApiVersion >> 4, kApiVersion & 0xF, maxVer >> 4, maxVer & 0xF, kGrid, kGrid);
    LOG("DLSS: object motion (optical flow) %s", g_status);
    return true;
}

const char* NvOf_StatusText() { return g_status; }
DXGI_FORMAT NvOf_InputFormat() { return g_inputFormat; }
DXGI_FORMAT NvOf_OutputFormat() { return g_outputFormat; }
uint32_t NvOf_Grid() { return kGrid; }
// Driver-advertised R16G16_SINT (DXGI 38), S10.5: pixels = raw / 32.
// The old standalone probe accepted an unadvertised SNORM texture, while
// production rejected the advertised SINT format and never ran object motion.
float NvOf_FlowToPixels() { return 1.0f / 32.0f; }

NvOfSession* NvOf_CreateSession(uint32_t width, uint32_t height, char* why, size_t whyBytes)
{
    if (!g_available || !width || !height)
    {
        snprintf(why, whyBytes, "%s", g_available ? "empty size" : g_status);
        return nullptr;
    }
    NvOFHandle handle = nullptr;
    NV_OF_STATUS status = g_api.nvCreateOpticalFlowD3D12(g_device, &handle);
    if (status != NV_OF_SUCCESS || !handle)
    {
        snprintf(why, whyBytes, "session: %s", StatusName(status));
        return nullptr;
    }
    NV_OF_INIT_PARAMS_V3 init{};
    init.width = width;
    init.height = height;
    init.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
    init.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
    init.mode = NV_OF_MODE_OPTICALFLOW;
    // MEDIUM: exact to the pixel on textured content in the probe (-8.00 for
    // an 8-px move at every level); it costs nothing on the shader cores
    // and about 0.1 ms more engine time than FAST at these sizes.
    init.perfLevel = NV_OF_PERF_LEVEL_MEDIUM;
    status = g_api.nvOFInit(handle, &init);
    if (status != NV_OF_SUCCESS)
    {
        char detail[256] = "";
        uint32_t size = sizeof(detail);
        g_api.nvOFGetLastError(handle, detail, &size);
        snprintf(why, whyBytes, "init %ux%u: %s%s%s", width, height, StatusName(status),
                 detail[0] ? " - " : "", detail);
        g_api.nvOFDestroy(handle);
        return nullptr;
    }
    NvOfSession* session = new NvOfSession{};
    session->handle = handle;
    session->width = width;
    session->height = height;
    return session;
}

void NvOf_DestroySession(NvOfSession* session)
{
    if (!session)
        return;
    if (session->handle)
        g_api.nvOFDestroy(session->handle);
    delete session;
}

bool NvOf_Register(NvOfSession* session, ID3D12Resource* resource,
                   ID3D12Fence* waitFence, uint64_t waitValue,
                   ID3D12Fence* signalFence, uint64_t signalValue,
                   void** outHandle)
{
    if (!outHandle)
        return false;
    *outHandle = nullptr;
    if (!session || !session->handle || !resource || !waitFence || !signalFence)
        return false;
    NvOFGPUBufferHandle handle = nullptr;
    NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 params{};
    params.resource = resource;
    params.inputFencePoint.fence = waitFence;
    params.inputFencePoint.value = waitValue;
    params.hOFGpuBuffer = &handle;
    params.outputFencePoint.fence = signalFence;
    params.outputFencePoint.value = signalValue;
    const NV_OF_STATUS status = g_api.nvOFRegisterResourceD3D12(session->handle, &params);
    if (status != NV_OF_SUCCESS || !handle)
    {
        char detail[256] = "";
        uint32_t size = sizeof(detail);
        g_api.nvOFGetLastError(session->handle, detail, &size);
        LOG("DLSS ERROR: optical flow could not register a texture: %s%s%s",
            StatusName(status), detail[0] ? " - " : "", detail);
        return false;
    }
    *outHandle = handle;
    return true;
}

void NvOf_Unregister(void* handle)
{
    if (!handle || !g_available)
        return;
    NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 params{};
    params.hOFGpuBuffer = static_cast<NvOFGPUBufferHandle>(handle);
    g_api.nvOFUnregisterResourceD3D12(&params);
}

bool NvOf_Execute(NvOfSession* session, void* input, void* reference, void* output,
                  bool temporalHints, ID3D12Fence* waitFence, uint64_t waitValue,
                  ID3D12Fence* signalFence, uint64_t signalValue)
{
    if (!session || !session->handle || !input || !reference || !output ||
        !waitFence || !signalFence)
        return false;
    NV_OF_FENCE_POINT wait{waitFence, waitValue};
    NV_OF_FENCE_POINT signal{signalFence, signalValue};
    NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
    in.inputFrame = static_cast<NvOFGPUBufferHandle>(input);
    in.referenceFrame = static_cast<NvOFGPUBufferHandle>(reference);
    in.disableTemporalHints = temporalHints ? NV_OF_FALSE : NV_OF_TRUE;
    in.numFencePoints = 1;
    in.fencePoint = &wait;
    NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};
    out.outputBuffer = static_cast<NvOFGPUBufferHandle>(output);
    out.fencePoint = &signal;
    const NV_OF_STATUS status = g_api.nvOFExecuteD3D12(session->handle, &in, &out);
    if (status != NV_OF_SUCCESS)
    {
        if (g_executeFailuresLogged < 3)
        {
            ++g_executeFailuresLogged;
            char detail[256] = "";
            uint32_t size = sizeof(detail);
            g_api.nvOFGetLastError(session->handle, detail, &size);
            LOG("DLSS ERROR: optical flow execute failed: %s%s%s", StatusName(status),
                detail[0] ? " - " : "", detail);
        }
        return false;
    }
    return true;
}
