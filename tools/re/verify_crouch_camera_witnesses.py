"""Offline own-kit-to-retail crouch camera layout witnesses; never opens MCC."""
from pathlib import Path
import hashlib
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "out/pydeps"))
import pefile

PINS = {
    "Halo2": ("halo2.dll", "DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946"),
    "Halo3ODST": ("halo3odst.dll", "5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A"),
}
source = (ROOT / "src/common/physical_crouch_additional_witnesses.h").read_text()
count = 0
for title, (name, digest) in PINS.items():
    raw = (ROOT / "out/deps/re-tools/inputs" / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == digest
    image = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
    for rva, pattern in re.findall(r'\{GameTitle::' + title + r',0x([0-9A-F]+),"([0-9A-F ]+)"\}', source):
        code = bytes.fromhex(pattern)
        assert image.count(code) == 1
        assert image.find(code) == int(rva, 16)
        count += 1
assert count == 10, count
print(f"PASS: {count} unique ODST/H2 native crouch witnesses; H2 retail tag heights +218/+21C")
