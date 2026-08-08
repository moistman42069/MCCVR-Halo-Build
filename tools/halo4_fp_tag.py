#!/usr/bin/env python3
"""Stream the official H4EK storm_fp render-model XML.

The H4 export is hundreds of megabytes because it carries import-time raw
geometry.  This parser deliberately keeps only the 80-node first-person
skeleton and the requested mesh records, so Blender never has to construct a
DOM for the whole tag.
"""

from __future__ import annotations

import math
from pathlib import Path
import xml.etree.ElementTree as ET


def _values(element, name):
    return [
        child.get("value", "") for child in list(element)
        if child.tag == "field" and child.get("name") == name
    ]


def _value(element, name):
    values = _values(element, name)
    if not values:
        raise ValueError("%s lacks %s" % (element.get("name"), name))
    return values[0]


def _floats(text, count):
    result = tuple(float(part) for part in text.split(","))
    if len(result) != count or not all(math.isfinite(v) for v in result):
        raise ValueError("bad %d-vector %r" % (count, text))
    return result


def _block_index(text):
    return int(text.rsplit(",", 1)[-1])


def parse_render_model(path, mesh_indices=(3, 50, 97, 98)):
    """Return nodes and selected raw meshes from H4EK export-tag-to-xml."""
    path = Path(path)
    wanted = set(int(index) for index in mesh_indices)
    nodes = []
    meshes = {
        index: {"vertices": [], "indices": [], "node_map": []}
        for index in wanted
    }
    blocks = []
    elements = []
    current_mesh = None
    current_node_map = None
    completed_meshes = set()
    completed_node_maps = set()
    compression = None

    for event, element in ET.iterparse(path, events=("start", "end")):
        if event == "start":
            if element.tag == "block":
                blocks.append(element.get("name", ""))
            elif element.tag == "element":
                parent = blocks[-1] if blocks else ""
                index = int(element.get("index", "-1"))
                elements.append((parent, index))
                if parent == "per mesh temporary":
                    current_mesh = index
                elif parent == "per mesh node map":
                    current_node_map = index
            continue

        if element.tag == "element":
            parent, index = elements.pop()
            if parent == "nodes" and len(nodes) < 80:
                nodes.append({
                    "index": index,
                    "name": _value(element, "name"),
                    "parent": _block_index(_value(element, "parent node")),
                    "translation": _floats(
                        _value(element, "default translation"), 3),
                    "rotation": _floats(
                        _value(element, "default rotation"), 4),
                    "scale": float(
                        _values(element, "default scale")[0]
                        if _values(element, "default scale") else 1.0),
                })
            elif parent == "compression info" and index == 0:
                flags = int(_value(element, "compression flags"))
                first = _floats(_value(element, "position bounds 0"), 3)
                second = _floats(_value(element, "position bounds 1"), 3)
                # H4's field is real_bounds position_bounds[3]. The XML tag
                # dumper exposes its six sequential floats as two point-3d
                # fields; they are not opposing XYZ corner vectors.
                packed = first + second
                bounds = tuple((packed[axis * 2], packed[axis * 2 + 1])
                               for axis in range(3))
                compression = {
                    "flags": flags,
                    "position_min": tuple(pair[0] for pair in bounds),
                    "position_max": tuple(pair[1] for pair in bounds),
                }
            elif current_mesh in wanted and parent == "raw vertices":
                local_nodes = [int(value) for value in _values(
                    element, "node index")]
                weights = [float(value) for value in _values(
                    element, "node weight")]
                if len(local_nodes) != 4 or len(weights) != 4:
                    raise ValueError("mesh %d vertex %d has bad skin data" %
                                     (current_mesh, index))
                meshes[current_mesh]["vertices"].append({
                    "position": _floats(_value(element, "position"), 3),
                    "normal": _floats(_value(element, "normal"), 3),
                    "nodes": local_nodes,
                    "weights": weights,
                })
            elif current_mesh in wanted and parent == "raw indices":
                raw = int(_value(element, "word"))
                meshes[current_mesh]["indices"].append(
                    raw + 0x10000 if raw < 0 else raw)
            elif current_node_map in wanted and parent == "node map":
                meshes[current_node_map]["node_map"].append(
                    int(_value(element, "node index")))

            if parent == "per mesh temporary":
                if current_mesh in wanted:
                    completed_meshes.add(current_mesh)
                current_mesh = None
            elif parent == "per mesh node map":
                if current_node_map in wanted:
                    completed_node_maps.add(current_node_map)
                current_node_map = None
            element.clear()
            if (len(nodes) == 80 and
                    completed_meshes == wanted and
                    completed_node_maps == wanted):
                # Everything requested precedes later editor-only fields. Some
                # H4EK exports contain raw control bytes in those trailing
                # fields, so do not ask a standards XML parser to consume data
                # that is irrelevant to this authored kit.
                break
        elif element.tag == "block":
            if not blocks or blocks[-1] != element.get("name", ""):
                raise ValueError("malformed block stack at %s" % path)
            blocks.pop()
            element.clear()

    nodes.sort(key=lambda node: node["index"])
    if len(nodes) != 80 or [node["index"] for node in nodes] != list(range(80)):
        raise ValueError("expected the official contiguous 80-node storm_fp body")
    if len({node["name"] for node in nodes}) != 80:
        raise ValueError("storm_fp has duplicate node names")
    for node in nodes:
        if node["parent"] >= node["index"] or node["parent"] < -1:
            raise ValueError("node hierarchy is not parent-before-child")

    for index, mesh in meshes.items():
        if not mesh["vertices"] or not mesh["indices"] or not mesh["node_map"]:
            raise ValueError("official mesh %d is incomplete" % index)
        if len(mesh["indices"]) % 3:
            raise ValueError("mesh %d is not a triangle list" % index)
        if max(mesh["indices"]) >= len(mesh["vertices"]):
            raise ValueError("mesh %d has an out-of-range index" % index)
        for vertex in mesh["vertices"]:
            for local, weight in zip(vertex["nodes"], vertex["weights"]):
                if weight <= 0.0:
                    continue
                if local < 0 or local >= len(mesh["node_map"]):
                    raise ValueError(
                        "mesh %d has local bone %d with weight %.9g but "
                        "node-map count %d" %
                        (index, local, weight, len(mesh["node_map"])))
    if compression is None or not (compression["flags"] & 1):
        raise ValueError("storm_fp lacks expected compressed-position evidence")
    if any(high <= low for low, high in zip(
            compression["position_min"], compression["position_max"])):
        raise ValueError("storm_fp has invalid position compression bounds")
    return {"nodes": nodes, "meshes": meshes, "compression": compression}
