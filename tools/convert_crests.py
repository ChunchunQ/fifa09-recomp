"""Team crests: fifa09_crests.zip (made by collect-crests.ps1) -> teams/<id>.png

  python convert_crests.py <extracted zip folder> <out folder>

Reads big/data1/data/ui/artassets/teamlogoslarge/t<id>.big: RefPack-compressed
BIGF archive whose RW4 entry is a 152x152 DXT5 Xbox 360 texture (tiled,
16-bit endian swap). Uses x360tex.py + refpack.py from this folder."""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refpack, x360tex

src = os.path.join(sys.argv[1], "big", "data1", "data", "ui", "artassets", "teamlogoslarge")
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
ok = 0
for fn in os.listdir(src):
    m = re.match(r"t(\d+)\.big$", fn)
    if not m:
        continue
    d = open(os.path.join(src, fn), "rb").read()
    if d[:2] == b"\x10\xfb":
        d = refpack.decompress(d)[0]
    n = struct.unpack(">I", d[8:12])[0]
    pos, rw4 = 16, None
    for _ in range(n):
        o, s = struct.unpack(">II", d[pos:pos + 8])
        pos = d.index(b"\0", pos + 8) + 1
        if d[o:o + 4] == b"\x89RW4":
            rw4 = d[o:o + s]
    if rw4:
        im, _ = x360tex.decode(rw4)
        if im.getbbox():
            im.save(os.path.join(out, m.group(1) + ".png"), optimize=True)
            ok += 1
print(ok, "crests")
