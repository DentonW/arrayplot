#pragma once

// Eigen support for arrayplot. Include <Eigen/Core> (or <Eigen/Dense>)
// *before* this header. Kept separate from arrayplot.h so the base client
// has no Eigen dependency for callers who only plot plain arrays.
//
// Watch/Immediate window: the generic template below can't be called
// directly (the evaluator doesn't deduce template arguments), so prefer the
// concrete overloads further down for interactive use, e.g.
// `aplot::plot("J", jacobian)` where jacobian is an Eigen::MatrixXd.

#include "arrayplot.h"

namespace aplot {
namespace detail {

// Works with Eigen::Matrix, Eigen::Array, Eigen::Vector, and arbitrary
// expressions (blocks, transposes, products, ...). Expressions aren't
// guaranteed to be contiguous in memory, so this evaluates into a plain,
// densely-packed temporary before sending.
template <typename Derived>
inline void plot_dense(const char* name, const Eigen::DenseBase<Derived>& expr) {
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

} // namespace detail

// Generic path for any Eigen expression. From code this covers everything
// (blocks, transposes, products, fixed-size types, ...); from the Watch
// window it requires an explicit template argument, e.g.
// `aplot::plot<Eigen::MatrixXd>("name", m)`.
template <typename Derived>
inline void plot(const char* name, const Eigen::DenseBase<Derived>& expr) {
    detail::plot_dense(name, expr);
}

// Concrete (non-template) overloads for the common dynamic-size types --
// these let `aplot::plot("name", myMatrix)` be called directly from the
// Watch/Immediate window with no explicit template argument.
inline void plot(const char* name, const Eigen::MatrixXd& m) { detail::plot_dense(name, m); }
inline void plot(const char* name, const Eigen::MatrixXf& m) { detail::plot_dense(name, m); }
inline void plot(const char* name, const Eigen::VectorXd& v) { detail::plot_dense(name, v); }
inline void plot(const char* name, const Eigen::VectorXf& v) { detail::plot_dense(name, v); }
inline void plot(const char* name, const Eigen::ArrayXXd& a) { detail::plot_dense(name, a); }
inline void plot(const char* name, const Eigen::ArrayXXf& a) { detail::plot_dense(name, a); }

} // namespace aplot
