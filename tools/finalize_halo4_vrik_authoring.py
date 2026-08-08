#!/usr/bin/env python3
"""Keep authored poles, reset test controls, and bind grip attachments."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import shutil
import sys

import bpy
from mathutils import Matrix


ATTACHMENTS = (
    {
        "name": "Gun placement left",
        "side": "left",
        "role": "gun_placement",
        "controller": "vrik:left_hand",
    },
    {
        "name": "right hand, two handed lock in zone",
        "side": "right",
        "role": "two_handed_lock_zone",
        "controller": "vrik:right_hand",
    },
)


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--extractor", type=Path, required=True)
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return parser.parse_args(values)


def seed_matrix(obj):
    raw = obj.get("halo4_vrik_seed_matrix")
    if raw is None or len(raw) != 16:
        raise ValueError("%s lacks its 4x4 seed matrix" % obj.name)
    matrix = Matrix([raw[row * 4:(row + 1) * 4] for row in range(4)])
    if not all(math.isfinite(matrix[row][column])
               for row in range(4) for column in range(4)):
        raise ValueError("%s seed matrix is non-finite" % obj.name)
    return matrix


def replace_text(name, source):
    old = bpy.data.texts.get(name)
    if old is not None:
        bpy.data.texts.remove(old)
    text = bpy.data.texts.new(name)
    text.write(Path(source).read_text(encoding="utf-8"))


def main():
    args = arguments()
    output = args.output.resolve()
    controls = [obj for obj in bpy.data.objects if obj.name.startswith("vrik:")]
    if len(controls) != 6:
        raise SystemExit("ERROR: expected exactly six VRIK controls")

    retained_poles = {}
    for obj in controls:
        role = obj.get("halo4_vrik_role")
        if role == "pole":
            retained_poles[obj.name] = [
                [float(obj.matrix_world[row][column]) for column in range(4)]
                for row in range(4)]
            seed = seed_matrix(obj)
            delta = max(abs(obj.matrix_world[row][column] - seed[row][column])
                        for row in range(4) for column in range(4))
            if delta <= 1e-5:
                raise SystemExit("ERROR: %s was not moved from seed" % obj.name)
        else:
            obj.matrix_world = seed_matrix(obj)
        obj["halo4_needs_user_placement"] = False

    attachment_evidence = []
    for spec in ATTACHMENTS:
        obj = bpy.data.objects.get(spec["name"])
        controller = bpy.data.objects.get(spec["controller"])
        if obj is None or obj.type != 'EMPTY':
            raise SystemExit("ERROR: missing authored Empty %s" % spec["name"])
        if controller is None or controller.type != 'EMPTY':
            raise SystemExit("ERROR: missing controller %s" % spec["controller"])
        world = obj.matrix_world.copy()
        obj.parent = controller
        obj.parent_type = 'OBJECT'
        obj.matrix_parent_inverse = Matrix.Identity(4)
        obj.matrix_world = world
        bpy.context.view_layer.update()
        preserved = max(abs(obj.matrix_world[row][column] - world[row][column])
                        for row in range(4) for column in range(4))
        if preserved > 1e-6:
            raise SystemExit("ERROR: parenting moved %s by matrix delta %.9g" %
                             (obj.name, preserved))
        obj["halo4_vrik_attachment_role"] = spec["role"]
        obj["halo4_vrik_side"] = spec["side"]
        obj["halo4_vrik_controller"] = spec["controller"]
        obj["halo4_vrik_scale_is_runtime_data"] = False
        local = controller.matrix_world.inverted() @ obj.matrix_world
        record = dict(spec)
        record["position_controller_local_metres"] = [
            float(value) for value in local.translation]
        record["scale_is_runtime_data"] = False
        attachment_evidence.append(record)

    evidence_text = bpy.data.texts.get("HALO4_VRIK_SOURCE_EVIDENCE.json")
    if evidence_text is None:
        raise SystemExit("ERROR: source evidence is missing")
    evidence = json.loads(evidence_text.as_string())
    evidence["authoring_filter"] = (
        "only saved pole transforms retained; hand and shoulder controls reset to seeds")
    evidence["retained_pole_matrices_metres"] = retained_poles
    evidence["expected_changed_controls"] = sorted(retained_poles)
    evidence["controller_attachments"] = attachment_evidence
    bpy.data.texts.remove(evidence_text)
    evidence_text = bpy.data.texts.new("HALO4_VRIK_SOURCE_EVIDENCE.json")
    evidence_text.write(json.dumps(evidence, indent=2) + "\n")

    replace_text("halo4_vrik_authoring.py", args.helper.resolve())
    replace_text("parse_halo4_vrik_points.py", args.extractor.resolve())
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    shutil.copy2(args.helper.resolve(), output.parent / args.helper.name)
    shutil.copy2(args.extractor.resolve(), output.parent / args.extractor.name)
    print("HALO4_VRIK_AUTHORED_READY")
    print("retained_poles=2 reset_non_poles=4 attachments=2")
    print("output=%s" % output)


if __name__ == "__main__":
    main()
