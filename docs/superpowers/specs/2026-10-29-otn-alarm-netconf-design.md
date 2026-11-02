# OTN 告警上报:NETCONF 通道设计

日期:2026-10-29
状态:已实施(方案一落地,见 docs 50/51 链路文档;SNMP 通道设计拆出另立文档,未随本篇)

## 意图与背景

gNMI 通道已实测闭环(doc 47:gnmi 容器 native 订阅 STATE_DB CURALARM,快照/UPDATE/DELETE 三幕全通,零代码)。本设计补 NETCONF 通道,使 OTAI 告警可经 NETCONF :830 上报网管。数据源复用现有 CURALARM/HISALARM,**syncd 零改动**。

(SNMP trap 通道的设计已拆出至 `2026-10-29-otn-alarm-snmp-design.md`,不属本篇。)

## 目标

- 网管端经 NETCONF :830 能 GET openconfig-alarms 告警列表,并订阅告警变化(notification/on-change,视平台支持程度,见风险)
- 注入旋钮复用 doc 46 的 sim json

## 非目标

- pmon 侧任何改动(明确排除)
- syncd 内嵌 events_publish(事件正统路,后续另议)
- gNMI 通道任何改动(已闭环)
- 告警用户清除入口(CLI clear)

## 反目标

- 让 syncd/关键进程承担网管协议客户端职责
- 为演示把认证/加密问题永久化(noTLS/noAuth 仅限现状,设计不新增明文通道)

## 约束

- 部署环境:VM(otn-kvm)容器体系;凡动镜像的走 buildimage 重编(长构建后台化,AGENTS.md 铁律)
- 网管侧验证均在 WSL:ncclient
- 生态查证结论:SONiC 无现成 NETCONF server(mgmt-framework/master、202012 老分支、sonic-mgmt-common 均无 netconf/)→ 自研 Go 壳(方案对比见 doc 49,推荐方案一)

## 方案(方案一:Go 自研协议壳,选定)

**考虑过的方案**:

| 方案 | 取舍 | 结论 |
|:--|:--|:--|
| 自定义 sonic-curalarm YANG(自有命名空间) | 映射最省,但非标,生态价值低 | 备选 fallback |
| openconfig-alarms.yang 标准模型 | 需写 translib app;电信级标准语义,NMS 生态(ODL/OpenNMS)直接消费;模型公开可取;translib 订阅机制有 acl_app.go 先例可抄 | **选定** |
| 上游移植 mgmt-netc | 经查证上游根本不存在可移植实现 | 否 |

**选定方案细节**:

1. **模型**:`openconfig-alarms.yang`(OC 公开版)进 `src/sonic-mgmt-common/models/`;该模型为纯 state 列表(alarm 以 id 为键,字段:severity/text/time-created/time-raised/time-cleared/status),无 config 节点
2. **字段映射**(app 内实现):

| OC alarm 字段 | CURALARM 字段 |
|:--|:--|
| resource | resource |
| alarm-type-id | type-id |
| severity | severity(MINOR/WARNING/MAJOR/CRITICAL 直接同枚举域) |
| text | text |
| time-created | time-created(ns→时间戳转换) |
| time-cleared | CURALARM 无(清除即删)——留空;历史从 HISALARM 补(二期) |

3. **translib app** `alarm_app.go`(参考 `acl_app.go` 的 app 骨架):
   - `translate`/`process` GET:扫 STATE_DB CURALARM → OC 列表
   - `translateSubscribe`/`processSubscribe`:监听 CURALARM 变化 → notification
4. **部署**:mgmt-framework 容器重编(deb 更新);netconfd 进容器 supervisord

## 风险与留给 builder 的问题

1. translib state-only app 的注册与映射机制:acl_app 是表映射型;alarm 这类「无 CVL 表映射、app 内直读 STATE_DB」的做法需以现有 state-only app 为参考确认(builder 查——实测:nonyang_app.go.demo 先例,isNative 裸 JSON 可行)
2. mgmt-framework 对 YANG-Push/on-change 订阅的服务端支持程度(builder 查版本与能力);**fallback**:订阅不可行则 NetConf 交付 GET(告警列表可查,轮询拉取),文档注明限制——实测:自研壳直接实现 RFC 5277 notification,该风险消解
3. VM redis keyspace notifications 配置实测(已验:AKE,事件正常发布;PSubscribe 模式匹配,勿用 SUBSCRIBE)

## 验证剧本

**NetConf**(WSL,ncclient):
```
get(openconfig-alarms:alarms)             # 列表与 sonic-db-cli STATE_DB 一致(实测通过)
create-subscription → 注入后收 notification(RAISED/CLEARED,实测通过)
# 一键:scripts/netconf_alarm_demo.sh
```

## 交付状态

已全部实施并推送(方案一):mgmt-common `tom-wsl`(607e84c 翻译层)、mgmt-framework `tom-wsl`(14d7b17 netconfd)、父仓 gitlink 同步;链路文档 docs 50/51。
