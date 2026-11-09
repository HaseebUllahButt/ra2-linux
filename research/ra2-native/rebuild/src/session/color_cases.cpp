#include "session.h"
#include <cstdint>
#include <iostream>
#include <limits>
int main() {
    for (const std::int32_t n : {std::numeric_limits<std::int32_t>::min(),-3,-2,-1,0,1,2,3,4,5,6,7,8,9,10,255,std::numeric_limits<std::int32_t>::max()})
        std::cout<<n<<' '<<ra2::session::multiplayer_house_color(n)<<'\n';
}
