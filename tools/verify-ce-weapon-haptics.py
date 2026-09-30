"""Read-only pinned verification of CE source-tagged authored recoil."""
from pathlib import Path
import hashlib
import re
import struct
import pefile

root=Path(__file__).resolve().parents[1]
raw=(root/'out/deps/re-tools/inputs/halo1.dll').read_bytes()
assert hashlib.sha256(raw).hexdigest().upper()=='0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C'
pe=pefile.PE(data=raw);image=pe.get_memory_mapped_image()
source=(root/'src/common/haloce_haptic_contract.h').read_text()
names={'enqueue':0xB9B518,'update':0xB9B400,'curve':0xC72528,'trigger':0xB78DE8,
       'contract::contact::contact_damage':0xB9EA28}
checks=1
for name,key,pattern,unwind in re.findall(r'\{"([^"]+)",(\w+),"([^"]+)",(true|false)\}',source):
    addr=names[key];expected=bytes.fromhex(pattern)
    assert [m.start() for m in re.finditer(re.escape(expected),image)]==[addr],name
    if unwind=='true':assert any(e.struct.BeginAddress==addr for e in pe.DIRECTORY_ENTRY_EXCEPTION),name
    else: assert key=='enqueue' # Own native leaf has no unwind entry.
    checks+=2
for address,pattern in re.findall(r'\{0x([0-9A-F]+),"([^"]+)"\}',source):
    addr=int(address,16);expected=bytes.fromhex(pattern)
    assert image[addr:addr+len(expected)]==expected,address
    checks+=1
for address,length,offset,key in re.findall(r'\{0x([0-9A-F]+),(\d+),(\d+),([\w:]+)\}',source):
    addr=int(address,16)
    assert addr+int(length)+struct.unpack_from('<i',image,addr+int(offset))[0]==names[key],address
    checks+=1
assert struct.unpack_from('<f',image,0x194F150)[0]==struct.unpack('<f',struct.pack('<f',1/30))[0]
checks+=1
kit_raw=(root/'out/deps/re-tools/inputs/halo_tag_test.exe').read_bytes()
assert hashlib.sha256(kit_raw).hexdigest().upper()=='FC9E2B6193C6F6D9FF988278B0D39A983747F3FDBDECA7CAFADD28F1F0C53E73'
kit=pefile.PE(data=kit_raw).get_memory_mapped_image()
for call,target in [(0x4F6A94,0x3F4440),(0x12E854,0x194C90),(0x194F51,0x3B4140)]:
    assert kit[call]==0xE8 and call+5+struct.unpack_from('<i',kit,call+1)[0]==target,hex(call)
    checks+=1
print(f'CE weapon haptics: {checks} pinned native and official-kit checks pass; headset acceptance pending')
