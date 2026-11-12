# Direct drive for the flight controllers: the Reach Sabre and the Halo 4 Broadsword

Measured and built 2026-10-13, on the retail Steam modules, in a simulated headset.

## What was wrong

Direct drive writes the hand's aim into player control's desired angles from a hook on the function that
accumulates the stick into them. Reach and Halo 4 never run that function for the pilot of a physics type-13
craft. Each title's per-user control dispatcher tests the vehicle's type and, for 13, calls a dedicated attitude
controller and jumps past the accumulator:

| | Reach | Halo 4 |
|---|---|---|
| Dispatcher | `haloreach+0x6350C` | `halo4+0xA8828` |
| Controller | `+0x64828` (called at `+0x63607`) | `+0xAA524` (called at `+0xA892E`) |
| Accumulator it skips | `+0x6375C` | `+0xA8B34` |
| Record | `*(TLS+0xD8) + user*0x180`, angles at `+0x94` | `*(TLS+0xB0) + user*0x1F0`, angles at `+0x138` |
| What the craft steers by | vehicle `+0x1E4` (desired facing) | vehicle `+0x4BC` |
| The game's own steering | stamp at vehicle `+0xABC`, direction `+0xAC0` | stamp at `+0x104C`, direction `+0x1050` |
| Its window (tag) | 1.0 s (Sabre) | 0.5 s (campaign Broadsword), 1.0 s (Forge) |
| Game time | `*(TLS+0xA0)`: tick `+0xC`, seconds per tick `+8` | `*(TLS+0x90)`: the same fields |

So the pilot was never written. The aim provider saw no heartbeat from the drive and kept the stick servo, which
steers through the controller's own look smoothing. In Halo 4 that smoothing is heavy: with the stick held fully
over for 2.6 s the record turned at 15 degrees a second.

Both controllers take seven arguments, three on the stack
(`int user, uint32 vehicle, float yawDelta, float pitchDelta, const void* input, float dtA, float dtB`),
write only player control, and work the same way: the record's yaw, pitch and roll become an attitude, the stick
turns it in the craft's own frame, and the Euler angles of the result are written back. The forward of
(yaw, pitch) is therefore the craft's desired forward at any roll, in world angles.

## What the drive does

The controller's hook is the drive's own in every build (`src/dll/reach_type13_drive.inl`,
`src/dll/halo4_type13_drive.inl`). It runs the game's controller unchanged, then writes the hand's yaw and pitch
into the same record, before the dispatcher derives its deltas from the angles.

- **World angles, both axes, roll untouched.** No seat frame applies and the controller has no pitch stop. Reach
  goes through the direct drive's own apply with a site that names the action (`ReachApplySite`). Halo 4 writes
  through the direct drive's state (`legacy_direct_drive.inl`): its setpoint and seat axes, its put-back slot, and
  the per-axis stamps the input thread reads to hold the stick neutral. The input thread leaves every other
  Halo 4 seat's pitch on the stick servo; it gives this seat its pitch while the hook reports its pilot.
- **A flight is written only once the link is measured.** On each game tick the craft's desired facing has to
  equal the forward of the record as the previous call left it (Halo 4: as one of the last two calls left it),
  within half a degree, twenty ticks running. Until then nothing is written and nothing is reported written, so
  the stick servo keeps aiming. Three ticks out of agreement end the proof.
- **The game's own steering is left alone.** While its stamp on the craft is younger than the window the update
  writes nothing, and the stick stays neutral.
- **The written direction moves at most 80 degrees a second.** Both crafts follow their desired facing with the
  same stiff gain (the tags' 30 pi), so an unlimited flick would whip the craft. 80 is the fastest Reach's own
  stick turned the record.
- **A flight ends** with a change of craft, the game's tick going back, or a quarter second with no call. Halo 4
  reuses the craft's handle when it reloads a checkpoint, so the handle does not tell a reload apart.
- **Extra calls** of the dispatcher in the same update (an effect impulse, Halo 4's weapon sway) have the last
  write put back; the call with no unit ends the update.

Keys: `aim_direct_drive`, `seat_direct_drive`, `reach_vehicle_aim = 2` (the pilot is a look-steered driver), and
in Halo 4 the mod's first-person seat view. With any of them off the detour only runs the game's controller.

Every RVA and offset is proven at install: the entry and the dispatcher's call are unique in the module, the call
lands on the entry, the dispatcher's frame comes from its unwind info and prologue, the stick-update caller calls
the dispatcher, and each field offset matches the instruction that reads it. A miss leaves the pilot on the stick
servo and changes nothing else.

In a dev build with `type13_log = 1` the same detour feeds the log-only observer
(`src/dll/type13_flight_log.inl`) around the native call, before anything is written.

## Measured

Before the drive (the observer alone):

| | Reach Sabre | Halo 4 Broadsword |
|---|---|---|
| Controller calls | 1 per tick, all from the stick-update caller | the same |
| Facing against the record, at rest | 0.000 deg | 0.020 deg |
| Facing against the record of a tick before, turning | at most 0.052 deg | not turned |
| Time arguments | both equal the frame's time step | the same |
| Stick at full deflection | record turns 78 deg/s in yaw | 15 deg/s after 2.6 s (smoothed) |
| The game's own steering | stamps a turn-back direction about every 15 ticks while it applies (seen at the edge of the play space) | stamped straight down, (0, 0, -1), while the craft climbed above the trench |
| Tag: gain, window | 94.25, 1.0 s | 94.25, 0.5 s |

With the drive:

| | Reach Sabre | Halo 4 Broadsword |
|---|---|---|
| Link proven | 20 ticks after a flight begins | the same |
| Facing against the record | at most 0.05 deg, no tick out of agreement | at most 0.03 deg, none |
| Updates written, hand pointing inside the play space | all of them | all of them, both axes |
| Record's turn on a hand swing | 80.0 deg/s | 80.0 deg/s in yaw and in pitch |
| Nose behind its facing while turning | about 3 deg | 1 to 4 deg |
| The game's own steering | left alone on every stamped update | left alone on 150 updates |
| A key switched off and on | stands down at once; a new flight is proven | the same |
| After a death and reload | not tried (nothing there kills the craft unattended) | a new flight proven after each of 6 |

## Not measured

- The feel in a headset. The hand's aim is absolute: the craft flies where the hand points, and the turn control
  turns the hand with the play space as on foot. The mod's seat view does not turn with an aircraft.
- What either craft's guns follow. The angles written are never claimed as converged ones.
- An extra call under damage (none came in either craft).
- A second machine. The write is the aim record, the same record every other seat is written through.
- The Seraph, the Forge Broadsword (its window is the longer one the drive already uses), and a Store build
  (the install's anchors decide there).
