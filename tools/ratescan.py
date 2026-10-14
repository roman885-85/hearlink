#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Замер: как приёмники слышат передатчик в разных режимах (скорость радио, качество звука, мощность).
   python3 tools/ratescan.py <порт> <секунд на режим> <режим> [<режим> …] [-- <команда возврата> …]
Режим — команды передатчику через запятую, например «p20,v5.5,q0» (мощность 20 дБм, 5,5 Мбит/с, «найвища»).
После смены режима 7 с не считаем (приёмники перестраиваются, уходят сообщения со старым счётом), потом заданное
время раз в секунду спрашиваем таблицу приёмников (команда P) и берём каждое новое сообщение приёмника о себе
(счётчик «отримано» вырос): потери до восстановления, невосстановленные кадры, провалы звука. Сообщения приходят
с подтверждением и несут счёт с прошлого доставленного, поэтому сумма по ним — полный счёт за время режима.
Команды возврата (после «--») уходят в конце в любом случае — и при ошибке, и при Ctrl-C.
Передатчик должен быть на проверочном звуке или файле: с живым входом PCM1808 порт молчит.
На время замера нижняя ступень скорости (2.78: ради слабого приёмника) запрещается командой Ql0, в конце — Ql1."""
import os, sys, time, termios, select, fcntl, struct, re

port = sys.argv[1]
secs = float(sys.argv[2])
rest = sys.argv[3:]
back = []
if '--' in rest:
    i = rest.index('--')
    back = rest[i + 1:]
    rest = rest[:i]
modes = rest
SETTLE = 7.0

fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
TIOCMBIC, TIOCM_DTR, TIOCM_RTS = 0x8004746b, 0x002, 0x004
fcntl.ioctl(fd, TIOCMBIC, struct.pack('I', TIOCM_DTR | TIOCM_RTS))

RE_RX = re.compile(r"приймач (\w{6}) .*?: (не на зв'язку|на зв'язку).*?сигнал (-?\d+) дБм \(тут (-?\d+)\), запас (\d+) мс, втрати ([\d.]+) %, пропусків (\d+), провалів (\d+) мс")
RE_GOT = re.compile(r"повідомлень про себе: отримано (\d+), не дійшло близько (\d+)")
RE_TX = re.compile(r"ПЕРЕДАВАЧ к=(\d+) шв=(\S+) як=(\S+) .*?пакетів (\d+).*?до ефіру сер\. (\d+) макс\. (\d+) мкс.*?проріджено (\d+), пропущено (\d+)")

buf = b''
cur = None            # приёмник, чья строка таблицы пришла последней
lastGot = {}          # приёмник -> счётчик «отримано» при прошлом опросе
samples = {}          # приёмник -> список (сигнал, запас, втрати %, пропусков, провалов мс)
txs = []              # строки передатчика: (скорость, качество, пакетов, до эфира сер., макс., прорежено, пропущено)
measuring = False


def pump(until):
    global buf, cur
    while time.time() < until:
        if not select.select([fd], [], [], 0.1)[0]:
            continue
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            continue
        while b'\n' in buf:
            raw, buf = buf.split(b'\n', 1)
            line = raw.decode('utf-8', 'replace')
            m = RE_RX.search(line)
            if m:
                cur = (m.group(1), m.group(2) == "на зв'язку", int(m.group(3)), int(m.group(5)), float(m.group(6)), int(m.group(7)), int(m.group(8)), int(m.group(4)))
                continue
            m = RE_GOT.search(line)
            if m and cur:
                rid, got = cur[0], int(m.group(1))
                if rid in lastGot and got != lastGot[rid] and measuring:
                    samples.setdefault(rid, []).append(cur[1:] + (got - lastGot[rid],))
                lastGot[rid] = got
                cur = None
                continue
            m = RE_TX.search(line)
            if m and measuring:
                txs.append((m.group(2), m.group(3)) + tuple(int(m.group(i)) for i in range(4, 9)))


def send(c):
    os.write(fd, (c + '\n').encode())


try:
    pump(time.time() + 2.5)
    send('Ql0')   # с 2.78 выбранная скорость — лишь верхняя ступень; на время замера нижнюю запрещаем
    pump(time.time() + 0.5)
    print('режим                 | приёмник сигнал сообщ. | потери ср./наиб. %  | кадров пропало (сообщений с пропажей) | провалов мс | запас мс | передатчик: до эфира ср./наиб. мс, прорежено, пропущено')
    for mode in modes:
        for c in mode.split(','):
            send(c)
            pump(time.time() + 0.4)
        measuring = False
        t = time.time()
        while time.time() - t < SETTLE:
            send('P')
            pump(time.time() + 1.0)
        samples.clear()
        del txs[:]
        measuring = True
        t = time.time()
        while time.time() - t < secs:
            send('P')
            pump(time.time() + 1.0)
        measuring = False
        txline = ''
        if txs:
            txline = '%s/%s: %.1f / %.1f, %d, %d' % (txs[-1][0], txs[-1][1], sum(x[3] for x in txs) / len(txs) / 1000.0, max(x[4] for x in txs) / 1000.0, sum(x[5] for x in txs), sum(x[6] for x in txs))
        if not samples:
            print('%-21s | сообщений приёмников нет | %s' % (mode, txline), flush=True)
        for rid in sorted(samples):
            s = samples[rid]
            on = [x for x in s if x[0]]
            if not on:
                print('%-21s | %s не на связи' % (mode, rid), flush=True)
                continue
            n = len(on)
            capped = sum(1 for x in on if x[3] >= 25.5 or x[4] >= 255 or x[5] >= 255)
            print('%-21s | %s %4d (тут %4d) дБм %3d | %5.1f / %4.1f%s       | %4d (%d из %d)%s                    | %5d       | %2d…%2d    | %s' % (
                mode, rid, round(sum(x[1] for x in on) / n), round(sum(x[6] for x in on) / n), n, sum(x[3] for x in on) / n, max(x[3] for x in on), '+' if capped else ' ',
                sum(x[4] for x in on), sum(1 for x in on if x[4] > 0), n, ' (есть «в упор»: %d)' % capped if capped else '',
                sum(x[5] for x in on), min(x[2] for x in on), max(x[2] for x in on), txline), flush=True)
finally:
    send('Ql1')
    time.sleep(0.6)
    for c in back:
        send(c)
        time.sleep(0.6)
    if back:
        print('возвращено: ' + ' '.join(back), flush=True)
    os.close(fd)
