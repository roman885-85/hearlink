# -*- coding: utf-8 -*-
import os, re, sys, glob
d = sys.argv[1]
tot = {}
for f in sorted(glob.glob(os.path.join(d, 'log-*.txt'))):
    s = open(f, 'rb').read().decode('utf-8', 'replace')
    boots = re.split(r'(?=# hearlink )', s)
    for bt in boots:
        m = re.match(r'# hearlink ([0-9.]+); запуск: ([^;]*);', bt)
        if not m: continue
        ts = [int(x) for x in re.findall(r'\bt=(\d+) ', bt)]
        rest = [int(x) for x in re.findall(r' рест=(\d+) ', bt)]
        stalls = re.findall(r'# РАДІО СТАЛО на (\d+)-й секунді, канал (\d+)', bt)
        chans = sorted(set(re.findall(r' к=(\d+) ', bt)), key=int)
        dur = max(ts) if ts else 0
        v = m.group(1)
        a = tot.setdefault(v, [0, 0, 0])
        a[0] += dur; a[1] += len(stalls); a[2] += 1
        if len(sys.argv) > 2:
            print('%s  %s  %-22s  работал %5d с  остановок %d %s  рест=%d  каналы %s' % (os.path.basename(f)[4:8], v, m.group(2)[:22], dur, len(stalls), ','.join(x[0] for x in stalls)[:44], max(rest) if rest else 0, ','.join(chans)))
print('версия  запусков  часов работы  остановок  в час')
for v in sorted(tot, key=lambda x: [int(p) for p in x.split('.')]):
    h = tot[v][0] / 3600.0
    print('%-6s  %8d  %12.2f  %9d  %s' % (v, tot[v][2], h, tot[v][1], ('%.1f' % (tot[v][1] / h)) if h > 0.05 else '—'))
