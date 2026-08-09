#!/usr/bin/env python3
"""Prefix every symbol in a Mach-O object file with an underscore.

Equivalent of `objcopy --prefix-symbols=_` for Mach-O, which llvm-objcopy
does not implement. Needed because the data assembly sources use plain
symbol names (GBA/ELF convention) while C objects on Darwin reference
them with the Mach-O leading-underscore convention. Prefixing *all*
symbols (defined and undefined) in the assembly-built objects keeps
asm<->asm and asm<->C references consistent, exactly like the COFF
32-bit Windows build did with binutils objcopy.

Usage: machopfx.py FILE.o [FILE.o ...]   (rewrites in place)
"""
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
LC_SYMTAB = 0x2
N_STAB_MASK = 0xE0
NLIST64_SIZE = 16


def prefix_object(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())

    if len(data) < 32 or struct.unpack_from("<I", data, 0)[0] != MH_MAGIC_64:
        raise SystemExit(f"{path}: not a 64-bit little-endian Mach-O file")

    ncmds = struct.unpack_from("<I", data, 16)[0]

    off = 32  # sizeof(mach_header_64)
    symtab_cmd_off = None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == LC_SYMTAB:
            symtab_cmd_off = off
            break
        off += cmdsize
    if symtab_cmd_off is None:
        return  # no symbols, nothing to do

    symoff, nsyms, stroff, strsize = struct.unpack_from(
        "<IIII", data, symtab_cmd_off + 8
    )

    # The string table must be the last thing in the file for an in-place
    # rewrite to be safe (true for MH_OBJECT files emitted by clang).
    if stroff + strsize < len(data) - 16:
        raise SystemExit(
            f"{path}: string table is not at end of file "
            f"(stroff={stroff} strsize={strsize} filesize={len(data)})"
        )

    def read_cstr(base, idx):
        end = data.index(b"\x00", base + idx)
        return bytes(data[base + idx : end])

    new_strtab = bytearray(b"\x00")
    for i in range(nsyms):
        entry_off = symoff + i * NLIST64_SIZE
        n_strx, n_type = struct.unpack_from("<IB", data, entry_off)
        if n_strx == 0:
            continue
        name = read_cstr(stroff, n_strx)
        if not name:
            struct.pack_into("<I", data, entry_off, 0)
            continue
        if n_type & N_STAB_MASK:
            new_name = name  # debug entries keep their names (paths etc.)
        else:
            new_name = b"_" + name
        struct.pack_into("<I", data, entry_off, len(new_strtab))
        new_strtab += new_name + b"\x00"

    while len(new_strtab) % 8:
        new_strtab += b"\x00"

    struct.pack_into(
        "<IIII", data, symtab_cmd_off + 8, symoff, nsyms, stroff, len(new_strtab)
    )
    data[stroff:] = new_strtab

    with open(path, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for p in sys.argv[1:]:
        prefix_object(p)
