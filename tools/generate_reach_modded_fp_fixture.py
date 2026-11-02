"""Extract production layout equality and held-model classification for tests."""
import argparse
import json
import re
from pathlib import Path
from generate_haptics_runtime_fixture import extract_function

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-root", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
output = ["// Extracted production functions; do not edit this generated file."]
for filename, signature in [
    ("game.cpp", "bool ReachLayoutsEqual("),
    ("legacy_runtime_weapon_bounds.inl", "bool LegacyClassifyRuntimeWeapon("),
]:
    path = args.source_root.resolve() / "src/dll" / filename
    source = path.read_text(encoding="utf-8-sig")
    matches = list(re.finditer(re.escape(signature) + r"[^;{]+\)", source))
    if len(matches) != 1:
        raise ValueError(f"Expected one complete signature for {signature}")
    body, line = extract_function(source, matches[0].group())
    output.extend([f'#line {line} "{path.as_posix()}"', body])
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n\n".join(output) + "\n", encoding="utf-8")

# Asset-derived expected identities are independent of production masks.
evidence = json.loads((args.source_root / "docs/REACH-MODDED-WEAPONS-M20-EVIDENCE.json").read_text())
assets = ["// Extracted from the hash-pinned user cache; no runtime dependency.",
    "struct AssetCase { int tag, count, weaponCount; std::array<int32_t,41> body; "
    "std::array<int32_t,120> weapon, parents; int left, right, camera; };",
    "static constexpr AssetCase assets[]{"]
for item in evidence["weapons"]:
    nodes = {node["name"]: node["index"] for node in item["graph_nodes"]}
    arrays = [item["body_to_source"], item["weapon_to_source"],
              [node["parent"] for node in item["graph_nodes"]]]
    text_arrays = ["{" + ",".join(map(str, array)) + "}" for array in arrays]
    assets.append("{" + ",".join(map(str, [item["weapon_tag"], item["live_source_count"],
        len(item["weapon_to_source"])])) + "," + ",".join(text_arrays) + "," +
        ",".join(map(str, [nodes["l_hand"], nodes["r_hand"], nodes["camera_control"]])) + "},")
assets.append("};")
args.output.with_name("reach_modded_fp_assets.inl").write_text("\n".join(assets) + "\n", encoding="utf-8")
