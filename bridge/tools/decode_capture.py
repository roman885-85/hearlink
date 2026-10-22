# -*- coding: utf-8 -*-
"""Розібрати запис сирих слів I2S, які передавач отримав від мосту (передавач: e21=<мс>, потім tools/adcdump.py).
   python3 bridge/tools/decode_capture.py запис.bin
Показує перші слова, рівень звуку (старші 24 біти) і кадри стану моста з молодших 8 біт (COBS + CRC-16, bridge_proto.h)."""
import math, struct, sys

data = open(sys.argv[1], 'rb').read()
w = struct.unpack('<%di' % (len(data) // 4), data[:len(data) // 4 * 4])
print('слів %d; перші 16: %s' % (len(w), ' '.join('%08x' % (x & 0xFFFFFFFF) for x in w[:16])))
for ch, name in ((0, 'Л'), (1, 'П')):
    s = [x >> 8 for x in w[ch::2]]
    rms = math.sqrt(sum(v * v for v in s) / len(s)) if s else 0
    print('%s: від %d до %d, СКЗ %.0f (%.1f дБ від повної шкали 24 біти)' % (
        name, min(s), max(s), rms, 20 * math.log10(rms / 8388608) if rms else -999))
# байт на кадр: біти 7…4 лівого слова — старший напівбайт, біти 7…4 правого — молодший (bridge_proto.h)
stream = bytes((w[i] & 0xF0) | ((w[i + 1] >> 4) & 0x0F) for i in range(0, len(w) - 1, 2))
nz = sum(1 for b in stream if b)
low = sum(1 for x in w if x & 0x0F)
print('байтів зворотного каналу не нуль: %d з %d; слів з бітами 3…0 не нуль: %d (мають бути 0)' % (nz, len(stream), low))


def crc16(p):
    c = 0xFFFF
    for b in p:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def cobs_dec(src):
    out, i = bytearray(), 0
    while i < len(src):
        code = src[i]; i += 1
        if code == 0:
            return None
        for _ in range(code - 1):
            if i >= len(src):
                return None
            out.append(src[i]); i += 1
        if code != 0xFF and i < len(src):
            out.append(0)
    return bytes(out)


F = '<12BIIhhHHHHI'
N = struct.calcsize(F)
ACT = ['нічого', 'AES', 'PCM1808', 'тиша', 'тон', 'перемикання']
AES = ['немає', 'ловлю', 'є', 'частота не та']
good = bad = 0
last = None
for chunk in stream.split(b'\x00'):
    if not chunk:
        continue
    d = cobs_dec(chunk)
    if d is None or len(d) != N + 2 or crc16(d[:N]) != (d[N] | d[N + 1] << 8):
        bad += 1
        continue
    good += 1
    last = struct.unpack(F, d[:N])
print('кадрів стану: цілих %d, зіпсованих %d' % (good, bad))
if last:
    (typ, vmaj, vmin, seq, src, fmt, tone, act, aes, pl, pr, lver, rate, par, ppm10, lev, under, over, resync, cpu1,
     up) = last
    print('міст %d.%02d (завантажувач %d), працює %d с; звучить: %s; AES: %s %d Гц, парність %d; запас %d, поправка %+.1f ppm,'
          ' провали %d/%d; перескоки %d; ядро 1 %d‰; пік Л -%.1f дБ, П -%.1f дБ' % (
              vmaj, vmin, lver, up, ACT[act] if act < len(ACT) else act, AES[aes] if aes < 4 else aes, rate, par,
              lev, ppm10 / 10, under, over, resync, cpu1, pl / 2, pr / 2))
