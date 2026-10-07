#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверка языка на трёх платах: команды по секундам (t: передатчику, a/b: приёмникам), печать ответов на L и ?.
   python3 tools/langtest.py <передатчик> <приёмник А> <приёмник Б> <секунд> 3:t:L1 6:a:L …"""
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
buf, t0, rev = {k: b'' for k in fds}, time.time(), {v: k for k, v in fds.items()}
bad = {'a': 0.0, 'b': 0.0}
while time.time() - t0 < secs:
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
            if re.search(r'мова написів|ім.я:|версія \d|перевірочний звук:|зараз мелодія', s):
                print('%5.1f  %s: %s' % (time.time() - t0, names[who], s.strip()[:170]), flush=True)
            m = re.search(r'порожньо ([\d.]+) мс', s)
            if m and who in bad:
                bad[who] += float(m.group(1))
            if 'сигналу немає' in s and who in bad:
                bad[who] += 1000
print('провалов звука за проверку: ПРМ-А %.1f мс, ПРМ-Б %.1f мс' % (bad['a'], bad['b']))
for fd in fds.values():
    os.close(fd)
