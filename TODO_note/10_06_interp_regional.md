# Interp 预测器 Regional QoI 适配

## 动机

当前 QPET 只走 blockwise 路径（`QpetBlockDecomp`，Lorenzo/Regression/Composed 预测器）。SZ3 另有 `InterpolationDecomposition`——用线性/三次样条插值做分层预测的独立路径——QoZ 上有对应的 `RegionalAverageInterp` 和 `RegionalAverageOfSquareInterp`。需要为 Interp 路径也接入 QPET 量化器 + EBProvider。

## Interp vs Blockwise 差异

| | Blockwise (QpetBlockDecomp) | Interp (InterpolationDecomposition) |
|---|---|---|
| 预测方式 | Lorenzo/Regression 邻域公式 | 线性或三次样条插值 |
| 遍历方式 | 逐块 block iteration | 多层分辨率逐级加密（stride 减半） |
| eb 控制 | QoI 级 `interpret_eb` 逐点推导 | **Decomposition 级**：每层 `eb * eb_ratio` 或 `eb / pow(eb_alpha, level)` |
| 锚点 | 无 | 可选的 anchor grid（隔 stride 无损存一个点） |
| QoZ 对应 | `RegionalAverage` / `RegionalAverageOfSquare` | `RegionalAverageInterp` / `RegionalAverageOfSquareInterp` |

两者是**完全不同的 decomposition 类**，不是给 `QpetBlockDecomp` 换个预测器就能解决的。

## 设计决策

### 照搬 QoZ：QoI 层不加 budget tracking

QoZ 的 `RegionalAverageInterp` 中：

```cpp
T interpret_eb(T data) const { return tolerance; }
void update_tolerance(T data, T dec_data) {}    // 空
```

eb 直接返回 tolerance，不做逐点预算分配。误差控制完全由 `InterpolationDecomposition` 的层间 eb 调整策略负责（`eb_ratio`、`eb_alpha`、`eb_beta`）。

**决策：照搬。** QoI 的 `interpret_eb` 对 Interp 路径返回 tolerance，不维护 budget。`EBProvider::advance()` 直接读 QoI 的 tolerance。

原因：
- QoZ 已验证可行，Interp 的层间 eb 控制与 QoI 级 budget tracking 是两套正交机制，叠加起来反而互相干扰
- Interp 遍历顺序不是线性逐点的（同一层内先遍历快维再慢维），`update_tolerance` 在该顺序下语义不明确
- 保持与 QoZ 一致，便于后续对照调试

## QpetInterpDecomp 设计

新建类 `QpetInterpDecomp<T, N>`，模板参数含 Qnt（QpetQnt）。**不是**给 `InterpolationDecomposition` 加模板参数，而是**照抄** `InterpolationDecomposition` 的主体逻辑，替换其中两处：

```
原 InterpolationDecomposition:
  quant_inds[quant_index++] = quantizer.quantize_and_overwrite(d, pred);   // 标准量化器
  d = quantizer.recover(pred, quant_inds[quant_index++]);                  // 标准恢复

改为 QpetInterpDecomp:
  T eb = eb_provider->advance(orig, dec);                                  // ① 获取 eb
  int qe = qnt.qnt_eb(eb);                                                 // ② 量化 eb
  int qd = qnt.qnt_overwrite(d, pred, eb);                                 // ③ 量化残差
  // 输出: [qe | qd]（两个 int 而非一个）
```

关键变化：
1. 每个点的输出从 1 个 `int`（原量化器：残差含 eb）变为 2 个 `int`（`qi_eb` + `qi_data`），与 `QpetBlockDecomp` 一致
2. 引入 `EBProvider`——对 Interp 路径，`advance()` 返回 tolerance 即可（不做 budget 更新）
3. 文件格式保持 `[qi_ebs | qi_datas]` 布局，与 blockwise 路径兼容

### 类结构

```cpp
template <class T, uint N, class Qnt>
class QpetInterpDecomp : public concepts::DecompositionInterface<T, int, N> {
public:
    QpetInterpDecomp(const Config &conf, Qnt qnt,
                     std::shared_ptr<concepts::QoIIf<T, N>> qoi);

    std::vector<int> compress(const Config &conf, T *data) override;
    T *decompress(const Config &conf, std::vector<int> &quant_inds, T *dec_data) override;
    void save(uchar *&c) override;
    void load(const uchar *&c, size_t &remaining_length) override;
};
```

### 与 QpetBlockDecomp 的对称性

```
               ┌─ QpetBlockDecomp ─────┐    ┌─ QpetInterpDecomp ──────────┐
eb_provider    │ PointwiseEBProvider /  │    │ InterpEBProvider (trivial)  │
               │ RegionalMeanProvider / │    │ 返回 tolerance 不做 budget  │
               │ RegionalMeanSqProvider │    │                             │
数据遍历        │ block_iter 逐块        │    │ interp_block 逐层 stride 减半 │
预测           │ Lorenzo/Regression      │    │ interp_linear / interp_cubic │
量化           │ qnt_eb + qnt_overwrite │    │ 同上（两步量化）              │
文件格式        │ [qi_ebs|qi_datas]      │    │ [qi_ebs|qi_datas]           │
锚点           │ 无                     │    │ 可选的 anchor grid           │
```

### EBProvider 对 Interp 路径的处理

对于 Interp 路径的 QoI（`RegionalAverageInterp` / `RegionalAverageOfSquareInterp`），其 `EBProvider` 的 `advance()` 只需调用 `interpret_eb()` 返回 tolerance。如果该 QoI 有 budget tracking（将来扩展），则在 `advance()` 中调用 `interpret_eb` + `update_tolerance`，与 blockwise 的 provider 行为一致。

但首版按照 QoZ 的照搬方案：
- `interpret_eb()` 返回 tolerance
- `update_tolerance()` 空实现
- `advance()` 只返回 tolerance

将来的扩展点：如果需要逐点 budget tracking，修改 EBProvider 的 `advance()` 即可，不需要改 `QpetInterpDecomp`。

### Interp 遍历中的 EBProvider 调用的时机

关键问题：Interp 遍历内部不是依次访问所有点——同一层内可能按方向交替遍历。EBProvider 的 `advance(orig, dec)` 和 `advance()` 的调用次序必须与实际量化次序**严格对应**，因为：
- 压缩端：`advance(orig, dec)` 决定当前点的 eb
- 解压端：`advance()` 对齐位置

由于 Interp 不维护预算（首版方案），`advance()` 只返回 tolerance + 推进计数器，只需保证两端的 `advance()` 被调用次数一致即可。不需要 `orig` / `dec` 的值。

```cpp
// Interp 遍历中量化每个点的位置（压缩端）
quantize_func = [&](size_t idx, T &d, T pred) {
    T orig = d;
    T eb = eb_provider->advance(orig, d);    // 返回 tolerance
    int qe = qnt.qnt_eb(eb);
    int qd = qnt.qnt_overwrite(d, pred, eb);
    // 写入 qebs, qds（两个 int）
};

// 解压端
quantize_func = [&](size_t idx, T &d, T pred) {
    int qe = qebs[quant_index];
    int qd = qds[quant_index];
    T eb = qnt.recv_eb(qe);
    d = qnt.recv(pred, qd, eb);
    eb_provider->advance();                    // 仅推进计数器
    quant_index++;
};
```

### anchor grid 处理

Interp 的 anchor grid 点需要额外处理——这些点是**无预测**直接存的（`force_save_unpred` / `recover_unpred`），不经过插值预测也不经过 QoI。当前 `QpetQnt` 继承了 `QpetQntIf` 接口，QoZ 对应使用的是 `QpetQnt` 的 `force_save` 和 `recover_unpred` 方法，当前 SZ3 的 `QpetQntIf` 没有这两个方法。

**方案：** 将 SZ3 中 Interp anchor grid 的 `force_save_unpred` / `recover_unpred` 逻辑复用，直接使用原 `SZDQnt` 或 `LinearQnt` 做无损保存/恢复。anchor 点不经过 `qnt_eb` / `qnt_overwrite`，因此不需要 eb。

### Config 扩展

需新增字段：

```cpp
int interpAlgo = 0;            // 0=linear, 1=cubic（已有，SZ3 Config 中）
int interpDirection = 0;       // 维度遍历方向（已有）
int interpAnchorStride = 0;    // 锚点步长（已有）
double interpAlpha = -1;       // eb alpha 参数（已有）
double interpBeta  = -1;       // eb beta 参数（已有）
double eb_ratio = 0.5;         // level-wise eb ratio（已有，InterpolationDecomposition 中常量）
```

SZ3 的 `Config` 中已有这些字段（用于标准的 `InterpolationDecomposition`）。`QpetInterpDecomp` 直接读取即可。

在 `QpetInterpDecomp` 中：
- anchor 点使用 `force_save_unpred` 无损保存
- 其余点走标准的两步量化 `qnt_eb + qnt_overwrite`
- 层间 eb 策略保持与 `InterpolationDecomposition` 一致

### 工厂选择逻辑

```cpp
// SZDispatcher 或 SZAlgoLorenzoReg 中
if (conf.interpAlgo >= 0) {
    // 走 Interp 路径
    auto qoi = GetQOI<T, N>(conf);
    return make_compressor_sz_generic<T, N>(
        QpetInterpDecomp<T, N, QpetQnt>(conf, qnt, qoi),
        encoder, lossless);
} else {
    // 走 blockwise 路径（现有逻辑）
    ...
}
```

### QoI 映射

Interp 路径的选择由 `conf.interpAlgo >= 0` 决定，不是由 `conf.qoi` 的 ID 值决定。QoI 的具体约束通过 nibble 编码（见 11_00）指定——Interp 的 QoI 实例只需要实现 `interpret_eb` 返回 tolerance 即可，不维护 budget。

但 Interp 有自己的专用 QoI 变体（对应 QoZ 的 `RegionalAverageInterp` / `RegionalAverageOfSquareInterp`），在 QoI 层的表现就是 `interpret_eb` 返回 tolerance、`update_tolerance` 为空。与 blockwise 路径的 `RegionalMean` / `RegionalMeanSq` 共享同一套 nibble 编码的函数组合逻辑，只是底层的 context 不同。

在工厂中根据预测器类型分派：

```cpp
if (conf.interpAlgo >= 0) {
    // Interp 路径：QoI 只做 tolerance 返回，不维护 budget
    auto qoi = GetQOI<T, N>(conf);       // 仍然走 nibble 编码解析
    // QpetInterpDecomp 内部用 qoi->interpret_eb 获取 tolerance
} else {
    // Blockwise 路径（现有逻辑）
    ...
}
```

Int qoi 的 regional 标记（最高位）对 Interp 仍有效——最高位置 1 时走块级 budget tracking（即使 Interp 的 QoI 不维护 budget，Decomposition 层可以通过其他机制控制）。

## 修改清单

| 文件 | 操作 | 依赖 |
|---|---|---|
| `qoi/QoI_RegionalAvgInterp.hpp` | **新建** | Layer 1（`interpret_eb`=tolerance, `update_tolerance`=空） |
| `qoi/QoI_RegionalMeanSqInterp.hpp` | **新建** | Layer 1 |
| `decomposition/QpetInterpDecomp.hpp` | **新建** | Layer 2（与 QpetBlockDecomp 同级，遍历逻辑照搬 InterpolationDecomposition） |
| `api/impl/SZAlgoLorenzoReg.hpp` | 修改 | 根据 `conf.interpAlgo` 选择 QpetInterpDecomp（与 nibble 编码无关） |

### 模块独立性

`QpetInterpDecomp` 只需要 `<cmath>`, `<cstring>`, `SZ3/def.hpp`, `SZ3/quantizer/QpetQnt.hpp`, `SZ3/qoi/EBProvider.hpp`, `SZ3/utils/Interpolators.hpp`。

## 测试计划

```
test_qpet_interp.cpp
  ├─ anchor 点无损往返
  ├─ interp 压缩/解压端到端（linear + cubic）
  ├─ qoi=3 的 blockwise vs interp 自动选择
  └─ 与 QoZ 的 RegionalAverageInterp 行为对比验证
```

## 风险

- Interp 路径的 eb 控制依赖 `eb_alpha/eb_beta` 调参，用户需要理解层级策略；与 blockwise 的 QoI 级 budget tracking 透明度有差距
- 解压端 Interp 预测的对称性要求高，任何整数量化误差会累积跨层
- 两套 decomposition 类的维护负担（需要同步修复 bug）
