// Original wrapper traces, generated separately from both executables.
#include "drive.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { unsigned path[23],count; int speed,frames; std::uint32_t hash; };
constexpr Golden goldens[]={
#include "route_golden.inc"
};
constexpr int directions[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
}
int main(int argc,char** argv) try {
    if (argc!=3) throw std::runtime_error("usage: test_route BASE_EXECUTABLE EXPANSION_EXECUTABLE");
    unsigned checks=0;
    for (int executable=1;executable<3;++executable) {
        const auto tables=DriveTables::from_executable(argv[executable]);
        for (const auto& golden:goldens) {
            Coord goal{13952,17792,0};
            const std::vector<unsigned> path(golden.path,golden.path+golden.count);
            for (auto direction:path) { goal.x+=directions[direction][0]*256; goal.y+=directions[direction][1]*256; }
            auto drive=DriveSegment::begin_route({13952,17792,0},goal,path[0]*32,golden.speed,path,tables);
            const auto swept=drive.swept_cells(tables);
            std::uint32_t hash=2166136261u; int frames=0;
            while (drive.active && frames<16384) {
                drive.tick(tables); ++frames;
                if (std::find(swept.begin(),swept.end(),std::array<int,2>{drive.current.x/256,drive.current.y/256})==swept.end())
                    throw std::runtime_error("original trace leaves the conservative swept cells");
                for (int value:{drive.current.x,drive.current.y,drive.current.z,drive.motion.track,drive.motion.sample,drive.motion.remainder,static_cast<int>(drive.facing)})
                    hash=(hash^static_cast<std::uint32_t>(value))*16777619u;
                for (int value:drive.motion.path) hash=(hash^static_cast<std::uint32_t>(value))*16777619u;
            }
            if (frames!=golden.frames || hash!=golden.hash) {
                std::cerr<<"route mismatch edition="<<executable<<" count="<<path.size()<<" first="<<path.front()<<" next="<<path[1]<<" speed="<<golden.speed<<" frames="<<frames<<" expected="<<golden.frames<<" hash="<<std::hex<<hash<<" expected="<<golden.hash<<std::dec<<"\n";
                return 1;
            }
            if (drive.current.x!=goal.x || drive.current.y!=goal.y || drive.current.z!=goal.z || drive.motion.anchor || drive.motion.destination)
                throw std::runtime_error("completed route state is invalid");
            ++checks;
        }
        for (const std::vector<unsigned>& path:{std::vector<unsigned>{0,4},std::vector<unsigned>{0,1,2},std::vector<unsigned>{1,1},std::vector<unsigned>{0,8},std::vector<unsigned>(24,0)}) {
            bool rejected=false;
            try { (void)DriveSegment::begin_route({13952,17792,0},{13952,17280,0},0,15,path,tables); }
            catch (const std::invalid_argument&) { rejected=true; }
            if (!rejected) throw std::runtime_error("unrecovered route was accepted");
            ++checks;
        }
    }
    std::cout<<"original connected drive traces and explicit limits: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
