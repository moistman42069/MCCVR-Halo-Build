"""Read-only verification of CE vehicle model-node identity retail bindings.

HCEEK render_objects.c -> object model -> mod2 node table establishes meaning;
this verifies its matched retail consumers, not headset camera behavior.
"""
import hashlib
from pathlib import Path
import re
import struct
import pefile

ROOT = Path(__file__).resolve().parents[1]
raw = (ROOT / 'out/deps/re-tools/inputs/halo1.dll').read_bytes()
assert hashlib.sha256(raw).hexdigest().upper() == '0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C'
pe = pefile.PE(data=raw)
image = pe.get_memory_mapped_image()
source = (ROOT / 'src/common/haloce_vehicle_identity_contract.h').read_text()
checks = 1
for name, address, pattern, unwind in re.findall(r'\{"([^"]+)",0x([0-9A-F]+),"([^"]+)",(true|false)\}', source):
    rva = int(address, 16)
    expected = bytes.fromhex(pattern)
    hits = [match.start() for match in re.finditer(re.escape(expected), image)]
    assert hits == [rva], (name, hits)
    checks += 1
    if unwind == 'true':
        assert any(row.struct.BeginAddress == rva for row in pe.DIRECTORY_ENTRY_EXCEPTION), name
        checks += 1
for address, pattern in re.findall(r'\{0x([0-9A-F]+),"([^"]+)"\}', source):
    rva, expected = int(address, 16), bytes.fromhex(pattern)
    assert image[rva:rva + len(expected)] == expected, address
    checks += 1
for address, length, displacement, target in re.findall(r'\{0x([0-9A-F]+),(\d+),(\d+),0x([0-9A-F]+)\}', source):
    rva = int(address, 16)
    assert rva + int(length) + struct.unpack_from('<i', image, rva + int(displacement))[0] == int(target, 16)
    checks += 1
for site in (0xAF5ECE, 0xAF5EDE, 0xB2A1CB, 0xB2A1D6):
    assert image[site] == 0xE8 and site + 5 + struct.unpack_from('<i', image, site + 1)[0] == 0xA9B648
    checks += 1
assert 0xA9B64F + struct.unpack_from('<i', image, 0xA9B64B)[0] == 0x1C34FB0
checks += 1
print(f'CE vehicle identity: {checks} pinned retail checks passed; native runtime acceptance pending')
