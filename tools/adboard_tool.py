"""FIFA 09 stadium ad boards (data3.big: data/sceneassets/adboard/adboard_N.rx2).

Each one is a RefPack-compressed RW4 texture: 512x1024 DXT1, Xbox 360 tiled,
with 8 smaller mip levels. The 1024 rows hold 16 board strips of 512x64.

  python adboard_tool.py topng  <adboard.rx2> <out.png>
  python adboard_tool.py fromng <original adboard.rx2> <edited.png> <out.rx2>
"""
import io, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, 'nobk_source', 'tools'))
import refpack, x360tex2
from x360tex import swap16, tiled_block_index
from PIL import Image

# (width, height, byte offset, pitch in blocks, block x, block y) for every level we write
LEVELS = [(512, 1024, 0x00000, 128, 0, 0), (256, 512, 0x40000, 64, 0, 0), (128, 256, 0x50000, 32, 0, 0),
          (64, 128, 0x54000, 32, 0, 0), (32, 64, 0x56000, 32, 0, 0),
          (16, 32, 0x58000, 32, 4, 0), (8, 16, 0x58000, 32, 2, 0), (4, 8, 0x58000, 32, 1, 0)]

def load(path):
    d = open(path, 'rb').read()
    return refpack.decompress(d)[0] if d[:2] in (b'\x10\xfb', b'\x90\xfb') else d

def to_png(rx2, png):
    im, f = x360tex2.decode(load(rx2))
    assert (f['fmt'], f['w'], f['h']) == (18, 512, 1024), f
    im.convert('RGB').save(png)

def dxt1_blocks(im):
    b = io.BytesIO()
    im.convert('RGB').save(b, 'DDS', pixel_format='DXT1')
    raw = b.getvalue()[128:]
    return [raw[i:i + 8] for i in range(0, len(raw), 8)]

def from_png(orig_rx2, png, out_rx2):
    d = bytearray(load(orig_rx2))
    hdr, data = d[:0x1000], bytearray(swap16(bytes(d[0x1000:])))
    src = Image.open(png).convert('RGB')
    if src.size != (512, 1024):
        src = src.resize((512, 1024), Image.LANCZOS)
    for w, h, off, pitch, ox, oy in LEVELS:
        lvl = src if w == 512 else src.resize((w, h), Image.BOX)
        blocks = dxt1_blocks(lvl)
        bw = w // 4
        for by in range(h // 4):
            for bx in range(bw):
                i = tiled_block_index(ox + bx, oy + by, pitch, 3)
                data[off + i * 8:off + i * 8 + 8] = blocks[by * bw + bx]
    out = bytes(hdr) + swap16(bytes(data))
    open(out_rx2, 'wb').write(refpack.compress_literal(out))

if __name__ == '__main__':
    if sys.argv[1] == 'topng':
        to_png(sys.argv[2], sys.argv[3])
    else:
        from_png(sys.argv[2], sys.argv[3], sys.argv[4])
