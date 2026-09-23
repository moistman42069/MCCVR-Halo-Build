"""Pinned native subtitle handoff witnesses, read-only and offline.

Own-kit derivation and limitations: docs/NATIVE-SUBTITLE-H2-H4-2026-09-23.md.
This does not install hooks, open a game process or validate headset output.
"""
import hashlib
import json
import struct
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "out/pydeps"))
import pefile

SOURCES = {
    "halo2.dll": ("DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946", [
        ("speech", 0x6B7ED0, 0x6B8232, 0x6B825A, 0x6B8260),
        ("named_subtitle", 0x6F6750, 0x6F686F, 0x6F68A1, 0x6F68A4),
    ]),
    "halo4.dll": ("7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84", [
        ("queue_first_of_two", 0x12FC38, 0x1301A2, 0x1301D4, 0x1301DA),
        ("queue_second", 0x12FC38, 0x13021C, 0x13024D, 0x130253),
        ("queue_single", 0x12FC38, 0x130267, 0x13029C, 0x1302A2),
    ]),
}


def main():
    report = []
    for name, (digest, callers) in SOURCES.items():
        raw = (ROOT / "out/deps/re-tools/inputs" / name).read_bytes()
        assert hashlib.sha256(raw).hexdigest().upper() == digest
        pe = pefile.PE(data=raw, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"]])
        image = pe.get_memory_mapped_image()
        section = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
        code = image[section.VirtualAddress:section.VirtualAddress + section.Misc_VirtualSize]
        for label, function, start, call, end in callers:
            pattern = image[start:end]
            assert code.count(pattern) == 1, label
            instruction = image[call:end]
            assert instruction in (bytes.fromhex("FF 90 C8 02 00 00"), bytes.fromhex("41 FF D2"))
            entry = next(e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION
                         if e.struct.BeginAddress <= start < end <= e.struct.EndAddress)
            begin, finish, unwind = entry.BeginAddress, entry.EndAddress, entry.UnwindData
            chain = [begin]
            for _ in range(8):
                if begin == function: break
                flags = image[unwind] >> 3
                assert flags & 4, (name, label, "unwind chain")
                offset = unwind + 4 + ((image[unwind+2] + 1) & ~1) * 2
                begin, finish, unwind = struct.unpack_from("<III", image, offset)
                chain.append(begin)
            assert begin == function, (name, label, "unwind owner")
            report.append({"module": name, "sha256": digest, "source": label,
                "function_rva": hex(function), "pattern_rva": hex(start),
                "call_rva": hex(call), "return_rva": hex(end),
                "pattern": pattern.hex(" ").upper(), "unique_matches": 1,
                "unwind_chain": [hex(value) for value in chain]})
    print(json.dumps({"result": "PASS_OFFLINE_ONLY", "callers": report}, indent=2))


if __name__ == "__main__":
    main()
