#!/bin/sh
# Собирает (и по желанию заливает) прошивку набора средством Arduino IDE.
#
#   ./build.sh linktest esp32                     — только собрать
#   ./build.sh linktest s3 /dev/cu.usbserial-X    — собрать и залить
#
# Платы: esp32 (WROOM), s3, s2, c3. У C2 нет I2S — для набора он не годится.
set -e
cd "$(dirname "$0")"
SKETCH=${1:?что собирать: linktest}
case "${2:?плата: esp32 | s3 | s2 | c3}" in
  esp32) FQBN=esp32:esp32:esp32 ;;
  s3) FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB ;;   # платы N16R8
  s2) FQBN=esp32:esp32:esp32s2 ;;
  c3) FQBN=esp32:esp32:esp32c3 ;;
  *) echo "неизвестная плата $2"; exit 1 ;;
esac
IDECLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
[ -x "$IDECLI" ] || IDECLI=arduino-cli
CFG=$HOME/.arduinoIDE/arduino-cli.yaml
OUT=build/$SKETCH-$2
rm -f "$OUT/$SKETCH.ino.bin"   # иначе при ошибке сборки в плату ушла бы прежняя прошивка
"$IDECLI" --config-file "$CFG" compile -b $FQBN --build-path "$OUT" --warnings default "$SKETCH" 2>&1 \
  | grep -E "Sketch uses|Global variables|warning|rror" || true
[ -f "$OUT/$SKETCH.ino.bin" ] || { echo "сборка не удалась"; exit 1; }
if [ -n "$3" ]; then
  # SPEED=115200 ./build.sh … — для плат, где мост USB сбоит на большой скорости (модуль 4848S040 с CH340)
  "$IDECLI" --config-file "$CFG" upload -b $FQBN -p "$3" --input-dir "$OUT" ${SPEED:+--upload-property upload.speed=$SPEED} 2>&1 \
    | grep -E -i "Chip is|Wrote|verified|error|fatal|Hard resetting" | cut -c1-200
fi
