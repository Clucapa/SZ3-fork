# 基础函数参数化 — 模块实现

## 1. Config 改动

```cpp
// Config.hpp — 新增字段
std::string qoiParams;     // base64-encoded double[]; empty = no params

// Config.hpp — 删除字段
double qEBase = 1e-15;     // ✗ 删除
double qELogB = 2;         // ✗ 删除
```

### 1.1 INI 解析

```cpp
// load_ini, [QoISettings] 区
if (eq(key, "qoiParams"))
    qoiParams = value;
```

### 1.2 save_ini

```cpp
ss << "qoiParams = " << qoiParams << "\n";
```

### 1.3 save 序列化

```cpp
// 写 uint32 长度 + base64 字符串
uint32_t len = static_cast<uint32_t>(qoiParams.size());
write(len, c);
if (len) write(qoiParams.data(), len, c);
```

### 1.4 load 反序列化

```cpp
uint32_t len = 0;
read(len, c);
if (len) {
    qoiParams.assign(reinterpret_cast<const char*>(c), len);
    c += len;
}
```

### 1.5 删除 qEBase/qELogB 的序列化

从 `save()` 和 `load()` 中移除：

```cpp
// 之前
write(qEBase, c);
write(qELogB, c);
// 之后 — 删除
```

`load()` 的对应 `c < c1` 保护长度判断自动适配，旧文件读取时 payload 提前结束，不会越界。

---

## 2. base64 decode 函数

内联在 `QoIIf.hpp` 的 `detail` 命名空间中：

```cpp
namespace detail {

inline std::vector<unsigned char> base64_decode(const std::string &in) {
    if (in.empty()) return {};

    auto val = [](char c) -> unsigned char {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0xFF;
    };

    std::vector<unsigned char> out;
    out.reserve((in.size() * 3) / 4);
    for (size_t i = 0; i + 3 < in.size() && in[i + 3] != '='; i += 4) {
        auto a = val(in[i]), b = val(in[i + 1]);
        auto c = val(in[i + 2]), d = val(in[i + 3]);
        out.push_back((a << 2) | (b >> 4));
        out.push_back((b << 4) | (c >> 2));
        out.push_back((c << 6) | d);
    }
    // handle trailing "=" padding (1 or 2 chars)
    // ... (standard base64 padding handling)

    // validate size is double-aligned
    // ...
    return out;
}

}
```

---

## 3. ParamReader 类

```cpp
class ParamReader {
public:
    explicit ParamReader(const std::vector<unsigned char> &data)
        : data_(data), pos_(0) {}

    double read() {
        if (pos_ + sizeof(double) > data_.size())
            return std::numeric_limits<double>::quiet_NaN();
        double v;
        std::memcpy(&v, data_.data() + pos_, sizeof(double));
        pos_ += sizeof(double);
        return v;
    }

private:
    const std::vector<unsigned char> &data_;
    size_t pos_;
};
```

**关键设计**：ParamReader 持有 `const vector<uchar>&`，decode 后的数据存在工厂函数的局部变量中，ParamReader 不拥有数据。

---

## 4. 参数化 make_base_qoi

```cpp
template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> make_base_qoi(
        int nib, ParamReader &params, double tol, T geb) {
    auto read_default = [&](double def) {
        double v = params.read();
        return std::isnan(v) ? def : v;
    };
    switch (nib) {
        case 0x0:
            return std::make_shared<QoI_XLin<T, N>>(
                tol, geb, read_default(1.0), read_default(0.0));
        case 0x1:
            return std::make_shared<QoI_X2<T, N>>(tol, geb);
        case 0x2:
            return std::make_shared<QoI_XCubic<T, N>>(tol, geb);
        case 0x3:
            return std::make_shared<QoI_XSqrt<T, N>>(tol, geb);
        case 0x4:
            return std::make_shared<QoI_XExp<T, N>>(
                tol, geb, read_default(std::exp(1.0)));
        case 0x5:
            return std::make_shared<QoI_XLogX<T, N>>(tol, geb);
        case 0x6:
            return std::make_shared<QoI_LogX<T, N>>(
                tol, geb, read_default(std::exp(1.0)));
        case 0x7:
            return std::make_shared<QoI_XRecip<T, N>>(tol, geb);
        case 0x8:
            return std::make_shared<QoI_XAbs<T, N>>(tol, geb);
        case 0x9:
            return std::make_shared<QoI_XSin<T, N>>(tol, geb);
        case 0xA:
            return std::make_shared<QoI_XTanh<T, N>>(tol, geb);
        case 0xB:
            return std::make_shared<QoI_XPower<T, N>>(
                tol, geb, read_default(2.0));
        default:
            throw std::invalid_argument(
                "Unknown nibble id: " + std::to_string(nib));
    }
}
```

**默认值规则**：
- 有参数的函数：`qoiParams` 中有值 → 读取；否则用硬编码默认
- 无参数的函数（XCubic, XSqrt, XLogX, XRecip, XAbs, XSin, XTanh）：不调用 `params.read()`，不改构造签名

---

## 5. 顶层流水线

```cpp
template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> assemble_from_nibbles(
        const Config &conf) {
    auto groups = parse_qoi_nibbles(conf.qoi);
    if (groups.empty()) return nullptr;

    // base64 decode 一次，共享于整个 assembly
    auto decoded = base64_decode(conf.qoiParams);
    ParamReader params(decoded);

    if (groups.size() == 1)
        return assemble_group<T, N>(groups[0], params,
                                    conf.qEB, conf.absErrorBound);

    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> grp_vec;
    for (auto &g : groups) {
        auto q = assemble_group<T, N>(g, params, conf.qEB, conf.absErrorBound);
        if (q) grp_vec.push_back(std::move(q));
    }
    return std::make_shared<QoI_MultiQoI<T, N>>(std::move(grp_vec));
}
```

`decoded` 是 `assemble_from_nibbles` 的局部变量，ParamReader 引用它。当 `assemble_from_nibbles` 返回后，decoded 被析构，但 QoI 对象不持有指向它的引用（参数在构造时已读取完毕）。

---

## 6. 各 Base QoI 构造器改动

### XLin

```cpp
QoI_XLin(double tol, T geb, double A = 1.0, double B = 0.0)
    : tol(tol), geb(geb), A_(A), B_(B) { id = 0; }

double eval(T val) const override {
    return A_ * static_cast<double>(val) + B_;
}

T interpret_eb(T) const override {
    // d(Ax+B)/dx = A
    return std::min(static_cast<T>(tol / std::fabs(A_)), geb);
}
```

### XExp

```cpp
QoI_XExp(double tol, T geb, double base = std::exp(1.0))
    : tol_(tol), geb_(geb), base_(base) { id = 4; }

double eval(T val) const override {
    return std::pow(base_, static_cast<double>(val));
}
// interpret_eb 保持数值导数（通过 eval）
```

### LogX

```cpp
QoI_LogX(double tol, T geb, double base = std::exp(1.0))
    : tol_(tol), geb_(geb), base_(base) { id = 6; }

double eval(T val) const override {
    return std::log(static_cast<double>(val)) / std::log(base_);
}
```

### XPower

```cpp
QoI_XPower(double tol, T geb, double expo = 2.0)
    : tol_(tol), geb_(geb), expo_(expo) { id = 0xB; }

double eval(T val) const override {
    return std::pow(static_cast<double>(val), expo_);
}
```

---

## 7. 向后兼容

`qoiParams = ""`（空串）时：
- `base64_decode` 返回空 vector
- `ParamReader::read()` 总是返回 NaN
- 所有函数使用硬编码默认值
- 等价于改动前行为

已有测试无需修改。

---

## 8. 编译 & 测试

**新增/修改文件清单见 12_01_plan.md。**

**新增测试**：

| Test | 内容 |
|------|------|
| `Param.XLinDefault` | qoiParams="" → f(x)=x |
| `Param.XLinCustom` | base64([2,1.5]) → f(x)=2x+1.5 |
| `Param.XExpDefault` | qoiParams="" → eˣ |
| `Param.XExpCustom` | base64([10]) → 10ˣ |
| `Param.GroupParams` | qoi=0x401, base64([2,1.5,10]) → XLin(2,1.5)+10ˣ+XSquare |
| `Param.ComposeParams` | qoi=0x04E, base64([3]) → Compose(Exp(3), Square) |
| `Param.Base64Roundtrip` | encode→decode→比较 double[] 一致性 |
