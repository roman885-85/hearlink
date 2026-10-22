#!/bin/bash
# Збирання моста: перевірка перерахунку частоти на Mac, потім прошивки Pico (програма + завантажувач) і образи:
#   build/bridge-full.uf2 — перша запис з комп'ютера; ../hearlink/bridge_fw.h — образ для оновлення з передавача.
# Середовище (компілятор Arm, Pico SDK) — ./setup-rp2040.sh, у ~/Library/hearlink-rp2040.
set -e
cd "$(dirname "$0")"
R="$HOME/Library/hearlink-rp2040"
export PICO_SDK_PATH="$R/pico-sdk"
export PICO_TOOLCHAIN_PATH="$R/arm-gnu-toolchain-14.2.rel1-darwin-x86_64-arm-none-eabi"
T="${TMPDIR:-/tmp}/hearlink-asrc-test.$$"
cc -O2 -Wall -Wextra -Wno-missing-field-initializers -Isrc test/asrc_test.c src/asrc.c -lm -o "$T"
"$T" > build-asrc-test.txt || { cat build-asrc-test.txt; rm -f "$T"; echo "ПЕРЕВІРКА ПЕРЕРАХУНКУ НЕ ПРОЙШЛА"; exit 1; }
rm -f "$T"
tail -1 build-asrc-test.txt
cmake -S . -B build -G Ninja -DPICO_BOARD=pico > build-cmake.log 2>&1 || { tail -30 build-cmake.log; exit 1; }
ninja -C build
ls -la build/bridge.bin build/loader.bin build/bridge-full.uf2
