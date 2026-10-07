#!/bin/sh
# Заливает проверочные звуки (build/assets.bin, делает tools/make_sounds.py) в раздел данных передатчика.
#   tools/flash_assets.sh /dev/cu.usbserial-XXXX          — всё (около 9 минут)
#   tools/flash_assets.sh /dev/cu.usbserial-XXXX golos    — только новый голос (секунды)
# Раздел «assets» начинается с 0x310000 (см. hearlink/partitions.csv). Прошивку это не трогает.
set -e
cd "$(dirname "$0")/.."
PORT=${1:?порт передатчика}
E=$(ls -d ~/Library/Arduino15/packages/esp32/tools/esptool_py/*/ | tail -1)
[ -f build/assets.bin ] || python3 tools/make_sounds.py
if [ "$2" = golos ]; then
  # сменился только голос (он в файле последний): переписать начало с оглавлением и хвост — секунды вместо девяти минут
  OFF=$(python3 - <<'PY'
import struct
x = open('build/assets.bin', 'rb').read()
n = struct.unpack('<I', x[4:8])[0]
off = [struct.unpack('<II', x[24 + 24 * i:32 + 24 * i])[0] for i in range(n) if x[8 + 24 * i:24 + 24 * i].rstrip(b'\0') == b'golos'][0] & ~4095
open('build/assets-head.bin', 'wb').write(x[:4096])
open('build/assets-tail.bin', 'wb').write(x[off:])
print(off)
PY
)
  "$E/esptool" --chip esp32s3 --port "$PORT" --baud ${SPEED:-230400} write-flash 0x310000 build/assets-head.bin $((0x310000 + OFF)) build/assets-tail.bin 2>&1 \
    | grep -E -i "Wrote|verified|error|fatal|Hard resetting" | cut -c1-160
  exit 0
fi
"$E/esptool" --chip esp32s3 --port "$PORT" --baud ${SPEED:-230400} write-flash 0x310000 build/assets.bin 2>&1 \
  | grep -E -i "Wrote|verified|error|fatal|Hard resetting" | cut -c1-160
