# VR action bindings: September 23 candidate

The intended Halo 3 reference behavior is a consistent controller action across
all titles and MCC layouts, with per-game VR overrides and explicit Unbound.
This candidate introduces a semantic VR source layer and resolves each action's
current native gamepad transport from verified, read-only title state. It removes
the hard-coded Reach trigger/X exchange. This is not yet a complete native action
override: native actions sharing a transport can still occur together.

## Independent title evidence

`tools/verify-vr-action-identities.py` checks 59 named UI-glyph-to-action chains
in the official H3, ODST, Reach, H4 and H2 editing kits and preserved decompiles.
`tools/re/verify_ce_controller_mapping.py` separately checks CE's pinned kit and
retail hashes, 66 names, 28 masks and five consumers. CE facts are recorded in
`CE-CONTROLLER-MAPPING-EVIDENCE-2026-09-23.md`.

`tools/verify-gesture-melee-bindings.py` checks six pinned retail identities,
12 unique reader/state signatures, six state pointers and each kit's own
transport table. The production decoder is `gesture_binding_reader.inl`;
`gesture_melee_bindings.inl` publishes a sequence-guarded coherent snapshot on
the worker. Input consumption requires matching title/generation, controller
ownership and a snapshot no older than 150 ms. Missing mappings neutralize VR
actions until a verified map is available; there is no guessed raw-button fallback
inside semantic gameplay. Native menus and physical gamepads retain native input.

The decoder fixture exercises all six titles, every relevant action, all sixteen
gamepad inputs and four controllers, malformed or unbound entries, rejected H2
held/axis bindings, guarded inaccessible memory and controller ownership. The
initial implementation passes 31,233 production checks. The separate mapping
fixture covers per-game save/load, overrides, Unbound, profile changes, held input,
flashlight suppression and nonfinite controller data. Final candidate test results
must come from the combined Release build, not these intermediate counts.

## Controller and interaction behavior

OpenXR interaction-profile events identify Touch-compatible, Index, Vive, WMR
and simple controllers; unknown profiles are labeled as an OpenXR fallback.
Vive/WMR upper/lower trackpad clicks provide equivalent face actions and latch
their zone until release. The documented component paths were checked against
the [Khronos OpenXR registry](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/registry/xr.xml).
Runtime profile advertisement is not proof of a physical headset model.

The VR Mappings page stores thirteen action choices separately for all six
titles. Defaults share one semantic table. Values distinguish Automatic from
Unbound. Title/profile changes, reconnect, pause and remapping require held
sources to release before activating new actions. Gesture reload/swap resolve
their semantic native transport independently of the player's button override,
so unbinding a button does not intentionally disable the physical gesture.

Two-hand grip acquisition remains based on tracked grips. Magazine/holster
gesture ownership consumes only its claimed sources. Holding the weapon grip
after completing a holster draw no longer blocks support acquisition. Flashlight
suppression uses the current support-latch transition and drains a suppressed
held flashlight source until release. Previous legacy button selectors remain
readable in existing configurations for the explicit legacy-routing fallback;
they are removed from the normal menu.

## Limits that must stay visible

- **Native aliases:** removing an action's VR source cannot prevent that native
  action from also responding to a transport emitted for another action. Reload
  and Use can share a native action itself. This prevents claiming full IN02,
  IN03 or RV04 independence from native layouts. No native binding tables are
  rewritten and no unproven engine action hooks are enabled.
- Native mappings with no verified gamepad transport remain unavailable. The
  menu reports native availability rather than inventing a button.
- Controller-family mapping, inversion, analogue vehicle controls, simultaneous
  gestures, co-op and headset usability still require runtime validation. These
  tests do not establish that every title/controller combination is accepted.

The accepted build pointer remains unchanged. This document is implementation
evidence and an explicit limitation record, not headset acceptance.
