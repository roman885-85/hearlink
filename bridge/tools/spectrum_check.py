# -*- coding: utf-8 -*-
"""Спектр і полярність запису слів I2S від мосту (передавач: e19=1000, tools/adcdump.py).
   python3 bridge/tools/spectrum_check.py запис.bin
Гьорцель на кількох частотах для Л, П, суми (Л+П) і різниці (Л−П); кореляція Л і П на низах (ковзне середнє як ФНЧ).
Обернена полярність одного каналу: на низах різниця сильніша за суму, кореляція від'ємна."""
import math, struct, sys

d = open(sys.argv[1], 'rb').read()
w = struct.unpack('<%di' % (len(d) // 4), d)
L = [x >> 8 for x in w[0::2]]
R = [x >> 8 for x in w[1::2]]
n = min(len(L), len(R))
L, R = L[:n], R[:n]
FS = 32000.0


def goertzel(x, f):
    k = 2 * math.cos(2 * math.pi * f / FS)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + k * s1 - s2
        s2, s1 = s1, s0
    p = s1 * s1 + s2 * s2 - k * s1 * s2
    return 10 * math.log10(p / (n * n) + 1e-20)


M = [a + b for a, b in zip(L, R)]
S = [a - b for a, b in zip(L, R)]
print('відліків %d (%.2f с)' % (n, n / FS))
print('%7s %8s %8s %8s %8s' % ('Гц', 'Л', 'П', 'Л+П', 'Л−П'))
for f in (40, 63, 100, 160, 250, 400, 630, 1000, 2000, 4000, 8000, 12000):
    print('%7d %8.1f %8.1f %8.1f %8.1f' % (f, goertzel(L, f), goertzel(R, f), goertzel(M, f), goertzel(S, f)))


def lp(x, k=64):   # ковзне середнє ~ ФНЧ до ~250 Гц
    out, acc = [], 0.0
    for i, v in enumerate(x):
        acc += v
        if i >= k:
            acc -= x[i - k]
        out.append(acc / k)
    return out


def corr(a, b):
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    da = math.sqrt(sum((x - ma) ** 2 for x in a))
    db = math.sqrt(sum((y - mb) ** 2 for y in b))
    return num / (da * db + 1e-9)


print('кореляція Л і П: уся смуга %+.2f; низи (<~250 Гц) %+.2f' % (corr(L, R), corr(lp(L), lp(R))))
