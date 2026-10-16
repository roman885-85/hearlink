#!/bin/sh
# Цветной экран приёмника (IPS 1,9″, 320×170) на компьютере: анимация out/pryimach-kolir.gif, лист ключевых кадров
# out/kadry.png и сколько точек за кадр уходило бы на экран (по видам экрана).
set -e
cd "$(dirname "$0")"
mkdir -p out build
rm -f out/f*.rgb
VER=$(sed -n 's/^#define FW_VERSION "\(.*\)".*/\1/p' ../../hearlink/config.h)
c++ -std=c++17 -O2 -Wall -Wno-unused-function -Wno-unused-variable -DSIM_VERSION="\"$VER\"" -I../sim/stub -I../../hearlink sim.cpp ../../hearlink/m2gfx.cpp -o build/sim
./build/sim
python3 - <<'PY'
# -*- coding: utf-8 -*-
from PIL import Image, ImageDraw, ImageFont
import io, os, glob
rows = [l.rstrip('\n').split('\t') for l in io.open('out/frames.tsv', encoding='utf-8')]
W, H = 320, 170
def load(n):
    return Image.frombytes('RGB', (W, H), open('out/f%04d.rgb' % int(n), 'rb').read())
frames = [load(r[0]) for r in rows]
big = [f.resize((W * 2, H * 2), Image.NEAREST) for f in frames]
big[0].save('out/pryimach-kolir.gif', save_all=True, append_images=big[1:], duration=40, loop=0, optimize=False)
try:
    font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 15)
except Exception:
    font = ImageFont.load_default()
keys = [(r, f) for r, f in zip(rows, frames) if r[3] == '1']
cols, PAD, CAP, K = 3, 12, 22, 2
sheet = Image.new('RGB', (cols * (W * K + PAD) + PAD, -(-len(keys) // cols) * (H * K + CAP + PAD) + PAD), (38, 42, 48))
d = ImageDraw.Draw(sheet)
for i, (r, f) in enumerate(keys):
    x, y = PAD + (i % cols) * (W * K + PAD), PAD + (i // cols) * (H * K + CAP + PAD)
    sheet.paste(f.resize((W * K, H * K), Image.NEAREST), (x, y + CAP))
    d.text((x, y + 2), u'%s · %.1f с' % (r[1], int(r[2]) / 1000.0), fill=(225, 225, 225), font=font)
    f.save('out/k%02d.png' % i)
sheet.save('out/kadry.png')
# точки на экран: по видам экрана (без первого кадра вида — там переход)
from collections import defaultdict
st = defaultdict(list)
for r in rows:
    st[r[1]].append(int(r[4]))
print('точок на екран за кадр (середнє / найбільше; повний кадр — 54400):')
for k, v in st.items():
    v2 = v[8:] if len(v) > 10 else v
    print('  %-22s %6.0f / %6d' % (k, sum(v2) / len(v2), max(v)))
# лист для описания (docs/img/rx-color-screens.png): по кадру на экран, подписи — по-украински и по-английски
DOC = [(u'заставка', 3440, u'заставка / splash'), (u'головний: спектр', None, u'головний: спектр / main: spectrum'),
       (u'меню', 1500, u'меню / menu'), (u'про пристрій', None, u'про пристрій / about'), (u"зв'язок", None, u"зв'язок / link"),
       (u'тиша', None, u'тиша / mute'), (u'пошук', None, u'пошук / searching'), (u'немає сигналу', None, u'немає сигналу / no signal'),
       (u'не підключено', None, u'не підключено / not paired')]
def pick(scene, t):
    rs = [(r, f) for r, f in zip(rows, frames) if r[1] == scene]
    if not rs:
        return None
    if t is None:
        return rs[-1][1]
    return min(rs, key=lambda q: abs(int(q[0][2]) - t))[1]
K2, PAD2, CAP2 = 3, 14, 26
sheet2 = Image.new('RGB', (3 * (W * K2 // 2 + PAD2) + PAD2, 3 * (H * K2 // 2 + CAP2 + PAD2) + PAD2), (248, 247, 243))
d2 = ImageDraw.Draw(sheet2)
try:
    font2 = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 16)
except Exception:
    font2 = ImageFont.load_default()
for i, (sc, t, cap) in enumerate(DOC):
    f = pick(sc, t)
    if f is None:
        continue
    x, y = PAD2 + (i % 3) * (W * K2 // 2 + PAD2), PAD2 + (i // 3) * (H * K2 // 2 + CAP2 + PAD2)
    sheet2.paste(f.resize((W * K2 // 2, H * K2 // 2), Image.LANCZOS), (x, y + CAP2))
    d2.text((x, y + 4), cap, fill=(30, 30, 30), font=font2)
sheet2.save('out/rx-color-screens.png')
for p in glob.glob('out/f*.rgb'):
    os.remove(p)
print('готово: out/pryimach-kolir.gif (%d кадрів), out/kadry.png (%d ключових)' % (len(frames), len(keys)))
PY
