# Regional Mean of Square（区域均方值）

## 约束定义

```
| (1/n) · Σ(dec_i² - orig_i²) | ≤ τ
⇒ | Σ(dec_i² - orig_i²) | ≤ τ · n
```

即解压后整个块的均方值与原始块的均方值之差不超过 τ。

## 算法原理

QoZ 采用**域转换 + 预算跟踪**两步法：

### Step 1: 平方域预算均分

将原始约束 `|Σ(dec² - orig²)| ≤ τ·n` 分解到各点，每点在平方域的 eb 为：

```
eb_sq = (aggregated_tolerance - |error|) / rest_elements

aggregated_tolerance = τ · n
error = Σ(orig² - dec²)   (已量化点的累积平方误差)
```

### Step 2: f⁻¹ 反解回原始域

平方域 eb (`eb_sq`) 是对 `dec²` 的容差。要在原始域（x 空间）施加对应的 eb，从 x² 的约束公式反解：

```
给定:  |(orig ± eb)² - orig²| ≤ eb_sq
   ⇒  eb = -|orig| + sqrt(orig² + eb_sq)
```

这正是 QoI_X2 的 `interpret_eb` 公式，但 `τ` 被替换为 `eb_sq`（在线预算分配的剩余平方容差）。

### 与 QoI_X2 的关系

```
QoI_X2:                τ 固定 → eb = -|x| + sqrt(x² + τ)
RegionalMeanSq:  eb_sq = 剩余预算/剩余点数 → eb = -|x| + sqrt(x² + eb_sq)
```

RegionalMeanSq 本质是 QoI_X2 的**自适应 τ 版本**——每点的 eb 取决于块内已用预算。

---

## QoI_RegionalMeanSq

对应 `qoi=11`。

### 构造参数

```
tol (double) — τ, 每元素均方容差
geb (T)     — 全局 eb 上限
```

### 内部状态

```cpp
double aggregated_tolerance;   // 总预算 = tol × n
double error;                   // 累计平方误差 Σ(orig² - dec²)
int rest_elements;              // 块内剩余未处理元素数
int block_elements;             // 块内总元素数
```

### precompress_block

```cpp
void precompress_block(size_t num_elements) override {
    rest_elements = num_elements;
    block_elements = num_elements;
    aggregated_tolerance = tol * num_elements;
    error = 0;
}
```

### interpret_eb

```cpp
T interpret_eb(T x) const override {
    double eb_sq = (aggregated_tolerance - fabs(error)) / rest_elements;
    T eb = -fabs(x) + sqrt(x * x + eb_sq);
    return std::min(eb, geb);
}
```

### update_tolerance

```cpp
void update_tolerance(T orig, T dec) override {
    error += orig * orig - dec * dec;
    rest_elements--;
}
```

### check_comply

恒 true。

### postcompress_block

可选择块结束时验证：

```cpp
void postcompress_block() override {
    // assert(fabs(error) <= aggregated_tolerance + 1e-10);
}
```

---

## RegionalMeanSqEBProvider

与 `RegionalMeanEBProvider` 结构相同，但内部绑定 `QoI_RegionalMeanSq`。

```cpp
template <typename T, uint N>
class RegionalMeanSqEBProvider : public EBProvider<T> {
public:
    RegionalMeanSqEBProvider(QoI_RegionalMeanSq<T, N> *qoi)
        : qoi(qoi), pos(0) {}

    void precompress_block(size_t n) override {
        qoi->precompress_block(n);
        pos = 0;
    }

    T advance(T orig, T dec) override {
        T eb = qoi->interpret_eb(orig);
        qoi->update_tolerance(orig, dec);
        pos++;
        return eb;
    }

    void advance() override { pos++; }

    void postcompress_block() override { qoi->postcompress_block(); }

    void reset() override {
        qoi->precompress_block(0);
        pos = 0;
    }

    void save(uchar *&c) const override {
        write(qoi->get_geb(), c);
        write(/* tol */, c);
    }

    void load(const uchar *&c, size_t &rl) override {
        T geb; read(geb, c, rl);
        double tol; read(tol, c, rl);
        qoi->set_geb(geb);
        qoi->set_tol(tol);
    }

private:
    QoI_RegionalMeanSq<T, N> *qoi;
    size_t pos;
};
```

save/load 只传 τ, geb，不传 ebs[] 数组。

---

## 与 QoZ 的差异对照

| 项 | QoZ `RegionalAverageOfSquare` | 本设计 |
|---|---|---|
| 域转换 | `eb_sq = (agg_tol - \|error\|) / rest`，`eb = -\|x\|+√(x²+eb_sq)` | 相同 |
| 前半段 *2 | 无（与 RegionalAverage 不同） | 沿用 QoZ，无 *2 |
| error 域 | 平方域 `error += orig² - dec²` | 相同 |
| check_compliance | 恒 true | 相同 |
| Interp 变体 | `RegionalAverageOfSquareInterp` 用 `compute_block_id(offset)` 维护多维块 ID 映射 | 本 v1 暂不实现，后续可按需扩展 |
| save/load | 通过 QoI 旧接口 | 通过 EBProvider，只传 τ, geb |

---

## 与 PointwiseEBProvider 的对称性

```
                 ┌─ PointwiseEBProvider ──┐    ┌─ RegionalEBProvider ──┐
advance(orig,dec)│ return ebs[pos++]      │    │ qoi->interpret_eb(orig)│
                 │                        │    │ qoi->update_tolerance  │
precompress_block│ no-op                  │    │ qoi->precompress_block │
save             │ write(ebs[])           │    │ write(τ, geb)          │
                 └────────────────────────┘    └───────────────────────┘
                   与标准 SZ3 ebs[] 兼容         压缩数据更小 (2 scalars)
```

两种 Provider 在 `QpetBlockDecomp` 视角完全等价——都是调用 `precompress_block()` → 循环 `advance()` → `postcompress_block()`。
