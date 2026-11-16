"""Run inside pmon. Mutates virtual hardware only; native daemons must report it."""
import fcntl,json,os,sys,tempfile,time
from pathlib import Path
import redis
p=Path('/lab-state/platform.json')
r=redis.Redis(unix_socket_path='/var/run/redis/redis.sock',db=6,decode_responses=True)
evidence=[]
def inject(kind,name,field,value):
 with p.with_suffix('.lock').open('a') as f:
  fcntl.flock(f,fcntl.LOCK_EX);d=json.loads(p.read_text());d['devices'][kind][name][field]=value
  fd,tmp=tempfile.mkstemp(dir=p.parent)
  with os.fdopen(fd,'w') as out:json.dump(d,out)
  os.replace(tmp,p)
def wait(label,fn,timeout=100):
 deadline=time.monotonic()+timeout
 while time.monotonic()<deadline:
  if fn():
   evidence.append({'test':label,'passed':True,'time_ns':time.time_ns()});print('PASS '+label,flush=True);return
  time.sleep(1)
 raise AssertionError(label)
def row(prefix):
 keys=list(r.scan_iter(prefix+'|*'))
 return r.hgetall(sorted(keys)[0]) if keys else {}
def temperature_is(value):
 actual=r.hget('TEMPERATURE_INFO|TEMP 1','temperature')
 try:return abs(float(actual)-float(value))<0.005
 except (TypeError,ValueError):return False
original=json.loads(p.read_text())
try:
 wait('native inventory',lambda:all(row(x) for x in ['PSU_INFO','FAN_INFO','TEMPERATURE_INFO','EEPROM_INFO','CHASSIS_MODULE_TABLE']))
 inject('psus','PSU 1','present',False)
 wait('PSU removed',lambda:row('PSU_INFO').get('presence')=='false')
 inject('psus','PSU 1','present',True)
 wait('PSU inserted',lambda:row('PSU_INFO').get('presence')=='true')
 inject('thermals','TEMP 1','temperature',78.0)
 wait('native temperature report',lambda:temperature_is(78))
 wait('thermal policy fan full speed',lambda:json.loads(p.read_text())['devices']['fans']['FAN 1']['target_speed']==100)
 inject('thermals','TEMP 1','temperature',35.0)
 wait('thermal policy recovery',lambda:json.loads(p.read_text())['devices']['fans']['FAN 1']['target_speed']==50)
 wait('native temperature recovery to 35 C',lambda:temperature_is(35),timeout=120)
 inject('modules','LINE-CARD1','present',False)
 wait('module removed',lambda:r.hget('CHASSIS_MODULE_TABLE|LINE-CARD1','oper_status')=='Empty')
 inject('modules','LINE-CARD1','present',True)
 wait('module inserted',lambda:r.hget('CHASSIS_MODULE_TABLE|LINE-CARD1','oper_status')=='Online')
finally:
 failed=sys.exc_info()[0] is not None
 try:
  for kind,name,field in [('psus','PSU 1','present'),('thermals','TEMP 1','temperature'),('modules','LINE-CARD1','present')]:
   inject(kind,name,field,original['devices'][kind][name][field])
  # Do not hide an earlier test failure behind a cleanup timeout.
  if not failed:
   wait('native temperature restored to original input',lambda:temperature_is(original['devices']['thermals']['TEMP 1']['temperature']),timeout=120)
 finally:
  Path('/lab-state/pmon-test.json').write_text(json.dumps(evidence,indent=2))
