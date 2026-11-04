# OTN 告警上报:SNMP trap 通道设计

日期:2026-10-29(自 snmp-netconf 联合设计拆出,2026-10-30)
状态:待实施(NETCONF 通道已完成并推送,见 `2026-10-29-otn-alarm-netconf-design.md`)

## 意图与背景

gNMI、NETCONF 两通道已闭环(doc 47/50/51)。本篇是第三通道:OTAI 告警经 **SNMP trap** 上报网管。数据源复用现有 CURALARM/HISALARM,syncd 零改动。

## 目标

- 网管端 snmptrapd 能收到 raise 与 clear 两方向的 trap,varbind 携带 resource/type/severity/text/time-created/state

## 非目标

- pmon 侧任何改动(明确排除)
- syncd 内嵌 events_publish(事件正统路,后续另议)
- gNMI/NETCONF 通道任何改动(已闭环)
- 告警用户清除入口(CLI clear)
- snmpd 标准功能(接口 MIB 轮询)的改动或回归

## 反目标

- 让 syncd/关键进程承担网管协议客户端职责
- 为演示把认证/加密问题永久化

## 约束

- 部署环境:VM(otn-kvm)容器体系;凡动镜像的走 buildimage 重编(长构建后台化,AGENTS.md 铁律)
- **UDP 拓扑待探**:SSH 隧道转发不了 UDP——trap(UDP 162)从 VM 到 WSL 的路径需第一步实测(直发 / socat 中继 / fallback:snmptrapd 先跑 VM host)
- 演示环境:OID 用占位根,认证从简(v2c community),但架构位置摆正

## 方案

**考虑过的方案**:

| 方案 | 取舍 | 结论 |
|:--|:--|:--|
| syncd 直接发 trap | 关键进程加协议客户端,回归风险 | 否 |
| 独立 daemon 放 pmon 容器 | 用户明确排除 pmon 侧 | 否 |
| trap 发送器进 snmp 容器 | snmp 容器本就要开;net-snmp 工具链(snmptrap)现成;supervisord 加 program 即可 | **选定** |

**选定方案细节**:

1. **MIB**:自定义 `OTAI-ALARM-MIB`,演示企业 OID 根 `.1.3.6.1.4.1.99999`(占位,正式部署换真实 PEN)
   - 一个 trap:`alarmNotification`(.99999.1.1.0.1)
   - varbinds:`resource` `.2`、`alarm-type` `.3`、`severity` `.4`(枚举映射 OTAI severity→SNMP 整数)、`alarm-text` `.5`、`time-created` `.6`(ns epoch 原值)、`alarm-state` `.7`(1=raise, 2=clear)
   - raise/clear 共用一个 trap OID,以 state varbind 区分(NMS 单 OID 过滤,实现最简)
2. **发送器** `alarmsync.py`(snmp 容器内,~百行 Python):
   - redis 订阅 db6 keyspace notifications(`__keyspace@6__:CURALARM*`,**PSubscribe 模式**——实测踩坑:SUBSCRIBE 是精确频道)——与 netconfd watcher 同构,复用已验证模式
   - `hset` 事件 → 组 varbind → `snmptrap -v 2c -c <community> <NMS:162> 'OTAI-ALARM-MIB::alarmNotification' ...`(调容器内 net-snmp 的 snmptrap,不引 pysnmp 依赖)
   - `hdel/del` 事件 → 同 trap,`alarm-state=2`
   - NMS 地址与 community 从 CONFIG_DB `SNMP` 表读(SONiC 惯例),缺省 `localhost:162`/`public`
3. **部署**:`dockers/docker-snmp/Dockerfile.j2` 拷入脚本与 MIB 文件;`supervisord.conf.j2` 加 `[program:alarmsync]`;CONFIG_DB FEATURE 表启用 snmp 容器;VM 部署先走 T9 同款 cp 技巧(容器 supervisord 加 program),镜像落源随下次构建

## 风险与留给 builder 的问题

1. snmp 容器内 Python 环境:redis-py 以 pip bake 进镜像还是系统包(builder 查容器基础镜像的包管理);备选:纯 socket 实现 redis 订阅,零依赖
2. VM redis keyspace notifications 配置:**已实测通过**(AKE,事件正常发布——NETCONF watcher 同款机制已在用,此风险消解)
3. UDP 拓扑(S1 探测定案,见约束)

## 验证剧本

**SNMP**(接收端位置按 S1 探测定案):
```
snmptrapd -f -Lo -c <conf> :162          # 网管端守着
VM: 注入 raise(json 旋钮)               # ≤10s 收到 trap,varbind 对照 §方案
VM: 注入 clear                            # 收到 state=2 trap
# 一键:scripts/snmp_verify.sh(风格对齐 netconf_alarm_demo.sh)
```

## 交付顺序(给 plan 用)

1. S1 拓扑探測(trap 目标地址定案)
2. snmp 容器启用 + 发送器部署(cp 技巧)
3. 验证 + snmp_verify.sh + commit/push(含 repo 落源)
