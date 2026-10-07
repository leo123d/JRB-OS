# PHASE1-LOG.md — 阶段 1「减法回收 Flash」实测记录

> 每步删除都**构建验证 + 记录固件字节数**，把"回收 1.6 MB"从估算变成实测。
> 分支：`paperread-phase1`（基线 commit `1512e8e`）
> 基线固件已备份：`F:\paperread\baseline-firmware.bin`
> 最后更新：2026-10-06

---

## 预算基准

| 项 | 值 |
|---|---|
| OTA 单槽 | 6,553,600 B（6.25 MiB） |
| 发布门槛（保留 512 KiB） | 6,029,312 B |
| **内嵌字库（三档压缩，已实测）** | **2,679,813 B** |
| **因此固件本体必须 ≤** | **3,349,499 B** ← 这是真正的目标线 |

---

## 检查点（git 提交）

> ⚠️ **每完成一个可构建的步骤就提交**。教训来源：见文末"流程教训"。

| 提交 | 内容 | 固件 |
|---|---|---|
| `1512e8e` | 基线（上游 HEAD） | 6,307,184 B |
| `fabbd3b` | step1: i18n → EN,ZH_CN | 5,892,352 B |
| `b3d8d0b` | step2: 删 `src/activities/apps/` + 阅读页 WeRead | **5,424,224 B** |
| 分支 `phase1-messy-archive` (`5da4ca6`) | **已废弃**：删除网络栈的失败尝试，仅留档 | — |

**回退方式**
```powershell
git -C F:\paperread\crossmux reset --hard b3d8d0b   # 回到已验证的绿状态
git -C F:\paperread\crossmux reset --hard 1512e8e   # 回到上游基线
```

---

## 进度表

| # | 步骤 | 固件大小 (B) | 相比上一步 | 相比基线 | 状态 |
|---|---|---|---|---|---|
| 0 | **基线**（未改造 CrossMux readpico） | **6,307,184** | — | — | ✅ 已验证 |
| 1 | **i18n：33 语言 → EN + ZH_CN** | **5,892,352** | **−414,832** | −414,832 | ✅ 已验证 |
| 2 | **删除 `src/activities/apps/`**（134 文件 / 1254 KB 源码） | **5,424,224** | **−468,128** | **−882,960** | ✅ 已验证 |
| | *OTA 槽剩余* | *1,129,376（1,103 KB）* | | | ✅ 远超 512 KiB 门槛 |
| | **目标线（留给字库 2.68 MB）** | **≤ 3,349,499** | | | ⬜ 还差 **2,074,725 B** |

**当前达成率：882,960 / 2,957,685 B ≈ 30%**（目标是从 6,307,184 降到 3,349,499）

---

## 步骤 2 详情：删除 apps 目录

**删除内容**：`src/activities/apps/` 整树，134 文件 / 1,254 KB 源码
- 游戏：2048、五子棋、中国象棋、扫雷、推箱子、数独、木鱼、像素开关、计算器
- AirPage 相册、丑头像（avatar）、Buddy、Standby 待机屏
- WeRead（微信读书）、ReadingStats（阅读统计）

**需要修的引用（逐轮编译暴露）**：

| 轮次 | 报错点 | 处理 |
|---|---|---|
| 1 | `ActivityManager.cpp:16` | 17 个 `#include "apps/..."` |
| 2 | `goToApps`/`goToStandby`×2/`ReadingStatsActivity`（5 处） | 删 16 个 `goTo*` 方法（cpp+h） |
| 3 | `HomeActivity.cpp:460 onAppsOpen` | 删 `HomeActivity::onAppsOpen` |
| 4 | `EpubReaderActivity.cpp:64 WeReadProgressSyncActivity` | **摘除阅读页的微信读书进度同步（12 处）** |
| 5 | `AppVisibilitySettingsActivity.cpp:8` | 删该活动 + `SettingsActivity` 引用 |

**附带完成的决策项**
- ✅ **决策 3 的核心部分**：微信读书从阅读页彻底摘除（含 `WeReadStore` 引用、书架提升、初始进度、长按同步、菜单同步动作、家键同步）
- ✅ **决策 8 的一部分**：`StandbyActivity`（待机屏含时钟/黄历面）删除，其 Back 键入口改走 `goToSleep(false)`
- ⬜ **待办**：`MainTab` 仍是 5 项（Recent/Library/Apps/Settings/Statistics）—— 枚举里 `Apps`/`Statistics` 已无活动可去，**需按决策 2/7 收敛为三项**
- ⬜ **待办**：`lib/WeReadWebApi`（373 KB 源码）尚未删；`I18nKeys.h` 里 91 个 WeRead 键仍在

**构建**：126s，exit 0，`Successfully created ESP32-S3 image.`

**产出脚本**（可复用）
- `F:\paperread\strip_apps.py` — 清 ActivityManager 的死引用
- `F:\paperread\fix_apps_refs.py` — 修 Tab 分派与 Home 菜单
- `F:\paperread\strip_weread.py` — 摘除阅读页 WeRead 集成
- `F:\paperread\del_appvis.py` — 删 AppVisibility 活动

---

## 步骤 1 详情：i18n 裁剪

**改动**：`platformio.ini` 第 137 行
```ini
custom_i18n_builtin_langs = all     →     custom_i18n_builtin_langs = EN,ZH_CN
```

**原理**：`lib/I18n/I18nStrings.cpp` 是**构建时生成**的文件（不在 git 里，由 `scripts/gen_i18n.py` 每次生成），
原本汇总 35 个语言 YAML 共 2.25 MB，是树里**最大的一块可回收 Flash**。

**实测效果**

```
Languages: 2 of 35 built in (EN, ZH_CN)
I18nStrings.cpp : 2,250,482 B → 277,827 B   (−1,972,655 B 源文件)
firmware.bin    : 6,307,184 B → 5,892,352 B (−414,832 B 固件)
```

> 注意：源文件减少 1.97 MB，但固件只减少 405 KB —— 因为大部分语言字符串本来就没被引用，链接器已经丢弃了。
> **这个 4.9:1 的比值是个重要教训：不能用源文件体积估算固件体积。**

**构建**：186s，exit 0，`Successfully created ESP32-S3 image.`

---

## 关键教训汇总（供后续步骤参考）

1. **不能用源文件体积估算固件体积**（本例 4.9:1）。每步必须实测 `firmware.bin`。
2. **构建配置**：改 `platformio.ini` 前**必须**先跑过一次
   `python scripts/patch_pioarduino_cache.py --prepare-platform`，否则必然失败（详见 `BUILD.md` §4）。
3. 增量构建约 **80s**，改 i18n 触发生成器时约 **186s**，冷编译约 **292s**。
4. **不要在构建不通时做大删改** —— 无法区分"删错了"和"环境坏了"。

---

## 下一步候选（按预计收益 / 风险排序）

| 目标 | 源码体积 | 引用面 | 风险 | 备注 |
|---|---|---|---|---|
| `src/network/` 整个网络栈 | 1,220 KB / 27 文件 | **23 个文件 include** | 高（深切割，要改 `main.cpp`） | 设计文档说收益最大（~400 KB） |
| `lib/WeReadWebApi` + `apps/weread` | 552 KB / 26 文件 | `I18nKeys.h` 91 处、`EpubReaderActivity.cpp` 29 处 | 中高（与阅读页进度同步耦合） | 决策 3 |
| `src/activities/apps/` 其余（游戏/airpage/standby/avatar…） | ~900 KB / 122 文件 | `MainTab::Apps` + `ActivityManager` | 中（要动 Tab 体系） | 决策 2 + 非目标 |
| `MainTab` 5 → 3 项 | — | `MainTab.h` + 各活动 | 中 | 决策 2 / 7 |

> ⚠️ **待确认项已解决**：`KOReaderSyncActivity.cpp` **依赖网络**（include 了网络头文件），
> 所以它也属于决策 6 的删除范围 —— **KOReader 进度同步在纸读上不可用**。
> 设计文档里"保留 KOReader 同步"的表述需据此修订。

---

## 流程教训（第 4 轮踩到的，务必遵守）

### 教训 A：每一步都要提交检查点

**发生了什么**：一次性做了「i18n + 删 apps + 删网络栈」三件大事且**没有提交**，
然后一个脚本错误导致半应用状态（文件没删成、引用却被改坏），
**那个已验证的 5,424,224 B 绿状态在 git 里没有任何记录**，只能从 stash 里手工捞回。

**规则**：
1. **每个能构建通过的步骤立刻 commit**（已建立 `fabbd3b` / `b3d8d0b`）。
2. 大改动（如整个网络栈）**拆成多次小提交**，每次都可构建。
3. 改之前先确认 `git status` 干净。

### 教训 B：不要用多行正则删 C++ 代码

**发生了什么**：用 `re.sub` 删 `return activityManager.replaceActivityWith<KOReaderSyncActivity>(...)`
时只匹配到首行，**把多行语句截断**，留下孤儿代码 `std::move(localChapterName));`，
产生 `expected ';' before ')'`。

**规则**：
- 删函数体用**花括号配对扫描**（`depth += ...count('{') ...`），不要用行正则。
- 删单条语句前先**打印该行的字节表示**确认匹配。
- 改完立刻**在同一个进程内回读验证**（`t2 = read(); print(t2.count(...))`）。

### 教训 C：脚本里的路径常量要先验证

**发生了什么**：脚本里写 `SET = S + r"\src\settings"`，但真实路径是
`src\activities\settings`。于是「删除文件」**静默什么都没删**（被 `if os.path.exists` 挡住），
而随后的「剥离引用」却把那些文件改坏了 —— 一半成功一半失败。

**规则**：`os.remove` / `rmtree` 前后都要 `assert` 存在性与结果；
路径常量先 `print(Test-Path ...)` 验证。

### 教训 D：PowerShell 的 here-string 会吞引号

`& python -c @" ... "@` 会把 Python 代码里的 `"` 弄乱（实测出现
`p = rF:\paperread\...` 缺引号）。**一律写成 `.py` 文件再执行。**

---

## 网络栈删除的正确做法（待下次执行）

上一轮的失败尝试（`phase1-messy-archive`）**顺序是对的、只是执行太粗**。复盘后的稳妥顺序：

| # | 步骤 | 说明 |
|---|---|---|
| 1 | 把 OTA 基元**移出** `src/network/` → `src/ota/` | `FirmwareFlasher`（**已是 SD 卡本地 OTA 实现**）、`OtaBootSwitch`、`FirmwareBoardTag`、`ProtectedPaths`。**不要删这三个**，决策 6 要求的 SD 本地 OTA 已存在（`SdFirmwareUpdateActivity`） |
| 2 | 删 `src/activities/network/`（5 个活动） | WifiSelection / CrossPointWebServer / UsbDrive / CalibreConnect / NetworkModeSelection |
| 3 | 删 `src/activities/browser/`、`CatalogActivity`、`plugins/`、`util/PluginEvents`、`util/PluginLocations`、`util/PluginHttp` | 均需联网 |
| 4 | 删 `src/network/` 其余（OtaUpdater / HttpDownloader / WebDAVHandler / CrossPointWebServer / WifiPowerSaveGuard / CrossMuxEndpoints） | `OtaUpdater` 是联网 OTA，删；本地 OTA 走 step 1 |
| 5 | 删 `NetworkStartup.*`、`WifiCredentialStore.*`、`OpdsServerStore.*` | |
| 6 | 删 3 个联网 settings 活动 | `ClockSyncActivity`（决策 8）、`FontDownloadActivity`（内置字库已覆盖）、`DictionaryDownloadActivity` |
| 7 | 删 KOReader 三件套 | `KOReaderSyncActivity`、`KOReaderAuthActivity`、`KOReaderSettingsActivity` —— 全都需要 Wi-Fi |
| 8 | 清理 `main.cpp` 的 WiFi/SNTP/JoinNetwork/Plugin 调用点 | **单独一步，单独提交** |
| 9 | 清理 `HomeActivity` 菜单（已删 OPDS/APPS/FILE_TRANSFER） | 见下方"菜单收敛" |
| 10 | 清 `platformio.ini` 的 wolfSSL/TLS flags、网络 lib_deps、`patch_wolfssl.py` | **收益最大的一步** |

### 菜单收敛（决策 7：仅三项）

当前 `HomeActivity` 的两个菜单表（`kDefaultMenuOrder` / `kCarouselMenuOrder`）各 6 项。
上一轮已把索引错位参数 `hasOpds` 彻底移除，并删掉了 `OPDS_BROWSER` / `APPS` / `FILE_TRANSFER` 行，
目标状态是**各 3 项**：

```
kDefaultMenuOrder  : FILE_BROWSER / LIBRARY / SETTINGS_MENU
kCarouselMenuOrder : FILE_BROWSER / RECENTS / SETTINGS_MENU
```
> 注意 `menuEntryAtIndex()` 里原有的 `if (!hasOpds && index >= 2) ++index;` 索引移位逻辑
> 是 OPDS 独有的，删除 OPDS 后**必须一并移除**，否则断言 `array subscript value '5' is outside bounds`。
> `HomeActivity.cpp` 里有 8 条 `static_assert` 会立刻抓到这个错误 —— 它们是有用的护栏，不要删。
