import struct,sys
img=open('xex/image.bin','rb').read();base=0x82000000
targets={int(a,16) for a in sys.argv[1:]}
for o in range(0x2E0000,0x12EA8AC,4):
    w=struct.unpack_from('>I',img,o)[0]
    if w>>26==18 and w&1:
        li=w&0x03fffffc
        if li&0x02000000: li-=0x04000000
        t=base+o+li
        if t in targets: print(hex(t),'<-',hex(base+o))
