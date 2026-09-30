# Halo 3 and ODST native world melee

This records the H3/ODST extension to physical contact melee. The intended
reference is each title's ordinary native melee: preserve its surface material,
damage authority and effects, while replacing only the native center sample
with the tracked physical sweep. World surfaces have no object datum and never
enter a unit/object accessor.

## Evidence

H3EK collision-vector function `652A10` and ODST kit `6A3B30` emit native type
1 for structure/BSP contacts. H3EK `651790` -> `654590` and ODST
`6A2850` -> `6A56B0` emit native type 3 for world-model contacts. Both forms
share the result layout: nearest-contact fraction at `+04`, point at `+08`,
four surface identity words at `+18..27`, material short at `+28`, normal at
`+2C`, section identity at `+3C`, and four structure/object identity words at
`+4C..58`. The type-3 producers set target `+40` to `-1`; they do not invent an
object datum. The adapters retain all nine identity words and selected
material, and require the producer's surface/object index and material to be
present.

The normal melee builders are retained as the effect path. H3EK `A58710` routes
collision types 1/3 through its world-surface selection branch; the H3EK
consumer `A59860` calls `38B890` when the selected target is `-1` and selected
material is not `-1`. Retail H3's matched consumer calls `11E27C` with the same
native surface tuple. ODST kit `AD3E60` likewise selects world results, and
`AD4FC0` calls `3B1D00` when target is `-1` and material is available. The
native builders therefore retain their title-owned material, breakable,
authority and effect decisions.

Read-only Ghidra output is preserved in:

- `out/h3-world-contact-collision-vector.txt`
- `out/contact-h3-impact-layout-console.txt`
- `out/contact-h3-update-and-damage-match-console.txt`
- `out/odst-world-contact-kit-review-console.txt`
- `out/odst-world-contact-selected-writer.txt`
- `out/h3-world-contact-type3-producer.txt`
- `out/odst-world-contact-type3-producer2.txt`

## Implementation

The shared `Hit` contract admits a `WorldSurface` only when the adapter marks
its title-specific metadata valid; the object handle stays `UINT32_MAX`. H3
and ODST adapters currently admit verified collision types 1 and 3, retain the native
surface identity and re-run the exact physical center sweep inside the native
melee builder. They reject it unless type, identity tuple, material, position,
normal and fraction still match the queried contact. Other 24 grid rays remain
suppressed, so head-directed aim assist cannot retarget the hit. Type 4 object
contacts continue through their existing full-handle validation and scoped
damage path.

The generic tests cover a no-handle world hit, refusal of unverified metadata,
title-specific native type values, and rejection of a changed surface identity.
Runtime and headset verification are still required. Other collision types
remain stock unless separately validated by the title adapter.
