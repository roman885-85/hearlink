# -*- coding: utf-8 -*-
"""Эмблема церкви для экрана передатчика: hearlink/logo.h.

Берётся та же картинка, что в шапке сайта и панелей. Кроме целой эмблемы (большой и малой) делаются три слоя —
глобус, книга и крест — для заставки, где они появляются по очереди. Слои вырезаются по цвету:
крест — оранжевый, море — голубое, книга — всё светлое ниже верхнего края страниц (край находится заливкой
по чуть сероватому фону страниц) и тёмная обложка. Под крестом и под книгой слои дорисованы, чтобы при
появлении частей не было дыр.  Запуск: python3 tools/gen_logo.py [путь к logo-emblem.png]"""
import os, sys
from collections import deque
from PIL import Image

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Documents/cloude_work/public/images/logo-emblem.png')
BG = (16, 12, 10)   # C_BG из txui.h
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'hearlink', 'logo.h')
PREVIEW = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build')
BIG_H, SM_H = 230, 52


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def flat(im, height):
    w = round(im.width * height / im.height)
    im = im.resize((w, height), Image.LANCZOS)
    bg = Image.new('RGBA', im.size, BG + (255,))
    px = Image.alpha_composite(bg, im).convert('RGB').load()
    return w, height, [rgb565(*px[x, y]) for y in range(height) for x in range(w)]


def layers(im):
    W, H = im.size
    px = im.load()

    def light(c):
        return c[3] >= 128 and min(c[:3]) > 150 and max(c[:3]) - min(c[:3]) < 30

    def is_cross(c):
        r, g, b, a = c
        return a >= 64 and r > 180 and 80 < g < 220 and b < 130 and r - b > 90

    def is_sea(c):
        r, g, b, a = c
        return a >= 64 and b > 180 and r < 205 and b - r > 25

    # верхний край книги: заливка по сероватому фону страниц от двух точек на страницах
    seen = [[False] * W for _ in range(H)]
    q = deque([(W * 30 // 100, H * 65 // 100), (W * 68 // 100, H * 65 // 100)])
    for x, y in q:
        seen[y][x] = True
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            xx, yy = x + dx, y + dy
            if 0 <= xx < W and 0 <= yy < H and not seen[yy][xx] and light(px[xx, yy]) and max(px[xx, yy][:3]) <= 251:
                seen[yy][xx] = True
                q.append((xx, yy))
    top = [min((y for y in range(H) if seen[y][x]), default=H) for x in range(W)]
    cols = [x for x in range(W) if top[x] < H]
    bx0, bx1 = min(cols) - 4, max(cols) + 4          # книга — только в этих столбцах
    top = [min(top[max(0, x - 8):x + 9]) if bx0 <= x <= bx1 else H for x in range(W)]
    # под крестом страниц не видно, и край книги там не нашёлся — провести его между соседними столбцами,
    # иначе в середине книги остаётся дыра вместо стыка страниц
    # столбцы, где над найденным краем стоит крест: настоящий край там выше, под крестом его не видно
    for x in range(bx0, bx1 + 1):
        if top[x] < H and any(is_cross(px[x, y]) for y in range(0, top[x], 3)):
            top[x] = H
    known = [x for x in range(bx0, bx1 + 1) if top[x] < H]
    for x in range(bx0, bx1 + 1):
        if top[x] == H:
            lft = max((k for k in known if k < x), default=None)
            rgt = min((k for k in known if k > x), default=None)
            if lft is not None and rgt is not None:
                top[x] = round(top[lft] + (top[rgt] - top[lft]) * (x - lft) / (rgt - lft))
            elif lft is not None or rgt is not None:
                top[x] = top[lft if lft is not None else rgt]

    kind = [[0] * W for _ in range(H)]   # 0 пусто, 1 глобус, 2 книга, 3 крест
    for y in range(H):
        for x in range(W):
            c = px[x, y]
            if c[3] < 24:
                continue
            if is_cross(c):
                kind[y][x] = 3
            elif max(c[:3]) < 110 and y > H * 0.45:
                kind[y][x] = 2                      # обложка и тени
            elif y >= top[x] and not is_sea(c):
                kind[y][x] = 2
            else:
                kind[y][x] = 1

    # сглаженный край креста (полупрозрачные оранжеватые точки вокруг него) — тоже крест,
    # иначе он остаётся рыжей каймой на глобусе и книге
    for _ in range(3):
        add = []
        for y in range(1, H - 1):
            for x in range(1, W - 1):
                if kind[y][x] in (1, 2) and px[x, y][0] - px[x, y][2] > 25 and \
                        (kind[y][x - 1] == 3 or kind[y][x + 1] == 3 or kind[y - 1][x] == 3 or kind[y + 1][x] == 3):
                    add.append((x, y))
        for x, y in add:
            kind[y][x] = 3

    # глобус: эллипс по голубым точкам
    sea = [(x, y) for y in range(H) for x in range(W) if is_sea(px[x, y])]
    xs = [p[0] for p in sea]
    ys = [p[1] for p in sea]
    x0, x1, y0 = min(xs), max(xs), min(ys)
    cx, a = (x0 + x1) / 2.0, (x1 - x0) / 2.0 + 2
    edge = sorted(y for x, y in sea if x <= x0 + 3 or x >= x1 - 3)
    cy = edge[len(edge) // 2]
    b = cy - y0 + 2
    n = len(sea)
    sea_rgb = tuple(sum(px[x, y][i] for x, y in sea) // n for i in range(3))

    def layer(which):
        out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        o = out.load()
        hole = []
        for y in range(H):
            for x in range(W):
                k = kind[y][x]
                if k == which:
                    o[x, y] = px[x, y]
                elif which == 1 and k in (2, 3) and ((x - cx) / a) ** 2 + ((y - cy) / b) ** 2 <= 1:
                    o[x, y] = sea_rgb + (255,) if k == 2 or y >= top[x] else (0, 0, 0, 0)
                    if k == 3 and y < top[x]:
                        hole.append((x, y))
                elif which == 2 and k == 3 and y >= top[x]:
                    hole.append((x, y))
        # под крестом: дорисовать от соседей (по кругу, пока дыра не закроется)
        hs = set(hole)
        while hs:
            done = []
            for x, y in hs:
                acc = [0, 0, 0]
                m = 0
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < W and 0 <= yy < H and (xx, yy) not in hs and o[xx, yy][3] > 200:
                        for i in range(3):
                            acc[i] += o[xx, yy][i]
                        m += 1
                if m:
                    done.append((x, y, tuple(v // m for v in acc) + (255,)))
            if not done:
                break
            for x, y, c in done:
                o[x, y] = c
                hs.discard((x, y))
        if which == 2 and hole:   # стык страниц — мягкая тень по середине того места, что было под крестом
            xs = [x for x, y in hole]
            mid = (min(xs) + max(xs)) // 2
            for x, y in hole:
                d = abs(x - mid)
                if d <= 5:
                    k = 0.72 + 0.28 * d / 5.0
                    pr, pg, pb, pa = o[x, y]
                    o[x, y] = (int(pr * k), int(pg * k), int(pb * k), pa)
        return out

    return [layer(k) for k in (1, 2, 3)]


def sized(layer_im, w, h):
    """Уменьшить слой с правильными краями (цвет умножен на прозрачность)."""
    pm = Image.new('RGBA', layer_im.size)
    pm.paste(layer_im)
    r, g, b, a = layer_im.split()
    from PIL import ImageChops
    r, g, b = [ImageChops.multiply(c, a) for c in (r, g, b)]
    small = Image.merge('RGBA', (r, g, b, a)).resize((w, h), Image.LANCZOS)
    px = small.load()
    rgb, al = [], []
    for y in range(h):
        for x in range(w):
            pr, pg, pb, pa = px[x, y]
            if pa:
                pr, pg, pb = [min(255, v * 255 // pa) for v in (pr, pg, pb)]
            rgb.append(rgb565(pr, pg, pb))
            al.append(pa)
    return rgb, al, small


im = Image.open(SRC).convert('RGBA')
o = ['// Создаёт tools/gen_logo.py — вручную не править.', '#pragma once', '#include <Arduino.h>', '']


def emit16(name, data):
    o.append('static const uint16_t %s[] = {' % name)
    for i in range(0, len(data), 16):
        o.append('  ' + ' '.join('0x%04X,' % v for v in data[i:i + 16]))
    o.append('};')


def emit8(name, data):
    o.append('static const uint8_t %s[] = {' % name)
    for i in range(0, len(data), 24):
        o.append('  ' + ' '.join('%d,' % v for v in data[i:i + 24]))
    o.append('};')


for name, h in (('LOGO_BIG', BIG_H), ('LOGO_SM', SM_H)):
    w, h, data = flat(im, h)
    o.append('#define %s_W %d' % (name, w))
    o.append('#define %s_H %d' % (name, h))
    emit16(name, data)
    o.append('')
w = round(im.width * BIG_H / im.height)
os.makedirs(PREVIEW, exist_ok=True)
check = Image.new('RGBA', (w * 4 + 50, BIG_H + 20), BG + (255,))
for i, (name, lay) in enumerate(zip(('GLOBE', 'BOOK', 'CROSS'), layers(im))):
    rgb, al, small = sized(lay, w, BIG_H)
    o.append('// слой заставки: цвет и прозрачность, размер как у LOGO_BIG')
    emit16('LOGO_%s' % name, rgb)
    emit8('LOGO_%s_A' % name, al)
    o.append('')
    check.alpha_composite(small, (10 + i * (w + 10), 10))
    check.alpha_composite(small, (10 + 3 * (w + 10), 10))
check.convert('RGB').save(os.path.join(PREVIEW, 'logo-sloi.png'))
open(OUT, 'w', encoding='utf-8').write('\n'.join(o))
print('ok', os.path.normpath(OUT))
