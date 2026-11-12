"""Read-only, hash-pinned Halo 3 Ultimate Firefight FP asset inspection.

Cache metadata only; this tool establishes no engine hook or runtime binding.
The v13 Halo 3 cache uses a tag-index signature at +0x44 and direct packed
virtual addresses, unlike the Reach cache inspected separately.
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
        if digest != "55382adaef39d0e1508256135d0ba028147fc1af1d561389a6fd8be0547a827c":
            raise ValueError("Unsupported cache: expected the supplied firefight_shrine.map")

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
            return virtual(address << 2, size)

        header = virtual(tag_header, 0x4C)
        if data[header + 0x44:header + 0x48] != b"sgat":
            raise ValueError("Missing tag index signature")
        groups = virtual(unpack("<Q", header + 8)[0], u32(header) * 16)
        tag_count = u32(header + 16)
        instances = virtual(unpack("<Q", header + 24)[0], tag_count * 8)
        if tag_count != u32(0x20):
            raise ValueError("Tag names and instances disagree")
        names, names_size, name_index = unpack("<III", 0x24)
        string_count, strings, strings_size, string_index = unpack("<IIII", 0x30)
        set_count, set_table = unpack("<II", 0x40)
        sets = unpack(f"<{set_count}I", set_table)

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
            count, address = unpack("<II", descriptor + (0x14 if animation else 0x30))
            if not 0 < count <= 120:
                raise ValueError("Unsupported node count")
            stride = 32 if animation else 96
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

        result=[]
        for index in (0x40e1,0x4279,0x4225,0x1ef1):
            t=tags[index]; start=packed(t["address"])
            if u32(start+0x3FC) != 2:
                raise ValueError("Unexpected first-person block count")
            refs=packed(u32(start+0x400),64)
            model=reference(refs,"mode"); anim=reference(refs+16,"jmad")
            graph=nodes(anim,True); modelnodes=nodes(model)
            byid={n["string_id"]:n["index"] for n in graph}
            if any(n["string_id"] not in byid for n in modelnodes):
                raise ValueError("Weapon model node absent from its animation graph")
            # Hash-pinned English localization chunk in this supplied cache.
            pickup_id=u32(start+0xFC)
            record=data.find(struct.pack("<I",pickup_id),0x7C3B0000,0x7C3BA000)
            pickup=None
            if record>=0 and record%8==0:
                text_offset=0x7C3BA000+u32(record+4)
                if not 0x7C3BA000<=text_offset<0x7C400000:
                    raise ValueError("Invalid localized pickup string")
                end=data.find(b"\0",text_offset,0x7C400000)
                if end<0: raise ValueError("Unterminated localized pickup string")
                pickup=dict(string_id=hex(pickup_id),identifier=string_id(pickup_id),
                    record_offset=hex(record),text_offset=hex(text_offset),
                    text=data[text_offset:end].decode("utf-8"))
            result.append(dict(weapon=t,model=model,animation=anim,graph=graph,
                               model_nodes=modelnodes,pickup=pickup))
        return dict(map_name=cstring(0xC0,0xE0),sha256=digest,size=len(data),weapons=result)

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    result=inspect(args.map)
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    for weapon in result["weapons"]:
        print(weapon["weapon"]["name"],len(weapon["graph"]),"graph nodes")
