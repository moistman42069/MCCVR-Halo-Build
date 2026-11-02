"""Read-only verification of H2/H3 per-weapon reload routing (pefile, capstone).
Pass --inputs a directory containing the four pinned user-supplied binaries.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import capstone
import pefile

HASHES = {
    'halo2.dll': 'de65b4f4fdbf3f0a5eab7431fe530da17dd815599182dfd6ae9b7e21cf171946',
    'halo3.dll': 'b209d8454b12dc77e54ccd2c9924ec8d44b8619d21cf98e36ffaf601e67efb63',
    'halo2_tag_test.exe': 'd0b71186d3948c48ddd02e2ccb88fa13e77e25a3d8f7fa60922f23a2a0073e36',
    'halo3_tag_test.exe': '59a78f2c96034d7ceb5d710505b2b36813aa141fc81a083e3f952973dbce4602',
}
# RVAs: kit discovery, then retail homolog verification. Magazine zero chooses
# the unit action by comparing the requested weapon with the owner's slots.
PROOFS = {
    'halo2_tag_test.exe': (0x4a3350, 0x4a3428, {
        0x4a338d: 'movsx ax, byte ptr [eax + 0x226]',
        0x4a33af: 'movsx cx, byte ptr [eax + 0x227]',
        0x4a33d6: 'test ax, ax', 0x4a33dd: 'mov eax, 0xa',
        0x4a33e5: 'cmovne eax, ecx', 0x4a33ea: 'call 0x4c8f60'}),
    'halo2.dll': (0x8eadf0, 0x8eaf2f, {
        0x8eae84: 'movsx edx, byte ptr [rax + 0x226]',
        0x8eaeb9: 'movsx edx, byte ptr [rax + 0x227]',
        0x8eaed8: 'test di, di', 0x8eaee0: 'mov edx, 0xa',
        0x8eaee5: 'cmovne edx, r15d', 0x8eaeeb: 'call 0x93b750'}),
    'halo3_tag_test.exe': (0xa8bce0, 0xa8be11, {
        0xa8bd71: 'movsx edx, byte ptr [rax + 0x262]',
        0xa8bd93: 'movsx edx, byte ptr [rax + 0x263]',
        0xa8bdae: 'test r14w, r14w', 0xa8bdbb: 'lea edx, [rax*2 + 0x3d]',
        0xa8bddb: 'call 0xa7e8d0', 0xa8bdee: 'call 0x1ab5b0'}),
    'halo3.dll': (0x35f040, 0x35f172, {
        0x35f0e7: 'movsx edx, byte ptr [r10 + 0x262]',
        0x35f0f4: 'movsx edx, byte ptr [r10 + 0x263]',
        0x35f11b: 'test bp, bp', 0x35f120: 'mov edx, 0x3f',
        0x35f125: 'lea eax, [rdx - 2]', 0x35f13d: 'cmove edx, eax',
        0x35f140: 'call 0x379c08', 0x35f151: 'call 0x1ffb0'}),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, required=True)
    args = parser.parse_args()
    bindings = (Path(__file__).resolve().parents[2] / 'src/dll/native_reload_bindings.generated.h').read_text()
    for name, digest in HASHES.items():
        raw = (args.inputs / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError(f'{name}: unexpected SHA-256')
        pe = pefile.PE(data=raw)
        data = pe.get_memory_mapped_image()
        mode = capstone.CS_MODE_64 if pe.FILE_HEADER.Machine == 0x8664 else capstone.CS_MODE_32
        start, end, proofs = PROOFS[name]
        instructions = {i.address: f'{i.mnemonic} {i.op_str}' for i in
                        capstone.Cs(capstone.CS_ARCH_X86, mode).disasm(data[start:end], start)}
        for rva, expected in proofs.items():
            if instructions.get(rva) != expected:
                raise ValueError(f'{name}+{rva:x}: expected {expected}')
        if name.endswith('.dll'):
            pattern = re.search(r'\{0x' + f'{start:x}' + r',"([^"]+)"', bindings).group(1)
            signature = bytes.fromhex(pattern)
            if data.find(signature) != start or data.find(signature, start + 1) != -1:
                raise ValueError(f'{name}: reload signature is not unique at expected RVA')
        if name == 'halo2_tag_test.exe':
            for index, label in ((0x2e, b'reload\0'), (0x2f, b'exit_vehicle\0')):
                pointer = struct.unpack_from('<I', data, 0xa447f8 + (index - 0x2e) * 4)[0]
                offset = pointer - pe.OPTIONAL_HEADER.ImageBase
                if data[offset:offset + len(label)] != label:
                    raise ValueError('H2 action-name table mismatch')
        if name == 'halo3_tag_test.exe':
            unit_instructions = {i.address: f'{i.mnemonic} {i.op_str}' for i in
                capstone.Cs(capstone.CS_ARCH_X86, mode).disasm(data[0xa7e9d0:0xa7ea9d], 0xa7e9d0)}
            if unit_instructions.get(0xa7ea7a) != 'cmp di, 4' or unit_instructions.get(0xa7ea7e) != 'jl 0xa7ea00':
                raise ValueError('H3EK four-slot unit-action updater mismatch')
        if name == 'halo3.dll':
            # Existing contact-melee unit updater, not the intermittent weapon
            # action callback previously used by shake reload.
            source = (Path(__file__).resolve().parents[2] / 'src/dll/halo3_contact_melee_runtime.inl').read_text()
            pattern = re.search(r'\{0x379B30,"([^"]+)"', source).group(1)
            expression = b''.join(b'.' if b == '??' else re.escape(bytes([int(b, 16)])) for b in pattern.split())
            matches = [m.start() for m in re.finditer(b'(?=' + expression + b')', data, re.DOTALL)]
            if matches != [0x379b30]:
                raise ValueError('H3 unit updater is not unique at the proved RVA')
            call = 0x39a5ad
            if data[call] != 0xe8 or call + 5 + struct.unpack_from('<i', data, call + 1)[0] != 0x379b30:
                raise ValueError('H3 native unit-update caller mismatch')
            pointer = struct.unpack_from('<Q', data, 0x8b1d78)[0]
            if pointer != pe.OPTIONAL_HEADER.ImageBase + 0x35f828:
                raise ValueError('H3 existing weapon-update callback table mismatch')
        print(f'{name}: identity, native slot routing and binding evidence verified')

if __name__ == '__main__':
    main()
