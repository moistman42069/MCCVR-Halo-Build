"""Read-only pinned-retail signature and call-edge check for Halo 3 haptics."""
from pathlib import Path
import hashlib
import re
import struct
import sys

root = Path(__file__).resolve().parents[1]
raw = (root / "out/deps/re-tools/inputs/halo3.dll").read_bytes()
assert hashlib.sha256(raw).hexdigest().upper() == "B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63"
sys.path.insert(0, str(root / "tools/re"))
from pe import PE
pe = PE(str(root / "out/deps/re-tools/inputs/halo3.dll"))
text_section, text_bytes = pe.text()
source = (root / "src/common/halo3_haptic_contract.h").read_text()
symbols = {name: int(value, 16) for name, value in re.findall(r"(\w+)\s*=\s*(0x[0-9A-F]+)", source)}

def address(value: str) -> int:
    return int(value, 16) if value.startswith("0x") else symbols[value]

checks = 1
for name, rva, pattern in re.findall(r'\{"([^\"]+)",\s*(\w+),\s*"([^\"]+)"', source):
    at = address(rva)
    needle = bytes.fromhex(pattern)
    hits = [text_section["va"] + match.start() for match in re.finditer(re.escape(needle), text_bytes)]
    assert hits == [at], name
    checks += 1
for call, size, displacement, target in re.findall(r"\{(\w+),\s*(\d+),\s*(\d+),\s*(\w+)\}", source):
    at = address(call)
    raw_at = pe.off(at)
    assert raw[raw_at] == 0xE8
    assert at + int(size) + struct.unpack_from("<i", raw, raw_at + int(displacement))[0] == address(target), call
    checks += 1
for at_text, pattern in re.findall(r'\{(0x[0-9A-F]+),\s*"([^\"]+)"\}', source):
    at = int(at_text, 16)
    needle = bytes.fromhex(pattern)
    hits = [text_section["va"] + match.start() for match in re.finditer(re.escape(needle), text_bytes)]
    assert hits == [at], hex(at)
    checks += 1
print(f"Halo 3 authored haptics: {checks} pinned retail checks pass; headset acceptance pending")
