# Mythic alignment after accepted hand/deformation correction

User tested e6edf4ad and confirms hands/digits/gun parts no longer stretch, but
held guns sit sideways and behind the right hand; both hands appear pitched
down. Automag upper-component loss is suspected, not confirmed. Preserve the
accepted body mapping and dual shake reload. Photos show Automag misalignment
and normal AR placement; dark shading does not prove a missing component.

The supplied log identifies e6edf4ad, Steam/OpenXR 2.18.4. It confirms native
Mythic maps; no DLL hash supplied. Log SHA-256:
4e61f1c5015c852ee886381825ad1c9d96bd88ede98cd8de6494b433155ae721.

## Reproduced implementation problems

Interpolation still measured wrist 6, then the body-palette correction measured
wrist 7. Both samples entered the same 2% barrel filter. This mixes two different
bones and rotates the weapon-hand target using an invalid blended axis. The
correction also lived only in the current interpolation context. If native code
interpolates again before a gun palette, it recreates wrist 6 / last-node camera,
losing both gun seating and the final gun component's carry/visibility mask.
The fixture now reproduces that lifecycle. The existing log does not establish
every native callback ordering, nor prove the suspected missing slide visually.

## Candidate

Retain one positively observed Mythic body layout per slot, keyed by runtime
generation, native view/id/slot/count arguments, and tracking reference epoch.
This is an exact reuse hint for the observed family, not the disabled union cache.
Apply its roles before barrel measurement and marker transformation, including
when interpolation runs again between body and gun. A different identity or
epoch discards the hint and returns to baseline behavior. Unsupported layouts
are not gated out of controller reconstruction.

First live recognition clears the wrong-wrist barrel filter before measuring
the corrected wrist. Once interpolation used that layout, the body palette
neither measures the barrel nor transforms markers twice. Native gun parts
38..count-1 retain the corrected right-hand mask; camera 1 stays excluded. Cold
self-check reports the selected layout rather than the pre-correction guess.
No guessed per-weapon pose offsets or visibility changes to shared code.

25,006 checks across 202 map-derived combinations pass GCC C++20,
warnings-as-errors, ASan/UBSan. Production-helper tests include first-recognition
filter reset, reinterpolation between palettes, no double measurement/transform,
last-component carry and identity/generation/reference-epoch invalidation.
Engine/matrix boundaries in this fixture are stubs; Windows DLL and visual
headset validation remain pending. No claim of confirmed gun seating or slide
visibility. Existing hands/fingers, Reach/H3 and dual reload code are preserved.

Test Automag and another affected weapon against AR, moving each hand and
switching repeatedly between normal and affected weapons. Check the whole gun
(including its top), hand tilt, reload animation, and unchanged dual shake.
Return the new log and photos if placement is still wrong. No installation,
game launch, release, PR or accepted release-pointer change.
