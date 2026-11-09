#include "battle_queue.h"
#include <stdexcept>
#include <iostream>

int main() {
    ra2::app::BattleBuildQueue queue;
    const auto require=[](bool ok){if(!ok)throw std::runtime_error("Battle build queue failed");};
    for(int i=0;i<4;++i)require(queue.add(1,"E1",1));
    require(!queue.add(1,"E1",1)); // Active product counts toward five.
    require(queue.add(1,"ENGINEER",0));
    require(queue.add(2,"MTNK",0)); // Other factories keep their own order.
    require(queue.count("E1")==4 && queue.count("MTNK")==1);
    require(queue.cancel(1,"E1") && queue.count("E1")==3);
    require(!queue.cancel(2,"E1") && queue.slots().at(2).front()=="MTNK");
    for(int i=0;i<3;++i){require(queue.slots().at(1).front()=="E1");queue.pop(1);}
    require(queue.slots().at(1).front()=="ENGINEER");
    queue.pop(1);
    require(queue.slots().at(1).empty() && queue.slots().at(2).size()==1);
    // A rejected sixth request must not stop the five accepted requests.
    ra2::app::BattleBuildQueue full;
    for(int i=0;i<5;++i)require(full.add(1,"E1",0));
    require(!full.add(1,"E1",0));
    require(full.add(1,"ENGINEER",0));
    for(int i=5;i>0;--i) {
        require(full.count("E1")==i && full.slots().at(1).front()=="E1");
        full.pop(1);
    }
    require(full.slots().at(1).front()=="ENGINEER");
    require(full.add(1,"E1",0)); // Capacity returns after completion.
    std::cout<<"Battle queue checks passed: five per type, factory order, pending cancellation.\n";
}
