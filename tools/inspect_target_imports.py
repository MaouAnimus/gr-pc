import json

with open("docs/evidence/imports.json", "r", encoding="utf-8") as f:
    data = json.load(f)

target_libs = {"libc", "libkernel", "libSceVideoOut", "libScePad", "libSceAudioOut", "libSceFiber"}
by_lib = {}
for imp in data["imports"]:
    lib = imp.get("library")
    if lib in target_libs:
        if lib not in by_lib:
            by_lib[lib] = []
        by_lib[lib].append(imp)

for lib, imps in sorted(by_lib.items()):
    print(f"=== {lib} ({len(imps)} imports) ===")
    for imp in imps:
        nid = imp["nid"]
        sym = imp.get("verified_symbol")
        print(f"  {nid}: {sym}")
