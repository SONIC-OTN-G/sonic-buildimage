#!/usr/bin/env python3
"""Verify or deliberately refresh repository input checksums; no build output included."""
import argparse,hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def entries():
    for f in sorted(ROOT.rglob('*')):
        if f.is_file() and not f.is_symlink() and f.name!='SOURCE-SHA256SUMS' and '__pycache__' not in f.parts:
            yield f
def main():
    ap=argparse.ArgumentParser();ap.add_argument('action',choices=['verify','refresh']);a=ap.parse_args()
    lock=ROOT/'SOURCE-SHA256SUMS'
    actual={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in entries()}
    if a.action=='refresh':
        lock.write_text(''.join(v+'  '+k+'\n' for k,v in actual.items()));print('Refreshed',len(actual),'inputs');return
    expected={line.split('  ',1)[1]:line.split('  ',1)[0] for line in lock.read_text().splitlines()}
    bad=[k for k in expected.keys()|actual.keys() if expected.get(k)!=actual.get(k)]
    if bad: raise SystemExit('Input mismatch (review changes before refresh):\n'+'\n'.join(sorted(bad)))
    print('PASS:',len(actual),'input hashes')
if __name__=='__main__':main()
