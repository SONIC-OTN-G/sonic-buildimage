#!/bin/sh

BASE=$(cd $(dirname $0)/.. && pwd)
export LD_LIBRARY_PATH=$BASE/lib
export OTAI_SIM_DATA_PATH=$BASE/otai_sim_data
cd $BASE

mkdir -p /usr/share/swss
cp $BASE/swss/*.lua /usr/share/swss

# 1. redis（标准 socket 路径 + dump.rdb 预置加载）
#    sonic-db 配置先行放置：swsscommon 硬编码读取 /var/run/redis/sonic-db/database_config.json
#    （dbconnector.h:90），/var/run 为 tmpfs，设备重启即失，每次拉起都须供给
mkdir -p /var/run/redis/sonic-db rdb-work
cp $BASE/conf/database_config.json /var/run/redis/sonic-db/database_config.json
cp dump.rdb rdb-work/
./bin/redis-server conf/redis.conf --dir rdb-work --daemonize yes
sleep 2

# 2. syncd（等价 syncd_init_common.sh 的 ot-vs 分支，slot 1）
printf 'OTAI_LINECARD_LOCATION=1\nOTAI_VS_LINECARD_TYPE=OTAI_VS_LINECARD_TYPE_OTN\n' > /tmp/otai_test.profile
./bin/syncd -p /tmp/otai_test.profile &
sleep 5

# 3. OTSS 三进程（supervisord 顺序还原）
./bin/orchagent -b 8192 &
sleep 2
./bin/linecardmgrd &
./bin/configsyncd &
echo "=== all started, check logs ==="
