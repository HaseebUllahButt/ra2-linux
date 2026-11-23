#include "object_damage.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ra2::weapons;
int main() {
  try {
    ObjectDamageInput input{};
    int active, trigger, redraw, attacker, mutate_at, mutation, bypass, immune, resolved;
    while (std::cin >> input.health >> input.maximum_health >> input.raw_damage >>
           input.condition_red >> bypass >> immune >> resolved >> active >> trigger >> redraw >> attacker >>
           mutate_at >> mutation) {
      input.bypass_armor = bypass != 0;
      input.immune = immune != 0;
      input.resolved_damage = resolved;
      ObjectDamageState state{input.health, input.maximum_health, active != 0,
                              trigger != 0, redraw != 0, true, 1};
      std::vector<ObjectDamageAction> events;
      const auto result = base_object_damage_path(input, state, attacker,
          [&](const ObjectDamageAction &event, ObjectDamageState &current) {
            events.push_back(event);
            if (static_cast<int>(events.size()) != mutate_at) return;
            if (mutation == 1) current.active = false;
            if (mutation == 2) current.health = 0;
            if (mutation == 3) current.has_trigger = false;
            if (mutation == 4) current.health = 1;
          });
      std::cout << result.status << ' ' << result.state.health << ' '
                << result.damage << ' ' << result.state.active << ' '
                << result.state.has_trigger << ' ' << events.size();
      for (const auto &event : events)
        std::cout << ' ' << static_cast<int>(event.kind) << ' ' << event.argument
                  << ' ' << event.attacker_argument;
      std::cout << '\n';
    }
    const ObjectDamageInput lethal{100,100,100,false,false,0.25,100};
    for (const auto kind : {std::optional<int>{},std::optional<int>{15}}) {
      ObjectDamageState state{100,100,true,false,false,true,kind};
      unsigned calls=0;
      bool rejected=false;
      try {
        base_object_damage_path(lethal,state,false,
            [&](const ObjectDamageAction&,ObjectDamageState&) { ++calls; });
      } catch (const std::invalid_argument&) { rejected=true; }
      if (!rejected || calls)
        throw std::runtime_error("unresolved infantry branch reached damage callbacks");
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
