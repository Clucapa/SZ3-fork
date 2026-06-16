# SZ3 QPET 插件实现说明

本仓库在 SZ3 v3.3.2 基础上，以独立模块化的方式实现了 QPET 插件。核心设计目标：**接口统一，对基底 SZ3 侵入最小，便于未来 SZ3 升级时同步移植**。

## 设计原则

- **新增功能放新文件，不改 SZ3 原生类**：所有 QoI 实现、EBProvider、组合逻辑都在 `qoi/` 下的独立头文件中，不修改 SZ3 的 Predictor、Quantizer、Encoder、Lossless 等原有模块。
- **通过虚接口消除硬编码 dispatch**：`QoIIf` 基类承担全部多态职责，外部代码不判断 `qoi->id`，不 static_cast 到具体类。
- **两类 Decomposition 共享同一套 QoI + EBProvider 接口**：`QpetBlockDecomp`（Lorenzo/Regression 预测）和 `QpetInterpDecomp`（插值预测）都通过 `qoi->create_eb_provider(conf)` 获取 eb 源，遍历和预测逻辑不同但量化路径一致。

## 工作流

```
原 SZ3:
  Config → [BlockwiseDecomp: Pred + LinearQnt(全局eb)] → quant_inds
                                                            ↓
                                     HuffmanEncoder → Lossless_zstd → 文件

现 QPET:
  Config → eb 预算 → [QpetBlockDecomp: Pred + QpetQnt(逐点eb)]
                         ├─ ① qnt_eb(eb) → qi_eb
                         ├─ ② qnt_overwrite(data,pred,eb) → qi_data
                         └─ ③ 拼接为 [qi_eb.. | qi_data..]
                                     ↓
                          HuffmanEncoder(统一) → Lossless_zstd → 文件
         └── 或 Interp 路径 ──→ [QpetInterpDecomp: 插值预测 + QpetQnt]
```

关键差异：
1. **量化前增加 eb 预算**：QoI 实例逐点计算误差界
2. **量化器改为 QpetQnt**：两步操作（先量化 eb 得 qi_eb，再用量化后的 eb' 量化残差得 qi_data），eb' ≤ 原始 eb（安全）

## QoI 接口体系

整个系统的核心是 `QoIIf<T,N>` 抽象基类（`include/SZ3/qoi/QoI.hpp`）。它定义了统一的虚接口，所有 QoI 实现只依赖这个接口：

| 方法 | 语义 | 默认实现 |
|------|------|----------|
| `interpret_eb(x)` | 给定数据值，返回逐点误差界 eb | 基于 `eval()` 数值导数 |
| `check_comply(orig, dec)` | 验证 `|f(orig)-f(dec)| ≤ tol` | 基于 `eval()` |
| `eval(val)` | 正向求值 f(val) | `return val`（恒等） |
| `create_eb_provider(conf)` | 创建对应的 EBProvider | 纯虚，子类实现 |
| `is_pointwise()` | 点态或块级 budget tracking | `false` |
| `precompress_block(N)` / `update_tolerance(o,d)` / `postcompress_block()` | 块生命周期（Regional 用） | 空 |

**设计意图**：`eval()` 和 `create_eb_provider()` 的加入使得——
- 组合 QoI（SumQoI、MultiQoI）可以通过 `eval()` 算出组合导数，无需了解每个子 QoI 的内部公式；
- 外部 Decomposition 类只需一行 `qoi->create_eb_provider(conf)`，不需要知道内部是 PointwiseEBProvider、RegionalMeanEBProvider 还是 MultiQoIEBProvider。

同样 `is_pointwise()` 替代了此前 `qoi->id < 10` 的硬编码判断，让 SZDispatcher 的 eb 预计算逻辑对 QoI 类型透明。

## EBProvider 抽象层

```cpp
template <typename T>
class EBProvider {
    virtual T advance(T orig, T dec) = 0;   // 压缩端：取 eb，更新状态
    virtual void advance() = 0;              // 解压端：推进计数器
    virtual void precompress_block(size_t) = 0;
    virtual void postcompress_block() = 0;
    virtual void save(uchar*&) const = 0;
    virtual void load(const uchar*&, size_t&) = 0;
};
```

所有 eb 来源统一为这个接口，目前有三种实现：

| 实现 | 适用场景 | eb 来源 |
|------|----------|---------|
| `PointwiseEBProvider` | 点态 QoI（XLin, X2, …, 所有基函数） | 预计算 `conf.ebs[]` |
| `RegionalMeanEBProvider` / `RegionalMeanSqEBProvider` | 区域均值/均方 QoI | 在线 budget tracking |
| `MultiQoIEBProvider` | 多组 AND 约束 | 持有多个子 Provider，`advance` 取 min |

## 新增与修改的文件

### 新增（全在 `include/SZ3/qoi/` 和 `include/SZ3/decomposition/`）

```
include/SZ3/qoi/
├── QoI.hpp                          QoIIf 基类（eval, create_eb_provider, is_pointwise）
├── QoIIf.hpp                        工厂 GetQOI + nibble 解析器
├── EBProvider.hpp                   EBProvider 抽象接口
├── PointwiseEBProvider.hpp          点态 eb 源
├── MultiQoIEBProvider.hpp           多 provider min 组合
├── QoIXLin.hpp / QoIX2.hpp          基函数（已有，增加 eval + create_eb_provider）
├── QoI_XCubic.hpp .. QoI_XPower.hpp  10 个新增基函数（通过 eval() 自动推导 interpret_eb）
├── QoI_SumQoI.hpp                   组内求和约束（Σ fi，数值导数推 eb）
├── QoI_MultiQoI.hpp                 多组 AND 约束（取 min eb）
├── RegionalMean.hpp / RegionalMeanSq.hpp            blockwise Regional QoI
├── QoI_RegionalAvgInterp.hpp / QoI_RegionalMeanSqInterp.hpp   Interp 路径专用 QoI
│
include/SZ3/decomposition/
├── QpetBlockDecomp.hpp              块级分解器（Lorenzo/Regression）
└── QpetInterpDecomp.hpp             插值分解器（linear/cubic，锚点支持）
```

### 修改（对 SZ3 基底侵入控制在最小范围）

| 文件 | 变更原因 |
|------|----------|
| `SZ3/qoi/QoI.hpp` | 增加 `eval()`, `create_eb_provider()`, `is_pointwise()` 三个虚方法。仅此一个文件有接口级变更，且向后兼容 |
| `SZ3/decomposition/Decomposition.hpp` | 补一个 `#include Config.hpp`（原代码依赖 GCC 惰性查模板名，非标准行为） |
| `SZ3/qoi/QoIIf.hpp` | 工厂从 4 个 case 扩展到 6 个 legacy case + nibble 解析路径 |
| `SZ3/api/impl/SZDispatcher.hpp` | `qoi->id < 10` 改为 `qoi->is_pointwise()`；Interp 路径增加 qoi≥12 的路由 |
| `SZ3/api/impl/SZAlgoInterp.hpp` | 新增 `SZ_compress_Interp_qpet` / `SZ_decompress_Interp_qpet` |
| `SZ3/decomposition/QpetBlockDecomp.hpp` | `if (qoi->id == 10/11)` 硬编码 dispatch 替换为 `qoi->create_eb_provider(conf)` |
| `SZ3/decomposition/QpetInterpDecomp.hpp` | 同上 |
| `SZ3/utils/Config.hpp` | 无修改（nibble 编码复用现有 `int qoi` 字段，不扩字段） |

## Nibble 编码：组合 QoI 无侵入表达

不需要在 Config 中新增字段来表达复合 QoI。`int qoi` 的低位每 4 bit 为一格，`0xF` 为组分隔符，组内 Sum、组间 AND：

| 编码 | 含义 |
|------|------|
| `0x1` | 单独 XSquare |
| `0x12` | XSquare + XCubic（组内求和约束） |
| `0x1F3` | XSquare AND XSqrt（两组 AND） |
| `0x12F3F456` | Sum(1,2) AND 3 AND Sum(4,5,6) |

## ~ 翻转标记 Regional

Regional QoI 有独立编号（0: RegionalMean, 1: RegionalMeanSq, 2: RegionalAvgInterp, 3: RegionalMeanSqInterp），使用时将编号做 `~` 翻转存入 `conf.qoi`。翻转后最高位为 1，工厂据此分流 regional/pointwise，无需额外 flag 字段。

| 编号 | `conf.qoi` | QoI |
|------|-----------|-----|
| 0 | `~0` | RegionalMean（区间均值 budget tracking） |
| 1 | `~1` | RegionalMeanSq（区间平方和 budget tracking） |
| 2 | `~2` | RegionalAvgInterp（Interp 路径均值） |
| 3 | `~3` | RegionalMeanSqInterp（Interp 路径平方和） |

`is_pointwise()` 基类实现为 `return id >= 0;`，regional QoI 的 id 为负值（`~rid`），自动返回 false，无需子类逐个重写。

legacy ID 0–1 继续走原工厂路径。其余正值自动进入 nibble 解析器。

## Interp 路径接入

InterpolationDecomposition（线性/三次样条分层插值）是 SZ3 的另一条独立压缩路径。我们新建 `QpetInterpDecomp` 将其接入 QPET：

- 照搬 InterpolationDecomposition 的所有遍历与插值逻辑，但每一处的 `quantize_and_overwrite(d, pred)` 替换为两步量化 `qnt_eb(eb) + qnt_overwrite(d, pred, eb)`
- EBProvider 通过 `qoi->create_eb_provider(conf)` 注入，与 blockwise 路径一致
- 锚点网格走 QpetQnt 的 `force_save_unpred` / `recv` 无损保存
- 文件格式 `[qi_ebs | qi_datas]` 与 blockwise 路径统一

这一设计使两类预测器（Lorenzo/Regression vs Interp）共享完全相同的 QoI + EBProvider + QpetQnt 栈，新增 QoI 类型时两条路径自动受益。

## 文件格式

```
[Config] [Decomp: fallback_pred + pred + QpetQnt(rd + unpred + eb_log)] [Huffman树] [qi_cnt] [E(qi_vec)] → Zstd
```

Blockwise 和 Interp 路径统一：`qi_vec` 前半为 qi_eb，后半为 qi_data。

## CI

commit message 末尾 `[关键字...]` 控制 CI：

| 关键字 | 效果 |
|--------|------|
| `[build]` | Linux 编译 |
| `[test]` 或 `[test build]` | Linux 编译 + QoI 单元测试 |
| `[]` | 仅检查非 ASCII 字符 |

## 编译与测试

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DSZ3_USE_BUNDLED_ZSTD=OFF
make -j4
../test/bin/sz3_qpet_test
```
