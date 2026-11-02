#!/usr/bin/env python3
# Anade al manejador del temporizador de la UEFI un "latido": cada 5 s imprime la direccion
# que estaba ejecutando la CPU cuando llego la interrupcion (ELR), LR, SP y SPSR.
# Uso: python3 patch.py <ruta a ArmPkg/Drivers/TimerDxe/TimerDxe.c>
import re, sys
p = sys.argv[1]
raw = open(p, "rb").read().decode("utf-8")
nl = "\r\n" if "\r\n" in raw else "\n"
pat = re.compile(r"(\r?\n)  OriginalTPL = gBS->RaiseTPL \(TPL_HIGH_LEVEL\);(\r?\n)")
assert len(pat.findall(raw)) == 1, "ancla no encontrada o repetida"
assert "HB ELR" not in raw, "ya parcheado"
block = nl.join([
    "",
    "  {",
    "    STATIC UINT64  HbNext = 0;",
    "    UINT64         HbNow  = ArmGenericTimerGetSystemCount ();",
    "",
    "    if (HbNow >= HbNext) {",
    "      HbNext = HbNow + (5 * (UINT64)ArmGenericTimerGetTimerFreq ());",
    "      DEBUG ((",
    "        DEBUG_ERROR,",
    '        "HB ELR=%lx LR=%lx SP=%lx SPSR=%lx\\n",',
    "        SystemContext.SystemContextAArch64->ELR,",
    "        SystemContext.SystemContextAArch64->LR,",
    "        SystemContext.SystemContextAArch64->SP,",
    "        SystemContext.SystemContextAArch64->SPSR",
    "        ));",
    "    }",
    "  }",
    ""])
out = pat.sub(lambda m: m.group(1) + "  OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);" + m.group(2).rstrip("\r\n") + nl + block, raw, count=1)
open(p, "wb").write(out.encode("utf-8"))
print("parche aplicado a", p)
