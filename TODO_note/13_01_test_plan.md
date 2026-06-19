# 端到端 QoI 测试套件 ✅ 已完成

## 当前状态

文件结构：

```
test/
├── CMakeLists.txt       # 链接 SZ3，ctest 入口 (e2e_fast + e2e_compose)
├── e2e_main.cpp          # CLI 主入口 + 参数化循环
├── data_gen.hpp          # D1~D8 合成数据（1D/2D/3D）
├── test_config.hpp       # 19 QoI 注册表 + 硬编码 eval 函数 + Config 工厂
├── verify.hpp            # roundtrip_compress, check_hardcoded_comply, max_abs_err
└── encoder_tests.hpp     # 27 个 encoder roundtrip 条目（表达式→encode()→压缩→硬编码验证）
```

## 测试矩阵

| 维度 | 内容 | 计数 |
|------|------|------|
| QOI 矩阵 | 19 QoI × 8 模式 × 3 维 × 5 算法变体 | 1672 |
| Encoder roundtrip | 27 表达式 × 2 数据 × 2 算法 | 104 |
| **总计 (`--full`)** | | **1776** |
| `--fast` (CI) | | 1232 |
| `--compose` | 仅 encoder roundtrip | 104 |

算法变体：Block, Interp-Cubic, Interp-Linear, ILorenzo-Cubic, ILorenzo-Linear。

## CLI

```bash
./test/bin/e2e              # --full：1776 条
./test/bin/e2e --fast       # CI 门禁：1232 条 (~0.3s)
./test/bin/e2e --compose    # Encoder roundtrip：104 条
./test/bin/e2e --interp-only / --block-only
```

## 关键设计决策

| 决策 | 原因 |
|------|------|
| **全部用硬编码 f(x) 验证** | 独立于 qoi->eval / check_comply，防止 encoder/压缩器/QOI 类的 bug 互相掩盖 |
| **QoiDef 含 feval 和 feval2** | MultiQoI 两组独立检查 |
| **Regional 用 check_regional_aggregate** | 逐点 check_comply 对 regional 无意义，聚合约束直接从 raw data 计算 |
| **Encoder 测试调用 encode() 运行时** | 表达式→qoi+qoiParams→压缩器，一轮透测 |
| **不依赖 GTest** | 编译速度，纯 C 风格报告 |

## 已发现的关键 bug

| Bug | 根因 | 修复 |
|-----|------|------|
| SZDispatcher qoi>=0 走旧 Interp 路径 | `if (conf.qoi < 0)` 分支错误 | 统一 `_qpet` |
| QpetInterpDecomp 缺 precompress_block | Regional EB provider 未初始化 | 加 `precompress_block` |
| QpetInterpDecomp 缺 check_comply 兜底 | Interp 无安全网 | 加 `check_comply` 回退 |
| QpetInterpDecomp eb 取值依赖 advance() 顺序 | PointwiseEBProvider 顺序假设不适用 stride 访问 | 改为 `interpret_eb()` |
| eb 引用污染导致 lossless fallback 失效 | `qnt_eb(&eb)` 修改 eb 后传给 `qnt_overwrite` | 临时量 `static_cast<T>(0)` |
| RegMeanSqInterp 无 budget tracking | interpret_eb 只返回 geb | 补齐 budget tracking |
| QoI_XRecip eval() 1e-15 guard 掩盖过零点违规 | eval 将 |x|<1e-15 映射为 0 | override check_comply |

## 实现步骤

- [x] 1. 创建 `test/` 目录
- [x] 2. `data_gen.hpp`：D1~D8
- [x] 3. `test_config.hpp`：QoI 配置
- [x] 4. `e2e_main.cpp`：CLI + 参数化循环 + 硬编码验证
- [x] 5. `--fast` 通过
- [x] 6. `--full` 通过 (1776/0/272)
- [x] 7. CI 集成（`[test]` fast, `[full]` full）

## 待扩展

| 项 | 状态 |
|----|------|
| FX 表达式 encoder roundtrip 测试 | ⬜ 依赖 12_04 FX |
| 单元素/非 2 的幂 1D 边界测试 | ⬜ |
| `interpAnchorStride > 0` 显式测试 | ⬜ |
