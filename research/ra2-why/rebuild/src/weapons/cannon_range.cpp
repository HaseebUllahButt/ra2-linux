#include "cannon_range.h"
#include "weapons.h"
#include <sim/fp.h>
#include <cmath>
#include <stdexcept>
namespace ra2::weapons {
CannonRangeStatus base_cannon_range(const CannonRangeInput& in,const sim::OriginalDistance& distance) {
    using S=CannonRangeStatus;
    if (!in.plain_cannon_and_ground || !*in.plain_cannon_and_ground ||
        in.weapon_range<0 || in.weapon_range>100000 || in.minimum_range<0 ||
        !std::isfinite(in.gravity) || in.gravity<=0 || in.gravity>1000)
        return S::NeedsInput;
    const auto delta=[](int a,int b)->std::optional<int> {
        auto d=static_cast<long long>(a)-b;
        if (d < -100000 || d > 100000) return {};
        return static_cast<int>(d);
    };
    auto dx=delta(in.target.x,in.origin.x),dy=delta(in.target.y,in.origin.y),dz=delta(in.target.z,in.origin.z);
    if (!dx || !dy || !dz) return S::NeedsInput;
    try {
        if (in.minimum_range && distance.between(in.origin,in.target)<in.minimum_range)
            return S::OutOfRange;
        const int horizontal=distance.between(in.origin,in.target,true);
        if (horizontal>in.weapon_range) return S::OutOfRange;
        const int speed=base_ballistic_launch_speed(in.weapon_range,in.gravity,distance);
        // Original 0047FBA0 discriminant and first solution. The second
        // solution cannot be positive when the first solution is negative.
        const auto speed2=sim::fp::multiply(speed,speed);
        const auto distance2=sim::fp::multiply(horizontal?static_cast<double>(horizontal):0.001,
                                                horizontal?static_cast<double>(horizontal):0.001);
        const auto term=sim::fp::multiply(sim::fp::multiply(speed2,*dz),in.gravity);
        const auto discriminant=sim::fp::add(
            sim::fp::add(sim::fp::multiply(speed2,speed2),-sim::fp::add(term,term)),
            -sim::fp::multiply(sim::fp::multiply(in.gravity,in.gravity),distance2));
        if (discriminant<0) return S::OutOfRange;
        const auto numerator=sim::fp::add(speed2,-sim::fp::multiply(*dz,in.gravity));
        if (numerator<0) {
            if (discriminant>0 && discriminant<1) return S::NeedsInput;
            if (sim::fp::add(distance.square_root(discriminant),numerator)<0)
                return S::OutOfRange;
        }
        if (!in.line_blocked) return S::NeedsInput;
        return *in.line_blocked?S::OutOfRange:S::InRange;
    } catch (const std::exception&) { return S::NeedsInput; }
}
}
