# SZ3 QPET 插件实现说明

在 SZ3 v3.3.2 基础上实现的 QPET 插件。核心设计目标：**接口统一，对基底 SZ3 侵入最小**。

## 设计原则

- **新增功能放新文件，不改 SZ3 原生类**
- **通过虚接口消除硬编码 dispatch**
- **两类 Decomposition 共享同一套 QoI + EBProvider 接口**

## 工作流

```
Config → eb 预算 → [QpetBlockDecomp: Pred + QpetQnt(逐点eb)]
                       ├─ qnt_eb(eb) → qi_eb
                       ├─ qnt_overwrite(data,pred,eb) → qi_data
                       └─ 拼接 [qi_eb.. | qi_data..]
         └── 或 Interp ──→ [QpetInterpDecomp: 插值预测 + QpetQnt]
                                    ↓
                         HuffmanEncoder → Lossless_zstd → 文件
```

## QoI 接口体系

`QoIIf<T,N>` 抽象基类（`include/SZ3/qoi/QoI.hpp`）：

| 方法 | 语义 | 默认实现 |
|------|------|----------|
| `interpret_eb(x)` | 逐点误差界 eb | 基于 `eval()` 数值导数 |
| `check_comply(orig, dec)` | 验证 `|f(orig)-f(dec)| ≤ tol` | 基于 `eval()` |
| `eval(val)` | 正向求值 f(val) | `return val` |
| `create_eb_provider(conf)` | 创建对应 EBProvider | 纯虚 |
| `is_pointwise()` | 点态/块级 budget tracking | `id >= 0` |
| `precompress_block(N)` / `update_tolerance(o,d)` | 块生命周期（Regional 用） | 空 |
| `has_bias()` / `precompute_data(data,n)` / `get_bias(idx)` | 卷积偏置预计算 | false / 空 / 0 |

## EBProvider 抽象层

```cpp
template <typename T>
class EBProvider {
    virtual T advance(T orig, T dec) = 0;
    virtual void advance() = 0;
    virtual void precompress_block(size_t) = 0;
    virtual void postcompress_block() = 0;
    virtual void save(uchar*&) const = 0;
    virtual void load(const uchar*&, size_t&) = 0;
};
```

| 实现 | 场景 | eb 来源 |
|------|------|---------|
| `PointwiseEBProvider` | 点态 QoI | `conf.ebs[]` 预计算 |
| RegionalNibble::Impl | Regional QoI | 在线 budget tracking |
| `MultiQoIEBProvider` | 多组 AND | 多个子 Provider，advance 取 min |

## 新增文件

```
include/SZ3/qoi/
├── QoI.hpp                          QoIIf 基类
├── QoIIf.hpp                        工厂 GetQOI + 全部 mode dispatch + nibble 解析器
├── QoI_IsolineNibble.hpp            Isoline 模式（mode 6）
├── QoI_FX.hpp                       FX 模式（mode 7，TinyExpr 任意函数）
├── QoI_Conv.hpp                     Conv 模式（Regional 族，滑动窗口卷积 + bias）
├── RegionalNibble.hpp               Regional 统一编码（nibble/FX）
├── QoIXLin.hpp .. QoI_XPower.hpp    12 个基函数
├── QoI_SumQoI.hpp / QoI_MultiQoI.hpp / QoI_Compose.hpp  组合逻辑
├── RegionalMean.hpp / RegionalMeanSq.hpp                 legacy（已由 RegionalNibble 取代）
├── QoI_RegionalAvgInterp.hpp / QoI_RegionalMeanSqInterp.hpp  legacy
│
include/SZ3/decomposition/
├── QpetBlockDecomp.hpp              块级分解器
└── QpetInterpDecomp.hpp             插值分解器
│
tools/qoi_encoder/                   encoder CLI（encode.hpp + fx_encode.hpp + main.cpp）
test/                                测试套件（e2e + isoline_tests + encoder_tests）
```

## 对 SZ3 基底的修改（最小侵入）

| 文件 | 变更 |
|------|------|
| `SZ3/qoi/QoI.hpp` | 增加 `eval`, `create_eb_provider`, `is_pointwise`, `has_bias`, `precompute_data`, `get_bias` |
| `SZ3/qoi/QoIIf.hpp` | 工厂扩展：FX / Isoline / Conv / Regional nibble / Regional FX, 未知 nibble → throw |
| `SZ3/api/impl/SZDispatcher.hpp` | `is_pointwise()` 替代 `id<10`；bias 预计算 + dataCopy 偏置施加 |
| `SZ3/api/impl/SZAlgoInterp.hpp` | 新增 `_qpet` 版本 |
| `SZ3/decomposition/QpetBlockDecomp.hpp` / `QpetInterpDecomp.hpp` | EBProvider 取代硬编码 dispatch |
| `SZ3/utils/Config.hpp` | `qoiParams` → `vector<uchar>` 原始二进制，base64 codec，删除旧扩展字段 |
| `CMakeLists.txt` | 新增 `tools/qoi_encoder` |

## Nibble 编码与 qoiParams

`conf.qoi` 为 32-bit int。高位 nibble 编码模式：

| 高位 | 模式 | 说明 |
|------|------|------|
| `0x0` | 常规 nibble | 基函数 + Sum/Compose/Multi |
| `0x6` | Isoline | 等值线约束模式 |
| `0x7` (qoi>0) | FX | SymEngine 任意函数 |
| `0xF` (qoi<0) | Regional nibble | `~rid` 翻转 |
| `0xC` (qoi<0) | Regional FX | `~rid` 翻转 |
| `0x8` (qoi<0) | Conv | `~rid` 翻转，滑动窗口卷积 |
| 其他非零 | — | throw |

`conf.qoiParams` 内部存储为原始二进制（`vector<uchar>`），base64 仅为 CLI 和 INI 的传输格式。

> 完整格式、参数、示例见 **[qoi.md](qoi.md)**。

## Encoder CLI

```bash
# Nibble
./test/bin/qoi_encoder "sqr+abs+cubic"
./test/bin/qoi_encoder "lin(2,0.5)+sqr"

# Isoline
./test/bin/qoi_encoder "iso6(sqr, -5, 5, 3, 0.01)"

# FX (需要 SymEngine)
./test/bin/qoi_encoder "sin(x)+x^2"

# Regional
./test/bin/qoi_encoder --regional "sqr+cubic"
./test/bin/qoi_encoder --regional "sin(x)+x^2"

# Convolution
./test/bin/qoi_encoder "conv(1,-2,1,0.5)"
```

## 编译与测试

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)

# 基础门禁
./test/bin/e2e --basic --encoder-path=./test/bin/qoi_encoder

# 全量（约 1890 条，约 2min）
./test/bin/e2e --full --encoder-path=./test/bin/qoi_encoder

# 分项
./test/bin/e2e --regional --encoder-path=./test/bin/qoi_encoder
./test/bin/e2e --conv --encoder-path=./test/bin/qoi_encoder
```

> 详细 CLI、测试覆盖清单见 **[test.md](test.md)**。编码格式见 **[qoi.md](qoi.md)**。

## API 调用

```cpp
#include "SZ3/api/SZ3.hpp"

SZ3::Config conf;
conf.setDims(dims.begin(), dims.end());
conf.cmprAlgo  = SZ3::ALGO_LORENZO_REG;

// Pointwise nibble
conf.qoi = 0x12;      // sqr + cubic (Sum)
conf.qoi = 0x1F3;     // sqr AND sqrt (Multi)
conf.qoi = 0x14E;     // Compose(exp, sqr) = e^(x²)

// Regional
conf.qoi = ~0;        // RegionalMean (XLin)
conf.qoi = ~1;        // RegionalMeanSq (sqr)

// Convolution
// Use encoder or manually construct:
// conf.qoi = ~(0x70000000 | (1<<24) | w);   // 1D conv, width w
// conf.qoiParams = base64_decode("...");     // double[w] weights + tol

conf.qEB    = 0.01;   // QoI tolerance
conf.qR     = 12;
conf.quantbinCnt = 65536;

size_t cmpSize = SZ_compress(conf, data, outBuf, outCap);
double *dec = SZ_decompress(conf2, outBuf, cmpSize);
```

