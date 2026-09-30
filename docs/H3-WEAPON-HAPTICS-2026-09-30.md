# Halo 3 authored weapon recoil haptics — 2026-09-30

## Result

The H3 adapter captures two distinct kinds of feedback. Admitted shots enter
through the networking `weapon-fire` event and ordinary trigger-fire path;
charging/held buzz enters through the weapon trigger-update function and its
two exact direct-effect call sites. Both establish the currently updated local
weapon's slot before capturing. Shot effects additionally require the
owner-side damage-response wrapper. Passenger effects and unrelated player
effects remain on the stock bilateral haptic path. The original native effect
worker still runs; after it writes an authored recoil/charge record, the
adapter copies that envelope to a private voice and retires only the verified
new queue record.

This is implemented and covered by production-body fixtures plus a pinned
retail verifier. It has not yet received a production DLL build or headset
acceptance. The old trigger-update/charging caller scope has been removed from
the capture contract. ODST has separate title-specific evidence and code.

## Shot and ownership evidence

- H3EK's admitted networking `weapon-fire` branch calls `A86110(weapon,
  barrel, flags)` only after its failure branch has returned. Retail
  `halo3.dll+0x914A3` calls `0x366858`, returning at `0x914A8`.
- The H3EK ordinary trigger update also calls `A86110` for the admitted shot.
  Retail `halo3.dll+0x361533` calls `0x366858`, returning at `0x361538`.
- The H3EK trigger updater also emits non-shot charging/held response effects.
  Its retail homolog is `halo3.dll+0x35F828`, which receives the full weapon
  datum. The two verified direct player-effect calls at `0x36076D` and
  `0x360913` return at `0x360772` and `0x360918`. A separate TLS scope accepts
  only these exact call returns and only when the updated weapon resolves to a
  currently equipped local primary or secondary slot. This scope nests safely
  with admitted-shot capture in the same update function.
- The retail shot routine `0x366858` matches the H3EK `A86110` ABI and
  owner/passenger behavior. Its owner branch calls `0x35C864` at `0x3673FF`
  (return `0x367404`); its passenger branch calls the same wrapper at
  `0x3674CA` (return `0x3674CF`).
- H3EK `A5C2C0` and retail `0x35C864` are the damage-effect wrapper. On the
  `drdf` damage-response path it calls the generic player-effect dispatcher
  `0x1B370C` at `0x35C9FC` (return `0x35CA01`). The capture scope is admitted
  only from the owner wrapper call site, so a passenger-side copy cannot be
  assigned to the local controller.
- Local owner and weapon-slot identity comes from the existing H3 native
  inventory reader. Primary maps to the dominant hand; secondary maps to the
  support hand. Invalid ownership, failed optional reads, and unmatched
  callers leave native feedback in place.

The pinned call edges and unique signatures are in
`src/common/halo3_haptic_contract.h`. H3EK and retail decompilation/assembly
captures are preserved in `out/h3-haptic-success-shot-kit-20260930.txt`,
`out/h3-haptic-root-shot-20260930.txt`, and
`out/h3-haptic-root-wrappers-20260930.txt`.

## Native envelope and lifecycle

The original effect worker continues to process visuals and other effects.
The adapter snapshots the local user's 0x98-byte TLS queue before the native
worker, then checks ownership, role, generation, queue identity, the selected
record change, full salted datum, row, scale, zero age, and unchanged neighboring
records afterward. It captures the exact authored envelope and writes the
verified native `UINT32_MAX` NONE datum only to that newly written slot. If any
check fails, native feedback remains untouched.

The haptic evaluator uses 0xC0-byte rows and two bands at offsets `+0x5C` and
`+0x74`. Each band has a duration and opaque 20-byte curve mapping. The adapter
retains the mapping bytes exactly and evaluates them through the pinned native
curve function; it does not reinterpret opaque fields as floats. A private
voice retires after 100 ms without a native evaluator update, on pause/rumble
skip, role change, generation change, or source-token invalidation. Pause does
not revive stale recoil on resume. Shared haptic routing separately admits a
coupled support-hand pulse only when current two-hand aiming is valid and there
is no secondary weapon; the legacy two-hand mode remains supported.

## Verification and limits

- `halomccvr_halo3_weapon_haptics_tests` covers curve sampling, finite guards,
  opaque mapping preservation, band mix, native NONE value, and voice
  selection.
- `halomccvr_halo3_weapon_haptics_backend` drives the production detour bodies,
  including admitted network and trigger shots, both local charge-response
  call sites, unadmitted/dry-fire and unrelated-effect fallback, primary and
  secondary role mapping, passenger and mismatched-owner fallback, queue
  retirement, source-token expiry, optional-read fault isolation, stock-worker
  exception propagation, and retry after cleanup failure. It reports the
  executed assertion count.
- `tools/verify-halo3-weapon-haptics.py` passed 21 pinned retail signature,
  call-edge, and byte-witness checks for the pinned module hash.
- The cumulative DLL build passes (`out/review-20260930-haptic-cumulative-build.log`). Headset testing is
  still required to confirm one-hand, dual-wield, and two-hand recoil assignment
  while checking that non-recoil native feedback remains unchanged.
