#!/usr/bin/env python3
"""A night on battery, recorded from the Mac over WiFi.

    python3 tools/battery_night.py 192.168.1.125                 # record, Ctrl-C to stop
    python3 tools/battery_night.py 192.168.1.125 --every 120     # poll interval, seconds
    python3 tools/battery_night.py --report night-2026-09-09.csv  # the morning after

Records /api/status and the PM mode statistics from /api/pmu?locks=1 every
--every seconds into a CSV, and prints one line per sample. The report reads
the CSV back and says what the night cost: percent and volts per hour, how
much of the time the chip was in light sleep, how often the screen woke, and
whether the board rebooted.

Run it under caffeinate so the Mac does not sleep:

    caffeinate -i python3 tools/battery_night.py 192.168.1.125

What it does to the measurement: one HTTP request every two minutes, which
wakes the chip for well under a second each time. Against eight hours it is
noise, and it is the same noise on every night, so nights compare.

The AXP2101 has no coulomb counter: 'percent' is its voltage-model gauge and
moves in whole steps; 'vbat' is the finer signal. Neither converts to mAh
without a discharge curve for this cell, so the report gives the cell's
nominal 300 mAh as a yardstick and says so.
"""
import argparse, csv, datetime, json, re, sys, time, urllib.request

FIELDS = ["ts", "uptime_s", "percent", "vbat", "vbus", "usb", "charging", "display",
          "panel_asleep", "light_sleep", "saving", "cpu_mhz", "board_temp",
          "drain_pct_h", "hours_left", "on_battery_s", "sleep_pct", "cpu_max_pct",
          "apb_max_pct", "heap", "ok"]

def get(base, path, timeout=12):
    with urllib.request.urlopen(base + path, timeout=timeout) as r:
        return r.read().decode()

def modes(txt):
    m = re.findall(r'Mode stats:(.*?)(?=\nSleep stats|\Z)', txt, re.S)
    out = {}
    if not m:
        return out
    for line in m[-1].splitlines():
        p = line.split()
        if len(p) >= 4 and p[0] in ('SLEEP', 'APB_MIN', 'APB_MAX', 'CPU_MAX', 'LIGHT_SLEEP'):
            out[p[0]] = int(p[3]) if p[3].isdigit() else int(p[2])
    return out

def record(base, every, out_path):
    prev_modes = None
    with open(out_path, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        if f.tell() == 0:
            w.writeheader()
        print("recording to %s every %d s, Ctrl-C to stop" % (out_path, every))
        while True:
            row = {k: "" for k in FIELDS}
            row["ts"] = datetime.datetime.now().isoformat(timespec="seconds")
            try:
                st = json.loads(get(base, "/api/status"))
                for k in ("uptime_s", "percent", "vbat", "vbus", "usb", "charging", "display",
                          "panel_asleep", "light_sleep", "saving", "cpu_mhz", "board_temp",
                          "drain_pct_h", "hours_left", "on_battery_s", "heap"):
                    row[k] = st.get(k, "")
                if "battery" in st and row["percent"] == "":
                    row["percent"] = st["battery"]
                cur = modes(get(base, "/api/pmu?locks=1"))
                if prev_modes and cur and cur.get("SLEEP", 0) >= prev_modes.get("SLEEP", 0):
                    tot = sum(cur.get(k, 0) - prev_modes.get(k, 0) for k in cur)
                    if tot > 0:
                        row["sleep_pct"] = round(100.0 * (cur.get("SLEEP", 0) - prev_modes.get("SLEEP", 0)) / tot, 1)
                        row["cpu_max_pct"] = round(100.0 * (cur.get("CPU_MAX", 0) - prev_modes.get("CPU_MAX", 0)) / tot, 1)
                        row["apb_max_pct"] = round(100.0 * (cur.get("APB_MAX", 0) - prev_modes.get("APB_MAX", 0)) / tot, 1)
                prev_modes = cur or prev_modes
                row["ok"] = 1
                print("%s  %3s%%  %.3f V  %s  sleep %s%%  cpu %s  disp %s  up %ss" % (
                    row["ts"][11:], row["percent"], float(row["vbat"] or 0),
                    "USB" if row["usb"] else "bat",
                    row["sleep_pct"] if row["sleep_pct"] != "" else "-", row["cpu_mhz"],
                    row["display"], row["uptime_s"]))
            except Exception as e:
                row["ok"] = 0
                print("%s  unreachable: %s" % (row["ts"][11:], e))
            w.writerow(row)
            f.flush()
            time.sleep(every)

def report(path, mah):
    rows = [r for r in csv.DictReader(open(path)) if r["ok"] == "1" and r["vbat"]]
    if len(rows) < 2:
        print("not enough samples in", path); return
    on_bat = [r for r in rows if r["usb"] in ("False", "false", "0", "")]
    if len(on_bat) < 2:
        print("no stretch on battery in", path); return
    t = lambda r: datetime.datetime.fromisoformat(r["ts"])
    a, b = on_bat[0], on_bat[-1]
    hours = (t(b) - t(a)).total_seconds() / 3600.0
    dp = float(a["percent"]) - float(b["percent"])
    dv = float(a["vbat"]) - float(b["vbat"])
    sleeps = [float(r["sleep_pct"]) for r in on_bat if r["sleep_pct"]]
    wakes = sum(1 for r in on_bat if r["display"] not in ("2", "", "off"))
    reboots = sum(1 for x, y in zip(on_bat, on_bat[1:])
                  if x["uptime_s"] and y["uptime_s"] and float(y["uptime_s"]) < float(x["uptime_s"]))
    lost = sum(1 for r in csv.DictReader(open(path)) if r["ok"] != "1")
    print("On battery from %s to %s: %.2f h" % (a["ts"], b["ts"], hours))
    print("  percent  %s%% -> %s%%   (%.1f %% in total, %.2f %%/h)" % (a["percent"], b["percent"], dp, dp / hours if hours else 0))
    print("  vbat     %.3f V -> %.3f V   (%.0f mV in total, %.1f mV/h)" % (float(a["vbat"]), float(b["vbat"]), 1000 * dv, 1000 * dv / hours if hours else 0))
    if hours and dp > 0:
        print("  against a nominal %d mAh cell: ~%.0f mAh, ~%.1f mA average (yardstick, not a measurement)" % (mah, mah * dp / 100.0, mah * dp / 100.0 / hours))
        print("  a full charge at this rate would last ~%.0f h" % (100.0 / (dp / hours)))
    if sleeps:
        print("  light sleep: %.0f%% of the time on average (min %.0f%%, max %.0f%%) over %d intervals" % (
            sum(sleeps) / len(sleeps), min(sleeps), max(sleeps), len(sleeps)))
    print("  samples with the screen not off: %d of %d   reboots: %d   unreachable polls: %d" % (wakes, len(on_bat), reboots, lost))
    temps = [float(r["board_temp"]) for r in on_bat if r["board_temp"] not in ("", "-1.0")]
    if temps:
        print("  board temperature %.1f..%.1f C" % (min(temps), max(temps)))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", help="board IP or name")
    ap.add_argument("--every", type=int, default=120, help="seconds between samples")
    ap.add_argument("--out", help="CSV to append to (default night-<date>.csv)")
    ap.add_argument("--report", help="summarise this CSV instead of recording")
    ap.add_argument("--mah", type=int, default=300, help="nominal cell capacity for the yardstick")
    args = ap.parse_args()
    if args.report:
        report(args.report, args.mah); return
    if not args.host:
        ap.error("host is required to record")
    base = "http://" + args.host
    out = args.out or "night-%s.csv" % datetime.date.today().isoformat()
    try:
        record(base, args.every, out)
    except KeyboardInterrupt:
        print("\nstopped; report with: python3 tools/battery_night.py --report %s" % out)

if __name__ == "__main__":
    main()
