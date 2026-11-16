# AGENTS.md — SONiC-OTN 演示工作區操作規則

> 給任何在此工作區工作的 AI/工程師:開工前必讀。
> **分層原則**:本檔只放「全局規則 + 索引」;**syncd-OT 戰役細節一律查 `docs/06-數據採集/42-syncd-OT-C++戰役記錄.md`**(構建/部署/閘門/補丁/未竟),演示操作查 `40`,數據模型查 `41`。

## 溝通守則

- **對話對象是入門級程式員**:解釋一律「一句話結論 → 生活比喻講機制 → 證據」,不堆術語;新名詞當場用一句白話註解(例:redis 連接=一條專線電話,回覆按提問順序排隊);日誌/代碼引用附「這行在做什麼」的白話;操作步驟寫成可照抄的指令,不假設用戶懂 C++/redis 內部實作。
- **文檔狀態標記**:⚠️ 只標「未實測」;已實測但與預期有出入 → ✅ + 括號註明差異,不堆警告符號(主管視角:滿屏 ⚠️ = 壞味道)。

## 環境地圖

| 位置 | 內容 |
|:--|:--|
| WSL(Ubuntu-26.04) | 構建機 + QEMU 宿主。`~/sonic-buildimage`(chomi54 fork;`src/sonic-otairedis`=OTN fork 含 C++ 採集器)、`~/sonic-otn-kvm.img`(演示 VM 磁碟) |
| VM(otn-kvm) | 演示機。SSH `localhost:2222` admin/YourPaSsWoRd。**VM 內有自己的 docker daemon**(與 WSL 的 docker 是兩個世界,別混) |
| opencode | session 存 `~/.local/share/opencode/`;導出:`opencode export <sessionID> > file.json`(歸檔於 `sessions/`);全局 config `~/.config/opencode/opencode.json`(permission edit=ask 已啟用、superpowers 插件,備份 .bak.20261022) |

## 持久性分級(鐵律)

| 層 | VM 重開機 | 容器重建 | 備註 |
|:--|:--|:--|:--|
| VM 磁碟(根檔案系統) | ✅ 實測持久 | — | 含 `/usr/local/bin`(host 端工具);`/host/warmboot/`(宿主盤)同層 |
| 容器層(`/var/lib/docker`) | ✅ | ❌ | 容器內改動隨容器重建消失 |
| 容器內 `/tmp`、`/var/tmp` | ❌ tmpfs | ❌ | 重啟即清 |
| `/var/run/redis/sonic-db/database_config.json` | ❌(tmpfs 開機重生成) | ❌ | HISTORY_DB 映射要重套 |
| redis 各庫(db1/2/4/5/6/10)——**真編排 warm reboot** | ✅(2026-11-13 實測) | ❌ | `sudo warm-reboot -f -d`:RDB 回魂,db1 帳本/db10 歷史/db5 組零丟;STATE_DB 白名單例外(規則 5b) |
| redis 各庫——**冷啟/普通 reboot** | ❌ | ❌ | 四庫重灌新態,seed SOP 重播(規則 5) |
| redis STATE_DB(db6)`LINECARD` key + ASIC_DB(db1)記帳 | 冷啟 ❌/warm ✅ | ❌ | 冷啟=db1 記帳蒸發要重播(規則 4);warm=RDB 回魂免重播,但 db6 白名單刪 `LINECARD|linecard1` 與 `CURALARM` → 補 hset/靠重發 |

## 硬規則(全局;戰役細節見 42)

1. **supervisor 改 conf 後必須 `reread` + `update`**;`restart` 不重讀 conf;`update` 後 `autostart=false` 的程序要手動 `start`。
2. **長構建**:背景化 `setsid nohup … > log 2>&1 < /dev/null &` + 短輪詢——終端工具逾時會殺整棵進程樹(nohup/setsid 都擋不住)。
3. **make 的 deb 目標不追源碼依賴**——改完源碼重編 deb 前,先 `rm` 掉 `target/debs/<env>/` 下的目標 deb 再 make,否則說 "up to date" 是假的。**衍生包(dbg/dev/ syncd-ot-vs)的噪音在主包配方裡**:rm 目標 deb 沒用,要 rm 主包(如 `libotairedis`)才會真重編。docker `.gz` 同病(rm 再 make);docker 目錄 `files/` 會被暫存 bind-mount 蓋住(腳本放 docker 目錄頂層);hostcfgd 重建容器認**版本標籤**不認 `:latest`(docker load 後兩個 tag 都要指新鏡像)。**syncd-ot-vs deb 直接裝 `/usr/bin/syncd`(2026-11-10 起,不再叫 syncd-ot-vs),且 `dpkg -i` 會把容器內補丁版 `syncd_init_common.sh`/`syncd_start.sh` 覆蓋回原版**(跳線、linkup 補丁全丟 → syncd EXITED)——修法=從 `docker commit` 備份鏡像 `docker cp` 取回補丁版腳本(別 sed 重打,引號地獄);驗證新碼進二進制=`dpkg-deb -x` 解包後 `strings` 查新日誌串。
4. **orchagent 從未參與告警管線,別去救它**:所有成功測試(10 月戰役、演示)中 orchagent 都是死的(開機 INIT_VIEW 握手失敗→安樂死)。它講 SAI 方言、OTN syncd 只聽 OTAI 方言,**讓它握手成功 = 餵毒 op 進 syncd 崩潰**(2026-11-06 實測)。線卡物件 = 手工 DB 播種(doc 43 §手工 create 配方:LPUSH create op + 4 佔位指針;另有 STATE_DB `LINECARD|linecard1` key),syncd 重啟靠 hardReinit 重放記帳。**redis 冷啟(db1/db6 清空)後必做**:① hset `LINECARD|linecard1` ② 重推 doc 43 配方;orchagent 不用管,讓它自生自滅。
5. **VM 冷啟 SOP(5 分鐘,零代碼;發現自己想改代碼 = 走錯路)**:① 等容器重建(`docker ps` 全 Up,約 5-8 分) ② hset `LINECARD|linecard1` ③ 等 syncd `starting main loop` 後**推一次** doc 43 配方(推前 `llen ASIC_STATE_KEY_VALUE_OP_QUEUE` 必須 0;冷啟後佇列常積壓 orchagent 的 notify 副本,先 DEL 再推;重複推 = 重複 create = syncd 退出) ④ 驗收指紋:`created LINECARD RID oid:0x10100000001` → +10s `sendAlarmNotification` 無 SEGV → db6 有 `CURALARM|#HIGH_TEMPERATURE_ALARM` ⑤ 跑三通道驗證腳本。配方腳本 VM `/tmp/seed_linecard.sh`(tmpfs 會丟,照規則 4 重敲)。
   **5b. 整機 warm reboot(2026-11-13 翻案實測,doc 55 §8)**:`sudo warm-reboot -f -d` 真編排成立(kexec 熱重啟 + /host/warmboot RDB 回魂)——**db1 帳本/db10 歷史/db5 組零丟,免 seed 重播**。舊判決「warm 同樣蒸發」作廢(當年測的是 sudo reboot 冷路)。**最終照片=清理後狀態(確定性行為)**:backup_database 清理 STATE_DB 後調用 `centralize_database`,其結尾自帶 `r.save()` 重拍(2026-11-13 MONITOR+tcpdump 取證破案,doc 55 §9 I1)——**手動 save 已從 SOP 移除**(純冗餘);關機自清掃還會清 pm_current 現窗+把採集組置 disable。**warm 後人工動作兩步(秒級)**:① hset `LINECARD|linecard1`(白名單刪了閘門 key,syncd 永等) ② 重開 `1S_STAT_GAUGE` 組(採集器不自啟)。旗標:-f=orchagent 死放行;-d=COUNTERS_PORT_NAME_MAP 缺席放行;-i 不用(asic_config_check 實測過)。**本機 `config` CLI 已修**(click 8.1 相容,wheel 重建部署 2026-11-13)。
6. **已證偽的彎路(看到以下現象,禁止往改代碼方向想)**:syncd 對 INIT_VIEW 不回應 = 設計現狀,修它 = 餵毒(規則 4);日誌 "Waiting for Linecard..." 出現在 "starting main loop" 之後 = 良性(等待循環末次迭代),非崩潰;`docker logs --since` 前先 `date` 對錶——VM 時鐘會 NTP 跳躍,時間窗結論全作廢;別手工種 VIDTORID/ASIC_STATE 記帳——LPUSH create op 自己記帳,hardReinit 自己重放。
7. **redis 一條連接 = 一條回覆隊列,一連接內多執緒 = 回覆交叉(2026-11-10 實證)**:NP 執緒已改用獨立 ASIC_DB 連接 + 獨立翻譯器(Syncd.cpp `npClient`/`npTranslator`),修復 UP 時 SoftReinit 與告警處理共搶 `m_client` 導致的回覆串台 → NP 例外逃逸 → SIGABRT(進程死亡,threads=0)。**未來任何新增執緒碰 db1,禁止共用 `m_client`**——自己開連接;redis 伺服器無責(保證單位=連接),hiredis context 非執緒安全。NP 迴圈已有 try/catch 保命層,異常記日誌不自殺。
