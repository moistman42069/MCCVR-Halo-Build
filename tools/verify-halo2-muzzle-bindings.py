"""Verify official H2EK-matched muzzle consumers in the pinned retail image."""
import hashlib
from pathlib import Path
import re
import struct
import sys

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'out/pydeps'))
import pefile
raw=(root/'out/deps/re-tools/inputs/halo2.dll').read_bytes()
assert hashlib.sha256(raw).hexdigest().upper()=='DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946'
pe=pefile.PE(data=raw);image=pe.get_memory_mapped_image()
source=(root/'src/dll/halo2_muzzle_lifecycle.inl').read_text()
bindings=re.findall(r'\{0x([0-9A-F]+),"([0-9A-F ]+)"\}',source)
assert len(bindings)==4
for rva,pattern in bindings:
    assert [m.start() for m in re.finditer(re.escape(bytes.fromhex(pattern)),image)]==[int(rva,16)]
for call,target in [(0x8E4B94,0x8E8990),(0x8E4BAA,0x8D6570),(0x8D657E,0x8D11A0)]:
    assert image[call]==0xE8 and call+5+struct.unpack_from('<i',image,call+1)[0]==target
for begin,end in [(0x8D6570,0x8D6588),(0x8D11A0,0x8D14D2)]:
    assert [(e.struct.BeginAddress,e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION
            if e.struct.BeginAddress==begin]==[(begin,end)]
# The firing caller supplies [rbp+190], then consumes world position at +1F0
# and forward at +1CC: marker-relative +60 and +3C. Each marker is 70 bytes.
for address,pattern in {
    0x8E4B9B:'4C 8D 85 90 01 00 00',
    0x8E4E01:'48 6B D8 70',
    0x8E4E10:'F2 0F 10 84 1D F0 01 00 00',
    0x8E4E30:'F2 0F 10 84 1D CC 01 00 00',
}.items():
    expected=bytes.fromhex(pattern)
    assert image[address:address+len(expected)]==expected,hex(address)
print('PASS: pinned H2 muzzle identity, four unique marker/owner witnesses, three call edges, two hook unwind entries and native marker consumers')
