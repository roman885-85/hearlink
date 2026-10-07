#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сценарий на трёх портах: передатчик (t) и два приёмника (a, b). Команды по секундам; печатаются перемены состояния.
   python3 tools/trio.py <порт передатчика> <порт приёмника А> <порт приёмника Б> <секунд> 5:t:J1 8:a:K …"""
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


fds = {'t': op(sys.argv[1]), 'a': op(sys.argv[2]), 'b': op(sys.argv[3])}
names = {'t': 'ПРД', 'a': 'ПРМ-А', 'b': 'ПРМ-Б'}
secs = float(sys.argv[4])
plan = sorted((float(x.split(':', 2)[0]), x.split(':', 2)[1], x.split(':', 2)[2]) for x in sys.argv[5:])
buf, last, t0 = {k: b'' for k in fds}, {}, time.time()
rev = {v: k for k, v in fds.items()}
alive = -99.0   # приёмники печатают отчёты, только пока с порта что-то приходит
while time.time() - t0 < secs:
    if time.time() - t0 - alive >= 20:
        alive = time.time() - t0
        for k in fds:
            if k != 't':
                os.write(fds[k], b'\n')
    t = time.time() - t0
    if plan and t >= plan[0][0]:
        _, who, cmd = plan.pop(0)
        os.write(fds[who], (cmd + '\n').encode())
        print('%5.1f  >>> %s: %s' % (t, names[who], cmd), flush=True)
    for fd in select.select(list(fds.values()), [], [], 0.2)[0]:
        who = rev[fd]
        try:
            buf[who] += os.read(fd, 4096)
        except (BlockingIOError, OSError):
            continue
        while b'\n' in buf[who]:
            line, buf[who] = buf[who].split(b'\n', 1)
            s = line.decode('utf-8', 'replace').rstrip()
            t = time.time() - t0
            if who != 't':
                if 'ПРИЙМАЧ к=' in s:
                    f = re.search(r'кадрів (\d+)', s)
                    key = 'не подключён' if 'НЕ ПІДКЛЮЧЕНО' in s else ('сигнала нет' + (' (чужая подпись)' if 'чужим підписом' in s else '')) if 'сигналу немає' in s else 'звук идёт'
                    if last.get(who) != key:
                        last[who] = key
                        print('%5.1f  %s: %s' % (t, names[who], key), flush=True)
                    continue
                m = re.search(r'стан: (.*?), \d+ с; виходів з очікування (\d+); кіл сну (\d+)', s)
                if m:
                    key = m.group(1)
                    if last.get(who + 's') != key:
                        last[who + 's'] = key
                        print('%5.1f  %s: состояние «%s», кругов сна %s, пробуждений %s' % (t, names[who], key, m.group(3), m.group(2)), flush=True)
                    last[who + 'n'] = m.group(3)
                    continue
                if 'виводи виходу' in s or not s.strip():
                    continue
            else:
                if 'ПЕРЕДАВАЧ к=' in s:
                    m = re.search(r'пакетів (\d+), відмов (\d+)', s)
                    key = 'ОТКАЗЫ' if m and int(m.group(2)) else 'передача стоит' if 'ПЕРЕДАЧА СТОЇТЬ' in s else 'передаёт'
                    if last.get('tx') != key:
                        last['tx'] = key
                        print('%5.1f  %s: %s' % (t, names[who], key), flush=True)
                    continue
                m = re.search(r'приймач (\w+) «(.*?)»: (.*?), гучність.*вихід (\S+ ?\S*) \((\S+)\)', s) or re.search(r'приймач (\w+) «(.*?)»: (.*?), гучність', s)
                if m:
                    key = m.group(3) + (' · вихід ' + m.group(5) if m.lastindex and m.lastindex >= 5 and 'не на' not in m.group(3) else '')
                    if last.get('p' + m.group(1)) != key:
                        last['p' + m.group(1)] = key
                        print('%5.1f  %s: в списке «%s» — %s' % (t, names[who], m.group(2), key), flush=True)
                    continue
                if not s.strip():
                    continue
            print('%5.1f  %s: %s' % (t, names[who], s[:190]), flush=True)
for k in ('a', 'b'):
    if k + 'n' in last:
        print('        %s: всего кругов сна %s' % (names[k], last[k + 'n']))
for fd in fds.values():
    os.close(fd)
