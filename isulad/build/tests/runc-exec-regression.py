#!/usr/bin/env python3
"""Run as root; all mounts are in a disposable namespace. No target SSH needed.
Usage: regression.py RUNC_BINARY STATIC_NATIVE_BUSYBOX
Covers outer chroot + no-pivot, exec root, cwd/user, seccomp, fd leaks,
stop/recreate, and an exec against a stopped container (must fail promptly).
"""
import json,os,pathlib,shutil,subprocess,sys,tempfile
runc,bb=map(pathlib.Path,sys.argv[1:])
p=pathlib.Path(tempfile.mkdtemp(prefix='offline-exec-regression-'))
o=p/'outer';r=o/'bundle/rootfs'
for d in [o/'proc',o/'sys',o/'dev',o/'state',r/'bin',r/'proc',r/'dev',r/'tmp']:
 d.mkdir(parents=True,exist_ok=True)
shutil.copy2(runc,o/'runc');shutil.copy2(bb,r/'bin/busybox')
(r/'bin/sh').symlink_to('busybox');(r/'marker').write_text('BUSINESS_ROOT\n');(r/'tmp').chmod(0o1777)
caps=['CAP_CHOWN','CAP_DAC_OVERRIDE','CAP_FOWNER','CAP_FSETID','CAP_KILL','CAP_SETGID','CAP_SETUID','CAP_SETPCAP','CAP_SYS_CHROOT','CAP_MKNOD','CAP_AUDIT_WRITE','CAP_SETFCAP']
c={'ociVersion':'1.0.2','root':{'path':'rootfs'},'process':{'terminal':False,'user':{'uid':0,'gid':0},'args':['/bin/busybox','sleep','300'],'env':['PATH=/bin:/usr/bin'],'cwd':'/','capabilities':{x:caps for x in ['bounding','effective','permitted']}},'mounts':[{'destination':'/proc','type':'proc','source':'proc'},{'destination':'/dev','type':'tmpfs','source':'tmpfs','options':['nosuid','mode=755']}],'linux':{'namespaces':[{'type':x} for x in ['pid','mount','uts','ipc']],'seccomp':{'defaultAction':'SCMP_ACT_ALLOW','syscalls':[{'names':['ptrace'],'action':'SCMP_ACT_ERRNO','errnoRet':1}]}}}
(o/'bundle/config.json').write_text(json.dumps(c))
s=r'''
set -eu
mount --make-rprivate /
R=$1
mount --bind "$R" "$R"
mount --bind /proc "$R/proc"
mount --rbind /sys "$R/sys"
mount --rbind /dev "$R/dev"
rr() { chroot "$R" /runc --root /state "$@"; }
trap 'rr delete -f regression >/dev/null 2>&1 || true' EXIT
for cycle in first recreate; do
 rr run -d --no-pivot --bundle /bundle regression
 P=$(rr state regression | python3 -c 'import json,sys;print(json.load(sys.stdin)["pid"])')
 A=$(stat -Lc '%d:%i' /proc/$P/root)
 B=$(rr exec regression /bin/busybox stat -Lc '%d:%i' /)
 [ "$A" = "$B" ]
 rr exec regression /bin/busybox sh -c 'test "$(/bin/busybox cat /marker)" = BUSINESS_ROOT; test -d /proc/$$'
 [ "$(rr exec --user 65534:65534 --cwd /tmp regression /bin/busybox pwd)" = /tmp ]
 [ "$(rr exec --user 65534:65534 regression /bin/busybox id -u)" = 65534 ]
 rr exec regression /bin/busybox sh -c '/bin/busybox grep -E "^Seccomp:[[:space:]]+2$" /proc/self/status'
 # fd 3 may be the listing command transiently opening /proc; none may
 # point at container-init-root, the host root or an outside directory.
 rr exec regression /bin/busybox sh -c 'for f in /proc/self/fd/*; do n=${f##*/}; [ "$n" -le 2 ] && continue; t=$(/bin/busybox readlink "$f") || continue; case "$t" in /proc/*/fd) ;; *) echo "unexpected-fd:$n:$t"; exit 1;; esac; done'
 rr kill regression KILL
 sleep 1
 if timeout 8 chroot "$R" /runc --root /state exec regression /bin/busybox true; then
  echo FAIL_stopped_exec_accepted; exit 1
 else
  RC=$?; [ "$RC" != 124 ] || { echo FAIL_stopped_exec_hung; exit 1; }
 fi
 rr delete regression
 echo "PASS $cycle root=$A user/cwd/proc/seccomp/fd/stopped-exec"
done
'''
print('WORKDIR',p,flush=True)
result=subprocess.run(['unshare','-m','--','bash','-c',s,'test',str(o)],timeout=90)
sys.exit(result.returncode)
