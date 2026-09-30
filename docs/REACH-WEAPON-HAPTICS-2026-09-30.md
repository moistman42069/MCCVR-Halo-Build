# Halo: Reach weapon haptics evidence (2026-09-30)

## Status

The native per-user rumble evaluator and envelope format are understood and
matched from HREK to the pinned retail module. A pure, counted-test envelope
helper exists in `src/common/reach_weapon_haptics_logic.h`; the counted tests
also cover identifying a newly written native voice without diverting an
unchanged or ambiguous slot. A runtime draft exists at
`src/dll/reach_weapon_haptics.inl`, but it is intentionally **not included,
installed, or active**. It was paused before its first compile/runtime review
to prioritize avatar work. Do not describe this as working Reach weapon
haptics. Native firing rumble remains unchanged in the current DLL.

## HREK weapon-authored source

The official Reach kit executable is `reach_tag_test.exe`. Its weapon update
`FUN_140DF7640` walks the `weap` trigger block (`+0x3FC`, records `0x8C` bytes)
and calls `FUN_140DF64E0(weaponDatum, triggerIndex)` each update. In the
normal-fire trigger branch, `FUN_140DF64E0` reads the authored firing-effect
datum at trigger record `+0x58` and dispatches it through
`FUN_1403A3880(ownerUnit, -1, firingEffectDatum, ...)` (call at `0xDF68EC`,
return `0xDF68F1`). Other branches dispatch other effects and must not be
treated as the firing branch. HREK tag-schema text says firing effects are
used when the weapon is loaded and fired normally; separate misfire, empty,
and secondary-fire effects exist.

The `drdf` worker `FUN_1403A3B90` handles an `rmbl` effect by calling the
native source enqueue function `FUN_1401ED640(user, rumbleTag, scale,
descriptor)`. That worker call is at `0x3A3E4C`. `FUN_140DEEBE0(weaponDatum)`
resolves the weapon's owning unit (parent datum at `+0x32C`, with the native
fallback through `+0x1B4` when flag `+0x1A9` is set). These facts identify the
appropriate weapon-authored branch; they do not prove every such effect
consumes ammunition, nor should charging/active-trigger feedback be excluded.
General damage, player, and vehicle rumble are separate sources and must stay
native.

HREK source strings identify `player_rumble.cpp` and `rmbl` definitions. The
`rmbl` record has two motor bands. Each band begins with a finite duration
float followed by an opaque 0x14-byte native mapping record; those bytes are
tag-block data, not a float array.

## Retail evaluator match

The pinned `haloreach.dll` SHA-256 is
`738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894`.
HREK `FUN_1401ECDA0` matched retail `FUN_1800DB19C` by BSim with similarity
`0.7088458776962865` and significance `161.4125490717588`. The retail
decompilation independently confirms the native evaluator:

- Eight source voices are walked at `0x34`-byte intervals, with parallel ages
  at row offset `+0x1B0`.
- Each `rmbl` band is evaluated by native curve helpers using elapsed age /
  duration, multiplied by voice scale and source attenuation.
- The attenuation used by the mixer is the float at voice-descriptor offset
  `+0x28`; the rest of that 0x2C-byte descriptor must remain opaque.
- The row is per output user in TLS (`+0x2D8` manager, `0x1D8` row stride).
  The two motor results are packed and sent through the native output helper.

Retail `FUN_1800DACD8(dt)` is the matched per-user update/evaluation loop. It
first checks global/pause/user/device gates, then for each admitted user
advances the eight ages, updates source attenuation, calls `FUN_1800DB19C`, and
emits output through `FUN_180023FBC`. Therefore a private Reach envelope must
advance/sample only when native user-0 evaluation actually occurs; a
wall-clock-only update would incorrectly advance through paused or skipped
evaluation frames. The update helper also iterates users 0 through 3, so any
VR routing must explicitly restrict to local output user 0.

Retail `FUN_180163DA4` is the matching descriptor source-attenuation helper
(HREK `FUN_1403A51D0`, BSim similarity `0.4371065127935633`, significance
`82.42335573889716`). Retail callers `FUN_1800DACD8` and `FUN_1800D9EE8` support
the update/clear lifecycle distinction; do not claim the latter is the
weapon enqueue path.

The queue writer is retail `FUN_1800DAEFC`. Its only analyzed caller is
`FUN_1801628D0` at `0x162BB9`. Retail `1628D0` matches the HREK `drdf` worker
semantically despite a weak whole-function BSim result: both walk `0x84`-byte
effect records, use record `+0x2A` as the rumble datum, build the native source
descriptor through the matched descriptor helper, and enqueue the rumble tag,
scale, and descriptor. In HREK the matching calls are `FUN_1403A1F70` then
`FUN_1401ED640` at `0x3A3E3A`/`0x3A3E4C`; retail calls matched `FUN_180163D24`
then `FUN_1800DAEFC` at `0x162BA8`/`0x162BB9`. Both dispatcher parents select
the `drdf` group and enumerate output users.

HREK's weapon-trigger updater calls that dispatcher with its resolved owning
unit and the `firing_effect` tag datum from trigger record `+0x58`. Retail
worker arguments preserve source unit (`param1`), output user (`param2`), and
effect datum (`param3`). A conservative runtime can admit only when the source
unit is the current local user-0 unit, output user is 0, and `param3` uniquely
matches a firing-effect datum in exactly one currently equipped weapon's
`weap` tag. If both hands share an effect datum, or ownership/effect lookup is
uncertain, keep the native path untouched.

The `weap` tag has a second, independent firing-effect block inside each
barrel. The official HREK export
`out/reload-kits/xml/HREK/01d6c56cbb32b213.weapon.xml` shows this beneath the
barrel record (the barrel block starts at the export's line 355); it contains
shot-count bounds and separate firing, misfire, empty, and optional secondary
effect references. This is distinct from the trigger block's `+0x58` firing
effect. HREK tag registration `FUN_14002FBE0` registers the barrel-effect
record schemas `barrel_firing_effect_fields_v1` and `_v2` with record sizes
`0x84` and `0xC4` respectively. Therefore a runtime that only matches the
trigger's `+0x58` datum is incomplete for ordinary per-barrel firing.

The broad HREK barrel helper `FUN_140DE4290` is not the admitted barrel-fire
handler: its direct calls do not include the `drdf` dispatcher, and callers
include trigger/reset walkers. The admitted successful `weapon-fire` branch
in `FUN_140CD5F90` bypasses the `cannot fire` diagnostic and calls
`FUN_140DE86B0(weaponDatum, barrelIndex, 1)` at `0xCD761F`. DE86B0 calls
`FUN_140DE93D0`, which selects the shot-count-appropriate record from the
barrel's `+0x178` firing-effects block (records `0xC4` bytes) and returns its
selected effect datum. It then calls `FUN_140DE9220`; that wrapper resolves the
weapon's owner unit and calls `FUN_140D70740(ownerUnit, selectedEffect,
scale)`. D70740 checks the effect group and, for `drdf` (`0x64726466`), builds
the effect-source descriptor and dispatches through `FUN_1403A3880` at
`0xD708FE`. This establishes the HREK source-to-generic-worker chain for
barrel-authored rumble as well as the separate trigger firing-effect path.
The pinned-retail barrel path is now structurally matched end-to-end. Retail
`FUN_1804C0554` performs the barrel-success check through
`FUN_1804C0B9C(weapon, barrel, shotCount, flag)`, selects the effect through
`FUN_1804C0F70`, and only on success calls `FUN_1804C1648(weapon, barrel,
selectedEffects)` at `0x4C0A03`. Retail 4C1648 calls
`FUN_180493720(ownerUnit, selectedEffect, scale)` at `0x4C169E` (and the
secondary selected effect at `0x4C1797`). 493720 checks the effect tag group,
builds the descriptor, and calls `FUN_180162654(owner, -1, effect,
descriptor)` at `0x493908`; 162654 calls the matched retail dispatcher
1626B8. This mirrors the HREK admission → selector → owner/effect wrapper →
dispatcher flow above. The generic retail worker/enqueue chain continues
1626B8 → 1628D0 → DAEFC.

The HREK/retail functions are structurally matched by admission conditions,
argument roles, selected barrel-effect dataflow, DRDF group check and nested
dispatcher call shape. Whole-function BSim returned only low/noise results for
DE86B0, DE93D0, DE9220 and D70740, so those scores are not presented as proof.
Runtime installation still requires unique pinned-module pattern and call-edge
checks for the chosen hook(s). Evidence dumps are
`out/reach-avatar-review/Reach-admitted-barrel-handler.txt`,
`Reach-admitted-barrel-effects.txt`, `Reach-barrel-effect-spawner.txt`, and
`Reach-fire-admission-calls.txt`, `Reach-retail-admitted-barrel-handler.txt`,
and `Reach-retail-weapon-effect-wrapper.txt`; the weapon XML export records
the schema.

## Runtime draft checkpoint (not build-integrated)

The draft is scoped to the admitted barrel-fire route only: 4C0554 admission,
4C1648 selected barrel effects, 493720 effect wrapper, and DAEFC native enqueue.
Its intended diversion transaction always calls the native queue writer once,
then matches the newly written user-0 record and retires only that record after
private staging succeeds. Native trigger/charge effects, general damage, player
and vehicle rumble stay native. It must not be enabled until the draft compiles,
the production transaction fixture passes, the 100 ms evaluator freshness and
pause-clear behavior are verified, and root has reviewed teardown and optional
failure isolation.

## Remaining proof before runtime implementation

BSim produced no signature/base for the HREK weapon-trigger updater
`FUN_140DF64E0` or trigger walker `FUN_140DF7640`. The standalone enqueue and
generic effect worker have only low/noise whole-function BSim matches, so no
single-function BSim identity is claimed for those functions. Their retail
addresses are supported by the matching dispatcher, record stride and rumble
field, descriptor-helper match, and exact queue-write dataflow above. Runtime
installation still needs unique pattern and call-edge checks against the
pinned retail module.

The existing muzzle owner helper is unsuitable as-is: it is gated by
`gun_barrel_aim` and `g_reachBarrelBindingsReady`, whose bindings are installed
only with the optional muzzle feature. A Reach haptics runtime needs an
independent, lifecycle-safe reader that confirms the complete local weapon
datum in the FP user-0 primary/secondary slot and remains correct through
vehicle changes, weapon swaps, pause, and generation changes.

The effect/enqueue path is now identified. Before integration, prove the live
tag-data token and native descriptor behavior through the production fixture;
preserve raw opaque descriptor and mapping bytes, preserve stock enqueue on
every failed guard, and isolate/log optional-hook faults. The tested helper
preserves authored duration/curve, descriptor attenuation and scale while
supporting token cancellation and native user-0 evaluator admission.
