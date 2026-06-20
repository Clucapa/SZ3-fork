# Regional 编码重新设计 — 设计问题

## 背景

当前 Regional 编码用简单 `~rid`（rid=0/1/2/3），qoiParams 为空。只能表达 `mean(x)` 和 `mean(x²)` 两种约束。RegionalFX 需要任意函数支持。

## Q1. 新编码方案

翻转前语义位布局：

```
bit 31: 固定 0（翻转后为 F，qoi<0 判别 Regional）
bit 30: 0=Block, 1=Interp
bit 29-28: 00=nibble函数, 11=FX函数
bit 27-0: nibble编码（或 FX 时全零）
```

翻转后存入 `conf.qoi`，工厂见 `qoi<0` 提取翻转前语义。

## Q2. nibble 模式下的 qoiParams

和 pointwise mode 0 完全一致：按 nibble 遍历顺序消费函数参数（XLin 的 A/B, XExp 的 base 等）。空串 = 全部默认值。

```
Regional(sqr+cubic): qoi = ~(0x00000012), qoiParams = ""
Regional(lin(2,0.5)+exp(10)): qoi = ~(0x00000140), qoiParams = base64([2.0, 0.5, 10.0])
```

## Q3. FX 模式下的 qoiParams

和 pointwise mode 7 完全一致：3 个 TinyExpr 字符串（f/df/ddf）的二进制序列化。

```
RegionalFX(sin(x)+x²): qoi = ~(0x30000000), qoiParams = base64(f_str|df_str|ddf_str)
```

## Q4. 向后兼容

| 旧编码 | 等价新编码 | 语义 |
|--------|-----------|------|
| `~0` = RegionalMean(Block) | `~(0x00000000)` | nibble 为空组 → XLin(1,0)，Block 路径 |
| `~1` = RegionalMeanSq(Block) | `~(0x00000001)` | nibble=[1]=sqr，Block 路径 |
| `~2` = RegionalAvgInterp | `~(0x40000000)` | nibble 为空 → XLin(1,0)，Interp 路径 |
| `~3` = RegionalMeanSqInterp | `~(0x40000001)` | nibble=[1]=sqr，Interp 路径 |

旧测试用例全部可以无损映射。

## Q5. 工厂分发

```cpp
if (conf.qoi < 0) {
    int raw = ~conf.qoi;
    bool is_interp = (raw >> 30) & 1;
    bool use_fx    = ((raw >> 28) & 0x3) == 0x3;
    int  nib_qoi   = raw & 0x0FFFFFFF;

    if (use_fx) {
        // 从 qoiParams 解析 TinyExpr → 构造 RegionalFX(f_expr, tol, geb)
    } else {
        // 解析 nibble → 构造嵌套 QoI → 包装为 RegionalMean / RegionalMeanSq 变体
    }
}
```

## Q6. 高位 nibble 全景

| 翻转前高 nibble | 翻转后 | 模式 |
|:--:|:--:|------|
| `0x0` | `0xF` | Regional Block + nibble |
| `0x1` | `0xE` | Regional Interp + nibble（预留，bit30=1, bit29-28=00→翻转后E） |
| `0x3` | `0xC` | Regional Block + FX |
| `0x7` | `0x8` | Regional Interp + FX |

当前只实现 `0x0`, `0x3`（=Block nibble/FX）。Interp 变体后续按需添加。

## Q7. test 覆盖

新编码需要 e2e 覆盖以下场景：
- nibble: sqr, cubic, sqr+cubic, sqrt, abs, expx 等通过新编码 → 和旧 `~0`/`~1` 等效
- FX: `sin(x)+x²`, `sqrt(x)` 等任意表达式
- 新旧编码等价性：`~(0x00000001)` 和 `~1` 产生相同 QoI
- Block/Interp 两路
