#include "cannon_line.h"
#include <cstdlib>
#include <iostream>
using namespace ra2::weapons;
int main() {
 int seed,cliffs,walls,rule,ignore;CannonLineInput input{};
 while (std::cin >> seed >> input.origin.x >> input.origin.y >> input.origin.z >> input.target.x >> input.target.y >> input.target.z
        >> cliffs >> walls >> rule >> ignore) {
  input.checks_cliffs=cliffs;input.checks_walls=walls;input.allied_wall_rule=rule;input.warhead_flag_144=ignore;
  const auto lookup=[&](std::int16_t x,std::int16_t y)->std::optional<CannonLineCell> {
   if (x<0 || x>=512 || y<0 || y>=512) return {};
   const int h=(x*37+y*73+seed*17)&255,level=h%7-3,bit=(h/7)%2,ov=(h/14)%7;
   const int overlay=ov==0?0:ov==1?2:ov==2?26:-1;
   return CannonLineCell{{static_cast<unsigned>(y*512+x+1),level+4*bit,overlay!=-1,false,false},level,overlay};
  };
  const auto r=base_cannon_line_trace(input,lookup);
  std::cout << static_cast<int>(r.status) << ' ' << r.cell_id << '\n';
 }
}
