#!/usr/bin/env python3
"""Merge the released V6 Halo 4 post-build layer into the two-hand DLL.

The public V6 source commit does not contain the five PE sections that were
added to the released DLL after linking.  Rebuilding that source therefore
drops accepted Halo 4 HUD, helmet, effect, muzzle, and pause behavior.  This
tool is intentionally narrow: it accepts only the exact released V6 donor and
the exact headset-tested two-hand candidate, ports the donor sections, and
retargets the small set of build-relative calls whose RVAs moved.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path


V6_SHA256 = "419f2ca425a41f3fe42a2f27cfd0ce55123f71c5ef34c1c45604018285efea82"
TWO_HAND_SHA256 = "60e5d57ebea0b4c1f9112fd3ae8029467f0dfbd2053786122448475230e6db5c"

CUSTOM_SECTION_NAMES = (b".h4fx", b".h4fd", b".h4hs", b".h4hp", b".h4pb")

# Calls changed by the released V6 post-build layer.  Candidate RVAs differ
# slightly because the two-hand source change moved a few compiled functions.
# Each tuple is (call RVA, expected stock target RVA, V6 wrapper target RVA).
BASE_CALL_PATCHES = (
    (0x003D52, 0x005830, 0x29F013),
    (0x0057FD, 0x005830, 0x29F013),
    (0x00C6D1, 0x001A20, 0x29F000),
    (0x013AEA, 0x001A20, 0x2A0000),
    (0x026105, 0x005830, 0x29F013),
    (0x026191, 0x005830, 0x29F013),
    (0x02E860, 0x111220, 0x29E40C),
    (0x02EFAC, 0x005830, 0x29F026),
    (0x02F034, 0x005830, 0x29F013),
    (0x048A7E, 0x04CEA0, 0x29C000),
    (0x04FC28, 0x001A20, 0x29C440),
)

# Direct calls made by the V6 layer back into the base DLL.  Only functions
# whose link RVAs moved in the two-hand build are listed; logger and stable
# helper calls retain their original targets.
CUSTOM_CALL_PATCHES = (
    (0x29C004, 0x04CE90, 0x04CEA0),
    (0x29C12D, 0x037F90, 0x037FA0),
    (0x29E06E, 0x0C78F0, 0x0C78B0),
    (0x29E08C, 0x0C78F0, 0x0C78B0),
    (0x29E410, 0x111270, 0x111220),
    (0x29E423, 0x1059D0, 0x105940),
    (0x29E436, 0x111270, 0x111220),
    (0x2A0065, 0x02A8B0, 0x02A8C0),
)


@dataclass(frozen=True)
class Section:
    name: bytes
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_pointer: int
    characteristics: int
    header: bytes


@dataclass(frozen=True)
class PeLayout:
    pe_offset: int
    file_header_offset: int
    optional_header_offset: int
    section_table_offset: int
    section_count: int
    optional_header_size: int
    file_alignment: int
    section_alignment: int
    size_of_headers: int
    image_base: int
    sections: tuple[Section, ...]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def parse_pe(data: bytes | bytearray) -> PeLayout:
    if data[:2] != b"MZ":
        raise ValueError("input is not an MZ image")
    pe_offset = u32(data, 0x3C)
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("input is not a PE image")
    file_header_offset = pe_offset + 4
    section_count = u16(data, file_header_offset + 2)
    optional_header_size = u16(data, file_header_offset + 16)
    optional_header_offset = file_header_offset + 20
    if u16(data, optional_header_offset) != 0x20B:
        raise ValueError("expected a PE32+ image")
    section_table_offset = optional_header_offset + optional_header_size
    file_alignment = u32(data, optional_header_offset + 36)
    section_alignment = u32(data, optional_header_offset + 32)
    size_of_headers = u32(data, optional_header_offset + 60)
    image_base = u64(data, optional_header_offset + 24)
    sections: list[Section] = []
    for index in range(section_count):
        offset = section_table_offset + index * 40
        header = bytes(data[offset : offset + 40])
        name = header[:8].rstrip(b"\0")
        sections.append(
            Section(
                name=name,
                virtual_size=u32(header, 8),
                virtual_address=u32(header, 12),
                raw_size=u32(header, 16),
                raw_pointer=u32(header, 20),
                characteristics=u32(header, 36),
                header=header,
            )
        )
    return PeLayout(
        pe_offset=pe_offset,
        file_header_offset=file_header_offset,
        optional_header_offset=optional_header_offset,
        section_table_offset=section_table_offset,
        section_count=section_count,
        optional_header_size=optional_header_size,
        file_alignment=file_alignment,
        section_alignment=section_alignment,
        size_of_headers=size_of_headers,
        image_base=image_base,
        sections=tuple(sections),
    )


def rva_to_offset(layout: PeLayout, rva: int) -> int:
    if rva < layout.size_of_headers:
        return rva
    for section in layout.sections:
        span = max(section.virtual_size, section.raw_size)
        if section.virtual_address <= rva < section.virtual_address + span:
            delta = rva - section.virtual_address
            if delta >= section.raw_size:
                raise ValueError(f"RVA 0x{rva:X} is in zero-filled section data")
            return section.raw_pointer + delta
    raise ValueError(f"RVA 0x{rva:X} is not mapped")


def patch_rel32(
    image: bytearray,
    layout: PeLayout,
    call_rva: int,
    expected_target_rva: int,
    new_target_rva: int,
) -> None:
    offset = rva_to_offset(layout, call_rva)
    if image[offset] != 0xE8:
        raise ValueError(
            f"RVA 0x{call_rva:X}: expected CALL rel32, found 0x{image[offset]:02X}"
        )
    old_displacement = struct.unpack_from("<i", image, offset + 1)[0]
    old_target = call_rva + 5 + old_displacement
    if old_target != expected_target_rva:
        raise ValueError(
            f"RVA 0x{call_rva:X}: expected target 0x{expected_target_rva:X}, "
            f"found 0x{old_target:X}"
        )
    new_displacement = new_target_rva - (call_rva + 5)
    struct.pack_into("<i", image, offset + 1, new_displacement)


def section_names(layout: PeLayout) -> tuple[bytes, ...]:
    return tuple(section.name for section in layout.sections)


def merge(v6_path: Path, two_hand_path: Path, output_path: Path) -> None:
    v6 = v6_path.read_bytes()
    two_hand = two_hand_path.read_bytes()
    if sha256(v6) != V6_SHA256:
        raise ValueError("V6 donor hash does not match the exact released V6 DLL")
    if sha256(two_hand) != TWO_HAND_SHA256:
        raise ValueError("two-hand base hash does not match the headset-tested candidate")

    donor_layout = parse_pe(v6)
    base_layout = parse_pe(two_hand)
    if section_names(base_layout) != (
        b".text",
        b".rdata",
        b".data",
        b".pdata",
        b".fptable",
        b".rsrc",
        b".reloc",
    ):
        raise ValueError("unexpected two-hand base section layout")
    if section_names(donor_layout)[-5:] != CUSTOM_SECTION_NAMES:
        raise ValueError("V6 donor is missing the expected post-build sections")
    if (
        base_layout.image_base != donor_layout.image_base
        or base_layout.file_alignment != donor_layout.file_alignment
        or base_layout.section_alignment != donor_layout.section_alignment
    ):
        raise ValueError("donor and base PE layouts are incompatible")

    base_raw_end = max(
        section.raw_pointer + section.raw_size for section in base_layout.sections
    )
    if base_raw_end != len(two_hand):
        raise ValueError("two-hand base contains an unexpected overlay")
    final_section_count = base_layout.section_count + len(CUSTOM_SECTION_NAMES)
    header_end = base_layout.section_table_offset + final_section_count * 40
    if header_end > base_layout.size_of_headers:
        raise ValueError("PE headers do not have room for the V6 sections")

    output = bytearray(two_hand)
    new_sections: list[Section] = list(base_layout.sections)
    raw_pointer = align_up(len(output), base_layout.file_alignment)
    if raw_pointer != len(output):
        output.extend(b"\0" * (raw_pointer - len(output)))

    donor_custom = {section.name: section for section in donor_layout.sections[-5:]}
    for index, name in enumerate(CUSTOM_SECTION_NAMES, start=base_layout.section_count):
        donor_section = donor_custom[name]
        if raw_pointer % base_layout.file_alignment:
            raise ValueError("new section raw pointer is not file-aligned")
        raw = v6[
            donor_section.raw_pointer : donor_section.raw_pointer + donor_section.raw_size
        ]
        if len(raw) != donor_section.raw_size:
            raise ValueError(f"donor section {name.decode()} is truncated")
        header = bytearray(donor_section.header)
        struct.pack_into("<I", header, 20, raw_pointer)
        header_offset = base_layout.section_table_offset + index * 40
        output[header_offset : header_offset + 40] = header
        output.extend(raw)
        new_sections.append(
            Section(
                name=donor_section.name,
                virtual_size=donor_section.virtual_size,
                virtual_address=donor_section.virtual_address,
                raw_size=donor_section.raw_size,
                raw_pointer=raw_pointer,
                characteristics=donor_section.characteristics,
                header=bytes(header),
            )
        )
        raw_pointer += donor_section.raw_size

    struct.pack_into("<H", output, base_layout.file_header_offset + 2, final_section_count)
    final_image_size = align_up(
        max(s.virtual_address + s.virtual_size for s in new_sections),
        base_layout.section_alignment,
    )
    struct.pack_into("<I", output, base_layout.optional_header_offset + 56, final_image_size)
    size_of_code = sum(
        section.raw_size for section in new_sections if section.characteristics & 0x20
    )
    struct.pack_into("<I", output, base_layout.optional_header_offset + 4, size_of_code)
    struct.pack_into("<I", output, base_layout.optional_header_offset + 64, 0)

    merged_layout = parse_pe(output)
    if section_names(merged_layout)[-5:] != CUSTOM_SECTION_NAMES:
        raise ValueError("merged section table validation failed")
    for patch in BASE_CALL_PATCHES:
        patch_rel32(output, merged_layout, *patch)
    for patch in CUSTOM_CALL_PATCHES:
        patch_rel32(output, merged_layout, *patch)

    # Reparse and prove every redirected call resolves to its intended target.
    final_layout = parse_pe(output)
    for call_rva, _, new_target in BASE_CALL_PATCHES + CUSTOM_CALL_PATCHES:
        offset = rva_to_offset(final_layout, call_rva)
        displacement = struct.unpack_from("<i", output, offset + 1)[0]
        actual_target = call_rva + 5 + displacement
        if actual_target != new_target:
            raise ValueError(f"RVA 0x{call_rva:X}: rel32 verification failed")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    print(f"wrote {output_path}")
    print(f"size {len(output)} bytes")
    print(f"sha256 {sha256(output)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--v6", type=Path, required=True, help="exact released V6 DLL")
    parser.add_argument(
        "--two-hand", type=Path, required=True, help="exact headset-tested two-hand DLL"
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    merge(args.v6, args.two_hand, args.output)


if __name__ == "__main__":
    main()
