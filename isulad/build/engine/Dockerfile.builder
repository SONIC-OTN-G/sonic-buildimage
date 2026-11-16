FROM golang@sha256:fd9a91f0ece5664556f4851d06dce5e5b505a71166dffd385a03ba9d200b6983
RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y --no-install-recommends gcc-aarch64-linux-gnu libc6-dev-arm64-cross libseccomp-dev libseccomp-dev:arm64 qemu-system-arm qemu-utils cpio e2fsprogs curl xz-utils && rm -rf /var/lib/apt/lists/*
WORKDIR /src
