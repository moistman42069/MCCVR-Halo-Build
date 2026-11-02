"""Read-only CE MCC XAM cache graph extraction; no runtime bindings derived."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def inspect(path):
    data=path.read_bytes()
    digest=hashlib.sha256(data).hexdigest()
    expected=json.loads((Path(__file__).resolve().parents[2]/'docs/CE-XAM-MAP-EVIDENCE.json').read_text())
    if digest!=expected['sha256']:raise ValueError('Not the supplied XAM a30.map')
    def u32(at):return struct.unpack_from('<I',data,at)[0]
    if data[:4]!=b'daeh' or u32(4)!=13 or u32(8)!=len(data):raise ValueError('Invalid CE cache')
    meta=u32(16)
    def pointer(value):
        offset=meta+value-0x50000000
        if not meta<=offset<len(data):raise ValueError('Pointer outside metadata')
        return offset
    def string(at):return data[at:data.index(b'\0',at)].decode('ascii')
    wanted={a['name'] for a in expected['assets']}
    assets=[]
    for index in range(u32(meta+12)):
        at=meta+40+index*32
        if data[at:at+4]!=b'rtna':continue
        name=string(pointer(u32(at+16)))
        if name not in wanted:continue
        descriptor=pointer(u32(at+20));count=u32(descriptor+0x68)
        if not 0<count<=64:raise ValueError('Invalid graph size')
        start=pointer(u32(descriptor+0x6c))
        nodes=[dict(name=string(start+i*64),parent=struct.unpack_from('<h',data,start+i*64+36)[0]) for i in range(count)]
        assets.append(dict(name=name,count=count,nodes=nodes))
    if len(assets)!=len(wanted):raise ValueError('Missing/duplicate requested graph')
    return dict(sha256=digest,assets=assets)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args()
    args.output.write_text(json.dumps(inspect(args.map),indent=2)+'\n')
