#include "drive.h"
#include "distance.h"
#include "fp.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <set>
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
    for (unsigned curve=1;curve<=6;++curve) {
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
    constexpr unsigned lengths[]={24,32,55,39,62,57};
    constexpr int descriptor_fields[][3]={{-1,0,-1},{-1,0,-1},{37,12,22},{26,11,19},{45,15,31},{44,16,27}};
    for (unsigned i=0;i<6;++i) {
        if (out.curves_[i].size()!=lengths[i]) throw std::runtime_error("unsupported original drive curve length");
        for (unsigned j=0;j<3;++j) if (signed32(read32(file,tracks+(i+1)*16+4+j*4))!=descriptor_fields[i][j])
            throw std::runtime_error("unsupported original drive curve descriptor");
    }
    std::uint32_t control_hash=2166136261u;
    for (unsigned i=0;i<64;++i) {
        const auto row=controls+i*12;
        const auto pair=read32(file,row);
        DriveControl control{pair&255u,(pair>>8)&255u,read32(file,row+4),read32(file,row+8)};
        if ((pair>>16) || control.primary>6 || control.secondary>10 || control.facing!=(i%8)*32 || control.flags>15)
            throw std::runtime_error("unsupported original drive control row");
        out.controls_[i]=control;
        for (unsigned j=0;j<3;++j) control_hash=(control_hash^read32(file,row+j*4))*16777619u;
    }
    if (control_hash!=0x3188aa02u) throw std::runtime_error("original drive controls differ from both verified editions");
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
const std::vector<DrivePoint>& DriveTables::curve(unsigned index) const {
    if (index<1 || index>curves_.size()) throw std::out_of_range("original drive curve is not recovered");
    return curves_[index-1];
}
const DriveControl& DriveTables::control(unsigned track) const {
    return controls_.at(track);
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
    return begin_route(source,destination,facing,speed,{direction},tables);
}
DriveSegment DriveSegment::begin_route(Coord source,Coord destination,unsigned facing,int speed,const std::vector<unsigned>& path,const DriveTables& tables) {
    (void)tables.straight(facing);
    if (speed<1 || speed>255) throw std::invalid_argument("original drive speed is outside 1..255");
    if (path.empty() || path.size()>23) throw std::invalid_argument("original resolved path needs 1..23 directions and its end marker");
    if (path.front()!=facing/32) throw std::invalid_argument("initial drive turn is not recovered");
    for (auto direction:path) if (direction>7) throw std::invalid_argument("original route direction must be 0..7; waiting is not recovered");
    std::int64_t x=source.x,y=source.y;
    for (std::size_t i=0;i<path.size();++i) {
        x+=directions[path[i]][0]*256; y+=directions[path[i]][1]*256;
        if (i+1<path.size() && path[i]!=path[i+1]) {
            if (i+2!=path.size() || !(tables.control(path[i]*8+path[i+1]).flags&8))
                throw std::invalid_argument("mid-curve connections and turn-in-place are not recovered");
        }
    }
    if (x!=destination.x || y!=destination.y || source.z!=destination.z)
        throw std::invalid_argument("resolved original path does not reach the requested flat destination");
    DriveSegment out; out.current=source; out.goal=destination; out.facing=facing; out.speed=speed;
    out.motion.destination=destination; out.active=true;
    for (std::size_t i=0;i<path.size();++i) out.motion.path[i]=static_cast<int>(path[i]);
    out.prepare(tables);
    return out;
}
void DriveSegment::prepare(const DriveTables& tables) {
    // Flat clear-cell branch of base 004A2670 and expansion 004B2630.
    const auto first=static_cast<unsigned>(motion.path[0]);
    const auto next=motion.path[1]<0?first:static_cast<unsigned>(motion.path[1]);
    unsigned track=first*8+next;
    if (!tables.control(track).primary) track=first*9;
    motion.track=static_cast<int>(track); motion.sample=0; motion.second_track=false;
    int dx=directions[first][0],dy=directions[first][1];
    if (tables.control(track).flags&8) {
        dx+=directions[next][0]; dy+=directions[next][1];
        // 004B41C7: copy 22 dwords, clear entry 22, and retain entry 23.
        for (unsigned i=0;i<22;++i) motion.path[i]=motion.path[i+2];
        motion.path[22]=-1;
    } else motion.consume_one_path_direction();
    const auto x=static_cast<std::int64_t>(current.x)+dx*256,y=static_cast<std::int64_t>(current.y)+dy*256;
    if (x<std::numeric_limits<int>::min() || x>std::numeric_limits<int>::max() || y<std::numeric_limits<int>::min() || y>std::numeric_limits<int>::max())
        throw std::overflow_error("original route anchor exceeds native coordinate range");
    motion.anchor=Coord{static_cast<int>(x),static_cast<int>(y),current.z}; motion.segment_active=true;
}
std::vector<std::array<int,2>> DriveSegment::swept_cells(const DriveTables& tables) const {
    auto plan=*this;
    std::set<std::array<int,2>> cells;
    Coord previous=current;
    const auto include=[&](Coord point) {
        // The actual fractional move stays within these endpoint bounds (004B22CC..004B23F5).
        for (int y=std::min(previous.y/256,point.y/256);y<=std::max(previous.y/256,point.y/256);++y)
            for (int x=std::min(previous.x/256,point.x/256);x<=std::max(previous.x/256,point.x/256);++x) cells.insert({x,y});
        previous=point;
    };
    include(current);
    for (;;) {
        if (plan.motion.track==-1) plan.prepare(tables);
        const auto& curve=tables.curve(tables.control(static_cast<unsigned>(plan.motion.track)).primary);
        for (std::size_t i=0;i+1<curve.size();++i) {
            const auto& p=curve[i]; const auto transformed=plan.motion.transform({p.x,p.y,0},static_cast<std::uint8_t>(p.facing));
            include({transformed.x,transformed.y,plan.current.z});
        }
        include(plan.motion.anchor.value()); plan.current=plan.motion.anchor.value();
        if (plan.motion.path[0]==-1) break;
        plan.motion.track=-1;
    }
    return {cells.begin(),cells.end()};
}
void DriveSegment::tick(const DriveTables& tables) { tick(tables,{}); }
void DriveSegment::tick(const DriveTables& tables,const std::function<void(Coord,Coord)>& position) { tick(tables,position,{}); }
void DriveSegment::tick(const DriveTables& tables,const std::function<void(Coord,Coord)>& position,
                        const std::function<void(const DriveEvent&)>& event) {
    if (!active) return;
    const bool continue_in_frame=!first_update_ && motion.track!=-1;
    const bool first=first_update_;
    first_update_=false;
    if (motion.track==-1) { prepare(tables);if (event) event({DriveEventKind::Prepare,current,motion}); }
    else if (first && event) event({DriveEventKind::Prepare,current,motion});
    follow(tables,true,position,event);
    // 004B0500 / base 004A05F0: one further preparation and Follow call, without new speed.
    if (continue_in_frame && motion.track==-1 && motion.path[0]!=-1) {
        prepare(tables);if (event) event({DriveEventKind::Prepare,current,motion});follow(tables,false,position,event);
    }
    active=motion.track!=-1 || motion.path[0]!=-1;
    if (!active) motion.destination.reset();
}
void DriveSegment::follow(const DriveTables& tables,bool add_speed,const std::function<void(Coord,Coord)>& position,
                          const std::function<void(const DriveEvent&)>& event) {
    const auto& path=tables.curve(tables.control(static_cast<unsigned>(motion.track)).primary);
    int budget=motion.remainder+(add_speed?speed:0);
    while (budget>7) {
        budget-=7;
        if (static_cast<std::size_t>(motion.sample)==path.size()-1) {
            // 004B0F20 terminal marker: place at the anchor and end this segment.
            // 004B1FD0..004B1FFB adds the remaining Manhattan distance correction.
            if (event) event({DriveEventKind::Terminal,current,motion});
            const auto anchor=motion.anchor.value();
            const int distance=std::abs(anchor.x-current.x)+std::abs(anchor.y-current.y);
            const int correction=static_cast<int>(fp::multiply(fp::add(1.0,-fp::multiply(distance,1.0/11.0)),7.0));
            const auto old=current;current=anchor;if (position) position(old,current); motion.segment_active=false; motion.anchor.reset(); motion.track=-1; motion.sample=0; motion.remainder=budget+correction;
            return;
        }
        if (event) event({DriveEventKind::Sample,current,motion});
        const auto& p=path.at(static_cast<std::size_t>(motion.sample));
        const auto point=motion.transform({p.x,p.y,0},static_cast<std::uint8_t>(p.facing));
        const auto old=current;current.x=point.x;current.y=point.y;if (position) position(old,current);facing=point.facing;++motion.sample;
    }
    motion.remainder=budget;
    if (budget>0 && static_cast<std::size_t>(motion.sample)<path.size()-1) {
        const auto& p=path.at(static_cast<std::size_t>(motion.sample));
        const auto target=motion.transform({p.x,p.y,0},static_cast<std::uint8_t>(p.facing));
        const float fraction=single_zero(fp::multiply(budget,1.0/7.0)); // Original double at 007E7FA8.
        Coord proposed=current;
        proposed.x+=static_cast<int>(fp::multiply(target.x-current.x,fraction));
        proposed.y+=static_cast<int>(fp::multiply(target.y-current.y,fraction));
        const auto same_cell=[](Coord a,Coord b){return a.x/256==b.x/256 && a.y/256==b.y/256;};
        const Coord next{target.x,target.y,current.z};
        // 004B22CC..004B23F5 prevents an interpolated diagonal point entering a third cell.
        const auto old=current;
        if (same_cell(proposed,current) || same_cell(proposed,next)) current=proposed;
        else if (budget>3) current=next;
        if (position) position(old,current);

    }
}
}
