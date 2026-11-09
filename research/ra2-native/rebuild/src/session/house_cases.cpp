#include "session.h"
#include <iostream>
using namespace ra2;
int main() {
    for (int credits : {0,1,2500,5000,10000,65535,2147483647}) for (int color=0;color<8;++color) {
        const auto v=session::initialize_house_values(credits,color);
        std::cout<<credits<<' '<<color<<' '<<v.initial_credits<<' '<<v.cash<<' '
                 <<v.color<<' '<<v.house_type_color<<'\n';
    }
}
