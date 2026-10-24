import struct

eboot_path = "CUSA01130/eboot.bin"
with open(eboot_path, "rb") as f:
    dynlib_base = 0x1D8AB40
    f.seek(dynlib_base + 0x18)
    strtab = f.read(0xDF0)
    
    sym_bytes = 0xB358 - 0x32A0
    sym_count = sym_bytes // 24
    f.seek(dynlib_base + 0x32A0)
    symbols = []
    for i in range(sym_count):
        sym_data = f.read(24)
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack("<IBBHQQ", sym_data)
        sname = ""
        if st_name < len(strtab):
            end = strtab.find(b"\x00", st_name)
            sname = strtab[st_name:end].decode("ascii", errors="replace")
        symbols.append(sname)
    
    f.seek(dynlib_base + 0xB358)
    relcount = 0x6ED180 // 24
    for i in range(relcount):
        r_offset, r_info, r_addend = struct.unpack("<QQq", f.read(24))
        sym = r_info >> 32
        rtype = r_info & 0xFFFFFFFF
        if 0x1D78200 <= r_offset <= 0x1D78280:
            sname = symbols[sym] if sym < len(symbols) else f"sym_{sym}"
            print(f"GOT[0x{r_offset:x}]: sym={sym} name={sname} rtype={rtype}")
