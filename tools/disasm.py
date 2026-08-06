# Disassemble a range of halo3.dll in the running game, by RVA.
# Read-only: attaches with PROCESS_VM_READ and uses capstone. No game files touched.
#   py -3 disasm.py <rva_hex> <length>
#   py -3 disasm.py --module <pe_path> <rva_hex> <length>  - target another module
import sys, ctypes as C
from ctypes import wintypes as W
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

GAME = "MCC-Win64-Shipping.exe"
DLL  = "halo3.dll"
DLL_PATH = r"N:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo3\halo3.dll"
# Optional module override; default stays halo3.dll so existing usage is
# unchanged. Live lookup uses the file's basename; the offline fallback reads
# the given file.
MODULE_OVERRIDE = False
if "--module" in sys.argv:
    _i = sys.argv.index("--module")
    DLL_PATH = sys.argv[_i + 1]
    del sys.argv[_i:_i + 2]
    DLL = DLL_PATH.replace("/", "\\").rsplit("\\", 1)[-1]
    MODULE_OVERRIDE = True
TH32CS_SNAPPROCESS = 0x2
TH32CS_SNAPMODULE   = 0x8
TH32CS_SNAPMODULE32 = 0x10
PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400

k32 = C.WinDLL("kernel32", use_last_error=True)

class PROCESSENTRY32(C.Structure):
    _fields_ = [("dwSize",W.DWORD),("cntUsage",W.DWORD),("th32ProcessID",W.DWORD),
        ("th32DefaultHeapID",C.POINTER(C.c_ulong)),("th32ModuleID",W.DWORD),
        ("cntThreads",W.DWORD),("th32ParentProcessID",W.DWORD),("pcPriClassBase",C.c_long),
        ("dwFlags",W.DWORD),("szExeFile",C.c_char*260)]

class MODULEENTRY32(C.Structure):
    _fields_ = [("dwSize",W.DWORD),("th32ModuleID",W.DWORD),("th32ProcessID",W.DWORD),
        ("GlblcntUsage",W.DWORD),("ProccntUsage",W.DWORD),("modBaseAddr",C.POINTER(C.c_byte)),
        ("modBaseSize",W.DWORD),("hModule",W.HMODULE),("szModule",C.c_char*256),
        ("szExePath",C.c_char*260)]

def find_pid():
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32(); pe.dwSize = C.sizeof(pe)
    ok = k32.Process32First(snap, C.byref(pe))
    pid = 0
    while ok:
        if pe.szExeFile.decode(errors="ignore").lower() == GAME.lower():
            pid = pe.th32ProcessID; break
        ok = k32.Process32Next(snap, C.byref(pe))
    k32.CloseHandle(snap); return pid

def module_base(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32, pid)
    me = MODULEENTRY32(); me.dwSize = C.sizeof(me)
    ok = k32.Module32First(snap, C.byref(me))
    base = size = 0
    while ok:
        if me.szModule.decode(errors="ignore").lower() == DLL.lower():
            base = C.cast(me.modBaseAddr, C.c_void_p).value; size = me.modBaseSize; break
        ok = k32.Module32Next(snap, C.byref(me))
    k32.CloseHandle(snap); return base, size

def rva_to_file_offset(f, rva):
    """Map an RVA to a raw file offset via the PE section table. Generic
    replacement for halo3's fixed 0xC00 .text delta when --module is used."""
    import struct
    f.seek(0)
    hdr = f.read(0x2000)
    pe = struct.unpack_from("<I", hdr, 0x3C)[0]
    nsec = struct.unpack_from("<H", hdr, pe + 6)[0]
    optsz = struct.unpack_from("<H", hdr, pe + 20)[0]
    off = pe + 24 + optsz
    for i in range(nsec):
        vsz, va, rsz, ra = struct.unpack_from("<IIII", hdr, off + 40 * i + 8)
        if va <= rva < va + max(vsz, rsz):
            return ra + (rva - va)
    raise SystemExit("rva 0x%X not mapped in %s" % (rva, DLL_PATH))

def main():
    float_mode = sys.argv[1].lower() == "floats"
    arg = 2 if float_mode else 1
    rva = int(sys.argv[arg], 16)
    length = (int(sys.argv[arg + 1]) * 4 if float_mode else int(sys.argv[arg + 1])) if len(sys.argv) > arg + 1 else 128
    # Optional explicit PID/base bypasses Toolhelp module enumeration, which
    # can intermittently miss dynamically loaded engine DLLs.
    pid_arg = arg + 2
    pid = int(sys.argv[pid_arg], 0) if len(sys.argv) > pid_arg else find_pid()
    if not pid: print("game not running"); return
    if len(sys.argv) > pid_arg + 1:
        base, size = int(sys.argv[pid_arg + 1], 0), 0x4768000
    else:
        base, size = module_base(pid)
    if not base: print("halo3.dll not loaded"); return
    h = k32.OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION, False, pid)
    buf = (C.c_char*length)(); read = C.c_size_t(0)
    if not k32.ReadProcessMemory(h, C.c_void_p(base+rva), buf, length, C.byref(read)):
        # halo3.dll can unload when the title returns to a menu. Code RVAs in
        # its .text section map to raw file offsets with this build's 0xC00
        # section delta, so retain an offline read-only fallback for RE work.
        # An overridden module gets a proper section-table mapping instead.
        with open(DLL_PATH, "rb") as f:
            if MODULE_OVERRIDE:
                f.seek(rva_to_file_offset(f, rva))
            else:
                f.seek(rva - 0xC00)
            raw = f.read(length)
        code = raw
        k32.CloseHandle(h)
        read = None
    else:
        code = bytes(buf[:read.value])
    k32.CloseHandle(h)
    if float_mode:
        import struct
        vals = struct.unpack("<%df" % (len(code) // 4), code)
        for i in range(0, len(vals), 4):
            print("[%02d] % .7f % .7f % .7f % .7f" % ((i,) + vals[i:i+4]))
        return
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    for insn in md.disasm(code, rva):
        print("halo3.dll+0x%X:  %-9s %s" % (insn.address, insn.mnemonic, insn.op_str))

main()
