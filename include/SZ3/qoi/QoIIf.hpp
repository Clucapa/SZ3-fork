#ifndef SZ3_QOI_INFO_HPP
#define SZ3_QOI_INFO_HPP

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
#include "SZ3/qoi/RegionalMean.hpp"
#include "SZ3/qoi/RegionalMeanSq.hpp"
#include "SZ3/qoi/QoI_RegionalAvgInterp.hpp"
#include "SZ3/qoi/QoI_RegionalMeanSqInterp.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

namespace detail {

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> make_base_qoi(int nib, double tol, T geb) {
    switch (nib) {
        case 0x0: return std::make_shared<QoI_XLin<T, N>>(tol, geb);
        case 0x1: return std::make_shared<QoI_X2<T, N>>(tol, geb);
        case 0x2: return std::make_shared<QoI_XCubic<T, N>>(tol, geb);
        case 0x3: return std::make_shared<QoI_XSqrt<T, N>>(tol, geb);
        case 0x4: return std::make_shared<QoI_XExp<T, N>>(tol, geb);
        case 0x5: return std::make_shared<QoI_XLogX<T, N>>(tol, geb);
        case 0x6: return std::make_shared<QoI_LogX<T, N>>(tol, geb);
        case 0x7: return std::make_shared<QoI_XRecip<T, N>>(tol, geb);
        case 0x8: return std::make_shared<QoI_XAbs<T, N>>(tol, geb);
        case 0x9: return std::make_shared<QoI_XSin<T, N>>(tol, geb);
        case 0xA: return std::make_shared<QoI_XTanh<T, N>>(tol, geb);
        case 0xB: return std::make_shared<QoI_XPower<T, N>>(tol, geb);
        default:
            throw std::invalid_argument(
                "Unknown nibble function id: " + std::to_string(nib));
    }
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
        } else if (nib >= 0x0 && nib <= 0xD) {
            cur.func_ids.push_back(nib);
        } else {
            throw std::invalid_argument("Unknown nibble: " + std::to_string(nib));
        }
    }
    if (!cur.func_ids.empty()) groups.push_back(cur);
    return groups;
}

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> assemble_from_nibbles(
        const Config &conf) {
    auto groups = parse_qoi_nibbles(conf.qoi);

    if (groups.empty()) return nullptr;

    if (groups.size() == 1) {
        size_t n = groups[0].func_ids.size();
        if (n == 0) return nullptr;
        if (n == 1) {
            return make_base_qoi<T, N>(groups[0].func_ids[0],
                                       conf.qEB, conf.absErrorBound);
        }
        std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> funcs;
        for (int fid : groups[0].func_ids)
            funcs.push_back(make_base_qoi<T, N>(fid, conf.qEB,
                                                 conf.absErrorBound));
        return std::make_shared<QoI_SumQoI<T, N>>(
            std::move(funcs), conf.qEB, conf.absErrorBound);
    }

    std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> grp_vec;
    for (auto &g : groups) {
        size_t n = g.func_ids.size();
        if (n == 0) continue;
        if (n == 1) {
            grp_vec.push_back(make_base_qoi<T, N>(g.func_ids[0],
                                                   conf.qEB, conf.absErrorBound));
        } else {
            std::vector<std::shared_ptr<concepts::QoIIf<T, N>>> funcs;
            for (int fid : g.func_ids)
                funcs.push_back(make_base_qoi<T, N>(fid, conf.qEB,
                                                     conf.absErrorBound));
            grp_vec.push_back(std::make_shared<QoI_SumQoI<T, N>>(
                std::move(funcs), conf.qEB, conf.absErrorBound));
        }
    }
    return std::make_shared<QoI_MultiQoI<T, N>>(std::move(grp_vec));
}

}  // namespace detail

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> GetQOI(const Config &conf) {
    if (conf.qoi <= 0xD && conf.qoi >= 0) {
        switch (conf.qoi) {
            case 0:
                return std::make_shared<QoI_XLin<T, N>>(conf.qEB, conf.absErrorBound);
            case 1:
                return std::make_shared<QoI_X2<T, N>>(conf.qEB, conf.absErrorBound);
            case 10:
                return std::make_shared<QoI_RegionalMean<T, N>>(conf.qEB, conf.absErrorBound);
            case 11:
                return std::make_shared<QoI_RegionalMeanSq<T, N>>(conf.qEB, conf.absErrorBound);
            case 12:
                return std::make_shared<QoI_RegionalAvgInterp<T, N>>(conf.qEB, conf.absErrorBound);
            case 13:
                return std::make_shared<QoI_RegionalMeanSqInterp<T, N>>(conf.qEB, conf.absErrorBound);
            default:
                return nullptr;
        }
    }
    return detail::assemble_from_nibbles<T, N>(conf);
}

}  // namespace SZ3

#endif
