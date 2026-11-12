# Mythic early-camera body correction

User tested source 552124dd and confirms the reload menu is present; affected
ODST guns still deform, as expected for that diagnostic baseline. Log SHA-256:
ce85fe2fb57c4711b942feac2396dc39db122d5b8145c71c0d534ef0da2a3c98.

The live probe confirms complete Mythic body checksum 14020C1F, remap
[0,2,3,...,37], and matching gun maps [38,...,count-1] for repeater (45),
Automag (43) and needle rifle (53). The cache also proves this same family for
Magnum and DMR. Normal body maps remain [0,...,36]. A partial secondary body
contains -1 entries and is explicitly not part of this correction.

Candidate keeps the pre-fix solver and disabled learned-palette experiment.
At the native body palette, only the exact evidenced checksum AND full remap
admit the correction: wrists 6/7, camera 1, mapped finger masks, appended gun
sources from 38. The original untouched interpolation is used to rebuild marker
transforms; corrected context remains available to the following gun palette.
No weapon-name list, new native binding, or change to shared IK/dual reload.
Other palettes retain the baseline behavior; New Covenant is not claimed fixed.
The probe stays available. The prior cache-completion failure is not claimed
solved: this candidate does not use that cache or require its admission.

24,845 checks across 202 map-derived combinations pass with GCC C++20,
warnings-as-errors, ASan and UBSan. Tests compile the actual new runtime helper,
verify exact live roles, finger ownership, full gun carry, camera exclusion,
marker rebuild from original data and unchanged unrelated layouts. Native
engine calls are stubbed; this does not substitute for Windows/headset testing.

Test the five affected Mythic guns and a previously normal gun; move each hand,
turn the head, swap weapons and reload. Supply a fresh log and visual result.
Dual shake remains unchanged. No release baseline promotion or game install.
