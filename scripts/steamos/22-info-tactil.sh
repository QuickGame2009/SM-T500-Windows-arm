#!/usr/bin/env bash
# Recoge informacion de la pantalla tactil desde el recovery de Lineage (alli el tactil funciona,
# asi que su driver de Linux esta cargado). Solo lee. Guarda en ~/sm-t500/kit/info-tactil
. "$(dirname "$0")/comun.sh"
necesito_adb
O="$KIT/info-tactil"; mkdir -p "$O"
run() { adb exec-out "$1" 2>&1 | tr -d '\r'; }
run 'cat /proc/bus/input/devices' > "$O/input-devices.txt"
run 'cat /proc/interrupts' > "$O/interrupts.txt"
run 'dmesg 2>/dev/null | grep -i -E "touch|tsp|ts_|himax|hx8|nvt|novatek|focal|fts|ilitek|ili9|sec_ts|zinitix|goodix|gtx|melfas|stm|synaptics|spi|i2c|geni"' > "$O/dmesg-tactil.txt"
run 'cat /proc/modules 2>/dev/null; ls /vendor/lib/modules /lib/modules 2>/dev/null' > "$O/modulos.txt"
run 'for b in spi i2c platform; do for d in /sys/bus/$b/devices/*; do n=$(cat $d/name 2>/dev/null); c=$(tr "\0" " " < $d/of_node/compatible 2>/dev/null); dr=$(readlink $d/driver); case "$n $c $dr" in *ts*|*touch*|*tsp*|*hx*|*nvt*|*fts*|*ili*|*spi*|*i2c*) echo "$b ${d##*/} name=$n compat=$c driver=${dr##*/} modalias=$(cat $d/modalias 2>/dev/null)";; esac; done; done' > "$O/buses.txt"
# Nodos del device tree (el arbol ya combinado con el dtbo de Samsung)
run 'cd /proc/device-tree 2>/dev/null || cd /sys/firmware/devicetree/base; for n in $(find . -type d | grep -i -E "touch|tsp|_ts|ts@|himax|nvt|novatek|focal|ilitek|sec_ts|zinitix|goodix"); do echo "=== $n"; for p in $n/*; do [ -f "$p" ] && { printf "%s = " "${p##*/}"; if tr -d "\0" < "$p" | grep -q "^[[:print:]]*$" 2>/dev/null && [ "${p##*/}" != reg ] && [ "${p##*/}" != interrupts ]; then tr "\0" " " < "$p"; else od -An -tx1 -v "$p" | tr -s " \n" " "; fi; echo; }; done; done' > "$O/devicetree-tactil.txt"
run 'cd /proc/device-tree; for n in $(find . -type d -name "spi@4a*" -o -type d -name "i2c@4a*" -o -type d -name "qupv3*"); do echo "=== $n status=$(tr -d "\0" < $n/status 2>/dev/null) compat=$(tr "\0" " " < $n/compatible 2>/dev/null)"; done' > "$O/devicetree-buses.txt"
run 'ls -la /vendor/firmware /vendor/firmware_mnt /lib/firmware /firmware /odm/firmware 2>/dev/null' > "$O/firmware-lista.txt"
adb exec-out 'cat /dev/block/by-name/dtbo' > "$O/dtbo.img" 2>/dev/null
ls -la "$O"; grep -i -E 'touch|tsp|sec|himax|nvt' "$O/input-devices.txt" | head
verde "Hecho. Comprime la carpeta $O y enviamela."
