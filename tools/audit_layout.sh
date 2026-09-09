#!/bin/zsh
#
# Layout audit of every screen, in every installed language.
#
#   ./tools/audit_layout.sh            es and en
#   ./tools/audit_layout.sh es en pt   whichever are asked for
#
# It opens each app in the simulator, lets it build its screen, walks LVGL's
# tree and reports three things: text that does not fit in its box (CLIPPED),
# text off the screen (OFFSCREEN) and text that runs out of its container
# (OVERFLOW). See AOS_SIM_AUDIT in sim/main.c.
#
# What matters is NOT the total but the DIFFERENCE between languages: a finding
# that appears identically in both is a pre-existing condition, not something a
# translation broke. The summary at the end separates the two.
#
# It does not replace looking at the screen: it sees no overlaps and no
# ugliness, and it does not see text nobody wrapped (that is what
# 'gen_lang.py unmarked' is for).
#
# --- WHEN TO RUN THIS, AND WHEN NOT TO --------------------------------------
#
# This is a SWEEP. It exists for the cases where every screen, or nearly every
# screen, has to be looked at:
#
#   - after adding or changing a language;
#   - after touching the theme, a font or a shared widget;
#   - before a release.
#
# It opens 47 screens per language, one process each, and takes minutes. It is
# NOT the tool for checking one or two screens, and reaching for it that way is
# a waste: the run is slow, and the answer arrives buried in a report about
# everything else.
#
# For one screen, open that screen:
#
#   cd sim
#   AOS_SIM_AUDIT=es/aos.settings AOS_SIM_VIEW=aos.settings ./build/amoledos_sim
#
# Same check, same output lines, one second. Add AOS_SIM_KEYS to reach a screen
# that needs navigating to, and AOS_SIM_AUDIT_MS if it takes a while to
# assemble -that is how the watchface picker is audited, below-.
set -e
ROOT=${0:a:h:h}
SIM=$ROOT/sim
BIN=$SIM/build/amoledos_sim
[[ -x $BIN ]] || { echo "$BIN is missing - build the simulator first"; exit 1; }

# Note: in zsh ${@:-es en} is NOT word-split, it ends up as a single language
# called "es en". It is the same trap that ruined the first complete run.
LANGS=("$@")
(( ${#LANGS[@]} )) || LANGS=(es en)
IDS=(${(f)"$(grep -rhoE '\.id\s*=\s*"[^"]+"' \
      $ROOT/components/aos_apps/*.c $ROOT/apps/*/main/*.c \
      | grep -oE '"[^"]+"' | tr -d '"' | sort -u)"})
# The menu has THREE styles and only one shows the app names: the honeycomb
# has no text. Auditing only "launcher" -which uses the stored style- left
# out precisely the screen where a long name gets clipped.
IDS+=(aos.remoto launcher grid honeycomb)

OUT=${TMPDIR:-/tmp}/aos_audit.txt
: > $OUT
echo "auditing $(( ${#IDS[@]} + 5 )) screens x ${#LANGS[@]} languages..."
for lang in $LANGS; do
    grep -v "^lang" $SIM/sim_fs/prefs.txt > /tmp/aos_prefs.$$ 2>/dev/null || true
    mv /tmp/aos_prefs.$$ $SIM/sim_fs/prefs.txt 2>/dev/null || true
    [[ $lang != es ]] && echo "lang=$lang" >> $SIM/sim_fs/prefs.txt
    for id in $IDS; do
        (cd $SIM && AOS_SIM_AUDIT="$lang/$id" AOS_SIM_VIEW="$id" $BIN 2>/dev/null) \
            | grep "^AUDIT" >> $OUT || true
    done
    # The access-point screen hangs off lv_layer_top behind a scroll and a
    # touch, so AOS_SIM_VIEW alone does not reach it: AOS_SIM_AP=2 opens it
    # in one go (see aos_app_settings.c). It is where German presses hardest
    # -the key, the mode and the QR caption are three long texts in a row-.
    (cd $SIM && AOS_SIM_AP=2 AOS_SIM_AUDIT="$lang/aos.settings.ap" \
        AOS_SIM_VIEW="aos.settings" $BIN 2>/dev/null) \
        | grep "^AUDIT" >> $OUT || true
    # The same for the three bluetooth screens: pairing from Settings (with
    # the six-figure number and the two buttons), the filter by category
    # (twelve boxes in a row, which is where German presses) and the pairing
    # request overlay, which appears when the phone feels like it and not
    # when you go looking for it.
    for bt in 1 2 3; do
        (cd $SIM && AOS_SIM_BT=$bt AOS_SIM_AUDIT="$lang/aos.settings.bt$bt" \
            AOS_SIM_VIEW="aos.settings" $BIN 2>/dev/null) \
            | grep "^AUDIT" >> $OUT || true
    done
    # The watchface picker only exists after a long press on the watch, so
    # AOS_SIM_VIEW cannot reach it and for a long time nobody audited it. That
    # is where the buttons below the touch limit lived -see
    # docs/internal/HANDOFF-PUBLICACION.md, section 3-. The script opens it with
    # a hold and waits for the audit at 3 s, because the default 2 s falls
    # inside the 400 ms of the long press plus the drawing.
    (cd $SIM && AOS_SIM_AUDIT="$lang/watchface.picker" AOS_SIM_AUDIT_MS=3000 \
        AOS_SIM_KEYS="ms:1200,hold:184x200:1500,ms:3000" $BIN 2>/dev/null) \
        | grep "^AUDIT" >> $OUT || true
done
python3 - $OUT $LANGS <<'PY'
import sys, re, collections
path, langs = sys.argv[1], sys.argv[2:]
per = {l: collections.defaultdict(list) for l in langs}
for line in open(path, encoding='utf-8', errors='replace'):
    m = re.match(r'AUDIT (\w+)\s+(\S+?)/(\S+) \| (.*)', line.rstrip())
    if not m or m.group(1) == 'END':
        continue
    kind, lang, app, rest = m.groups()
    if lang in per:
        per[lang][app].append((kind, rest))

base = langs[0]
def geo(t):
    kind, rest = t
    return (kind, rest.split('|', 1)[1].strip() if '|' in rest else rest)

print("\n=== regressions: appear in one language and not in %s ===" % base)
total = 0
ellipsis = []
lowedge = []
for lang in langs[1:]:
    for app in sorted(per[lang]):
        b = collections.Counter(geo(t) for t in per[base][app])
        o = collections.Counter(geo(t) for t in per[lang][app])
        solo = [t for t in per[lang][app] if o[geo(t)] > b.get(geo(t), 0)]
        for kind, rest in solo:
            if kind in ("ELLIPSIS", "LOWEDGE"):
                (ellipsis if kind == "ELLIPSIS" else lowedge).append((lang, rest))
                continue
            print("  %-4s %-9s %s" % (lang, kind, rest[:100]))
            total += 1
print("  none" if not total else "  %d in total" % total)

if ellipsis:
    # These are not breakages: the label asked for an ellipsis. But saying
    # WHICH ones get clipped is exactly what the translator needs in order to
    # shorten them.
    print("\n=== clipped with an ellipsis (on purpose; check they still read) ===")
    for lang, rest in ellipsis:
        print("  %-4s %s" % (lang, rest[:100]))

# LOWEDGE is not breakage: a control low on the screen that still leaves a
# usable strip. It is here so that, if something feels unresponsive on the
# board, you know where to look first -and the first suspect is the touch
# calibration, not the layout-. See docs/internal, section 9.
bajos = [(a, r) for a in sorted(per[base]) for k, r in per[base][a] if k == "LOWEDGE"]
if bajos or lowedge:
    print("\n=== low on the screen (NOT breakage; check on the board if in doubt) ===")
    for app, rest in bajos:
        print("  %-14s %s" % (app, rest[:90]))
    for lang, rest in lowedge:
        print("  %-4s %s" % (lang, rest[:90]))

print("\n=== pre-existing (identical in every language) ===")
n = 0
for app in sorted(per[base]):
    for kind, rest in per[base][app]:
        if kind == "LOWEDGE":
            continue
        print("  %-9s %-14s %s" % (kind, app, rest[:90]))
        n += 1
print("  none" if not n else "  %d in total" % n)
sys.exit(1 if total else 0)
PY
