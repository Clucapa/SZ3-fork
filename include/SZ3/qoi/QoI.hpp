#ifndef SZ3_QOI_HPP
#define SZ3_QOI_HPP

#include <cmath>
#include <memory>

namespace SZ3 {
class Config;
}  // namespace SZ3

namespace SZ3::concepts {

template <typename T>
class EBProvider;

template <class T, uint N>
class QoIIf {
public:
    virtual ~QoIIf() = default;

    virtual T interpret_eb(T x) const {
        double h = std::max(1e-6, 1e-4 * std::fabs(static_cast<double>(x)));
        double deriv = (eval(static_cast<T>(x + h)) -
                        eval(static_cast<T>(x - h))) / (2 * h);
        if (std::fabs(deriv) < 1e-15) return get_geb();
        T eb = static_cast<T>(get_tol() / std::fabs(deriv));
        return std::min(eb, get_geb());
    }

    virtual bool check_comply(T orig, T dec) const {
        return std::fabs(eval(orig) - eval(dec)) <= get_tol() * (1.0 + 1e-12);
    }

    virtual void precompress_block(size_t num_elements) {}
    virtual void update_tolerance(T orig, T dec) {}
    virtual void postcompress_block() {}

    virtual T get_geb() const = 0;
    virtual void set_geb(T eb) = 0;
    virtual double get_tol() const = 0;
    virtual void set_tol(double tol) = 0;

    virtual double eval(T val) const {
        return static_cast<double>(val);
    }

    virtual std::unique_ptr<EBProvider<T>> create_eb_provider(
        const Config &conf) = 0;

    virtual bool is_pointwise() const { return id >= 0; }

    virtual bool has_bias() const { return false; }
    virtual void precompute_data(const T *, size_t) {}
    virtual T    get_bias(size_t) const { return 0; }

    int id = 0;
};

}  // namespace SZ3::concepts

#endif
