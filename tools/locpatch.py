"""Replace text in the FIFA 09 (X360) localisation DBs inside data/loc/locale.big.

  python3 locpatch.py <locale.big in> <locale.big out>

Each <lang>.db is an EA DB with one table of (key, text, hash, 0) records;
key/text are Huffman-coded strings (tree at the start of the string section).
Changed texts are encoded with the same tree and appended to the string
section; the record's text offset is re-pointed. Header sizes and the
CRC-32/MPEG-2 checksums (header, table header, table data) are recomputed.
The changed DBs are appended to the BIG archive and their entries re-pointed.
"""
import re
import struct
import sys

REPLACE = [
    (re.compile(r"Xbox\s+LIVE", re.I), "NOBK.Online"),
    # main menu item "NEWS AND RECENT OFFERS" -> quits the game (hook Ui_QuitFromNews)
    (re.compile(r"^News\s+and\s+Recent\s+Offers$", re.I), "Quit to Desktop"),
    # the same menu item reads "Sign in to EA" when the game is not signed in
    (re.compile(r"^Sign\s+in\s+to\s+EA$", re.I), "Quit to Desktop"),
]

_T = []
for _i in range(256):
    _c = _i << 24
    for _ in range(8):
        _c = ((_c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if _c & 0x80000000 else (_c << 1) & 0xFFFFFFFF
    _T.append(_c)


def crc(b):
    c = 0xFFFFFFFF
    for x in b:
        c = ((c << 8) & 0xFFFFFFFF) ^ _T[(c >> 24) ^ x]
    return c


def align(n, a):
    return (n + a - 1) // a * a


def patch_db(d):
    d = bytearray(d)
    assert d[:4] == b"DB\x00\x08" and struct.unpack(">I", d[16:20])[0] == 1
    t = 36
    recsize, = struct.unpack(">I", d[t + 4:t + 8])
    strsize, = struct.unpack(">I", d[t + 12:t + 16])
    nrec, = struct.unpack(">H", d[t + 16:t + 18])
    nf = d[t + 24]
    R = t + 36 + nf * 16
    S = R + nrec * recsize
    assert recsize == 16 and struct.unpack(">I", d[-4:])[0] == crc(d[72:-4])
    # Huffman tree: nodes of two 2-byte halves; half = (0, byte) leaf or (node, ?) branch
    nodes = []
    i = S
    while True:
        nodes.append((d[i:i + 2], d[i + 2:i + 4]))
        i += 4
        if len(nodes) * 4 >= 2048 or i >= S + 4 * 512:
            break
    codes = {}

    def walk(node, prefix, depth=0):
        if depth > 64 or node >= len(nodes):
            return
        for b in (0, 1):
            h = nodes[node][b]
            if h[0] == 0:
                codes.setdefault(h[1], prefix + str(b))
            else:
                walk(h[0], prefix + str(b), depth + 1)
    walk(0, "")

    def dec(p, lb):
        L = d[S + p] if lb == 1 else struct.unpack(">H", d[S + p:S + p + 2])[0]
        base = S + p + lb
        out = bytearray()
        node = bit = 0
        while len(out) < L:
            b = (d[base + bit // 8] >> (7 - bit % 8)) & 1
            bit += 1
            h = nodes[node][b]
            if h[0] == 0:
                out.append(h[1])
                node = 0
            else:
                node = h[0]
        return bytes(out)

    def enc(raw):
        bits = "".join(codes[c] for c in raw)
        bits += "0" * (-len(bits) % 8)
        return struct.pack(">H", len(raw)) + int(bits, 2).to_bytes(len(bits) // 8, "big") if bits else struct.pack(">H", 0)

    strings = bytearray(d[S:S + strsize])
    changed = 0
    for r in range(nrec):
        at = R + 16 * r
        to, = struct.unpack(">I", d[at + 4:at + 8])
        if to == 0xFFFFFFFF:
            continue
        txt = dec(to, 2).decode("utf-8")
        new = txt
        for rx, rep in REPLACE:
            new = rx.sub(rep, new)
        if new == txt:
            continue
        if "Quit to Desktop" in new and "Quit to Desktop" not in txt:
            print("  menu text:", repr(txt), "->", repr(new))
        raw = new.encode("utf-8")
        if any(c not in codes for c in raw):
            print("  skip (char not in tree):", txt[:60])
            continue
        off = len(strings)
        strings += enc(raw)
        struct.pack_into(">I", d, at + 4, off)
        changed += 1
    body = bytes(d[72:S]) + bytes(strings)
    body += b"\0" * (align(72 + len(body), 4) - 72 - len(body)) + b"\0" * 4
    out = bytearray(d[:72]) + body
    struct.pack_into(">I", out, t + 12, len(strings))
    out += struct.pack(">I", crc(out[72:]))
    struct.pack_into(">I", out, 8, len(out))
    struct.pack_into(">I", out, 20, crc(out[0:20]))
    struct.pack_into(">I", out, t + 32, crc(out[t:t + 32]))
    # self-check: re-decode every changed record
    return bytes(out), changed


def main(src, dst):
    big = bytearray(open(src, "rb").read())
    n, hl = struct.unpack(">II", big[8:16])
    p = 16
    ents = []
    for _ in range(n):
        e = big.index(b"\0", p + 8)
        ents.append((p, big[p + 8:e].decode()))
        p = e + 1
    for ep, name in ents:
        if not name.endswith(".db"):
            continue
        o, s = struct.unpack(">II", big[ep:ep + 8])
        new, changed = patch_db(big[o:o + s])
        print("%s: %d text(s) changed" % (name, changed))
        if not changed:
            continue
        no = align(len(big), 0x80)
        big += b"\0" * (no - len(big)) + new
        struct.pack_into(">II", big, ep, no, len(new))
    big += b"\0" * (align(len(big), 0x80) - len(big))
    struct.pack_into("<I", big, 4, len(big))
    open(dst, "wb").write(big)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
