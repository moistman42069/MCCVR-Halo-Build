# Halo 4 persistent vehicle identity

The native marker evaluator at 0x5D5B74 supplies the own-title chain:
object definition +8, object tag model +0x70, model render tag +0x0C.
Three exact cold witnesses in halo4_vehicle_identity.inl gate this optional
reader; failure retains per-game offsets. Existing Halo 4 render-model
checksum/node-count resolution is shared through halo4_render_model_identity.inl.
Saved identity combines authored runtime-import checksum and native node count,
not a map-local datum. The parent owner and seat remain native adapter receipts.
All tag references and descriptors are reread before publication; invalid,
unloaded, overflowing or changing references return no identity.

The production-reader fixture tests 64 changed map-local index assignments,
missing proof, unloaded tags, invalid node counts, invalid checksums, address
overflow and mid-read mutation. Offline checks do not establish vehicle pose,
seat usability or stutter behavior in the headset. CE's independent reader is
still research-only and must not reuse this layout.
