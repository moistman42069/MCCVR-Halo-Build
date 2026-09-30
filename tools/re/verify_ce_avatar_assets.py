"""Inspect the official CE world-body rig without treating FP hands as a body.

Uses the existing own-kit descriptor checks and tag parser. Outputs evidence
only; it neither modifies game assets nor establishes a retail render binding.
"""
import hashlib
import json
from pathlib import Path

from verify_ce_floating_hand_mesh import inspect_official_kit, read_mesh
from verify_ce_weapon_mesh import inspect_extra_descriptors, read_weapon

ROOT = Path(__file__).resolve().parents[2]


def inspect():
    inspect_official_kit(ROOT / "out/deps/re-tools/inputs/halo_tag_test.exe")
    inspect_extra_descriptors()
    world = ROOT / "out/ce-hand-angle-audit/tags/characters/cyborg/cyborg.gbxmodel"
    data, names, parents, rest, vertices, parts = read_weapon(world)
    digest = hashlib.sha256(data).hexdigest().upper()
    assert digest == "92842DA6B94E0A4E172209EE0D858640FD6B22147AB805A94A18056838735BF7"
    assert len(names) == 19 and parts == 17 and len(vertices) == 6025
    chains = {}
    for side in ("l", "r"):
        chain = [names.index(f"bip01 {side} {name}")
                 for name in ("upperarm", "forearm", "hand")]
        assert parents[chain[1]] == chain[0] and parents[chain[2]] == chain[1]
        assert not any(parent == chain[2] for parent in parents), "world hand has descendants"
        chains[side] = chain
    first_person = ROOT / "out/ce-hands-hceek-tags/tags/characters/cyborg/fp/fp.gbxmodel"
    fp_data, fp_names, fp_parents, _, _, _ = read_mesh(first_person)
    finger_roots = {}
    for side in ("l", "r"):
        wrist = fp_names.index(f"frame {side} wriste")
        finger_roots[side] = [fp_names[i] for i, parent in enumerate(fp_parents) if parent == wrist]
        assert len(finger_roots[side]) == 5
    return {
        "source": "Official HCEEK tags and executable descriptors",
        "world_model": str(world.relative_to(ROOT)),
        "sha256": digest,
        "nodes": [{"index": i, "name": name, "parent": parents[i], "rest": rest[i],
                   "weighted_vertices": sum((v[1] == i and v[3] > 0) or
                                            (v[2] == i and v[4] > 0) for v in vertices)}
                  for i, name in enumerate(names)],
        "arm_chains": chains,
        "parts": parts,
        "vertices_across_lods": len(vertices),
        "world_has_finger_joints": False,
        "first_person_sha256": hashlib.sha256(fp_data).hexdigest().upper(),
        "first_person_finger_roots": finger_roots,
        "limit": "Asset proof only. World visibility, private palette, geometry masking and Anniversary bridge are unbound."
    }


if __name__ == "__main__":
    result = inspect()
    output = ROOT / "out/ce-avatar-model-20260930.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"CE avatar asset proof: 19 world nodes, 17 parts, 6025 vertices; no world finger joints. {output}")
