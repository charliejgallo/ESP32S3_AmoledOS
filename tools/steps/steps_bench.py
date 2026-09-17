#!/usr/bin/env python3
"""Step detector bench: runs the candidate (the same arithmetic as the
firmware's, in float) over /api/imu dumps and prints the count, so the
parameters are chosen against counted walks and quiet minutes.

  python3 tools/steps/steps_bench.py file.csv [file.csv ...]
  python3 tools/steps/steps_bench.py --grid pocket.csv=100 wrist.csv=100..120
"""
import sys, csv, math, itertools

def load(path):
    rows=[r for r in csv.DictReader(open(path))]
    t=[int(r['t_ms']) for r in rows]
    mag=[math.sqrt(int(r['ax'])**2+int(r['ay'])**2+int(r['az'])**2)/1000 for r in rows]
    fw=[int(r['steps']) for r in rows]
    return t,mag,fw

def detect(t, mag, tau_g=2.0, tau_lp=0.16, thr_min=0.04, thr_frac=0.35,
           min_ms=330, max_ms=1500, need=4, trace=None):
    # defaults = the board's AOS_STEP_* (aos_step_detect.h); keep them equal
    """Steps in the signal. Time-based filters, so 10 Hz and 25 Hz behave alike.
      g:      slow low-pass = gravity (tau_g seconds)
      lp:     the dynamic part smoothed (tau_lp seconds) so a stride is one hump
      peak:   local maximum of lp above the threshold
      thr:    max(thr_min, thr_frac * recent peak height), so a strong pocket
              signal is not counted twice and a weak wrist one still counts
      rhythm: peaks min_ms..max_ms apart form a run; a run counts once it has
              'need' steps (all at once), a longer gap ends it
    """
    g=mag[0]; lp=0.0; prev_t=t[0]
    prev_lp=0.0; rising=False
    peak_avg=0.0; last_peak=-10**9; run=0; count=0
    for ts,m in zip(t,mag):
        dt=max(1,ts-prev_t)/1000.0; prev_t=ts
        g += (m-g)*min(1.0, dt/tau_g)
        d = m-g
        lp += (d-lp)*min(1.0, dt/tau_lp)
        thr = max(thr_min, thr_frac*peak_avg)
        if lp > prev_lp: rising=True
        elif rising and lp < prev_lp:
            rising=False
            h=prev_lp                      # the hump's top
            if h > thr:
                gap = ts-last_peak
                if gap >= min_ms:
                    if gap > max_ms: run=0
                    run += 1; last_peak=ts
                    if peak_avg==0: peak_avg=h
                    else: peak_avg += 0.2*(h-peak_avg)
                    if run==need: count+=need
                    elif run>need: count+=1
                    if trace is not None: trace.append((ts,h,run))
        prev_lp=lp
    return count

def main():
    args=sys.argv[1:]
    if args and args[0]=='--grid':
        targets=[]
        for a in args[1:]:
            f,rng=a.split('='); lo,_,hi=rng.partition('..'); hi=hi or lo
            targets.append((f,load(f),int(lo),int(hi)))
        best=[]
        for tau_lp,thr_min,thr_frac,min_ms,need in itertools.product((0.08,0.12,0.16,0.20),(0.04,0.06,0.08,0.10),(0.25,0.35,0.45),(300,330,380),(4,5,6)):
            res=[detect(d[0],d[1],tau_lp=tau_lp,thr_min=thr_min,thr_frac=thr_frac,min_ms=min_ms,need=need) for _,d,_,_ in targets]
            err=sum(max(0,lo-r,r-hi) for r,(_,_,lo,hi) in zip(res,targets))
            best.append((err,(tau_lp,thr_min,thr_frac,min_ms,need),res))
        best.sort()
        for err,p,res in best[:15]: print(f"err {err:3d}  tau_lp={p[0]} thr_min={p[1]} frac={p[2]} min_ms={p[3]} need={p[4]}  ->", res)
        return
    for f in args:
        t,mag,fw=load(f)
        print(f"{f}: firmware {fw[-1]-fw[0]}, candidate {detect(t,mag)}")

if __name__=='__main__': main()
