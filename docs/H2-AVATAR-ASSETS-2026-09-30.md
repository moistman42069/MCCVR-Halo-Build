# Halo 2 own-world avatar evidence

The target is the same tracked full-player experience requested for all six
games. This is asset groundwork, not a working native avatar or headset result.
The existing single-weapon first-person arm solve remains separate.

Official H2EK world render models for Master Chief, Dervish and Elite were
extracted from `D:/SteamLibrary/steamapps/common/H2EK/H2EK.7z` into ignored
`out/h2-avatar-review/tags`. The official `tool.exe export-tag-to-xml` ran with
that isolated working directory. No installed game/kit asset was modified.
Raw XML, export logs and compact summaries are retained there.
`tools/re/summarize_h2_avatar.py out/h2-avatar-review` reproduces the summary,
records source/XML SHA-256 and validates each own-kit parent and finger chain.

Source tag SHA-256:

- Chief: `611147D817D9A3C3F5B60E725AA390FB824C74A2604A3E8EB6C35F2F39AA8A56`.
- Dervish: `BF38494E26275851E9DBA2E7F94D6559CA173C532834FEEFC7598A0D312A6FD3`.
- Elite: `DFBC394BB5260094AB91F53D53190E8F490D9ECE19FAF91A640E2468D67FEC35`.

| World model | Nodes | Anatomical left arm | Anatomical right arm | Authored finger chains per hand | Regions |
| --- | --- | --- | --- | --- | --- |
| Master Chief | 33 | 14 → 17 → 19 | 16 → 18 → 20 | Index, ring, thumb; two joints each | Arms, body, helmet, legs |
| Dervish | 55 | 15 → 18 → 21 | 17 → 20 → 38 | Index, pinky, ring, thumb; two joints each | Body, brand, head |
| Elite | 55 | 15 → 18 → 21 | 17 → 20 → 38 | Index, pinky, ring, thumb; two joints each | Body, head, helmet, hg, lights |

These are independently read H2 indices. Their resemblance to other engines
does not establish a shared runtime layout. Chief's 33-node world rig differs
from both its first-person rig and Halo 3's 51-node world rig. Its world asset
does not expose five independently jointed fingers; use its actual authored
groups or an explicitly designed hybrid presentation, never invent joints.

Chief has a separate authored legs region. Dervish and Elite have no separate
lower-body region; hiding their pelvis subtree would also damage upper-body
presentation. Region-to-render submission and mixed-mesh masking still need
native proof before implementing the lower-body option.

World head and palm markers are present. As with the existing H2 FP alignment,
the world wrist frame cannot be directly replaced by an FP wrist frame without
the semantic palm conversion. H2EK's XML spells parent links as `block_index`
elements with the semantic label in `type`; ignoring these loses the hierarchy.

The exported inverse-transform fields are not valid orthonormal matrices
(Chief root forward `(1,0,1)`, up `(1,1,0)`, scale approximately `-0.000001`).
Their layout/serialization is unproven, so they must not be used as runtime
inverse-bind constants. The exported node-list checksum is also zero; it is
not sufficient identity proof. Native palette/visibility consumers, complete
model identity, renderer-specific Classic/Anniversary presentation and safe
private output remain to implement.

## Controller finger pose and world-avatar boundary

The shipped H2 first-person animation graphs have separately verified left
and right wrists and authored digit chains. The final packet path can therefore
pose the free support hand after the controller/weapon transforms have been
applied. The implementation derives the Chief five-chain or Elite four-chain
shape from the live graph's parent table, excludes the authored secondary
weapon branch, and refuses malformed or unfamiliar chains. Grip adds a
bounded flex to the verified chains as a group. The per-frame packet builder
provides a fresh native palette, so the pose is rebuilt from that source rather
than accumulated over a prior VR result. This is not a bind-pose reset or a
claim that zero grip forces an anatomically open hand. Trigger-specific pointing is not implemented:
the official evidence does not prove a shared semantic index-finger identity
across both shipped rigs. The active support-grip relationship and dual-wield
packet path keep their native authored finger poses. The feature is gated by
the experimental body/gesture setting and uses the exact prepared-frame input
snapshot; stale or invalid tracking leaves the stock pose.

This first-person hand pose is not the requested full-world avatar IK. The
retained H2 evidence establishes player-unit ownership for gameplay helpers
and an owned first-person render-packet transaction, but it does not establish
a local-player-only world-model palette producer and draw-consumer pair. The
world Chief/Elite skeletons also differ from the first-person rigs and expose
different digit counts. Until a title-specific world palette row, local-unit
admission, renderer consumer, and private transactional output are proven, no
world body or lower-body visibility mutation is installed. The whole-player
avatar request remains unfinished for Halo 2.
