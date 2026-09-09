#!/usr/bin/env python3
"""Extract translatable strings and check language packs.

Spanish is the source language and the catalog key is the Spanish string
itself, so there is nothing to invent: this walks the sources for `_("...")`
and for the app names the launcher translates, and writes one template per
catalog.

    python3 tools/gen_lang.py template en      write /lang/en templates, keeping
                                               any translations already there
    python3 tools/gen_lang.py check en         what the pack is missing, and
                                               what it has that the code lost
    python3 tools/gen_lang.py stats            how much of the UI is marked up

Templates go to `sim/sim_fs/lang/<code>/` by default, which is where the
simulator looks; copy that directory to the card's `/lang/` for the board.
Use --out to write somewhere else.

Where a string ends up
----------------------
`_sistema.lang`   everything under components/aos_ui and components/aos_apps,
                  plus EVERY app's `desc.name`, built-in and dynamic alike.
                  App names have to live here: the launcher draws "Gemas"
                  without having opened Gemas, so that app's own catalog is
                  not loaded yet.
<app id>.lang     `_()` inside apps/<dir>/main, filed under the app's
                  `desc.id` ("demo.2043"), which is what the runtime uses to
                  find the catalog.

The escaping
------------
Keys are written exactly as they appear between the quotes in the C source -
`\\n` stays two characters on the page. That is deliberate and it has to match
what the loader does: the C compiler turns `_("Ene\\nFeb")` into a string with
a real newline, and a line-oriented catalog cannot hold one, so the file keeps
the escaped form and `unescape()` in aos_i18n.c undoes it before hashing. Get
this wrong in either direction and multi-line strings miss every lookup in
silence.
"""

import argparse
import os
import re
import sys
from collections import OrderedDict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(REPO, "sim", "sim_fs", "lang")

SYSTEM_CATALOG = "_sistema.lang"
FIRMWARE_DIRS = ["components/aos_ui", "components/aos_apps", "main"]

# `_("...")` and `N_("...")`, allowing the adjacent-literal concatenation C
# does for you:
#   _("Ene\n" "Feb\n")
# N_ is the extract-but-do-not-translate marker for static tables, where a
# function call is not allowed; the matching _() sits at the draw site. Both
# put the same key in the catalog, which is the point.
# The lookbehind keeps it from matching the tail of an identifier like foo_().
MARKED = re.compile(r'(?<![A-Za-z0-9_])N?_\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')

# C_("ctx", "texto") / NC_("ctx", "texto"). The key becomes ctx + 0x04 + texto,
# written into the catalog as the printable escape \x04 so the file stays
# plain ASCII; aos_i18n.c's unescape() turns it back into the byte. This is how
# "MAR" can be TUE as a weekday and MAR as a month in the same catalog.
CONTEXT = re.compile(
    r'(?<![A-Za-z0-9_])N?C_\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*'
    r'((?:"(?:[^"\\]|\\.)*"\s*)+)\)')

# `.id = "aos.timer"` / `app->desc.id = "demo.2043";`
ID_RE = re.compile(r'(?:desc\s*)?\.\s*id\s*=\s*"([^"]+)"')
# ...and the same through a macro, which is how remoto writes it:
#     #define APP_ID "aos.remoto"
#     app->desc.id = APP_ID;
# Without this the app's whole catalog is skipped - with a warning, but
# skipped, which is a quiet way to lose 50 strings.
ID_MACRO_RE = re.compile(r'(?:desc\s*)?\.\s*id\s*=\s*([A-Z_][A-Z0-9_]*)\s*;')
# `.name = "Gemas"` / `app->desc.name = "2043";`
NAME_RE = re.compile(r'(?:desc\s*)?\.\s*name\s*=\s*"([^"]+)"')


def joined_literals(chunk):
    """Raw contents of one or more adjacent string literals, concatenated."""
    return "".join(m.group(1) for m in LITERAL.finditer(chunk))


def strip_comments(text):
    """Blank out C comments, keeping every byte offset where it was.

    Without this the extractor happily collects `_("...")` written inside a
    doc comment - the first run of this script pulled "Ene\\nFeb" out of the
    example in aos_i18n.c and filed it as a real UI string. A phantom entry in
    a catalog is invisible: it translates nothing and never fires.

    Replacing comment bodies with spaces rather than deleting them keeps the
    reported file:line numbers honest. Has to track string and char literals
    too, or the `//` in "http://..." would eat the rest of the line.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "'":
            # Char literal. Its contents are blanked to spaces because a '"'
            # -perfectly normal in a JSON parser- leaves a stray quote that
            # makes the literal regex swallow half the file.
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                if i < n:
                    out[i] = " "
                    i += 1
            i += 1
        elif c == '"':
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for j in range(i, end):
                if out[j] != "\n":
                    out[j] = " "
            i = end
        else:
            i += 1
    return "".join(out)


# Generated sources. They are checked in and they compile, but nothing in them
# was written by hand, so scanning them is at best noise and at worst a lie:
# aos_lang_embedded.c IS the English catalog, so every string in it looks to
# `unmarked` like UI text nobody wrapped. It buried the two real candidates
# under 211 false ones the day it was added.
GENERATED = ("aos_lang_embedded.c", "aos_symbols.c")


def c_sources(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames
                       if d not in ("build", "managed_components")]
        for name in sorted(filenames):
            if name.endswith(".c") and name not in GENERATED:
                yield os.path.join(dirpath, name)


ANY_CALL = re.compile(r'(?<![A-Za-z0-9_])N?C?_\(')


def suspicious_calls(text, rel, covered):
    """_() calls that MARKED could not read, and that still hold a literal.

    The dangerous shape is a macro glued to a string:

        snprintf(buf, n, _(LV_SYMBOL_WIFI "  conectando..."));

    The preprocessor pastes the symbol's bytes onto the front, so what
    aos_tr() looks up at run time starts with the glyph - but this script
    reads the source, where the macro is just a name, and would file the key
    as "  conectando...". The two never meet, nothing errors, and the string
    simply stays Spanish forever.

    A call with no literal at all (`_(app->desc.name)`) is fine and stays
    quiet: the key comes from wherever that pointer was built.
    """
    out = []
    for m in ANY_CALL.finditer(text):
        if m.start() in covered:
            continue
        depth, i, n = 1, m.end(), len(text)
        while i < n and depth:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        body = text[m.end():i - 1]
        if pasted_literal(body):
            line = text.count("\n", 0, m.start()) + 1
            out.append((rel, line, " ".join(body.split())[:70]))
    return out


def pasted_literal(body):
    """True when a string literal sits right up against an identifier.

    That is the macro-paste shape and the only one worth warning about:

        _(LV_SYMBOL_WIFI "  conectando...")     <- flagged

    It is NOT the same as a literal simply appearing beside other arguments,
    which is how the context form is supposed to look:

        C_("dia", days[i])                      <- fine, the key comes from
                                                   the NC_ in the table
    """
    for m in LITERAL.finditer(body):
        before = body[:m.start()].rstrip()
        after = body[m.end():].lstrip()
        if before and (before[-1].isalnum() or before[-1] == "_"):
            return True
        if after and (after[0].isalnum() or after[0] == "_"):
            return True
    return False


def scan_marked(root, warnings=None):
    """Every _()-wrapped string under root, as {key: [file:line, ...]}."""
    found = OrderedDict()
    for path in c_sources(root):
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = strip_comments(fh.read())
        rel = os.path.relpath(path, REPO)
        covered = set()
        for m in CONTEXT.finditer(text):
            covered.add(m.start())
            body = joined_literals(m.group(2))
            if not body:
                continue
            key = "%s\\x04%s" % (m.group(1), body)
            line = text.count("\n", 0, m.start()) + 1
            found.setdefault(key, []).append("%s:%d" % (rel, line))
        for m in MARKED.finditer(text):
            if m.start() in covered:
                continue            # ya lo tomo CONTEXT: NC_( tambien matchea _(
            covered.add(m.start())
            key = joined_literals(m.group(1))
            if not key:
                continue
            line = text.count("\n", 0, m.start()) + 1
            found.setdefault(key, []).append("%s:%d" % (rel, line))
        if warnings is not None:
            warnings.extend(suspicious_calls(text, rel, covered))
    return found


def app_dirs():
    apps = os.path.join(REPO, "apps")
    for name in sorted(os.listdir(apps)):
        main = os.path.join(apps, name, "main")
        if os.path.isdir(main):
            yield name, main


def app_identity(main_dir):
    """(id, name) for a dynamic app, read out of its sources."""
    app_id = app_name = None
    for path in c_sources(main_dir):
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = strip_comments(fh.read())
        if app_id is None:
            m = ID_RE.search(text)
            if m:
                app_id = m.group(1)
            else:
                m = ID_MACRO_RE.search(text)
                if m:
                    d = re.search(r'#define\s+%s\s+"([^"]+)"' % re.escape(m.group(1)),
                                  text)
                    if d:
                        app_id = d.group(1)
        if app_name is None:
            m = NAME_RE.search(text)
            if m:
                app_name = m.group(1)
    return app_id, app_name


def builtin_app_names():
    """desc.name of every built-in app, for the launcher."""
    names = []
    for path in c_sources(os.path.join(REPO, "components", "aos_apps")):
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = strip_comments(fh.read())
        rel = os.path.relpath(path, REPO)
        for m in NAME_RE.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            names.append((m.group(1), "%s:%d" % (rel, line)))
    return names


def collect():
    """Everything, as an ordered {catalog filename: {key: [refs]}}."""
    catalogs = OrderedDict()
    warnings = []

    system = OrderedDict()
    for d in FIRMWARE_DIRS:
        for key, refs in scan_marked(os.path.join(REPO, d), warnings).items():
            system.setdefault(key, []).extend(refs)

    for key, ref in builtin_app_names():
        system.setdefault(key, []).append(ref + "  (nombre de app)")

    for dirname, main in app_dirs():
        app_id, app_name = app_identity(main)
        if app_name:
            system.setdefault(app_name, []).append(
                "apps/%s  (nombre de app)" % dirname)

        marked = scan_marked(main, warnings)
        if not marked:
            continue
        if not app_id:
            print("warning: apps/%s has marked strings but I could not find "
                  "its desc.id; skipping it" % dirname, file=sys.stderr)
            continue
        catalogs["%s.lang" % app_id] = marked

    catalogs[SYSTEM_CATALOG] = system
    catalogs.move_to_end(SYSTEM_CATALOG, last=False)

    if warnings:
        print("\nWARNING: %d calls to _() with a literal I could not read."
              % len(warnings), file=sys.stderr)
        print("Nearly always a macro glued onto the text (LV_SYMBOL_*). At\n"
              "run time the key carries the macro's bytes in front, here it\n"
              "does not, and the translation never matches. Pass the macro as\n"
              "an argument:  LV_SYMBOL_WIFI \"  %s\", _(\"text\")",
              file=sys.stderr)
        for rel, line, body in warnings[:10]:
            print("   %s:%d  _(%s)" % (rel, line, body), file=sys.stderr)
        print("", file=sys.stderr)

    return catalogs


def read_pack(path):
    """Existing translations, so regenerating never loses work."""
    out = OrderedDict()
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\n").rstrip("\r")
            if not line or line.startswith("#"):
                continue
            if "\t" not in line:
                continue
            key, val = line.split("\t", 1)
            out[key] = val
    return out


def write_catalog(path, entries, existing, code):
    translated = 0
    lines = [
        "# amoledos-lang 1   es -> %s" % code,
        "# %s" % os.path.basename(path),
        "#",
        "# Una linea por cadena: el espanol, un TAB, la traduccion.",
        "# Los \\n van escapados a proposito; el cargador los desescapa.",
        "# Una linea sin traducir (las dos mitades iguales) se ve en espanol.",
        "",
    ]
    for key, refs in entries.items():
        for ref in refs[:4]:
            lines.append("#: %s" % ref)
        # An untranslated line has to default to what would be SHOWN, which for
        # a context key is the part after the \x04 - never the key itself, or
        # an unedited template would paint "dia\x04DOM" on the watch face.
        shown = key.split("\\x04", 1)[1] if "\\x04" in key else key
        val = existing.get(key, shown)
        if val != shown:
            translated += 1
        if "\t" in key:
            print("warning: the string %r has a literal TAB and breaks the "
                  "format; skipping it" % key, file=sys.stderr)
            continue
        lines.append("%s\t%s" % (key, val))
        lines.append("")

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    return translated


def cmd_template(args):
    catalogs = collect()
    out_dir = os.path.join(args.out, args.code)
    os.makedirs(out_dir, exist_ok=True)

    meta = os.path.join(out_dir, "meta.txt")
    if not os.path.exists(meta):
        with open(meta, "w", encoding="utf-8") as fh:
            fh.write("name=%s\nformat=1\n" % args.code)
        print("  meta.txt        created, edit 'name=' with the visible name")

    total = done = 0
    for filename, entries in catalogs.items():
        if not entries:
            continue
        path = os.path.join(out_dir, filename)
        existing = read_pack(path)
        n = write_catalog(path, entries, existing, args.code)
        total += len(entries)
        done += n
        print("  %-22s %4d strings, %4d translated" % (filename, len(entries), n))

    print("\n  %d strings in total, %d translated (%d%%)"
          % (total, done, (100 * done // total) if total else 0))
    print("  in %s" % out_dir)


def cmd_check(args):
    catalogs = collect()
    pack_dir = os.path.join(args.out, args.code)
    if not os.path.isdir(pack_dir):
        sys.exit("the pack %s does not exist" % pack_dir)

    problems = 0
    for filename, entries in catalogs.items():
        path = os.path.join(pack_dir, filename)
        have = read_pack(path)

        missing = [k for k in entries if k not in have]
        orphan = [k for k in have if k not in entries]
        untranslated = [k for k in entries
                        if have.get(k) == (k.split("\\x04", 1)[1]
                                           if "\\x04" in k else k)]

        if not (missing or orphan):
            # "identical" and not "untranslated": many of them are identical on
            # purpose - proper nouns, cities, SI units. Calling them pending
            # sends you hunting for work that does not exist.
            print("  %-22s ok  (%d strings, %d identical in both languages)"
                  % (filename, len(entries), len(untranslated)))
            continue

        problems += 1
        print("  %s" % filename)
        if missing:
            print("     %d missing (in the code, not in the pack):" % len(missing))
            for k in missing[:6]:
                print("        %s" % k)
        if orphan:
            print("     %d left over (in the pack, no longer in the code --"
                  " did the Spanish text change?):" % len(orphan))
            for k in orphan[:6]:
                print("        %s" % k)

    # The apps that draw with their own bitmap font have a MUCH smaller
    # character set than the screen, and whatever falls outside is silently
    # discarded. It is impossible to see by reading the catalogue, so it is
    # checked here. See docs/I18N.md, F4.
    #
    # Only NON-ASCII is flagged, for two measured reasons:
    #   - gx_text() and ak_text() FOLD lower case to upper, so a-z draws
    #     perfectly even though the table starts at 0x20.
    #   - the catalogue does not know which string goes to the canvas and which
    #     to an LVGL label; in these apps both coexist. An accent is the one
    #     case that fails by EITHER path, so it is the only one that can be
    #     asserted without looking at the code.
    CHARSETS = {
        "demo.2043.lang":     "gx_text(), fuente 5x7 propia",
        "demo.arkanos.lang":  "ak_text(), fuente 5x7 propia",
        "demo.claudito.lang": "cl_text(), fuente 5x7 propia",
        "demo.chatarra.lang": "ch_text(), fuente 5x7 propia",
    }
    strip = re.compile(r"\\[ntr]|\\x[0-9A-Fa-f]{2}"
                       r"|%[-+ #0]*[0-9*]*(?:\.[0-9*]+)?(?:hh|h|ll|l|z|j|t|L)?[a-zA-Z%]")
    for filename, why in CHARSETS.items():
        path = os.path.join(pack_dir, filename)
        if not os.path.exists(path):
            continue
        malos = []
        for key, val in read_pack(path).items():
            fuera = {c for c in strip.sub("", val) if ord(c) > 0x7E}
            if fuera:
                malos.append((val, "".join(sorted(fuera))))
        if malos:
            problems += 1
            print("  %s: %d texts with non-ASCII characters" % (filename, len(malos)))
            print("     this app draws with %s: accents are discarded"
                  " silently. Transliterate (ae, oe, ue, ss)." % why)
            for val, fuera in malos[:6]:
                print("        %r  ->  %s" % (val, fuera))

    # Catalogs in the pack that no longer match any app.
    known = set(catalogs)
    for name in sorted(os.listdir(pack_dir)):
        if name.endswith(".lang") and name not in known:
            problems += 1
            print("  %s: the pack carries it but no app asks for it" % name)

    sys.exit(1 if problems else 0)


# Words that give away Spanish interface text. It is not a language detector,
# it is a cheap filter for telling copy from data.
SPANISH_WORDS = re.compile(
    r"\b(el|la|los|las|un|una|de|en|con|sin|para|por|que|no|si|se|al|del|es|"
    r"hay|mas|solo|ahora|nada|todo|toca|tocar|pasos|red|redes|tiempo|hasta|"
    r"vale|curva|terminado|max|abierta|captura|sigue|voy|elegir|elegi|midiendo|"
    r"esperando|pude|puede|falta|rechazo|mejor|fallaste|repeti|fecha|placa)\b",
    re.IGNORECASE)

# What is NOT copy: logs, comparisons, file opens, the preprocessor. As a WORD
# and not as a substring - 'printf' inside 'snprintf' was exactly the mistake
# that left 23 strings unmarked during F4, including a whole app I had written
# off as empty.
NOT_UI = re.compile(
    r"\b(ESP_LOGI|ESP_LOGW|ESP_LOGE|ESP_LOGD|getenv|printf|aos_hal_log|memcmp|"
    r"fopen|strcmp|strncmp|strstr|sscanf|setenv)\s*\(|#\s*(include|define)")


def looks_like_art(s):
    body = s.replace("\\n", "")
    if (len(body) >= 4 and re.fullmatch(r"[.#A-Za-z0-9 ,:*+@=\-]+", body)
            and sum(body.count(c) for c in ". ") / max(len(body), 1) > 0.30):
        return True
    return re.fullmatch(r"[.#\- ]+", body) is not None


# --------------------------------------------------------------------------
# Pseudolocalizacion
# --------------------------------------------------------------------------

# Each letter for its accented version from the Latin-1 supplement, which is
# exactly the range tools/gen_fonts.py compiles: if any of this does NOT draw,
# what is wrong is the font set.
PSEUDO_MAP = {
    "a": "á", "e": "é", "i": "í", "o": "ó", "u": "ú", "n": "ñ", "c": "ç",
    "y": "ý", "s": "š" if False else "s",          # s-caron es Latin Ext-A: fuera
    "A": "Á", "E": "É", "I": "Í", "O": "Ó", "U": "Ú", "N": "Ñ", "C": "Ç",
    "Y": "Ý",
}

# What has to be left untouched or something really breaks:
#   %d %s %02d %.1f %+.2f %lu %llu ...  consumed by snprintf
#   \n \t \r \\ \xHH               unescaped by the loader
PSEUDO_KEEP = re.compile(
    r'%[-+ #0]*[0-9*]*(?:\.[0-9*]+)?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfgGaAcsp%]'
    r'|\\x[0-9A-Fa-f]{2}|\\[ntr\\]')

# Catalogues that are expanded but NOT accented: these three apps draw their
# text with their own bitmap font -ASCII and nothing else- so an accent there
# does not render and only adds noise. See docs/I18N.md, F4.
PSEUDO_NO_ACCENT = {"demo.2043.lang", "demo.arkanos.lang", "demo.claudito.lang",
                    "demo.chatarra.lang"}

PSEUDO_PAD = "\u00b7"      # middle dot: it is in Latin-1, that is, in the font
PSEUDO_PAD_ASCII = "."     # ...but NOT in the apps' own fonts


def pseudo_value(shown, accents=True, grow=0.40):
    """Pseudolocalised version of what is shown on screen.

    Brackets so a clipping shows at once: if the ']' is missing, the string did
    not fit. Accents to test the font with real text on every screen at the
    same time. And proportional padding, because the point is to measure the
    MAXIMUM length and not the length some real language happens to have.
    """
    out = []
    letters = 0
    pos = 0
    for m in PSEUDO_KEEP.finditer(shown):
        chunk = shown[pos:m.start()]
        for ch in chunk:
            if accents and ch in PSEUDO_MAP:
                out.append(PSEUDO_MAP[ch])
            else:
                out.append(ch)
            if ch.isalpha():
                letters += 1
        out.append(m.group(0))
        pos = m.end()
    for ch in shown[pos:]:
        if accents and ch in PSEUDO_MAP:
            out.append(PSEUDO_MAP[ch])
        else:
            out.append(ch)
        if ch.isalpha():
            letters += 1

    body = "".join(out)
    pad = max(1, int(round(letters * grow)))
    # With a restricted font the padding also has to be ASCII: the middle dot
    # is Latin-1 and those fonts do not have it, so it came out as a gap.
    fill = PSEUDO_PAD if accents else PSEUDO_PAD_ASCII

    # The padding goes before the closing bracket, and if the string has line
    # breaks it is put on the LAST one, which is the one that decides the box's
    # height.
    if "\\n" in body:
        head, _, tail = body.rpartition("\\n")
        return "[" + head + "\\n" + tail + fill * pad + "]"
    return "[" + body + fill * pad + "]"


def cmd_pseudo(args):
    catalogs = collect()
    out_dir = os.path.join(args.out, args.code)
    os.makedirs(out_dir, exist_ok=True)

    with open(os.path.join(out_dir, "meta.txt"), "w", encoding="utf-8") as fh:
        fh.write("name=Pseudo\nformat=1\n")

    total = 0
    for filename, entries in catalogs.items():
        if not entries:
            continue
        accents = filename not in PSEUDO_NO_ACCENT
        lines = [
            "# amoledos-lang 1   es -> %s  (pseudolocalizacion, generada)" % args.code,
            "# NO editar a mano: se regenera con gen_lang.py pseudo.",
            "# Expansion %d%%%s. Si falta el corchete de cierre, no entro." % (
                int(args.grow * 100),
                "" if accents else ", sin acentos (fuente propia de la app)"),
            "",
        ]
        for key in entries:
            shown = key.split("\\x04", 1)[1] if "\\x04" in key else key
            lines.append("%s\t%s" % (key, pseudo_value(shown, accents, args.grow)))
        with open(os.path.join(out_dir, filename), "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
        total += len(entries)
        print("  %-22s %4d strings%s" % (filename, len(entries),
                                         "" if accents else "   (no accents)"))

    print("\n  %d strings in %s" % (total, out_dir))
    print("  now: ./tools/audit_layout.sh es %s" % args.code)


# --------------------------------------------------------------------------
# Catalogues embedded in the firmware
# --------------------------------------------------------------------------

EMBED_OUT = os.path.join(REPO, "components", "aos_ui", "aos_lang_embedded.c")


def c_escape(text):
    """One catalogue line as a C literal.

    Mind the double layer of escaping: the catalogue ALREADY stores \n as two
    characters -backslash and n- because that is how unescape() reads it. Here
    that backslash has to be protected so the C compiler does not swallow it
    and turn it into a real line break, which is precisely what the format
    avoids. Hence the doubled backslash.
    """
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 0x20:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def cmd_embed(args):
    """Generates aos_ui/aos_lang_embedded.c with the requested packs.

    It goes inside aos_ui and not in a component of its own for the usual
    reason: aos_ui is already compiled by the firmware, already globbed by the
    simulator and already in gen_symbols.py's DEFAULT_LIBS. A new component is
    three pieces of wiring by hand and each one is a place to forget.
    """
    packs = []
    for code in args.codes:
        d = os.path.join(args.out, code)
        if not os.path.isdir(d):
            sys.exit("the pack %s does not exist" % d)

        name = code
        meta = os.path.join(d, "meta.txt")
        if os.path.exists(meta):
            for line in open(meta, encoding="utf-8"):
                if line.startswith("name="):
                    name = line[5:].strip() or code

        files = []
        strings = apps = 0
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".lang"):
                continue
            pairs = [(k, v) for k, v in read_pack(os.path.join(d, fn)).items()]
            if not pairs:
                continue
            # The "#: file:line" comments are NOT embedded: they are the long
            # half of the file and are of no use at all on the board.
            files.append((fn, pairs))
            if fn == SYSTEM_CATALOG:
                strings = len(pairs)
            else:
                apps += 1
        packs.append((code, name, strings, apps, files))

    out = []
    out.append("/*")
    out.append(" * GENERADO por tools/gen_lang.py embed - no editar a mano.")
    out.append(" *")
    out.append(" * Catalogos que viajan DENTRO del binario. Existen para que el")
    out.append(" * aparato tenga un segundo idioma aunque no haya tarjeta, y para que")
    out.append(" * catalogo y codigo no puedan desincronizarse: con la clave siendo el")
    out.append(" * texto espanol, un cambio en el fuente huerfana su traduccion en")
    out.append(" * silencio, y eso solo se evita si los dos se compilan juntos.")
    out.append(" *")
    out.append(" * La tarjeta le GANA a esto (ver catalog_load en aos_i18n.c): asi se")
    out.append(" * corrige una traduccion sin recompilar, y el tercer idioma entra por")
    out.append(" * el mismo camino que el segundo.")
    out.append(" */")
    out.append('#include "aos_lang_embedded.h"')
    out.append("")

    for code, name, strings, apps, files in packs:
        for fn, pairs in files:
            sym = "blob_%s_%s" % (code, re.sub(r"[^A-Za-z0-9]", "_", fn))
            out.append("static const char %s[] =" % sym)
            for k, v in pairs:
                out.append('    "%s\\t%s\\n"' % (c_escape(k), c_escape(v)))
            out.append("    ;")
            out.append("")
        out.append("static const aos_lang_file_t files_%s[] = {" % code)
        for fn, pairs in files:
            sym = "blob_%s_%s" % (code, re.sub(r"[^A-Za-z0-9]", "_", fn))
            out.append('    { "%s", %s },' % (fn, sym))
        out.append("};")
        out.append("")

    out.append("const aos_lang_pack_t aos_lang_packs[] = {")
    for code, name, strings, apps, files in packs:
        out.append('    { "%s", "%s", %d, %d, files_%s, %d },'
                   % (code, c_escape(name), strings, apps, code, len(files)))
    out.append("};")
    out.append("const int aos_lang_pack_count = %d;" % len(packs))
    out.append("")

    with open(EMBED_OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(out))

    total = os.path.getsize(EMBED_OUT)
    print("  %s" % os.path.relpath(EMBED_OUT, REPO))
    for code, name, strings, apps, files in packs:
        print("    %-4s %-10s %3d strings, %2d app catalogues"
              % (code, name, strings, apps))
    print("    C source: %.1f KB (the real flash figure is in the .map)" % (total / 1024))
    print("\n  Regenerate aos_symbols.c if the API changes, and rebuild.")


def cmd_unmarked(args):
    """Spanish-looking strings that no marker covers.

    The counterpart to `check`: that one compares catalog against code, this
    one looks for UI text the code never handed to a marker at all. Neither
    the extractor nor AOS_I18N_MARK can see that class - a string nobody
    wrapped simply never reaches aos_tr().
    """
    total = 0
    for root in FIRMWARE_DIRS + ["apps/%s/main" % a for a in sorted(os.listdir(
            os.path.join(REPO, "apps")))
            if os.path.isdir(os.path.join(REPO, "apps", a, "main"))]:
        if root in args.skip:
            continue
        rows = []
        for path in c_sources(os.path.join(REPO, root)):
            raw = open(path, encoding="utf-8", errors="replace").read()
            text = strip_comments(raw)
            covered = [(m.start(), m.end()) for m in
                       list(MARKED.finditer(text)) + list(CONTEXT.finditer(text))]
            lines = raw.split("\n")
            for m in LITERAL.finditer(text):
                s = m.group(1)
                if not re.search(r"[A-Za-zÁÉÍÓÚÑáéíóúñ]{3,}", s):
                    continue
                if looks_like_art(s) or not SPANISH_WORDS.search(s):
                    continue
                if any(a <= m.start() < b for a, b in covered):
                    continue
                line = text.count("\n", 0, m.start()) + 1
                # The whole statement, not the literal's line. aos_hal_log()
                # has always been in NOT_UI, but a call split across three
                # lines left the literal alone on its own and the filter saw
                # nothing. Same shape as F2's printf/snprintf: a one-line
                # heuristic over code spanning several.
                corte = max(text.rfind(";", 0, m.start()),
                            text.rfind("{", 0, m.start()),
                            text.rfind("}", 0, m.start()))
                src = text[corte + 1:m.start()]
                if NOT_UI.search(src) or re.search(r"desc\.(name|icon|id)", src):
                    continue
                rows.append((os.path.relpath(path, REPO), line, s))
        if rows:
            print("  %s" % root)
            for rel, line, s in rows:
                print("     %s:%d  %r" % (rel, line, s))
            total += len(rows)
    if total:
        print("\n  %d strings that look like interface text and are unmarked" % total)
    else:
        print("  nothing unmarked")
    sys.exit(1 if total else 0)


def cmd_stats(args):
    catalogs = collect()
    marked = sum(len(v) for v in catalogs.values())

    # Everything that looks like UI text, marked or not, so the number below
    # says how much of F2/F4 is left rather than how much exists.
    total_calls = 0
    for d in FIRMWARE_DIRS:
        for path in c_sources(os.path.join(REPO, d)):
            with open(path, encoding="utf-8", errors="replace") as fh:
                total_calls += len(MARKED.findall(strip_comments(fh.read())))

    print("  marked and in a catalogue : %d unique strings" % marked)
    print("  of those, system          : %d" % len(catalogs.get(SYSTEM_CATALOG, {})))
    print("  app catalogues            : %d" % (len(catalogs) - 1))
    print("  calls to _() in the firmware: %d" % total_calls)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=DEFAULT_OUT,
                    help="directorio de packs (por defecto sim/sim_fs/lang)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    t = sub.add_parser("template", help="escribe o actualiza los templates")
    t.add_argument("code")
    t.set_defaults(func=cmd_template)

    c = sub.add_parser("check", help="compara un pack con las fuentes")
    c.add_argument("code")
    c.set_defaults(func=cmd_check)

    s = sub.add_parser("stats", help="cuanto hay marcado")
    s.set_defaults(func=cmd_stats)

    ps = sub.add_parser("pseudo",
                        help="pack de pseudolocalizacion para probar el layout")
    ps.add_argument("code", nargs="?", default="xx")
    ps.add_argument("--grow", type=float, default=0.40,
                    help="cuanto estirar, 0.40 = 40%% mas largo")
    ps.set_defaults(func=cmd_pseudo)

    e = sub.add_parser("embed",
                       help="mete packs adentro del binario (aos_ui/aos_lang_embedded.c)")
    e.add_argument("codes", nargs="+")
    e.set_defaults(func=cmd_embed)

    u = sub.add_parser("unmarked",
                       help="texto con pinta de interfaz que nadie envolvio")
    u.add_argument("--skip", nargs="*", default=["apps/truco/main"],
                   help="directorios a saltear (truco se deja en espanol)")
    u.set_defaults(func=cmd_unmarked)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
