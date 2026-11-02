# ODST mapped-palette regression and Mythic Automag — October 23, 2026

## User result and source identity

The previous ODST mapped candidate failed in headset: New Covenant and Mythic
Overhaul weapons, including previously working weapons, remain in the native
head-relative pose. User later reconfirmed the same behavior in New Covenant.
This supersedes the previous candidate's offline success; it is not accepted.
A separate rollback commit disables that candidate before this replacement.
Dormant prior count-based code is retained, per the repository failure policy.

Supplied log SHA-256:
`7c707815e31532dcd2747f7297fa0643374a5a86ef45b50b9b70e146e76b1218`.
It identifies source `f7a0778d7172ed79e6c80d9e01fd123630557311`, Steam,
SteamVR/OpenXR 2.18.4, headset `SteamVR/OpenXR : holographic`, 90 Hz. This is
before the dual-shake implementation, so that feature is not implicated by this
log. The installed DLL hash was not supplied.

Every observed ODST FP layout is rejected: counts 41, 42, 43 and 53 show
`layoutAccepted=NO`, wrists/camera -1, bodyNodes 0. This establishes the immediate
reason for head locking: controller reconstruction was never admitted. The old
log cannot distinguish an unseen body palette, an unknown body, stale tracking,
or a native-reader failure; it does not prove one exact callback order.

## Read-only map evidence

Supplied Mythic Overhaul cache `sc100.map`, SHA-256:
`4de591bea89ac6d427c15fd2bf79f16bdc81e38259e2a63a54a6fe70d66da5c0`.
The hash-pinned inspector now supports both supplied ODST maps and records
custom body paths, not only stock `.../fp/fp` names. Null weapon FP references
are skipped by datum; non-null group/salt/pointer validation remains enforced.
`ODST-MYTHIC-FP-MAP-EVIDENCE.json` records the actual nodes and references.

The Automag weapon references
`objects/weapons/pistol/automag/automag_mythic/automag_mythic`. Its combined graph
has 43 nodes, with this order:

| Role | Mythic Automag source | Old fixed assumption |
|---|---:|---:|
| base | 0 | 0 |
| camera_control | 1 | 42 |
| left / right shoulder | 2 / 3 | 1 / 2 |
| left / right elbow | 4 / 5 | 3 / 4 |
| left / right wrist | 6 / 7 | 5 / 6 |
| body fingers | 8–37 | 7–36 |
| gun / lever / magazine / safety / slide | 38–42 | 37–41 as held, 42 camera |

The old count gate accepts 43, but then treats a forearm as the left wrist and
the left wrist as the right wrist, assigns fingers to the wrong chains, and
excludes the slide as if it were the camera. This directly explains why that
asset can deform under the old fixed-index solver despite correct shot aim.
The map tag is the Automag candidate corresponding to the user's in-game
“Automag (Kinetic)” report; this inspector does not resolve localized UI labels.

The Mythic custom human body is
`objects/characters/odst_recon/fp/fp_mythic/fp_mythic`, 37 nodes, checksum
`14020C1F`. Its arm/finger topology matches the evidenced ODST human body, but
its checksum is absent from the failed candidate's identity list. Thus that
candidate rejects the custom body even for ordinary camera-last weapon graphs.
The map also contains the stock 37-node ONI body `18080211`.

## Replacement behavior

- Admit the newly evidenced Mythic body identity, retaining the four prior
  evidenced identities. No weapon-name allowlist, new native address or hook.
- Stop presuming that the final source is camera_control. Observe disjoint body
  and gun native maps; only when exactly one source remains, identify that source
  as the camera in this evidenced graph family. Body-only evidence is incomplete.
  Ambiguous unions, overlaps, duplicate sources and out-of-bounds reads stay stock.
- Preserve the interpolation receipt for all matching palette calls. Previously
  `candidate.valid=false` consumed it on the first call. If the gun arrived first,
  the later body could never teach the cache. The former test fixture recaptured
  before every palette and missed this production lifetime failure.
- Learn body and gun in either order and freeze complete evidence only for the
  following stereo pair. Revalidate tag, mapping, generation and pair identity;
  restore speculative marker transforms on rejection.
- Add cold-worker diagnostics for palette tag/model count, rejection stage and
  cumulative body/gun/accepted observations. Hot callbacks only update atomics.
  The next runtime log can distinguish a remaining callback/reader failure from
  unsupported rig/union evidence. Do not claim the live callback-order cause is
  conclusively established by the old log alone.

These changes remain ODST-specific. Reach, H3 and dual-shake implementation are
preserved. The old count-based runtime branch is retained but inactive again in
this replacement candidate.

## Validation and limits

24,186 checks across 202 map-derived asset/body combinations passed with GCC
C++20, `-Wall -Wextra -Werror`, ASan and UBSan. Expectations use graph node names
and parent links, independently of the production remap resolver. They include
Mythic's actual early-camera Automag, prior New Covenant human/Elite rigs,
arbitrary source permutations, both palette orders from ONE interpolation,
next-pair admission, stale generation/epoch, changed maps, unreadable pointers,
rollback and rejection of ambiguous camera inference. The generator excludes
graphs without exactly one camera or with extra unmapped nodes: those are not
claimed supported by this candidate.

Retained production-fixture regressions: H3 828 and Reach 2,793 checks passed
with the same sanitizers. Full Windows DLL compilation, cumulative CTest and
headset verification are still outstanding. No accepted-pointer change, binary
package, installation, game launch or release. Source delivery remains the
user-authorized commit/push workflow; compile the replacement source, not the
older pinned workflow artifact.

Reproduce ODST checks:

```sh
python3 tools/re/inspect_odst_modded_fp_map.py /path/to/sc100.map /tmp/mythic-assets.json
python3 tools/generate_odst_modded_fp_fixture.py --source-root . --output out/odst-modded-weapons/odst_modded_fp_assets.inl
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iout/odst-modded-weapons tests/odst_modded_fp_tests.cpp -o out/odst-modded-weapons/odst_modded_fp_tests
out/odst-modded-weapons/odst_modded_fp_tests
```

Headset priority: Mythic human ODST, first Automag then a previously normal
weapon; move each hand, turn the head, reload, swap repeatedly and test normal
versus floating hands. Then regress New Covenant. Supply the log even if the
pose remains locked: `ODST mapped FP` now reports which admission stage fails.
No all-mod, arbitrary-body, co-op or Store acceptance is asserted.
