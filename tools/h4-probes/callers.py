# Find direct callers of a function (E8/E9 rel32) in a PE, and show how the
# first argument (rcx) is set up just before each call.
import sys, struct, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import petool as P, rdis

path = sys.argv[1]
target = int(sys.argv[2], 16)
data, secs, ib = P.load(path)
pd = rdis.pdata(data, secs)
tva, tvsz, tra, trsz = [(va, vsz, ra, rsz)
                        for n, va, vsz, ra, rsz in secs if n == ".text"][0]
text = data[tra:tra+trsz]
md = Cs(CS_ARCH_X86, CS_MODE_64)

hits = []
for m in re.finditer(rb"[\xe8\xe9]", text):
    o = m.start()
    if o + 5 > len(text):
        continue
    disp = struct.unpack_from("<i", text, o + 1)[0]
    if tva + o + 5 + disp == target:
        hits.append(tva + o)

print("direct callers of 0x%X: %d" % (target, len(hits)))
for h in hits:
    b, e = rdis.enclosing(pd, h)
    print("\n--- call at 0x%X (inside 0x%X-0x%X) ---" % (h, b or 0, e or 0))
    start = max(h - 0x50, b or (h - 0x50))
    fo = P.rva2f(secs, start)
    code = data[fo:fo + (h - start) + 5]
    for ins in md.disasm(code, start):
        print("   %08X  %-22s %s %s" % (ins.address, ins.bytes.hex(" "),
                                        ins.mnemonic, ins.op_str))
