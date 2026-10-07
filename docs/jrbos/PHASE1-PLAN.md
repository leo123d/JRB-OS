# PHASE1-PLAN.md — 阶段 1「减法回收 Flash」执行清单

> 目标：删除非阅读功能与整个网络栈，回收约 1.6 MB Flash。
> 依据：`PAPERREAD-DESIGN.md` §6.3 的代码结构 + 决策 3/6/8。
> 状态：**清单已备好，待构建可用后执行**。
> 最后更新：2026-10-06

---

## 0. 前置条件

⚠️ **必须先有一个能成功的基线构建**。当前构建被 `tool-scons` 故障阻塞（见 `BUILD.md` §4）。
**不要在无法编译的状态下做大规模删除**——否则无法区分"我删错了"和"环境坏了"。

---

## 1. 整目录删除

| 目录/文件 | 大小 | 依据 |
|---|---|---|
| `src/activities/apps/` | **1,284 KB / 138 文件** | 决策：游戏（2048/五子棋/象棋/扫雷/推箱子/像素开关/计算器/木鱼/丑头像）、`airpage`、`standby`、`reading-stats`、`weread` 全部非阅读 |
| `src/activities/network/` | **117 KB / 10 文件** | 决策 6：删 WiFi/传书/WebDAV/USB 挂载 |
| `src/activities/browser/` | 11 KB / 2 文件 | OPDS 需要联网 |
| `src/activities/plugins/` | 62 KB / 2 文件 | `PluginCatalogActivity`，插件目录需联网 |
| `lib/WeReadWebApi/` | **373 KB / 14 文件** | 决策 3：删微信读书 |
| `lib/TrustedTime/` | 6 KB / 2 文件 | 决策 8：删时间 |
| `lib/OpdsParser/` | 12 KB / 4 文件 | 需联网 |
| `lib/AvatarGen/` | 40 KB / 19 文件 | 丑头像，非阅读 |
| `lib/SoundFeedback/` | 9 KB / 2 文件 | 蜂鸣器音效，上游刻意未启用 |
| `src/activities/settings/ClockSyncActivity.*` | — | 决策 8 |
| `src/activities/settings/OtaUpdateActivity.*` | — | 改为 SD 卡本地 OTA（重写） |
| `src/activities/settings/OpdsServerListActivity.*` | — | 需联网 |
| `src/activities/settings/FontDownloadActivity.*` | — | 内置字库已覆盖 |
| `src/activities/settings/DictionaryDownloadActivity.*` | — | 需联网 |
| `src/activities/settings/AppVisibilitySettingsActivity.*` | — | 无 apps 可管 |

---

## 2. 需要改代码的引用点（按引用数排序）

> 这些是**编译会断的地方**。删目录后必须逐个处理。

| 引用数 | 文件 | 要做什么 |
|---|---|---|
| **16** | `src/main.cpp` | 删 WiFi/SNTP/`JoinNetwork`/`silentRestartToJoinNetwork`/sleep-event 入网/`trustedtime::startSync`/联网字体下载提示/OTA 联网分支 |
| **5** | `src/CrossPointSettings.cpp` | 删网络/weread/时钟/apps 相关设置键 |
| 4 | `src/activities/reader/EpubReaderActivity.cpp` | 删 WeRead 进度同步分支 |
| 3 | `src/activities/reader/ReaderActivity.cpp` | 同上 |
| 2 | `src/activities/settings/SettingsActivity.cpp` | 删已移除的设置页 |
| 2 | `src/activities/settings/SettingsList.h` | 同上 |
| 2 | `src/CrossPointSettings.h` | 删键定义 |
| 2 | `src/activities/ActivityManager.cpp` | 删已移除活动的注册 |
| 2 | `src/activities/reader/KOReaderSyncActivity.cpp` | 保留（KOReader 同步不依赖我们的网络栈？**需确认**） |
| 2 | `src/activities/home/HomeActivity.cpp` | 改造成单页三入口（决策 7） |
| 1 | `src/activities/CatalogActivity.cpp` | 删 |
| 1 | `src/activities/settings/KOReaderAuthActivity.cpp` | 需确认是否联网 |

### 待确认项（动手前先查）

- ⚠️ **`KOReaderSyncActivity` 是否需要联网？** 如果需要，它属于决策 6 的删除范围（决策 6 删了 WiFi，KOReader 同步就没了）。这会影响设计文档里"KOReader 同步"的表述。
- ⚠️ **`src/network/`（与 `src/activities/network/` 不同）** 是否有独立目录？需一并处理。

---

## 3. 构建配置改动（`platformio.ini`）

`[base]`（第 9–192 行）与 `[readpico_hardware]`（第 968–1059 行）需改：

### 3.1 删 build_flags

```
-DFREEINK_NET_WOLFSSL=1
-DFREEINK_CONTENT_WOLFSSL=1
-DCONTENT_EXTERNAL_MINIZ=1
-DWOLFSSL_KEY_GEN
-DWC_RC2
-DNO_WOLFSSL_ESP32_CRYPT_RSA_PRI
-DWOLFSSL_USER_SETTINGS
-DWOLFSSL_OPTIONS_H
-DWOLFSSL_CLIENT_EXAMPLE
-DWOLFSSL_TLS13
-DWOLFSSL_HAVE_SP_ECC
-DWOLFSSL_HAVE_SP_RSA
-DWOLFSSL_SP_SMALL
-DWOLFSSL_SP_4096
-DHAVE_MAX_FRAGMENT
-DHAVE_FFDHE_2048
-DHAVE_CURVE25519
-DHAVE_SNI
```

### 3.2 删 lib_deps

```
BleKeyboardHost=symlink://freeink-sdk/libs/network/BleKeyboardHost
SecureNet=symlink://freeink-sdk/libs/network/SecureNet
JsonSax=symlink://freeink-sdk/libs/network/JsonSax
CatalogList=symlink://freeink-sdk/libs/network/CatalogList
ContentProtection=symlink://freeink-sdk/libs/book/ContentProtection
links2004/WebSockets @ 2.7.3
knolleary/PubSubClient @ ^2.8
wolfssl/Arduino-wolfSSL @ 5.7.2
ricmoo/QRCode @ 0.0.1          # 若只用于传书二维码，可删
```

### 3.3 删 extra_scripts

```
pre:scripts/patch_wolfssl.py
```

### 3.4 改 i18n

```ini
custom_i18n_builtin_langs = all      →      custom_i18n_builtin_langs = en,zh-CN
```
> ⚠️ 需确认 `zh-CN` 的正确 code（查 `lib/I18n/translations/` 目录名）。

### 3.5 保留不动

- `-DUSE_UTF8_LONG_NAMES=1`（**修 B1 的关键**，必须保留）
- `-DUSE_SEPARATE_FAT_CACHE=1`、`-DUSE_SPI_ARRAY_TRANSFER=1`
- `-DFREEINK_FONT_ENABLE_AUTOHINT=1`
- `-DENABLE_CHINESE_VERSION=1`
- `extends = base, firmware_tuned`（**不要改回 `base`**——决策 6 不做 USB 挂载，所以能保住这 32–37 KB 堆回收）

---

## 4. 执行顺序（每步都要构建验证）

```
1. 先单独删 src/activities/network/ + lib/OpdsParser + browser/ + plugins/
   → 构建；记录固件大小
2. 单独删 lib/WeReadWebApi + src/activities/apps/weread/
   → 构建；记录
3. 单独删 src/activities/apps/ 其余（游戏/airpage/avatar/standby/reading-stats）
   → 构建；记录
4. i18n: all → en,zh-CN
   → 构建；记录
5. 清 platformio.ini 的 wolfSSL/TLS/网络 flags 与 lib_deps
   → 构建；记录 ← 这一步收益最大
6. 清 main.cpp 的网络分支
   → 构建；记录
```

**每步记录到一张表**：`步骤 | 固件字节 | 相比上一步 | 相比基线`。
这样"回收 1.6 MB"就从估算变成实测，也能立刻定位哪一步删坏了。

---

## 5. 验收标准

- `pio run -e readpico` 成功
- 固件 ≤ **6,029,312 B**（保留 512 KiB 门槛）——**但注意字库还要占 2.68 MB**，
  所以**减法后的固件本体应 ≤ 3.35 MB**，否则加上字库会超。
- 串口日志无 WiFi 初始化
- 链接后的 map 文件里**无 wolfSSL / WiFi 符号**
