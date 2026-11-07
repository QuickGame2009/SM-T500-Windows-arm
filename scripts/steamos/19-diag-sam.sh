#!/usr/bin/env bash
# Diagnostico de SOLO LECTURA de las colmenas SAM / SECURITY / SYSTEM (por que falla el inicio de sesion).
# Con la tablet en modo almacenamiento (03-massstorage.sh). No modifica nada en la tablet.
# Resultado: ~/sm-t500/kit/diag-sam.txt  (subelo al chat)
. "$(dirname "$0")/comun.sh"
WIN=${1:-80}
mkdir -p "$KIT"; exec > >(tee "$KIT/diag-sam.txt") 2>&1
necesito_caja
DISCO=$(buscar_disco) || morir "No encuentro el disco de la tablet (03-massstorage.sh)."
montar "$WIN" "$MNT/win" ntfs; trap desmontar_todo EXIT
C="$MNT/win/Windows/System32/config"
T="$BASE/trabajo"; mkdir -p "$T"; CP="$T/sam-$(date +%H%M%S)"; mkdir -p "$CP"
for h in SAM SECURITY SYSTEM SOFTWARE; do
  cp "$C/$h" "$CP/" 2>/dev/null
  for l in LOG1 LOG2; do [ -f "$C/$h.$l" ] && cp "$C/$h.$l" "$CP/"; done
  echo "== $h: $(colmena_ok "$CP/$h")  tam=$(stat -c %s "$CP/$h")  LOG1=$(stat -c %s "$C/$h.LOG1" 2>/dev/null || echo -)  LOG2=$(stat -c %s "$C/$h.LOG2" 2>/dev/null || echo -)"
done
echo; echo "== SECURITY\\Policy (dominios que conoce LSA)"
for v in PolAcDmN PolAcDmS PolPrDmN PolPrDmS PolDnDDN PolDnDmG PolDnTrN PolMod; do
  echo "-- $v"; caja hivexregedit --export --prefix SEC "$CP/SECURITY" "\\Policy\\$v" 2>&1 | head -12
done
echo; echo "== SAM\\SAM\\Domains"
caja hivexsh -f /dev/stdin "$CP/SAM" <<'S' 2>&1 | head -40
cd \SAM\Domains
ls
cd Account
ls
lsval
cd Users\Names
ls
S
echo; echo "== SAM\\SAM\\Domains\\Account\\F y V (cabecera)"
caja hivexregedit --export --prefix SAM "$CP/SAM" "\\SAM\\Domains\\Account" 2>&1 | head -20
echo; echo "== SYSTEM: ComputerName, Select, servicios clave"
for k in "ControlSet001\\Control\\ComputerName\\ComputerName" ; do caja hivexregedit --export --prefix SYS "$CP/SYSTEM" "\\$k" 2>&1 | head; done
caja hivexregedit --export --prefix SYS "$CP/SYSTEM" "\\Select" 2>&1 | head
for s in SamSs EventLog LSM lanmanworkstation; do
  echo "-- $s"; caja hivexregedit --export --prefix SYS "$CP/SYSTEM" "\\ControlSet001\\Services\\$s" 2>&1 | grep -v '^$' | head -20
done
echo; echo "== SECURITY\\Policy\\Accounts (SIDs con derechos, solo nombres)"
caja hivexsh -f /dev/stdin "$CP/SECURITY" <<'S' 2>&1 | head -30
cd \Policy\Accounts
ls
S
verde "Listo: $KIT/diag-sam.txt"
