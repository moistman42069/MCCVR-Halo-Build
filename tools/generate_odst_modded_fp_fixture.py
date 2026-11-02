"""Generate independent ODST topology expectations from inspected user maps."""
import argparse
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
lines=['// Generated asset expectations; production code is included directly.',
       'struct Asset { uint32_t checksum; int count,bodyCount,modelCount,camera; std::array<int32_t,64> body,model,parents; };',
       'static constexpr Asset assets[]{']
for name in ('ODST-MODDED-WEAPONS-MAP-EVIDENCE.json','ODST-MYTHIC-FP-MAP-EVIDENCE.json'):
    evidence=json.loads((a.source_root/'docs'/name).read_text())
    for weapon in evidence['weapons']:
        for fp in weapon['first_person']:
            graph=fp['graph']
            if len(graph)>64:continue
            byid={n['string_id']:n['index'] for n in graph}
            cameras=[n['index'] for n in graph if n['name']=='camera_control']
            if len(cameras)!=1:continue
            for body in evidence['bodies']:
                if len(body['nodes']) not in (31,37):continue
                if not all(n['string_id'] in byid for n in body['nodes']):continue
                bodymap=[byid[n['string_id']] for n in body['nodes']]
                parents=[n['parent'] for n in graph]
                # Validate actual mapped topology, not a fixed graph prefix.
                if any(parents[bodymap[n['index']]] != (-1 if n['parent']==-1 else bodymap[n['parent']]) for n in body['nodes']):continue
                model=[byid[n['string_id']] for n in fp['model_nodes']]
                assert not set(bodymap)&set(model)
                if set(range(len(graph)))-set(bodymap)-set(model)!=set(cameras):continue
                values=[body['checksum'],len(graph),len(bodymap),len(model),cameras[0]]
                lines.append('{'+','.join(map(str,values))+',{' + ','.join(map(str,bodymap))+'},{'+','.join(map(str,model))+'},{'+','.join(map(str,parents))+'}},')
lines.append('};')
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text('\n'.join(lines)+'\n')
