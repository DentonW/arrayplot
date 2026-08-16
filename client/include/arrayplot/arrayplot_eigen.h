#pragma once

// Eigen support for arrayplot. Include <Eigen/Core> (or <Eigen/Dense>)
// *before* this header. Kept separate from arrayplot.h so the base client
// has no Eigen dependency for callers who only plot plain arrays.

#include "arrayplot.h"

namespace aplot {

// Works with Eigen::Matrix, Eigen::Array, Eigen::Vector, and arbitrary
// expressions (blocks, transposes, products, ...). Expressions aren't
// guaranteed to be contiguous in memory, so this evaluates into a plain,
// densely-packed temporary before sending.
template <typename Derived>
inline void plot(const char* name, const Eigen::DenseBase<Derived>& expr) {
    using PlainType = typename Derived::PlainObject;
    using Scalar = typename PlainType::Scalar;
    static_assert(std::is_same<Scalar, double>::value || std::is_same<Scalar, float>::value,
                  "arrayplot: only float/double Eigen types are supported");

    PlainType m = expr;

    if (m.rows() == 1 || m.cols() == 1) {
        plot1d(name, m.data(), static_cast<size_t>(m.size()));
    } else {
        constexpr bool rowMajor = PlainType::IsRowMajor;
        plot2d(name, m.data(), static_cast<size_t>(m.rows()), static_cast<size_t>(m.cols()), rowMajor);
    }
}

} // namespace aplot
