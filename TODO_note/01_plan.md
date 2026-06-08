# QPET 插件实现计划

## 核心原则

1. **QPET 不是新 ALGO**。通过 `conf.qoi` 控制 QoI 类型；预测器（Lorenzo/Regression）自由选择。
2. **始终 QpetQnt**。`qoi=0` 对应 `QoI_XLin(inline)`（`f(x)=x`），走 QpetQnt + QpetBlockDecomp，与传统 LinearQnt 行为等价。文件格式始终为 `[qi_eb|qi_data]`。
3. **Predictor ↔ QoI/eb 零依赖**。预测器只看数据值，不接触 eb。eb 仅在量化器阶段作用于残差。
4. **模块可移植**。Layer 0/1/2 文件只依赖接口契约，可整体复制到维护相同接口的未来 SZ3 版本。

## 整体数据流

```
压缩:
  data  →  [eb 预计算: QoI.interpret_eb]  →  ebs[]
  data  →  [Predictor]  →  pred
  data  →  [QpetQnt]
               ├─ ① log 有损变换 eb (floor → 安全, qi_eb)
               ├─ ② 用 eb' 量化 |data-pred| (qi_data)
               └─ ③ 缓冲 {qi_eb, qi_data}, 块结束时 flush
                                        ↓
         [qi_eb.. | qi_data..]  →  Huffman (统一)  →  Zstd  →  文件

解压:
  文件  →  Zstd  →  Huffman  →  [qi_eb.. | qi_data..]
                                 ↓
                   qnt.recv_eb(qi_eb)  →  eb
                   qnt.recv(pred, qi_data, eb)  →  data
```

## 架构分层

```
Layer 0 ─ 接口契约（不引入任何实现依赖）
  QuantizerIf<Ti,To>     ← quantizer/Quantizer.hpp (已有)
  QpetQntIf<Ti,To>       ← quantizer/Quantizer.hpp (新增, 继承 QuantizerIf)
  PredictorIf<T,N>        ← predictor/Predictor.hpp (已有)
  QoIIf<T,N>              ← qoi/QoI.hpp (新增)
  DecompIf<Ti,To,N>       ← decomposition/Decomposition.hpp (已有)

Layer 1 ─ 独立实现（只依赖 Layer 0 接口 + def.hpp + std）
  QoI_XLin.hpp           ← qoi=0 默认, f(x)=x
  QoI_X2.hpp             ← qoi=1, f(x)=x²
  QoIIf.hpp              ← 工厂, switch(conf.qoi)
  EBLogQnt.hpp           ← log 尺度量化 eb
  QpetQnt.hpp            ← 三步合一 (含 EBLogQnt 实例)

Layer 2 ─ 编排器（依赖 Layer 0 + Layer 1 + block_data）
  QpetBlockDecomp.hpp    ← 块循环 + flush + 合规回退

Layer 3 ─ 集成层（依赖所有上层，每版本需适配）
  SZAlgoLorenzoReg.hpp   ← conf.qoi 分支: QpetQnt vs LinearQnt
  SZDispatcher.hpp       ← eb 预计算 (if conf.qoi != 0)
```

## 文档导航

| 文件 | 内容 | 层 | 可移植 |
|------|------|:--:|:--:|
| `00_questions.md` | 3 个待定设计问题 | — | — |
| `01_plan.md` | 本文件：总览 | — | — |
| `02_config_mods.md` | Config 新增字段, save/load/ini | L3 | ✗ |
| `03_module_boundaries.md` | 依赖树、#include 清单、移植规则 | — | — |
| `04_qoi_module.md` | QoIIf, QoI_XLin, QoI_X2, QoIIf 工厂 | L0+L1 | ✓ |
| `05_qpet_quantizer.md` | QpetQntIf, QpetQnt (三步合一), EBLogQnt | L0+L1 | ✓ |
| `06_qpet_decomp.md` | QpetBlockDecomp 编排器 | L2 | ✓ |
| `07_integration.md` | eb 预计算, SZAlgoLorenzoReg 分支, 预测器复用 | L3 | ✗ |
| `08_data_layout.md` | qi_eb/qi_data 布局, 单 Huffman/单 Zstd | — | — |
| `09_testing_plan.md` | 6 阶段测试 | — | — |

## 新增/修改文件清单

```
include/SZ3/
├── qoi/                          (新目录, Layer 0+1)
│   ├── QoI.hpp                   (QoIIf 接口)
│   ├── QoIXLin.hpp               (默认 f(x)=x)
│   ├── QoIX2.hpp                 (f(x)=x²)
│   └── QoIIf.hpp                 (工厂)
├── quantizer/
│   ├── Quantizer.hpp             (修改：新增 QpetQntIf)
│   ├── EBLogQnt.hpp              (新文件)
│   └── QpetQnt.hpp               (新文件)
├── decomposition/
│   └── QpetBlockDecomp.hpp       (新文件)
├── api/impl/
│   └── SZAlgoLorenzoReg.hpp      (修改：conf.qoi 分支)
└── utils/
    └── Config.hpp                (修改：qoi 字段)
```

## v1 范围

- 仅实现 `ALGO_LORENZO_REG` 路径的 QPET 支持
- `qoi=0`（QoI_XLin）和 `qoi=1`（QoI_X2）均走 `QpetQnt + QpetBlockDecomp`
- 不保留传统 `LinearQnt + BlockDecomp` 路径——这是新压缩器
- 不做 auto-tuning、不做插值预测器 QPET、不做分块编码
