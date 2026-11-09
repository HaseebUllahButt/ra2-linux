#include "fire_eligibility.h"
#include <iostream>
using namespace ra2::weapons;
int main() {
 int a[19];
 while (std::cin >> a[0]) {
  for (int i=1;i<19;++i) std::cin >> a[i];
  FireEligibilityPrefixInput x;
  x.target_present=bool(a[0]); x.shooter_virtual_1c0=bool(a[1]); x.shooter_virtual_1bc=bool(a[2]);
  x.shooter_flag_325=bool(a[3]); x.shooter_virtual_1c4=bool(a[4]); x.attachment_present=bool(a[5]);
  x.target_is_attachment=bool(a[6]); x.target_is_object=bool(a[7]); x.target_cell_resolved=true;
  x.target_limbo=bool(a[8]); x.target_flag_210=bool(a[9]); x.target_virtual_64=a[10];
  x.target_cell_visible=bool(a[11]); x.averaged_weapon_damage=a[12]; x.target_house_allied=bool(a[13]);
  x.shooter_flag_7a=bool(a[14]); x.shooter_virtual_324=bool(a[15]); x.shooter_original_kind=a[16];
  x.unit_type_bb0=bool(a[17]); x.unit_type_bb1=bool(a[18]);
  auto r=base_fire_eligibility_prefix(x); std::cout << (r.continued?0:r.rejection.value_or(-1)) << '\n';
 }
}
