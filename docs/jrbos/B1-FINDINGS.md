# B1 调查记录 — 中文文件名乱码

> **结论：B1 在当前代码基线上不可复现。** 记录依据与唯一残留隐患，避免重复调查。
> 调查时间：2026-10-06（第 9–10 轮）

---

## 原始描述（据 PHASE1-PLAN.md / 设计文档）

> `recent.json` 里显示 `"瀵昏抗"`，而磁盘上实际是 `寻迹.epub`；
> 错误发生在"目录扫描 → 内存字符串"阶段；因传书只能靠人为拷贝，升级为**阻塞级**。

---

## 逐条验证结果

### ✅ ① `recent.json` 本身是正确的 UTF-8（否认了原始描述）

用户 SD 卡上 `F:\小纸pico\.crosspoint\recent.json`（2,130 B）：

```json
{"path":"/寻迹.epub","title":"寻迹","author":"Unknown",...}
{"path":"/解忧杂货铺 (东野圭吾) (z-library.sk, 1lib.sk, z-lib.sk).epub","title":"解忧杂货铺","author":"东野圭吾",...}
```

- **字节级验证**：`寻` 存的是 `e5 af bb`（标准 UTF-8），位于偏移 20
- 前 3 字节 `7B 22 62` → **无 BOM**
- 中文**全部正确**，8 条记录无异常

### ✅ ② `瀵昏抗` 是「读」的假象，不是「写」的错误

反向变换完全成立：

```python
'瀵昏抗'.encode('gbk').decode('utf-8')   # -> '寻迹'   ✅
```

即：**某处（很可能是 Windows 控制台/编辑器）用 GBK 去解码 UTF-8 字节**，于是
`e5 af bb` 被当作两个 GBK 汉字 `瀵` + 残留字节，产生 `瀵昏抗`。

**所以原始观察的来源是"读取工具编码错误"，不是设备缺陷。**

### ✅ ③ FAT 长文件名层配置正确

`USE_UTF8_LONG_NAMES=1` 出现在：
- `platformio.ini:49` 与 `:323`
- `freeink-sdk/.../SDCardManager/inject_build_flags.py:20`，列为 **`_ALWAYS`**（无条件注入到每个 lib builder）

该头文件注释写明了若不定义会怎样：
> 没有它，SdFat 会把任何非 ASCII 长文件名糟蹋成打不开的路径

### ✅ ④ 目录扫描与文件名处理是 UTF-8 安全的

`src/activities/home/FileBrowserActivity.cpp`：

| 位置 | 行为 | 判定 |
|---|---|---|
| `:32` `NAME_BUFFER_SIZE = 500` | 行/网格名缓冲 | ✅ 足够（最长实测 206 B） |
| `:884-885` `formatFileName` | `dot = filename.rfind('.')` 后 `%.*s` | ✅ 字节长度前缀**终止于字符边界** |
| `:776-780` 路径左截断 | 显式跳过 UTF-8 续字节 `(c & 0xC0) == 0x80` | ✅ **正确** |
| `:887` `utf8ComposeNfcInPlace(buffer)` | 仅作用于**显示副本** | ✅ 注释明确"filesystem lookup needs the raw entry bytes" |

**设计上明确保留原始字节用于文件系统查找**，只对显示副本做 NFC 归一化。方向是反的（不会破坏查找）。

### ✅ ⑤ 不存在 Unicode 归一化陷阱

对用户真实的 8 条路径逐条检测：

```
NFC == NFD : True   （全部 8 条）
```

所以即使某处做了 NFC/NFD 转换，也**不会**导致"列出但打不开"。

---

## ⚠️ 唯一发现的残留隐患（未证实是 B1 的原因）

`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:261`：

```cpp
char name[128];
for (auto f = root.openNextFile(); f && count < maxFiles; f = openNextFile()) {
  f.getName(name, sizeof(name));   // ← 128 字节
  ret.emplace_back(name);
}
```

- **风险**：路径超过 127 字节会被 `getName` 静默截断，表现为"文件列出但打不开"
- **用户实测最长路径：206 字节**（《枪炮、病菌与钢铁…》）→ **确实超过**
- **但**：`HalStorage::listFiles()`（唯一包装层）**在全仓库没有任何调用者**；
  `FileBrowserActivity` 直接用 `HalFile::getName(buf, 500)`，不走这个 128 字节路径
- **判定**：⚠️ **潜在缺陷，但当前不可达**。修它的成本极低（改 128→512），可顺手做掉作为加固

---

## 未能完成的验证（诚实记录）

- **无法做"设备列出的名字 ↔ 卡上真实字节"的直接比对**：SD 卡未挂在主机上（只有 `D:\ E:\ F:\`，均不含书中文件）
- 因此**不能 100% 排除**设备端存在某种显示层问题；但所有能在主机侧验证的环节都通过了

---

## 建议

1. **不按"乱码"去改** —— 那会去修一个不存在的写入缺陷
2. **向用户确认实际症状**：到底是
   - (a) 文件名显示成乱码？（则问题在渲染/字体，不在编码）
   - (b) 中文书名打不开？（则与 128 字节截断或 FAT 有关）
   - (c) 只是从电脑上看 `recent.json` 时乱码？（则**不是 bug**）
3. **顺手加固**：`SDCardManager::listFiles` 的 `char name[128]` → 512，避免将来被调用时踩坑
4. 若用户确认是 (a)，则改查**字形缺失**：内嵌 12pt 只有 3500 常用字，
   而《枪炮、病菌与钢铁…》《李世民私密生活全记录》等含生僻字，**渲染为方框**容易被误认为"乱码"
   —— 这与 B1 的原始描述高度吻合，且与 PHASE2 字库工作直接相关

---

## 附：验证脚本

| 脚本 | 用途 |
|---|---|
| `F:\paperread\b1_probe.py` | 穷举乱码反向变换，确认 `瀵昏抗 → 寻迹` |
| `F:\paperread\b1_nfc_check.py` | 对真实路径做 NFC/NFD 敏感性检测 |

**教训**：诊断编码问题时，必须先用**原始字节**确认存储内容，
否则"用错编码读取"会被误报成"写入错误"——本项目为此浪费了一轮调查。
