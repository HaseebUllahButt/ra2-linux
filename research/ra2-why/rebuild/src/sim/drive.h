#pragma once
#include "ground.h"
#include <array>
#include <string>
#include <vector>
namespace ra2::sim {
enum class DriveEdition { BaseRa2, YurisRevenge };
enum class DriveIdleMission { Guard=5, AreaGuard=11 };
struct DriveIdleInputs {
    DriveEdition edition=DriveEdition::BaseRa2;
    int house_iq=0,guard_area_iq=4;
    bool default_to_guard_area=false,ability_guard_area=false;
    bool slot_5d4_nonzero=false,slot_2dc_nonzero=false,slot_2d8_nonzero=false,slot_2e4_nonzero=false;
};
// Armed, ordinary vehicle slice: base 006FD7F2..006FD844; expansion 00738B67..00738C9D.
DriveIdleMission original_armed_drive_idle(const DriveIdleInputs&);
struct DrivePoint { int x,y,facing; };
class DriveTables {
public:
    static DriveTables from_executable(const std::string&);
    const std::vector<DrivePoint>& straight(unsigned facing) const;
    DriveEdition edition() const { return edition_; }
private:
    DriveEdition edition_=DriveEdition::BaseRa2;
    std::array<std::vector<DrivePoint>,2> curves_;
    std::array<unsigned,8> curve_for_direction_{};
};
// 0071465F..00714699. -1 means retained engine value and is not resolved here.
int original_rule_speed(int authored);
struct DriveSegment {
    Coord current,goal;
    GroundMotion motion;
    unsigned facing=0;
    int speed=0;
    bool active=false;
    static DriveSegment begin(Coord source,Coord destination,unsigned facing,int speed,const DriveTables&);
    void tick(const DriveTables&);
};
}
