#!/bin/sh
# Собирает меню передатчика на компьютере и кладёт картинки страниц в tools/sim/out/.
set -e
cd "$(dirname "$0")/../.."
mkdir -p tools/sim/out build
rm -f tools/sim/out/*
c++ -std=c++17 -O1 -Wall -Wno-unused-function -Wno-unused-variable -Itools/sim/stub -Ihearlink tools/sim/sim.cpp hearlink/m2gfx.cpp -o build/sim
./build/sim
python3 - <<'PY'
# -*- coding: utf-8 -*-
import glob, os
from PIL import Image
names = sorted(glob.glob('tools/sim/out/*.rgb'))
for f in names:
    Image.frombytes('RGB', (480, 480), open(f, 'rb').read()).save(f[:-4] + '.png')
    os.remove(f)
# общий лист: по четыре в ряд
ims = [Image.open(f[:-4] + '.png') for f in names]
cols = 4
rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * 490 + 10, rows * 490 + 10), (60, 60, 60))
for i, im in enumerate(ims):
    sheet.paste(im, (10 + (i % cols) * 490, 10 + (i // cols) * 490))
sheet.save('tools/sim/out/_vse.png')
print(len(ims), 'картинок')
PY
