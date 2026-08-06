# READ-ONLY live probe: find every pointer anywhere in the running game's
# memory that references a specific target address (or falls within
# [target, target+range) of it).
#
# Purpose: findvalue.py proved common floats (0.45, 1.0, etc) are far too
# common in a live 3D engine to be useful alone. This instead starts from an
# address that is ALREADY KNOWN to be a real, correctly-shaped record and asks
# "does anything else in the process hold a pointer to this record, or into
# it?" If the engine caches a pointer to the record, or copies a sub-range of
# it into a separate structure, this catches that reference directly - a
# fundamentally different signal than guessing struct shapes or scanning for
# common values.
#
# Never writes to the process. Uses PROCESS_VM_READ only.
#
#   py -3 findptr.py 0x7FF45204EBAC
#   py -3 findptr.py 0x7FF45204EBAC 0x100     (also match pointers into the
#                                               following 0x100 bytes, i.e.
#                                               anywhere inside the record)
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
    target = int(sys.argv[1], 16)
    span = int(sys.argv[2], 16) if len(sys.argv) > 2 else 1

    pid = find_pid()
    if not pid:
        print(f"{GAME} is not running.")
        return 1
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print(f"OpenProcess failed: {C.get_last_error()}")
        return 1

    print(f"attached to pid {pid}, searching for 8-byte pointers into "
          f"[0x{target:X}, 0x{target+span:X}) across all readable memory...")
    addr = 0x10000
    mbi = MEMORY_BASIC_INFORMATION64()
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
                # 8-byte aligned pointer scan
                n = len(blob)
                for off in range(0, n - 8, 8):
                    v = struct.unpack_from("<Q", blob, off)[0]
                    if target <= v < target + span:
                        hits.append((base + off, v, mbi.Protect, mbi.Type))
        addr = base + size if size else addr + 0x1000

    print(f"\n{len(hits)} pointer(s) found referencing that address/range:")
    for holder_addr, pointed, prot, typ in hits[:200]:
        t = TYPE.get(typ, hex(typ))
        p = PROT.get(prot & 0xFF, hex(prot))
        delta = pointed - target
        print(f"  holder 0x{holder_addr:016X} ({t:8} {p:4}) -> "
              f"0x{pointed:016X} (target+0x{delta:X})")
    if len(hits) > 200:
        print(f"  ... and {len(hits)-200} more")
    if not hits:
        print("  (nothing holds a pointer to this address - it is not "
              "reached indirectly, or only reached via a computed/relative "
              "offset that does not store the literal address anywhere)")

    k32.CloseHandle(h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
