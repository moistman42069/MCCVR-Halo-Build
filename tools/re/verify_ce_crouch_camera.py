"""Verify CE's own native crouch interpolation and camera-height contract offline."""
from pathlib import Path
import hashlib
import re
import struct
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "out/pydeps"))
import pefile
raw = (ROOT / "out/deps/re-tools/inputs/halo1.dll").read_bytes()
assert hashlib.sha256(raw).hexdigest().upper() == "0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C"
image = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
source = (ROOT / "src/common/haloce_crouch_contract.h").read_text()
patterns = re.findall(r'0x([0-9A-F]+),"([0-9A-F ]+)"', source)
assert len(patterns) == 7
for rva, pattern in patterns:
    code = bytes.fromhex(pattern)
    assert image.count(code) == 1
    assert image.find(code) == int(rva, 16)
for rva, length, offset, target in ((0xBAFD76,7,3,0x1C34ED2), (0xBAFDD3,7,3,0x2E9FD68), (0xBAFDDF,8,4,0x194F150)):
    assert rva + length + struct.unpack_from("<i", image, rva + offset)[0] == target
assert abs(struct.unpack_from("<f", image, 0x194F150)[0] - 1/30) < 1e-8
print("PASS: 7 CE crouch witnesses, 3 exact native globals; interpolated fraction and partial-tick camera height")
