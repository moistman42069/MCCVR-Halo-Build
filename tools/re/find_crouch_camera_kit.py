"""Read-only official-kit leads for native crouch camera evaluation.

This is discovery, not a runtime binding verifier. Locate the kit's named
estimate-mode assertions and their data-record consumers. No retail discovery
or process memory access; print RVAs with the PE image base separately.
"""
from pathlib import Path
import bisect
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "out/pydeps"))
import pefile

NAMES = ("halo3_tag_test.exe", "halo3odst_tag_test.exe", "reach_tag_test.exe",
         "halo4_tag_test.exe", "halo2_tag_test.exe", "halo_tag_test.exe")

for name in NAMES:
    pe = pefile.PE(str(ROOT / "out/deps/re-tools/inputs" / name), fast_load=True)
    raw = pe.__data__
    base = pe.OPTIONAL_HEADER.ImageBase
    psize = 8 if pe.OPTIONAL_HEADER.Magic == 0x20B else 4
    target_names = {}
    for match in re.finditer(rb"[\x20-\x7e]{5,}\x00", raw):
        value = match.group()[:-1].decode("ascii")
        if "estimated_body_position != NULL" in value:
            target_names[pe.get_rva_from_offset(match.start())] = value
    records = dict(target_names)
    # PE32 kits pass assertion text directly. Treating every code immediate as
    # a possible metadata-record start produces unrelated instruction leads.
    for target, value in (target_names.items() if psize == 8 else ()):
        pointer = struct.pack("<Q" if psize == 8 else "<I", base + target)
        for match in re.finditer(re.escape(pointer), raw):
            for delta in range(0, psize * 6, psize):
                try:
                    records[pe.get_rva_from_offset(match.start() - delta)] = value
                except pefile.PEFormatError:
                    pass
    ranges = []
    if psize == 8:
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"]])
        ranges = sorted((x.struct.BeginAddress, x.struct.EndAddress)
                        for x in pe.DIRECTORY_ENTRY_EXCEPTION)
    starts = [x[0] for x in ranges]
    found = set()
    print(f"KIT {name} base={base:X}; all addresses below are RVAs")
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        data = section.get_data()
        if psize == 8:
            for match in re.finditer(rb"[\x48-\x4f]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", data):
                at = section.VirtualAddress + match.start()
                if match.start() + 7 > len(data):
                    continue
                target = at + 7 + struct.unpack_from("<i", data, match.start() + 3)[0]
                if target not in records:
                    continue
                index = bisect.bisect_right(starts, at) - 1
                begin, end = ranges[index] if index >= 0 else (0, 0)
                if begin <= at < end:
                    found.add((begin, at, target))
        else:
            for target in records:
                pointer = struct.pack("<I", base + target)
                for match in re.finditer(re.escape(pointer), data):
                    found.add((0, section.VirtualAddress + match.start(), target))
    for begin, at, target in sorted(found):
        print(f"  candidate={begin:X} reference={at:X} assertion_record={target:X}")
