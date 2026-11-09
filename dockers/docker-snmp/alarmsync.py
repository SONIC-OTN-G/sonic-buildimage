#!/usr/bin/env python3
# ---------------------------------------------------------------------
# OTN 告警上报(第三通道)—— alarmsync.py:CURALARM 监听 → SNMP trap 发送
# 事件源与 netconfd watcher 同构(PSubscribe keyspace + 缓存 diff);
# 出口:每条 RAISED/CLEARED 调一次 snmptrap(UDP,net-snmp 工具链,零 pysnmp)。
# 配置全部走环境变量:
#   SNMP_TARGET    默认 10.0.2.2:16162(VM→WSL slirp 网关直发,S1 探测定案)
#   SNMP_COMMUNITY 默认 public
#   REDIS_ADDR     默认 127.0.0.1:6379(容器 host 网络,STATE_DB db6)
# ---------------------------------------------------------------------
import os
import subprocess
import time

import redis

TARGET = os.environ.get("SNMP_TARGET", "10.0.2.2:16162")
COMMUNITY = os.environ.get("SNMP_COMMUNITY", "public")
REDIS_ADDR = os.environ.get("REDIS_ADDR", "127.0.0.1:6379")
TRAP_OID = ".1.3.6.1.4.1.99999.1.1.0.1"  # OTAI-ALARM-MIB::alarmNotification(演示占位根)
VB = {
    "resource": ".1.3.6.1.4.1.99999.1.2",
    "alarm-type": ".1.3.6.1.4.1.99999.1.3",
    "severity": ".1.3.6.1.4.1.99999.1.4",
    "alarm-text": ".1.3.6.1.4.1.99999.1.5",
    "time-created": ".1.3.6.1.4.1.99999.1.6",
    "alarm-state": ".1.3.6.1.4.1.99999.1.7",
}
SEV_NUM = {"UNKNOWN": "1", "MINOR": "2", "WARNING": "3", "MAJOR": "4", "CRITICAL": "5"}


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def scan_entries(r):
    """全表扫描:CURALARM* → {裸键: 字段dict}(与 netconfd 的 GetAlarmEntries 同构)"""
    out = {}
    for key in r.keys("CURALARM*"):
        bare = key.split("|", 1)[1] if "|" in key else key
        out[bare] = r.hgetall(key)
    return out


def send_trap(entry, state):
    """组 varbind 并调 snmptrap 发一包 UDP(state: 1=RAISED 2=CLEARED)"""
    host, port = TARGET.split(":")
    sev = entry.get("severity", "UNKNOWN")
    cmd = [
        "snmptrap", "-v", "2c", "-c", COMMUNITY, TARGET, "",
        TRAP_OID,
        VB["alarm-state"], "i", "1" if state == "RAISED" else "2",
        VB["resource"], "s", entry.get("resource", ""),
        VB["alarm-type"], "s", entry.get("type-id", ""),
        VB["severity"], "i", SEV_NUM.get(sev, "1"),
        VB["alarm-text"], "s", entry.get("text", ""),
        VB["time-created"], "s", entry.get("time-created", ""),
    ]
    log(f"trap -> {host}:{port} [{state}] {entry.get('resource','')}#{entry.get('alarm-type-id','')}")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
    if r.returncode != 0:
        log(f"snmptrap FAILED rc={r.returncode}: {r.stderr.strip()}")


def main():
    r = redis.Redis.from_url(f"redis://{REDIS_ADDR}/6", decode_responses=True)
    r.ping()
    log(f"connected redis {REDIS_ADDR}, target {TARGET} community {COMMUNITY}")

    cache = {}  # 裸键 → 字段(上次扫描快照;RAISED/CLEARED 判定与补全依据)
    pubsub = r.pubsub(ignore_subscribe_messages=True)
    pubsub.psubscribe("__keyspace@6__:CURALARM*")
    log("psubscribed __keyspace@6__:CURALARM*")

    cache = scan_entries(r)
    log(f"initial scan: {len(cache)} entries")

    while True:
        msg = pubsub.get_message(timeout=5.0)
        if msg is None:
            continue
        now = scan_entries(r)
        for key, e in now.items():
            if key not in cache:
                send_trap(e, "RAISED")
        for key, e in cache.items():
            if key not in now:
                send_trap(e, "CLEARED")
        cache = now


if __name__ == "__main__":
    main()
