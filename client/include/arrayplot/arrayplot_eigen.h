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

#include <complex>
#include <string>

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

// ---- complex matrices ----
//
// The wire protocol only carries real-valued float/double arrays, so a
// complex matrix has to be decomposed into one or more real views. Each of
// these sends its result under "<name> (<view>)", so it shows up as its own
// window in the viewer. Call whichever view(s) you actually want; there's
// no single "right" default (magnitude/phase is the usual choice for
// signal-processing-style data, real/imag for e.g. debugging raw complex
// arithmetic), so `plot()` on a complex matrix sends magnitude and phase --
// call plot_real/plot_imag directly if that's not what you want instead.
template <typename Derived>
inline void plot_real(const char* name, const Eigen::MatrixBase<Derived>& m) {
    detail::plot_dense((std::string(name) + " (real)").c_str(), m.real());
}
template <typename Derived>
inline void plot_imag(const char* name, const Eigen::MatrixBase<Derived>& m) {
    detail::plot_dense((std::string(name) + " (imag)").c_str(), m.imag());
}
template <typename Derived>
inline void plot_magnitude(const char* name, const Eigen::MatrixBase<Derived>& m) {
    detail::plot_dense((std::string(name) + " (mag)").c_str(), m.cwiseAbs());
}
template <typename Derived>
inline void plot_phase(const char* name, const Eigen::MatrixBase<Derived>& m) {
    detail::plot_dense((std::string(name) + " (phase)").c_str(),
                        m.unaryExpr([](const typename Derived::Scalar& c) { return std::arg(c); }));
}

// Concrete overloads so `aplot::plot("name", complexMatrix)` works both from
// code and from the Watch/Immediate window (same reasoning as the real
// overloads above -- no template argument deduction needed).
inline void plot(const char* name, const Eigen::MatrixXcd& m) { plot_magnitude(name, m); plot_phase(name, m); }
inline void plot(const char* name, const Eigen::MatrixXcf& m) { plot_magnitude(name, m); plot_phase(name, m); }
inline void plot(const char* name, const Eigen::VectorXcd& v) { plot_magnitude(name, v); plot_phase(name, v); }
inline void plot(const char* name, const Eigen::VectorXcf& v) { plot_magnitude(name, v); plot_phase(name, v); }

} // namespace aplot
