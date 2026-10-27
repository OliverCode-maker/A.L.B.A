#!/bin/bash
# Compila y corre las tareas de la pulsera en el PC con FreeRTOS real (puerto POSIX).
# Uso:  tools/host_sim/run.sh [segundos]            (por defecto 100)
#       FAST=0 tools/host_sim/run.sh 1500           (tiempos reales)
# Necesita gcc, g++, git y pthread. La primera vez descarga FreeRTOS-Kernel
# (una copia de ~10 MB en tools/host_sim/.cache, ignorada por git).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BAND="$HERE/../.."
KERNEL="${FREERTOS_KERNEL_PATH:-$HERE/.cache/FreeRTOS-Kernel}"
OUT="${BUILD_DIR:-$HERE/.cache/build}"
FAST="${FAST:-1}"

if [ ! -d "$KERNEL" ]; then
  mkdir -p "$HERE/.cache"
  git clone --depth 1 --branch V11.1.0 https://github.com/FreeRTOS/FreeRTOS-Kernel.git "$KERNEL"
fi
mkdir -p "$OUT"

PORT="$KERNEL/portable/ThirdParty/GCC/Posix"
INC="-I$HERE -I$KERNEL/include -I$PORT -I$PORT/utils -I$BAND/include -I$BAND/src -I$BAND/../common"
for d in "$BAND"/lib/*/; do INC="$INC -I$d"; done
DEFS="-DBAND_SIM=1 -DBAND_SIM_FAST=$FAST -D_GNU_SOURCE"

CSRC="$KERNEL/tasks.c $KERNEL/queue.c $KERNEL/list.c $KERNEL/portable/MemMang/heap_3.c \
      $PORT/port.c $PORT/utils/wait_for_event.c $BAND/lib/*/*.c"
CXXSRC="$HERE/host_main.cpp $HERE/port_host.cpp $BAND/src/band_tasks.cpp $BAND/src/port_sim.cpp"

mkdir -p "$OUT/o"; rm -f "$OUT"/o/*.o
for f in $CSRC;  do gcc -std=gnu11   -Wall -Wextra -Wno-unused-parameter $INC $DEFS -c "$f" -o "$OUT/o/$(basename "$f" .c).o"; done
for f in $CXXSRC; do g++ -std=gnu++14 -Wall -Wextra -Wno-unused-parameter $INC $DEFS -c "$f" -o "$OUT/o/$(basename "$f" .cpp).o"; done
g++ "$OUT"/o/*.o -lpthread -lm -o "$OUT/band_host"
exec "$OUT/band_host" "$@"
