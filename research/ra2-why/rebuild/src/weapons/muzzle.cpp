#include "muzzle.h"
#include <sim/fp.h>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ra2::weapons {
namespace {
#include "../voxel/base_facing.inc"
#include "muzzle_turn.inc"
float single(double value) {
    if (!std::isfinite(value) || std::abs(value)>10000000) throw std::range_error("muzzle coordinate");
    float result=static_cast<float>(value);
    if (std::abs(static_cast<double>(result))>std::abs(value)) result=std::nextafter(result,0.0f);
    return result;
}
}
std::optional<sim::Coord> base_flat_unit_muzzle(const FlatUnitMuzzleInput& in) {
    if (!in.flat_drive_locomotor || !*in.flat_drive_locomotor || in.burst_index<0 ||
        std::abs(static_cast<long long>(in.turret_offset))>10000) return {};
    for (int value:in.fire_flh) if (std::abs(static_cast<long long>(value))>10000) return {};
    const auto direction=[](std::uint16_t facing) {return (((unsigned(facing)>>10)+1)>>1)&31;};
    const auto hull=direction(in.hull_facing), turret=direction(in.turret_facing);
    try {
        const double c=base_facing[hull][0],s=base_facing[hull][1];
        const auto& rotation=turn[static_cast<int>(turret)-static_cast<int>(hull)+31];
        const double rc=rotation[0],rs=rotation[1];
        const double m00=single(sim::fp::add(sim::fp::multiply(c,rc),sim::fp::multiply(-s,rs)));
        const double m01=single(sim::fp::add(sim::fp::multiply(-s,rc),-sim::fp::multiply(c,rs)));
        const double m10=single(sim::fp::add(sim::fp::multiply(s,rc),sim::fp::multiply(c,rs)));
        const double m11=single(sim::fp::add(sim::fp::multiply(c,rc),-sim::fp::multiply(s,rs)));
        const double x=in.fire_flh[0],y=(in.burst_index%2?-1:1)*in.fire_flh[1],z=in.fire_flh[2];
        // 00590090 stores each translation as a single after every call.
        const auto tx=single(sim::fp::multiply(c,in.turret_offset));
        const auto ty=single(sim::fp::multiply(s,in.turret_offset));
        const auto fx=single(sim::fp::add(sim::fp::add(sim::fp::multiply(m01,y),sim::fp::multiply(m00,x)),tx));
        const auto fy=single(sim::fp::add(sim::fp::add(sim::fp::multiply(m11,y),sim::fp::multiply(m10,x)),ty));
        const auto fz=single(z);
        const auto sum=[](int a,int b)->std::optional<int> {
            const auto n=static_cast<long long>(a)+b;
            if (n<std::numeric_limits<int>::min() || n>std::numeric_limits<int>::max()) return {};
            return static_cast<int>(n);
        };
        auto ox=sum(in.position.x,static_cast<int>(fx));
        auto oy=sum(in.position.y,-static_cast<int>(fy));
        auto oz=sum(in.position.z,static_cast<int>(fz));
        if (!ox || !oy || !oz) return {};
        return sim::Coord{*ox,*oy,*oz};
    } catch (const std::exception&) { return {}; }
}
}
