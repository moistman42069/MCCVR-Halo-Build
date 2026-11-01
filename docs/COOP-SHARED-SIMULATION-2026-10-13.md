# Co-op campaign is one shared simulation: what the mod may do inside a tick

Halo: Reach, measured 2026-10-12 and 2026-10-13. The rule that came out of it applies to every title whose
campaign co-op runs the same simulation on every machine from the same inputs.

## The fault

Two Reach co-op campaign sessions ended in a disconnect with Co-op safe on. Co-op safe already left every shot as
the game built it and wrote nothing but the aim record, so whatever parted the two machines' simulations was
something else the mod did inside the simulation.

## The instrument: replaying the session's own film

MCC saves a film of every campaign session (Creative > Theater lists them). A film is the session's recorded
actions; watching it runs the simulation again from those actions. Replay the same film twice and every object
should be in the same state at every tick. A replay made with a build that changes the simulation is not the same
replay, and the first tick at which it differs names what was changed.

Dev builds only:

| Key | What it does |
|---|---|
| `dev_sim_trace = 1` | At every simulation tick, each object's identity, parent and position and a hash each of its frame, motion, skeleton and aim go to a file beside the log (`src/dll/reach_sim_trace.inl`). |
| `dev_sim_minimal = 1` | The reference. Nothing of the mod is installed in Reach but the recorder: no VR view, no aim, no other hook. |
| `dev_sim_bisect = <bits>` | Each bit makes one group of the mod's Reach detours a plain pass-through, to find which one a difference belongs to. |
| `dev_sim_probe = <bits>` | One of the mod's own calls made on demand inside or between ticks, to test a call by itself. |

`tools/simtrace_compare.py` lists and compares two trace files; `tools/simtrace_first.py` prints the first tick at
which each object differs.

**A replay made with the mod installed is not a baseline.** Two replays with the same build agree with each other
whether or not that build changes the simulation, so comparing them proves nothing. The first night's analysis
compared mod replays with each other and attributed the fault to the wrong thing. Only the reference replay says
what the game itself does.

In Theater the mod takes the film's player for the local player: the camera core arms and the first-person lanes
run, but no control pass runs, so the aim drive does not arm and the aim trace is never asked for.

## What the film of the second session shows

The film's gameplay runs from tick 3 to tick 6880. Every replay below is compared with the reference.

| Replay | Against the reference |
|---|---|
| The build as it was, Co-op safe on (three replays) | A piece knocked off a punched Grunt lands 0.14 to 0.26 world units away, differently each time (tick 6223). Two Grunts are 1e-5 world units off at the first shot of a burst fired on the run (tick 6660), then the Ghost the player boards and its boarder (tick 6755), then an object exists in one and not the other (tick 6878). The live session stopped at tick 6880. |
| All nine detour groups as pass-throughs | 0 objects differ |
| All but the per-unit update as pass-throughs | 0 objects differ |
| Only the per-unit update a pass-through | only the Grunt's piece differs |
| Only the unit-adjust a pass-through | only the Grunt's piece differs |
| The fire eye not measured (`shot_point_transport = 0`), all else as it was | only the Grunt's piece differs |
| An extra camera-evaluator call or collision test after the local unit's update, every tick | only the Grunt's piece differs |
| The same extra calls made between ticks | as the build as it was |
| **The build with the rule below, Co-op safe on** | **0 objects differ** |

## What the film of the first session shows

A longer film: 33,634 ticks (560 s), most of it flown in a Falcon.

| Replay | Against the reference |
|---|---|
| The build as it was, Co-op safe on | The local player's aim differs for the seven ticks after boarding a Falcon's second seat (tick 347). A piece knocked off by a melee hit lands 0.19 world units away at tick 33619, and two characters' aim differs on the film's last tick, 33636: the session ended 17 ticks after the piece was made. |
| The build with the effect and fire-eye rules | Only the seven ticks of seated aim differ |
| **The build with all of the rule below, Co-op safe on** | **0 objects differ** |

So both sessions ended after a melee hit knocked a piece off an enemy: 17 ticks later in the first, 657 ticks later in
the second. How soon the game notices a difference was not measured; the second session's replay had also parted at a
shot by then.

## The three causes

**An effect's place, moved inside a tick.** Reach's effect-location resolver is asked for a place both by the
simulation, inside a tick, and by the frame, from another thread. The mod moved every place that lay near the stock
weapon onto the controller-held gun, whoever asked. A place asked for inside a tick is where the effect makes its
objects and does its damage, so the piece knocked off a Grunt standing at arm's length was made somewhere else on
this machine than on the other. Over the film, about 14,000 places near the gun were asked for inside a tick and
about 11,000 from the frame's thread.

**The fire eye, asked for inside the fire.** For flat parity the mod asked Reach's unit camera-position evaluator
for the shooter's eye inside each of the local player's fires, before Reach built the shot. With that call made, the
replay parts from the reference at a shot; without it, it does not. The call alone is not enough: with the mod's own
work after each unit's update skipped, the same 17 asks leave the replay the game's own, and an extra call of the
game's own at the end of that work also hides the fault. So this is not a value the mod wrote. Code that changes
nothing by itself, run inside a tick, changed what the game computed when combined with a call elsewhere in the
same tick. It was not traced further than that, and it is the reason the rule below is a rule and not a list of
known-bad calls.

**A seat's aim, not rebased.** Reach rotates a seated player's aim by the vehicle's up frame unless the seat carries
the third-person camera flag. The mod's first-person seat patch clears that flag on some seats, and a code patch
then skips the rotation so those seats aim as they do in stock. Co-op safe already stops the flag being cleared, but
the skip stayed in place. It is put back while the local player sits in a seat that natively lacks the flag, once
the sampler has seen the seat: seven ticks late in this film, and never for another player sitting in such a seat.
Nothing followed from those seven ticks here; a shot fired in them would have left along a different line.

## The rule

While the simulation is shared (Co-op safe on: `Sight_FireTimeChangesAllowed()` is false), **inside a simulation
tick the mod asks nothing of the game and moves nothing the game reads.**

- **The tick is the game's own word.** `src/dll/sim_tick_reader.h` reads the byte the tick loop sets before every
  tick and the post-tick record clears (Reach and Halo 4; resolved by signature). `ReachOutsideTheSimulation()` is
  true only where that byte says the calling thread is outside a tick. Where it cannot be read, nothing is known and
  every lane that depends on it stays off.
- **The fire eye is not measured** in any title (`Sight_FireEyeAsked`): no ask inside the fire, no proof sample at
  rest, and no shot point moved with the eye. The shot is left as the game built it there anyway.
- **An effect's place is moved only when it is asked for outside a tick** (`ReachEffectMoveAllowed`). The frame's
  places still move onto the gun; the simulation's stay where the game put them.
- **Reach's per-unit update is the game's own.** Contact melee is stood down there already, and the aim trace is
  made from the control pass, between ticks, where the game makes its own collision tests for the local player.
  The trace was not itself shown to change a replay; it is moved on the rule.
- **Reach's hand and weapon world collision** (off by default) makes its own collision tests only from a game call
  that came from outside a tick.
- **The seat aim keeps the game's own rebase** (Halo 3, ODST, Reach: `SeatAimFrameWanted`). No seat's camera flag is
  cleared there, so the skip has nothing to undo.

Alone in the game (Co-op safe off) nothing changes: there is one simulation and it is the game.

## What is proven, and what is not

- Proven: with the rule, the ordinary Co-op safe replays of both sessions' films are the game's own, object for
  object: all 6878 ticks of the second and all 33,634 ticks of the first.
- Not proven by a replay: anything the film does not do. A replay runs no control pass, so the aim trace at the
  control pass, the written aim record and the hands' world collision are not exercised by it.
- Not proven at all: a second machine. The film test shows this machine's simulation is the game's own; the
  session test is two machines playing.
- Not covered by the rule yet, outside Reach: the other titles' aim traces run inside their per-unit updates, and
  Halo 3's and ODST's hand and weapon world collision (off by default) makes its own collision tests from inside
  the game's collision calls. Halo 3, ODST, Halo 2 and CE have no in-tick byte of this kind to read. A collision
  test inside the tick did not change the Reach replay.
- The other titles have no recorder. Their fire-eye asks are stood down by the same predicate; anything else they
  do inside a tick has not been measured.
