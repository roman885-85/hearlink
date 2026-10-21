# -*- coding: utf-8 -*-
"""Проверка доставки настроек приёмнику (с 3.30 / 4.00): серии команд эквалайзера через порт передатчика и сверка с тем,
что приёмник сообщил о себе (строка «еквалайзер …» в списке P).
   python3 tools/cmdtest.py <порт передатчика> <номер приёмника> [серий] [пауза_мс]
Каждая серия — 6 команд подряд с паузой (по умолчанию 120 мс): пять полос и срез низов (i30…i35), значения
случайные; потом 6 с ожидания и P. В конце серия «всё по нулям, срез выключен» — так и остаётся (у владельца было так).
Итог — сколько серий сошлось точно, и строка счёта команд передатчика («команди приймачам: …»)."""
import os, random, re, select, struct, sys, termios, time, fcntl

port, rid = sys.argv[1], sys.argv[2].upper()
series = int(sys.argv[3]) if len(sys.argv) > 3 else 5
gap = float(sys.argv[4]) / 1000 if len(sys.argv) > 4 else 0.12
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
fcntl.ioctl(fd, 0x8004746b, struct.pack('I', 0x002 | 0x004))
buf = b''


def send(c):
    os.write(fd, (c + '\n').encode())


def lines(secs):
    global buf
    out, t0 = [], time.time()
    while time.time() - t0 < secs:
        if select.select([fd], [], [], 0.1)[0]:
            try:
                buf += os.read(fd, 65536)
            except BlockingIOError:
                pass
            while b'\n' in buf:
                l, buf = buf.split(b'\n', 1)
                out.append(l.decode('utf-8', 'replace').rstrip())
    return out


def report():
    send('P')
    eq, stat, mine = None, None, False
    for l in lines(2.5):
        if 'приймач %s' % rid in l:
            mine = True
        elif re.search(r'приймач [0-9A-F]{6} ', l):
            mine = False
        m = re.search(r'еквалайзер (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) дБ, зріз низів (\d)', l)
        if m and mine:
            eq = [int(x) for x in m.groups()]
        if 'команди приймачам' in l:
            stat = l.strip()
    return eq, stat


lines(1.0)
ok = 0
plans = [[random.choice(range(-12, 13, 2)) for _ in range(5)] + [random.choice((0, 1))] for _ in range(series)]
plans.append([0, 0, 0, 0, 0, 0])
for n, want in enumerate(plans, 1):
    for b, v in enumerate(want):
        send('i%d=%d@%s' % (30 + b, v, rid))
        time.sleep(gap)
    lines(6.0)
    got, stat = report()
    same = got == want
    ok += same
    print('серія %d: задано %s, приймач повідомив %s — %s' % (n, want, got, 'збіглося' if same else 'НЕ ЗБІГЛОСЯ'), flush=True)
print('збіглося %d з %d' % (ok, len(plans)))
print(stat or 'рядка «команди приймачам» немає (прошивка до 3.30?)')
os.close(fd)
