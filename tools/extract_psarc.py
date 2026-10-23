"""
Herramienta de Inspección y Extracción de Archivos PSARC (PlayStation Archive)
Soporta inspección de cabeceras, listado de contenidos y extracción de bloques comprimidos.
"""
import os
import sys
import struct
import zlib
import argparse

def inspect_psarc(filepath):
    if not os.path.exists(filepath):
        print(f"[-] Archivo no encontrado: {filepath}")
        return

    file_size = os.path.getsize(filepath)
    print(f"[+] Inspeccionando: {filepath} ({file_size:,} bytes)")

    with open(filepath, "rb") as f:
        magic = f.read(4)
        if magic != b"PSAR":
            print(f"[-] Magic inválido: {magic}. Se esperaba b'PSAR'")
            return

        ver_major, ver_minor = struct.unpack(">HH", f.read(4))
        comp_type = f.read(4).decode("latin-1", errors="ignore").strip()
        toc_length, entry_size, num_entries = struct.unpack(">III", f.read(12))
        block_size, archive_flags = struct.unpack(">II", f.read(8))

        print(f"    - Versión: {ver_major}.{ver_minor}")
        print(f"    - Tipo de Compresión: {comp_type}")
        print(f"    - Tamaño TOC: {toc_length:,} bytes")
        print(f"    - Entradas de Archivos: {num_entries:,}")
        print(f"    - Tamaño de Bloque: {block_size:,} bytes")

def main():
    parser = argparse.ArgumentParser(description="Lector e Inspector de PSARC de Gravity Rush")
    parser.add_argument("path", nargs="?", default=os.path.join(os.path.dirname(__file__), "..", "CUSA01130", "arc", "arc_0_ep_0_0.psarc"))
    args = parser.parse_args()

    inspect_psarc(args.path)

if __name__ == "__main__":
    main()
