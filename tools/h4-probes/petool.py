import re, struct

def load(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    magic = struct.unpack_from("<H", data, pe + 24)[0]
    imgbase = struct.unpack_from("<Q" if magic == 0x20b else "<I", data,
                                 pe + 24 + (24 if magic == 0x20b else 28))[0]
    secs = []
    off = pe + 24 + optsz
    for i in range(nsec):
        b = off + 40 * i
        name = data[b:b+8].rstrip(b"\0").decode("latin1")
        vsz, va, rsz, ra = struct.unpack_from("<IIII", data, b + 8)
        secs.append((name, va, vsz, ra, rsz))
    return data, secs, imgbase

def f2rva(secs, foff):
    for name, va, vsz, ra, rsz in secs:
        if ra <= foff < ra + rsz:
            return va + (foff - ra), name
    return None, None

def rva2f(secs, rva):
    for name, va, vsz, ra, rsz in secs:
        if va <= rva < va + max(vsz, rsz):
            o = ra + (rva - va)
            if o < ra + rsz:
                return o
    return None

def cstr(data, foff, limit=200):
    e = foff
    while e < len(data) and e - foff < limit and 0x20 <= data[e] < 0x7f:
        e += 1
    return data[foff:e].decode("latin1")

def strings_in_range(data, secs, lo, hi):
    """Yield (rva, string) for NUL-terminated ASCII strings in an RVA range."""
    out = []
    f_lo, f_hi = rva2f(secs, lo), rva2f(secs, hi)
    if f_lo is None or f_hi is None:
        return out
    i = f_lo
    while i < f_hi:
        if 0x20 <= data[i] < 0x7f and (i == 0 or data[i-1] == 0):
            s = cstr(data, i)
            if len(s) >= 3 and i + len(s) < len(data) and data[i+len(s)] == 0:
                out.append((f2rva(secs, i)[0], s))
                i += len(s)
        i += 1
    return out

def find_qword_refs(data, secs, imgbase, target_rva):
    """Find absolute VA pointers (relocated data tables) to target_rva."""
    va = imgbase + target_rva
    needle = struct.pack("<Q", va)
    return [f2rva(secs, m.start()) for m in re.finditer(re.escape(needle), data)]

def find_lea_refs(data, secs, target_rva):
    """Find rip-relative lea/mov references to target_rva from .text."""
    out = []
    for name, va, vsz, ra, rsz in secs:
        if name != ".text":
            continue
        text = data[ra:ra+rsz]
        for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", text):
            o = m.start()
            if o + 7 > len(text):
                continue
            disp = struct.unpack_from("<i", text, o + 3)[0]
            if va + o + 7 + disp == target_rva:
                out.append(va + o)
    return out
