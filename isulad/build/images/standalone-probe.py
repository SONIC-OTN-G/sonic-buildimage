#!/usr/bin/python3
import ctypes,os,signal,sys,time
from pathlib import Path
from swsscommon import swsscommon
component=sys.argv[1]
assert component in ('otss','syncd')
for lib in ('libotaimeta.so','libotaimetadata.so', 'libotairedis.so' if component=='otss' else 'libotaivs.so'):
    ctypes.CDLL(lib, mode=ctypes.RTLD_GLOBAL)
    print('loaded '+lib,flush=True)
print(component+': ARM64 libraries loaded; standalone inspection process ready; business daemon not started',flush=True)
if '--check' in sys.argv: sys.exit(0)
ready=Path('/tmp/standalone-probe-ready')
ready.write_text(component+'\n')
running=True
def stop(sig,frame):
    global running
    running=False
signal.signal(signal.SIGTERM,stop)
signal.signal(signal.SIGINT,stop)
while running: time.sleep(0.25)
ready.unlink(missing_ok=True)
