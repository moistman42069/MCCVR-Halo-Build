"""Verify every read-only gesture binding against its own pinned engine/kit."""
import hashlib
import json
import pathlib
import re
import struct
import pefile
import runpy
import sys

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / 'src/dll/gesture_binding_reader.inl').read_text()
descriptors = re.findall(
    r'\{GameTitle::(\w+),([^\n]+),\s*0x([0-9A-F]+),0x([0-9A-F]+),\s*'
    r'"([0-9A-F? ]+)",\s*"([0-9A-F? ]+)"\}', source)
assert len(descriptors) == 6
inputs = root / 'out/deps/re-tools/inputs'
pinned = {
    'HaloCE': ('halo1.dll', '0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C', 0x2EA0610),
    'Halo2': ('halo2.dll', 'DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946', 0x15EB7A0),
    'Halo3': ('halo3.dll', 'B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63', 0x2229B54),
    'Halo4': ('halo4.dll', '7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84', 0x2E959D4),
}
# Obtain the other two pinned hashes from their existing independently checked
# contact verifier, avoiding a second hand-maintained copy of those identities.
contact_verifier = (root / 'tools/verify-contact-melee-bindings.py').read_text()
for title, filename, state in [('Halo3ODST','halo3odst.dll',0x227AA54), ('HaloReach','haloreach.dll',0x287FF94)]:
    after = contact_verifier[contact_verifier.index("inputs/"+filename):]
    expected = re.search(r"expected = '([A-F0-9]{64})'", after)[1]
    pinned[title] = (filename, expected, state)

report = []
for title, fields, reader, load, reader_pattern, state_pattern in descriptors:
    filename, expected, state = pinned[title]
    raw = (inputs / filename).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == expected, filename
    image = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
    for rva, pattern in [(int(reader,16),reader_pattern),(int(load,16),state_pattern)]:
        expression = b''.join(b'.' if byte == '??' else re.escape(bytes([int(byte,16)])) for byte in pattern.split())
        matches = [m.start() for m in re.finditer(b'(?='+expression+b')',image,re.S)]
        assert matches == [rva], (filename,hex(rva),matches)
    load = int(load,16)
    displacement,next_instruction = (6,10) if title == 'HaloCE' else (10,14)
    assert load+next_instruction+struct.unpack_from('<i',image,load+displacement)[0] == state
    action,count,stride,remap,controller = [int(v,0) for v in fields.split(',')]
    assert action < count and state + stride*4 <= len(image)
    report.append({'title':title,'sha256':expected,'unique_patterns':2,'state_rva':hex(state),
                   'action':action,'stride':hex(stride),'remap':hex(remap)})

# Named action identities and independently evidenced kit transport tables.
kits = [
    ('halo2_tag_test.exe',0xAA8E3C,0xE44A,0x7B2F78,4,True),
    ('halo3_tag_test.exe',0x1281110,0xE447,0x109F038,8,False),
    ('halo3odst_tag_test.exe',0x132D3E0,0xE447,0x115F8A0,8,False),
    ('reach_tag_test.exe',0x20A24B0,0xE448,0x1649098,8,False),
    ('halo4_tag_test.exe',0x24FE440,0xE44A,0x19B56E0,8,False),
]
for filename, entry, glyph, table, pointer_size, halo2 in kits:
    pe = pefile.PE(str(inputs / filename), fast_load=True)
    image = pe.get_memory_mapped_image()
    string = struct.unpack_from('<I' if pointer_size == 4 else '<Q',image,entry)[0] - pe.OPTIONAL_HEADER.ImageBase
    expected_text = 'button_melee_attack\0'.encode('utf-16-le')
    assert image[string:string+len(expected_text)] == expected_text
    assert struct.unpack_from('<I',image,entry+pointer_size)[0] == glyph
    masks = [1,2,4,8,16,32,64,128] + ([256,512,4096,8192,16384,32768] if halo2 else [4096,8192,16384,32768,256,512])
    assert list(struct.unpack_from('<14I',image,table)) == masks, filename

# CE's 33 ASCII action names and independent XI converter use a different
# evidence shape from the later games' Unicode glyph entries. Run its own
# hash-pinned kit/retail consumer proof rather than inventing a glyph entry.
sys.path.insert(0, str(root / 'tools/re'))
ce_proof = runpy.run_path(str(root / 'tools/re/verify_ce_controller_mapping.py'))
ce_proof['main']()

(root / 'out/gesture-melee-native-bindings.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 6 title-specific melee identities, 12 unique native patterns, 6 state pointers, 6 kit XInput tables')
