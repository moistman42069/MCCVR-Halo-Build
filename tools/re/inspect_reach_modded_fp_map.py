"""Read the supplied MCC Reach cache without modifying it or deriving hooks.

This deliberately supports the inspected v13 cache layout only. Validate all
spans, tag-reference salts, node identities and the official Elite body checksum
before emitting the selected weapon comparison. No geometry is exported.
"""
import argparse
import hashlib
import json
import mmap
import struct
from pathlib import Path


def inspect(path):
    with path.open("rb") as stream, mmap.mmap(
        stream.fileno(), 0, access=mmap.ACCESS_READ
    ) as data:
        def unpack(fmt, offset):
            size = struct.calcsize(fmt)
            if offset < 0 or offset + size > len(data):
                raise ValueError("Cache read outside file")
            return struct.unpack_from(fmt, data, offset)

        def u32(offset):
            return unpack("<I", offset)[0]

        def cstring(offset, end):
            if not 0 <= offset < end <= len(data):
                raise ValueError("Invalid string span")
            terminator = data.find(b"\0", offset, end)
            if terminator < 0:
                raise ValueError("Unterminated cache string")
            return data[offset:terminator].decode("ascii")

        if data[:4] != b"daeh" or u32(4) != 13 or u32(8) != len(data):
            raise ValueError("Unsupported cache header or incomplete download")
        digest = hashlib.sha256(data).hexdigest()
        if digest != "75d7aa599da3aeab9ae54f4a49b00ddf5854657b8131075cc306cf840a0eea28":
            raise ValueError("This inspector is pinned to the supplied Artifact cache")
        meta_start, meta_size = unpack("<II", 0x10)
        virtual_base, tag_header = unpack("<QQ", 0x2E0)

        def virtual(address, size=1):
            offset = meta_start + address - virtual_base
            if not meta_start <= offset <= meta_start + meta_size - size:
                raise ValueError("Tag pointer outside metadata section")
            unpack(f"<{size}s", offset)
            return offset

        def packed(address, size=1):
            if not address:
                raise ValueError("Null packed tag pointer")
            return virtual((address << 2) + 0x50000000, size)

        header = virtual(tag_header, 0x4C)
        if data[header + 0x48:header + 0x4C] != b"sgat":
            raise ValueError("Missing tag index signature")
        groups = virtual(unpack("<Q", header + 8)[0], u32(header) * 16)
        tag_count = u32(header + 16)
        instances = virtual(unpack("<Q", header + 24)[0], tag_count * 8)
        if tag_count != u32(0x20):
            raise ValueError("Tag names and instances disagree")
        names, names_size, name_index = unpack("<III", 0x24)
        string_count, strings, strings_size, string_index = unpack("<IIII", 0x30)
        set_count, set_table = unpack("<II", 0x40)
        if set_count != 17:
            raise ValueError("Unsupported string-ID sets")
        sets = unpack("<17I", set_table)

        def string_id(identifier):
            if identifier >> 24:
                raise ValueError("Unsupported string-ID encoding")
            set_id, index = identifier >> 16, identifier & 0xFFFF
            if set_id >= len(sets):
                raise ValueError("Invalid string-ID set")
            if set_id:
                index += sum(sets[:set_id])
            elif index >= sets[0]:
                index += sum(sets[1:])
            if index >= string_count:
                raise ValueError("String ID outside table")
            return cstring(strings + u32(string_index + index * 4),
                           strings + strings_size)

        tags = []
        for index in range(tag_count):
            group, salt, address = unpack("<HHI", instances + index * 8)
            null = group == 0xFFFF and salt == 0xFFFF and address == 0
            if group >= u32(header) and not null:
                raise ValueError("Invalid tag group")
            name = "" if null else cstring(names + u32(name_index + index * 4), names + names_size)
            tags.append(dict(index=index, salt=salt, address=address, name=name,
                             group="null" if null else data[groups + group * 16:groups + group * 16 + 4][::-1].decode("ascii")))

        def find(group, name):
            matches = [t for t in tags if t["group"] == group and t["name"] == name]
            if len(matches) != 1:
                raise ValueError(f"Ambiguous tag: {group} {name}")
            return matches[0]

        def reference(offset, expected_group):
            group = data[offset:offset + 4][::-1].decode("ascii")
            datum = u32(offset + 12)
            index, salt = datum & 0xFFFF, datum >> 16
            if group != expected_group or index >= tag_count:
                raise ValueError("Invalid first-person tag reference")
            tag = tags[index]
            if tag["salt"] != salt or tag["group"] != group:
                raise ValueError("First-person tag reference identity mismatch")
            return tag

        def nodes(tag, animation=False):
            descriptor = packed(tag["address"], 0x54)
            count, address = unpack("<II", descriptor + (0x48 if animation else 0x30))
            if not 0 < count <= 120:
                raise ValueError("Unsupported node count")
            stride = 44 if animation else 96
            start = packed(address, count * stride)
            result = []
            for index in range(count):
                record = start + index * stride
                identifier = u32(record)
                parent = unpack("<h", record + (8 if animation else 4))[0]
                if not -1 <= parent < count or parent == index:
                    raise ValueError("Invalid node parent")
                result.append(dict(index=index, string_id=identifier,
                                   name=string_id(identifier), parent=parent))
            if len({n["string_id"] for n in result}) != count:
                raise ValueError("Ambiguous node names")
            return result

        body = find("mode", "objects\\characters\\elite\\fp")
        body_nodes = nodes(body)
        checksum = u32(packed(body["address"], 12) + 8)
        if checksum != 0x19021311 or len(body_nodes) != 41:
            raise ValueError("This comparison requires the official Elite FP body")
        comparison = []
        selected = (0x20CB, 0x21DB, 0x24E3, 0x22E2, 0x2377, 0x2629,
                    0x3ECB, 0x3EA8, 0x2645)
        for index in selected:
            weapon = tags[index]
            if weapon["group"] != "weap":
                raise ValueError("Selected tag is not a weapon")
            name = weapon["name"]
            descriptor = packed(weapon["address"], 0x3C4)
            count, address = unpack("<II", descriptor + 0x3B8)
            if count != 2:
                raise ValueError("Unexpected first-person reference block")
            references = packed(address, count * 32)
            model = reference(references, "mode")
            animation = reference(references + 16, "jmad")
            graph = nodes(animation, animation=True)
            by_id = {n["string_id"]: n["index"] for n in graph}
            mapping = [by_id.get(n["string_id"], -1) for n in body_nodes]
            weapon_nodes = nodes(model)
            held = [by_id.get(n["string_id"], -1) for n in weapon_nodes]
            if -1 in mapping or -1 in held or set(mapping) & set(held):
                raise ValueError("Missing or overlapping body/weapon node identities")
            extra = [n for n in graph if n["index"] not in set(mapping) | set(held)]
            pickup_strings = []
            # These offsets describe the English localization chunk in this
            # supplied cache only; they are not runtime bindings.
            for field in range(0x16C, 0x188, 4):
                identifier = u32(descriptor + field)
                if not identifier:
                    continue
                record = data.find(struct.pack("<I", identifier), 0x493EC000, 0x493FC000)
                if record < 0 or record % 8:
                    continue
                start = 0x493FC000 + u32(record + 4)
                if not 0x493FC000 <= start < 0x49430000:
                    continue
                end = data.find(b"\0", start, 0x49430000)
                if end < 0:
                    raise ValueError("Unterminated localized string")
                pickup_strings.append(dict(field=hex(field), string_id=hex(identifier),
                    identifier_name=string_id(identifier), record_offset=hex(record),
                    text_offset=hex(start), text=data[start:end].decode("utf-8")))
            comparison.append(dict(weapon=name, weapon_tag=weapon["index"],
                first_person_model=model["name"], first_person_animation=animation["name"],
                model_checksum=f"0x{u32(packed(model['address'], 12) + 8):08X}",
                live_source_count=len(graph), body_to_source=mapping,
                weapon_to_source=held, extra_source_nodes=extra,
                pickup_strings=pickup_strings, graph_nodes=graph))
        return dict(map_name=cstring(0xC0, 0xE0), size=len(data),
                    sha256=digest,
                    body_checksum=f"0x{checksum:08X}", body_nodes=body_nodes,
                    weapons=comparison)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = inspect(args.map)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for weapon in result["weapons"]:
        print(weapon["weapon"], "graph nodes:", weapon["live_source_count"],
              "weapon sources:", weapon["weapon_to_source"])
