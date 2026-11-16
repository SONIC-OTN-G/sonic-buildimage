#!/usr/bin/env python3
"""Import clean committed module trees from sonic-buildimage; refuses dirty sources."""
import argparse,io,json,subprocess,tarfile,shutil,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def git(p,*args):return subprocess.check_output(['git','-C',str(p),*args])
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--sonic-src',type=Path,required=True);ap.add_argument('--replace-snapshots',action='store_true',required=True);a=ap.parse_args();src=a.sonic_src.resolve()
    mapping={'sonic-swss-common':src/'src/sonic-swss-common','sonic-otairedis':src/'src/sonic-otairedis','sonic-otss':src/'src/sonic-otss','docker-database':src/'dockers/docker-database'}
    commits={}
    # Validate every source before replacing any snapshot. Submodule dirtiness is also rejected.
    for name,p in mapping.items():
        if not p.is_dir():raise SystemExit('Missing source: '+str(p))
        if git(p,'status','--porcelain').strip():raise SystemExit('Commit source changes before importing: '+name)
        commits[name]=git(p,'rev-parse','HEAD').decode().strip()
    otai=src/'src/sonic-otairedis/OTAI'
    if not otai.is_dir() or not any(otai.iterdir()):raise SystemExit('Initialize OTAI submodule first')
    if git(otai,'status','--porcelain').strip():raise SystemExit('OTAI worktree is dirty')
    commits['OTAI']=git(otai,'rev-parse','HEAD').decode().strip()
    with tempfile.TemporaryDirectory(prefix='isulad-import-') as temp:
        staging=Path(temp)
        for name,p in mapping.items():
            dest=staging/name;dest.mkdir()
            args=['archive','HEAD']
            if name=='docker-database':args=['archive','HEAD:dockers/docker-database'];p=src
            with tarfile.open(fileobj=io.BytesIO(git(p,*args))) as t:t.extractall(dest,filter='data')
        with tarfile.open(fileobj=io.BytesIO(git(otai,'archive','HEAD'))) as t:t.extractall(staging/'sonic-otairedis/OTAI',filter='data')
        # Upstream ignore rules must not omit vendored build inputs from the outer Git repository.
        for f in staging.rglob('.gitignore'):f.rename(f.with_name('UPSTREAM.gitignore'))
        for name in mapping:
            dst=ROOT/'sources'/name
            if dst.exists():shutil.rmtree(dst)
            shutil.copytree(staging/name,dst,symlinks=True)
    origin={'scope':'explicit committed source import','versions':commits,'sonic_buildimage_commit':git(src,'rev-parse','HEAD').decode().strip()}
    (ROOT/'sources/SOURCE-ORIGIN.json').write_text(json.dumps(origin,indent=2)+'\n')
    (ROOT/'locks/ot-source-lock.json').write_text(json.dumps({k:commits[k] for k in ('sonic-otairedis','OTAI','sonic-otss')},indent=2)+'\n')
    prov=json.loads((ROOT/'locks/provenance.json').read_text());prov.update(sonic_buildimage_commit=origin['sonic_buildimage_commit'],swsscommon_commit=commits['sonic-swss-common'],native_device_validation='NOT RUN for imported version')
    (ROOT/'locks/provenance.json').write_text(json.dumps(prov,indent=2)+'\n')
    print('Imported source snapshots. Review diff, then run build/scripts/inputs.py refresh.')
if __name__=='__main__':main()
