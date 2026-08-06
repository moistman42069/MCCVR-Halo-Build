# Verify an AOB signature is unique in halo3.dll on disk.
#   py -3 verify_sig.py ["SIG"]
#   py -3 verify_sig.py --module <pe_path> ["SIG"]   - target another module
import re, sys

DLL = r"N:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo3\halo3.dll"
# Optional module override; default stays halo3.dll so existing usage is
# unchanged. Strip it before the positional SIG argument is read.
if "--module" in sys.argv:
    _i = sys.argv.index("--module")
    DLL = sys.argv[_i + 1]
    del sys.argv[_i:_i + 2]
SIG = (sys.argv[1] if len(sys.argv) > 1 else
       "48 89 5C 24 08 57 48 83 EC 30 0F 29 74 24 20 48 8B FA 48 8B D9 48 85 D2 74 ?? F3 0F 10 1D ?? ?? ?? ??")

# Build a byte-regex from the pattern.
pat = b""
for tok in SIG.split():
    pat += b"." if tok == "??" else re.escape(bytes([int(tok, 16)]))

data = open(DLL, "rb").read()
hits = [m.start() for m in re.finditer(pat, data, re.DOTALL)]
print(f"file size {len(data):,} bytes")
print(f"pattern length {len(SIG.split())} bytes")
print(f"matches: {len(hits)}")
for h in hits:
    print(f"  file offset 0x{h:X}")
