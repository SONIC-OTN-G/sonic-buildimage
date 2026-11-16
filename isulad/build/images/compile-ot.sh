#!/bin/bash
set -euo pipefail
test "$(dpkg --print-architecture)" = arm64
mkdir -p /usr/include/swss/c-api /usr/lib/aarch64-linux-gnu /work /output/ot-rootfs /output/otss-rootfs
cp /sources/sonic-swss-common/common/*.h /usr/include/swss/
cp /sources/sonic-swss-common/common/c-api/*.h /usr/include/swss/c-api/
cp -a /output/rootfs/usr/lib/aarch64-linux-gnu/libswsscommon.so* /usr/lib/aarch64-linux-gnu/
ldconfig
if [ ! -f /work/sonic-otairedis/config.status ]; then
    cp -a /sources/sonic-otairedis /work/
    cd /work/sonic-otairedis
    ./autogen.sh
    ./configure --prefix=/usr --disable-static --with-otai=vs --with-swss-common-inc=/usr/include/swss --with-swss-common-lib=/usr/lib/aarch64-linux-gnu CFLAGS='-O0 -g0' CXXFLAGS='-O0 -g0'
fi
cd /work/sonic-otairedis
if [ ! -f .doxygen-updated ]; then
    doxygen -u OTAI/meta/Doxyfile
    touch .doxygen-updated
fi
grep -q 'Wno-error=address' OTAI/meta/Makefile || printf '\nCFLAGS += -Wno-error=address\n' >> OTAI/meta/Makefile
if [ ! -f .arm64-build-complete ]; then
    make -j8 CXXFLAGS='-O0 -g0' DBGFLAGS='-DNDEBUG'
    touch .arm64-build-complete
fi
mkdir -p /output/ot-rootfs/usr/lib /output/ot-rootfs/usr/bin
cp -a meta/.libs/libotaimeta.so* meta/.libs/libotaimetadata.so* lib/.libs/libotairedis.so* vslib/.libs/libotaivs.so* /output/ot-rootfs/usr/lib/
cp -a /output/ot-rootfs/usr/lib/*.so* /usr/lib/
cp syncd/.libs/syncd syncd/syncd_request_shutdown /output/ot-rootfs/usr/bin/
mkdir -p /usr/include/otai
cp OTAI/inc/*.h OTAI/meta/*.h meta/*.h lib/otairedis.h vslib/otaivs.h /usr/include/otai/
ldconfig
if [ ! -f /work/sonic-otss/config.status ]; then
    cp -a /sources/sonic-otss /work/
    cd /work/sonic-otss
    ./autogen.sh
    ./configure --prefix=/usr CFLAGS='-O0 -g0' CXXFLAGS='-O0 -g0'
fi
cd /work/sonic-otss
make -j8 CXXFLAGS='-O0 -g0' DBGFLAGS='-DNDEBUG'
make install DESTDIR=/output/otss-rootfs
find /output/ot-rootfs /output/otss-rootfs -type f -exec file {} + > /output/ot-compiled-files.txt
