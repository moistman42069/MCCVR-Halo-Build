"""Pinned, read-only proof of Reach/H4 native physical world-contact paths."""
from pathlib import Path
import hashlib
import re
import struct
import pefile

ROOT=Path(__file__).resolve().parents[1]
INPUTS=ROOT/'out/deps/re-tools/inputs'
PINS={
 'reach_tag_test.exe':'CBDD8448A87A433B0DFFC0DE47D06DB7A18B4BF868B96B057135DAA86790ABA8',
 'haloreach.dll':'738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894',
 'halo4_tag_test.exe':'B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6',
 'halo4.dll':'7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84',
}
images={}
checks=0
for name,pin in PINS.items():
    raw=(INPUTS/name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper()==pin,name
    images[name]=pefile.PE(data=raw,fast_load=True).get_memory_mapped_image()
    checks+=1

def call(image,site,target):
    global checks
    assert image[site]==0xE8 and site+5+struct.unpack_from('<i',image,site+1)[0]==target,hex(site)
    checks+=1

def contains(image,begin,end,text):
    global checks
    assert bytes.fromhex(text) in image[begin:end],(hex(begin),text)
    checks+=1

kit=images['reach_tag_test.exe'];retail=images['haloreach.dll']
call(kit,0xD6C67C,0x41B960)  # melee's own native collision ray
call(kit,0xD6D231,0x2B16C0)  # native authored breakable damage
call(retail,0x491E89,0x12969C)
call(retail,0x493200,0x15AF28)
# HREK world target preserves the real surface tuple only under breakable flag8.
contains(kit,0xD6C1B0,0xD6CD60,'89 43 18')
contains(kit,0xD6C1B0,0xD6CD60,'89 43 1C')
contains(kit,0xD6C1B0,0xD6CD60,'89 43 28')
contains(kit,0xD6C1B0,0xD6CD60,'89 43 2C')
contains(retail,0x492234,0x492283,'F6 45 FD 08')
contains(retail,0x492234,0x492283,'0F B6 45 02 89 43 18 0F B6 45 FE 89 43 1C')
contains(retail,0x4931D8,0x493205,'44 0F BF 4B 22 44 0F BF 43 20 8B 53 24 8B 4B 18')
# Every installed runtime proof must be unique in the pinned image.
source=(ROOT/'src/dll/reach_contact_melee_runtime.inl').read_text()
for rva,pattern in re.findall(r'\{(0x[0-9A-Fa-f]+),"([0-9A-Fa-f? ]+)"\}',source):
    expression=b''.join(b'.' if x=='??' else re.escape(bytes([int(x,16)])) for x in pattern.split())
    assert [m.start() for m in re.finditer(expression,retail,re.DOTALL)]==[int(rva,16)],rva
    checks+=1
kit=images['halo4_tag_test.exe'];retail=images['halo4.dll']
call(kit,0xE7362E,0x3D4D20)
call(retail,0x603768,0x21E908)
call(retail,0x6020E5,0x1C1D4C)
call(retail,0x1C1E7A,0x1C1330)
contains(retail,0x602580,0x6025B3,'8B 45 00 89 43 14')
contains(retail,0x602580,0x6025B3,'0F B7 45 18 66 89 43 20')
# Official object collision wrapper calls the real BSP vector traversal.
call(kit,0x4DC132,0x7ADE90)
for site,target in ((0x21F814,0xDEA60),(0x24F631,0x2DE9FC),
                    (0x24F675,0x2DE9FC),(0x24F657,0x2DEC78),
                    (0x24F663,0x2DEF10),(0x24F69E,0x2DF400),
                    (0x24F6AA,0x2DF698),(0x21E95A,0x21DFF8)):
    call(retail,site,target)
# SIMD return ABI: actual caller stores XMM0, not RAX. The native initializer
# returns a four-float segment span and retains its 10-argument stack layout.
contains(retail,0x24F636,0x24F63D,'0F 29 44 24 50')
contains(retail,0x24F67A,0x24F681,'0F 29 44 24 50')
contains(retail,0x2DE9FC,0x2DEC76,'F3 0F 10 6D 50')
contains(retail,0x2DEC64,0x2DEC76,'0F 14 D1 0F 57 C0 0F 14 C2')
# Both BSP representations write genuine surface/set bytes and material into
# the same native result; this is distinct from the render-triangle ray list.
for begin,end in ((0x2DEF10,0x2DF400),(0x2DF698,0x2DFB64)):
    for pattern in ('88 41 1D','88 41 1E','88 41 1F','66 89 41 20'):
        contains(retail,begin,end,pattern)
source=(ROOT/'src/dll/halo4_contact_melee_runtime.inl').read_text()
for rva,pattern in re.findall(r'\{(0x[0-9A-Fa-f]+),"([0-9A-Fa-f? ]+)"\}',source):
    expression=b''.join(b'.' if x=='??' else re.escape(bytes([int(x,16)])) for x in pattern.split())
    assert [m.start() for m in re.finditer(expression,retail,re.DOTALL)]==[int(rva,16)],rva
    checks+=1
print(f'Reach/H4 world-contact pinned proof: {checks} checks passed')
