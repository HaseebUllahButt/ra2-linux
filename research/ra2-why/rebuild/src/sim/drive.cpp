#include "drive.h"
#include "distance.h"
#include "fp.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace ra2::sim {
namespace {
std::uint32_t read32(std::ifstream& file,std::uint32_t address) {
    file.clear(); file.seekg(address-0x400000u);
    unsigned char b[4]; file.read(reinterpret_cast<char*>(b),4);
    if (!file) throw std::runtime_error("truncated original drive table");
    return b[0]|static_cast<std::uint32_t>(b[1])<<8|static_cast<std::uint32_t>(b[2])<<16|static_cast<std::uint32_t>(b[3])<<24;
}
int signed32(std::uint32_t n) {
    return n<=0x7fffffffu?static_cast<int>(n):static_cast<int>(static_cast<std::int64_t>(n)-0x100000000LL);
}
constexpr int directions[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}; // 0049F2F0.
float single_zero(double value) {
    float result=static_cast<float>(value);
    if (std::abs(static_cast<double>(result))>std::abs(value)) result=std::nextafter(result,0.0f);
    return result;
}
}
DriveTables DriveTables::from_executable(const std::string& path) {
    // Check the exact supported helper signatures before reading static drive data.
    (void)OriginalDistance::from_executable(path);
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("cannot read original drive tables");
    // Supported images have equal RVA and raw offsets in these .text/.rdata ranges.
    const bool base=read32(file,0x4ba410)==0x042444ddu && read32(file,0x4ba414)==0xa73815d8u;
    const auto tracks=base?0x7a00a0u:0x7e7a28u;
    const auto controls=base?0x7a0180u:0x7e7b28u;
    const auto rdata_first=base?0x79a000u:0x7e1000u;
    const auto rdata_end=base?0x7c9000u:0x812000u;
    DriveTables out; out.edition_=base?DriveEdition::BaseRa2:DriveEdition::YurisRevenge;
    for (unsigned curve=1;curve<=2;++curve) {
        const auto address=read32(file,tracks+curve*16);
        if (address<rdata_first || address>=rdata_end) throw std::runtime_error("original drive curve pointer is outside verified .rdata");
        bool ended=false;
        for (unsigned i=0;i<256;++i) {
            if (address+i*12+12>rdata_end) throw std::runtime_error("original drive curve exceeds .rdata");
            DrivePoint point{signed32(read32(file,address+i*12)),signed32(read32(file,address+i*12+4)),signed32(read32(file,address+i*12+8))};
            if (std::abs(static_cast<std::int64_t>(point.x))>512 || std::abs(static_cast<std::int64_t>(point.y))>512 || point.facing<0 || point.facing>255)
                throw std::runtime_error("invalid original drive curve sample");
            out.curves_[curve-1].push_back(point);
            if (i && !point.x && !point.y) { ended=true; break; }
        }
        if (!ended) throw std::runtime_error("unterminated original drive curve");
    }
    if (out.curves_[0].size()!=24 || out.curves_[1].size()!=32 || out.curves_[0][0].y!=245 || out.curves_[1][0].x!=-248)
        throw std::runtime_error("unsupported original straight drive geometry");
    for (unsigned d=0;d<8;++d) {
        const auto row=controls+d*9*12;
        const auto first=read32(file,row)&255u;
        if (first<1 || first>2 || read32(file,row+4)!=d*32)
            throw std::runtime_error("unsupported original straight drive control row");
        out.curve_for_direction_[d]=first-1;
    }
    return out;
}
const std::vector<DrivePoint>& DriveTables::straight(unsigned facing) const {
    if (facing>=256 || facing%32) throw std::invalid_argument("original adjacent drive requires an aligned eight-way facing");
    return curves_[curve_for_direction_[facing/32]];
}
DriveIdleMission original_armed_drive_idle(const DriveIdleInputs& in) {
    if (in.house_iq<in.guard_area_iq && !in.ability_guard_area && !in.default_to_guard_area)
        return DriveIdleMission::Guard;
    if (in.slot_5d4_nonzero) return DriveIdleMission::Guard;
    if (in.edition==DriveEdition::YurisRevenge && (in.slot_2dc_nonzero || in.slot_2d8_nonzero || in.slot_2e4_nonzero))
        return DriveIdleMission::Guard;
    return DriveIdleMission::AreaGuard;
}
int original_rule_speed(int authored) {
    if (authored==-1) throw std::invalid_argument("unresolved retained original Speed");
    return std::min(std::clamp(authored,0,100)*256/100,255);
}
DriveSegment DriveSegment::begin(Coord source,Coord destination,unsigned facing,int speed,const DriveTables& tables) {
    (void)tables.straight(facing);
    const auto direction=facing/32;
    if (static_cast<std::int64_t>(destination.x)-source.x!=directions[direction][0]*256 || static_cast<std::int64_t>(destination.y)-source.y!=directions[direction][1]*256 || source.z!=destination.z)
        throw std::invalid_argument("only an aligned adjacent flat original drive segment is recovered");
    if (speed<1 || speed>255) throw std::invalid_argument("original drive speed is outside 1..255");
    DriveSegment out; out.current=source; out.goal=destination; out.facing=facing; out.speed=speed;
    out.motion.anchor=destination; out.motion.destination=destination;
    out.motion.track=static_cast<int>(direction*9); out.motion.segment_active=true; out.active=true;
    return out;
}
void DriveSegment::tick(const DriveTables& tables) {
    if (!active) return;
    const auto& path=tables.straight(facing);
    int budget=motion.remainder+speed;
    while (budget>7) {
        budget-=7;
        if (static_cast<std::size_t>(motion.sample)==path.size()-1) {
            // 004B0F20 terminal marker: place at the anchor and end this segment.
            // 004B1FD0..004B1FFB adds the remaining Manhattan distance correction.
            const int distance=std::abs(goal.x-current.x)+std::abs(goal.y-current.y);
            const int correction=static_cast<int>(fp::multiply(fp::add(1.0,-fp::multiply(distance,1.0/11.0)),7.0));
            current=goal; active=false; motion.segment_active=false; motion.anchor.reset(); motion.track=-1; motion.sample=0; motion.remainder=budget+correction;
            return;
        }
        const auto& p=path.at(static_cast<std::size_t>(motion.sample));
        const auto point=motion.transform({p.x,p.y,0},static_cast<std::uint8_t>(p.facing));
        current.x=point.x; current.y=point.y; ++motion.sample;
    }
    motion.remainder=budget;
    if (budget && static_cast<std::size_t>(motion.sample)<path.size()-1) {
        const auto& p=path.at(static_cast<std::size_t>(motion.sample));
        const auto target=motion.transform({p.x,p.y,0},static_cast<std::uint8_t>(p.facing));
        const float fraction=single_zero(fp::multiply(budget,1.0/7.0)); // Original double at 007E7FA8.
        Coord proposed=current;
        proposed.x+=static_cast<int>(fp::multiply(target.x-current.x,fraction));
        proposed.y+=static_cast<int>(fp::multiply(target.y-current.y,fraction));
        const auto same_cell=[](Coord a,Coord b){return a.x/256==b.x/256 && a.y/256==b.y/256;};
        const Coord next{target.x,target.y,current.z};
        // 004B22CC..004B23F5 prevents an interpolated diagonal point entering a third cell.
        if (same_cell(proposed,current) || same_cell(proposed,next)) current=proposed;
        else if (budget>3) current=next;

    }
}
}
