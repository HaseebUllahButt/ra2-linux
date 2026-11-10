#include "walk_motion.h"
#include <cmath>
#include <string>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
void check(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
struct Vector {
    Coord current,destination;
    int speed;
    Coord position;
    std::int16_t facing;
};
// Golden outputs emulated from game.exe by original_walk_math_check.py
// (original step 0071F2FA..0071F420 with fake Foot stubs).
const Vector vectors[]={
    {{1000,2000,0},{2000,2000,0},100,{1100,2000,0},16383},
    {{2000,2000,0},{1000,2000,0},100,{1900,1999,0},-16383},
    {{2000,2000,0},{2000,1000,0},100,{2000,1900,0},0},
    {{2000,1000,0},{2000,2000,0},100,{1999,1100,0},32767},
    {{1000,2000,0},{2000,1000,0},141,{1100,1901,0},8315},
    {{2000,2000,0},{1000,1000,0},141,{1899,1901,0},-8315},
    {{1000,1000,0},{2000,2000,0},141,{1100,1098,0},24451},
    {{2000,1000,0},{1000,2000,0},141,{1899,1098,0},-24454},
    {{5000,5000,3},{5300,5100,3},50,{5047,5015,3},19588},
    {{5000,5000,3},{5100,5300,3},50,{5015,5047,3},29388},
    {{5000,5000,3},{4700,5100,3},50,{4952,5015,3},-19591},
    {{5000,5000,3},{4900,5300,3},50,{4984,5047,3},-29391},
    {{5000,5000,3},{5300,4900,3},50,{5047,4984,3},13178},
    {{5000,5000,3},{4700,4900,3},50,{4952,4984,3},-13178},
    {{5000,5000,3},{5100,4700,3},50,{5015,4952,3},3378},
    {{5000,5000,3},{4900,4700,3},50,{4984,4952,3},-3378},
    {{1000,1000,0},{1010,1000,0},100,{1100,1000,0},16383},
    {{1000,1000,0},{1006,1008,0},100,{1060,1079,0},25998},
    {{1000,2000,7},{5000,9000,7},0,{1000,2000,7},27309},
    {{0,0,0},{1000,1,0},1,{1,0,0},16383},
    {{100000,200000,0},{200000,100000,0},500,{100357,199650,0},8315},
    {{-5000,-5000,0},{-8000,-2000,0},200,{-5143,-4860,0},-24454},
    {{1234,4321,9},{1234,4321,9},100,{1334,4321,9},16383},
    {{0,0,0},{0,-10000,0},300,{0,-300,0},0},
    {{0,0,0},{-10000,0,0},300,{-300,0,0},-16383},
    {{0,0,0},{0,10000,0},300,{0,300,0},32767},
    {{0,0,0},{3,1,0},10,{9,3,0},19588},
    {{0,0,0},{1,3,0},10,{3,9,0},29388},
    {{0,0,0},{-1,3,0},10,{-3,9,0},-29391},
    {{0,0,0},{10000,250,0},300,{299,6,0},16638},
    {{0,0,0},{-250,-1000,0},300,{-73,-290,0},-2567},
    {{7000,7000,1},{6990,7010,1},25,{6982,7017,1},-24454},
};
int main(int argc,char** argv) {
    if (argc<2) {
        std::cerr<<"usage: test_walk_motion game.exe [--print]\n";
        return 2;
    }
    const bool print_only=argc>2&&std::string(argv[2])=="--print";
    const auto math=OriginalWalkMath::from_executable(argv[1]);
    const int count=static_cast<int>(sizeof(vectors)/sizeof(vectors[0]));
    for (int index=0;index<count;++index) {
        const auto& vector=vectors[index];
        const auto result=math.step(vector.current,vector.destination,vector.speed);
        if (print_only) {
            std::cout<<index<<' '<<result.position.x<<' '<<result.position.y<<' '
                     <<result.position.z<<' '<<result.facing<<'\n';
            continue;
        }
        check(result.position.x==vector.position.x&&
              result.position.y==vector.position.y&&
              result.position.z==vector.position.z,"walk step position");
        check(result.facing==vector.facing,"walk step facing");
        check(result.position.z==vector.current.z,"z passes through unchanged");
    }
    if (print_only) return 0;
    // Facing convention: binary angle, clockwise, north is zero.
    check(math.step({0,0,0},{0,-10000,0},300).facing==0,"north faces 0");
    check(math.step({0,0,0},{10000,0,0},300).facing==0x3FFF,"east faces 0x3FFF");
    check(math.step({0,0,0},{0,10000,0},300).facing==0x7FFF,"south faces 0x7FFF");
    check(math.step({0,0,0},{-10000,0,0},300).facing==-16383,"west faces 0xC001");
    // One speed budget on the 45 degree diagonals: each axis gets about
    // 0.707 * speed, never the full speed on both axes.
    const auto northeast=math.step({1000,2000,0},{2000,1000,0},141);
    const int dx=northeast.position.x-1000,dy=northeast.position.y-2000;
    check(std::abs(dx)<141&&std::abs(dy)<141,"diagonal axes stay under the speed");
    std::cout<<count<<" walk step vectors match, diagonal speed 141 moved ("
             <<dx<<","<<dy<<")\n";
    // Speed zero keeps the position but still turns.
    const auto stopped=math.step({1000,2000,7},{5000,9000,7},0);
    check(stopped.position.x==1000&&stopped.position.y==2000,"speed 0 keeps position");
    // Near target: the original spends the full speed, no clamp here.
    const auto overshoot=math.step({1000,1000,0},{1010,1000,0},100);
    check(overshoot.position.x==1100,"near target spends the full speed");
    std::cout<<"walk motion checks passed\n";
    return 0;
}
