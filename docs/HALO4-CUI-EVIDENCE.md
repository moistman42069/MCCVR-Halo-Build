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
