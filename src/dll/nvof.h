#pragma once

// NVIDIA optical flow (the OFA engine on RTX 20-series and newer) through the
// driver's nvofapi64.dll, DirectX 12 interface. It measures the real
// per-pixel motion between the previous and current eye image for things
// the engine gives no velocity for (characters, vehicles, the weapon); the
// camera-only motion vectors are right for the world and wrong for those.
//
// DirectX 12, not 11, because only the DirectX 12 interface takes fences:
// the engine waits for a fence before it reads the frames and signals one
// when the flow is written. The DirectX 11 interface has neither, and a
// draw that read its output straight away got the previous frame's flow 17
// times in 40 (tools/probes/oforder, DLSS-17's smear). Measured on this GPU
// with tools/probes/nvof_d3d12_probe.cpp: 40 of 40 fresh at every level.
//
// Everything is fail-open: without the DLL, on a non-NVIDIA card, or on any
// error, the functions report false and the mod keeps camera-only motion.
// nvofapi64.dll ships with the NVIDIA driver and is never bundled.

#include <d3d12.h>
#include <cstddef>
#include <cstdint>

struct NvOfSession;

// Loads the driver library and creates the API instance once for `device`.
// False (with NvOf_StatusText saying why) when optical flow cannot be used.
bool NvOf_Available(ID3D12Device* device);
const char* NvOf_StatusText();

// The formats the driver accepts (queried at first use): the input frames
// and the flow output the mod must create its textures in.
DXGI_FORMAT NvOf_InputFormat();
DXGI_FORMAT NvOf_OutputFormat();
// Flow is one vector per grid x grid block of the input.
uint32_t NvOf_Grid();
// Flow output texels are 16-bit signed-normalized; multiply the sampled
// [-1, 1] value by this to get pixels.
float NvOf_FlowToPixels();

// One session per eye: a fixed input size. `why` receives the reason on
// failure.
NvOfSession* NvOf_CreateSession(uint32_t width, uint32_t height, char* why, size_t whyBytes);
void NvOf_DestroySession(NvOfSession* session);

// Registers a D3D12 texture (input frame at the session size in
// NvOf_InputFormat, or the flow output at grid size in NvOf_OutputFormat).
// The engine waits for `waitFence` to reach `waitValue` before touching it
// and signals `signalFence` with `signalValue` when its own work is done.
bool NvOf_Register(NvOfSession* session, ID3D12Resource* resource,
                   ID3D12Fence* waitFence, uint64_t waitValue,
                   ID3D12Fence* signalFence, uint64_t signalValue,
                   void** outHandle);
void NvOf_Unregister(void* handle);

// Computes the flow from `input` (this frame) to `reference` (the previous
// frame): each output vector is where that block WAS, minus where it is,
// in pixels - exactly the motion vector DLSS asks for (measured: an 8-px
// mover reads -8.0). The engine waits for `waitFence` >= `waitValue` (the
// frames are written) and signals `signalFence` = `signalValue` when the
// output is complete. `temporalHints` lets the engine seed the search with
// the last result (successive frames).
bool NvOf_Execute(NvOfSession* session, void* input, void* reference, void* output,
                  bool temporalHints, ID3D12Fence* waitFence, uint64_t waitValue,
                  ID3D12Fence* signalFence, uint64_t signalValue);
