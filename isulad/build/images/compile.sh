#!/bin/bash
set -euo pipefail
test "$(dpkg --print-architecture)" = arm64
cp -a /input/. /src/
./autogen.sh
./configure --prefix=/usr --disable-static --disable-python2 --disable-yangmodules PYTHON=/usr/bin/python3 PYTHON3=/usr/bin/python3
make -j4 CXXFLAGS="-O0 -g0" DBGFLAGS="-DNDEBUG" common/libswsscommon.la pyext/py3/_swsscommon.la sonic-db-cli/sonic-db-cli common/swssloglevel
mkdir -p /output/rootfs/usr/lib/python3/dist-packages/swsscommon /output/rootfs/usr/lib/aarch64-linux-gnu /output/rootfs/usr/bin /output/rootfs/usr/share/swss /output/rootfs/var/run/redis/sonic-db
cp -a common/.libs/libswsscommon.so* /output/rootfs/usr/lib/aarch64-linux-gnu/
cp pyext/py3/.libs/_swsscommon.so pyext/py3/swsscommon.py pyext/py3/__init__.py /output/rootfs/usr/lib/python3/dist-packages/swsscommon/
cp sonic-db-cli/.libs/sonic-db-cli common/.libs/swssloglevel /output/rootfs/usr/bin/
cp common/*.lua /output/rootfs/usr/share/swss/
cp common/database_config.json /output/rootfs/var/run/redis/sonic-db/
find /output/rootfs -type f -exec file {} + > /output/compiled-files.txt
