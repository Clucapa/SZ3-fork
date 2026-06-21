# SZ3 QOI 编码与 qoiParams 格式说明

## 总体结构

`conf.qoi` 是一个 `int`（32 bit）。高位 nibble 编码模式，`qoi < 0`（bit31=1）为 Regional 族。

工厂函数 `GetQOI(conf)` 按优先级依次判断：

```cpp
if ((qoi >> 28) & 0xF == 7 && qoi > 0)   → FX 模式（pointwise）
if ((qoi >> 28) & 0xF == 6)              → Isoline 模式
if (qoi < 0) {
    if ((raw >> 28) & 0xF == 7)          → Conv 模式（Regional 族）
    if ((raw >> 28) & 0x3 == 0x3)        → Regional FX
                                          → Regional nibble
}
if ((qoi >> 28) & 0xF != 0)              → throw（非法）
                                          → 常规 nibble 模式
```

## qoiParams 存储格式

`Config::qoiParams` 内部存储为 **原始二进制**（`std::vector<unsigned char>`），不是 base64 字符串。

```
encoder CLI 输出:  base64 文本 (可打印, 便于 CLI 使用)
        ↓ base64_decode
Config 存储:      原始二进制字节
        ↓
QoIIf 工厂:       直接读取
```

- 二进制文件格式（`Config::save/load`）直接写入/读取原始字节
- INI 文本格式（`Config::save_ini/load_ini`）通过 base64 编解码做文本传输

---

## 模式 0 —— 常规 Nibble 编码

高位 nibble 必须为 `0`（`(qoi >> 28) & 0xF == 0`）。

低 28 bit 每 4 bit 为一格（nibble），LSB 先行。特殊 nibble：

| nibble | 语义 |
|--------|------|
| `0x0` ~ `0xB` | 基函数 |
| `0xC` ~ `0xD` | 预留 |
| `0xE` | Compose(f, g) — prefix 式，严格 2 操作数 |
| `0xF` | 组分隔符 — 结束当前 group，开始下一 group |

### 组内组合（SumQoI）

同一 group 内的函数 nibble 按 SumQoI 求和约束，约束 `Σ|fi(orig) - fi(dec)| ≤ qEB`。

```
qoi = 0x12      ← nibbles [2,1] = sqr + cubic
qoi = 0x128     ← nibbles [8,2,1] = abs + cubic + sqr
```

### 组间组合（MultiQoI / AND）

`0xF` 分隔不同 group，组间为 AND 关系：

```
qoi = 0x1F3    ← group 1: [1]=sqr, group 2: [3]=sqrt
```

生成 `QoI_MultiQoI`，同时满足两组约束，eb 取各组 min。

### Compose

`0xE` 标记函数嵌套：`Compose(f, g) = f(g(x))`：

```
qoi = 0x14E    ← nibbles [E,4,1] = Compose(Exp, Sqr) = e^(x²)
```

恒等消除：`Compose(XLin(1,0), g)` → `g`，`Compose(f, XLin(1,0))` → `f`。

### 基函数 Nibble 与参数

| nibble | 类名 | f(x) | 参数 | 默认值 |
|--------|------|------|------|--------|
| `0x0` | QoI_XLin | A·x + B | A (double), B (double) | 1.0, 0.0 |
| `0x1` | QoI_X2 | x² | — | — |
| `0x2` | QoI_XCubic | x³ | — | — |
| `0x3` | QoI_XSqrt | √x | — | — |
| `0x4` | QoI_XExp | aˣ | base a (double) | e |
| `0x5` | QoI_XLogX | x·log(x) | — | — |
| `0x6` | QoI_LogX | log_a x | base a (double) | e |
| `0x7` | QoI_XRecip | 1/x | — | — |
| `0x8` | QoI_XAbs | \|x\| | — | — |
| `0x9` | QoI_XSin | sin(x) | — | — |
| `0xA` | QoI_XTanh | tanh(x) | — | — |
| `0xB` | QoI_XPower | x^a | expo a (double) | 2.0 |

### qoiParams 格式（模式 0 / nibble）

`qoiParams` 为所有基函数参数按 nibble 遍历顺序编码的 **base64 编码 double[]**。顺序遵循 nibble 的 LSB→MSB 遍历，每个函数按上表消费对应数量的 double。无参数为空串，全用默认。

---

## 模式 6 —— Isoline 等值线模式

高位 nibble 为 `0x6`，`qoi = 0x60000000 | (nibble_qoi)`。

子 QoI 编码与模式 0 一致。`qoiParams` 前半段为函数参数，**紧接着**每个 group 追加 4 个 double 作为 isoline 配置：`[min, max, count, meb]`。

### Encoder 语法

```bash
./qoi_encoder "iso6(sqr, -5, 5, 3, 0.01)"
./qoi_encoder "iso6(sqr, -5, 5, 3, 0.01 ; abs, -3, 3, 3, 0.001)"
./qoi_encoder "iso6(exp, 0, 5, 3, 0.001)"
```

---

## 模式 7 —— FX 任意函数模式（pointwise）

高位 nibble 为 `0x7`，`qoi = 0x70000000`。

### qoiParams 格式

```
[4B uint32: len_f] [len_f bytes: f_str]
[4B uint32: len_df] [len_df bytes: df_str]
[4B uint32: len_ddf] [len_ddf bytes: ddf_str]
+ padding 0 → base64
```

f_str/df_str/ddf_str 为 TinyExpr 表达式（`^` 表示幂运算）。encoder 内部用 SymEngine 解析 → 符号求导 → 转换字符串。

### Encoder 语法

```bash
./qoi_encoder "sin(x)+x^2"        # 自动 fallback 到 FX
```

---

## Regional QoI 族（`qoi < 0`）

Regional QoI 通过 `conf.qoi = ~raw` 翻转存储，解码时 `qoi < 0` 判别。

### 翻转前位布局

```
bit 31: 固定 0（翻转后变 1，保证 qoi<0）
bit 30: 预留（置 0）
bit 29-28: 00=nibble, 11=FX, 其他=见下
bit 27-0: 子编码
```

### 编码类型

| 类型 | 翻转前 raw | 翻转后 conf.qoi | 高nibble | qoiParams |
|------|-----------|-----------------|:--:|------|
| Regional nibble | 低 28bit nibble | `~(nibble_qoi)` | F | nibble func params |
| Regional FX | `0x30000000` | `~\u0030x30...` | C | FX binary (f/df/ddf) |
| Conv 1D | `0x7 d 00 00 w` | `~\u0030x7d0000w` | 8 | double[w] weights + double tol |
| Conv 2D | `0x7 d 00 h w` | `~\u0030x7d00hw` | 8 | double[h×w] weights + double tol |

其中 `d` = 维度（1=1D, 2=2D），`w` = 宽度，`h` = 高度。

约束类型：
- nibble/FX：块内聚合 `|mean(f(orig)) - mean(f(dec))| ≤ qEB`
- Conv：逐滑动位置 `|conv[p] - conv[p]| ≤ tol`

### Encoder 调用

```bash
# Regional nibble / FX
./qoi_encoder --regional "sqr+cubic"
./qoi_encoder --regional "sin(x)+x^2"

# Convolution
./qoi_encoder "conv(1,1,1,0.1)"              # 3 点滑动和
./qoi_encoder "conv(1,-2,1,0.5)"             # Laplacian
```

---

## 高位 Nibble 速查

| 高位 nibble | 判断方式 | 模式 | qoiParams 格式 |
|-------------|----------|------|---------------|
| `0x0` | `(qoi>>28)&0xF==0, qoi≥0` | 常规 nibble | double[] — 函数参数 |
| `0x6` | `(qoi>>28)&0xF==6` | Isoline | double[] — 函数参数 + [min,max,cnt,meb]×N |
| `0x7` | `(qoi>>28)&0xF==7, qoi≥0` | FX pointwise | binary — 3 个 TinyExpr 字符串 |
| `0xF` | `qoi<0, bit29-28=00` | Regional nibble | double[] — nibble 函数参数 |
| `0xC` | `qoi<0, bit29-28=11` | Regional FX | FX binary |
| `0x8` | `qoi<0, (raw>>28)&0xF==7` | Conv (Regional 族) | double[] — 核权重 + tol |
| 其他非零 | — | throw invalid_argument | — |
