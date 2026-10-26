import serial, sys, time
port = sys.argv[1]; baud = int(sys.argv[2]); secs = float(sys.argv[3])
s = serial.Serial(port, baud, timeout=0.2)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
end = time.time() + secs
while time.time() < end:
    d = s.read(4096)
    if d: sys.stdout.write(d.decode('ascii', 'replace').replace('\ufffd','?')); sys.stdout.flush()
