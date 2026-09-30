# CE caption and vehicle identity research — 2026-09-30

## Current implementation update

The vehicle-reader blocker described in the historical research below is now
resolved for a bounded ordered-node-name identity. Subtitles remain unresolved.
New retail evidence is preserved in
`out/ce-research-tags/retail-model-node-consumers.txt`. `AB25BC` iterates a loaded
model's `B8` count / relocated `BC` data with `9C` stride. `B2A1AC` independently
reads that exact table, compares node names at record offset zero against the
animation graph's named `40`-stride nodes, and writes the model-to-animation
mapping. The HCEEK render helper `7DC9B0` establishes the source table as mod2
nodes; retail `AF5EC8` establishes object definition `+34` to model lookup.
This does not reuse the disproven `2D8/2DC` block discussed below.

`haloce_vehicle_identity_contract.h` proves the cached getter, both retail
consumers, name/table instructions and cache relocation operands at cold
admission. `haloce_vehicle_identity.inl` resolves the currently validated
vehicle parent's definition and model, then hashes ordered nonempty terminated
printable node names. Its optional-reader budget is 64 nodes and 32 bytes per
name; exceeding it keeps per-game offsets. These are conservative reader
bounds, not a claim that every modded model is supported. The key includes a
versioned CE namespace, node count and delimiters, never a live datum/address.
No unproven parent field is read. Models with identical ordered node names
share the same trim profile, including custom variants sharing a rig.

The reader checks cache/table/model receipts and the node hash again, and the
owner path rechecks current unit/parent pointers, parent handle, seat and title
generation. A fault or changed receipt drops only the optional identity. The
existing camera and per-game offsets remain available. It introduces no hook,
game-memory writes, allocation, scanning or logging in the native read path.

Validation: production CE controls fixture passes relocation/datum stability,
different-model separation, invalid count/cache/name/model fallback and changed
seat rejection. Its retirement fixture validates all ten compiled wrapper
ranges after fixing the shared helper's old eight-range limit.
`tools/verify-ce-vehicle-identity.py` passes 15 pinned retail checks. Cumulative
DLL builds; runtime vehicle placement and both graphics modes need headset
acceptance. Findings below retain the research history and are superseded only
where this section explicitly supplies newer evidence.

## Earlier research

This continuation investigates two CE WIP items for the current candidate: gameplay subtitle capture and persistent per-vehicle first-person camera trims. Work used the pinned HCEEK executable and pinned MCC retail `halo1.dll` through existing Ghidra projects in read-only, no-analysis mode. At this initial research stage no runtime hook, address, or shared game/config code was added.

## CE gameplay captions

The retail image contains a real CE caption resource family: `hce_uhbs_caption_0_{ko,zh,it,jp,es,fr,chs,de,bp}`, the `hce_uhbs_caption_` prefix, and `ui\\hud\\bitmaps\\sniper\\caption_r`. These are caption-related asset names in static resource/pointer tables. They establish that authored caption resources exist; they do not reveal the gameplay text presentation contract.

The only code-backed `subtitle` string found in CE retail remains `GameOpt.Subtitles`, whose references go to settings/config event handling (`FUN_18047B180`, `FUN_18047ADE0`, `FUN_18006ACB0`). HCEEK's sound schema includes a per-permutation `subtitle data` field. I checked the official tool XML export for all 128 `tags\\sound\\dialog\\a10\\*.sound` files from the pinned archive; every one reports `checksum: ffffffff` for this field, meaning these campaign voice assets do not populate it. This is a negative result for the HCEEK authored dialogue path, not for MCC retail's full subtitle system. Caption resource labels have references only from non-function static data. No text value, string ID consumer, encoding conversion, expiry lifetime or caption draw function was identified. In particular, the resource strings do not make the event-format stack buffer or a guessed generic text slot safe to hook.

**Implementation status:** still blocked at the source-to-runtime handoff. The next proof needed is a CE caption renderer/caller plus the final localized payload representation and lifetime. A matching retail call-site signature and bounded in-call copy point are required before connecting this to the common subtitle compositor. Do not infer a producer from `GameOpt.Subtitles` or these asset names.

## CE vehicle definition identity

A new official-kit lead gives a concrete definition datum path. HCEEK Ghidra RVA `0x147190`, whose embedded assertion source is `game/aim_assist.cpp` line `0x415`, validates a vehicle parent using object type `2`, then passes the first dword returned by the object getter to the tag lookup with class `vehi` (`0x76656869`). The nearby unit branch similarly obtains class `unit` (`0x756e6974`). Thus CE's live vehicle object supplies a vehicle-definition tag datum index that is suitable as an input to a path-based identity resolver. It is not itself a persistent identity: this review has not established that tag datum indices remain stable across map/tag-cache loads.

The lookup `tag_get(class,index)` and returned `vehi` definition are concrete and native. Further HCEEK tag-manager tracing found the index-to-instance side of the path: `FUN_008adfd0(index)` resolves the tag-instance vector entry and returns its `+0x118` data-reference field. The same `0x124`-byte entry stores the copied authored path at `+0x4`, group at `+0x104`, and loaded definition pointer at `+0x11c`; therefore `FUN_008adfd0`'s result minus `0x114` is the entry base and its inline path begins four bytes into that base. `FUN_008adeb0(class,index)` independently returns the entry's definition pointer. These are read-only HCEEK findings; I have not established the corresponding retail functions or global vector and will not carry the kit address/layout into the DLL.

I extracted `banshee.vehicle` and `ghost.vehicle` from the pinned HCEEK archive and exported both using official `tool.exe`. Their top-level `model` is an authored `mod2` reference. This establishes the source vehicle-to-model relation, while the renderer trace below establishes its HCEEK runtime use. Neither gives the retail layout/binding.

**Implementation status:** the HCEEK vehicle-to-render-model data path is proven, but the retail runtime binding is not. Do not persist the 16-bit datum/index, object handle, or tag-cache address as the model key. The viable next route is to match the retail object-render/model lookup, correlate its model datum with the current guarded vehicle parent, then fingerprint a bounded model-node name/parent table. The readable path resolver is an alternate only if its retail equivalent is uniquely matched. Only after one of these routes is proven can CE persistent per-vehicle profiles be enabled safely.

## Evidence outputs

Read-only Ghidra results are preserved under ignored `out/coop-stability-20260923/`:

- `ce-next-sub-strings-20260930.txt`: HCEEK subtitle/tag schema labels and XREFs.
- `ce-next-sub-retail-strings-20260930.txt`: retail subtitle settings, localized/caption resource strings.
- `ce-next-sub-captions-refs-20260930.txt`: resource pointer XREFs and code-less table references.
- `ce-vehicle-def-strings-20260930.txt`: HCEEK vehicle/object symbols and assertions.
- `ce-vehicle-def-chain-20260930.txt`: official HCEEK `aim_assist.cpp` vehicle-definition access.
- `ce-vehicle-object-api-20260930.txt`: object/tag API callers and known class/index lookups.
- `ce-tag-path-strings-20260930.txt`: tag path/file vocabulary, read alongside the tag-manager helper trace below.
- `ce-tag-manager-fns-20260930.txt`: HCEEK tag manager record construction, layout, index-to-data and path lookup helpers.
- `ce-vehicle-tagcallers-20260930.txt` and `ce-tagget-retail-20260930.txt`: existing HCEEK/retail tag cache analysis; the retail match for the path helper remains unlocated.
- `out/ce-dialogue-batch/*.xml`: ignored official HCEEK exports for 128 A10 dialogue sound tags; all subtitle-data checksums are empty (`ffffffff`).
- `out/ce-research-tags/banshee.xml` and `ghost.xml`: official tool XML exports from only the two matching vehicle tags extracted into ignored output; they prove authored `mod2` model references, not retail offsets.
- `out/ce-research-tags/banshee.gbxmodel.xml` and `ghost.gbxmodel.xml`: official model-tag XML exports used to compare their ordered node names and parent links.
- `ce-render-objects-model-trace.txt` and `ce-render-model-helper-followup.txt`: HCEEK `render_objects.c` -> object definition `+0x34` -> `mod2` draw helper trace.
- `out/ce-research-tags/banshee.gbxmodel.xml` and `ghost.gbxmodel.xml`: official model exports used to check ordered node names and parent links on two static vehicle examples.
- `ce-object-definition-functions-followup.txt`: generic object-type helper inspection preceding the render-object trace.

No build, game installation, game launch, packaging, or commit was performed.

## Existing CE model fingerprint route checked

CE does already have a content-derived model identity for first-person weapons. `BuildFirstPersonBinding` hashes the complete ordered animation-node names and parent links with FNV-1a into `nodeIdentity`; the graph definition comes from the verified CE cached-tag path (`first_person_cached_tag_get`, followed by a bounded tag-cache remap). The per-weapon geometry and accessory tables require that structural identity, not the transient graph datum. This is good precedent for identity policy, but it is specifically a weapon animation graph, not a vehicle model identity.

The vehicle lead reaches the native `vehi` definition through the vehicle object's first dword tag index. Official HCEEK source XML and renderer code establish the model field and loaded `mod2` model path, but not its node layout or identity fields. The weapon node-name fingerprint remains non-transferable as an implementation; a separate bounded model-node fingerprint is plausible only after its retail producer and node fields are matched. Hashing a vehicle datum, guessed definition bytes, or a similarly shaped graph would still be unsound.

The follow-up retail query searched the existing read-only `CERetail` project for string references containing `tag_get`, `tag_get_by_index`, `vehicle_definition`, `vehicle tag`, and `tag_index`, then searched imported/demangled function names for `tag`. Both searches returned zero matches. This is not proof the optimized retail image lacks a tag resolver; it confirms names/xrefs cannot identify one in this project. A later function-level trace established the object-to-model field path described below, but not the node layout or a safe vehicle-profile binding.

The existing retail cached-tag accessor at `0xA9B648` has 1,139 direct xrefs in the read-only `CERetail` project. Its compact signature loads a cache-table entry by a 16-bit index and returns a relocated loaded-data pointer; broad use supports the claim that it is a generic tag-data accessor, not a weapon-graph-only routine. The xref count does not identify which callsites resolve vehicle definitions or model nodes, so it does not close the retail field-offset path. Captured query: `ce-retail-cached-tag-get-xrefs-20260930.txt`.

A separate retail string scan found only a static data reference for `render_model_nodes`; no function xref or model-render diagnostic led to a retail consumer. `ce-retail-model-render-strings-20260930.txt` preserves that negative string query.

I first inspected generic object-type definition helpers, which only checked tag group references. That path was not the render-model traversal; the render_objects/models call chain below is the useful lead.

The documented HCEEK archive exists and its SHA-256 was recorded locally as `6C9C156237C78AB1B983080495FDCFF698A3F15FE0BAAADB1C663654D27F7BD4`; its file listing includes authored vehicle tag paths such as `tags\\vehicles\\banshee\\banshee.vehicle` and `tags\\vehicles\\ghost\\ghost.vehicle`. I extracted those two small tag files into ignored output and confirmed their authored `mod2` model references with the kit's official exporter. They remain source-level evidence only, not a runtime lookup contract.

Further caption checks queried every retail `caption` and `subtitle` string, then the data references around the complete `hce_uhbs_caption_0_*` language table. The 10 language resource names and `sniper\\caption_r` are referenced only by neighboring static table entries (`<no function>`); no code xref from those entries was present. The only code-backed subtitle name remains the settings key. This does not prove no dynamic consumer exists (the language table may be indexed indirectly), so the finding stays “consumer not located”, not “captions unused”. Added outputs: `ce-caption-retail-strings-20260930.txt` and `ce-caption-table-refs-20260930.txt`.

The A10 scenario XML contains compiled script/help text and an `ingame help text` `ustr` reference, but no direct subtitle/caption payload. Combined with all 128 A10 sound `subtitle data` fields being empty, this narrows the old-kit tag route only; it does not prove MCC retail captions are unused or have no other source.

An additional read-only `CERetail` query searched string xrefs for `tag_get`, `tag_get_by_index`, `vehicle_definition`, `vehicle tag`, and `tag_index`; it found none, and a function-name search for `tag` likewise returned no functions. This only rules out those straightforward textual leads. It does not show that retail lacks a tag manager or that its caption source is absent.

The vehicle follow-up advanced beyond the generic-definition check. In official HCEEK `render_objects.c`, `FUN_00830260` resolves a rendered object's definition using its object datum and calls `FUN_007dc9b0` with the definition dword at `+0x34`. The `FUN_007dc9b0` body resolves that argument with class `mod2` and reads the loaded render-model definition. This proves the HCEEK edge `live object -> object definition +0x34 -> mod2 model`. Because Banshee/Ghost are vehicle tags with that same inherited object model reference, it proves the vehicle model-field path in HCEEK. This establishes no node layout or identity fields by itself.

The initial retail follow-up was corrected after re-reading the decompilation: in `FUN_180af5ec8`, the `+0x2D8` count and `+0x2DC` cache-relative block are read from `lVar2`, the original object-definition pointer, not `lVar3`, the model pointer returned by the second cached-tag lookup. Those fields therefore are **not evidence of a mod2 node table**. A separate reader at `FUN_180b08914` does resolve an object definition's `+0x34` as a model tag and accesses the resulting model's `+0x2D8/+0x2DC`, but the record member it reads at `+0x0C` is passed into attachment/tag helpers and its name/parent semantics are not proven. Do not treat that block or member as a stable fingerprint source. The retail object-to-model edge is now established; the renderer's model-node schema, a reliable current vehicle-parent correlation, and record lifetime remain open.

The exact HCEEK trace is preserved in `out/coop-stability-20260923/ce-render-objects-model-trace.txt` and `ce-render-model-helper-followup.txt`. Official Banshee/Ghost model XML exports also show distinct ordered node names/parents (`frame hull/cab/left rudder/right rudder` vs `frame hull/guns/left flap/right flap/seat`). A bounded offline serialization of `name:parent` rows yields SHA-256 `A5C4F795B3B1BF94AF97EA67E1262C6600223D95AD94C02C1F4A20561569A11E` (Banshee) and `FB68BE379C62F6B2DAF890CE1C0BB92F18657F2C05BFC9FCDD419184C71C092A` (Ghost). This only demonstrates that these static examples are structurally distinguishable; the values are not runtime identities or shipped profile keys. The retail object-to-model edge is established, but neither a renderer model-node schema nor reliable current local parent observation is established. Those require additional retail evidence and runtime ownership/lifetime guards before implementation.

I also followed HCEEK `vehi` class references around its native physics/seat code. The official `FUN_008e1640` uses the vehicle definition's physics tag (`vehi +0x8c` -> `phys`) for vehicle orientation physics. That branch itself did not expose the model; the separate renderer path above did. No retail match was established by this research.
