#!/bin/sh
# Экраны приёмника на компьютере: анимация out/pryimach.gif и лист ключевых кадров out/kadry.png.
set -e
cd "$(dirname "$0")"
U8=${U8G2_DIR:-$HOME/Documents/Arduino/libraries/U8g2/src/clib}
LIB=build/libu8g2.a
[ -f "$LIB" ] || LIB=$HOME/Documents/hearing-amp/tools/sim/build/libu8g2.a   # уже собрана для усилителя
if [ ! -f "$LIB" ]; then
  echo "собираю U8g2 (один раз, с минуту)…"
  LIB=build/libu8g2.a
  for f in "$U8"/*.c; do
    cc -O1 -w -I"$U8" -c "$f" -o "build/$(basename "$f" .c).o" &
    while [ "$(jobs -r | wc -l)" -ge 6 ]; do sleep 0.2; done
  done
  wait
  ar rcs "$LIB" build/*.o && rm build/*.o
fi
rm -f out/f*.pgm
c++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-function -I"$U8" sim.cpp "$LIB" -o build/sim
./build/sim
python3 - <<'PY'
# -*- coding: utf-8 -*-
from PIL import Image, ImageDraw, ImageFont
import io, os, glob
rows = [l.rstrip('\n').split('\t') for l in io.open('out/frames.tsv', encoding='utf-8')]
K = 4
def big(p):
    im = Image.open(p).convert('L').resize((128 * K, 64 * K), Image.NEAREST)
    out = Image.new('RGB', im.size, (6, 10, 14))
    out.paste(Image.new('RGB', im.size, (150, 215, 255)), mask=im)
    return out
frames = [big('out/f%04d.pgm' % int(r[0])) for r in rows]
frames[0].save('out/pryimach.gif', save_all=True, append_images=frames[1:], duration=40, loop=0, optimize=False)
try:
    font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 15)
except Exception:
    font = ImageFont.load_default()
keys = [(r, f) for r, f in zip(rows, frames) if r[3] == '1']
cols = 4
W, H, PAD, CAP = 128 * 3, 64 * 3, 10, 22
sheet = Image.new('RGB', (cols * (W + PAD) + PAD, -(-len(keys) // cols) * (H + CAP + PAD) + PAD), (38, 42, 48))
d = ImageDraw.Draw(sheet)
for i, (r, f) in enumerate(keys):
    x, y = PAD + (i % cols) * (W + PAD), PAD + (i // cols) * (H + CAP + PAD)
    sheet.paste(f.resize((W, H), Image.NEAREST), (x, y + CAP))
    d.text((x, y + 2), u'%s · %.1f с' % (r[1], int(r[2]) / 1000.0), fill=(225, 225, 225), font=font)
sheet.save('out/kadry.png')
for p in glob.glob('out/f*.pgm'):
    os.remove(p)
print('готово: out/pryimach.gif (%d кадров, %.0f с), out/kadry.png (%d кадров)' % (len(frames), len(frames) * 0.04, len(keys)))
PY
