"""Extract XAM FP reference pairs and root-local firing marker rotations."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]

def inspect(path):
    data=path.read_bytes()
    digest=hashlib.sha256(data).hexdigest()
    if digest!=json.loads((ROOT/'docs/CE-XAM-MAP-EVIDENCE.json').read_text())['sha256']:
        raise ValueError('Expected supplied a30.map')
    u=lambda at:struct.unpack_from('<I',data,at)[0]
    meta=u(16)
    def ptr(v):
        at=meta+v-0x50000000
        if not meta<=at<len(data):raise ValueError('Invalid metadata pointer')
        return at
    def string(at):return data[at:data.index(b'\0',at)].decode('ascii')
    tags=[]
    for i in range(u(meta+12)):
        at=meta+40+i*32
        tags.append(dict(index=i,group=data[at:at+4][::-1].decode('ascii'),
                         name=string(ptr(u(at+16))),address=u(at+20)))
    assets=[]
    for weapon in tags:
        if weapon['group']!='weap' or weapon['name'] not in (
            'weapons\\hl1 mp5\\hl1 mp5','weapons\\hl2 smg\\hl2 smg','weapons\\m4a1\\m4a1'):continue
        weapon['offset']=ptr(weapon['address'])
        model=tags[u(weapon['offset']+0x468)&65535]
        graph=tags[u(weapon['offset']+0x478)&65535]
        if model['group']!='mod2' or graph['group']!='antr':raise ValueError('Invalid FP reference')
        model['offset']=ptr(model['address']);graph['offset']=ptr(graph['address'])
        count=u(graph['offset']+0x68);start=ptr(u(graph['offset']+0x6c))
        identity=14695981039346656037
        payload=bytes([count])
        for i in range(count):
            at=start+i*64
            payload+=string(at).encode()+b'\0'+data[at+36:at+38]
        for value in payload:identity=((identity^value)*1099511628211)&((1<<64)-1)
        model_nodes=ptr(u(model['offset']+0xbc))
        markers=ptr(u(model['offset']+0xb0));found=[]
        for i in range(u(model['offset']+0xac)):
            at=markers+i*64
            if string(at)!='primary trigger':continue
            if u(at+52)!=1:raise ValueError('Ambiguous marker')
            instance=ptr(u(at+56));node=data[instance+2]
            node_name=string(model_nodes+node*156)
            graph_matches=[i for i in range(count) if string(start+i*64)==node_name]
            if len(graph_matches)!=1:raise ValueError('Ambiguous graph marker node')
            found.append(dict(node_name=node_name,graph_node=graph_matches[0],
                translation=struct.unpack_from('<3f',data,instance+4),rotation_xyzw=struct.unpack_from('<4f',data,instance+16)))
        if len(found)!=1:raise ValueError('Missing/ambiguous primary trigger')
        assets.append(dict(weapon=weapon['name'],model=model['name'],graph=graph['name'],count=count,
                           graph_identity=f'{identity:016X}',marker=found[0]))
    if len(assets)!=3:raise ValueError('Missing requested weapons')
    return dict(map_sha256=digest,assets=assets)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();args.output.write_text(json.dumps(inspect(args.map),indent=2)+'\n')
