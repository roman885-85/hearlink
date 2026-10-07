# Встановлення

[← до головної](../../README.uk.md) · [English](../en/install.md)

Прошивка одна для всіх плат. Свіжопрошита плата — **приймач**; передавачем її роблять дві команди в порт.

## Що знадобиться

- кабель USB **із даними** (із зарядним плату не видно);
- комп'ютер із macOS, Windows або Linux;
- архів `hearlink-<версія>-flash-kit.zip` з [випусків](../../../releases).

У архіві:

| Файл | Адреса у флеш | Що це |
|---|---|---|
| `firmware/bootloader.bin` | `0x0` | завантажувач |
| `firmware/partitions.bin` | `0x8000` | розмітка пам'яті |
| `firmware/boot_app0.bin` | `0xe000` | вибір розділу прошивки |
| `firmware/hearlink.bin` | `0x10000` | прошивка |
| `firmware/assets.bin` | `0x310000` | перевірочні звуки передавача (голос, музика) — приймачу не потрібні |
| `на карту/UPDATE/hearlink-<версія>.bin` | — | файл для оновлення через картку пам'яті |
| `tools/esptool` | — | програма прошивання для macOS |
| `1 Прошить приёмник.command` … | — | сценарії для macOS (подвійне клацання) |
| `firmware/ВЕРСИЯ.txt` | — | версія й контрольні суми SHA-256 |

## Програма прошивання — esptool

- **macOS** — уже в архіві (`tools/esptool`).
- **Windows** — `esptool-…-win64.zip` зі сторінки <https://github.com/espressif/esptool/releases>.
- **Linux** — `pip install esptool` або пакет дистрибутива.
- Драйвер моста USB: у модуля передавача CH340, у плат S3 — CH343 або «рідний» USB. У Windows 10/11 і macOS вони
  зазвичай уже є; якщо порт не з'являється — драйвер CH34x із сайту WCH.

Назва порту: macOS — `/dev/cu.usbserial-…` або `/dev/cu.usbmodem…`, Linux — `/dev/ttyUSB0` або `/dev/ttyACM0`,
Windows — `COM5` (дивитися в «Диспетчері пристроїв»).

## Приймач

macOS: запустити `1 Прошить приёмник.command`.

Будь-яка система, з теки архіву:

```
esptool --chip esp32s3 --port ПОРТ --baud 460800 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware/bootloader.bin 0x8000 firmware/partitions.bin 0xe000 firmware/boot_app0.bin 0x10000 firmware/hearlink.bin
```

Після прошивання плата перезапуститься: на екрані заставка, потім «НЕ ПІДКЛЮЧЕНО» з кодом — приймач просить
доступ до набору (див. [Користування](user-guide.md#додати-приймач)).

## Передавач

macOS: `2 Прошить передатчик.command`, на питання про звуки для нового модуля відповісти «д».

Будь-яка система (швидкість 230400 — міст CH340 цього модуля на більшій помиляється):

```
esptool --chip esp32s3 --port ПОРТ --baud 230400 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware/bootloader.bin 0x8000 firmware/partitions.bin 0xe000 firmware/boot_app0.bin 0x10000 firmware/hearlink.bin 0x310000 firmware/assets.bin
```

Звуки (`assets.bin`, 8,7 МБ) заливаються близько 9 хвилин і потрібні лише раз; при наступних прошиваннях останню
пару можна не писати.

Потім призначити роль. Відкрити порт на **115200 бод** (будь-яка програма-термінал: монітор порту Arduino IDE,
PuTTY, `screen`) і надіслати два рядки:

```
t
b1
```

`t` — «ти передавач», `b1` — «плата з екраном»; після кожної плата перезапускається. Назад: `r` (приймач),
`b0` (звичайна плата). З macOS/Linux те саме робить `python3 tools/port.py ПОРТ 6 t`.

## Якщо прошивання не йде

- Перевести плату в режим прошивання вручну: затиснути **BOOT**, коротко натиснути **RESET** (або подати
  живлення), відпустити BOOT — і повторити команду.
- Зменшити швидкість: `--baud 115200`.
- Модуль передавача живити від блока 5 В ≥ 1 А, не від порту без запасу струму.
- Повне стирання (зітре й налаштування, і доступ до набору — приймач доведеться додавати знову):
  `esptool --chip esp32s3 --port ПОРТ erase-flash`, потім прошити заново.

Налаштування, ключ набору та список приймачів лежать в окремому місці пам'яті — звичайне прошивання їх зберігає.

## Що на порту?

`3 Что подключено.command` (macOS) або `esptool --chip esp32s3 --port ПОРТ chip-id`. Версію прошивки й роль плата
друкує сама у відповідь на `?` у порту.

> Якщо у вас поруч є інші плати з мостом CH340 (Arduino-сумісні тощо) — від'єднайте їх: esptool їм ні до чого,
> а порти виглядають однаково.

## Оновлення вже працюючого набору

Кабель не потрібен — див. [Користування → Оновлення](user-guide.md#оновлення-прошивки).

## Збирання з вихідних текстів

1. Arduino IDE 2.x (або `arduino-cli`), у «Менеджері плат» — **esp32 by Espressif 3.3.3** (ESP-IDF 5.5).
   З іншими версіями ядра не перевірялося.
2. Бібліотека **U8g2** (olikraus) через «Менеджер бібліотек». Більше нічого ставити не треба.
3. Відкрити `hearlink/hearlink.ino`. Плата: *ESP32S3 Dev Module*; **PSRAM: OPI PSRAM**; **Flash Size: 16MB**.
   Розмітку пам'яті IDE візьме з `hearlink/partitions.csv` сама.
4. Зібрати й залити. Для модуля передавача — Upload Speed 230400.

З командного рядка (macOS/Linux):

```
./build.sh hearlink s3                                    # тільки зібрати → build/hearlink-s3/
./build.sh hearlink s3 /dev/cu.usbmodemXXXX               # зібрати й залити (приймач)
SPEED=230400 ./build.sh hearlink s3 /dev/cu.usbserial-XX  # те саме для модуля передавача
```

`build.sh` викликає `arduino-cli` з налаштуваннями
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB`.

Перевірочні звуки: покласти свої `assets-src/golos.wav` (голос), `muzyka.mp3`, `muzyka2.mp3`… та назви в
`assets-src/nazvy.txt`, далі `python3 tools/make_sounds.py` і `tools/flash_assets.sh ПОРТ`.

Набір для прошивання (те, що лежить у випусках) збирає `tools/make_kit.sh`.

### Після зміни написів

Усі написи — українською в тексті програми; англійський переклад — словник `tools/lang_en.py`.
`python3 tools/make_lang.py` збирає `hearlink/lang_en.h` і показує написи без перекладу.

### Екрани без плати

- `tools/sim/run.sh` — меню передавача → картинки `tools/sim/out/*.png` (той самий `txui.h`, що йде в плату);
- `tools/simrx/run.sh` — екрани приймача → `tools/simrx/out/kadry.png` і `pryimach.gif`;
- `tools/host/run.sh` — перевірка стиснення звуку й шифру на комп'ютері.
