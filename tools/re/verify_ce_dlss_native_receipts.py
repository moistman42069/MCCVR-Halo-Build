"""Read-only pinned CE depth/projection witnesses for the optional DLSS adapter.

This verifies original PE bytes, not live resource ownership or GPU correctness.
The production adapter must still admit the exact same-eye camera transaction,
the selected depth owner and its immutable D3D resource descriptor.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "out/pydeps"))
import pefile

SHA = "0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C"


def main():
    data = (ROOT / "out/deps/re-tools/inputs/halo1.dll").read_bytes()
    assert hashlib.sha256(data).hexdigest().upper() == SHA
    pe = pefile.PE(data=data, fast_load=True)
    image = pe.get_memory_mapped_image()
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    code = image[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize]
    entries = [
        ("anniversary_camera_constant_dispatch", 0x2351E0, 24),
        ("anniversary_camera_constant_writer", 0x235860, 39),
    ]
    witnesses = [
        ("constant_block_to_shader_matrix", 0x235883, 23),
        ("constant_block_to_shader_origin", 0x2358F1, 26),
        ("dispatch_constants_virtual", 0x23573C, 50),
        ("classic_primary_setup_call", 0xBBD16D, 10),
    ]
    rows = []
    for name, rva, length in entries + witnesses:
        pattern = bytes(image[rva:rva+length])
        assert len(pattern) == length and code.count(pattern) == 1, name
        rows.append({"name": name, "rva": hex(rva),
                     "pattern": pattern.hex(" ").upper(), "matches": 1})
    pointers = [(0x1815378 + 0x48, 0x2351E0), (0x1815378 + 0x110, 0x235860)]
    for rva, target in pointers:
        assert struct.unpack_from("<Q", image, rva)[0] == pe.OPTIONAL_HEADER.ImageBase + target
    assert 0xBBD177 + struct.unpack_from("<i", image, 0xBBD173)[0] == 0xAE06FC
    print(json.dumps({"result": "PASS_OFFLINE_ONLY", "sha256": SHA,
        "patterns": rows,
        "vtable_pointers": [{"rva": hex(r), "target_rva": hex(t)} for r,t in pointers]}, indent=2))


if __name__ == "__main__":
    main()
