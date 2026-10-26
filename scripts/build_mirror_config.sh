#!/bin/bash

# Generate the sources.list.<arch> in the config path
CONFIG_PATH=$1
export ARCHITECTURE=$2
export DISTRIBUTION=$3

# Handling default
[[ -z $APT_RETRIES_COUNT ]] && APT_RETRIES_COUNT=20
export APT_RETRIES_COUNT

DEFAULT_MIRROR_URL_PREFIX=http://packages.trafficmanager.net
MIRROR_VERSION_FILE=
[[ "$MIRROR_SNAPSHOT" == "y" ]] && MIRROR_VERSION_FILE=files/build/versions/default/versions-mirror
[ -f target/versions/default/versions-mirror ] && MIRROR_VERSION_FILE=target/versions/default/versions-mirror

# The default mirror urls
DEFAULT_MIRROR_URLS=http://debian-archive.trafficmanager.net/debian/
DEFAULT_MIRROR_SECURITY_URLS=http://debian-archive.trafficmanager.net/debian-security/


# The debian-archive.trafficmanager.net does not support armhf, use debian.org instead
if [ "$ARCHITECTURE" == "armhf" ]; then
    DEFAULT_MIRROR_URLS=http://deb.debian.org/debian/
    DEFAULT_MIRROR_SECURITY_URLS=http://deb.debian.org/debian-security/
fi

if [ "$DISTRIBUTION" == "buster" ]; then
    DEFAULT_MIRROR_URLS=http://archive.debian.org/debian/
    DEFAULT_MIRROR_SECURITY_URLS=http://archive.debian.org/debian-security/
elif [ "$DISTRIBUTION" == "bullseye" ]; then
    # bullseye is EOL (Aug 2026): deb.debian.org no longer serves it and
    # archive.debian.org's merged state has intra-suite version mismatches
    # (e.g. libssl1.1 deb11u2 vs libssl-dev deb11u1). Pin a point-in-time
    # snapshot from the era this branch was built instead.
    DEFAULT_MIRROR_URLS=http://snapshot.debian.org/archive/debian/20260601T000000Z/
    DEFAULT_MIRROR_SECURITY_URLS=http://snapshot.debian.org/archive/debian-security/20260601T000000Z/
fi
if [ "$MIRROR_SNAPSHOT" == y ]; then
    if [ -f "$MIRROR_VERSION_FILE" ]; then
        DEBIAN_TIMESTAMP=$(grep "^debian==" $MIRROR_VERSION_FILE | tail -n 1 | sed 's/.*==//')
        DEBIAN_SECURITY_TIMESTAMP=$(grep "^debian-security==" $MIRROR_VERSION_FILE | tail -n 1 | sed 's/.*==//')
    elif [ -z "$DEBIAN_TIMESTAMP" ] || [ -z "$DEBIAN_SECURITY_TIMESTAMP" ]; then
        DEBIAN_TIMESTAMP=$(curl $DEFAULT_MIRROR_URL_PREFIX/snapshot/debian/latest/timestamp)
        DEBIAN_SECURITY_TIMESTAMP=$(curl $DEFAULT_MIRROR_URL_PREFIX/snapshot/debian-security/latest/timestamp)
    fi

    DEFAULT_MIRROR_URLS=http://deb.debian.org/debian/,http://packages.trafficmanager.net/snapshot/debian/$DEBIAN_TIMESTAMP/
    DEFAULT_MIRROR_SECURITY_URLS=http://deb.debian.org/debian-security/,http://packages.trafficmanager.net/snapshot/debian-security/$DEBIAN_SECURITY_TIMESTAMP/

    mkdir -p target/versions/default
    if [ ! -f target/versions/default/versions-mirror ]; then
        echo "debian==$DEBIAN_TIMESTAMP" > target/versions/default/versions-mirror
        echo "debian-security==$DEBIAN_SECURITY_TIMESTAMP" >> target/versions/default/versions-mirror
    fi
fi

# Handle sources list
[ -z "$MIRROR_URLS" ] && MIRROR_URLS=$DEFAULT_MIRROR_URLS
[ -z "$MIRROR_SECURITY_URLS" ] && MIRROR_SECURITY_URLS=$DEFAULT_MIRROR_SECURITY_URLS

TEMPLATE=files/apt/sources.list.j2
[ -f files/apt/sources.list.$ARCHITECTURE.j2 ] && TEMPLATE=files/apt/sources.list.$ARCHITECTURE.j2
[ -f $CONFIG_PATH/sources.list.j2 ] && TEMPLATE=$CONFIG_PATH/sources.list.j2
[ -f $CONFIG_PATH/sources.list.$ARCHITECTURE.j2 ] && TEMPLATE=$CONFIG_PATH/sources.list.$ARCHITECTURE.j2

MIRROR_URLS=$MIRROR_URLS MIRROR_SECURITY_URLS=$MIRROR_SECURITY_URLS j2 $TEMPLATE | sed '/^$/N;/^\n$/D' > $CONFIG_PATH/sources.list.$ARCHITECTURE
# Drop the security repo entry when no security mirror is set (e.g. EOL bullseye)
if [ -z "$MIRROR_SECURITY_URLS" ]; then
    # Use only the main suite: -updates/-backports on the archive are no longer
    # mutually consistent with the base suite and break apt dependency resolution
    sed -i '/-security\|-updates\|-backports/d' $CONFIG_PATH/sources.list.$ARCHITECTURE
fi
# bullseye EOL: -updates/-backports were retired upstream and are absent from
# the pinned snapshot; keep only main + bullseye-security
if [ "$DISTRIBUTION" == "bullseye" ] && [ "$MIRROR_SNAPSHOT" != y ]; then
    # -updates was retired upstream and -backports is absent from the 2026
    # snapshot. Keep main + security, then append backports from an older
    # snapshot where it still exists (priority 100, so it is only used
    # explicitly, e.g. apt-get -t bullseye-backports install rsyslog).
    sed -i '/bullseye-updates\|bullseye-backports/d' $CONFIG_PATH/sources.list.$ARCHITECTURE
    echo "deb [arch=$ARCHITECTURE] http://snapshot.debian.org/archive/debian/20241001T000000Z/ bullseye-backports main contrib non-free" >> $CONFIG_PATH/sources.list.$ARCHITECTURE
    echo "deb-src [arch=$ARCHITECTURE] http://snapshot.debian.org/archive/debian/20241001T000000Z/ bullseye-backports main contrib non-free" >> $CONFIG_PATH/sources.list.$ARCHITECTURE
fi
if [ "$MIRROR_SNAPSHOT" == y ]; then
    # Set the snapshot mirror, and add the SET_REPR_MIRRORS flag
    sed -i -e "/^#*deb.*packages.trafficmanager.net/! s/^#*deb/#&/" -e "\$a#SET_REPR_MIRRORS" $CONFIG_PATH/sources.list.$ARCHITECTURE
fi

# Handle apt retry count config
APT_RETRIES_COUNT_FILENAME=apt-retries-count
TEMPLATE=files/apt/$APT_RETRIES_COUNT_FILENAME.j2
j2 $TEMPLATE > $CONFIG_PATH/$APT_RETRIES_COUNT_FILENAME
