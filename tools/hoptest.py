#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверка перехода на другой канал: команды передатчику по секундам, а по каждому приёмнику — посекундно канал,
кадры, потерянное и провалы звука.  python3 tools/hoptest.py <порт передатчика> <секунд> <порт приёмника>… -- 5:c1 14:h3"""
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


args = sys.argv[1:]
cut = args.index('--')
tx, secs, rxs = op(args[0]), float(args[1]), [op(p) for p in args[2:cut]]
plan = sorted((float(a.split(':', 1)[0]), a.split(':', 1)[1]) for a in args[cut + 1:])
names = {tx: 'ПРД'}
for i, r in enumerate(rxs):
    names[r] = 'ПРМ-' + 'АБВГ'[i]
buf, t0 = {f: b'' for f in names}, time.time()
tot = {r: [0.0, 0, 0, 0, 0] for r in rxs}   # провалы мс, потеряно, восстановлено, секунд без сигнала, секунд
while time.time() - t0 < secs:
    t = time.time() - t0
    if plan and t >= plan[0][0]:
        c = plan.pop(0)[1]
        os.write(tx, (c + '\n').encode())
        print('%5.1f  >>> ПРД: %s' % (t, c), flush=True)
    for fd in select.select(list(names), [], [], 0.2)[0]:
        try:
            buf[fd] += os.read(fd, 4096)
        except (BlockingIOError, OSError):
            continue
        while b'\n' in buf[fd]:
            line, buf[fd] = buf[fd].split(b'\n', 1)
            s = line.decode('utf-8', 'replace').rstrip()
            t = time.time() - t0
            if fd == tx:
                if re.search(r'ЗАТОР|канал змінено|вільнішого каналу|канал при заторі', s):
                    print('%5.1f  ПРД: %s' % (t, s[s.find('ЗАТОР') if 'ЗАТОР' in s else 0:][:170]), flush=True)
                continue
            m = re.search(r'ПРИЙМАЧ к=(\d+) .*кадрів (\d+), відновлено (\d+), втрачено (\d+), порожньо ([\d.]+) мс', s)
            if m:
                e = float(m.group(5))
                tot[fd][0] += e; tot[fd][1] += int(m.group(4)); tot[fd][2] += int(m.group(3)); tot[fd][4] += 1
                print('%5.1f  %s: канал %2s, кадров %3s, восстановлено %s, потеряно %s, провал %5.1f мс%s' % (
                    t, names[fd], m.group(1), m.group(2), m.group(3), m.group(4), e, '   <<<' if e > 0 or int(m.group(4)) else ''), flush=True)
            elif 'сигналу немає' in s:
                k = re.search(r'ПРИЙМАЧ к=(\d+)', s)
                tot[fd][3] += 1; tot[fd][4] += 1
                print('%5.1f  %s: канал %2s, СИГНАЛА НЕТ   <<<' % (t, names[fd], k.group(1) if k else '?'), flush=True)
print()
for r in rxs:
    v = tot[r]
    print('%s: за %d с — провалов звука %.1f мс, потеряно кадров %d, восстановлено %d, секунд без сигнала %d' % (names[r], v[4], v[0], v[1], v[2], v[3]))
for f in names:
    os.close(f)
