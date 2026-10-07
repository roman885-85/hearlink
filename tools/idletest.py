#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Замер вывода кадра экрана передатчика в покое, по страницам (прошивка 2.28+).
   python3 tools/idletest.py <порт> [команда перед замером …]
На каждой странице 6 секунд: сколько порций кадра готовились дольше шага развёртки и сколько раз прерывание шло вдогонку."""
import subprocess, sys, re, os
port, pre = sys.argv[1], sys.argv[2:]
plan, t = [], 1.0
for c in pre:
    plan.append('%.1f:%s' % (t, c)); t += 1.0
pages = [(5, 'Довідка (ничего не движется)'), (0, 'Головна'), (2, 'Звук (спектр)'), (1, 'Приймачі')]
for pg, _ in pages:
    plan.append('%.1f:U%d' % (t, pg)); t += 2.0
    plan.append('%.1f:u' % t); t += 6.0
    plan.append('%.1f:u' % t); t += 0.6
here = os.path.dirname(os.path.abspath(__file__))
out = subprocess.run(['python3', os.path.join(here, 'txlog.py'), port, '%.0f' % (t + 1)] + plan, capture_output=True).stdout.decode('utf-8', 'replace')
rows, loads, mx, tr = [], [], [], []
for l in out.splitlines():
    m = re.search(r'у спокої з минулого звіту: довших за крок (\d+), навздогін (\d+)', l)
    if m: rows.append((int(m.group(1)), int(m.group(2))))
    m = re.search(r'забирає (\d+) % другого ядра .*сер\. (\d+) мкс', l)
    if m: loads.append((int(m.group(1)), int(m.group(2))))
    m = re.search(r'не встигло до строку (\d+) \(найбільше запізнення (\d+) мкс\), із них помітних (\d+); крок розгортки (\d+) мкс; переривання запізнювалось до (\d+)', l)
    if m: tr.append(tuple(int(x) for x in m.groups()))
    m = re.search(r'найдовша порція (\d+) мкс', l)
    if m: mx.append(int(m.group(1)))
for i, (pg, name) in enumerate(pages):
    k = 2 * i + 1
    if k < len(rows):
        print('  %-30s за 6 с: порций дольше шага %3d, вдогонку %3d | порция сред. %3d мкс, наиб. %3d (%d %% ядра)' % (name, rows[k][0], rows[k][1], loads[k][1] if k < len(loads) else 0, mx[k] if k < len(mx) else 0, loads[k][0] if k < len(loads) else 0))
