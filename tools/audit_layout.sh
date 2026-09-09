#!/bin/zsh
#
# Layout audit of every screen, in every installed language.
#
#   ./tools/audit_layout.sh            es and en
#   ./tools/audit_layout.sh es en pt   whichever are asked for
#
# It opens each app in the simulator, lets it build its screen, walks LVGL's
# tree and reports three things: text that does not fit in its box (CORTADO),
# text off the screen (AFUERA) and text that runs out of its container
# (DESBORDA). See AOS_SIM_AUDIT in sim/main.c.
#
# What matters is NOT the total but the DIFFERENCE between languages: a finding
# that appears identically in both is a pre-existing condition, not something a
# translation broke. The summary at the end separates the two.
#
# It does not replace looking at the screen: it sees no overlaps and no
# ugliness, and it does not see text nobody wrapped (that is what
# 'gen_lang.py unmarked' is for).
set -e
ROOT=${0:a:h:h}
SIM=$ROOT/sim
BIN=$SIM/build/amoledos_sim
[[ -x $BIN ]] || { echo "falta $BIN - compila el simulador primero"; exit 1; }

# Note: in zsh ${@:-es en} is NOT word-split, it ends up as a single language
# called "es en". It is the same trap that ruined the first complete run.
LANGS=("$@")
(( ${#LANGS[@]} )) || LANGS=(es en)
IDS=(${(f)"$(grep -rhoE '\.id\s*=\s*"[^"]+"' \
      $ROOT/components/aos_apps/*.c $ROOT/apps/*/main/*.c \
      | grep -oE '"[^"]+"' | tr -d '"' | sort -u)"})
# El menu tiene TRES estilos y solo uno muestra los nombres de las apps: el
# panal no tiene texto. Auditar solo "launcher" -que usa el estilo guardado-
# dejaba fuera justo la pantalla donde un nombre largo se recorta.
IDS+=(aos.remoto launcher grid honeycomb)

OUT=${TMPDIR:-/tmp}/aos_audit.txt
: > $OUT
echo "auditando ${#IDS[@]} pantallas x ${#LANGS[@]} idiomas..."
for lang in $LANGS; do
    grep -v "^lang" $SIM/sim_fs/prefs.txt > /tmp/aos_prefs.$$ 2>/dev/null || true
    mv /tmp/aos_prefs.$$ $SIM/sim_fs/prefs.txt 2>/dev/null || true
    [[ $lang != es ]] && echo "lang=$lang" >> $SIM/sim_fs/prefs.txt
    for id in $IDS; do
        (cd $SIM && AOS_SIM_AUDIT="$lang/$id" AOS_SIM_VIEW="$id" $BIN 2>/dev/null) \
            | grep "^AUDIT" >> $OUT || true
    done
    # La pantalla del punto de acceso cuelga de lv_layer_top detras de un
    # scroll y un toque, asi que AOS_SIM_VIEW sola no llega: AOS_SIM_AP=2 la
    # abre de una (ver aos_app_settings.c). Es donde el aleman aprieta mas -la
    # clave, el modo y el pie del QR son tres textos largos seguidos-.
    (cd $SIM && AOS_SIM_AP=2 AOS_SIM_AUDIT="$lang/aos.settings.ap" \
        AOS_SIM_VIEW="aos.settings" $BIN 2>/dev/null) \
        | grep "^AUDIT" >> $OUT || true
    # Lo mismo para las tres pantallas de bluetooth: emparejar desde Ajustes
    # (con el numero de seis cifras y los dos botones), el filtro por categoria
    # (doce casillas seguidas, que es donde el aleman aprieta) y el overlay del
    # pedido de emparejamiento, que aparece cuando el telefono quiere y no
    # cuando uno lo busca.
    for bt in 1 2 3; do
        (cd $SIM && AOS_SIM_BT=$bt AOS_SIM_AUDIT="$lang/aos.settings.bt$bt" \
            AOS_SIM_VIEW="aos.settings" $BIN 2>/dev/null) \
            | grep "^AUDIT" >> $OUT || true
    done
done
python3 - $OUT $LANGS <<'PY'
import sys, re, collections
path, langs = sys.argv[1], sys.argv[2:]
per = {l: collections.defaultdict(list) for l in langs}
for line in open(path, encoding='utf-8', errors='replace'):
    m = re.match(r'AUDIT (\w+)\s+(\S+?)/(\S+) \| (.*)', line.rstrip())
    if not m or m.group(1) == 'FIN':
        continue
    kind, lang, app, rest = m.groups()
    if lang in per:
        per[lang][app].append((kind, rest))

base = langs[0]
def geo(t):
    kind, rest = t
    return (kind, rest.split('|', 1)[1].strip() if '|' in rest else rest)

print("\n=== regresiones: aparecen en un idioma y no en %s ===" % base)
total = 0
puntos = []
for lang in langs[1:]:
    for app in sorted(per[lang]):
        b = collections.Counter(geo(t) for t in per[base][app])
        o = collections.Counter(geo(t) for t in per[lang][app])
        solo = [t for t in per[lang][app] if o[geo(t)] > b.get(geo(t), 0)]
        for kind, rest in solo:
            if kind == "PUNTOS":
                puntos.append((lang, rest))
                continue
            print("  %-4s %-9s %s" % (lang, kind, rest[:100]))
            total += 1
print("  ninguna" if not total else "  %d en total" % total)

if puntos:
    # No son roturas: el label pidio puntos suspensivos. Pero decir CUALES se
    # recortan es justo lo que necesita el que traduce para acortarlas.
    print("\n=== recortados con puntos (a proposito; revisar si se entienden) ===")
    for lang, rest in puntos:
        print("  %-4s %s" % (lang, rest[:100]))

print("\n=== preexistentes (iguales en todos los idiomas) ===")
n = 0
for app in sorted(per[base]):
    for kind, rest in per[base][app]:
        print("  %-9s %-14s %s" % (kind, app, rest[:90]))
        n += 1
print("  ninguna" if not n else "  %d en total" % n)
sys.exit(1 if total else 0)
PY
