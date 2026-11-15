#!/usr/bin/env python3
"""Cuts the fixtures of page_detector_host.cpp out of upright 1280 x 800 screenshots of the Key 61's primary plane (mpc-tools/shot.sh): the left strip (x 0..63, y 100..163: the markers and the
instance picture of the Play tab) and the right strip (x 1000..1279, all rows: the host's Q-Link panel when it is drawn). usage: make_qlink_panel_fixtures.py OUT name=file.png ..."""
import struct, sys
from PIL import Image
def rle(img, box):
    c = img.crop(box).convert("RGB"); w, h = c.size; px = list(c.getdata()); out = bytearray(); i = 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 65535: j += 1
        out += struct.pack("<H", j - i) + bytes(px[i]); i = j
    return struct.pack("<HHHHI", box[0], box[1], w, h, len(out)) + bytes(out)
out = bytearray()
for a in sys.argv[2:]:
    name, path = a.split("=")
    img = Image.open(path)
    out += struct.pack("<B", len(name)) + name.encode() + rle(img, (0, 100, 64, 164)) + rle(img, (1000, 0, 1280, 800))
open(sys.argv[1], "wb").write(out)
