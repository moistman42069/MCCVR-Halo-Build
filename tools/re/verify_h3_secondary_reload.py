"""Read-only H3EK/retail proof for secondary reload's input consumer aliases.
Requires pefile. No process is opened and no image is modified.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import pefile

parser = argparse.ArgumentParser()
parser.add_argument('--kit', type=Path, required=True)
parser.add_argument('--retail', type=Path, required=True)
args = parser.parse_args()
images = []
for path, expected in [(args.kit, '59a78f2c96034d7ceb5d710505b2b36813aa141fc81a083e3f952973dbce4602'),
                       (args.retail, 'b209d8454b12dc77e54ccd2c9924ec8d44b8619d21cf98e36ffaf601e67efb63')]:
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == expected, path
    images.append(pefile.PE(data=raw).get_memory_mapped_image())
kit, retail = images

def call(image, site, target):
    assert image[site] == 0xE8
    assert site + 5 + struct.unpack_from('<i', image, site + 1)[0] == target

for site in (0x44B0F0, 0x44B119, 0x44B14C, 0x44B17D):
    call(kit, site, 0x4D9BC0)
for site, action in [(0x44B0EB, 4), (0x44B114, 0x42), (0x44B147, 4), (0x44B178, 0x1F)]:
    assert kit[site:site+5] == b'\xba' + struct.pack('<I', action)
# Aliases feed held bit4, including promotion of long-held bit5. The native
# edge resolver derives bit6; the unit-action path requests secondary reload.
assert kit[0x44B138:0x44B13C] == bytes.fromhex('83 4E 2C 10')
assert kit[0x44B1B5:0x44B1B8] == bytes.fromhex('83 C8 10')
assert kit[0x341D67:0x341D6A] == bytes.fromhex('83 C8 40')
assert kit[0x344309:0x34430D] == bytes.fromhex('F6 46 40 40')
assert kit[0x344330:0x344339] == bytes.fromhex('B8 00 00 00 80 48 09 46 08')
for site in (0xF6154, 0xF616D, 0xF6191, 0xF61AD):
    call(retail, site, 0x187028)
source = (Path(__file__).resolve().parents[2] / 'src/dll/native_vr_actions_h3.inl').read_text()
pattern = bytes.fromhex(re.search(r'secondaryReloadPattern="([0-9A-F ]+)"', source)[1])
assert retail.count(pattern) == 1 and retail.find(pattern) == 0xF614F
# Pickup keeps long-held bit5; secondary reload uses the distinct pressed bit6.
assert kit[0x344157:0x34415B] == bytes.fromhex('F6 46 40 20')
call(kit, 0x344165, 0x346370)
# Kit's named assertion proves the B8 output-user -> player table, not a unit.
expression = struct.unpack_from('<Q', kit, 0x105A6F0)[0] - 0x140000000
assert kit[expression:expression+150].split(b'\0')[0] == b'player_mapping_globals->output_user_player_mapping[output_user_index]==NONE'
assert kit[0x3E9DBF:0x3E9DC7] == bytes.fromhex('41 89 B4 1F B8 00 00 00')
call(kit, 0x34D138, 0x341C20)
call(retail, 0xE4CDF, 0xE7C68)
for name, rva in [('actionEdgesPattern',0xE7C68),('localPlayerPattern',0xEE45C),
                  ('edgeCallerPattern',0xE4CD7),('edgeFlagsPattern',0xE7D8B)]:
    proof = bytes.fromhex(re.search(name+r'="([0-9A-F ]+)"', source)[1])
    assert retail.count(proof)==1 and retail.find(proof)==rva, name
print('H3 secondary reload: local ownership, pickup/reload separation and unique edge-hook proofs passed')
