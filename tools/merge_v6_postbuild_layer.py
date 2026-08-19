#!/usr/bin/env python3
"""Merge the released V6 Halo 4 post-build layer into a guarded base DLL.

The public V6 source commit does not contain the five PE sections that were
added to the released DLL after linking.  Rebuilding that source therefore
drops accepted Halo 4 HUD, helmet, effect, muzzle, and pause behavior. This
tool is intentionally narrow: it accepts only the exact released V6 donor and
one of the explicitly verified base layouts below, ports the donor sections,
and retargets the small set of build-relative calls whose RVAs moved.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path


V6_SHA256 = "419f2ca425a41f3fe42a2f27cfd0ce55123f71c5ef34c1c45604018285efea82"
TWO_HAND_SHA256 = "60e5d57ebea0b4c1f9112fd3ae8029467f0dfbd2053786122448475230e6db5c"
RESTORED_TWO_HAND_SHA256 = (
    "9ca9b6da6f4e1dfb9b3cf23dd84c9134f7bb23619bdc423b7f7a7b45ca0c7c22"
)

# The cumulative 60c9198 source build changes code and link layout after the
# older d184 candidate. Its complete file hash is expected to change when the
# embedded 40-character source commit changes, while its executable layout
# must not. Therefore the new profile is guarded by both the exact raw .text
# hash and every stock PE section's geometry. A mismatch refuses the merge.
V6_TWO_HAND_CURRENT_TEXT_SHA256 = (
    "1b9da51e2c6657e3dfb5a373a1403e5b65697c51018794ec87757bee4183cf23"
)
V6_TWO_HAND_CURRENT_SECTION_GEOMETRY = (
    (b".text", 0x1586B0, 0x001000, 0x158800, 0x000400),
    (b".rdata", 0x0B2720, 0x15A000, 0x0B2800, 0x158C00),
    (b".data", 0x078A9C, 0x20D000, 0x068200, 0x20B400),
    (b".pdata", 0x00DBA8, 0x286000, 0x00DC00, 0x273600),
    (b".fptable", 0x000100, 0x294000, 0x000200, 0x281200),
    (b".rsrc", 0x004B60, 0x295000, 0x004C00, 0x281400),
    (b".reloc", 0x001054, 0x29A000, 0x001200, 0x286000),
)

# The 950f0ba pause-retention and solved-arm two-hand changes move both code
# and data relative to the accepted d145ece/60c9198 layout. This profile was
# recovered from an x64 Release link made with MSVC 19.44.35228 and is guarded
# independently so the earlier accepted layout remains valid and untouched.
PAUSE_TWO_HAND_TEXT_SHA256 = (
    "4eed9bb45fa63fcfbe186a4459c33da84ce844e2dc3e62c2b37056364627b69f"
)
PAUSE_TWO_HAND_SECTION_GEOMETRY = (
    (b".text", 0x158DB0, 0x001000, 0x158E00, 0x000400),
    (b".rdata", 0x0B2160, 0x15A000, 0x0B2200, 0x159200),
    (b".data", 0x078ABC, 0x20D000, 0x068200, 0x20B400),
    (b".pdata", 0x00DBE4, 0x286000, 0x00DC00, 0x273600),
    (b".fptable", 0x000100, 0x294000, 0x000200, 0x281200),
    (b".rsrc", 0x004B60, 0x295000, 0x004C00, 0x281400),
    (b".reloc", 0x00105C, 0x29A000, 0x001200, 0x286000),
)

CUSTOM_SECTION_NAMES = (b".h4fx", b".h4fd", b".h4hs", b".h4hp", b".h4pb")

# Calls changed by the released V6 post-build layer.  Candidate RVAs differ
# slightly because the two-hand source change moved a few compiled functions.
# Each tuple is (call RVA, expected stock target RVA, V6 wrapper target RVA).
LEGACY_BASE_CALL_PATCHES = (
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
LEGACY_CUSTOM_CALL_PATCHES = (
    (0x29C004, 0x04CE90, 0x04CEA0),
    (0x29C12D, 0x037F90, 0x037FA0),
    (0x29E06E, 0x0C78F0, 0x0C78B0),
    (0x29E08C, 0x0C78F0, 0x0C78B0),
    (0x29E410, 0x111270, 0x111220),
    (0x29E423, 0x1059D0, 0x105940),
    (0x29E436, 0x111270, 0x111220),
    (0x2A0065, 0x02A8B0, 0x02A8C0),
)

# Layout recovered from the cumulative 60c9198 source build. Nine V6 wrapper
# call sites are unchanged from d184; the two calls in the moved functions are
# now 0x48ACE and 0x4FC78. Four internal destinations also moved. These values
# are used only after the exact .text hash and section geometry above match.
CURRENT_BASE_CALL_PATCHES = (
    (0x003D52, 0x005830, 0x29F013),
    (0x0057FD, 0x005830, 0x29F013),
    (0x00C6D1, 0x001A20, 0x29F000),
    (0x013AEA, 0x001A20, 0x2A0000),
    (0x026105, 0x005830, 0x29F013),
    (0x026191, 0x005830, 0x29F013),
    (0x02E860, 0x111280, 0x29E40C),
    (0x02EFAC, 0x005830, 0x29F026),
    (0x02F034, 0x005830, 0x29F013),
    (0x048ACE, 0x04CEF0, 0x29C000),
    (0x04FC78, 0x001A20, 0x29C440),
)

CURRENT_CUSTOM_CALL_PATCHES = (
    (0x29C004, 0x04CE90, 0x04CEF0),
    (0x29C12D, 0x037F90, 0x037FA0),
    (0x29E06E, 0x0C78F0, 0x0C7910),
    (0x29E08C, 0x0C78F0, 0x0C7910),
    (0x29E410, 0x111270, 0x111280),
    (0x29E423, 0x1059D0, 0x1059A0),
    (0x29E436, 0x111270, 0x111280),
    (0x2A0065, 0x02A8B0, 0x02A8C0),
)

# Re-profiled from the exact 950f0ba MSVC link. Every base site was relocated
# by matching its surrounding accepted instruction stream, then its unpatched
# rel32 target was decoded and resolved to the named linker-map symbol. The
# donor-side expected targets remain the released V6 instruction operands.
PAUSE_TWO_HAND_BASE_CALL_PATCHES = (
    (0x003C72, 0x005750, 0x29F013),  # ConfigSave
    (0x00571D, 0x005750, 0x29F013),  # ConfigSave
    (0x00C2E1, 0x0019D0, 0x29F000),  # Logf
    (0x01350A, 0x0019D0, 0x2A0000),  # Logf
    (0x025B25, 0x005750, 0x29F013),  # ConfigSave
    (0x025BB1, 0x005750, 0x29F013),  # ConfigSave
    (0x02DEB0, 0x111860, 0x29E40C),  # ImGui::TextDisabled
    (0x02E5FC, 0x005750, 0x29F026),  # ConfigSave
    (0x02E684, 0x005750, 0x29F013),  # ConfigSave
    (0x04834E, 0x04C770, 0x29C000),  # Halo4SafeRead / SafeReadBytes
    (0x04F4F8, 0x0019D0, 0x29C440),  # Logf
)

PAUSE_TWO_HAND_CUSTOM_CALL_PATCHES = (
    (0x29C004, 0x04CE90, 0x04C770),  # Halo4SafeRead / SafeReadBytes
    (0x29C12D, 0x037F90, 0x037CD0),  # ControllerWorldPoseEx
    (0x29E06E, 0x0C78F0, 0x0C78D0),  # EnableHook
    (0x29E08C, 0x0C78F0, 0x0C78D0),  # EnableHook
    (0x29E410, 0x111270, 0x111860),  # ImGui::TextDisabled
    (0x29E423, 0x1059D0, 0x105590),  # ImGui::ButtonBehavior
    (0x29E436, 0x111270, 0x111860),  # ImGui::TextDisabled
    (0x2A0065, 0x02A8B0, 0x02A6E0),  # ValidateStereoImagesOnce
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


@dataclass(frozen=True)
class MergeProfile:
    name: str
    base_call_patches: tuple[tuple[int, int, int], ...]
    custom_call_patches: tuple[tuple[int, int, int], ...]
    exact_base_sha256: str | None = None
    text_sha256: str | None = None
    section_geometry: tuple[tuple[bytes, int, int, int, int], ...] | None = None
    expected_output_sha256: str | None = None


LEGACY_PROFILE = MergeProfile(
    name="d184 headset-tested two-hand base",
    exact_base_sha256=TWO_HAND_SHA256,
    base_call_patches=LEGACY_BASE_CALL_PATCHES,
    custom_call_patches=LEGACY_CUSTOM_CALL_PATCHES,
    expected_output_sha256=RESTORED_TWO_HAND_SHA256,
)

CURRENT_PROFILE = MergeProfile(
    name="60c9198 cumulative V6/two-hand code layout",
    text_sha256=V6_TWO_HAND_CURRENT_TEXT_SHA256,
    section_geometry=V6_TWO_HAND_CURRENT_SECTION_GEOMETRY,
    base_call_patches=CURRENT_BASE_CALL_PATCHES,
    custom_call_patches=CURRENT_CUSTOM_CALL_PATCHES,
)

PAUSE_TWO_HAND_PROFILE = MergeProfile(
    name="950f0ba pause-retain/solved-arm two-hand layout (MSVC 19.44)",
    text_sha256=PAUSE_TWO_HAND_TEXT_SHA256,
    section_geometry=PAUSE_TWO_HAND_SECTION_GEOMETRY,
    base_call_patches=PAUSE_TWO_HAND_BASE_CALL_PATCHES,
    custom_call_patches=PAUSE_TWO_HAND_CUSTOM_CALL_PATCHES,
)


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


def section_geometry(
    layout: PeLayout,
) -> tuple[tuple[bytes, int, int, int, int], ...]:
    return tuple(
        (
            section.name,
            section.virtual_size,
            section.virtual_address,
            section.raw_size,
            section.raw_pointer,
        )
        for section in layout.sections
    )


def raw_section_sha256(data: bytes, layout: PeLayout, name: bytes) -> str:
    section = next((section for section in layout.sections if section.name == name), None)
    if section is None:
        raise ValueError(f"missing {name.decode()} section")
    raw = data[section.raw_pointer : section.raw_pointer + section.raw_size]
    if len(raw) != section.raw_size:
        raise ValueError(f"{name.decode()} section is truncated")
    return sha256(raw)


def select_profile(data: bytes, layout: PeLayout) -> MergeProfile:
    complete_hash = sha256(data)
    if complete_hash == LEGACY_PROFILE.exact_base_sha256:
        return LEGACY_PROFILE
    text_hash = raw_section_sha256(data, layout, b".text")
    geometry = section_geometry(layout)
    for profile in (CURRENT_PROFILE, PAUSE_TWO_HAND_PROFILE):
        if text_hash == profile.text_sha256 and geometry == profile.section_geometry:
            return profile
    raise ValueError(
        "unrecognized base DLL: complete SHA-256 "
        f"{complete_hash}, .text SHA-256 {text_hash}; no guarded layout matches"
    )


def verify_rel32(
    image: bytes | bytearray,
    layout: PeLayout,
    call_rva: int,
    expected_target_rva: int,
) -> None:
    offset = rva_to_offset(layout, call_rva)
    if image[offset] != 0xE8:
        raise ValueError(
            f"RVA 0x{call_rva:X}: expected CALL rel32, found 0x{image[offset]:02X}"
        )
    displacement = struct.unpack_from("<i", image, offset + 1)[0]
    actual_target = call_rva + 5 + displacement
    if actual_target != expected_target_rva:
        raise ValueError(
            f"RVA 0x{call_rva:X}: expected target 0x{expected_target_rva:X}, "
            f"found 0x{actual_target:X}"
        )


def verify_original_diff_scope(
    original: bytes,
    merged: bytes | bytearray,
    base_layout: PeLayout,
    profile: MergeProfile,
) -> None:
    allowed: set[int] = set()

    def allow(offset: int, size: int) -> None:
        allowed.update(range(offset, offset + size))

    allow(base_layout.file_header_offset + 2, 2)  # NumberOfSections
    allow(base_layout.optional_header_offset + 4, 4)  # SizeOfCode
    allow(base_layout.optional_header_offset + 56, 4)  # SizeOfImage
    allow(base_layout.optional_header_offset + 64, 4)  # CheckSum
    allow(
        base_layout.section_table_offset + base_layout.section_count * 40,
        len(CUSTOM_SECTION_NAMES) * 40,
    )
    for call_rva, _, _ in profile.base_call_patches:
        allow(rva_to_offset(base_layout, call_rva) + 1, 4)

    unexpected = [
        offset
        for offset, (before, after) in enumerate(zip(original, merged))
        if before != after and offset not in allowed
    ]
    if unexpected:
        first = unexpected[0]
        raise ValueError(
            f"merge changed base byte 0x{first:X} outside the guarded headers/calls"
        )


def verify_custom_section_diff_scope(
    donor: bytes,
    donor_layout: PeLayout,
    merged: bytes | bytearray,
    merged_layout: PeLayout,
    profile: MergeProfile,
) -> None:
    patched_displacements = {
        byte_rva
        for call_rva, _, _ in profile.custom_call_patches
        for byte_rva in range(call_rva + 1, call_rva + 5)
    }
    donor_sections = {section.name: section for section in donor_layout.sections}
    merged_sections = {section.name: section for section in merged_layout.sections}
    for name in CUSTOM_SECTION_NAMES:
        source = donor_sections[name]
        target = merged_sections[name]
        if (
            source.virtual_size != target.virtual_size
            or source.virtual_address != target.virtual_address
            or source.raw_size != target.raw_size
            or source.characteristics != target.characteristics
        ):
            raise ValueError(f"merged {name.decode()} geometry differs from donor")
        source_raw = donor[source.raw_pointer : source.raw_pointer + source.raw_size]
        target_raw = merged[target.raw_pointer : target.raw_pointer + target.raw_size]
        for delta, (before, after) in enumerate(zip(source_raw, target_raw)):
            rva = source.virtual_address + delta
            if before != after and rva not in patched_displacements:
                raise ValueError(
                    f"merged {name.decode()} byte RVA 0x{rva:X} differs outside a redirect"
                )


def merge(
    v6_path: Path,
    two_hand_path: Path,
    output_path: Path,
    expected_base_sha256: str | None = None,
) -> None:
    v6 = v6_path.read_bytes()
    two_hand = two_hand_path.read_bytes()
    if sha256(v6) != V6_SHA256:
        raise ValueError("V6 donor hash does not match the exact released V6 DLL")
    base_hash = sha256(two_hand)
    if expected_base_sha256 is not None and base_hash != expected_base_sha256.lower():
        raise ValueError(
            f"base hash {base_hash} does not match --expected-base-sha256 "
            f"{expected_base_sha256.lower()}"
        )

    donor_layout = parse_pe(v6)
    base_layout = parse_pe(two_hand)
    profile = select_profile(two_hand, base_layout)
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
    for patch in profile.base_call_patches:
        patch_rel32(output, merged_layout, *patch)
    for patch in profile.custom_call_patches:
        patch_rel32(output, merged_layout, *patch)

    # Reparse and prove every redirected call resolves to its intended target.
    final_layout = parse_pe(output)
    for call_rva, _, new_target in (
        profile.base_call_patches + profile.custom_call_patches
    ):
        verify_rel32(output, final_layout, call_rva, new_target)
    verify_original_diff_scope(two_hand, output, base_layout, profile)
    verify_custom_section_diff_scope(
        v6, donor_layout, output, final_layout, profile
    )

    output_hash = sha256(output)
    if (
        profile.expected_output_sha256 is not None
        and output_hash != profile.expected_output_sha256
    ):
        raise ValueError(
            f"merged output hash {output_hash} does not match the proven "
            f"{profile.expected_output_sha256}"
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    print(f"profile {profile.name}")
    print(f"base sha256 {base_hash}")
    print(f"base .text sha256 {raw_section_sha256(two_hand, base_layout, b'.text')}")
    print(
        f"verified {len(profile.base_call_patches)} base redirects, "
        f"{len(profile.custom_call_patches)} internal redirects, and donor diff scope"
    )
    print(f"wrote {output_path}")
    print(f"size {len(output)} bytes")
    print(f"sha256 {output_hash}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--v6", type=Path, required=True, help="exact released V6 DLL")
    parser.add_argument(
        "--two-hand", type=Path, required=True, help="guarded unmerged base DLL"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--expected-base-sha256",
        help="optional complete base hash recorded by the candidate build",
    )
    args = parser.parse_args()
    merge(
        args.v6,
        args.two_hand,
        args.output,
        expected_base_sha256=args.expected_base_sha256,
    )


if __name__ == "__main__":
    main()
