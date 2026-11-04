#!/usr/bin/env python3
"""Calcula y escribe el CheckSum del encabezado PE (como MapFileAndCheckSumW)."""
import struct, sys

def pe_checksum(data: bytes, csum_off: int) -> int:
    n = len(data)
    total = 0
    # sumar palabras de 16 bits, saltando el campo CheckSum (4 bytes)
    padded = data + (b"\x00" if n % 2 else b"")
    for i in range(0, len(padded), 2):
        if csum_off <= i < csum_off + 4:
            continue
        total += padded[i] | (padded[i + 1] << 8)
        total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    return (total + n) & 0xFFFFFFFF

for path in sys.argv[1:]:
    b = bytearray(open(path, "rb").read())
    e_lfanew = struct.unpack_from("<I", b, 0x3C)[0]
    assert b[e_lfanew:e_lfanew + 4] == b"PE\0\0", "no es un PE"
    csum_off = e_lfanew + 4 + 20 + 64          # OptionalHeader.CheckSum (PE32+ y PE32)
    cs = pe_checksum(bytes(b), csum_off)
    struct.pack_into("<I", b, csum_off, cs)
    open(path, "wb").write(b)
    print(f"{path}: CheckSum = 0x{cs:08X}")
