# Offline disassembler for halo3.dll (on-disk PE -> RVA addressing).
# Read-only. Never modifies the game file.
#   py -3 pedis.py fn   <rva_hex> [maxbytes]     - disassemble a function
#   py -3 pedis.py scan <off_hex> [off_hex ...]  - find [reg+off] memory operands
#   py -3 pedis.py --module <pe_path> <cmd> ...  - target another module
import sys, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PATH = r"N:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo3\halo3.dll"

# Optional module override; default stays halo3.dll so existing usage is
# unchanged. Must be handled before load() runs at import time below.
if "--module" in sys.argv:
    _i = sys.argv.index("--module")
    PATH = sys.argv[_i + 1]
    del sys.argv[_i:_i + 2]

def load():
    data = open(PATH, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe+4] == b"PE\0\0"
    nsec = struct.unpack_from("<H", data, pe+6)[0]
    optsz = struct.unpack_from("<H", data, pe+20)[0]
    imgbase = struct.unpack_from("<Q", data, pe+24+24)[0]
    secs = []
    off = pe + 24 + optsz
    for i in range(nsec):
        b = data[off:off+40]
        name = b[0:8].rstrip(b"\0").decode(errors="ignore")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", b, 8)
        chars = struct.unpack_from("<I", b, 36)[0]
        secs.append(dict(name=name, va=vaddr, vsize=vsize, raw=rawptr,
                         rawsize=rawsize, **{"exec": bool(chars & 0x20000000)}))
        off += 40
    return data, secs, imgbase

def rva2off(secs, rva):
    for s in secs:
        if s["va"] <= rva < s["va"] + max(s["vsize"], s["rawsize"]):
            d = rva - s["va"]
            if d < s["rawsize"]:
                return s["raw"] + d
    return None

def peek(rvas):
    for rva in rvas:
        off = rva2off(secs, rva)
        if off is None or off + 16 > len(data):
            print("0x%X: not mapped" % rva)
            continue
        raw = data[off:off+16]
        floats = struct.unpack_from("<ffff", raw)
        print("0x%X file=0x%X bytes=%s floats=%s" %
              (rva, off, raw.hex(), ", ".join("%.9g" % v for v in floats)))

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
data, secs, imgbase = load()

def fn(rva, maxb=1400):
    o = rva2off(secs, rva)
    if o is None:
        print("rva not mapped"); return
    code = data[o:o+maxb]
    depth = 0
    for i in md.disasm(code, rva):
        marks = ""
        for op in i.operands:
            if op.type == 3 and op.mem.disp != 0:  # X86_OP_MEM
                marks = "   ; disp=0x%X" % (op.mem.disp & 0xFFFFFFFF)
        print("%08X  %-24s %s %s%s" % (i.address, i.bytes.hex(), i.mnemonic, i.op_str, marks))
        if i.mnemonic == "ret":
            depth += 1
            if depth >= 1 and i.address - rva > 40:
                break

def scan(offsets):
    # capstone's disasm() stops dead at the first undecodable byte; a linear
    # sweep of a real .text hits data islands constantly. Resync by advancing
    # one byte and restarting, or whole sections are silently skipped.
    want = set(offsets)
    hits = 0
    for s in [x for x in secs if x["exec"]]:
        code = data[s["raw"]:s["raw"]+s["rawsize"]]
        print("== section %s rva=%08X size=%X" % (s["name"], s["va"], s["rawsize"]))
        pos = 0
        n = len(code)
        while pos < n:
            last = pos
            for i in md.disasm(code[pos:], s["va"] + pos):
                last = i.address - s["va"] + i.size
                for op in i.operands:
                    if op.type == 3 and op.mem.disp in want and op.mem.base != 0 and op.mem.index == 0:
                        print("%08X  %-20s %s %s" % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
                        hits += 1
            pos = last + 1 if last <= pos else last
    print("== total hits: %d" % hits)

def xref(targets):
    """Find direct branches and RIP-relative memory operands resolving to RVAs."""
    want = set(targets)
    hits = 0
    for s in [x for x in secs if x["exec"]]:
        code = data[s["raw"]:s["raw"]+s["rawsize"]]
        pos = 0
        while pos < len(code):
            last = pos
            for i in md.disasm(code[pos:], s["va"] + pos):
                last = i.address - s["va"] + i.size
                found = False
                for op in i.operands:
                    if op.type == 2 and op.imm in want:
                        found = True
                    elif op.type == 3 and op.mem.base == 41:  # X86_REG_RIP
                        resolved = i.address + i.size + op.mem.disp
                        if resolved in want:
                            found = True
                if found:
                    print("%08X  %-20s %s %s" %
                          (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
                    hits += 1
            pos = last + 1 if last <= pos else last
    print("== total hits: %d" % hits)

def imm(values):
    """Find instructions containing one of the requested immediate values."""
    want = set(values)
    hits = 0
    for s in [x for x in secs if x["exec"]]:
        code = data[s["raw"]:s["raw"]+s["rawsize"]]
        pos = 0
        while pos < len(code):
            last = pos
            for i in md.disasm(code[pos:], s["va"] + pos):
                last = i.address - s["va"] + i.size
                if any(op.type == 2 and op.imm in want for op in i.operands):
                    print("%08X  %-20s %s %s" %
                          (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
                    hits += 1
            pos = last + 1 if last <= pos else last
    print("== total hits: %d" % hits)

def off2rva(secs, off):
    for s in secs:
        if s["raw"] <= off < s["raw"] + s["rawsize"]:
            return s["va"] + (off - s["raw"])
    return None

def runtime(rva):
    """Print the x64 RUNTIME_FUNCTION containing an RVA (offline .pdata)."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    opt = pe + 24
    # PE32+ data directory index 3 = exception directory (.pdata).
    pdata_rva, pdata_size = struct.unpack_from("<II", data, opt + 112 + 3 * 8)
    pdata_off = rva2off(secs, pdata_rva)
    if pdata_off is None:
        print("exception directory not mapped")
        return
    for off in range(pdata_off, pdata_off + pdata_size, 12):
        begin, end, unwind = struct.unpack_from("<III", data, off)
        if begin <= rva < end:
            print("function containing 0x%X: 0x%X..0x%X size=0x%X unwind=0x%X" %
                  (rva, begin, end, end - begin, unwind))
            return
    print("no runtime function contains 0x%X" % rva)

def sig(pattern):
    import re
    pat = b""
    for tok in pattern.split():
        pat += b"." if tok == "??" else re.escape(bytes([int(tok, 16)]))
    hits = [m.start() for m in re.finditer(pat, data, re.DOTALL)]
    print("matches: %d" % len(hits))
    for h in hits:
        print("  file 0x%X  rva 0x%X" % (h, off2rva(secs, h)))

def rtti(text):
    """Trace an MSVC x64 RTTI type name to COLs/vftables and raw references."""
    needle = text.encode("ascii")
    names = []
    pos = 0
    while True:
        pos = data.find(needle, pos)
        if pos < 0:
            break
        names.append(pos)
        pos += 1
    print("name matches: %d" % len(names))
    for name_off in names:
        td_off = name_off - 16
        td_rva = off2rva(secs, td_off)
        print("name file=0x%X rva=0x%X TypeDescriptor rva=0x%X" %
              (name_off, off2rva(secs, name_off), td_rva))
        ref = struct.pack("<I", td_rva)
        p = 0
        while True:
            p = data.find(ref, p)
            if p < 0:
                break
            if p >= 12:
                col_off = p - 12
                col_rva = off2rva(secs, col_off)
                if (col_rva is not None and
                    struct.unpack_from("<I", data, col_off)[0] == 1 and
                    struct.unpack_from("<I", data, col_off + 20)[0] == col_rva):
                    print("  COL rva=0x%X" % col_rva)
                    absolute = struct.pack("<Q", imgbase + col_rva)
                    q = 0
                    while True:
                        q = data.find(absolute, q)
                        if q < 0:
                            break
                        q_rva = off2rva(secs, q)
                        print("    COL pointer rva=0x%X -> vftable rva=0x%X" %
                              (q_rva, q_rva + 8))
                        vftable_va = struct.pack("<Q", imgbase + q_rva + 8)
                        v = 0
                        while True:
                            v = data.find(vftable_va, v)
                            if v < 0:
                                break
                            print("      vftable absolute ref rva=0x%X" % off2rva(secs, v))
                            v += 1
                        q += 1
            p += 1

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "sig":
        sig(sys.argv[2])
    elif cmd == "fn":
        fn(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 1400)
    elif cmd == "scan":
        scan([int(x, 16) for x in sys.argv[2:]])
    elif cmd == "rtti":
        rtti(sys.argv[2])
    elif cmd == "xref":
        xref([int(x, 16) for x in sys.argv[2:]])
    elif cmd == "imm":
        imm([int(x, 16) for x in sys.argv[2:]])
    elif cmd == "runtime":
        runtime(int(sys.argv[2], 16))
    elif cmd == "peek":
        peek([int(x, 16) for x in sys.argv[2:]])
