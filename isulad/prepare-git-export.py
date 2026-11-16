#!/usr/bin/env python3
"""Copy release/source repository without nested Git metadata; never changes the original."""
import argparse,shutil
from pathlib import Path

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    source=Path(__file__).resolve().parent;dest=a.output.resolve()
    if dest==source or source in dest.parents:ap.error('output must be outside this repository')
    if dest.exists():ap.error('output must not exist')
    shutil.copytree(source,dest,symlinks=True,ignore=shutil.ignore_patterns('.git','__pycache__','.build-work','build-output'))
    print('Repository export ready:',dest)
    print('Original repository and nested Git history were not modified.')
if __name__=='__main__':main()
