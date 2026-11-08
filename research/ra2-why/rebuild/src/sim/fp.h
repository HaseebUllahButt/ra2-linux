#pragma once
#include <cmath>
#include <cfenv>
#ifdef __FAST_MATH__
#error "Original arithmetic does not support fast-math builds"
#endif
#include <stdexcept>
namespace ra2::sim::fp {
// Match x87 control E7F: 53-bit precision, rounding toward zero.
// Error-free residuals assume the host's ordinary round-to-nearest environment.
inline void domain(double x) {
    if (std::fegetround()!=FE_TONEAREST) throw std::runtime_error("native x87 helpers require the ordinary nearest host rounding mode");
    if (!std::isfinite(x) || std::abs(x)>1e100 || (x && std::abs(x)<1e-100))
        throw std::range_error("x87 arithmetic outside verified normal numeric domain");
}
inline double adjust(double rounded,double residual) {
    domain(rounded);
    if ((rounded>0 && residual<0) || (rounded<0 && residual>0)) return std::nextafter(rounded,0.0);
    return rounded;
}
inline double add(double a,double b) {
    domain(a); domain(b);
    const double rounded=a+b;
    const double virtual_b=rounded-a;
    const double residual=(a-(rounded-virtual_b))+(b-virtual_b);
    return adjust(rounded,residual);
}
inline double multiply(double a,double b) {
    domain(a); domain(b);
    const double rounded=a*b;
    return adjust(rounded,std::fma(a,b,-rounded));
}
inline double divide(double a,double b) {
    domain(a); domain(b);
    if (!b) throw std::domain_error("x87 division by zero");
    const double rounded=a/b;
    const double residual=std::fma(-rounded,b,a);
    return adjust(rounded,b<0?-residual:residual);
}
}
