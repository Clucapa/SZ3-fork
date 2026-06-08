# QoI 模块（Layer 0+1，可移植）

## 文件

```
include/SZ3/qoi/
├── QoI.hpp       (Layer 0: QoIIf 接口)
├── QoIXLin.hpp   (Layer 1: 默认 f(x)=x)
├── QoIX2.hpp     (Layer 1: f(x)=x²)
└── QoIIf.hpp     (Layer 1: 工厂)
```

## QoIIf<T, N> 接口 (QoI.hpp)

```cpp
namespace SZ3::concepts {
template <class T, uint N>
class QoIIf {
public:
    virtual ~QoIIf() = default;

    // 由原始值 x 推导该点的 eb
    virtual T interpret_eb(T x) const = 0;

    // 验证: |f(dec) - f(orig)| ≤ τ
    virtual bool check_comply(T orig, T dec) const = 0;

    // 全局 eb (用于 capping)
    virtual T get_geb() const = 0;
    virtual void set_geb(T eb) = 0;

    // QoI 容差 τ
    virtual void set_tol(double tol) = 0;

    int id = 0;
};
}
```

`N` 预留用于未来可能需要多维上下文的 QoI（如区域均值），当前实现不使用。

## QoI_XLin<T, N> — 默认实现 (qoi=0, f(x)=x)

**文件**: `QoIXLin.hpp`

```cpp
template <class T, uint N>
class QoI_XLin : public concepts::QoIIf<T, N> {
    double tol;
    T geb;

    QoI_XLin(double t, T g) : tol(t), geb(g) { id = 0; }

    T interpret_eb(T x) const { return std::min((T)tol, geb); }
    // f(x)=x → |dec - orig| ≤ τ
    bool check_comply(T orig, T dec) const {
        return fabs(orig - dec) <= tol;
    }
};
```

当 `qoi=0` 时，`interpret_eb` 对所有点返回 `min(qEB, absEB)`。若 `qEB ≥ absEB`（默认），所有点 eb = absEB，行为等价标准 SZ3 但走 QpetQnt 流程。

## QoI_X2<T, N> — f(x)=x² (qoi=1)

**文件**: `QoIX2.hpp`

由 `|(x±eb)² - x²| ≤ τ` 解出：

```cpp
T interpret_eb(T x) const {
    T eb = -fabs(x) + sqrt(x*x + tol);
    return std::min(eb, geb);
}

bool check_comply(T orig, T dec) const {
    return fabs(orig*orig - dec*dec) <= tol;
}
```

## QoIIf 工厂 (QoIIf.hpp)

```cpp
template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> GetQOI(const Config &conf) {
    switch (conf.qoi) {
        case 0:
            return std::make_shared<QoI_XLin<T, N>>(conf.qEB, conf.absEB);
        case 1:
            return std::make_shared<QoI_X2<T, N>>(conf.qEB, conf.absEB);
        default:
            return nullptr;
    }
}
```

switch-case 结构便于后续扩展新 QoI 类型。

## eb 预计算

由集成层完成，不在 QoI 模块内：

```cpp
conf.ebs.resize(conf.num);
auto qoi = GetQOI<T, N>(conf);
for (size_t i = 0; i < conf.num; ++i)
    conf.ebs[i] = qoi->interpret_eb(data[i]);
```
