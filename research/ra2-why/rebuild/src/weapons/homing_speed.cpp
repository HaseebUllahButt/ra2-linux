#include "homing_speed.h"
#include "sim/fp.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace ra2::weapons {
BaseHomingSpeed BaseHomingSpeed::from_executable(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("cannot read base game.exe");
    constexpr unsigned char signature[]={0xdd,0x44,0x24,0x04,0xd8,0x15,0x38,0xa7,0x79,0x00};
    unsigned char actual[sizeof(signature)]{};
    file.seekg(0x4ba410-0x400000);
    file.read(reinterpret_cast<char*>(actual),sizeof(actual));
    if (!file || !std::equal(std::begin(signature),std::end(signature),actual))
        throw std::runtime_error("unsupported base square root helper");
    BaseHomingSpeed out;
    file.seekg(0x817dfc-0x400000);
    for (auto& entry:out.table_) {
        unsigned char b[4]; file.read(reinterpret_cast<char*>(b),4);
        if (!file) throw std::runtime_error("truncated base square root table");
        entry=std::uint32_t(b[0]) | std::uint32_t(b[1])<<8 |
              std::uint32_t(b[2])<<16 | std::uint32_t(b[3])<<24;
    }
    return out;
}

double BaseHomingSpeed::square_root(double squared) const {
    // 004BA410 uses a float lookup, NOT fsqrt or std::sqrt. Unlike the
    // integer-distance wrapper, negative exponents must use floor division.
    if (!squared) return 0;
    if (!std::isfinite(squared) || squared<0 || squared>std::numeric_limits<float>::max())
        throw std::range_error("homing squared length outside finite float range");
    float single=static_cast<float>(squared);
    if (double(single)>squared) single=std::nextafter(single,0.0f);
    if (!std::isnormal(single)) throw std::range_error("homing squared length outside normal float range");
    std::uint32_t bits; std::memcpy(&bits,&single,4);
    const int exponent=int(bits>>23)-127;
    const auto mantissa=(bits&0x7fffffu) | (exponent%2 ? 0x800000u : 0u);
    const int half=exponent>=0 ? exponent/2 : (exponent-1)/2;
    const auto result_bits=table_[mantissa>>10]+(std::uint32_t(half+127)<<23);
    float result; std::memcpy(&result,&result_bits,4);
    if (!std::isfinite(result) || result<=0) throw std::runtime_error("invalid base square root table");
    return result;
}

void BaseHomingSpeed::tick(HomingSpeedState& state,const HomingSpeedInput& input) const {
    namespace fp=ra2::sim::fp;
    auto next=state;
    auto& v=next.velocity;
    const auto square=[](double n) {return fp::multiply(n,n);};
    // The two original sums use different orders.
    double speed=square_root(fp::add(fp::add(square(v[0]),square(v[1])),square(v[2])));
    if (!input.threshold) {
        if (next.limit>=40 || double(next.limit)<=fp::add(speed,0.5)) next.flag=0;
    } else if (next.counter<input.threshold) {
        ++next.counter; // Cannot overflow: counter < a signed 32-bit threshold.
        if (next.counter>=input.threshold) next.flag=0;
    }
    int acceleration=input.acceleration;
    if (next.flag && !input.threshold) acceleration=input.frame%2==0 ? 1 : 0;
    if (speed!=double(next.limit)) {
        if (speed<double(next.limit)) {
            speed=fp::add(double(acceleration),speed);
            if (speed>=double(next.limit)) speed=double(next.limit);
        } else {
            speed=fp::add(speed,-double(acceleration/2)); // Signed truncation toward zero.
            if (speed<=0) speed=0;
        }
        if (v[0]==0 && v[1]==0 && v[2]==0) v[0]=100;
        const double length=square_root(fp::add(fp::add(square(v[2]),square(v[1])),square(v[0])));
        const double scale=fp::divide(speed,length);
        for (auto& component:v) component=fp::multiply(scale,component);
    }
    state=next;
}
}
