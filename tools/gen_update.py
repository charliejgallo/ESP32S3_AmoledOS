#!/usr/bin/env python3
"""
Writes amoledos-update.json, the file the watch reads to update itself
(Settings -> Update, components/aos_hal/aos_update.c).

    python3 tools/gen_update.py --ver v0.18.0 --notes-dir notes/ --out build/amoledos-update.json
    python3 tools/gen_update.py --ver v0.18.0 --notes-es "..." --notes-en "..." --notes-de "..."

It goes up to the release as one more asset, beside amoledos.bin: the watch
asks for .../releases/latest/download/amoledos-update.json, so only the latest
PUBLISHED release is ever offered, never a draft or a pre-release.

What it says:

    ver      the tag. The watch offers it when it is newer than what runs.
    url      where amoledos.bin is: the release's own address of the asset,
             with the tag in it, so a later release does not change what an
             earlier manifest points at.
    size, sha256
             of that amoledos.bin. The watch hashes the image as it arrives
             and sets it to boot only when both match.
    abi      AOS_ABI_VERSION of this firmware (components/aos_ui/include/
             aos_app.h). When it is not the running one's, the watch warns
             that the card's apps have to follow.
    notes_es, notes_en, notes_de
             a few lines about the version, shown under the offer, in the
             watch's language (English when its language has none). Keep
             them short: the watch keeps 640 bytes of each.

The image must be the app alone (build/amoledos.bin), the one /api/ota eats,
not amoledos-full.bin.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTES_MAX = 640


def abi_version():
    with open(os.path.join(REPO, "components/aos_ui/include/aos_app.h"), encoding="utf-8") as f:
        m = re.search(r"#define\s+AOS_ABI_VERSION\s+(\d+)", f.read())
    if not m:
        sys.exit("AOS_ABI_VERSION not found in aos_app.h")
    return int(m.group(1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", required=True, help="the release's tag, v0.18.0")
    ap.add_argument("--bin", default=os.path.join(REPO, "build", "amoledos.bin"))
    ap.add_argument("--repo", default="charliejgallo/ESP32S3_AmoledOS")
    ap.add_argument("--url", help="the image's address, when it is not the release's asset")
    ap.add_argument("--date", default=datetime.date.today().isoformat())
    ap.add_argument("--notes-dir", help="a folder with es.txt, en.txt and de.txt")
    for code in ("es", "en", "de"):
        ap.add_argument("--notes-" + code, default="")
    ap.add_argument("--out", default=os.path.join(REPO, "build", "amoledos-update.json"))
    a = ap.parse_args()

    if not re.match(r"^v\d+\.\d+\.\d+", a.ver):
        sys.exit("--ver must be a tag like v0.18.0")
    img = open(a.bin, "rb").read()
    if img[:1] != b"\xe9":
        sys.exit("%s is not an ESP app image (amoledos.bin, not the merged one)" % a.bin)

    notes = {}
    for code in ("es", "en", "de"):
        text = getattr(a, "notes_" + code)
        if a.notes_dir and os.path.exists(os.path.join(a.notes_dir, code + ".txt")):
            text = open(os.path.join(a.notes_dir, code + ".txt"), encoding="utf-8").read()
        text = text.strip()
        if len(text.encode("utf-8")) >= NOTES_MAX:
            sys.exit("the %s notes are %d bytes; the watch keeps %d" % (code, len(text.encode()), NOTES_MAX - 1))
        notes[code] = text

    m = {
        "schema": 1,
        "ver": a.ver,
        "date": a.date,
        "abi": abi_version(),
        "url": a.url or "https://github.com/%s/releases/download/%s/amoledos.bin" % (a.repo, a.ver),
        "size": len(img),
        "sha256": hashlib.sha256(img).hexdigest(),
    }
    for code in ("es", "en", "de"):
        if notes[code]:
            m["notes_" + code] = notes[code]

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        # one flat object, ASCII with \u escapes: the watch reads it with a
        # parser of six keys, not a JSON library
        json.dump(m, f, ensure_ascii=True, indent=1)
        f.write("\n")
    print("%s: %s, %d B, sha256 %s, abi %d" % (a.out, a.ver, m["size"], m["sha256"][:16], m["abi"]))


if __name__ == "__main__":
    main()
