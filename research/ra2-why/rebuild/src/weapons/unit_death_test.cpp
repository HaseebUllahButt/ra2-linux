#include "unit_death.h"
#include <iostream>
#include <vector>
using namespace ra2::weapons;
int main() {
 int trigger,active,field,attacker,special,mutate_at,mutation;
 while (std::cin >> trigger >> active >> field >> attacker >> special >> mutate_at >> mutation) {
  UnitDeathTriggerState s{bool(trigger),bool(active),field};std::vector<int> events;
  auto cb=[&](int id,UnitDeathTriggerState& current) {
   events.push_back(id);if (static_cast<int>(events.size())==mutate_at) {
    if (mutation==1) current.has_trigger=false;
    if (mutation==2) current.active=false;
   }
  };
  if (!base_unit_death_trigger_prefix(s,attacker,bool(special),cb)) return 1;
  std::cout << s.has_trigger << ' ' << s.active << ' ' << events.size();
  for (int event:events) std::cout << ' ' << event;
  std::cout << '\n';
 }
}
