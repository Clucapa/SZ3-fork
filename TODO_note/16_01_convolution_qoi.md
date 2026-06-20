# 16_01: 卷积 QoI 技术设计

## 1. 问题定义

给定 1D 序列 x[0..N-1] 和卷积核 K = [k₀, k₁, ..., k_{w-1}]，定义滑动卷积：

```
c[i] = Σ_{j=0}^{w-1} K[j] · x[i + j - center]
# center = (w-1)/2  (中心对齐)
# 有效范围: center ≤ i < N - (w - center)
```

QoI 约束：对每个有效输出位置，压缩后的卷积值与原始卷积值之差 ≤ tol：

```
|c_dec[i] - c_orig[i]| ≤ ε_c[i]   (ε_c 可以是 tol 或更小)
```

---

## 2. 误差累积分析

原始数据点 x[t] 出现在 w 个卷积输出中：
- c[t-center]  ：权重 K[0]
- c[t-center+1]：权重 K[1]
- ...
- c[t+center]  ：权重 K[w-1]

当 x[t] 被压缩为 x_dec[t] 时，误差 e[t] = x_dec[t] - x_orig[t] 会同时传播到这 w 个卷积输出：

```
c_dec[i] - c_orig[i] = Σⱼ K[j] · (x_dec[i+j-center] - x_orig[i+j-center])
```

如果走标准逐点 QoI 路线（把卷积函数当作点态 QoI，`eb = tol / |K_center|`），有两个问题：

1. **历史误差锁死预算**：在压缩 x[t] 之前，卷积输出 c[t-1] 和 c[t-2] 的误差已经部分锁定了（取决于前面已压缩的点）。到了 x[t]，留给它的修正余地可能已经很小，导致 eb 极保守甚至为 0。

2. **前向误差在线无法预测**：x[t] 的误差会影响未来的 c[t+1]、c[t+2]，但这些位置的约束还未生效——当前只能基于过去做保守估计，无法利用 future point 的补偿能力。

结论：需要 **bias 机制**把误差在中点重新归零，而不是靠缩小 eb 逐个死撑。

---

## 3. Bias 机制

### 3.1 核心思想

不改变 x_orig，而是在压缩阶段给 x[t] 一个**目标偏移**：

```
target[t] = x_orig[t] + bias[t]
```

压缩器用 target[t] 而非 x_orig[t] 做量化，解码后的值接近 target[t]。约束检查仍然用 `|c_dec - c_orig|`（不是 `|c_dec - c_target|`）。

bias[t] 把当前点的允许误差窗口"推"到合适的中位，使得：
- 之前的卷积误差被对冲，不继续累积
- 预算被均匀分配给所有参与位置

### 3.2 公式（单核、3 点为例）

设 K = [a, 1, b]（center=1，中间权重归一化为 1）。

压缩到位置 t，需要涉及的卷积输出：
- at pos t-1: 权重 = b（包含 x[t] 作为右侧）
- at pos t:   权重 = 1（x[t] 是中心）
- at pos t+1: 权重 = a（包含 x[t] 作为左侧，但 x[t+1] 尚未压缩）

已落定的累积误差：

```
acc[t-1] = a·e[t-2] + 1·e[t-1] + b·(to be filled)
acc[t]   = a·e[t-1] + 1·(to be filled) + b·(future)
```

由于 x[t+1] 未知，c[t+1] 暂不参与限制（x[t] 在该位置的角色可以以后再被修正，因为 x[t+1] 的 bias 也能补偿 c[t+1]）。

所以仅考虑已被 x[t] 唯一决定的窗口：pos t-1 和 pos t。

```
约束 t-1: |acc[t-1] + b·e[t]| ≤ tol
约束 t:   |acc[t]   + 1·e[t]| ≤ tol
```

（注：acc 不含 x[t] 的贡献，acc[t-1] 只含 a·e[t-2] + 1·e[t-1]）

解出 e[t] 的范围：

```
Δₘᵢₙ = max( (-tol - acc[t-1])/b,  (-tol - acc[t])/1 )
Δₘₐₓ = min( ( tol - acc[t-1])/b,  ( tol - acc[t])/1 )
```

bias 取中点：

```
bias[t] = (Δₘₐₓ + Δₘᵢₙ) / 2
eb[t]   = min(Δₘₐₓ - bias[t] + ε, geb)
```

当 Δₘᵢₙ > Δₘₐₓ 时，无可行解，回退到 geb（标记当前窗口为不可维持）。

### 3.3 推广到任意 w

对于一般核 K[w]（center = w/2 向下取整），位置 t 参与的窗口为：
- p ∈ [t - w + 1 + center, t + center]
- 每个位置 p 对应的权重为 K[p - t + center]

只计入 x[t] 是窗口中最后一个未定元素的位置（即该窗口全部依赖均已压缩完毕的）：

```
valid_p = {p | max(pos in window) ≤ t}   // 该窗口所有点都已落定
```

对每个 p ∈ valid_p：

```
acc[p] = Σ_{j < t} K[j - p + center] · e[j]
Δₘₐₓ = min_{p}  (tol - acc[p]) / K[t - p + center]
Δₘᵢₙ = max_{p} (-tol - acc[p]) / K[t - p + center]
```

bias / eb 计算同上。

---

## 4. check_comply 语义

保持标准语义（验证卷积输出的合规性，不涉及 bias）：

```cpp
bool check_comply(T orig, T dec) const override {
    // 卷积核在当前位置对应的输出
    for (int p : involved_windows(pos)) {
        double conv_orig = compute_convolution(p, original_data   buffers);
        double conv_dec  = compute_convolution(p, decompressed_data buffers);
        if (fabs(conv_orig - conv_dec) > tol) return false;
    }
    return true;
}
```

但在 streaming compress 中，check_comply 只能验证已完成的窗口——future 窗口在压缩点的时候还没有数据，无法检查，依赖 bias 保证 budget。

---

## 5. 数据结构

### 5.1 QoI_Conv 状态

```cpp
template <class T, uint N>
class QoI_Conv : public concepts::QoIIf<T, N> {
    struct KernelConfig {
        std::vector<double> weights;  // w entries
        double tolerance;
        int center;                   // floor(w / 2)
    };
    std::vector<KernelConfig> kernels_;

    // Per-window accumulated error (ring buffer, width = max window)
    std::vector<double> acc_err_;     // size = num_valid_outputs
    size_t output_offset_ = 0;        // current position in convolution output space
    size_t data_offset_ = 0;          // current data position

    // Window manager sliding state
    struct Window {
        bool active;
        double acc;           // accumulator for this window
        int remaining;        // points remaining to reach closure
    };
    std::deque<Window> windows_;

    double tol_; // ?
    T geb_;

    // Bias and eb computation
    T compute_bias(T orig_data, size_t pos);
    T compute_eb(T orig_data, size_t pos);
};
```

### 5.2 EBProvider（bias 扩展）

扩展 EBProvider 接口，附带 bias：

```cpp
namespace concepts {
template <class T>
struct EBResult {
    T eb;
    T bias;
};

template <class T>
class EBProvider {
public:
    virtual EBResult<T> advance_with_bias(T orig, T dec) = 0;
    // ... existing api
};
}
```

如果不想改基类，也可以新增一个独立接口：

```cpp
class QBiasProvider : public EBProvider<T> {
public:
    virtual T get_bias(T orig) = 0;  // 在 advance 之前调用
};
```

**推荐**：不改基类，在 compressor 的 quantize 点单独调用：

```cpp
T bias = qoi->get_bias(orig_val);
T target = orig_val + bias;
T eb = qoi_provider->advance(orig_val, dec_val);  // 使用 biased target
qnt.quantize_and_overwrite(target, pred, eb);
// dec_val 现在接近 orig + bias，但 check_comply 用 orig vs dec 验证卷积约束
```

---

## 6. Encoder

### 6.1 编码

高位 nibble = 4：
```
qoi = 0x40000000 | (num_kernels << 24) | (core_qoi_nibble & 0xFFFFFF)
```

qoiParams（base64）：
```
[4B: num_kernels (uint32)]
for each kernel k:
    [4B: window_width (uint32)]
    [w × 8B: weights (double)]
    [8B: tolerance (double)]
```

core_qoi_nibble 定义子 QoI（对数据进行变换后再做卷积），默认 0（identity / XLin）。

### 6.2 CLI

```bash
./qoi_encoder --conv-kernel "0.25,0.5,0.25" --conv-tol 0.001 "data_expression"

# 多核（如同时保持平滑和二阶差分）
./qoi_encoder \
  --conv-kernel "1,-2,1" --conv-tol 0.01 \
  --conv-kernel "0.25,0.5,0.25" --conv-tol 0.1 \
  "sqr"
```

---

## 7. 与其他模式的互动

| 组合 | 行为 |
|------|------|
| 卷积 + Regional | 先做卷积 QoI（点态 bias），再做 regional 聚合约束。需要双层预算。 |
| 卷积 + Isoline | 对卷积输出而非原始数据做等值线检测 |
| 卷积 + MultiQoI | 如支持多核，内部已是 MultiQoI 语义（每个核独立 tolerance） |

---

## 8. 实现顺序

1. **QoI_Conv 类**（单核、3 点硬编码）——验证 bias 机制是否可行
2. **generalize 到任意 w 和任意核**——kernels_ 容器
3. **多核支持**——vector<KernelConfig>，取最紧 eb
4. **EBProvider 对接**——确认 compressor 调用方式
5. **Encoder**——encode.hpp 新增 conv_encode()
6. **Test**——gaussian smoothing + laplacian 两条典型核的约束验证

---

## 9. 风险与边界情况

- **bias 可能导致 x_dec 偏离 x_orig 过大**（conv 约束满足但绝对值偏差大）→ 加一个 bias 上限 `|bias| ≤ geb` 或更小的 bound
- **窗口边界**（数据首尾）没有完整卷积输出 → 边界点的 bias = 0, eb = geb
- **多核冲突**（一个核要往正偏，另一个核要往负偏）→ 取交集，intersection 为空时退化为 geb
