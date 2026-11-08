#include "combat.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ra2::sim {
namespace {
float single_toward_zero(long double value) {
    float out=static_cast<float>(value);
    if (std::abs(static_cast<long double>(out))>std::abs(value)) out=std::nextafter(out,0.0f);
    return out;
}
double double_toward_zero(long double value) {
    double out=static_cast<double>(value);
    if (std::abs(static_cast<long double>(out))>std::abs(value)) out=std::nextafter(out,0.0);
    return out;
}
double multiply_toward_zero(double a,double b) {
    const double rounded=a*b;
    const double error=std::fma(a,b,-rounded);
    if ((rounded>0 && error<0) || (rounded<0 && error>0)) return std::nextafter(rounded,0.0);
    return rounded;
}
int truncate(long double value) {
    if (!std::isfinite(value) || value<std::numeric_limits<int>::min() || value>std::numeric_limits<int>::max())
        throw std::range_error("00489180 integer conversion overflow");
    const auto integer=std::trunc(value);
    if (value!=integer && std::abs(value-std::round(value))<=std::ldexp(std::max(1.0L,std::abs(value)),-62))
        throw std::range_error("00489180 x87 rounding boundary needs an exact x87 backend");
    return static_cast<int>(integer);
}
}
int total_damage(const DamageInputs& in) {
    if (!in.damage || in.scenario_inert || !in.warhead_present) return 0;
    if (in.damage<0) return in.distance<8?in.damage:0;
    if (!std::isfinite(in.cell_spread) || !std::isfinite(in.percent_at_max) || !std::isfinite(in.armor_verse) || in.maximum<0)
        throw std::invalid_argument("00489180 needs finite resolved warhead inputs");
    // 004891C6 stores the initial damage and scaled damage as single precision.
    const float initial=single_toward_zero(in.damage);
    // Control word 00000E7F selects 53-bit arithmetic and rounding toward zero.
    const float scaled=single_toward_zero(double_toward_zero(static_cast<long double>(in.damage)*in.percent_at_max));
    const int radius=truncate(static_cast<long double>(in.cell_spread)*256);
    int damage=in.damage;
    if (scaled!=initial && radius) {
        // Limit the supported interpolation to exact binary division.
        // Other radii need faithful x87 rounding at every operation.
        if (radius<0 || (radius&(radius-1)) || radius>16777216 || in.damage>16777216 || in.distance<0 || in.distance>radius*2)
            throw std::range_error("00489180 interpolation outside verified binary-radius domain");
        const auto difference=double_toward_zero(static_cast<long double>(initial)-scaled);
        const auto multiplied=double_toward_zero(static_cast<long double>(difference)*(radius-in.distance));
        const auto divided=double_toward_zero(static_cast<long double>(multiplied)/radius);
        damage=truncate(double_toward_zero(static_cast<long double>(divided)+scaled));
    }
    damage=std::max(damage,0);
    return std::min(truncate(multiply_toward_zero(damage,in.armor_verse)),in.maximum);
}
std::optional<std::uint32_t> select_resolved_table_target(const std::vector<ResolvedCandidate>& candidates) {
    int best=-1; std::optional<std::uint32_t> target;
    for (const auto& candidate:candidates) if (candidate.eligible && candidate.score>best) {
        if (!candidate.id) throw std::invalid_argument("native target ID zero is reserved");
        best=candidate.score; target=candidate.id;
    }
    return target;
}
std::uint32_t threat_categories(std::uint32_t threat) {
    std::uint32_t categories=(threat&0x100)?0x8042:0;
    if (threat&4) categories|=4;
    if (threat&0x1ba60) categories|=0x40;
    if (threat&8) categories|=0x8000;
    if (threat&0x50) categories|=2;
    return categories;
}
}
