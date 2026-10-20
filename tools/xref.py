import struct,sys
img=open('xex/image.bin','rb').read(); base=0x82000000
lo,hi=0x822E0000-base,0x832EA8AC-base
def refs(target):
    hi16=(target+0x8000)>>16; lo16=target&0xffff
    out=[]
    for o in range(lo,hi,4):
        w=struct.unpack_from('>I',img,o)[0]
        op=w>>26
        if op in (14,24,32,34,36,40,42,44,48):  # addi, ori, lwz, lbz, stw, lhz,...
            imm=w&0xffff; ra=(w>>16)&31
            if imm!=lo16: continue
            for k in range(1,12):
                p=struct.unpack_from('>I',img,o-4*k)[0]
                if p>>26==15 and ((p>>16)&31)==0 and (p&0xffff)==hi16 and ((p>>21)&31)==ra:
                    out.append(base+o); break
    return out
for a in sys.argv[1:]:
    t=int(a,16); print(a,[hex(x) for x in refs(t)])
