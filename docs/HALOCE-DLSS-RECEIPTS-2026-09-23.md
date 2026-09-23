# CE optional DLSS depth and projection audit

Read-only native evidence for an optional adapter. This is not a headset result
and does not establish DLSS quality, moving-object motion vectors, or live GPU
resource lifetime. Failure of these receipts must leave CE's existing color
pair and camera ownership intact.

Pinned retail `halo1.dll` SHA-256 is
`0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C`.
Official CE kit `halo_tag_test.exe` SHA-256 is
`FC9E2B6193C6F6D9FF988278B0D39A983747F3FDBDECA7CAFADD28F1F0C53E73`.
All native addresses below are RVAs; offsets are hexadecimal.

## Classic same-eye projection

The established official-kit frustum builder `427C90` and matched retail
`B8F690` write the projection-valid byte at frustum `+140` and the 16-float
projection at `+144`. The same builders produce the world/view matrices,
planes and viewport-dependent scales; see `HALOCE-RENDER-EVIDENCE.md` E-CE-1.
The conventional, non-oblique projection uses the raster camera near/far at
`+3C/+40`. In row-vector notation the ordinary nonzero matrix terms are:

```
m[0]  = horizontal scale
m[5]  = vertical scale
m[8], m[9] = frustum asymmetry
m[10] = near / (far - near)
m[11] = -1
m[14] = far * near / (far - near)
```

The near plane maps to depth one and the far plane to zero. Near zero selects
the native oblique-plane path, which can populate `m[2]` and `m[6]`; an adapter
which supports only ordinary perspective must reject that path. Read the
actual supplied matrix, validate its finite values/shape and preserve native
depth convention. A guessed near plane or the fog-adjusted render-camera far
plane is not an equivalent receipt.

`ClassicViewBody` already admits only the exact `BBCCAD -> BBCF30` primary
class-one view, verifying both tracked cameras, window addresses, generation,
renderer epoch, reference revision and native tick. Its `rasterFrustum`
argument is the relevant same-eye projection. Freeze it while this exact
receipt is live. The render frustum is a distinct culling input.

Inside `BBCF30`, `BBD172 -> AE06FC` copies the supplied raster camera/frustum
to globals `29E0588/29E05DC`. Later native first-person code temporarily
changes lens and depth range; these globals are therefore not a stable
substitute for the admitted entry matrix. The existing isolated executable
test `tools/re/test_ce_classic_fp_projection_native.py` verifies native
save/change/restore and upload behavior. Weapon pixels may still use a
different native depth range from world pixels; this audit does not claim a
uniform world projection for every depth pixel.

## Classic depth owner and capture timing

`AE06FC` calls `B51670` for view kinds one/two. `B51670` routes through
`AEB00`. When native host-kind flag `1B7AA84` is zero, kind one selects color
wrapper `2E3B918`, depth wrapper `*(2E3BDD8)+318`, and descriptor flags zero
through `1DBE50`. The existing native binder proof in
`HALOCE-HUD-TARGET-EVIDENCE-2026-09-15.md` establishes the final selected
surface, writable DSV, descriptor and actual backend cache.

This is conditional routing. When the host-kind flag is nonzero, kind one
can restore the native target stack instead. A hardcoded global depth pointer
alone does not prove that it rendered the primary eye. Require the actual
bound descriptor/cache and selected resource to match the expected root.
`HaloCEHudTarget_Read` already performs bounded read-only verification of
these identities. Its depth slot is index four. Resource descriptor admission
must also reject unsupported shape, MSAA, stale device/epoch and aliases.

The end of `BBCF30` follows postprocess `B0D40` and HUD dispatcher `B31D88`.
Consequently an arbitrary end-of-view DSV is not a valid depth receipt.
The already hooked gameplay HUD at `B32106 -> B12B08`
(`haloce_hud_layout.cpp` MainHook) offers an optional capture boundary before
HUD work, within the exact Classic primary scope. Postprocess has already
run, so if the bound owner does not match, refuse that optional depth capture.
No additional native binding or depth copy was implemented by this audit.

## Anniversary CPU constant receipt

The existing `HALOCE-STEREO-AUDIT-2026-09-15.md` identifies renderer vtable
`1815378`, virtual `+48 -> 2351E0` and virtual `+110 -> 235860`. The latter
reads backend `+D8` to the constant-block owner, then block `+8` to CPU shader
data. It writes the transposed camera matrix at shader `+270` and origin at
`+240`. Existing `verify_ce_native_camera_math.py` executes these actual
native routines in isolation and checks their relation to the staged camera.
The renderer's alternate projection-selection branch remains significant;
read and validate the actual consumed constants rather than rebuilding a
projection from configuration alone.

`tools/re/verify_ce_dlss_native_receipts.py` passes the pinned retail SHA and
checks six unique code patterns, the two vtable targets and the Classic setup
call edge. It prints the exact patterns for optional cold contract integration.
These are witnesses for read-only field access, not permission to call native
functions, reuse a later worker's constants, or retain borrowed pointers after
their same-eye transaction.

Read-only decompilations are preserved under
`out/coop-stability-20260923/ce-classic-depth-{owner,init}-retail.txt`; earlier
official-kit output is `out/ce-kit-camera-functions.txt`. No game was launched,
and no installed module or game file was modified.
