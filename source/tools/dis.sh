#!/bin/bash
# dis.sh ADDR COUNT
A=$(( $1 )); N=$2
OFF=$(( A - 0x82000000 ))
HEX=$(python3 -c "import sys;d=open('$(dirname $0)/xex/image.bin','rb').read();print(d[$OFF:$OFF+4*$N].hex())")
$(dirname $0)/cs/build/cstool -s ppc32be "$HEX" $(printf "0x%x" $A) 2>&1 | sed 's/^ *//' | cut -c1-90
