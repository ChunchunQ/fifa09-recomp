import struct,sys
img=open('xex/image.bin','rb').read();base=0x82000000
for tag in sys.argv[1:]:
    v=struct.unpack('>I',tag.encode())[0]
    hi=v>>16; lo=v&0xffff
    hi_adj=((v+0x8000)>>16)&0xffff
    res=[]
    for o in range(0x2E0000,0x12EA8AC,4):
        w=struct.unpack_from('>I',img,o)[0]
        if w>>26==15 and (w&0xffff) in (hi,hi_adj) and ((w>>16)&31)==0:
            rd=(w>>21)&31
            for k in range(1,6):
                p=struct.unpack_from('>I',img,o+4*k)[0]
                op=p>>26
                if op in (24,14) and (p&0xffff)==(lo if op==24 else (lo if lo<0x8000 else lo)) and ((p>>16)&31 if op==14 else (p>>21)&31)==rd:
                    res.append(hex(base+o));break
    print(tag,res)
