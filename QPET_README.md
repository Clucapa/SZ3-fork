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
├── QoIIf.hpp                        工厂 GetQOI + nibble 解析器 + base64_decode
├── EBProvider.hpp                   EBProvider 抽象接口
├── PointwiseEBProvider.hpp          点态 eb 源
├── MultiQoIEBProvider.hpp           多 provider min 组合
├── QoIXLin.hpp / QoIX2.hpp          基函数（已有，增加 eval + create_eb_provider + 参数化构造）
├── QoI_XCubic.hpp .. QoI_XPower.hpp  10 个新增基函数（通过 eval() 自动推导 interpret_eb）
├── QoI_SumQoI.hpp                   组内求和约束（Σ fi，数值导数推 eb）
├── QoI_MultiQoI.hpp                 多组 AND 约束（取 min eb）
├── QoI_Compose.hpp                  函数嵌套 Compose(f,g) = f(g(x))
├── RegionalMean.hpp / RegionalMeanSq.hpp            blockwise Regional QoI
├── QoI_RegionalAvgInterp.hpp / QoI_RegionalMeanSqInterp.hpp   Interp 路径专用 QoI
│
include/SZ3/decomposition/
├── QpetBlockDecomp.hpp              块级分解器（Lorenzo/Regression）
└── QpetInterpDecomp.hpp             插值分解器（linear/cubic，锚点支持 + check_comply 安全网）
│
tools/qoi_encoder/                   独立 encoder CLI 工具（见下方）
test/                                端到端参数化测试套件（见下方）
```

### 修改（对 SZ3 基底侵入控制在最小范围）

| 文件 | 变更原因 |
|------|----------|
| `SZ3/qoi/QoI.hpp` | 增加 `eval()`, `create_eb_provider()`, `is_pointwise()` 三个虚方法。仅此一个文件有接口级变更，且向后兼容 |
| `SZ3/decomposition/Decomposition.hpp` | 补一个 `#include Config.hpp`（原代码依赖 GCC 惰性查模板名，非标准行为） |
| `SZ3/qoi/QoIIf.hpp` | 工厂从 4 个 case 扩展到 6 个 legacy case + nibble 解析路径（含 0xE Compose 递归下降） |
| `SZ3/api/impl/SZDispatcher.hpp` | `qoi->id < 10` 改为 `qoi->is_pointwise()`；Interp 路径统一走 `_qpet` |
| `SZ3/api/impl/SZAlgoInterp.hpp` | 新增 `SZ_compress_Interp_qpet` / `SZ_decompress_Interp_qpet` |
| `SZ3/decomposition/QpetBlockDecomp.hpp` | `if (qoi->id == 10/11)` 硬编码 dispatch 替换为 `qoi->create_eb_provider(conf)` |
| `SZ3/decomposition/QpetInterpDecomp.hpp` | 同上；新增 `precompress_block` + `check_comply` 安全网 + `interpret_eb` 位置无关 eb 取值 |
| `SZ3/utils/Config.hpp` | 新增 `std::string qoiParams`（base64 参数）；删除 `qEBase`/`qELogB` |

## Nibble 编码：组合 QoI 无侵入表达

不需要在 Config 中新增字段来表达复合 QoI。`int qoi` 的低位每 4 bit 为一格，`0xF` 为组分隔符，`0xE` 为函数嵌套运算符，组内 Sum、组间 AND。

### 编码算子表

| nibble | 含义 |
|--------|------|
| `0x0` ~ `0xB` | 基函数（见下方对照表） |
| `0xC` ~ `0xD` | 预留 |
| `0xE` | Compose(f, g) — 严格 2 操作数，prefix 式（`E f g`） |
| `0xF` | 组分隔符 — 结束当前组，开始下一组 |

### 编码示例

| `conf.qoi` | 含义 |
|------------|------|
| `0x1` | 单独 XSquare |
| `0x12` | XSquare + XCubic（组内求和约束） |
| `0x1F3` | XSquare AND XSqrt（两组 AND） |
| `0x12F3F456` | Sum(1,2) AND 3 AND Sum(4,5,6) |
| `0x14E` | Compose(XExp, XSquare) = eˣ² |
| `0x0E1E2E3` | Compose(XLin, Compose(XSquare, Compose(XCubic, XSqrt))) |

### Compose 恒等 elision

内部做恒等消除：`Compose(f, XLin(1,0))` → `f`，`Compose(XLin(1,0), g)` → `g`。最外层 `GetQOI(conf)` 对 `qoi == 0` 和 `qoi == 1` 的硬编码 dispatch 不参与 elision，保证接口稳定。

### 基函数 Nibble 对照表

| nibble | 类名 | f(x) | 参数 | 默认 |
|--------|------|------|------|------|
| `0x0` | QoI_XLin | Ax + B | A, B | 1, 0 |
| `0x1` | QoI_X2 | x² | — | — |
| `0x2` | QoI_XCubic | x³ | — | — |
| `0x3` | QoI_XSqrt | √x | — | — |
| `0x4` | QoI_XExp | aˣ | base (a) | e |
| `0x5` | QoI_XLogX | x·log(x) | — | — |
| `0x6` | QoI_LogX | log_a x | base (a) | e |
| `0x7` | QoI_XRecip | 1/x | — | — |
| `0x8` | QoI_XAbs | \|x\| | — | — |
| `0x9` | QoI_XSin | sin(x) | — | — |
| `0xA` | QoI_XTanh | tanh(x) | — | — |
| `0xB` | QoI_XPower | x^a | expo (a) | 2 |

## qoiParams：Base64 参数传递

有参数的基函数（XLin, XExp, LogX, XPower）通过 `Config::qoiParams` 传入 base64 编码的 `double[]`。工厂在运行时 base64 decode → `ParamReader` 按 nibble 遍历顺序依次消费。

空串 = 无参数 = 全部使用默认值，向后兼容。

```
表达式: XLin(2, 1.5) + Exp(10) + XSquare
qoi = 0x140
qoiParams = base64_encode([2.0, 1.5, 10.0])
→ nibble 遍历顺序: XSquare(无参) → XLin(读2,1.5) → XExp(读10)
```

## FX 模式：任意函数约束（计划中）

`qoi` 的最高 nibble（bit 28–31）为 `0x7` 时标记 FX 模式，跳过 nibble 解析，走 `QoI_FX` 路径。

```
qoi = 0x70000000
qoiParams = base64( f_str + df_str + ddf_str )

管道:
  SymEngine 符号求导 → TinyExpr compile → QoI_FX(eval/interpret_eb 精确导数)
```

FX 模式专用 `QoI_FX` 类，不依赖数值导数。`qoiParams` 格式：`[4B:len_f][len_f:f_str][4B:len_df][len_df:df_str][4B:len_ddf][len_ddf:ddf_str]` → base64。

### qoi_encoder 工具（计划中）

```
tools/qoi_encoder/
  main.cpp    CLI: 表达式 → qoi + base64

用法:
  ./qoi_encoder "sqr+abs+cubic"              → qoi=0x128
  ./qoi_encoder "lin(2,0.5)+exp(10)+sqr"    → qoi=0x140, params="AAAAABAAAAAAAAAAF8A..."
  ./qoi_encoder "exp(2.718)@sqr"             → qoi=0x14E, params="..."
  ./qoi_encoder "fx(\"sin(x)+x^2\")"         → qoi=0x70000000, params="..."
```

## ~ 翻转标记 Regional

Regional QoI 有独立编号（0: RegionalMean, 1: RegionalMeanSq, 2: RegionalAvgInterp, 3: RegionalMeanSqInterp），使用时将编号做 `~` 翻转存入 `conf.qoi`。翻转后最高位为 1，工厂据此分流 regional/pointwise，无需额外 flag 字段。

| 编号 | `conf.qoi` | QoI | 约束 | 路径 |
|------|-----------|-----|------|------|
| 0 | `~0` | RegionalMean | `|mean(orig) - mean(dec)| ≤ τ` | blockwise |
| 1 | `~1` | RegionalMeanSq | `|mean(orig²) - mean(dec²)| ≤ τ` | blockwise |
| 2 | `~2` | RegionalAvgInterp | 同 RegionalMean | interp |
| 3 | `~3` | RegionalMeanSqInterp | 同 RegionalMeanSq | interp |

`is_pointwise()` 基类实现为 `return id >= 0;`，regional QoI 的 id 为负值（`~rid`），自动返回 false，无需子类逐个重写。

legacy ID 0–1 继续走原工厂路径。其余正值自动进入 nibble 解析器。

## Interp 路径接入

InterpolationDecomposition（线性/三次样条分层插值）是 SZ3 的另一条独立压缩路径。我们新建 `QpetInterpDecomp` 将其接入 QPET：

- 照搬 InterpolationDecomposition 的所有遍历与插值逻辑，但每一处的 `quantize_and_overwrite(d, pred)` 替换为两步量化 `qnt_eb(eb) + qnt_overwrite(d, pred, eb)`
- EBProvider 通过 `qoi->create_eb_provider(conf)` 注入，与 blockwise 路径一致
- 锚点网格走 QpetQnt 的 `force_save_unpred` / `recv` 无损保存
- **QoI 安全网**：量化后逐点 `check_comply`，不满足时回写原始值（无损），与 BlockDecomp 一致
- **位置无关 eb 取值**：使用 `qoi->interpret_eb(orig_val)` 直接计算 eb，不依赖 `advance()` 的顺序递增假设，适应 stride 遍历顺序
- 文件格式 `[qi_ebs | qi_datas]` 与 blockwise 路径统一

这一设计使两类预测器（Lorenzo/Regression vs Interp）共享完全相同的 QoI + EBProvider + QpetQnt 栈，新增 QoI 类型时两条路径自动受益。

## 文件格式

```
[Config] [Decomp: fallback_pred + pred + QpetQnt(rd + unpred + eb_log)] [Huffman树] [qi_cnt] [E(qi_vec)] → Zstd
```

Blockwise 和 Interp 路径统一：`qi_vec` 前半为 qi_eb，后半为 qi_data。

## 编译与测试

### 依赖

- CMake ≥ 3.14
- C++17 编译器
- zstd (或使用内置版: `-DSZ3_USE_BUNDLED_ZSTD=ON`)
- GTest（自动从 GitHub 拉取 v1.16.0，也可 `apt install libgtest-dev`）

### 编译

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

### 运行测试

```bash
# CI 门禁（e2e --fast，<1s）
cd build && ctest --output-on-failure

# 全量 e2e（e2e --full，~2min）
./test/bin/e2e --full

# 单个 GTest 模块（本地调试用）
./test/bin/test_qpet_qoi
./test/bin/test_qpet_composite
```

### 测试覆盖清单

| 测试 | 类型 | 内容 |
|------|------|------|
| `e2e` | 端到端 | 19 QoI × 8 数据模式 × 3 维 × 5 算法变体（Block / Interp-Cubic / Interp-Linear / ILorenzo-Cubic / ILorenzo-Linear）。逐点 check_comply + Regional 聚合约束 + absErrorBound |
| `test_qpet_interp_pointwise` | 端到端 | ALGO_INTERP + pointwise QoI 通过 dispatcher 的完整 compress→decompress→QoI 验证 |
| `test_qpet_qoi` | 单元 | XLin / X2 的 `interpret_eb`、`check_comply`、`set_tol`/`set_geb` |
| `test_qpet_eb_provider` | 单元 | PointwiseEBProvider 的 `advance`（压缩/解压）、`reset`、`save/load`、`double` 类型 |
| `test_qpet_regional` | 单元 | RegionalMean / RegionalMeanSq 的 budget tracking、geb capping、负值对称性 |
| `test_qpet_interp` | 单元+端到端 | QpetInterpDecomp 的 1D/2D round-trip、linear/cubic、anchor 无损、RegionalMeanSqInterp |
| `test_qpet_composite` | 单元+端到端 | 12 基函数全覆盖；SumQoI、MultiQoI、Compose；nibble 解析器；端到端 Lorenzo 压缩 |

### CI

commit message 末尾 `[关键字...]` 控制：

| 关键字 | 效果 |
|--------|------|
| （不带标签） | 仅检查非 ASCII 字符 |
| `[build]` | Linux 编译 |
| `[test]` | 编译 + `e2e --fast`（CI 门禁，<1s） |
| `[full]` | 编译 + `e2e --full`（全量 1700+ 条，~2min） |

## API 调用

QPET 的使用入口仍是 `SZ_compress_dispatcher`，在 `Config` 中设置：

```cpp
#include "SZ3/api/SZ3.hpp"

SZ3::Config conf;
conf.setDims(dims.begin(), dims.end());
conf.cmprAlgo  = SZ3::ALGO_LORENZO_REG;  // 或 ALGO_INTERP / ALGO_INTERP_LORENZO

// --- 选 QoI ---
// 1) legacy
conf.qoi = 0;   // XLin (f(x)=x)
conf.qoi = 1;   // X2   (f(x)=x²)

// 2) nibble 编码（点态组合，含 Compose）
conf.qoi = 0x2;       // XCubic
conf.qoi = 0x12;      // X2 + XCubic (Sum)
conf.qoi = 0x1F3;     // X2 AND XSqrt (MultiQoI)
conf.qoi = 0x14E;     // Compose(Exp, Square) = e^(x²)

// 3) regional（~翻转）
conf.qoi = ~0;   // RegionalMean
conf.qoi = ~1;   // RegionalMeanSq
conf.qoi = ~2;   // RegionalAvgInterp
conf.qoi = ~3;   // RegionalMeanSqInterp

// --- 参数化（可选，空串 = 默认值）---
conf.qoiParams = "";   // 无参数，全部用默认值

// --- QPET 量化参数 ---
conf.qEB    = 0.01;   // QoI tolerance (τ / geb)
conf.qR     = 12;     // eb 量化 range
conf.quantbinCnt = 65536;

size_t cmpSize = SZ_compress(conf, data, outBuf, outCap);
double *dec = SZ_decompress(conf2, outBuf, cmpSize);
```
