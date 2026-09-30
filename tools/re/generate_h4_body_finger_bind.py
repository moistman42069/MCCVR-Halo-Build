"""Regenerate H4 world-finger locals from the pinned official model summary."""
from pathlib import Path
import hashlib
import json

root = Path(__file__).resolve().parents[2]
source = root / 'out/h4-body-identity-review/storm_masterchief.summary.json'
raw = source.read_bytes()
assert hashlib.sha256(raw).hexdigest() == '43fc6aa87c9e6a524412341d19426b400db3eccc3d0df3add51d7411b8ee7379'
model = json.loads(raw)
assert len(model['nodes']) == 120
assert int(model['identity']['runtime import info checksum']) == 0x17010100
joints = {84,100,110,94,99,115,89,104,114,93,102,113,90,103,109,118,
          85,101,111,81,107,116,87,106,112,96,98,117,80,105,108,119}
lines = ['#pragma once',
    '// H4EK storm_masterchief own 120-node default local finger poses.',
    '// Source tag SHA256 C2699114B2FF80031F00AA4CBE6ABB0FEB70D86A29F9D05BB7EA086211563DAB.',
    '// Fields use valid default translation/rotation, never shifted XML inverse labels.',
    'struct Halo4BodyFingerBind { unsigned node,parent; float quaternion[4],position[3]; };',
    'inline constexpr Halo4BodyFingerBind kHalo4BodyFingerBind[]{']

def cpp_values(value):
    return ','.join(v + ('.0f' if '.' not in v and 'e' not in v.lower() else 'f')
                    for v in value.split(','))

for i in sorted(joints):
    node = model['nodes'][i]
    fields = node['fields']
    parent = int(fields['parent node'][0].split(',')[-1])
    assert parent < i and (parent in joints or parent in (68,73))
    q = cpp_values(fields['default rotation'][0])
    p = cpp_values(fields['default translation'][0])
    lines.append(f'    {{{i},{parent},{{{q}}},{{{p}}}}}, // {node["name"]}')
lines.append('};')
(root / 'src/common/halo4_body_finger_bind.generated.h').write_text('\n'.join(lines)+'\n')
