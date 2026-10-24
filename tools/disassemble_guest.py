"""Focused, read-only SELF inspection using the local LLVM disassembler (not REA)."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('address', type=lambda s: int(s, 0), help='Guest module offset, no load base')
    parser.add_argument('--size', type=lambda s: int(s, 0), default=160)
    parser.add_argument('--image', type=Path, default=ROOT / 'CUSA01130/eboot.bin')
    args = parser.parse_args()
    if args.address < 0 or not 1 <= args.size <= 4096:
        parser.error('range must be positive and at most 4096 bytes')
    source = args.image.resolve()
    data = source.read_bytes()
    is_self = data[:4] == bytes.fromhex('4f153d1d')
    entries = [struct.unpack_from('<QQQQ', data, 0x20 + i * 32)
               for i in range(struct.unpack_from('<H', data, 0x18)[0])] if is_self else []
    elf = 0x20 + len(entries) * 32 if is_self else 0
    if data[elf:elf + 6] != b'\x7fELF\x02\x01':
        raise ValueError('requires little-endian ELF64 or uncompressed SELF')
    phoff = struct.unpack_from('<Q', data, elf + 32)[0]
    count = struct.unpack_from('<H', data, elf + 56)[0]
    for index in range(count):
        kind, flags, offset, address, _, size, _, _ = struct.unpack_from('<IIQQQQQQ', data, elf + phoff + index * 56)
        if kind != 1 or not address <= args.address or args.address + args.size > address + size:
            continue
        if is_self:
            matches = [item for item in entries if item[0] & 0x800 and (item[0] >> 20) & 0xfff == index]
            if len(matches) != 1 or matches[0][0] & 3 or matches[0][2] != size:
                raise ValueError('ambiguous, compressed or unsupported SELF segment')
            offset = matches[0][1]
        file_offset = offset + args.address - address
        code = data[file_offset:file_offset + args.size]
        break
    else:
        raise ValueError('range not in one file-backed load segment')
    # A minimal analysis-only ELF preserves the module addresses for relative calls.
    section_table = (0x100 + len(code) + 0x100) & ~0xff
    names = b'\0.text\0.shstrtab\0'
    image = bytearray(section_table + 3 * 64 + len(names))
    image[:64] = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + bytes(9),
        1, 62, 1, 0, 0, section_table, 0, 64, 56, 0, 64, 3, 2)
    image[0x100:0x100 + len(code)] = code
    image[section_table + 64:section_table + 128] = struct.pack('<IIQQQQIIQQ',
        1, 1, 6, args.address, 0x100, len(code), 0, 0, 16, 0)
    image[section_table + 128:section_table + 192] = struct.pack('<IIQQQQIIQQ',
        7, 3, 0, 0, section_table + 192, len(names), 0, 0, 1, 0)
    image[section_table + 192:] = names
    folder = ROOT / 'out/evidence/disassembly'
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f'{source.stem}_{args.address:x}_{len(code):x}.elf'
    path.write_bytes(image)
    print(f'Local LLVM evidence; source SHA256={hashlib.sha256(data).hexdigest()}\n'
          f'Source file offset=0x{file_offset:x}; module offset=0x{args.address:x}; bytes={len(code)}', flush=True)
    result = subprocess.run(['rtk', 'proxy', r'C:\msys64\clang64\bin\llvm-objdump.exe',
                             '-d', '--x86-asm-syntax=intel', str(path)], cwd=ROOT)
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
