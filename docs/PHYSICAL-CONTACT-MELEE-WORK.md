# Physical contact melee work in progress, 2026-09-05

This is an investigation record, not an accepted candidate or a claim that
direct contact damage is implemented. The accepted build remains `ad7fbf5`.
Delivery remains build ZIP plus matching source ZIP in chat, without install,
game-folder changes, launch, or PR.

## Current source checkpoint: Reach native integration

`reach_contact_melee_runtime.inl` now connects prepared-frame hand nodes and
authored weapon bounds to HREK's native hit builder and damage consumer. This
supersedes older statements below that no damage hook has been written. It is
not a headset result, an all-title completion claim, or an accepted candidate.

- Separate bounded queues/latches service both physical hands on the owned
  unit's native action-update thread. World collision need not be enabled.
- The HREK builder's 25-ray grid is restricted to one substituted physical
  segment. Its native damage/material/node selection is retained; a changed
  or reparented target is rejected unless it exactly matches the struck biped.
- Current mesh geometry is evaluated through consecutive controller poses,
  excluding reload/finger animation from swing speed. Predicted display time
  supplies elapsed time. Duplicate eyes, stale tracking, reference-space/session
  changes and support-grip transitions cannot become new strikes.
- HREK `119360` matches retail `58EEC`: initialized game globals and the
  playback flag at +0x1DA. Retail helper `588E0` checks initialization, with TLS
  slot +0x48. Playback and uninitialized simulation reject contact damage.
- HREK `D6CD60` matches retail `492D68`, reached from direct apply `491100`.
  Its fifth parameter is the optional impulse direction. A scoped hook supplies
  the physical world-space swing direction only for our exact attacker/target.
  Authoritative submissions are counted only if this native constructor runs.
- Native hook teardown includes both new detours, targets and trampolines in
  Reach's existing callback/quiescence checks. Feature failure leaves VR active.

The first builder signature was ambiguous (two matches). Its extended verified
prologue now has one match. `tools/verify-contact-melee-bindings.py` checks all
five bindings and seven call edges against pinned Reach SHA-256
`738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894`.
Result: `out/contact-reach-native-bindings.json`. Native direction/ABI trace:
`out/contact-reach-direct-apply-match-console.txt`. Playback/host cooldown proof:
`out/contact-melee-reach-playback-console.txt`.

Still open: actual headset damage/effects, unarmed Reach behavior, and the
predicted client's native host-event cooldown/direction (a submitted request
does not prove host acceptance). H2, H3, ODST and H4 still require their native
contact backends. Nothing here makes those rows complete. The complete request
ledger also includes per-weapon alignment and H2/H4 first-person vehicles.

## Requested behavior

- Either physical hand, or the weapon it holds, strikes an NPC at the configured
  speed and damages that actual NPC. Both hands must support alternating punches.
  The existing swing-triggered virtual-button route does not satisfy this task;
  the user explicitly rejected it as a substitute on 2026-09-05.
- Melee and world collision are independent options. Retain the speed slider and
  default 5 m/s. Use elapsed time and tracked motion, without headset-specific
  button assumptions. Tracking loss, recenter, teleport, weapon changes, title
  changes, and duplicate stereo samples must not generate attacks.
- Smooth sustained surface contact while retaining immediate blocking/release.
- Resolve H2 AI behavior and H3/H2A lower-edge visibility reported against the
  accepted build. The user tested H2 without the mod and found no AI issue.
- Investigate the older Reach body/collision/clarity report without treating its
  old source or different runtime as current-build evidence.
- Latest addition: optional left-handed primary weapon placement, and correct
  dual-wield placement, shot origin/direction, collision, and melee per hand.
  Reuse existing per-slot paths; do not conflate physical hands with weapon slots.
  The user asks for focused work to limit usage.
- Further addition: H2 controller-directed vehicle controls matching the other
  titles, and Halo 3's weapon-side zoom window in every supported title. Audit
  existing implementations first. These remain open, not accepted functionality.

## Confirmed local source findings

`Halo2NativeAimUpdateDetour` skipped the original update for non-owned units
while direct VR aim was active. The pending fix calls the original exactly once
unless the owned-unit replacement actually succeeded. This is a code finding;
AI recovery still requires the user's headset test.

The accepted shared legacy collision publication adds weapon bounds only to
the right-hand publication. `LegacyBuildMappedWeaponBounds` explicitly rejects
`context.slot != 0`. Halo 3 already has independent interpolation contexts for
slots 0 and 1, and native marker placement uses `slot == 1` for the left
controller. Those existing paths must be traced through actual shot consumers;
their presence is not proof that dual-wield shooting works. H2 final packet
collision publication likewise currently appends gun geometry to the right.

Pending H3 source changes now keep two weapon observations, keyed by the native
interpolation slot/source and runtime generation. Slot 1 publishes its gun
bounds with the left hand and does not overwrite the primary/right publication.
Secondary ownership of the left publication expires after a 150 ms gap or a
player-identity change; it resets on feature installation. Release build/tests
pass, but dual-wield headset behavior has not been tested. The full remaining
request checklist is `CONTACT-PASS-REQUEST-CHECKLIST.md`.

## H2 dual-packet implementation checkpoint

Read-only capture `out/h2-live-dual-slots.json` from PID 19712 found full weapon
handles `E57102FE` and `E57302AC`, both using graph `FAA61871`. Each slot reports
four weapon nodes, 37 hand nodes and 42 animation nodes. The secondary hand map
omits right wrist 6 and retains left wrist 5. Weapon names were not verified.

H2EK packet builder `306E04` merges the two hand remaps into one packet and then
emits separate primary/secondary gun packets. Evidence is preserved in
`out/contact-dual-h2-kit-packets-console.txt`; retail packet builder and visible
consumer bindings remain the independently verified existing bindings.

Pending code now stages hands plus both guns before changing any packet. The
secondary gun's completed native root drives a rigid left wrist/gun transform;
its own graph/remap selects the left subtree. Anniversary defers the primary
gun until the secondary callback; missing secondary packets flush deferred stock
packets. Classic owns the same three packets and includes the secondary in
per-eye compensation and restoration. Collision publication adds the actual
secondary model's bounds to the left hand and uses its full weapon identity.

The H2 prepared snapshot now also carries mount-calibrated right aim without
support-hand coupling, so an active two-hand latch cannot steer a dual primary
mesh with the other controller. Single-weapon behavior retains its existing aim.

Dual-packet tests cover independent opposite aims, a partial secondary remap,
invalid graph size, overlapping hand ownership, and a malformed final secondary
node leaving all three packets byte-identical. Release build/tests passed after
packet integration and the independent-aim snapshot change. These are
presentation changes, **not completed dual shooting or melee**.
No new native firing or damage hook is installed and no headset result is claimed.

The native firing wrapper is H2EK `49C960`, matched to retail `8E4940` with
arguments `(weapon_handle, int16 barrel, int32 argument, int8 argument)`. Its
helper call at `8E4FC8` still receives the owning unit, which cannot distinguish
the two guns alone. The full wrapper's weapon handle supplies that distinction.
The later barrel-flag bit 3 branch replaces the helper's origin from a marker
and clips it again (`8F9190` / `75A110` calls). Therefore changing only the
earlier helper's origin cannot establish correct origin for every weapon.
Preserved trace: `out/contact-dual-h2-retail-fire-console.txt`, particularly the
native helper call and following origin-replacement branch. This must be covered
by a future firing transaction before claiming dual-wield shot alignment.

## H2 vehicle controller steering checkpoint

H3 behavior being matched: when seated, the tracked controller steers the normal
native look/stick path used by driving and turrets. H2's rejected on-foot hand
turning stays disabled. The two-grip virtual wheel is a separate feature and is
not claimed by this change.

Official H2EK `unit_in_vehicle` descriptor references evaluator `221D60`, which
calls `4DA690`. That native predicate resolves a unit (mask 3) and checks signed
seat index +0x210 against NONE. The pinned retail descriptor at `B22148` names
`unit_in_vehicle` at `B22188`, references evaluator `785650`, and calls the
homologous predicate `93FB20` at `78566C`. Retail's predicate calls the already
verified object accessor `8D7000` at `93FB51` and compares the same seat member
at `93FB5E`. The complete predicate pattern has exactly one mapped-image match.
Proof: `out/contact-h2-vehicle-binding.json`, kit dumps
`out/contact-h2-vehicle-predicate-console.txt` and
`out/contact-h2-vehicle-native-console.txt`.

Pending implementation reads this verified member only inside the existing
native aim update for the mapped local player's exact unit. It publishes the
seated bit and timestamp atomically; input consumes no live object pointers and
rejects samples older than 100 ms. Seated units retain their complete native aim
update. Only a fresh seated sample admits the H2 controller stick loop; the
on-foot branch remains disabled. Binding failure affects vehicle steering only
and logs stock fallback. Teardown clears the publication. Servo history also
resets after an inactive gap or generation change.

Release/core tests and Reach consistency passed before the final servo reset
addition. Vehicle driving, turret use, entering/exiting, and H3 regression still
require headset testing; no headset result or vehicle-type-specific wheel
support is claimed.

## Reach native melee evidence

Official HREK `reach_tag_test.exe` SHA-256:
`CBDD8448A87A433B0DFFC0DE47D06DB7A18B4BF868B96B057135DAA86790ABA8`.
Pinned retail `haloreach.dll` SHA-256:
`738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894`.

Discovery used HREK; the following retail locations were then matched through
the corresponding action dispatch and verified function behavior. These are
research locations, not installed hooks or permission to use an unverified ABI.

| Operation | HREK RVA | Retail RVA |
|---|---:|---:|
| Unit action request dispatcher | `DFC7F0` | `49F24C` |
| Update active unit action slots | `DFCC90` | `49F138` |
| Player melee request handler | `E4AA50` | `50B63C` |
| Player melee action update | `E4ACC0` | `50B8F0` |
| Build native melee hit/damage parameters | `D6C1B0` | `4919D4` |
| Consume native parameters, apply/send damage and effects | `D6BBC0` | `4924F0` |
| Native collision vector used by the builder | `41B960` | `12969C` |

Retail's `magic_melee_attack` descriptor at `A227B8` references the evaluator
`19F56C`. It constructs the same 0x44-byte request with action 0x2D and calls
`49F24C`. The dispatcher indexes the table at `A52030`; entry 0x3E points to
`B73E58`, whose request/update entries are `50B63C` and `50B8F0`. The update
calls builder `4919D4` at `50BA00` and consumer `4924F0` at `50BCA7`.
The action-slot update iterates four native slots, calls each active record's
update member at +8, and clears a slot's action when that callback returns false.
HREK caller `DDA7D0` and retail caller `4A0450` place this in the unit action
update sequence. No new update hook has yet been installed.

The builder produces 0x50 bytes of native parameters, including the actual
object, surface/node/material information, contact position/normal, and authored
weapon damage/effect definitions. HREK's vector call at `D6C67C` corresponds to
retail `491E89`. Both interpret result type 4 as an object and read its full
object index at result +0x40. The retail vector flags are loaded from `28FFCF0`.
The builder also has target selection, held-object parent handling, and a
secondary object probe: replacing a ray alone is not yet proof that its final
target must remain the hand-contact NPC.

The matched retail builder writes the final full target handle at parameter
+0x00, contact position at +0x34, and normal at +0x40. The consumer reads those
same members. Any redirected contact implementation must validate the final
handle after the builder has completed, including its secondary probe.

HREK event handler `B33680` also rejects recent damage using the attacking unit's
shared melee state and last-damage tick. Independent hand detection alone does
not remove that shared restriction. Investigation must preserve legitimate
damage/authority handling while allowing independently timed hand strikes;
neither a button pulse nor blindly sending two native events proves this.

HREK wrapper `D6BB30` calls the builder then consumer with apply=true and no
optional network-output buffer. Its caller `E5C440` passes native state 0x23,
type 0, and a computed power. This call is in a weapon/action context, not
evidence that an arbitrary simulation callback can safely invoke it for a player.
The consumer distinguishes types 0/1/2, prediction/authority, networking, and
native melee action state. Do not guess these contracts or copy another title's
damage structures. Safe execution context and exact contact retention remain
open before implementation.

Preserved outputs under ignored `out/`: `contact-melee-reach-impact-console.txt`,
`contact-melee-reach-wrapper-console.txt`, `contact-melee-reach-context-console.txt`,
`contact-melee-reach-retail-action-console.txt`, and
`contact-melee-reach-retail-impact-console.txt`.

## Other engine leads, not retail bindings

Official H3 kit `A58710(unit, nativeState, meleeType, power)` contains both native
contact selection and damage application; vector call `652A10`, damage-selection
helper `A5DE20`, native apply `A65300`. Its callers include `A5FEF0`, `B0A2D0`,
and `B0D990`. Output: `out/contact-melee-h3-builder-console.txt`. Unlike Reach's
separate builder/consumer, this kit function combines those steps.

Official H2 kit player-melee request handler `4C11B0` reads a short at request+4
(values 1..2), a target at +8, and a byte at +6, then calls `4C82A0` and
`367030`. These differ from Reach's request layout. Output:
`out/contact-melee-h2-player-console.txt`. Further H2, ODST, and H4 contact/damage
mapping is still required.

Further official-kit leads: H2 `4C82A0` starts the authored melee animation and
is not an immediate contact-damage substitute. H3 `B0D990` invokes `A58710` at
the native attack tick; `B0A2D0` invokes it with state 0x22/type 0 from a weapon
action. H4 request handler `F9CC00` likewise constructs an animation action.
H4's `melee_damage: suppressing melee damage` string references event handler
`BCB820` (reference `BCBD40`); that handler has its own resolved/recovery checks.
These are title-specific kit findings, not permission to reuse their constants
or structures in another title or to call them from arbitrary callbacks.
Outputs: `out/contact-melee-h2-action-console.txt`,
`out/contact-melee-h3-action-console.txt`, `out/contact-melee-h4-action-console.txt`,
and `out/contact-melee-h4-event-console.txt`.

## Verification so far

### Motion and damage-contract checkpoint

`contact_melee_motion.h` now computes per-point sweeps from elapsed nanoseconds
and tracking-space metres. Both endpoints use the current tracking-to-world
transform, excluding actor locomotion/turning. Stable shape, full unit handle,
reference epoch, sample count and serial are required. Duplicate eyes, invalid
tracking, backward time, gaps, recenter, respawn and weapon changes cannot
produce an initial strike. Analytic tests cover 60--240 Hz, three world scales,
independent opposite hand motions, a rotating gun tip, and crossing a thin target
between samples. Release build and core tests pass. This is geometry code only;
it is not yet connected to native damage or a claim that M1 is complete.

The shared `Hand` contact transaction now queries qualified point sweeps,
selects the earliest valid NPC contact, and submits only that target through an
explicit native backend interface. Each physical hand has its own strike latch;
duplicate eye callbacks and multiple collider points cannot multiply damage.
A 25 mm withdrawal after at least 60 ms rearms that hand without treating the
withdrawal as another hit. Native rejection is explicit, with no button fallback.
Scene tests cover simultaneous hands, repeated penetration, withdrawal/restrike,
walls, self-contact and rejected native submissions. These tests use a synthetic
scene backend; they do **not** validate actual game damage. Release build and
core tests passed after this addition (`out/contact-melee-hand-build.txt`).

Important correction to the earlier working interpretation: HREK `119420`
(retail inline game-globals byte +0x11 == 4) is **game_is_predicted**, not
game_is_playback. HREK `21D390` asserts the result at `21D3A8`; descriptor
`16390D0` names `game_is_predicted()` at `16390E8`. Playback is a different
predicate, `119360`, as proven by the `!game_is_playback()` assertion in
`1AF780` at `1AF7EF` (descriptor `1621D98`, string `1621DB0`). Therefore type 0
does not apply authoritative damage in predicted-client simulation. Do not
describe that branch as a replay-only restriction or blindly enable type 0 on
every simulation role. Proof: `out/contact-melee-reach-state-names-console.txt`.

HREK consumer type 0 calls `D7CD00` (retail `491100`). Its ordinary, non-clash
branch calls `D6CD60`, which constructs the native melee damage event with the
attacker's ownership, exact hit object at impact-record +0x1C, contact position,
material, node and authored damage definition. Its optional final direction
argument falls back to the unit's forward vector when null. This direction
must also follow the physical strike before claiming hand-directed melee.
The clash branch requires an existing opposing melee action; use the native
clash classifier, not a guessed flag. Consumer updates the native melee tick
and resolved bit after application, whereas the network event handler also
checks the shared last-damage tick before application. Preserved details:
`out/contact-melee-reach-damage-authority-console.txt` and
`out/contact-melee-reach-authority2-console.txt`.

The pending AI, independent-toggle, contact-release smoothing, and visibility
changes compile in Release; `ctest --preset release` passed on 2026-09-05.
This does not establish native contact-melee runtime behavior. No new candidate
has been packaged or installed, and no new headset result has been claimed.


## 2026-09-06 native adapter implementation (not headset accepted)

Reach now publishes immutable timed hand/controller references, queues ordered
hand nodes plus authored weapon bounds, and consumes physical segments in its
native simulation update. Its native builder, consumer, playback predicate,
and damage constructor have unique pinned-image signatures and seven verified
call edges. Authoritative submissions require reaching the native constructor
for the exact struck NPC. The optional constructor impulse vector is supplied
from the physical strike. The virtual swing/button route is disabled for Reach.
The native consumer's power parameter is a clamped damage multiplier, not an
optional zero-valued argument: it now receives 1.0 for full authored damage.
Proof: retail `4924F0` stores clamp(power,0,1) at impact +34; the matched native
constructor carries it into the damage event. Speed remains solely admission.
`out/contact-reach-damage-scale-console.txt` preserves the consumer decompile.

Halo 3 now has its own source adapter, rather than reusing Reach's native ABI:

| Native responsibility | H3EK | Pinned Halo 3 retail |
|---|---|---|
| Four-slot native action update | `A7E9D0` | `379B30`, called at `39A5AD` |
| Combined melee hit selection/application | `A58710` | `35AE34` |
| First authored melee damage response selector | `A5DE20` | `35A9A4`, state `79` selects +24C |
| Collision first-obstruction query | `652A10` | `1FD748`, melee call at `35B292` |
| Native melee apply / constructor | `A65300` / `A59860` | `35E744` / `35BEFC` |
| Native object damage event | `A745B0` wrapper to `A73390` | `36FE00`, constructor call `35C0FD` |

The kit's two-argument object-damage wrapper expands to six arguments; retail
inlines that wrapper. The consumer is void(event*, full target handle, short,
short, ushort, pointer). Both constructors put attacker at event+18, contact at
+24, and the two impulse vectors at +3C/+48. The adapter alters only its own
native stack event, for the selected full target and exact constructor caller.
Ordinary native damage remains unchanged. Native scalar 1.0 requests authored
melee damage, independent of the speed slider. Native material, node, damage,
clash classification and impact effects remain handled by the melee routine.

Querying is separate from application: local aligned scratch receives only the
native collision query. Both kit and retail initialize/use fields through +63;
result type is +0, fraction +4, point +8, normal +2C and full object handle +40.
Only the first obstruction can qualify. The selected physical segment replaces
sample 13 of the native 25-ray melee grid during one application; all other
rays are suppressed. A changed target at re-query or final damage is rejected.
Native flags come from the verified RIP-relative load `35B265` -> `2127108`.
The authored hit builder is never used as a damage-producing dry run.

H3EK datum_get `364640` checks entry stride +24, live extent +3C, entries +48,
and full signed salt at entry+0. Retail `8308C` retains the extent/stride/entries
checks; the existing H3 unit getter independently establishes object stride 18,
kind+3 and data+10. Contact admission checks full salt, bounds and biped kind.
The existing CHUD playback call `2EDFA1` -> `F01A0` supplies an independently
verified H3 playback predicate (game globals +160, unlike Reach's +1DA).

H3 physical geometry uses the same on-foot tracking-to-world transform as its
controller carrier. The palette cache now retains the exact applied collision
correction so the contact publisher can recover the desired unblocked points.
The shared bounded queue carries values only, with no engine-owned pointers.
Each hand has independent strike/rearm state. Secondary palettes own left-hand
weapon bounds after detection, with a bounded 100 ms hand-only transition.
Both native contact hooks are disabled and checked for callback/trampoline
quiescence before removal; unresolved cleanup retains the hooks for retry.

Preserved native evidence: `contact-h3-native-melee-match-console.txt`,
`contact-h3-native-apply-match-console.txt`,
`contact-h3-update-and-damage-match-console.txt`,
`contact-h3-simulation-and-damage-entry-console.txt`,
`contact-h3-damage-entry-kit-console.txt`,
`contact-h3-impact-layout-console.txt`,
`contact-h3-datum-and-query-kit-console.txt`, and
`contact-h3-standalone-query-console.txt` under ignored out/.

Still open: actual headset health/shield/damage/effects results; predicted-client
host cooldown/direction handling; truly unarmed native damage; secondary weapon
specific damage selection; H2/ODST/H4 native backends; all other request-ledger
items. The native builder needs a held weapon for its ordinary collision grid,
so an empty offhand with a primary gun is covered in source, but no-weapon melee
is not yet implemented. Build and analytic tests do not establish these results.
No new ZIP has been provided, no files installed, and acceptance is unchanged.

## 2026-09-06 resumed checkpoint: H2 native contact adapter

Recovered the full request checklist and the interrupted H2 integration. The
initial build found three declared/called but undefined H2 contact functions;
these are now implemented in `halo2_contact_melee_runtime.inl`. This supersedes
the older H2-backend status above. It does not complete the full requested pass.

Halo 3 behavior being matched: either physical hand or its held geometry selects
the first struck biped, retains the exact salted target, and uses native authored
melee damage without a virtual-button pulse. The H3 adapter itself still awaits
headset testing; the headset-accepted H3 collision/camera behavior stays the
regression reference.

H2EK `480D40` matches retail `8F3C80`: uint32 unit, int32 native state, byte
no-apply, float power. State `C000073` selects the first weapon melee response;
zero no-apply and power 1 request authored damage. H2EK `4C1620` / retail
`936910` call this at the native melee tick. The existing H2 native unit aim
callback consumes our queued contacts only for the exact local unit, on foot,
with live VR and fresh generation-matched packets. Both renderers use it.

The collision builder's 25 queries call retail `75B850` at `8F401F`; only the
centre query is replaced with the selected physical segment. The first query
and the application re-query must both hit the same full biped handle. H2EK
`481850` / retail `8F4690` construct the native damage event and call the
two-argument damage wrapper `90F4B0` at `8F4825`. This wrapper calls the native
six-argument consumer `90EB80`. The constructor's event puts attacker at +18,
position at +24 and impulse vectors at +3C/+48, independently verified against
H2EK's stack layout. Only that exact NPC event gets physical position/impulse;
the separate native weapon-recoil/self-damage call remains stock.

H2's predicted path calls `8857B0`, then native event generation `8BD810` at
`8858B0`: event 17, two full object handles, 1C-byte payload. A scoped event
hook rejects changed targets and records issued requests separately from local
damage. Native generation/host rejection remains authoritative. An issued request
is not proof of host acceptance, and this payload has no physical direction
field; remote-host direction and alternating-hand cooldown remain open.

H2EK's `!game_is_playback()` assertion calls `7A00`, which returns constant
false in the kit. It does not justify copying H3/Reach playback fields into H2.
H2EK's separate prediction assertion calls `469D00`; the native melee call
matches retail `6A6410` (game globals +C == 4). The adapter preserves native
authority routing and does not synthesize a foreign simulation mode.

H2EK datum_get `A24F0` verifies valid/stride/full salt. Retail array initializer
`67AFA0` proves capacity +20, stride +24, valid +29 and relative storage +48.
Object-array creation `8DD4D0` proves stride C and capacities 800/2800, with the
pointer store at `8DD51C` resolving to `18B7398`. Contact admission bounds the
index, verifies the complete salt and biped kind before calling the existing
object accessor. No H3 datum-table layout was copied.

New frozen H2 frames use the observer publication's timestamp/reference space
and independent physical controller pose, even when support-hand aim rotates
the visual gun. Actor movement, duplicate eyes, recenter and tracking loss do
not generate strikes. Melee runs with world collision unchecked. The former
automatic swing/button route is disabled for H2; manual native melee remains.

Release build, core tests and Reach consistency pass. New analytic H2 cases
cover independent opposite hand motion at 60/72/90/120/144/240 Hz, three scales,
actor movement, support-hand aim, duplicate eyes, recenter and lost tracking.
All five H2 signatures have exactly one pinned-image match; seven native calls
and the object-array data edge pass verification. These checks also retain the
H3/Reach binding checks. Output: `out/contact-h2-native-bindings.json`;
build/gate: `out/contact-h2-native-build.txt`, `out/contact-h2-native-gate.txt`.

Preserved additional proof: `contact-h2-adapter-contract-console.txt`,
`contact-h2-role-kit-console.txt`, `contact-h2-role-proof-console.txt`,
`contact-h2-playback-kit-console.txt`, `contact-h2-event-contract-console.txt`,
plus the previous `contact-h2-native-builder/apply/damage` evidence under out/.
Still open: headset damage/effects, fully unarmed attacks, secondary weapon
specific damage selection, predicted-host behavior, ODST/H4 backends and the
rest of `CONTACT-PASS-REQUEST-CHECKLIST.md`. No ZIP, install, launch or pointer
advance occurred.

### Follow-up contact review in the resumed pass

Review found that the pending H3 shared tracking publication and Reach contact
controller pose used the two-hand aim solver's orientation. That lets support
hand movement create primary-hand angular speed even with the primary controller
still. H3's contact snapshot now uses independently calibrated physical aim;
Reach publishes that physical orientation beside its existing visual aim and
uses it only for contact motion. Invalid contact data rejects that feature's
sample without rejecting first-person rendering. H2 already uses its independent
physical pose. Visual aim, shots and two-hand weapon placement retain their
existing paths. Release/core tests/Reach gate pass after this change.

The final H3/Reach damage callback now classifies our native constructor call
before checking the attacker and target. A changed target/attacker in that call
is rejected; it cannot fall through as ordinary native damage. Reach additionally
revalidates the complete target handle at this last boundary. Ordinary native
calls remain outside this scope. Release/core tests/Reach gate pass; no headset
claim follows from them. Logs: `contact-independent-native-build.txt`,
`contact-independent-native-gate.txt`, `contact-native-guard-build.txt` and
`contact-native-guard-gate.txt` under out/.

## ODST native contact adapter checkpoint, 2026-09-06

ODST now has its own contact adapter and independent physical-hand queues in
source. Player behavior being matched is the intended H3 contact path: a fast
physical hand or held-gun strike applies the native authored melee response to
the first struck biped, independently of World collision. This remains a test
implementation, not a headset result or all-title completion claim.

Official H3ODSTEK `atlas_tag_test.exe` (local input renamed
`halo3odst_tag_test.exe`, SHA-256
`354EC94158AECCE3E9D0F6463023AD5FA6D2AFE49B390E6067EBC17465C63C2D`)
and pinned retail SHA-256
`5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A`
independently establish:

| Responsibility | ODST kit | ODST retail |
|---|---|---|
| Unit simulation update | `B208C0`, calls `B003D0` four-slot action update | `3DEB98`, inlines the same loop; native descriptor pointer `8F6A30` |
| Combined hit selection/application | `AD3E60` | `39FA48` |
| Collision vector | `6A3B30` | `22F80C`, native melee caller `39FEB8` |
| Authored damage selection | `AD9810`, state `79` selects response +25C | `39F5AC`, same verified fields |
| Direct melee apply / constructor | `AE10F0` / `AD4FC0` | `3A3800` / `3A0B00` |
| Damage event | `AF2D90` wrapper / `AF1770` | `3B68A8`, constructor caller `3A0CFF` |
| Playback | `38F290` | `10B0A8`, existing CHUD caller `329502` |

Kit assertion caller `291F88` tests `!game_is_playback()`. Both playback
predicates test initialized globals and +160; ODST retail game globals use
TLS +40. Kit prediction predicate `38F350` tests globals +11 == 4, retained in
retail combined melee routing. These are ODST proofs, not copied H3 bindings.

The combined routine enumerates -2..2 on both grid axes. Only center sample13
is redirected to the physical segment, and every other native grid query is
suppressed inside that scoped transaction. Native flags are read from the
verified load `39FE8B` -> `217E3C8`. Collision type4, fraction+4, position+8,
normal+2C and full object+40 match ODST's existing native collision evidence.
Full handle salt, extent, stride and biped kind are checked using the ODST
object-table path already established in ODST-VEHICLE-EVIDENCE.md.

ODST's damage-event layout differs: attacker+18, position+28 and impulses
+40/+4C. The kit wrapper supplies seven arguments to its consumer; retail
inlines/specializes it to six (event, target, short region, short node, ushort
material, pointer). The scoped detour preserves that retail ABI and modifies
only the exact constructor's event for the exact full attacker/target. Native
clash, damage-tag, material and effect handling remain in the native routine.
The physical contact point is retained from the substituted native ray result.

The VR-side tracking-to-world transform is deliberately shared with ODST's
existing on-foot controller carrier. Both endpoints use the current actor frame
and consecutive independent controller poses. ODST does not use H3's secondary
weapon-palette ownership rule. Both hands still have separate strike state.
Optional contact hooks have their own failure admission and quiescent cleanup
before the world-collision trampoline/module can be released.

Validation: release build, core tests, Reach consistency gate, five unique
ODST binding patterns, five call edges, native descriptor pointer and flag-load
checks passed. Artifacts are `out/contact-odst-native-build.txt`,
`out/contact-odst-native-gate.txt`, `out/contact-odst-native-bindings.json`.
Kit/retail decompiles are preserved under `out/contact-odst-*-console.txt`.
Fully unarmed handling, network-host behavior and actual headset acceptance
remain open, along with H4 and the latest Gesture melee binding request.

## Additional gesture mode request

The user explicitly requests the old swing-gesture mode alongside true contact
melee as a separate toggle. Gesture mode must resolve the active in-game melee
binding; fixed Reclaimer/grip/right-shoulder injection is rejected. Preserve
both modes as distinct features. Implement and verify binding resolution,
setting persistence, independent toggles and duplicate-attack handling before
the melee candidate is packaged. M10 in the complete checklist tracks this.

## Contact-reactive finger request: bounded source audit, 2026-09-30

The user requested a separate Half-Life: Alyx-style hand physics/finger-reaction
toggle for all six games. This is distinct from the implemented controller
trigger/grip gestures. No contact-finger option or contact-finger behavior was
added by this audit; an option with no actual surface-driven articulation would
misrepresent the feature.

Existing source provides useful foundations, not a complete implementation:

- CE, H2, H3, ODST, Reach and H4 have their own native contact query adapters and
  admitted gameplay update paths. Native queries need not run in render/palette
  hooks. CE's `haloce_contact.cpp` already separates world and melee processing;
  the other title adapters are in their `*_contact_melee_runtime.inl` files.
- `contact_melee_queue.h` transfers bounded copied data, without native pointers,
  from render publication to the simulation consumer. Its frame includes a
  serial, reference epoch, full unit handle, shape and stable point indices.
  Those indices do **not** identify a digit, joint, segment radius or allowable
  curl. Hand and weapon samples share this transport.
- `contact_melee_motion.h` qualifies moving samples against a swing threshold,
  selects one hit, and invokes `backend.Apply` for a strike. It deliberately
  suppresses animation-only motion through its rigid-controller comparison.
  Running this path for resting fingers would either produce no contact or
  wrongly introduce melee behavior. H2/Reach/H4 admission is also explicitly
  gated by `physical_melee`; a hand-physics option must operate independently.
- Existing world-collision responses constrain a rigid hand/weapon target.
  They do not return separate contact limits for individual fingers. The
  new controller-finger helpers accept trigger/grip-derived flexion; they do
  not yet accept measured collision limits.

A concrete follow-up can reuse the native query contracts but requires a new
query-only transaction: publish each known rig's labeled joint segments and
desired flexion; run bounded stationary/swept contact queries in the admitted
native update callback; return independent digit/joint limits; apply them only
to a matching free hand and immutable render-pair snapshot. Match title,
generation, full unit, model/checksum, anatomical hand, reference epoch and
pose/shape identity. Expired results, model changes, lost tracking and optional
faults must release the limits without affecting camera, weapons or melee.

The missing contracts are segment thickness/tip extent for each admitted rig,
a tested conversion from contact point/normal to bounded joint flexion, initial
penetration/release handling, and an independent request/response scheduler with
a measured query budget. Existing zero-width native rays alone do not prove
collision of the rendered finger surface. A conservative synthetic capsule or
multi-ray hand model is possible, but must be explicitly designed and validated;
it is not an established native per-finger physics model. Stable contacts also
need hysteresis so simulation-rate results do not make the two rendered eyes
or adjacent frames alternate poses.

Required regression cases include stationary fingertips against a wall, moving
and disappearing objects, initial overlap, grip/release while touching a
surface, same-pair eye consistency, stale ownership/model changes, query faults,
and zero damage/event dispatch. Target-title and H3 headset checks remain needed
for visible contact quality and performance. This bounded audit does not expand
the current avatar candidate into an untested all-title physics subsystem.
