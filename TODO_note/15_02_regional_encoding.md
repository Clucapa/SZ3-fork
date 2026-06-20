# Regional 编码格式详解

## 翻转前语义位布局

```
bit:  31 30 29 28 27 ... 0
      [0][B][FX][FX] [nibble_qoi]

B:   0 = Block, 1 = Interp
FX:  00 = nibble 函数, 11 = FX 函数
     (01, 10 预留)
```

翻转后存储：`conf.qoi = ~raw`

## 编码类型表

| 类型 | 翻转前 raw | 翻转后 conf.qoi | 高nibble(翻转后) | qoiParams |
|------|-----------|-----------------|:--:|------|
| RegionalMean(nibble) | `0x0nnnnnnn` | `0xFn¬n¬n¬n¬n` | F | nibble params |
| RegionalMean(FX) | `0x30nnnnnnn` | `0xCn¬n¬n¬n¬n` | C | FX binary |
| RegionalInterp(nibble) | `0x40nnnnnnn` | `0xBn¬n¬n¬n¬n` | B | nibble params |
| RegionalInterp(FX) | `0x70nnnnnnn` | `0x8n¬n¬n¬n¬n` | 8 | FX binary |

(¬ 表示位翻转)

## nibble 模式 (FX=00)

低 28 bit 使用和 pointwise mode 0 完全相同的 nibble 编码。

```
Regional(lin(2,0.5) + sqr):
  翻转前: 0x00000120  (nibbles: [0, 2, 1] = lin + sqr)
  翻转后: ~0x00000120 = 0xFFFFFEDF
  qoiParams = base64([2.0, 0.5])
```

解码：
1. `raw = ~conf.qoi = 0x00000120`
2. `nib_qoi = raw & 0x0FFFFFFF = 0x120`
3. `parse_qoi_nibbles(0x120)` → [{func_ids:[2,1]}, {func_ids:[0]}]  → `sqr, lin`
4. 从 qoiParams 消费参数: A=2.0, B=0.5
5. 构造 `RegionalMean(sqr + lin(2,0.5))`，Block 路径

## FX 模式 (FX=11)

```
RegionalFX(sin(x)+x²):
  翻转前: 0x30000000
  翻转后: ~0x30000000 = 0xCFFFFFFF
  qoiParams = FX binary (f_str|df_str|ddf_str)
```

低 28 bit 全零作为标记。qoiParams 和 pointwise FX mode 7 格式完全一致。

## 工厂分发伪码

```cpp
if (conf.qoi < 0) {
    int raw = ~conf.qoi;
    bool is_interp = (raw >> 30) & 1;
    bool use_fx    = ((raw >> 28) & 0x3) == 0x3;
    int  nib_qoi   = raw & 0x0FFFFFFF;

    if (use_fx) {
        // 解析 conf.qoiParams → f(x) TinyExpr → RegionalFX 实例
        return makeRegionalFX(conf, is_interp);
    }

    // nibble 模式
    auto groups = parse_qoi_nibbles(nib_qoi);
    if (groups.empty())
        groups.push_back(QoIGroup{});  // 退化为 XLin(1,0)
    ParamReader params(conf.qoiParams);
    auto sub_qoi = groups.size() == 1
        ? assemble_group(groups[0], params, conf.qEB, conf.absErrorBound)
        : ...;  // MultiQoI

    if (is_interp)
        return makeRegionalInterp(sub_qoi);
    else
        return makeRegionalMean(sub_qoi);
}
```

## 与 pointwise 编码的对称性

| | Pointwise | Regional |
|---|---|---|
| 基础格式 | nibble 低28bit | 同 |
| FX 模式 | 高nibble 7 | 翻转后高nibble C/8 |
| Isoline | 高nibble 6 | 不需要 |
| 参数传递 | qoiParams raw binary | 同 |
| 空编码 | qoi=0 → XLin(1,0) | 翻转后=0xFFFFFFF → nibble空 → XLin(1,0) |
