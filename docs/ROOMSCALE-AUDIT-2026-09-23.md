# Roomscale movement audit and bounded lifetime fixes

The user requires physical movement to keep the body aligned while also using
in-game movement, without sliding or losing position. This remains broader
than the bounded fixes below. No simultaneous-follow completion or headset
acceptance is claimed; the accepted build pointer is unchanged.

## Reproduced and corrected

The actual `roomscale.cpp` transport fixture reproduced the cancellation/tail
failures, ten retained-debt failures across all five active titles, and five
stale-polling recovery failures before their respective changes. It passes
afterward, including all existing delay, turn, native
stopping, repeat-walk, scale, collision-stall and title/freshness cases.

1. A manual VR stick press/release can occur between native camera callbacks.
   `Roomscale_Input` previously changed its command epoch only when general
   admission changed. The camera never observed the manual interval, so the
   prior physical-follow command could be consumed immediately after release.
   Manual-state transitions now also invalidate that epoch. A fresh camera
   sample must re-establish the current state before another command is used.
2. `RoomscaleFollow` checked its stopping-tail age after consuming new native
   movement. A callback gap of 180 ms (below the 250 ms general reset) could
   therefore consume unrelated platform motion even though its 150 ms quiet
   tail had already expired. The age checks now precede that consumption.
   The fixture generates a real stopping tail, then supplies unrelated travel
   after the gap. The tracking reference remains unchanged.
3. The physical lean baseline was reset on every manual VR-stick frame and
   again on manual input transitions. Physical steps taken during ordinary
   stick travel were lost as follow debt. Manual input now retires only the
   command and observed-motion authority; the original physical baseline is
   retained. On release, catch-up waits until the observed native camera has
   remained quiet for 150 ms. Ordinary manual travel and its braking tail are
   never consumed by that deferred follow path. Continuing platform/camera
   movement extends the wait rather than being claimed as physical follow.
4. Fresh input after a polling gap could retain old debt if the Boolean
   admission state stayed true. A gap exceeding the existing 100 ms input
   freshness limit now resets both command and physical-history epochs. This
   prevents unexpected catch-up when input resumes after an interruption.

The production fixture also repeats manual/physical cycles to verify that
deferred physical travel does not lose accumulated debt or cancel manual world
distance. Tracking loss still discards debt. The quiet-motion threshold is
0.1 mm per observed sample; it does not establish native physics velocity.

These corrections retain native locomotion/collision, finite/bounds checks,
tracking/title/generation admission, and no position/velocity writes.

## Why removing the manual-stick gate is insufficient

`input.cpp` calls `Roomscale_Input` with VR manual axes and suspends it for an
actual physical gamepad stick. `Roomscale_Move` intentionally declines manual
VR movement. The correction retains physical debt for later catch-up;
it does not deliver simultaneous body catch-up. All five active integrations
pass the unmodified native camera
position as their observed-body sample. CE's experimental path stays disabled.

Simply adding a follow vector to the manual stick and retaining today's
reference update would subtract all observed manual world movement from the
headset reference. That would incorrectly cancel ordinary locomotion. The
current receipt does not identify support/platform velocity, native unit
position, the applied manual/follow components, or independent external
impulses. At a saturated native stick, arbitrary additional parallel physical
speed also cannot be represented by the same bounded input vector.

The retained-debt correction is deliberately partial. It pauses catch-up during
manual movement and does not identify the preceding physical follow command's
braking tail at manual takeover. A moving platform or animated native camera
can delay the quiet interval. Teleport, recenter, invalid tracking, title or
generation change, and a physical offset beyond the existing 1.2 m tracking-jump
bound still discard the old baseline. None of this is labeled as the requested
simultaneous behavior.

## Native support-motion investigation

Existing contact/collision adapters supply ray/sweep results and ownership,
not a native ground-support-motion receipt. Read-only official-kit analysis
found useful independent H3/H4 paths:

- H3EK `halo3_tag_test.exe+664420` uses the ground-mode source assertion
  `linear_velocity!=NULL`, resolves the selected supporting physics entity,
  checks previous support entity/body identity, and obtains its transform and
  point velocity. Retail `halo3+21970C` structurally matches the contact
  iterator, support selection, identity comparisons, transform/velocity
  calls and stored result. This is a verified behavioral match, **not an
  installed binding or proven local-player callback contract**. Its existing
  call to matrix helper `120DF8` does not authorize hooking that forbidden
  helper; no hook was added.
- H4EK `halo4_tag_test.exe+74F610` is identified independently from its own
  ground-mode assertions. It has support transform/velocity handling plus
  additional biped/object branches. A BSim lead at retail `2873AC` was not yet
  structurally verified and remains a candidate only.

Read-only decompilation and match output is preserved under
`out/coop-stability-20260923/roomscale-{h3,h4}-ground-*`. There are no new
runtime offsets or hooks from this incomplete investigation. Next proof
requirements are the full local unit identity, precise callback ABI/lifetime,
physics tick receipt, support motion semantics, and independent title matches.

## Contributor comparison

The pinned [LivingFray PR153 Game.cpp](https://github.com/LivingFray/HaloCEVR/blob/5f3cc880ca81b157c663f7ec4e7670c8890fcbdf/HaloCEVR/Game.cpp)
was downloaded after the saved API diff omitted that large file. Its joined
client path retains historical manual/room/combined commands and estimates
room motion from observed displacement aligned with a selected combined
command. This is a useful design lead, not exact platform attribution or an
MCC-compatible native binding. Its own multiplayer and host paths differ;
the donor's host position-write path was not ported. Full pinned files are
preserved at `out/community-audit-20260923/livingfray-pr153/`.

Required remaining validation includes simultaneous manual/physical motion,
moving/rotating platforms, ladders, jumps/weapon-camera effects, external
impulses, co-op host/client, respawn/teleport, and a Halo 3 headset regression.

## CE parity investigation at the packaging stop

CE remains explicitly disabled in `RoomscaleGameplayEligible` and
`kCeExperimentalRoomscaleEnabled`. Its dormant `FollowRoomscale` still uses
the old full-reference transform, whereas accepted CE camera/controller
composition uses `BuildTrackingFrame`'s horizontal reference. The dormant
function is currently called only by Classic window preparation; Anniversary
construction has no corresponding invocation. Merely changing the enable
constant would therefore produce inconsistent graphics-mode behavior.

CE already has independently verified on-foot owner/input admission, native
movement basis and a network outgoing-action adapter. See
`HALOCE-UNIT-CONTROL-2026-09-16.md` and `haloce_network_input.cpp`.
Local private movement inputs are restored around the native consumer;
network mode deliberately retains native simulation so prediction and host
consume the same outgoing action. A future roomscale implementation must
retain those title-specific contracts and prepare a coherent same-reference
pair for both graphics modes. The native center camera is not proof of the
unit collision origin or support-relative displacement.

No incomplete CE integration was enabled. Under the latest user instruction
to finish current integrations and package, further concurrent/platform and CE
roomscale discovery stops here until explicit resume. Next concrete proof is
a generation- and local-unit-owned physics-tick receipt combining actual unit
travel with the separately identified support transform/velocity, followed by
native action transport attribution; a synthetic mixed-stick ratio alone does
not establish that receipt. Existing five-title bounded fixes stay enabled.
