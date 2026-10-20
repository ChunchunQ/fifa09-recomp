import struct,sys
img=open('xex/image.bin','rb').read();base=0x82000000
def fstart(a):
    o=a-base
    while o>0:
        w=struct.unpack_from('>I',img,o)[0]
        if w==0x7d8802a6: return base+o
        o-=4
for a in sys.argv[1:]: print(a, hex(fstart(int(a,16))))
