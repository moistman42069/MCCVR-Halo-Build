# Optional native bloom consumer override

This is implementation and offline verification evidence for an unaccepted
candidate. No MCC installation or launch, contributor-binary execution, or
headset acceptance occurred. The accepted pointer remains unchanged.

## Contribution and behavior

The target experience is an optional Picture setting that removes the verified
bloom contribution while preserving the native scene shader and draw. Each
supported title saves its own setting; all six `bloom_enabled` values default
true, preserving native bloom. The control is exposed only for Halo 3, ODST and
Reach because these are the evidenced consumer paths. CE, Halo 2 and Halo 4
retain native behavior. This does not claim exposure control or all-title bloom
parity.

The adapted source is martysl1's final active V14/V17/H3ODST5 consumer path in
`MCCVR_CUSTOM_CHANGES/integration_source/src/dll/d3d11_hook.cpp`, preserved under
`out/community-audit-20260923/martysl1-custom`. The original source ZIP SHA-256 is
`374D8D12EB509D34F0CB7E6B68793A6CEF62DA5E642A1C4D00FB4A744E0AB365`.
The dormant V13 producer-clear experiment (`6A2F1FE6`) is not enabled.

The exact consumer contracts are:

- Reach: pixel-shader CRC32 `8A9EA430`, slot 1, a 72 by 45 two-dimensional
  color view. Only slot 1 is substituted.
- Halo 3 and ODST: pixel-shader CRC32 `2D28CEF0`, slots 0 and 1, matching known
  uncompressed formats, slot 0 at least 32 by 32, slot 1 at least 8 by 8,
  and both dimensions approximately 4:1 with the donor's three-texel rounding
  tolerance. Both inputs change together or neither changes.

The draw also requires the actual active title, exact immediate context,
current raster eye, and raster owner thread. Dimensions alone never identify
a bloom draw. The original native draw executes exactly once. A scope restores
the original input views immediately afterward, including the shared HUD draw
paths; no shader replacement, producer clear, or draw skipping is used.

## Resource and lifetime constraints

`CreateShaderResourceView` observes eligible resource metadata outside the draw
hook and precreates a zero-filled immutable texture/view. Eligible sources are
single-sample, single-array, mip-zero 2D renderable color resources of known
byte size. Unknown formats, failed creation, unknown state and cache misses
leave native bloom for that draw. Hook installation is feature-local: missing
observer/draw coverage leaves bloom native and never disarms camera or OpenXR.

The source view and zero view are owned references. A nonblocking reader gate
prevents eviction while any draw can restore a view. Cold writers serialize
resource mutation, never wait for a draw reader, and defer a requested drain.
The cache has 512 entries and a 64 MiB logical texel-footprint cap counting
both the full pinned source mip chain and the zero twin. Driver allocation
alignment/metadata and transient CPU upload storage are outside that logical
footprint. Under pressure, cold admission evicts larger entries before smaller
pyramid candidates; it never allocates or evicts during drawing.

Creation can precede title discovery, and resources can survive a warm title
return. The pointer-owned bounded cache therefore survives title/generation
changes; those changes invalidate observed bindings. Device replacement and
resize request a safe full drain. Backing resources are queried at creation
through `IDXGIResource::GetUsage`; failure or `DXGI_USAGE_BACK_BUFFER` rejects
admission. Microsoft documents this flag as identifying a swap-chain buffer,
so cached source ownership cannot pin such a buffer and block resize.
See [DXGI usage flags](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-usage).

Shader creation records exact CRC identity in a bounded atomic registry and
invalidates a reused address even when the new shader is unrelated. A sequence
check rejects partly published pointer/hash pairs. Binding observers never
acquire the resource-factory writer lock. Shader class instances are declined.

Shader/resource binding, OM target changes, context clearing, command-list
execution, and context-state swaps maintain or invalidate the observed state.
OM changes invalidate resource bindings because Direct3D can implicitly unbind
conflicting inputs. Native setters perform all actual binding and restoration;
the override does not query COM state in a draw. Microsoft documents both the
hazard behavior and retained binding references in
[OMSetRenderTargets](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets)
and [PSSetShaderResources](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshaderresources).

Draw hooks contain no allocation, resource descriptions/queries, AddRef/Release,
file I/O, logs, signature scans, or locks. They use bounded metadata lookups,
atomic admission, and the original native setter/draw. Cold worker telemetry
reports substitutions, stock misses, capacity, creation failure and pending
drain without affecting VR ownership.

## Verification and remaining limits

The focused `halomccvr_bloom_override_tests` target passes using a synthetic WARP
device and actual D3D11 textures/views. It verifies exact title/CRC/topology,
default/native and non-eye behavior, all-zero RGBA texture contents, simultaneous
H3/ODST pair replacement and exact restoration, Reach slot 1 only, shader address
reuse, state invalidation, bounded footprint arithmetic, admission while a
writer is active, deferred drain while restoration owns a reader, small-resource
priority, and resources created before title discovery/switch.

A hidden synthetic WARP swap chain provides a shader-readable backbuffer that
otherwise matches the H3 pyramid. The cache declines it, then native
`ResizeBuffers` succeeds after caller references are released, without requiring
a cache drain. This checks the backbuffer exclusion against actual DXGI behavior.
No real game directory or process is involved. The focused Release tests and
the DLL/launcher Release targets compile and pass on September 23.

This does not prove the donor CRCs appear in every retail/custom-map draw or
that the visual result is comfortable. Unsupported source formats, missing
factory observations, resource creation rejected during a reader, bounded
registry/cache exhaustion, and invalidated state without fresh bindings retain
native bloom. Target-title headset tests, title switching/resizing checks, and
a Halo 3 regression are still required before runtime acceptance.
