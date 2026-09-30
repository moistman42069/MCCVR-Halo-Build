"""Read official H2EK world render-model exports; never modify game assets."""
import argparse
import hashlib
import json
from pathlib import Path

from reload_tag_xml import read, block


def fields(element):
    result = {}
    for child in element:
        if child.tag == "field":
            result[child.get("name")] = child.get("value") or (child.text or "").strip()
        elif child.tag == "block_index":
            # H2EK uses the type attribute for the semantic field name.
            result[child.get("type")] = int(child.get("index"))
    return result


def summarize(directory, name):
    xml = directory / (name + "-world.xml")
    tag = directory / "tags/objects/characters" / name / (name + ".render_model")
    root = read(xml)
    nodes = [fields(n) for n in block(root, "nodes")]
    expected = 33 if name == "masterchief" else 55
    if len(nodes) != expected:
        raise ValueError(f"unexpected own-kit node count: {name}: {len(nodes)}")
    for index, node in enumerate(nodes):
        parent = node["parent node"]
        if parent < -1 or parent >= index:
            raise ValueError(f"non-topological parent at {name}:{index}")
    names = {node["name"]: i for i, node in enumerate(nodes)}
    arms = []
    fingers = []
    for side in ("l", "r"):
        chain = [names[f"{side}_{part}"] for part in ("upperarm", "forearm", "hand")]
        if any(nodes[chain[i]]["parent node"] != chain[i - 1] for i in (1, 2)):
            raise ValueError(f"unexpected own-kit arm chain: {name}:{side}")
        arms.append(chain)
        digits = []
        for digit in ("index", "middle", "pinky", "ring", "thumb"):
            entries = [names[f"{side}_{digit}{n}"] for n in (1, 2, 3)
                       if f"{side}_{digit}{n}" in names]
            if entries:
                if nodes[entries[0]]["parent node"] != chain[-1] or any(
                    nodes[entries[i]]["parent node"] != entries[i - 1]
                    for i in range(1, len(entries))
                ):
                    raise ValueError(f"unexpected own-kit digit chain: {name}:{side}:{digit}")
                digits.append({"digit": digit, "nodes": entries})
        fingers.append(digits)
    regions = [dict(fields(n), permutations=[fields(x) for x in block(n, "permutations")])
               for n in block(root, "regions")]
    markers = [dict(fields(n), markers=[fields(x) for x in block(n, "markers")])
               for n in block(root, "marker groups")]
    report = dict(model=name, tag_sha256=hashlib.sha256(tag.read_bytes()).hexdigest(),
                  xml_sha256=hashlib.sha256(xml.read_bytes()).hexdigest(), fields=fields(root),
                  nodes=nodes, regions=regions, markers=markers, arms=arms, fingers=fingers,
                  inverse_bind_status="unverified: exported inverse fields are not an orthonormal transform")
    (directory / (name + "-world-summary.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"{name}: {len(nodes)} nodes, arms {arms}, {len(regions)} regions, "
          f"{[len(digits) for digits in fingers]} digit chains per hand")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    for model in ("masterchief", "dervish", "elite"):
        summarize(args.directory, model)
