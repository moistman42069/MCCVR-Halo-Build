// NVIDIA Optical Flow SDK, DirectX 12 interface (API 3.0 layouts).
//
// NVIDIA publishes only the CUDA interface on GitHub; the DirectX 12 header
// ships inside the SDK download. This file reproduces the interface from:
//   * the NVIDIA Optical Flow SDK Programming Guide (NVOF_PG-09417-001_v03),
//     sections 3.2.2, 4.2, 5.2 and 6.2: the function names, the call
//     sequence, and the meaning of every field (fence points are pairs of an
//     ID3D12Fence pointer and a value; the execute call takes an array of
//     input fence points the engine waits for and one output fence point it
//     signals when done);
//   * nvofapi64.dll itself: the exported NvOFAPICreateInstanceD3D12 accepts
//     API versions 3.0, 4.0 and 5.0 and stores its ten entries at the same
//     slots as the DirectX 11 table (0x00 create, 0x08 init, 0x10/0x18
//     surface-format count/list, 0x20 register, 0x28 unregister, 0x30
//     execute, 0x38 destroy, 0x40 last-error, 0x48 caps), and its parameter
//     validation reads each field at the offsets fixed below (register:
//     resource +0x00, input fence +0x08/+0x10, handle out +0x18, output fence
//     +0x20/+0x28; execute input: fence count +0x3C, fence array +0x40;
//     execute output: fence point +0x38; init: three more dwords after the
//     2.0 layout for API 3.0).
// The mod verifies this table offline against the driver before use
// (tools/probes/nvof_d3d12_probe.cpp), and every call is fail-open.
#pragma once

#include <d3d12.h>
#include "nvOpticalFlowCommon.h"

#if defined(__cplusplus)
extern "C" {
#endif

// The DirectX 12 interface exists from API 3.0 on; the driver serves 3.0
// with the 3.0 parameter layouts below.
#define NV_OF_D3D12_API_VERSION ((uint16_t)0x30)

typedef struct _NV_OF_FENCE_POINT
{
    ID3D12Fence* fence;
    uint64_t     value;
} NV_OF_FENCE_POINT;

// NV_OF_INIT_PARAMS as API 3.0 lays it out: the 2.0 fields, then three
// dwords (prediction direction, global-flow enable, input buffer format);
// zero means forward prediction, no global flow, format from the resource.
typedef struct _NV_OF_INIT_PARAMS_V3
{
    uint32_t                        width;                 // +0x00
    uint32_t                        height;                // +0x04
    NV_OF_OUTPUT_VECTOR_GRID_SIZE   outGridSize;           // +0x08
    NV_OF_HINT_VECTOR_GRID_SIZE     hintGridSize;          // +0x0C
    NV_OF_MODE                      mode;                  // +0x10
    NV_OF_PERF_LEVEL                perfLevel;             // +0x14
    NV_OF_BOOL                      enableExternalHints;   // +0x18
    NV_OF_BOOL                      enableOutputCost;      // +0x1C
    NvOFPrivDataHandle              hPrivData;             // +0x20
    NV_OF_STEREO_DISPARITY_RANGE    disparityRange;        // +0x28
    NV_OF_BOOL                      enableRoi;             // +0x2C
    uint32_t                        predDirection;         // +0x30 (0 = forward)
    NV_OF_BOOL                      enableGlobalFlow;      // +0x34
    uint32_t                        inputBufferFormat;     // +0x38 (0 = from the resource)
    uint32_t                        reserved3C;            // +0x3C
} NV_OF_INIT_PARAMS_V3;

typedef struct _NV_OF_REGISTER_RESOURCE_PARAMS_D3D12
{
    ID3D12Resource*       resource;          // +0x00
    NV_OF_FENCE_POINT     inputFencePoint;   // +0x08: the engine waits for this before touching the resource
    NvOFGPUBufferHandle*  hOFGpuBuffer;      // +0x18: receives the handle
    NV_OF_FENCE_POINT     outputFencePoint;  // +0x20: signalled when the registration work is done
} NV_OF_REGISTER_RESOURCE_PARAMS_D3D12;

typedef struct _NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12
{
    NvOFGPUBufferHandle   hOFGpuBuffer;      // +0x00
} NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12;

typedef struct _NV_OF_EXECUTE_INPUT_PARAMS_D3D12
{
    NvOFGPUBufferHandle   inputFrame;            // +0x00
    NvOFGPUBufferHandle   referenceFrame;        // +0x08
    NvOFGPUBufferHandle   externalHints;         // +0x10
    NV_OF_BOOL            disableTemporalHints;  // +0x18
    uint32_t              padding;               // +0x1C
    NvOFPrivDataHandle    hPrivData;             // +0x20
    uint32_t              padding2;              // +0x28
    uint32_t              numRois;               // +0x2C
    NV_OF_ROI_RECT*       roiData;               // +0x30
    uint32_t              reserved38;            // +0x38
    uint32_t              numFencePoints;        // +0x3C: at least one is required
    NV_OF_FENCE_POINT*    fencePoint;            // +0x40: the engine waits for every one
} NV_OF_EXECUTE_INPUT_PARAMS_D3D12;

typedef struct _NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12
{
    NvOFGPUBufferHandle   outputBuffer;          // +0x00
    NvOFGPUBufferHandle   outputCostBuffer;      // +0x08
    NvOFPrivDataHandle    hPrivData;             // +0x10
    NvOFGPUBufferHandle   bwdOutputBuffer;       // +0x18
    NvOFGPUBufferHandle   bwdOutputCostBuffer;   // +0x20
    NvOFGPUBufferHandle   globalFlowBuffer;      // +0x28
    uint64_t              reserved30;            // +0x30
    NV_OF_FENCE_POINT*    fencePoint;            // +0x38: signalled when the flow is written
} NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12;

typedef NV_OF_STATUS(NVOFAPI* PFNNVCREATEOPTICALFLOWD3D12)(ID3D12Device* pD3D12Device, NvOFHandle* hOf);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFINITD3D12)(NvOFHandle hOf, const NV_OF_INIT_PARAMS_V3* initParams);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFGETSURFACEFORMATCOUNTD3D12)(
    NvOFHandle hOf, NV_OF_BUFFER_USAGE bufferUsage, NV_OF_MODE ofMode, uint32_t* pCount);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFGETSURFACEFORMATD3D12)(
    NvOFHandle hOf, NV_OF_BUFFER_USAGE bufferUsage, NV_OF_MODE ofMode, DXGI_FORMAT* pFormat);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFREGISTERRESOURCED3D12)(
    NvOFHandle hOf, NV_OF_REGISTER_RESOURCE_PARAMS_D3D12* registerParams);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFUNREGISTERRESOURCED3D12)(
    NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12* unregisterParams);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFEXECUTED3D12)(
    NvOFHandle hOf, const NV_OF_EXECUTE_INPUT_PARAMS_D3D12* executeInParams,
    NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12* executeOutParams);

typedef struct _NV_OF_D3D12_API_FUNCTION_LIST
{
    PFNNVCREATEOPTICALFLOWD3D12        nvCreateOpticalFlowD3D12;        // 0x00
    PFNNVOFINITD3D12                   nvOFInit;                        // 0x08
    PFNNVOFGETSURFACEFORMATCOUNTD3D12  nvOFGetSurfaceFormatCountD3D12;  // 0x10
    PFNNVOFGETSURFACEFORMATD3D12       nvOFGetSurfaceFormatD3D12;       // 0x18
    PFNNVOFREGISTERRESOURCED3D12       nvOFRegisterResourceD3D12;       // 0x20
    PFNNVOFUNREGISTERRESOURCED3D12     nvOFUnregisterResourceD3D12;     // 0x28
    PFNNVOFEXECUTED3D12                nvOFExecuteD3D12;                // 0x30
    PFNNVOFDESTROY                     nvOFDestroy;                     // 0x38
    PFNNVOFGETLASTERROR                nvOFGetLastError;                // 0x40
    PFNNVOFGETCAPS                     nvOFGetCaps;                     // 0x48
} NV_OF_D3D12_API_FUNCTION_LIST;

typedef NV_OF_STATUS(NVOFAPI* PFNNVOFAPICREATEINSTANCED3D12)(
    uint32_t apiVer, NV_OF_D3D12_API_FUNCTION_LIST* functionList);

#if defined(__cplusplus)
}
#endif
