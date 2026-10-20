import sys
def decompress(src):
    p=0
    hdr=(src[0]<<8)|src[1]; p=2
    big = hdr & 0x8000  # 4-byte sizes when set (0x90FB)
    if hdr & 0x0100:  # compressed size present
        p += 4 if big else 3
    n = 4 if big else 3
    size=int.from_bytes(src[p:p+n],'big'); p+=n
    out=bytearray()
    while p < len(src):
        b0=src[p]
        if b0 < 0x80:
            b1=src[p+1]; p+=2
            lit=b0&3; ln=((b0&0x1c)>>2)+3; off=((b0&0x60)<<3)+b1+1
        elif b0 < 0xC0:
            b1,b2=src[p+1],src[p+2]; p+=3
            lit=(b1>>6)&3; ln=(b0&0x3f)+4; off=((b1&0x3f)<<8)+b2+1
        elif b0 < 0xE0:
            b1,b2,b3=src[p+1],src[p+2],src[p+3]; p+=4
            lit=b0&3; ln=((b0&0x0c)<<6)+b3+5; off=((b0&0x10)<<12)+(b1<<8)+b2+1
        elif b0 < 0xFC:
            p+=1; lit=((b0&0x1f)<<2)+4; out+=src[p:p+lit]; p+=lit; continue
        else:
            p+=1; lit=b0&3; out+=src[p:p+lit]; p+=lit; break
        out+=src[p:p+lit]; p+=lit
        for _ in range(ln): out.append(out[-off])
    return bytes(out), size
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read()
    o,s=decompress(d); print('declared',s,'got',len(o)); open(sys.argv[2],'wb').write(o)

def compress_literal(data):
    """Valid RefPack stream using only literal blocks (no real compression)."""
    n=len(data)
    out=bytearray(b'\x10\xfb')+n.to_bytes(3,'big') if n < (1<<24) else bytearray(b'\x90\xfb')+n.to_bytes(4,'big')
    p=0
    while n-p >= 4:
        chunk=min(112,(n-p)//4*4)
        out.append(0xE0+(chunk-4)//4); out+=data[p:p+chunk]; p+=chunk
    rest=n-p
    out.append(0xFC+rest); out+=data[p:]
    return bytes(out)
