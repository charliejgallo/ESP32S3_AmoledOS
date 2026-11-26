#!/usr/bin/env python3
"""
Builds the Store's catalogue, amoledos-store.json, and the release files it
points at (docs/STORE.md).

    python3 tools/gen_store.py --release v0.18.0 --apps <apps.zip, unzipped> \
        --lang <lang.zip, unzipped> --out build/store

What goes into --out is what is uploaded to the release, flat, the way GitHub
keeps a release's files: one file per piece, so the watch downloads, hashes
and puts each one in place by itself.

    amoledos-store.json      the catalogue
    clima.so, clima.html     an app and its page in the portal
    golf.pak, monsterhop.pak.1 ...   its packs, in the parts the card keeps
    face_agua.so ...         a watchface
    lang-en-aos.clima.lang   its translations (two languages share a name,
                             and a release's file names are one flat list)

Where each piece comes from:

    store/catalog.json       the category, the line about it, the prefix of
                             its pictures in docs/img: what the code does not say
    the simulator            the name and the icon (AIC bytes and colours), as
                             the app says them of itself: it is run once with
                             AOS_SIM_STOREDUMP (sim/build/amoledos_sim must be
                             built from this tree)
    sim/sim_fs/lang/{en,de}/_sistema.lang   the names in English and German
    --apps, --lang           the files themselves: the same apps.zip and
                             lang.zip the release carries

--base is where the watch will find the files: the release's own address by
default. For a test, any http:// address that serves --out
(python3 -m http.server), with the watch pointed at its catalogue.
"""
import argparse
import base64
import datetime
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(REPO, "sim", "build", "amoledos_sim")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def abi_version():
    with open(os.path.join(REPO, "components/aos_ui/include/aos_app.h"), encoding="utf-8") as f:
        return int(re.search(r"#define\s+AOS_ABI_VERSION\s+(\d+)", f.read()).group(1))


def sim_dump():
    """What every app and face says of itself, from the simulator."""
    if not os.path.exists(SIM):
        sys.exit("%s is missing: build the simulator first (docs/BUILDING.md)" % SIM)
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "dump.json")
        env = dict(os.environ, AOS_SIM_STOREDUMP=out)
        subprocess.run([SIM], cwd=os.path.join(REPO, "sim"), env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        if not os.path.exists(out):
            sys.exit("the simulator wrote no dump")
        d = json.load(open(out, encoding="utf-8"))
    return {a["id"]: a for a in d["apps"]}, {f["id"]: f for f in d["faces"]}


def names(code):
    """Spanish name -> translation, from the system catalogue of a language."""
    p = os.path.join(REPO, "sim", "sim_fs", "lang", code, "_sistema.lang")
    out = {}
    if os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 2 and not line.startswith("#"):
                out[parts[0]] = parts[1]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--release", required=True)
    ap.add_argument("--apps", required=True, help="apps.zip unzipped: the .so, .html and .pak, and faces/")
    ap.add_argument("--lang", required=True, help="lang.zip unzipped: en/, de/")
    ap.add_argument("--out", default=os.path.join(REPO, "build", "store"))
    ap.add_argument("--repo", default="charliejgallo/ESP32S3_AmoledOS")
    ap.add_argument("--base", help="where the files are; the release's address by default")
    ap.add_argument("--pictures", help="where docs/img is read from; raw.githubusercontent at the tag by default")
    a = ap.parse_args()

    base = a.base or "https://github.com/%s/releases/download/%s/" % (a.repo, a.release)
    if not base.endswith("/"):
        base += "/"
    pictures = a.pictures or "https://raw.githubusercontent.com/%s/%s/docs/img/" % (a.repo, a.release)
    src = json.load(open(os.path.join(REPO, "store", "catalog.json"), encoding="utf-8"))
    apps_dump, faces_dump = sim_dump()
    tr = {"en": names("en"), "de": names("de")}

    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    os.makedirs(a.out)

    def piece(local, dest, asset):
        """Copies a file into --out under its release name; its entry."""
        shutil.copyfile(local, os.path.join(a.out, asset))
        return {"dest": dest, "url": base + asset, "size": os.path.getsize(local), "sha256": sha256(local)}

    def langs(item_id):
        out = []
        for code in sorted(os.listdir(a.lang)):
            p = os.path.join(a.lang, code, item_id + ".lang")
            if re.match(r"^[a-z]{2}$", code) and os.path.exists(p):
                out.append(piece(p, "lang/%s/%s.lang" % (code, item_id), "lang-%s-%s.lang" % (code, item_id)))
        return out

    def shots(prefix):
        found = sorted(glob.glob(os.path.join(REPO, "docs", "img", prefix + "*.png")))
        # the exact name first (app-radio.png before app-radio-idle.png)
        found.sort(key=lambda p: (os.path.basename(p) != prefix + ".png", p))
        return [pictures + os.path.basename(p) for p in found[:4]]

    def name3(es):
        return {"es": es, "en": tr["en"].get(es, es), "de": tr["de"].get(es, es)}

    items, missing = [], []
    for e in src["apps"]:
        so = os.path.join(a.apps, e["file"] + ".so")
        d = apps_dump.get(e["id"])
        if not os.path.exists(so) or not d:
            missing.append("%s (%s)" % (e["file"], "no .so" if not os.path.exists(so) else "not in the simulator"))
            continue
        files = [piece(so, "apps/%s.so" % e["file"], e["file"] + ".so")]
        html = os.path.join(a.apps, e["file"] + ".html")
        if os.path.exists(html):
            files.append(piece(html, "apps/%s.html" % e["file"], e["file"] + ".html"))
        for pak in sorted(glob.glob(os.path.join(a.apps, e["file"] + ".pak*"))):
            n = os.path.basename(pak)
            if re.match(r"^[a-z0-9_]+\.pak(\.\d+)?$", n):
                files.append(piece(pak, "apps/" + n, n))
        files += langs(e["id"])
        items.append({
            "kind": "app", "id": e["id"], "file": e["file"], "cat": e["cat"],
            "name": name3(d["name"]), "about": e["about"],
            "icon": base64.b64encode(bytes.fromhex(d["icon"])).decode(),
            "colors": [d["color_a"], d["color_b"]],
            "shots": shots(e["shots"]),
            "size": sum(f["size"] for f in files), "files": files,
        })
    for e in src["faces"]:
        so = os.path.join(a.apps, "faces", e["file"] + ".so")
        d = faces_dump.get(e["id"])
        if not os.path.exists(so) or not d:
            missing.append("%s (%s)" % (e["file"], "no .so" if not os.path.exists(so) else "not in the simulator"))
            continue
        files = [piece(so, "faces/%s.so" % e["file"], e["file"] + ".so")]
        html = os.path.join(a.apps, "faces", e["file"] + ".html")
        if os.path.exists(html):
            files.append(piece(html, "faces/%s.html" % e["file"], e["file"] + ".html"))
        files += langs(e["id"])
        items.append({
            "kind": "face", "id": e["id"], "file": e["file"], "cat": "esferas",
            "name": name3(d["name"]), "about": e["about"],
            "shots": shots(e["shots"])[:1],
            "size": sum(f["size"] for f in files), "files": files,
        })

    # what the card has that the catalogue does not describe
    listed = {e["file"] for e in src["apps"]} | {e["file"] for e in src["faces"]}
    loose = sorted({os.path.basename(p)[:-3] for p in glob.glob(os.path.join(a.apps, "*.so"))} - listed)

    cat = {
        "schema": 1,
        "release": a.release,
        "date": datetime.date.today().isoformat(),
        "abi": abi_version(),
        "repo": a.repo,
        "cats": src["cats"],
        "items": items,
    }
    with open(os.path.join(a.out, "amoledos-store.json"), "w", encoding="utf-8") as f:
        json.dump(cat, f, ensure_ascii=False, separators=(",", ":"))
    total = sum(i["size"] for i in items)
    print("%s: %d apps, %d watchfaces, %d files, %.1f MB; catalogue %d KB" % (
        a.out, sum(i["kind"] == "app" for i in items), sum(i["kind"] == "face" for i in items),
        sum(len(i["files"]) for i in items), total / 1e6,
        os.path.getsize(os.path.join(a.out, "amoledos-store.json")) // 1024))
    if missing:
        print("left out: " + ", ".join(missing))
    if loose:
        print("on the card but not in store/catalog.json: " + ", ".join(loose))


if __name__ == "__main__":
    main()
