FROM --platform=linux/arm64 publicmirror.azurecr.io/debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends redis-server redis-tools python3 python3-redis python3-click libhiredis0.14 libnl-3-200 libnl-genl-3-200 libnl-route-3-200 libnl-nf-3-200 libzmq5 libboost-serialization1.74.0 libuuid1 libstdc++6 && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends libpython3.11 && rm -rf /var/lib/apt/lists/*
COPY target/rootfs/ /
COPY redis-standalone.conf /etc/redis/sonic-standalone.conf
COPY sources/sonic-buildimage/dockers/docker-database/ /usr/share/sonic/upstream/docker-database/
COPY sources/sonic-swss-common/LICENSE /usr/share/doc/sonic-swss-common/copyright
COPY provenance.json /usr/share/sonic/standalone-provenance.json
RUN ldconfig && python3 -c 'from swsscommon import swsscommon; print("swsscommon-import-ok")'
LABEL org.opencontainers.image.title="SONiC database ARM64 standalone test" org.opencontainers.image.revision="a77538a37517b9a953e32191a77e83c6b5a11c14" sonic.test.scope="Standalone Redis and swsscommon; not full SONiC database"
STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/bin/redis-server"]
CMD ["/etc/redis/sonic-standalone.conf"]
