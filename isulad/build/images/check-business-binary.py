#!/usr/bin/python3
import os,subprocess,sys,time
from pathlib import Path
component=sys.argv[1]
assert component in ('otss','syncd')
exe='/usr/bin/orchagent' if component=='otss' else '/usr/bin/syncd'
emulator=os.environ.get('ARM64_TEST_QEMU')
prefix=[emulator] if emulator else []
redis_args=prefix+['/usr/bin/redis-server','/etc/redis/sonic-standalone.conf']
if emulator or os.environ.get('ALLOW_EMULATION')=='1':
    redis_args+=['--ignore-warnings','ARM64-COW-BUG']
sock=Path('/var/run/redis/redis.sock')
assert not sock.exists(), 'Unexpected existing Redis socket; refuse to touch it'
proc=subprocess.Popen(redis_args)
try:
    for _ in range(100):
        if proc.poll() is not None: raise RuntimeError('Private Redis exited')
        if sock.exists(): break
        time.sleep(.1)
    assert sock.exists(), 'Private Redis not ready'
    # This pinned OTSS revision declares h: in getopt, so -h requires a value.
    help_args=[exe,'-h','probe'] if component=='otss' else [exe,'-h']
    out=subprocess.run(prefix+help_args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
    print(out.stdout,flush=True)
    assert out.returncode==0, (exe,out.returncode)
    assert 'usage' in out.stdout.lower(), out.stdout
    print('PASS: '+component+' binary help executed with private in-container Redis; no inter-container communication',flush=True)
finally:
    proc.terminate()
    try: proc.wait(timeout=10)
    except subprocess.TimeoutExpired: proc.kill();proc.wait()
