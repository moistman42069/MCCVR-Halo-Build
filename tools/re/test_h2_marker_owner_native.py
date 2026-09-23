"""Execute H2's pinned marker-owner selector offline; never accesses MCC.

The actual selector executes. Object-data access and the pre-existing native
visibility predicate are fixture services, not emulated game-world claims.
"""
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "out/pydeps"))
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RIP, UC_X86_REG_RSP

BASE, HEAP, STACK, STOP = 0x180000000, 0x60000000, 0x70000000, 0x71000000
raw = (ROOT / "out/deps/re-tools/inputs/halo2.dll").read_bytes()
assert hashlib.sha256(raw).hexdigest().upper() == "DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946"
image = pefile.PE(data=raw, fast_load=True).get_memory_mapped_image()
uc = Uc(UC_ARCH_X86, UC_MODE_64)
uc.mem_map(BASE, (len(image) + 4095) & ~4095)
uc.mem_write(BASE, image)
for address in (HEAP, STACK, STOP):
    uc.mem_map(address, 0x10000)
header, weapon_data, instances, definition = [HEAP + n * 0x1000 for n in range(4)]
uc.mem_write(BASE + 0x18B7398, struct.pack("<Q", header))
uc.mem_write(header + 0x48, struct.pack("<Q", 0x100))
uc.mem_write(BASE + 0x15E4B30, struct.pack("<Q", instances))
uc.mem_write(BASE + 0x15E4B38, struct.pack("<Q", definition))
uc.mem_write(weapon_data, struct.pack("<I", 7))
uc.mem_write(instances + 7 * 16 + 8, struct.pack("<i", 0))
weapon, predicate, object_calls, predicate_calls = 0, 0, 0, 0


def native_return():
    sp = uc.reg_read(UC_X86_REG_RSP)
    uc.reg_write(UC_X86_REG_RIP, struct.unpack("<Q", uc.mem_read(sp, 8))[0])
    uc.reg_write(UC_X86_REG_RSP, sp + 8)


def step(machine, address, size, unused):
    global object_calls, predicate_calls
    rva = address - BASE
    if rva == 0x8D7000:
        assert uc.reg_read(UC_X86_REG_RCX) == header + 0x100 + (weapon & 0xFFFF) * 12
        object_calls += 1
        uc.reg_write(UC_X86_REG_RAX, weapon_data)
        native_return()
    elif rva == 0x8D7150:
        assert uc.reg_read(UC_X86_REG_RCX) == weapon
        predicate_calls += 1
        uc.reg_write(UC_X86_REG_RAX, predicate)
        native_return()
    elif not 0x8E8990 <= rva < 0x8E8A10:
        raise AssertionError(f"Unexpected instruction/call {rva:x}")


uc.hook_add(UC_HOOK_CODE, step)
cases = 0
for weapon in (0x56780002, 0x789A0003):
    for parent in (0x12340001, 0xFFFF0001, 0xFFFFFFFF):
        for model in (0x43210007, 0xFFFFFFFF):
            for predicate in (0, 1):
                uc.mem_write(weapon_data + 0x14, struct.pack("<I", parent))
                uc.mem_write(definition + 0x38, struct.pack("<I", model))
                sp = STACK + 0xFF08
                uc.mem_write(sp, struct.pack("<Q", STOP))
                uc.reg_write(UC_X86_REG_RSP, sp)
                uc.reg_write(UC_X86_REG_RCX, weapon)
                object_calls = predicate_calls = 0
                uc.emu_start(BASE + 0x8E8990, STOP, count=200)
                expected = parent if (predicate or model == 0xFFFFFFFF) and parent != 0xFFFFFFFF else weapon
                assert uc.reg_read(UC_X86_REG_RIP) == STOP
                assert uc.reg_read(UC_X86_REG_RAX) == expected
                assert object_calls == predicate_calls == 1
                cases += 1
print(f"PASS: {cases} actual H2 marker-owner selector cases; primary/secondary, full salted parent, model/predicate and NONE branches")
