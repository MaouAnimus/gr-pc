#!/usr/bin/env python3
"""
Genera el manifiesto criptográfico y metadatos de la copia local CUSA01130.
Tarea N01: Baseline y manifiesto de copia/versión.
"""
import os
import sys
import struct
import hashlib
import json
import time

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GAME_DIR = os.path.join(PROJECT_ROOT, "CUSA01130")
PARAM_SFO = os.path.join(GAME_DIR, "sce_sys", "param.sfo")
OUT_JSON = os.path.join(PROJECT_ROOT, "docs", "evidence", "manifest_cusa01130.json")

def parse_param_sfo(sfo_path):
    if not os.path.exists(sfo_path):
        return {}
    with open(sfo_path, "rb") as f:
        magic, version, key_off, data_off, count = struct.unpack("<4sIIII", f.read(20))
        if magic != b"\x00PSF":
            return {"error": "Invalid magic"}
        entries = []
        for _ in range(count):
            k_off, fmt, d_len, max_len, d_off = struct.unpack("<HHIII", f.read(16))
            entries.append((k_off, fmt, d_len, max_len, d_off))
        
        meta = {}
        for k_off, fmt, d_len, max_len, d_off in entries:
            f.seek(key_off + k_off)
            k_name = b""
            while True:
                b = f.read(1)
                if b == b"\x00" or not b:
                    break
                k_name += b
            f.seek(data_off + d_off)
            val = f.read(d_len)
            key_str = k_name.decode("utf-8", errors="replace")
            if fmt == 0x0404:
                val_parsed = struct.unpack("<I", val)[0]
            else:
                val_parsed = val.rstrip(b"\x00").decode("utf-8", errors="replace")
            meta[key_str] = val_parsed
        return meta

def hash_file_sha256(filepath):
    sha = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(1024 * 1024):
            sha.update(chunk)
    return sha.hexdigest()

def main():
    if not os.path.exists(GAME_DIR):
        print(f"Error: No existe el directorio {GAME_DIR}", file=sys.stderr)
        sys.exit(1)

    print(f"[*] Escaneando copia local: {GAME_DIR}")
    start_time = time.time()
    
    sfo_meta = parse_param_sfo(PARAM_SFO)
    files_list = []
    total_bytes = 0

    for root, dirs, files in os.walk(GAME_DIR):
        dirs.sort()
        for fname in sorted(files):
            fpath = os.path.join(root, fname)
            relpath = os.path.relpath(fpath, GAME_DIR).replace("\\", "/")
            fsize = os.path.getsize(fpath)
            total_bytes += fsize
            fhash = hash_file_sha256(fpath)
            files_list.append({
                "path": relpath,
                "size_bytes": fsize,
                "sha256": fhash
            })
            print(f"  + [{fsize:>10} B] {relpath} -> {fhash[:12]}...")

    manifest = {
        "manifest_version": "1.0",
        "generated_at": time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()),
        "game_metadata": sfo_meta,
        "summary": {
            "file_count": len(files_list),
            "total_bytes": total_bytes,
            "total_gib": round(total_bytes / (1024**3), 3)
        },
        "files": files_list
    }

    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    elapsed = time.time() - start_time
    print(f"\n[+] Manifiesto generado exitosamente en {OUT_JSON}")
    print(f"    Archivos procesados: {len(files_list)}")
    print(f"    Total datos: {total_bytes:,} bytes (~{manifest['summary']['total_gib']} GiB)")
    print(f"    Tiempo total: {elapsed:.2f} s")

if __name__ == "__main__":
    main()
