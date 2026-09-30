"""Extract H4's own FP finger hierarchy and defaults, without geometry."""
from pathlib import Path
import hashlib
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
source = root / 'out/left-hand-shared-evidence/h4/storm-fp.xml'
with source.open('rb') as stream:
    assert hashlib.file_digest(stream,'sha256').hexdigest() == '047501a9c6811097fc8e6abbb591ec5bc4610ee441976caafed9eeff6f13591f'
nodes=[]
for event,element in ET.iterparse(source,events=('end',)):
    if element.tag=='block' and element.get('name')=='nodes':
        for node in element:
            fields={f.get('name'):f.get('value') for f in node.findall('field')}
            nodes.append((node.get('name'),int(fields['parent node'].split(',')[-1]),fields))
        break
assert len(nodes)==80 and nodes[37][0]=='b_l_hand' and nodes[29][0]=='b_r_hand'
joints={39,43,46,48,57,59,60,61,62,64,69,70,73,74,75,79,
        40,41,44,49,56,58,63,65,66,67,68,71,72,76,77,78}
def values(text):
    return ','.join(v+('.0f' if '.' not in v and 'e' not in v.lower() else 'f') for v in text.split(','))
lines=['#pragma once', '// Generated from own SHA-pinned H4EK storm_fp XML; see tools/re/generate_h4_fp_finger_bind.py.',
       'inline constexpr int16_t kHalo4FpFingerParents[80]{'+','.join(str(n[1]) for n in nodes)+'};',
       'struct Halo4FpFingerBind { unsigned node,parent; float quaternion[4],position[3]; };',
       'inline constexpr Halo4FpFingerBind kHalo4FpFingerBind[]{']
for i in sorted(joints):
    name,parent,fields=nodes[i]
    assert parent<i and (parent in joints or parent in (29,37))
    lines.append(f'    {{{i},{parent},{{{values(fields["default rotation"])} }},{{{values(fields["default translation"])} }}}}, // {name}')
lines.append('};')
(root/'src/common/halo4_fp_finger_bind.generated.h').write_text('\n'.join(lines)+'\n')
