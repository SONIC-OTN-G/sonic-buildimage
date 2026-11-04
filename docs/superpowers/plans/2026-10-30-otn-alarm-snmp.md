# SNMP trap 通道 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** OTAI 告警经 SNMP trap 上报网管:alarmsync.py 监听 CURALARM keyspace → snmptrap(UDP)发 RAISED/CLEARED,网管端 snmptrapd 实测收到。

**Architecture:** 与 netconfd watcher 同构的事件模式(redis PSubscribe keyspace + 缓存 diff)复用;出口换 snmptrap 命令行(net-snmp 工具链,零 pysnmp);发送器宿主 = snmp 容器(FEATURE 已 enabled)。

**Tech Stack:** Python3 + redis-py(容器内 pip)、net-snmp(snmptrap/snmptrapd)、UDP。

**Spec:** `docs/superpowers/specs/2026-10-29-otn-alarm-snmp-design.md`

## Global Constraints

- pmon 侧零改动;syncd 零改动
- 演示 OID 根 `.1.3.6.1.4.1.99999`(占位 PEN);severity 以整数 varbind 发送(映射表见 spec)
- trap 目标:S1 探测定案 = `10.0.2.2:16162`(VM slirp 网关直发 WSL;16162 避特权端口)
- 容器内 pip 安装属容器层(重建即失)——S4 必须落源(Dockerfile.j2)

## Review Focus

1. UDP 来源地址:实测 VM→10.0.2.2 到达 WSL 时显示来源 127.0.0.1(slirp 映射)——接收端绑定 0.0.0.0 即可收到;若收不到改绑回环(S1 实测已证 127.0.0.1 可达)
2. v2c community 不匹配时 snmptrapd 静默丢——接收端配置必须显式 `authCommunity log public`
3. 容器重建后 redis-py 消失 → alarmsync 起不来(S4 落源后由镜像解决;cp 技巧阶段重建需重 pip)
4. keyspace 事件用 PSubscribe(SUBSCRIBE 是精确频道,实测踩坑——与 netconfd 同款)

---

### Task S1: UDP 拓扑探测 ✅(已执行)

- [x] VM → 10.0.2.2:16200 发 UDP,WSL 0.0.0.0:16200 监听 → **收到(来源 127.0.0.1,slirp 映射)**
- [x] 定案:trap 目标 `10.0.2.2:16162`,snmptrapd 绑 WSL

### Task S2: snmp 容器启用 ✅(已执行)

- [x] `docker start snmp`(FEATURE 已 enabled,容器 4 天前干净退出;host 网络;restart=no)
- [x] 验证:snmptrap ✓、python3.11 ✓、redis-py 5.0.1(pip,容器层)✓

### Task S3: 发送器 alarmsync.py ✅

**Files:**
- Create: `dockers/docker-snmp/files/alarmsync.py`

**Interfaces:**
- Produces: 环境变量 `SNMP_TARGET`(默认 `10.0.2.2:16162`)/ `SNMP_COMMUNITY`(public)/ `REDIS_ADDR`(127.0.0.1:6379);事件循环:PSubscribe `__keyspace@6__:CURALARM*` → 缓存 diff(RAISED/CLEARED,复用 netconfd 模式)→ 每条调 `snmptrap -v 2c -c public <target> "" <trapOID> <varbinds>`;trap OID `.1.3.6.1.4.1.99999.1.1.0.1`,varbinds .2=resource(s) .3=type(s) .4=severity(i) .5=text(s) .6=time-created(s) .7=state(i)

- [x] **Step 1: 编写 alarmsync.py**
- [x] **Step 2: 语法检查(python3 -m py_compile,WSL 与容器内双验)**
- [x] **Step 3: docker cp 进容器 + 冒烟(手动注入一枪,trap 到达 WSL snmptrapd)**

### Task S4: 部署落位(cp 技巧 + repo 落源)✅

**Files:**
- Modify: `dockers/docker-snmp/supervisord.conf.j2`(加 [program:alarmsync])
- Modify: `dockers/docker-snmp/Dockerfile.j2`(pip install redis 落源 + COPY alarmsync)

- [x] **Step 1: VM 部署:docker cp alarmsync.py → /usr/local/bin/;容器 supervisord.conf 追加 program;reread+update;status=RUNNING**
- [x] **Step 2: repo 落源 commit**(files/alarmsync.py + supervisord 模板 + Dockerfile)
- [x] **Step 3: 韧性验证:alarmsync 重启(docker cp 更新脚本后 supervisorctl restart)正常拉起**

### Task S5: 网管端验证与收尾 ✅

**Files:**
- Create: `scripts/snmp_verify.sh`(风格对齐 netconf 系:预检→注入 raise/clear→trap 摘要→DB 终态)

- [x] **Step 1: WSL 装 snmptrapd(apt),最小配置 `authCommunity log public`,监听 udp:16162**
- [x] **Step 2: 注入 raise/clear → 实测收到两帧 trap(varbind 对照 spec;键名实测修正 type-id)**
- [x] **Step 3: snmp_verify.sh 跑通;commit + push**

## Execution 备注(执行期实测)

1. **UDP 路径**:VM(slirp 10.0.2.15)→ 10.0.2.2 → WSL 127.0.0.1,直发成立,无需中继;接收端 snmptrapd 绑 0.0.0.0:16162(16162 避特权端口与 WSL 保留段 829–831)
2. **键名踩坑**:syncd 六字段是 `type-id`,首版误写 `alarm-type-id` → varbind 空;snmptrapd 日志实证修正
3. **接收端配置**:apt 装 snmptrapd 后需手写最小配置;`authCommunity log,execute public` 在 5.9.4 会解析炸,最小形态 `authCommunity log public` 通过;默认还要占 udp:162(与系统 snmpd 冲突)→ 显式指定 `udp:16162`
4. **摘要解析两坑**:(a) snmptrapd 日志与注入脚本分属 root/用户双写者,append 标记会被覆盖 → 改按「取最后 2 个 OTAI trap」;(b) 日志末块无尾换行,块正则 `(?:[^\n]*\n)+` 吞尾块 → 改 splitlines 行切片
5. **冷启动行为**:alarmsync 初扫即建立缓存,首个事件若为清除且缓存已含该键 → 正常推 CLEARED;重启后缓存空,清除类首推缺失(靠 GET 对账,与 netconfd 同款已知限制)
6. **部署形态**:cp 技巧(容器 /usr/local/bin/alarmsync.py + supervisord 追加 program);repo 落源(Dockerfile pip redis + COPY、supervisord 模板)随下次镜像构建生效
