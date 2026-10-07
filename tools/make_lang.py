#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Делает hearlink/lang_en.h из tools/lang_en.py и проверяет, что у каждой украинской надписи в исходниках
интерфейса есть перевод (а в словаре нет надписей, которых в исходниках уже нет).   Запуск: python3 tools/make_lang.py"""
import io, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from lang_en import PAIRS, KEEP
SRC = [os.path.join(HERE, '..', 'hearlink', f) for f in ('txui.h', 'fmui.h', 'ui_tx.h', 'ui_rx.h', 'rxscreens.h', 'rxlink.h')]
lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
cyr = re.compile(u'[А-Яа-яІіЇїЄєҐґ]')
found = {}
for f in SRC:
    for ln, line in enumerate(io.open(f, encoding='utf-8'), 1):
        code, i, ins = '', 0, False
        while i < len(line):          # отрезать комментарий
            c = line[i]
            if c == '"' and (i == 0 or line[i - 1] != '\\'):
                ins = not ins
            if not ins and line[i:i + 2] == '//':
                break
            code += c
            i += 1
        if 'Serial.print' in code:
            continue
        for m in lit.finditer(code):
            t = m.group(1)
            if cyr.search(t):
                found.setdefault(t, '%s:%d' % (os.path.basename(f), ln))
# надписи, которые в исходниках записаны несколькими кусками подряд (справка): компилятор склеивает их в одну строку,
# значит, и в словаре нужна склеенная
runs = []
for f in SRC:
    cur = []
    for line in io.open(f, encoding='utf-8'):
        code = line.split('//')[0] if '"' not in line.split('//')[0][-3:] else line
        m1 = re.fullmatch(r'\s*"((?:[^"\\]|\\.)*)"\s*', code.rstrip('\n'))
        m2 = re.match(r'\s*"((?:[^"\\]|\\.)*)"\s*[,}]', code)
        if m1:
            cur.append(m1.group(1))
        else:
            if cur and m2:
                cur.append(m2.group(1))
                if cyr.search(''.join(cur)):
                    runs.append(cur)
            cur = []
d = {}
for uk, en in PAIRS:
    if uk in d and d[uk] != en:
        print('дважды с разным переводом:', uk)
    d[uk] = en
for r in runs:
    if all(x in d for x in r):
        d[''.join(r)] = ''.join(d[x] for x in r)
        for x in r:
            found.setdefault(x, 'кусок')
        found[''.join(r)] = 'склеено'
missing = [k for k in found if k not in d and k not in KEEP]
extra = [k for k in d if k not in found]
for k in missing:
    print('НЕТ ПЕРЕВОДА  %-18s %s' % (found[k], k[:90]))
for k in extra:
    print('в исходниках уже нет:', k[:90])
bad = []
for uk, en in d.items():              # знаки подстановки должны совпадать по порядку
    a, b = re.findall(r'%[-+0-9.]*[a-zA-Z%]', uk), re.findall(r'%[-+0-9.]*[a-zA-Z%]', en)
    if a != b:
        bad.append((uk, a, b))
    if uk.count(r'\n') != en.count(r'\n'):
        bad.append((uk, 'переводы строк', ''))
for uk, a, b in bad:
    print('НЕ СХОДЯТСЯ ПОДСТАНОВКИ:', uk[:70], a, b)
out = [u'// Английские надписи. Файл сделан сценарием tools/make_lang.py из tools/lang_en.py — руками не править.',
       u'#pragma once', u'static const char *const LANG_EN[][2] = {']
for uk, en in d.items():
    out.append(u'  { "%s", "%s" },' % (uk, en))
out += [u'};', u'#define LANG_EN_N (sizeof(LANG_EN) / sizeof(LANG_EN[0]))', u'']
io.open(os.path.join(HERE, '..', 'hearlink', 'lang_en.h'), 'w', encoding='utf-8').write(u'\n'.join(out))
print('надписей в исходниках: %d, в словаре: %d, без перевода: %d, лишних: %d, с несходящимися подстановками: %d'
      % (len(found), len(d), len(missing), len(extra), len(bad)))
sys.exit(1 if missing or bad else 0)
