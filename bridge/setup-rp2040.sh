#!/bin/bash
# Середовище збирання для мосту на RP2040 (поза git): компілятор Arm, Pico SDK, бібліотека прийому S/PDIF.
# Усе кладеться в ~/Library/hearlink-rp2040; системні та brew-пакети не чіпаються.
set -e
R="$HOME/Library/hearlink-rp2040"
mkdir -p "$R"
cd "$R"
TC=arm-gnu-toolchain-14.2.rel1-darwin-x86_64-arm-none-eabi
if [ ! -x "$TC/bin/arm-none-eabi-gcc" ]; then
  curl -fL --retry 3 -o "$TC.tar.xz" "https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/$TC.tar.xz"
  tar -xJf "$TC.tar.xz"
  rm -f "$TC.tar.xz"
fi
"$TC/bin/arm-none-eabi-gcc" --version | head -1
[ -d pico-sdk ] || git clone --depth 1 -b 2.3.1 https://github.com/raspberrypi/pico-sdk.git
(cd pico-sdk && git submodule update --init --depth 1 lib/tinyusb)
[ -d pico-extras ] || git clone --depth 1 -b sdk-2.3.0 https://github.com/raspberrypi/pico-extras.git
[ -d pico_spdif_rx ] || git clone --depth 1 --recursive --shallow-submodules https://github.com/elehobica/pico_spdif_rx.git
du -sh "$R"/* 2>/dev/null
echo "ГОТОВО: $R"
