"""Verify existing Reach/H4 crouch-camera contracts against pinned own-title code.

Read-only offline validation. Own-kit derivation is documented in
docs/PHYSICAL-CROUCH-CAMERA-2026-09-23.md. No game process is opened.
"""
from pathlib import Path
import hashlib
import re
import struct
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'out/pydeps'))
import pefile
import capstone

specs={
    'HaloReach':('haloreach.dll','738dd2d24ea3aea12e1ee9aa4a61094bf116027d42004c35a19e5048608b0894',
        'reach_tag_test.exe','cbdd8448a87a433b0dffc0de47d06db7a18b4bf868b96b057135daa86790aba8',
        0x4A0D14,0x53,0x57,0x67,0x6B,0x4E39F20,0xC1A600,
        [(0x48A284,0x4A2D50),(0x4A0E5E,0x4A0D14)]),
    'Halo4':('halo4.dll','7c53e7d5bc9848545a1b70e2768242479336fba1b7630d7ab955f7fd0c34fa84',
        'halo4_tag_test.exe','b7468db9fd160b035c329540ee0b0d47bcf609e1ba6e85ae4f204b70661113a6',
        0x62E0A4,0x52,0x56,0x68,0x6C,0x496A180,0x107C0B0,
        [(0x5F8BF2,0x6320D0),(0x62E1FD,0x62E0A4)])}
source=(ROOT/'src/common/physical_crouch_cached_witnesses.h').read_text()
patterns=re.findall(r'\{GameTitle::(\w+),0x([0-9A-F]+),"([0-9A-F ]+)"\}',source)
assert len(patterns)==10
for title,spec in specs.items():
    name,digest,kit,kitdigest,selector,pdisp,pnext,tdisp,tnext,pages,table,edges=spec
    raw=(ROOT/'out/deps/re-tools/inputs'/name).read_bytes()
    assert hashlib.sha256(raw).hexdigest()==digest
    assert hashlib.sha256((ROOT/'out/deps/re-tools/inputs'/kit).read_bytes()).hexdigest()==kitdigest
    pe=pefile.PE(data=raw);image=pe.get_memory_mapped_image()
    section=next(s for s in pe.sections if s.Name.rstrip(b'\0')==b'.text')
    code=image[section.VirtualAddress:section.VirtualAddress+section.Misc_VirtualSize]
    selected=[(int(rva,16),bytes.fromhex(text)) for game,rva,text in patterns if game==title]
    assert len(selected)==5
    for rva,pattern in selected:
        assert image[rva:rva+len(pattern)]==pattern
        assert code.count(pattern)==1
        if rva == selector:
            # The exact full selector is a leaf: Windows x64 does not require
            # unwind metadata when no stack/nonvolatile register is changed.
            decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
            decoder.detail=True
            instructions=list(decoder.disasm(pattern,rva))
            assert sum(i.size for i in instructions)==len(pattern)
            assert instructions[-1].mnemonic=='ret'
            nonvolatile={'rbx','ebx','rsi','esi','rdi','edi','rbp','ebp','rsp','esp',
                         'r12','r13','r14','r15',*(f'xmm{i}' for i in range(6,16))}
            for ins in instructions[:-1]:
                assert ins.mnemonic not in ('call','push','pop','ret')
                assert not {ins.reg_name(r) for r in ins.regs_access()[1]} & nonvolatile
        else:
            assert any(e.struct.BeginAddress<=rva<rva+len(pattern)<=e.struct.EndAddress
                       for e in pe.DIRECTORY_ENTRY_EXCEPTION),(title,hex(rva),'unwind range')
    assert selector+pnext+struct.unpack_from('<i',image,selector+pdisp)[0]==pages
    assert selector+tnext+struct.unpack_from('<i',image,selector+tdisp)[0]==table
    assert pages+16*8<=len(image) and table+8<=len(image)
    for caller,target in edges:
        assert image[caller]==0xE8 and caller+5+struct.unpack_from('<i',image,caller+1)[0]==target
    print(f'{title}: 5 unique native witnesses, 2 call edges, own tag table/page RIP decode PASS')
print('PASS: 10 pinned own-title cached-crouch witnesses; no runtime/headset claim')
