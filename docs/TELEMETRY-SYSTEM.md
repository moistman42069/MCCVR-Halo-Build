# MCCVR telemetry system

This is the canonical durable documentation for MCCVR's telemetry subsystem: a
mod-wide troubleshooting and evidence facility. It describes the subsystem
contract, user workflow, evidence model, recorder architecture, schema-extension
rules, analyser/validator responsibilities, packaging rules, failure behaviour
and required verification for future changes.

Telemetry is **mod-wide troubleshooting and evidence infrastructure**. It is not
a Virtual Stock feature, and it must not become a collection of one-off research
fields tied to whichever problem was investigated most recently. Virtual Stock
is a major current consumer; it is not the definition of the system.

Authority when sources disagree:

1. the hard safety rules in [`AGENTS.md`](../AGENTS.md);
2. current implementation and tests;
3. this document;
4. the analyser-owned [`TELEMETRY_AI_GUIDE.md`](#54-telemetry_ai_guidemd).

If this document and the code/tests disagree, do not paper over the drift with a
one-off note. Fix the code, or update this document and the tests together. If
this document and `AGENTS.md` appear to conflict, reconcile against code and
tests; never weaken the `AGENTS.md` safety contract by inference.

Field-level truth does not live here. It lives in `TelemetryFrame`, the
serializer, the validator, the analyser signal bindings and the tests. This
document explains **what telemetry is, how to use it, and how it is allowed to
grow**.

### Quick orientation

| Question | Where to look |
| --- | --- |
| How do I record a troubleshooting session? | [§3 User workflow](#3-user-workflow) |
| Where did the recording go? | [§4 Filesystem layout](#4-filesystem-layout) |
| What must I send with a bug report? | [§3.1](#31-what-to-provide-for-a-telemetry-backed-bug-report) |
| What does this JSON field mean? | [§8 Frame schema model](#8-frame-schema-model) and the raw record itself |
| Can I trust the manifest/diagnostics? | [§5 Evidence and file model](#5-evidence-and-file-model), [§10 Analyser](#10-analyser) |
| How do I query a recording? | [§10.5 Raw navigation and query presets](#105-raw-navigation-and-query-presets) |
| Should a new signal be a frame field or an event? | [§9.3](#93-choosing-between-frame-event-analyser-and-log) |
| Do I need a schema bump, and what tests must I add? | [§12](#12-extending-telemetry-adding-a-new-signal), [§14](#14-schema-versioning-and-compatibility) |
| What happens if the analyser or Python is missing? | [§16](#16-failure-modes-and-troubleshooting) |
| What are the hot-path rules? | [§7 Hot-path and performance contract](#7-hot-path-and-performance-contract) |

## 1. Purpose

MCCVR telemetry exists to capture structured, time-related evidence about what
the mod and the runtime observed while a problem occurred in-headset, without
changing the behaviour being observed. It is used when a troubleshooting
question needs one or more of:

- exact per-prepared-frame state;
- relationships between several observations from the same frame;
- controller/head/pose provenance and validity;
- numerical analysis across a time series;
- exact transitions or ordering across callback/thread domains;
- reproducible evidence that can be inspected after the game has stopped;
- a neutral recording from which questions not known at capture time can later
  be asked.

Telemetry is deliberately separate from behavioural tuning. Diagnostic
instrumentation should observe the behaviour under test; it must not change that
behaviour (see [§7](#7-hot-path-and-performance-contract)).

### 1.1 Telemetry is not an oracle

The evidence hierarchy is:

1. **raw JSONL** — the evidentiary source;
2. **manifest** — a mechanically derived map of the raw capture;
3. **diagnostics** — derived calculations, indexes, heuristics, simulations and
   cross-checks;
4. **human/AI interpretation** — reasoning appropriate to the question.

The manifest and diagnostics can contain generator bugs or omissions. A derived
result must never silently become stronger evidence than the raw recording from
which it was produced. When a material conclusion depends on a sidecar result,
verify that conclusion against the raw telemetry.

## 2. Telemetry versus `HaloMCCVR.log`

MCCVR has two complementary diagnostic surfaces. Use the log when the question
is primarily **what happened operationally**. Use telemetry when the question is
primarily **what changed, in what order, and what runtime state accompanied it**.

| Surface | Use it for |
| --- | --- |
| `HaloMCCVR.log` | Startup, hook/install state, binding/signature discovery, configuration and build identity, fallbacks, exceptional errors, one-shot or low-rate human-readable diagnostics, recorder/analyser launch status. |
| Telemetry | One observation per prepared VR frame; exact frame identity and timing; pose/input/solver relationships that must be compared on the same frame; validity and provenance that must travel with a payload; sparse cross-thread events where ordering itself is evidence; investigations requiring later deterministic computation over many observations. |

Do not add high-frequency `LOG(...)` calls to answer a frame-by-frame question.
Logging, formatting and file I/O are forbidden in the prepared-frame telemetry
hot path. Conversely, do not use telemetry merely because structured JSON is
convenient.

## 3. User workflow

Telemetry is controlled from the **Telemetry Recorder** section of the F1 MCCVR
menu (current UI label; the section description reads "Records controller,
headset and aim-solver data for troubleshooting.").

Typical troubleshooting workflow:

1. Start MCC and enter the state needed to reproduce the problem.
2. Open F1 and select **Telemetry Recorder**.
3. Select **Start recording**.
4. Reproduce the problem with as little unrelated activity as practical.
5. Select **Stop & save** (current UI label for the stop/save operation).
6. Wait for the recorder to finish finalising the raw file. MCCVR then makes a
   best-effort attempt to run the packaged analyser against the closed
   recording (see [§6.6](#66-analyser-launch)).
7. Collect the relevant files from the telemetry recordings directory
   ([§4](#4-filesystem-layout)).

Telemetry is opt-in. Recording should be kept focused enough to make later
analysis tractable, but there is no requirement to pre-select a hypothesis or
metric before recording.

While a session is active (or finalising) the menu reports health counters,
including: recorder status/state, duration, producer calls, enqueued frames,
written frames, queue-full drops, weapon events enqueued/written/dropped,
duplicate prepared serials suppressed, and the recorder error name with its
system code when the recorder is in `Error` or `Unavailable`. These counters are
part of the evidence about capture quality — see
[§8.7](#87-session-envelope-and-accounting) and
[§16.8](#168-health-before-conclusions).

### 3.1 What to provide for a telemetry-backed bug report

For a normal open-ended investigation, provide at least:

```text
<stem>.jsonl                 raw recording (always the most important file)
<stem>.manifest.json         neutral mechanical map, if generated
HaloMCCVR.log                from the same run
```

Add when available and useful:

```text
<stem>.diagnostics.json      derived aid (accelerates investigation)
<stem>.diagnostics.md        same content, readable rendering
TELEMETRY_AI_GUIDE.md        generated capture-analysis orientation
```

Also include a short description of what the player did and when the symptom
occurred. For a deliberate A/B experiment, an optional human-written
`EXPERIMENT_CONTEXT.md` may explain intent; it is context, not neutral telemetry
(see [§5.5](#55-experiment-context-files)).

The diagnostics can accelerate investigation but are never a substitute for the
raw JSONL. `HaloMCCVR.log` remains useful for build/runtime/lifecycle context and
should normally accompany a telemetry report when the issue may involve startup,
title switching, binding failure, or recorder/analyser failure. If in doubt,
send the whole contents of the capture's `Telemetry Recordings\` folder plus the
log from the same run.

## 4. Filesystem layout

### 4.1 Canonical layout

`<mod folder>` means the directory containing `HaloMCCVR.dll` and
`HaloMCCVR.log` (in production the DLL install directory; the log is placed
beside the DLL at startup).

The canonical layout is:

```text
<mod folder>\
├─ HaloMCCVR.dll
├─ HaloMCCVR.log
├─ ...
├─ Telemetry Recordings\
│  ├─ HaloMCCVR-Telemetry-YYYYMMDD-HHMMSS-mmm.jsonl
│  ├─ <stem>.manifest.json
│  ├─ <stem>.diagnostics.json
│  ├─ <stem>.diagnostics.md
│  └─ TELEMETRY_AI_GUIDE.md
└─ TelemetryAnalyser\
   └─ analyse_mccvr_telemetry.py
```

Required relationship for the recordings directory:

```text
dirname(recordings_directory)  == mod_directory
basename(recordings_directory) == "Telemetry Recordings"
```

`Telemetry Recordings\` is a direct child of the mod folder. It contains
user-generated evidence; it is not packaged program content. The analyser is
program content and lives separately under `TelemetryAnalyser\`, because it is
code, not captured evidence.

Recording filenames are UTC-timestamped:

```text
HaloMCCVR-Telemetry-<YYYYMMDD>-<HHMMSS>-<mmm>.jsonl
```

with a numeric suffix (`-1`, `-2`, ...) if a file with that name already exists.
A recording and its per-capture sidecars share the same stem, so their
relationship is obvious.

## 5. Evidence and file model

### 5.1 Raw JSONL — authoritative evidence

Each recording is UTF-8 JSON Lines: one JSON object per physical line. The
normal record families are:

```text
session_start
frame
weapon_event
event_gap
session_end
```

A clean capture begins with `session_start` and ends with `session_end`.
`frame` records are complete logical prepared-frame observations; a frame is
never split across several raw records. Additional record types are treated as
future-compatible "other records" by the analyser and inventoried, but only
documented families carry agreed semantics.

A capture can also be recoverably unclean, for example when the process was
killed and the final line was truncated. Tools must distinguish a recoverable
trailing partial line from malformed JSON in the middle of a capture:

- a truncated final line is a recoverable partial;
- a malformed or non-object line in the middle of a capture is corruption and
  must not be silently skipped by strict validation;
- a missing `session_end` means the capture was not finalised cleanly.

Raw telemetry is deliberately self-describing and relatively verbose. Do not
trade away frame locality, precision, validity information or crash recovery
merely to reduce byte size. Compression for transport or storage is a separate
concern from the logical evidence format. Do not rewrite raw files to make them
look clean, and do not rewrite old JSONL to resemble a newer schema.

### 5.2 Manifest — neutral mechanical map

`<stem>.manifest.json` is generated from one raw recording. Its job is to make
mechanical orientation cheap without deciding what is important. It includes,
as applicable:

- the exact source-byte SHA-256 and size, with the hash basis recorded as
  `exact_input_file_bytes`;
- the actual input filename and the recorder-declared filename;
- telemetry schema, build commit and generator identity (name/version/SHA-256);
- parse/finalisation health (clean vs recoverable unclean, malformed middle
  lines, trailing partial line);
- frame, serial, and time ranges and a serial-gap count;
- session-end accounting plus derived accounting checks;
- recorder runtime declarations copied from `session_start` (queue capacity,
  drop policy, worker cadence);
- coordinate/tracking semantics and timebase formulas;
- profile and enum registries declared by the recorder;
- record, event and collapsed field-path inventories;
- authoritative validity pairings known from the schema or an explicit tested
  mapping;
- directly observed structural runs and settings events.

The manifest is intended to be exhaustive but dumb. It must not rank events,
select likely causes, recommend anything, or encode the current research
hypothesis. Absence from the manifest means only that a path or category was
not observed under that identity in the successfully parsed records; it is not
proof of absence from reality.

A manifest generated from a capture without `session_end` is marked
`provisional` (its sidecar names carry a `.provisional` marker) and its `kind`
field reflects that.

### 5.3 Diagnostics — optional derived aids

`<stem>.diagnostics.json` and `<stem>.diagnostics.md` are two renderings of one
underlying derived analysis model. Diagnostics may legitimately contain:

- deterministic calculations;
- heuristic indexes;
- selected candidate intervals;
- thresholds;
- ranked transitions/jumps;
- simulations;
- same-frame control/counterfactual comparisons;
- regression/health checks.

Because these operations select and transform evidence, diagnostics must expose
sufficient provenance to reproduce or falsify them: source bindings, method
classification, active parameters, signal definitions with supported schema
versions, schema compatibility and raw locators. Diagnostics must not
automatically emit a final causal or product verdict such as "best setting",
"root cause", or "recommended implementation".

Diagnostics are compact by default. Per-frame derived row dumps are omitted
unless explicitly requested (`--include-debug-derived-rows`), because
duplicating the raw dataset is not their purpose.

### 5.4 `TELEMETRY_AI_GUIDE.md`

The analyser emits this capture-independent orientation guide beside recordings.
It documents the epistemic role of raw, manifest and diagnostics; JSONL
mechanics; validity rules; source binding; and raw-navigation commands.

Do not duplicate the whole guide into this document, and do not treat the guide
as subsystem documentation. This file explains how MCCVR telemetry is designed
and extended; the guide explains how an investigator should orient itself to
one capture. When analysis-facing semantics change, update the analyser-owned
guide and its tests together.

### 5.5 Experiment context files

`EXPERIMENT_CONTEXT.md` is an optional human-written note describing experiment
intent for a deliberate A/B session. It is not telemetry and it is not neutral;
when supplied to the analyser it is disclosed as a hashed dependency and never
changes the neutral manifest. Do not encode experiment intent into the raw
recorder.

## 6. Runtime architecture

The recorder deliberately separates **capture** from **serialization**:

```text
prepared-frame / event producers
        │  fixed-size records only
        ▼
preallocated lock-free transport (frame ring + event channel)
        │
        ▼
below-normal-priority telemetry worker
        │  JSON serialization
        │  buffered file writes / periodic flush
        │  orderly final drain
        │  session_end / close
        ▼
best-effort analyser launch (after the raw file is closed)
```

Current implementation lives primarily in:

- `src/dll/telemetry_recorder.h` — record structs, API, schema version;
- `src/dll/telemetry_recorder.cpp` — queues, worker, serialization,
  finalisation, analyser launch;
- `src/dll/vr.cpp` — prepared-frame population and several sparse-event
  publishers;
- `src/dll/game.cpp`, `src/dll/halo2_observer_6dof.cpp`,
  `src/dll/haloce_first_person.cpp` — additional sparse-event publishers;
- `src/dll/dllmain.cpp` — recorder initialisation;
- `src/dll/menu.cpp` — F1 controls and status.

Implementation details may be refactored; the safety and evidence contracts in
this document must survive any refactor.

### 6.1 Recorder states

```text
Unavailable   recorder infrastructure could not initialise; gameplay continues
Idle          ready to start
Starting      opening/resetting a session while admission is controlled
Recording     frame/event producers may publish
Finalizing    admission is closed while admitted producers and queues drain
Error         recorder-side failure; gameplay continues; status/log exposes it
```

`Unavailable` and `Error` are visible in the menu with the recorder error name
and system error code, and are reported in `HaloMCCVR.log`.

### 6.2 Admission, generations and duplicate suppression

Start/Stop are control-plane operations; prepared-frame capture is admitted
only while a recording generation is accepting producers.

The frame producer uses a **two-check admission handshake** with an in-flight
counter: read the admission token, claim a producer receipt, re-check the token,
and abandon the observation if a Stop/Start raced the claim. Finalisation
disables admission and waits for all admitted producers before draining and
closing. This prevents a Stop/Start race from publishing a frame into the wrong
recording or after the file has closed. The sparse event channel uses the same
generation/in-flight principle.

Additional admission properties:

- a repeat of the most recently accepted `prepared_serial` is suppressed at
  admission and counted (`duplicates suppressed`), so duplicate captures do not
  become duplicate frame records;
- a full frame queue rejects the new frame **before** the caller captures it,
  so queue pressure does not pay the capture cost; the rejection is counted;
- when recording is off, the disabled path performs no diagnostic reads,
  allocates nothing, and publishes nothing — for events this is a single
  admission check and return.

### 6.3 Transport

Two bounded, preallocated transports feed the worker:

| Transport | Producer model | Current implementation constant |
| --- | --- | --- |
| Frame ring | single frame producer | 4096 slots; 4095 usable; `drop_new` |
| Sparse event channel | small known producer set (Present/capture and first-person threads), claimed sequence | 1024 slots |

Current frame-ring behaviour: a full ring drops the incoming frame and
increments the drop counter; drops are reported in session accounting and in
the F1 status. The ring is published and consumed with release/acquire
atomics; payloads are plain fixed-size records copied into
preallocated storage.

Current event-channel behaviour: producers claim a process-wide monotonically
increasing sequence, write a fixed-size record, then release-publish a per-slot
commit marker; the consumer reads payload fields only after observing the exact
committed sequence. This makes payload publication observable atomically and
lets the worker distinguish "still in flight" from "definitively lost" (see
[§9.2](#92-ordering-sequence-and-gap-semantics)).

Queue pressure is evidence, recorded in session accounting and in gap markers,
rather than hidden. Nothing in either transport blocks or allocates on the
producer path.

The exact constants above are current implementation, not the durable contract.
The durable contract is: bounded, preallocated, non-blocking transport with
explicit accounting of loss.

### 6.4 Serialization worker

The worker is created outside loader lock (the DLL starts a background init
thread from `DllMain`; `Telemetry_Init` runs there), and below-normal thread
priority is attempted where Windows permits it (a failure is tolerated and
logged).

While recording it polls/drains on a bounded interval (current: 10 ms). While
idle it waits for control work. The worker owns:

- JSON string construction;
- file writes;
- flushes;
- event/frame drain ordering;
- `session_start` / `session_end` serialization;
- closing the raw recording.

Current flush policy: the file is flushed when either 256 frames have
accumulated or about one second has elapsed since the last flush (the
frame-count condition is checked during frame drains at least every 32 frames,
and the time condition is also re-evaluated once per worker poll). File output
uses a 256 KiB buffer. These activities never belong in the prepared-frame hot
path.

### 6.5 Finalisation

A clean Stop follows this logical order:

```text
disable producer admission
    ↓
wait for frame + event publishers already in flight
    ↓
final-drain sparse event claim frontier
    ↓
drain frame ring
    ↓
write session_end
    ↓
flush
    ↓
close raw JSONL
    ↓
return recorder to Idle
    ↓
queue best-effort analyser launch against the closed file
```

For the sparse event stream, every claimed sequence up to the immutable final
frontier must become either a serialized `weapon_event` or an explicit
`event_gap`. A shortfall is an internal inconsistency: the recorder publishes
an error instead of a false clean stop. Frame drain happens after the event
frontier drain so that event evidence is not lost behind frame pressure.

### 6.6 Analyser launch

After a clean close, the recorder requests a best-effort analyser launch on a
dedicated thread (below-normal priority attempted; the raw file is already
closed before this point). Current launch behaviour:

- the analyser must exist at
  `<mod folder>\TelemetryAnalyser\analyse_mccvr_telemetry.py`; if it is missing,
  the launch is skipped before any interpreter discovery;
- interpreter resolution tries `python.exe` first, then the `py.exe -3`
  launcher;
- the analyser process is started with a hidden window, a UTF-8 environment
  (`PYTHONUTF8=1`, `PYTHONIOENCODING=utf-8`), standard handles redirected to
  `NUL`, and the mod folder as working directory;
- only the recording path is passed; the analyser derives the canonical
  sidecars itself, and the automatic path requests no legacy/custom outputs;
- success and failure are logged (for example
  `telemetry: analyser started with ...` and
  `telemetry: analyser launch failed; ... recording remains valid at ...`).

Analyser failure — missing Python, missing analyser, process creation failure,
or an analyser error — never invalidates or relocates the raw recording and
never affects gameplay. The raw file is the evidence; the sidecars are
convenience.

## 7. Hot-path and performance contract

This is a hard invariant, mirroring `AGENTS.md`. Diagnostic capture in a
render/prepared-frame or similarly hot path may:

- read state already captured for that same frame;
- read `QueryPerformanceCounter`;
- perform a small, bounded set of allocation-free, side-effect-free
  calculations;
- perform telemetry-only pure counterfactual solves over already-captured
  inputs;
- fill fixed-size trivially-copyable records;
- publish to preallocated lock-free transport;
- update lock-free atomic counters.

It must **not**:

- allocate or free memory;
- format or serialize JSON/text;
- log per frame;
- perform filesystem I/O;
- take a lock, wait, or otherwise block;
- signal/notify a per-frame kernel object;
- perform COM work or signature scanning;
- mutate Config, the selected runtime Test Profile, diagnostic override state,
  tracking state, solver state, game state, or production output;
- call runtime sampling APIs merely to obtain extra diagnostic evidence.

**OpenXR sampling rule.** Telemetry must reuse the frame's existing OpenXR
observations. Do not add telemetry-only hot-path calls such as `xrSyncActions`,
`xrGetActionState*`, `xrLocateSpace` or `xrLocateViews`. If an investigation
genuinely requires a new runtime sample, that is a separate evidence-backed
design change with explicit behaviour and performance review.

**Record budget.** `TelemetryFrame` is currently required to remain trivially
copyable, standard layout, and at most 2048 bytes (compile-time checked). If a
proposed signal threatens that budget, reconsider whether it belongs in the
per-frame schema at all.

**No observation of the subject may change the subject.** Telemetry-only
counterfactual calculations must not mutate Config, the selected Test Profile,
diagnostic override state, tracking state, or gameplay output, and must not
feed production.

**Formatting belongs on the worker.** JSON serialization, flushing and file I/O
run only on the dedicated telemetry worker, which is created outside loader
lock ([§6.4](#64-serialization-worker)).

## 8. Frame schema model

The current raw telemetry schema version is **2**. The analyser remains able to
read schema 1 where its compatibility rules say so
([§14.3](#143-compatibility-rules)). A schema-2 `frame` is best understood as
several evidence families rather than as one Virtual Stock record.

### 8.1 Frame families

| Family | Representative raw fields | Purpose |
| --- | --- | --- |
| Identity/timing | `schema_version`, `prepared_serial`, `predicted_display_*`, `capture_begin_qpc`, `capture_end_qpc` | Stable frame identity; timing; telemetry hot-path profiling |
| Runtime/render state | `active_title`, `openxr_session_state`, `should_render`, `upcoming_views_valid`, `located_view_count`, `focused`, `stereo_enabled`, `menu_open`, `contact_space_epoch` | Context in which observations were made |
| Semantic controller poses | `semantic_primary_aim`, `semantic_primary_forward`, `semantic_support_aim`, `semantic_support_endpoint` | Primary/support roles after MCC handedness routing |
| Physical controller poses | `physical_left_aim`, `physical_right_aim` | Raw anatomical left/right aim roles before semantic routing |
| Grip-position observations | `support_grip_position`, `semantic_primary_grip_position`, `support_endpoint_used_grip` | Position-only grip-action endpoints (including the fixed VS-OFF grip pair) and the Virtual Stock/Lab support-endpoint source selection |
| Head/views | `semantic_hmd`, `views[]`, `headset_smoothing` | HMD pose and located stereo views |
| Velocities | semantic primary/support linear velocity + sample timestamps | Controller motion observations |
| Input | `pad` | Routed pad state used that frame |
| Active settings/profile | `test_profile_*`, `effective_settings` | Exact effective control settings used for the solve |
| Aim trace | `aim_trace` | Intermediate geometry, authority, path and provenance from the canonical solve |
| Canonical output | `canonical_aim` | Recorded output of the active settings/profile |
| Grab/release aim continuity | `transition_stock_mode`, `transition_active`, `transition_presented_forward`, `transition_one_hand_anchor_forward`, `transition_advance_count`, ... | Presentation-only correction layered after the live solve for Virtual Stock (Standard and Plus); raw live aim is still `aim_trace`/`canonical_aim` |
| Two-Hand transition/input smoothing | `two_hand_transition_smoothing_configured`, `two_hand_transition_active`, `two_hand_smoothing_configured`, `two_hand_smoothing_applied`, `two_hand_smoothing_strength`, `two_hand_smoothing_mix`, `two_hand_smoothing_alpha`, `two_hand_smoothing_*_error_*`, `two_hand_offhand_influence` | Additive config/activity and raw-to-filtered input error values; `two_hand_smoothing_strength` is the user amount in [0,25] (0 = raw/off, 25 = the full fixed speed-25 filter) frozen for the frame's serial, `two_hand_smoothing_mix` is strength/25 in [0,1] (the applied wet/dry amount, never a filter speed), `two_hand_smoothing_alpha` is the filter's internal clamp(25*dt,0,1) coefficient, and `two_hand_offhand_influence` is the frozen VS-OFF offhand directional authority in [0,1]; orientation error is degrees, position errors are OpenXR LOCAL metres |
| Two-Hand Lab | `two_hand_lab_enabled`, `two_hand_lab_anchor_resolved`, `two_hand_lab_effective_influence`, `two_hand_lab_stateless_direction`, `two_hand_lab_presented_direction`, `two_hand_lab_temporal_active`, ... | VS-off experimental anchor/authority/temporal presentation; the stateless Lab observation and the temporal presented orientation are kept distinct |
| Persistent support grip | `persistent_support_grip_configured`, `persistent_support_grip_applicable`, `support_relationship_readable`, `support_relationship_engaged`, `support_epoch`, `support_solve_trusted`, `support_force_one_hand`, `support_solve_serial` | Frozen solve-time persistent-grip qualification of the frame's own canonical assembly, plus this frame's config knob and title gate |
| Same-frame controls | `cf_vs_off`, `cf_fixed_head`, `cf_fixed_shoulder` | Pure telemetry-only counterfactual solves using the same captured frame inputs |
| Presented aim | `presented_aim_valid`, `presented_aim_forward` | Same-frame reconstruction of the composed steering ray (frame-local solve, then VS continuity, then Lab temporal presentation) |
| Presented reticle | `reticle_presented_*` | Exact consumer-visible reticle pose with its own serial (normally one frame behind; never asserted equal to `prepared_serial`) |
| Engine aim | `engine_aim_*` | Per-title engine aim feedback in engine-world units with source provenance |
| Engine camera/scale | `engine_camera_*`, `world_scale_*` | Engine camera truth (pre-lean base, rendered eye) plus the authoritative units-per-metre scale |
| Reticle/steering settings | `aim_stabilization`, `crosshair_distance_m`, `crosshair_size_deg`, `crosshair`, `kill_reticle` in `effective_settings` | Exact effective knobs behind the presented ray |
| Weapon pad | `weapon_buttons`, `weapon_pulse_until_ms`, `weapon_generation` in `pad` | Gesture output copied with the pad sample |
| Dual predicate | `dual_active` | Shared dual-weapon presentation eligibility (per-shot slot identity travels in the sparse `shot` event, [§9.1](#91-the-weapon-order-channel-as-the-model)) |

This table is a contribution map, not a substitute for the schema. When adding
or changing fields, inspect `TelemetryFrame`, the serializer, the validator,
the analyser inventories and the tests together, and follow
[§12](#12-extending-telemetry-adding-a-new-signal).

The `transition_*` family was added as an additive schema-2 family: every
existing field meaning is unchanged, absence in older recordings is
unambiguous, and the validator accepts its absence as legacy while checking
every present field and cross-field invariant. `transition_stock_mode` (0 =
Standard, 1 = Plus) is only meaningful while `transition_stock_mode_valid` is
true (Virtual Stock enabled); an invalid mode must read 0. The family keeps the
schema-2 version: the experiment-era `transition_mode` and
`transition_motion_gate_*` fields were removed before landing, so no released
recording contains them. The feature contract is in
`docs/VIRTUAL-STOCK-AIM-CONTINUITY-2026-09-27.md`.

The `two_hand_lab_*` family is the same kind of additive schema-2 family for
the runtime-only Two-Handed Lab rig (VS-off anchor selection, soft off-hand
authority, temporal damping experiments). `two_hand_lab_enabled` means the Lab
steered that frame's canonical solve; while false the family reads its
inactive defaults (enums 0, scalars 0, validity false, zero vectors), never a
stale observation. The stateless observation (requested/resolved anchors,
fallback, agreement, confidence, requested/effective influence, selected
pivots, stateless direction) comes from the canonical solve's trace Lab
payload; the presented orientation (`two_hand_lab_presented_direction`,
valid only while `two_hand_lab_temporal_active`) comes from the Lab temporal
packet/state for the same serial. Raw live aim stays raw in
`aim_trace`/`canonical_aim`. The family contract is in
`docs/TWO-HAND-LAB-2026-09-27.md`.

The `two_hand_transition_*` / `two_hand_smoothing_*` fields are an additive
schema-2 family. `two_hand_smoothing_strength` is the user amount in [0,25]
(0 = off/raw, 25 = the full fixed speed-25 filter) and `two_hand_smoothing_mix`
is `strength / 25` in [0,1] derived from that same frozen strength: the applied
wet/dry amount, never a filter speed. `two_hand_smoothing_alpha` is the
filter's **internal** `clamp(25 * dt, 0, 1)` temporal coefficient and is never
the user amount. The primary orientation error is degrees and the
primary/support position errors are OpenXR LOCAL metres. Error values are
raw-to-full-filtered input differences; they do not describe solver output or
presented-aim error. `two_hand_offhand_influence` rides beside the family (not
inside it) as the free two-hand (VS-OFF) product offhand directional authority
in [0,1], frozen per frame from the frame's own assembly and never consumed by
Virtual Stock solves or by a Lab-active solve (which uses
`two_hand_lab_offhand_influence`). The latch continuity the family's
`two_hand_transition_*` flags report is fixed-on internal product behaviour with
no user toggle; the retired `two_hand_transition_smoothing` key no longer
resolves. Older recordings may omit the family, recordings from the earlier
boolean era carry it without the strength/mix keys, and a capture written before
`two_hand_offhand_influence` existed omits that key - absence in every case is
not a zero value.

The `persistent_support_grip_*` / `support_*` family is the same kind of
additive schema-2 family, recording what the persistent-grip (PG)
qualification gave the frame's **own** canonical assembly. It is frozen
solve-time provenance, never a live re-read of mutable PG state and never a
second assembly: `persistent_support_grip_configured` is the config knob read
for that frame and `persistent_support_grip_applicable` the pure title gate;
`support_relationship_readable` / `support_relationship_engaged` /
`support_epoch` are the assembly's frozen relationship qualification;
`support_solve_trusted` is the qualification **permission** (this invocation
proved the relationship owner) - whether the solve actually *consumed*
support geometry is a separate, unrecorded narrowing, so this field must not
be read as a consumption receipt; `support_force_one_hand` is **explicit PG
forcing** of that invocation to the one-hand path and is distinct from an
ordinary `effective_settings.two_hand_enabled == false`, which conflates
config-off, dual presentation and forcing. `support_epoch` is a plain unsigned
integer so the all-ones `18446744073709551615` sentinel ("the relationship
could not be read") survives the wire; it is deliberately not zero, because
epoch 0 means the relationship was coherently readable and disengaged - an
unreadable relationship must never be read as a disengaged one, and epochs are
process-local values that must never be compared across sessions.
`support_solve_serial` is the solve serial the assembly stamped: at capture it
is the last published prepared serial, normally `prepared_serial` minus one,
and it must never be asserted equal to `prepared_serial`. With PG off the
qualification short-circuits and the copied fields read their frozen
zero/false defaults, so a PG-off frame never reports a stale receipt; a
recording from before this family simply lacks its keys.

The shots-vs-reticle families (`presented_aim_*`, `reticle_presented_*`,
`engine_aim_*`, `engine_camera_*`, `world_scale_*`, `dual_active`, plus the
reticle/steering settings and weapon pad additions) are the same kind of
additive schema-2 families for "shots vs reticle" troubleshooting:

- `presented_aim_forward` is a same-frame reconstruction of the composed
  steering ray: the frame-local solve orientation, then the VS continuity
  presentation, then the Lab temporal presentation, exactly as
  `VR_GetAimPoseWithSupportProvenance` composes them, including its final
  solve-validity gate. `presented_aim_valid` is true only when every stage is
  provably that getter's output for the frame's own assembly; otherwise it is
  false and the `transition_*` / `two_hand_lab_*` piece fields carry the
  evidence (never a fabricated ray).
- `reticle_presented_*` is the exact consumer-visible reticle pose with its
  own serial. Capture runs before the reticle block publishes in the same
  frame, so the pose normally lags one prepared frame; the serial travels in
  the field and must never be asserted equal to `prepared_serial`.
- `engine_aim_*` / `engine_camera_*` live in the engine-world domain (Blam
  Z-up, world units; forward = `(cos p cos y, cos p sin y, sin p)`), never
  OpenXR LOCAL. The source enums name the exact publication per title
  (shared H3/ODST aim and camera singletons are title-qualified at read;
  Reach seated truth prefers native unit aim with the compact fallback
  labelled; Halo 4 carries its observer aim/pitch serial and publishes its
  pristine observer camera position from the stereo transaction with its own
  serial, source 6 `H4Observer`, while a frame without a usable publication
  stays source 5 `H4Absent`; Halo 2 carries its observer stock; Halo CE has
  no publication). Latest-only sources read serial 0; sampleMs 0 means no
  sample clock.
- The Halo 4 stock-camera publication is version-counter bracketed
  (`stockCameraVersion`): the recorded position, validity and serial always
  come from one whole writer iteration, or the read fails open to source 5
  `H4Absent` with exact zeros and serial 0. The pre-existing Halo 4
  engine-aim publication is **not** version-bracketed: its payload is stored
  before its serial is bumped, so the recorded `engine_aim_serial` may differ
  from its payload by one writer iteration. Treat the pair as serial ±1 and
  never assert an exact serial-to-payload pairing for source 6; the vector is
  still a genuine published sample, not a fabricated one.
- Invalid payloads in these families read exact zeros (never stale,
  never non-finite) while the source still names the attributed publication;
  validity flags stay authoritative.

### 8.2 Identity and timing

`prepared_serial` is the preferred frame identity. Duplicate captures of the
same serial are suppressed at admission, so a recording should not contain two
frame records for the same prepared serial. `capture_begin_qpc` and
`capture_end_qpc` sit around the capture and copy of the record (the end
timestamp is written at the ring publication boundary), so their difference is
a usable upper-bound cost signal for the hot path.

`contact_space_epoch` and `contact_space_change_at_ns` describe reference-space
discontinuities. A change of epoch means frame-to-frame relationships across
that boundary may not be continuous; analysis must not silently compare poses
across an epoch change.

### 8.3 Semantic versus physical routing

`session_start` declares how to interpret the two pose families:

- **semantic** poses (`semantic_primary_*`, `semantic_support_*`) are
  aim-action poses after MCC handedness role routing;
- **physical** poses (`physical_left_aim`, `physical_right_aim`) are aim-action
  poses before semantic handedness role routing.

Both are recorded on purpose. Do not treat one as a duplicate of the other, and
do not infer semantic roles from physical names.

### 8.4 Source selection and provenance: the support-grip example

A useful telemetry field answers not only **what value was observed**, but also
**what that value means and when it is applicable**. The support endpoint is
the model for source-selection provenance, and it is a real product path (the
OpenXR support grip-pose endpoint used by the "Reduce Support-Hand Rotation"
option), not an obsolete research artefact:

```text
support_grip_position            position-only OpenXR grip-action locate
support_grip_valid               whether that locate is usable at all
support_endpoint_used_grip       which source the VS/Lab support-endpoint selector chose
semantic_support_endpoint        the selected support endpoint for those VS/Lab paths
semantic_support_endpoint_valid  whether that selected endpoint is usable
```

This separates **what was sampled** from **what the selector chose** and from
**what that selection is used for**. The selection is provenance for the
Virtual Stock support path and the Two-Handed Lab Production anchor
pass-through; it is **not** a consumption receipt for the free two-hand product
geometry. With Virtual Stock off (and the Lab inactive), the two-hand solve
consumes the fixed primary-Grip -> support-Grip positional pair reported by
`support_grip_position` / `semantic_primary_grip_position` and does not read
`support_endpoint_used_grip` or `semantic_support_endpoint` at all (both are
still reported for the frame's fresh support sample, so their presence must not
be read as consumption). Reuse this pattern wherever several sources can
provide similar data; the `effective_settings` block also records the relevant
enabling settings (for example `support_grip_pose_enabled`) so a frame explains
its own configuration context.

### 8.5 Validity is authoritative

A numeric/vector/pose payload is not valid merely because it contains finite
numbers. When a validity flag exists, it decides whether the payload may be
interpreted:

```text
support_grip_valid = false, support_grip_position = [0, 0, 0]
```

does not mean the grip pose was located at the origin; it means the position is
not applicable. Examples of explicit validity relationships in schema 2 include
(the manifest annotates the analyser's known-validity pairings; the
head-sample/HMD pairing is declared by the analyser's signal bindings; the
`transition_*` pairings below are enforced by `tools/validate_telemetry_jsonl.py`
and are not listed in the analyser's known-validity table, because that
analyser's approved byte identity is pinned by the packaging and install gates):

```text
semantic_primary_aim_valid         -> semantic_primary_aim / semantic_primary_forward
semantic_support_aim_valid         -> semantic_support_aim
semantic_support_endpoint_valid    -> semantic_support_endpoint
physical_left_aim_valid            -> physical_left_aim
physical_right_aim_valid           -> physical_right_aim
support_grip_valid                 -> support_grip_position
semantic_primary_grip_position_valid -> semantic_primary_grip_position
head_sample_valid                  -> semantic_hmd
upcoming_views_valid               -> views
pad.valid                          -> pad
canonical_aim.valid                -> canonical_aim payload
transition_live_calibrated_forward_valid -> transition_live_calibrated_forward
transition_presented_forward_valid -> transition_presented_forward
transition_one_hand_anchor_valid   -> transition_one_hand_anchor_forward
transition_stock_mode_valid        -> transition_stock_mode (0 = Standard, 1 = Plus; an invalid mode reads 0)
two_hand_lab_enabled               -> the whole two_hand_lab_* family payload
two_hand_lab_primary_pivot_valid   -> two_hand_lab_primary_pivot
two_hand_lab_support_pivot_valid   -> two_hand_lab_support_pivot
two_hand_lab_stateless_direction_valid -> two_hand_lab_stateless_direction
two_hand_lab_presented_direction_valid -> two_hand_lab_presented_direction (requires two_hand_lab_temporal_active)
```

`aim_trace` and the counterfactual sections contain additional applicability
and validity flags. Rules:

- obey the validity flag or documented applicability rule; never interpret an
  invalid payload anyway;
- do not infer validity from a non-zero, finite, plausible, or
  previously-valid value;
- invalid/stale payload values may remain present in the raw record; do not
  "simplify" the schema by zeroing or nulling them, because that destroys
  recorded forensic state;
- the manifest only declares a validity pairing that is known from the schema
  or an explicit, tested mapping. Similar names are not enough to invent a
  semantic relationship.

### 8.6 Coordinates and time

`session_start` records the interpretation needed to read frame data. Current
declarations:

```text
tracking space:        OpenXR LOCAL
position units:        metres
quaternion order:      x,y,z,w
coordinate handedness: OpenXR right-handed
up axis:               +Y
forward axis:          -Z
```

It also describes semantic routing, physical left/right routing, HMD/view
provenance, stereo view semantics, grip-position provenance (including that
a position-only grip locate is valid only when its validity flag is true), and
the provenance of the free two-hand product controls
(`two_hand_smoothing_provenance`, `two_hand_offhand_influence_provenance`).

Do not rely on undocumented assumptions about these semantics, and do not
assume two vector fields share a coordinate domain merely because both are
vectors. If the meaning of a recorded observation changes, the schema and the
`session_start` declarations must change accordingly.

QPC values are process-local. QPC values from different captures or processes
do not share an origin unless analysis explicitly establishes a common basis.
Session-relative seconds are a derived convenience, not an identity; the
manifest publishes the timebase formula it used.

### 8.7 Session envelope and accounting

`session_start` captures recording-wide metadata: telemetry schema, source build
commit, process id, UTC start wall clock, recorder-declared filename, QPC
frequency, ring capacity/drop policy/worker cadence, coordinate declarations,
the Test Profile registry with semantic roles, and the diagnostic-override
registry.

`session_end` is the clean-stop accounting record. It includes duration,
producer calls, duplicate suppression, enqueued/written frame counts, queue-full
drops, writer failures, first/last written prepared serial, and sparse-event
enqueued/written/drop/skip counts.

These accounting identities are evidence about the quality and completeness of
the capture:

```text
producer_calls == duplicate_serial_suppressed + enqueued + dropped_queue_full
written        == enqueued                      (clean stop)
```

The analyser additionally checks `written` against the parsed frame count and
the event counters against parsed event/gap records. Never hide a dropped or
lost observation by pretending the stream was complete; drops are counted,
reported, and (for events) represented as explicit gaps.

## 9. Sparse event model

Not every useful observation belongs in `TelemetryFrame`. MCCVR has a separate
fixed-size sparse event transport in the same JSONL stream, introduced for
weapon-order investigations. The current public record types are
`weapon_event` and `event_gap`. Despite its origin, this is the model to reuse
whenever the important evidence is an asynchronous event or a cross-thread
ordering fact rather than a value sampled once per prepared frame.

### 9.1 The weapon-order channel as the model

A `weapon_event` contains:

```text
seq               process-wide monotonic sequence (evidence order)
session           recorder admission generation at publish
qpc               publish timestamp
thread_id         publishing thread
kind              event kind (markers/probes around Present, capture and
                  first-person weapon commit paths)
status            outcome classification
title             title id
title_generation  title generation/lifecycle token
prepared_serial   frame identity at publish time, where known
controlled_unit   game unit/entity id, where known
primary_weapon    weapon identity, where known
aux0, aux1        kind-specific payload
```

Current kind names: `present_begin`, `after_present_before_prepare`,
`capture_pre_latch`, `capture_probe_begin`, `capture_probe_result`,
`fp_entry`, `fp_weapon_commit`, `shot`. Current status names include `success`,
`no_observation`, `reader_returned_false`, `guard_rejected`,
`exception_or_fault`, `not_attempted_no_safe_thread`,
`not_attempted_thread_mismatch`, and `definitively_absent`.

`aux0`/`aux1` are kind-specific; currently, `capture_probe_result` carries the
matching `capture_probe_begin` sequence in `aux0`. Any new use must document
and test the exact meaning per kind. Do not create undocumented bit soup.

#### The `shot` kind (firing-path shot evidence)

`shot` is the first kind whose record is per-shot rather than per-weapon-order
marker. It carries one fixed payload in addition to the shared envelope
(`prepared_serial`, `controlled_unit`, `primary_weapon`, `title`,
`title_generation`, `qpc`, `seq`):

| Field | Meaning |
| --- | --- |
| `shot_origin`, `shot_direction` | The **final** ray the engine consumed, in engine-world units, recorded after the title's own aim call and after any substitution. A non-finite ray is suppressed by the producer (never published as a null ray). A `[0,0,0]` direction means the producer's firing call-site carries no direction (ODST's firing-origin hook), never a fabricated ray. |
| `shot_engine_aim` | The title's engine aim feedback snapshot in engine-world units (Halo 3/ODST shared aim publication; Reach seated native or shared compact; Halo 4 stereo-transaction observer aim; Halo 2 observer stock forward). Zeros when that publication is unavailable or carries no usable direction, by the same rule as the frame's `engine_aim_*` capture (read success plus a finite, non-zero-length forward). |
| `shot_engine_aim_source` | Provenance ordinal for `shot_engine_aim`, using the frame's `engine_aim_source` vocabulary: `0` none, `1` Halo 3 shared, `2` ODST shared, `3` Reach seated unit, `4` Reach seated compact, `5` Reach on-foot compact, `6` Halo 4 observer, `7` Halo 2 observer, `8` Halo 2 absent. `0` and `8` mean no usable snapshot was available for that shot (the read failed or the direction was unusable), and the aim vector is then exactly zero. |
| `shot_reticle_direction` | The consumer-visible presented reticle forward in OpenXR LOCAL. The reticle publication lags the frame by construction, so this is normally the previous prepared serial's pose; no serial equality with `prepared_serial` exists or is asserted. Zeros when the publication is unavailable. |
| `shot_slot`, `shot_barrel` | `0`/`1` when the firing context carries them; `-1` when it does not (in-memory sentinel `0xFF`, mapped at serialization). Never a fabricated identity. |
| `shot_flags` | Eight documented bits, unknown = 0 (below). |

```text
shot_flags bit0  predicted        the title's fire call carried prediction
shot_flags bit1  substituted      this mod's independent path produced the ray
shot_flags bit2  vrAimActive
shot_flags bit3  twoHandActive    bounded read of the shared two-hand bit
shot_flags bit4  leftHanded       captured MCC handedness
shot_flags bit5  dualActive       shared dual-presentation predicate
shot_flags bit6  firesFromCamera  the title's aim call carries the flag
shot_flags bit7  unitAim          the title's aim call carries the flag
```

Per-title producers (all five supported firing paths; Halo CE produces no
`shot` records):

- **Halo 3** publishes from the firing invocation of its own aim call
  (`Halo3IndependentAimDetour`), after the native call, whether or not the
  independent path substituted the ray. bit0 is the native fire call's own
  prediction flag; `substituted` is true only when the independent ray rewrite
  produced the final direction or when the committed muzzle ray was the
  engine's input. Slot rides the acquisition scope, barrel rides the per-shot
  muzzle request.
- **Halo 4** publishes from the firing invocation of its muzzle aim call, after
  the native call, whether or not the committed muzzle barrel produced the ray.
  Its four-argument fire scope carries no prediction flag, so bit0 stays 0
  (unknown) there rather than being inferred from the simulation byte.
- **ODST** publishes from the weapon-barrel call-site's single call into
  `unit_get_camera_position` (the runtime-derived firing return address). The
  origin is the `out[]` value the engine's own barrel function will consume:
  the rendered eye when the seated-origin substitution wrote it (`substituted`
  true), the engine's stock value otherwise. **The direction is absent by
  construction** and is published as an exact `[0,0,0]`; slot, barrel, weapon
  and predicted are not carried by this call-site and stay unknown (`-1` /
  the envelope sentinel). The event is published on every invocation of the
  firing call-site, including the cinematic-locked, foreign-unit,
  barrel-firing and invalid-camera branches, because those shots still consume
  an origin the evidence needs.
- **Reach** publishes one event per `unit-adjust` invocation. That transaction
  is the engine's projectile-builder call (HREK `0xD67EE0`; the pinned retail
  module has exactly one image caller, the projectile transaction at
  `0x004C303A`), so every invocation the hook serves is a firing invocation.
  Final origin/direction are read after the body: post-substitution when the
  on-foot hand ray, the committed muzzle barrel, or the reticle redirect
  produced the ray (`substituted` true), the engine's stock values otherwise.
  Weapon and barrel come from the firing transaction's own request context
  where present, else unknown; slot is unknown; bit0 (predicted) stays 0
  (unknown), never inferred. **Reach consumes no prepared serial inside this
  weapon transaction**, so `prepared_serial` is the latest published serial —
  a bounded snapshot, never an equality claim.
- **Halo 2** publishes from the firing invocation of its weapon-firing
  transaction's own aim call (`Halo2IndependentAimDetour`, fired by the single
  retail caller of the aim helper `+0x8F0F70` at `+0x8E4FCD`, inside the firing
  function `+0x8E4940`), after the native call, whether or not the independent
  path substituted the ray. `substituted` is true when this mod produced the
  final direction: either the committed muzzle barrel was the native call's
  input (the mod then passes camera projection/unit aim `0/0`, so bits 6/7 are
  reported false) or the controller-carrier rewrite produced the returned
  direction. Slot is the acquisition scope's resolved slot when that scope owns
  the firing unit, else unknown. Weapon and barrel are the firing transaction's
  own arguments carried by the per-shot request (barrel unknown where the
  argument is not a barrel index). The aim call carries no prediction flag, so
  bit0 stays 0 (unknown) rather than being inferred from the fire scope's own
  byte. **The Halo 2 firing path consumes no prepared serial inside this
  weapon transaction**, so `prepared_serial` is the latest published serial —
  a bounded snapshot, never an equality claim.

  *Halo 2 coverage boundary (documented cut).* The producer observes exactly
  the firing invocations that execute that aim call. The mod's own
  independent/dual substitution happens at the same call-site, so those shots
  are observed by construction. The H2EK-proven helper has exactly one code
  caller in the pinned module, and that call sits inside the engine's
  per-weapon firing function (`+0x8E4940`), i.e. inside the ordinary
  weapon-firing transaction rather than a dual-wield-only branch; other firing
  invocations that reach it (the local player's single-wield shots, other
  actors' shots) are therefore published too, unfiltered like the other
  titles. What is **not** claimed: a firing shape that returns from the
  engine's firing transaction before that call (a weapon-type or state
  early-out is not visible from the call-site), and any non-weapon damage
  source (melee, grenades, vehicle collisions). Such a shot produces no `shot`
  record and cannot be distinguished in the capture from a shot that never
  happened; observing it needs a new hook on that path, which is deliberately
  outside this recorder-only change. **Verify in a headset capture**: whether
  the local player's ordinary single-wield shots reach the call — look for
  `shot` records with `shot_flags` bit1 clear while single-wielding. If they
  are absent, that path returns before the call and this producer covers only
  the invocations that do reach it. The event is per invocation of the aim
  call (the same granularity as the other titles' fire hooks), never a
  trigger-pull or ammunition counter.

**Local-actor qualification (investigated, deliberately unfiltered).** These
hooks run on shared engine weapon functions, so an invocation can belong to a
unit the local player does not control; the events therefore describe local
*and* other actors' firing invocations. A filter was investigated for each
title and deliberately not added, because every candidate local-identity helper
is used in its subsystem with an explicit unknown/excluded arm and would drop
some of the local player's own shots if reused as a hard predicate:

- Halo 3: `Halo3IndependentTargetStorage` qualifies `unit ==
  g_halo3PlayerUnitGetter(0)` but deliberately refuses controlling-parent
  (in-vehicle) units, and native firing can hand the aim call such a unit.
- ODST: the firing hook already compares the engine's unit against
  `g_odstSeatedPlayerUnit` (which is `g_odstPlayerUnitGetter(0)`) for
  substitution; the value is refreshed by the camera probe and reset to `-1`
  on teardown, and driver/mounted-gun shots intentionally never reach the
  substitution arm.
- Reach: `LegacyCollisionIgnoredObject(HaloReach)` is used inside this very
  transaction, but only as one arm of the ownership check (`-1` is explicitly
  handled as unknown) and it is not the identity used for the vehicle branch.
- Halo 4: the muzzle target-storage helper requires `!Halo4ReadVehicleInput(
  seat).seated`, so it cannot identify the local player's in-vehicle shots.
- Halo 2: the firing-path ownership helper `Halo2IndependentTargetStorage`
  refuses any unit with a controlling parent (`+0x260 != UINT32_MAX`) and
  `Halo2ReadIndependentWeapons` reads only output user 0's two weapon slots, so
  a hard filter would drop the local player's own in-vehicle/mounted firing
  invocations. The producer therefore publishes every firing invocation the
  shared firing transaction's aim call serves.

Volume may therefore rise relative to a local-only expectation, and drops and
gaps remain fully accounted by the channel; use `title`, `controlled_unit` and
the install state in `HaloMCCVR.log` before drawing actor conclusions.
**Never "fix" this by filtering on a helper that can return unknown or that
excludes a local case**: dropping the local player's own shots is worse than
observing other actors' shots.

**Firing-path producers are gate-qualified evidence.** The producer's first
action is the disabled gate (a single admission load) before any diagnostic
read; when telemetry is not accepting it performs no diagnostic read at all.
Because ODST's and Reach's firing hooks are shared with gameplay timing, those
producers cannot return early; they still perform zero diagnostic reads past
the gate. A `shot` record therefore requires that the title's firing-path hooks
exist for that build, that they are installed for the running title, and that a
recording is active. **Absence of `shot` records does not mean no shots were
fired**: it means this channel did not observe them. Do not use the sparse
event channel as a shot counter, and do not compare a capture's `shot` count
against gameplay statistics without checking the recorder/health accounting and
the title's install state in `HaloMCCVR.log`.

The channel's admission, generation, sequence and gap semantics are unchanged
by the `shot` kind: `event_gap` markers cover exactly the dropped or
cross-session claims, and an analysis whose interval crosses a gap stays
indeterminate ([§9.2](#92-ordering-sequence-and-gap-semantics)).

The event channel is read-only diagnostic instrumentation. Producer gates are
extremely cheap when recording is off, and producers may perform only bounded
read-only observations (see [§7](#7-hot-path-and-performance-contract)). A
producer that requires allocation, locks, I/O, formatting, or extra runtime
sampling is not admissible.

### 9.2 Ordering, sequence and gap semantics

- Multiple known producer threads claim a process-wide monotonically increasing
  sequence. The claim counter is never reset across sessions, so a sequence is
  unique even across Stop/Start.
- Publication is atomic to the consumer via a per-slot commit marker; payload
  fields are read only after the exact committed sequence is observed.
- If a claimed sequence can no longer arrive (for example, queue pressure
  dropped the event, or the producer's session ended before commit), the worker
  emits an explicit gap record:

```json
{"type":"event_gap","first_missing_seq":N,"last_missing_seq":M}
```

- A gap is not cosmetic. The worker emits a gap only after it can prove the
  sequence is definitively lost (no producer in flight, marker absent) or that
  the claim belongs to a different session (cross-session claims are counted
  and gapped). Current recorder behaviour emits one gap per verified-missing
  sequence (`N == M`); the fields can express a range, but consumers must not
  assume gaps are always singletons.
- During the final drain, the claim frontier is immutable (admission disabled,
  producers quiesced) and every claimed sequence must be serialized or
  explicitly gapped before `session_end` ([§6.5](#65-finalisation)).
- The analyser treats any ordering conclusion whose supporting interval crosses
  a gap as indeterminate: "event gaps always win over any ordering
  conclusion." Weapon-order transition outcomes include explicit
  `INDETERMINATE_*` variants (dropped events, missing entries/commits, owner
  mismatch, overlap, and other rejected evidence) in addition to positive
  classifications. A completed positive observation is not erased by a later
  failure, but a gap blocks classification rather than closing the sequence
  silently.

### 9.3 Choosing between frame, event, analyser and log

| Need | Preferred layer |
| --- | --- |
| Reusable same-frame state compared with other same-frame observations | `TelemetryFrame` field |
| Sparse transition, callback/lifecycle edge, cross-thread ordering | Sparse event channel |
| Threshold, ranking, episode/threshold selector, simulation, hypothesis-specific classification | Analyser diagnostics |
| Low-rate lifecycle/error information only | `HaloMCCVR.log` |

Prefer a frame field when the value has meaningful same-frame state, comparing
it with other frame observations is the main use, sampling once per prepared
frame is sufficient, and the value is already available in the frame's captured
runtime/game state.

Prefer a sparse event when the observation occurs only on a specific
callback/lifecycle transition, ordering across threads/callbacks is itself the
question, the event may occur zero/one/several times between prepared frames,
or forcing it into one-sample-per-frame form would lose causally relevant
order.

Prefer analyser-only derivation when the proposed "signal" is actually a
threshold, a ranking, an episode/candidate selector, a difference/ratio/angle
computable from already recorded facts, a hypothesis-specific classification,
or an offline simulation.

Do not emit an event every frame merely to avoid editing `TelemetryFrame`, and
do not add a frame field for a sparse cross-thread edge merely because frame
JSON is easier to query.

## 10. Analyser

The canonical analyser source is:

```text
tools/telemetry_analyser/analyse_mccvr_telemetry.py
```

The packaged runtime copy is:

```text
<mod folder>\TelemetryAnalyser\analyse_mccvr_telemetry.py
```

The analyser is a standalone Python-standard-library tool (no third-party
dependencies). MCCVR launches it best-effort after a clean close (see
[§6.6](#66-analyser-launch)); a human can run the same script manually at any
time. The analyser is not runtime truth; it is a derived-aid and navigation
tool over authoritative raw evidence.

Current responsibilities:

- generate the neutral manifest and the AI orientation guide;
- generate structured/readable derived diagnostics;
- provide deterministic raw-navigation and query helpers;
- support explicit multi-capture comparisons;
- provide self-tests for its own parsing/derivation layers;
- analyse sparse weapon-order evidence.

The analyser never modifies the raw recording.

### 10.1 Sidecar version identities

Do not conflate these version domains:

| Domain | Identity | Current value |
| --- | --- | --- |
| raw telemetry schema | `kTelemetrySchemaVersion` (recorder) | 2 |
| manifest schema | `manifest_schema_version` | 1 |
| diagnostics schema | `diagnostics_schema_version` | 1 |
| analyser implementation | generator `version` + `sha256` | `6.3.0-standalone` + file SHA-256 |

A new analyser version does not automatically require a new raw/manifest/
diagnostics schema. Bump only the schema whose consumer-visible contract
actually changed.

### 10.2 Source binding

Canonical sidecars are cryptographically bound to their source capture using
the SHA-256 of the exact input file bytes (hash basis
`exact_input_file_bytes`), plus supporting identity and shape metadata: size,
telemetry schema, build commit, frame count, and first/last prepared serial.

A manifest whose binding does not match a supplied JSONL is stale or belongs to
another capture and must not orient analysis of that file. `--verify-manifest`
checks binding (hash, size, manifest kind, frame count, serial bounds) and
reports match/mismatch with a non-zero exit on mismatch.

For multi-capture diagnostics, every contributing source remains independently
bound. Diagnostic `source_id` values are content-derived
(`sha256:<content hash>`), not filename/order identities, and raw locators carry
source and physical-line information so a derived result can be traced back to
the records that produced it.

### 10.3 Manifest responsibilities (summary)

Per [§5.2](#52-manifest--neutral-mechanical-map), the manifest is exhaustive but
dumb. Its neutrality is a feature: it must not encode the current research
question. Mechanical discovery covers unknown fields and record types; add a
special manifest interpretation only when a relationship is truly part of the
schema.

### 10.4 Diagnostics responsibilities (summary)

Per [§5.3](#53-diagnostics--optional-derived-aids), diagnostics disclose their
method, parameters, inputs, locators and classification. Current method
classifications include deterministic derivation, heuristic index, mixed
heuristic/deterministic metrics, offline simulation, counterfactual and health
check. JSON and Markdown render from the same analysis model; calculations must
not be implemented twice. Diagnostics must not emit product verdicts.

### 10.5 Raw navigation and query presets

Raw navigation returns original raw lines without reinterpretation for the
selected records (blank or unparseable lines are skipped rather than
reinterpreted). Exactly one recording and exactly one selection mode are
required:

```powershell
$analyser = ".\TelemetryAnalyser\analyse_mccvr_telemetry.py"
$recording = ".\Telemetry Recordings\HaloMCCVR-Telemetry-....jsonl"

# Full analysis: writes manifest, diagnostics and guide beside the recording
python $analyser $recording

# One deterministic query result (use --query list for preset names)
python $analyser $recording --query list
python $analyser $recording --query transitions
python $analyser $recording --query performance

# Raw retrieval around one prepared serial (defaults: 20 before/after)
python $analyser $recording --raw-window-serial 12345 --before 20 --after 20

# Raw serial range and raw physical-line range
python $analyser $recording --raw-range-serial 12300:12400
python $analyser $recording --raw-range-lines 500:650

# Optional record-type filter and file output
python $analyser $recording --raw-range-lines 500:650 --raw-record-type frame --raw-output .\window.jsonl

# Manifest binding check
python $analyser $recording --verify-manifest ".\Telemetry Recordings\<stem>.manifest.json"

# Sparse weapon-order evidence
python $analyser $recording --weapon-order

# Self-tests
python $analyser --self-test
python $analyser --weapon-order-self-test
```

Query presets currently include: `profile-provenance`, `profile-changes`,
`settings`, `grip-held-aim-inactive`, `latch-release-while-grip-held`,
`b-boundary`, `b-chatter`, `w-transitions`, `head-yaw`, `horizontal-reach`,
`low-held-retention`, `steady-jumps`, `all-jumps`, `singularity`,
`control-oracle`, `authority`, `transitions`, `performance`, `pg-lab`,
`pg-lab-events`, `events`.
`--query list` returns the sorted set. `--query` currently requires exactly one
recording.

### 10.6 Multi-capture analysis

Multiple positional recordings enable cross-run comparison; explicit combined
reports are written with `--combined-report` / `--combined-json`. Each source is
independently bound in the combined document scope, and per-capture sidecars are
still written beside each recording. Compatibility grouping (schema/build)
decides whether runs may be pooled; incompatible builds remain per-run even when
condition identity matches.

### 10.7 Determinism and console behaviour

Given the same final raw bytes, generator version and relevant options, the
canonical manifest semantic content is deterministic; sidecars are written only
when content changes, and canonical output avoids gratuitous nondeterministic
values. The analyser also configures its console streams so Unicode diagnostics
do not break legacy Windows console encodings when output is not redirected.

## 11. Validation and test layers

Three layers guard telemetry; a change can require updates in more than one.

| Layer | Executable / fixture | Proves |
| --- | --- | --- |
| C++ recorder/transport tests | `halomccvr_telemetry_tests` | Transport, admission races, finalisation, serialization, event gaps, fail-open, lifecycle/accounting |
| Strict JSONL validator | `halomccvr_telemetry_jsonl_tests` (`python -O tools/validate_telemetry_jsonl.py <fixture> --expect-fixture --self-test`) | Schema-2 frame/session contract and semantic invariants for the recorder fixture |
| Analyser self-tests + packaged-layout smoke | `--self-test`, `--weapon-order-self-test`, `halomccvr_telemetry_analyser_smoke` | Parsing, sidecars, binding, inventories, diagnostics, raw navigation, weapon-order algorithm, canonical output and pinned identity |

### 11.1 C++ recorder tests

`halomccvr_telemetry_tests` (with `HALOMCCVR_TELEMETRY_TESTING`) covers, among
other cases:

- ring FIFO behaviour, concurrent ring publication, drop-new accounting;
- serializer helpers: JSON escaping and finite-number handling;
- weapon-order transport: multi-producer sequence uniqueness, session
  generation isolation, stalled/uncommitted claims, queue-pressure drops,
  explicit gap emission, write failure not advancing the drain pointer, final
  claim-frontier drain, and no undrained tail carrying into a later session;
- analyser launch helpers: Windows argument quoting, UTF-8 environment
  construction, launch plan shape for `python.exe` and `py.exe -3`, fail-open
  path when the analyst script is absent, Unicode module directory handling;
- lifecycle/accounting: state transitions, duplicate suppression, clean Stop
  draining all enqueued frames, session provenance declarations, real-session
  event accounting.

### 11.2 JSONL validator

`tools/validate_telemetry_jsonl.py` answers: "Is this structurally and
semantically valid MCCVR telemetry for the schema it claims?" It validates the
recorder fixture (profile mapping and role consistency, per-frame field
presence/types, finite numerics — NaN/Infinity are rejected, validity/value
coupling, control-profile relationships, and session accounting identities).
Recoverable-unclean semantics:

- only an unterminated final line is treated as a trailing partial;
- a malformed line anywhere else is an error;
- a session without `session_end` is reported as recoverable unclean rather
  than hard-failing (the CTest invocation uses `--expect-fixture`, which
  additionally requires a clean, fully terminated fixture).

Important limitation: this validator's fixture path is **not** the sole proof of
correctness for every record family in a production mixed stream. The sparse
`weapon_event`/`event_gap` transport is proved by the dedicated C++ tests, and
the analyser has its own parser/manifest/event-inventory/sidecar self-tests.

### 11.3 Analyser self-tests and smoke

- `--self-test` exercises the analyser's neutral sidecar layer (manifest,
  guide, diagnostics envelope, raw tools, schema-1 compatibility paths,
  canonical output) including console-encoding safety.
- `--weapon-order-self-test` exercises the deterministic weapon-order
  transition algorithm, including gap/indeterminacy handling.
- `halomccvr_telemetry_analyser_smoke` launches the packaged-layout copy of the
  analyser through the real launcher path with Unicode directory/recording
  names, and requires the canonical sidecars (manifest, diagnostics JSON/MD,
  guide) with no legacy `_analysis` directory, the manifest binding fields,
  and the pinned analyser version/hash recorded in diagnostics.

### 11.4 Test obligations when telemetry changes

- transport/recorder change → extend `halomccvr_telemetry_tests` for the
  affected behaviour (and event-layer behaviour when the event path changes);
- frame/schema change → extend the recorder fixture and the JSONL validator so
  the new field's presence, type, finite values, validity semantics and
  cross-field invariants are checked;
- analyser change → run/update `--self-test`, the analyser smoke test,
  binding/inventory/determinism tests, diagnostics/locator tests, raw-navigation
  tests and multi-capture tests where affected;
- packaging/identity change → update all analyser byte/size identity pins
  together ([§15.2](#152-changing-analyser-bytes)).

A signal that cannot be distinguished as valid, invalid, missing, dropped or
stale is not ready to support a troubleshooting conclusion.

## 12. Extending telemetry: adding a new signal

Do not start by editing `TelemetryFrame`. Start with the question the new
evidence must answer. Telemetry grows by **evidence need**, not by curiosity.

### 12.1 The required decision process

```text
question (exactly what must become answerable?)
    ↓
inspect existing raw evidence and the manifest inventories
    ↓
try analyser-only derivation / raw queries first
    ↓
identify the genuinely missing neutral fact (if any)
    ↓
choose frame field vs sparse event
    ↓
define validity and provenance before code
    ↓
bounded implementation (capture + worker serialization)
    ↓
tests
    ↓
focused headset evidence
    ↓
only then interpret the result
```

### 12.2 Prove the existing schema is insufficient

Before adding raw data:

- inspect the manifest field/event inventory for the capture (or a fixture);
- inspect existing raw fields and validity flags;
- use raw-navigation queries to test whether the needed fact is already
  present;
- determine whether the desired value can be derived offline without losing
  information.

If the answer can be computed reliably from existing neutral facts, extend the
analyser instead of the recorder ([§13](#13-extending-analyser-diagnostics)).

### 12.3 Define semantics and provenance first

For a new signal specify, before implementation:

- stable field/event name;
- units and coordinate space where applicable;
- the exact source observation;
- sampling time and thread/callback domain;
- validity/applicability rule;
- handedness/semantic routing if relevant;
- relationship to existing frame/event identity;
- whether absence is meaningful or merely not observed;
- how a future investigator will know whether the value is usable.

Do not add a bare float/vector/pointer-derived id and leave future
investigators to guess what it meant. Do not record a pre-baked conclusion
where a neutral primitive will do:

```text
record:  position, validity, source selection, serial, event status
not:     probably_bad_grip_frame  (belongs in offline diagnostics)
```

### 12.4 Frame field extension

- capture only already-available same-frame state;
- keep the calculation bounded, allocation-free and side-effect-free;
- preserve `TelemetryFrame` type/size invariants (trivially copyable,
  standard layout, ≤2048 bytes) and publish through the existing fixed
  transport;
- serialize on the worker (off the hot path), keeping every emitted number
  JSON-safe/finite;
- consider the record-size cost: every frame field has permanent cost in
  record size, queue memory, file size, serializer complexity, validator
  burden and analyser compatibility.

### 12.5 Sparse event extension

- keep the record fixed-size and trivially copyable;
- make the disabled gate extremely cheap;
- avoid diagnostic-only reads when telemetry is not accepting;
- preserve generation/session admission and sequence/gap semantics;
- perform only bounded read-only observations;
- document `aux` semantics and per-kind/status meaning;
- add the event-layer tests ([§11.4](#114-test-obligations-when-telemetry-changes)).

If the desired signal requires new runtime sampling or a blocking/game-memory
operation, stop: that is not a routine telemetry-schema edit
([§7](#7-hot-path-and-performance-contract)).

### 12.6 Update neutral orientation

As applicable:

- decide whether the raw schema version must change ([§14](#14-schema-versioning-and-compatibility));
- update `session_start` declarations/registries when interpretation context
  changed;
- add explicit validity mappings in the analyser only when the relationship is
  truly part of the schema;
- update manifest inventory expectations if the analyser needs a special
  interpretation (prefer mechanical discovery);
- update the AI guide only when analysis-facing mechanics changed.

### 12.7 What not to add

Do not add a permanent per-frame value merely because:

- it is convenient for one session;
- it can be derived exactly from existing raw state;
- it is a thresholded analyser label;
- it represents a conclusion rather than an observation;
- it duplicates ordinary logging;
- it requires an extra runtime call only for curiosity;
- a compact id/provenance value would answer the same question.

Do not preserve an obsolete experiment forever merely because it once had an
analyser section. If a signal has become genuinely reusable subsystem evidence,
document its stable semantics. If it is only a hypothesis or derived measure,
keep it out of the raw recorder.

### 12.8 Naming guidance

Names should reveal the layer:

- `physical_*` — raw anatomical/controller-side source;
- `semantic_*` — handedness/title/product routing applied;
- `*_valid` — payload availability/authority;
- `*_used_*` — source-selection provenance;
- `effective_*` — settings/output actually selected for production;
- `cf_*` — diagnostic counterfactual/control, not production output.

Do not call a derived estimate "physical", and do not call a raw source
"effective". If compatibility forces an imperfect historical name to remain,
document its exact semantics in analyser bindings and tests.

### 12.9 Review checklist — new runtime signal

```text
[ ] What exact question does it answer?
[ ] Does the raw schema already contain the fact?
[ ] Could this be analyser-only?
[ ] Frame field or sparse event — why?
[ ] Exact source and sampling point?
[ ] Units / coordinate domain?
[ ] Validity representation and applicability rule?
[ ] Provenance/source-selection needed?
[ ] Reuses existing runtime/OpenXR samples?
[ ] Hot-path work bounded and allocation-free?
[ ] Record size remains reviewed and bounded?
[ ] Queue-full behaviour remains non-blocking with accounting?
[ ] Serializer updated off the hot path, JSON-safe/finite?
[ ] Validator updated?
[ ] Analyser binding/method actually needed?
[ ] Historical schema readability preserved?
[ ] Tests cover valid/invalid/missing/dropped/stale cases?
[ ] Analyser/package identity pins updated if analyser bytes changed?
[ ] Does this document need an architectural update?
```

## 13. Extending analyser diagnostics

Prefer analyser work over raw-schema growth whenever all necessary neutral facts
already exist. Diagnostics are the correct home for metrics, rankings, episode
detection, thresholding, heuristics and investigation-specific calculations.

### 13.1 Method requirements

A diagnostic method should expose, as applicable:

- method name and classification (deterministic derivation, heuristic index,
  simulation, counterfactual, health check, ...);
- purpose, without pretending the method proves its hypothesis;
- source raw paths/signal aliases and their supported telemetry schema
  versions;
- validity requirements; missing stays missing;
- thresholds/parameters actually used;
- formula/inclusion/exclusion rules;
- source-attributed raw locators for material results;
- context dependency if experiment context influenced selection.

Additional rules:

- respect validity flags; never coerce missing to zero for causal logic;
- distinguish deterministic derivation from heuristic/simulation/counterfactual
  output;
- do not imply causation from correlation and do not emit final product
  verdicts;
- keep default diagnostics compact; no second frame-by-frame dataset;
- render JSON and Markdown from the same model;
- self-test important logic, including schema-compatibility paths.

### 13.2 Review checklist — analyser-only extension

```text
[ ] Raw inputs already exist.
[ ] Validity flags are respected; missing stays missing.
[ ] Method classification is explicit.
[ ] Thresholds/parameters are disclosed.
[ ] Raw evidence can be located for material results.
[ ] No causal claim is inferred from correlation.
[ ] Deterministic logic is self-tested.
[ ] Existing manifest/raw behaviour remains neutral.
[ ] Pinned analyser identity regenerated if bytes changed ([§15.2]).
```

## 14. Schema, versioning and compatibility

### 14.1 Raw schema changes

Bump the raw telemetry schema when a consumer-visible raw contract changes in a
way that cannot be safely interpreted as the same schema, for example:

- changing field meaning, units, coordinate space or validity semantics;
- removing/renaming a required field;
- changing record identity/order semantics;
- changing the interpretation of an enum without preserving mapping/provenance.

A purely additive optional field may remain within the current schema only if
all existing meanings are unchanged, absence is unambiguous, older readers can
safely ignore it, and validator/analyser compatibility is deliberate and
tested. A purely additive change can still justify a schema bump when downstream
tools need to distinguish recordings with/without the capability; make that
decision explicitly rather than relying on parser accidents.

Do not reuse an old field name with a new meaning to avoid a schema bump, and do
not avoid a bump merely to make packaging easier.

### 14.2 Sidecar schema changes

Manifest and diagnostics schemas evolve independently of raw telemetry and of
the analyser executable/script version. Bump a sidecar schema when its
structured contract changes enough that a consumer must know which
representation it received ([§10.1](#101-sidecar-version-identities)).

### 14.3 Compatibility rules

When supporting more than one raw schema:

- branch explicitly on the recorded telemetry schema;
- define alias/path/validity mappings per schema where necessary;
- preserve missing/inapplicable data as missing, not zero/false;
- do not assume similarly named fields across schemas are semantically equal;
- exclude unsupported sources from a diagnostic method explicitly.

Schema-1 compatibility in the analyser is a maintained compatibility path, not
permission to create new schema-1 recordings.

### 14.4 Determinism and historical readability

Canonical sidecar content should be deterministic for the same final raw bytes,
generator version and options. Keep historical captures readable where
practical; never rewrite old JSONL to resemble a newer schema.

## 15. Packaging, installation and analyser identity

### 15.1 Analyser is program content; recordings are user data

CMake installs the canonical analyser to:

```text
TelemetryAnalyser\analyse_mccvr_telemetry.py
```

Candidate packaging records the analyser as a manifest-tracked file with exact
bytes and SHA-256, and runs the staged analyser's `--self-test`. Installation
verifies the candidate analyser, installs it under the mod folder, and verifies
the installed identity; update/rollback backs up and restores the analyser as a
versioned mod component. The parity gate keeps the CMake install destination and
the package manifest entry wired together.

`Telemetry Recordings\` is user-generated evidence:

- it is not a candidate-package input (packages contain no recordings);
- it must not be deleted or overwritten merely because MCCVR is updated;
- install/backup/rollback logic currently operates on the DLL, launcher,
  config, log and analyser only — recordings are outside those sets and must
  stay outside them;

Do not add a third-party dependency solely for diagnostic transport or
serialization without revisiting the hard safety/governance rule in `AGENTS.md`.

### 15.2 Changing analyser bytes

The packaged analyser is identity-pinned. If `analyse_mccvr_telemetry.py`
changes:

1. run the analyser `--self-test`;
2. run the relevant telemetry/analyser tests (including the packaged-layout
   smoke);
3. regenerate the approved byte size and SHA-256 pins (package script, smoke
   test, any other recorded identity);
4. preserve byte-stable line endings (`.gitattributes` pins the file as
   `-text -whitespace`);
5. package the exact analyser bytes that were tested;
6. update this document only if the contract changed, not for a routine
   re-pin.

Do not churn analyser bytes for cosmetic reasons without considering the
delivery contract.

## 16. Failure modes and troubleshooting

Telemetry is diagnostic: it must **fail open** with respect to gameplay.
Recorder, analyser, Python, filesystem, or diagnostic failure must not break
tracking, aiming, rendering, input, title behaviour, gameplay, or the OpenXR
session.

### 16.1 Recorder unavailable or start failure

Directory creation, worker/control-event creation, or file-open failure can
make the recorder `Unavailable`/`Error`. In that case the user may lose that
diagnostic recording; MCCVR continues unaffected, and `HaloMCCVR.log` records
the failure (`telemetry: recorder unavailable; VR startup continues`, plus
recorder-specific lines). Current error classes visible in the menu: log
directory unavailable, control event creation failed, worker creation failed,
session file creation failed, file write failed, file flush failed, file close
failed, memory allocation failed, internal recorder failure, recording
directory creation failed.

### 16.2 Queue pressure

Frame/event transport is bounded. When capacity is exhausted, observations are
dropped according to the documented policy and the loss is exposed through
accounting (frame drops) or explicit gap markers (events). Do not block the
gameplay thread waiting for diagnostic capacity. If drops increase, investigate
record growth, filesystem stalls, excessive event volume, unintended capture
frequency, or a worker regression — not by blocking the producer.

### 16.3 Write, flush and close failures

On a writer failure the recorder publishes an error state, stops accepting new
observations, and preserves as much of the session as it can. It must not alter
gameplay behaviour in an attempt to "save" telemetry. Failing to write the final
event frontier is treated as an internal failure, never as a clean stop.

### 16.4 Partial or corrupt recordings

Tools distinguish clean, recoverable trailing-partial and malformed-middle
conditions ([§5.1](#51-raw-jsonl--authoritative-evidence)). Absence from a
partially parsed capture must not be overclaimed as absence from reality.
Preserve the original bytes; use tolerant analysis where appropriate and treat
the result as provisional (sidecars for non-finalised captures are marked
`provisional`).

### 16.5 Missing analyser or interpreter

The raw recording is closed before any analyser launch is attempted. If the
analyser script is absent, launch is skipped before interpreter discovery; if
neither `python.exe` nor `py.exe -3` resolves, or process creation fails, the
failure is logged and the raw recording remains valid and available. Check
`HaloMCCVR.log` for the analyser lines and run the analyser manually if needed
([§10.5](#105-raw-navigation-and-query-presets)).

### 16.6 Analyser failure

An analyser error, a corrupt middle line, or a sidecar-generation failure does
not invalidate the raw capture. Sidecar generation is not transactional: a run
that fails partway can leave a manifest (and possibly the guide) without
diagnostics. The raw JSONL is never modified.

### 16.7 Sidecar disagreement

If independent raw-derived analysis disagrees with the manifest or diagnostics,
report the disagreement and verify against the raw source. Do not automatically
privilege the sidecar because the official analyser produced it
([§1.1](#11-telemetry-is-not-an-oracle)). A manifest that does not verify
against a recording must not be used to orient that recording; regenerate
sidecars from the exact raw file if the binding is stale.

### 16.8 Health before conclusions

Telemetry exposes its own reliability through session accounting, queue
counters, gap markers and parse/finalisation health. Before drawing a
conclusion that depends on continuity or ordering, confirm capture health. A
capture with a few drops may still answer a geometry question; it cannot answer
an exact ordering question.

## 17. Keeping telemetry general without turning it into a dumping ground

Telemetry should grow when real troubleshooting evidence is missing, but growth
must be deliberate. Before adding a field or event, ask:

1. What exact future question becomes answerable?
2. Can existing raw facts answer it already?
3. Can the value be derived offline without loss?
4. Is this neutral/reusable evidence, or an investigation-specific
   interpretation?
5. Does it belong on a prepared frame, on a sparse event edge, or only in the
   analyser?
6. What validity/provenance makes the observation interpretable?
7. What is the hot-path cost when recording is on and off?
8. What tests prove it cannot change gameplay and cannot silently lie?

Prefer small neutral primitives over pre-baked conclusions. Record the
positions/validity/source-selection facts needed to study a geometry issue; put
"near singular", "large jump", "candidate episode" and threshold-based
classifications in diagnostics.

Telemetry's value is not that schema 2 currently contains extensive Virtual
Stock signals. Its value is the reusable evidence pattern:

```text
observe → record bounded neutral facts → preserve raw evidence →
analyse offline → identify the missing fact → extend deliberately →
test the extension → change behaviour separately
```

Future investigations can reuse it for aiming, controller/hand behaviour,
title lifecycle, first-person weapon identity/order, vehicles,
rendering/presentation transitions, tracking/reference-space issues,
performance/deadline evidence, and other systems where ordinary logging loses
frame-level relationships. The recorder should remain **general enough to grow
and disciplined enough to trust**.

### 17.1 Cross-title honesty

MCC titles have different engine/runtime paths. A shared telemetry field should
either have genuinely shared semantics, or carry enough validity/title/source
provenance to prevent false equivalence. Never populate a shared field with
guessed title-specific data merely to avoid nulls. Missing but truthful is
better than populated but semantically inconsistent.

## 18. Canonical source map and maintenance

### 18.1 Primary implementation references

| Area | Canonical source |
| --- | --- |
| Recorder structs/API/schema version/sparse-event types | `src/dll/telemetry_recorder.h` |
| Recorder queues/worker/serialization/finalisation/analyser launch | `src/dll/telemetry_recorder.cpp` |
| Prepared-frame population and frame provenance | `src/dll/vr.cpp` |
| F1 telemetry controls/status | `src/dll/menu.cpp` |
| Sparse weapon-order publishers | `src/dll/vr.cpp`, `src/dll/game.cpp`, `src/dll/halo2_observer_6dof.cpp`, `src/dll/haloce_first_person.cpp` |
| Recorder init | `src/dll/dllmain.cpp` |
| Strict frame-fixture validator | `tools/validate_telemetry_jsonl.py` |
| Canonical analyser / manifest / diagnostics / guide / weapon-order analysis | `tools/telemetry_analyser/analyse_mccvr_telemetry.py` |
| Recorder/transport/analyser-launch/smoke tests | `tests/telemetry_recorder_tests.cpp` |
| Build/test/install wiring | `CMakeLists.txt`, `tools/package-candidate.ps1`, `tools/install-candidate.ps1` |
| Analyser byte pinning and wiring checks | `tools/package-candidate.ps1`, `tools/install-candidate.ps1`, `tools/check-reach-fp-parity.ps1`, `.gitattributes` |
| Hard diagnostic hot-path rules | `AGENTS.md` |

Historical experiment/research documents may explain why a signal exists, but
they are not the subsystem contract. Search the current tree rather than
assuming this table remains exhaustive.

### 18.2 Current implementation snapshot (not a contract)

The following values describe the working tree at the time of drafting and may
change; each must be updated with the process that owns it.

```text
raw telemetry schema:        2 (analyser reads schema 1 where its rules say so)
manifest schema:             1
diagnostics schema:          1
analyser version:            6.3.0-standalone
analyser package pin:        395283 bytes;
                             SHA-256 053CF61671BE281551B89A459E0611490F29D0FFD00387B43668043793BAE5B4
frame ring:                  4096 slots / 4095 usable / drop_new
sparse event queue:          1024 slots
TelemetryFrame size ceiling: 2048 bytes (compile-time); trivially copyable; standard layout
worker poll while recording: 10 ms
flush policy:                256 frames or ~1000 ms (checked during drains / each poll)
file buffer:                 256 KiB
recording location:          <mod folder>\Telemetry Recordings\
```

### 18.3 Documentation ownership and maintenance

Update this document when the subsystem contract changes, including: canonical
recording location; architecture/threading; queue/publication model; evidence
authority; schema/version rules; analyser responsibilities; packaging/install
rules; extension workflow. Do not update it for every leaf field.

Field-level truth belongs in `TelemetryFrame`, serializers, the validator,
analyser signal bindings and tests. Capture-orientation truth belongs in the
generated `TELEMETRY_AI_GUIDE.md`. This document is the durable explanation of
what telemetry is, how to use it, and how it is allowed to grow.

The verifier pass for any telemetry change must check this document, the
analyser-owned `TELEMETRY_AI_GUIDE.md`, CLI `--help`, F1 wording, and the actual
runtime paths/filenames for contradictions. If code and this document disagree,
fix the drift or explicitly update the contract and tests — do not add another
one-off note.
