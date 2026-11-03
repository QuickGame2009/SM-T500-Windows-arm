#!/usr/bin/env python3
# Pone una "baliza" (brk #imm) en una direccion virtual de bootmgfw.efi (idealmente ya parcheado con parchear-bootmgfw.py).
# Si la CPU llega a esa instruccion, la UEFI imprime la excepcion con ELR = direccion de la baliza; si no la imprime, no llego.
# Uso: python3 baliza-bootmgfw.py entrada.efi salida.efi 0x10023490
import struct, sys
src, dst, va = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
d = bytearray(open(src, "rb").read())
fo = 0x400 + (va - 0x10001000)
assert va % 4 == 0
old = struct.unpack_from("<I", d, fo)[0]
struct.pack_into("<I", d, fo, 0xD4200000 | ((0x5A00 & 0xFFFF) << 5))  # brk #0x5a00
open(dst, "wb").write(d)
print("baliza en %#x (instruccion original %#010x) -> %s" % (va, old, dst))
