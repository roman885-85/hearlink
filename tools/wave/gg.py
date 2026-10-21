# -*- coding: utf-8 -*-
# Сверка: «общее усиление» (global_gain) кадров MP3 против настоящего уровня кадра (разбор ffmpeg).
import sys, struct, subprocess, math
BR1=[0,32,40,48,56,64,80,96,112,128,160,192,224,256,320]
BR2=[0,8,16,24,32,40,48,56,64,80,96,112,128,144,160]
SR={3:[44100,48000,32000],2:[22050,24000,16000],0:[11025,12000,8000]}
class Bits:
    def __init__(s,b,o): s.b=b; s.p=o*8
    def get(s,n):
        v=0
        for _ in range(n):
            v=(v<<1)|((s.b[s.p>>3]>>(7-(s.p&7)))&1); s.p+=1
        return v
def frames(data):
    i=0
    if data[:3]==b'ID3':
        sz=(data[6]<<21)|(data[7]<<14)|(data[8]<<7)|data[9]; i=10+sz
    out=[]
    while i+4<=len(data):
        h=struct.unpack('>I',data[i:i+4])[0]
        if (h>>21)&0x7FF!=0x7FF: i+=1; continue
        ver=(h>>19)&3; lay=(h>>17)&3; prot=(h>>16)&1; bri=(h>>12)&15; sri=(h>>10)&3; pad=(h>>9)&1; mode=(h>>6)&3
        if ver==1 or lay!=1 or bri in (0,15) or sri==3: i+=1; continue
        rate=SR[ver][sri]; mpeg1=ver==3
        br=(BR1 if mpeg1 else BR2)[bri]*1000
        flen=(144 if mpeg1 else 72)*br//rate+pad
        nch=1 if mode==3 else 2
        b=Bits(data,i+4+(0 if prot else 2))
        gg=[]; p23=[]
        if mpeg1:
            b.get(9); b.get(5 if nch==1 else 3); b.get(4*nch)
            for gr in range(2):
                for ch in range(nch):
                    p=b.get(12); bv=b.get(9); g=b.get(8); b.get(4); ws=b.get(1); b.get(22); b.get(3)
                    gg.append(g); p23.append(p)
        else:
            b.get(8); b.get(1 if nch==1 else 2)
            for ch in range(nch):
                p=b.get(12); bv=b.get(9); g=b.get(8); b.get(9); ws=b.get(1); b.get(22); b.get(2)
                gg.append(g); p23.append(p)
        out.append((i,flen,rate,1152 if mpeg1 else 576,max(gg),max(p23)))
        i+=flen
    return out
for path in sys.argv[1:]:
    data=open(path,'rb').read()
    fr=frames(data)
    rate=fr[0][2]; spf=fr[0][3]
    pcm=subprocess.run(['ffmpeg','-v','quiet','-i',path,'-f','s16le','-ac','1','-ar',str(rate),'-'],capture_output=True).stdout
    s=struct.unpack('<%dh'%(len(pcm)//2),pcm)
    # уровень по кадрам (пик и СКЗ)
    xs=[];ys=[];zs=[]
    for k,f in enumerate(fr):
        seg=s[k*spf:(k+1)*spf]
        if not seg: break
        pk=max(abs(v) for v in seg); rms=math.sqrt(sum(v*v for v in seg)/len(seg))
        if pk<30: continue
        xs.append(1.5*f[4]); ys.append(20*math.log10(pk)); zs.append(20*math.log10(max(rms,1)))
    n=len(xs); mx=sum(xs)/n; my=sum(ys)/n; mz=sum(zs)/n
    cxy=sum((a-mx)*(b-my) for a,b in zip(xs,ys)); cxz=sum((a-mx)*(c-mz) for a,c in zip(xs,zs))
    vx=sum((a-mx)**2 for a in xs); vy=sum((b-my)**2 for b in ys); vz=sum((c-mz)**2 for c in zs)
    # уровень по столбцам (224 столбца): пик за столбец против наибольшего gg за столбец
    W=224; colp=[0]*W; colg=[0]*W
    for k,f in enumerate(fr):
        c=min(W-1,k*W//len(fr)); seg=s[k*spf:(k+1)*spf]
        if seg: colp[c]=max(colp[c],max(abs(v) for v in seg))
        colg[c]=max(colg[c],f[4])
    cp=[20*math.log10(max(v,1)) for v in colp]; cg=[1.5*v for v in colg]
    a=sum(cp)/W; b=sum(cg)/W
    r2=sum((x-a)*(y-b) for x,y in zip(cp,cg))/math.sqrt(sum((x-a)**2 for x in cp)*sum((y-b)**2 for y in cg))
    print('%s: кадров %d, %d Гц; корреляция 1,5·gg с пиком кадра %.2f, с СКЗ кадра %.2f; по 224 столбцам с пиком %.2f'%(path.split('/')[-1],len(fr),rate,cxy/math.sqrt(vx*vy),cxz/math.sqrt(vx*vz),r2))
