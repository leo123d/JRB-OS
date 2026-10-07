# FONT-STRATEGY.md — 中文字库策略与实测数据

> 阶段 2 的关键依据。所有数字均为**实测**，非估算。
> 生成脚本：`F:\paperread\measure_cjk_compress.py`、`F:\paperread\font_size_true.py`
> 最后更新：2026-10-06

---

## 1. 最重要的发现：上游已经内嵌了 CJK 字体

`lib/EpdFont/builtinFonts/` 里有 6 个未压缩 CJK 字体（源码头文件合计 12.8 MB），
但**只有 3 个真正被链接进固件**（用 linker map 实测，其余 3 个是死代码，不占空间）：

| 字体 | 覆盖 | Bitmaps | Glyphs | Intervals | 实测小计 |
|---|---|---|---|---|---|
| `notosans_cjk_12` | **3500 常用字** ∪ i18n | 525,663 | 64,224 | 8,724 | **598,611 B** |
| `notosans_cjk_14` | 仅 i18n 用字 | 190,951 | 20,800 | 8,724 | **220,475 B** |
| `notosans_cjk_16` | 仅 i18n 用字 | 244,194 | 20,800 | 8,724 | **273,718 B** |
| `notosans_cjk_8/10/18` | — | **0（未被链接）** | | | **免费** |
| | | | | | **合计 ≈ 1,092,804 B** |

**这意味着**：
1. 中文**已能显示**（3500 常用字，12pt）
2. 大字号中文只有 UI 用字 → **正文中文靠 SD 卡字库**（这就是为什么设备需要 `.cpfont`）
3. 上游的分层策略见 `lib/EpdFont/scripts/build-cn-builtin-fonts.sh`

> ⚠️ **不要用 `.h` 文件体积判断**：位图存为十六进制文本，文件大小约为真实字节的 **6 倍**。
> 唯一可信的测量口径是 **linker map**（`.pio/build/readpico/firmware.map`）。

---

## 2. 实测：DEFLATE 压缩的收益

用仓库自带的 `fontconvert.py --2bit --compress`，源字体 `NotoSansSC-Regular.otf`，
按上游相同参数（`--additional-intervals 0x4E00,0x9FFF` 等）实测。

### ⚠️ 先修正一个坑：`gb2312_lv1.txt` 只是一级字

仓库里的 `gb2312_lv1.txt` 只有 **3,755 字**（GB2312 **一级**），
而设计文档要的是**一二级共 6,763 字**。字符集必须自己从 codec 生成：

```python
hanzi = []
for hi in range(0xB0, 0xF8):
    for lo in range(0xA1, 0xFF):
        try: ch = bytes([hi, lo]).decode("gb2312")
        except UnicodeDecodeError: continue
        if len(ch) == 1 and 0x4E00 <= ord(ch) <= 0x9FFF: hanzi.append(ch)
# -> 6763 unique
```

### GB2312 一级+二级（6,763 字，✅ 设计目标）—— **权威数字**

| 档位 | Bitmaps | Glyphs | Intervals | Groups | **总计** |
|---|---|---|---|---|---|
| 12pt | 571,347 | 101,640 | 42,696 | 342 | **716,025** |
| 14pt | 685,415 | 101,640 | 42,696 | 432 | **830,183** |
| 20pt | 1,028,412 | 101,640 | 42,696 | 792 | **1,173,540** |
| **合计** | **2,285,174** | **304,920** | **128,088** | **1,566** | **2,719,748 B (2.59 MB)** |

### GB2312 仅一级（3,755 字）—— 对照

| 档位 | 压缩后总计 |
|---|---|
| 12pt | 406,914 |
| 14pt | 470,034 |
| 20pt | 657,732 |
| 合计 | 1,534,680 |

### 上游 i18n 档（14pt，仅 UI 用字，4,252 glyph）

| | 原始 | 压缩 | 压缩率 |
|---|---|---|---|
| 14pt i18n | 217,875 | **116,244** | 0.53 |

### 关键结构事实

每个字体由四部分组成，**只有 bitmaps 会被压缩**：

```
EpdFontData = Bitmaps[] + EpdGlyph Glyphs[] + EpdUnicodeInterval Intervals[] + EpdFontGroup Groups[]
                 ↑ 可压缩          ↑ 固定 14 B/字      ↑ 固定 12 B/区间     ↑ 固定 18 B/组
```

- `EpdGlyph` = **14 B/字** → 6,763 字 → **94,682 B + 表头 ≈ 101,640 B/档**
  **每档一份，无法压缩、无法跨档共用** —— 三档共 304,920 B，这是**压缩挤不掉的固定成本**
- `EpdUnicodeInterval` = **12 B/区间** → 连续区间合并后 3,558 个 → **42,696 B/档**
- 压缩后多出 `Groups[]`（18 B/组，几百字节，可忽略）

**压缩率**：bitmaps 部分 12pt 约 0.57、14pt 约 0.55、20pt 约 0.47。

> **结论**：早先"三档压缩 ≈ 2,679,813 B"的估算**是准确的**（实测 2,719,748 B，差 1.5%）。
> 中途我曾按一级字得到 1,534,680 B 而误以为估算偏悲观 —— 那是**字符集搞错**，不是估算错。

---

## 3. ⚠️ 但有一个必须先验证的风险

`build-cn-builtin-fonts.sh` 第 17–20 行明确说明了**为什么 CJK 故意不压缩**：

```sh
# Raw bitmaps (no --compress): with 6 simultaneously-loaded CJK fonts each
# group would need ~50 KB scratch, fragmenting the heap on boot and crashing
# FontDecompressor with std::bad_alloc. Latin fonts ship compressed because
# their groups are tiny (~5 KB).
```

而 `FontDecompressor.h` 第 72–74 行记录了那次**真实现场崩溃**及其修复：

```cpp
// Nothrow high-water malloc buffers, NOT std::vector: getBitmap() runs on the
// render path, and under -fno-exceptions a vector resize that hits OOM abort()s
// the firmware instead of failing (field crash: hotGroup.resize() ->
// std::bad_alloc -> abort with ~11 KB free).
```

### 为什么那条理由**可能已过时**

我核实了当前代码的解压缓冲区分配器：

```c
// freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c:11
void* fiFontMalloc(size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);        // ← PSRAM 优先（8 MB）
  if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}
```

`FontDecompressor.cpp` 的**所有**解压缓冲都走它：
- 第 57 行 `hotGroup`（热组缓存）
- 第 367–368 行 page buffer + lookup
- 第 477 行 prewarm 临时缓冲

**所以那 ~50 KB scratch 不再抢那 30–45 KB 内部堆，而是走 8 MB PSRAM。**

### 结论与行动

**不能只凭读代码就砍掉这个约束** —— 它是真机崩溃换来的。

**验证路径**（在上游现成字体上做，最省事）：
1. 用 `build-cn-builtin-fonts.sh` 加 `--compress` 重新生成 `notosans_cjk_12/14/16`
2. 构建并上真机
3. **重点观察**：开机是否 `std::bad_alloc` / `abort`；连续翻 200 页中文正文；切换字号
4. 记录 `peakTempBytes`（`FontDecompressor` 已内建该计数器，见 `.h` 第 42 行）

**预期收益**：12/14/16pt 的 961 KB bitmaps → 约 513 KB，**纯赚约 448 KB，功能完全不变**。

**若验证失败**：退回未压缩，改走"扩大 SD 卡字库覆盖"或"减少档位"路线。

---

## 4. 上游已有的字库工具链（可直接复用，不必自己写）

`lib/EpdFont/scripts/`：

| 文件 | 用途 |
|---|---|
| `fontconvert.py` | 核心生成器，支持 `--2bit` / `--compress` / `--zopfli` / `--characters` / `--additional-intervals` |
| `fontconvert_sdcard.py` | `.cpfont`（SD 卡字库）生成器 |
| `build-cn-builtin-fonts.sh` | **内嵌 CJK 字体的完整构建流程** |
| `build_cn_charset.py` | 从词频生成字符集（`--top N`，`--require-from` 强制包含） |
| `share-cn-font-intervals.py` | 生成 `notosans_cjk_common_intervals.h`（共享 interval 表） |
| `verify_compression.py` | **压缩正确性验证器** |
| `gb2312_lv1.txt` | GB2312 一级字符集（11,265 B） |
| `cn_common_chars.txt` | 3500 常用字 ∪ i18n 必须字 |
| `cn_i18n_chars.txt` | 仅 i18n 用字 |
| `chars_3500_common.txt` | 3500 常用字表 |
| `sd-fonts.yaml` | SD 卡字库清单 |

**`--zopfli` 也装了**（`zopfli` 包可用），比 zlib -9 再小几个百分点，且生成耗时无所谓。

### 源字体从哪来

上游**不把 `NotoSansSC-Regular.otf` 提交进仓库**（`build-cn-builtin-fonts.sh` 会报错让你自己放）。
我们已有的副本：**`F:\paperread\font-probe\NotoSansSC-Regular.otf`（8,331,336 B）**。

> ⚠️ FreeType **无法打开非 ASCII 路径下的字体**，所以生成必须在 ASCII 路径下做
> （工作目录 `F:\paperread\fontwork`）。

---

## 5. 目标线重算（全部为实测）

```
OTA 单槽                            6,553,600 B
发布门槛（保留 512 KiB 余量）        6,029,312 B
当前固件（已删网络栈）               4,682,288 B
                                    余量 1,347,024 B

当前固件里已含的 CJK 字体开销           1,092,804 B  ← 已计入上面的 4,682,288
  （12pt 3500常用字 598,611 + 14pt i18n 220,475 + 16pt i18n 273,718）

当前固件的"非字库"部分               3,589,484 B  ← 4,682,288 − 1,092,804
```

### 方案 A：把上游现有 CJK 字体改为压缩（✅ **已实施**，提交 `2bdd6dd`）

**实测结果**（DEFLATE，`--compress`，字符覆盖逐一核对一致）：

| 字体 | 原 Bitmaps | 压缩后 | glyph 数 | 原总计 | 压缩后总计 | **省下** |
|---|---|---|---|---|---|---|
| `notosans_cjk_12` | 525,663 | 296,438 | 4,014（一致） | 581,859 | 383,126 | **198,733** |
| `notosans_cjk_14` | 190,951 | 89,212 | 1,300（一致） | 217,875 | 116,244 | **101,631** |
| `notosans_cjk_16` | 244,194 | 103,586 | 1,300（一致） | 271,118 | 130,636 | **140,482** |
| | | | | | | **440,846** |

**固件实测**：
```
4,682,288 B → 4,211,216 B   (−471,072 B / −460 KB)
```
> 实测比预测（−440,846）**还多省 30 KB** —— 因为压缩后 interval 表变小（连续码位更容易合并）。

**原始未压缩字体已备份**：`lib/EpdFont/builtinFonts/raw_backup/`（可随时回退，也可 `git revert 2bdd6dd`）

⚠️ **仍需真机验证**：见 §6 待验证清单。上游刻意不压缩是有历史崩溃原因的，虽然当前
PSRAM 优先分配器可能已解决，但**未经真机证实**。

### 方案 C：三档完整 GB2312 内嵌（设计目标，**待做**）

```
非字库部分              3,589,484 B
+ 三档 GB2312 压缩字库   2,719,748 B
= 预测固件              6,309,232 B
发布门槛                6,029,312 B
                       超支 279,920 B  ❌ 还差约 280 KB
```

**注意**：方案 A 已省下 471,072 B，所以现在：
```
当前固件 4,211,216 B − 现有 CJK 字库 1,092,804 B + GB2312 三档 2,719,748 B
= 5,838,160 B  ✅ 已低于发布门槛 6,029,312，余量 191,152 B
```
**方案 A + C 组合后三档完整 GB2312 可以放下**（但余量只有 191 KB，偏紧）。

其他可选优化：
1. 若 12pt GB2312 兼任 UI 字体，可删掉现有 3500 字版本
2. 未链接的死字体（`cjk_8/10/18`、`notoserif_16_*` 等）可从仓库删除以减小仓库体积（不影响固件）
3. 只嵌 **12pt + 20pt 两档**（省 830,183 B）→ 余量充足

---

## 6. 待验证清单

- [ ] 压缩 CJK 在真机上是否稳定（`std::bad_alloc` / `abort` / 启动崩溃）
- [ ] `peakTempBytes` 实测峰值是多少（PSRAM 是否足够）
- [ ] 连续翻页 200 次是否内存碎片化
- [ ] 切换字号（12↔14↔16↔20）是否触发重新分配失败
- [ ] 压缩后中文渲染是否与未压缩**逐像素一致**（用 `verify_compression.py`）
