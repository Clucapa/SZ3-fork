# Encoder Regional 支持

## 当前状态

encoder **没有** Regional 编码能力。`argc=2`，只接受一个表达式，输出 pointwise qoi（nibble / FX / iso6）。

CLI 用法：

```bash
./qoi_encoder "sqr+cubic"              → 0x12           (nibble pointwise)
./qoi_encoder "sin(x)+x^2"             → 0x70000000     (FX pointwise, SymEngine fallback)
./qoi_encoder "iso6(sqr, -5, 5, 3, 0.01)" → 0x60000001  (isoline mode 6)
```

## 设计方案

### 不加新语法，用 `--regional` flag 控制

保持 `argc==2` 单表达式语法。`--regional` 是输出模式开关：把 pointwise 编码结果（nibble 或 FX）**包装**为 Regional 格式。

```
argc=2, expr="sqr+cubic"     → encode(expr) → nibble qoi=0x12
argc=3, --regional "sqr+cubic" → encode(expr) → nibble qoi=0x12 → ~(marker | 0x12)
```

### 包装逻辑

```cpp
// pointwise_qoi = encode(expr) 的结果，或 fx_encode(expr) 的结果
int make_regional(int pointwise_qoi, bool is_interp, bool is_fx) {
    int raw;
    if (is_fx) {
        raw = 0x30000000;                    // Block + FX
        if (is_interp) raw = 0x70000000;     // Interp + FX
    } else {
        raw = pointwise_qoi & 0x0FFFFFFF;    // 复用低 28 bit 的 nibble 编码
        if (is_interp) raw |= 0x40000000;    // 设 bit30
    }
    return ~raw;
}
```

`--regional --interp` 组合启用 Interp 路径。无 `--interp` 默认 Block。

### 调用格式

```bash
# nibble Regional
./qoi_encoder --regional "sqr+cubic"
  → qoi = ~(0x00000012) = 0xFFFFFFED

./qoi_encoder --regional "lin(2,0.5)+sqr"
  → qoi = ~(0x00000201), qoiParams = base64([2.0, 0.5])

# FX Regional
./qoi_encoder --regional "sin(x)+x^2"
  → qoi = ~(0x30000000), qoiParams = FX binary

# Interp Regional
./qoi_encoder --regional --interp "sqr"
  → qoi = ~(0x40000001)
```

### 实现

```cpp
int main(int argc, char **argv) {
    bool is_regional = false, is_interp = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--regional")) is_regional = true;
        else if (!strcmp(argv[i], "--interp")) is_interp = true;
    }
    // ... extract expression from last non-flag arg ...

    // iso6 + regional doesn't make sense
    // try nibble → try FX → get pointwise qoi/qoiParams
    // if is_regional, wrap qoi with make_regional()
}
```

### 优点

- **零新编码逻辑**：`encode()` 和 `fx_encode()` 完全复用，Regional 只是在上层翻转 bit
- **统一调用格式**：所有模式共用 `expr` 参数，Regional/Interp 是 flag 修饰
- **和已有的 `--interp-only` / `--block-only` e2e flag 命名一致**
