# 1402: Isoline module — mode 6

## Current encoding hierarchy (after cleanup)

| qoi 条件                      | 模式       |
|-------------------------------|-----------|
| `bits[28:31] == 0x7`          | FX (`QoI_FX`) |
| `qoi < 0` (bit31=1)           | Regional   |
| `bits[28:31] != 0` (且 ≠7)    | **预留**，返回 nullptr |
| `bits[28:31] == 0`, `qoi ≥ 0` | 常规 nibble 算式 |

Mode 6（高 nibble = `0x6`）填补第一个预留位置，成为 Isoline 模式。

---

## Mode 6 qoi 格式

与 mode 0（常规算式）完全相同的 nibble 布局，但语义不同：不再是为了保持 `|f(orig)-f(dec)| ≤ τ`，而是为了防止 `f(数据)` 穿越等值线。

| qoi 值 | 含义 |
|--------|------|
| `0x60000000` | 保持 x 本身不穿越等值线（退化为 QoZ 的 Isoline） |
| `0x60000001` | 保持 x² 值不穿越等值线 |
| `0x60000012` | 保持 `XCubic + X2` 的和不穿越等值线（同组加法） |
| `0x6000014E` | 保持 `Compose(Exp, X2)` 即 `e^(x²)` 值不穿越等值线 |
| `0x600001F3` | 同时保持 `X2 AND XSqrt` 各自不穿越等值线（1个F=2组） |
| `0x600001F3F7`| 同时保持 `X2 AND XSqrt AND XRecip` 不穿越等值线（2个F=3组） |

### 算子规则

- **同组加法** `+`：多个函数 nibble 在同一组内 → 保持它们的**和**不穿越等值线
- **Compose** `0xE`：保持 `f(g(x))` 值不穿越等值线
- **MultiQoI** `F`：有 k 个 F 就有 k+1 个 QoI 组，**各组分别**保障自己的等值线约束

---

## qoiParams 布局

```
[函数参数...][组1: min, max, count, meb][组2: min, max, count, meb]...
```

### 第一部分：函数参数

与 mode 0 完全相同的格式。nibble 解析器按遍历顺序消费参数（XLin 读 A,B；XExp 读 base；etc.）。

### 第二部分：每组 4 个等值线参数（每组一包）

| 参数  | 类型     | 含义 |
|-------|----------|------|
| `min`   | double | 数据下界，用于自动生成等值线 |
| `max`   | double | 数据上界，用于自动生成等值线 |
| `count` | double→int | 等值线数量（与 QoZ 的 `qoiIsoNum` 一致） |
| `meb`   | double | minimum error bound（见下方） |

**等值线生成公式**（与 QoZ 一致）：
```
isovalues[i] = min + (i + 1) * (max - min) / (count + 1)
```
生成 `count` 条等值线，均匀分布在 `(min, max)` 开区间内，不包含端点。

**meb**：每个点的 eb **下限**（floor）。即使离等值线很远（`isoline_eb` 很大），eb 也不会小于 meb。公式：
```
final_eb = max(meb, min(geb, inner_eb, isoline_eb))
```

如果不想要 meb 约束，设 `meb = 0`。

### 示例 param 编码

`0x6000014E`（`Compose(Exp, X2)` = `e^(x²)`，单组）：

```
qoiParams = base64([
    // 函数参数: Exp 的 base
    2.718281828459045,
    // 等值线参数
    -5.0,    // min
    5.0,     // max
    10.0,    // count = 10 条等值线
    0.01     // meb = 0.01
])
```

`0x600001F3`（`X2 AND XSqrt`，2 组）：

```
qoiParams = base64([
    // 函数参数: 无（X2 和 XSqrt 都没有参数）
    // 第1组: X2 的等值线
    0.0,     // min
    100.0,   // max
    5.0,     // count = 5
    0.001,   // meb
    // 第2组: XSqrt 的等值线
    0.0,     // min
    10.0,    // max
    3.0,     // count = 3
    0.0001   // meb
])
```

---

## 实现

### 1. `include/SZ3/qoi/QoI_IsolineNibble.hpp`（新文件）

```cpp
template<class T, uint N>
class QoI_IsolineNibble : public concepts::QoIIf<T, N> {
public:
    // 构造: sub_qoi 是 nibble 解析出的下层 QoI
    // config 包含 min/max/count/meb
    QoI_IsolineNibble(double tol, T geb,
                      std::shared_ptr<concepts::QoIIf<T, N>> sub_qoi,
                      IsolineConfig config);
```

#### `pre_compute(const T *data)`

扫描数据找实际 min/max 并生成等值线。或者直接用 config 里的 min/max 生成：

```cpp
void pre_compute(const T *data) override {
    double range = config_.max - config_.min;
    isovalues_.reserve(config_.count);
    for (int i = 0; i < config_.count; i++)
        isovalues_.push_back(config_.min + (i + 1) * range / (config_.count + 1));
    std::sort(isovalues_.begin(), isovalues_.end());
}
```

**注意**：如果 `max < min`（或 range=0），跳过等值线生成，退化为无约束。

#### `interpret_eb(T data)`

```cpp
T interpret_eb(T data) const override {
    // 1) 下层 QoI 的 eb
    T inner_eb = sub_qoi_ ? sub_qoi_->interpret_eb(data) : geb_;

    // 2) 等值线 eb: 不让 f(data) 穿越 nearest_iso
    T isoline_eb = std::numeric_limits<T>::max();
    if (!isovalues_.empty()) {
        double nearest = nearest_isovalue(sub_qoi_ ? sub_qoi_->eval(data) : data);
        if (nearest < std::numeric_limits<double>::max()) {
            double deriv = approx_derivative(data);
            if (deriv > 1e-15)
                isoline_eb = nearest / deriv;
        }
    }

    // 3) 合并: max(meb, min(geb, inner_eb, isoline_eb))
    T eb = std::min(geb_, std::min(inner_eb, static_cast<T>(isoline_eb)));
    return std::max(static_cast<T>(config_.meb), eb);
}
```

- `nearest_isovalue(val)`：在 isovalues 中找 `|eval(data) - iso|` 的最小值，返回距离（若没有等值线返回极大值）
- `approx_derivative(x)`: `(eval(x+h) - eval(x-h)) / (2h)`，h=max(1e-8, 1e-8·|x|)
- 如果没有 sub_qoi（f(x)=x），`eval` 返回 `x`，`deriv` = 1

#### `check_comply(T orig, T dec)`

```cpp
bool check_comply(T orig, T dec) const override {
    // 下层 QoI 的 check_comply
    if (sub_qoi_ && !sub_qoi_->check_comply(orig, dec))
        return false;
    // 等值线穿越检测：比较 f(orig) 和 f(dec) 在两边的符号
    double vorig = sub_qoi_ ? sub_qoi_->eval(orig) : orig;
    double vdec  = sub_qoi_ ? sub_qoi_->eval(dec)  : dec;
    for (double iso : isovalues_) {
        if ((vorig - iso) * (vdec - iso) < 0)
            return false;
    }
    return true;
}
```

#### `eval(T val)`

```cpp
double eval(T val) const override {
    return sub_qoi_ ? sub_qoi_->eval(val) : static_cast<double>(val);
}
```

#### `create_eb_provider`

```cpp
std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(const Config &conf) override {
    // 使用 PointwiseEBProvider 风格，但 eb 通过 interpret_eb 实时计算
    // 因为等值线 eb 与数据值相关，不能预计算
    return std::make_unique<PointwiseEBProvider<T>>(this, conf.num);
}
```

**注意**：等值线的 `interpret_eb` 是数据值相关的（依赖 `|f(data) - nearest_iso|`），不能在压缩前预计算。所以 EBProvider 必须是在线（per-element）的，不能提前算好 `ebs[]`。需要一个新的 `OnlineEBProvider` 类，在 `advance(orig, dec)` 时调用 `qoi->interpret_eb(orig)`。

在线 EBProvider：

```cpp
template <typename T>
class OnlineEBProvider : public concepts::EBProvider<T> {
public:
    OnlineEBProvider(const concepts::QoIIf<T, ...> *qoi, size_t num)
        : qoi_(qoi), num_(num), pos_(0) {}

    T advance(T orig, T dec) override {
        T eb = qoi_->interpret_eb(orig);
        pos_++;
        return eb;
    }
    void advance() override { pos_++; }
    void precompress_block(size_t n) override { num_ = n; pos_ = 0; }
    void postcompress_block() override {}
    bool has_next() const override { return pos_ < num_; }
    void save(uchar *&c) const override {}
    void load(const uchar *&c, size_t &) override {}

private:
    const concepts::QoIIfInterface<T, ...> *qoi_;
    size_t num_, pos_;
};
```

或者更简单地，让 `is_pointwise()` 返回 `true`，让 `create_eb_provider` 创建 `PointwiseEBProvider`，但 `interpret_eb` 改为在线计算。但 `PointwiseEBProvider` 当前在 `precompress_block` 里预计算所有 eb 并缓存。对于 isoline 不行，因为 eb 依赖数据值，数据在压缩时不断被 `quantize_and_overwrite` 修改。

所以需要新的 `OnlineEBProvider`，或者修改 `PointwiseEBProvider` 支持在线模式。推荐新写一个简单的 `OnlineEBProvider`。

#### `is_pointwise()` / 其他接口

```cpp
bool is_pointwise() const override { return true; }  // pointwise eb
int id = 0xD;  // nibble id (but mode 6 wraps it)
```

### 2. `include/SZ3/qoi/QoIIf.hpp` — GetQOI 加 mode 6 分支

```cpp
// Isoline mode: high nibble = 0x6
if (((conf.qoi >> 28) & 0xF) == 6) {
    return detail::assemble_isoline_nibble<T, N>(conf);
}
```

`assemble_isoline_nibble`:

```cpp
template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> assemble_isoline_nibble(const Config &conf) {
    // 屏蔽高 nibble，只取低 28bit 的 nibble 表达式
    int nibble_qoi = conf.qoi & 0x0FFFFFFF;
    auto groups = parse_qoi_nibbles(nibble_qoi);
    if (groups.empty()) {
        // 退化为保持 x 本身
        groups.push_back(QoIGroup{{}});  // 空组 → XLin
    }

    auto decoded = base64_decode(conf.qoiParams);
    ParamReader params(decoded);

    // 先创建 nibble 子 QoI（消费函数参数）
    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> sub_qois;
    for (auto &g : groups) {
        auto q = assemble_group<T, N>(g, params, conf.qEB, conf.absErrorBound);
        if (!q) q = std::make_shared<QoI_XLin<T, N>>(conf.qEB, conf.absErrorBound);
        sub_qois.push_back(std::move(q));
    }

    // 然后读取每组的等值线配置（消费剩余参数）
    auto read_iso_config = [&]() -> IsolineConfig {
        double min_v = params.read();
        double max_v = params.read();
        double cnt_d = params.read();
        int count = static_cast<int>(cnt_d);
        double meb = params.read();
        return {min_v, max_v, count, meb};
    };

    // 如果有 k 个 F 就有 k+1 组，但 groups 已经按 F 分割好了
    // groups.size() = sub_qois.size() = 组数
    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> isoline_qois;
    for (size_t i = 0; i < sub_qois.size(); i++) {
        auto cfg = read_iso_config();
        auto isoline_qoi = std::make_shared<QoI_IsolineNibble<T, N>>(
            conf.qEB, conf.absErrorBound, sub_qois[i], cfg);
        isoline_qois.push_back(std::move(isoline_qoi));
    }

    if (isoline_qois.size() == 1)
        return isoline_qois[0];

    return std::make_shared<QoI_MultiQoI<T, N>>(std::move(isoline_qois), conf.qEB, conf.absErrorBound);
}
```

### 3. `tools/qoi_encoder/encode.hpp`

Encoder 端不需要大改。mode 6 的表达式语法与 mode 0 完全相同，只是 encoder 输出高 nibble=6：

```
# mode 0: 常规 QoI（保持 |f(orig)-f(dec)| ≤ τ）
qoi_encoder "sqr+abs"

# mode 6: 等值线保持（防止 f(data) 穿越等值线）
qoi_encoder "iso6(sqr+abs, -10, 10, 5, 0.01)"
```

`iso6(expr, min, max, count, meb)` 语法：
- 第一参数：nibble 表达式（同 mode 0）
- 后四个参数：等值线配置

Encoder 需要：
1. 提取 `iso6(...)` 中的 nibble 表达式，按 mode 0 方式编码
2. 将等值线参数追加到 qoiParams
3. qoi 高 nibble 设为 0x6

对于多组（MultiQoI）：

```
iso6(sqr, -10, 10, 5, 0.01; abs, -5, 5, 3, 0.001)
```

用 `;` 分隔各组，每组有自己的 nibble 表达式和等值线配置。

### 4. `include/SZ3/qoi/EBProvider.hpp` — 新增 OnlineEBProvider

```cpp
template <typename T>
class OnlineEBProvider : public concepts::EBProvider<T> {
public:
    OnlineEBProvider(const concepts::QoIIf<T, uint(1)> *qoi, size_t num)
        : qoi_(qoi), num_(num), pos_(0) {}

    T advance(T orig, T dec) override {
        pos_++;
        return qoi_->interpret_eb(orig);
    }

    void advance() override { pos_++; }

    void precompress_block(size_t n) override {
        num_ = n;
        pos_ = 0;
    }

    void postcompress_block() override {}

    bool has_next() const override { return pos_ < num_; }

    void save(uchar *&c) const override {}
    void load(const uchar *&c, size_t &) override {}

private:
    const concepts::QoIIf<T, uint(1)> *qoi_;
    size_t num_, pos_;
};
```

需要改 `EBProvider.hpp` 加 `has_next()` 虚方法（或直接用 `pos_ < num_` 判断）。

### 5. `test/test_config.hpp`

```cpp
// Mode 6 isoline
{0x60000000, "Iso6_XLin",   DOM_UNRESTRICTED, false, 1.0, 10.0, h_xlin,     nullptr, 0, 0, "iso6(lin, -5, 5, 5, 0.01)"},
{0x60000001, "Iso6_X2",     DOM_UNRESTRICTED, false, 1.0, 10.0, h_x2,       nullptr, 0, 0, "iso6(sqr, -5, 5, 5, 0.01)"},
// MultiQoI isoline
{0x600001F3, "Iso6_Multi",  DOM_UNRESTRICTED, false, 1.0, 10.0, h_multi_sqrt, h_multi_x2, 1.0, 0, "iso6(sqr, -5, 5, 5, 0.01; sqrt, 0, 10, 3, 0.001)"},
```

---

## 文件清单

| 文件 | 操作 |
|------|------|
| `include/SZ3/qoi/QoI_IsolineNibble.hpp` | 新建 — mode 6 QoI + IsolineConfig |
| `include/SZ3/qoi/QoIIf.hpp` | 修改 — GetQOI 加 mode 6 分支, `assemble_isoline_nibble` |
| `include/SZ3/qoi/EBProvider.hpp` | 修改 — 加 `OnlineEBProvider` |
| `tools/qoi_encoder/encode.hpp` | 修改 — 加 `iso6()` 语法解析 |
| `tools/qoi_encoder/main.cpp` | 可选 — 更新 CLI 帮助 |
| `test/test_config.hpp` | 修改 — 加 mode 6 测试条目 |
| `test/encoder_tests.hpp` | 修改 — 加 encoder 往返测试 |

---

## 测试策略

1. **Raw isoline** (`0x60000000`): f(x)=x，等值线穿越检测等价于 QoZ
2. **QoI-aware isoline** (`0x60000001`): f(x)=x²，验证 `interpret_eb` 按 `|f'(x)|` 缩放
3. **Sum isline** (`0x60000012`): f(x)=x³+x²，验证复合和值不穿越
4. **MultiQoI isoline** (`0x600001F3`): 两组各自检测等值线穿越
5. **Force-lossless**: 无法避免穿越时走 lossless fallback
6. **Meb 测试**: meb=0.1 时远处点的 eb 不低于 0.1
7. **Negatives**: 空等值线（count=0）、min≥max、NaN/Inf 输入

---

## 注意点

- **Derivative 精度**: 数值微分 `(f(x+h)-f(x-h))/(2h)` 对大部分 QoI 足够，但对 `XRecip`（1/x）在 x≈0 附近发散，fallback 到 geb
- **OnlineEBProvider**: 与现有 `PointwiseEBProvider` 不同，不在 `precompress_block` 预计算，每次 `advance` 在线调用 `interpret_eb`
- **`has_next()`**: EBProvider 接口需要加 `virtual bool has_next() const`（或子类直接比较 pos）
- **Encoder 分隔符**: `iso6(...; ...)` 用 `;` 分隔各组，与 mode 0 的 `|` 对应
- **Compose 等值线**: `iso6(exp@sqr, ...)` 保持 `e^(x²)` 值，derivative 来自 `d/dx e^(x²) = 2x·e^(x²)` 数值近似
- **qoiParams 长度不确定性**: 函数参数数量由 nibble 决定，等值线参数数量由 groups 数决定。`assemble_isoline_nibble` 先创建子 QoI 消费函数参数，然后按 groups 数等量读取等值线参数
