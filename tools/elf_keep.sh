#!/bin/zsh
#
# Keeps the firmware's ELF under the name a core dump knows it by:
# build/elf/<first 16 hex of its sha256>.elf, the last 10 of them.
#
#   ./tools/elf_keep.sh [build/amoledos.elf]
#
# A panic's core dump carries that same sha in its notes (16 characters since
# CONFIG_APP_RETRIEVE_LEN_ELF_SHA=16), and tools/coredump.sh decodes a dump
# only against the ELF it names: read against another build, an old dump
# looks like a new bug pointing at the wrong lines. install_fw.sh calls this
# for every firmware it installs, so the ELF of whatever the watch runs is
# here when it crashes. A release carries its own (amoledos.elf.xz).
set -e
ROOT=${0:a:h:h}
ELF=${1:-$ROOT/build/amoledos.elf}
[[ -f $ELF ]] || { echo "elf_keep: no $ELF"; exit 1; }
sha=$(shasum -a 256 $ELF | cut -c1-16)
mkdir -p $ROOT/build/elf
[[ -f $ROOT/build/elf/$sha.elf ]] || cp $ELF $ROOT/build/elf/$sha.elf
# the last 10, by date
old=($ROOT/build/elf/*.elf(Om))
(( ${#old} > 10 )) && rm -f ${old[1,-11]}
echo "   ELF kept as build/elf/$sha.elf"
