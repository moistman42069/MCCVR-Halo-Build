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
    values = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = parser.parse_args(values)

    armature = bpy.data.objects.get("storm_fp_skeleton")
    controls = [obj for obj in bpy.data.objects if obj.name.startswith("vrik:")]
    meshes = [obj for obj in bpy.data.objects if obj.type == 'MESH']
    if armature is None or len(armature.data.bones) != 80:
        raise SystemExit("ERROR: storm_fp 80-node armature missing")
    if len(controls) != 6 or len(meshes) != 4:
        raise SystemExit("ERROR: expected six controls and four meshes")

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
