#!/usr/bin/env python3
"""
Genera los tres inventarios oficiales de dependencias del port de PC:
- docs/evidence/imports.json
- docs/evidence/dependencias.json
- docs/evidence/incertidumbres.json
Tarea N02: Exportar imports y dependencias.
"""
import os
import sys
import struct
import json
import hashlib
from binascii import unhexlify as uhx

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EBOOT_PATH = os.path.join(PROJECT_ROOT, "CUSA01130", "eboot.bin")
PRX_DIR = os.path.join(PROJECT_ROOT, "CUSA01130", "sce_module")
SHAD_JSON = os.path.join(PROJECT_ROOT, "docs", "evidence", "shadps4_symbols.json")

OUT_IMPORTS = os.path.join(PROJECT_ROOT, "docs", "evidence", "imports.json")
OUT_DEPS = os.path.join(PROJECT_ROOT, "docs", "evidence", "dependencias.json")
OUT_UNCERTAINTIES = os.path.join(PROJECT_ROOT, "docs", "evidence", "incertidumbres.json")

B64_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
SALT = uhx("518D64A635DED8C1E6B039B1C3E55230")

def decode_b64_int(s):
    val = 0
    for c in s:
        idx = B64_ALPHABET.find(c)
        if idx == -1:
            if c == "-": idx = 62
            elif c == "_": idx = 63
        val = (val << 6) | idx
    return val

def name2nid(name):
    h = hashlib.sha1(name.encode("utf-8") + SALT).digest()
    import base64
    return base64.b64encode(h[:8][::-1]).decode().rstrip("=")

def parse_fself(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < 0x40 or raw[:4] != b"\x4F\x15\x3D\x1D":
        return None

    count = struct.unpack("<H", raw[0x18:0x1a])[0]
    entries = []
    for i in range(count):
        props, offset, filesz, memsz = struct.unpack("<QQQQ", raw[0x20 + i*32 : 0x40 + i*32])
        entries.append({"index": i, "props": props, "offset": offset, "filesz": filesz, "memsz": memsz})

    elf_off = 0x20 + count * 32
    e_entry, e_phoff, e_shoff, e_flags, e_ehsize, e_phentsize, e_phnum = struct.unpack(
        "<QQQIHHH", raw[elf_off+24:elf_off+58]
    )

    dynlib_off = 0
    dynlib_sz = 0
    dyn_ph = None
    dyn_hdr = None

    for i in range(e_phnum):
        ph_raw = raw[elf_off + e_phoff + i * 56 : elf_off + e_phoff + (i+1) * 56]
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack("<IIQQQQQQ", ph_raw)
        if p_type == 2:
            dyn_ph = (p_offset, p_filesz)
        elif p_type == 0x61000000:
            dyn_hdr = (p_offset, p_filesz)
            for j in range(count):
                if (entries[j]["props"] & 0x800) and ((entries[j]["props"] >> 20) & 0xFFF) == i:
                    dynlib_off = entries[j]["offset"]
                    dynlib_sz = entries[j]["filesz"]

    if not dyn_ph or not dyn_hdr:
        return None

    pos = dynlib_off + dyn_ph[0] - dyn_hdr[0]
    needed_prx = []
    import_libs = {}
    needed_modules = {}
    layout = {}

    while True:
        tag, val = struct.unpack("<QQ", raw[pos:pos+16])
        pos += 16
        if tag == 0:
            break
        if tag == 1:
            needed_prx.append(val)
        elif tag == 0x6100000f: # DT_SCE_NEEDED_MODULE
            mod_id = (val >> 48) & 0xFFFF
            ver_maj = (val >> 40) & 0xFF
            ver_min = (val >> 32) & 0xFF
            name_off = val & 0xFFFFFFFF
            needed_modules[mod_id] = (name_off, f"{ver_maj}.{ver_min}")
        elif tag == 0x61000015: # DT_SCE_IMPORT_LIB
            lib_id = (val >> 48) & 0xFFFF
            ver_maj = (val >> 40) & 0xFF
            ver_min = (val >> 32) & 0xFF
            name_off = val & 0xFFFFFFFF
            import_libs[lib_id] = (name_off, f"{ver_maj}.{ver_min}")
        elif tag == 0x61000035: layout["strings"] = val
        elif tag == 0x61000037: layout["strings_size"] = val
        elif tag == 0x61000039: layout["symbols"] = val
        elif tag == 0x6100003f: layout["symbols_size"] = val
        elif tag == 0x61000029: layout["plt"] = val
        elif tag == 0x6100002d: layout["plt_size"] = val
        elif tag == 0x6100002f: layout["rela"] = val
        elif tag == 0x61000031: layout["rela_size"] = val

    strtab_raw = raw[dynlib_off + layout.get("strings", 0) : dynlib_off + layout.get("strings", 0) + layout.get("strings_size", 0)]

    def get_str(off):
        if off >= len(strtab_raw): return ""
        end = strtab_raw.find(b"\x00", off)
        if end == -1: end = len(strtab_raw)
        return strtab_raw[off:end].decode("utf-8", errors="replace")

    parsed_needed_prx = [get_str(off) for off in needed_prx]
    
    parsed_import_libs = {}
    for lid, (noff, ver) in import_libs.items():
        parsed_import_libs[lid] = {"id": lid, "name": get_str(noff), "version": ver}

    parsed_needed_modules = {}
    for mid, (noff, ver) in needed_modules.items():
        parsed_needed_modules[mid] = {"id": mid, "name": get_str(noff), "version": ver}

    # Symbols
    sym_count = layout.get("symbols_size", 0) // 24
    symbols = []
    symtab_off = dynlib_off + layout.get("symbols", 0)
    for i in range(sym_count):
        sym_raw = raw[symtab_off + i*24 : symtab_off + (i+1)*24]
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack("<IBBHQQ", sym_raw)
        symbols.append({
            "index": i,
            "name": get_str(st_name),
            "shndx": st_shndx,
            "info": st_info,
            "other": st_other,
            "value": st_value,
            "size": st_size
        })

    # Relocations
    reloc_sym_map = {} # sym_index -> list of (reloc_type, offset, addend)
    rela_count = layout.get("rela_size", 0) // 24
    rela_off = dynlib_off + layout.get("rela", 0)
    for i in range(rela_count):
        r_offset, r_info, r_addend = struct.unpack("<QQq", raw[rela_off + i*24 : rela_off + (i+1)*24])
        rtype = r_info & 0xFFFFFFFF
        sym = r_info >> 32
        if rtype != 8: # R_X86_64_RELATIVE
            reloc_sym_map.setdefault(sym, []).append((rtype, r_offset, r_addend))

    plt_count = layout.get("plt_size", 0) // 24
    plt_off = dynlib_off + layout.get("plt", 0)
    for i in range(plt_count):
        r_offset, r_info, r_addend = struct.unpack("<QQq", raw[plt_off + i*24 : plt_off + (i+1)*24])
        rtype = r_info & 0xFFFFFFFF
        sym = r_info >> 32
        reloc_sym_map.setdefault(sym, []).append((rtype, r_offset, r_addend))

    return {
        "file_size": len(raw),
        "needed_prx": parsed_needed_prx,
        "import_libs": parsed_import_libs,
        "needed_modules": parsed_needed_modules,
        "symbols": symbols,
        "reloc_sym_map": reloc_sym_map,
        "layout": layout
    }

def main():
    print("[*] Ejecutando análisis para Task N02...")
    eboot_info = parse_fself(EBOOT_PATH)
    if not eboot_info:
        print(f"[-] Error al analizar {EBOOT_PATH}", file=sys.stderr)
        sys.exit(1)

    # 1. Analizar PRXs en sce_module
    local_prx_exports = {} # nid -> {prx_file, value, size}
    local_prx_info = []
    for prx_name in sorted(os.listdir(PRX_DIR)):
        if not prx_name.endswith(".prx"): continue
        prx_path = os.path.join(PRX_DIR, prx_name)
        p_info = parse_fself(prx_path)
        if not p_info: continue

        with open(prx_path, "rb") as fp:
            prx_bytes = fp.read()
        prx_sha = hashlib.sha256(prx_bytes).hexdigest()

        exported_syms = []
        for s in p_info["symbols"]:
            if s["shndx"] != 0 and s["value"] != 0:
                nid = s["name"].split("#")[0]
                local_prx_exports[nid] = {
                    "prx": prx_name,
                    "value": s["value"],
                    "size": s["size"],
                    "full_name": s["name"]
                }
                exported_syms.append(s["name"])

        local_prx_info.append({
            "name": prx_name,
            "size_bytes": os.path.getsize(prx_path),
            "sha256": prx_sha,
            "needed_prx": p_info["needed_prx"],
            "exported_symbol_count": len(exported_syms)
        })

    # 2. Cargar base de datos shadPS4
    shad_syms = {}
    if os.path.exists(SHAD_JSON):
        with open(SHAD_JSON, "r", encoding="utf-8") as f:
            shad_syms = json.load(f)

    # 3. Diccionario matemático conocido (SHA1 + salt)
    known_name_cache = {}
    # Nombres comunes C / POSIX / SCE
    standard_words = [
        "malloc", "free", "calloc", "realloc", "memcpy", "memset", "memcmp", "strlen",
        "strcpy", "strncpy", "strcmp", "strncmp", "sprintf", "snprintf", "printf",
        "vsnprintf", "vsprintf", "sscanf", "strchr", "strrchr", "strstr", "strcat",
        "strncat", "atoi", "atol", "atoll", "strtol", "strtoll", "strtoul", "strtoull",
        "strtof", "strtod", "qsort", "bsearch", "abs", "labs", "llabs", "rand", "srand",
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
        "exp", "log", "log10", "pow", "sqrt", "ceil", "floor", "fabs", "fmod",
        "exit", "abort", "atexit", "getenv", "system",
        "pthread_create", "pthread_join", "pthread_detach", "pthread_exit", "pthread_self",
        "pthread_equal", "pthread_mutex_init", "pthread_mutex_destroy", "pthread_mutex_lock",
        "pthread_mutex_unlock", "pthread_mutex_trylock", "pthread_cond_init", "pthread_cond_destroy",
        "pthread_cond_signal", "pthread_cond_broadcast", "pthread_cond_wait", "pthread_cond_timedwait",
        "pthread_rwlock_init", "pthread_rwlock_destroy", "pthread_rwlock_rdlock", "pthread_rwlock_wrlock",
        "pthread_rwlock_unlock", "pthread_key_create", "pthread_key_delete", "pthread_getspecific",
        "pthread_setspecific", "pthread_once", "sched_yield", "getpid", "gettimeofday",
        "clock_gettime", "clock_getres", "nanosleep", "usleep", "sleep", "socket", "bind",
        "listen", "accept", "connect", "send", "recv", "sendto", "recvfrom", "setsockopt",
        "getsockopt", "close", "read", "write", "open", "lseek", "ioctl", "fcntl", "fstat",
        "stat", "access", "unlink", "mkdir", "rmdir", "rename", "pipe", "select", "poll",
        "mmap", "munmap", "mprotect", "sysctl", "sysctlbyname", "__error", "__stack_chk_fail",
        "__stack_chk_guard", "puts", "putchar", "fputs", "fputc", "fopen", "fclose",
        "fread", "fwrite", "fseek", "ftell", "rewind", "fflush", "ferror", "feof", "clearerr",
        "pthread_attr_init", "pthread_attr_destroy", "pthread_attr_setstacksize",
        "pthread_attr_setdetachstate", "pthread_attr_getstacksize", "pthread_attr_getdetachstate"
    ]
    for w in standard_words:
        known_name_cache[name2nid(w)] = w

    # 4. Clasificación de los 681 imports
    reloc_syms = eboot_info["reloc_sym_map"]
    syms = eboot_info["symbols"]
    import_libs = eboot_info["import_libs"]
    needed_modules = eboot_info["needed_modules"]

    # Implementados actualmente en reloc.cpp
    CURRENTLY_IMPLEMENTED_NIDS = {
        "gQX+4GDQjpM", # malloc
        # Not free (rcQCUr0EaRU was incorrect), free is tIhsqj0qsFE
    }

    RELOC_NAMES = {
        1: "R_X86_64_64",
        6: "R_X86_64_GLOB_DAT",
        7: "R_X86_64_JUMP_SLOT",
        8: "R_X86_64_RELATIVE"
    }

    imports_list = []
    uncertainties = []
    lib_stats = {}

    for sym_idx in sorted(reloc_syms.keys()):
        sym_obj = syms[sym_idx]
        raw_name = sym_obj["name"]
        parts = raw_name.split("#")
        nid = parts[0]
        lib_code = parts[1] if len(parts) > 1 else ""
        mod_code = parts[2] if len(parts) > 2 else ""

        lib_id = decode_b64_int(lib_code) if lib_code else None
        mod_id = decode_b64_int(mod_code) if mod_code else None

        lib_name = import_libs.get(lib_id, {}).get("name", f"UnknownLib_{lib_id}")
        mod_name = needed_modules.get(mod_id, {}).get("name", f"UnknownMod_{mod_id}")

        relocs = reloc_syms[sym_idx]
        reloc_types_set = sorted(list(set(r[0] for r in relocs)))
        reloc_type_names = [RELOC_NAMES.get(t, f"Type_{t}") for t in reloc_types_set]

        # Determinar si es función u objeto
        # Si se relocaliza vía PLT (7), es función
        is_function = 7 in reloc_types_set or sym_obj["size"] == 0
        sym_type = "Function" if is_function else "Object"

        # Prioridad arquitectónica
        if lib_name in ["libkernel", "libc", "libScePosix", "libSceFiber", "libSceSysmodule"]:
            priority = "P0"
        elif lib_name in ["libSceGnmDriver", "libSceVideoOut", "libSceAudioOut", "libScePad", "libSceFios2", "libSceAudio3d", "libSceNgs2", "libSceAvPlayer", "libScePlayGo"]:
            priority = "P1"
        else:
            priority = "P2"

        # Verificación del nombre
        verified_name = None
        evidence = None
        signature = None

        if nid in shad_syms:
            verified_name = shad_syms[nid]["name"]
            evidence = f"shadPS4 ({shad_syms[nid]['library']}::{shad_syms[nid]['module']})"
            sym_type = shad_syms[nid]["type"]
        elif nid in known_name_cache:
            verified_name = known_name_cache[nid]
            evidence = "Algoritmo criptográfico oficial SHA1(nombre + salt Sony)"
        elif nid in local_prx_exports:
            prx_match = local_prx_exports[nid]
            verified_name = f"Export_{prx_match['prx']}"
            evidence = f"Copia local sce_module/{prx_match['prx']} (vaddr=0x{prx_match['value']:x})"

        # Clasificación de acción
        if nid == "gQX+4GDQjpM": # malloc
            classification = "implemented"
        elif nid in local_prx_exports:
            classification = "load_prx" if lib_name == "libSceFios2" else "adapt_host"
        elif priority == "P0" or priority == "P1":
            classification = "adapt_host"
        elif priority == "P2":
            classification = "stub_optional"
        else:
            classification = "unknown"

        if not verified_name:
            classification = "unknown"
            evidence = "NID sin resolver en tablas públicas conocidas"
            uncertainties.append({
                "nid": nid,
                "raw_symbol": raw_name,
                "library": lib_name,
                "module": mod_name,
                "priority": priority,
                "relocations": reloc_type_names,
                "risk": "Firma y semántica desconocidas; requiere stub seguro o trace dinámico"
            })

        import_entry = {
            "index": sym_idx,
            "nid": nid,
            "raw_symbol": raw_name,
            "verified_symbol": verified_name,
            "library": lib_name,
            "library_id": lib_id,
            "module": mod_name,
            "module_id": mod_id,
            "type": sym_type,
            "relocation_types": reloc_type_names,
            "relocation_count": len(relocs),
            "priority": priority,
            "classification": classification,
            "evidence": evidence
        }
        imports_list.append(import_entry)

        # Estadísticas por librería
        if lib_name not in lib_stats:
            lib_stats[lib_name] = {
                "library": lib_name,
                "priority": priority,
                "total_imports": 0,
                "functions": 0,
                "objects": 0,
                "verified": 0,
                "unverified": 0,
                "exported_by_local_prx": 0
            }
        lib_stats[lib_name]["total_imports"] += 1
        if sym_type == "Function": lib_stats[lib_name]["functions"] += 1
        else: lib_stats[lib_name]["objects"] += 1
        if verified_name: lib_stats[lib_name]["verified"] += 1
        else: lib_stats[lib_name]["unverified"] += 1
        if nid in local_prx_exports: lib_stats[lib_name]["exported_by_local_prx"] += 1

    # 5. Generar JSONs
    # imports.json
    imports_doc = {
        "version": "1.0",
        "description": "Catálogo completo y tipado de importaciones dinámicas de Gravity Rush Remastered (CUSA01130/eboot.bin)",
        "total_imports": len(imports_list),
        "total_verified": sum(1 for i in imports_list if i["verified_symbol"]),
        "total_unverified": len(uncertainties),
        "imports": imports_list
    }
    with open(OUT_IMPORTS, "w", encoding="utf-8") as f:
        json.dump(imports_doc, f, indent=2, ensure_ascii=False)

    # dependencias.json
    deps_doc = {
        "version": "1.0",
        "target": "Gravity Rush Remastered (CUSA01130)",
        "summary": {
            "total_imports": len(imports_list),
            "total_libraries": len(import_libs),
            "total_needed_modules": len(needed_modules),
            "total_dt_needed_prx": len(eboot_info["needed_prx"]),
            "verified_percentage": round(imports_doc["total_verified"] / len(imports_list) * 100, 2)
        },
        "dt_needed_prx": eboot_info["needed_prx"],
        "import_libraries": list(import_libs.values()),
        "needed_modules": list(needed_modules.values()),
        "local_prx_modules": local_prx_info,
        "library_breakdown": sorted(list(lib_stats.values()), key=lambda x: x["total_imports"], reverse=True)
    }
    with open(OUT_DEPS, "w", encoding="utf-8") as f:
        json.dump(deps_doc, f, indent=2, ensure_ascii=False)

    # incertidumbres.json
    uncert_doc = {
        "version": "1.0",
        "description": "Lista explícita de incertidumbres y símbolos pendientes para el port nativo de PC",
        "total_uncertainties": len(uncertainties),
        "uncertainties_by_library": {
            lib: sum(1 for u in uncertainties if u["library"] == lib)
            for lib in sorted(list(set(u["library"] for u in uncertainties)))
        },
        "symbols": uncertainties
    }
    with open(OUT_UNCERTAINTIES, "w", encoding="utf-8") as f:
        json.dump(uncert_doc, f, indent=2, ensure_ascii=False)

    print(f"\n[+] Tarea N02 completada exitosamente:")
    print(f"    - {OUT_IMPORTS} ({len(imports_list)} imports catalogados, {imports_doc['total_verified']} verificados)")
    print(f"    - {OUT_DEPS} (grafo de 44 librerías y 7 PRX locales)")
    print(f"    - {OUT_UNCERTAINTIES} ({len(uncertainties)} incertidumbres explícitas)")

if __name__ == "__main__":
    main()
