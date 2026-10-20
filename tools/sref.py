import struct,sys
d=open('/home/claude/re/image.bin','rb').read()
B=0x82000000
targets={int(a,16):a for a in sys.argv[1:]}
regs={}
for o in range(0x400000,0x1890000,4):
    w=struct.unpack_from('>I',d,o)[0]; op=w>>26
    if op==15 and ((w>>16)&31)==0: regs[(w>>21)&31]=((w&0xffff)<<16,o)
    elif op==14:
        ra=(w>>16)&31
        if ra in regs and o-regs[ra][1]<64:
            v=(regs[ra][0]+((w&0xffff)^0x8000)-0x8000)&0xffffffff
            if v in targets: print(targets[v], hex(B+o))
