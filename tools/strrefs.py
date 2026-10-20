import struct,sys
d=open('/home/claude/re/image.bin','rb').read()
B=0x82000000
def s_at(a):
    o=a-B
    if not (0<=o<len(d)): return None
    e=o
    while e<len(d) and 32<=d[e]<127 and e-o<120: e+=1
    if e-o>=4 and d[e]==0: return d[o:e].decode()
def scan(lo,hi):
    regs={}
    out=[]
    for o in range(lo-B,hi-B,4):
        w=struct.unpack_from('>I',d,o)[0]
        op=w>>26
        if op==15 and ((w>>16)&31)==0:
            regs[(w>>21)&31]=(w&0xffff)<<16
        elif op==14:
            ra=(w>>16)&31
            if ra in regs:
                v=(regs[ra]+((w&0xffff)^0x8000)-0x8000)&0xffffffff
                s=s_at(v)
                if s: out.append((B+o,s))
    return out
for a,s in scan(int(sys.argv[1],16),int(sys.argv[2],16)): print(hex(a),repr(s))
