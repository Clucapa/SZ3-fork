# 函数嵌套 — 模块实现

## 1. QoI_Compose 类

文件：`include/SZ3/qoi/QoI_Compose.hpp`

```cpp
template <class T, uint N>
class QoI_Compose : public concepts::QoIIf<T, N> {
public:
    QoI_Compose(std::shared_ptr<concepts::QoIIf<T, N>> outer,
                std::shared_ptr<concepts::QoIIf<T, N>> inner,
                double tol, T geb)
        : outer_(std::move(outer)), inner_(std::move(inner)),
          tol_(tol), geb_(geb) {}

    double eval(T val) const override {
        return outer_->eval(inner_->eval(val));
    }

    T interpret_eb(T x) const override {
        double h = std::max(1e-8, 1e-6 * std::fabs(static_cast<double>(x)));
        double inner_x = inner_->eval(x);
        double f_prime = (outer_->eval(static_cast<T>(inner_x + h)) -
                          outer_->eval(static_cast<T>(inner_x - h))) / (2 * h);
        double g_prime = (inner_->eval(static_cast<T>(x + h)) -
                          inner_->eval(static_cast<T>(x - h))) / (2 * h);
        double deriv = f_prime * g_prime;
        T eb = (deriv != 0) ? static_cast<T>(tol_ / std::fabs(deriv)) : geb_;
        return std::min(eb, geb_);
    }

    bool check_comply(T orig, T dec) const override {
        return std::fabs(eval(orig) - eval(dec)) <= tol_;
    }

    std::unique_ptr<concepts::EBProvider<T>> create_eb_provider(
            const Config &conf) override {
        return std::make_unique<PointwiseEBProvider<T>>(
            conf.ebs.data(), conf.ebs.size());
    }

    T get_geb() const override { return geb_; }
    void set_geb(T eb) override { geb_ = eb; }
    double get_tol() const override { return tol_; }
    void set_tol(double t) override { tol_ = t; }

private:
    std::shared_ptr<concepts::QoIIf<T, N>> outer_, inner_;
    double tol_;
    T geb_;
};
```

**关键点**：
- create_eb_provider → PointwiseEBProvider（Compose 是 pointwise，不走 budget tracking）
- `interpret_eb` 在 deriv=0 时返回 geb（安全，不触发 unpred）
- eval 链式调用 outer(inner(x))

---

## 2. 内部 XLin 恒等 elision

在 `QoI_Compose` 外部的 `parse_composition` 或 `make_compose` 工厂函数中做：

```cpp
template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> make_compose(
        std::shared_ptr<concepts::QoIIf<T, N>> outer,
        std::shared_ptr<concepts::QoIIf<T, N>> inner,
        double tol, T geb) {
    // elision: f(x)∘identity → f
    if (inner->get_tol() == tol && inner->get_geb() == geb) {
        if (auto xlin = dynamic_cast<QoI_XLin<T, N>*>(inner.get())) {
            if (xlin->A() == 1.0 && xlin->B() == 0.0)
                return outer;
        }
    }
    // elision: identity∘g(x) → g
    if (outer->get_tol() == tol && outer->get_geb() == geb) {
        if (auto xlin = dynamic_cast<QoI_XLin<T, N>*>(outer.get())) {
            if (xlin->A() == 1.0 && xlin->B() == 0.0)
                return inner;
        }
    }
    return std::make_shared<QoI_Compose<T, N>>(
        std::move(outer), std::move(inner), tol, geb);
}
```

**最外层不变**：`GetQOI(conf)` 对 `conf.qoi == 0` 和 `conf.qoi == 1` 的硬编码 dispatch 保持返回 `QoI_XLin` / `QoI_X2` 实例，不走 elision。

---

## 3. 递归下降解析

在 `detail` 命名空间新增：

```cpp
template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> parse_composition(
        const std::vector<int> &ids, size_t &pos,
        ParamReader &params, double tol, T geb) {
    int nib = ids[pos++];
    if (nib == 0xE) {
        auto outer = parse_composition<T, N>(ids, pos, params, tol, geb);
        auto inner = parse_composition<T, N>(ids, pos, params, tol, geb);
        return make_compose<T, N>(std::move(outer), std::move(inner), tol, geb);
    }
    return make_base_qoi<T, N>(nib, params, tol, geb);
}
```

---

## 4. 流水线融合

`parse_qoi_nibbles` 的 nibble 范围检查放宽：

```diff
- } else if (nib >= 0x0 && nib <= 0xD) {
+ } else if ((nib >= 0x0 && nib <= 0xB) || nib == 0xE) {
```

`assemble_group` 检测 E 并分流：

```cpp
template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> assemble_group(
        const QoIGroup &group, ParamReader &params,
        double tol, T geb) {
    auto &ids = group.func_ids;
    for (int nib : ids)
        if (nib == 0xE) {
            size_t pos = 0;
            return parse_composition<T, N>(ids, pos, params, tol, geb);
        }

    size_t n = ids.size();
    if (n == 0) return nullptr;
    if (n == 1) return make_base_qoi<T, N>(ids[0], params, tol, geb);
    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> funcs;
    for (int fid : ids)
        funcs.push_back(make_base_qoi<T, N>(fid, params, tol, geb));
    return std::make_shared<QoI_SumQoI<T, N>>(
        std::move(funcs), tol, geb);
}
```

---

## 5. Edge Cases

| 场景 | nibble | 行为 |
|------|--------|------|
| 单 E 无操作数 | `0xE` | parse 读 E → pos 越界 → throw |
| E 只有 1 操作数 | `0x2E` | E → outer=2 → inner 读越界 → throw |
| 深度嵌套 | `0x0E1E2E3` | Compose(0, Compose(1, Compose(2, 3))) |
| multi-group + E | `0x01EF3E4` | 组1: Compose(1,0); 组2: Compose(4,3); MultiQoI 包裹 |
| interp 路径 | — | 和 blockwise 相同（pointwise → PointwiseEBProvider） |
