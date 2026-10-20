"""Decode a FIFA 09 (X360) localisation .db (eng_us.db) to {key: text}."""
import struct, sys, json

def load(path):
    d = open(path, 'rb').read()
    n = struct.unpack('>H', d[52:54])[0]
    R = 120; S = R + n * 16
    tree = d[S:S + 692]
    nodes = [(tree[i:i + 2], tree[i + 2:i + 4]) for i in range(0, 692, 4)]
    def dec(p, lenbytes):
        L = d[S + p] if lenbytes == 1 else struct.unpack('>H', d[S + p:S + p + 2])[0]
        base = S + p + lenbytes; out = bytearray(); node = 0; bit = 0
        while len(out) < L:
            b = (d[base + bit // 8] >> (7 - bit % 8)) & 1; bit += 1
            half = nodes[node][b]
            if half[0] == 0: out.append(half[1]); node = 0
            else: node = half[0]
        return out.decode('utf-8', 'replace')
    res = {}
    for i in range(n):
        k, t, h, _ = struct.unpack('>4I', d[R + 16 * i:R + 16 * i + 16])
        try: res[dec(k, 1)] = dec(t, 2)
        except Exception: pass
    return res

if __name__ == '__main__':
    m = load(sys.argv[1])
    json.dump(m, open(sys.argv[2], 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print(len(m))
