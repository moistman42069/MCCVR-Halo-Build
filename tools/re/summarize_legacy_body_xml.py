"""Record official H3/ODST body geometry without confusing nodes with meshes.

Reads an existing export and source tag only. Keeps raw skin indices explicitly
unresolved when the exporter supplies no node map; does not create bindings.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def fields(element):
    result = {}
    for item in element.findall("field"):
        result.setdefault(item.get("name"), []).append(item.get("value"))
    return result


def summarize(xml, tag):
    raw = xml.read_bytes()
    # The official exporter emits this invalid XML attribute. Sanitize only
    # the parser input; retain the exact original bytes/hash on disk.
    replacement = b'value="<unavailable>"'
    root = ET.fromstring(raw.replace(replacement, b'value="&lt;unavailable&gt;"'))
    result = {
        "xml": str(xml), "xml_sha256": hashlib.sha256(raw).hexdigest(),
        "source_tag": str(tag), "source_sha256": hashlib.sha256(tag.read_bytes()).hexdigest(),
        "unavailable_attribute_repairs": raw.count(replacement),
        "tag": dict(root.attrib), "identity": fields(root),
        "nodes": [], "regions": [], "meshes": [], "raw_skin": [], "node_maps": [],
    }
    for block in root.findall("block"):
        name = block.get("name")
        if name == "nodes":
            result["nodes"] = [dict(e.attrib, fields=fields(e)) for e in block.findall("element")]
        elif name == "regions":
            for element in block.findall("element"):
                result["regions"].append(dict(element.attrib, permutations=[
                    dict(e.attrib, fields=fields(e))
                    for e in element.findall("block[@name='permutations']/element")]))
    for block in root.iter("block"):
        name = block.get("name")
        if name == "meshes":
            result["meshes"] = [dict(e.attrib, fields=fields(e)) for e in block.findall("element")]
        elif name == "per mesh node map":
            result["node_maps"] = [dict(e.attrib, fields=[fields(n) for n in e.iter("element")])
                                   for e in block.findall("element")]
        elif name == "per mesh temporary":
            for mesh in block.findall("element"):
                counts = Counter()
                vertices = mesh.findall("block[@name='raw vertices']/element")
                for vertex in vertices:
                    row = fields(vertex)
                    indices, weights = row.get("node index", []), row.get("node weight", [])
                    if len(indices) != len(weights):
                        raise ValueError("mismatched skin arrays")
                    for index, weight in zip(indices, weights):
                        if float(weight) > 0:
                            if int(index) < 0:
                                raise ValueError("positive skin weight has invalid node")
                            counts[int(index)] += 1
                result["raw_skin"].append(dict(mesh.attrib, vertices=len(vertices),
                                               weighted_raw_indices=dict(sorted(counts.items()))))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("xml", type=Path)
    parser.add_argument("tag", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = summarize(args.xml, args.tag)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"{len(report['nodes'])} nodes, {len(report['regions'])} regions, "
          f"{len(report['raw_skin'])} raw meshes; original files unchanged")
