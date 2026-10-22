# -*- coding: utf-8 -*-
"""Образи моста після збирання.
   make_images.py loader.bin bridge.bin version.h bridge-full.uf2 ../hearlink/bridge_fw.h
1) bridge-full.uf2 — для ПЕРШОЇ записи з комп'ютера (кнопка BOOTSEL + USB): завантажувач (0x10000000), відомості про
   програму (0x10007000: розмір, CRC-32, версія) і програма (0x10008000) — одним файлом.
2) hearlink/bridge_fw.h — та сама програма всередині прошивки передавача: він сам оновлює нею міст."""
import re
import struct
import sys
import zlib

loader_bin, app_bin, version_h, out_uf2, out_h = sys.argv[1:6]
loader = open(loader_bin, 'rb').read()
app = open(app_bin, 'rb').read()
ver_src = open(version_h, encoding='utf-8').read()
major = int(re.search(r'BRIDGE_VER_MAJOR\s+(\d+)', ver_src).group(1))
minor = int(re.search(r'BRIDGE_VER_MINOR\s+(\d+)', ver_src).group(1))
if len(loader) > 0x7000:
    sys.exit('завантажувач більший за 28 КБ: %d' % len(loader))
crc = zlib.crc32(app) & 0xFFFFFFFF
INFO_MAGIC = 0x4D695374
info = struct.pack('<IIIII', INFO_MAGIC, len(app), crc, (major << 8) | minor, (~INFO_MAGIC) & 0xFFFFFFFF)
info += b'\xff' * (256 - len(info))


def pad(b):
    return b + b'\xff' * ((-len(b)) % 256)


regions = [(0x10000000, pad(loader)), (0x10007000, info), (0x10008000, pad(app))]
blocks = []
for addr, data in regions:
    for off in range(0, len(data), 256):
        blocks.append((addr + off, data[off:off + 256]))
uf2 = bytearray()
for n, (addr, data) in enumerate(blocks):
    hdr = struct.pack('<IIIIIIII', 0x0A324655, 0x9E5D5157, 0x00002000, addr, 256, n, len(blocks), 0xE48BFF56)
    uf2 += hdr + data + b'\x00' * (476 - len(data)) + struct.pack('<I', 0x0AB16F30)
open(out_uf2, 'wb').write(uf2)

lines = []
for i in range(0, len(app), 16):
    lines.append('  ' + ', '.join('0x%02x' % x for x in app[i:i + 16]) + ',')
h = '''// Образ програми моста (Raspberry Pi Pico) для оновлення з передавача. НЕ ПРАВИТИ РУКАМИ — генерує
// bridge/tools/make_images.py під час збирання моста (bridge/build.sh) з bridge.bin і bridge/src/version.h.
#pragma once
#include <stdint.h>

#define BRIDGE_FW_VER_MAJOR %d
#define BRIDGE_FW_VER_MINOR %d
#define BRIDGE_FW_SIZE %du
#define BRIDGE_FW_CRC32 0x%08xu

static const uint8_t bridgeFw[BRIDGE_FW_SIZE] = {
%s
};
''' % (major, minor, len(app), crc, '\n'.join(lines))
open(out_h, 'w', encoding='utf-8').write(h)
print('міст %d.%02d: програма %d байт, CRC-32 %08x; завантажувач %d байт; %s (%d блоків)' % (
    major, minor, len(app), crc, len(loader), out_uf2, len(blocks)))
