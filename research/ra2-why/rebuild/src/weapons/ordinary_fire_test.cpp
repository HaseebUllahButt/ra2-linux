#include "fire_eligibility.h"
#include <iostream>
using namespace ra2::weapons;
int main() {
 int weapon,immune,flag157,land,cell50,field54c,start,duration,frame,ammo,decloak,cloak,af2,target325,b47,v328;
 double verse;
 while (std::cin >> weapon >> immune >> flag157 >> land >> cell50 >> field54c >> start >> duration >> frame
        >> ammo >> decloak >> cloak >> af2 >> verse >> target325 >> b47 >> v328) {
  OrdinaryUnitFireInput in; auto& p=in.prefix;
  p.target_present=true;p.shooter_virtual_1c0=false;p.shooter_virtual_1bc=false;p.shooter_flag_325=false;
  p.shooter_virtual_1c4=false;p.target_is_object=true;p.target_cell_resolved=true;p.target_limbo=false;
  p.target_flag_210=false;p.target_virtual_64=0;p.shooter_flag_7a=false;p.shooter_virtual_324=false;
  in.shooter_is_unit=true;in.target_is_unit=true;in.primary_weapon_index_zero=true;in.weapon_present=bool(weapon);in.weapon_special_flags_clear=true;
  in.alternate_weapon_effects_clear=true;
  in.warhead_special_flags_clear=true;in.warhead_present=true;in.target_virtual_1bc=bool(immune);
  in.warhead_flag_157=bool(flag157);in.target_virtual_54=false;in.target_cell_land_type=land;
  in.target_virtual_50=bool(cell50);in.shooter_type_54c=field54c;in.barrel_override_active=false;
  in.timer=FireTimer{start,duration};in.frame=frame;in.ammo_field_254=ammo;
  in.weapon_flag_10f=bool(decloak);in.shooter_state_1b8=cloak;in.shooter_type_af2=bool(af2);
  in.warhead_armor_verse=verse;in.target_flag_325=bool(target325);in.matching_bridge_flags=true;
  in.shooter_type_b47=bool(b47);in.shooter_virtual_328=bool(v328);
  auto r=base_ordinary_unit_fire_check(in);std::cout << r.rejection.value_or(-1) << '\n';
 }
}
