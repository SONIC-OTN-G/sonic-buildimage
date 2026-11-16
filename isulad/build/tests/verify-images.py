#!/usr/bin/env python3
"""Regression of newly built standalone images; Docker/QEMU does not replace device tests."""
import argparse,json,subprocess,time,uuid,os
from pathlib import Path

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--release',required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--allow-emulation',action='store_true');a=ap.parse_args()
    if a.output.exists():ap.error('output directory must not exist')
    a.output.mkdir(parents=True); report={'release':a.release,'environment':'local Docker','emulation':a.allow_emulation,'tests':{},'scope':'standalone only; no complete business loop or device acceptance'}
    log=(a.output/'test.log').open('w',buffering=1)
    def run(args):
        log.write('+ '+' '.join(map(str,args))+'\n');p=subprocess.run(list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);log.write(p.stdout)
        if p.returncode:raise RuntimeError('command failed: '+str(args)+' rc='+str(p.returncode))
        return p.stdout
    env=['-e','ALLOW_EMULATION=1'] if a.allow_emulation else []
    try:
        for component in ['database','otss','syncd']:
            tag='sonic-'+component+'-standalone:arm64-'+a.release;name='rebuild-test-'+component+'-'+uuid.uuid4().hex[:10]
            d=a.output/component;d.mkdir();d.chmod(0o777)
            image=json.loads(run(['docker','image','inspect',tag]))[0];assert image['Architecture']=='arm64'
            args=['docker','run','-d','--name',name,'--platform','linux/arm64','--network','none',*env,'-v',str(d.resolve())+':/probe-output:rw',tag]
            if component=='database' and a.allow_emulation:args+=['/etc/redis/sonic-standalone.conf','--ignore-warnings','ARM64-COW-BUG']
            try:
                run(args)
                def ex(*cmd):return run(['docker','exec',*env,name,*cmd])
                for phase in ['initial','restart']:
                    for attempt in range(30):
                        try:
                            if component=='database':assert ex('redis-cli','-s','/var/run/redis/redis.sock','PING').strip()=='PONG'
                            else:ex('test','-f','/tmp/standalone-probe-ready')
                            break
                        except (RuntimeError,AssertionError):
                            if attempt==29:raise
                            time.sleep(1)
                    assert ex('dpkg','--print-architecture').strip()=='arm64'
                    ex('python3','-c','from swsscommon import swsscommon;print("swsscommon-import-ok")')
                    if component=='database':
                        ex('redis-cli','-s','/var/run/redis/redis.sock','SET','rebuild_probe',phase)
                        assert ex('redis-cli','-s','/var/run/redis/redis.sock','GET','rebuild_probe').strip()==phase
                        ex('python3','-c','from swsscommon import swsscommon as s; d=s.DBConnector(0,"/var/run/redis/redis.sock",0); t=s.Table(d,"REBUILD_PROBE");t.set("key",s.FieldValuePairs([("value","ok")]));ok,v=t.get("key");assert ok and dict(v)["value"]=="ok";t._del("key");print("swsscommon-redis-read-write-ok")')
                    else:
                        ex('python3','/usr/local/bin/standalone-probe.py',component,'--check')
                        ex('python3','/usr/local/bin/check-business-binary.py',component)
                    ex('python3','-c','from pathlib import Path;Path("/probe-output/mount.txt").write_text("mount-ok")')
                    assert (d/'mount.txt').read_text()=='mount-ok'
                    if phase=='initial':run(['docker','stop',name]);run(['docker','start',name])
                report['tests'][component]={'status':'PASS','image_id':image['Id'],'checks':['arm64','exec','swsscommon import','bind mount','stop/start']+(['Redis and swsscommon read/write'] if component=='database' else ['shared libraries','actual business binary help'])}
            finally:
                for command in ['logs','inspect']:
                    result=subprocess.run(['docker',command,name],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);(d/(command+'.txt')).write_text(result.stdout)
                subprocess.run(['docker','rm','-f',name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        report['status']='PASS'
    except Exception as e:report['status']='FAIL';report['error']=str(e);raise
    finally:
        (a.output/'report.json').write_text(json.dumps(report,indent=2)+'\n');log.close();print(json.dumps(report,indent=2))
if __name__=='__main__':main()
