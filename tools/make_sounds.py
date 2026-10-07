# -*- coding: utf-8 -*-
"""Проверочные звуки передатчика → build/assets.bin (мелодии — MP3 стерео, голос — 32 кГц, моно, 16 бит).

  музыка — assets-src/muzyka.mp3 («Morning in the Conservatory») и muzyka2.mp3 («Ранкова прогулянка») — обе дал
           владелец, muzyka3.mp3 («Glass Orchard») — тоже; ещё мелодии — файлами muzyka4 … muzyka7, названия для
           меню — в assets-src/nazvy.txt. Владелец решил (06.10): мелодии лежат в MP3 и в стерео, как в исходниках,
           плата разбирает их на лету; кодировать из исходников в 32 кГц, 128 кбит/с;
  голос  — assets-src/golos.mp3: «Увага! Йде перевірка звуку.» дикторским голосом. Сделан так же, как в чате
           про радио, — нейросетевым голосом Microsoft «Остап»:
             build/venv-tts/bin/edge-tts --voice uk-UA-OstapNeural --rate=-6% --text "…" --write-media assets-src/golos.mp3
           (средство ставится так: python3 -m venv build/venv-tts && build/venv-tts/bin/pip install edge-tts;
           женский голос — uk-UA-PolinaNeural). Если файла нет, берётся голос macOS — только как заглушка.

Мелодии в файле идут первыми, голос — последним: при замене голоса достаточно перезаписать начало и хвост.
Файл assets.bin заливается в раздел данных платы (tools/flash_assets.sh). Формат: «HLPK», число записей,
записи (имя 15 байт, вид записи — 0 без сжатия или 1 MP3, смещение, длина), затем сами звуки.
Запуск: python3 tools/make_sounds.py"""
import os, subprocess, struct, tempfile
import numpy as np

SR = 32000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'assets')
PACK = os.path.join(HERE, '..', 'build', 'assets.bin')
NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def freq(name):                       # «G4», «F#3»
    n = NOTE[name[0]] + (1 if '#' in name else 0)
    return 440.0 * 2 ** ((n + 12 * (int(name[-1]) + 1) - 69) / 12.0)


def tone(f, dur, vol=1.0, bell=False):
    """Одна нота: гармоники затухают, верхние — быстрее (как у струны)."""
    tail = 1.2 if not bell else 2.5
    t = np.arange(int((dur + tail) * SR)) / SR
    y = np.zeros_like(t)
    parts = [(1, 1.0), (2, 0.5), (3, 0.28), (4, 0.14), (5, 0.08), (6, 0.04)] if not bell else \
            [(1, 1.0), (2.76, 0.45), (5.4, 0.25), (8.9, 0.1)]
    for k, a in parts:
        if f * k > SR * 0.45:
            continue
        tau = (1.6 if bell else 1.1) / (k ** 0.7) * (220.0 / f) ** 0.3
        y += a * np.exp(-t / tau) * (np.sin(2 * np.pi * f * k * t) + 0.5 * np.sin(2 * np.pi * f * k * 1.0015 * t))
    att = np.minimum(1.0, t / 0.006)
    rel = np.where(t > dur, np.exp(-(t - dur) / (0.18 if not bell else 0.9)), 1.0)
    return vol * y * att * rel


def render(voices, beat):
    """voices: списки (нота или None, длительность в долях, громкость)."""
    total = max(sum(d for _, d, _ in v) for v in voices) * beat + 3.0
    mix = np.zeros(int(total * SR))
    for v in voices:
        pos = 0.0
        for item in v:
            name, d, vol = item
            if name:
                for nm in name.split('+'):
                    y = tone(freq(nm), d * beat * 0.96, vol, bell=nm.endswith('b') if False else False)
                    i = int(pos * SR)
                    mix[i:i + len(y)] += y[:len(mix) - i]
            pos += d * beat
    return mix


def echo(x):                          # лёгкое эхо зала
    out = x.copy()
    for delay, gain in ((0.031, 0.28), (0.047, 0.22), (0.071, 0.17), (0.113, 0.12), (0.167, 0.08)):
        d = int(delay * SR)
        out[d:] += gain * x[:-d]
    return out


def finish(x, peak_db=-5.0):
    x = echo(x)
    x = x[:np.max(np.nonzero(np.abs(x) > 1e-4)) + SR // 4] if np.any(np.abs(x) > 1e-4) else x
    fade = int(0.4 * SR)
    x[-fade:] *= np.linspace(1, 0, fade)
    x = x / np.max(np.abs(x)) * 10 ** (peak_db / 20.0)
    return (x * 32767).astype('<i2')


def seq(text, vol=1.0):               # «E4:1 F4:1 -:2» → [(нота, длительность, громкость)]
    out = []
    for tok in text.split():
        n, d = tok.split(':')
        out.append((None if n == '-' else n, float(d), vol))
    return out


def ode():
    m = ('E4:1 E4:1 F4:1 G4:1 G4:1 F4:1 E4:1 D4:1 C4:1 C4:1 D4:1 E4:1 E4:1.5 D4:.5 D4:2 '
         'E4:1 E4:1 F4:1 G4:1 G4:1 F4:1 E4:1 D4:1 C4:1 C4:1 D4:1 E4:1 D4:1.5 C4:.5 C4:2 '
         'D4:1 D4:1 E4:1 C4:1 D4:1 E4:.5 F4:.5 E4:1 C4:1 D4:1 E4:.5 F4:.5 E4:1 D4:1 C4:1 D4:1 G3:2 '
         'E4:1 E4:1 F4:1 G4:1 G4:1 F4:1 E4:1 D4:1 C4:1 C4:1 D4:1 E4:1 D4:1.5 C4:.5 C4:2')
    b = ('C3+G3:2 C3+G3:2 G2+D3:2 G2+D3:2 C3+G3:2 C3+G3:2 G2+D3:2 G2+D3:2 '
         'C3+G3:2 C3+G3:2 G2+D3:2 G2+D3:2 C3+G3:2 C3+G3:2 G2+D3:2 C3+G3:2 '
         'G2+D3:2 C3+G3:2 G2+D3:2 C3+G3:2 G2+D3:2 A2+E3:2 D3+A3:2 G2+D3:2 '
         'C3+G3:2 C3+G3:2 G2+D3:2 G2+D3:2 C3+G3:2 C3+G3:2 G2+D3:2 C3+G3:2')
    return render([seq(m, 1.0), seq(b, 0.42)], 0.52)


def grace():
    m = ('D4:1 G4:2 B4:.5 G4:.5 B4:2 A4:1 G4:2 E4:1 D4:2 D4:1 G4:2 B4:.5 G4:.5 B4:2 A4:1 D5:3 D5:2 '
         'B4:1 D5:1.5 B4:.5 D5:.5 B4:.5 G4:2 D4:1 E4:1.5 G4:.5 G4:.5 E4:.5 D4:2 D4:1 '
         'G4:2 B4:.5 G4:.5 B4:2 A4:1 G4:3 G4:2')
    b = ('-:1 G2+D3+B3:3 G2+D3+B3:3 C3+G3+E3:3 G2+D3+B3:3 G2+D3+B3:3 G2+D3:3 D3+A3+F#3:3 D3+A3:3 '
         'G2+D3+B3:3 G2+D3+B3:3 C3+G3+E3:3 G2+D3+B3:3 G2+D3+B3:3 D3+A3:3 G2+D3+B3:3 G2+D3:3')
    return render([seq(m, 1.0), seq(b, 0.36)], 0.60)


def chimes():
    rng = np.random.RandomState(7)
    scale = ['C5', 'D5', 'E5', 'G5', 'A5', 'C6', 'D6', 'E6']
    total = 22.0
    mix = np.zeros(int((total + 4) * SR))
    t = 0.0
    k = 0
    pattern = [0, 2, 4, 5, 4, 2, 3, 1, 0, 3, 5, 7, 5, 3, 4, 2]
    while t < total:
        nm = scale[pattern[k % len(pattern)] if k % 32 < 24 else rng.randint(0, 8)]
        y = tone(freq(nm), 0.25, 0.8 + 0.2 * rng.rand(), bell=True)
        i = int(t * SR)
        mix[i:i + len(y)] += y[:len(mix) - i]
        if k % 4 == 0:
            y = tone(freq(['C4', 'G3', 'A3', 'F3'][(k // 8) % 4]), 1.6, 0.5, bell=True)
            mix[i:i + len(y)] += y[:len(mix) - i]
        t += 0.34 if k % 8 != 7 else 0.68
        k += 1
    return mix


def voice(text):
    with tempfile.TemporaryDirectory() as d:
        aiff, wav = os.path.join(d, 'v.aiff'), os.path.join(d, 'v.wav')
        subprocess.check_call(['say', '-v', 'Lesya', '-r', '150', '-o', aiff, text])
        subprocess.check_call(['afconvert', '-f', 'WAVE', '-d', 'LEI16@%d' % SR, '-c', '1', aiff, wav])
        raw = open(wav, 'rb').read()
    i = raw.index(b'data') + 8
    x = np.frombuffer(raw[i:i + (len(raw) - i) // 2 * 2], dtype='<i2').astype(np.float64) / 32768.0
    nz = np.nonzero(np.abs(x) > 0.01)[0]
    x = x[max(0, nz[0] - SR // 20):nz[-1] + SR // 5]
    x = x / np.max(np.abs(x)) * 10 ** (-3.0 / 20.0)
    return (x * 32767).astype('<i2')


SRC = os.path.join(HERE, '..', 'assets-src')


def from_file(path, peak_db):
    """Любой звуковой файл → 32 кГц моно; тишина по краям срезается, уровень — по наибольшему отсчёту."""
    raw = subprocess.check_output(['/usr/local/bin/ffmpeg', '-v', 'error', '-i', path, '-ac', '1', '-ar', str(SR), '-f', 's16le', '-'])
    x = np.frombuffer(raw[:len(raw) // 2 * 2], dtype='<i2').astype(np.float64) / 32768.0
    nz = np.nonzero(np.abs(x) > 0.004)[0]
    x = x[max(0, nz[0] - SR // 20):nz[-1] + SR // 4]
    x = x / np.max(np.abs(x)) * 10 ** (peak_db / 20.0)
    return (x * 32767).astype('<i2')


def find(name):
    for ext in ('.wav', '.mp3', '.m4a', '.aiff', '.flac'):
        p = os.path.join(SRC, name + ext)
        if os.path.exists(p):
            return p
    return None


PART = 0x9E0000                       # раздел «assets» в hearlink/partitions.csv (с 07.10 — 9,9 МБ: в конце флеша второй раздел прошивки)
os.makedirs(os.path.dirname(PACK), exist_ok=True)
g = find('golos')
if g:
    vo = from_file(g, -2.0)
    print('голос: запись', os.path.basename(g))
else:
    vo = voice('Увага! Йде перевірка звуку.')
    print('голос: ЗАГЛУШКА — синтезатор macOS; сделайте assets-src/golos.mp3 (см. начало файла)')
# мелодии: muzyka, muzyka2, muzyka3 … Лежат в MP3 и остаются в стерео, как в исходных файлах.
# Кодируются каждый раз заново из исходника: 32 кГц (частота набора; приводить её в плате было бы хуже и дороже),
# MUSIC_KBPS кбит/с, уровень — наибольший отсчёт на −2,5 дБ (у MP3 после разбора вершины чуть выше, чем были).
MUSIC_KBPS = 128      # владелец, 06.10: «в 32 кГц 128 кб/с, для экономии пространства и облегчения работы при декодировании»


def mp3_music(path):
    info = subprocess.check_output(['/usr/local/bin/ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                                    'stream=channels,sample_rate,bit_rate', '-of', 'default=nw=1:nk=1', path]).decode().split()
    rate, chans, kbps = int(info[0]), int(info[1]), int(info[2]) // 1000
    src_kbps, kbps = kbps, MUSIC_KBPS
    if rate == SR and src_kbps <= MUSIC_KBPS:
        # исходник уже в нужном виде — кладём как есть, без второго сжатия (только без меток и служебного кадра)
        mp3 = subprocess.check_output(['/usr/local/bin/ffmpeg', '-v', 'error', '-i', path, '-vn', '-map_metadata', '-1', '-c:a', 'copy',
                                       '-id3v2_version', '0', '-write_xing', '0', '-f', 'mp3', '-'])
        kbps = src_kbps
        print('  исходник уже 32 кГц и не больше %d кбит/с — положен без перекодирования' % MUSIC_KBPS)
    else:
        raw = subprocess.check_output(['/usr/local/bin/ffmpeg', '-v', 'error', '-i', path, '-f', 's16le', '-'])
        peak = np.max(np.abs(np.frombuffer(raw[:len(raw) // 2 * 2], dtype='<i2').astype(np.float64))) / 32768.0
        gain = -2.5 - 20 * np.log10(peak)
        mp3 = subprocess.check_output(['/usr/local/bin/ffmpeg', '-v', 'error', '-i', path, '-vn', '-map_metadata', '-1',
                                       '-af', 'volume=%.2fdB,aresample=%d:filter_size=128:cutoff=0.97' % (gain, SR),
                                       '-ac', str(min(chans, 2)), '-c:a', 'libmp3lame', '-b:a', '%dk' % kbps,
                                       '-id3v2_version', '0', '-write_xing', '0', '-f', 'mp3', '-'])
    back = subprocess.check_output(['/usr/local/bin/ffmpeg', '-v', 'error', '-f', 'mp3', '-i', '-', '-f', 's16le', '-'], input=mp3)
    y = np.frombuffer(back[:len(back) // 2 * 2], dtype='<i2')
    ch = min(chans, 2)
    print('  исходник: %d Гц, каналов %d, %d кбит/с → в плату: %d Гц, каналов %d, %d кбит/с, %.1f с, наибольший отсчёт %.1f дБ'
          % (rate, chans, src_kbps, SR, ch, kbps, len(y) / ch / SR, 20 * np.log10(np.max(np.abs(y.astype(np.float64))) / 32768.0)))
    return mp3, len(y) / ch / SR


items = []                                         # (имя, данные, вид записи: 0 — без сжатия, 1 — MP3, секунд)
for name in ['muzyka'] + ['muzyka%d' % k for k in range(2, 8)]:
    m = find(name)
    if m:
        print('%s: %s' % (name, os.path.basename(m)))
        data, secs = mp3_music(m)
        items.append((name, data, 1, secs))
items.append(('golos', vo.tobytes(), 0, len(vo) / SR))
# английский голос объявления (assets-src/golos-en.*): звучит, когда на передатчике выбран английский язык.
# Лежит сразу за украинским, поэтому заливается так же быстро: tools/flash_assets.sh <порт> golos
ge = find('golos-en')
if ge:
    ve = from_file(ge, -2.0)
    items.append(('golos-en', ve.tobytes(), 0, len(ve) / SR))
    print('голос (англ.): запись', os.path.basename(ge))
# названия мелодий для меню: assets-src/nazvy.txt, строки «muzyka=Название»; чего там нет — «Мелодія N» в плате
titles = os.path.join(SRC, 'nazvy.txt')
if os.path.exists(titles):
    items.append(('nazvy', open(titles, 'rb').read().strip() + b'\n', 0, 0))
blob = b''
table = b''
TABLE = 8                                          # в оглавлении всегда место на 8 записей: добавление записи не сдвигает звук
off = 8 + 24 * TABLE
for name, data, kind, secs in items:
    data += b'\0' * (-len(data) % 4)
    table += name.encode().ljust(15, b'\0') + bytes([kind]) + struct.pack('<II', off + len(blob), len(data))
    blob += data
    print('%-9s %6.1f с  %7d КБ  %s' % (name, secs, len(data) // 1024, 'MP3' if kind else 'названия' if name == 'nazvy' else 'без сжатия'))
open(PACK, 'wb').write(b'HLPK' + struct.pack('<I', len(items)) + table.ljust(24 * TABLE, b'\0') + blob)
size = os.path.getsize(PACK)
assert len(items) <= TABLE, 'записей больше восьми — увеличить assetTab в hearlink/assets.h'
assert size <= PART, 'не помещается в раздел: %d > %d' % (size, PART)
print('всего %d КБ из %d → %s' % (size // 1024, PART // 1024, os.path.normpath(PACK)))
