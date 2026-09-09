#!/bin/zsh
#
# Uploads a language pack to the board over wifi, without taking the card out.
#
#   ./tools/install_lang.sh 192.168.1.116 en
#   ./tools/install_lang.sh amoledos.local en pt
#
# It uses the portal that already exists: /api/upload with dir=lang/<code>. The
# firmware creates /lang and /lang/<code> on the microSD the first time.
#
# After uploading you have to choose the language in Settings; this only puts
# the files there. And note: the pack is read at startup and on changing
# language, so overwriting the active one has no effect until it is chosen
# again.
set -e
ROOT=${0:a:h:h}
HOST=${1:?uso: install_lang.sh <ip-o-nombre> <codigo> [codigo...]}
shift
CODES=("$@")
(( ${#CODES[@]} )) || { echo "falta el codigo de idioma (en, pt, ...)"; exit 1; }

for code in $CODES; do
    DIR=$ROOT/sim/sim_fs/lang/$code
    [[ -d $DIR ]] || { echo "no existe $DIR - genera el pack con gen_lang.py"; exit 1; }
    files=($DIR/*(.N))
    (( ${#files[@]} )) || { echo "$DIR esta vacio"; exit 1; }

    echo "== $code -> $HOST =="
    total=0
    for f in $files; do
        name=${f:t}
        size=$(wc -c < $f | tr -d ' ')
        code_http=$(curl -sS -o /dev/null -w '%{http_code}' \
            --max-time 30 \
            -X POST "http://$HOST/api/upload?dir=lang/$code&name=$name" \
            --data-binary "@$f") || code_http=000
        if [[ $code_http == 2* ]]; then
            printf "   %-24s %6s B  ok\n" $name $size
            (( total += size ))
        else
            printf "   %-24s %6s B  FALLO (HTTP %s)\n" $name $size $code_http
            exit 1
        fi
    done
    echo "   $((${#files[@]})) archivos, $total bytes"
    echo "   comprobando: $(curl -sS --max-time 10 "http://$HOST/api/list?dir=lang/$code" | head -c 200)"
done
echo
echo "listo. Elegi el idioma en Ajustes > IDIOMA."
