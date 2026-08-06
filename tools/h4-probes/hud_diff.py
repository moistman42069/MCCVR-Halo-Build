# READ-ONLY live memory-diff probe: find what ACTUALLY changes when a config
# value (e.g. hud_size) is moved in the F1 menu, making no assumption about
# which struct or field owns the value.
#
# This is the same method that originally found Halo 3's working safe-frame
# anchor: snapshot memory, change the slider, snapshot again, and look at what
# is actually different.
#
# Never writes to the process. Uses PROCESS_VM_READ only.
#
# Usage (two-shot, needs the game roughly still between snapshots):
#   1. py -3 hud_diff.py snap baseline.bin      (while HUD looks normal)
#   2. move the slider in the F1 menu by a LARGE amount (e.g. 0.87 -> 0.30)
#   3. py -3 hud_diff.py snap after.bin
#   4. py -3 hud_diff.py diff baseline.bin after.bin
#
# Usage (poll, RECOMMENDED - isolates the exact moment of the slider move
# instead of accumulating minutes of gameplay drift between two slow
# snapshots):
#   py -3 hud_diff.py poll
#   Then just play normally and move the slider a few times whenever ready -
#   the tool prints ONLY what changed since the last poll, a few seconds ago,
#   not since the start. A real scale-like field will show up as a small,
#   tight group of changes appearing in the same poll cycle as your slider
#   move; gameplay motion (camera, animation, physics) shows up continuously
#   in every cycle regardless of the slider, so it's visually obvious which
#   is which. Ctrl+C to stop.
#
# diff/poll report every 4-byte-aligned float that changed and is still a
# plausible 0.0-2.0 scale-like value, grouped by region, so a real scale
# field stands out from render-camera/animation noise instead of being lost
# in it.
import ctypes as C, struct, sys, pickle, time
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


# A config/UI-scale scalar lives in a small config/UI-state struct or a
# tag-data block, not a multi-megabyte world/asset heap. Capping region size
# keeps each poll cycle fast (seconds, not a minute) and keeps
# world/animation/physics heaps (which ARE typically huge) out of the noise
# entirely. The 4MB cap was calibrated on Reach, where it was still enough to
# hold the exact proven tag-data record of interest (a 4194304-byte region
# among the hits), so it does not exclude tag-data-sized blocks.
MAX_REGION_BYTES = 4 << 20


def scan_regions(h):
    """Yield (base, protect, type, data) for every committed private/mapped
    readable region up to MAX_REGION_BYTES."""
    addr = 0x10000
    mbi = MEMORY_BASIC_INFORMATION64()
    while addr < 0x7FFFFFFF0000:
        if k32.VirtualQueryEx(h, C.c_void_p(addr), C.byref(mbi),
                              C.sizeof(mbi)) != C.sizeof(mbi):
            addr += 0x1000
            continue
        base, size = mbi.BaseAddress, mbi.RegionSize
        readable = (mbi.State == 0x1000 and
                    not (mbi.Protect & 0x101) and mbi.Protect != 0)
        interesting_type = mbi.Type in (0x20000, 0x40000)  # PRIVATE, MAPPED
        if readable and interesting_type and 0 < size <= MAX_REGION_BYTES:
            blob = read(h, base, size)
            if blob:
                yield (base, mbi.Protect, mbi.Type, blob)
        addr = base + size if size else addr + 0x1000


def do_snap(outpath):
    pid = find_pid()
    if not pid:
        print(f"{GAME} is not running.")
        return 1
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print(f"OpenProcess failed: {C.get_last_error()}")
        return 1
    print(f"attached to pid {pid}, snapshotting all private+mapped RW/RO "
          f"regions <={MAX_REGION_BYTES>>20}MB (should take a few seconds)...")
    regions = list(scan_regions(h))
    total = sum(len(b) for _, _, _, b in regions)
    print(f"captured {len(regions)} region(s), {total/1e6:.1f} MB total")
    with open(outpath, "wb") as f:
        pickle.dump(regions, f, protocol=4)
    print(f"wrote {outpath}")
    k32.CloseHandle(h)
    return 0


def _plausible(x):
    return -0.01 <= x <= 3.0 and (x == 0.0 or abs(x) > 1e-6)


def compare(regions_a, regions_b, quiet_ok=False):
    """Returns (reported_count, total_changed_floats); prints per-region
    detail. quiet_ok suppresses the 'no changes' line for poll mode."""
    b_by_base = {base: (prot, typ, blob) for base, prot, typ, blob in regions_b}
    total_changed_floats = 0
    reported = 0
    for base, prot, typ, blob_a in regions_a:
        entry = b_by_base.get(base)
        if not entry:
            continue
        _, _, blob_b = entry
        n = min(len(blob_a), len(blob_b))
        changes = []
        for off in range(0, n - 4, 4):
            wa = blob_a[off:off+4]
            wb = blob_b[off:off+4]
            if wa == wb:
                continue
            fa = struct.unpack("<f", wa)[0]
            fb = struct.unpack("<f", wb)[0]
            if _plausible(fa) and _plausible(fb) and abs(fa - fb) > 0.02:
                changes.append((off, fa, fb))
        total_changed_floats += len(changes)
        if changes:
            reported += 1
            typename = "MAPPED" if typ == 0x40000 else "PRIVATE"
            print(f"\nregion 0x{base:016X} ({typename}, protect 0x{prot:X}, "
                  f"{len(blob_a)} bytes): {len(changes)} plausible float "
                  f"change(s)")
            for off, fa, fb in changes[:40]:
                print(f"    +0x{off:06X}  {fa:.4f} -> {fb:.4f}")
            if len(changes) > 40:
                print(f"    ... and {len(changes)-40} more in this region")
    if reported or not quiet_ok:
        print(f"  -> {reported} region(s), {total_changed_floats} float(s) "
              f"changed this cycle")
    return reported, total_changed_floats


def do_diff(path_a, path_b):
    with open(path_a, "rb") as f:
        regions_a = pickle.load(f)
    with open(path_b, "rb") as f:
        regions_b = pickle.load(f)
    print(f"comparing {len(regions_a)} baseline region(s) against "
          f"{len(regions_b)} after-region(s)...")
    compare(regions_a, regions_b)
    return 0


def do_poll():
    pid = find_pid()
    if not pid:
        print(f"{GAME} is not running.")
        return 1
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print(f"OpenProcess failed: {C.get_last_error()}")
        return 1
    print(f"attached to pid {pid}. Polling every ~3s; each cycle reports only "
          f"what changed since the PREVIOUS cycle (not since start), so a "
          f"slider move stands out from a few seconds of gameplay drift "
          f"instead of minutes of it. Play normally; move the slider whenever "
          f"ready. Ctrl+C to stop.\n")
    prev = list(scan_regions(h))
    cycle = 0
    try:
        while True:
            time.sleep(3.0)
            cycle += 1
            cur = list(scan_regions(h))
            t0 = time.time()
            print(f"[cycle {cycle}, {time.strftime('%H:%M:%S')}]", end="")
            reported, changed = compare(prev, cur, quiet_ok=True)
            if not reported:
                print("  (no plausible scale-like change)")
            prev = cur
    except KeyboardInterrupt:
        print("\nstopped.")
    k32.CloseHandle(h)
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "snap":
        sys.exit(do_snap(sys.argv[2]))
    elif len(sys.argv) >= 4 and sys.argv[1] == "diff":
        sys.exit(do_diff(sys.argv[2], sys.argv[3]))
    elif len(sys.argv) >= 2 and sys.argv[1] == "poll":
        sys.exit(do_poll())
    else:
        print(__doc__)
        sys.exit(1)
