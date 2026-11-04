#!/usr/bin/env python3
# Genera font8x14.h (ASCII 32..126, 8x14, 1 byte/fila) a partir de DejaVu Sans Mono.
from PIL import Image, ImageDraw, ImageFont
f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 13)
W, H = 8, 14
rows = []
for c in range(32, 127):
    im = Image.new("L", (W, H), 0)
    ImageDraw.Draw(im).text((0, 11), chr(c), font=f, fill=255, anchor="ls")
    for y in range(H):
        b = 0
        for x in range(W):
            if im.getpixel((x, y)) > 110: b |= 0x80 >> x
        rows.append(b)
with open("font8x14.h", "w") as o:
    o.write("/* generado por genfont.py */\nstatic const unsigned char kFont[95*14] = {\n")
    for i in range(0, len(rows), 14):
        o.write("  " + ",".join("0x%02X" % r for r in rows[i:i+14]) + ",\n")
    o.write("};\n")
for ch in "Az0":
    i = (ord(ch) - 32) * 14
    print(ch); print("\n".join("".join("#" if rows[i+y] & (0x80 >> x) else "." for x in range(8)) for y in range(14)))
