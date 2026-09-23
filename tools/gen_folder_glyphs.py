#!/usr/bin/env python3
"""Generate the folder glyph catalogue (docs/MENU.md).

A folder's icon is a hexagon with a glyph in the middle, and the glyph comes
from Material Design Icons (https://pictogrammers.com/library/mdi/). MDI is a
font AND a set of SVG paths from the same drawings, which is what makes the
portal's preview honest: the page draws the SVG path, the watch draws the font
glyph, and both are the same shape.

This script is the only place the catalogue is written down. From CATALOGUE
it produces three files that must never disagree:

    components/aos_ui/aos_folder_font.c     the glyphs as an LVGL font,
                                            GLYPH_PX tall, 4 bpp
    components/aos_ui/aos_folder_glyphs.c   name -> code point, for menu.txt
    components/aos_web/glifos.js            name, group and SVG path, for /menu

and, from SETTINGS, the Settings app's small font and its header:

    components/aos_ui/aos_settings_font.c
    components/aos_ui/include/aos_settings_glyphs.h

The names are what menu.txt stores, so they are MDI's own and are never
renamed: removing one from CATALOGUE makes folders that use it fall back to
the first glyph, adding one is free. The order is the order the page shows.

MDI is released under the Pictogrammers Free License, which allows use in any
project; the icons derived from Google's are Apache 2.0.

Requirements: lv_font_conv (npm install -g lv_font_conv). The MDI files are
downloaded once, pinned to MDI_VERSION, into ~/.cache/amoledos-mdi.

Usage:
    python3 tools/gen_folder_glyphs.py
"""

import json
import os
import re
import subprocess
import sys
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MDI_VERSION = "7.4.47"
CACHE = os.path.expanduser("~/.cache/amoledos-mdi/" + MDI_VERSION)

# The font is drawn once per folder into an image and scaled down from here
# (aos_folder_icon.c): half the hexagon's height is the glyph, and the largest
# hexagon is the grid's 82 px, so 42 covers it with a little to spare.
GLYPH_PX = 42

# (group key, [MDI names]). The group keys are the page's dictionary keys.
CATALOGUE = [
    ("g_general", ["folder", "apps", "star", "heart", "bell", "lock", "shield",
                   "briefcase", "cart", "cash"]),
    ("g_juegos",  ["gamepad-variant", "controller-classic", "cards-playing", "dice-5",
                   "puzzle", "trophy", "sword-cross", "ghost", "chess-knight"]),
    ("g_media",   ["music", "headphones", "camera", "image", "video", "movie-open",
                   "microphone", "palette"]),
    ("g_herram",  ["tools", "wrench", "calculator", "cog", "flashlight", "ruler",
                   "code-braces", "console", "flask"]),
    ("g_tiempo",  ["clock-outline", "timer-outline", "calendar", "alarm"]),
    ("g_salud",   ["run", "dumbbell", "food-apple", "bed"]),
    ("g_red",     ["wifi", "bluetooth", "earth", "remote", "access-point",
                   "message"]),
    ("g_casa",    ["home-variant", "lightbulb", "thermometer",
                   "weather-partly-cloudy", "leaf", "flower"]),
    ("g_varios",  ["book-open-variant", "school", "chart-line", "rocket-launch",
                   "robot", "cat", "paw", "car", "airplane", "bike"]),
]

# The Settings app's icons (aos_app_settings.c): one small font from the same
# MDI release, so the rows, the quick tiles and the folders share one visual
# language. Each becomes a UTF-8 string macro, AOS_SG_<NAME>, in
# components/aos_ui/include/aos_settings_glyphs.h.
SETTINGS_PX = 24
SETTINGS = ["wifi", "bluetooth", "flashlight", "watch-variant", "leaf",
            "moon-waning-crescent", "usb", "brightness-6", "volume-high",
            "bell-outline", "view-grid-outline", "clock-outline", "translate",
            "battery-heart-variant", "gesture-tap", "information-outline",
            "chart-box-outline", "white-balance-sunny", "chevron-right",
            "chevron-left", "restart",
            # the control centre (aos_control.c), v0.5.1
            "cellphone", "lightning-bolt", "skip-previous", "play", "pause",
            "skip-next", "cog", "music-note"]

FILES = {
    "mdi.ttf":   "https://cdn.jsdelivr.net/npm/@mdi/font@%s/fonts/materialdesignicons-webfont.ttf",
    "meta.json": "https://cdn.jsdelivr.net/npm/@mdi/svg@%s/meta.json",
    "mdi.js":    "https://cdn.jsdelivr.net/npm/@mdi/js@%s/mdi.js",
}


def fetch():
    os.makedirs(CACHE, exist_ok=True)
    for name, url in FILES.items():
        path = os.path.join(CACHE, name)
        if not os.path.exists(path):
            print("downloading", name)
            urllib.request.urlretrieve(url % MDI_VERSION, path)
    return {n: os.path.join(CACHE, n) for n in FILES}


def camel(name):
    return "mdi" + "".join(p[:1].upper() + p[1:] for p in name.split("-"))


def utf8_escape(cp):
    return "".join("\\x%02X" % b for b in chr(cp).encode("utf-8"))


def settings_font(paths, meta):
    for name in SETTINGS:
        if name not in meta:
            sys.exit("%s is not an MDI %s icon" % (name, MDI_VERSION))
    cps = [int(meta[n]["codepoint"], 16) for n in SETTINGS]
    font_c = os.path.join(REPO, "components", "aos_ui", "aos_settings_font.c")
    subprocess.run(["lv_font_conv", "--no-compress", "--no-prefilter", "--bpp", "4",
                    "--size", str(SETTINGS_PX), "--font", paths["mdi.ttf"],
                    "-r", ",".join("0x%X" % c for c in cps),
                    "--format", "lvgl", "--lv-font-name", "aos_settings_font",
                    "--force-fast-kern-format", "-o", font_c], check=True)
    src = open(font_c).read()
    src = re.sub(r'#ifdef LV_LVGL_H_INCLUDE_SIMPLE.*?#endif\n', '#include "lvgl.h"\n', src,
                 count=1, flags=re.S)
    src = src.replace(paths["mdi.ttf"], "materialdesignicons-webfont.ttf %s" % MDI_VERSION)
    src = src.replace(font_c, "components/aos_ui/aos_settings_font.c")
    open(font_c, "w").write(src)

    out = ["/* Generated by tools/gen_folder_glyphs.py from Material Design Icons %s." % MDI_VERSION,
           " * Do not edit: change SETTINGS there and run it again.",
           " *",
           " * Each glyph as a UTF-8 string, for a label whose font is aos_settings_font",
           " * (%d px). */" % SETTINGS_PX,
           "#pragma once", "", '#include "lvgl.h"', "",
           "LV_FONT_DECLARE(aos_settings_font);", ""]
    for name, cp in zip(SETTINGS, cps):
        out.append('#define AOS_SG_%-22s "%s"' % (name.upper().replace("-", "_"), utf8_escape(cp)))
    out.append("")
    open(os.path.join(REPO, "components", "aos_ui", "include", "aos_settings_glyphs.h"), "w").write("\n".join(out))
    return len(SETTINGS)


def main():
    paths = fetch()
    meta = {m["name"]: m for m in json.load(open(paths["meta.json"]))}
    js = open(paths["mdi.js"]).read()
    svg = dict(re.findall(r'export var (mdi\w+) = "([^"]+)";', js))

    glyphs = []
    seen = set()
    for group, names in CATALOGUE:
        for name in names:
            if name in seen:
                sys.exit("%s listed twice" % name)
            seen.add(name)
            if name not in meta:
                sys.exit("%s is not an MDI %s icon" % (name, MDI_VERSION))
            d = svg.get(camel(name))
            if not d:
                sys.exit("%s has no SVG path in @mdi/js" % name)
            glyphs.append((group, name, int(meta[name]["codepoint"], 16), d))

    # 1. The font.
    font_c = os.path.join(REPO, "components", "aos_ui", "aos_folder_font.c")
    ranges = ",".join("0x%X" % g[2] for g in glyphs)
    subprocess.run(["lv_font_conv", "--no-compress", "--no-prefilter", "--bpp", "4",
                    "--size", str(GLYPH_PX), "--font", paths["mdi.ttf"], "-r", ranges,
                    "--format", "lvgl", "--lv-font-name", "aos_folder_font",
                    "--force-fast-kern-format", "-o", font_c], check=True)
    src = open(font_c).read()
    # lv_font_conv includes "lvgl/lvgl.h" when it cannot find lvgl.h; the
    # firmware's other fonts include plain lvgl.h.
    src = re.sub(r'#ifdef LV_LVGL_H_INCLUDE_SIMPLE.*?#endif\n', '#include "lvgl.h"\n', src,
                 count=1, flags=re.S)
    # The Opts line would carry this machine's paths.
    src = src.replace(paths["mdi.ttf"], "materialdesignicons-webfont.ttf %s" % MDI_VERSION)
    src = src.replace(font_c, "components/aos_ui/aos_folder_font.c")
    open(font_c, "w").write(src)

    # 2. The name table.
    out = ["/* Generated by tools/gen_folder_glyphs.py from Material Design Icons %s." % MDI_VERSION,
           " * Do not edit: change CATALOGUE there and run it again. */",
           '#include "aos_folder_icon.h"', "",
           "const aos_folder_glyph_t aos_folder_glyphs[] = {"]
    for group, name, cp, _ in glyphs:
        out.append('    { "%s", 0x%X },' % (name, cp))
    out += ["};", "",
            "const int aos_folder_glyph_count = sizeof(aos_folder_glyphs) / sizeof(aos_folder_glyphs[0]);",
            ""]
    open(os.path.join(REPO, "components", "aos_ui", "aos_folder_glyphs.c"), "w").write("\n".join(out))

    # 3. The page's copy, same order.
    items = ",\n".join('  {n:"%s",g:"%s",d:"%s"}' % (name, group, d) for group, name, _, d in glyphs)
    js_out = ("/* Generado por tools/gen_folder_glyphs.py desde Material Design Icons %s\n"
              "   (Pictogrammers Free License). No editar a mano. */\n"
              "window.AOS_GLIFOS = [\n%s\n];\n" % (MDI_VERSION, items))
    open(os.path.join(REPO, "components", "aos_web", "glifos.js"), "w").write(js_out)

    n = settings_font(paths, meta)
    print("%d glyphs, font %d KB of source; %d Settings glyphs" %
          (len(glyphs), os.path.getsize(font_c) // 1024, n))


if __name__ == "__main__":
    main()
