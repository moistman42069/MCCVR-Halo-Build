"""Generate own ODST FP bind fixture from the preserved official XML exports."""
from pathlib import Path
import hashlib
import xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[2]
models=[]
for character,digest in [
    ('odst_recon','c7c6b3bca2a440843ed850b1fb1e5fd4548e5720adc03a54733b9607598c9e53'),
    ('odst_oni_op','6126a5e692b8c309c8a2071cc50f5af8b4ca153b2835c534b313674960312fe6')]:
    path=root/f'out/left-hand-shared-evidence/odst/objects_characters_{character}_fp_fp.render_model.xml'
    assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
    tag=ET.fromstring(path.read_text().replace('value="<unavailable>"','value="unavailable"'))
    nodes=[]
    for node in tag.find("block[@name='nodes']"):
        fields={f.get('name'):f.get('value') for f in node.findall('field')}
        nodes.append((node.get('name'),int(fields['parent node'].split(',')[-1]),
                      fields['default translation'].split(',')+fields['default rotation'].split(',')))
    assert len(nodes)==37
    models.append(nodes)
assert models[0]==models[1], 'The two own profiles must not silently share different binds'
lines=['#pragma once', '// Generated from both SHA-pinned official ODST recon/ONI FP exports.',
       '// Default TR fields are used; the official XML inverse labels are shifted.',
       'inline constexpr float OdstFpBindLocal[37][7]{']
for name,parent,values in models[0]:
    values=[x if ('.' in x or 'e' in x.lower()) else x+'.0' for x in values]
    lines.append('    {'+','.join(x+'f' for x in values)+'}, // '+name)
lines += ['};','inline constexpr int16_t OdstFpBindParents[37]{'+','.join(str(x[1]) for x in models[0])+'};','']
(root/'tests/odst_finger_bind_fixture.h').write_text('\n'.join(lines))
print('Verified both own ODST exports; generated 37-node default-bind fixture')
