#!/usr/bin/env python3
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ICONS = [
    ("res/icon/icon-64px.png", "data/icon_64.bin"),
    ("res/icon/icon-32px.png", "data/icon_32.bin"),
]


def decode_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s: not a PNG" % path)

    pos, idat, plte, trns, ihdr = 8, b"", None, None, None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"PLTE":
            plte = chunk
        elif kind == b"tRNS":
            trns = chunk
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break

    width, height, depth, color, _, _, interlace = ihdr
    if depth != 8 or interlace != 0:
        raise SystemExit("%s: only 8-bit non-interlaced PNGs are supported" % path)

    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    stride = width * bpp
    raw = zlib.decompress(idat)

    rows = bytearray(stride * height)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                c = prev[i - bpp] if i >= bpp else 0
                b = prev[i]
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif f != 0:
            raise SystemExit("%s: bad row filter %d" % (path, f))
        rows[y * stride:(y + 1) * stride] = line
        prev = line

    pixels = []
    for i in range(width * height):
        s = rows[i * bpp:(i + 1) * bpp]
        if color == 6:
            r, g, b, a = s
        elif color == 2:
            r, g, b = s
            a = 255
        elif color == 0:
            r = g = b = s[0]
            a = 255
        elif color == 4:
            r = g = b = s[0]
            a = s[1]
        else:
            idx = s[0]
            r, g, b = plte[idx * 3:idx * 3 + 3]
            a = trns[idx] if trns and idx < len(trns) else 255
        pixels.append((r, g, b, a))
    return width, height, pixels


def build_blob(width, height, pixels):
    palette, lookup, indices = [], {}, bytearray()
    for px in pixels:
        i = lookup.get(px)
        if i is None:
            i = lookup[px] = len(palette)
            if i > 255:
                raise SystemExit("more than 256 distinct colours")
            palette.append(px)
        indices.append(i)

    blob = bytearray(struct.pack(">HHHH", width, height, len(palette), 0))
    for r, g, b, a in palette:
        blob += struct.pack(">I", (a << 24) | (b << 16) | (g << 8) | r)
    return bytes(blob + indices)


def main():
    for src, dst in ICONS:
        src_path, dst_path = os.path.join(ROOT, src), os.path.join(ROOT, dst)
        width, height, pixels = decode_png(src_path)
        blob = build_blob(width, height, pixels)
        with open(dst_path, "wb") as f:
            f.write(blob)
        print("%s -> %s (%dx%d, %d bytes)" % (src, dst, width, height, len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
