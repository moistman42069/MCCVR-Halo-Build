"""Check VR semantic identities against each official kit's named UI glyphs.

Uses the preserved Ghidra decompiles of those kits, never a cross-title enum or
retail binary for discovery. Retail layout/signature checks are separate in
verify-gesture-melee-bindings.py. No game is launched or modified.
"""
import ast
import hashlib
import json
from pathlib import Path
import re
import struct
import pefile

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ROOT / 'out/deps/re-tools/inputs'
source = (ROOT / 'src/common/vr_action_mapping.h').read_text()
names = ['fire','throw_grenade','jump','melee_attack','action_reload','action_generic',
         'switch_weapon','switch_grenade','use_equipment','crouch','scope_zoom','flashlight','sprint']
kits = [
    ('h2','halo2_tag_test.exe',0xAA8DF4,4,0x312EC7),
    ('h3','halo3_tag_test.exe',0x12810B0,8,0x98D390),
    ('odst','halo3odst_tag_test.exe',0x132D380,8,0x9F4780),
    ('reach','reach_tag_test.exe',0x20A2420,8,0x909730),
    ('h4','halo4_tag_test.exe',0x24FE380,8,0x97B530),
]
report=[]
for title,filename,table,psize,callback in kits:
    pe=pefile.PE(str(INPUTS/filename),fast_load=True)
    glyphs={}
    for index in range(65):
        entry=table+index*psize*3
        raw=pe.get_data(entry,psize+4)
        pointer=struct.unpack_from('<I' if psize==4 else '<Q',raw)[0]-pe.OPTIONAL_HEADER.ImageBase
        if not 0<=pointer<pe.OPTIONAL_HEADER.SizeOfImage: continue
        data=pe.get_data(pointer,160)
        text=data[:next((n for n in range(0,len(data)-1,2) if data[n:n+2]==b'\0\0'),len(data))].decode('utf-16-le',errors='replace')
        if text.startswith('button_'): glyphs[text]=(entry,struct.unpack_from('<I',raw,psize)[0])
    log=ROOT/f'out/contact-gesture-{title}-action-kit-console.txt'
    decompile=log.read_text(encoding='utf-16').split('DECOMPILE:',1)[1]
    function=f'FUN_{pe.OPTIONAL_HEADER.ImageBase+callback:08x}'
    assert function in decompile,(filename,function)
    identities={}
    for match in re.finditer(r'case (0x[0-9a-f]+):',decompile):
        glyph=int(match[1],16)
        if glyph<0xE000: continue
        body=decompile[match.end():].split('break;',1)[0]
        if title=='h2':
            value=re.search(r'local_2f94 = CONCAT31\(local_2f94\._1_3_,(0x[0-9a-f]+|\d+)\)',body)
            if value: identities[glyph]=int(value[1],0)
            elif 'local_2f94 = uVar2 << 8' in body: identities[glyph]=0
        else:
            variable='bVar7' if title=='reach' else ('cVar3' if title=='h4' else 'cVar6')
            value=re.search(variable+r" = ('(?:\\.|[^'])*'|-?0x[0-9a-f]+|-?\d+);",body)
            if value:
                parsed=ast.literal_eval(value[1])
                identities[glyph]=(ord(parsed) if isinstance(parsed,str) else parsed)&255
    if title in ('h3','odst'):
        assert re.search(r"if \(param_2 == 0xe443\) \{\s*cVar6 = '\\0';",decompile)
        identities[0xE443]=0
    values=re.search(r'constexpr unsigned '+title+r'\[Count\]\{([^}]+)\}',source)[1].split(',')
    title_names=list(names)
    if title in ('reach','h4'): title_names[8]='equipment'
    if title=='h4': title_names[7]='switch_grenade_next'
    if title=='h2': title_names[5]='touch_device'
    checked=[]
    for index,value in enumerate(values):
        if value.strip()=='kNoAction': continue
        expected=int(value,0)
        name='button_'+title_names[index]
        assert name in glyphs,(title,name)
        entry,glyph=glyphs[name]
        assert identities[glyph]==expected,(title,name,identities.get(glyph),expected)
        checked.append({'name':name,'entry':hex(entry),'glyph':hex(glyph),'action':expected})
    report.append({'title':title,'kit':filename,'sha256':hashlib.sha256((INPUTS/filename).read_bytes()).hexdigest(),
        'callback':hex(callback),'decompile_sha256':hashlib.sha256(log.read_bytes()).hexdigest(),'actions':checked})
    print(f'PASS: {title}: {len(checked)} named glyph-to-action chains',flush=True)
(ROOT/'out/vr-action-identities.json').write_text(json.dumps(report,indent=2)+'\n')
