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
the five sections, redirects the eleven V6 wrapper call sites, and remaps every
custom-section call whose linker RVA moved. The remapped functions were
verified instruction-for-instruction after normalizing build-relative
addresses.

Three base profiles are retained:

- The d184 headset-tested base is selected only by its exact complete SHA-256.
  Its merged output must also reproduce the known restored DLL's exact SHA-256.
- The cumulative `60c9198` V6/two-hand code layout is selected only when both
  its exact raw `.text` SHA-256 and every stock PE section's geometry match.
  The complete base hash is supplied on the command line and recorded because
  the embedded 40-character source commit legitimately changes that hash.
- The `950f0ba` pause-retention/solved-arm two-hand layout is separately
  selected by its exact raw `.text` SHA-256 and complete stock-section
  geometry. The older profiles are not changed or used as fallbacks.

The cumulative layout audit found nine unchanged base call RVAs, two base calls
that moved by `0x50`, and four distinct internal destinations that moved. The
tool refuses a different code hash or PE geometry; it never applies the old
d184 table as a fallback. After merging, it verifies all 11 base redirects,
all 8 internal redirects, the complete custom-section geometry, and that only
the allowed PE headers/call displacements differ from the base and donor.

## `950f0ba` relocation evidence

The new profile was audited against the accepted `d145ece` merged DLL, the
exact released V6 donor, and an x64 Release link from Visual Studio 2022/MSVC
19.44.35228. A `/MAP` link supplied the symbol RVAs. Each relocated base site
was selected by comparing the surrounding accepted and new instruction
streams; `dumpbin /DISASM` then proved the instruction was `E8 rel32` and that
its decoded stock target was the listed map symbol. The merge tool independently
repeats the opcode and decoded-target checks before writing any displacement.

| Accepted site | New site | Decoded new stock target | V6 wrapper |
| ---: | ---: | --- | ---: |
| `003D52` | `003C72` | `005750` `ConfigSave` | `29F013` |
| `0057FD` | `00571D` | `005750` `ConfigSave` | `29F013` |
| `00C6D1` | `00C2E1` | `0019D0` `Logf` | `29F000` |
| `013AEA` | `01350A` | `0019D0` `Logf` | `2A0000` |
| `026105` | `025B25` | `005750` `ConfigSave` | `29F013` |
| `026191` | `025BB1` | `005750` `ConfigSave` | `29F013` |
| `02E860` | `02DEB0` | `111860` `ImGui::TextDisabled` | `29E40C` |
| `02EFAC` | `02E5FC` | `005750` `ConfigSave` | `29F026` |
| `02F034` | `02E684` | `005750` `ConfigSave` | `29F013` |
| `048ACE` | `04834E` | `04C770` `Halo4SafeRead`/`SafeReadBytes` | `29C000` |
| `04FC78` | `04F4F8` | `0019D0` `Logf` | `29C440` |

The accepted `02E860` site was specifically matched to the fourth disabled-text
call in its instruction sequence. The new sequence calls `TextDisabled` at
`02DE8C`, `02DE98`, `02DEA4`, and `02DEB0`; the wrapper replaces only the last
call immediately before the same short-jump landmark as the accepted binary.
Likewise, `04F4F8` is the wrapper-bearing log call; the adjacent ordinary
branch call at `04F506` remains unchanged.

The original eight-entry relocation set still decodes to the original
destinations in the third column below. Their replacements come directly from
the same linker map. `patch_rel32` refuses the donor instruction unless both
its `E8` opcode and original decoded target match exactly.

| Donor call | Original target | New map symbol target |
| ---: | ---: | --- |
| `29C004` | `04CE90` | `04C770` `Halo4SafeRead`/`SafeReadBytes` |
| `29C12D` | `037F90` | `037CD0` `ControllerWorldPoseEx` |
| `29E06E` | `0C78F0` | `0C78D0` `EnableHook` |
| `29E08C` | `0C78F0` | `0C78D0` `EnableHook` |
| `29E410` | `111270` | `111860` `ImGui::TextDisabled` |
| `29E423` | `1059D0` | `105590` `ImGui::ButtonBehavior` |
| `29E436` | `111270` | `111860` `ImGui::TextDisabled` |
| `2A0065` | `02A8B0` | `02A6E0` `ValidateStereoImagesOnce` |

Unlike `60c9198`, this layout also moves the previously stable `Logf` from
`001A20` to `0019D0` and `ConfigSave` from `005830` to `005750`. A complete
instruction-boundary disassembly of all five custom sections found 21 direct
calls to the old `Logf` RVA and two direct calls to the old `ConfigSave` RVA.
Those 23 calls are part of this profile in addition to the original eight,
giving 31 verified custom-to-base redirects. This exhaustive pass is required:
leaving the old calls in place jumps into the middle of the newly linked
functions and causes an immediate `FAST_FAIL_STACK_COOKIE_CHECK_FAILURE` during
the first startup wrapper.

The guarded new `.text` hash is
`4eed9bb45fa63fcfbe186a4459c33da84ce844e2dc3e62c2b37056364627b69f`.
All seven stock sections' virtual sizes, virtual addresses, raw sizes, and raw
pointers are part of the profile rather than inferred during the merge.

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
