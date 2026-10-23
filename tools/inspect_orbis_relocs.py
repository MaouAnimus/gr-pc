import struct

def main():
    eboot_path = "CUSA01130/eboot.bin"
    with open(eboot_path, "rb") as f:
        dynlib_base = 0x1D8AB40
        f.seek(dynlib_base + 0x18)
        strtab = f.read(0xDF0)
        
        # Read symbols from 0x32A0 to 0xB358
        sym_bytes = 0xB358 - 0x32A0
        sym_count = sym_bytes // 24
        print(f"Possible symbols: {sym_count}")
        f.seek(dynlib_base + 0x32A0)
        symbols = []
        for i in range(sym_count):
            sym_data = f.read(24)
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack("<IBBHQQ", sym_data)
            sname = ""
            if st_name < len(strtab):
                end = strtab.find(b"\x00", st_name)
                sname = strtab[st_name:end].decode("ascii", errors="replace")
            symbols.append((sname, st_shndx, st_value))
        
        f.seek(dynlib_base + 0xB358)
        relcount = 0x6ED180 // 24
        max_sym = 0
        ref_syms = set()
        for i in range(relcount):
            r_offset, r_info, r_addend = struct.unpack("<QQq", f.read(24))
            sym = r_info >> 32
            rtype = r_info & 0xFFFFFFFF
            if rtype != 8:
                ref_syms.add(sym)
                if sym > max_sym:
                    max_sym = sym
        print(f"Max sym referenced: {max_sym}")
        print(f"All referenced syms count: {len(ref_syms)}")
        for s in sorted(ref_syms):
            print(f"Sym[{s}]: name='{symbols[s][0]}', shndx={symbols[s][1]}, val=0x{symbols[s][2]:x}")

if __name__ == "__main__":
    main()
