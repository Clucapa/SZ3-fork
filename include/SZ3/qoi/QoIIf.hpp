#ifndef SZ3_QOI_INFO_HPP
#define SZ3_QOI_INFO_HPP

#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/QoIXLin.hpp"
#include "SZ3/qoi/QoIX2.hpp"
#include "SZ3/qoi/QoI_XCubic.hpp"
#include "SZ3/qoi/QoI_XSqrt.hpp"
#include "SZ3/qoi/QoI_XExp.hpp"
#include "SZ3/qoi/QoI_XLogX.hpp"
#include "SZ3/qoi/QoI_LogX.hpp"
#include "SZ3/qoi/QoI_XRecip.hpp"
#include "SZ3/qoi/QoI_XAbs.hpp"
#include "SZ3/qoi/QoI_XSin.hpp"
#include "SZ3/qoi/QoI_XTanh.hpp"
#include "SZ3/qoi/QoI_XPower.hpp"
#include "SZ3/qoi/QoI_SumQoI.hpp"
#include "SZ3/qoi/QoI_MultiQoI.hpp"
#include "SZ3/qoi/QoI_Compose.hpp"
#include "SZ3/qoi/QoI_FX.hpp"
#include "SZ3/qoi/RegionalMean.hpp"
#include "SZ3/qoi/RegionalMeanSq.hpp"
#include "SZ3/qoi/QoI_RegionalAvgInterp.hpp"
#include "SZ3/qoi/QoI_RegionalMeanSqInterp.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

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
    out.reserve((in.size() * 3) / 4 + 1);

    size_t i = 0;
    for (; i + 4 <= in.size(); i += 4) {
        unsigned char a = val(in[i]);
        unsigned char b = val(in[i + 1]);
        if (a == 0xFF || b == 0xFF) break;
        out.push_back((a << 2) | (b >> 4));

        if (in[i + 2] == '=') {
            out.push_back((b << 4));
            break;
        }
        unsigned char c = val(in[i + 2]);
        if (c == 0xFF) break;
        out.push_back((b << 4) | (c >> 2));

        if (in[i + 3] == '=') break;
        unsigned char d = val(in[i + 3]);
        if (d == 0xFF) break;
        out.push_back((c << 6) | d);
    }

    return out;
}

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

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> make_base_qoi(
        int nib, ParamReader &params, double tol, T geb) {
    auto rd = [&](double def) {
        double v = params.read();
        return std::isnan(v) ? def : v;
    };
    switch (nib) {
        case 0x0: {
            double A = rd(1.0);
            double B = rd(0.0);
            return std::make_shared<QoI_XLin<T, N>>(tol, geb, A, B);
        }
        case 0x1:
            return std::make_shared<QoI_X2<T, N>>(tol, geb);
        case 0x2:
            return std::make_shared<QoI_XCubic<T, N>>(tol, geb);
        case 0x3:
            return std::make_shared<QoI_XSqrt<T, N>>(tol, geb);
        case 0x4: {
            double B = rd(std::exp(1.0));
            return std::make_shared<QoI_XExp<T, N>>(tol, geb, B);
        }
        case 0x5:
            return std::make_shared<QoI_XLogX<T, N>>(tol, geb);
        case 0x6: {
            double B = rd(std::exp(1.0));
            return std::make_shared<QoI_LogX<T, N>>(tol, geb, B);
        }
        case 0x7:
            return std::make_shared<QoI_XRecip<T, N>>(tol, geb);
        case 0x8:
            return std::make_shared<QoI_XAbs<T, N>>(tol, geb);
        case 0x9:
            return std::make_shared<QoI_XSin<T, N>>(tol, geb);
        case 0xA:
            return std::make_shared<QoI_XTanh<T, N>>(tol, geb);
        case 0xB: {
            double E = rd(2.0);
            return std::make_shared<QoI_XPower<T, N>>(tol, geb, E);
        }
        default:
            throw std::invalid_argument(
                "Unknown nibble function id: " + std::to_string(nib));
    }
}

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> make_compose(
        std::shared_ptr<concepts::QoIIf<T, N>> outer,
        std::shared_ptr<concepts::QoIIf<T, N>> inner,
        double tol, T geb) {
    if (auto xl = dynamic_cast<QoI_XLin<T, N>*>(inner.get())) {
        if (xl->A() == 1.0 && xl->B() == 0.0) return outer;
    }
    if (auto xl = dynamic_cast<QoI_XLin<T, N>*>(outer.get())) {
        if (xl->A() == 1.0 && xl->B() == 0.0) return inner;
    }
    return std::make_shared<QoI_Compose<T, N>>(
        std::move(outer), std::move(inner), tol, geb);
}

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> parse_composition(
        const std::vector<int> &ids, size_t &pos,
        ParamReader &params, double tol, T geb) {
    if (pos >= ids.size())
        throw std::invalid_argument("Unexpected end of composition group");
    int nib = ids[pos++];
    if (nib == 0xE) {
        auto outer = parse_composition<T, N>(ids, pos, params, tol, geb);
        auto inner = parse_composition<T, N>(ids, pos, params, tol, geb);
        return make_compose<T, N>(std::move(outer), std::move(inner), tol, geb);
    }
    return make_base_qoi<T, N>(nib, params, tol, geb);
}

struct QoIGroup {
    std::vector<int> func_ids;
};

inline std::vector<QoIGroup> parse_qoi_nibbles(int qoi) {
    std::vector<QoIGroup> groups;
    QoIGroup cur;
    while (qoi) {
        int nib = qoi & 0xF;
        qoi >>= 4;
        if (nib == 0xF) {
            if (!cur.func_ids.empty()) {
                groups.push_back(cur);
                cur = QoIGroup{};
            }
        } else if ((nib >= 0x0 && nib <= 0xB) || nib == 0xE) {
            cur.func_ids.push_back(nib);
        } else {
            throw std::invalid_argument("Unknown nibble: " + std::to_string(nib));
        }
    }
    if (!cur.func_ids.empty()) groups.push_back(cur);
    return groups;
}

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> assemble_group(
        const QoIGroup &group, ParamReader &params,
        double tol, T geb) {
    auto &ids = group.func_ids;
    for (int nib : ids) {
        if (nib == 0xE) {
            size_t pos = 0;
            return parse_composition<T, N>(ids, pos, params, tol, geb);
        }
    }
    size_t n = ids.size();
    if (n == 0) return nullptr;
    if (n == 1) return make_base_qoi<T, N>(ids[0], params, tol, geb);
    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> funcs;
    for (int fid : ids)
        funcs.push_back(make_base_qoi<T, N>(fid, params, tol, geb));
    return std::make_shared<QoI_SumQoI<T, N>>(std::move(funcs), tol, geb);
}

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> assemble_from_nibbles(
        const Config &conf) {
    auto groups = parse_qoi_nibbles(conf.qoi);
    if (groups.empty()) return nullptr;

    auto decoded = base64_decode(conf.qoiParams);
    ParamReader params(decoded);

    if (groups.size() == 1)
        return assemble_group<T, N>(groups[0], params,
                                    conf.qEB, conf.absErrorBound);

    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> grp_vec;
    for (auto &g : groups) {
        auto q = assemble_group<T, N>(g, params,
                                      conf.qEB, conf.absErrorBound);
        if (q) grp_vec.push_back(std::move(q));
    }
    return std::make_shared<QoI_MultiQoI<T, N>>(std::move(grp_vec));
}

}  // namespace detail

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> GetQOI(const Config &conf) {
    // FX mode: high nibble = 0x7
    if (((conf.qoi >> 28) & 0xF) == 7) {
        auto raw = detail::base64_decode(conf.qoiParams);
        return std::make_shared<QoI_FX<T, N>>(conf.qEB, conf.absErrorBound, raw);
    }

    if (conf.qoi < 0) {
        int rid = ~conf.qoi;
        switch (rid) {
            case 0:
                return std::make_shared<QoI_RegionalMean<T, N>>(conf.qEB, conf.absErrorBound);
            case 1:
                return std::make_shared<QoI_RegionalMeanSq<T, N>>(conf.qEB, conf.absErrorBound);
            case 2:
                return std::make_shared<QoI_RegionalAvgInterp<T, N>>(conf.qEB, conf.absErrorBound);
            case 3:
                return std::make_shared<QoI_RegionalMeanSqInterp<T, N>>(conf.qEB, conf.absErrorBound);
            default:
                return nullptr;
        }
    }
    if (conf.qoi == 0)
        return std::make_shared<QoI_XLin<T, N>>(conf.qEB, conf.absErrorBound);
    if (conf.qoi == 1)
        return std::make_shared<QoI_X2<T, N>>(conf.qEB, conf.absErrorBound);
    return detail::assemble_from_nibbles<T, N>(conf);
}

}  // namespace SZ3

#endif
