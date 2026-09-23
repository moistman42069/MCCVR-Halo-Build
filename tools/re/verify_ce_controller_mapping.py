"""Verify CE's own kit-to-retail controller mapping evidence, without execution."""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
import struct
import sys

from pe import PE

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ROOT / "out/deps/re-tools/inputs"
NAMES = (
    "jump switch_grenade action switch_weapon melee flashlight throw_grenade fire "
    "accept back crouch zoom showscores reload exchange_weapon screenshot zoom_in "
    "zoom_out secondary_fire ext_vehicle_2 ext_vehicle_3 navpoint_ping next_grenade "
    "previous_grenade flashlight_alt forward backward left right look_up look_down "
    "look_left look_right"
).split()
MASKS = (1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80,
         0x1000, 0x2000, 0x4000, 0x8000, 0x100, 0x200)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def read_image(filename: str, digest: str) -> PE:
    pe = PE(INPUTS / filename)
    require(hashlib.sha256(pe.blob).hexdigest().upper() == digest,
            f"{filename}: pinned hash mismatch")
    return pe


def names_at(pe: PE, rva: int, pointer_size: int, image_base: int) -> list[str]:
    result = []
    for index in range(33):
        pointer = struct.unpack_from("<I" if pointer_size == 4 else "<Q",
                                     pe.blob, pe.off(rva) + index * pointer_size)[0]
        offset = pe.off(pointer - image_base)
        result.append(pe.blob[offset:pe.blob.index(b"\0", offset)].decode("ascii"))
    return result


def unique(pe: PE, rva: int, pattern: str) -> None:
    expression = b"".join(b"." if item == "??" else re.escape(bytes([int(item, 16)]))
                          for item in pattern.split())
    matches = [pe.rva(match.start()) for match in re.finditer(expression, pe.blob, re.DOTALL)]
    require(matches == [rva], f"{rva:X}: expected unique pattern, got {matches}")


def main() -> None:
    kit = read_image("halo_tag_test.exe",
        "FC9E2B6193C6F6D9FF988278B0D39A983747F3FDBDECA7CAFADD28F1F0C53E73")
    retail = read_image("halo1.dll",
        "0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C")
    require(names_at(kit, 0x8117B0, 4, 0x400000) == NAMES, "kit action order")
    require(names_at(retail, 0x1887C20, 8, 0x180000000) == NAMES, "retail action order")
    require(struct.unpack_from("<14I", kit.blob, kit.off(0x686CD8)) == MASKS, "kit XI masks")
    require(struct.unpack_from("<14I", retail.blob, retail.off(0x18A8AC0)) == MASKS,
            "retail XI masks")
    unique(retail, 0xADC5A4,
        "4C 8B CA 48 8D 15 ?? ?? ?? ?? 83 F9 04 7D 0D 48 63 C1 "
        "4C 69 C0 A0 08 00 00 49 03 D0 41 B8 A0 08 00 00 49 8B C9 E9 ?? ?? ?? ??")
    unique(retail, 0xADE503,
        "49 6B C7 21 44 8B EE 48 89 44 24 40 4D 63 FD 49 03 C7 "
        "45 0F B7 A4 46 2A 01 00 00 83 C8 FF 66 44 3B E0 0F 84 1B 03 00 00")
    unique(retail, 0xADD7DA,
        "4D 63 E5 48 8D 05 ?? ?? ?? ?? 4D 69 F4 A0 08 00 00 "
        "41 8B CD 4C 8D 0D ?? ?? ?? ?? 4C 03 F3")
    unique(retail, 0xC0E418,
        "49 8D 51 12 BB 0E 00 00 00 4C 8D 1D ?? ?? ?? ?? "
        "4D 8D 41 24 41 0F B7 4A 04 41 23 0B")
    unique(retail, 0xC0E378,
        "66 45 85 C0 74 06 41 8A 52 07 EB 04 41 8A 52 06 "
        "49 0F BF C8 41 8A 5C 09 08 42 88 14 09")
    displacement = struct.unpack_from("<i", retail.blob, retail.off(0xADC5A4) + 6)[0]
    require(0xADC5A4 + 10 + displacement == 0x2EA0610, "preference RIP target")
    mask_displacement = struct.unpack_from("<i", retail.blob, retail.off(0xC0E421) + 3)[0]
    require(0xC0E421 + 7 + mask_displacement == 0x18A8AC0, "XI mask RIP target")
    print("CE mapping evidence passed: pinned hashes, 66 action names, 28 masks, "
          "five unique consumers, two resolved pointers.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print(f"CE mapping evidence failed: {error}", file=sys.stderr)
        sys.exit(1)
