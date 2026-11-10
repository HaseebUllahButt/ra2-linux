#pragma once
#include "ground.h"
#include <array>
#include <string>
namespace ra2::sim {
// Original walking coordinate step, base game Walk Internal_Process
// 0071E220, coordinate step at 0071F2FA..0071F448.
// Arctangent 004BA600 over table 00813DF4; sine 004BA480 and cosine 004BA4D0
// over table 00801DC4; integer truncation helper 0077F110. Read-only input.
// Outside this kernel on purpose: arrival detection, subcell placement and
// the bridge/layer decisions around the step (0071F424.. onward).
class OriginalWalkMath {
public:
    struct Step {
        Coord position;
        std::int16_t facing;
    };
    static OriginalWalkMath from_executable(const std::string& path);
    // One original walk step. Flat branch only: z passes through unchanged.
    // The speed budget is spent once: speed scales the sine and cosine terms,
    // it is not added to both axes.
    Step step(Coord current,Coord destination,int speed) const;
private:
    std::int64_t truncate_to_integer(long double value) const; // 0077F110
    float sine(double radians) const;                          // 004BA480
    float cosine(double radians) const;                        // 004BA4D0
    long double arctangent(double rise,double run) const;      // 004BA600
    std::array<float,0x2800> trig_table_{};   // 00801DC4
    std::array<float,0x1001> atan_table_{};   // 00813DF4
    double rad_scale_{};                      // 0079B620
    double angle_scale_{};                    // 0079B628
    double angle_offset_{};                   // 0079B630
    double pi_{};                             // 0079D158
    float trig_scale_{};                      // 007D8048
    float atan_divisor_{};                    // 00817DF8
    float positive_half_pi_{};                // 007A0F8C
    float negative_half_pi_{};                // 007A0F90
};
}
