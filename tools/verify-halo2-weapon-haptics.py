"""Read-only own-kit / pinned-retail hand-specific authored recoil evidence."""
from pathlib import Path
import hashlib
import re
import struct
import pefile
root=Path(__file__).resolve().parents[1]
raw=(root/'out/deps/re-tools/inputs/halo2.dll').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='de65b4f4fdbf3f0a5eab7431fe530da17dd815599182dfd6ae9b7e21cf171946'
pe=pefile.PE(data=raw);image=pe.get_memory_mapped_image()
source=(root/'src/common/halo2_haptic_contract.h').read_text()
names={name:int(value,16) for name,value in re.findall(r'(\w+)=(0x[0-9A-F]+)',source)}
def address(text):return int(text,16) if text.startswith('0x') else names[text]
checks=1
for key,pattern in re.findall(r'\{(\w+),"([^"]+)"\}',source):
    a=address(key);pattern=bytes.fromhex(pattern)
    assert [m.start() for m in re.finditer(re.escape(pattern),image)]==[a],key
    if key in ['trigger','effect','enqueue','update','evaluate','curve']:
        assert any(e.struct.BeginAddress==a for e in pe.DIRECTORY_ENTRY_EXCEPTION),key
    checks+=1
for call,target in re.findall(r'\{(0x[0-9A-F]+),(\w+)\}',source):
    a=address(call);assert image[a]==0xE8 and a+5+struct.unpack_from('<i',image,a+1)[0]==address(target),call
    checks+=1
for at,length,disp,target in re.findall(r'\{(0x[0-9A-F]+),(\d+),(\d+),(\w+)\}',source):
    a=address(at);assert a+int(length)+struct.unpack_from('<i',image,a+int(disp))[0]==address(target),at
    checks+=1
kitraw=(root/'out/deps/re-tools/inputs/halo2_tag_test.exe').read_bytes()
assert hashlib.sha256(kitraw).hexdigest().upper()=='D0B71186D3948C48DDD02E2CCB88FA13E77E25A3D8F7FA60922F23A2A0073E36'
kit=pefile.PE(data=kitraw).get_memory_mapped_image()
for a,target in [(0x49F1B8,0x484420),(0x4844F5,0x99490),(0x99734,0xD3150),(0xD3314,0xD2AD0),(0xD2C09,0x1CF760)]:
    assert kit[a]==0xE8 and a+5+struct.unpack_from('<i',kit,a+1)[0]==target,hex(a)
    checks+=1
print(f'Halo 2 weapon haptics: {checks} pinned/own-kit checks pass; headset acceptance pending')
