"""Xbox 360 RW4 textures: DXT1 (fmt 18), DXT3 (19), DXT5 (20); tiled; decode + encode."""
import struct, sys
sys.path.insert(0, __file__.rsplit('/',1)[0] + '/nobk_source/tools')
from x360tex import tiled_block_index, swap16, c565, parse_fetch, dxt5_block
from PIL import Image

BLOCK = {18: 8, 19: 16, 20: 16}

def color_block(blk, dxt1):
    c0, c1 = struct.unpack('<HH', blk[0:4]); idx = int.from_bytes(blk[4:8], 'little')
    p0, p1 = c565(c0), c565(c1)
    if dxt1 and c0 <= c1:
        cols = [p0 + (255,), p1 + (255,), tuple((a + b) // 2 for a, b in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
    else:
        cols = [p0 + (255,), p1 + (255,), tuple((2 * a + b) // 3 for a, b in zip(p0, p1)) + (255,),
                tuple((a + 2 * b) // 3 for a, b in zip(p0, p1)) + (255,)]
    return [cols[(idx >> (2 * i)) & 3] for i in range(16)]

def block_pixels(fmt, blk):
    if fmt == 18:
        return color_block(blk, True)
    if fmt == 19:
        a = int.from_bytes(blk[0:8], 'little')
        c = color_block(blk[8:16], False)
        return [(r, g, b, ((a >> (4 * i)) & 15) * 17) for i, (r, g, b, _) in enumerate(c)]
    return dxt5_block(blk)

def decode(rw4):
    f = parse_fetch(rw4); data = rw4[0x1000:]
    if f['endian'] == 1: data = swap16(data)
    bs = BLOCK[f['fmt']]; log_bpp = 3 if bs == 8 else 4
    bw = max(f['pitch'], f['w']) // 4; bh = (f['h'] + 3) // 4
    img = Image.new('RGBA', (f['w'], f['h']))
    px = img.load()
    for by in range(bh):
        for bx in range((f['w'] + 3) // 4):
            i = tiled_block_index(bx, by, bw, log_bpp) if f['tiled'] else by * bw + bx
            blk = data[i * bs:i * bs + bs]
            if len(blk) < bs: continue
            for k, p in enumerate(block_pixels(f['fmt'], blk)):
                x = bx * 4 + k % 4; y = by * 4 + k // 4
                if x < f['w'] and y < f['h']: px[x, y] = p
    return img, f
