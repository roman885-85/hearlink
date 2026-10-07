#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сравнение каналов и скоростей прошивкой linktest: обе платы подключены к этому компьютеру.
   python3 tools/sweep.py <порт передатчика> <порт приёмника> <секунд на замер> <канал:скорость> …
Пример: python3 tools/sweep.py /dev/cu.A /dev/cu.B 30 1:6 6:6 11:6 13:6 13:12"""
import os, sys, time, termios, select, fcntl, struct, re


def open_port(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))  # отпустить DTR и RTS
    return fd


class Port:
    def __init__(self, path):
        self.fd, self.buf, self.lines = open_port(path), b'', []

    def send(self, cmd):
        os.write(self.fd, (cmd + '\n').encode())

    def pump(self, secs):
        t0 = time.time()
        while time.time() - t0 < secs:
            if select.select([self.fd], [], [], 0.1)[0]:
                try:
                    self.buf += os.read(self.fd, 4096)
                except BlockingIOError:
                    pass
                while b'\n' in self.buf:
                    line, self.buf = self.buf.split(b'\n', 1)
                    self.lines.append(line.decode('utf-8', 'replace').rstrip())


def pump_both(a, b, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        a.pump(0.1)
        b.pump(0.1)


tx, rx = Port(sys.argv[1]), Port(sys.argv[2])
secs = float(sys.argv[3])
pump_both(tx, rx, 1)
print('канал шв. | потери пакетов | подряд ≥2 | опоздание: медиана / худшая секунда | ожидание эфира: ср. / макс. | '
      'пропало кадров с повтором при запасе 4 6 8 10 12 16 24 мс | сигнал')
for spec in sys.argv[4:]:
    ch, rate = spec.split(':')
    tx.send('c' + ch); pump_both(tx, rx, 0.7)
    tx.send('v' + rate); pump_both(tx, rx, 0.7)
    rx.send('c' + ch); pump_both(tx, rx, 2.5)
    rx.send('0'); pump_both(tx, rx, 0.3)
    tx.lines, rx.lines = [], []
    pump_both(tx, rx, secs + 1.5)
    late, rssi, tot, red, air_avg, air_max = [], [], None, None, [], []
    for ln in rx.lines:
        m = re.search(r'запізнення макс\. ([\d.]+) мс \| сигнал (-?\d+)', ln)
        if m:
            late.append(float(m.group(1))); rssi.append(int(m.group(2)))
        m = re.search(r'за (\d+) с: кадрів (\d+), втрачено пакетів (\d+) \(([\d.]+) %\), підряд ≥2 — (\d+)', ln)
        if m:
            tot = m.groups()
        m = re.search(r'пропало з повтором:\s+(.*)', ln)
        if m:
            red = m.group(1).split()
    for ln in tx.lines:
        m = re.search(r'до ефіру: сер\. (\d+) мкс, макс\. (\d+) мкс', ln)
        if m:
            air_avg.append(int(m.group(1))); air_max.append(int(m.group(2)))
    if not tot or not late:
        print('%5s %4s | нет данных (приёмник молчит)' % (ch, rate), flush=True)
        continue
    late.sort()
    print('%5s %4s | %6s из %s (%s %%) | %3s | %4.1f / %4.1f мс | %4.2f / %4.1f мс | %s | %d дБм'
          % (ch, rate, tot[2], tot[1], tot[3], tot[4], late[len(late) // 2], late[-1],
             (sum(air_avg) / len(air_avg) / 1000.0) if air_avg else 0, (max(air_max) / 1000.0) if air_max else 0,
             ' '.join('%3s' % v for v in (red or [])), sum(rssi) / len(rssi)), flush=True)
