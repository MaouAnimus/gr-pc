#!/usr/bin/env python3
"""
Extrae las definiciones de LIB_FUNCTION de shadPS4 para mapear NIDs a nombres y bibliotecas.
Guarda el resultado en docs/evidence/shadps4_symbols.json.
"""
import urllib.request
import re
import json
import os
import sys

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT_FILE = os.path.join(PROJECT_ROOT, "docs", "evidence", "shadps4_symbols.json")

# Archivos clave de registro de librerías en shadPS4
LIB_FILES = [
    # Audio
    "src/core/libraries/audio/audioout.cpp",
    "src/core/libraries/audio3d/audio3d.cpp",
    "src/core/libraries/ngs2/ngs2.cpp",
    # Video & Graphics
    "src/core/libraries/videoout/video_out.cpp",
    "src/core/libraries/gnmdriver/gnmdriver.cpp",
    "src/core/libraries/screenshot/screenshot.cpp",
    # Input
    "src/core/libraries/pad/pad.cpp",
    # Kernel & Concurrency
    "src/core/libraries/kernel/kernel.cpp",
    "src/core/libraries/kernel/threads.cpp",
    "src/core/libraries/kernel/threads/pthread.cpp",
    "src/core/libraries/kernel/threads/mutex.cpp",
    "src/core/libraries/kernel/threads/condvar.cpp",
    "src/core/libraries/kernel/threads/semaphore.cpp",
    "src/core/libraries/kernel/threads/event_flag.cpp",
    "src/core/libraries/kernel/threads/rwlock.cpp",
    "src/core/libraries/kernel/file_system.cpp",
    "src/core/libraries/kernel/memory.cpp",
    "src/core/libraries/kernel/time.cpp",
    "src/core/libraries/kernel/process.cpp",
    "src/core/libraries/kernel/equeue.cpp",
    "src/core/libraries/fiber/fiber.cpp",
    "src/core/libraries/sysmodule/sysmodule.cpp",
    # Libc
    "src/core/libraries/libc_internal/libc_internal.cpp",
    "src/core/libraries/libc_internal/libc_internal_str.cpp",
    "src/core/libraries/libc_internal/libc_internal_memory.cpp",
    "src/core/libraries/libc_internal/libc_internal_math.cpp",
    "src/core/libraries/libc_internal/libc_internal_io.cpp",
    "src/core/libraries/libc_internal/libc_internal_threads.cpp",
    # System & User & Dialogs
    "src/core/libraries/system/systemservice.cpp",
    "src/core/libraries/system/userservice.cpp",
    "src/core/libraries/system/commondialog.cpp",
    "src/core/libraries/system/msgdialog.cpp",
    "src/core/libraries/save_data/savedata.cpp",
    "src/core/libraries/save_data/dialog/savedatadialog.cpp",
    "src/core/libraries/playgo/playgo.cpp",
    "src/core/libraries/rtc/rtc.cpp",
    "src/core/libraries/avplayer/avplayer.cpp",
    "src/core/libraries/font/font.cpp",
    "src/core/libraries/font/fontft.cpp",
    # Network & NP
    "src/core/libraries/net/net.cpp",
    "src/core/libraries/network/netctl.cpp",
    "src/core/libraries/network/http.cpp",
    "src/core/libraries/network/ssl.cpp",
    "src/core/libraries/np/np_common.cpp",
    "src/core/libraries/np/np_manager.cpp",
    "src/core/libraries/np/np_trophy.cpp",
    "src/core/libraries/np/np_auth/np_auth.cpp",
    "src/core/libraries/np/np_score/np_score.cpp",
    "src/core/libraries/np/np_tus/np_tus.cpp",
    "src/core/libraries/np/np_utility/np_utility.cpp",
    "src/core/libraries/np/np_commerce/np_commerce.cpp",
    "src/core/libraries/np/np_web_api/np_web_api.cpp",
    "src/core/libraries/np/np_matching2/np_matching2.cpp"
]

PAT_LIB_FUNC = re.compile(
    r'LIB_FUNCTION\s*\(\s*["\']([^"\']+)["\']\s*,\s*["\']([^"\']+)["\']\s*,\s*[^,]+,\s*["\']([^"\']+)["\']\s*,\s*([^\)\s,]+)'
)
PAT_LIB_OBJ = re.compile(
    r'LIB_OBJ\s*\(\s*["\']([^"\']+)["\']\s*,\s*["\']([^"\']+)["\']\s*,\s*[^,]+,\s*["\']([^"\']+)["\']\s*,\s*([^\)\s,]+)'
)

def main():
    print(f"[*] Extrayendo base de símbolos desde shadPS4 ({len(LIB_FILES)} archivos)...")
    symbols = {}

    for rel_path in LIB_FILES:
        url = f"https://raw.githubusercontent.com/shadps4-emu/shadPS4/main/{rel_path}"
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=10) as resp:
                text = resp.read().decode("utf-8")
                
                funcs = PAT_LIB_FUNC.findall(text)
                objs = PAT_LIB_OBJ.findall(text)
                
                count = len(funcs) + len(objs)
                print(f"  + {rel_path:55}: {count:>3} símbolos")
                
                for nid, lib, mod, target in funcs:
                    symbols[nid] = {
                        "name": target,
                        "library": lib,
                        "module": mod,
                        "type": "Function"
                    }
                for nid, lib, mod, target in objs:
                    symbols[nid] = {
                        "name": target,
                        "library": lib,
                        "module": mod,
                        "type": "Object"
                    }
        except Exception as e:
            print(f"  - {rel_path:55}: Error ({e})", file=sys.stderr)

    os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
    with open(OUT_FILE, "w", encoding="utf-8") as f:
        json.dump(symbols, f, indent=2)

    print(f"\n[+] Total símbolos extraídos y guardados en {OUT_FILE}: {len(symbols)}")

if __name__ == "__main__":
    main()
