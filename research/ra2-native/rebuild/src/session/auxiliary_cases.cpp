#include "session.h"
#include <iostream>
int main() {
    for (const auto& house:ra2::session::auxiliary_house_types())
        std::cout<<house.country<<' '<<house.color<<' '<<house.color_argument<<'\n';
}
