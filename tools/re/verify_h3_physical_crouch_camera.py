"""Offline own-title H3 camera witnesses and actual native math execution.

No process access, native hook, or engine/game file writes.
"""
from pathlib import Path
import hashlib
import re
import struct
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'out/pydeps'))
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64
from unicorn.x86_const import UC_X86_REG_RDI,UC_X86_REG_R12,UC_X86_REG_XMM6,UC_X86_REG_XMM7

raw=(ROOT/'out/deps/re-tools/inputs/halo3.dll').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='b209d8454b12dc77e54ccd2c9924ec8d44b8619d21cf98e36ffaf601e67efb63'
kit=(ROOT/'out/deps/re-tools/inputs/halo3_tag_test.exe').read_bytes()
assert hashlib.sha256(kit).hexdigest()=='59a78f2c96034d7ceb5d710505b2b36813aa141fc81a083e3f952973dbce4602'
pe=pefile.PE(data=raw);image=pe.get_memory_mapped_image()
source=(ROOT/'src/dll/halo3_physical_crouch.inl').read_text()
patterns=re.findall(r'"([0-9A-F ]{35,})"',source)
assert len(patterns)==5
for text in patterns:
    pattern=bytes.fromhex(text)
    assert image.count(pattern)==1
    print('unique',hex(image.index(pattern)),text)
assert 0x3556B7+5+struct.unpack_from('<i',image,0x3556B8)[0]==0x37C394
assert 0x37C4C2+5+struct.unpack_from('<i',image,0x37C4C3)[0]==0x37E58C
assert struct.unpack_from('<f',image,0x37C490+8+struct.unpack_from('<i',image,0x37C494)[0])[0]==1

def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def xmm(v):return struct.unpack('<I',struct.pack('<f',v))[0]
emulator=Uc(UC_ARCH_X86,UC_MODE_64)
emulator.mem_map(0x10000,0x1000)
emulator.mem_map(0x20000,0x2000)
emulator.mem_write(0x10000,image[0x37C61E:0x37C642])
emulator.reg_write(UC_X86_REG_RDI,0x20000)
emulator.reg_write(UC_X86_REG_R12,0x21000)
cases=0
for scale in (.5,1,2):
 for standing in (.4,.7,1.4):
  for crouched in (0,standing*.4,standing):
   for fraction in (0,.01,.25,.5,.75,1):
    emulator.mem_write(0x2008C,struct.pack('<f',scale))
    emulator.mem_write(0x212E8,struct.pack('<ff',standing,crouched))
    emulator.reg_write(UC_X86_REG_XMM6,xmm(1))
    emulator.reg_write(UC_X86_REG_XMM7,xmm(fraction))
    emulator.emu_start(0x10000,0x10000+0x24,count=10)
    actual=struct.unpack('<f',struct.pack('<I',emulator.reg_read(UC_X86_REG_XMM6)&0xffffffff))[0]
    expected=f32(f32(f32(1-f32(fraction))*f32(standing))+f32(f32(fraction)*f32(crouched)))
    expected=f32(expected*f32(scale))
    assert abs(actual-expected)<1e-6,(scale,standing,crouched,fraction,actual,expected)
    cases+=1
print(f'PASS: 5 unique H3 witnesses, 2 native call edges, {cases} executed native height cases')
