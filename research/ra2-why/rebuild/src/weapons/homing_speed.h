#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace ra2::weapons {
struct HomingSpeedState {
    std::array<double,3> velocity{}; // Bullet +D0, +D8, +E0.
    std::int32_t limit=0;           // +F8, signed.
    std::uint8_t flag=0;            // +ED; preserve nonzero byte values.
    std::int32_t counter=0;         // +F0, signed.
};
struct HomingSpeedInput {
    std::int32_t acceleration=0; // BulletType +2A0.
    std::int32_t threshold=0;    // BulletType +2B0.
    std::int32_t frame=0;        // 00A40D2C.
};

// Base game.exe 0045F8FF..0045FB0A only. Caller requires ROT > 0,
// Arcing=false. This does not steer, move, or select a target.
// Arithmetic matches x87 E7F (53 bits, toward zero). Host must use
// ordinary nearest rounding and no fast-math. Unsupported numeric input
// throws before state changes. Zero and fractional velocities are supported;
// nonzero squared lengths must convert to a normal finite float.
class BaseHomingSpeed {
public:
    static BaseHomingSpeed from_executable(const std::string& path);
    void tick(HomingSpeedState& state,const HomingSpeedInput& input) const;
private:
    double square_root(double squared) const;
    std::array<std::uint32_t,16384> table_{};
};
}
