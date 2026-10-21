# -*- coding: utf-8 -*-
# Сверка оценки уровня кадров MP3 с настоящей волной (плеер карты передатчика, 3.28): python3 tools/wave/gg3.py <файлы.mp3>
# -*- coding: utf-8 -*-
import sys, struct, subprocess, math
sys.path.insert(0,'.')
from gg import frames
from PIL import Image, ImageDraw
import numpy as np
A,B=0.65,3.0
rows=[]
for path in sys.argv[1:]:
    data=open(path,'rb').read(); fr=frames(data); rate=fr[0][2]; spf=fr[0][3]
    pcm=subprocess.run(['ffmpeg','-v','quiet','-i',path,'-f','s16le','-ac','1','-ar',str(rate),'-'],capture_output=True).stdout
    s=np.frombuffer(pcm,dtype='<i2').astype(np.float32)
    nf=min(len(fr),len(s)//spf)
    pk=np.array([np.abs(s[k*spf:(k+1)*spf]).max() for k in range(nf)])
    gg=np.array([f[4] for f in fr[:nf]],dtype=np.float32); p23=np.array([f[5] for f in fr[:nf]],dtype=np.float32)
    L=A*gg+B*np.log2(p23+1)
    L[p23<8]=-1e9            # кадр без звука (данных почти нет) — тишина
    ref=np.percentile(L[L>-1e8],99.5)
    amp=np.clip(10**((L-ref)/20),0,1)
    W=224
    def cols(v):
        return np.array([v[c*nf//W:max(c*nf//W+1,(c+1)*nf//W)].max() for c in range(W)])
    tp=cols(pk); tp=tp/np.percentile(tp,99.5); tp=np.clip(tp,0,1)
    ep=cols(amp)
    r=np.corrcoef(20*np.log10(np.maximum(tp,1e-3)),20*np.log10(np.maximum(ep,1e-3)))[0,1]
    print('%s: корреляция столбцов (дБ) %.2f'%(path.split('/')[-1],r))
    rows.append((tp,ep))
H=80; W=224; img=Image.new('RGB',(W*2+20,len(rows)*(2*(H+8))+10),(20,20,24)); d=ImageDraw.Draw(img)
y0=5
for tp,ep in rows:
    for v,col in ((tp,(90,160,255)),(ep,(120,220,140))):
        mid=y0+H//2
        for c in range(W):
            h=int(v[c]*(H//2-2)); d.rectangle([10+2*c,mid-h,10+2*c+1,mid+h],fill=col)
        y0+=H+8
img.save('cmp2.png'); print('cmp2.png')
