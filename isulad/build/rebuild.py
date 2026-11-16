#!/usr/bin/env python3
"""Rebuild standalone ARM64 images and patched-runtime offline engine from repository inputs."""
import argparse, hashlib, json, os, shutil, subprocess, tarfile, tempfile, gzip, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def run(args, **kw):
    print('+', ' '.join(map(str,args)), flush=True)
    subprocess.run(list(map(str,args)),check=True,**kw)
def copy(src,dst):
    if src.is_dir(): shutil.copytree(src,dst,symlinks=True,dirs_exist_ok=True)
    else: dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
def export_image(tag,out):
    raw=out.with_suffix('')
    run(['docker','save','-o',raw,tag])
    with raw.open('rb') as f, out.open('wb') as g:
        with gzip.GzipFile(filename='',mode='wb',fileobj=g,mtime=0,compresslevel=1) as z:shutil.copyfileobj(f,z)
    raw.unlink()
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('target',choices=['images','engine','all'])
    ap.add_argument('--release',required=True)
    ap.add_argument('--work',required=True,help='new empty directory on Linux filesystem')
    ap.add_argument('--ot-builder',help='explicit preinstalled compiler image; skips building compiler')
    ap.add_argument('--runc-builder',help='explicit preinstalled compiler image')
    ap.add_argument('--runtime-base',help='explicit prepared ARM64 Debian runtime dependency image')
    a=ap.parse_args()
    import re
    if not re.fullmatch(r'[a-z0-9][a-z0-9_.-]*',a.release):ap.error('invalid release')
    run([sys.executable,ROOT/'scripts/inputs.py','verify'])
    w=Path(a.work).resolve()
    if w.exists():ap.error('work directory must not exist; never reuses old output')
    w.mkdir(parents=True);out=w/'dist';out.mkdir();ctx=w/'images';ctx.mkdir()
    results={'release':a.release,'source_manifest':hashlib.sha256((ROOT/'SOURCE-SHA256SUMS').read_bytes()).hexdigest(),'validation':'build only; run regression before deployment','outputs':{}}
    if a.target in ('images','all'):
        copy(ROOT/'sources',ctx/'sources')
        copy(ROOT/'images',ctx)
        copy(ROOT/'locks/provenance.json',ctx/'provenance.json')
        copy(ROOT/'locks/ot-source-lock.json',ctx/'ot-source-lock.json')
        target=ctx/'target';target.mkdir()
        builder=a.ot_builder
        if not builder:
            sw='isulad-rebuild-swss-builder:'+a.release
            run(['docker','build','--platform','linux/arm64','-f',ctx/'Dockerfile.build','-t',sw,ctx])
            df=ctx/'Dockerfile.ot-builder';df.write_text(df.read_text().replace('sonic-arm64-swss-builder:20261022',sw))
            builder='isulad-rebuild-ot-builder:'+a.release
            run(['docker','build','--platform','linux/arm64','-f',df,'-t',builder,ctx])
        for script,bindings,wd in [('compile.sh',[('sources/sonic-swss-common','/input'),('target','/output')],'/src'),('compile-ot.sh',[('sources','/sources'),('target','/output')],'/src')]:
            cmd=['docker','run','--rm','--platform','linux/arm64','--network','none','-w',wd]
            for rel,dest in bindings:cmd+=['-v',str(ctx/rel)+':'+dest+('' if rel=='target' else ':ro')]
            cmd+=['-v',str(ctx/script)+':/compile.sh:ro',builder,'bash','/compile.sh'];run(cmd)
        db='sonic-database-standalone:arm64-'+a.release
        df=ctx/'Dockerfile';s=df.read_text().replace('sources/sonic-buildimage/dockers/docker-database/','sources/docker-database/')
        if a.runtime_base:
            s='FROM '+a.runtime_base+'\n'+s[s.index('COPY target/rootfs/'):]
        s=s.replace('a77538a37517b9a953e32191a77e83c6b5a11c14',results['source_manifest'])
        df.write_text(s)
        run(['docker','build','--platform','linux/arm64','-t',db,ctx])
        for rel in ['ot-rootfs','otss-rootfs']:copy(target/rel,ctx/rel)
        for f in ctx.rglob('*.la'):f.unlink()
        (ctx/'licenses').mkdir(exist_ok=True)
        for src,name in [('sonic-otairedis/LICENSE','otairedis-LICENSE'),('sonic-otairedis/OTAI/LICENSE.txt','OTAI-LICENSE'),('sonic-otss/LICENSE','otss-LICENSE')]:copy(ctx/'sources'/src,ctx/'licenses'/name)
        tags={'database':db}
        for component in ['otss','syncd']:
            df=ctx/('Dockerfile.'+component);df.write_text(df.read_text().replace('sonic-database-standalone:arm64-20261022',db))
            tag='sonic-'+component+'-standalone:arm64-'+a.release
            run(['docker','build','--platform','linux/arm64','--network','none','-f',df,'-t',tag,ctx]);tags[component]=tag
        for component,tag in tags.items():
            archive=out/('docker-'+component+'-standalone-arm64.tar.gz');export_image(tag,archive)
            results['outputs'][tag]=json.loads(subprocess.check_output(['docker','image','inspect',tag]))[0]['Id']
        results['ot_builder']=builder;results['runtime_base_override']=a.runtime_base
        results['ot_builder_id']=json.loads(subprocess.check_output(['docker','image','inspect',builder]))[0]['Id']
    if a.target in ('engine','all'):
        e=w/'engine';e.mkdir();rs=e/'runc';rs.mkdir()
        run(['tar','-xf',ROOT/'sources/runc-v1.5.2.tar','-C',rs])
        run(['git','apply',ROOT/'engine/runc-exec-root.patch'],cwd=rs)
        copy(ROOT/'engine/build-runc.sh',e/'build-runc.sh')
        # git archive has no .git; build uses explicit version information.
        script=e/'build-runc.sh';script.write_text(script.read_text().replace('git diff --check','true').replace('git diff > /src/runc-exec-root.patch','true'))
        builder=a.runc_builder or 'isulad-rebuild-runc-builder:'+a.release
        if not a.runc_builder:run(['docker','build','-f',ROOT/'engine/Dockerfile.builder','-t',builder,ROOT/'engine'])
        run(['docker','run','--rm','--network','none','-v',str(e)+':/src',builder,'bash','/src/build-runc.sh'])
        package=e/'isulad-offline-arm64';(package/'rootfs').mkdir(parents=True)
        run(['tar','-xJf',ROOT/'engine/rootfs-arm64.tar.xz','-C',package/'rootfs'])
        for n in sorted(json.loads((ROOT/'engine/rootfs-delete.json').read_text()),key=len,reverse=True):
            dest=package/'rootfs'/n
            if dest.is_symlink() or dest.is_file():dest.unlink()
            elif dest.is_dir():
                try:dest.rmdir()
                except OSError:pass
        copy(ROOT/'engine/overlay',package)
        for n,link in json.loads((ROOT/'engine/overlay-links.json').read_text()).items():
            dest=package/n
            if dest.is_symlink() or dest.is_file():dest.unlink()
            dest.parent.mkdir(parents=True,exist_ok=True);dest.symlink_to(link)
        copy(e/'bin/runc-fixed.arm64',package/'rootfs/usr/libexec/runc-real')
        # Device tests must exercise this release, never the older preinstalled image tags.
        for name in ['test-container.sh','test-ot-containers.sh']:
            f=package/'rootfs/tests'/name
            f.write_text(f.read_text().replace('arm64-20261022','arm64-'+a.release).replace('arm64-20261023','arm64-'+a.release))
        for n in ['data','run','logs','evidence','inbox']:(package/n).mkdir(exist_ok=True)
        copy(ROOT/'engine/arm64-smoke.tar.gz',package/'inbox/arm64-smoke.tar.gz')
        for f in [package/'bin/busybox',package/'rootfs/usr/libexec/runc-real']:f.chmod(0o755)
        for f in (package/'scripts').glob('*.sh'):f.chmod(0o755)
        # Windows checkout cannot preserve every POSIX mode: apply recorded overlay modes.
        for n,mode in json.loads((ROOT/'engine/overlay-modes.json').read_text()).items():
            dest=package/n
            if dest.exists() and not dest.is_symlink():dest.chmod(mode)
        (package/'BUILD-RELEASE.json').write_text(json.dumps({'release':a.release,'source_manifest':results['source_manifest'],'images':{n:'sonic-'+n+'-standalone:arm64-'+a.release for n in ['database','otss','syncd']},'note':'Bundled historical reports describe the previous release; this rebuild requires its own device acceptance.'},indent=2)+'\n')
        version=package/'VERSIONS.txt';version.write_text(version.read_text()+'\nRebuild release: '+a.release+'\nActual runc-real SHA256: '+hashlib.sha256((package/'rootfs/usr/libexec/runc-real').read_bytes()).hexdigest()+'\n')
        files=sorted(f for f in package.rglob('*') if f.is_file() and not f.is_symlink() and f.name!='SHA256SUMS')
        (package/'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+str(f.relative_to(package))+'\n' for f in files))
        run(['tar','-cJf',out/('isulad-offline-arm64-'+a.release+'.tar.xz'),'-C',e,'isulad-offline-arm64'])
        results['runc_builder']=builder
        results['runc_builder_id']=json.loads(subprocess.check_output(['docker','image','inspect',builder]))[0]['Id']
        results['runc_real_sha256']=hashlib.sha256((package/'rootfs/usr/libexec/runc-real').read_bytes()).hexdigest()
    (out/'release.json').write_text(json.dumps(results,indent=2))
    (out/'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\n' for f in sorted(out.iterdir()) if f.is_file() and f.name!='SHA256SUMS'))
    print('BUILD COMPLETE:',out)
if __name__=='__main__':main()
