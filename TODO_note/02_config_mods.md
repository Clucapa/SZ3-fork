# Config.hpp 修改

## 原则

- **不新增** `ALGO` 枚举值。`conf.qoi` 选择 QoI 类型。
- 文件格式始终为 `[qi_eb|qi_data]`。
- `qoi=0` 时走 `QoI_XLin(f(x)=x)` + `QpetQnt`，行为等价标准 SZ3，格式统一。

## 新增成员变量

```cpp
class Config {
    // ... 现有字段 ...

    int    qoi = 0;       // QoI 类型: 0=f(x)=x, 1=x²
    double qEB = 1.0;     // QoI 容差 τ
    double qEBase = 1e-15; // eb 对数最小可分辨值
    double qELogB = 2;    // eb 对数底
    int    qR = 128;      // eb 对数量化级数 (radius_eb)

    std::vector<double> ebs;  // 预计算逐点 eb (不序列化)
};
```

## save() / load()

在现有字段之后追加（顺序一致）：

```cpp
// save:
write(qoi, c);
write(qEB, c);
write(qEBase, c);
write(qELogB, c);
write(qR, c);

// load (注意 c < c1 检查):
read(qoi, c);
read(qEB, c);
read(qEBase, c);
read(qELogB, c);
read(qR, c);
```

## loadcfg() / save_ini()

新增 `[QoISettings]` 小节：

```ini
[QoISettings]
qoi = 0             ; 0=f(x)=x (默认), 1=x²
qoiEB = 1.0         ; QoI tolerance τ
qoiEBBase = 1e-15   ; eb 对数最小可分辨值
qoiEBLogBase = 2    ; eb 对数底
qoiQuantbinCnt = 32 ; eb 对数量化级数
```

`load_ini()` 解析对应的 key，`save_ini()` 对应输出。

## 注意事项

- `ebs` 不在 `save()/load()` 中序列化——压缩端由 QoI 预计算、解压端从 qi_eb 恢复
- 所有新增字段默认值保证 `qoi=0` 时行为完全向后兼容
