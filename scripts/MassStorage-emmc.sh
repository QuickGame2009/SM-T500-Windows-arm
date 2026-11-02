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
sleep 2

G=/config/usb_gadget/g1
DISK=/dev/block/mmcblk0
[ -b "$DISK" ] || DISK=/dev/block/sda
UDC=$(ls /sys/class/udc 2>/dev/null | head -n 1)
[ -n "$UDC" ] || UDC=4e00000.dwc3

# 1. soltar el UDC (se corta adb)
echo "" > $G/UDC
sleep 1

# 2. configurar
echo 0xEF > $G/bDeviceClass
echo 0x02 > $G/bDeviceSubClass
echo 0x01 > $G/bDeviceProtocol

[ -d $G/functions/mass_storage.0 ] || mkdir $G/functions/mass_storage.0
[ -e $G/configs/b.1/mass_storage.0 ] || ln -s $G/functions/mass_storage.0 $G/configs/b.1/mass_storage.0

echo $DISK > $G/functions/mass_storage.0/lun.0/file
echo 0 > $G/functions/mass_storage.0/lun.0/cdrom
echo 0 > $G/functions/mass_storage.0/lun.0/removable
echo 0 > $G/functions/mass_storage.0/lun.0/ro

# 3. volver a activar el UDC
sleep 1
echo $UDC > $G/UDC
echo "Disco $DISK expuesto por $UDC"
