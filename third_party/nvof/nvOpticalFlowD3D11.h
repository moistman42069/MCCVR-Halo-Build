// NVIDIA Optical Flow SDK, DirectX 11 interface.
//
// NVIDIA publishes only the CUDA interface on GitHub; the DirectX 11 header
// ships inside the SDK download. This file reproduces the DirectX 11
// interface from two sources, so that nothing in it is assumed:
//   * the function NAMES and call sequence from the NVIDIA Optical Flow SDK
//     Programming Guide (NVOF_PG-09417-001_v03), sections 3.2.1 and 4.1;
//   * the function-table LAYOUT from nvofapi64.dll itself: the exported
//     NvOFAPICreateInstanceD3D11 stores its entries at fixed offsets, and
//     the entries it shares with the CUDA table (whose layout IS public in
//     nvOpticalFlowCuda.h) sit at the same addresses, which fixes every
//     slot: 0x00 create, 0x08 nvOFInit, 0x10/0x18 surface-format
//     count/list, 0x20 register(hOf, resource, &handle), 0x28 unregister(handle), 0x30 nvOFExecute,
//     0x38 nvOFDestroy, 0x40 nvOFGetLastError, 0x48 nvOFGetCaps.
// The mod verifies this table offline against the driver before use
// (tools/probes/nvof_d3d11_probe.cpp), and every call is fail-open.
#pragma once

#include <d3d11.h>
#include "nvOpticalFlowCommon.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef NV_OF_STATUS(NVOFAPI* PFNNVCREATEOPTICALFLOWD3D11)(
    ID3D11Device* pD3D11Device, ID3D11DeviceContext* pD3D11DeviceContext, NvOFHandle* hOf);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFGETSURFACEFORMATCOUNTD3D11)(
    NvOFHandle hOf, NV_OF_BUFFER_USAGE bufferUsage, NV_OF_MODE ofMode, uint32_t* pCount);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFGETSURFACEFORMATD3D11)(
    NvOFHandle hOf, NV_OF_BUFFER_USAGE bufferUsage, NV_OF_MODE ofMode, DXGI_FORMAT* pFormat);
// Verified against the driver's implementation (nvofapi64.dll 580-series,
// API 5.0): the register entry validates its second and third arguments as
// the resource and the output handle pointer, and unregister takes the
// handle itself. There is no parameter block on the DirectX 11 path.
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFREGISTERRESOURCED3D11)(
    NvOFHandle hOf, ID3D11Resource* pResource, NvOFGPUBufferHandle* hOFGpuBuffer);
typedef NV_OF_STATUS(NVOFAPI* PFNNVOFUNREGISTERRESOURCED3D11)(
    NvOFGPUBufferHandle hOFGpuBuffer);

typedef struct _NV_OF_D3D11_API_FUNCTION_LIST
{
    PFNNVCREATEOPTICALFLOWD3D11        nvCreateOpticalFlowD3D11;        // 0x00
    PFNNVOFINIT                        nvOFInit;                        // 0x08
    PFNNVOFGETSURFACEFORMATCOUNTD3D11  nvOFGetSurfaceFormatCountD3D11;  // 0x10
    PFNNVOFGETSURFACEFORMATD3D11       nvOFGetSurfaceFormatD3D11;       // 0x18
    PFNNVOFREGISTERRESOURCED3D11       nvOFRegisterResourceD3D11;       // 0x20
    PFNNVOFUNREGISTERRESOURCED3D11     nvOFUnregisterResourceD3D11;     // 0x28
    PFNNVOFEXECUTE                     nvOFExecute;                     // 0x30
    PFNNVOFDESTROY                     nvOFDestroy;                     // 0x38
    PFNNVOFGETLASTERROR                nvOFGetLastError;                // 0x40
    PFNNVOFGETCAPS                     nvOFGetCaps;                     // 0x48
} NV_OF_D3D11_API_FUNCTION_LIST;

typedef NV_OF_STATUS(NVOFAPI* PFNNVOFAPICREATEINSTANCED3D11)(
    uint32_t apiVer, NV_OF_D3D11_API_FUNCTION_LIST* functionList);

#if defined(__cplusplus)
}
#endif
