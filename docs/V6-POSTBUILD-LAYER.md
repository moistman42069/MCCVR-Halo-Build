# Released V6 post-build layer

The released V6 DLL is not reproduced by compiling source commit `7da8f7c`.
After linking, the release DLL received five PE sections:

- `.h4fx`: Halo 4 effect/held-model bridge
- `.h4fd`: bridge state, strings, and copied hook payloads
- `.h4hs`: Halo 4 HUD-scale installer and wrapper
- `.h4hp`: Halo 4 helmet/HUD support
- `.h4pb`: Halo 4 pause, muzzle, HUD height, and curvature support

The first two-hand candidate was compiled from the narrow source change but
did not contain those sections. That explains why its Halo 3 hand behavior was
correct while previously accepted Halo 4 V6 behavior regressed.

`tools/merge_v6_postbuild_layer.py` is a guarded recovery tool. It accepts only
the exact released V6 DLL and exact headset-tested two-hand candidate by
SHA-256, copies the five sections, redirects the eleven V6 wrapper call sites,
and remaps eight internal calls whose linker RVAs moved in the two-hand build.
The remapped functions were verified instruction-for-instruction after
normalizing build-relative addresses.

Example:

```powershell
python tools/merge_v6_postbuild_layer.py `
  --v6 path/to/released-v6/halo3xr.dll `
  --two-hand path/to/two-hand/HaloMCCVR.dll `
  --output path/to/restored/HaloMCCVR.dll
```

This is a recovery bridge, not a replacement for source. The custom sections'
maintainable source should be recovered or reconstructed before changing their
behavior. Until then, do not rebuild a release from `7da8f7c` alone and call it
V6 parity.
