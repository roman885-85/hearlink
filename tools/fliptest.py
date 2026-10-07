#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Замер перелистывания вкладок меню передатчика «пальцем» с порта (прошивка 2.19+).
   python3 tools/fliptest.py <порт> [команда перед замером …]
Шесть перелистываний; по каждому — время, самый долгий шаг, кадров в движении и повторённых, самая долгая порция кадра."""
import subprocess, sys, re, os
port = sys.argv[1]
pre = sys.argv[2:]
plan, t = [], 1.0
for c in pre:
    plan.append('%.1f:%s' % (t, c)); t += 1.0
plan.append('%.1f:u' % t); t += 1.0
for x in (120, 200, 280, 360, 40, 200):
    plan.append('%.1f:Ux%d,450,80' % (t, x)); t += 1.8
    plan.append('%.1f:u' % t); t += 1.0
here = os.path.dirname(os.path.abspath(__file__))
out = subprocess.run(['python3', os.path.join(here, 'txlog.py'), port, '%.0f' % (t + 1)] + plan, capture_output=True).stdout.decode('utf-8', 'replace')
flips, fill, load, sl, tr = [], [], [], [], []
for l in out.splitlines():
    m = re.search(r'перегортання сторінки: (\d+) кроків за (\d+) мс, найдовший крок ([\d.]+) мс', l)
    if m: flips.append((int(m.group(2)), float(m.group(3))))
    m = re.search(r'найдовша порція (\d+) мкс.*кадрів у русі (\d+), з них повторених (\d+)', l)
    if m: fill.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
    m = re.search(r'у русі: порцій (\d+), сер\. (\d+) мкс, найдовша (\d+) мкс, довших за крок розгортки (\d+), навздогін (\d+); у спокої з минулого звіту: довших за крок (\d+), навздогін (\d+)', l)
    if m: sl.append(tuple(int(x) for x in m.groups()))
    m = re.search(r'не встигло до строку (\d+) \(найбільше запізнення (\d+) мкс\), із них помітних (\d+); крок розгортки (\d+) мкс; переривання запізнювалось до (\d+)', l)
    if m: tr.append(tuple(int(x) for x in m.groups()))
    m = re.search(r'забирає (\d+) % другого ядра .*сер\. (\d+) мкс', l)
    if m: load.append((int(m.group(1)), int(m.group(2))))
for i in range(1, min(len(flips), len(fill))):
    extra = ''
    if i < len(sl):
        n, avg, mx, over, chase, io, ic = sl[i]
        extra = ' | в движении: порций %d, сред. %d мкс, наиб. %d, дольше шага %d, вдогонку %d | в покое: дольше шага %d, вдогонку %d' % (n, avg, mx, over, chase, io, ic)
    print('  %4d мс, шаг до %5.1f мс, повторов %2d%s' % (flips[i][0], flips[i][1], fill[i][2], extra))
if tr: print('  шаг развёртки: %d мкс' % tr[-1][3])
if load: print('  вывод кадра в среднем: %d %% ядра, %d мкс на порцию' % (sum(a for a, _ in load) // len(load), sum(b for _, b in load) // len(load)))
