# Head Turn Sway Correction removal (product, 2026-09-27)

Status: **the feature is removed from the product surface. Source-level
removal is implemented and covered by the offline suites; this exact packaged
build has not been headset-accepted. The headset result remains the gate, and
this change does not move `docs/CURRENT-STATE.md`.**

Relationship to the aim-continuity work: this is a separate, additional change
on top of the accepted Virtual Stock grab/release aim-continuity candidate
(`docs/VIRTUAL-STOCK-AIM-CONTINUITY-2026-09-27.md`). The continuity layer is
untouched here.

## 1. Why it was retired

"Head Turn Sway Correction" resolved the product Virtual Stock path into the
mature fixed-neutral ("NK100") inverse-neck model. After the headset A/B and
the telemetry investigation, the frozen-neutral inverse-neck correction
produced a persistent, physical-yaw-dependent aim bias while the stock was
engaged. With the correction off, the bias disappeared - user-confirmed. The
correction therefore must not be reachable from any product configuration.

## 2. What the product does now

- Shipped Virtual Stock - Standard rear references 0/1/2 and Plus rear
  reference 3 - always resolves with inverse-neck disabled and zeroed knobs.
  That is the former `virtual_stock_head_turn_sway_correction = 0` path.
- Standard and Plus use the raw/coherent HMD positional geometry they already
  used with the correction off.
- No replacement neck/body model is applied, and no neutral recapture or
  recentre salvage was added.

## 3. Not solved by this change

The separate divergence between physical HMD yaw and character-body yaw
(avatar facing) is **not** addressed here. This change only removes the
correction; it does not imply that body orientation is solved. That work
remains open.

## 4. Removed from the product

- The F1 checkbox "Head-turn sway correction" and its description.
- The config option: `Config` member, default constant, parser branch, generated
  comment and emission, and the reset-to-default assignment.
- The product resolver mapping that activated the NK fields, and the four
  NK100 product constants.
- Nothing else: an old `virtual_stock_head_turn_sway_correction` key in an
  existing `halomccvr.cfg` now falls through the generic unknown-key
  log-and-ignore tail and is never re-emitted.

## 5. Retained as isolated diagnostic machinery

The low-level capability is unchanged and unreachable from product paths:

- the inverse-neck solver fields and `EvaluateHybridInverseNeck`;
- the diagnostic test-profile registry (A-N plus the AB0/AB1 experiment
  profiles) and the explicit `HybridDiagnosticOverride` path;
- the neutral-capture state machine, `VirtualStockHeadTurnCorrectionFamilyActive`
  and the `vr.h` setters.

Product resolution can never set `hybridInverseNeckEnabled`, so the capture
family stays inactive for every product configuration; only an explicit
diagnostic profile can activate it.

## 6. Verification

Offline evidence for this change (local, pre-headset): the release build, the
full CTest suite, the aim-continuity lifecycle guard, the Reach FP parity gate
and a literal byte scan of the staged DLL for the removed strings. See the
candidate directory produced by this pass for exact commands, counts and
hashes. Absence from stripped symbols is not used as evidence.
