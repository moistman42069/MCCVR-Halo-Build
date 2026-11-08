# Update resilience: what to make survive the next host or game update

A running list, not work for the current release. Each entry names a fact the mod takes as fixed, what broke (or
would break) when a host changed it, and how the mod could learn it at run time instead, the way the direct drive
resolves its addresses and proves them before it writes (drive_cascade.h).

Started 2026-11-07 from the Project Reclaimer 0.10 port, where the client recompiled Halo 3, replaced about 730 of its
functions with its own versions, and enlarged several of its tables.

## Layouts the mod hard-codes

1. **Halo 3's first-person weapon record.** The mod reads the per-player first-person block (engine TLS +0x568) as two
   weapon slots of 0x11BC bytes, a bone array at +0x4A4, the selected node at +0x11A4, a player stride of 0x2430 and
   a cached render root at +0x23F0 (game.cpp FindFirstPersonWeapon, LoadCachedRenderRoot; halo3_muzzle_publication.inl;
   native_reload_tail.inl). Reclaimer 0.10 raises the first-person node limit from 64 to 128 (its log:
   `fp_nodes=128`), so every field after the bone array moves: the left-hand (dual-wield) weapon was never found, and
   the cached render root read empty. *Resilient form:* take the slot stride from the engine itself, as the difference
   between the bone pointers the first-person interpolator hands back for slot 0 and slot 1, or from the node
   capacity the composer is given, and derive the later fields from it; prove it by the weapon handle at +0x3C
   matching the owner's weapon. *Done for Halo 3 (2026-11-07):* the node capacity is now proven on the first composed
   primary weapon (the selected node must be one of its nodes, the render root a real transform) and every later
   field is derived from it. The same block exists in ODST, Reach and Halo 4 with their own constants.
2. **Reclaimer's other enlarged tables.** Its log lists them (`[capacity]` lines): player_control 0x80 -> 0x400 bytes,
   input mapping users and controllers, HUD markers, simulation players and views, entity counts. Any mod read of
   one of these structures by a fixed stride or field offset is at risk. *Action:* audit the mod's reads against that
   list on each Reclaimer release, and prefer strides read from the engine's own code (an imul or lea in a proven
   function) over constants.

2a. **The session reader (Co-op safe, automatic).** It believes the player count and machine mask only when the
   players array proves Halo 3's layout, with a maximum of 16 players. Reclaimer 0.10 raises players to 128 and
   machines to 128 (`[capacity] players.count: 0x10 -> 0x80`), so every read says "not read" and Co-op safe stays on
   for the whole session, solo levels included (2026-11-07 log: "The session as last read: not read"). That fails
   safe, but every one-machine feature is then off in solo play too. *Resilient form:* take the array's maximum
   and the mask's width from the array header and the host's own capacity, not from constants, and keep the
   simulation type (which still reads) as the floor: sync-client is always shared.
2b. **The navpoint count.** The player tag settings read how many navpoints a HUD keeps from the draw's own
   displacements in halo3.dll's code: 20. Reclaimer 0.10 keeps 212 (`hud.markers: 0x14 -> 0xd4`) and runs a version
   of the draw of its own (exe+0x2E636C0) that draws all 212 itself when it takes the call, and only otherwise runs the
   20-entry translated body the mod hooks. In the co-op games of 2026-11-07 the translated body ran every frame, but
   a host that takes the call would leave every tag as the game draws it. *Resilient form:* hook the host's own
   version of a replaced function where there is one (its entry is in the "client's own version" log line) and read
   the count from the host's capacity.
2c. **Which HUD layout record a level draws from.** Under Reclaimer the HUD size and width are written to every
   Halo 3 layout record (motion sensor at 55,661) in the engine's tag data, keeping the level's own apart from copies
   left from earlier levels. ODST's levels are different: the client draws their HUD from its own parse of ODST's HUD
   globals, a list of five 0x110-byte buffers ({capacity, pointer, length} each), and the mod writes the 1280x720
   buffer of the one list that names all five in ODST's order (FA 2026-11-07; hundreds of freed copies sit beside it).
   A Reclaimer that stores that block differently (another element layout, a struct instead of byte buffers, records
   merged into one buffer) leaves ODST's HUD at the game's size; the log then says "0 list(s) name ODST's whole block"
   instead of "the client draws its HUD from its own copy". *Resilient form:* find the client's ODST HUD globals
   from the code that reads them (the client's draw of ODST's HUD) rather than by the shape of its heap data, or ask
   Reclaimer's developers for a HUD safe-area setting the mod can set.

## Behaviour the mod assumes of its host

3. **Window focus.** The mod's MCC keep-alive (a faked WM_ACTIVATE, a swallowed WM_KILLFOCUS) left Reclaimer 0.10
   ignoring every input after pause, Alt+Tab and unpause. *Resilient form:* apply a host behaviour only to the host
   it was proven on, and log the focus messages the host receives when a new host version is first seen.
4. **The cursor lives on the monitor.** The menu pointer moved the real cursor; Reclaimer 0.10's window is larger than
   the monitor. *Resilient form:* check, at run time, whether the window fits its monitor, and pick the delivery that
   reaches every point of it (now done for Reclaimer).
5. **Per-host tuning constants.** The Aim Frame's Reclaimer HUD width (1.25) and the Halo 3 HUD layout a host reads
   before the mod's (0.97 vs 0.87) were set for one host version. *Resilient form:* read the layout the host actually
   uses (the SAFEFRAME scan already reads it) and derive the correction from it, instead of keying on the host's name.

## Hooks and calls

6. **Functions the host replaces.** A hook on a function the client runs its own version of never fires unless the
   stub that calls that version is hooked too (lifted_engine.cpp). *Resilient form, mostly in place:* the 30 s
   "hook calls / not called" line. Add: a hook that has never fired a minute into play is logged as such, with the
   feature it serves, so a new client version shows at once which features went quiet.
7a. **What a replaced function does with what the mod hands it.** Shots from the muzzle answer the fire routine's
   barrel-marker query with the muzzle's position and the line to the target. Reclaimer 0.10's own fire routine takes
   the direction from that answer but starts the round at the eye, so rounds flew the muzzle's line from the eye
   (0.9 m off with the head 0.4 m from the body). *Now:* under 0.10 the shot is aimed from the eye. *Resilient form:*
   a witness on each fire that compares where the round spawned with the origin the mod handed over (the dev SHOT
   IMPACT witness already measures it), and that falls back to the eye's line by itself when they disagree; and,
   longer term, a hook on the host's own fire routine so the muzzle origin is honoured again.
7. **Calling a hooked function from outside its hook.** The lifted layer ran a hook's original only inside that hook,
   so the aim's ray trace silently returned "miss". *Done:* it now runs as any game call. *Keep:* every refused call
   is logged with the function's address.

## Evidence the host keeps

9. **The host's own log is overwritten on every launch.** Reclaimer's `client.log` (when it hands input to the game or
   to its menus, which device it reads, its pause menu) explained the dead controls, but the next launch replaced it,
   so a later report (a few seconds without controls after Alt+Tab) had no client side to read. *Resilient form:* at
   the mod's teardown, copy the host's log beside HaloMCCVR.log under the run's start time, keeping the last few.

## Recognising the host

8. **Window class and executable names.** Reclaimer's window class changed (ReclaimerStandaloneClient ->
   H3RecompHost) and its 0.10 build ships under a 0.9-style file name. *Resilient form:* recognise the host by its
   version resource and by behaviour, not by names; keep the FA scripts' window lookups in one place.
