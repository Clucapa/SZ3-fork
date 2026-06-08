# 模块边界与移植规则

## 依赖树

```
def.hpp
 ├─ Quantizer.hpp (QuantizerIf, QpetQntIf)
 │    ├─ EBLogQnt.hpp        ← 只 #include Quantizer.hpp, cmath
 │    └─ QpetQnt.hpp         ← 只 #include Quantizer.hpp, EBLogQnt.hpp, cmath
 ├─ Predictor.hpp (PredictorIf)  [已有，不动]
 ├─ QoI.hpp (QoIIf)
 │    ├─ QoIXLin.hpp         ← 只 #include QoI.hpp, cmath
 │    ├─ QoIX2.hpp           ← 只 #include QoI.hpp, cmath
 │    └─ QoIIf.hpp           ← 只 #include QoI.hpp, QoIXLin.hpp, QoIX2.hpp
 ├─ Decomposition.hpp (DecompIf)  [已有，不动]
 └─ block_data (BlockwiseIterator.hpp)  [已有，不动]

QpetBlockDecomp.hpp           ← #include Decomposition.hpp, Predictor.hpp,
                                   QoI.hpp, Quantizer.hpp, block_data

SZAlgoLorenzoReg.hpp (修改)    ← #include 以上全部 + Config + Encoder + Lossless
```

## 各层文件清单

| 文件 | 层 | 新增/修改 | 可移植 |
|------|:--:|:---------|:--:|
| `quantizer/Quantizer.hpp` | L0 | 修改：新增 QpetQntIf | ✗ (修改已有) |
| `qoi/QoI.hpp` | L0 | 新增 | ✓ |
| `qoi/QoIXLin.hpp` | L1 | 新增 | ✓ |
| `qoi/QoIX2.hpp` | L1 | 新增 | ✓ |
| `qoi/QoIIf.hpp` | L1 | 新增 | ✓ |
| `quantizer/EBLogQnt.hpp` | L1 | 新增 | ✓ |
| `quantizer/QpetQnt.hpp` | L1 | 新增 | ✓ |
| `decomposition/QpetBlockDecomp.hpp` | L2 | 新增（替代 BlockwiseDecomp） | ✓ |
| `utils/Config.hpp` | L3 | 修改 | ✗ |
| `api/impl/SZAlgoLorenzoReg.hpp` | L3 | 修改 | ✗ |
| `api/impl/SZDispatcher.hpp` | L3 | 修改 | ✗ |

## 移植规则

**"可移植"** = 复制到未来 SZ3 版本时，只需该版本的以下接口保持兼容：

| 接口 | 契约要求 |
|------|----------|
| `QuantizerIf<Ti,To>` | `qnt_overwrite(Ti&,Ti)→To`, `recv(Ti,To)→Ti`, `get_out_range()`, `save/load`, `pre/post` 生命周期 |
| `QpetQntIf<Ti,To>` | 继承 QuantizerIf + `qnt_overwrite(Ti&,Ti,Ti)→To`, `recv(Ti,To,Ti)→Ti`, `flush(vector<To>&)`, `recv_eb(To)→Ti` |
| `PredictorIf<T,N>` | `predict(blk_iter,T*,array<size_t,N>)→T`, `pre/post_compress`, `save/load` |
| `QoIIf<T,N>` | `interpret_eb(T)→T`, `check_comply(T,T)→bool`, `get_geb/set_geb/set_tol` |
| `DecompIf<Ti,To,N>` | `compress(Config&,Ti*)→vector<To>`, `decompress(Config&,vector<To>&,Ti*)→Ti*`, `save/load`, `get_out_range` |

**"不可移植"** = 直接引用 `SZ3::Config` 的具体字段名、`block_data` 的具体迭代器 API、或 `SZDispatcher` 的调度逻辑。这些在新版本中可能变化。

## 移植操作清单

将 QPET 迁移到新 SZ3 版本时：

1. 复制 L0+L1+L2 文件（7 个 `.hpp`）
2. 如果 `Quantizer.hpp` 的接口签名有变化 → 微调 `QpetQntIf` 和 `QpetQnt`
3. 如果 `Decomposition.hpp` 的接口签名有变化 → 微调 `QpetBlockDecomp`
4. 如果 `PredictorIf` 的接口签名有变化 → 微调 `QpetBlockDecomp`
5. 重写 `SZAlgoLorenzoReg.hpp` 的 `conf.qoi` 分支 + `SZDispatcher.hpp` 的 eb 预计算
6. 重写 `Config.hpp` 的新增字段
