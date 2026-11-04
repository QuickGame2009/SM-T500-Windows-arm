#!/bin/bash
# Compila canary1.sys, canary2.sys y canary3.sys (ARM64) con clang + lld-link.
set -e
cd "$(dirname "$0")"
mkdir -p out
llvm-dlltool -m arm64 -d ntoskrnl.def -l out/ntoskrnl.lib
for s in 1 2 3; do
  clang -target aarch64-pc-windows-msvc -O1 -ffreestanding -fno-builtin -fno-stack-protector \
        -funwind-tables -fno-omit-frame-pointer -mgeneral-regs-only -fms-extensions -Wall -Wno-unused-function -DSLOT=$s \
        -c canary.c -o out/canary$s.obj
  lld-link /machine:arm64 /subsystem:native /driver /entry:DriverEntry /nodefaultlib \
        /dynamicbase /nxcompat /highentropyva:no /align:4096 /filealign:512 \
        /out:out/canary$s.sys out/canary$s.obj out/ntoskrnl.lib
  python3 firmar.py out/test.cer out/test.key out/canary$s.sys
  python3 pechecksum.py out/canary$s.sys
done
ls -l out/*.sys
