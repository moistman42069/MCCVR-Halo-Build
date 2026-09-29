"""Validate the telemetry recorder's recoverable JSON Lines output."""

import argparse
import copy
import json
import math
from pathlib import Path


SCHEMA_VERSION = 2
PROFILE_ROLES = frozenset(
    (
        "none",
        "vs_off_control",
        "fixed_head_control",
        "fixed_shoulder_control",
        "tuning",
        "diagnostic",
    )
)
CONTROL_ROLE_BY_FIELD = {
    "cf_vs_off": "vs_off_control",
    "cf_fixed_head": "fixed_head_control",
    "cf_fixed_shoulder": "fixed_shoulder_control",
}


def reject_constant(value: str) -> None:
    raise ValueError(f"invalid JSON numeric constant: {value}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def is_finite_number(value) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def approximately_equal(left, right, tolerance=1e-6) -> bool:
    return math.isclose(left, right, rel_tol=0.0, abs_tol=tolerance)


def compute_proximity_influence(distance, full_distance, release_distance) -> float:
    if distance <= full_distance:
        return 1.0
    if distance >= release_distance:
        return 0.0
    transition = (distance - full_distance) / (release_distance - full_distance)
    smooth = transition * transition * (3.0 - 2.0 * transition)
    return 1.0 - smooth


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jsonl", type=Path)
    parser.add_argument("--expect-fixture", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    raw = args.jsonl.read_text(encoding="utf-8")
    clean = validate_text(raw, args.expect_fixture)
    if args.self_test:
        run_self_test(raw)
    print(f"valid telemetry JSONL ({'clean' if clean else 'recoverable unclean'} session)")


def parse_recoverable(raw: str):
    lines = raw.splitlines()
    records = []
    trailing_partial = False
    for index, line in enumerate(lines):
        if not line:
            continue
        try:
            records.append(json.loads(line, parse_constant=reject_constant))
        except (json.JSONDecodeError, ValueError):
            if index == len(lines) - 1 and not raw.endswith(("\n", "\r")):
                trailing_partial = True
                break
            raise
    unterminated_final = bool(raw) and not raw.endswith(("\n", "\r"))
    return records, trailing_partial or unterminated_final


def build_profile_mapping(start):
    mapping = start.get("test_profile_enum_mapping")
    require(isinstance(mapping, list) and mapping, "missing test-profile mapping")

    profiles_by_id = {}
    profiles_by_name = {}
    profiles_by_role = {role: [] for role in PROFILE_ROLES}
    for entry in mapping:
        require(isinstance(entry, dict), "test-profile mapping entry is not an object")
        profile_id = entry.get("id")
        name = entry.get("name")
        role = entry.get("role")
        uses_custom_settings = entry.get("uses_custom_settings")
        horizontal_release_enabled = entry.get("horizontal_release_enabled")
        require(
            type(profile_id) is int and 0 <= profile_id <= 255,
            "test-profile mapping ID is not an unsigned byte",
        )
        require(isinstance(name, str) and name, "test-profile mapping name is empty")
        require(
            isinstance(role, str) and role in PROFILE_ROLES,
            "test-profile mapping role is unknown",
        )
        require(
            type(uses_custom_settings) is bool,
            "test-profile mapping Custom policy is not boolean",
        )
        require(
            type(horizontal_release_enabled) is bool,
            "test-profile mapping horizontal-release policy is not boolean",
        )
        require(profile_id not in profiles_by_id, "duplicate test-profile mapping ID")
        require(name not in profiles_by_name, "duplicate test-profile mapping name")
        profiles_by_id[profile_id] = entry
        profiles_by_name[name] = entry
        profiles_by_role[role].append(entry)

    for role in CONTROL_ROLE_BY_FIELD.values():
        require(
            len(profiles_by_role[role]) == 1,
            f"test-profile mapping requires exactly one {role}",
        )
        require(
            not profiles_by_role[role][0]["uses_custom_settings"],
            f"control role {role} cannot use caller Custom settings",
        )
    require(
        sum(entry["uses_custom_settings"] for entry in mapping) == 1,
        "test-profile mapping requires exactly one Custom-settings profile",
    )
    return profiles_by_id, profiles_by_name, profiles_by_role


def validate_profile_reference(record, profiles_by_id, context):
    require(isinstance(record, dict), f"{context} is not an object")
    profile_id = record.get("profile_id")
    profile_name = record.get("profile_name")
    require(type(profile_id) is int, f"{context} profile ID is not an integer")
    require(profile_id in profiles_by_id, f"{context} profile ID is unknown")
    require(
        profile_name == profiles_by_id[profile_id]["name"],
        f"{context} profile name/id mismatch",
    )
    return profiles_by_id[profile_id]


def validate_text(raw: str, expect_fixture: bool = False) -> bool:
    records, trailing_partial = parse_recoverable(raw)
    if not records:
        raise ValueError("missing session_start")

    start = records[0]
    require(isinstance(start, dict), "session_start is not an object")
    require(isinstance(records[-1], dict), "final record is not an object")
    end = records[-1] if records[-1].get("type") == "session_end" else None
    frames = records[1:-1] if end else records[1:]

    require(start.get("type") == "session_start", "first record is not session_start")
    require(start.get("schema_version") == SCHEMA_VERSION, "unexpected session schema")
    require(start.get("ring_slots") == 4096, "unexpected ring slot count")
    require(start.get("ring_usable_capacity") == 4095, "unexpected ring capacity")
    require(start.get("drop_policy") == "drop_new", "unexpected drop policy")
    require(start.get("tracking_space") == "OpenXR LOCAL", "unexpected tracking space")
    require(start.get("quaternion_order") == "x,y,z,w", "unexpected quaternion order")
    require(
        start.get("coordinate_handedness") == "OpenXR right-handed",
        "unexpected coordinate handedness",
    )
    # Shots-vs-reticle recorder declarations (additive): older schema-2
    # captures predate them, so presence is required only for the fixture
    # (which a current recorder wrote); any present declaration must be a
    # non-empty string.
    for key in SESSION_DECLARATIONS:
        if key in start:
            require(
                isinstance(start[key], str) and start[key],
                f"session declaration {key} is not a non-empty string",
            )
    if expect_fixture:
        for key in SESSION_DECLARATIONS:
            require(
                key in start,
                f"fixture session_start is missing declaration {key}",
            )
    profiles_by_id, _, profiles_by_role = build_profile_mapping(start)

    previous_advance = None
    # The raw stream is a mixed stream: complete frames plus the sparse event
    # channel (weapon_event / event_gap). Frames keep their full per-record
    # contract; the sparse records are validated by the event contract. Any
    # other record family inside the session range is still rejected.
    frame_rows = []
    event_rows = []
    gap_rows = []
    for record in frames:
        require(isinstance(record, dict), "record is not an object")
        record_type = record.get("type")
        if record_type == "weapon_event":
            event_rows.append(record)
            validate_weapon_event(record)
            continue
        if record_type == "event_gap":
            gap_rows.append(record)
            validate_event_gap(record)
            continue
        frame = record
        frame_rows.append(frame)
        require(frame.get("type") == "frame", "unexpected record inside session range")
        require(frame.get("schema_version") == SCHEMA_VERSION, "unexpected frame schema")
        require(
            type(frame.get("capture_begin_qpc")) is int
            and type(frame.get("capture_end_qpc")) is int,
            "capture QPC fields are not integers",
        )
        require(
            frame["capture_end_qpc"] >= frame["capture_begin_qpc"],
            "capture QPC interval is negative",
        )
        selected = validate_profile_reference(
            {
                "profile_id": frame.get("test_profile_id"),
                "profile_name": frame.get("test_profile_name"),
            },
            profiles_by_id,
            "selected",
        )
        require(
            type(frame.get("test_profile_custom")) is bool
            and frame["test_profile_custom"]
            == selected["uses_custom_settings"],
            "selected profile Custom policy mismatch",
        )
        for field, role in CONTROL_ROLE_BY_FIELD.items():
            control = frame.get(field)
            control_profile = validate_profile_reference(
                control, profiles_by_id, field
            )
            require(
                control_profile["id"] == profiles_by_role[role][0]["id"],
                f"{field} does not use the profile assigned to {role}",
            )
        settings = frame.get("effective_settings")
        require(isinstance(settings, dict), "effective settings are not an object")
        require(
            all(
                key in settings
                for key in (
                    "chest_height_m",
                    "chest_back_m",
                    "chest_side_m",
                    "two_hand_toggle",
                    "horizontal_release_enabled",
                    "horizontal_release_full_m",
                    "horizontal_release_release_m",
                )
            ),
            "effective settings are incomplete",
        )
        require(
            type(settings["horizontal_release_enabled"]) is bool,
            "horizontal release setting is not boolean",
        )
        for key in (
            "horizontal_release_full_m",
            "horizontal_release_release_m",
        ):
            require(
                is_finite_number(settings[key]),
                f"effective setting {key} is not a finite number",
            )
        require(
            settings["horizontal_release_enabled"]
            == selected["horizontal_release_enabled"],
            "selected profile horizontal-release policy mismatch",
        )
        if settings["horizontal_release_enabled"]:
            require(
                0.0 <= settings["horizontal_release_full_m"]
                < settings["horizontal_release_release_m"],
                "enabled horizontal release thresholds are invalid",
            )
        trace = frame.get("aim_trace")
        require(isinstance(trace, dict), "aim trace is not an object")
        require(
            all(
                key in trace
                for key in (
                    "path",
                    "w_natural_valid",
                    "w_natural",
                    "w_after_diagnostic_valid",
                    "w_after_diagnostic",
                    "w_effective_valid",
                    "w_effective",
                    "horizontal_release_attempted",
                    "horizontal_release_valid",
                    "rear_horizontal_reach_m",
                    "horizontal_release_influence",
                    "effective_diagnostic_override",
                    "override_applied",
                    "force_stock_eligible",
                    "c_valid",
                )
            ),
            "aim trace is missing Hybrid authority stages",
        )
        require(
            type(trace["path"]) is int and 0 <= trace["path"] <= 4,
            "aim trace solver path is unknown",
        )
        for key in (
            "w_natural_valid",
            "w_after_diagnostic_valid",
            "w_effective_valid",
            "horizontal_release_attempted",
            "horizontal_release_valid",
            "override_applied",
            "force_stock_eligible",
            "c_valid",
        ):
            require(type(trace[key]) is bool, f"aim trace {key} is not boolean")
        for key in (
            "w_natural",
            "w_after_diagnostic",
            "w_effective",
            "horizontal_release_influence",
        ):
            require(
                is_finite_number(trace[key]) and 0.0 <= trace[key] <= 1.0,
                f"aim trace {key} is not a finite unit influence",
            )
        require(
            is_finite_number(trace["rear_horizontal_reach_m"])
            and trace["rear_horizontal_reach_m"] >= 0.0,
            "rear horizontal reach is not a non-negative finite number",
        )
        diagnostic = trace["effective_diagnostic_override"]
        require(
            type(diagnostic) is int and diagnostic in (0, 1, 2),
            "effective diagnostic override is unknown",
        )
        settings_diagnostic = settings.get("hybrid_diagnostic_override")
        require(
            type(settings_diagnostic) is int and settings_diagnostic in (0, 1, 2),
            "effective settings diagnostic override is unknown",
        )
        for validity_key, value_key in (
            ("w_natural_valid", "w_natural"),
            ("w_after_diagnostic_valid", "w_after_diagnostic"),
            ("w_effective_valid", "w_effective"),
        ):
            require(
                trace[validity_key] or trace[value_key] == 0,
                f"invalid Hybrid authority stage {value_key} is nonzero",
            )
        if not settings["horizontal_release_enabled"]:
            require(
                not trace["horizontal_release_attempted"]
                and not trace["horizontal_release_valid"],
                "disabled horizontal release is not explicitly non-applicable",
            )
        # Shots-vs-reticle effective settings (additive): older captures lack
        # these keys, so each is checked only when present. The recorder
        # clamps aim_stabilization (0..0.95) and crosshair_distance_m (2..50)
        # exactly like production consumes them.
        for key in (
            "aim_stabilization",
            "crosshair_distance_m",
            "crosshair_size_deg",
        ):
            if key in settings:
                require(
                    is_finite_number(settings[key]),
                    f"effective setting {key} is not a finite number",
                )
        if "aim_stabilization" in settings:
            require(
                0.0 <= settings["aim_stabilization"] <= 0.95,
                "aim stabilization is outside the production clamp range",
            )
        if "crosshair_distance_m" in settings:
            require(
                2.0 <= settings["crosshair_distance_m"] <= 50.0,
                "crosshair distance is outside the production clamp range",
            )
        if "crosshair_size_deg" in settings:
            require(
                settings["crosshair_size_deg"] >= 0.0,
                "crosshair size is negative",
            )
        for key in ("crosshair", "kill_reticle"):
            if key in settings:
                require(
                    type(settings[key]) is bool,
                    f"effective setting {key} is not boolean",
                )
        # Weapon-gesture pad state (additive): checked only when present.
        pad = frame.get("pad")
        if isinstance(pad, dict):
            for key in (
                "weapon_buttons",
                "weapon_pulse_until_ms",
                "weapon_generation",
            ):
                if key in pad:
                    require(
                        type(pad[key]) is int and pad[key] >= 0,
                        f"pad field {key} is not a non-negative integer",
                    )
        require(
            not trace["horizontal_release_valid"]
            or trace["horizontal_release_attempted"],
            "horizontal release is valid without being attempted",
        )

        if trace.get("path") == 4:
            require(
                diagnostic == settings_diagnostic,
                "effective settings and Hybrid trace diagnostic overrides disagree",
            )
            require(
                trace["horizontal_release_attempted"]
                == settings["horizontal_release_enabled"],
                "Hybrid horizontal release attempt does not match effective settings",
            )
            require(
                trace["force_stock_eligible"] == trace["c_valid"],
                "Hybrid Force Stock eligibility disagrees with C validity",
            )
            require(
                trace["override_applied"]
                == (trace["force_stock_eligible"] and diagnostic != 0),
                "Hybrid diagnostic override application is inconsistent",
            )
            if trace["override_applied"]:
                forced_influence = 0.0 if diagnostic == 1 else 1.0
                require(
                    trace["w_after_diagnostic_valid"]
                    and approximately_equal(
                        trace["w_after_diagnostic"], forced_influence
                    ),
                    "Hybrid diagnostic override did not produce its exact authority",
                )
            else:
                require(
                    trace["w_after_diagnostic_valid"]
                    == trace["w_natural_valid"]
                    and approximately_equal(
                        trace["w_after_diagnostic"], trace["w_natural"]
                    ),
                    "Hybrid post-diagnostic authority does not preserve natural authority",
                )

            if settings["horizontal_release_enabled"]:
                expected_valid = (
                    trace["w_after_diagnostic_valid"]
                    and trace["horizontal_release_valid"]
                )
                expected_influence = (
                    trace["w_after_diagnostic"]
                    * trace["horizontal_release_influence"]
                    if trace["horizontal_release_valid"]
                    else 0.0
                )
            else:
                expected_valid = trace["w_after_diagnostic_valid"]
                expected_influence = trace["w_after_diagnostic"]
            require(
                trace["w_effective_valid"] == expected_valid
                and approximately_equal(trace["w_effective"], expected_influence),
                "Hybrid effective authority does not match its recorded stages",
            )
        else:
            require(
                not trace["horizontal_release_attempted"]
                and not trace["horizontal_release_valid"],
                "horizontal release ran outside the Hybrid solver path",
            )

        if not trace["horizontal_release_attempted"]:
            require(
                trace["rear_horizontal_reach_m"] == 0
                and trace["horizontal_release_influence"] == 0,
                "non-applicable horizontal release contains computed geometry",
            )
        elif not trace["horizontal_release_valid"]:
            require(
                trace["rear_horizontal_reach_m"] == 0
                and trace["horizontal_release_influence"] == 0,
                "invalid horizontal release retained geometry or authority",
            )
        else:
            expected_release_influence = compute_proximity_influence(
                trace["rear_horizontal_reach_m"],
                settings["horizontal_release_full_m"],
                settings["horizontal_release_release_m"],
            )
            require(
                approximately_equal(
                    trace["horizontal_release_influence"],
                    expected_release_influence,
                ),
                "horizontal release influence disagrees with reach and thresholds",
            )
        require(
            all(
                not isinstance(value, float) or math.isfinite(value)
                for value in walk_values(frame)
            ),
            "serialized frame contains a non-finite JSON number",
        )
        validate_transition_family(frame)
        validate_two_hand_lab_family(frame)
        validate_two_hand_input_smoothing_family(frame)
        validate_persistent_support_grip_family(frame)
        validate_presented_aim_family(frame)
        validate_reticle_presented_family(frame)
        validate_engine_aim_family(frame)
        validate_engine_camera_family(frame)
        frame_advance = frame.get("transition_advance_count")
        if type(frame_advance) is int and frame_advance >= 0:
            require(
                previous_advance is None or frame_advance >= previous_advance,
                "transition advance count is not monotonic",
            )
            previous_advance = frame_advance
        if expect_fixture:
            validate_fixture_frame(frame, selected)

    if end:
        require(isinstance(end, dict), "session_end is not an object")
        require(end.get("schema_version") == SCHEMA_VERSION, "unexpected end schema")
        require(end.get("clean_stop") is True, "session_end is not clean")
        require(
            all(
                type(end.get(key)) is int
                for key in (
                    "producer_calls",
                    "duplicate_serial_suppressed",
                    "enqueued",
                    "written",
                    "dropped_queue_full",
                )
            ),
            "session_end accounting fields are not integers",
        )
        require(
            end["producer_calls"]
            == end["duplicate_serial_suppressed"]
            + end["enqueued"]
            + end["dropped_queue_full"],
            "producer accounting identity failed",
        )
        require(
            end["written"] == end["enqueued"] == len(frame_rows),
            "clean session did not write every enqueued frame",
        )

    if expect_fixture:
        require(
            not trailing_partial and end is not None,
            "fixture session is not cleanly terminated",
        )
        require(len(frame_rows) == 2, "fixture frame count mismatch")
        require(
            [frame["prepared_serial"] for frame in frame_rows] == [0, 42],
            "fixture serial sequence mismatch",
        )
        require(end["producer_calls"] == 4, "fixture producer count mismatch")
        require(
            end["duplicate_serial_suppressed"] == 2,
            "fixture duplicate count mismatch",
        )
        require(end["enqueued"] == 2, "fixture enqueue count mismatch")
        require(end["written"] == 2, "fixture written count mismatch")
        require(end["dropped_queue_full"] == 0, "fixture drop count mismatch")
        # Sparse shot evidence: exactly three shot events in the fixture (an
        # H3-shaped one, a direction-absent ODST-shaped one and a Halo 2-shaped
        # one), their accounting exact, and each payload the contract this
        # validator enforces.
        require(len(event_rows) == 3 and not gap_rows,
            "fixture sparse event count mismatch")
        require(all(row.get("kind") == "shot" for row in event_rows),
            "fixture event is not a shot record")
        require(
            approximately_equal_sequence(
                event_rows[0].get("shot_origin"), [11.5, -0.25, 4.0]
            ),
            "fixture shot origin mismatch",
        )
        require(
            approximately_equal_sequence(
                event_rows[0].get("shot_direction"), [0.0, 0.0, -1.0]
            ),
            "fixture shot direction mismatch",
        )
        require(
            approximately_equal_sequence(
                event_rows[0].get("shot_engine_aim"), [0.5, 0.25, -0.75]
            ),
            "fixture shot engine aim mismatch",
        )
        require(
            approximately_equal_sequence(
                event_rows[0].get("shot_reticle_direction"),
                [0.125, 0.0, -0.9921568],
            ),
            "fixture shot reticle direction mismatch",
        )
        require(
            event_rows[0].get("shot_slot") == 0
            and event_rows[0].get("shot_barrel") == 1,
            "fixture shot slot/barrel mismatch",
        )
        require(event_rows[0].get("shot_flags") == 0x67,
            "fixture shot flags mismatch")
        require(event_rows[0].get("shot_engine_aim_source") == 1,
            "fixture shot engine aim source mismatch")
        # Direction-absent ODST-shaped event: zero direction, unknown
        # slot/barrel, no engine aim snapshot.
        require(
            approximately_equal_sequence(
                event_rows[1].get("shot_origin"), [3.25, 1.5, -2.0]
            ),
            "fixture ODST shot origin mismatch",
        )
        require(
            approximately_equal_sequence(
                event_rows[1].get("shot_direction"), [0.0, 0.0, 0.0]
            )
            and approximately_equal_sequence(
                event_rows[1].get("shot_engine_aim"), [0.0, 0.0, 0.0]
            ),
            "fixture ODST shot is not direction/aim absent",
        )
        require(
            event_rows[1].get("shot_slot") == -1
            and event_rows[1].get("shot_barrel") == -1,
            "fixture ODST shot slot/barrel is not the unknown sentinel",
        )
        require(
            event_rows[1].get("shot_flags") == 0x02
            and event_rows[1].get("shot_engine_aim_source") == 0,
            "fixture ODST shot flags/source mismatch",
        )
        # Halo 2-shaped event: the firing detour's own slot, no barrel index in
        # this firing context, the independent-path substitution flag, and the
        # observer engine aim snapshot (source 7).
        require(
            approximately_equal_sequence(
                event_rows[2].get("shot_origin"), [6.0, -1.5, 2.25]
            )
            and approximately_equal_sequence(
                event_rows[2].get("shot_direction"), [0.5, -0.5, 0.5]
            ),
            "fixture Halo 2 shot ray mismatch",
        )
        require(
            approximately_equal_sequence(
                event_rows[2].get("shot_engine_aim"), [0.25, 0.5, -0.75]
            )
            and approximately_equal_sequence(
                event_rows[2].get("shot_reticle_direction"), [0.125, 0.25, -0.5]
            ),
            "fixture Halo 2 shot aim snapshot mismatch",
        )
        require(
            event_rows[2].get("shot_slot") == 1
            and event_rows[2].get("shot_barrel") == -1
            and event_rows[2].get("shot_flags") == 0xA2
            and event_rows[2].get("shot_engine_aim_source") == 7,
            "fixture Halo 2 shot context/source mismatch",
        )
        require(
            end.get("weapon_events_enqueued") == 3
            and end.get("weapon_events_written") == 3
            and end.get("weapon_events_dropped_queue_full") == 0
            and end.get("weapon_events_session_skipped") == 0,
            "fixture shot accounting mismatch",
        )

    return end is not None and not trailing_partial


TRANSITION_STOCK_MODES = frozenset((0, 1))
TRANSITION_PHASES = frozenset((0, 1, 2))
TRANSITION_EDGES = frozenset((0, 1, 2))
TRANSITION_ANCHORS = frozenset((0, 1, 2, 3))
TRANSITION_BOOL_FIELDS = (
    "transition_active",
    "transition_stock_mode_valid",
    "transition_live_calibrated_forward_valid",
    "transition_presented_forward_valid",
    "transition_one_hand_anchor_valid",
)
TRANSITION_FLOAT_FIELDS = (
    "transition_initial_correction_deg",
    "transition_remaining_correction_deg",
    "transition_elapsed_ms",
)


def validate_transition_family(frame) -> None:
    """Grab/release aim-continuity family (optional, additive schema 2).

    Recordings made before this family existed must still validate, so absence
    is accepted; when present every field and cross-field invariant is checked.
    """
    if "transition_applied_serial" not in frame:
        return
    for key in (
        "transition_stock_mode",
        "transition_phase",
        "transition_edge_kind",
        "transition_anchor_source",
        "transition_advance_count",
        "transition_last_prepared_serial",
        "transition_applied_serial",
    ):
        require(type(frame.get(key)) is int and frame[key] >= 0,
                f"transition field {key} is not a non-negative integer")
    for key in TRANSITION_BOOL_FIELDS:
        require(type(frame.get(key)) is bool,
                f"transition field {key} is not boolean")
    for key in TRANSITION_FLOAT_FIELDS:
        require(is_finite_number(frame.get(key)),
                f"transition field {key} is not a finite number")
        require(frame[key] >= 0.0,
                f"transition field {key} is negative")
    require(frame["transition_stock_mode"] in TRANSITION_STOCK_MODES,
            "transition stock mode is unknown")
    require(frame["transition_phase"] in TRANSITION_PHASES,
            "transition phase is unknown")
    require(frame["transition_edge_kind"] in TRANSITION_EDGES,
            "transition edge kind is unknown")
    require(frame["transition_anchor_source"] in TRANSITION_ANCHORS,
            "transition anchor source is unknown")
    # The stock mode is only meaningful while Virtual Stock is enabled; an
    # invalid mode must read Standard (0), never a stale or guessed mode.
    if not frame["transition_stock_mode_valid"]:
        require(
            frame["transition_stock_mode"] == 0,
            "an invalid transition stock mode must read 0",
        )
    for key in (
        "transition_live_calibrated_forward",
        "transition_presented_forward",
        "transition_one_hand_anchor_forward",
    ):
        vector = frame.get(key)
        require(
            isinstance(vector, list) and len(vector) == 3
            and all(is_finite_number(value) for value in vector),
            f"transition vector {key} is not a finite three-element array",
        )
    require(
        frame["transition_initial_correction_deg"] <= 180.0
        and frame["transition_remaining_correction_deg"] <= 180.0,
        "transition correction exceeds a half turn",
    )
    require(
        frame["transition_remaining_correction_deg"]
        <= frame["transition_initial_correction_deg"] + 1e-3,
        "transition remaining correction exceeds its seed",
    )
    if frame["transition_active"]:
        require(
            frame["transition_phase"] in (1, 2),
            "an active transition must be acquiring or releasing",
        )
        require(
            frame["transition_edge_kind"] != 0,
            "an active transition must name its edge",
        )
        require(
            frame["transition_anchor_source"] != 0,
            "an active transition must name its anchor source",
        )
        require(
            frame["transition_presented_forward_valid"]
            or not frame["transition_live_calibrated_forward_valid"],
            "an active transition must present a valid orientation unless the "
            "live solve is invalid",
        )
    if frame["transition_applied_serial"]:
        # The process-lifetime advance count and the module's per-state serial
        # are independent: any invalidation resets the module's serial to zero
        # while the count persists, and a non-positive prepared display delta
        # writes the layer output without consuming a serial. Combinations such
        # as (applied_serial=N, advance_count=0, last=0) and
        # (applied_serial=N, advance_count>0, last=0) are therefore legitimate,
        # so only the ordering below is invariant.
        require(
            frame["transition_applied_serial"]
            >= frame["transition_last_prepared_serial"],
            "transition applied serial precedes the module's last serial",
        )
    require(
        frame["transition_live_calibrated_forward_valid"]
        == frame["transition_presented_forward_valid"],
        "presented forward without a valid live calibrated solve",
    )
    require(
        not frame["transition_one_hand_anchor_valid"]
        or frame["transition_live_calibrated_forward_valid"],
        "one-hand anchor without a valid live calibrated solve",
    )


TWO_HAND_LAB_ANCHORS = frozenset((0, 1, 2, 3, 4))
TWO_HAND_LAB_FALLBACKS = frozenset((0, 1, 2, 3))
TWO_HAND_LAB_AGREEMENT_MODES = frozenset((0, 1))
TWO_HAND_LAB_TEMPORAL_MODES = frozenset((0, 1, 2, 3))
TWO_HAND_LAB_BOOL_FIELDS = (
    "two_hand_lab_enabled",
    "two_hand_lab_primary_pivot_valid",
    "two_hand_lab_support_pivot_valid",
    "two_hand_lab_stateless_direction_valid",
    "two_hand_lab_presented_direction_valid",
    "two_hand_lab_temporal_active",
)
TWO_HAND_LAB_FLOAT_FIELDS = (
    "two_hand_lab_offhand_influence",
    "two_hand_lab_agreement",
    "two_hand_lab_agreement_confidence",
    "two_hand_lab_effective_influence",
    "two_hand_lab_temporal_error_deg",
)
TWO_HAND_LAB_VECTORS = (
    "two_hand_lab_primary_pivot",
    "two_hand_lab_support_pivot",
    "two_hand_lab_stateless_direction",
    "two_hand_lab_presented_direction",
)
TWO_HAND_LAB_PAYLOAD_VALIDITIES = (
    "two_hand_lab_primary_pivot_valid",
    "two_hand_lab_support_pivot_valid",
    "two_hand_lab_stateless_direction_valid",
)


def validate_two_hand_lab_family(frame) -> None:
    """Two-Hand Lab family (optional, additive schema 2).

    Recordings made before this family existed must still validate, so
    absence is accepted; when present every field and cross-field invariant
    is checked. ``two_hand_lab_enabled`` means the Lab steered that frame's
    canonical solve; while false the family reads its inactive defaults,
    never a stale observation.
    """
    if "two_hand_lab_enabled" not in frame:
        return
    for key in (
        "two_hand_lab_anchor_requested",
        "two_hand_lab_anchor_resolved",
        "two_hand_lab_anchor_fallback",
        "two_hand_lab_agreement_mode",
        "two_hand_lab_temporal_mode",
    ):
        require(type(frame.get(key)) is int and frame[key] >= 0,
                f"two-hand-lab field {key} is not a non-negative integer")
    for key in TWO_HAND_LAB_BOOL_FIELDS:
        require(type(frame.get(key)) is bool,
                f"two-hand-lab field {key} is not boolean")
    for key in TWO_HAND_LAB_FLOAT_FIELDS:
        require(is_finite_number(frame.get(key)),
                f"two-hand-lab field {key} is not a finite number")
    require(frame["two_hand_lab_anchor_requested"] in TWO_HAND_LAB_ANCHORS,
            "two-hand-lab requested anchor is unknown")
    require(frame["two_hand_lab_anchor_resolved"] in TWO_HAND_LAB_ANCHORS,
            "two-hand-lab resolved anchor is unknown")
    require(frame["two_hand_lab_anchor_fallback"] in TWO_HAND_LAB_FALLBACKS,
            "two-hand-lab anchor fallback is unknown")
    require(frame["two_hand_lab_agreement_mode"]
            in TWO_HAND_LAB_AGREEMENT_MODES,
            "two-hand-lab agreement mode is unknown")
    require(frame["two_hand_lab_temporal_mode"]
            in TWO_HAND_LAB_TEMPORAL_MODES,
            "two-hand-lab temporal mode is unknown")
    require(
        -1.0 - 1e-6 <= frame["two_hand_lab_agreement"] <= 1.0 + 1e-6,
        "two-hand-lab agreement is outside the dot-product range",
    )
    require(
        0.0 <= frame["two_hand_lab_agreement_confidence"] <= 1.0,
        "two-hand-lab agreement confidence is outside [0,1]",
    )
    require(
        -1e-6 <= frame["two_hand_lab_offhand_influence"] <= 1.0 + 1e-6,
        "two-hand-lab offhand influence is outside [0,1]",
    )
    require(
        -1e-6 <= frame["two_hand_lab_effective_influence"]
        <= frame["two_hand_lab_offhand_influence"] + 1e-6,
        "two-hand-lab effective influence exceeds the requested influence",
    )
    require(
        0.0 <= frame["two_hand_lab_temporal_error_deg"] <= 180.0,
        "two-hand-lab temporal error is outside the half-turn range",
    )
    for key in TWO_HAND_LAB_VECTORS:
        vector = frame.get(key)
        require(
            isinstance(vector, list) and len(vector) == 3
            and all(is_finite_number(value) for value in vector),
            f"two-hand-lab vector {key} is not a finite three-element array",
        )
    # A presented direction is only meaningful while Lab temporal presentation
    # is active; every other payload needs an enabled Lab.
    if frame["two_hand_lab_presented_direction_valid"]:
        require(
            frame["two_hand_lab_enabled"]
            and frame["two_hand_lab_temporal_active"],
            "presented direction without an active Lab temporal mode",
        )
    for key in TWO_HAND_LAB_PAYLOAD_VALIDITIES:
        if frame[key]:
            require(
                frame["two_hand_lab_enabled"],
                f"two-hand-lab payload {key} without an enabled Lab",
            )
    if not frame["two_hand_lab_enabled"]:
        require(
            frame["two_hand_lab_anchor_requested"] == 0
            and frame["two_hand_lab_anchor_resolved"] == 0
            and frame["two_hand_lab_anchor_fallback"] == 0
            and frame["two_hand_lab_agreement_mode"] == 0
            and frame["two_hand_lab_temporal_mode"] == 0,
            "a disabled Lab must read the inactive enum defaults",
        )
        require(
            frame["two_hand_lab_offhand_influence"] == 0.0
            and frame["two_hand_lab_agreement"] == 0.0
            and frame["two_hand_lab_agreement_confidence"] == 0.0
            and frame["two_hand_lab_effective_influence"] == 0.0
            and frame["two_hand_lab_temporal_error_deg"] == 0.0,
            "a disabled Lab must read zero scalars",
        )
        require(
            not frame["two_hand_lab_temporal_active"],
            "a disabled Lab must not report temporal active",
        )


TWO_HAND_INPUT_SMOOTHING_BOOL_FIELDS = (
    "two_hand_transition_smoothing_configured",
    "two_hand_transition_active",
    "two_hand_smoothing_configured",
    "two_hand_smoothing_applied",
)
TWO_HAND_INPUT_SMOOTHING_FLOAT_FIELDS = (
    "two_hand_smoothing_alpha",
    "two_hand_smoothing_primary_orientation_error_deg",
    "two_hand_smoothing_primary_position_error_m",
    "two_hand_smoothing_support_position_error_m",
)
# Strength-slider addition: the user-facing 0..25 amount and its normalized
# wet/dry mix, emitted from the same frozen value. Recordings written by the
# earlier boolean-era candidate carry this family without these keys, so the
# block is checked only when two_hand_smoothing_strength is present; the
# current recorder always writes it, so the fixture exercises every check.
TWO_HAND_INPUT_SMOOTHING_STRENGTH_FLOAT_FIELDS = (
    "two_hand_smoothing_strength",
    "two_hand_smoothing_mix",
)


def validate_two_hand_input_smoothing_family(frame) -> None:
    """Two-Hand transition/input smoothing diagnostics (optional schema 2).

    Legacy recordings may omit the additive family. Present alpha is the
    filter's INTERNAL clamp(25*dt, 0, 1) temporal coefficient, never the
    user-facing strength amount; orientation errors are degrees in [0, 180] and
    position errors are non-negative OpenXR LOCAL metres. The `*_error_*`
    values are raw-to-full-filtered input differences, never solver or
    presented-aim errors.

    two_hand_smoothing_strength is the user amount in 0..25 (0 = raw/off,
    25 = the full fixed speed-25 filter) and two_hand_smoothing_mix is
    strength/25 in [0, 1]. Both come from the strength frozen for this prepared
    serial, never a live re-read of the config slider; mix is the applied
    wet/dry amount and is never a filter speed.

    two_hand_offhand_influence (the frozen free two-hand offhand directional
    authority in 0..1) is checked when present: it is a separate product
    setting carried beside this family, so a recording written before it
    existed omits the key and absence is never read as a zero authority.
    """
    marker = "two_hand_transition_smoothing_configured"
    if marker not in frame:
        return
    for key in TWO_HAND_INPUT_SMOOTHING_BOOL_FIELDS:
        require(type(frame.get(key)) is bool,
                f"two-hand-smoothing field {key} is not boolean")
    for key in TWO_HAND_INPUT_SMOOTHING_FLOAT_FIELDS:
        require(is_finite_number(frame.get(key)),
                f"two-hand-smoothing field {key} is not a finite number")
    require(
        0.0 <= frame["two_hand_smoothing_alpha"] <= 1.0,
        "two-hand-smoothing alpha is outside [0,1]",
    )
    require(
        0.0 <= frame["two_hand_smoothing_primary_orientation_error_deg"]
        <= 180.0,
        "two-hand-smoothing orientation error is outside [0,180] degrees",
    )
    for key in (
        "two_hand_smoothing_primary_position_error_m",
        "two_hand_smoothing_support_position_error_m",
    ):
        require(frame[key] >= 0.0,
                f"two-hand-smoothing position error {key} is negative")
    require(
        not frame["two_hand_smoothing_applied"]
        or frame["two_hand_smoothing_configured"],
        "two-hand smoothing applied while not configured",
    )
    require(
        not frame["two_hand_transition_active"]
        or frame["two_hand_transition_smoothing_configured"],
        "two-hand transition active while not configured",
    )
    # Free two-hand (VS-OFF) product offhand directional authority, frozen per
    # frame from the assembly the solve consumed. It is a separate product
    # setting carried beside this family, not a member of it: a recording
    # written before the field existed simply omits the key, which is not a
    # zero authority, so the check is presence-tolerant. range 0..1 and finite
    # where present.
    if "two_hand_offhand_influence" in frame:
        require(
            is_finite_number(frame["two_hand_offhand_influence"]),
            "two-hand offhand influence is not a finite number",
        )
        require(
            0.0 <= frame["two_hand_offhand_influence"] <= 1.0,
            "two-hand offhand influence is outside [0,1]",
        )
    if "two_hand_smoothing_strength" not in frame:
        # Boolean-era recording: absence is unambiguous and the strength-only
        # invariants have nothing to check.
        return
    for key in TWO_HAND_INPUT_SMOOTHING_STRENGTH_FLOAT_FIELDS:
        require(is_finite_number(frame.get(key)),
                f"two-hand-smoothing field {key} is not a finite number")
    require(
        0.0 <= frame["two_hand_smoothing_strength"] <= 25.0,
        "two-hand-smoothing strength is outside [0,25]",
    )
    require(
        0.0 <= frame["two_hand_smoothing_mix"] <= 1.0,
        "two-hand-smoothing mix is outside [0,1]",
    )
    require(
        abs(
            frame["two_hand_smoothing_mix"]
            - frame["two_hand_smoothing_strength"] / 25.0
        )
        <= 1.0e-4,
        "two-hand-smoothing mix does not match strength/25",
    )
    require(
        frame["two_hand_smoothing_configured"]
        == (frame["two_hand_smoothing_strength"] > 0.0),
        "two-hand-smoothing configured disagrees with a non-zero strength",
    )
    require(
        not frame["two_hand_transition_active"]
        or frame["two_hand_transition_smoothing_configured"],
        "two-hand transition active while not configured",
    )


# Persistent support grip (PG) frozen solve-time provenance family (additive
# schema 2). The epoch sentinel is `support_grip::kUnknownRelationshipEpoch`:
# the frozen relationship read failed, which is deliberately NOT the same as a
# coherently disengaged relationship (epoch 0).
PERSISTENT_SUPPORT_GRIP_BOOL_FIELDS = (
    "persistent_support_grip_configured",
    "persistent_support_grip_applicable",
    "support_relationship_readable",
    "support_relationship_engaged",
    "support_solve_trusted",
    "support_force_one_hand",
)
PERSISTENT_SUPPORT_GRIP_KEYS = PERSISTENT_SUPPORT_GRIP_BOOL_FIELDS + (
    "support_epoch",
    "support_solve_serial",
)
SUPPORT_EPOCH_UNKNOWN = 2**64 - 1


def validate_persistent_support_grip_family(frame) -> None:
    """Persistent support grip frozen solve-time provenance (additive schema 2).

    Recordings made before this family existed must still validate, so absence
    is accepted; when present every field, domain and qualification invariant
    is checked. The values are the frame-local assembly's frozen receipt plus
    this frame's config knob and pure title gate - never a live re-read of
    persistent-grip state. Deliberately not enforced here: that the config knob
    or title gate agree with the freeze (the knob is read once per frame, so a
    toggle between solve and capture is a legitimate, unprovable race) or that
    the active title's applicability is true (the wired-title list is
    implementation detail, not part of the wire contract).
    """
    if "persistent_support_grip_configured" not in frame:
        return
    for key in PERSISTENT_SUPPORT_GRIP_BOOL_FIELDS:
        require(
            type(frame.get(key)) is bool,
            f"persistent-grip field {key} is not boolean",
        )
    for key in ("support_epoch", "support_solve_serial"):
        value = frame.get(key)
        require(
            type(value) is int and value >= 0,
            f"persistent-grip field {key} is not a non-negative integer",
        )
    # The stamped solve serial is the last published prepared serial at
    # capture time: the assembly freezes it before this frame's publish store,
    # so it lags this frame's serial by one - the very first prepared frame
    # (serial 1, frozen value 0) already shows that lag, and equality is not
    # expected in practice. Equality is tolerated here only as deliberate
    # conservative slack in the <= bound below; it is never an equality claim
    # against prepared_serial.
    require(
        frame["support_solve_serial"] <= frame["prepared_serial"],
        "persistent-grip solve serial runs ahead of the frame serial",
    )
    # Frozen-qualification invariants (support_grip::QualifySolveSupport):
    # an unreadable relationship is never reported as engaged; the permission
    # bit implies a readable, engaged relationship carrying a live epoch and
    # excludes explicit forcing; the all-ones sentinel is only ever produced
    # together with the forcing flag.
    if not frame["support_relationship_readable"]:
        require(
            not frame["support_relationship_engaged"],
            "an unreadable relationship was reported as engaged",
        )
    if frame["support_solve_trusted"]:
        require(
            frame["support_relationship_readable"]
            and frame["support_relationship_engaged"],
            "a trusted solve without a readable engaged relationship",
        )
        require(
            frame["support_epoch"] not in (0, SUPPORT_EPOCH_UNKNOWN),
            "a trusted solve without a live relationship epoch",
        )
        require(
            not frame["support_force_one_hand"],
            "a solve cannot be trusted and forced to one hand",
        )
    if frame["support_epoch"] == SUPPORT_EPOCH_UNKNOWN:
        require(
            frame["support_force_one_hand"]
            and not frame["support_relationship_engaged"],
            "the unknown-relationship epoch without its forcing receipt",
        )


SESSION_DECLARATIONS = (
    "frame_field_domains",
    "engine_world_axes",
    "engine_world_scale_authority",
    "engine_aim_sources",
    "engine_camera_sources",
    "reticle_presented_lag",
    "dual_slot_semantics",
    "shot_event_semantics",
    "persistent_support_grip_provenance",
    "two_hand_smoothing_provenance",
    "two_hand_offhand_influence_provenance",
)


def require_finite_vec3(frame, key) -> None:
    vector = frame.get(key)
    require(
        isinstance(vector, list)
        and len(vector) == 3
        and all(is_finite_number(value) for value in vector),
        f"vector {key} is not a finite three-element array",
    )


def validate_presented_aim_family(frame) -> None:
    """Composed presented/steering aim (optional, additive schema 2).

    Recordings made before this family existed must still validate, so
    absence is accepted; when present every field and cross-field invariant
    is checked. Validity is authoritative: a valid forward carries a real
    direction, an invalid one reads exactly zero (never stale).
    """
    if "presented_aim_valid" not in frame:
        return
    require(
        type(frame.get("presented_aim_valid")) is bool,
        "presented_aim_valid is not boolean",
    )
    require_finite_vec3(frame, "presented_aim_forward")
    if frame["presented_aim_valid"]:
        require(
            any(value != 0 for value in frame["presented_aim_forward"]),
            "a valid presented aim forward is the zero vector",
        )
    else:
        require(
            frame["presented_aim_forward"] == [0, 0, 0],
            "an invalid presented aim forward is not zero",
        )


def validate_reticle_presented_family(frame) -> None:
    """Consumer-visible reticle pose (optional, additive schema 2).

    The pose carries its own serial because capture runs before the reticle
    block publishes in the same frame: the serial normally lags one prepared
    frame behind, so equality with prepared_serial must never be asserted
    (and is not, anywhere below). Validity is authoritative with zero
    payloads when invalid.
    """
    if "reticle_presented_valid" not in frame:
        return
    require(
        type(frame.get("reticle_presented_valid")) is bool,
        "reticle_presented_valid is not boolean",
    )
    for key in (
        "reticle_presented_serial",
        "reticle_presented_sample_ms",
        "reticle_presented_support_epoch",
    ):
        require(
            type(frame.get(key)) is int and frame[key] >= 0,
            f"reticle field {key} is not a non-negative integer",
        )
    require(
        type(frame.get("reticle_presented_support_trusted")) is bool,
        "reticle_presented_support_trusted is not boolean",
    )
    orientation = frame.get("reticle_presented_orientation")
    require(
        isinstance(orientation, list)
        and len(orientation) == 4
        and all(is_finite_number(value) for value in orientation),
        "reticle_presented_orientation is not a finite four-element array",
    )
    require_finite_vec3(frame, "reticle_presented_position")
    if not frame["reticle_presented_valid"]:
        require(
            frame["reticle_presented_serial"] == 0
            and frame["reticle_presented_sample_ms"] == 0
            and frame["reticle_presented_support_epoch"] == 0,
            "an invalid reticle pose retains serial/sample provenance",
        )
        require(
            orientation == [0, 0, 0, 0]
            and frame["reticle_presented_position"] == [0, 0, 0],
            "an invalid reticle pose is not zero",
        )
    else:
        require(
            any(value != 0 for value in orientation),
            "a valid reticle orientation is the zero quaternion",
        )


ENGINE_AIM_SOURCES = frozenset((0, 1, 2, 3, 4, 5, 6, 7, 8))
# Latest-only shared publications: no serial and no sample clock.
ENGINE_AIM_LATEST_ONLY_SOURCES = frozenset((1, 2, 5))
ENGINE_CAMERA_SOURCES = frozenset((0, 1, 2, 3, 4, 5, 6))


def validate_engine_aim_family(frame) -> None:
    """Per-title engine aim feedback (optional, additive schema 2).

    Vectors are ENGINE-WORLD (Blam Z-up, world units), never OpenXR LOCAL.
    Validity is authoritative with zero payloads when invalid; the source
    still names the attributed publication (Halo 2 absent and the Reach
    fallback label the attempt even when no sample exists).
    """
    if "engine_aim_valid" not in frame:
        return
    require(
        type(frame.get("engine_aim_valid")) is bool,
        "engine_aim_valid is not boolean",
    )
    require(
        type(frame.get("engine_aim_source")) is int
        and frame["engine_aim_source"] in ENGINE_AIM_SOURCES,
        "engine_aim_source is unknown",
    )
    require_finite_vec3(frame, "engine_aim_forward")
    for key in ("engine_aim_serial", "engine_aim_sample_ms"):
        require(
            type(frame.get(key)) is int and frame[key] >= 0,
            f"engine aim field {key} is not a non-negative integer",
        )
    require(
        type(frame.get("engine_aim_pitch_valid")) is bool,
        "engine_aim_pitch_valid is not boolean",
    )
    require(
        is_finite_number(frame.get("engine_aim_pitch_deg")),
        "engine_aim_pitch_deg is not a finite number",
    )
    source = frame["engine_aim_source"]
    if frame["engine_aim_valid"]:
        # None and Halo 2-absent are never valid observations.
        require(source != 0 and source != 8,
                "a valid engine aim carries no publication")
        require(
            any(value != 0 for value in frame["engine_aim_forward"]),
            "a valid engine aim forward is the zero vector",
        )
        if source in ENGINE_AIM_LATEST_ONLY_SOURCES:
            require(
                frame["engine_aim_serial"] == 0
                and frame["engine_aim_sample_ms"] == 0,
                "a latest-only engine aim carries a serial or sample time",
            )
        if source in (3, 4):
            require(
                frame["engine_aim_serial"] == 0
                and frame["engine_aim_sample_ms"] != 0,
                "a Reach seated engine aim lacks its sample time",
            )
        if source in (6, 7):
            require(
                frame["engine_aim_serial"] != 0
                and frame["engine_aim_sample_ms"] == 0,
                "an observer engine aim lacks its publication serial",
            )
        if source == 6:
            require(
                frame["engine_aim_pitch_valid"],
                "a valid Halo 4 engine aim lacks its pitch publication",
            )
        if frame["engine_aim_pitch_valid"]:
            require(
                source == 6,
                "engine aim pitch is valid outside Halo 4",
            )
    else:
        require(
            frame["engine_aim_forward"] == [0, 0, 0]
            and frame["engine_aim_serial"] == 0
            and frame["engine_aim_sample_ms"] == 0
            and frame["engine_aim_pitch_deg"] == 0
            and not frame["engine_aim_pitch_valid"],
            "an invalid engine aim retains payload",
        )


def validate_engine_camera_family(frame) -> None:
    """Engine camera truth and scale (optional, additive schema 2).

    Positions are ENGINE-WORLD units. Base is the pre-lean origin (Halo 3 /
    ODST only); eye is the rendered camera. world_scale is engine world units
    per metre under the active title's authority. dual_active is the shared
    dual-presentation predicate, not per-shot slot identity.
    """
    if "engine_camera_base_valid" not in frame:
        return
    for key in ("engine_camera_base_valid", "engine_camera_eye_valid"):
        require(type(frame.get(key)) is bool,
                f"engine camera field {key} is not boolean")
    require_finite_vec3(frame, "engine_camera_base_position")
    require_finite_vec3(frame, "engine_camera_eye_position")
    require(
        type(frame.get("engine_camera_source")) is int
        and frame["engine_camera_source"] in ENGINE_CAMERA_SOURCES,
        "engine_camera_source is unknown",
    )
    require(
        type(frame.get("engine_camera_serial")) is int
        and frame["engine_camera_serial"] >= 0,
        "engine_camera_serial is not a non-negative integer",
    )
    require(
        type(frame.get("world_scale_valid")) is bool,
        "world_scale_valid is not boolean",
    )
    require(
        is_finite_number(frame.get("world_scale")),
        "world_scale is not a finite number",
    )
    require(
        type(frame.get("dual_active")) is bool,
        "dual_active is not boolean",
    )
    source = frame["engine_camera_source"]
    if not frame["engine_camera_base_valid"]:
        require(
            frame["engine_camera_base_position"] == [0, 0, 0],
            "an invalid engine camera base is not zero",
        )
    if not frame["engine_camera_eye_valid"]:
        require(
            frame["engine_camera_eye_position"] == [0, 0, 0],
            "an invalid engine camera eye is not zero",
        )
    if source == 0 or source == 5:
        # No publication (Halo CE / no title) or explicitly absent (Halo 4).
        require(
            not frame["engine_camera_base_valid"]
            and not frame["engine_camera_eye_valid"],
            "an absent engine camera carries a camera position",
        )
    if source in (1, 2):
        require(
            frame["engine_camera_serial"] == 0,
            "a shared engine camera carries a serial",
        )
    if source in (3, 4, 6) and frame["engine_camera_eye_valid"]:
        require(
            frame["engine_camera_serial"] != 0,
            "a valid completed/observer engine eye lacks its serial",
        )
    if source == 6:
        # Halo 4's stereo transaction publishes the observer eye alone; there
        # is no pre-lean base for it, and a published source always implies a
        # usable eye with its serial.
        require(
            frame["engine_camera_eye_valid"]
            and frame["engine_camera_serial"] != 0
            and not frame["engine_camera_base_valid"],
            "a Halo 4 observer camera is missing its eye/serial or carries a base",
        )
    if frame["world_scale_valid"]:
        require(
            frame["world_scale"] > 0.0,
            "a valid world scale is not positive",
        )
    else:
        require(
            frame["world_scale"] == 0,
            "an invalid world scale is not zero",
        )


# ---- Sparse event channel (weapon_event / event_gap) ----
# The kinds the recorder currently emits. Unknown kinds stay forward
# compatible: the envelope is still checked and the analyser inventories them.
WEAPON_EVENT_KINDS = frozenset(
    (
        "present_begin",
        "after_present_before_prepare",
        "capture_pre_latch",
        "capture_probe_begin",
        "capture_probe_result",
        "fp_entry",
        "fp_weapon_commit",
        "shot",
    )
)
# Fixed T-2 shot payload. Emitted ONLY for kind shot.
SHOT_PAYLOAD_KEYS = (
    "shot_origin",
    "shot_direction",
    "shot_engine_aim",
    "shot_reticle_direction",
    "shot_slot",
    "shot_barrel",
    "shot_flags",
    "shot_engine_aim_source",
)
# Shot engine-aim provenance uses the frame's engine_aim_source ordinals.
SHOT_ENGINE_AIM_SOURCES = frozenset((0, 1, 2, 3, 4, 5, 6, 7, 8))


def validate_event_gap(record) -> None:
    """Explicit gap marker for a claimed sequence that can no longer arrive.

    The worker currently emits one marker per provably lost sequence, but the
    fields can express a range, so only the ordering contract is asserted.
    """
    for key in ("first_missing_seq", "last_missing_seq"):
        require(
            type(record.get(key)) is int and record[key] >= 1,
            f"event gap field {key} is not a positive integer",
        )
    require(
        record["first_missing_seq"] <= record["last_missing_seq"],
        "event gap range is inverted",
    )


def validate_weapon_event(record) -> None:
    """Sparse event record.

    The Shot payload is validated in full (presence, type, finiteness, flags
    bit bound, slot/barrel bounds). Every other kind keeps the shared envelope
    contract only, so a future kind remains forward compatible while a shot
    record can never carry a malformed or fabricated ray. There is
    deliberately no requirement that a shot's prepared_serial equals any frame
    serial: the firing path consumes no serial inside the weapon transaction,
    so the recorded value is a bounded snapshot, not an equality claim.
    """
    kind = record.get("kind")
    require(isinstance(kind, str) and kind, "weapon_event kind is not a string")
    require(
        isinstance(record.get("status"), str) and record["status"],
        "weapon_event status is not a string",
    )
    for key in (
        "session",
        "thread_id",
        "title",
        "title_generation",
        "controlled_unit",
        "primary_weapon",
        "aux0",
        "aux1",
    ):
        require(
            type(record.get(key)) is int and record[key] >= 0,
            f"weapon_event field {key} is not a non-negative integer",
        )
    require(
        type(record.get("seq")) is int and record["seq"] >= 1,
        "weapon_event sequence is not a positive integer",
    )
    require(type(record.get("qpc")) is int, "weapon_event qpc is not an integer")
    require(
        type(record.get("prepared_serial")) is int
        and record["prepared_serial"] >= 0,
        "weapon_event prepared_serial is not a non-negative integer",
    )
    if kind != "shot":
        # The payload keys are only meaningful for kind shot; another kind
        # carrying them would be an uninterpretable mixture.
        require(
            not any(key in record for key in SHOT_PAYLOAD_KEYS),
            "a non-shot weapon_event carries shot payload keys",
        )
        return
    require(
        all(key in record for key in SHOT_PAYLOAD_KEYS),
        "a shot weapon_event is missing its fixed payload",
    )
    for key in (
        "shot_origin",
        "shot_direction",
        "shot_engine_aim",
        "shot_reticle_direction",
    ):
        value = record[key]
        require(
            isinstance(value, list)
            and len(value) == 3
            and all(is_finite_number(component) for component in value),
            f"shot vector {key} is not a finite three-element array",
        )
    # -1 is the documented "the firing context did not carry it" wire value;
    # 0/1 are the only real weapon slots.
    require(
        type(record["shot_slot"]) is int and -1 <= record["shot_slot"] <= 1,
        "shot slot is outside the -1 (unknown) / 0 / 1 contract",
    )
    require(
        type(record["shot_barrel"]) is int and record["shot_barrel"] >= -1,
        "shot barrel is below the -1 (unknown) contract",
    )
    require(
        type(record["shot_flags"]) is int
        and 0 <= record["shot_flags"] <= 0xFF,
        "shot flags set a bit outside the documented eight",
    )
    require(
        type(record["shot_engine_aim_source"]) is int
        and record["shot_engine_aim_source"] in SHOT_ENGINE_AIM_SOURCES,
        "shot engine aim source is unknown",
    )
    # Sources 0 (no snapshot) and 8 (Halo 2 active without a safe observer
    # publication, the frame vocabulary's H2Absent) are the producers' "no
    # snapshot" labels, and a producer only sets them together with an exact
    # zero vector. The converse is not asserted because a real published
    # forward is never guaranteed non-zero.
    if record["shot_engine_aim_source"] in (0, 8):
        require(
            record["shot_engine_aim"] == [0, 0, 0],
            "a shot without an engine aim snapshot carries an aim vector",
        )


def validate_fixture_frame(frame, selected_profile) -> None:
    require(
        frame["semantic_primary_aim"]["position"] == [1, 2, 3],
        "fixture primary position mismatch",
    )
    require(
        approximately_equal_sequence(
            frame["semantic_primary_aim"]["orientation"],
            [0.1, 0.2, 0.3, 0.9],
        ),
        "fixture primary orientation mismatch",
    )
    require(
        frame["semantic_primary_linear_velocity"] == [None, 2, 3],
        "fixture velocity null mismatch",
    )
    require(math.isclose(frame["pad"]["moveX"], 0.5, abs_tol=1e-6), "fixture pad mismatch")
    require(
        selected_profile["role"] == "tuning" and not frame["test_profile_custom"],
        "fixture profile is not a non-Custom tuning profile",
    )
    require(
        frame["effective_settings"]["two_hand_toggle"] is False,
        "fixture acquisition mode mismatch",
    )
    require(
        math.isclose(
            frame["effective_settings"]["hybrid_seat_full_m"],
            0.6,
            abs_tol=1e-6,
        ),
        "fixture full-seat threshold mismatch",
    )
    require(
        math.isclose(
            frame["effective_settings"]["hybrid_seat_release_m"],
            0.8,
            abs_tol=1e-6,
        ),
        "fixture release-seat threshold mismatch",
    )
    require(frame["aim_trace"]["w_natural_valid"], "fixture natural W is invalid")
    require(
        frame["aim_trace"]["w_after_diagnostic_valid"],
        "fixture post-diagnostic W is invalid",
    )
    require(frame["aim_trace"]["w_effective_valid"], "fixture effective W is invalid")
    require(
        math.isclose(frame["aim_trace"]["w_natural"], 0.375, abs_tol=1e-6),
        "fixture natural W mismatch",
    )
    require(
        math.isclose(
            frame["aim_trace"]["w_after_diagnostic"], 0.375, abs_tol=1e-6
        ),
        "fixture post-diagnostic W mismatch",
    )
    require(
        math.isclose(frame["aim_trace"]["w_effective"], 0.375, abs_tol=1e-6),
        "fixture effective W mismatch",
    )
    require(
        math.isclose(
            frame["aim_trace"]["rear_to_stock_target_distance_m"],
            0.25,
            abs_tol=1e-6,
        ),
        "fixture release distance mismatch",
    )
    require(
        frame["effective_settings"]["horizontal_release_enabled"] is False
        and frame["aim_trace"]["horizontal_release_attempted"] is False
        and frame["aim_trace"]["horizontal_release_valid"] is False,
        "fixture disabled horizontal release is not explicitly non-applicable",
    )
    require(
        frame["cf_fixed_head"]["path"] == 3
        and frame["cf_fixed_head"]["fixed_direction_valid"]
        and frame["cf_fixed_head"]["orientation_rebuild_succeeded"],
        "fixture fixed-control cause fields mismatch",
    )
    require(
        frame["transition_stock_mode"] == 1
        and frame["transition_stock_mode_valid"]
        and frame["transition_active"]
        and frame["transition_phase"] == 1
        and frame["transition_edge_kind"] == 1
        and frame["transition_anchor_source"] == 1,
        "fixture transition-family identity fields mismatch",
    )
    require(
        approximately_equal(frame["transition_initial_correction_deg"], 12.5)
        and approximately_equal(frame["transition_remaining_correction_deg"], 4.25)
        and approximately_equal(frame["transition_elapsed_ms"], 33.3),
        "fixture transition-family correction values mismatch",
    )
    require(
        frame["transition_live_calibrated_forward_valid"]
        and frame["transition_presented_forward_valid"]
        and frame["transition_one_hand_anchor_valid"],
        "fixture transition-family forward validity mismatch",
    )
    require(
        approximately_equal_sequence(
            frame["transition_live_calibrated_forward"], [0.0, 0.0, -1.0]
        ),
        "fixture transition-family live calibrated forward mismatch",
    )
    require(
        approximately_equal_sequence(
            frame["transition_presented_forward"],
            frame["transition_one_hand_anchor_forward"],
        ),
        "fixture grab frame does not present the same-frame one-hand aim",
    )
    require(
        frame["transition_advance_count"] == 7,
        "fixture transition-family advance count mismatch",
    )
    if frame["prepared_serial"] == 42:
        require(
            frame["transition_last_prepared_serial"] == 42
            and frame["transition_applied_serial"] == 42,
            "fixture transition-family serial evidence mismatch",
        )
    require(
        frame["two_hand_lab_enabled"]
        and frame["two_hand_lab_anchor_requested"] == 4
        and frame["two_hand_lab_anchor_resolved"] == 4
        and frame["two_hand_lab_anchor_fallback"] == 0
        and frame["two_hand_lab_agreement_mode"] == 1
        and frame["two_hand_lab_temporal_mode"] == 2
        and frame["two_hand_lab_temporal_active"],
        "fixture two-hand-lab identity fields mismatch",
    )
    require(
        approximately_equal(frame["two_hand_lab_offhand_influence"], 1.0)
        and approximately_equal(frame["two_hand_lab_agreement"], 0.958)
        and approximately_equal(
            frame["two_hand_lab_agreement_confidence"], 1.0)
        and approximately_equal(
            frame["two_hand_lab_effective_influence"], 1.0)
        and approximately_equal(
            frame["two_hand_lab_temporal_error_deg"], 2.5),
        "fixture two-hand-lab scalar values mismatch",
    )
    require(
        frame["two_hand_lab_primary_pivot_valid"]
        and frame["two_hand_lab_support_pivot_valid"]
        and frame["two_hand_lab_stateless_direction_valid"]
        and frame["two_hand_lab_presented_direction_valid"],
        "fixture two-hand-lab validity mismatch",
    )
    require(
        approximately_equal_sequence(
            frame["two_hand_lab_support_pivot"], [0.5, 0.0, -1.0]
        )
        and approximately_equal_sequence(
            frame["two_hand_lab_stateless_direction"], [0.0, 0.0, -1.0]
        ),
        "fixture two-hand-lab vector values mismatch",
    )
    require(
        frame["persistent_support_grip_configured"]
        and frame["persistent_support_grip_applicable"]
        and frame["support_relationship_readable"]
        and frame["support_relationship_engaged"]
        and frame["support_solve_trusted"]
        and not frame["support_force_one_hand"]
        and frame["support_epoch"] == 5,
        "fixture persistent-grip qualification mismatch",
    )
    # The stamped solve serial lags the frame by one; serial 0 can only carry
    # the initial published serial 0. Never an equality with prepared_serial.
    require(
        frame["support_solve_serial"]
        == (0 if frame["prepared_serial"] == 0 else frame["prepared_serial"] - 1),
        "fixture persistent-grip solve serial lag mismatch",
    )
    require(
        frame["presented_aim_valid"]
        and approximately_equal_sequence(
            frame["presented_aim_forward"], [0.0, 0.0, -1.0]
        ),
        "fixture presented-aim validity or forward mismatch",
    )
    # The reticle serial (41) matches neither fixture prepared serial (0,
    # 42): capture precedes the reticle publish, so the lag is structural and
    # must validate. No equality assertion with prepared_serial exists.
    require(
        frame["reticle_presented_valid"]
        and frame["reticle_presented_serial"] == 41
        and frame["reticle_presented_serial"] != frame["prepared_serial"]
        and frame["reticle_presented_sample_ms"] == 1234567
        and frame["reticle_presented_support_epoch"] == 5
        and frame["reticle_presented_support_trusted"] is True,
        "fixture reticle-presented provenance mismatch",
    )
    require(
        approximately_equal_sequence(
            frame["reticle_presented_orientation"], [0, 0, 0, 1]
        )
        and approximately_equal_sequence(
            frame["reticle_presented_position"], [0.5, -0.25, 4.0]
        ),
        "fixture reticle-presented pose mismatch",
    )
    require(
        frame["engine_aim_valid"]
        and frame["engine_aim_source"] == 1
        and approximately_equal_sequence(
            frame["engine_aim_forward"], [1, 0, 0]
        )
        and frame["engine_aim_serial"] == 0
        and frame["engine_aim_sample_ms"] == 0
        and frame["engine_aim_pitch_valid"] is False
        and frame["engine_aim_pitch_deg"] == 0,
        "fixture engine-aim validity, source or latest-only coupling mismatch",
    )
    require(
        frame["engine_camera_base_valid"]
        and approximately_equal_sequence(
            frame["engine_camera_base_position"], [10, 20, 30]
        )
        and frame["engine_camera_eye_valid"]
        and approximately_equal_sequence(
            frame["engine_camera_eye_position"], [11, 21, 31]
        )
        and frame["engine_camera_source"] == 1
        and frame["engine_camera_serial"] == 0
        and frame["world_scale_valid"]
        and approximately_equal(frame["world_scale"], 1.0 / 3.048)
        and frame["dual_active"] is True,
        "fixture engine-camera, scale or dual identity mismatch",
    )
    require(
        approximately_equal(
            frame["effective_settings"]["aim_stabilization"], 0.48
        )
        and approximately_equal(
            frame["effective_settings"]["crosshair_distance_m"], 41.0
        )
        and approximately_equal(
            frame["effective_settings"]["crosshair_size_deg"], 10.1
        )
        and frame["effective_settings"]["crosshair"] is True
        and frame["effective_settings"]["kill_reticle"] is True,
        "fixture effective reticle/steering settings mismatch",
    )
    require(
        frame["pad"]["weapon_buttons"] == 17
        and frame["pad"]["weapon_pulse_until_ms"] == 7654321
        and frame["pad"]["weapon_generation"] == 3,
        "fixture weapon-gesture pad fields mismatch",
    )


def records_to_text(records) -> str:
    return "".join(json.dumps(record) + "\n" for record in records)


def frame_records(records):
    """The frame rows of a raw capture.

    A raw stream is mixed: complete frames plus sparse event records
    (weapon_event / event_gap). Self-test mutations that touch per-frame fields
    must operate on frames only.
    """
    return [record for record in records if record.get("type") == "frame"]


def require_rejected(records, message: str) -> None:
    try:
        validate_text(records_to_text(records))
    except ValueError:
        return
    raise AssertionError(message)


def profile_for_role(start, role):
    return next(
        entry
        for entry in start["test_profile_enum_mapping"]
        if entry["role"] == role
    )


def run_self_test(fixture_raw: str) -> None:
    records, partial = parse_recoverable(fixture_raw)
    require(not partial, "clean fixture parsed as partial")
    start = records[0]
    # The self-test's per-frame mutations are positional ([1] and [2] are the
    # two fixture frames), so keep the frame rows in capture order and place the
    # sparse event rows after them in this in-memory copy. Both orders are legal
    # in a real file (the worker drains the event channel before the frame ring),
    # and the reorder is confined to the self-test's own record list.
    body_frames = frame_records(records[1:-1])
    body_events = [
        record
        for record in records[1:-1]
        if record.get("type") != "frame"
    ]
    records = [start] + body_frames + body_events + [records[-1]]
    end = dict(records[-1])
    end.update(
        producer_calls=0,
        duplicate_serial_suppressed=0,
        enqueued=0,
        written=0,
        dropped_queue_full=0,
    )
    empty_clean = json.dumps(start) + "\n" + json.dumps(end) + "\n"
    require(validate_text(empty_clean), "empty clean session was rejected")

    validate_two_hand_input_smoothing_family({})
    # Boolean-era shape: the family without the strength block is still valid.
    validate_two_hand_input_smoothing_family({
        "two_hand_transition_smoothing_configured": True,
        "two_hand_transition_active": True,
        "two_hand_smoothing_configured": True,
        "two_hand_smoothing_applied": True,
        "two_hand_smoothing_alpha": 0.25,
        "two_hand_smoothing_primary_orientation_error_deg": 12.0,
        "two_hand_smoothing_primary_position_error_m": 0.02,
        "two_hand_smoothing_support_position_error_m": 0.03,
    })
    smoothing_family = {
        "two_hand_transition_smoothing_configured": True,
        "two_hand_transition_active": True,
        "two_hand_smoothing_configured": True,
        "two_hand_smoothing_applied": True,
        "two_hand_smoothing_strength": 12.5,
        "two_hand_smoothing_mix": 0.5,
        "two_hand_smoothing_alpha": 0.5,
        "two_hand_smoothing_primary_orientation_error_deg": 12.0,
        "two_hand_smoothing_primary_position_error_m": 0.02,
        "two_hand_smoothing_support_position_error_m": 0.03,
    }
    validate_two_hand_input_smoothing_family(smoothing_family)
    # Strength 25 -> mix exactly 1, and strength 0 -> mix exactly 0 with the
    # configured flag clear are both legal shapes.
    validate_two_hand_input_smoothing_family({
        **smoothing_family,
        "two_hand_smoothing_strength": 25.0,
        "two_hand_smoothing_mix": 1.0,
    })
    validate_two_hand_input_smoothing_family({
        **smoothing_family,
        "two_hand_smoothing_strength": 0.0,
        "two_hand_smoothing_mix": 0.0,
        "two_hand_smoothing_configured": False,
        "two_hand_smoothing_applied": False,
    })
    invalid_smoothing_family = dict(smoothing_family)
    invalid_smoothing_family["two_hand_smoothing_configured"] = False
    try:
        validate_two_hand_input_smoothing_family(invalid_smoothing_family)
        raise AssertionError("applied smoothing without config was accepted")
    except ValueError:
        pass
    invalid_smoothing_family = dict(smoothing_family)
    invalid_smoothing_family["two_hand_smoothing_alpha"] = 1.01
    try:
        validate_two_hand_input_smoothing_family(invalid_smoothing_family)
        raise AssertionError("out-of-range smoothing alpha was accepted")
    except ValueError:
        pass
    invalid_smoothing_family = dict(smoothing_family)
    invalid_smoothing_family["two_hand_smoothing_strength"] = 26.0
    try:
        validate_two_hand_input_smoothing_family(invalid_smoothing_family)
        raise AssertionError("out-of-range smoothing strength was accepted")
    except ValueError:
        pass
    invalid_smoothing_family = dict(smoothing_family)
    invalid_smoothing_family["two_hand_smoothing_mix"] = 0.9
    try:
        validate_two_hand_input_smoothing_family(invalid_smoothing_family)
        raise AssertionError(
            "a mix inconsistent with the strength was accepted")
    except ValueError:
        pass
    invalid_smoothing_family = dict(smoothing_family)
    invalid_smoothing_family["two_hand_smoothing_mix"] = 1.2
    try:
        validate_two_hand_input_smoothing_family(invalid_smoothing_family)
        raise AssertionError("out-of-range smoothing mix was accepted")
    except ValueError:
        pass
    invalid_smoothing_family = dict(smoothing_family)
    invalid_smoothing_family["two_hand_smoothing_configured"] = False
    invalid_smoothing_family["two_hand_smoothing_applied"] = False
    try:
        validate_two_hand_input_smoothing_family(invalid_smoothing_family)
        raise AssertionError(
            "a non-zero strength with configured false was accepted")
    except ValueError:
        pass
    # alpha is the temporal coefficient, never the user amount: mix 0.5 with
    # strength 12.5 is legal while alpha reads its own, unrelated response.
    distinguish_alpha = dict(smoothing_family)
    distinguish_alpha["two_hand_smoothing_alpha"] = 0.0
    validate_two_hand_input_smoothing_family(distinguish_alpha)
    # The free two-hand offhand authority is presence-tolerant and range-checked
    # where present; boolean-era frames omit it legitimately.
    validate_two_hand_input_smoothing_family({
        **smoothing_family,
        "two_hand_offhand_influence": 0.5,
    })
    validate_two_hand_input_smoothing_family({
        **smoothing_family,
        "two_hand_offhand_influence": 0.0,
    })
    validate_two_hand_input_smoothing_family({
        **smoothing_family,
        "two_hand_offhand_influence": 1.0,
    })
    invalid_smoothing_family = dict(smoothing_family)
    invalid_smoothing_family["two_hand_offhand_influence"] = 1.25
    try:
        validate_two_hand_input_smoothing_family(invalid_smoothing_family)
        raise AssertionError("out-of-range offhand influence was accepted")
    except ValueError:
        pass
    invalid_smoothing_family = dict(smoothing_family)
    invalid_smoothing_family["two_hand_offhand_influence"] = "0.5"
    try:
        validate_two_hand_input_smoothing_family(invalid_smoothing_family)
        raise AssertionError("a non-numeric offhand influence was accepted")
    except ValueError:
        pass

    reordered = copy.deepcopy(records)
    reordered[0]["test_profile_enum_mapping"].reverse()
    require(
        validate_text(records_to_text(reordered)),
        "self-describing mapping became order-dependent",
    )

    renumbered = copy.deepcopy(records)
    id_remap = {}
    for index, entry in enumerate(renumbered[0]["test_profile_enum_mapping"]):
        old_id = entry["id"]
        entry["id"] = 200 + index
        id_remap[old_id] = entry["id"]
    for frame in frame_records(renumbered):
        frame["test_profile_id"] = id_remap[frame["test_profile_id"]]
        for field in CONTROL_ROLE_BY_FIELD:
            frame[field]["profile_id"] = id_remap[frame[field]["profile_id"]]
    require(
        validate_text(records_to_text(renumbered)),
        "self-consistent file-local profile renumbering was rejected",
    )

    extended = copy.deepcopy(records)
    used_ids = {
        entry["id"] for entry in extended[0]["test_profile_enum_mapping"]
    }
    extra_id = next(value for value in range(255, -1, -1) if value not in used_ids)
    extended[0]["test_profile_enum_mapping"].append(
        {
            "id": extra_id,
            "name": "FutureTuningProfile",
            "role": "tuning",
            "uses_custom_settings": False,
            "horizontal_release_enabled": False,
        }
    )
    require(
        validate_text(records_to_text(extended)),
        "additional non-control profile was rejected",
    )

    enabled_release = copy.deepcopy(records)
    selected_id = enabled_release[1]["test_profile_id"]
    selected_mapping = next(
        entry
        for entry in enabled_release[0]["test_profile_enum_mapping"]
        if entry["id"] == selected_id
    )
    selected_mapping["horizontal_release_enabled"] = True
    for enabled_frame in frame_records(enabled_release):
        enabled_frame["effective_settings"]["horizontal_release_enabled"] = True
        enabled_frame["effective_settings"]["hybrid_diagnostic_override"] = 2
        enabled_trace = enabled_frame["aim_trace"]
        enabled_trace["effective_diagnostic_override"] = 2
        enabled_trace["override_applied"] = True
        enabled_trace["w_after_diagnostic"] = 1.0
        enabled_trace["horizontal_release_attempted"] = True
        enabled_trace["horizontal_release_valid"] = True
        enabled_trace["rear_horizontal_reach_m"] = 0.3475
        enabled_trace["horizontal_release_influence"] = 0.5
        enabled_trace["w_effective"] = 0.5
    require(
        validate_text(records_to_text(enabled_release)),
        "internally consistent enabled horizontal release was rejected",
    )

    enabled_non_hybrid = copy.deepcopy(enabled_release)
    for non_hybrid_frame in frame_records(enabled_non_hybrid):
        non_hybrid_trace = non_hybrid_frame["aim_trace"]
        non_hybrid_trace["path"] = 1
        non_hybrid_trace["c_valid"] = False
        non_hybrid_trace["force_stock_eligible"] = False
        non_hybrid_trace["effective_diagnostic_override"] = 0
        non_hybrid_trace["override_applied"] = False
        non_hybrid_trace["w_natural_valid"] = False
        non_hybrid_trace["w_natural"] = 0.0
        non_hybrid_trace["w_after_diagnostic_valid"] = False
        non_hybrid_trace["w_after_diagnostic"] = 0.0
        non_hybrid_trace["w_effective_valid"] = False
        non_hybrid_trace["w_effective"] = 0.0
        non_hybrid_trace["horizontal_release_attempted"] = False
        non_hybrid_trace["horizontal_release_valid"] = False
        non_hybrid_trace["rear_horizontal_reach_m"] = 0.0
        non_hybrid_trace["horizontal_release_influence"] = 0.0
    require(
        validate_text(records_to_text(enabled_non_hybrid)),
        "enabled profile taking a non-Hybrid path was rejected",
    )

    wrong_release_curve = copy.deepcopy(enabled_release)
    wrong_release_curve[1]["aim_trace"]["rear_horizontal_reach_m"] = 0.20
    wrong_release_curve[1]["aim_trace"]["horizontal_release_influence"] = 0.5
    wrong_release_curve[1]["aim_trace"]["w_effective"] = 0.5
    require_rejected(
        wrong_release_curve,
        "horizontal release influence inconsistent with reach was accepted",
    )

    invalid_nonzero_authority = copy.deepcopy(records)
    invalid_trace = invalid_nonzero_authority[1]["aim_trace"]
    invalid_trace["c_valid"] = False
    invalid_trace["force_stock_eligible"] = False
    invalid_trace["w_natural_valid"] = False
    invalid_trace["w_natural"] = 0.5
    invalid_trace["w_after_diagnostic_valid"] = False
    invalid_trace["w_after_diagnostic"] = 0.5
    invalid_trace["w_effective_valid"] = False
    invalid_trace["w_effective"] = 0.5
    require_rejected(
        invalid_nonzero_authority,
        "invalid Hybrid authority stages retained nonzero values",
    )

    duplicate_id = copy.deepcopy(records)
    duplicate_id[0]["test_profile_enum_mapping"].append(
        {
            "id": duplicate_id[0]["test_profile_enum_mapping"][0]["id"],
            "name": "DuplicateIdProfile",
            "role": "tuning",
            "uses_custom_settings": False,
            "horizontal_release_enabled": False,
        }
    )
    require_rejected(duplicate_id, "duplicate profile ID was accepted")

    duplicate_name = copy.deepcopy(records)
    duplicate_name[0]["test_profile_enum_mapping"].append(
        {
            "id": extra_id,
            "name": duplicate_name[0]["test_profile_enum_mapping"][0]["name"],
            "role": "tuning",
            "uses_custom_settings": False,
            "horizontal_release_enabled": False,
        }
    )
    require_rejected(duplicate_name, "duplicate profile name was accepted")

    missing_role = copy.deepcopy(records)
    profile_for_role(missing_role[0], "vs_off_control")["role"] = "tuning"
    require_rejected(missing_role, "missing required control role was accepted")

    duplicate_role = copy.deepcopy(records)
    profile_for_role(duplicate_role[0], "tuning")["role"] = "vs_off_control"
    require_rejected(duplicate_role, "duplicate required control role was accepted")

    unknown_role = copy.deepcopy(records)
    unknown_role[0]["test_profile_enum_mapping"][0]["role"] = "unknown_role"
    require_rejected(unknown_role, "unknown profile role was accepted")

    unknown_selected = copy.deepcopy(records)
    unknown_selected[1]["test_profile_id"] = extra_id
    require_rejected(unknown_selected, "unknown selected profile ID was accepted")

    mismatched_name = copy.deepcopy(records)
    mismatched_name[1]["test_profile_name"] = "MismatchedProfileName"
    require_rejected(mismatched_name, "selected profile name mismatch was accepted")

    mismatched_custom = copy.deepcopy(records)
    mismatched_custom[1]["test_profile_custom"] = not mismatched_custom[1][
        "test_profile_custom"
    ]
    require_rejected(mismatched_custom, "selected profile Custom policy mismatch was accepted")

    unknown_control = copy.deepcopy(records)
    unknown_control[1]["cf_vs_off"]["profile_id"] = extra_id
    require_rejected(unknown_control, "unknown control profile ID was accepted")

    wrong_control = copy.deepcopy(records)
    fixed_head = profile_for_role(wrong_control[0], "fixed_head_control")
    wrong_control[1]["cf_vs_off"]["profile_id"] = fixed_head["id"]
    wrong_control[1]["cf_vs_off"]["profile_name"] = fixed_head["name"]
    require_rejected(wrong_control, "control profile assigned to the wrong role was accepted")

    malformed_role = copy.deepcopy(records)
    malformed_role[0]["test_profile_enum_mapping"][0]["role"] = []
    require_rejected(malformed_role, "non-string profile role was accepted")

    malformed_qpc = copy.deepcopy(records)
    malformed_qpc[1]["capture_begin_qpc"] = "100"
    malformed_qpc[1]["capture_end_qpc"] = "140"
    require_rejected(malformed_qpc, "string capture QPC fields were accepted")

    malformed_final = copy.deepcopy(records)
    malformed_final.append([])
    require_rejected(malformed_final, "non-object final record was accepted")

    missing_authority_stage = copy.deepcopy(records)
    del missing_authority_stage[1]["aim_trace"]["w_after_diagnostic"]
    require_rejected(
        missing_authority_stage,
        "missing post-diagnostic authority stage was accepted",
    )

    disabled_but_attempted = copy.deepcopy(records)
    disabled_but_attempted[1]["aim_trace"]["horizontal_release_attempted"] = True
    require_rejected(
        disabled_but_attempted,
        "disabled horizontal release was accepted as attempted",
    )

    malformed_authority = copy.deepcopy(records)
    malformed_authority[1]["aim_trace"]["w_natural"] = "0.375"
    require_rejected(
        malformed_authority,
        "non-numeric Hybrid authority was accepted",
    )

    out_of_range_influence = copy.deepcopy(records)
    out_of_range_influence[1]["aim_trace"]["horizontal_release_influence"] = 7.0
    require_rejected(
        out_of_range_influence,
        "out-of-range horizontal release influence was accepted",
    )

    inconsistent_authority = copy.deepcopy(records)
    inconsistent_authority[1]["aim_trace"]["w_effective"] = 0.9
    require_rejected(
        inconsistent_authority,
        "inconsistent Hybrid authority stages were accepted",
    )

    policy_mismatch = copy.deepcopy(records)
    policy_mismatch[1]["effective_settings"]["horizontal_release_enabled"] = True
    require_rejected(
        policy_mismatch,
        "profile horizontal-release policy mismatch was accepted",
    )

    inconsistent_diagnostic = copy.deepcopy(records)
    inconsistent_diagnostic[1]["effective_settings"][
        "hybrid_diagnostic_override"
    ] = 2
    inconsistent_diagnostic[1]["aim_trace"]["effective_diagnostic_override"] = 2
    inconsistent_diagnostic[1]["aim_trace"]["override_applied"] = True
    require_rejected(
        inconsistent_diagnostic,
        "inconsistent Force Stock diagnostic authority was accepted",
    )

    legacy_without_family = copy.deepcopy(records)
    for legacy_frame in frame_records(legacy_without_family):
        for key in list(legacy_frame):
            if key.startswith("transition_"):
                del legacy_frame[key]
    require(
        validate_text(records_to_text(legacy_without_family)),
        "an additive recording without the transition family was rejected",
    )

    invalid_stock_mode = copy.deepcopy(records)
    invalid_stock_mode[1]["transition_stock_mode_valid"] = False
    invalid_stock_mode[1]["transition_stock_mode"] = 1
    require_rejected(
        invalid_stock_mode,
        "an invalid stock mode reading Plus was accepted",
    )

    legacy_without_shots = copy.deepcopy(records)
    for legacy_frame in frame_records(legacy_without_shots):
        for key in list(legacy_frame):
            if (
                key.startswith("presented_aim")
                or key.startswith("reticle_presented")
                or key.startswith("engine_aim")
                or key.startswith("engine_camera")
                or key in ("world_scale_valid", "world_scale", "dual_active")
            ):
                del legacy_frame[key]
        for key in (
            "aim_stabilization",
            "crosshair_distance_m",
            "crosshair_size_deg",
            "crosshair",
            "kill_reticle",
        ):
            del legacy_frame["effective_settings"][key]
        for key in (
            "weapon_buttons",
            "weapon_pulse_until_ms",
            "weapon_generation",
        ):
            del legacy_frame["pad"][key]
    require(
        validate_text(records_to_text(legacy_without_shots)),
        "an additive recording without the shots-vs-reticle families was rejected",
    )

    legacy_without_persistent_grip = copy.deepcopy(records)
    for legacy_frame in frame_records(legacy_without_persistent_grip):
        for key in PERSISTENT_SUPPORT_GRIP_KEYS:
            del legacy_frame[key]
    require(
        validate_text(records_to_text(legacy_without_persistent_grip)),
        "an additive recording without the persistent-grip family was rejected",
    )

    # The all-ones sentinel means "the relationship could not be read" and must
    # survive the JSON round-trip as an exact integer, with its forcing
    # receipt and without an engaged relationship.
    unknown_epoch = copy.deepcopy(records)
    unknown_frame = unknown_epoch[1]
    unknown_frame["support_relationship_readable"] = False
    unknown_frame["support_relationship_engaged"] = False
    unknown_frame["support_solve_trusted"] = False
    unknown_frame["support_force_one_hand"] = True
    unknown_frame["support_epoch"] = SUPPORT_EPOCH_UNKNOWN
    require(
        validate_text(records_to_text(unknown_epoch)),
        "the unknown-relationship epoch sentinel was rejected",
    )

    sentinel_without_forcing = copy.deepcopy(unknown_epoch)
    sentinel_without_forcing[1]["support_force_one_hand"] = False
    require_rejected(
        sentinel_without_forcing,
        "an unknown-relationship epoch without its forcing receipt was accepted",
    )

    trusted_without_engagement = copy.deepcopy(records)
    trusted_without_engagement[1]["support_relationship_engaged"] = False
    require_rejected(
        trusted_without_engagement,
        "a trusted solve with a disengaged relationship was accepted",
    )

    trusted_and_forced = copy.deepcopy(records)
    trusted_and_forced[1]["support_force_one_hand"] = True
    require_rejected(
        trusted_and_forced,
        "a solve both trusted and forced to one hand was accepted",
    )

    unreadable_but_engaged = copy.deepcopy(records)
    unreadable_but_engaged[1]["support_relationship_readable"] = False
    require_rejected(
        unreadable_but_engaged,
        "an unreadable relationship reported as engaged was accepted",
    )

    fractional_epoch = copy.deepcopy(records)
    fractional_epoch[1]["support_epoch"] = 5.0
    require_rejected(
        fractional_epoch,
        "a non-integer support epoch was accepted",
    )

    solve_serial_ahead = copy.deepcopy(records)
    solve_serial_ahead[1]["support_solve_serial"] = (
        solve_serial_ahead[1]["prepared_serial"] + 1
    )
    require_rejected(
        solve_serial_ahead,
        "a solve serial ahead of the frame serial was accepted",
    )

    # The solve serial is its own value and is never asserted against
    # prepared_serial: the bound is deliberately conservative slack
    # (support_solve_serial <= prepared_serial), so the equality edge must
    # validate even though a real capture never produces it - the freeze
    # reads the serial published before this frame's own publish store, so
    # the stamped value stays one behind this frame's serial.
    equality_slack_solve = copy.deepcopy(records)
    for slack_frame in frame_records(equality_slack_solve):
        slack_frame["support_solve_serial"] = slack_frame["prepared_serial"]
    require(
        validate_text(records_to_text(equality_slack_solve)),
        "a solve serial equal to its frame serial was rejected",
    )

    # The reticle serial travels with the pose and is never asserted against
    # prepared_serial: equality must validate as well as the fixture's lag.
    lag_free = copy.deepcopy(records)
    for lag_frame in frame_records(lag_free):
        lag_frame["reticle_presented_serial"] = lag_frame["prepared_serial"]
    require(
        validate_text(records_to_text(lag_free)),
        "a reticle serial equal to prepared serial was rejected",
    )

    invalid_engine_source = copy.deepcopy(records)
    invalid_engine_source[1]["engine_aim_source"] = 9
    require_rejected(
        invalid_engine_source,
        "an unknown engine aim source was accepted",
    )

    valid_without_source = copy.deepcopy(records)
    valid_without_source[1]["engine_aim_source"] = 0
    require_rejected(
        valid_without_source,
        "a valid engine aim without a publication was accepted",
    )

    valid_zero_forward = copy.deepcopy(records)
    valid_zero_forward[1]["engine_aim_forward"] = [0, 0, 0]
    require_rejected(
        valid_zero_forward,
        "a valid engine aim with a zero forward was accepted",
    )

    serial_on_latest_only = copy.deepcopy(records)
    serial_on_latest_only[1]["engine_aim_serial"] = 7
    require_rejected(
        serial_on_latest_only,
        "a latest-only engine aim carrying a serial was accepted",
    )

    pitch_outside_h4 = copy.deepcopy(records)
    pitch_outside_h4[1]["engine_aim_pitch_valid"] = True
    pitch_outside_h4[1]["engine_aim_pitch_deg"] = 5.0
    require_rejected(
        pitch_outside_h4,
        "engine aim pitch valid outside Halo 4 was accepted",
    )

    retained_invalid_payload = copy.deepcopy(records)
    retained_invalid_payload[1]["engine_aim_valid"] = False
    require_rejected(
        retained_invalid_payload,
        "an invalid engine aim retaining its forward was accepted",
    )

    stale_reticle_payload = copy.deepcopy(records)
    stale_reticle_payload[1]["reticle_presented_valid"] = False
    require_rejected(
        stale_reticle_payload,
        "an invalid reticle pose retaining its serial was accepted",
    )

    absent_camera_with_eye = copy.deepcopy(records)
    absent_camera_with_eye[1]["engine_camera_source"] = 5
    require_rejected(
        absent_camera_with_eye,
        "an absent engine camera carrying an eye position was accepted",
    )

    # T-3 Halo 4 observer camera: eye-only with its serial; a missing serial
    # (or a base position) is rejected.
    h4_observer_camera = copy.deepcopy(records)
    h4_observer_camera[1]["engine_camera_source"] = 6
    h4_observer_camera[1]["engine_camera_base_valid"] = False
    h4_observer_camera[1]["engine_camera_base_position"] = [0, 0, 0]
    h4_observer_camera[1]["engine_camera_serial"] = 123
    require(
        validate_text(records_to_text(h4_observer_camera)),
        "a Halo 4 observer camera frame was rejected",
    )
    h4_camera_without_serial = copy.deepcopy(records)
    h4_camera_without_serial[1]["engine_camera_source"] = 6
    h4_camera_without_serial[1]["engine_camera_base_valid"] = False
    h4_camera_without_serial[1]["engine_camera_base_position"] = [0, 0, 0]
    require_rejected(
        h4_camera_without_serial,
        "a Halo 4 observer camera without its serial was accepted",
    )

    invalid_stock_mode_zero = copy.deepcopy(records)
    invalid_stock_mode_zero[1]["transition_stock_mode_valid"] = False
    invalid_stock_mode_zero[1]["transition_stock_mode"] = 0
    require(
        validate_text(records_to_text(invalid_stock_mode_zero)),
        "a disabled Virtual Stock reading Standard mode was rejected",
    )

    unknown_stock_mode = copy.deepcopy(records)
    unknown_stock_mode[1]["transition_stock_mode"] = 2
    require_rejected(
        unknown_stock_mode,
        "an unknown transition stock mode was accepted",
    )

    remaining_over_seed = copy.deepcopy(records)
    remaining_over_seed[1]["transition_remaining_correction_deg"] = 20.0
    require_rejected(
        remaining_over_seed,
        "a remaining correction larger than its seed was accepted",
    )

    nonmonotonic_advance = copy.deepcopy(records)
    nonmonotonic_advance[1]["transition_advance_count"] = 9
    nonmonotonic_advance[2]["transition_advance_count"] = 3
    require_rejected(
        nonmonotonic_advance,
        "a decreasing transition advance count was accepted",
    )

    # An engaged frame may legitimately write the layer output without the
    # process-lifetime advance count and the module's per-state serial implying
    # anything about each other. Two accepted shapes:
    #   (a) first engaged frame, non-positive prepared delta: no consumed serial
    #       at all, so advance_count == 0 and last_prepared_serial == 0;
    #   (b) an invalidation reset the module's serial to zero while the
    #       process-lifetime count persists: advance_count > 0 and last == 0.
    first_engaged = copy.deepcopy(records)
    for first_frame in frame_records(first_engaged):
        first_frame["transition_advance_count"] = 0
        first_frame["transition_last_prepared_serial"] = 0
    require(
        validate_text(records_to_text(first_engaged)),
        "the first engaged frame without a consumed advance was rejected",
    )

    applied_after_reset = copy.deepcopy(records)
    applied_after_reset[2]["transition_last_prepared_serial"] = 0
    require(
        validate_text(records_to_text(applied_after_reset)),
        "an applied serial after a module-serial reset was rejected",
    )

    applied_before_module_serial = copy.deepcopy(records)
    applied_before_module_serial[2]["transition_last_prepared_serial"] = 50
    require_rejected(
        applied_before_module_serial,
        "an applied serial preceding the module's last serial was accepted",
    )

    # An active transition over a frame whose live solve is invalid must present
    # nothing, not be rejected: the seam only marks presented valid when the
    # live solve is valid.
    invalid_live_active = copy.deepcopy(records)
    invalid_live_active[2]["transition_live_calibrated_forward_valid"] = False
    invalid_live_active[2]["transition_presented_forward_valid"] = False
    invalid_live_active[2]["transition_one_hand_anchor_valid"] = False
    require(
        validate_text(records_to_text(invalid_live_active)),
        "an active transition over an invalid live solve was rejected",
    )

    presented_without_live = copy.deepcopy(records)
    presented_without_live[2]["transition_live_calibrated_forward_valid"] = False
    require_rejected(
        presented_without_live,
        "a presented orientation without a valid live solve was accepted",
    )

    # ---- Sparse shot event contract (T-2) ----
    shot_index = next(
        (
            index
            for index, record in enumerate(records)
            if record.get("type") == "weapon_event"
        ),
        None,
    )
    require(shot_index is not None, "fixture has no sparse event to exercise")
    require(
        records[shot_index].get("kind") == "shot",
        "the fixture's sparse event is not a shot",
    )

    # A capture without the sparse event channel at all (older recordings, or a
    # session in which nothing fired) stays valid.
    legacy_without_events = [
        record for record in records if record.get("type") != "weapon_event"
    ]
    require(
        validate_text(records_to_text(legacy_without_events)),
        "a recording without sparse events was rejected",
    )

    # Non-shot kinds keep the shared envelope and stay forward compatible.
    other_kind = copy.deepcopy(records)
    other_kind[shot_index] = {
        "type": "weapon_event",
        "seq": 1,
        "session": 0,
        "qpc": 1,
        "thread_id": 2,
        "kind": "fp_weapon_commit",
        "status": "success",
        "title": 3,
        "title_generation": 4,
        "prepared_serial": 5,
        "controlled_unit": 6,
        "primary_weapon": 7,
        "aux0": 0,
        "aux1": 0,
    }
    require(
        validate_text(records_to_text(other_kind)),
        "a non-shot sparse event was rejected",
    )

    # A gap marker is valid and range-checked.
    with_gap = copy.deepcopy(records)
    with_gap.insert(
        shot_index,
        {"type": "event_gap", "first_missing_seq": 4, "last_missing_seq": 6},
    )
    require(
        validate_text(records_to_text(with_gap)),
        "an explicit gap marker was rejected",
    )
    inverted_gap = copy.deepcopy(with_gap)
    inverted_gap[shot_index]["last_missing_seq"] = 3
    require_rejected(inverted_gap, "an inverted gap range was accepted")

    # The shot payload is typed, finite and bounded, and its recorded serial is
    # deliberately not tied to any frame serial (zero and lagging are both
    # legal; the firing path consumes no serial inside the transaction).
    zero_serial = copy.deepcopy(records)
    zero_serial[shot_index]["prepared_serial"] = 0
    require(
        validate_text(records_to_text(zero_serial)),
        "a shot carrying no frame serial was rejected",
    )
    unknown_index = copy.deepcopy(records)
    unknown_index[shot_index]["shot_slot"] = -1
    unknown_index[shot_index]["shot_barrel"] = -1
    require(
        validate_text(records_to_text(unknown_index)),
        "a shot with unknown slot/barrel was rejected",
    )
    bad_flags = copy.deepcopy(records)
    bad_flags[shot_index]["shot_flags"] = 0x100
    require_rejected(
        bad_flags, "a shot flag outside the documented eight was accepted"
    )
    bad_source = copy.deepcopy(records)
    bad_source[shot_index]["shot_engine_aim_source"] = 9
    require_rejected(
        bad_source, "a shot engine aim source above 8 was accepted"
    )
    bad_source_type = copy.deepcopy(records)
    bad_source_type[shot_index]["shot_engine_aim_source"] = "1"
    require_rejected(
        bad_source_type, "a non-integer shot engine aim source was accepted"
    )
    missing_source = copy.deepcopy(records)
    del missing_source[shot_index]["shot_engine_aim_source"]
    require_rejected(
        missing_source, "a shot without its engine aim source was accepted"
    )
    source_without_aim = copy.deepcopy(records)
    source_without_aim[shot_index]["shot_engine_aim_source"] = 0
    require_rejected(
        source_without_aim,
        "a source-0 shot carrying an engine aim vector was accepted",
    )
    # Halo 2's provenance ordinals: 7 carries the observer publication's stock
    # forward (accepted), 8 is the explicit absent label and must then carry an
    # exactly zero vector just like source 0.
    h2_observer = copy.deepcopy(records)
    h2_observer[shot_index]["shot_engine_aim_source"] = 7
    require(
        validate_text(records_to_text(h2_observer)),
        "a Halo 2 observer shot snapshot was rejected",
    )
    h2_absent = copy.deepcopy(records)
    h2_absent[shot_index]["shot_engine_aim_source"] = 8
    h2_absent[shot_index]["shot_engine_aim"] = [0.0, 0.0, 0.0]
    require(
        validate_text(records_to_text(h2_absent)),
        "an absent Halo 2 shot snapshot was rejected",
    )
    h2_absent_with_aim = copy.deepcopy(h2_absent)
    h2_absent_with_aim[shot_index]["shot_engine_aim"] = [0.0, 0.0, -1.0]
    require_rejected(
        h2_absent_with_aim,
        "a source-8 shot carrying an engine aim vector was accepted",
    )
    bad_slot = copy.deepcopy(records)
    bad_slot[shot_index]["shot_slot"] = 2
    require_rejected(bad_slot, "a shot slot above 1 was accepted")
    bad_barrel = copy.deepcopy(records)
    bad_barrel[shot_index]["shot_barrel"] = -2
    require_rejected(bad_barrel, "a shot barrel below -1 was accepted")
    missing_vector = copy.deepcopy(records)
    del missing_vector[shot_index]["shot_origin"]
    require_rejected(missing_vector, "a shot without its origin was accepted")
    short_vector = copy.deepcopy(records)
    short_vector[shot_index]["shot_direction"] = [0.0, 0.0]
    require_rejected(short_vector, "a two-element shot vector was accepted")
    nan_vector = copy.deepcopy(records)
    nan_vector[shot_index]["shot_origin"] = [float("nan"), 0.0, -1.0]
    require_rejected(nan_vector, "a non-finite shot vector was accepted")
    string_vector = copy.deepcopy(records)
    string_vector[shot_index]["shot_engine_aim"] = ["0", 0.0, 1.0]
    require_rejected(string_vector, "a non-numeric shot vector was accepted")
    shot_key_on_other_kind = copy.deepcopy(other_kind)
    shot_key_on_other_kind[shot_index]["shot_flags"] = 1
    require_rejected(
        shot_key_on_other_kind,
        "a non-shot event carrying shot payload keys was accepted",
    )

    truncated = fixture_raw.rstrip("\r\n")[:-8]
    require(not validate_text(truncated), "truncated final record was accepted as clean")

    unterminated = fixture_raw.rstrip("\r\n")
    require(
        not validate_text(unterminated),
        "valid JSON without a final newline was accepted as clean",
    )

    corrupted_middle = json.dumps(start) + "\n{broken}\n" + json.dumps(end) + "\n"
    try:
        validate_text(corrupted_middle)
    except json.JSONDecodeError:
        pass
    else:
        raise AssertionError("malformed non-final records must be rejected")


def walk_values(value):
    if isinstance(value, dict):
        for child in value.values():
            yield from walk_values(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk_values(child)
    else:
        yield value


def approximately_equal_sequence(actual, expected, tolerance=1e-6):
    return len(actual) == len(expected) and all(
        math.isclose(left, right, abs_tol=tolerance)
        for left, right in zip(actual, expected)
    )


if __name__ == "__main__":
    main()
