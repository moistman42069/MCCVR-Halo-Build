"""Read-only H4EK-to-pinned-retail weapon haptic source and native envelope proof."""
from pathlib import Path
import hashlib,re,struct
import pefile
ROOT=Path(__file__).resolve().parents[1]
PINS={'halo4_tag_test.exe':'B7468DB9FD160B035C329540EE0B0D47BCF609E1BA6E85AE4F204B70661113A6',
      'halo4.dll':'7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84'}
images={};checks=0
for name,pin in PINS.items():
    raw=(ROOT/'out/deps/re-tools/inputs'/name).read_bytes()
    assert hashlib.sha256(raw).hexdigest().upper()==pin,name
    images[name]=pefile.PE(data=raw,fast_load=True).get_memory_mapped_image();checks+=1
kit=images['halo4_tag_test.exe'];retail=images['halo4.dll']
def call(image,site,target):
    global checks
    assert image[site]==0xE8 and site+5+struct.unpack_from('<i',image,site+1)[0]==target,hex(site)
    checks+=1
def contains(image,start,end,pattern):
    global checks
    assert bytes.fromhex(pattern) in image[start:end],(hex(start),pattern)
    checks+=1
# Own authored barrel firing response, distinct owner and passenger branches.
for site,target in [(0xE9DA88,0xE9DD50),(0xE9DDE0,0xE78230),(0xE9DE99,0xE78230),(0xE783FF,0x497F20)]:call(kit,site,target)
for site,target in [(0x61539E,0x6161DC),(0x616232,0x603DE0),(0x616324,0x603DE0),(0x1E69F0,0x145E80),(0x145D3B,0x1E7B64),(0x145D72,0x1460C8),(0x146194,0x234E08),(0x14619F,0x235460)]:call(retail,site,target)
# Fire admission guards this response. Empty-fire follows a separate path.
contains(retail,0x615390,0x6153A3,'45 84 ED 74 0E')
# Enqueue ABI: scalar arrives XMM2, slot stride0x34, eight slots, age+1B0.
contains(retail,0x145E80,0x145E96,'0F 28 DA')
contains(retail,0x145E80,0x145FB2,'48 6B C8 34')
contains(retail,0x145E80,0x145FB2,'41 83 A4 80 B0 01 00 00 00')
contains(retail,0x145E80,0x145FB2,'F3 42 0F 11 5C 01 04')
# Observed user-zero evaluation: own TLS2D0 queue bank, per-user stride1D8.
contains(retail,0x145CE3,0x145CFA,'B8 D0 02 00 00')
contains(retail,0x145CE3,0x145CFA,'49 69 DE D8 01 00 00')
# Native source attenuation +28; native authored duration and mapping ABI.
contains(retail,0x146163,0x1461C8,'F3 44 0F 59 47 2C')
contains(retail,0x146163,0x1461C8,'0F 28 C8 48 8B CE E8')
# Tag table and segment bank references used by the native evaluator itself.
assert 0x146106+struct.unpack_from('<i',retail,0x146102)[0]==0x107C0B0;checks+=1
assert 0x146149+struct.unpack_from('<i',retail,0x146145)[0]==0x496A180;checks+=1
source=(ROOT/'src/dll/halo4_weapon_haptics.inl').read_text(encoding='utf-8')
for rva,pattern in re.findall(r'\{(0x[0-9A-Fa-f]+),"([0-9A-Fa-f ]+)"\}',source):
    raw=bytes.fromhex(pattern);assert retail.count(raw)==1 and retail.find(raw)==int(rva,16),rva;checks+=1
print(f'{checks} Halo 4 weapon haptics pinned checks passed')
