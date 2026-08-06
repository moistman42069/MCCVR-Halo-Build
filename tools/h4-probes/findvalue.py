# READ-ONLY live probe: find every occurrence of a specific float value
# anywhere in the running game's memory, and classify each hit by how "live"
# looking its containing region is (does the surrounding memory look like
# static/const data, or an actively-touched runtime struct).
#
# Use this to find candidate locations for a known config value (hud_size or
# any other) without assuming which struct or field owns it - instead of
# starting from a known struct shape and asking "does this change", this
# starts from the value and asks "where does this value live at all".
#
# Never writes to the process. Uses PROCESS_VM_READ only.
#
#   py -3 findvalue.py 0.45
#   py -3 findvalue.py 0.45 0.4497     (search for multiple values in one pass)
import ctypes as C, struct, sys
from ctypes import wintypes as W

GAME = "MCC-Win64-Shipping.exe"
PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400
TH32CS_SNAPPROCESS = 0x2

k32 = C.WinDLL("kernel32", use_last_error=True)


class PROCESSENTRY32(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD),
                ("th32ProcessID", W.DWORD),
                ("th32DefaultHeapID", C.POINTER(C.c_ulong)),
                ("th32ModuleID", W.DWORD), ("cntThreads", W.DWORD),
                ("th32ParentProcessID", W.DWORD), ("pcPriClassBase", C.c_long),
                ("dwFlags", W.DWORD), ("szExeFile", C.c_char * 260)]


class MEMORY_BASIC_INFORMATION64(C.Structure):
    _fields_ = [("BaseAddress", C.c_ulonglong),
                ("AllocationBase", C.c_ulonglong),
                ("AllocationProtect", W.DWORD), ("__alignment1", W.DWORD),
                ("RegionSize", C.c_ulonglong), ("State", W.DWORD),
                ("Protect", W.DWORD), ("Type", W.DWORD),
                ("__alignment2", W.DWORD)]


PROT = {0x01: "NOACCESS", 0x02: "R", 0x04: "RW", 0x08: "WC",
        0x10: "X", 0x20: "RX", 0x40: "RWX", 0x80: "WCX"}
TYPE = {0x20000: "PRIVATE", 0x40000: "MAPPED", 0x1000000: "IMAGE"}


def find_pid():
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    e = PROCESSENTRY32()
    e.dwSize = C.sizeof(e)
    ok = k32.Process32First(snap, C.byref(e))
    while ok:
        if e.szExeFile.decode("latin1").lower() == GAME.lower():
            k32.CloseHandle(snap)
            return e.th32ProcessID
        ok = k32.Process32Next(snap, C.byref(e))
    k32.CloseHandle(snap)
    return None


def read(h, addr, size):
    buf = (C.c_char * size)()
    got = C.c_size_t(0)
    if not k32.ReadProcessMemory(h, C.c_void_p(addr), buf, size, C.byref(got)):
        return None
    return buf.raw[:got.value]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    targets = [struct.pack("<f", float(a)) for a in sys.argv[1:]]
    labels = {t: v for t, v in zip(targets, sys.argv[1:])}

    pid = find_pid()
    if not pid:
        print(f"{GAME} is not running.")
        return 1
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print(f"OpenProcess failed: {C.get_last_error()}")
        return 1

    print(f"attached to pid {pid}, searching for {sys.argv[1:]} "
          f"across all readable memory...")
    addr = 0x10000
    mbi = MEMORY_BASIC_INFORMATION64()
    total_hits = 0
    by_type_count = {}
    hits = []
    while addr < 0x7FFFFFFF0000:
        if k32.VirtualQueryEx(h, C.c_void_p(addr), C.byref(mbi),
                              C.sizeof(mbi)) != C.sizeof(mbi):
            addr += 0x1000
            continue
        base, size = mbi.BaseAddress, mbi.RegionSize
        readable = (mbi.State == 0x1000 and
                    not (mbi.Protect & 0x101) and mbi.Protect != 0)
        if readable and 0 < size <= (128 << 20):
            blob = read(h, base, size)
            if blob:
                for t in targets:
                    i = blob.find(t)
                    while i != -1:
                        # 4-byte alignment only - real fields are aligned
                        if (base + i) % 4 == 0:
                            hits.append((base + i, mbi.Protect, mbi.Type,
                                        labels[t]))
                            total_hits += 1
                            k = TYPE.get(mbi.Type, hex(mbi.Type))
                            by_type_count[k] = by_type_count.get(k, 0) + 1
                        i = blob.find(t, i + 1)
        addr = base + size if size else addr + 0x1000

    print(f"\n{total_hits} aligned hit(s) across memory:")
    for k, c in sorted(by_type_count.items(), key=lambda x: -x[1]):
        print(f"  {k}: {c}")

    print(f"\nfirst 200 hits (address, region type, protect, value):")
    for addr_, prot, typ, val in hits[:200]:
        t = TYPE.get(typ, hex(typ))
        p = PROT.get(prot & 0xFF, hex(prot))
        print(f"  {addr_:016X}  {t:8} {p:4}  = {val}")
    if len(hits) > 200:
        print(f"  ... and {len(hits)-200} more")

    k32.CloseHandle(h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
