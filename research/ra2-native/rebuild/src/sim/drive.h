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
struct DriveControl { unsigned primary,secondary,facing,flags; };
class DriveTables {
public:
    static DriveTables from_executable(const std::string&);
    const std::vector<DrivePoint>& straight(unsigned facing) const;
    const std::vector<DrivePoint>& curve(unsigned index) const;
    const DriveControl& control(unsigned track) const;
    DriveEdition edition() const { return edition_; }
private:
    DriveEdition edition_=DriveEdition::BaseRa2;
    std::array<std::vector<DrivePoint>,6> curves_;
    std::array<DriveControl,64> controls_{};
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
    static DriveSegment begin_route(Coord source,Coord destination,unsigned facing,int speed,const std::vector<unsigned>& directions,const DriveTables&);
    void tick(const DriveTables&);
    // Conservative cells crossed by original samples and interpolation between each pair.
    std::vector<std::array<int,2>> swept_cells(const DriveTables&) const;
private:
    bool first_update_=true;
    void prepare(const DriveTables&);
    void follow(const DriveTables&,bool add_speed);
};
}
