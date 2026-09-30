"""Stream an official kit XML export into bounded rig/skin evidence.

No tag modification, game access, or runtime bindings. Raw geometry is omitted;
weighted vertex masks retain which nodes must stay together when hiding a part.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def summarize(path, checkpoint=None):
    result = {"tag": {}, "identity": {}, "nodes": [], "permutations": [], "meshes": {}}
    stack = []
    expected_meshes = None
    expected_temporary = None
    completed_temporary = set()
    completed_maps = set()

    def ancestor_element(block_name):
        for index in range(len(stack) - 2, 0, -1):
            item = stack[index]
            if item.tag == "element" and stack[index - 1].tag == "block" and stack[index - 1].get("name") == block_name:
                return item
        return None

    def mesh(index):
        return result["meshes"].setdefault(index, {"vertices": 0, "weighted_local_masks": Counter(), "node_map": [], "fields": {}})

    for event, element in ET.iterparse(path, events=("start", "end")):
        if event == "start":
            stack.append(element)
            if len(stack) == 1:
                result["tag"] = dict(element.attrib)
            if element.tag == "block" and element.get("name") == "per mesh temporary":
                expected_temporary = int(element.get("count"))
            if element.tag == "block" and element.get("name") == "meshes":
                expected_meshes = int(element.get("count"))
            continue
        parent = stack[-2] if len(stack) > 1 else None
        if element.tag == "field" and len(stack) == 2:
            result["identity"][element.get("name")] = element.get("value")
        elif element.tag == "element":
            block = parent.get("name", "") if parent is not None else ""
            fields = {}
            for child in element:
                if child.tag == "field":
                    fields.setdefault(child.get("name"), []).append(child.get("value", ""))
            index = int(element.get("index", "-1"))
            if block == "nodes" and len(stack) == 3:
                result["nodes"].append({"index": index, "name": element.get("name"), "fields": fields})
            elif block == "permutations":
                region = ancestor_element("regions")
                result["permutations"].append({"region": dict(region.attrib) if region is not None else {},
                    "index": index, "name": element.get("name"), "fields": fields})
            elif block == "raw vertices":
                owner = ancestor_element("per mesh temporary")
                if owner is not None:
                    row = mesh(int(owner.get("index")))
                    indices = [int(value) for value in fields.get("node index", [])]
                    weights = [float(value) for value in fields.get("node weight", [])]
                    if len(indices) != len(weights):
                        raise ValueError("mismatched skin indices/weights")
                    mask = 0
                    for node, weight in zip(indices, weights):
                        if weight > 0:
                            if node < 0 or node >= 256:
                                raise ValueError("weighted local node out of bounds")
                            mask |= 1 << node
                    row["vertices"] += 1
                    row["weighted_local_masks"][hex(mask)] += 1
            elif block == "node map":
                owner = ancestor_element("per mesh node map")
                if owner is not None:
                    mesh(int(owner.get("index")))["node_map"].extend(int(v) for v in fields.get("node index", []))
            elif block == "meshes":
                mesh(index)["fields"] = fields
            elif block == "per mesh temporary":
                completed_temporary.add(index)
            elif block == "per mesh node map":
                completed_maps.add(index)
            # Drop completed records immediately; even empty raw-vertex XML
            # elements would otherwise accumulate across a gigabyte export.
            element.clear()
            if parent is not None:
                parent.remove(element)
        elif element.tag == "block":
            if element.get("name") == "nodes" and len(stack) == 2 and checkpoint:
                checkpoint.write_text(json.dumps(result, indent=2), encoding="utf-8")
            # All requested rig/skin information precedes H4EK's malformed
            # api resource value="<unavailable>" and unrelated editor data.
            if element.get("name") == "per mesh node map":
                if (expected_meshes is None or expected_temporary is None or
                    len(result["meshes"]) != expected_meshes or
                    completed_maps != set(range(expected_meshes)) or
                    completed_temporary != set(range(expected_temporary))):
                    raise ValueError(f"incomplete mesh table: {len(result['meshes'])}/{expected_meshes}, "
                        f"temporary {len(completed_temporary)}/{expected_temporary}, maps {len(completed_maps)}")
                result["raw_geometry_mesh_count"] = expected_temporary
                result["mesh_blocks_complete"] = True
                break
            element.clear()
            if parent is not None:
                parent.remove(element)
        stack.pop()
    for row in result["meshes"].values():
        global_masks = Counter()
        for text, count in row["weighted_local_masks"].items():
            mask = int(text, 16)
            mapped = 0
            for local in range(mask.bit_length()):
                if mask & (1 << local):
                    if local >= len(row["node_map"]):
                        raise ValueError("weighted vertex exceeds its official node map")
                    node = row["node_map"][local]
                    if node < 0 or node >= len(result["nodes"]):
                        raise ValueError("node map exceeds official skeleton")
                    mapped |= 1 << node
            global_masks[hex(mapped)] += count
        row["weighted_global_masks"] = dict(global_masks)
        row["weighted_local_masks"] = dict(row["weighted_local_masks"])
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("xml", type=Path)
    parser.add_argument("source_tag", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = summarize(args.xml, args.output.with_suffix(".rig.json"))
    result["provenance"] = {"xml": str(args.xml), "source_tag": str(args.source_tag),
        "source_sha256": hashlib.sha256(args.source_tag.read_bytes()).hexdigest().upper(),
        "export_command": "tool.exe export-tag-to-xml <absolute source_tag> <absolute xml> (from official kit root)"}
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"Retained {len(result['nodes'])} nodes, {len(result['permutations'])} permutations and {len(result['meshes'])} mesh summaries")
