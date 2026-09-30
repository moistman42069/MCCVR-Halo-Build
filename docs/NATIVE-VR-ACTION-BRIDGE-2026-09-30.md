# Native VR action isolation: September 30 implementation

The Halo 3 reference experience is a consistent VR action regardless of MCC's
selected controller layout, with per-game overrides and explicit Unbound. This
change starts the native consumer boundary needed to achieve that behavior.
**All six titles now have their own native bridges in this change.** The older
layout-aware transport adapter remains the explicit fallback when an optional
bridge cannot be proven or admitted. Per-title headset/layout/multiplayer
acceptance remains outstanding; implementation and offline proof are not acceptance.

## Reach evidence

Official `reach_tag_test.exe` SHA-256:
`CBDD8448A87A433B0DFFC0DE47D06DB7A18B4BF868B96B057135DAA86790ABA8`.
Pinned `haloreach.dll` SHA-256:
`738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894`.

The official kit established each behavior before retail matching:

| Contract | Official kit RVA | Pinned retail match |
| --- | --- | --- |
| Native gamepad converter, six arguments including elapsed milliseconds | `2511F0` | `ABDBC` |
| Direct XInput wrapper | `2510A0`, call `25110D` | Inlined in `239E0`, converter call `23DF6` |
| MCC supplied-pad wrapper, copies its input before conversion | `251150`, call `2511DB` | Inlined in `239E0`, converter call `23D87` |
| Four-controller input polling loop | `C2910` | `239E0..23EEF` |
| Raw gamepad records, stride `3C` | `3F3DC14` | `2DFC224` |
| Abstract button reader | `1D6B70` | `D1070` |
| Abstract state, stride `700`, controller field `6FC` | `404EC44` | `287FF94`, resolved from `62C2D` |
| Native player-control builder | `1E2530` | `5DF80..60F12`, native exception-table bounds verified |
| Dedicated reload reads, action `3E` | `1E5B4E`, `1E5B86` | `60C8F`, `60CAF` |
| Generic-use reads, action `3` | `1E592A`, `1E598D` | Same matched player-control builder |

The kit's reload reads set separate input packet flags `10000` and `20000`.
This distinction is established by the consumer, not merely by a UI glyph name.
The sixteen-byte abstract button record has milliseconds at `0`, held frames at
`2`, consumed flags at `3`, amount at `4`, requested action at `8`, and owner at
`C`. The unclaimed owner is **`7F`**, unlike H3's byte-sized `FF` owner. The kit
abstraction updater `1D84B0` copies frame/millisecond counters and clears consumed
bit zero on release; it does not clear consumption on every held-frame read.

That updater also calls the reader to **write** keyboard counters. Returning a
private VR record to every reader caller would let the native updater overwrite
the VR state. The adapter therefore admits only the uniquely matched native
player-control builder's caller range. Other readers always receive the original
native pointer. Its bounds are checked against the loaded PE exception table.

`tools/verify-native-vr-actions.py` performs 33 pinned kit/retail checks, including
seven unique production signatures, both converter routes, the owned output-row
calculation and controller stride, separate reload consumer calls, and the
native function bounds. Preserved full decompiles are under ignored
`out/input-independent-reach-{cadence,poll,abstraction,owner,reset}.txt`.

## Implementation and failure isolation

`Mapper::ApplyDetailed` retains both the named action mask and the older transport
mask. It does not reconstruct names from shared XInput bits. Once the bridge has
observed an owned native converter and an owned player-control read, Reach's VR
buttons are published as named actions and omitted from the XInput button/trigger
payload. Physical gamepad data and the movement/aim axes retain their existing
paths. Per-game Unbound suppresses the selected virtual action.

The controller-zero converter output is verified by its exact native record
address; the converter's unused first argument is not treated as a controller
ID. A coherent generation-tagged atomic publication accommodates MCC polling
XInput on a different thread or copying the input packet. Held counters advance
on the native converter cadence, never per XR frame or reader access. Private
records preserve consumption until release. When a physical keyboard/gamepad
button is held, the original native record remains authoritative, including its
analog amount and consumption state.

Magazine/holster pulses, gesture melee and physical crouch carry named action
bits into the bridge. A pulse that was captured using the previous transport
route drains on that route before direct admission. Route changes use the
mapper's release-before-rebind rule. Universal scope clears the native Zoom
semantic action explicitly. Menu, invalid input and early return paths cancel
the publication; a cancellation epoch prevents a later poll reviving an older
converter sample. Publication captures its generation before admission and
rechecks it before commit, so retirement/reinstallation cannot relabel an old
action as belonging to a new title generation.

The bridge waits for the existing transport resolver to verify and cache the
unmodified reader before installing MinHook. The worker retires stale hooks
before refreshing the transport resolver; this avoids caching a patched
prologue as a permanent verification failure. Missing/ambiguous signatures,
hook failures and guarded access faults retain transport routing, without
disarming camera ownership. A failed generation is not retried each worker
tick. Disable, quiescence and trampoline removal are separate steps; failed
quiescence retains the trampolines until safe retirement.

## Validation and remaining work

The production adapter is compiled directly into
`tests/native_vr_actions_runtime_tests.cpp`, with simulated native originals,
native record memory, clock, signatures and hook lifecycle calls. Tests cover
the actual converter and reader hooks, cross-thread publication, the copied
MCC pad route, physical input preservation, consumption/release, rebind/Unbound,
legacy-to-direct held-input draining, separate Reload/Use, non-player reader
writers, cancellation, generation replacement, hook creation/enable failure,
and quiescence failure/retry. These are native-boundary tests, not headset
acceptance. The September 30 combined fixture passed 255 checks, including CE's
production converter/consumer, exception restoration, nested-call isolation and
Reach's publication-expiry boundary; use the final cumulative build's results
for delivery.

Reach still needs a headset result across MCC layouts, reload/use targets,
vehicle input, physical gamepad coexistence and pause/title transitions. The
H2's existing reader returns binding metadata, so its new adapter instead uses
its native packed snapshot copier. CE's different representation is handled
separately below.
No cross-title addresses or layouts have been substituted. The accepted build
pointer and installed game files remain unchanged.

## CE evidence and scoped consumer bridge

Official HCEEK `halo_tag_test.exe` (the pin in
`CE-CONTROLLER-MAPPING-EVIDENCE-2026-09-23.md`) establishes another representation,
not the Reach button-record ABI. Kit state getter `1C0180` returns
`5248E68 + controller*60` (absolute kit VA). The 25 digital actions have frame
bytes at `state+1+action` and millisecond words at `state+1A+2*action`.
Kit action-frame getter `1C0780` asserts an action below `19` hex and a
controller below four. Movement/look floats occupy `4C..58`; the player-control
builder `18DC90` also writes `state+5C=1`. Returning a private whole-state copy
would therefore need to preserve a native side effect, not merely copy buttons.
The kit builder calls the state getter at `18DD36`, and copies the 25 frame/ms
pairs before interpreting action flags. Reload `13` and Use `2` are distinct.

Retail `ADC640` independently matches the state getter: its RIP-relative load
at `ADC643` resolves `2EA2890`, followed by the `controller*3 << 5` stride.
Retail updater `ADD6FC` copies all `60` hex bytes of each previous state before
building its replacement. Injecting virtual counters after that updater without
separating previous native values would feed virtual history into its next
native update. The retail XInput converter `C0E330` matches kit `1C6660`, whose
ABI has **four** arguments, unlike Reach's six.

Retail player-control builder **`A97CF4..A99159`** matches kit `18DC90`.
It is called at `A991EF` with user in ECX, controller in EDX, seconds in XMM2
and output in R9. The getter was inlined: `A97E13` obtains the image base, then
`A97E1A` adds `2EA2890`, and the following instructions add `controller*60`.
This image-base-relative form explains why a RIP-to-state reference scan missed
the actual consumer. It does not establish an unused input system. The 25-action
copy loop at `A98A31..A98A68` independently matches the kit's frame/ms array.
Retail converter preserves the real controller index in ESI and forwards it to
the preference copier at `C0E4FF`; unlike Reach, RCX is a valid controller here.

`native_vr_actions_ce.inl` installs only the verified converter and this local
player-control builder. The converter advances private CE frame/ms counters on
native input cadence. Only controller zero, user `0..3`, the exact caller return
`A991F4`, matching generation, fresh publication and same-thread converter
observation admit the scoped overlay. Native keyboard/gamepad frames take
precedence; only inactive digital entries receive virtual values. Unsupported
equipment/sprint IDs are skipped. Reload `13` and Use `2` remain independent.

The overlay saves and restores exactly the frame/ms pairs it borrowed around
the original consumer, including when the original raises an exception. The
native exception propagates. Movement/look values, flags and the native `+5C`
write remain intact. A nested consumer temporarily suspends the outer overlay,
sees native state, then resumes the outer view; this prevents a nested nonlocal
reader inheriting virtual values. The abstraction updater never sees virtual
history in its next previous-state copy. Menu cancellation, stale publications,
title/generation changes and optional hook failures retain transport fallback.
Native byte/word access is isolated in small non-inlined, volatile read/write
helpers that return failure from SEH; lifecycle fault publication happens in
ordinary code outside those handlers. Reach uses the same guarded-helper shape
for its controller and native-frame reads. The optimized fixture deliberately
points each title's state at inaccessible memory and verifies stock invocation plus optional
retirement, rather than trusting the presence of a `__try` statement.
The coherent publication includes title as well as generation: equal generation
numbers in CE and Reach cannot be confused. No simulation/network packet hook
or remote-player control is modified.

The native producer/consumer thread contract and scoped writes still require
headset/online regression testing; the fixture is not a claim about scheduling
or behavior in a running game. The adapter deliberately uses CE's native arrays
instead of a fabricated Reach record layout.

Read-only decompilations are retained in ignored
`out/input-independent-ce-{state-reader,consumer,consumption,control-update,local-control}.txt`;
the prior updater/converter evidence remains under
`out/coop-stability-20260923/ce-input-*`. The combined read-only verifier
`tools/verify-native-vr-actions.py` passes 53 pinned kit/retail checks, including
all unique CE entry/state/caller patterns, converter ABI witnesses and native
unwind bounds. The accepted pointer and installed games remain unchanged.


## H3: correcting UI alias identity with the actual consumer

Official H3EK `halo3_tag_test.exe` SHA-256:
`59A78F2C96034D7CEB5D710505B2B36813AA141FC81A083E3F952973DBCE4602`.
Pinned `halo3.dll` SHA-256:
`B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63`.

The old UI-glyph lookup names Reload and Use as action `3`. That does **not**
prove a shared native player-control consumer. The official kit's actual builder
`448A70` reads action **`26`** at `44B246` and `44B284`, writing held bit `1000`
and long-held bit `2000` in its output `+2C`. Generic Use independently reads
`3` and writes bits `1/2`. The player-action packers `44DC90` and `44F130` copy
these flags from control state `+D0` into action packet `+40`. Native edge
resolver `341C20` converts Reload held bit `1000` to pressed bit `4000`.
`343FC0` then sets primary reload control bit 30 after checking the weapon;
`344C30` copies action `+8` into unit-control `+10`, and `A5BB10` copies that
into unit `+188`. Finally `A6A790` requests native action `3D` through `A7E8D0`.
Secondary reload has its own bit31/action `3F`. These downstream functions were
read to establish semantics; none is hooked or called by the VR adapter.

The retail match independently preserves action `26` at `F6231/F6257`, reader
calls `F6236/F625F`, and held-bit12 output. The player-control builder has exact
exception-table bounds `F42D8..F6487`. This permits separate Reload and Use
records at the input reader without modifying simulation, network packets,
weapon validation, automatic reload or native context selection.

| Contract | Official H3 kit RVA | Pinned retail match |
| --- | --- | --- |
| Six-argument native gamepad converter | `4FC7B0` | `172434..172719` |
| Direct / MCC copied wrapper calls | `4FC654`, `4FC707` | `A7D16`, `A7CB5` |
| Real controller selects preference row | `4FC99E -> 4DA510` | ECX preserved to R14 at `17246E`; row stride `21C` at `1725EB` |
| Abstract reader | `4D9BC0` | `187028` |
| Abstract state | independently established H3 mapping evidence | `2229B54`, row stride `518`, controller field `514` |
| Player-control builder | `448A70` | `F42D8..F6487` |
| Dedicated reload | action `26` | action `26`, not glyph alias `3` |

`native_vr_actions_h3.inl` owns only converter/reader hooks. It admits the real
controller-zero conversion, then only reads from that player's exact abstract
state inside the verified builder. The private H3 record is twelve bytes with
byte action/owner fields at `8/9`, unowned value `FF`; it is not a Reach record.
Native physical counters take precedence. Consumed state persists while held
and clears on release. Existing freshness, title/generation, canceled-poll,
quiescence and isolated-fault rules apply. `ConsumerAction` expresses the proven
H3 reload identity while the older transport lookup stays unchanged for safe
fallback. No ODST or H4 consumer identity is inferred from this result.

The production fixture covers H3 legacy warmup, separate Reload/Use, Unbound,
consumed holds/release, physical input, copied input, other controllers and
non-player callers, menu cancellation, expiry, inaccessible state, partial hook
failure and deferred retirement. Combined fixture: **151 checks passed**.
Headset/online/layout regression remains required. Read-only traces are retained
in `out/input-independent-h3-{kit,action-generation,action-resolution,action-pack,
unit-control-producer,unit-weapon-control}.txt`. Prior claims that H3 necessarily
requires a shared Reload/Use consumer split are superseded by this evidence.


## ODST: dedicated consumer action 2A

Official `halo3odst_tag_test.exe` SHA-256:
`354EC94158AECCE3E9D0F6463023AD5FA6D2AFE49B390E6067EBC17465C63C2D`.
Retail `halo3odst.dll` SHA-256:
`5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A`.

ODST is independently different: action `26` contributes an unrelated input
flag, while **`2A`** is the dedicated primary Reload consumer. Kit builder
`4A66D0` reads it at `4A9093/4A90D1`, generating held/long-held flags `1000/2000`
at output `+2C`. Generic Use stays action `3`. ODST's own edge resolver `3659A0`
converts the held bit into pressed `4000`; simulation consumer `368170` checks
the primary weapon and writes control bit30 at `368524`. The player update calls
these at `371419/371442`. Ghidra's partial analysis named the latter consumer
from `368190`; the actual call target and prologue are `368170`. None of these
simulation functions is hooked.

| Contract | Official ODST kit RVA | Pinned retail match |
| --- | --- | --- |
| Six-argument converter | `554A90` | `1A0E30..1A10AF` |
| Direct / copied routes | `554934`, `5549E7` | `B4BCE`, `B4B6D` |
| Real controller preference copy | `554C7E -> 52E810` | ECX retained in EBP; `1A0FEE -> 1B9890` |
| Abstract reader | `52DE30` | `1B8420` |
| Abstract state | established ODST mapping evidence | `227AA54`, stride `558`, controller field `554` |
| Builder and dedicated Reload reads | `4A66D0`, `4A9093/4A90D1` | `10FBFC..111F5F`, `111CC7/111CF0` |

`native_vr_actions_odst.inl` uses those own-title converter, state, reader and
consumer proofs with ODST's twelve-byte record. Its lifecycle and routing checks
match the tested input behavior, while every address, controller field and
Reload identity comes from ODST. It preserves physical inputs, native original
calls, per-title cancellation and optional-feature retirement. The combined
production fixture passes **183 checks**. Runtime/headset and multiplayer
acceptance remain outstanding; the adapter does not claim to fix the separately
reported ODST bridge-explosion crash. Preserved evidence is
`out/input-independent-odst-{consumer,reload-edges,reload-consumer}.txt`.


## Halo 4: dedicated consumer action 31

Official `halo4_tag_test.exe` SHA-256:
`B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6`.
Retail `halo4.dll` SHA-256:
`7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84`.

H4's kit builder `223580` reads dedicated **Reload 31** at `226B5F/226B9D`,
producing input bits18/19. Generic Use is independently action2 and bits22/23.
Input flags are output `+30`; `22C0A0` stores them at player-control state `+10`,
and packer `222350` copies that field to player-action `+40` (`2223B9`). Native
edge resolver `1C90B0` turns bit18 into pressed bit20 (`1C9575`). Player action
consumer `1CE1D0` checks the primary weapon and sets control bit44 (`1CEB84`).
Unit weapon update `E8DE70` checks its control bit44 and submits primary reload
**action46** at `E8E204 -> F3F070`; the separately named
`weapon_owner_submit_reload_action` path `EA7FB0` confirms the reload identity.
These simulation and unit-control functions are evidence only and are not hooked.

| Contract | Official H4 kit RVA | Pinned retail match |
| --- | --- | --- |
| Six-argument gamepad converter | `342E60` | `1201A8..120391` |
| Direct / copied converter calls | `342D7D`, `342E50` | `56F62`, `56EED` |
| Native polling loop | `CCD00`, calls `CD136/CD0C3` | `56B40..5705E` |
| Raw output row, stride3C | `4518014` | `28ACA74` |
| Abstract reader | `1FFED0` | `13F5CC` |
| Abstract state / controller field | stride7E8 / field7E4 | `2E959D4`, own matching layout |
| Player builder / reload reads | `223580`, `226B5F/226B9D` | `A2670..A55EE`, `A526C/A5291` |

Unlike H3/ODST, the H4 converter **does not use RCX as a controller ID**. Its
adapter proves the native output row from both direct and copied routes and
accepts only row zero. It uses H4's own sixteen-byte native record, DWORD owner
`7F`, controller field `7E4`, unique signatures and exception-table caller bounds.
Only the native player builder can receive a private record; the abstraction
writer and all other consumers retain original pointers. Physical holds,
consumption, cancellation, lifecycle failure and generation checks are preserved.

The combined production fixture passes **215 checks**, including H4's actual
converter/reader, distinct Reload/Use, Unbound, owner/consumption continuity,
native/copied output-row admission, physical input, non-player callers, menu
cancellation, expiry, guarded faults and retirement. The pinned verifier passes
**142 checks** across five titles. This still requires H4 and Halo 3 headset,
layout, vehicle, multiplayer and transition regression results. It does not
claim headset acceptance. Read-only traces are under
`out/input-independent-h4-{consumer,poll-next,action-flow,edge-packet,pack-proof,unit-reload}.txt`.


## Halo 2: private packed snapshot merge

Official `halo2_tag_test.exe` SHA-256:
`D0B71186D3948C48DDD02E2CCB88FA13E77E25A3D8F7FA60922F23A2A0073E36`.
Retail `halo2.dll` SHA-256:
`DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946`.

The official H2 kit establishes a different representation: abstraction updater
`9AE40` produces a **C0-byte** controller snapshot with 48 frame counters at0,
48 16-bit millisecond counters at30, primary/secondary trigger floats at90/94,
and movement/look floats from98. Getter `9BEC0` copies that snapshot to its
caller's output. Player-control builder `72D40` uses that copier at
`72E01/72E29`, then applies its native player consumption masks to the copied
counters before generating gameplay actions. Dedicated Reload2E sets packet
bit21; Use23 sets bit10. Those consumers are independently selectable.

H2's own primary fire selector `9F7D0` returns action0A,16 or18 according to
weapon context, and its secondary selector `9F810` returns17 or19. The updater
uses those same families for its trigger floats. VR Fire fills the primary
family and trigger90; the deliberately combined Grenade/secondary action fills
Grenade7 plus secondary17/19 and trigger94. The native consumer chooses the
applicable context, including dual weapons and vehicle weapons. This is an
explicit H2 semantic grouping, not an inferred raw-button alias. Physical
nonzero counters and analog amounts always retain their own native values.

| Contract | Official H2 kit RVA | Pinned retail match |
| --- | --- | --- |
| Six-argument abstraction update, real controller first | `9AE40`, caller `A02AC` | `6D8AB0`, caller `6E0E9E` |
| Native digital cadence helper | `49C50`, called at `9B2FA` | `6D1E30`, called at `6D913F` |
| Native elapsed milliseconds | `48700`, called at `9B2D8` | `6D0C60`, called at `6D9128`, global `15EA688` |
| Packed snapshot copier | `9BEC0`, source `BFFF48` | `6D9C40`, source `15F1E30`, strideC0 |
| Player builder and owned copier return | `72D40`, calls `72E01/72E29` | `6C0E30`, call `6C0F42`, return `6C0F47` |

`native_vr_actions_h2.inl` hooks only the updater and copier. The updater observes
controller zero writing its proved native output row and advances private
counters using H2's own elapsed input clock. The copier calls the original once,
then merges only the local player's builder copy at the exact proved return
address. It never changes the global input snapshot or binding preferences.
Native consumption masks, remaining fields and simulation code stay native.
The first builder exception-table fragment `6C0E30..6C10F0` contains the copier
call and is checked; it is not incorrectly described as the entire large builder.
The additional Reload consumer signature at `6C36BF` anchors the downstream
input packet meaning. Copy/clock access faults retire only this optional feature.
Native exceptions propagate with balanced callback counts.

The combined production fixture now passes **255 checks** and covers all six
actual adapters. H2 coverage includes warmup, separate Reload/Use, rebind/release,
repeated-copy cadence, physical counters and analog preservation, complete
primary/secondary context families, no global mutation, movement preservation,
other-controller/non-player exclusion, cancellation/expiry, original exceptions,
guarded access faults, partial-hook rollback and deferred retirement.
Pinned verifier: **172 checks** across all six titles. Headset, layout, vehicle,
multiplayer and Halo3 regression results remain required. Retained read-only
traces: `out/input-independent-h2-{consumer,retail,trigger-context,match}.txt`.
