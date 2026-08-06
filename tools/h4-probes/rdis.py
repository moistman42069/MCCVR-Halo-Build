# Offline disassembler + RUNTIME_FUNCTION lookup for any MCC title DLL.
#   py -3 rdis.py <dll> fn   <rva_hex> [maxbytes]
#   py -3 rdis.py <dll> at   <rva_hex> [before] [after]
#   py -3 rdis.py <dll> find <rva_hex>            - enclosing function bounds
#   py -3 rdis.py <dll> disp <offset_hex>          - memory operands using offset
#   py -3 rdis.py <dll> xref <rva_hex>             - direct/RIP-relative refs
import sys, struct, hashlib
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
import petool as P

def pdata(data, secs):
    """Return sorted list of (begin_rva, end_rva, unwind_rva)."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    # IMAGE_DIRECTORY_ENTRY_EXCEPTION == 3
    ddoff = pe + 24 + 112  # PE32+ data directory start
    va, sz = struct.unpack_from("<II", data, ddoff + 3 * 8)
    fo = P.rva2f(secs, va)
    out = []
    for i in range(sz // 12):
        b, e, u = struct.unpack_from("<III", data, fo + i * 12)
        if b == 0 and e == 0:
            break
        out.append((b, e, u))
    return out

def enclosing(pd, rva):
    lo, hi = 0, len(pd) - 1
    while lo <= hi:
        m = (lo + hi) // 2
        b, e, u = pd[m]
        if rva < b:
            hi = m - 1
        elif rva >= e:
            lo = m + 1
        else:
            return b, e
    return None, None

def dis(data, secs, rva, length, mark=None):
    fo = P.rva2f(secs, rva)
    code = data[fo:fo+length]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = False
    for ins in md.disasm(code, rva):
        flag = ""
        if mark is not None and ins.address <= mark < ins.address + ins.size:
            flag = "   <=== "
        print("  %08X  %-24s %-8s %s%s" % (
            ins.address, ins.bytes.hex(" ").upper(), ins.mnemonic, ins.op_str, flag))

def executable_instructions(data, secs):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    for name, rva, virtual_size, raw, raw_size in secs:
        if name != ".text":
            continue
        code = data[raw:raw + raw_size]
        pos = 0
        while pos < len(code):
            decoded = list(md.disasm(code[pos:pos + 0x10000], rva + pos))
            if not decoded:
                pos += 1
                continue
            yield from decoded
            end = decoded[-1].address + decoded[-1].size - rva
            pos = end if end > pos else pos + 1

def scan_displacement(data, secs, displacement):
    hits = 0
    for ins in executable_instructions(data, secs):
        if any(op.type == X86_OP_MEM and op.mem.disp == displacement
               for op in ins.operands):
            print("  %08X  %-24s %-8s %s" % (
                ins.address, ins.bytes.hex(" ").upper(), ins.mnemonic,
                ins.op_str))
            hits += 1
    print("total references: %d" % hits)

def scan_xrefs(data, secs, target):
    hits = 0
    for ins in executable_instructions(data, secs):
        resolved = []
        for op in ins.operands:
            if op.type == X86_OP_IMM:
                resolved.append(op.imm)
            elif op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                resolved.append(ins.address + ins.size + op.mem.disp)
        if target in resolved:
            print("  %08X  %-24s %-8s %s" % (
                ins.address, ins.bytes.hex(" ").upper(), ins.mnemonic,
                ins.op_str))
            hits += 1
    print("total references: %d" % hits)

if __name__ == "__main__":
    path, cmd, arg = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    data, secs, ib = P.load(path)
    pd = pdata(data, secs)
    if cmd == "find":
        b, e = enclosing(pd, arg)
        print("function 0x%X - 0x%X (size 0x%X)" % (b, e, e - b) if b else "not found")
    elif cmd == "fn":
        b, e = enclosing(pd, arg)
        if not b:
            b, e = arg, arg + 0x200
        n = int(sys.argv[4], 16) if len(sys.argv) > 4 else (e - b)
        fo = P.rva2f(secs, b)
        print("function 0x%X - 0x%X size 0x%X  bodySHA256 %s" % (
            b, e, e - b, hashlib.sha256(data[fo:fo+(e-b)]).hexdigest().upper()))
        dis(data, secs, b, min(n, e - b), mark=arg)
    elif cmd == "at":
        before = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x40
        after = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0x40
        dis(data, secs, arg - before, before + after, mark=arg)
    elif cmd == "disp":
        scan_displacement(data, secs, arg)
    elif cmd == "xref":
        scan_xrefs(data, secs, arg)
