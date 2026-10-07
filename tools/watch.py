#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Долгое наблюдение за набором через порт передатчика: в файл пишутся только «плохие» секунды.
   python3 tools/watch.py <порт передатчика> <минут> <файл>
Плохая секунда: отказ отправки, ожидание свободного эфира дольше 15 мс, сбой числа пакетов; у приёмника —
потери больше 2 %, пропавшие кадры; у декодера музыки — срывы. Раз в минуту — строка-сводка."""
import os, sys, time, termios, select, fcntl, struct, re

port, mins, path = sys.argv[1], float(sys.argv[2]), sys.argv[3]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 6))
out = open(path, 'a', encoding='utf-8')


def log(s):
    out.write(time.strftime('%H:%M:%S ') + s + '\n')
    out.flush()


log('--- наблюдение начато, %g мин' % mins)
buf, t0, nextP, nextQ, nextSum = b'', time.time(), 3.0, 7.0, 60.0
sec = bad = 0
airMax = 0
try:
    while time.time() - t0 < mins * 60:
        t = time.time() - t0
        if t >= nextP:
            os.write(fd, b'P\n'); nextP += 4
        if t >= nextQ:
            os.write(fd, b'?\n'); nextQ += 30
        if t >= nextSum:
            log('сводка за минуту: секунд %d, плохих %d, наибольшее ожидание эфира %.1f мс' % (sec, bad, airMax / 1000.0))
            sec = bad = airMax = 0
            nextSum += 60
        if select.select([fd], [], [], 0.2)[0]:
            try:
                buf += os.read(fd, 4096)
            except BlockingIOError:
                pass
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                s = line.decode('utf-8', 'replace')
                m = re.search(r'к=(\d+) шв=(\S+) як=(\S+) .*пакетів (\d+), відмов (\d+), не пішло в ефір (\d+), перезапусків радіо (\d+), '
                              r'до ефіру сер\. (\d+) макс\. (\d+)', s)
                if m:
                    sec += 1
                    pk, ref, fail, avg, mx = int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(8)), int(m.group(9))
                    airMax = max(airMax, mx)
                    if ref or fail or mx > 15000:
                        bad += 1
                        log('ПЕРЕДАВАЧ к=%s шв=%s %s: пакетів %d, відмов %d, не в ефір %d, до ефіру сер. %.1f макс. %.1f мс'
                            % (m.group(1), m.group(2), m.group(3), pk, ref, fail, avg / 1000.0, mx / 1000.0))
                m = re.search(r'приймач (\w+) .*?: (.*?), гучність.*сигнал (-?\d+) дБм \(тут (-?\d+)\), запас (\d+) мс, втрати ([\d.]+) %, пропусків (\d+)', s)
                if m and 'на зв' in m.group(2) and 'не на' not in m.group(2) and (float(m.group(6)) > 2 or int(m.group(7))):
                    log('ПРИЙМАЧ %s: втрати %s %%, пропусків %s, запас %s мс, сигнал %s' % (m.group(1), m.group(6), m.group(7), m.group(5), m.group(3)))
                m = re.search(r'розбір кадру сер\. (\d+), макс\. (\d+) мкс.*недобір (\d+)', s)
                if m and (int(m.group(3)) or int(m.group(2)) > 30000):
                    log('МУЗИКА: розбір сер. %.1f макс. %.1f мс, недобір %s відліків' % (int(m.group(1)) / 1000.0, int(m.group(2)) / 1000.0, m.group(3)))
except OSError as e:
    log('порт пропал: %s' % e)
log('--- наблюдение окончено')
os.close(fd)
