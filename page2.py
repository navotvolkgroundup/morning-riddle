import sys
from PIL import Image, ImageDraw, ImageFont
BLOB=open('main/ui/font24HE.FON','rb').read()
HE_BASE,HE_LAST,HE_H,ROWB,HE_GAP,HE_SPACE,HE_W=0x5D0,0x5EA,41,3,3,9,24
HE_LAT_W=12                      # was 16; M5GFX size-2 glyph advance is 12
N=HE_LAST-HE_BASE+1; CELL=ROWB*HE_H
WID=[BLOB[N*CELL+i] for i in range(N)]
W,H=400,600; MX,BODY_BOTTOM=14,H-8
HDR_Y,HDR_RULE_Y,LINE_H,BAND_PAD,ZONE_GAP=8,40,41,8,6
RIDDLE_TOP_MIN,CHOICE_H,CHOICE_GAP=200,56,8
RED,BLK=(200,0,0),(0,0,0)
img=Image.new('RGB',(W,H),'white'); d=ImageDraw.Draw(img)
LAT=ImageFont.truetype('/System/Library/Fonts/Menlo.ttc',15)
HEB_DAY=["יום ראשון","יום שני","יום שלישי","יום רביעי","יום חמישי","יום שישי","שבת"]

def adv(c,s=1):
    o=ord(c)
    if HE_BASE<=o<=HE_LAST: return (WID[o-HE_BASE]+HE_GAP)*s
    if c==' ': return HE_SPACE*s
    if 0x20<o<0x7F: return HE_LAT_W*s
    return 0
def measure(t,s=1): return sum(adv(c,s) for c in t)
def glyph(x,y,cp,col,s=1):
    off=(cp-HE_BASE)*CELL; g=BLOB[off:off+CELL]
    for r in range(HE_H):
        for b in range(ROWB):
            by=g[r*ROWB+b]
            if by==0xFF: continue
            for bit in range(8):
                if not ((by>>(7-bit))&1):
                    px,py=x+(b*8+bit)*s,y+r*s
                    if s==1: d.point((px,py),col)
                    else: d.rectangle([px,py,px+s-1,py+s-1],fill=col)
def rtl(right,y,t,col=BLK,s=1):
    x=right; i=0
    while i<len(t):
        c=t[i]; o=ord(c)
        if HE_BASE<=o<=HE_LAST:
            x-=(WID[o-HE_BASE]+HE_GAP)*s
            if x<0: return x
            glyph(x,y,o,col,s); i+=1
        elif c==' ': x-=HE_SPACE*s; i+=1
        elif 0x20<o<0x7F:
            j=i
            while j<len(t) and 0x20<ord(t[j])<0x7F: j+=1
            run=t[i:j]; x-=len(run)*HE_LAT_W*s
            d.text((x,y+(HE_H*s-28)//2+4),run,font=LAT,fill=col); i=j
        else: i+=1
    return x
def rtl_fit(right,left,y,t,col=BLK):
    if measure(t)<=right-left: return rtl(right,y,t,col)
    cut=len(t)
    while cut>0:
        c=max([i for i in range(cut) if t[i]==','] or [0])
        if c==0: c=max([i for i in range(cut) if t[i]==' '] or [0])
        if c==0: break
        cut=c; b=t[:cut]+"..."
        if measure(b)<=right-left: return rtl(right,y,b,col)
    return rtl(right,y,t,col)
def brk(t,width):
    w=0; last=None
    for i,c in enumerate(t):
        a=adv(c)
        if c==' ': last=i
        if w+a>width: return last if last else i
        w+=a
    return len(t)
def wrap_lines(t,width,maxl):
    out=[]; p=t
    for _ in range(maxl):
        if not p: break
        n=brk(p,width)
        if n<=0: break
        out.append(p[:n]); p=p[n:].lstrip(' ')
    return out

def draw_page(date,weekday,streak,sched,wx,callout,birthday,question,choices,answer,show_answer):
    d.rectangle([0,0,W,H],fill='white')
    band_y=HDR_RULE_Y+ZONE_GAP; y=band_y+BAND_PAD
    sy=wy=by=cy=None
    if sched: sy=y; y+=LINE_H
    if wx:    wy=y; y+=LINE_H
    band_h=0
    if sy is not None or wy is not None:
        band_h=(y+BAND_PAD)-band_y; y=band_y+band_h+ZONE_GAP
    else: y=band_y
    if birthday: by=y; y+=2*LINE_H+16+ZONE_GAP
    if callout:  cy=y; y+=LINE_H+ZONE_GAP
    if y<RIDDLE_TOP_MIN: y=RIDDLE_TOP_MIN
    riddle_top=y
    # header: Hebrew weekday on the right (RTL anchor), numeric date on the left
    d.text((MX,HDR_Y+12),date,font=LAT,fill=BLK)
    rtl(W-MX,HDR_Y-10,weekday)
    if streak>1: rtl(W-MX,HDR_Y+34,f"{streak} ימים")
    d.line([(MX,HDR_RULE_Y+34),(W-MX,HDR_RULE_Y+34)],fill=BLK)
    off=34
    if band_h>0: d.rectangle([MX,band_y+off,W-MX,band_y+band_h+off],outline=BLK)
    if sy is not None: rtl_fit(W-MX-BAND_PAD,MX+BAND_PAD,sy+off,sched)
    if wy is not None: rtl_fit(W-MX-BAND_PAD,MX+BAND_PAD,wy+off,wx)
    if by is not None:
        d.rectangle([MX,by+off,W-MX,by+off+2*HE_H+16],outline=RED)
        rtl(W-MX-12,by+off+8,"יום הולדת שמח",RED); rtl(W-MX-12,by+off+8+HE_H,birthday,RED)
    if cy is not None: rtl_fit(W-MX,MX,cy+off,callout)
    riddle_top+=off
    # measure the whole riddle block, then centre it in what is left
    lines=wrap_lines(question,W-2*MX,5)
    blk_h=len(lines)*(HE_H-6)
    if show_answer: blk_h+=20+16+HE_H*2
    elif choices:   blk_h+=12+len(choices)*(CHOICE_H+CHOICE_GAP)-CHOICE_GAP
    y=riddle_top+max(0,(BODY_BOTTOM-riddle_top-blk_h)//2)
    for ln in lines: rtl(W-MX,y,ln); y+=HE_H-6
    if show_answer:
        y+=20; d.line([(MX,y),(W-MX,y)],fill=BLK); y+=16
        rtl(W-MX,y,answer,RED,s=2)
    elif choices:
        y+=12
        for ch in choices:
            rtl(W-MX,y,ch); y+=CHOICE_H+CHOICE_GAP
    return riddle_top
