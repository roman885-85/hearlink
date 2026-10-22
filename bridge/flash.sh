#!/bin/bash
# Зібрати й залити міст через USB (picotool, без кнопки й без диска RPI-RP2 — див. пам'ять «Диск Pico вішає цей Mac»).
# Заливає лише свіжу збірку: якщо ./build.sh упав — нічого не пише.
set -e
cd "$(dirname "$0")"
./build.sh
P="$HOME/Library/hearlink-rp2040/picotool-2.3.1-mac/picotool/picotool"
"$P" load -f -v build/bridge-full.uf2 | grep -E "OK|ERROR|rror" || true
for i in $(seq 1 40); do ls /dev/cu.usbmodem* >/dev/null 2>&1 && break; sleep 0.25; done
ls /dev/cu.usbmodem*
