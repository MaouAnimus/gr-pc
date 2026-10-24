import json

data = json.load(open('docs/evidence/imports.json', 'r', encoding='utf-8'))
gnm_imps = [x for x in data['imports'] if x.get('library') == 'libSceGnmDriver']
print(f"Total libSceGnmDriver imports in imports.json: {len(gnm_imps)}")
for x in gnm_imps:
    print(f"idx={x['index']:3d} nid={x['nid']:12s} name={str(x.get('verified_symbol')):35s} relocs={x.get('relocation_count')}")
