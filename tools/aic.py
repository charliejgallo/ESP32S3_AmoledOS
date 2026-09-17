#!/usr/bin/env python3
"""Tools for AIC icon blobs (components/aos_ui/include/aos_icon_ops.h).

    python3 tools/aic.py asm   icon.txt out.aic     # the macros -> the blob
    python3 tools/aic.py lint  demo.topos.aic        # parse, size cap, palette, ROT warning
    python3 tools/aic.py dump  demo.topos.aic        # one line per op, as the C macros
    python3 tools/aic.py selftest                    # asm(dump(x)) == x over the tree
    python3 tools/aic.py halves shot.ppm [x_split]   # icontest: left column == right column?
    python3 tools/aic.py golden shot.ppm out.gz       # keep the LEFT half (the old switch's pixels)
    python3 tools/aic.py cmp shot.ppm golden.gz       # the RIGHT half of a new shot vs that golden

'golden' and 'cmp' exist because the switch that drew the icons is gone (F4):
the left halves of the last bench run with it are kept in tools/icon_golden/,
gzipped, and any later change to the interpreter or the tables is checked
against them with tools/icon_bench.sh --golden.

'asm' is what makes an icon a FILE. Until it existed a blob could only be
written as C macros inside an app, compiled in, and there was no way for
somebody with a Lua script -or with no toolchain at all- to give their app an
icon, even though the firmware has read .aic files from the card since v0.3.8.
It takes the same macros the header documents, so an icon already written in
C is assembled by pointing this at the .c file, and it accepts what 'dump'
prints as well: the two are inverses, which is what 'selftest' checks.

'halves' is the gate of docs/ICONS.md phase F1: the simulator's
AOS_SIM_VIEW=icontest draws the same icon by switch case (left) and by blob
(right) at 66, 74 and 82 px, AOS_SIM_SHOT dumps the screen to PPM, and this
compares pixel (x, y) with (x + split, y) for the whole left half. Zero
differing pixels means the interpreter reproduces the case exactly.

A negative width, height, radius, border or diameter is 'size / n' (AIC_DIV);
offsets are always percent.

No dependencies, like the rest of tools/.
"""

import struct
import sys

OPS = {
    0x00: ("END", ""),
    0x01: ("RECT", "align i8 i8 i8 i8 radius color u8"),
    0x02: ("RING", "i8 i8 color u8"),
    0x03: ("ARC", "align i8 i8 i8 i8 i8 i16 i16 i16 i16 i16 color u8 color u8"),
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
FONTS = ["BODY", "TITLE"]
GRADS = {"VER": 1, "HOR": 2}
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
            if font >= len(FONTS):
                raise Bad(op_at, f"font {font} is not a font")
            fields = [("font", FONTS[font]),
                      ("text", '"%s"' % blob[at:at + n].decode("utf-8", "replace"))]
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
                    if kind == "radius":
                        # On the wire it is a byte, but radius_px() in
                        # aos_icon.c reads 255 as LV_RADIUS_CIRCLE and
                        # everything else back as int8_t: positive is a
                        # percent, negative is size/-p. Printing it unsigned
                        # showed AIC_DIV(38) as 218.
                        v = "CIRCLE" if v == 255 else (v - 256 if v > 127 else v)
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
    print(f"{path}: ok, {len(blob)} bytes, {shapes} shape{'' if shapes == 1 else 's'}")
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


# ---------------------------------------------------------------------------
# asm: the macros -> the blob
#
# The input is the same thing the header documents and an app writes in its
# source, so a .c file with an icon in it is valid input as it stands: the
# wrapper (`static const uint8_t X[] = {` ... `};`), the comments and the
# commas are all skipped. The prefixes are optional, because 'dump' prints
# without them - AIC_CENTER and CENTER, AIC_C_TEXT and TEXT, AIC_C_LIT(0x..)
# and LIT(0x..) are the same token. That is what makes the two commands
# inverses, which 'selftest' then checks over everything in the tree.
# ---------------------------------------------------------------------------

import ast
import os
import re

ALIGN_BY_NAME = {v: k for k, v in ALIGN.items()}

# Per macro: the opcode and one letter per argument.
#   a align   b i8   u u8   w i16   c colour   f font   g gradient direction
#   r radius (a number, or CIRCLE)
ASM = {
    "RECT":   (0x01, "abbbbrcu"),
    "RING":   (0x02, "bbcu"),
    "ARC":    (0x03, "abbbbbwwwwwcucu"),
    "HAND":   (0x04, "bbwc"),
    "ROT":    (0x06, "w"),
    "BORDER": (0x07, "bcu"),
    "GRAD":   (0x08, "cg"),
    "INTO":   (0x09, ""),
    "OUT":    (0x0A, ""),
    "END":    (0x00, ""),
}


class AsmError(Exception):
    pass


def _number(tok):
    """An integer, possibly written as arithmetic. No names, no calls."""
    try:
        tree = ast.parse(tok, mode="eval")
    except SyntaxError:
        raise AsmError(f"{tok!r} is not a number")
    allowed = (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Constant,
               ast.Add, ast.Sub, ast.Mult, ast.FloorDiv, ast.Div, ast.USub, ast.UAdd)
    for node in ast.walk(tree):
        if not isinstance(node, allowed):
            raise AsmError(f"{tok!r} is not a number")
        if isinstance(node, ast.Constant) and not isinstance(node.value, int):
            raise AsmError(f"{tok!r} is not a number")
    return int(eval(compile(tree, "<aic>", "eval")))      # nosec: walked above


def _strip_prefix(tok):
    for pre in ("AIC_C_", "AIC_FONT_", "AIC_GRAD_", "AIC_"):
        if tok.startswith(pre):
            return tok[len(pre):]
    return tok


def _colour(tok, out):
    """A palette name, an index, or a literal. Writes 1 or 4 bytes."""
    bare = _strip_prefix(tok).strip()
    lit = re.fullmatch(r"LIT\s*\(\s*([^)]+?)\s*\)", bare)
    if lit:
        rgb = _number(lit.group(1))
        if not 0 <= rgb <= 0xFFFFFF:
            raise AsmError(f"literal colour {tok} is not 0x000000..0xFFFFFF")
        out += [0xFF, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF]
        return
    if bare in PALETTE:
        out.append(PALETTE.index(bare))
        return
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", bare):
        # A name that is not in the palette. Saying "not a number" here, which
        # is what fell out of the number path, sent you looking in the wrong
        # place for a typo.
        raise AsmError(f"{tok} is not a palette colour. They are "
                       f"{', '.join(PALETTE)}, or LIT(0xRRGGBB)")
    idx = _number(bare)
    if not 0 <= idx < len(PALETTE):
        raise AsmError(f"palette index {idx} is out of 0..{len(PALETTE) - 1}; "
                       f"the names are {', '.join(PALETTE)}")
    out.append(idx)


def _arg(kind, tok, out):
    bare = _strip_prefix(tok).strip()
    if kind == "c":
        _colour(tok, out)
        return
    if kind == "a":
        if bare in ALIGN_BY_NAME:
            out.append(ALIGN_BY_NAME[bare])
            return
        v = _number(bare)
        if v not in ALIGN:
            raise AsmError(f"align {tok} is not an lv_align_t")
        out.append(v)
        return
    if kind == "f":
        if bare in FONTS:
            out.append(FONTS.index(bare))
            return
        v = _number(bare)
        if not 0 <= v < len(FONTS):
            raise AsmError(f"font {tok} is not BODY or TITLE")
        out.append(v)
        return
    if kind == "g":
        if bare in GRADS:
            out.append(GRADS[bare])
            return
        v = _number(bare)
        if v not in GRADS.values():
            raise AsmError(f"gradient direction {tok} is not VER or HOR")
        out.append(v)
        return
    if kind == "r":
        if bare == "CIRCLE":
            out.append(0xFF)
            return
        div = re.fullmatch(r"DIV\s*\(\s*([^)]+?)\s*\)", bare)
        v = -_number(div.group(1)) if div else _number(bare)
        if not -128 <= v <= 127:
            raise AsmError(f"radius {v} is out of -128..127 (CIRCLE is the "
                           f"only other value, and DIV(n) is -n)")
        out.append(v & 0xFF)
        return

    # DIV(n) is the header's way of saying "size / n", which is a negative
    # dimension. It is only ever a dimension, so it only reaches i8.
    div = re.fullmatch(r"DIV\s*\(\s*([^)]+?)\s*\)", bare)
    v = -_number(div.group(1)) if div else _number(bare)

    if kind == "b":
        if not -128 <= v <= 127:
            raise AsmError(f"{v} does not fit in a signed byte")
        out.append(v & 0xFF)
    elif kind == "u":
        if not 0 <= v <= 255:
            raise AsmError(f"{v} is out of 0..255")
        out.append(v)
    elif kind == "w":
        if not -32768 <= v <= 32767:
            raise AsmError(f"{v} does not fit in an int16")
        out += [v & 0xFF, (v >> 8) & 0xFF]
    else:
        raise AsmError(f"internal: unknown field kind {kind!r}")


def _split_args(text):
    """Commas at depth zero, so LIT(0x102030) stays one argument."""
    args, depth, cur, quoted = [], 0, "", False
    for ch in text:
        if ch == '"':
            quoted = not quoted
        if not quoted:
            if ch in "([":
                depth += 1
            elif ch in ")]":
                depth -= 1
            elif ch == "," and depth == 0:
                args.append(cur.strip())
                cur = ""
                continue
        cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


def assemble(text, source="<input>"):
    # The C wrapper and the comments, gone. A block comment can hold anything,
    # including something that looks like a macro, so it goes first.
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r"^.*?\{", "", text, count=1, flags=re.S) if "=" in text.split("AIC_HEADER")[0] else text
    text = text.replace("};", " ").strip()

    out = []
    at = 0
    n = len(text)
    while at < n:
        ch = text[at]
        if ch in ", \t\r\n":
            at += 1
            continue

        word = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[at:])
        if not word:
            # A bare number: the raw UTF-8 bytes that follow AIC_TEXT in C.
            num = re.match(r"[-+]?(0[xX][0-9a-fA-F]+|\d+)", text[at:])
            if not num:
                raise AsmError(f"{source}: cannot read {text[at:at + 20]!r}")
            v = _number(num.group(0))
            if not 0 <= v <= 255:
                raise AsmError(f"{source}: raw byte {v} is out of 0..255")
            out.append(v)
            at += num.end()
            continue

        name = word.group(0)
        at += word.end()
        args_text = ""
        if at < n and text[at] == "(":
            depth, start = 0, at
            while at < n:
                if text[at] == "(":
                    depth += 1
                elif text[at] == ")":
                    depth -= 1
                    if depth == 0:
                        at += 1
                        break
                at += 1
            else:
                raise AsmError(f"{source}: {name}( is never closed")
            args_text = text[start + 1:at - 1]

        bare = name[4:] if name.startswith("AIC_") else name
        if bare == "HEADER":
            out += [ord("A"), ord("I"), ord("C"), 1]
            continue
        if bare == "TEXT":
            args = _split_args(args_text)
            if len(args) != 2:
                raise AsmError(f"{source}: AIC_TEXT takes a font and a string "
                               f"(or a byte count, with the bytes after it)")
            out.append(0x05)
            _arg("f", args[0], out)
            if args[1].startswith('"') and args[1].endswith('"') and len(args[1]) >= 2:
                raw = args[1][1:-1].encode("utf-8")
                if not 1 <= len(raw) <= 15:
                    raise AsmError(f"{source}: the text is {len(raw)} bytes, "
                                   f"the format allows 1..15")
                out.append(len(raw))
                out += list(raw)
            else:
                _arg("u", args[1], out)     # the C form: the bytes follow
            continue
        if bare not in ASM:
            raise AsmError(f"{source}: {name} is not an AIC macro")

        op, layout = ASM[bare]
        args = _split_args(args_text)
        if len(args) != len(layout):
            raise AsmError(f"{source}: AIC_{bare} takes {len(layout)} arguments, "
                           f"got {len(args)}")
        out.append(op)
        for kind, tok in zip(layout, args):
            try:
                _arg(kind, tok, out)
            except AsmError as e:
                raise AsmError(f"{source}: in AIC_{bare}: {e}")

    if not out:
        raise AsmError(f"{source}: nothing to assemble")
    if out[:4] != [ord("A"), ord("I"), ord("C"), 1]:
        out = [ord("A"), ord("I"), ord("C"), 1] + out      # AIC_HEADER is implied
    if out[-1] != 0x00:
        out.append(0x00)                                    # and so is AIC_END
    blob = bytes(out)

    # Assembled is not the same as valid: the reader is the authority, and it
    # is right here. A blob that does not parse never reaches a file.
    try:
        list(parse(blob))
    except Bad as e:
        raise AsmError(f"{source}: assembles to something the reader refuses "
                       f"at byte {e.args[0]}: {e.args[1]}")
    return blob


def cmd_asm(src, out_path=None):
    text = open(src, encoding="utf-8").read()
    try:
        blob = assemble(text, src)
    except AsmError as e:
        print(e)
        return 1
    if out_path is None:
        # foo.aic.txt -> foo.aic, and anything else -> its own name with the
        # extension swapped. The double extension is worth having: it says
        # which file is the source and which one the watch reads.
        out_path = src[:-4] if src.endswith(".aic.txt") else \
                   os.path.splitext(src)[0] + ".aic"
    with open(out_path, "wb") as f:
        f.write(blob)
    shapes = sum(1 for _, name, _ in parse(blob)
                 if name in ("RECT", "RING", "ARC", "HAND", "TEXT"))
    print(f"{out_path}: {len(blob)} bytes, {shapes} shape{'' if shapes == 1 else 's'}")
    return 0


def cmd_selftest(root=None):
    """asm(dump(x)) == x for every blob in the tree, and the same for the
    icons written as C macros inside the apps. It is the only check that
    matters: the two commands have to be inverses, or an icon edited through
    them comes out different from the one that went in."""
    root = root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cases = []
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs
                   if d not in ("build", "build_nommu", "managed_components", ".git")]
        for name in files:
            path = os.path.join(base, name)
            rel = os.path.relpath(path, root)
            if name.endswith(".aic"):
                cases.append((rel, open(path, "rb").read()))
            elif name.endswith(".c"):
                text = open(path, encoding="utf-8", errors="replace").read()
                if "AIC_HEADER" not in text:
                    continue
                for n, piece in enumerate(text.split("AIC_HEADER")[1:]):
                    end = piece.find("};")
                    if end < 0:
                        continue
                    try:
                        cases.append((f"{rel}[{n}]",
                                      assemble("AIC_HEADER" + piece[:end])))
                    except AsmError as e:
                        print(f"  FAIL {rel}[{n}]: {e}")
                        return 1

    bad = 0
    for label, blob in cases:
        lines = []
        try:
            for _, name, fields in parse(blob):
                lines.append(fmt(name, fields))
        except Bad as e:
            print(f"  FAIL {label}: does not parse at byte {e.args[0]}: {e.args[1]}")
            bad += 1
            continue
        try:
            again = assemble(",\n".join(lines), label)
        except AsmError as e:
            print(f"  FAIL {label}: {e}")
            bad += 1
            continue
        if again != blob:
            print(f"  FAIL {label}: {len(blob)} bytes in, {len(again)} out")
            print(f"        in  {blob.hex()}")
            print(f"        out {again.hex()}")
            bad += 1
        else:
            print(f"  ok   {label}  ({len(blob)} bytes)")

    print(f"{len(cases)} blobs, {bad} failing")
    return 1 if bad else 0


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


def half(px, w, h, split, right):
    out = bytearray()
    for y in range(h):
        row = y * w * 3
        x0 = split if right else 0
        out += px[row + x0 * 3: row + (x0 + split) * 3]
    return bytes(out)


def cmd_golden(path, out):
    import gzip
    w, h, px = read_ppm(path)
    split = w // 2
    with gzip.open(out, "wb") as f:
        f.write(("%d %d\n" % (split, h)).encode())
        f.write(half(px, w, h, split, False))
    print(f"{out}: left half {split}x{h} kept")
    return 0


def cmd_cmp(path, golden):
    import gzip
    w, h, px = read_ppm(path)
    split = w // 2
    with gzip.open(golden, "rb") as f:
        head = f.readline().split()
        gw, gh = int(head[0]), int(head[1])
        gpx = f.read()
    if (gw, gh) != (split, h):
        print(f"{golden}: {gw}x{gh} does not match the shot's half {split}x{h}")
        return 2
    mine = half(px, w, h, split, True)
    diff = sum(1 for i in range(0, len(mine), 3) if mine[i:i + 3] != gpx[i:i + 3])
    print(f"{path} vs {golden}: {diff} differing pixel(s)")
    return 1 if diff else 0


def main(argv):
    if len(argv) < 2 or (len(argv) < 3 and argv[1] != "selftest"):
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]
    if cmd == "asm":
        return cmd_asm(*args)
    if cmd == "lint":
        return cmd_lint(args[0])
    if cmd == "dump":
        return cmd_dump(args[0])
    if cmd == "halves":
        return cmd_halves(*args)
    if cmd == "golden":
        return cmd_golden(*args)
    if cmd == "cmp":
        return cmd_cmp(*args)
    if cmd == "selftest":
        return cmd_selftest(*args)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
