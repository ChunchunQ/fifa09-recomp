"""FIFA 09 (X360) fifa_ng_db.db reader -> {table: [ {field: value} ]} using the meta XML."""
import re, struct, sys

def load_meta(path):
    x = open(path, encoding='utf-8', errors='replace').read()
    meta = {}
    for m in re.finditer(r'<table name="([^"]+)" shortname="([^"]+)".*?</table>', x, re.S):
        fields = {}
        for f in re.finditer(r'<field ([^>]*)/>', m.group(0)):
            a = dict(re.findall(r'(\w+)="([^"]*)"', f.group(1)))
            fields[a['shortname']] = a
        meta[m.group(2)] = (m.group(1), fields)
    return meta

def read(dbpath, metapath, want=None, lsb=False):
    d = open(dbpath, 'rb').read()
    meta = load_meta(metapath)
    n = struct.unpack('>I', d[16:20])[0]
    tabs = [(d[24 + 8 * i:28 + 8 * i].decode(), struct.unpack('>I', d[28 + 8 * i:32 + 8 * i])[0]) for i in range(n)]
    base = 24 + 8 * n + 4
    out = {}
    for sn, off in tabs:
        name, fmeta = meta.get(sn, (sn, {}))
        if want and name not in want:
            continue
        t = base + off
        recsize, = struct.unpack('>I', d[t + 4:t + 8])
        nrec, nvalid = struct.unpack('>HH', d[t + 16:t + 20])
        nf = d[t + 24]
        p = t + 36
        fields = []
        for i in range(nf):
            ftype, bitoff = struct.unpack('>II', d[p:p + 8]); fsn = d[p + 8:p + 12].decode(); depth, = struct.unpack('>I', d[p + 12:p + 16])
            fields.append((ftype, bitoff, fsn, depth)); p += 16
        S = p + nrec * recsize  # compressed strings: Huffman tree, then strings
        def dec(o):
            if o == 0xFFFFFFFF:
                return ''
            L = d[S + o]; b0 = S + o + 1; outb = bytearray(); node = 0; bit = 0
            while len(outb) < L:
                b = (d[b0 + bit // 8] >> (7 - bit % 8)) & 1; bit += 1
                h = d[S + node * 4 + b * 2:S + node * 4 + b * 2 + 2]
                if h[0] == 0:
                    outb.append(h[1]); node = 0
                else:
                    node = h[0]
            return outb.decode('utf-8', 'replace')
        rows = []
        for r in range(nvalid):
            rec = d[p + r * recsize:p + (r + 1) * recsize]
            big = int.from_bytes(rec, 'little' if lsb else 'big')
            total = recsize * 8
            row = {}
            for ftype, bitoff, fsn, depth in fields:
                fm = fmeta.get(fsn, {})
                fname = fm.get('name', fsn)
                if ftype == 0:  # string
                    s = rec[bitoff // 8:bitoff // 8 + depth // 8]
                    row[fname] = s.split(b'\0')[0].decode('utf-8', 'replace')
                elif ftype == 13:  # Huffman string, 32-bit offset
                    row[fname] = dec(struct.unpack('>I', rec[bitoff // 8:bitoff // 8 + 4])[0])
                elif ftype == 4:  # float
                    row[fname] = struct.unpack('>f', rec[bitoff // 8:bitoff // 8 + 4])[0]
                else:
                    if bitoff + depth > total: continue
                    if lsb:
                        v = (big >> bitoff) & ((1 << depth) - 1)
                    else:
                        v = (big >> (total - bitoff - depth)) & ((1 << depth) - 1)
                    row[fname] = v + int(fm.get('rangelow', 0) or 0)
            rows.append(row)
        out[name] = {'fields': fields, 'rows': rows}
    return out
