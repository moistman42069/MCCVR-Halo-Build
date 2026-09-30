"""Verify Reach admitted-fire and native haptic bindings against pinned binaries."""
import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "out/pydeps"))
import pefile

RETAIL = ROOT / "out/deps/re-tools/inputs/haloreach.dll"
HREK = ROOT / "out/deps/re-tools/inputs/reach_tag_test.exe"
RETAIL_SHA = "738dd2d24ea3aea12e1ee9aa4a61094bf116027d42004c35a19e5048608b0894"
HREK_SHA = "cbdd8448a87a433b0dffc0de47d06db7a18b4bf868b96b057135daa86790aba8"


def pinned(path, expected):
    raw = path.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    assert actual == expected, f"{path.name} SHA-256 changed: {actual}"
    pe = pefile.PE(data=raw)
    return pe, pe.get_memory_mapped_image()


retail_pe, retail = pinned(RETAIL, RETAIL_SHA)
hre_pe, hre = pinned(HREK, HREK_SHA)
contract = (ROOT / "src/common/reach_haptic_contract.h").read_text()

# Each native binding is a unique exact byte prefix in the pinned retail image.
section = retail_pe.sections[0]
text = retail[section.VirtualAddress:section.VirtualAddress + section.Misc_VirtualSize]
entries = re.findall(r'\{([a-z_]+),\s*\n?\s*"([0-9A-F ]+)"\}', contract)
assert len(entries) == 15, f"expected 15 retail function signatures, got {len(entries)}"
for name, pattern in entries:
    signature = bytes.fromhex(pattern)
    matches = [section.VirtualAddress + m.start() for m in re.finditer(re.escape(signature), text)]
    expected = int(re.search(rf'{name}\s*=\s*0x([0-9A-Fa-f]+)', contract).group(1), 16)
    assert matches == [expected], f"{name}: expected unique RVA {expected:#x}, got {matches}"

# Direct E8 edges declared by the C++ contract must land on their exact targets.
symbols = {name: int(value, 16) for name, value in
           re.findall(r'inline constexpr uint32_t ([a-z_]+)\s*=\s*0x([0-9A-Fa-f]+)', contract)}
edge_block = contract.split("inline constexpr Edge edges[]{", 1)[1].split("};", 1)[0]
edges = []
for left, right in re.findall(r'\{([^,]+),\s*([^}]+)\}', edge_block):
    def symbol_value(token):
        token = token.strip()
        return int(token, 16) if token.startswith("0x") else symbols[token]
    edges.append((symbol_value(left), symbol_value(right)))
for call, target in edges:
    assert retail[call] == 0xE8, f"expected CALL at {call:#x}"
    actual = call + 5 + struct.unpack_from("<i", retail, call + 1)[0]
    assert actual == target, f"call {call:#x}: {actual:#x} != {target:#x}"

# Fixed byte witnesses establish the successful-shot branch, primary/secondary
# effect dispatches, and local-user queue write site.
witness_block = contract.split("inline constexpr Witness witnesses[]{", 1)[1].split("};", 1)[0]
witnesses = [(int(a, 16), bytes.fromhex(p)) for a, p in
             re.findall(r'\{0x([0-9A-Fa-f]+),\s*"([0-9A-F ]+)"\}', witness_block)]
assert len(witnesses) == 7, f"expected seven exact byte witnesses, got {len(witnesses)}"
for address, pattern in witnesses:
    assert retail[address:address + len(pattern)] == pattern, f"byte witness mismatch at {address:#x}"

# Verify RIP-relative data references to the tag table and group-dispatch table.
data_block = contract.split("inline constexpr DataReference data_references[]{", 1)[1].split("};", 1)[0]
refs = [(int(i, 16), int(size), int(disp), symbols[target.strip()]) for i, size, disp, target in
        re.findall(r'\{0x([0-9A-Fa-f]+),\s*(\d+),\s*(\d+),\s*([a-z_]+)\}', data_block)]
assert len(refs) == 2
for insn, size, displacement_offset, target in refs:
    displacement = struct.unpack_from("<i", retail, insn + displacement_offset)[0]
    assert insn + size + displacement == target, f"RIP reference at {insn:#x} misses {target:#x}"

# HREK is the discovery authority: confirm its semantic call chain independently.
kit_edges = [(0xCD761F, 0xDE86B0), (0xDE9027, 0xDE93D0),
             (0xDE9054, 0xDE9220), (0xDE92B9, 0xD70740),
             (0xD708FE, 0x3A3880), (0xDF7AB7, 0xDF64E0),
             (0xDF68EC, 0x3A3880), (0xDF6B69, 0x3A3880)]
for call, target in kit_edges:
    assert hre[call] == 0xE8, f"HREK expected CALL at {call:#x}"
    actual = call + 5 + struct.unpack_from("<i", hre, call + 1)[0]
    assert actual == target, f"HREK edge {call:#x}: {actual:#x} != {target:#x}"

print(f"PASS: pinned HREK/retail hashes; {len(entries)} unique retail signatures, "
      f"{len(edges)} retail edges, {len(witnesses)} byte witnesses, "
      f"{len(refs)} data references, {len(kit_edges)} HREK semantic edges")
