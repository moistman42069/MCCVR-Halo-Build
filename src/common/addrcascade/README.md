# addrcascade

A verification harness for **resolved memory addresses**. It is not a pattern scanner — it is the
layer that checks whether your scanner, your recorded offsets, and your fallbacks are actually
doing what you believe.

MIT licensed, self-contained, `<cstdint>` + `<Windows.h>` only. It deliberately includes nothing
from the project it currently lives in.

## Why this exists

Any RVA or struct offset written into source is a **measurement of one build**. It rots when the
target ships a patch, and it was never valid for a different store's binary of the same game. The
failure is silent and arrives for every user at once, the day they update:

- **Hooking is positional.** `MinHook`/`register_inline_hook` take an *address*, not a symbol. They
  succeed on any readable address and report success. **"Installed" is not "running."**
- **Reads pass their sanity gates.** Unrelated data is usually finite and in range.
- **Reinstalling does not help** — it reinstalls the same constants.

The standard remedy is a cascade: signature → recorded address → alternate resolver → degraded
fallback. The problem with cascades is structural: **every rung below the first only runs when
something is already broken**, so on a healthy machine none of them ever execute, and they rot
unobserved. Building a fallback is not the same as knowing it works.

That gap is not hypothetical or specific to one project. UEVR — among the most battle-tested
binary-modding codebases there is — carries 22 distinct fallback/validate sites in a single file
(`FFakeStereoRenderingHook.cpp`), with no facility to force any of them. That is normal for the
field, and it is exactly what this addresses.

## What it gives you

| Piece | Answers |
|---|---|
| `Signature` + `scan_signature` | Find by shape, and **refuse an ambiguous match** instead of taking the first hit |
| `scan_string`, `scan_reference` | Anchor on a string literal, or on the code that *references* a known address — both survive recompilation better than byte patterns |
| `FaultMask` / `fault()` | Force any single rung to fail **on a healthy machine**, so the rung below it can be watched working. Compiled out of release builds |
| `HookWatchdog` | Catch an address that installs but is never called |
| `CoVariation` | Validate a **data offset**, which no code signature can check |
| `TierReporter` | Say *which rung won*, and only when it changes |

## The two mistakes it is shaped to prevent

Both of these were real, both survived code review, and both were found only by testing:

**1. A watchdog that counts impossible time.** `HookWatchdog::tick()` *requires* an
`event_possible` argument. It is not optional bookkeeping. A watchdog that counts from installation
rather than from the first moment the call could occur will condemn a healthy address: measured on
a real title, the hook installed at the main menu and the alarm fired **11 seconds before gameplay
started**, on every launch. Note also that the timeout is in units of *your* tick — state that
cadence at the call site, because getting it wrong fails silently in the direction that looks fine
(the alarm simply never fires).

**2. Validating a data offset by reading back your own write.** That only proves the address is
*writable*. If the field moved, you write your value to the wrong place and read your own value
straight back. `CoVariation` instead asks whether the candidate **moves when the reference moves**,
and it must be sampled **while you are not driving the candidate** — sampling while you drive it
proves only that you moved it.

## Using it

```cpp
#include "addrcascade/AddressCascade.hpp"

// 1. Give it somewhere to talk. There is no quiet mode by design.
addrcascade::set_logger([](const char* m) { my_log("%s", m); });

// 2. Resolve by shape, and refuse ambiguity.
constexpr unsigned char SIG[]  = { 0x40,0x53, 0x48,0x81,0xEC,0,0,0,0 };
constexpr char          MASK[] = "xx" "xxx????";
static_assert(sizeof(SIG) == sizeof(MASK) - 1, "signature and mask must pair up");

const auto hit = addrcascade::scan_signature(module, {SIG, MASK, sizeof(SIG)});
if (hit.unique())        use(hit.address);
else if (hit.matches > 1) fall_back("ambiguous");   // never take the first of several
else                      fall_back("no match");

// 3. Prove the address is live, counting only time in which the call was possible.
static addrcascade::HookWatchdog wd{5};   // 5 ticks; this caller ticks about every 2 s
if (wd.tick(/*event_possible=*/in_gameplay && !standing_down, /*event_happened=*/hook_ran)) {
    log("installed at %p but NEVER CALLED after ~%u s of live gameplay", addr, wd.counted() * 2);
    engage_fallback();
}
```

Fault injection is compiled out unless the host defines `ADDRCASCADE_FAULTS=1` (dev builds only):

```cpp
if (addrcascade::fault(FAULT_SIG_AMBIG)) hit.matches = 2;   // vanishes entirely in release
```

## Testing your cascade

Three checks, none of which replaces the others:

1. **A self-test** that runs every resolver independently and reports what each found, without
   changing which one the cascade uses. The valuable line is a **cross-check between two resolvers
   that share no code** — agreement is far stronger evidence than either passing its own gate.
2. **Fault injection** — one bit per rung. The self-test proves resolvers *resolve*; only this
   proves the cascade *falls through*.
3. **An offline check against the binary**, runnable after every target update with no session and
   no device. Parse the signature out of the source rather than copying it, or you have created the
   duplicated-constant problem you were trying to solve.

Then **run the healthy configuration as a control.** A suite that only exercises failure paths can
leave the primary rung broken and still score full marks — and a run that never reached the tested
state is INCONCLUSIVE, not a pass. Assert a positive marker ("the hook ran at all"), not merely the
absence of an alarm.

Two structural notes that decide how such a suite is built:

- **Most faults are re-testable in one session** if toggling the feature's own on/off key re-runs
  the whole install path. That turns a suite of reboots into a few seconds each.
- **Session-latched state forces its own boot.** A "did this ever run?" flag and an "already warned"
  latch are one-way by design, so any fault depending on them must be armed *before the process
  starts*. Splitting the suite along that line is the honest shape of what is being tested.

## Lifting it out

Copy the folder. Set a log sink. Define `ADDRCASCADE_FAULTS=1` in your dev build and leave it
undefined in release. There is nothing else to unpick — no project headers, no engine types, no
global state beyond the log sink and the fault mask.

**That claim is checked, not asserted.** In this repository the two source files are unmodified
copies of the library, and the `halomccvr_addrcascade_tests` target compiles them alone — of the
host's folders only this one on the include path, `ADDRCASCADE_FAULTS` undefined, `/W4 /WX` — against a consumer that
knows nothing about the host project (`tests/addrcascade_tests.cpp`; a second target builds the
same consumer with `ADDRCASCADE_FAULTS=1`). It runs with every build. "Self-contained" is precisely
the kind of property that is true when written and quietly false later, because the host build has
every project header on its include path anyway and one convenient `#include` will never fail
there.

That consumer doubles as the unit tests, and they encode the two lessons this library exists for:

- the watchdog **fires** after its limit when the event was possible, and **never** fires when it
  was not, however long it runs
- `CoVariation` returns Match when the candidate tracks the reference and Mismatch when it is frozen
- `TierReporter` reports the same address again when the **tier** changed

None of it needs the game, a device, or a session.
