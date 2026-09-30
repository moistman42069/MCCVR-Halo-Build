"""Read-only pinned proof for the optional H3 world-avatar transaction."""
from pathlib import Path
import hashlib
import re
import struct
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools/re"))
from pe import PE

checks = 0

def image(name, digest):
    global checks
    p = PE(str(root / "out/deps/re-tools/inputs" / name))
    assert hashlib.sha256(p.blob).hexdigest().upper() == digest, name
    checks += 1
    return p

def unique(p, rva, pattern):
    global checks
    section, text = p.text()
    needle = bytes.fromhex(pattern)
    found = [section["va"] + m.start() for m in re.finditer(re.escape(needle), text)]
    assert found == [rva], (hex(rva), found)
    checks += 1

def call(p, site, target):
    global checks
    at = p.off(site)
    assert p.blob[at] == 0xE8
    assert site + 5 + struct.unpack_from("<i", p.blob, at + 1)[0] == target
    checks += 1

kit = image("halo3_tag_test.exe", "59A78F2C96034D7CEB5D710505B2B36813AA141FC81A083E3F952973DBCE4602")
retail = image("halo3.dll", "B209D8454B12DC77E54CCD2C9924EC8D44B8619D21CF98E36FFAF601E67EFB63")

# Own official kit identity/submission, conversion and exclusion functions.
for rva, pattern in [
    (0x798B80, "48 8B C4 89 48 08 53 55 41 55 41 56 48 81 EC C8 00 00 00 44 0F B6 B4 24"),
    (0x617890, "48 8B C4 55 56 57 41 55 48 81 EC A8 02 00 00 48 8B BC 24 F0 02 00 00 45"),
    (0x7D2DC0, "48 89 5C 24 18 48 89 74 24 20 57 48 81 EC D0 00 00 00 48 8B FA 49 8B D8"),
    (0x8D3B10, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 57 48 83 EC 30 48 8B 7C 24 70 49"),
    (0x8D3F20, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 40 32 F6 8B DA 8B F9 83 FA"),
]:
    unique(kit, rva, pattern)
for site, target in [
    (0x798BBF, 0x8D3F20), (0x798EA5, 0x617890),
    (0x617989, 0x8D3B10), (0x617BF7, 0x7D2A50),
    (0x7D2B49, 0x7D2DC0), (0x7D2F89, 0x7D26E0),
    (0x8D3B9B, 0x4D7C20), (0x8D3BB3, 0xA431F0),
]:
    call(kit, site, target)

# Retail matches are justified by the official chain/layout, not BSim rank.
for rva, pattern in [
    (0x252C24, "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 89 50 10 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 44 8B AC 24 E8 00 00 00 83 CF FF 0F 29 70"),
    (0x20B3BC, "48 8B C4 48 89 58 08 44 89 48 20 55 56 57 41 54 41 55 41 56 41 57 48 81"),
    (0x266838, "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 48 20 55 41 54 41 55 41 56 41 57 B8 20 31 00 00 E8 82 CA 48 00 48 2B E0 48 8D AC 24 A0 00 00"),
    (0x28B0C8, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 30 44 8B 15 B1 EE 7A 00 4D 8B F0 65 48 8B 04 25 58 00 00 00 4D"),
    (0x28CDC0, "48 83 EC 28 44 8B CA 45 33 D2 48 63 D1 41 83 F9 FF 0F 84 83 00 00 00 83"),
    # No-node-map output starts at +44; region count at +2.
    (0x26694E, "49 8D 79 44 0F B7 43 0C 66 41 89 41 02"),
    # Source 34 stride; default inverse at node 60 stride plus28.
    (0x266997, "48 8D 14 49 48 C1 E2 05 48 6B C9 34 48 83 C2 28 49 03 CC 49 03 D7"),
    # Native frame allocation size is palette_count*30 +44.
    (0x20B674, "8D 04 40 41 C1 E0 04 41 83 C0 44"),
]:
    unique(retail, rva, pattern)
for site, target in [
    (0x252C75, 0x28CDC0), (0x252E1E, 0x20B3BC),
    (0x20B586, 0x28B0C8), (0x20B6F6, 0x266838),
    (0x28B17A, 0x1846AC), (0x28CE45, 0x0EE48C),
    # Witness only: 120DF8 is native math and must never be hooked.
    (0x2669AD, 0x120DF8),
]:
    call(retail, site, target)

for rva, pattern in [
    (0x935840, "48 89 54 24 10 48 89 4C 24 08 53 41 55 48 81 EC 88 00 00 00 80 3D E9 DF 71 03 00 48 8B DA 4C 8B"),
    (0x9372F0, "44 88 4C 24 20 48 89 4C 24 08 53 55 57 41 55 41 56 48 81 EC B0 00 00 00 4C 8B EA B9 65 64 6F 6D"),
    (0x9377C0, "44 88 44 24 18 48 89 4C 24 08 53 55 56 57 41 56 41 57 48 81 EC 98 00 00 00 4C 8B F2 B9 65 64 6F"),
    (0x935E60, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 41 08 48 8B F1 B9 65 64 6F 6D 8B 50 04 E8 FD"),
    (0x8ED7B0, "48 89 5C 24 08 57 48 83 EC 20 8D 3C 52 48 8B D9 C1 E7 04 4C 8D 44 24 38 8B D7 33 C9 E8 CF 94 03"),
    (0x926CA0, "40 53 48 83 EC 40 49 8B D8 4C 8D 1D 50 93 6D FF 4C 63 C1 45 33 C9 B8 40 00 00 00 43 8B 8C 83 A0"),
    (0x928890, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B F2 48 63 C1 48 8B 0D 15 D2 65 03 48 8D 15 4E 77"),
]:
    unique(kit, rva, pattern)
for site, target in [(0x9373EB,0x935840),(0x9378AD,0x935840),
                     (0x935F32,0x8ED7B0),(0x8ED7CC,0x926CA0),(0x8ED7EE,0x928890)]:
    call(kit,site,target)

# Keep every actually installed entry signature checked, including the two
# outer region scopes which restore our temporary count/mesh changes.
source=(root/'src/dll/halo3_avatar.inl').read_text()
bindings=re.findall(r'\{(0x[0-9A-F]+),"([0-9A-F ]+)"\}',source)
assert len(bindings)==9
checks+=1
for rva,pattern in bindings:
    unique(retail,int(rva,16),pattern)
for site,target in [(0x2B38D2,0x2B32C0),(0x2B3D9C,0x2B32C0),
                    (0x2B550D,0x2AFB88),(0x2B5533,0x2AFC14),
                    (0x276E47,0x28ADDC),(0x28AE53,0x2C0D20),
                    (0x2C0F39,0x2C5A38),(0x2C0FCD,0x2C5A38),
                    (0x2C12ED,0x2C5A38),(0x2C5A74,0x2C561C)]:
    call(retail,site,target)

print(f"H3 avatar evidence: {checks} pinned witnesses pass; headset validation remains required")
