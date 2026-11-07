#!/usr/bin/env bash
# Prueba: pone el grupo de trabajo "WORKGROUP" en SECURITY\Policy\PolPrDmN (en la imagen esta vacio;
# el log de seguridad muestra SubjectDomainName vacio y el logon local falla con 0xC00000DF).
# Con la tablet en modo almacenamiento (03-massstorage.sh).  --restaurar devuelve la SECURITY original.
. "$(dirname "$0")/comun.sh"
WIN=80; REST=0
while [ $# -gt 0 ]; do case "$1" in --win) WIN=$2; shift;; --restaurar) REST=1;; *) morir "Opcion desconocida: $1";; esac; shift; done
mkdir -p "$KIT"; exec > >(tee "$KIT/diag-20.txt") 2>&1
necesito_caja
DISCO=$(buscar_disco) || morir "No encuentro el disco de la tablet (03-massstorage.sh)."
montar "$WIN" "$MNT/win" ntfs; trap desmontar_todo EXIT
C="$MNT/win/Windows/System32/config"; [ -f "$C/SECURITY" ] || morir "No encuentro SECURITY"
if [ $REST = 1 ]; then
  B=$(ls -t "$KIT"/SECURITY.orig-* 2>/dev/null | head -1) || true
  [ -n "${B:-}" ] || morir "No hay copia SECURITY.orig-* en $KIT"
  cp "$B" "$C/SECURITY" && verde "SECURITY restaurada desde $B"; exit 0
fi
T="$BASE/trabajo"; mkdir -p "$T"; S="$T/SECURITY"
B="$KIT/SECURITY.orig-$(date +%H%M%S)"; cp "$C/SECURITY" "$B"; cp "$C/SECURITY" "$S"
echo "Copia de seguridad: $B"; echo "SECURITY antes: $(colmena_ok "$S")"
echo "PolPrDmN antes: $(regget "$S" 'Policy\PolPrDmN' '')"
R="$T/wg.reg"
printf 'Windows Registry Editor Version 5.00\r\n\r\n[HKEY_LOCAL_MACHINE\\SECURITY\\Policy\\PolPrDmN]\r\n@=hex(0):12,00,14,00,08,00,00,00,57,00,4f,00,52,00,4b,00,47,00,52,00,4f,00,55,00,50,00,00,00\r\n' > "$R"
regmerge "$S" 'HKEY_LOCAL_MACHINE\SECURITY' "$R" && verde "PolPrDmN = WORKGROUP" || morir "ERROR al escribir SECURITY"
# Marca la colmena como limpia (secuencias iguales + checksum) para que Windows no le aplique los .LOG antiguos encima.
python3 - "$S" <<'P'
import struct,sys
f=open(sys.argv[1],'r+b'); h=bytearray(f.read(512))
s1,s2=struct.unpack_from('<II',h,4); struct.pack_into('<I',h,8,s1)
c=0
for i in range(0,508,4): c^=struct.unpack_from('<I',h,i)[0]
c=0xFFFFFFFE if c==0xFFFFFFFF else (1 if c==0 else c)
struct.pack_into('<I',h,508,c); f.seek(0); f.write(h); f.close(); print('secuencias',s1,s2,'-> iguales; checksum',hex(c))
P
echo "SECURITY despues: $(colmena_ok "$S")"
cp "$S" "$C/SECURITY" && verde "SECURITY escrita en la tablet. Arranca Windows y prueba el inicio de sesion."
