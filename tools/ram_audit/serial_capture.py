import sys, time, serial, serial.tools.list_ports
port = sys.argv[1]; out = sys.argv[2]; secs = float(sys.argv[3])
end = time.time() + secs
f = open(out, 'ab', buffering=0)
while time.time() < end:
    try:
        s = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        time.sleep(0.5); continue
    try:
        while time.time() < end:
            d = s.read(4096)
            if d:
                f.write(d); f.flush()
    except Exception as e:
        f.write(("\n[serial error: %s]\n" % e).encode()); f.flush()
        try: s.close()
        except: pass
        time.sleep(0.5)
f.close()
