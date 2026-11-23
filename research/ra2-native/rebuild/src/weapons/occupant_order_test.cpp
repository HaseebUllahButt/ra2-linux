#include "occupant_order.h"
#include <iostream>
#include <stdexcept>
using namespace ra2::weapons;
int main() {
  try {
    ContentLinks links;
    char op; unsigned cell, layer, object; int type, count;
    while (std::cin >> op) {
      if (op=='O') {
        const auto order=base_map_placement_order();
        for (auto kind:order) std::cout << static_cast<int>(kind) << ' ';
        std::cout << '\n';
        continue;
      }
      if (op=='Z') {
        links={};
        for (unsigned i=1;i<=3;++i) links.cells[i]={};
        for (unsigned i=1;i<=5;++i) links.objects[i]={};
        continue;
      }
      std::cin >> layer >> object >> type >> count;
      if (object) links.objects.at(object).original_type=type;
      std::vector<std::uint32_t> cells;
      for (int i=0;i<count;++i) { std::cin >> cell; cells.push_back(cell); }
      const auto result=links.place(cells,layer?ContentLayer::Bridge:ContentLayer::Ground,
                                    object,op=='A');
      std::cout << static_cast<int>(result);
      for (unsigned i=1;i<=3;++i)
        std::cout << ' ' << links.cells.at(i).ground_head << ' ' << links.cells.at(i).bridge_head;
      for (unsigned i=1;i<=5;++i) std::cout << ' ' << links.objects.at(i).next;
      std::cout << '\n';
    }
    return 0;
  } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
