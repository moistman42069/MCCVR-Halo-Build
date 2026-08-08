#!/usr/bin/env python3
"""Build an official-H4EK storm_fp VRIK landmark scene in Blender."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from halo4_fp_tag import parse_render_model  # noqa: E402
from validate_halo4_vrik_alignment import validate_alignment  # noqa: E402


WU_TO_M = 3.048
MESH_NAMES = {
    3: "storm_fp:techsuit",
    50: "storm_fp:chief_arms",
    97: "storm_fp:left_shoulder",
    98: "storm_fp:right_shoulder",
}

README = """HALO 4 storm_fp VRIK LANDMARK KIT

Open the newest halo4_storm_fp_vrik_v*.blend in Blender 4.3 or newer.

Enable the panel by either:
  * Edit > Preferences > Add-ons > Install from Disk, then choose
    halo4_vrik_authoring.py from this folder; or
  * open the Scripting workspace, open the embedded
    halo4_vrik_authoring.py text, and press Run Script.

Open the 3D View sidebar with N and choose Halo 4 VRIK.
For each arm:
  1. Put the shoulder circle at the shoulder pivot.
  2. Put the hand axes at the desired grip position and rotation.
  3. Put the pole sphere in the direction the elbow should point.
  4. Click Placed for all six controls.
  5. Click Export VRIK JSON.

The exporter writes halo4_vrik_points.json beside the saved .blend. It refuses
missing, renamed, non-finite, or unplaced controls. The runtime consumes this
calibration; the scene never modifies Halo 4 tags.

Scene coordinates are metres. Blam tag axes are +X forward, +Y left, +Z up.
The visible skinned mesh and 80-node skeleton come from the official H4EK
storm_fp.render_model tag. Exact source identity is embedded in the .blend.
"""


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--xml", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--extractor", type=Path, required=True)
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return parser.parse_args(values)


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest().upper()


def flatten_matrix(matrix):
    return [float(matrix[row][column]) for row in range(4) for column in range(4)]


def world_matrices(nodes):
    result = []
    for node in nodes:
        x, y, z, w = node["rotation"]
        rotation = Quaternion((w, x, y, z)).normalized()
        local = (Matrix.Translation(Vector(node["translation"]) * WU_TO_M) @
                 rotation.to_matrix().to_4x4() @
                 Matrix.Scale(float(node["scale"]), 4))
        parent = node["parent"]
        result.append(local if parent < 0 else result[parent] @ local)
    return result


def reset_scene():
    bpy.ops.object.mode_set(mode='OBJECT') if bpy.context.object and bpy.context.object.mode != 'OBJECT' else None
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)


def new_collection(name):
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    return collection


def relink(obj, collection):
    for current in list(obj.users_collection):
        current.objects.unlink(obj)
    collection.objects.link(obj)


def make_armature(nodes, worlds, collection):
    data = bpy.data.armatures.new("storm_fp_skeleton")
    obj = bpy.data.objects.new("storm_fp_skeleton", data)
    collection.objects.link(obj)
    obj.show_in_front = True
    obj.display_type = 'WIRE'
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')

    children = {index: [] for index in range(len(nodes))}
    for node in nodes:
        if node["parent"] >= 0:
            children[node["parent"]].append(node["index"])
    preferred = {
        4: 16, 16: 29,
        5: 8, 8: 37,
        29: 49, 37: 43,
    }
    for node in nodes:
        index = node["index"]
        bone = data.edit_bones.new(node["name"])
        head = worlds[index].translation
        child_indices = children[index]
        child = preferred.get(index)
        if child is None and child_indices:
            child = max(child_indices,
                        key=lambda item: (worlds[item].translation - head).length)
        if child is not None and (worlds[child].translation - head).length > 0.002:
            tail = worlds[child].translation
        else:
            parent = node["parent"]
            axis = (head - worlds[parent].translation
                    if parent >= 0 else Vector())
            if axis.length >= 0.002:
                axis = axis.normalized() * min(0.035, max(0.012, axis.length * 0.5))
            else:
                axis = worlds[index].to_quaternion() @ Vector((0.025, 0.0, 0.0))
            if axis.length < 0.002:
                axis = Vector((0.025, 0.0, 0.0))
            tail = head + axis
        bone.head = head
        bone.tail = tail
        bone.use_deform = True
    for node in nodes:
        if node["parent"] >= 0:
            data.edit_bones[node["name"]].parent = data.edit_bones[
                nodes[node["parent"]]["name"]]
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.select_set(False)
    obj["halo4_source_node_count"] = len(nodes)
    return obj


def make_material(name, color, metallic=0.0):
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*color, 1.0)
    material.metallic = metallic
    material.roughness = 0.5
    return material


def make_mesh(index, raw, compression, nodes, armature, collection, material):
    name = MESH_NAMES[index]
    lower = compression["position_min"]
    upper = compression["position_max"]
    vertices = [tuple(
        (lower[axis] + item["position"][axis] *
         (upper[axis] - lower[axis])) * WU_TO_M
        for axis in range(3)) for item in raw["vertices"]]
    faces = [tuple(raw["indices"][offset:offset + 3])
             for offset in range(0, len(raw["indices"]), 3)]
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.materials.append(material)
    data.update()
    obj = bpy.data.objects.new(name, data)
    collection.objects.link(obj)
    obj.show_in_front = False
    obj["halo4_raw_mesh_index"] = index

    used_nodes = sorted(set(raw["node_map"]))
    groups = {node_index: obj.vertex_groups.new(name=nodes[node_index]["name"])
              for node_index in used_nodes}
    for vertex_index, vertex in enumerate(raw["vertices"]):
        accumulated = {}
        for local, weight in zip(vertex["nodes"], vertex["weights"]):
            if weight <= 0.0:
                continue
            global_index = raw["node_map"][local]
            accumulated[global_index] = accumulated.get(global_index, 0.0) + weight
        total = sum(accumulated.values())
        if total <= 0.0:
            continue
        for global_index, weight in accumulated.items():
            groups[global_index].add([vertex_index], weight / total, 'REPLACE')
    modifier = obj.modifiers.new("Official storm_fp skin", 'ARMATURE')
    modifier.object = armature
    return obj


def make_empty(name, matrix, role, side, collection, size, display):
    obj = bpy.data.objects.new(name, None)
    collection.objects.link(obj)
    obj.empty_display_type = display
    obj.empty_display_size = size
    obj.show_name = True
    obj.show_in_front = True
    obj.matrix_world = matrix
    obj["halo4_vrik_role"] = role
    obj["halo4_vrik_side"] = side
    obj["halo4_needs_user_placement"] = True
    obj["halo4_vrik_seed_matrix"] = flatten_matrix(matrix)
    return obj


def pole_position(shoulder, elbow, wrist, side):
    line = wrist - shoulder
    if line.length_squared <= 1e-8:
        return elbow + Vector((0.0, 0.0, 0.35))
    closest = shoulder + line * ((elbow - shoulder).dot(line) / line.length_squared)
    bend = elbow - closest
    if bend.length <= 0.002:
        bend = line.cross(Vector((0.0, 0.0, 1.0)))
    if bend.length <= 0.002:
        bend = Vector((0.0, -1.0 if side == "right" else 1.0, 0.0))
    return elbow + bend.normalized() * 0.35


def calibrate_pole_angle(armature, pose_bone, constraint, expected_head):
    """Choose the mirrored-chain pole angle that preserves the tag bind pose."""
    best_angle = 0.0
    best_error = float("inf")

    def consider(angle):
        nonlocal best_angle, best_error
        constraint.pole_angle = angle
        bpy.context.view_layer.update()
        world_head = armature.matrix_world @ pose_bone.head
        error = (world_head - expected_head).length_squared
        if error < best_error:
            best_angle, best_error = angle, error

    # Blender's pole basis depends on each edit bone's roll. Search once while
    # building the scene, then store the solved constant in the .blend. This
    # avoids copying a right-arm angle onto the mirrored left chain.
    for sample in range(361):
        consider(-math.pi + sample * (2.0 * math.pi / 360.0))
    for radius, samples in ((math.radians(2.0), 81),
                            (math.radians(0.05), 101)):
        center = best_angle
        for sample in range(samples):
            consider(center - radius + sample * (2.0 * radius / (samples - 1)))
    constraint.pole_angle = best_angle
    bpy.context.view_layer.update()
    # Blender's iterative IK solver settles within a fraction of a millimetre
    # even when the target is exactly the bind-pose wrist.
    if math.sqrt(best_error) > 2.5e-4:
        raise ValueError(
            "neutral IK moves %s elbow by %.9g m" %
            (pose_bone.name, math.sqrt(best_error)))
    return best_angle


def add_controls(nodes, worlds, armature, collection):
    by_name = {node["name"]: node["index"] for node in nodes}
    result = {}
    chains = {
        "right": ("b_r_upperarm", "b_r_forearm", "b_r_hand"),
        "left": ("b_l_upperarm", "b_l_forearm", "b_l_hand"),
    }
    for side, (upper_name, fore_name, hand_name) in chains.items():
        upper = worlds[by_name[upper_name]]
        fore = worlds[by_name[fore_name]]
        hand = worlds[by_name[hand_name]]
        shoulder = make_empty(
            "vrik:%s_shoulder" % side, upper.copy(), "shoulder", side,
            collection, 0.045, 'CIRCLE')
        hand_seed = armature.matrix_world @ armature.pose.bones[hand_name].matrix
        hand_target = make_empty(
            "vrik:%s_hand" % side, hand_seed, "hand", side,
            collection, 0.075, 'ARROWS')
        pole_matrix = Matrix.Translation(pole_position(
            upper.translation, fore.translation, hand.translation, side))
        pole = make_empty(
            "vrik:%s_pole" % side, pole_matrix, "pole", side,
            collection, 0.055, 'SPHERE')
        result[side] = {"shoulder": shoulder, "hand": hand_target, "pole": pole}

        upper_bone = armature.pose.bones[upper_name]
        shoulder_constraint = upper_bone.constraints.new('COPY_LOCATION')
        shoulder_constraint.name = "Authored shoulder anchor"
        shoulder_constraint.target = shoulder
        shoulder_constraint.target_space = 'WORLD'
        shoulder_constraint.owner_space = 'WORLD'

        fore_bone = armature.pose.bones[fore_name]
        ik = fore_bone.constraints.new('IK')
        ik.name = "Authored two-bone arm IK"
        ik.target = hand_target
        ik.pole_target = pole
        ik.chain_count = 2
        ik.use_tail = True
        ik.use_rotation = False
        solved_angle = calibrate_pole_angle(
            armature, fore_bone, ik, fore.translation)
        pole["halo4_vrik_solved_pole_angle"] = solved_angle
        hand_bone = armature.pose.bones[hand_name]
        rotation = hand_bone.constraints.new('COPY_ROTATION')
        rotation.name = "Authored hand orientation"
        rotation.target = hand_target
        rotation.target_space = 'WORLD'
        rotation.owner_space = 'WORLD'
    return result


def validate_neutral_arm_pose(armature):
    bpy.context.view_layer.update()
    for name in ("b_r_upperarm", "b_r_forearm", "b_r_hand",
                 "b_l_upperarm", "b_l_forearm", "b_l_hand"):
        pose_head = armature.pose.bones[name].head
        rest_head = armature.data.bones[name].head_local
        delta = (pose_head - rest_head).length
        if delta > 2.5e-4:
            raise ValueError(
                "neutral IK moves %s by %.9g m" % (name, delta))


def add_reference(name, matrix, collection, display='PLAIN_AXES', size=0.08):
    obj = bpy.data.objects.new(name, None)
    collection.objects.link(obj)
    obj.empty_display_type = display
    obj.empty_display_size = size
    obj.show_name = True
    obj.show_in_front = True
    obj.matrix_world = matrix
    obj.hide_select = True
    obj.lock_location = (True, True, True)
    obj.lock_rotation = (True, True, True)
    obj.lock_scale = (True, True, True)
    obj["halo4_locked_reference"] = True
    return obj


def embed_text(path, name=None):
    text = bpy.data.texts.new(name or Path(path).name)
    text.write(Path(path).read_text(encoding="utf-8"))
    return text


def main():
    args = arguments()
    xml = args.xml.resolve()
    output = args.output.resolve()
    helper = args.helper.resolve()
    extractor = args.extractor.resolve()
    for path in (xml, helper, extractor):
        if not path.is_file():
            raise SystemExit("missing input: %s" % path)
    parsed = parse_render_model(xml)
    nodes = parsed["nodes"]
    worlds = world_matrices(nodes)

    reset_scene()
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1.0
    scene.render.engine = 'BLENDER_EEVEE'
    scene.world.color = (0.025, 0.03, 0.04)

    rig_collection = new_collection("01 storm_fp rig")
    model_collection = new_collection("02 official Chief arms")
    control_collection = new_collection("03 authored VRIK controls")
    reference_collection = new_collection("04 references")

    armature = make_armature(nodes, worlds, rig_collection)
    tech_material = make_material("storm_fp techsuit", (0.08, 0.10, 0.12), 0.1)
    armor_material = make_material("Chief armor", (0.15, 0.28, 0.12), 0.55)
    for index in sorted(parsed["meshes"]):
        make_mesh(index, parsed["meshes"][index], parsed["compression"],
                  nodes, armature,
                  model_collection, tech_material if index == 3 else armor_material)
    validate_alignment(verbose=False)
    add_controls(nodes, worlds, armature, control_collection)
    validate_neutral_arm_pose(armature)
    add_reference("ref:tag_origin", Matrix.Identity(4), reference_collection)
    camera_index = next(node["index"] for node in nodes
                        if node["name"] == "b_camera_control")
    add_reference("ref:camera_control", worlds[camera_index], reference_collection,
                  display='CUBE', size=0.045)

    evidence = {
        "schema": 1,
        "title": "Halo 4 official storm_fp VRIK authoring source",
        "source_render_model": str(xml),
        "source_render_model_sha256": digest(xml),
        "h4ek_build": "1.890.0.0",
        "world_units_to_metres": WU_TO_M,
        "axes": {"x": "forward", "y": "left", "z": "up"},
        "node_count": len(nodes),
        "mesh_indices": sorted(parsed["meshes"]),
        "position_compression": parsed["compression"],
        "chains": {
            "right": ["b_r_upperarm", "b_r_forearm", "b_r_hand"],
            "left": ["b_l_upperarm", "b_l_forearm", "b_l_hand"],
        },
    }
    evidence_text = bpy.data.texts.new("HALO4_VRIK_SOURCE_EVIDENCE.json")
    evidence_text.write(json.dumps(evidence, indent=2) + "\n")
    embed_text(helper)
    embed_text(extractor)
    readme = bpy.data.texts.new("README_HALO4_VRIK.txt")
    readme.write(README)

    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    shutil.copy2(helper, output.parent / helper.name)
    shutil.copy2(extractor, output.parent / extractor.name)
    (output.parent / "README.txt").write_text(README, encoding="utf-8")
    print("HALO4_VRIK_SCENE_READY")
    print("nodes=%d meshes=%d controls=6" % (len(nodes), len(parsed["meshes"])))
    print("output=%s" % output)


if __name__ == "__main__":
    main()
