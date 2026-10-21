# -*- coding: utf-8 -*-
"""Тональний баланс запису слів I2S (передавач: e19=<мс>, tools/adcdump.py) — третьоктавні смуги.
   python3 bridge/tools/bands.py запис.bin [другий.bin]
   python3 bridge/tools/bands.py a1.bin a2.bin … -- b1.bin b2.bin …   (групи: потужність усереднюється по записах групи)
Сталий складник, середньоквадратичний рівень і пік Л та П (дБ від повної шкали); далі — потужність у третьоктавних
смугах 25 Гц … 16 кГц для середини (Л+П)/2 і різниці (Л−П)/2: середнє за часом вікнами Ганна по 4096 відліків з
перекриттям навпіл (у spectrum_check.py — окремі частоти за одне вікно на весь запис: для музики це лотерея).
З другим файлом — ще й різниця середин «перший мінус другий» по смугах.
Без numpy (його тут немає): своє БПФ."""
import cmath, math, os, struct, sys

FS = 32000.0
N = 4096


def load(path):
    """Запис e19 — слова I2S «Л П Л П…» (24 біти у 31…8). Запис e22 (у назві файла «efir») — те, що передавач шле
    в ефір після всієї обробки: слово = середина (молодші 16 біт) | різниця (старші 16), по одному на відлік;
    тут з нього роблять Л = с + р, П = с − р у шкалі 24 біт, щоб смуги порівнювались з e19."""
    d = open(path, 'rb').read()
    w = struct.unpack('<%di' % (len(d) // 4), d)
    if 'efir' in os.path.basename(path):
        def s16(v):
            v &= 0xFFFF
            return v - 65536 if v & 0x8000 else v
        L, R = [], []
        for x in w:
            m, sd = s16(x), s16(x >> 16)
            L.append((m + sd) * 256)
            R.append((m - sd) * 256)
        return L, R
    L = [x >> 8 for x in w[0::2]]
    R = [x >> 8 for x in w[1::2]]
    n = min(len(L), len(R))
    return L[:n], R[:n]


def fft(a):
    n = len(a)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    m = 2
    while m <= n:
        wm = cmath.exp(-2j * math.pi / m)
        h = m // 2
        tw = [wm ** k for k in range(h)]
        for s in range(0, n, m):
            for k in range(h):
                t = tw[k] * a[s + k + h]
                u = a[s + k]
                a[s + k] = u + t
                a[s + k + h] = u - t
        m <<= 1
    return a


WIN = [0.5 - 0.5 * math.cos(2 * math.pi * i / N) for i in range(N)]
WPOW = sum(v * v for v in WIN)


def psd(x):
    """Середня потужність на відлік БПФ (одностороння), у частках повної шкали 24 біт у квадраті."""
    acc = [0.0] * (N // 2 + 1)
    segs = 0
    for s in range(0, len(x) - N + 1, N // 2):
        seg = x[s:s + N]
        mu = sum(seg) / N
        X = fft([complex((v - mu) * WIN[i] / 8388608.0, 0) for i, v in enumerate(seg)])
        for k in range(N // 2 + 1):
            p = (X[k].real ** 2 + X[k].imag ** 2) / (WPOW * N)
            acc[k] += p * (1 if k in (0, N // 2) else 2)
        segs += 1
    return [v / max(segs, 1) for v in acc], segs


CENTERS = [25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500,
           3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]


def bands(p):
    out = []
    df = FS / N
    for fc in CENTERS:
        lo, hi = fc / 2 ** (1 / 6), min(fc * 2 ** (1 / 6), FS / 2)
        s = 0.0
        for k in range(1, N // 2 + 1):
            f = k * df
            if lo <= f < hi:
                s += p[k]
        out.append(10 * math.log10(s + 1e-20))
    return out


def stats(x):
    n = len(x)
    dc = sum(x) / n
    rms = math.sqrt(sum((v - dc) ** 2 for v in x) / n)
    pk = max(abs(v) for v in x)
    db = lambda v: 20 * math.log10(v / 8388608.0 + 1e-12)
    return dc, db(rms), db(pk)


def analyse(paths):
    """Група записів: потужність смуг усереднюється по всіх вікнах усіх записів."""
    pm_all, ps_all, segs_all = None, None, 0
    for path in paths:
        L, R = load(path)
        n = len(L)
        print('%s: %d відліків (%.2f с)' % (path, n, n / FS))
        for name, x in (('Л', L), ('П', R)):
            dc, rms, pk = stats(x)
            print('  %s: сталий складник %+.0f (з 2^23), рівень %.1f дБ, пік %.1f дБ' % (name, dc, rms, pk))
        M = [(a + b) / 2 for a, b in zip(L, R)]
        S = [(a - b) / 2 for a, b in zip(L, R)]
        pm, segs = psd(M)
        ps, _ = psd(S)
        if pm_all is None:
            pm_all, ps_all = [v * segs for v in pm], [v * segs for v in ps]
        else:
            pm_all = [a + v * segs for a, v in zip(pm_all, pm)]
            ps_all = [a + v * segs for a, v in zip(ps_all, ps)]
        segs_all += segs
    return bands([v / segs_all for v in pm_all]), bands([v / segs_all for v in ps_all]), segs_all


args = sys.argv[1:]
if '--' in args:
    g1, g2 = args[:args.index('--')], args[args.index('--') + 1:]
else:
    g1, g2 = args[:1], args[1:2]
bm, bs, segs = analyse(g1)
second = analyse(g2) if g2 else None
print('третьоктавні смуги, дБ від повної шкали; вікон %d' % segs)
hdr = '%7s %8s %8s' % ('Гц', 'серед.', 'різн.')
if second:
    hdr += ' %8s %8s %9s' % ('серед.2', 'різн.2', '1 − 2')
print(hdr)
ref = None
for i, fc in enumerate(CENTERS):
    line = '%7g %8.1f %8.1f' % (fc, bm[i], bs[i])
    if second:
        line += ' %8.1f %8.1f %+9.1f' % (second[0][i], second[1][i], bm[i] - second[0][i])
    print(line)
