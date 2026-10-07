#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Наблюдение за набором с двух сторон сразу: порт передатчика и порт приёмника (тот, что печатает отчёты).
   python3 tools/watch2.py <порт передатчика> <порт приёмника> <секунд> [время:команда передатчику …]
Каждую секунду — одна строка: у передатчика ожидание свободного эфира (среднее/наибольшее), отказы, чужие передачи
в канале (если включено A1); у приёмника — провалы звука («порожньо»), запас (наименьший за секунду и цель), потери.
В конце — итог по отрезкам между командами."""
import os, sys, time, termios, select, fcntl, struct, re


def op(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))
    return fd


tx, rx, secs = op(sys.argv[1]), op(sys.argv[2]), float(sys.argv[3])
plan = sorted((float(a.split(':', 1)[0]), a.split(':', 1)[1]) for a in sys.argv[4:])
quiet = os.environ.get('QUIET')          # печатать только плохие секунды и итоги
buf = {tx: b'', rx: b''}
cur = {}                                  # последняя строка приёмника
def newseg(name):
    return {'name': name, 'n': 0, 'empty': 0.0, 'bad': 0, 'air': [], 'airmax': 0, 'ref': 0, 'foreign': [], 'slow': [], 'tgt': [], 'lost': 0, 'rec': 0,
            'thin': 0, 'skip': 0, 'qmax': 0, 'copies': 0, 'ppm': []}


segs, seg = [], newseg('начало')
t0 = time.time()
print('  с   | до ефіру сер./макс. мс, відмов | чужі: кадрів, зайнято %, повільних %, найдовший мс | приймач: порожньо мс, запас мін./ціль мс, відновл./втрач., темп', flush=True)
alive = -99.0   # приёмник (с 2.9) печатает отчёты, только пока с порта что-то приходит: раз в 20 с шлём пустую строку
while time.time() - t0 < secs:
    t = time.time() - t0
    if t - alive >= 20:
        alive = t
        os.write(rx, b'\n')
    if plan and t >= plan[0][0]:
        c = plan.pop(0)[1]
        os.write(tx, (c + '\n').encode())
        print('%5.0f  >>> %s' % (t, c), flush=True)
        segs.append(seg)
        seg = newseg(c)
    for fd in select.select([tx, rx], [], [], 0.2)[0]:
        try:
            buf[fd] += os.read(fd, 4096)
        except BlockingIOError:
            continue
        while b'\n' in buf[fd]:
            line, buf[fd] = buf[fd].split(b'\n', 1)
            s = line.decode('utf-8', 'replace')
            if fd == rx:
                m = re.search(r'кадрів (\d+), відновлено (\d+), втрачено (\d+), порожньо ([\d.]+) мс.*запас ([\d.]+) мс \(([\d.]+)…([\d.]+), ціль ([\d.]+).*темп ([+-]?\d+) ppm', s)
                if m:
                    cur = {'rec': int(m.group(2)), 'lost': int(m.group(3)), 'empty': float(m.group(4)), 'min': float(m.group(6)), 'tgt': float(m.group(8)), 'ppm': int(m.group(9))}
                    k = re.search(r'з копій (\d+)', s)
                    cur['copies'] = int(k.group(1)) if k else 0
                elif 'сигналу немає' in s:
                    cur = {'nosig': True}
                continue
            m = re.search(r'відмов (\d+), не пішло в ефір (\d+), перезапусків радіо (\d+), до ефіру сер\. (\d+) макс\. (\d+)', s)
            if not m:
                continue
            ref, avg, mx = int(m.group(1)), int(m.group(4)) / 1000.0, int(m.group(5)) / 1000.0
            f = re.search(r'чужі: кадрів (\d+), зайнято ([\d.]+) % часу \(повільних 1–2 Мбіт/с ([\d.]+) %\), найдовший (\d+)', s)
            ft = '%4s кадрів, %5s %%, %5s %%, %5.1f' % (f.group(1), f.group(2), f.group(3), int(f.group(4)) / 1000.0) if f else '        —'
            k = re.search(r'черга макс\. (\d+), проріджено (\d+), пропущено (\d+)', s)
            qmax, thin, skip = (int(k.group(1)), int(k.group(2)), int(k.group(3))) if k else (0, 0, 0)
            ft += ' | черга %2d, прорідж. %3d, пропущ. %3d' % (qmax, thin, skip)
            seg['thin'] += thin; seg['skip'] += skip; seg['qmax'] = max(seg['qmax'], qmax)
            r = cur
            cur = {}
            if r.get('nosig'):
                rt = 'сигналу немає'
            elif r:
                rt = '%5.1f, %4.1f / %2.0f, %d / %d, %+d, з копій %d' % (r['empty'], r['min'], r['tgt'], r['rec'], r['lost'], r['ppm'], r.get('copies', 0))
            else:
                rt = '—'
            bad = ref or (r.get('empty', 0) > 0) or r.get('lost', 0)
            if not quiet or bad:
                print('%5.0f | %5.1f / %5.1f, %3d | %s | %s%s' % (t, avg, mx, ref, ft, rt, '   <<<' if bad else ''), flush=True)
            seg['n'] += 1
            seg['air'].append(avg); seg['airmax'] = max(seg['airmax'], mx); seg['ref'] += ref
            if f:
                seg['foreign'].append(float(f.group(2))); seg['slow'].append(float(f.group(3)))
            if r and not r.get('nosig'):
                seg['empty'] += r['empty']; seg['bad'] += 1 if r['empty'] > 0 else 0
                seg['tgt'].append(r['tgt']); seg['lost'] += r['lost']; seg['rec'] += r['rec']; seg['copies'] += r.get('copies', 0); seg['ppm'].append(abs(r['ppm']))
segs.append(seg)
print('\nИтог по отрезкам:')
for g in segs:
    if not g['n']:
        continue
    av = lambda v: sum(v) / len(v) if v else float('nan')
    print('  после «%s»: %d с | до эфира сред. %.1f мс, наиб. %.1f мс, очередь до %d, отказов %d, прорежено %d, пропущено %d | чужие: занято %.1f %% | '
          'приёмник: провалов %.1f мс в %d с из %d, цель запаса сред. %.0f мс, из копий %d, восстановлено %d, потеряно %d, темп сред. ±%.0f, наиб. %.0f ppm'
          % (g['name'], g['n'], av(g['air']), g['airmax'], g['qmax'], g['ref'], g['thin'], g['skip'], av(g['foreign']), g['empty'], g['bad'], g['n'], av(g['tgt']),
             g['copies'], g['rec'], g['lost'], av(g['ppm']), max(g['ppm']) if g['ppm'] else 0))
os.close(tx); os.close(rx)
