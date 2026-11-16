#!/bin/sh
set -eu
for f in /inbox/docker-*-standalone-arm64.tar.gz; do
 [ -f "$f" ] || { echo 'No SONiC image archives in inbox/'; exit 1; }
 tmp=$(mktemp /inbox/import-XXXXXX.tar)
 gzip -dc "$f" > "$tmp"
 isula load -i "$tmp"
 rm -f "$tmp"
done
