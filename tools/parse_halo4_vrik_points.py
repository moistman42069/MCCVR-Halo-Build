#!/usr/bin/env python3
"""Validate and export Halo 4 storm_fp authored IK controls from Blender."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import bpy


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
        controls[name] = {
            "side": side,
            "role": role,
            "placed": name not in unplaced,
            "position_metres": rounded(matrix.translation, args.precision),
            "position_world_units": rounded(matrix.translation / scale, args.precision),
            "rotation_xyzw": rounded(
                (quaternion.x, quaternion.y, quaternion.z, quaternion.w),
                args.precision),
            "matrix_world_metres": matrix_rows(matrix, args.precision),
        }
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
        "sides": sides,
        "controls": controls,
    }
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print("HALO4_VRIK_POINTS_READY")
    print("controls=6 placed=%d" % (6 - len(unplaced)))
    print("output=%s" % output)


if __name__ == "__main__":
    main()
