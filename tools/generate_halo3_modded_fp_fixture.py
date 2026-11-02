"""Extract production admission functions and independently inspected H3 assets."""
import argparse
import json
import re
from pathlib import Path
from generate_haptics_runtime_fixture import extract_function

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
output = ['// Generated production fixture; do not edit.']
for filename, signature in [
    ('legacy_runtime_weapon_bounds.inl', 'bool LegacyClassifyRuntimeWeapon('),
    ('game.cpp', 'bool Halo3SelectHeldPaletteSources('),
]:
    path = args.source_root.resolve() / 'src/dll' / filename
    source = path.read_text(encoding='utf-8-sig')
    matches = list(re.finditer(re.escape(signature) + r'[^;{]+\)\s*\{', source))
    if len(matches) != 1:
        raise ValueError(f'Ambiguous definition: {signature}')
    full_signature = matches[0].group().rsplit('{', 1)[0].strip()
    body, line = extract_function(source, full_signature)
    output.extend([f'#line {line} "{path.as_posix()}"', body])
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n\n'.join(output) + '\n', encoding='utf-8')
evidence = json.loads((args.source_root / 'docs/HALO3-MODDED-WEAPONS-MAP-EVIDENCE.json').read_text())
assets = ['// Expected topology from the supplied cache, not production rules.',
          'struct Asset { int count, modelCount, left, right, camera; std::array<int32_t,64> parents, model; };',
          'static constexpr Asset assets[]{']
for item in evidence['weapons']:
    byid = {n['string_id']: n['index'] for n in item['graph']}
    names = {n['name']: n['index'] for n in item['graph']}
    values = [len(item['graph']), len(item['model_nodes']), names['l_hand'], names['r_hand'], names['camera_control']]
    parents = [n['parent'] for n in item['graph']]
    model = [byid[n['string_id']] for n in item['model_nodes']]
    assets.append('{' + ','.join(map(str,values)) + ',{' + ','.join(map(str,parents)) + '},{' + ','.join(map(str,model)) + '}},')
assets.append('};')
args.output.with_name('halo3_modded_fp_assets.inl').write_text('\n'.join(assets)+'\n',encoding='utf-8')
