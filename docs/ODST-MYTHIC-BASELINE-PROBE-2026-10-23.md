# ODST Mythic: restore the pre-fix solver and collect native maps

User confirms dual wield shake reload now works in all requested games. Preserve
444f7a15 behavior; this is scoped feature acceptance, not a release promotion.
User rejects the latest ODST attempt in both Mythic and New Covenant and asks to
restart ODST from `5fdf982e979cc4dab693bbaccfb941d2bc199307`, retaining dual reload.
Focus is Mythic; other mods are deferred. No additional mod/map is needed yet.

## Supplied evidence

All three logs identify Steam / SteamVR OpenXR 2.18.4 / holographic headset.
The installed DLL hashes are not supplied.

| Log | Source | SHA-256 |
|---|---|---|
| HaloMCCVRMYTHIC.log | 62ef856f | d034f1677298e7f7adefcedc6d53678c8c1c29f9d4e246289dd8a440be7d1429 |
| HaloMCCVRNEWCOVENANT.log | 62ef856f | 2e17f2bc71895823731bb278d569b7197b31f06a2591b9745613bee1b7d95d9c |
| HaloMCCVRMYTHICBEFOREFIX.log | 5fdf982e | d771a5246e8b5c6eb5c354d841a854a5a3fea58c406b3b93f188c2408dc7bd81 |

Both failed-build logs have zero accepted mapped palettes. Mythic repeatedly
reports stage 4 despite bodySeen and gunSeen advancing. Body checksum 14020C1F
is recognized. The previous claim that receipt lifetime and checksum additions
were sufficient is disproven by this headset result. The log does not contain
native remap arrays or cache keys, so it cannot establish why the union/cache
never completes. Do not enable that candidate again on the strength of its
synthetic tests.

Re-ran the read-only inspector against the retained sc100.map. Output exactly
matches ODST-MYTHIC-FP-MAP-EVIDENCE.json; map SHA-256 is
4de591bea89ac6d427c15fd2bf79f16bdc81e38259e2a63a54a6fe70d66da5c0.
The five user-named weapons map to assets with this common organization:

| Asset | Combined nodes | camera | left/right wrists | gun root |
|---|---:|---:|---|---:|
| Needle rifle | 53 | 1 | 6 / 7 | 38 |
| Plasma repeater | 45 | 1 | 6 / 7 | 38 |
| Magnum | 43 | 1 | 6 / 7 | 38 |
| DMR | 42 | 1 | 6 / 7 | 38 |
| Automag | 43 | 1 | 6 / 7 | 38 |

The original solver uses wrists 5/6 and camera=count-1. Early insertion of the
camera therefore shifts every arm/finger role and misclassifies a weapon part
as camera. This explains the original deformation for this asset family. It
does not prove native output maps equal offline name-derived maps; that missing
runtime evidence is the next measurement, not another assumed fix.

## Candidate scope

Separate rollback disables kOdstMappedFpEnabled and restores the original
count-based interpolation/palette path. Dormant attempted mapping code remains.
Reach/Halo 3 weapon fixes and accepted dual shake code are unchanged. Expected
behavior is the user's pre-fix baseline: previously normal guns work again;
the five early-camera weapons are still expected to deform. This is explicitly
a diagnostic baseline, not a deformation fix.

A passive probe records the native model tag/checksum, complete output-to-source
map, interpolation view/id/slot/count, generation, pair serial and reference
epoch. It has its own interpolation receipt, checks the active pair/generation,
and never consumes or changes solver contexts, bones, native maps or palettes.
128 immutable entries bound storage and output. Identical maps deduplicate
without treating each frame ID as a new map. Producers use nonblocking atomics;
only the existing cold worker formats/logs observations. Restart MCC for a fresh
capture if the bounded buffer is exhausted during a long session.

Validation: production probe capture/observe/log compiled in a portable fixture;
ASan/UBSan plus warnings-as-errors pass nonmutation, stale-pair/failed-capture
rejection, deduplication, changed maps, unreadable records, concurrent producers
and full-buffer retention. Retained dual-shake tests pass 1,350 + 125 checks.
No Windows SDK here: full DLL build and headset behavior remain untested.

Next test: compile this source, launch Mythic sc100, equip a previously normal
weapon for several seconds, then Automag and another affected weapon for several
seconds each, and send the new HaloMCCVR.log. Confirm normal weapons regained
tracking. The `ODST baseline map probe` lines will determine the next correction.
No installation, game launch, release, PR, or accepted baseline update.
