> Superseded by a failed headset result: the user reports all weapons head-locked.
> See `ODST-MYTHIC-REGRESSION-2026-10-23.md` for rollback and replacement evidence.

# ODST New Covenant: native body mapping instead of a human-only skeleton

Status: source candidate on `modded-weapons-fix`; Windows compilation and
headset validation pending. The player-facing target is Halo 3's confirmed
tracked hands and correctly carried visible gun. Reach and Halo 3 runtime
behavior are preserved; no accepted release pointer is advanced.

## Evidence

User reports head-relative arms/weapons for Plasma Pistol, Covenant Carbine and
Energy Sword; deformed fingers/arms/gun pieces for Spiker, Plasma Rifle, Brute
Plasma Rifle and Needler. Aim and shots remain correct. The log is from source
`5fdf982e979cc4dab693bbaccfb941d2bc199307`, Steam, SteamVR/OpenXR 2.18.4,
`SteamVR/OpenXR : holographic`, 90 Hz. Log SHA-256:
`07b77f4113181399821d6352d894b0c003138a18c8d01555cb28a8bc56bbbe0a`.
It records rejected 34/37-node graphs and accepted 39/47-node graphs; it does
not identify every weapon selection or the installed DLL hash.

Supplied map: internal name `01_hijacked`, MCC v13. SHA-256:
`9d6cf73d249e0289b0465fd9f7ded7067c90a7352e9725fda15cae38f068783e`.
`tools/re/inspect_odst_modded_fp_map.py` reads it without writes and pins this
hash. `ODST-MODDED-WEAPONS-MAP-EVIDENCE.json` preserves node names, string IDs,
parents, both FP references and render-model checksums. The ODST cache's weapon
FP block was located and reference-validated at +0x40C (NOT H3's +0x3FC), with
32-byte model/graph reference pairs. Packed tag pointers, group/salt identities,
string spans, node count/parent bounds and unique names are checked. These are
asset facts, not new executable bindings.

The second FP reference contains the Elite rig with 31 body nodes. Its model
is `objects/characters/dervish/fp/fp`, checksum `10000E0B`. The same supplied
ODST map independently includes the 37-node recon (`1114091C`), ONI (`18080211`)
and Master Chief (`1B120B14`) body models. These identities are obtained from
this ODST asset, not assumed from Halo 3.

| Reported weapon | Elite combined nodes | Gun sources | camera_control | Old behavior |
| --- | ---: | --- | ---: | --- |
| Plasma Pistol | 35 | 31–33 | 34 | Rejected below 39 |
| Covenant Carbine | 37 | 31–35 | 36 | Rejected below 39 |
| Energy Sword | 34 | 31–32 | 33 | Rejected below 39 |
| Spiker | 39 | 31–37 | 38 | Wrong human finger masks |
| Plasma Rifle / Brute Plasma Rifle | 39 | 31–37 | 38 | Wrong human finger masks |
| Needler | 47 | 31–45 | 46 | Wrong human finger masks |

Both rigs have wrists 5/6 and arm anchors 1–4, but Elite has four digit chains
per hand rather than five. For example, Elite right index starts at 11, where
the old human mask selects a LEFT finger. Gun nodes 31–36 can also be classified
as human fingers. Lowering the minimum count alone cannot repair this defect.
The log supports the count-dependent failure; assigning every log line to a
weapon is not justified. Plasma Pistol's 35-node path is asset evidence rather
than an independently identified selection in this log.

## Candidate

The ODST interpolation hook captures the untouched graph. The native body
palette supplies its own model identity and source remap. The resolver maps
shoulders, elbows, wrists and digit subtrees through that injective remap;
count alone no longer authorizes a skeleton. The current gun palette supplies
an explicit, disjoint set of held outputs, used by existing shared carry/scale
logic and the ODST floating-hands visibility filter. No weapon allowlist.

Learning is frozen until the next stereo pair, keyed by runtime generation,
reference epoch, native interpolation view/id/slot and source count. This
mirrors Reach's deferred-learning principle: both eyes use the same learned
layout. A first observation stays stock for the learning pair. Body palettes
revalidate the exact tag and mapping; changed/unreadable mappings restore this
invocation's untouched marker graph and retire incompatible learning. Gun
selection rejects duplicate, out-of-range, body-overlapping and camera outputs.
The cache, transaction snapshots and remaps are bounded; no new native hook,
TLS offset, scan, allocation, file I/O or log write is added to the hot hooks.
The existing worker logs body node count/checksum and acceptance.

Scope: the four evidenced body identities, reordered injective body maps,
source graphs up to 64 nodes, and the camera-last graph family independently
observed in the official ODST samples and every FP graph in this supplied map.
This is generic across weapons using those rigs, not proof for arbitrary custom
body models, missing body nodes, graphs over 64, or early-camera graph families.
Unknown inputs remain stock. Optional Elite controller-finger posing and palm
alignment are not newly enabled; this fixes anatomical ownership/carry without
inventing markers for those features. Shared solver code is unchanged.

## Verification and handoff

`odst_modded_fp_tests` compiles the actual runtime `.inl` with stubbed native
boundaries and independently generated asset expectations. **4,742 checks**
cover 50 graph/body combinations: reported guns and other available controls,
old failure reproduction, four/five-digit masks, permuted remaps, native gun
outputs, unsupported/invalid maps, both weapon slots, learning across both eyes,
interpolation-ID isolation, same-count body replacement, rollback, generation
and reference-epoch retirement. Engine stubs do not establish native call order
or headset behavior. GCC C++20, warnings-as-errors, ASan/UBSan pass. Existing H3
**828** and Reach **2,793** focused checks also pass with ASan/UBSan.

Reproduce the ODST suite:

```sh
python3 tools/generate_odst_modded_fp_fixture.py --source-root . --output out/odst-modded-weapons/odst_modded_fp_assets.inl
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iout/odst-modded-weapons tests/odst_modded_fp_tests.cpp -o out/odst-modded-weapons/odst_modded_fp_tests
out/odst-modded-weapons/odst_modded_fp_tests
```

The Linux environment lacks Visual Studio/Windows SDK and PowerShell. Full
Release DLL, cumulative CTest, packaging and the Reach PowerShell gate remain
unrun; the user compiles on Windows. CMake registers the new portable fixture
for that build. The existing CI workflow still pins `5fdf982e` and only triggers
when its own YAML changes; it will not compile this source automatically.
No CI, game files, map contents, installation, launch or release is changed.

Headset test: all seven reported weapons, repeated swaps between small/large
Elite graphs, the other ODST mod exhibiting deformation, and stock ODST human
arms. Check independent hands, gun alignment, intact fingers/pieces, both eyes,
reloads and firing. A Reach/H3 regression check is useful before broad release;
current offline regressions are not headset acceptance.
