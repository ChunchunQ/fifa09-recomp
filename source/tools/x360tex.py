import struct
from PIL import Image

def tiled_block_index(x, y, width_blocks, log_bpp):
    aligned_width = (width_blocks + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (aligned_width >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return ((((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) +
             ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp)

def swap16(b):
    out = bytearray(len(b)); out[0::2] = b[1::2]; out[1::2] = b[0::2]; return bytes(out)

def c565(v):
    r=(v>>11)&31; g=(v>>5)&63; b=v&31
    return (r*255//31, g*255//63, b*255//31)

def dxt5_block(blk):
    a0,a1=blk[0],blk[1]; ab=int.from_bytes(blk[2:8],'little')
    if a0>a1: al=[a0,a1]+[((7-i)*a0+i*a1)//7 for i in range(1,7)]
    else: al=[a0,a1]+[((5-i)*a0+i*a1)//5 for i in range(1,5)]+[0,255]
    c0,c1=struct.unpack('<HH',blk[8:12]); idx=int.from_bytes(blk[12:16],'little')
    p0,p1=c565(c0),c565(c1)
    cols=[p0,p1,tuple((2*a+b)//3 for a,b in zip(p0,p1)),tuple((a+2*b)//3 for a,b in zip(p0,p1))]
    px=[]
    for i in range(16):
        r,g,b=cols[(idx>>(2*i))&3]; px.append((r,g,b,al[(ab>>(3*i))&7]))
    return px

def parse_fetch(rw4):
    # find texture fetch constant: dword with type==2 and plausible format right before
    hdr=rw4[:0x1000]
    for o in range(0,0x1000-24,4):
        d0,d1,d2=struct.unpack('>III',hdr[o:o+12])
        if d0&3==2 and (d1&0x3f) in (0x12,0x13,0x14,0x06) and d2:
            w=(d2&0x1FFF)+1; h=((d2>>13)&0x1FFF)+1
            if 1<=w<=4096 and 1<=h<=4096:
                return dict(off=o,tiled=bool(d0>>31),pitch=((d0>>22)&0x1FF)*32,fmt=d1&0x3f,endian=(d1>>6)&3,w=w,h=h)
    return None

def decode(rw4):
    f=parse_fetch(rw4); data=rw4[0x1000:]
    if f['endian']==1: data=swap16(data)
    bw=max(f['pitch'],f['w'])//4; bh=(f['h']+3)//4
    img=Image.new('RGBA',(f['w'],f['h']))
    for by in range(bh):
        for bx in range((f['w']+3)//4):
            i=tiled_block_index(bx,by,bw,4) if f['tiled'] else by*bw+bx
            blk=data[i*16:i*16+16]
            if len(blk)<16: continue
            for k,p in enumerate(dxt5_block(blk)):
                x=bx*4+k%4; y=by*4+k//4
                if x<f['w'] and y<f['h']: img.putpixel((x,y),p)
    return img,f

def _565(c):
    r,g,b=c[:3]; return ((r*31+127)//255)<<11 | ((g*63+127)//255)<<5 | ((b*31+127)//255)

def dxt5_encode_block(px):
    # alpha
    al=[p[3] for p in px]; a0,a1=max(al),min(al)
    if a0==a1:
        ablk=bytes([a0,a1])+bytes(6)
    else:
        pal=[a0,a1]+[((7-i)*a0+i*a1)//7 for i in range(1,7)]
        bits=0
        for i,a in enumerate(al):
            k=min(range(8),key=lambda j:abs(pal[j]-a)); bits|=k<<(3*i)
        ablk=bytes([a0,a1])+bits.to_bytes(6,'little')
    # colour: endpoints = brightest/darkest of visible pixels
    vis=[p for p in px if p[3]>0] or px
    lum=lambda p:p[0]*3+p[1]*6+p[2]
    hi=max(vis,key=lum); lo=min(vis,key=lum)
    c0,c1=_565(hi),_565(lo)
    if c0<c1: c0,c1,hi,lo=c1,c0,lo,hi
    if c0==c1:
        cblk=struct.pack('<HH',c0,c1)+bytes(4)
    else:
        p0,p1=c565(c0),c565(c1)
        pal=[p0,p1,tuple((2*a+b)//3 for a,b in zip(p0,p1)),tuple((a+2*b)//3 for a,b in zip(p0,p1))]
        bits=0
        for i,p in enumerate(px):
            k=min(range(4),key=lambda j:sum((pal[j][t]-p[t])**2 for t in range(3))); bits|=k<<(2*i)
        cblk=struct.pack('<HH',c0,c1)+bits.to_bytes(4,'little')
    return ablk+cblk

def encode_into(rw4, img):
    """Writes img (RGBA, same size as the texture) into an RW4 blob, returns new blob."""
    f=parse_fetch(rw4); assert img.size==(f['w'],f['h']) and f['fmt']==0x14
    data=bytearray(swap16(rw4[0x1000:]) if f['endian']==1 else rw4[0x1000:])
    bw=max(f['pitch'],f['w'])//4
    for by in range((f['h']+3)//4):
        for bx in range((f['w']+3)//4):
            px=[]
            for k in range(16):
                x=min(bx*4+k%4,f['w']-1); y=min(by*4+k//4,f['h']-1); px.append(img.getpixel((x,y)))
            i=tiled_block_index(bx,by,bw,4) if f['tiled'] else by*bw+bx
            data[i*16:i*16+16]=dxt5_encode_block(px)
    data=swap16(bytes(data)) if f['endian']==1 else bytes(data)
    return rw4[:0x1000]+data
