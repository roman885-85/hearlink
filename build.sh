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
# Ядро Arduino. Выпуски с 2.59 собираются ядром esp32 3.3.12 (ESP-IDF 5.5.5); до 2.58 — 3.3.3 (ESP-IDF 5.5.1).
# Если ядро для этого проекта поставлено в отдельную папку ~/Library/Arduino15-hearlink (так у автора: общим ядром
# 3.3.3 в ~/Library/Arduino15 собираются другие проекты, и трогать его нельзя) — берётся оно. Иначе — то ядро, что
# стоит в Arduino IDE. CORE=old ./build.sh … — нарочно собрать общим ядром; результат ляжет рядом, в
# build/<набросок>-<плата>-old, чтобы сборки можно было сравнить и залить любую.
OWNCFG=$HOME/Library/Arduino15-hearlink/arduino-cli.yaml
if [ "${CORE:-}" = old ]; then OUT=build/$SKETCH-$2-old
elif [ -f "$OWNCFG" ]; then CFG=$OWNCFG
fi
rm -f "$OUT/$SKETCH.ino.bin"   # иначе при ошибке сборки в плату ушла бы прежняя прошивка
"$IDECLI" --config-file "$CFG" compile -b $FQBN --build-path "$OUT" --warnings default "$SKETCH" 2>&1 \
  | grep -E "Sketch uses|Global variables|warning|rror" || true
[ -f "$OUT/$SKETCH.ino.bin" ] || { echo "сборка не удалась"; exit 1; }
if [ -n "$3" ]; then
  # Заливка по кабелю: процессор на это время стоит в загрузчике и рисовать не может — экран передатчика около минуты
  # чёрный. Поэтому сначала команда M8 (прошивка 2.50+): на экране крупно «ОНОВЛЕННЯ ПРОШИВКИ… ЕКРАН ЗГАСНЕ НА ХВИЛИНУ…
  # НЕ ВИМИКАЙТЕ ЖИВЛЕННЯ». Приёмник и старая прошивка эту команду просто не поймут. NOTE=0 — без предупреждения.
  # Когда порт передатчика «говорит» (вход — встроенный АЦП), лучше обновлять его же средствами, с ходом на экране:
  #   python3 tools/sendfile.py <порт> build/hearlink-s3/hearlink.ino.bin /UPDATE/hearlink-<версия>.bin, затем M3 и M2.
  if [ "$SKETCH" = hearlink ] && [ "${NOTE:-1}" != 0 ]; then
    python3 tools/port.py "$3" 6 M8 >/dev/null 2>&1 || true
  fi
  # SPEED=115200 ./build.sh … — для плат, где мост USB сбоит на большой скорости (модуль 4848S040 с CH340)
  "$IDECLI" --config-file "$CFG" upload -b $FQBN -p "$3" --input-dir "$OUT" ${SPEED:+--upload-property upload.speed=$SPEED} 2>&1 \
    | grep -E -i "Chip is|Wrote|verified|error|fatal|Hard resetting" | cut -c1-200
fi
