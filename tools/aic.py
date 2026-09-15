#!/usr/bin/env python3
"""Tools for AIC icon blobs (components/aos_ui/include/aos_icon_ops.h).

    python3 tools/aic.py lint  demo.topos.aic        # parse, size cap, palette, ROT warning
    python3 tools/aic.py dump  demo.topos.aic        # one line per op, as the C macros
    python3 tools/aic.py halves shot.ppm [x_split]   # icontest: left column == right column?

'halves' is the gate of docs/ICONS.md phase F1: the simulator's
AOS_SIM_VIEW=icontest draws the same icon by switch case (left) and by blob
(right) at 66, 74 and 82 px, AOS_SIM_SHOT dumps the screen to PPM, and this
compares pixel (x, y) with (x + split, y) for the whole left half. Zero
differing pixels means the interpreter reproduces the case exactly.

No dependencies, like the rest of tools/.
"""

import struct
import sys

OPS = {
    0x00: ("END", ""),
    0x01: ("RECT", "align i8 i8 i8 i8 u8 color u8"),
    0x02: ("RING", "i8 i8 color u8"),
    0x03: ("ARC", "i8 i8 u8 i16 color"),
    0x04: ("HAND", "i8 i8 i16 color"),
    0x05: ("TEXT", "text"),
    0x06: ("ROT", "i16"),
    0x07: ("BORDER", "i8 color u8"),
    0x08: ("GRAD", "color u8"),
    0x09: ("INTO", ""),
    0x0A: ("OUT", ""),
}
PALETTE = ["TEXT", "BG", "CARD", "CARD2", "DIM", "ACCENT", "GREEN", "RED",
           "ORANGE", "YELLOW", "PURPLE", "PINK", "TEAL"]
ALIGN = {0: "DEFAULT", 1: "TOP_LEFT", 2: "TOP_MID", 3: "TOP_RIGHT",
         4: "BOTTOM_LEFT", 5: "BOTTOM_MID", 6: "BOTTOM_RIGHT", 7: "LEFT_MID",
         8: "RIGHT_MID", 9: "CENTER"}
MAX_BYTES = 256
DEPTH = 4


class Bad(Exception):
    pass


def parse(blob):
    """Yields (offset, name, [fields]) per op. Raises Bad at the first fault."""
    if len(blob) > MAX_BYTES:
        raise Bad(0, f"{len(blob)} bytes, the cap is {MAX_BYTES}")
    if len(blob) < 4 or blob[:3] != b"AIC":
        raise Bad(0, "no AIC header")
    if blob[3] != 1:
        raise Bad(3, f"version {blob[3]}, expected 1")
    at = 4
    depth = 0
    while True:
        if at >= len(blob):
            raise Bad(at, "ran off the end without END")
        op_at = at
        op = blob[at]
        at += 1
        if op not in OPS:
            raise Bad(op_at, f"unknown opcode 0x{op:02X}")
        name, layout = OPS[op]
        fields = []
        if layout == "text":
            if at + 2 > len(blob):
                raise Bad(op_at, "truncated TEXT")
            font, n = blob[at], blob[at + 1]
            at += 2
            if n == 0 or n > 15 or at + n > len(blob):
                raise Bad(op_at, f"TEXT length {n} out of 1..15 or truncated")
            fields = [("font", font), ("text", blob[at:at + n].decode("utf-8", "replace"))]
            at += n
        else:
            for kind in layout.split():
                if kind == "color":
                    if at >= len(blob):
                        raise Bad(op_at, f"truncated {name}")
                    idx = blob[at]
                    at += 1
                    if idx == 0xFF:
                        if at + 3 > len(blob):
                            raise Bad(op_at, "truncated literal colour")
                        fields.append(("color", "LIT(0x%02X%02X%02X)" % tuple(blob[at:at + 3])))
                        at += 3
                    elif idx >= len(PALETTE):
                        raise Bad(op_at, f"palette index {idx} out of range")
                    else:
                        fields.append(("color", PALETTE[idx]))
                elif kind == "i16":
                    if at + 2 > len(blob):
                        raise Bad(op_at, f"truncated {name}")
                    fields.append(("i16", struct.unpack_from("<h", blob, at)[0]))
                    at += 2
                else:
                    if at >= len(blob):
                        raise Bad(op_at, f"truncated {name}")
                    v = blob[at]
                    at += 1
                    if kind == "i8":
                        v = v - 256 if v > 127 else v
                    if kind == "align":
                        if v > 9:
                            raise Bad(op_at, f"align {v} is not an lv_align_t")
                        v = ALIGN[v]
                    fields.append((kind, v))
        if name == "INTO":
            depth += 1
            if depth > DEPTH:
                raise Bad(op_at, f"INTO nested deeper than {DEPTH}")
        elif name == "OUT":
            depth -= 1
            if depth < 0:
                raise Bad(op_at, "OUT with nothing to go out of")
        yield op_at, name, fields
        if name == "END":
            return


def fmt(name, fields):
    vals = [str(v) for _, v in fields]
    return f"AIC_{name}({', '.join(vals)})" if vals else f"AIC_{name}"


def cmd_lint(path):
    blob = open(path, "rb").read()
    shapes = rots = 0
    try:
        for _, name, _ in parse(blob):
            if name in ("RECT", "RING", "ARC", "HAND", "TEXT"):
                shapes += 1
            if name == "ROT":
                rots += 1
    except Bad as e:
        print(f"{path}: FAIL at byte {e.args[0]}: {e.args[1]}")
        return 1
    print(f"{path}: ok, {len(blob)} bytes, {shapes} shapes")
    if rots:
        print(f"  warning: {rots} ROT op(s). A rotated object costs LVGL a layer per "
              f"frame, and the launcher redraws every visible icon while scrolling "
              f"(docs/ARCHITECTURE.md). Prefer cardinal shapes.")
    return 0


def cmd_dump(path):
    blob = open(path, "rb").read()
    try:
        for at, name, fields in parse(blob):
            print(f"{at:4d}  {fmt(name, fields)}")
    except Bad as e:
        print(f"{path}: FAIL at byte {e.args[0]}: {e.args[1]}")
        return 1
    return 0


def read_ppm(path):
    data = open(path, "rb").read()
    if data[:2] != b"P6":
        raise SystemExit(f"{path}: not a P6 PPM")
    parts = []
    at = 2
    while len(parts) < 3:
        while data[at:at + 1].isspace():
            at += 1
        start = at
        while not data[at:at + 1].isspace():
            at += 1
        parts.append(int(data[start:at]))
    at += 1
    w, h, _ = parts
    return w, h, data[at:at + w * h * 3]


def cmd_halves(path, split=None):
    w, h, px = read_ppm(path)
    split = int(split) if split else w // 2
    diff = 0
    first = None
    rows = set()
    for y in range(h):
        row = y * w * 3
        for x in range(split):
            a = row + x * 3
            b = row + (x + split) * 3
            if px[a:a + 3] != px[b:b + 3]:
                diff += 1
                rows.add(y)
                if first is None:
                    first = (x, y, px[a:a + 3].hex(), px[b:b + 3].hex())
    print(f"{path}: {w}x{h}, split at x={split}: {diff} differing pixel(s)"
          + (f" across {len(rows)} row(s); first at {first[0]},{first[1]}: "
             f"left {first[2]} right {first[3]}" if diff else ""))
    return 1 if diff else 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]
    if cmd == "lint":
        return cmd_lint(args[0])
    if cmd == "dump":
        return cmd_dump(args[0])
    if cmd == "halves":
        return cmd_halves(*args)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
