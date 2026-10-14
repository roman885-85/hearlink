#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Расчёт окна (прототипа) восьмиполосного набора фильтров для подполосного кодека дальнего режима (hearlink/subc.h).
   python3 tools/subc_design.py            — посчитать, проверить и записать hearlink/subc_tab.h
Набор — косинусно-модулированный (как в MPEG-1 и SBC): M = 8 полос по 2 кГц при 32 кГц, длина окна L = 80
(задержка L − 1 = 79 отсчётов = 2,47 мс). Окно — «синк под окном Кайзера»; частота среза подбирается так, чтобы
свёртка окна с собой была фильтром «каждый 2M-й отсчёт — ноль» (способ Лина и Вайдьянатхана): тогда разбор и сборка
без квантования возвращают звук с ошибкой ниже −70 дБ. Подавление вне полосы (от него зависят «чужие» составляющие,
когда соседние полосы квантуются по-разному) задаётся числом beta."""
import sys
import numpy as np
from scipy.signal import firwin
from scipy.optimize import minimize_scalar

M, L = 8, 80

def proto(wc, beta):
    return firwin(L, wc / np.pi, window=('kaiser', beta))

def cost(wc, beta):
    p = proto(wc, beta)
    g = np.convolve(p, p)
    c = L - 1
    idx = [c + 2 * M * k for k in range(-(L // (2 * M)) - 1, L // (2 * M) + 2) if k != 0 and 0 <= c + 2 * M * k < len(g)]
    return np.max(np.abs(g[idx]))

def bank(p):
    n = np.arange(L)
    k = np.arange(M)[:, None]
    ph = (2 * k + 1) * np.pi / (2 * M) * (n - (L - 1) / 2)
    th = ((-1) ** k) * np.pi / 4
    return 2 * p * np.cos(ph + th), 2 * p * np.cos(ph - th)

def run(p, x):
    h, f = bank(p)
    nb = len(x) // M
    buf = np.zeros(L)
    s = np.zeros((nb, M))
    for m in range(nb):
        buf = np.concatenate((x[m * M:(m + 1) * M][::-1], buf[:-M]))   # новейший отсчёт — первый
        s[m] = h @ buf
    y = np.zeros(nb * M + L)
    for m in range(nb):
        y[m * M:m * M + L] += s[m] @ f
    return s, y

best = None
for beta in (7.5, 8.0, 8.25, 8.5, 8.75, 9.0, 9.5):
    r = minimize_scalar(lambda w: cost(w, beta), bounds=(np.pi / (2 * M) * 0.7, np.pi / (2 * M) * 1.6), method='bounded', options={'xatol': 1e-10})
    p = proto(r.x, beta)
    rng = np.random.default_rng(1)
    x = rng.standard_normal(M * 400)
    s, y = run(p, x)
    # усиление и ошибка восстановления (задержка L − 1 − (M − 1): последний отсчёт блока лежит в начале буфера)
    d = L - M
    a, b = x[:len(x) - 200], y[d:d + len(x) - 200]
    g = np.dot(a, b) / np.dot(a, a)
    err = 10 * np.log10(np.sum((b / g - a) ** 2) / np.sum(a ** 2))
    W = np.abs(np.fft.rfft(p, 8192))
    W /= W[0]
    # Не гасятся только просачивания через одну полосу и дальше: это частоты окна выше 1,5·π/M (соседние полосы
    # гасят наложения друг друга по построению — их остаток и есть «ошибка восстановления»)
    stop = 20 * np.log10(np.max(W[int(4096 * 1.5 / M):]))
    print('beta %.1f: срез %.5f·π/(2M), усиление %.5f, ошибка восстановления %.1f дБ, подавление вне полосы %.1f дБ' % (beta, r.x / (np.pi / (2 * M)), g, err, stop))
    if best is None or err < best[4]:   # главное — точность восстановления; подавление у всех вариантов с запасом
        best = (beta, r.x, g, stop, err, p)

beta, wc, g, stop, err, p = best
print('выбрано: beta %.1f, подавление %.1f дБ, ошибка %.1f дБ' % (beta, stop, err))
# Масштаб: разбор — так, чтобы синус полной шкалы давал в своей полосе отсчёты около полной шкалы; сборка — обратный.
h, f = bank(p)
n = np.arange(4000)
x = 32767 * np.sin(2 * np.pi * 3000 / 32000 * n)
s, y = run(p, x)
pk = np.max(np.abs(s[20:, 1]))
ka = 32767 / pk
print('пик полосы на синусе полной шкалы до масштаба: %.1f; множитель разбора %.5f, сборки %.5f' % (pk, ka, 1 / (g * ka)))
with open('hearlink/subc_tab.h', 'w', encoding='utf-8') as o:
    o.write('// Окно набора фильтров подполосного кодека (hearlink/subc.h). Создано tools/subc_design.py — руками не править.\n')
    o.write('// M = %d полос, L = %d; окно Кайзера beta %.1f, срез %.6f рад; подавление вне полосы %.1f дБ; разбор и сборка без\n' % (M, L, beta, wc, stop))
    o.write('// квантования возвращают звук с ошибкой %.1f дБ. SUBC_KA — множитель разбора, SUBC_KS — сборки.\n' % err)
    o.write('#pragma once\n#define SUBC_M %d\n#define SUBC_L %d\n' % (M, L))
    o.write('#define SUBC_KA %.8ff\n#define SUBC_KS %.8ff\n' % (ka, 1 / (g * ka)))
    o.write('static const float SUBC_P[SUBC_L] = {\n')
    for i in range(0, L, 5):
        o.write('  ' + ', '.join('%.9ef' % v for v in p[i:i + 5]) + ',\n')
    o.write('};\n')
print('записано hearlink/subc_tab.h')
