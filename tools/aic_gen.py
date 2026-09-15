#!/usr/bin/env python3
"""Generates components/aos_ui/aos_icon_tables.c from the simulator's icon dump.

    cd sim && AOS_SIM_ICONDUMP=/tmp/icons.txt ./build/amoledos_sim
    python3 tools/aic_gen.py /tmp/icons.txt          # writes the C file
    python3 tools/aic_gen.py /tmp/icons.txt --check  # only reports

The dump has every LVGL object each built-in icon's switch case created, at
66, 74 and 82 px (the launcher's three sizes). For every value - a width, an
offset, a radius, a border - this finds the percent p with `size * p / 100`,
or the divisor n with `size / n`, that gives the SAME pixels at all three
sizes, and writes the AIC ops. So the tables are exact by construction where
it matters, and anything it cannot express is reported instead of guessed.

Why not transcribe the C by hand: 36 cases, 1271 lines, and the interesting
bugs would be the ones a diff would not catch - a 40 that should be 44.
"""

import re
import sys
import os

SIZES = (66, 74, 82)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "components", "aos_ui", "include", "aos_app.h")
OUT = os.path.join(ROOT, "components", "aos_ui", "aos_icon_tables.c")

PALETTE = {"FFFFFF": "AIC_C_TEXT", "000000": "AIC_C_BG", "1C1C1E": "AIC_C_CARD",
           "2C2C2E": "AIC_C_CARD2", "8E8E93": "AIC_C_DIM", "0A84FF": "AIC_C_ACCENT",
           "30D158": "AIC_C_GREEN", "FF453A": "AIC_C_RED", "FF9F0A": "AIC_C_ORANGE",
           "FFD60A": "AIC_C_YELLOW", "BF5AF2": "AIC_C_PURPLE", "FF375F": "AIC_C_PINK",
           "40C8E0": "AIC_C_TEAL"}
ALIGNS = ["AIC_CENTER", "AIC_TOP_MID", "AIC_BOTTOM_MID", "AIC_LEFT_MID", "AIC_RIGHT_MID",
          "AIC_TOP_LEFT", "AIC_TOP_RIGHT", "AIC_BOTTOM_LEFT", "AIC_BOTTOM_RIGHT"]
LV_RADIUS_CIRCLE = 0x7FFF
LV_PCT_50 = None   # filled from the dump: lv_pct(50) as LVGL encodes it


def enum_names():
    names = {}
    src = open(HEADER).read()
    body = src[src.index("AOS_ICON_NONE = 0"):src.index("} aos_icon_id_t")]
    i = 0
    for line in body.splitlines():
        m = re.match(r"\s*(AOS_ICON_[A-Z_0-9]+)", line)
        if m:
            names[i] = m.group(1)
            i += 1
    return names


def parse(path):
    """{icon_id: {size: [obj, ...]}}; obj = dict of fields, 'kids' nested."""
    icons = {}
    cur = None
    stack = []
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("icon "):
            f = dict(kv.split("=", 1) for kv in line[5:].split())
            cur = icons.setdefault(int(f["id"]), {})[int(f["size"])] = []
            stack = [cur]
            continue
        if not line.startswith("o "):
            continue
        # text= is last and may contain spaces
        text = None
        if " text=" in line:
            line, text = line.split(" text=", 1)
        o = {}
        for kv in line[2:].split():
            k, v = kv.split("=", 1)
            o[k] = v
        for k in list(o):
            if k not in ("class", "font"):
                o[k] = int(o[k], 16) if k in ("bg", "bc", "gc", "ct", "ci") else int(o[k])
        o["text"] = text
        o["kids"] = []
        depth = o["depth"]
        del stack[depth + 1:]
        stack[depth].append(o)
        stack.append(o["kids"])
    return icons


class Unsolvable(Exception):
    pass


APPROX = []     # (what, wanted, got, encoding) for the report


def best_fit(values, candidates, what):
    """The candidate with the smallest worst-case error, then the smallest
    total error. Recorded in APPROX so the report says exactly where the
    tables are not the old drawing to the pixel."""
    best = None
    for enc, got in candidates:
        errs = [abs(g - v) for g, v in zip(got, values)]
        key = (max(errs), sum(errs))
        if best is None or key < best[0]:
            best = (key, enc, got)
    APPROX.append((what, list(values), list(best[2]), best[1]))
    return best[1]


def solve_dim(values, what):
    """percent p or divisor n reproducing values[] at SIZES; 0 stays 0."""
    if all(v == 0 for v in values):
        return "0"
    for p in range(1, 128):
        if all(max(1, s * p // 100) == v for s, v in zip(SIZES, values)):
            return str(p)
    for n in range(1, 129):
        if all(max(1, s // n) == v for s, v in zip(SIZES, values)):
            return "AIC_DIV(%d)" % n
    cands = [(str(p), [max(1, s * p // 100) for s in SIZES]) for p in range(1, 128)]
    cands += [("AIC_DIV(%d)" % n, [max(1, s // n) for s in SIZES]) for n in range(1, 129)]
    return best_fit(values, cands, what)


def solve_radius(values, what):
    if all(v == LV_RADIUS_CIRCLE for v in values):
        return "AIC_CIRCLE"
    if all(v == 0 for v in values):
        return "0"
    for p in range(1, 128):
        if all(s * p // 100 == v for s, v in zip(SIZES, values)):
            return str(p)
    for n in range(1, 129):
        if all(s // n == v for s, v in zip(SIZES, values)):
            return "AIC_DIV(%d)" % n
    cands = [(str(p), [s * p // 100 for s in SIZES]) for p in range(1, 128)]
    cands += [("AIC_DIV(%d)" % n, [s // n for s in SIZES]) for n in range(1, 129)]
    return best_fit(values, cands, what)


def solve_offset(values, what, exact=True):
    """percent p, possibly negative, with C truncation toward zero. Several
    p can give the same three pixels (-1 and 0 both truncate to nothing);
    the one nearest zero is the canonical choice."""
    for p in sorted(range(-127, 128), key=abs):
        if all(int(s * p / 100) == v for s, v in zip(SIZES, values)):
            return str(p)
    if exact:
        raise Unsolvable("%s: %s" % (what, values))
    return best_fit(values, [(str(p), [int(s * p / 100) for s in SIZES]) for p in range(-127, 128)], what)


def base_pos(align, pw, ph, w, h):
    """lv_area_align(): each half truncated on its own, pw/2 - w/2."""
    cx, cy = pw // 2 - w // 2, ph // 2 - h // 2
    return {"AIC_TOP_LEFT": (0, 0), "AIC_TOP_MID": (cx, 0), "AIC_TOP_RIGHT": (pw - w, 0),
            "AIC_BOTTOM_LEFT": (0, ph - h), "AIC_BOTTOM_MID": (cx, ph - h),
            "AIC_BOTTOM_RIGHT": (pw - w, ph - h), "AIC_LEFT_MID": (0, cy),
            "AIC_RIGHT_MID": (pw - w, cy), "AIC_CENTER": (cx, cy)}[align]


def solve_align(objs, parents, what):
    """objs[i] at SIZES[i] inside parents[i] (w,h): (align, dx, dy). Exact
    with some alignment if possible; otherwise CENTER with the best fit."""
    for align in ALIGNS:
        try:
            dx = [o["x"] - base_pos(align, pw, ph, o["w"], o["h"])[0] for o, (pw, ph) in zip(objs, parents)]
            dy = [o["y"] - base_pos(align, pw, ph, o["w"], o["h"])[1] for o, (pw, ph) in zip(objs, parents)]
            return align, solve_offset(dx, what + " dx"), solve_offset(dy, what + " dy")
        except Unsolvable:
            continue
    align = "AIC_CENTER"
    dx = [o["x"] - base_pos(align, pw, ph, o["w"], o["h"])[0] for o, (pw, ph) in zip(objs, parents)]
    dy = [o["y"] - base_pos(align, pw, ph, o["w"], o["h"])[1] for o, (pw, ph) in zip(objs, parents)]
    return align, solve_offset(dx, what + " dx", exact=False), solve_offset(dy, what + " dy", exact=False)


def color(hexval):
    key = "%06X" % hexval
    return PALETTE.get(key, "AIC_C_LIT(0x%s)" % key)


def is_hand(o):
    """aos_hand_create: an obj with its pivot at (w/2, h) and radius w/2."""
    return (o["class"] == "obj" and o["h"] > 0 and o["w"] > 0
            and o["px"] == o["w"] // 2 and o["py"] == o["h"] and o["r"] == o["w"] // 2)


def same_shape(trio):
    a = trio[0]
    return all(o["class"] == a["class"] and len(o["kids"]) == len(a["kids"]) for o in trio)


def emit_objs(trios, parents, what, out, problems):
    """trios: list over children of [obj@66, obj@74, obj@82]."""
    for idx, trio in enumerate(trios):
        tag = "%s/%d" % (what, idx)
        if not same_shape(trio):
            problems.append("%s: shapes differ across sizes" % tag)
            continue
        a = trio[0]
        try:
            if a["class"] == "label":
                text = a["text"].encode("utf-8")
                font = "AIC_FONT_TITLE" if a["font"] == "title" else "AIC_FONT_BODY"
                out.append("    AIC_TEXT(%s, %d), %s," % (font, len(text), ", ".join("0x%02X" % b for b in text)))
            elif a["class"] == "arc":
                align, dx, dy = solve_align(trio, parents, tag)
                d = solve_dim([o["w"] for o in trio], tag + " d")
                wt = solve_dim([o["wt"] for o in trio], tag + " track width")
                wi = solve_dim([o["wi"] for o in trio], tag + " ind width")
                for k in ("bs", "be", "is", "ie", "arot", "ot", "oi", "ct", "ci"):
                    if len(set(o[k] for o in trio)) != 1:
                        raise Unsolvable("%s: %s varies with size" % (tag, k))
                out.append("    AIC_ARC(%s, %s, %s, %s, %s, %s, %d, %d, %d, %d, %d, %s, %d, %s, %d),"
                           % (align, dx, dy, d, wt, wi, a["bs"], a["be"], a["is"], a["ie"], a["arot"],
                              color(a["ct"]), a["ot"], color(a["ci"]), a["oi"]))
            elif is_hand(a):
                w = solve_dim([o["w"] for o in trio], tag + " hand w")
                ln = solve_dim([o["h"] for o in trio], tag + " hand len")
                if len(set(o["rot"] for o in trio)) != 1:
                    raise Unsolvable("%s: hand angle varies" % tag)
                # the hand's own alignment is CENTER (0, -len/2): check it
                for o, (pw, ph) in zip(trio, parents):
                    bx, by = base_pos("AIC_CENTER", pw, ph, o["w"], o["h"])
                    if o["x"] != bx or o["y"] != by + int(-o["h"] / 2):
                        raise Unsolvable("%s: hand not at the helper's position" % tag)
                out.append("    AIC_HAND(%s, %s, %d, %s)," % (w, ln, a["rot"], color(a["bg"])))
            else:
                align, dx, dy = solve_align(trio, parents, tag)
                w = solve_dim([o["w"] for o in trio], tag + " w")
                h = solve_dim([o["h"] for o in trio], tag + " h")
                r = solve_radius([o["r"] for o in trio], tag + " radius")
                for k in ("opa", "bg", "bo", "bc", "gd", "gc", "rot"):
                    if len(set(o[k] for o in trio)) != 1:
                        raise Unsolvable("%s: %s varies with size" % (tag, k))
                out.append("    AIC_RECT(%s, %s, %s, %s, %s, %s, %s, %d),"
                           % (align, dx, dy, w, h, r, color(a["bg"]), a["opa"]))
                if a["bw"] or any(o["bw"] for o in trio):
                    bw = solve_dim([o["bw"] for o in trio], tag + " border")
                    out.append("    AIC_BORDER(%s, %s, %d)," % (bw, color(a["bc"]), a["bo"]))
                if a["gd"]:
                    out.append("    AIC_GRAD(%s, %d)," % (color(a["gc"]), a["gd"]))
                if a["rot"]:
                    if not all(o["px"] == LV_PCT_50 and o["py"] == LV_PCT_50 for o in trio):
                        raise Unsolvable("%s: rotation with a pivot that is not 50%%" % tag)
                    out.append("    AIC_ROT(%d)," % a["rot"])
        except Unsolvable as e:
            problems.append(str(e))
            continue
        if a["kids"]:
            out.append("    AIC_INTO,")
            # children align to the parent's CONTENT area, which in LVGL is
            # the box minus padding minus border: a bordered body shrinks the
            # frame its children are placed in by the border on every side.
            emit_objs(list(zip(*[o["kids"] for o in trio])),
                      [(o["w"] - 2 * o["bw"], o["h"] - 2 * o["bw"]) for o in trio], tag, out, problems)
            out.append("    AIC_OUT,")


def main(argv):
    global LV_PCT_50
    if len(argv) < 2:
        print(__doc__)
        return 2
    check = "--check" in argv
    icons = parse(argv[1])
    names = enum_names()
    # lv_pct(50): LVGL encodes it as LV_COORD_SET_PCT(50); read it off any rotated object
    for per_size in icons.values():
        for objs in per_size.values():
            def walk(os_):
                for o in os_:
                    if o["rot"] and o["class"] == "obj" and o["px"] == o["py"] and o["px"] > 1000:
                        return o["px"]
                    r = walk(o["kids"])
                    if r: return r
            v = walk(objs)
            if v:
                LV_PCT_50 = v
    lines = ["/*",
             " * AmoledOS - the firmware's icons as AIC tables.",
             " *",
             " * GENERATED by tools/aic_gen.py from the simulator's AOS_SIM_ICONDUMP of the",
             " * switch that used to draw them (docs/ICONS.md, F4). Every value was solved",
             " * against 66, 74 and 82 px, so these are the old drawings pixel for pixel at",
             " * the launcher's sizes. Edit an icon here by hand if you like - the macros",
             " * are meant to be read - but then it is no longer the generator's.",
             " */",
             "#include \"aos_icon_ops.h\"",
             ""]
    problems = []
    total = 0
    biggest = (0, "")
    for icon_id in sorted(icons):
        name = names.get(icon_id, "AOS_ICON_%d" % icon_id)
        short = name.replace("AOS_ICON_", "")
        per_size = icons[icon_id]
        if sorted(per_size) != list(SIZES):
            problems.append("%s: sizes missing in the dump" % name)
            continue
        out = []
        trios = list(zip(*[per_size[s] for s in SIZES]))
        emit_objs(trios, [(s, s) for s in SIZES], short, out, problems)
        lines.append("static const uint8_t ICON_%s[] = {" % short)
        lines.append("    AIC_HEADER,")
        lines.extend(out)
        lines.append("    AIC_END")
        lines.append("};")
        lines.append("")
        # size estimate: count bytes by evaluating the macros roughly
        total += 1
    lines.append("const aos_icon_table_t aos_icon_tables[AOS_ICON_COUNT] = {")
    for icon_id in sorted(icons):
        name = names.get(icon_id, "AOS_ICON_%d" % icon_id)
        short = name.replace("AOS_ICON_", "")
        lines.append("    [%s] = { ICON_%s, sizeof ICON_%s }," % (name, short, short))
    lines.append("};")
    lines.append("")

    for p in problems:
        print("PROBLEM:", p)
    by_icon = {}
    for what, wanted, got, enc in APPROX:
        by_icon.setdefault(what.split("/")[0], []).append((what, wanted, got, enc))
    for icon, items in sorted(by_icon.items()):
        print("APPROX %s: %d value(s) within %d px" % (icon, len(items),
              max(max(abs(g - w) for g, w in zip(got, wanted)) for _, wanted, got, _ in items)))
        for what, wanted, got, enc in items:
            print("    %-28s wanted %-14s got %-14s as %s" % (what, wanted, got, enc))
    print("%d icons, %d problem(s), %d approximated value(s) in %d icon(s)"
          % (total, len(problems), len(APPROX), len(by_icon)))
    if by_icon:
        # before the " */" that closes the header comment
        lines.insert(8, " *\n * Not every value fits: %d of them, in %s,\n"
                        " * are the nearest percent or divisor, within a pixel or two at one of the\n"
                        " * three sizes. The generator's report lists each one."
                        % (len(APPROX), ", ".join(sorted(by_icon))))
    if problems:
        return 1
    if not check:
        open(OUT, "w").write("\n".join(lines))
        print("wrote", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
