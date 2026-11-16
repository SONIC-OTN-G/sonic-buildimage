#!/bin/sh
# Uses bundled BusyBox: target does not need a system sha256sum or Python.
# Hashes detect transfer errors/stale files, not authenticity by themselves.
set -eu
case "$0" in */*) D=${0%/*};; *) D=.;; esac
cd "$D"
exec ./bin/busybox sha256sum -c SHA256SUMS
