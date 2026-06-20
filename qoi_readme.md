# SZ3 QOI 编码与 qoiParams 格式说明

## 总体结构

`conf.qoi` 是一个 `int`（32 bit），低 28 bit 为数据载荷，高 4 bit（bit 28–31）编码模式。

```
bit:  31 30 29 28  |  27 ... 0
      高位 nibble   |  低位数据
```

工厂函数 `GetQOI(conf)` 按优先级依次判断：

```cpp
if ((qoi >> 28) & 0xF == 7)  → FX 模式
if (qoi < 0)                  → Regional（bit 31 = 1）
if ((qoi >> 28) & 0xF == 6)  → Isoline 模式
if ((qoi >> 28) & 0xF != 0)  → 非法
                              → 常规 nibble 模式
```

**重要区分**：pointwise QoI（常规/FX/Isoline）的 `qoi` 均为正数或零；Regional QoI 的 `qoi` 为负数（`~rid` 翻转）。高位 nibble 对 Regional 而言是 `~` 翻转产生的 `0xF` 而非显式编码的模式标记——解码时通过 `qoi < 0` 判别。

## qoiParams 存储格式

`Config::qoiParams` 内部存储为 **原始二进制**（`std::vector<unsigned char>`），不是 base64 字符串。

```
encoder CLI 输出:  base64 文本 (可打印, 便于 CLI 使用)
        ↓ base64_decode
Config 存储:      原始二进制字节
        ↓
QoIIf 工厂:       直接读取 ParamReader / QoI_FX(conf.qoiParams)
```

- 二进制文件格式（`Config::save/load`）直接写入/读取原始字节
- INI 文本格式（`Config::save_ini/load_ini`）通过 base64 编解码做文本传输
- base64 仅在 encoder CLI 和 INI 文本中扮演传输角色，存储格式始终是原始二进制

---

## 模式 0 —— 常规 Nibble 编码

高位 nibble 必须为 `0`（`(qoi >> 28) & 0xF == 0`）。

低 28 bit 每 4 bit 为一格（nibble），LSB 先行。特殊 nibble：

| nibble | 语义 |
|--------|------|
| `0x0` ~ `0xB` | 基函数（见下表） |
| `0xC` ~ `0xD` | 预留 |
| `0xE` | Compose(f, g) — prefix 式，严格 2 操作数 |
| `0xF` | 组分隔符 — 结束当前 group，开始下一 group |

### 组内组合（Sum）

同一 group 内的函数 nibble 按 SumQoI 求和约束：

```
qoi = 0x12      ← nibbles [2,1] = sqr + cubic
qoi = 0x128     ← nibbles [8,2,1] = abs + cubic + sqr
```

生成 `QoI_SumQoI`，约束 `Σ|fi(orig) - fi(dec)| ≤ qEB`。

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

### 编码示例

| `conf.qoi` | nibbles (LSB→MSB) | 含义 |
|------------|-------------------|------|
| `0x1` | `[1]` | XSquare |
| `0x12` | `[2,1]` | XSquare + XCubic (Sum) |
| `0x1F3` | `[3,F,1]` | XSquare AND XSqrt (MultiQoI) |
| `0x14E` | `[E,4,1]` | Compose(XExp, XSquare) |
| `0x12F3F456` | `[6,5,4,F,3,F,2,1]` | Sum(1,2) AND 3 AND Sum(4,5,6) |
| `0x0E1E2E3` | `[3,E,2,E,1,E,0]` | Compose(Lin, Compose(Sqr, Compose(Cubic, Sqrt))) |

### 基函数 Nibble 与参数对照

基函数可能有参数（如 XLin 的 A/B、XExp 的 base），参数按 nibble 遍历顺序依次放入 `conf.qoiParams`。

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

### qoiParams 格式（模式 0）

`qoiParams` 为所有基函数参数按 nibble 遍历顺序编码的 **base64 编码 double[]**。顺序遵循 nibble 的 LSB→MSB 遍历，每个函数按上表消费对应数量的 double：

```
表达式: lin(2, 0.5) + exp(10) + sqr
nibbles: [0, 4, 1]   ← sqr(1), exp(4), lin(0)
→ lin 读 2 个: [2.0, 0.5], exp 读 1 个: [10.0], sqr 读 0 个
→ double[] = [2.0, 0.5, 10.0]
→ base64_encode([2.0, 0.5, 10.0])
```

无参数时 `qoiParams = ""`（空串），全部使用默认值。

Legacy id `qoi = 0` 和 `qoi = 1` 解码为 XLin(1,0) 和 X2，无参数。

---

## 模式 6 —— Isoline 等值线模式

高位 nibble 为 `0x6`，`qoi = 0x60000000 | (nibble_qoi)`。

### 子 QoI 编码（与模式 0 一致）

低 28 bit 使用与模式 0 完全相同的 nibble 编码。空 nibble（`qoi = 0x60000000`）退化为 raw data isoline（子 QoI = XLin(1,0)）。

### Isoline 配置参数

`qoiParams` 前半段与模式 0 完全一致（所有 group 的基函数参数），**紧接着**每个 group 追加 4 个 double 作为 isoline 配置：

```
qoiParams = base64( [func_params_group1..., func_params_group2..., ...]
                    [min1, max1, count1, meb1]
                    [min2, max2, count2, meb2]
                    ... )
```

| 参数 | 类型 | 含义 |
|------|------|------|
| `min_v` | double | 等值线值域下界 |
| `max_v` | double | 等值线值域上界 |
| `count` | double (→int) | 等值线数量 |
| `meb` | double | 最小误差界（minimum error bound） |

等值线计算公式（与 `QoI_IsolineNibble::generate_isovalues()` 一致）：

```
isovalues[i] = min_v + (i + 1) * (max_v - min_v) / (count + 1)
```

即 `count` 条等值线将 `[min_v, max_v]` 均分为 `count + 1` 段。

### 解码流程

```
1. nibble_qoi = qoi & 0x0FFFFFFF
2. groups = parse_qoi_nibbles(nibble_qoi)
3. 若 groups 为空 → 插入空 QoIGroup（退化为 raw XLin）
4. 遍历 groups，从 ParamReader 依次消费函数参数，构建子 QoI
5. 遍历 groups，从 ParamReader 依次消费 [min, max, count, meb] × 4
6. 每个子 QoI 包装为 QoI_IsolineNibble（内部子 QoI + IsolineConfig）
```

### 编码示例

```
# 单组：sqr + isoline
qoi = 0x60000001          # nibble 0x1 = sqr
qoiParams = base64([-5, 5, 3, 0.01])
→ isolines 在 [-5, 5] 内设 3 条: -2.5, 0, 2.5

# 多组：sqr 和 abs 各自独立 isoline
qoi = 0x600003F1          # nibbles [1, F, 3] = sqr | abs
qoiParams = base64([-5, 5, 3, 0.01,   -3, 3, 3, 0.001])
→ group 1 (sqr): func_params=[], iso=[-5,5,3,0.01]
→ group 2 (abs): func_params=[], iso=[-3,3,3,0.001]

# 带参数函数 + isoline
qoi = 0x60000004          # nibble 0x4 = exp, 默认 base=e
qoiParams = base64([e,   0, 5, 3, 0.001])
→ group 1 (exp): func_params=[e], iso=[0,5,3,0.001]
```

### qoi_encoder 语法

```bash
./qoi_encoder "iso6(sqr, -5, 5, 3, 0.01)"
./qoi_encoder "iso6(sqr, -5, 5, 3, 0.01 ; abs, -3, 3, 3, 0.001)"
./qoi_encoder "iso6(sqr+cubic, -5, 5, 3, 0.01)"
./qoi_encoder "iso6(exp, 0, 5, 3, 0.001)"
```

语法：`iso6(nibble_expr, min, max, count, meb [; nibble_expr2, min2, max2, count2, meb2] ...)`

---

## 模式 7 —— FX 任意函数模式

高位 nibble 为 `0x7`，`qoi = 0x70000000`（固定值，低 28 bit 不使用）。

### qoiParams 格式

`qoiParams` 为三个 TinyExpr 字符串序列化的 base64 编码：

```
[4B uint32: len_f] [len_f bytes: f_str]
[4B uint32: len_df] [len_df bytes: df_str]
[4B uint32: len_ddf] [len_ddf bytes: ddf_str]
+ padding 0 填充到 3 的倍数
→ base64
```

| 字段 | 含义 | 示例 |
|------|------|------|
| `f_str` | f(x) 表达式，TinyExpr 格式 | `sin(x)+x^2` |
| `df_str` | f'(x) 一阶导数 | `cos(x)+2*x` |
| `ddf_str` | f''(x) 二阶导数 | `-sin(x)+2` |

注意：TinyExpr 使用 `^` 表示幂运算（如 `x^2`），而 SymEngine 使用 `**`。`fx_encode()` 内部将 `**` 替换为 `^`。

### 编码管道

```
用户输入: fx("sin(x)+x^2")
   → SymEngine::parse("sin(x)+x^2")     → f_expr
   → f_expr->diff(x) / diff(x)           → df_expr, ddf_expr
   → sym_to_te(__str__())                → "sin(x)+x^2", "cos(x)+2*x", "2-sin(x)"
   → verify_te_string() 各验证可编译
   → 序列化为二进制 + base64
   → qoi = 0x70000000
```

### qoi_encoder 语法

```bash
./qoi_encoder 'fx("sin(x)+x^2")'
./qoi_encoder 'fx("sqrt(x)+exp(-x)")'
```

需要 SymEngine + GMP（`cmake -DQOI_ENABLE_FX=ON`，或在检测到 SymEngine 时自动启用）。

---

## Regional QoI（~ 翻转标记）

Regional QoI 使用 `conf.qoi < 0` 判断。编号为独立 ID，通过 `~` 翻转存入 `qoi`：

| Regional ID | `conf.qoi` | QoI 类 | 约束 | 路径 |
|-------------|-----------|--------|------|------|
| 0 | `~0` | RegionalMean | \|mean(orig) - mean(dec)\| ≤ qEB | blockwise |
| 1 | `~1` | RegionalMeanSq | \|mean(orig²) - mean(dec²)\| ≤ qEB | blockwise |
| 2 | `~2` | RegionalAvgInterp | 同 RegionalMean | interp |
| 3 | `~3` | RegionalMeanSqInterp | 同 RegionalMeanSq | interp |

Regional QoI 的 `qoiParams` 为空串（无额外参数）。约束由 `qEB` 控制。

解码时工厂先检查 `qoi < 0`，再对 `~qoi`（翻转回正数）做 switch dispatch。高位 nibble 此时为 `0xF`（翻转产生），但此非显式编码的模式标记，而是 `if (qoi < 0)` 分支在 dispatch 中优先于 nibble 检查。

---

## 高位 Nibble 速查

| 高位 nibble | 判断方式 | 模式 | qoiParams 格式 |
|-------------|----------|------|---------------|
| `0x0` | `(qoi>>28)&0xF == 0 && qoi >= 0` | 常规 nibble | base64(double[]) — 函数参数 |
| `0x6` | `(qoi>>28)&0xF == 6` | Isoline | base64(double[]) — 函数参数 + [min,max,cnt,meb]×N |
| `0x7` | `(qoi>>28)&0xF == 7` | FX | base64(binary) — 3 个 TinyExpr 字符串 |
| `0xF` | `qoi < 0` | Regional | 空串（无参数） |
