#ifndef SZ3_QOI_HPP
#define SZ3_QOI_HPP

namespace SZ3::concepts {

template <class T, uint N>
class QoIIf {
public:
    virtual ~QoIIf() = default;

    virtual T interpret_eb(T x) const = 0;

    virtual bool check_comply(T orig, T dec) const = 0;

    virtual T get_geb() const = 0;
    virtual void set_geb(T eb) = 0;

    virtual void set_tol(double tol) = 0;

    int id = 0;
};

}  // namespace SZ3::concepts

#endif
