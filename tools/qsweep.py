#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сравнение качества звука и скоростей радио на работающем наборе (прошивка hearlink).
К компьютеру подключён только передатчик; о приёмнике он рассказывает сам (команда P).
   python3 tools/qsweep.py <порт передатчика> <секунд на замер> <качество:скорость[:мощность]> …
Пример: python3 tools/qsweep.py /dev/cu.usbserial-144240 12 0:24 0:6 1:6 2:1 3:0.5:2
В конце возвращает «скорость сама, качество найвища, мощность 20»."""
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


fd, buf, lines = open_port(sys.argv[1]), b'', []
secs = float(sys.argv[2])


def pump(t):
    global buf
    t0 = time.time()
    while time.time() - t0 < t:
        if select.select([fd], [], [], 0.1)[0]:
            try:
                buf += os.read(fd, 4096)
            except BlockingIOError:
                pass
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                lines.append(line.decode('utf-8', 'replace').rstrip())


def send(cmd, wait=0.8):
    os.write(fd, (cmd + '\n').encode())
    pump(wait)


def avg(v):
    return sum(v) / len(v) if v else float('nan')


pump(1.5)
print('якість  шв. потужн. | пакетів/с | до ефіру сер./макс., мс | відмов | сигнал у приймача (тут) | втрати пакетів сер./найгірше | '
      'не відновлено кадрів | запас, мс')
for spec in sys.argv[3:]:
    part = spec.split(':')
    q, rate, power = part[0], part[1], part[2] if len(part) > 2 else '20'
    send('p' + power)
    send('q' + q)
    send('v' + rate)
    pump(6)                     # приёмник перестраивается, запас подбирается
    lines[:] = []
    t0 = time.time()
    while time.time() - t0 < secs:
        send('P', 2.1)
    pk, air_a, air_m, ref, rssi, here, loss, lostf, depth, qn = [], [], [], [], [], [], [], [], [], '?'
    for ln in lines:
        m = re.search(r'як=(\S+) .*пакетів (\d+), відмов (\d+).*до ефіру сер\. (\d+) макс\. (\d+) мкс', ln)
        if m:
            qn = m.group(1); pk.append(int(m.group(2))); ref.append(int(m.group(3)))
            air_a.append(int(m.group(4))); air_m.append(int(m.group(5)))
        m = re.search(r'приймач .*: (.*?), гучність.*сигнал (-?\d+) дБм \(тут (-?\d+)\), запас (\d+) мс, втрати ([\d.]+) %, пропусків (\d+)', ln)
        if m and 'на зв' in m.group(1):
            rssi.append(int(m.group(2))); here.append(int(m.group(3))); depth.append(int(m.group(4)))
            loss.append(float(m.group(5))); lostf.append(int(m.group(6)))
    if not loss:
        print('%-10s %4s %3s | %4.0f | %5.2f / %5.2f | %3d | ПРИЙМАЧ МОВЧИТЬ' % (qn, rate, power, avg(pk), avg(air_a) / 1000, (max(air_m) if air_m else 0) / 1000,
                                                                           sum(ref)), flush=True)
        continue
    print('%-10s %4s %3s | %4.0f | %5.2f / %5.2f | %3d | %4.0f (%4.0f) дБм | %5.1f / %5.1f %% | %3d за %d звітів | %2.0f'
          % (qn, rate, power, avg(pk), avg(air_a) / 1000, max(air_m) / 1000, sum(ref), avg(rssi), avg(here), avg(loss), max(loss), sum(lostf), len(loss),
             avg(depth)), flush=True)
send('v0')
send('q0')
send('p20')
os.close(fd)
