# Halo 4 CUI evidence

Status: **C-H4-43i through C-H4-43p are headset-rejected. C-H4-44 carries
C-H4-43q's return to
the Halo 3/ODST/Reach presentation architecture: captured authored pixels on
the already-correct OpenXR reticle quad, with the flat native copy hidden.** Halo 4 has no CHUD; its HUD is the
CUI system. Identity pins live in `docs/HALO4-EVIDENCE-MANIFEST.json`;
camera/render signature proofs live in `docs/HALO4-SIGNATURE-EVIDENCE.md`.
Nothing in this file promotes C-H4-43i through C-H4-44 to an accepted headset result.

The 2026-08-11 Steam/SteamVR/PSVR2 run loaded the correct `3baabc7` bytes but
logged `prepared capture/discard resources unavailable`, followed by zero main
gameplay CUI passes, zero begin markers, zero completed redirects and zero
uploads. The crosshair swapchain had been created immediately beforehand. The
candidate's eager validation of every XR swapchain RTV therefore prevented the
optional hooks from installing and produced exactly the unchanged view the
user reported. This is a runtime rejection of 43i's install gate, not negative
evidence against the pinned CUI boundary.

## Measured facts (2026-08-06, from the pinned H4EK)

**Tag census** (`bin\!public_tags.txt`, 85,634 lines, re-counted this
session):

| Tag class | Count |
| --- | --- |
| `chud_definition` | 0 |
| `chud_globals_definition` | 0 |
| any `chud_*` class at all | 0 |
| `cui_screen` | 409 |
| `cui_static_data` | 9 |
| `cui_logic` | 2 |
| `user_interface_hud_globals_definition` | 1 |

Consequence: every CHUD finding from Halo 3, ODST, and Reach — classes,
scripting-class byte, `chud_draw_widget`, capture points — is inapplicable.
Whatever surviving `chud_*` symbols exist in the kit binaries must be treated
as Megalo/navpoint/debug-var leftovers until proven otherwise.

**Extracted tag tree** (post-extraction census, 88,142 files):

- `tags\ui\chud\` exists but contains only 5 `.bitmap` + 1
  `.multilingual_unicode_string_list` — no HUD definition tags. The name is
  a leftover; it is not evidence of a CHUD system.
- `tags\ui\hud\` is the HUD art/content tree: `common`, `devices`,
  `equipment`, `game_mode`, `grenades`, `image`, `main`, `navpoints`,
  `ordenance`, `player_huds`, `reticles`, `toasts`, `turrets`, `vehicles`,
  `weapons`.
- `tags\ui\hud\reticles\` holds exactly 5 bitmaps: `ar_corner`, `dmr_cross`,
  `magnum_circle`, `magnum_quarter_circle`, `forge_reticles`. These are raw
  reticle art. The tag census alone did not reveal their draw boundary; the
  C-H4-43i H4EK command proof below now does.
- `tags\ui\cui\` is the CUI screen tree: `alert`, `common`, `in_game`,
  `lobbies`, `postgame`, `sounds`, `start_menu`, `strings`.
- `tags\ui\hud_globals.user_interface_hud_globals_definition` is the single
  hud-globals tag from the census.
- `cui_render_view.cpp` is among `halo4_tag_test.exe`'s retained source
  names.

**cui_screen census** (409 names saved to
`out/h4ek-evidence/cui/cui_screen_census.txt`; measured 2026-08-06):

- **Per-weapon HUD screens**: every weapon has
  `ui\hud\weapons\<faction>\<path>\<weapon>.cui_screen`, and scoped weapons a
  separate `<weapon>_scope.cui_screen` (e.g. `dmr.cui_screen` +
  `dmr_scope.cui_screen`). The per-weapon crosshair analog therefore lives in
  the weapon's own screen, not a central crosshair collection.
- **Root/in-game composition candidates**: `ui\hud\main\main.cui_screen` (+
  the exported `main.cui_logic`), `ui\hud\player_huds\shared\base_hud.cui_screen`,
  `ui\hud\player_huds\mc_hud\mc_hud.cui_screen` (campaign),
  `mp_hud`, `forge_hud`, and per-game-engine screens
  (`...\game_engines\campaign\campaign.cui_screen` etc.).
- **`ui\hud\player_huds\shared\curve_template\hud_curve_global.cui_screen`**
  exists — a named curvature construct for the later HUD-layout milestone.
- **Vehicle/turret screens** per vehicle seat family
  (`ui\hud\vehicles\warthog\warthog_driver.cui_screen`, `banshee`, `mantis`,
  `scorpion_cannon`, `ui\hud\turrets\...`), relevant to M5.
- 12 tag XML exports (9 `cui_static_data`, 2 `cui_logic`, 1
  `user_interface_hud_globals_definition`) are in `out/h4ek-evidence/cui/`,
  all non-empty; the working `tool.exe` convention is an **absolute tag path
  under the kit's `tags` root** with cwd at the kit root.
- **`tool.exe`'s XML has two distinct defects, both mechanically repairable.**
  Measured across the full 18-tag `export_h4_kit.ps1` run, then corrected —
  an earlier version of this section claimed 11 exports were unusable, which
  was wrong, and the error is worth recording because the two defects look
  like one:
  1. **Encoding.** `tool.exe` writes raw tag bytes — notably the
     `FF FF FF FF` of a NONE tag reference — straight into attribute values
     under an `<?xml version="1.0"?>` declaration that names no encoding and
     therefore defaults to UTF-8. All 11 affected exports are consequently
     invalid UTF-8. This is why `XmlDocument.Load` (which reads **bytes** and
     honours the declared encoding) rejected all 11, while parsing the same
     bytes as an already-decoded **string** accepted 8 of them. That gap is
     an encoding artefact, not malformed markup.
  2. **Unescaped ampersands.** Authored string content is written into
     attribute values without XML escaping, so a HaloScript-style expression
     appears literally as `value="a&&b"`. That *is* malformed markup, and it
     is the genuine fault in exactly 3 of the 18 —
     `scoreboard.cui_logic` (2 bare `&`), `base_hud.cui_screen` (2), and
     `mc_hud.cui_screen` (4).
  `tools/export_h4_kit.ps1` now repairs both — declaring `iso-8859-1` and
  escaping only ampersands that do not already open a valid entity — and
  **all 18 exports parse, zero quarantined**. Every repair is reported and
  the untouched `tool.exe` bytes are preserved beside the repaired file as
  `.xml.orig`, so nothing is silently rewritten.

## Measured from the repaired exports (2026-08-06)

These come from `out/h4-kit-source/canonical/`, produced by
`tools/export_h4_kit.ps1` and readable only after the two XML defects above
were repaired. Plan steps 1 and 2 are now partly discharged.

### `ui\hud_globals` — the single hud-globals tag (70 fields)

VR-relevant fields, quoted with their authored values:

- **`screen transform basis`** — an array of exactly **9 `real point 2d`
  elements**, a 3x3 grid: `(-1,-1) (-0.98,0) (-1,1)` / `(0,-0.92) (0,0)
  (0,0.92)` / `(1,-1) (0.98,0) (1,1)`. The mid-edge points are pulled inward
  (0.98 and 0.92 rather than 1.0), i.e. this is Halo 4's authored HUD screen
  **warp**. It is the construct a flat VR HUD would need to neutralise, and
  the functional counterpart of Reach's curvature records.
- **`Reticule maximum spread angle` = 1** — a reticle global living in
  hud_globals rather than in a per-weapon screen. Relevant to M3.
- High-contrast HUD levers, matching the `high_contrast_hud_*` debug globals
  found in E-H4-2: `High Contrast Flags`, `Minimum Threshold` 0.05,
  `Maximum Threshold` 0.41, `Clamp Threshold` 0.5, `Darken Factor` 0.75,
  `Brighten Factor` 1.25.
- First-person damage overlay: `tiled mesh seen when hit in 1st person` →
  `ui\hud\player_huds\shared\damage_flash\microtexture`, with
  `number of tiles across the screen` 35 and four mesh-alpha reals — a
  screen-space overlay worth knowing about for VR comfort.
- Radar/detection ranges (`vehicle radar range` 100, `remote sensor range`
  7.62, the height-classification pair +/-3.28) — note 3.28 and 7.62 are
  foot/metre-flavoured constants; do **not** infer a world-scale factor from
  them without its own proof.

### `ui\hud\player_huds\shared\curve_template\hud_curve_global` (cui_screen)

The curvature construct is a CUI widget, not a flat record:

- Component type **`curvature_container_widget`**, instantiated as
  `component_[visual]_[curvature_container_widget]_[0]`.
- **Nine named per-point properties**, matching the nine-element basis above:
  `prop_curvature_point_top_left_y`, `prop_curvature_point_top_middle_x`,
  `prop_curvature_point_top_middle_y`, `prop_curvature_point_top_right_y`,
  `prop_curvature_point_center_left_y`,
  `prop_curvature_point_bottom_left_y`,
  `prop_curvature_point_bottom_middle_x`,
  `prop_curvature_point_bottom_middle_y`,
  `prop_curvature_point_bottom_right_y`.
- **Six resolution classes**, each appearing twice:
  `resolution_widescreen`, `resolution_widescreen_half`,
  `resolution_widescreen_quarter`, `resolution_standard`,
  `resolution_standard_half`, `resolution_standard_quarter`. One theme:
  `theme_default` (12 occurrences).
- Also present: a **`parallax_component`** named
  `metrics_parallax_listner` with `metrics_parallax_x_expression` /
  `metrics_parallax_y_expression` — HUD parallax driven by expressions.

**Warning recorded in advance, because this is a known way to lose hours.**
`AGENTS.md` and the Reach record both describe the failure where a write is
verified correct yet has no visible effect because the engine reads a
*different copy* of that data, selected at runtime by resolution class or
skin — Reach's HUD sliders appeared inert for exactly that reason. Halo 4
presents the same hazard in a more elaborate form: six resolution classes
times a theme, per curvature point. Any future HUD-layout candidate must
first establish **which resolution class and theme the live VR player view
actually resolves to**, and prove it from the runtime rather than assuming
`resolution_widescreen` because VR renders a single non-split view.

### `ui\hud\weapons\human\ar\assault_rifle` (cui_screen) — the crosshair, componentised

A per-weapon screen is a graph of typed components. The assault rifle's
reticle surface, quoted from the export's `type` fields:

- Containers: **`reticule_container`**, **`reticule_art_container`**,
  **`reticule_offset_container`**, `all_weapon_art_container`,
  `parallax_container`, `scope_container`,
  `unique_weapon_positioning_container`.
- Art states: **`unscoped_art`**, **`scoped_art`**, **`hit_art`**,
  `hit_indicator_art`, **`headshot_dot`**, `headshot_art`, plus
  **`reticule_art_color`** and 8 `bitmap_widget` leaves.
- Data feeds: **`reticule_base_data_reader`** and
  **`reticule_spread_data_reader`**, alongside `weapon_data_reader`,
  `view_data_reader`, `player_weapon_data_provider`.
- Ammo/affiliation logic, and a `rampancy_*` family (11 components: position
  and scale jitter expressions, shearing and chromatic shaders) — Cortana's
  rampancy distortion, which is a screen-space effect worth remembering for
  VR comfort.

**Two consequences that bear directly on the M3 decision.**

1. **`reticule_spread_data_reader` is the same hazard that broke Reach.**
   Reach's crosshair vanished on damage because its five petal widgets were
   driven by weapon barrel error, bloomed outward on a hit, and left the
   magnified centre crop the capture used — the empty middle then published
   over good art (see `docs/CURRENT-STATE.md`, GitHub #70). Halo 4 feeds its
   reticle from spread data by the same design, so **any future centre-crop
   capture of the Halo 4 reticle should be expected to fail the same way**,
   and must be designed against that from the start rather than discovering
   it in a headset. C-H4-43i therefore starts at a neutral 1x capture mapping,
   retains the procedural reticle until nonblank authored pixels are proven,
   and uses the shared coverage hold. Those mitigations are not a substitute
   for the pending Halo 4 headset test.
2. **`reticule_offset_container` means the reticle is authored to be
   offsettable**, which is the natural attachment point if a captured or
   native reticle ever has to ride a controller ray instead of screen centre.
   The H4EK proof below resolves what drives it: the widget serialises paired
   CUI commands that push and pop a renderer transform around its child
   command stream. It does not itself issue GPU draws.

`base_hud.cui_screen` (507 KB) and `mc_hud.cui_screen` (1.3 MB) also parse
after repair. Their broader HUD-layout internals remain separate work; the
authored-reticle draw-order question is resolved below.

## C-H4-43i authored-reticle boundary (offline proof, 2026-08-10)

This section records static reverse-engineering proof and the feature-local
software contract. Discovery came only from the official H4EK executables and
canonical tags. The pinned stripped retail module was used only to match and
verify the already-understood H4EK functions.

### Official H4EK producer and command format

`ReticuleOffsetContainerWidget` is a derived CUI container, not a CHUD widget.
In `halo4_tag_test.exe`, its RTTI type descriptor is at RVA `0x2686BC0`, its
complete-object locator is at `0x1FDEF58`, and its 28-slot vtable is at
`0x1C3E738`. The RTTI hierarchy is:

`ReticuleOffsetContainerWidget -> c_cui_container_widget -> c_cui_widget ->
c_cui_object_component -> c_cui_component`.

Only the destructor, slot 16 (`0xADDDB0`), and slot 27 (`0xADE020`) differ
from the base container. The independently matched slot-27 identities are:

| Official H4EK program | Vtable RVA | Slot-27 RVA |
| --- | ---: | ---: |
| `halo4_tag_test.exe` | `0x1C3E738` | `0xADE020` |
| `halo4_tag_play.exe` | `0x186FB78` | `0x8AD6E4` |
| `halo4_sapien_play.exe` | `0x1ADFF50` | `0xC160FC` |

The exact slot-27 ABI is:

```cpp
using ReticuleOffsetSlot27 = void(__fastcall*)(
    ReticuleOffsetContainerWidget* self,
    c_cui_render_context* context);
```

The tag-test body `0xADE020..0xADE0A5` does the following, in order:

1. Reads the reticle-transform ID from `[self+0x1E8]`.
2. If byte `[self+0x1EC]` is set, builds `float2 {0.0f,
   [self+0x1F0]}`; otherwise it passes no optional vector.
3. Calls `0x9B6800(context, id, optionalFloat2)`.
4. Calls virtual slot 26 (`[vtable+0xD0]`) to serialise the child subtree.
5. Calls `0x9B6390(context)`.

`0x9B6800` allocates command type `0x28` with a `0x0C`-byte payload. Payload
offset `+0` is the 32-bit transform ID and payload offset `+4` is the
eight-byte float pair. `0x9B6390` emits type `0x29` with no payload. The CUI
command header is exactly four bytes:

```cpp
struct cui_command_header {
    int16_t type;
    uint16_t payload_size;
};
```

The payload starts at header `+4`. In the official tag-test image,
`0x9B6800` has exactly one direct call, at slot 27's `0xADE072`, and
`0x9B6390` has exactly one, at `0xADE089`. The canonical assault-rifle and
magnum exports place `reticule_art_container` and `hit_indicator_art` below
this offset container while ammo art is outside it. Thus `0x28` and `0x29`
are a unique paired boundary around authored reticle descendants.

This slot is only a command-buffer producer. It performs no GPU draw and is
therefore deliberately not the runtime capture hook.

### Actual command playback boundary and ABI

The H4EK per-command executor is tag-test RVA `0x9C4690` (tag-play homolog
`0x87E54C`). Its retained source identity is
`c:\mcc\release\h4\shared\engine\source\blofeld\interface\cui\cui_render_renderer.cpp`,
beginning at source line 274. Retained asserts name `m_view`, `header`,
`(numOpenRenderSections)`, and `command->execute_func`.

Its exact machine ABI is:

```cpp
bool __fastcall execute_cui_render_command(
    void* renderer,
    const void* command_header,
    void* open_render_sections,
    void* render_context);
```

All four Microsoft x64 arguments must be preserved. The tag-test prologue
moves `r9 -> rsi`, `r8 -> rdi`, `rdx -> rbx`, and `rcx -> r15`; the semantic
return is the byte in `AL`.

- Type `0x28` reaches `0x9C47B8`, reads the ID and float pair, calls
  `0x9BC7B0` to compute the transform, then `0x9BE760` to push/apply it.
  Apply increments the renderer transform-stack count at `renderer+0x870`
  and copies a `0x34`-byte entry.
- Type `0x29` reaches `0x9C47AE` and calls `0x9BDE40` to pop the transform
  stack.
- The sole outer playback call to the executor is at tag-test `0x9C3F3F`.
  After each call, the playback loop advances by
  `sizeof(cui_command_header) + payload_size`, following buffer pages as
  required.

The important ordering fact is that `0x28`, every descendant command, and
`0x29` are separate executor invocations while renderer state persists. The
safe redirect order is consequently:

1. Run the original `0x28` exactly once so Halo 4 pushes its state.
2. Redirect the immediate D3D11 context's render target for later descendant
   command invocations.
3. Run every descendant command exactly once while that redirect is active.
4. Run the original matching `0x29` exactly once so Halo 4 pops its state.
5. Restore the saved render targets, DSV, viewports, and scissors.

The markers themselves do not draw. Their interval brackets synchronous CPU
submission of the descendant D3D11 draws, not asynchronous GPU completion.
Render-target binding at submission time is what routes the pixels; waiting
for GPU completion before restoring bindings is neither required nor correct.

### `user_interface_render` ABI and the exact gameplay scope

The official H4EK function at `0x91DD70` and retail homolog at `0x3ACD60`
have this proven detour contract:

```cpp
void __fastcall user_interface_render(
    uint32_t window_index,
    uint32_t render_buffer_channel,
    const void* viewport_bounds,
    const void* optional_profile_value,
    uint32_t render_mode,
    bool flag);
```

The first two arguments are consumed as 32-bit values; `r8` and `r9` are
64-bit pointers; argument 5 is read as a dword from entry `[rsp+0x28]`; and
argument 6 is read as a byte from entry `[rsp+0x30]`. Observed callers ignore
`RAX` and the epilogue establishes no semantic return, so the callable return
type is `void`. `window_index` is range-checked and selects a per-window
record. H4EK asserts that `viewport_bounds` is non-null. The fourth argument
optionally carries a 16-byte profiler/event value. `render_mode` and `flag`
are conservative names; their finer semantics are not needed by the hook and
are not claimed.

There are two in-wrapper calls, and they must not be conflated:

| Pass | Official H4EK call | Pinned retail call | Bounds / role |
| --- | ---: | ---: | --- |
| Auxiliary | `0x8B5D6C -> 0x91DD70` | `0x3790E9 -> 0x3ACD60` | Hard-coded `216x96`, H4EK channel 1 |
| Main gameplay HUD | `0x8B72F3 -> 0x91DD70` | `0x375C69 -> 0x3ACD60` | Full player-view bounds, H4EK channel 2 |

H4EK `0x93EDD0` indexes distinct CUI render buffers by
`window_index * 0x1038 + channel * 0x4A0`. Retail caller marshaling is not
numerically identical to H4EK, so a copied channel value is not a safe retail
gate. The candidate instead admits only the exact main-call return address,
retail RVA `0x375C6E`. The auxiliary return (`0x3790EE`) and later
menu/overlay callers never open the reticle phase.

The main playback is synchronously inside the accepted wrapper:

- H4EK: `0x1F7C00 -> 0x8B63C0 -> 0x91DD70 -> 0x9439D0 -> 0x93EDD0 ->
  0x9C1280` and the command playback/executor below it.
- Retail: wrapper `0x1222F4`, call `0x12251E -> 0x3751D0`, then
  `0x375C69 -> 0x3ACD60 -> 0x3BAED4 -> 0x3F7A7C -> 0x3F3808`, whose
  executor call is `0x3F4B7C -> 0x3F0EA4`.

Each direct call returns through this chain before `user_interface_render`
and then the wrapper return. Therefore the main CUI playback occurs once per
wrapper invocation, and C-H4-43i's two wrapper replays execute it once for
each VR eye. A separate post-eye menu/overlay bracket still exists, but it is
outside this exact return-address phase and remains stock.

### Pinned retail homolog and unique anchors

The retail image is pinned by SHA-256:

`7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84`.

The retail `ReticuleOffsetContainerWidget` vtable is at RVA `0xD6C2D0` and
its H4EK-understood slot-27 homolog is `0x4181B4`. That independently confirms
the producer mapping, but the producer remains unhooked.

The actual executor is retail RVA `0x003F0EA4`. This 24-byte entry signature
matches exactly once in the pinned module and at that RVA:

```text
48 8B C4 55 56 57 41 56 41 57 48 8D A8 B8 FC FF FF 48 81 EC 50 04 00 00
```

Its sole playback caller anchor is retail RVA `0x003F4B6B`, also unique:

```text
49 8B 8F 10 04 00 00 4D 8D 8F 20 04 00 00 49 8B D6 E8 ?? ?? ?? ??
```

The call opcode is anchor `+17` (`0x3F4B7C`), its displacement is at `+18`,
and the next instruction is anchor `+22` (`0x3F4B81`). The pinned rel32 is
`-0x3CDD` (`23 C3 FF FF`), which decodes exactly to `0x3F0EA4`. Retail type
`0x28` reaches `0x3F1EA7`, compute `0x3F21CC`, and apply `0x3F3338`; type
`0x29` performs the homologous pop inline at `0x3F0FEE`.

The main gameplay-scope function is retail RVA `0x003ACD60`. Its 31-byte
entry signature matches exactly once and at that RVA:

```text
48 8B C4 55 53 56 57 41 56 41 57 48 8D 68 B1 48 81 EC A8 00 00 00 0F 29 78 B8 44 0F 29 40 A8
```

The unique main caller anchor begins at retail RVA `0x00375C51`:

```text
8B 8E 8C 03 00 00 4C 8D 45 A0 45 33 C9 44 88 6C 24 28 33 D2 89 7C 24 20 E8 ?? ?? ?? ?? 83 FB 03
```

Its call opcode is anchor `+24` (`0x375C69`), displacement is at `+25`, and
the next instruction/return address is anchor `+29` (`0x375C6E`). Decoding
that rel32 must produce exactly `0x3ACD60`.

Installation requires all four signatures to match exactly once at their
pinned RVAs, both rel32 call edges to decode to their proven targets, all four
addresses to lie in executable committed image memory, the module mapping to
remain stable, and the capture plus private-discard resources to be prepared
on the cold path. Zero, multiple, moved, or edge-mismatched anchors refuse
only this optional reticle feature.

### Why neither slot 27 nor `hud_show_crosshair` is hooked

Slot 27 only serialises commands before playback. Redirecting a render target
there cannot bracket descendant GPU submissions, and suppressing it would
also skip Halo 4's own command/state construction. The executor is the first
proven point where the paired markers surround the actual descendant draw
submission, so it is the hook boundary.

`hud_show_crosshair` is also not a draw boundary. In H4EK, its name is at RVA
`0x198AF80`; registration at `0x2B8CD9` selects callback `0x95A320`.
`0x95A320` converts the bool, selects category 3, and tail-jumps to common
setter `0x95A270`. That setter sets or clears bit 3 of
`[UI-manager+0x2A38]` and notifies the UI through a virtual call. It is a
persistent global/category visibility mutation, not a per-eye transaction.
Turning it off can remove the source subtree before capture; leaving it on
without a redirect leaves the face-centred copy. C-H4-43i therefore does not
hook or mutate it. The old `chud_debug_crosshair` research item is retired:
Halo 4 has no CHUD and it is not this CUI boundary.

## C-H4-43i feature-local runtime contract

The two optional hooks install and remove as one feature transaction. A TLS
scope is opened around each accepted eye's wrapper replay, but the executor
admits markers only while the exact main `user_interface_render` call is
active. Ownership is checked against the current thread, renderer, generation,
prepared-frame serial, eye, exact main caller, active title, armed Halo 4
camera, and live stereo state. Nested `0x28` markers increment depth; only the
matching outer `0x29` closes the redirect.

Every original CUI command is called exactly once. The original outer `0x28`
runs before redirect; the original outer `0x29` runs before restoration. The
main-call `finally` and the eye-wrapper `finally` both force-close an unmatched
redirect, restore all saved D3D bindings on the same render thread, and
invalidate a partial authored capture rather than publishing it.

The configuration behavior is:

| `crosshair` | `kill_reticle` | Main-pass behavior |
| ---: | ---: | --- |
| `0` | either | Execute the reticle subtree into the private discard target for both eyes; submit no gun-ray quad. |
| `1` | `0` | Leave Halo 4's native face-centred reticle stock; do not submit held Halo 4 authored art on the gun ray. |
| `1` | `1` | Capture the configured first eye on a sample frame; execute the opposite eye into private discard; execute the selected eye into discard on cadence-skipped frames; submit validated authored art on the gun ray. |

The configured first eye is eye 1 when `right_eye_first=1`, otherwise eye 0.
Only that eye can publish. The opposite eye uses a different discard target,
so it cannot alpha-accumulate a second copy into the same capture serial and
cannot leave a second native face copy behind.

Until a nonblank authored image has passed validation and reached the reticle
swapchain, Halo 4 deliberately keeps the procedural gun-ray pixels as a
bootstrap. Before a redirect begins, an unreadable or unrelated command and
an unowned phase stay stock. Unavailable resources, redirect failure,
signature mismatch, hook failure, or partial cleanup also retain the
feature-local stock/procedural fallback and are logged. If a command stream
becomes malformed after redirect begins, the enclosing `finally` restores
state and invalidates the partial art; it does not replay the subtree or drop
the stereo frame. The optional lifecycle states are `StockFallback`,
`CleanupRequired`, and `Installed`. None disarms the camera or hands or ends
the OpenXR session. Hot callbacks do no allocation, signature scan, file I/O,
or logging.

### Fixed identity, cadence, and coverage caveat

The `0x28` payload ID is a renderer transform ID, not a proven weapon or art
identity. C-H4-43i therefore publishes the fixed nonzero key `1`; zero cannot
admit an authored upload. Halo 4 uses the shared `BoundedAnimation` policy.
The default `crosshair_animation_frames=6` samples and publishes at most every
six prepared frames; nonzero configuration is clamped to `6..60`. A configured
zero disables the normal animation cadence but deliberately re-samples every
30 frames so a weapon swap can eventually replace held art. Before any valid
authored art is held, bootstrap sampling is not skipped.

Halo 4 starts with a neutral 1x capture viewport. Halo 3/ODST's 4x and Reach's
2x mappings are title-specific and are not copied. The asynchronous alpha
coverage guard never publishes a blank capture. For the same identity it also
requires at least half the held ink, with an escape after 24 consecutive
qualifying nonblank reductions. Because Halo 4 deliberately uses one fixed
identity, a legitimate high-ink to low-ink weapon change looks like a thinning
of the same reticle and can be held for roughly 24 samples: about 144 prepared
frames at the default cadence or 720 with animation disabled. This is a
bounded mitigation, not proof that the player-facing cadence or crop is right.

## Validation status and required headset result

The C-H4-43i candidate builds, its automated tests pass, and the repository
gate passes with the gameplay-only phase described above. Those are software
checks, not runtime acceptance. The static proof establishes the producer,
command meanings, exact ABIs, synchronous draw-submission interval, per-eye
main-pass scope, retail signatures, and fail-open ownership contract. It does
**not** establish that a real gameplay run emits every desired reticle state
inside the measured container, that its pixels fit the neutral 1x viewport,
or that the result is comfortable and correct in a headset.

C-H4-43j was rejected by the Steam/SteamVR/PSVR2 90 Hz headset run. The hooks
were live and balanced, but three independent marker scopes per eye redirected
shared HUD pixels while every private reticle capture remained blank. This
runtime result disproves the static assumption that the marker interval is a
GPU draw-submission boundary. The render-target redirect is dormant; the
following list is retained as the acceptance contract for a replacement:

### C-H4-43k/43l rejection and C-H4-43m native-transform correction

C-H4-43k did not redirect or capture any CUI draw. H4EK `0x9BE760` and its
retail homolog prove that a successful type-`0x28` command increments the
renderer stack count at `+0x870` and composes one `0x34`-byte
`real_matrix4x3` entry beginning at `+0x878`; the entry's final float3
translation begins at `+0x28`. The matching type `0x29` restores the prior
entry. The optional dispatcher hook therefore calls the original type `0x28`
first and changes only X/Y in that newly pushed translation. It never changes
an RTV, DSV, viewport, scissor, bitmap, colour, alpha, visibility bit, or CUI
command. The exact gameplay-front-end scope still excludes the 216x96 auxiliary
pass and later menus.

The desired normalized projection is derived for each eye from the engine's own
steered aim direction and the finished eye camera/FOV read back before the
wrapper. The 43k headset log measured changing offsets but a fixed native
reticle: values such as `0.092/-0.072` were added directly to the matrix's
measured pixel-space centre `-1893.000/1064.517`, so the movement was sub-pixel.
43l maps normalized X/Y through `abs(baseX)/abs(baseY)`, the live half extents
in the exact same reticle transform. NDC `+1` therefore moves one measured half
width, and NDC `-1` one measured half height, independent of resolution and
aspect ratio.
The correct `20fc086` 43l Steam/SteamVR/PSVR2 run then proved the architecture
in the headset: the whole HUD remained, and the native animated/target-coloured
reticle moved with the gun. It also rejected the final mapping because vertical
movement was inverted and the reticle was much too large. The `+0x28` composed
translation therefore consumes positive Y as headset/camera up; the prior
"raster Y points down" inference was wrong at this already-composed matrix.

For size, the official H4EK `tool.exe` exports of canonical assault-rifle and
magnum `cui_screen` tags both measure `_auto_prop_height = 81.92` in the
widescreen reticle overlay (`out/h4ek-evidence/cui-reticle-size/`). 43m treats
that title-authored height as the nominal reticle span. For each eye it derives
the desired pixel height without a copied resolution or guessed multiplier:

`pixels = 2 * abs(liveCuiHalfHeight) * tan(crosshair_size_deg/2) / tan(eyeHalfFovY)`

and writes `real_matrix4x3.scale = pixels / 81.92`. The matrix layout itself is
already H4EK/retail-proven: uniform scale is float zero and translation begins
at `+0x28`. One guarded `0x34`-byte write returns the untouched nine-float
basis with only uniform scale and X/Y translation changed. Invalid FOV,
configuration, scale, matrix, or memory leaves that reticle draw stock and the
procedural fallback eligible; no camera/session ownership changes.

Both eyes move and size their native reticle independently. When both current
eyes prove the matrix write, the compositor does not submit the procedural
reticle quad. Thus Halo 4's existing animation, spread, hit marker, and
red/green target state remain native while the face-centred copy is the same
object moved onto the gun ray. This is a new headset candidate, not an accepted
result; C-H4-43 remains the rollback pointer.

- the authored weapon reticle follows the controller/gun ray and no
  face-centred copy remains in either eye;
- `crosshair=0`, `crosshair=1/kill_reticle=0`, and
  `crosshair=1/kill_reticle=1` match the table above;
- fire kick, target/hit colour, spread/bloom, and damage do not blank or
  replace good art;
- a high-ink to low-ink weapon swap converges acceptably under the fixed-key
  coverage hold;
- scoped/zoomed weapons either retain their correct native scope behavior or
  expose a clearly logged, feature-local limitation;
- telemetry reports admitted main CUI passes, completed matrix writes, no
  write failures, an aim Y sign matching gun movement, and a finite stock to
  configured scale change.

C-H4-43m changes no shared compositor code and no Halo 3/ODST/Reach path; their
reticle implementation and lifecycle remain byte-for-byte untouched. Until the
explicit Halo 4 headset result exists, the accepted-build pointer in
`docs/CURRENT-STATE.md` remains authoritative.

### C-H4-43n through 43p rejection and C-H4-43q presentation correction

C-H4-43n corrected the live viewport mapping; C-H4-43o reconstructed a finite
controller/head/Halo-camera target; C-H4-43p shared the hidden quad's final
LOCAL-space 3D point and reprojected it into each Halo 4 eye. All three were
headset-rejected. The `2f2b072` 43p Steam/SteamVR/PSVR2 90 Hz log proved the
hooks and matrix writes live, but the result was still not the already-correct
hidden VR crosshair. Reprojecting any point into CUI remains a second placement
implementation and is now rejected as an architecture.

The accepted Halo 3, ODST, and Reach architecture does not move native HUD art
to an independently calculated 2D coordinate. It captures the game's authored
crosshair pixels into `g_authoredReticleTexture`, suppresses the flat native
copy, uploads those pixels to `g_reticleChain`, and lets `reticleQuad` own the
one working controller-ray pose. C-H4-43q does exactly that for Halo 4.

The 43j headset result remains important: redirecting only from type `0x28` to
the matching `0x29` produced blank art and removed shared HUD pixels because
Halo 4 batches actual draws past that logical interval. 43q replays the bounded
capture-eye gameplay CUI pass. Its dispatcher binds the centred private capture
before the first command and keeps it bound until the exact gameplay front end
returns, crossing the proven batching boundary. The capture viewport is the
existing title-neutral 512x512 centred crop: outer HUD falls outside it while
the authored reticle and hit art remain at its centre. The subsequent normal
eye pass runs every original command and changes only each type-`0x28` reticle
translation enough to hide the flat copy. `kill_reticle=0` remains the stock
face-centred escape hatch.

The shared compositor is not given a new position and is not modified. Halo 4
only reports that it now captures authored art, allowing the existing upload,
coverage guard, procedural bootstrap, angular-size, distance, stabilization,
and quad placement paths to operate exactly as they do for the other titles.
C-H4-43 remains the accepted pointer pending a headset result.

### C-H4-44 Ghidra replay verification and native HUD layout

Before the 43q headset test, Ghidra 12.1.2 targeted the official
`halo4_tag_test.exe` (SHA-256
`B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6`).
The decompile independently reconfirmed the relevant state transitions:

- `user_interface_render` at H4EK RVA `0x91DD70` reaches `0x9439D0`, then
  render-buffer selector `0x93EDD0`.
- `0x93EDD0` indexes `window * 0x1038 + channel * 0x4A0`, atomically removes
  the pending buffer at `+0x490`, promotes it to active `+0x498`, and calls
  playback `0x9C1280` from the retained active buffer. If no new pending buffer
  exists, the retained active buffer remains and is drawn again. Thus 43q's
  capture call followed by its normal call replays the same authored command
  buffer; it does not advance UI logic or construct another reticle position.
- Reticle transform helper `0x9BE760` increments the renderer stack count at
  `+0x870` and copies one `0x34`-byte transform into entries beginning at
  `+0x878`. This independently agrees with the retail stack shape used only to
  hide the native flat type-`0x28` copy.

C-H4-44 then adds the requested HUD configuration through a separate data
feature. Official H4EK has one `user_interface_hud_globals_definition`, whose
`screen transform basis` is nine contiguous `real point 2d` values at offset
`0xFD6` in
`tags\ui\hud_globals.user_interface_hud_globals_definition`. The exact bytes
are bracketed by the documented damage-mesh values (`35`, `0.6`, `3`, `0.04`,
`1`) and the reticle-spread/high-contrast fields (`1`, flags `0`, `0.05`,
`0.41`, `0.5`, `0.75`, `1.25`). The tag reference between them is wildcarded
because it relocates; every semantic float and flag is exact.

The cold locator requires exactly one complete H4EK payload in committed
private or mapped memory and makes at most three attempts per level. It never
runs in a render or CUI hook. The Present-side writer runs only on a config/FOV
change plus a one-second integrity check. It maps the shared controls onto Halo
4's own construct: `hud_size` and `hud_aspect` scale the basis,
`hud_vertical_offset` translates its 720-unit virtual screen, and
`hud_curvature` interpolates from identity at `0`, through the authored H4 warp
at `0.5`, to twice the authored bow at `1`. Exit restores the authored basis.
Any zero/multiple match, changed immutable neighbor, non-finite value, or write
failure disables only HUD layout and logs `H4HUD`; camera, hands, authored
reticle, stereo, and OpenXR remain independent.

## C-H4-43j correction after the 43i headset rejection

C-H4-43i required all OpenXR crosshair swapchain images to have D3D render
target views before installing either CUI hook. That was unnecessary: the CUI
capture and suppression paths bind only their private authored/discard targets.
An XR image view is consumed later, after upload acquires one exact image. The
Steam runtime refused at least one eager view and 43i then permanently rejected
the whole title generation, producing zero hook traffic.

C-H4-43j removes only that false dependency and retains the accepted
Halo 3/ODST lazy-view shape. A resource-preparation miss is transient state,
not signature failure: the optional feature stays stock and retries at a
bounded 500 ms worker cadence. Static signature, edge, executable-range, mapping, hook-creation and
hook-enable failures still reject the generation. The HUD pixels themselves
remain unmodified, so Halo 4's native target/friendly colour state is captured
with the reticle rather than recreated procedurally.
