#!/usr/bin/env python3
"""Render a deterministic validation preview of the saved Halo 4 VRIK kit."""

import argparse
from pathlib import Path
import sys

import bpy
from mathutils import Vector


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--show-bones", action="store_true")
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = parser.parse_args(values)

    armature = bpy.data.objects.get("storm_fp_skeleton")
    controls = [obj for obj in bpy.data.objects if obj.name.startswith("vrik:")]
    meshes = [obj for obj in bpy.data.objects if obj.type == 'MESH']
    if armature is None or len(armature.data.bones) != 80:
        raise SystemExit("ERROR: storm_fp 80-node armature missing")
    if len(controls) != 6 or len(meshes) != 4:
        raise SystemExit("ERROR: expected six controls and four meshes")
    for name in ("b_r_upperarm", "b_r_forearm", "b_r_hand",
                 "b_l_upperarm", "b_l_forearm", "b_l_hand"):
        pose_head = armature.pose.bones[name].head
        rest_head = armature.data.bones[name].head_local
        delta = (pose_head - rest_head).length
        if delta > 2.5e-4:
            raise SystemExit(
                "ERROR: neutral IK moves %s by %.9g m" % (name, delta))

    if args.show_bones:
        material = bpy.data.materials.new("alignment bones")
        material.diffuse_color = (1.0, 0.12, 0.02, 1.0)
        names = [
            bone.name for bone in armature.data.bones
            if bone.name.startswith(("b_r_", "b_l_"))]
        for name in names:
            bone = armature.data.bones[name]
            curve = bpy.data.curves.new("alignment:" + name, 'CURVE')
            curve.dimensions = '3D'
            curve.bevel_depth = 0.006
            curve.bevel_resolution = 2
            curve.materials.append(material)
            spline = curve.splines.new('POLY')
            spline.points.add(1)
            head = armature.matrix_world @ bone.head_local
            tail = armature.matrix_world @ bone.tail_local
            spline.points[0].co = (*head, 1.0)
            spline.points[1].co = (*tail, 1.0)
            obj = bpy.data.objects.new("alignment:" + name, curve)
            bpy.context.scene.collection.objects.link(obj)

    camera_data = bpy.data.cameras.new("validation_camera")
    camera = bpy.data.objects.new("validation_camera", camera_data)
    bpy.context.scene.collection.objects.link(camera)
    camera.location = Vector((2.2, -2.2, 1.45))
    target = Vector((0.10, 0.0, -0.23))
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.lens = 58.0
    bpy.context.scene.camera = camera

    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = 'WORLD'
    scene.display.shading.color_type = 'MATERIAL'
    scene.render.resolution_x = 1100
    scene.render.resolution_y = 700
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(output)
    bpy.ops.render.render(write_still=True)
    print("HALO4_VRIK_PREVIEW_READY")
    print("output=%s" % output)


if __name__ == "__main__":
    main()
