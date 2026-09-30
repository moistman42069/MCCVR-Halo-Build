"""Verify all six native action-bridge contracts against pinned official kits/retail.

Read-only. Requires pefile; no game process or game installation is accessed.
The kit establishes behavior; the retail image only matches those contracts.
"""
import hashlib
from pathlib import Path
import re
import struct
import pefile

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ROOT / 'out/deps/re-tools/inputs'
PINS = {
    'reach_tag_test.exe': 'CBDD8448A87A433B0DFFC0DE47D06DB7A18B4BF868B96B057135DAA86790ABA8',
    'haloreach.dll': '738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894',
}
images = {}
for name, expected in PINS.items():
    raw = (INPUTS / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == expected, name
    images[name] = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
kit, retail = images['reach_tag_test.exe'], images['haloreach.dll']
checks = 2


def call(image, site, target):
    global checks
    assert image[site] == 0xE8 and site + 5 + struct.unpack_from('<i', image, site + 1)[0] == target, hex(site)
    checks += 1


# Both official input wrappers call the same converter, whose second argument
# is elapsed milliseconds and whose output contains digital frame/ms counters.
call(kit, 0x25110D, 0x2511F0)
call(kit, 0x2511DB, 0x2511F0)
call(kit, 0xC2CCA, 0x251150)
call(kit, 0xC2D34, 0x2510A0)
# Native builder keeps generic use and dedicated reload separate.
for site in (0x1E592A, 0x1E598D, 0x1E5B4E, 0x1E5B86):
    call(kit, site, 0x1D6B70)
assert kit[0x1E5920:0x1E5925] == bytes.fromhex('BA 03 00 00 00')
assert kit[0x1E5B44:0x1E5B49] == bytes.fromhex('BA 3E 00 00 00')
assert kit[0x1E5B7C:0x1E5B81] == bytes.fromhex('BA 3E 00 00 00')
# The native consumer accepts owner 0x7f, not H3's one-byte 0xff sentinel.
consumer = kit[0x1E5B4E:0x1E5BBC]
assert bytes.fromhex('8B 48 0C 83 F9 7F') in consumer
assert bytes.fromhex('00 00 01 00') in consumer  # reload-down packet flag
assert bytes.fromhex('00 00 02 00') in consumer  # reload-held packet flag
checks += 6

source = (ROOT / 'src/dll/native_vr_actions.cpp').read_text()


def pattern(name):
    definition = re.search(r'constexpr const char\* ' + name + r'\s*=\s*((?:"[^"]*"\s*)+);', source)
    assert definition, name
    return ' '.join(re.findall(r'"([^"]*)"', definition[1]))


def unique_at(pattern_text, expected):
    global checks
    atoms = pattern_text.split()
    expression = b''.join(b'.' if value == '??' else re.escape(bytes([int(value, 16)])) for value in atoms)
    hits = [match.start() for match in re.finditer(expression, retail, re.DOTALL)]
    assert hits == [expected], (hex(expected), hits)
    checks += 1


for name, rva in [('converterPattern', 0xABDBC), ('padProofPattern', 0x23D00), ('controllerLoopPattern', 0x23E72),
                  ('consumerEntryPattern', 0x5DF80), ('consumerReloadPattern', 0x60C85)]:
    unique_at(pattern(name), rva)
reader = (ROOT / 'src/dll/gesture_binding_reader.inl').read_text()
descriptor = re.search(r'\{GameTitle::HaloReach,[^\n]+,\s*0xD1070,0x62C2D,\s*"([^"]+)",\s*"([^"]+)"', reader)
assert descriptor
unique_at(descriptor[1], 0xD1070)
unique_at(descriptor[2], 0x62C2D)
call(retail, 0x23D87, 0xABDBC)
call(retail, 0x23DF6, 0xABDBC)
call(retail, 0x60C8F, 0xD1070)
call(retail, 0x60CAF, 0xD1070)
pe = pefile.PE(str(INPUTS / 'haloreach.dll'))
assert any(entry.struct.BeginAddress == 0x5DF80 and entry.struct.EndAddress == 0x60F12
           for entry in pe.DIRECTORY_ENTRY_EXCEPTION)
checks += 1
pad_base = 0x23D0E + struct.unpack_from('<i', retail, 0x23D0A)[0]
assert pad_base == 0x2DFC224
# Copied and direct branches converge on the same 0x3c-stride output rows.
assert retail[0x23DA0:0x23DB1] == bytes.fromhex('4C 63 E3 4D 8D B2 B4 03 00 00 49 6B C4 3C 4C 03 F0')
raw_input_base = 0x23CD1 + struct.unpack_from('<i', retail, 0x23CCD)[0]
assert raw_input_base + 0x3B4 == pad_base
abstract_base = 0x62C3B + struct.unpack_from('<i', retail, 0x62C37)[0]
assert abstract_base == 0x287FF94
mask_table = struct.pack('<14I', 1, 2, 4, 8, 16, 32, 64, 128, 4096, 8192, 16384, 32768, 256, 512)
assert retail[0x9D7470:0x9D7470 + len(mask_table)] == mask_table
checks += 5
for name, expected in {
    'halo_tag_test.exe': 'FC9E2B6193C6F6D9FF988278B0D39A983747F3FDBDECA7CAFADD28F1F0C53E73',
    'halo1.dll': '0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C',
}.items():
    raw = (INPUTS / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == expected, name
    images[name] = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
    checks += 1
kit, retail = images['halo_tag_test.exe'], images['halo1.dll']
source = (ROOT / 'src/dll/native_vr_actions_ce.inl').read_text()
for name, rva in [('converterPattern', 0xC0E330), ('consumerPattern', 0xA97CF4),
                  ('getterPattern', 0xADC640), ('stateProofPattern', 0xA97E04),
                  ('callerPattern', 0xA991D8)]:
    unique_at(pattern(name), rva)
# The kit explains the four-argument converter and distinct CE array state.
call(kit, 0x1C6441, 0x1C6660)
call(kit, 0x1C654B, 0x1C6660)
call(kit, 0x1C681B, 0x1C01E0)  # controller-index preference copy
call(kit, 0x18DD36, 0x1C0180)  # player consumer obtains the state
call(retail, 0xC0E4FF, 0xADC5A4)  # same preserved controller in ESI
call(retail, 0xA991EF, 0xA97CF4)
assert retail[0xA991E7:0xA991EF] == bytes.fromhex('0F 28 D3 41 8B D0 8B CB')
assert 0xADC64A + struct.unpack_from('<i', retail, 0xADC646)[0] == 0x2EA2890
# Inlined native consumer uses image-base-relative, not RIP-to-state addressing.
assert 0xA97E1A + struct.unpack_from('<i', retail, 0xA97E16)[0] == 0
assert retail[0xA97E1A:0xA97E21] == bytes.fromhex('48 8D 80 90 28 EA 02')
assert retail[0xA98A37:0xA98A3C] == bytes.fromhex('42 8A 44 2A 01')
assert retail[0xA98A62:0xA98A68] == bytes.fromhex('41 83 F8 19 7C C9')
pe = pefile.PE(str(INPUTS / 'halo1.dll'))
assert any(entry.struct.BeginAddress == 0xA97CF4 and entry.struct.EndAddress == 0xA99159
           for entry in pe.DIRECTORY_ENTRY_EXCEPTION)
checks += 7

# H3's UI glyph aliases are not its actual player-control consumer identities.
for name, expected in {
    'halo3_tag_test.exe': '59A78F2C96034D7CEB5D710505B2B36813AA141FC81A083E3F952973DBCE4602',
    'halo3.dll': 'B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63',
}.items():
    raw = (INPUTS / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == expected, name
    images[name] = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
    checks += 1
kit, retail = images['halo3_tag_test.exe'], images['halo3.dll']
source = (ROOT / 'src/dll/native_vr_actions_h3.inl').read_text()
for name, rva in [('converterPattern', 0x172434), ('consumerEntryPattern', 0xF42D8),
                  ('consumerReloadPattern', 0xF6231), ('controllerProofPattern', 0x1725EB)]:
    unique_at(pattern(name), rva)
descriptor = re.search(r'\{GameTitle::Halo3,[^\n]+,\s*0x187028,0xF751A,\s*"([^"]+)",\s*"([^"]+)"', reader)
assert descriptor
unique_at(descriptor[1], 0x187028)
unique_at(descriptor[2], 0xF751A)
call(kit, 0x4FC654, 0x4FC7B0)
call(kit, 0x4FC707, 0x4FC7B0)
call(kit, 0x4FC99E, 0x4DA510)  # real controller selects native preference row
call(kit, 0x44B246, 0x4D9BC0)
call(kit, 0x44B284, 0x4D9BC0)
assert kit[0x44B241:0x44B246] == bytes.fromhex('BA 26 00 00 00')
assert kit[0x44B26A:0x44B26E] == bytes.fromhex('0F BA E9 0C')  # reload held bit12
assert kit[0x341DEF:0x341DF3] == bytes.fromhex('0F BA E8 0E')  # edge becomes bit14
assert kit[0x3442FA:0x344302] == bytes.fromhex('48 81 4E 08 00 00 00 40')  # primary reload control
call(kit, 0xA6AA71, 0xA7E8D0)
assert kit[0xA6AA6A:0xA6AA6F] == bytes.fromhex('BA 3D 00 00 00')
call(retail, 0xA7CB5, 0x172434)  # MCC copied input
call(retail, 0xA7D16, 0x172434)  # native XInput route
call(retail, 0xF6236, 0x187028)
call(retail, 0xF625F, 0x187028)
assert retail[0x17246E:0x172471] == bytes.fromhex('4C 63 F1')
assert retail[0x1725EB:0x1725F2] == bytes.fromhex('49 69 CE 1C 02 00 00')
assert 0xF7528 + struct.unpack_from('<i', retail, 0xF7524)[0] == 0x2229B54
pe = pefile.PE(str(INPUTS / 'halo3.dll'), fast_load=True)
pdata = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
assert any(struct.unpack_from('<II', retail, offset) == (0xF42D8, 0xF6487)
           for offset in range(pdata.VirtualAddress, pdata.VirtualAddress + pdata.Size, 12))
checks += 9

# ODST independently uses 0x2a, not H3's 0x26.
for name, expected in {
    'halo3odst_tag_test.exe': '354EC94158AECCE3E9D0F6463023AD5FA6D2AFE49B390E6067EBC17465C63C2D',
    'halo3odst.dll': '5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A',
}.items():
    raw = (INPUTS / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == expected, name
    images[name] = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
    checks += 1
kit, retail = images['halo3odst_tag_test.exe'], images['halo3odst.dll']
source = (ROOT / 'src/dll/native_vr_actions_odst.inl').read_text()
for name, rva in [('converterPattern', 0x1A0E30), ('consumerEntryPattern', 0x10FBFC),
                  ('consumerReloadPattern', 0x111CBF), ('controllerProofPattern', 0x1A0FE7)]:
    unique_at(pattern(name), rva)
descriptor = re.search(r'\{GameTitle::Halo3ODST,[^\n]+,\s*0x1B8420,0xBFC81,\s*"([^"]+)",\s*"([^"]+)"', reader)
assert descriptor
unique_at(descriptor[1], 0x1B8420)
unique_at(descriptor[2], 0xBFC81)
call(kit, 0x554934, 0x554A90)
call(kit, 0x5549E7, 0x554A90)
call(kit, 0x554C7E, 0x52E810)
call(kit, 0x4A9093, 0x52DE30)
call(kit, 0x4A90D1, 0x52DE30)
assert kit[0x4A908E:0x4A9093] == bytes.fromhex('BA 2A 00 00 00')
assert bytes.fromhex('0F BA E9 0C') in kit[0x4A9098:0x4A90D1]
assert kit[0x365B64:0x365B68] == bytes.fromhex('0F BA E8 0E')
assert kit[0x368524:0x36852C] == bytes.fromhex('48 81 4E 08 00 00 00 40')
call(kit, 0x371419, 0x3659A0)
call(kit, 0x371442, 0x368170)
call(retail, 0xB4B6D, 0x1A0E30)
call(retail, 0xB4BCE, 0x1A0E30)
call(retail, 0x1A0FEE, 0x1B9890)
call(retail, 0x111CC7, 0x1B8420)
call(retail, 0x111CF0, 0x1B8420)
assert retail[0x1A0E6A:0x1A0E6C] == bytes.fromhex('8B E9')
assert retail[0x1A0FEC:0x1A0FEE] == bytes.fromhex('8B CD')
assert 0xBFC8F + struct.unpack_from('<i', retail, 0xBFC8B)[0] == 0x227AA54
pe = pefile.PE(str(INPUTS / 'halo3odst.dll'), fast_load=True)
pdata = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
assert any(struct.unpack_from('<II', retail, offset) == (0x10FBFC, 0x111F5F)
           for offset in range(pdata.VirtualAddress, pdata.VirtualAddress + pdata.Size, 12))
checks += 8

# H4's own consumer/action pipeline proves 0x31 reload independently of Use 2.
for name, expected in {
    'halo4_tag_test.exe': 'B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6',
    'halo4.dll': '7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84',
}.items():
    raw = (INPUTS / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == expected, name
    images[name] = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
    checks += 1
kit, retail = images['halo4_tag_test.exe'], images['halo4.dll']
source = (ROOT / 'src/dll/native_vr_actions_h4.inl').read_text()
for name, rva in [('converterPattern', 0x1201A8), ('consumerEntryPattern', 0xA2670),
                  ('consumerReloadPattern', 0xA5264), ('padProofPattern', 0x56E79),
                  ('controllerLoopPattern', 0x56FE0)]:
    unique_at(pattern(name), rva)
descriptor = re.search(r'\{GameTitle::Halo4,[^\n]+,\s*0x13F5CC,0xA2818,\s*"([^"]+)",\s*"([^"]+)"', reader)
assert descriptor
unique_at(descriptor[1], 0x13F5CC)
unique_at(descriptor[2], 0xA2818)
call(kit, 0x342D7D, 0x342E60)
call(kit, 0x342E50, 0x342E60)
call(kit, 0xCD136, 0x342D10)
call(kit, 0xCD0C3, 0x342DC0)
call(kit, 0x226B5F, 0x1FFED0)
call(kit, 0x226B9D, 0x1FFED0)
assert kit[0x226B55:0x226B5A] == bytes.fromhex('BA 31 00 00 00')
assert kit[0x226B86:0x226B8B] == bytes.fromhex('BA 12 00 00 00')
assert kit[0x226B6D:0x226B73] == bytes.fromhex('8B 48 0C 83 F9 7F')
# Control state input flags +0x10 -> player-action +0x40, then native edges.
assert kit[0x2223B9:0x2223C1] == bytes.fromhex('48 8B 47 10 48 89 43 40')
assert kit[0x1C9575:0x1C957A] == bytes.fromhex('48 0F BA E9 14')
assert kit[0x1CEB81:0x1CEB89] == bytes.fromhex('41 B0 01 BA 2C 00 00 00')
call(kit, 0x1CEB8D, 0x1DD5D0)
call(kit, 0xE8E204, 0xF3F070)
assert kit[0xE8E1FD:0xE8E202] == bytes.fromhex('BA 46 00 00 00')
call(retail, 0x56EED, 0x1201A8)
call(retail, 0x56F62, 0x1201A8)
call(retail, 0xA526C, 0x13F5CC)
call(retail, 0xA5291, 0x13F5CC)
assert 0xA2826 + struct.unpack_from('<i', retail, 0xA2822)[0] == 0x2E959D4
assert 0x56E87 + struct.unpack_from('<i', retail, 0x56E83)[0] == 0x28ACA74
assert 0x56E45 + struct.unpack_from('<i', retail, 0x56E41)[0] + 0x3B4 == 0x28ACA74
assert retail[0x56F09:0x56F17] == bytes.fromhex('4C 63 E3 4D 8D B2 B4 03 00 00 49 6B C4 3C')
assert retail[0xA15FC:0xA1602] == bytes.fromhex('8B 51 0C 83 FA 7F')
pe = pefile.PE(str(INPUTS / 'halo4.dll'), fast_load=True)
pdata = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
assert any(struct.unpack_from('<II', retail, offset) == (0xA2670, 0xA55EE)
           for offset in range(pdata.VirtualAddress, pdata.VirtualAddress + pdata.Size, 12))
checks += 13

# H2 owns a different ABI: six-argument abstraction updater and packed copier.
for name, expected in {
    'halo2_tag_test.exe': 'D0B71186D3948C48DDD02E2CCB88FA13E77E25A3D8F7FA60922F23A2A0073E36',
    'halo2.dll': 'DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946',
}.items():
    raw = (INPUTS / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper() == expected, name
    images[name] = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
    checks += 1
kit, retail = images['halo2_tag_test.exe'], images['halo2.dll']
source = (ROOT / 'src/dll/native_vr_actions_h2.inl').read_text()
for name, rva in [('converterPattern', 0x6D8AB0), ('copierPattern', 0x6D9C40),
                  ('elapsedPattern', 0x6D0C60), ('cadenceProofPattern', 0x6D9128),
                  ('consumerEntryPattern', 0x6C0E30), ('callerPattern', 0x6C0F3B),
                  ('consumerReloadPattern', 0x6C36BF)]:
    unique_at(pattern(name), rva)
call(kit, 0xA02AC, 0x9AE40)
call(kit, 0x9B2D8, 0x48700)
call(kit, 0x9B2FA, 0x49C50)
call(kit, 0x72E01, 0x9BEC0)
call(kit, 0x72E29, 0x9BEC0)
# Native copier owns a C0-byte row selected by its controller argument.
assert kit[0x9BF46:0x9BF57] == bytes.fromhex('8D 34 76 B9 30 00 00 00 C1 E6 06 81 C6 48 FF FF 00')
call(kit, 0x7310E, 0x9F810)
call(kit, 0x74917, 0x9F810)
call(kit, 0x748E8, 0x9F7D0)
call(kit, 0x74C62, 0x9F7D0)
# Context selectors return primary 0A/16/18 and secondary17/19, as documented.
assert all(bytes.fromhex(value) in kit[0x9F7D0:0x9F840] for value in
           ['B8 0A 00 00 00','B8 16 00 00 00','B8 18 00 00 00','B8 17 00 00 00','B8 19 00 00 00'])
call(retail, 0x6E0E9E, 0x6D8AB0)
call(retail, 0x6D9128, 0x6D0C60)
call(retail, 0x6D913F, 0x6D1E30)
call(retail, 0x6C0F42, 0x6D9C40)
assert 0x6D9C52 + struct.unpack_from('<i', retail, 0x6D9C4E)[0] == 0x15F1E30
assert 0x6D0C66 + struct.unpack_from('<i', retail, 0x6D0C62)[0] == 0x15EA688
assert retail[0x6E0BAD:0x6E0BB1] == bytes.fromhex('4C 8D 34 7F')
assert retail[0x6E0BB9:0x6E0BBD] == bytes.fromhex('49 C1 E6 06')
assert retail[0x6E0E88:0x6E0E8D] == bytes.fromhex('4C 89 74 24 28')
pe = pefile.PE(str(INPUTS / 'halo2.dll'), fast_load=True)
pdata = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
assert any(struct.unpack_from('<II', retail, offset) == (0x6C0E30, 0x6C10F0)
           for offset in range(pdata.VirtualAddress, pdata.VirtualAddress + pdata.Size, 12))
checks += 8
print(f'native VR action evidence: {checks} pinned kit/retail checks passed; all six titles')
