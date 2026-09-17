#!/usr/bin/env python3
"""Offline step detector on an /api/imu dump. Usage: steps_tune.py walk.csv [expected]"""
import sys, csv, math
rows=[r for r in csv.DictReader(open(sys.argv[1]))]
t=[int(r['t_ms']) for r in rows]; mag=[math.sqrt(int(r['ax'])**2+int(r['ay'])**2+int(r['az'])**2)/1000 for r in rows]
fw=[int(r['steps']) for r in rows]
print(f"{len(rows)} samples, {(t[-1]-t[0])/1000:.1f} s, firmware counted {fw[-1]-fw[0]} steps in the window")
# current firmware algorithm, re-run offline for reference
def current(mag,t,hi=1.18,lo=1.02,dead=250):
    n=0; above=False; last=-10**9
    for m,ts in zip(mag,t):
        if not above and m>hi:
            above=True
            if ts-last>dead: n+=1; last=ts
        elif above and m<lo: above=False
    return n
print("current algorithm offline:", current(mag,t))
# candidate: gravity removed by a slow low-pass, dynamic threshold, cadence gate
def candidate(mag,t,alpha=0.05,thr=0.12,minp=280,maxp=1500,need=4):
    g=mag[0]; n=0; pending=[]; last=-10**9; above=False; env=0.0
    for m,ts in zip(mag,t):
        g += alpha*(m-g)              # gravity + slow drift
        d = m-g                       # dynamic part
        env = max(abs(d), env*0.97)   # envelope
        th = max(thr, env*0.5)
        if not above and d>th:
            above=True
            if ts-last>minp:
                if ts-last>maxp: pending=[]   # the walk stopped: forget the run
                pending.append(ts); last=ts
                if len(pending)==need: n+=need
                elif len(pending)>need: n+=1
        elif above and d<0: above=False
    return n
for thr in (0.08,0.10,0.12,0.15,0.20):
    print(f"candidate thr={thr}: {candidate(mag,t,thr=thr)}")
