#include "session.h"
#include <iostream>
using namespace ra2;
int main() {
    for (int human : {0,1})
        std::cout<<human<<' '<<static_cast<int>(session::starting_unit_mission(human!=0))<<'\n';
}
