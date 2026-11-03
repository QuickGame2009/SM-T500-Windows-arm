#!/sbin/sh
# Expone la memoria interna de la tablet al PC como disco USB, desde TWRP.
#
# Adaptado de Project-Silicium/Guides:
#   SoCs/Snapdragon/Mass-Storage/0x04E00000/MassStorage.sh  (el de SM6115)
# Cambios respecto al original, motivados por lo que se vio en TU tablet (info\usb-gadget.txt):
#   - el UDC es 4e00000.dwc3 (se autodetecta igualmente)
#   - el gadget de TWRP solo tiene ffs.adb y ffs.fastboot: NO existe la funcion
#     mass_storage.0, asi que se crea
#   - se suelta el UDC ANTES de cambiar la configuracion (configfs no deja enlazar
#     funciones con el gadget activo) y todo se ejecuta en segundo plano, porque al
#     soltar el UDC se corta adb y mataria el script
#   - el disco es mmcblk0 (eMMC); el original usa sda (UFS)
#   - deja un registro en /cache/ms.log
#   - QUITA la funcion adb de la configuracion USB: mientras dure esto adb NO funciona;
#     reinicia la tablet (Encendido) para recuperarlo
#
# Uso (lo lanza 3-massstorage.cmd; a mano):
#   adb push MassStorage-emmc.sh /cache/
#   adb shell sh /cache/MassStorage-emmc.sh
# Windows vera un disco nuevo con todas las particiones. NO formatees nada salvo
# "win" y "esp", y cancela cualquier aviso de "inicializar" o "formatear" de otras.
# Para volver a la normalidad: reinicia la tablet. Si no funciona, tras reiniciar a
# TWRP ejecuta:  adb shell cat /cache/ms.log   y envia el resultado.

if [ "$1" != "run" ]; then
  ( trap '' HUP; sh "$0" run > /cache/ms.log 2>&1 < /dev/null & )
  echo "Lanzado en segundo plano. adb se cortara en unos segundos (es normal)."
  exit 0
fi

set -x
setenforce 0
sleep 1

G=/config/usb_gadget/g1
DISK=/dev/block/mmcblk0
[ -b "$DISK" ] || DISK=/dev/block/sda
UDC=$(ls /sys/class/udc 2>/dev/null | head -n 1)
[ -n "$UDC" ] || UDC=4e00000.dwc3
F=$G/functions/mass_storage.0
C=$G/configs/b.1

# 1. preparar TODO lo que se pueda con el gadget aun activo (asi la ventana critica es minima)
[ -d $F ] || mkdir $F
echo $DISK > $F/lun.0/file
echo 0 > $F/lun.0/cdrom
echo 0 > $F/lun.0/removable
echo 0 > $F/lun.0/ro

# 2. ventana critica, SIN pausas. El recovery reenlaza adb (f1) y pone PID d001 cuando adbd se reinicia
#    al soltar el USB; si ganamos la carrera y activamos primero, configfs no le deja tocar nada.
#    Se reintenta hasta 10 veces.
n=0
while [ $n -lt 10 ]; do
  n=$((n+1))
  echo "" > $G/UDC
  for f in $C/*; do [ -L "$f" ] && [ "${f##*/}" != "mass_storage.0" ] && rm "$f"; done
  [ -e $C/mass_storage.0 ] || ln -s $F $C/mass_storage.0
  echo 0x00 > $G/bDeviceClass
  echo 0x00 > $G/bDeviceSubClass
  echo 0x00 > $G/bDeviceProtocol
  echo 0x5a5a > $G/idProduct
  echo $UDC > $G/UDC
  # correcto si: activado, mass_storage enlazado, y adb (f1) no esta
  if [ "$(cat $G/UDC)" = "$UDC" ] && [ -e $C/mass_storage.0 ] && [ ! -e $C/f1 ]; then
    echo "OK en el intento $n"
    break
  fi
  echo "intento $n fallido (UDC=$(cat $G/UDC) f1=$([ -e $C/f1 ] && echo si || echo no))"
  sleep 1
done
echo "Disco $DISK expuesto por $UDC"

# 3. diagnostico
set +x
for i in 1 2 3 4 5 6; do
  sleep 3
  echo "--- t=$((i*3))s  estado UDC: $(cat /sys/class/udc/$UDC/state 2>&1)  UDC=$(cat $G/UDC 2>&1)  PID=$(cat $G/idProduct 2>&1)  f1=$([ -e $C/f1 ] && echo si || echo no)"
done
ls -l $C 2>&1
