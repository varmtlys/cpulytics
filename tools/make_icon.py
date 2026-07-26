"""Draws res/cpulytics.ico: the tray icon and the icon of the executable.

The image is a cpu package with three load bars. Everything is rasterised here so
the icon can be regenerated instead of being an opaque binary in the repository.

    python tools/make_icon.py
"""

import os
import struct
import zlib

SS = 4  # supersampling per axis

BODY = (0x22, 0x2E, 0x3C)  # dark slate
EDGE = (0x3F, 0xB8, 0xAF)  # teal
PIN = (0x9A, 0xB0, 0xC4)   # grey blue
BARS = ((0x29, 0x34, (0x3F, 0xB8, 0xAF)), (0x455, 0x50, (0xF7, 0xC5, 0x59)), (0x62, 0x66, (0xFF, 0x6B, 0x5B)))

# Sizes stored in the file. The two large ones are PNG compressed, which is what
# every icon since vista does: a 256 px bitmap entry alone would be 256 KB.
SIZES = [16, 24, 32, 48, 64, 128, 256]
PNG_FROM = 128


def rounded(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def sample(x, y):
    """Colour at normalised coordinates, alpha 0 outside the icon."""
    for i in range(3):  # pins on all four sides
        p = 0.30 + i * 0.20
        if (0.08 <= x <= 0.20 or 0.80 <= x <= 0.92) and p - 0.045 <= y <= p + 0.045:
            return PIN + (255,)
        if (0.08 <= y <= 0.20 or 0.80 <= y <= 0.92) and p - 0.045 <= x <= p + 0.045:
            return PIN + (255,)
    if rounded(x, y, 0.16, 0.16, 0.84, 0.84, 0.10):
        if not rounded(x, y, 0.20, 0.20, 0.80, 0.80, 0.075):
            return EDGE + (255,)
        for bx, h, col in ((0.29, 0.34, BARS[0][2]), (0.455, 0.50, BARS[1][2]), (0.62, 0.66, BARS[2][2])):
            if bx <= x <= bx + 0.09 and (0.72 - h) <= y <= 0.72:
                return col + (255,)
        return BODY + (255,)
    return (0, 0, 0, 0)


def render(n):
    rows = []
    for py in range(n):
        row = []
        for px in range(n):
            acc = [0, 0, 0, 0]
            for sy in range(SS):
                for sx in range(SS):
                    r, g, b, a = sample((px + (sx + 0.5) / SS) / n, (py + (sy + 0.5) / SS) / n)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            alpha = acc[3] / (SS * SS)
            if alpha < 0.5:
                row.append((0, 0, 0, 0))
            else:
                row.append((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]), int(round(alpha))))
        rows.append(row)
    return rows


def bitmap_entry(rows, n):
    """BITMAPINFOHEADER, bottom up BGRA pixels, then the (unused) AND mask."""
    header = struct.pack('<IiiHHIIiiII', 40, n, n * 2, 1, 32, 0, n * n * 4, 0, 0, 0, 0)
    data = bytearray()
    for py in range(n - 1, -1, -1):
        for px in range(n):
            r, g, b, a = rows[py][px]
            data += bytes((b, g, r, a))
    mask = bytes(((n + 31) // 32) * 4 * n)
    return header + bytes(data) + mask


def png_entry(rows, n):
    raw = b''.join(b'\x00' + b''.join(bytes(rows[y][x]) for x in range(n)) for y in range(n))

    def chunk(tag, payload):
        return (struct.pack('>I', len(payload)) + tag + payload +
                struct.pack('>I', zlib.crc32(tag + payload) & 0xFFFFFFFF))

    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', n, n, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) +
            chunk(b'IEND', b''))


def main():
    images = []
    for n in SIZES:
        rows = render(n)
        images.append((n, png_entry(rows, n) if n >= PNG_FROM else bitmap_entry(rows, n)))

    out = bytearray(struct.pack('<HHH', 0, 1, len(images)))  # ICONDIR, type 1 = icon
    offset = 6 + 16 * len(images)
    for n, blob in images:
        out += struct.pack('<BBBBHHII', n if n < 256 else 0, n if n < 256 else 0, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    for _, blob in images:
        out += blob

    path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'res', 'cpulytics.ico')
    with open(path, 'wb') as f:
        f.write(bytes(out))
    print('%s: %d bytes, sizes %s' % (path, len(out), ', '.join(str(n) for n in SIZES)))


if __name__ == '__main__':
    main()
