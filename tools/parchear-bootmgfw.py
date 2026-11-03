#!/usr/bin/env python3
# Parchea una copia de bootmgfw.efi (ARM64, build 22631) para DIAGNOSTICO:
# cuando bootmgr recibe una excepcion sincrona / SError en sus propios vectores, en vez de colgarse
# vuelve al contexto del firmware y provoca un "brk" a proposito. Asi la UEFI imprime en pantalla
# x0..x7 con: ESR_EL1, ELR_EL1, FAR_EL1, SPSR_EL1, SP, LR, x0 original, x18.
# Uso: python3 parchear-bootmgfw.py bootmgfw.efi bootmgfw-diag.efi
import struct, subprocess, sys, re

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, "rb").read())
BASE_TEXT_VA, TEXT_FO = 0x10001000, 0x400
fo = lambda va: TEXT_FO + (va - BASE_TEXT_VA)

HOOKS = {0x10021000: 0x14000000, 0x10021180: 0x14000000, 0x10021380: 0x14000000, 0x10021200: 0xA9077A57}
P = 0x10021400
SWITCH = 0x101682A8          # BlpArchSwitchContext (x0=1 -> contexto del firmware)
STACK = 0x101CAF00           # zona .data/.bss de bootmgr, usada como pila de emergencia

# comprobaciones del original
for va, orig in HOOKS.items():
    got = struct.unpack_from("<I", d, fo(va))[0]
    assert got == orig, (hex(va), hex(got))
assert d[fo(P):fo(P) + 0x80] == bytes([0x00, 0x00, 0x00, 0x14]) + bytes(0x7C), "zona de payload no esperada"

lines = [
    "mrs x21, esr_el1", "mrs x22, elr_el1", "mrs x23, far_el1", "mrs x24, spsr_el1",
    "mov x25, sp", "mov x26, x30", "mov x27, x0", "mov x28, x18",
]
# pila propia (adrp relativo a la pagina de STACK)
n = len(lines)
adrp_at = P + 4 * n
lines.append("adrp x9, #%d" % ((STACK & ~0xFFF) - (adrp_at & ~0xFFF)))
lines.append("add x9, x9, #%d" % (STACK & 0xFFF))
lines.append("mov sp, x9")
lines.append("mov x0, #1")
bl_at = P + 4 * len(lines)
lines.append("bl #%d" % (SWITCH - bl_at))
lines += ["mov x0, x21", "mov x1, x22", "mov x2, x23", "mov x3, x24", "mov x4, x25", "mov x5, x26", "mov x6, x27", "mov x7, x28",
          "brk #0x1234"]
asm = "\n".join(lines) + "\n"
out = subprocess.run(["llvm-mc", "-triple=aarch64", "-show-encoding"], input=asm, capture_output=True, text=True)
assert out.returncode == 0, out.stderr
words = []
for m in re.finditer(r"encoding: \[([^\]]+)\]", out.stdout):
    b = bytes(int(x, 16) for x in m.group(1).split(","))
    assert len(b) == 4
    words.append(b)
assert len(words) == len(lines), (len(words), len(lines), out.stdout)
payload = b"".join(words)
assert len(payload) <= 0x80
d[fo(P):fo(P) + len(payload)] = payload
for va in HOOKS:
    off = P - va
    assert off % 4 == 0 and 0 < off < (1 << 27)
    struct.pack_into("<I", d, fo(va), 0x14000000 | ((off >> 2) & 0x3FFFFFF))
open(dst, "wb").write(d)
print("escrito", dst, "payload", len(payload), "bytes")
