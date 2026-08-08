#!/usr/bin/env python3
"""Validate and export Halo 4 storm_fp authored IK controls from Blender."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix


EVIDENCE_TEXT = "HALO4_VRIK_SOURCE_EVIDENCE.json"
CONTROL_NAMES = tuple(
    "vrik:%s_%s" % (side, role)
    for side in ("right", "left")
    for role in ("shoulder", "hand", "pole"))


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--allow-unplaced", action="store_true")
    parser.add_argument("--precision", type=int, default=9)
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return parser.parse_args(values)


def rounded(values, precision):
    return [round(float(value), precision) for value in values]


def matrix_rows(matrix, precision):
    return [rounded(matrix[row], precision) for row in range(4)]


def finite_matrix(matrix):
    return all(math.isfinite(matrix[r][c]) for r in range(4) for c in range(4))


def main():
    args = arguments()
    if not bpy.data.is_saved:
        raise SystemExit("ERROR: save the Halo 4 VRIK kit before exporting")
    evidence_text = bpy.data.texts.get(EVIDENCE_TEXT)
    if evidence_text is None:
        raise SystemExit("ERROR: missing embedded source evidence")
    evidence = json.loads(evidence_text.as_string())
    scale = float(evidence["world_units_to_metres"])
    if scale != 3.048 or evidence.get("node_count") != 80:
        raise SystemExit("ERROR: source evidence is not official storm_fp")

    actual = sorted(obj.name for obj in bpy.data.objects
                    if obj.name.startswith("vrik:"))
    problems = []
    if actual != sorted(CONTROL_NAMES):
        problems.append("control set differs: %s" % actual)
    controls = {}
    unplaced = []
    changed_controls = []
    for name in CONTROL_NAMES:
        obj = bpy.data.objects.get(name)
        if obj is None or obj.type != 'EMPTY':
            problems.append("%s is missing or not an Empty" % name)
            continue
        matrix = obj.matrix_world.copy()
        if not finite_matrix(matrix):
            problems.append("%s has a non-finite transform" % name)
            continue
        side, role = name[5:].split("_", 1)
        if obj.get("halo4_vrik_side") != side or obj.get("halo4_vrik_role") != role:
            problems.append("%s metadata changed" % name)
        if bool(obj.get("halo4_needs_user_placement", True)):
            unplaced.append(name)
        quaternion = matrix.to_quaternion().normalized()
        raw_seed = obj.get("halo4_vrik_seed_matrix")
        if raw_seed is None or len(raw_seed) != 16:
            problems.append("%s lacks its seed matrix" % name)
            continue
        seed = Matrix([raw_seed[row * 4:(row + 1) * 4]
                       for row in range(4)])
        seed_delta = max(abs(matrix[row][column] - seed[row][column])
                         for row in range(4) for column in range(4))
        changed = seed_delta > 1e-5
        if changed:
            changed_controls.append(name)
        controls[name] = {
            "side": side,
            "role": role,
            "placed": name not in unplaced,
            "changed_from_seed": changed,
            "max_matrix_delta_from_seed": round(seed_delta, args.precision),
            "position_metres": rounded(matrix.translation, args.precision),
            "position_world_units": rounded(matrix.translation / scale, args.precision),
            "rotation_xyzw": rounded(
                (quaternion.x, quaternion.y, quaternion.z, quaternion.w),
                args.precision),
            "matrix_world_metres": matrix_rows(matrix, args.precision),
        }
    expected_changed = sorted(evidence.get("expected_changed_controls", []))
    if expected_changed and sorted(changed_controls) != expected_changed:
        problems.append("changed controls differ: expected=%s actual=%s" %
                        (expected_changed, sorted(changed_controls)))
    if unplaced and not args.allow_unplaced:
        problems.append("%d controls still need placement: %s" %
                        (len(unplaced), ", ".join(unplaced)))
    if problems:
        for problem in problems:
            print("ERROR: " + problem, file=sys.stderr)
        raise SystemExit(2)

    sides = {}
    for side in ("right", "left"):
        shoulder = bpy.data.objects["vrik:%s_shoulder" % side].matrix_world
        hand = bpy.data.objects["vrik:%s_hand" % side].matrix_world
        pole = bpy.data.objects["vrik:%s_pole" % side].matrix_world
        pole_vector = pole.translation - shoulder.translation
        if pole_vector.length <= 1e-6:
            problems.append("%s pole coincides with shoulder" % side)
            continue
        hand_vector = hand.translation - shoulder.translation
        hand_q = hand.to_quaternion().normalized()
        sides[side] = {
            "shoulder_position_metres": rounded(shoulder.translation, args.precision),
            "shoulder_position_world_units": rounded(
                shoulder.translation / scale, args.precision),
            "hand_target_position_metres": rounded(hand.translation, args.precision),
            "hand_target_position_world_units": rounded(
                hand.translation / scale, args.precision),
            "hand_target_rotation_xyzw": rounded(
                (hand_q.x, hand_q.y, hand_q.z, hand_q.w), args.precision),
            "shoulder_to_hand_metres": rounded(hand_vector, args.precision),
            "pole_position_metres": rounded(pole.translation, args.precision),
            "pole_position_world_units": rounded(
                pole.translation / scale, args.precision),
            "pole_direction_from_shoulder": rounded(
                pole_vector.normalized(), args.precision),
        }

    expected_attachments = {
        item["name"]: item for item in evidence.get("controller_attachments", [])}
    actual_attachments = {
        obj.name: obj for obj in bpy.data.objects
        if obj.get("halo4_vrik_attachment_role")}
    if set(actual_attachments) != set(expected_attachments):
        problems.append("controller attachment set differs: expected=%s actual=%s" %
                        (sorted(expected_attachments), sorted(actual_attachments)))
    attachments = {}
    for name, expected in expected_attachments.items():
        obj = actual_attachments.get(name)
        if obj is None:
            continue
        controller_name = expected["controller"]
        controller = bpy.data.objects.get(controller_name)
        if controller is None or obj.parent != controller:
            problems.append("%s is not parented to %s" % (name, controller_name))
            continue
        world = obj.matrix_world.copy()
        local = controller.matrix_world.inverted() @ world
        if not finite_matrix(world) or not finite_matrix(local):
            problems.append("%s has a non-finite attachment transform" % name)
            continue
        expected_local = expected.get("position_controller_local_metres")
        if expected_local is not None:
            position_error = max(abs(local.translation[index] - expected_local[index])
                                 for index in range(3))
            if position_error > 1e-6:
                problems.append("%s controller-local origin changed by %.9g m" %
                                (name, position_error))
        if (obj.get("halo4_vrik_attachment_role") != expected["role"] or
                obj.get("halo4_vrik_side") != expected["side"] or
                obj.get("halo4_vrik_controller") != controller_name):
            problems.append("%s attachment metadata changed" % name)
        local_q = local.to_quaternion().normalized()
        attachments[name] = {
            "role": expected["role"],
            "side": expected["side"],
            "controller": controller_name,
            "runtime_uses_scale": False,
            "position_controller_local_metres": rounded(
                local.translation, args.precision),
            "rotation_controller_local_xyzw": rounded(
                (local_q.x, local_q.y, local_q.z, local_q.w), args.precision),
            "matrix_controller_local_metres_audit_only": matrix_rows(
                local, args.precision),
            "matrix_world_metres_audit_only": matrix_rows(world, args.precision),
        }
    if problems:
        for problem in problems:
            print("ERROR: " + problem, file=sys.stderr)
        raise SystemExit(2)

    output = args.output or Path(bpy.data.filepath).with_name("halo4_vrik_points.json")
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": 1,
        "title": "Halo 4 storm_fp authored VRIK landmarks",
        "source_blend": str(Path(bpy.data.filepath).resolve()),
        "source_render_model": evidence["source_render_model"],
        "source_render_model_sha256": evidence["source_render_model_sha256"],
        "h4ek_build": evidence["h4ek_build"],
        "world_units_to_metres": scale,
        "axes": {"x": "forward", "y": "left", "z": "up"},
        "node_count": 80,
        "all_controls_marked_placed": not unplaced,
        "changed_controls_from_seed": sorted(changed_controls),
        "sides": sides,
        "controls": controls,
        "controller_attachments": attachments,
    }
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print("HALO4_VRIK_POINTS_READY")
    print("controls=6 placed=%d" % (6 - len(unplaced)))
    print("output=%s" % output)


if __name__ == "__main__":
    main()
