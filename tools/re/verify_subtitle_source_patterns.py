"""Read-only validation of the exact production caption source table.

Native semantics are documented in RENDER-CONTRIBUTIONS-2026-09-23.md and
NATIVE-SUBTITLE-H2-H4-2026-09-23.md. This verifies current bindings, not gameplay.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'out/pydeps'))
import pefile

MODULES = {
    'Halo3': ('halo3.dll', 'B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63'),
    'Halo3ODST': ('halo3odst.dll', '5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A'),
    'Halo2': ('halo2.dll', 'DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946'),
    'Halo4': ('halo4.dll', '7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84'),
}
source = (ROOT / 'src/dll/native_subtitles_sources.inl').read_text(encoding='utf-8')
rows = re.findall(r'\{GameTitle::(\w+),0x([0-9A-F]+),0x([0-9A-F]+),(\d+),(true|false),\s*"([0-9A-F ]+)"\}', source)
assert len(rows) == 10
images = {}
for title, (module, digest) in MODULES.items():
    raw = (ROOT / 'out/deps/re-tools/inputs' / module).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == digest, module
    images[title] = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
report = []
for title, start, size, channel, refreshed, pattern in rows:
    start, size = int(start, 16), int(size, 16)
    expected = bytes.fromhex(pattern)
    image = images[title]
    assert len(expected) == size and image[start:start+size] == expected
    assert image.count(expected) == 1, (title, hex(start))
    assert expected.endswith(bytes.fromhex('FF 90 C8 02 00 00')) or expected.endswith(bytes.fromhex('41 FF D2'))
    report.append({'title': title, 'return_rva': hex(start+size),
                   'channel': int(channel), 'refreshed': refreshed == 'true'})
print(json.dumps({'result': 'PASS_OFFLINE_ONLY', 'production_sources': report}, indent=2))
