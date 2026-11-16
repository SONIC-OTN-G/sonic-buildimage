#!/bin/bash
set -euo pipefail
cd /src/runc
gofmt -w libcontainer/process_linux.go
mkdir -p /src/bin
# Use upstream Go pathrs implementation (-libpathrs), retaining seccomp.
# This avoids adding a Rust shared library dependency to the offline bundle.
make static COMMIT=29dd3dc2b13b4123162e5fe132504bb4b15569f1 RUNC_BUILDTAGS=-libpathrs EXTRA_VERSION=sonic-offline-execroot1
cp runc /src/bin/runc-fixed.amd64
make static COMMIT=29dd3dc2b13b4123162e5fe132504bb4b15569f1 RUNC_BUILDTAGS=-libpathrs EXTRA_VERSION=sonic-offline-execroot1 GOARCH=arm64 CC=aarch64-linux-gnu-gcc PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig
cp runc /src/bin/runc-fixed.arm64
git diff --check
git diff > /src/runc-exec-root.patch
