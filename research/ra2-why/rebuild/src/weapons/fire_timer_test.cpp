#include "fire_timer.h"
#include <iostream>
int main() {
  ra2::weapons::FireTimer t; int now;
  while (std::cin >> t.start_frame >> t.duration >> now)
    std::cout << ra2::weapons::base_fire_timer_ready(t,now) << '\n';
}
