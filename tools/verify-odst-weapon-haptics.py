"""Read-only own ODSTEK-to-pinned-retail authored recoil verification."""
from pathlib import Path
import hashlib,re,struct
import pefile
ROOT=Path(__file__).resolve().parents[1]
PINS={'halo3odst_tag_test.exe':'354EC94158AECCE3E9D0F6463023AD5FA6D2AFE49B390E6067EBC17465C63C2D',
      'halo3odst.dll':'5BB20976EFDFD9E1CE59C589339804725FEC239021027C8D65B2733EAB94829A'}
images={};checks=0
for name,pin in PINS.items():
    raw=(ROOT/'out/deps/re-tools/inputs'/name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper()==pin,name
    images[name]=pefile.PE(data=raw,fast_load=True).get_memory_mapped_image();checks+=1
kit=images['halo3odst_tag_test.exe'];retail=images['halo3odst.dll']
def call(image,site,target):
    global checks
    assert image[site]==0xE8 and site+5+struct.unpack_from('<i',image,site+1)[0]==target,hex(site)
    checks+=1
def contains(image,start,end,pattern):
    global checks
    assert bytes.fromhex(pattern) in image[start:end],(hex(start),pattern)
    checks+=1
# Own admitted barrel response: owner and passenger are distinct exact calls.
for site,target in [(0xB11F29,0xAD7C10),(0xB11FCC,0xAD7C10),(0xAD7D86,0x6176B0),
                    (0x6176F0,0x617700),(0x617AFC,0x617B10),(0x617D01,0x53E8B0),
                    (0x53EC5D,0x53E0A0)]:call(kit,site,target)
for site,target in [(0x3AD8ED,0x3A1464),(0x3AD9B8,0x3A1464),(0x3A15FC,0x1E3B98),
                    (0x1E3CD6,0x1E3D40),(0x1E3D1B,0x1E3D40),(0x1BE11D,0x1BE2B0),
                    (0x1BE39F,0x27B458),(0x1E3FED,0x1E4BE8),(0x1E4002,0x1E4FC4)]:call(retail,site,target)
# Barrel successful-admission gate skips all own/passenger response effects.
contains(retail,0x3AD4B0,0x3AD4B9,'40 84 ED 0F 84 13 06 00 00')
# Exact scope ABI: uint32 weapon, int16 barrel, byte predicted.
contains(retail,0x3ACCC8,0x3ACD00,'66 89 50 10')
contains(retail,0x3ACCC8,0x3ACD00,'45 8A E0')
# Inline native allocator: TLS2B0, user98, eight12-byte entries and ages70.
contains(retail,0x1E3F6E,0x1E3F86,'4D 69 C2 98 00 00 00 B8 B0 02 00 00')
contains(retail,0x1E3F90,0x1E3FB6,'83 F9 08')
contains(retail,0x1E3FB6,0x1E3FCF,'4B 8D 04 52')
# Full datum stored in native entry; never mask its identity to low16.
contains(retail,0x1E3FBA,0x1E3FCF,'45 89 24 80')
contains(retail,0x1E3FBA,0x1E3FCF,'47 89 4C 90 70')
# Native evaluator tag INDEX uses low16; row is independent signed32.
contains(retail,0x1BE329,0x1BE33E,'48 63 4F FC')
contains(retail,0x1BE336,0x1BE33E,'0F B7 47 F8')
contains(retail,0x1BE363,0x1BE375,'49 C1 E6 06 49 83 C6 5C')
# Each band is evaluated only while age<duration, allowing exact restoration of
# verified expired records, never replacement/restoration of active voices.
contains(retail,0x1BE375,0x1BE381,'41 0F 2F 04 24 76 33')
contains(retail,0x1BE3B4,0x1BE3C4,'49 83 C6 18')
# Own native NONE initializer: tag and row -1, scalar1; evaluator skips NONE.
contains(retail,0x1BE063,0x1BE07D,'41 83 C8 FF')
contains(retail,0x1BE06F,0x1BE07D,'44 89 40 F8 44 89 40 FC C7 00 00 00 80 3F')
contains(retail,0x1BE31F,0x1BE329,'83 7F F8 FF 0F 84 9B 00 00 00')
# Native own evaluator's table and tag-base slots, no borrowed title globals.
assert 0x1BE2EC+struct.unpack_from('<i',retail,0x1BE2E8)[0]==0x2022AA8;checks+=1
assert 0x1BE2F9+struct.unpack_from('<i',retail,0x1BE2F5)[0]==0xA9F0A8;checks+=1
source=(ROOT/'src/dll/odst_weapon_haptics.inl').read_text(encoding='utf-8')
for rva,pattern in re.findall(r'\{(0x[0-9A-Fa-f]+),"([0-9A-Fa-f ]+)"\}',source):
    raw=bytes.fromhex(pattern);assert retail.count(raw)==1 and retail.find(raw)==int(rva,16),rva;checks+=1
print(f'{checks} ODST weapon haptics pinned checks passed')
