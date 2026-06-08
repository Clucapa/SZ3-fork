#ifndef SZ3_QOI_INFO_HPP
#define SZ3_QOI_INFO_HPP

#include <memory>
#include "SZ3/qoi/QoI.hpp"
#include "SZ3/qoi/QoIXLin.hpp"
#include "SZ3/qoi/QoIX2.hpp"
#include "SZ3/utils/Config.hpp"

namespace SZ3 {

template <class T, uint N>
std::shared_ptr<concepts::QoIIf<T, N>> GetQOI(const Config &conf) {
    switch (conf.qoi) {
        case 0:
            return std::make_shared<QoI_XLin<T, N>>(conf.qEB, conf.absErrorBound);
        case 1:
            return std::make_shared<QoI_X2<T, N>>(conf.qEB, conf.absErrorBound);
        default:
            return nullptr;
    }
}

}  // namespace SZ3

#endif
