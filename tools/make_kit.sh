#!/bin/sh
# Собирает «набор восстановления» — папку kit/ со всем, чем прошить новую плату или оживить испорченную:
# прошивка, загрузчик, разметка, звуки передатчика, программа заливки и сценарии, которые запускаются двойным щелчком.
# Владелец 07.10: «всегда должен быть набор программ и прошивок для восстановления устройств или для прошивки новых».
# Запускать после каждой новой версии:   tools/make_kit.sh      (берёт то, что сейчас лежит в build/hearlink-s3)
set -e
cd "$(dirname "$0")/.."
B=build/hearlink-s3
[ -f $B/hearlink.ino.bin ] || { echo "сначала собрать: ./build.sh hearlink s3"; exit 1; }
VER=$(sed -n 's/^#define FW_VERSION "\(.*\)".*/\1/p' hearlink/config.h)
strings -a $B/hearlink.ino.bin | grep -q "HEARLINK-FW:$VER" || { echo "в build/ лежит не версия $VER — пересобрать"; exit 1; }
A=$HOME/Library/Arduino15/packages/esp32
[ -d "$HOME/Library/Arduino15-hearlink/packages/esp32" ] && A=$HOME/Library/Arduino15-hearlink/packages/esp32   # ядро проекта (см. build.sh)
E=$(ls -d $A/tools/esptool_py/*/ | tail -1)
K=kit
rm -rf $K
mkdir -p $K/firmware $K/tools "$K/на карту/UPDATE"
cp $B/hearlink.ino.bin $K/firmware/hearlink.bin
cp $B/hearlink.ino.bootloader.bin $K/firmware/bootloader.bin
cp $B/hearlink.ino.partitions.bin $K/firmware/partitions.bin
cp $(ls $A/hardware/esp32/*/tools/partitions/boot_app0.bin | tail -1) $K/firmware/boot_app0.bin
[ -f build/assets.bin ] && cp build/assets.bin $K/firmware/assets.bin
cp $B/hearlink.ino.bin "$K/на карту/UPDATE/hearlink-$VER.bin"
cp "$E/esptool" $K/tools/esptool
[ -f "$E/LICENSE" ] && cp "$E/LICENSE" $K/tools/esptool-LICENSE
cp tools/port.py $K/tools/port.py
{
  echo "прошивка набора: версия $VER, собрана $(date '+%Y-%m-%d %H:%M')"
  echo "контрольные суммы SHA-256:"
  (cd $K/firmware && shasum -a 256 *.bin)
} > $K/firmware/ВЕРСИЯ.txt

cat > $K/tools/lib.sh <<'LIB'
# общее для сценариев набора: выбор порта и заливка
cd "$(dirname "$0")"
ESPTOOL=./tools/esptool
chmod +x "$ESPTOOL" 2>/dev/null
xattr -d com.apple.quarantine "$ESPTOOL" 2>/dev/null
pick_port() {
  echo
  echo "Подключённые платы:"
  i=0
  for p in /dev/cu.usb*; do
    [ -e "$p" ] || continue
    i=$((i + 1))
    echo "  $i) $p"
    eval "P$i=\"$p\""
  done
  [ $i -gt 0 ] || { echo "  ни одной — подключите плату кабелем USB (кабель должен быть с данными, не только зарядный)"; exit 1; }
  echo
  echo "ВНИМАНИЕ: не выбирайте порт платы усилителя наушников (LGT8F328P) — ей эта заливка вредна."
  echo "Если не уверены, какая плата на каком порту, — отключите всё лишнее и запустите заново."
  if [ $i -eq 1 ]; then n=1; printf "Порт один. Заливать в него? (Enter — да, Ctrl+C — отмена) "; read x
  else printf "Номер порта: "; read n; fi
  eval "PORT=\$P$n"
  [ -n "$PORT" ] || { echo "нет такого порта"; exit 1; }
}
# flash <скорость> [ещё пары адрес файл …]
flash() {
  speed=$1; shift
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud "$speed" --before default-reset --after hard-reset write-flash -z \
    --flash-mode keep --flash-freq keep --flash-size keep \
    0x0 firmware/bootloader.bin 0x8000 firmware/partitions.bin 0xe000 firmware/boot_app0.bin 0x10000 firmware/hearlink.bin "$@" \
    || { echo; echo "НЕ ВЫШЛО. Что попробовать — в файле «ПРОЧТИ.md», раздел «Если заливка не идёт»."; exit 1; }
}
done_msg() { echo; echo "ГОТОВО: $1"; echo "Окно можно закрыть."; }
LIB

cat > "$K/1 Прошить приёмник.command" <<'S'
#!/bin/sh
. "$(dirname "$0")/tools/lib.sh"
echo "ПРИЁМНИК (плата ESP32-S3 N16R8): прошивка $(head -1 firmware/ВЕРСИЯ.txt)"
echo "Настройки и доступ к набору, уже записанные в плату, сохраняются."
pick_port
flash 460800
done_msg "плата — приёмник. Новый приёмник добавляется на передатчике: «Приймачі» → «+ Додати»."
S

cat > "$K/2 Прошить передатчик.command" <<'S'
#!/bin/sh
. "$(dirname "$0")/tools/lib.sh"
echo "ПЕРЕДАТЧИК (модуль ESP32-4848S040 с экраном): прошивка $(head -1 firmware/ВЕРСИЯ.txt)"
echo "Настройки, ключ набора и список приёмников, уже записанные в плату, сохраняются."
pick_port
printf "Залить и проверочные звуки (голос и музыка, 9 минут)? Нужно для нового модуля. (д/Н) "; read a
case "$a" in
  д|Д|y|Y) flash 230400 0x310000 firmware/assets.bin ;;
  *) flash 230400 ;;
esac
echo "Назначаю плате роль передатчика с экраном…"
sleep 4
python3 tools/port.py "$PORT" 6 t >/dev/null 2>&1
sleep 2
python3 tools/port.py "$PORT" 6 b1 >/dev/null 2>&1
done_msg "плата — передатчик. Если экран не засветился — см. «ПРОЧТИ.md», раздел «Роль платы»."
S

cat > "$K/3 Что подключено.command" <<'S'
#!/bin/sh
. "$(dirname "$0")/tools/lib.sh"
pick_port
echo "Слушаю $PORT восемь секунд (и спрашиваю у платы настройки)…"
python3 tools/port.py "$PORT" 8 "?" 2>&1 | cut -c1-200
done_msg "строка «версія …, ПЕРЕДАВАЧ» или «ПРИЙМАЧ» — это плата набора; пусто — не она (или приёмник спит)."
S
chmod +x "$K"/*.command $K/tools/esptool

cat > $K/ПРОЧТИ.md <<DOC
# Набор восстановления и прошивки — «Відродження», набор для слабослышащих

Версия прошивки в наборе: **$VER** (см. \`firmware/ВЕРСИЯ.txt\`). Набор собран $(date '+%d.%m.%Y').
Прошивка одна на все платы: приёмник это или передатчик, записано в самой плате.

## Что здесь

| Файл | Для чего |
|---|---|
| \`1 Прошить приёмник.command\` | новая плата приёмника или приёмник, который перестал запускаться |
| \`2 Прошить передатчик.command\` | новый модуль передатчика или передатчик, который перестал запускаться |
| \`3 Что подключено.command\` | узнать, что за плата на порту и какая в ней версия |
| \`на карту/UPDATE/hearlink-$VER.bin\` | файл для обычного обновления через карту памяти |
| \`firmware/\` | сама прошивка по частям (загрузчик, разметка, прошивка, звуки передатчика) |
| \`tools/esptool\` | программа заливки (macOS) |

Сценарии \`.command\` запускаются двойным щелчком на Mac. При первом запуске macOS может спросить разрешение —
правой кнопкой → «Открыть».

## Обычное обновление (кабель не нужен)

1. Файл \`hearlink-….bin\` положить на карту памяти передатчика в папку **UPDATE** (папку передатчик создаёт сам).
2. Вставить карту. Передатчик сам найдёт файл и откроет окно «Оновлення» («Приймачі» → «Оновлення»).
3. «Оновити»: сначала приёмники — по радио (около 15 с, звук в эфире стоит), затем сам передатчик (около минуты,
   экран мигает — на нём крупно «ОНОВЛЕННЯ ПРОШИВКИ — НЕ ВИМИКАЙТЕ ЖИВЛЕННЯ!!!»), потом он перезапускается.
   Приёмники должны быть включены; спящий не обновится — его потом обновит передатчик («Оновлення» → «Почати»).
4. «Пробне» в том же окне — показать, как это выглядит: приёмники всё принимают, но ничего не записывают.

## Новый приёмник

1. Плату ESP32-S3 (N16R8) подключить кабелем к Mac, запустить \`1 Прошить приёмник.command\`.
2. Включить приёмник рядом с передатчиком. На передатчике: «Приймачі» → «+ Додати», сверить код на двух экранах,
   «Дозволити».

## Новый передатчик

1. Модуль ESP32-4848S040 подключить к Mac, запустить \`2 Прошить передатчик.command\`, на вопрос о звуках — «д».
2. Сценарий сам назначит плате роль передатчика с экраном. Приёмники к новому передатчику добавляются заново.

## Восстановление

Плата не запускается, экран тёмный, обновление оборвалось на записи: запустить сценарий 1 или 2 — он запишет
прошивку заново. Настройки, ключ набора и список приёмников лежат в другом месте памяти и сохраняются.
Оборванное обновление по радио приёмник не портит: пока новая прошивка не записана и не проверена целиком, он
запускается с прежней.

## Если заливка не идёт

- Кабель должен быть «с данными» (с зарядным плата не видна).
- Перевести плату в режим заливки вручную: зажать кнопку **BOOT**, коротко нажать **RESET** (или подать питание),
  отпустить BOOT — и запустить сценарий снова.
- У модуля передатчика мост USB не любит высокую скорость — сценарий 2 уже работает на 230400.
- Полная очистка платы (сотрёт и настройки, и доступ к набору — приёмник придётся добавлять заново):
  \`tools/esptool --chip esp32s3 --port <порт> erase-flash\`, затем сценарий 1 или 2.

## Роль платы

Свежепрошитая плата — приёмник. Передатчиком с экраном её делают две команды в порт (115200 бод):
\`t\` (передатчик) и \`b1\` (модуль с экраном); обратно — \`r\` и \`b0\`. С Mac:
\`python3 tools/port.py <порт> 6 t\`, затем \`python3 tools/port.py <порт> 6 b1\`.

## Windows

Нужна программа esptool (https://github.com/espressif/esptool/releases, файл esptool-…-win64.zip). Команда та же:

\`\`\`
esptool --chip esp32s3 --port COM5 --baud 460800 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware\\bootloader.bin 0x8000 firmware\\partitions.bin 0xe000 firmware\\boot_app0.bin 0x10000 firmware\\hearlink.bin
\`\`\`

Для передатчика — скорость 230400 и, для нового модуля, в конце добавить \`0x310000 firmware\\assets.bin\`.
DOC
echo "набор собран: $K (версия $VER)"; du -sh $K | cut -f1; ls -1 $K
