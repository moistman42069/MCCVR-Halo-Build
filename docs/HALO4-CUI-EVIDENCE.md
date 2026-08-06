# Halo 4 CUI evidence

Status: **fresh research track, measured facts only, no draw-path evidence
yet.** Halo 4 has no CHUD; its HUD is the CUI system. This file is where CUI
findings are recorded before any HUD/crosshair candidate exists. Identity
pins live in `docs/HALO4-EVIDENCE-MANIFEST.json`; camera/render signature
proofs live in `docs/HALO4-SIGNATURE-EVIDENCE.md`.

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
  reticle art; what composes them on screen is not yet evidenced.
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
   it in a headset. This is positive support for the plan of record:
   procedural VR reticle first, capture only if later proven.
2. **`reticule_offset_container` means the reticle is authored to be
   offsettable**, which is the natural attachment point if a captured or
   native reticle ever has to ride a controller ray instead of screen centre.
   What drives that offset is not yet measured.

`base_hud.cui_screen` (507 KB) and `mc_hud.cui_screen` (1.3 MB) also parse
after repair; their internals and the draw-order question below are the next
CUI steps.

## Research plan (not findings)

Ordered, per the approved bring-up plan; each step produces exports or
measurements that get recorded above as facts:

1. Export the 1 `user_interface_hud_globals_definition`, 9
   `cui_static_data`, and 2 `cui_logic` tags via `tool.exe`
   export-tag-to-xml.
2. Census the 409 `cui_screen` names for reticle/crosshair/in-game-HUD
   candidates (the `tags\ui\cui\in_game` subtree first).
3. Recover the CUI tag schemas via ManagedBlam/Corinth reflection.
4. Settle what the `chud_debug_crosshair` debug var actually gates in this
   engine (candidate cheapest suppression anchor — currently unproven).
5. Draw-order call-graph from `cui_render_view.cpp` symbols: is CUI drawn
   per-eye inside the player view, or as a separate compositing pass? That
   answer decides capture-vs-suppress+procedural-reticle, and it must be in
   the ledger before any HUD candidate.

The M3 plan of record: procedural VR reticle first (ODST precedent — a
sanctioned, stated difference, not a silent fallback); CUI capture research
runs in parallel and only ever replaces the procedural reticle from proven
evidence.
