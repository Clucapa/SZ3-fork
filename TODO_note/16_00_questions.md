# 16_00: 卷积 QoI 关键问题

给定 1D 数据 x[0..n-1] 和一个固定小卷积核 K[w]（窗口宽度 w，如 3 或 5），要求保证滑动卷积输出在每个位置的 QoI 约束：

```
c[i] = Σⱼ K[j] · x[i + j - offset]

约束: |c_orig[i] - c_dec[i]| ≤ tol   ∀ valid i
```

---

## Q1. 点态约束 vs 区域聚合约束？

**选项 A：点态（per-output-position）**

每个卷积输出位置独立约束。但一个数据点参与 w 个卷积窗口——压缩 x[i] 时，涉及它的 w 个约束已经有部分被 prior 误差"污染"了。

- 好处：约束语义精确，逐位置可验证
- 坏处：误差传播不可避免——x[i-1] 的误差会泄漏到 c[i-1], c[i], c[i+1]…，累积后越来越大的偏移需要用更多 bit 来弥补

**选项 B：区域聚合（每 block 一个聚合约束）**

```
| Σᵢⱼ K[j] · x_orig[i+j] - Σᵢⱼ K[j] · x_dec[i+j] | ≤ tol · N
```

- 好处：每个 block 内独立结算，误差不跨 block 泄漏
- 坏处：只能保证总量，无法保证单个位置的卷积值偏差不大

**推荐：选项 A（点态）+ bias offset 抵消误差**。见 Q3。

---

## Q2. 如何在压缩器中施加 bias？

bias 的本质：压缩 x[i] 时，目标值不是 `x_orig[i]`，而是 `x_orig[i] + bias[i]`。bias[i] 由之前已经落定的卷积误差算出来，用来"对冲"即将发生的偏差。

三种实施位置：

| 位置 | 做法 | 优劣 |
|------|------|------|
| **EBProvider / QoI** | `interpret_eb(orig)` 返回 `(eb, bias)`，compressor 用 `x_orig + bias` 代替 `x_orig` 做量化 | QoI 接口需扩展返回类型，compressor 需配合改；cleanest |
| **Pre-processing** | 压缩前扫描一遍数据，在 data copy 上加 bias | 不用改 compressor，但 bias 无法响应实际量化误差 |
| **Compressor 层** | QpetDecomp 内部在量化前做一次偏移 | 侵入 compressor 实现，不够通用 |

**推荐：扩展 EBProvider/QoI 接口**。`interpret_eb` 返回 `struct {T eb; T bias;}`（或新增虚函数 `get_bias(x)`），compressor 处改动极小。

---

## Q3. bias 公式怎么算？如何避免波动/振荡？

**问题模型**（3 点核 [k₀, k₁, k₂] 为例，offset=1 中心对齐）：

当压缩到第 i 个点时：
- 前面已经落定的点：[0, i-1] 的 `x_dec` 已确定
- 已涉及 i 的卷积位置：p = i-2, i-1, i（各包含 x[i]）

对每个已可见的卷积位置 p，已经积累的误差：

```
acc_err[p] = Σ_{j already decoded} K[j-p+1] · x_dec[j] - Σ_{j} K[j-p+1] · x_orig[j]
```

未定部分只差 x[i] 的贡献：
```
pending[p] = K[i-p+1] · (x_dec[i] - x_orig[i])
```

我们希望 `|acc_err[p] + pending[p]| ≤ tol`，即：

```
-tol - acc_err[p] ≤  K_weight · (x_dec[i] - x_orig[i])  ≤  tol - acc_err[p]
```

这说明 **x[i] 的允许误差范围被多个卷积位置同时约束**，取最紧的：

```
Δ_min = max_{p} (-tol - acc_err[p]) / K_weight
Δ_max = min_{p} (tol - acc_err[p]) / K_weight
```

**bias 策略**：取中点：

```
bias[i] = -(Δ_min + Δ_max) / 2   （偏移回原点附近）
eb[i]   = min(Δ_max - bias[i], geb)   （对称压缩窗口）
```

**为什么不会振荡**：bias 不是"对上一次误差的简单反方向补偿"（那样会导致过冲—欠冲—过冲…），而是基于**剩余 budget** 算出的最优偏移位置。当 budget 充裕时 bias≈0，budget 紧张时才偏。

---

## Q4. 编码格式

需要新增模式还是扩展现有模式？

**提议：高位 nibble = 4 或 5，表示卷积模式**。

qoi 值：
```
conv_qoi = 0x40000000 | (window_width << 16) | (nibble_qoi)
```
- bit 31-28: 0x4 = 卷积模式
- bit 27-16: window_width（卷积核宽度，例如 3）
- bit 15-0: 子 QoI 的 nibble 编码（可选，默认为 XLin）

qoiParams 格式（base64）：
```
[kernel_count: uint32]
[kernel_weights: w × double]  (第一个核)
[kernel_weights: w × double]  (第二个核，如果有)
...
[tol0, tol1, ...: double] (每组 tolerance)
```

每核有独立的 tolerance，多核间取交集（MultiQoI 语义）。

### Encoder 语法

```bash
./qoi_encoder --conv-kernel "0.25,0.5,0.25" --conv-tol 0.001 "data"
./qoi_encoder --conv-kernel "1,-2,1" --conv-tol 0.01 "lin(2,0)"
```

### Decoder 构造

在 QoIIf.hpp 增加 mode 4 分支：
```cpp
if ((qoi>>28)==4) {
    ConvConfig cfg = parse_conv_config(conf.qoiParams);
    // 或根据 qoi 的低位选用子 QoI
    return make_shared<QoI_ConvNibble>(conf, cfg);
}
```

---

## Q5. 多核支持？

是否支持同时维持多个不同核的卷积约束？

**建议**：支持。类似 MultiQoI，多核约束取交集（取最紧 eb）。qoiParams 内多个 kernel 数组依次排列，每核后跟一个 tolerance。

---

## Q6. 核的对称性和奇偶性

- 中心对称核（如 [0.25, 0.5, 0.25]）：前向和后向对称，bias 计算更简单
- 非对称核（如 [0.2, 0.5, 0.3]）：前向/后向权重不同，但 bias 公式不变

不需要区分。

---

## 决策摘要

| 问题 | 选项 | 理由 |
|------|------|------|
| 约束粒数 | 点态 + bias | 比 block 聚合更精确 |
| bias 施加处 | EBProvider 扩展 | 不改 compressor 核心 |
| bias 公式 | residual budget midpoint | 避免振荡 |
| 编码模式 | 高位 nibble 4 | 与其他模式不冲突 |
| 多核 | 支持 | 和 MultiQoI 一致 |
