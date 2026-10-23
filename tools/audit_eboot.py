"""
Auditor de Binario PS4 (eboot.bin) para Gravity Rush Remastered (CUSA01130)
Analiza encabezados ELF, Program Headers, módulos dinámicos y genera el reporte inicial.
"""
import os
import struct
import json
import re

EBOOT_PATH = os.path.join(os.path.dirname(__file__), "..", "CUSA01130", "eboot.bin")
OUT_REPORT = os.path.join(os.path.dirname(__file__), "..", "docs", "evidence", "eboot_audit_report.json")

def parse_eboot():
    if not os.path.exists(EBOOT_PATH):
        print(f"[-] Error: {EBOOT_PATH} no encontrado.")
        return

    print(f"[+] Analizando binario: {EBOOT_PATH}")
    file_size = os.path.getsize(EBOOT_PATH)
    print(f"[+] Tamaño de archivo: {file_size:,} bytes")

    with open(EBOOT_PATH, "rb") as f:
        hdr = f.read(0x400)
        elf_offset = hdr.find(b"\x7fELF")
        if elf_offset == -1:
            print("[-] No se encontró el encabezado ELF en los primeros 1024 bytes.")
            return

        print(f"[+] Encabezado ELF encontrado en offset 0x{elf_offset:X}")
        f.seek(elf_offset)
        elf_hdr = f.read(64)

        (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff,
         e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx) = struct.unpack("<HHIQQQIHHHHHH", elf_hdr[16:64])

        print(f"[+] Arquitectura: 0x{e_machine:X} (EM_X86_64: 0x3E)")
        print(f"[+] Punto de entrada (Entry Point): 0x{e_entry:X}")
        print(f"[+] Program Headers: {e_phnum} entradas en offset 0x{elf_offset + e_phoff:X}")

        f.seek(elf_offset + e_phoff)
        program_headers = []
        dynlib_offset = 0
        dynlib_size = 0

        PT_NAMES = {
            0: "PT_NULL", 1: "PT_LOAD", 2: "PT_DYNAMIC", 3: "PT_INTERP",
            4: "PT_NOTE", 5: "PT_SHLIB", 6: "PT_PHDR", 7: "PT_TLS",
            0x61000000: "PT_SCE_DYNLIBDATA", 0x61000001: "PT_SCE_PROGLIBDATA",
            0x61000010: "PT_SCE_RELRO", 0x6474e550: "PT_EH_FRAME",
            0x6fffff00: "PT_SCE_COMMENT", 0x6fffff01: "PT_SCE_VERSION"
        }

        for i in range(e_phnum):
            ph_raw = f.read(e_phentsize)
            p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack("<IIQQQQQQ", ph_raw[:56])
            p_name = PT_NAMES.get(p_type, f"0x{p_type:X}")
            program_headers.append({
                "index": i,
                "type": p_name,
                "offset": f"0x{p_offset:X}",
                "vaddr": f"0x{p_vaddr:X}",
                "filesz": f"0x{p_filesz:X}",
                "memsz": f"0x{p_memsz:X}",
                "flags": p_flags
            })
            if p_type == 0x61000000:
                dynlib_offset = p_offset
                dynlib_size = p_filesz

        modules = []
        if dynlib_offset > 0 and dynlib_size > 0:
            f.seek(dynlib_offset)
            dyn_data = f.read(dynlib_size)
            modules = sorted(list(set(m.decode("ascii") for m in re.findall(b"[a-zA-Z0-9_]+\\.prx", dyn_data))))
            print(f"[+] Módulos dinámicos importados encontrados: {len(modules)}")
            for mod in modules:
                print(f"    - {mod}")

        report = {
            "title": "Gravity Rush Remastered (CUSA01130) ELF Audit",
            "file_size": file_size,
            "elf_offset": f"0x{elf_offset:X}",
            "machine": f"0x{e_machine:X}",
            "entry_point": f"0x{e_entry:X}",
            "program_headers": program_headers,
            "imported_prx": modules
        }

        os.makedirs(os.path.dirname(OUT_REPORT), exist_ok=True)
        with open(OUT_REPORT, "w", encoding="utf-8") as out:
            json.dump(report, out, indent=2)

        print(f"[+] Reporte generado exitosamente en: {OUT_REPORT}")

if __name__ == "__main__":
    parse_eboot()
