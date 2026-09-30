# Physical melee against native world surfaces

User scope: both hands and held weapons should reach supported world impacts,
glass and damageable objects. Ordinary native material destructibility remains
authoritative; a collision does not imply that an indestructible wall breaks.

The existing shared motion filter and title adapters admitted objects only.
The shared `WorldSurfaceIdentity` now allows an explicit no-object world hit,
with four opaque identity words owned and validated by each native adapter.
No world identity is passed to a native object accessor. Each hand retains its
existing speed threshold, first-obstruction query and retraction latch.

## Halo 2 official breakable-surface path

H2EK `units.cpp` 0x480D40 (`damage_target`) accepts collision result types 1
and 3 as world geometry. For the selected collision it reads material at
result +0x24, structure index at +0x3C, feature at +0x50, breakable flag bit 3
at +0x58, and breakable-surface index at +0x59. It constructs the native
target with object `-1`; when a breakable index exists, the ordinary melee
consumer 0x481850 calls H2EK `physics/breakable_surfaces.cpp` 0x1489F0. That
function resolves the selected structure/instance, checks the feature's
material and authored breakable definition, and performs the stock damage and
effects. The adapter preserves this native path rather than writing damage or
surface state itself.

The optional H2 contact query admits only results carrying the official
breakable flag and a valid material. During native melee it replaces only the
centre ray with a fresh physical-sweep result, then requires the full surface
identity, contact point, normal, and fraction to match before allowing the
native builder to continue. Object hits retain their existing target check.
Non-breakable walls remain collision/haptic contacts without being reported as
glass damage. Kit semantics are mapped to the pinned retail collision-vector
consumer; headset confirmation that representative H2 Classic and Anniversary
glass surfaces break at the physical impact point remains outstanding.

Preserved H2EK decompilation: `out/contact-h2-native-builder-kit-console.txt`,
`out/contact-h2-native-damage-kit-console-0x481850.c`, and
`out/contact-h2-role-proof-console.txt`. Unit coverage is in
`tests/left_hand_h2_tests.cpp` and verifies the breakable-tail decoder and
rejects non-breakable, object, and truncated results.

## CE native glass branch

Halo 3 is the experience reference: a physical strike should use the same native
material damage/effect choices as ordinary melee at that point. CE achieves it
through its own native surface branch; no foreign offsets are used.

Official HCEEK `8D4A90` (RVA `4D4A90`) selects type 2 world contacts and writes
material plus a breakable index/feature when collision flags contain bit 3.
Its retail counterpart `B0BFAC` consumes result material +34, flags +4C,
breakable byte +4D and feature word +44. The existing player melee helper
`8CF1B0` / retail `B0C388` calls this selector for a missing object target.
Its world branch invokes `811D90` / retail `A9C174`: short breakable index,
private native damage event, surface feature. HCEEK assertions explicitly name
`physics/breakable_surfaces.c`. Damage uses the authored weapon/unit damage
definition, material modifier and native surface strength before shattering.

Retail `A9C1B8` reads the active BSP short at `1B860A4`; its surface strength
lookup selects BSP*256 + breakable index. The receipt captures this native BSP
and revalidates it before selector output and breakable submission. It does not
invent a scene epoch or assume that a surface index is globally unique.

The optional CE bridge hooks the selector and breakable consumer. Only a scoped
call from player melee return sites `B0C467` / `B0C66E` is redirected. Physical
collision is queried again before dispatch and must retain the material,
breakable index, feature, flags and BSP. The selector fills the native helper's
private outputs, with object `-1`; it never falls back to a head-directed fan.
The breakable wrapper rechecks ownership and identity and changes only private
event point +20 and direction +38. Native material, damage, strength and other
calls are preserved. Missing optional proof/hooks leave existing melee and
world-contact presentation active. Cleanup now includes all ten callback ranges.

Solid world surfaces can enter the native material-response path without a
breakable call. This is not proof of precisely placed wall impact VFX: CE's
ordinary response helper still derives its visual origin from the unit/weapon.
Non-biped CE object damage also remains under investigation: official and retail
player melee helpers themselves put target damage inside a biped-only branch.
Widening the adapter's object mask alone cannot complete that request.

Preserved evidence: `out/ce-contact-kit-unit-melee.txt`,
`out/ce-contact-kit-melee.txt`, `out/ce-contact-retail-rayfan.txt`,
`out/ce-contact-retail-melee-disasm.txt`,
`out/ce-world-melee-kit-20260930.txt`,
`out/ce-world-melee-retail-20260930.txt`.
Pinned verifier: `tools/verify-ce-world-melee.py`. Production fixture covers
both hands, physical point/direction, same-swing latch, changed surface/BSP and
solid-wall/no-breakable behavior. Validation is ongoing; no runtime acceptance.
