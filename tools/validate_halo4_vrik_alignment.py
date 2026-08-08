#!/usr/bin/env python3
"""Report storm_fp rest-bone alignment against strongly weighted vertices."""

import math

import bpy
from mathutils import Vector


NAMES = (
    "b_r_upperarm", "b_r_forearm", "b_r_hand",
    "b_l_upperarm", "b_l_forearm", "b_l_hand",
    "b_r_index1", "b_r_index2", "b_r_index3",
    "b_l_index1", "b_l_index2", "b_l_index3",
)


def point_segment_distance(point, start, end):
    edge = end - start
    if edge.length_squared <= 1e-12:
        return (point - start).length
    amount = max(0.0, min(1.0, (point - start).dot(edge) / edge.length_squared))
    return (point - (start + edge * amount)).length


def validate_alignment(verbose=True):
    armature = bpy.data.objects["storm_fp_skeleton"]
    meshes = [obj for obj in bpy.data.objects if obj.type == 'MESH']
    records = {}
    problems = []
    for name in NAMES:
        points = []
        for obj in meshes:
            group = obj.vertex_groups.get(name)
            if group is None:
                continue
            for vertex in obj.data.vertices:
                weights = [item.weight for item in vertex.groups
                           if item.group == group.index]
                if weights and weights[0] >= 0.25:
                    points.append(obj.matrix_world @ vertex.co)
        bone = armature.data.bones.get(name)
        if bone is None or not points:
            problems.append(name + " has no strongly weighted vertices")
            continue
        head = armature.matrix_world @ bone.head_local
        tail = armature.matrix_world @ bone.tail_local
        centroid = sum(points, Vector()) / len(points)
        distances = [point_segment_distance(point, head, tail) for point in points]
        rms = math.sqrt(sum(value * value for value in distances) / len(distances))
        centroid_distance = point_segment_distance(centroid, head, tail)
        records[name] = {
            "centroid": centroid,
            "centroid_distance": centroid_distance,
            "rms": rms,
        }
        if verbose:
            print("ALIGN", name,
                  "count", len(points),
                  "head", tuple(round(value, 5) for value in head),
                  "centroid", tuple(round(value, 5) for value in centroid),
                  "centroid_to_bone_m", round(centroid_distance, 6),
                  "weighted_rms_m", round(rms, 6))
        if centroid_distance > 0.06:
            problems.append("%s weighted centroid is %.6g m from bone" %
                            (name, centroid_distance))
        if rms > 0.08:
            problems.append("%s weighted RMS is %.6g m" % (name, rms))

    for right_name in NAMES:
        if not right_name.startswith("b_r_"):
            continue
        left_name = "b_l_" + right_name[4:]
        if right_name not in records or left_name not in records:
            continue
        right = records[right_name]["centroid"]
        left = records[left_name]["centroid"]
        mirror_error = Vector((right.x - left.x, right.y + left.y,
                               right.z - left.z)).length
        if mirror_error > 0.02:
            problems.append("%s/%s mirror error is %.6g m" %
                            (right_name, left_name, mirror_error))
    if problems:
        raise ValueError("; ".join(problems))
    return records


def main():
    records = validate_alignment(verbose=True)
    print("HALO4_VRIK_ALIGNMENT_READY bones=%d" % len(records))


if __name__ == "__main__":
    main()
