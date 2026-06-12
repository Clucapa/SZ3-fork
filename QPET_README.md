# SZ3 QPET 插件实现说明

本仓库在 SZ3 v3.3.2 基础上，以相对独立模块化的方式，实现了 QPET 插件。

## 工作流变更

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
```

关键差异：
1. **量化前增加 eb 预算**：QoI 实例逐点计算误差界，存入 `conf.ebs`
2. **量化器改为 QpetQnt**：两步操作（先量化 eb 得 qi_eb，再用量化后的 eb' 量化残差得 qi_data），eb' ≤ 原始 eb（安全）

## 总体思路

- 通过 QoI 函数（如 f(x)=x²）将全局误差界转换为逐点误差界
- 预测器选择（Lorenzo/Regression/Composed）不受影响，自由组合
- 即使 qoi=0（不启用 qoi 控制功能），也走 QPET 量化器（默认 f(x)=x），文件格式统一

## 新增模块

| 目录 | 文件 | 职责 |
|------|------|------|
| `include/SZ3/qoi/` | `QoI.hpp` | QoI 抽象接口 `QoIIf<T,N>`：定义 `interpret_eb(x)`（推导逐点 eb）、`check_comply(orig,dec)`（验证合规）、及块级钩子 `precompress_block/update_tolerance/postcompress_block`（Regional 用） |
| | `QoIXLin.hpp` | 默认实现：f(x)=x，eb ≡ tol。行为等价标准 SZ3 |
| | `QoIX2.hpp` | f(x)=x² 的解析实现：`eb = -|x| + sqrt(x²+τ)` |
| | `QoIIf.hpp` | 工厂函数 `GetQOI(conf)` ，根据 `conf.qoi` 创建对应 QoI 实例 |
| | `EBProvider.hpp` | 抽象 eb 读取接口：`advance(orig,dec)`(压缩) / `advance()`(解压) / `save` / `load` |
| | `PointwiseEBProvider.hpp` | 点 QoI 版：读取预计算 `conf.ebs[]` |
| | `RegionalMean.hpp` | RegionalMean QoI (qoi=10) + 配套 EBProvider：残差绝对值 budget tracking，前半块 ×2 |
| | `RegionalMeanSq.hpp` | RegionalMeanSq QoI (qoi=11) + 配套 EBProvider：平方域 budget tracking，f⁻¹ 反变换 |
| `include/SZ3/quantizer/` | `EBLogQnt.hpp` | 误差界对数尺度量化器：将 eb 值量化为小整数，floor 语义保证量化后 eb ≤ 原始 eb |
| | `QpetQnt.hpp` | QPET 量化器：持有 `EBLogQnt` 实例，对外提供 `qnt_overwrite(data,pred,eb)` 和 `qnt_eb(eb)` 两步操作 |
| `include/SZ3/decomposition/` | `QpetBlockDecomp.hpp` | QPET 块级分解器：块循环中调用 Pred 预测、QoI 检查合规、QpetQnt 量化，输出 `[所有qi_eb | 所有qi_data]` |

## 修改的模块

| 文件 | 变更 |
|------|------|
| `include/SZ3/quantizer/Quantizer.hpp` | 新增 `QpetQntIf<Ti,To>` 接口，继承自 `QuantizerInterface`，增加带 eb 参数的 `qnt_overwrite(d,pred,eb)`、`recv(pred,qi,eb)`、`recv_eb(qe)` |
| `include/SZ3/utils/Config.hpp` | 新增 `qoi`、`qEB`、`qEBase`、`qELogB`、`qR` 五个字段及 `ebs` 向量；`save()` / `load()` 追加序列化 |
| `include/SZ3/api/impl/SZAlgoLorenzoReg.hpp` | 替换原 `LinearQnt + BlockwiseDecomp` 为 `QpetQnt + QpetBlockDecomp`。`conf.qoi` 任意值时均走 QPET 路径 |
| `include/SZ3/api/impl/SZDispatcher.hpp` | 在算法 dispatch 前增加 eb 预计算步骤（点 QoI 模式）或跳过（regional 模式） |
| `include/SZ3/decomposition/QpetBlockDecomp.hpp` | 改用 EBProvider 读取 eb，不再裸读 `conf.ebs[off++]`；内建自动选择点/regional provider |

## v2 新增：统一 QoI 接口 + Regional QoI

| 改动 | 说明 |
|------|------|
| `EBProvider` 抽象层 | `advance(orig,dec)` / `advance()` 双接口替代裸 `conf.ebs[off++]`，屏蔽 pointwise vs regional eb 来源差异 |
| `PointwiseEBProvider` | 延续旧行为，读取预计算 `conf.ebs[]` |
| 点 QoI + Regional QoI 两套 | 点 QoI（XLin/X2）eb 提前算好存数组；Regional（Mean/MeanSq）逐块在线预算，走 QoZ 一致的 budget tracking |
| `QoIIf` 基类增加生命周期钩子 | `precompress_block(N)`, `update_tolerance(orig,dec)`, `postcompress_block()` — 默认空，Regional 用 |
| `RegionalMean` (qoi=10) | 残差绝对值 budget tracking，前半块 ×2 |
| `RegionalMeanSq` (qoi=11) | 平方域 budget tracking，f⁻¹ 反变换回 eb |
| `QpetBlockDecomp` | 内建 provider 创建（点/regional 自动选择），compress/decompress 入口一致 |
| `SZDispatcher` | regional 模式跳过全局 ebs 预计算步骤 |

## 文件格式

```
[Config] [Decomp: fallback_pred + pred + QpetQnt(rd + unpred + eb_log)] [Huffman树] [qi_cnt] [E(qi_vec)] → Zstd
```

解压端: `qi_vec` 前 `conf.num` 个为 qi_eb，后 `conf.num` 个为 qi_data。

## 编译与测试

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DSZ3_USE_BUNDLED_ZSTD=OFF
make -j4
../test/bin/sz3_qpet_test
```
