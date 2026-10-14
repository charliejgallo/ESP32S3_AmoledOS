#!/bin/zsh
#
# The last panic, decoded on the Mac.
#
#   ./tools/coredump.sh [host]                 charlie.local by default
#   ERASE=1 ./tools/coredump.sh ...            and forgets it on the watch afterwards
#   ELF=path/to/amoledos.elf ./tools/...       that ELF, whatever its sha (at your risk)
#
# Downloads the core dump the watch keeps in flash (GET /api/coredump) and
# decodes it -the tasks, the registers, the backtraces- against the ELF of THE
# FIRMWARE THAT MADE IT, and no other. Which one that is comes from the dump
# itself: its notes carry the ELF's sha, the name tools/elf_keep.sh keeps each
# ELF under in build/elf/ (install_fw.sh does it on every install). A dump
# stays in flash through any number of restarts until it is erased, and read
# against the wrong ELF an old dump looks like a new bug pointing at the wrong
# lines. A firmware installed from a release has its ELF in that release
# (amoledos.elf.xz): the script looks for it with gh and keeps it only if its
# sha is the dump's. With no ELF of that sha anywhere it stops and says so.
set -e
ROOT=${0:a:h:h}
HOST=${1:-charlie.local}
B="http://$HOST"
# Off a trusted network the watch's API wants its token (docs/SECURITY.md):
# AOS_TOKEN=<token> from the portal's Settings, sent with every request.
curl() { command curl ${AOS_TOKEN:+-H} ${AOS_TOKEN:+"Authorization: Bearer $AOS_TOKEN"} "$@"; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

info=$(curl -s --max-time 10 "$B/api/coredump?info=1") || { echo "$HOST does not answer"; exit 1; }
print -r -- "$info" | python3 -c '
import json, sys
d = json.load(sys.stdin)
if not d.get("present"):
    print("no core dump on the watch"); sys.exit(0)
where = {"running": "the firmware running now", "other": "the other OTA slot"}.get(d.get("slot"), "neither slot (an older image)")
print("dump: task %s, pc %s, %s bytes" % (d.get("task") or "?", d.get("pc"), d.get("size")))
print("made by firmware %s (%s%s)" % (d.get("elf_sha") or "?", where, ", " + d["version"] if d.get("version") else ""))
'
print -r -- "$info" | grep -q '"present":true' || exit 0
curl -sf --max-time 60 -o $tmp/core.bin "$B/api/coredump" || { echo "the dump could not be downloaded"; exit 1; }
# the response is core_dump_header_t and then the ELF
python3 -c "import sys; d=open(sys.argv[1],'rb').read(); i=d.find(b'\x7fELF'); open(sys.argv[2],'wb').write(d[i:] if i >= 0 else b'')" \
    $tmp/core.bin $tmp/core.elf
[[ -s $tmp/core.elf ]] || { echo "the dump holds no ELF: it may be cut short (erase it: ERASE=1)"; exit 1; }
sha=$(print -r -- "$info" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("elf_sha",""))')
ver=$(print -r -- "$info" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("version",""))')

if [[ -n $ELF ]]; then
    elf=$ELF
    echo "== using $elf as asked (the dump says firmware ${sha:-?})"
elif [[ -z $sha ]]; then
    echo "the dump does not say which firmware made it: not decoding it against a guess"
    echo "(ELF=build/elf/<sha>.elf $0 $HOST to force one)"
    exit 1
elif elfs=($ROOT/build/elf/$sha*.elf(N)) && (( ${#elfs} == 1 )); then
    # by prefix: a dump from a firmware built before the 16-character sha carries 9
    elf=$elfs[1]
    echo "== the firmware that crashed: ${elf#$ROOT/}"
elif [[ -f $ROOT/build/amoledos.elf && $(shasum -a 256 $ROOT/build/amoledos.elf | cut -c1-${#sha}) == $sha ]]; then
    elf=$ROOT/build/amoledos.elf
    echo "== the firmware that crashed: build/amoledos.elf (the one built last)"
elif [[ $ver =~ '^v[0-9]+\.[0-9]+\.[0-9]+$' ]] && command -v gh > /dev/null &&
     gh release download $ver --repo charliejgallo/ESP32S3_AmoledOS -p amoledos.elf.xz -D $tmp > /dev/null 2>&1 &&
     xz -dc $tmp/amoledos.elf.xz > $tmp/fw.elf &&
     [[ $(shasum -a 256 $tmp/fw.elf | cut -c1-${#sha}) == $sha ]]; then
    # a released firmware: its ELF goes with the release (from v0.14.0 on)
    mkdir -p $ROOT/build/elf
    elf=$ROOT/build/elf/$(shasum -a 256 $tmp/fw.elf | cut -c1-16).elf
    mv $tmp/fw.elf $elf
    echo "== the firmware that crashed: $ver, its ELF from the release (${elf#$ROOT/})"
else
    echo "the ELF of firmware $sha is not in build/elf/ (it keeps the last 10) nor in a release:"
    echo "this dump cannot be decoded here, and decoding it against another firmware would"
    echo "point at the wrong lines. If it is old, erase it: ERASE=1 $0 $HOST"
    [[ -n $ERASE ]] && curl -s "$B/api/coredump?erase=1" && echo
    exit 1
fi

source ~/esp/esp-idf/export.sh > /dev/null 2>&1
python -m esp_coredump info_corefile --core $tmp/core.elf --core-format elf $elf 2>&1 |
    sed -n '/CURRENT THREAD/,/THREADS INFO/p;/^==================== THREAD [0-9]/,/^$/p' | head -120
[[ -n $ERASE ]] && curl -s "$B/api/coredump?erase=1" && echo
exit 0
