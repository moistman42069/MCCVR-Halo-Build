"""Read-only pinned verification of CE's optional native world-melee bridge."""
import hashlib
from pathlib import Path
import re
import struct
import pefile

root = Path(__file__).resolve().parents[1]
raw = (root / 'out/deps/re-tools/inputs/halo1.dll').read_bytes()
assert hashlib.sha256(raw).hexdigest().upper() == '0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C'
pe = pefile.PE(data=raw)
image = pe.get_memory_mapped_image()
source = (root / 'src/common/haloce_world_melee_contract.h').read_text()
names = {'fan': 0xB0BFAC, 'damage': 0xA9C174, 'bsp': 0x1B860A4}
checks = 1
for name, key, pattern in re.findall(r'\{"([^"]+)",(fan|damage),"([^"]+)",true\}', source):
    rva = names[key]
    expected = bytes.fromhex(pattern)
    assert [m.start() for m in re.finditer(re.escape(expected), image)] == [rva], name
    assert any(row.struct.BeginAddress == rva for row in pe.DIRECTORY_ENTRY_EXCEPTION), name
    checks += 2
for address, pattern in re.findall(r'\{0x([0-9A-F]+),"([^"]+)"\}', source):
    rva, expected = int(address, 16), bytes.fromhex(pattern)
    assert image[rva:rva+len(expected)] == expected, address
    checks += 1
for address, length, offset, key in re.findall(r'\{0x([0-9A-F]+),(\d+),(\d+),(fan|damage|bsp)\}', source):
    rva = int(address, 16)
    assert rva+int(length)+struct.unpack_from('<i', image, rva+int(offset))[0] == names[key]
    checks += 1
assert checks == 10
print(f'CE world melee: {checks} pinned entry/unwind/operand/call checks passed; runtime acceptance pending')
