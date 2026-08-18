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
the exact released V6 donor DLL and an explicitly verified base profile, copies
the five sections, redirects the eleven V6 wrapper call sites, and remaps eight
internal calls whose linker RVAs moved. The remapped functions were verified
instruction-for-instruction after normalizing build-relative addresses.

Two base profiles are retained:

- The d184 headset-tested base is selected only by its exact complete SHA-256.
  Its merged output must also reproduce the known restored DLL's exact SHA-256.
- The cumulative `60c9198` V6/two-hand code layout is selected only when both
  its exact raw `.text` SHA-256 and every stock PE section's geometry match.
  The complete base hash is supplied on the command line and recorded because
  the embedded 40-character source commit legitimately changes that hash.

The cumulative layout audit found nine unchanged base call RVAs, two base calls
that moved by `0x50`, and four distinct internal destinations that moved. The
tool refuses a different code hash or PE geometry; it never applies the old
d184 table as a fallback. After merging, it verifies all 11 base redirects,
all 8 internal redirects, the complete custom-section geometry, and that only
the allowed PE headers/call displacements differ from the base and donor.

Example:

```powershell
python tools/merge_v6_postbuild_layer.py `
  --v6 path/to/released-v6/halo3xr.dll `
  --two-hand path/to/new-build/HaloMCCVR.dll `
  --expected-base-sha256 <sha256-of-new-build> `
  --output path/to/restored/HaloMCCVR.dll
```

This is a recovery bridge, not a replacement for source. The custom sections'
maintainable source should be recovered or reconstructed before changing their
behavior. Until then, do not rebuild a release from `7da8f7c` alone and call it
V6 parity.
