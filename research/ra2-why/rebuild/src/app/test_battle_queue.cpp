#include "battle_queue.h"
#include "cameo_click.h"
#include <stdexcept>
#include <iostream>

int main() {
    ra2::app::BattleBuildQueue queue;
    const auto require=[](bool ok){if(!ok)throw std::runtime_error("Battle build queue failed");};
    using ra2::app::cameo_click;
    using Click=ra2::app::CameoClick;
    require(cameo_click(true,true,false,false,false,false)==Click::Hold);
    require(cameo_click(true,true,false,true,false,false)==Click::Cancel);
    require(cameo_click(false,true,false,true,false,false)==Click::Resume);
    require(cameo_click(true,true,true,true,false,false)==Click::Cancel);
    require(cameo_click(false,true,true,true,false,false)==Click::Place);
    require(cameo_click(true,true,false,false,true,true)==Click::RemoveQueued);
    require(cameo_click(true,true,false,false,false,true)==Click::Hold);
    require(cameo_click(true,false,false,false,false,false)==Click::None);
    require(cameo_click(false,true,false,false,false,true)==Click::Queue);
    require(cameo_click(false,false,false,false,false,false)==Click::Begin);
    // The active product and every type share one factory limit of 30.
    for(int i=0;i<29;++i)require(queue.add(1,"E1",1));
    require(!queue.add(1,"ENGINEER",1));
    require(queue.count("E1")==29);
    require(queue.add(2,"MTNK",0));
    require(queue.cancel(1,"E1") && queue.count("E1")==28);
    require(queue.add(1,"ENGINEER",1));
    require(!queue.add(1,"E1",1));
    require(!queue.cancel(2,"E1") && queue.slots().at(2).front()=="MTNK");
    for(int i=0;i<28;++i) {require(queue.slots().at(1).front()=="E1");queue.pop(1);}
    require(queue.slots().at(1).front()=="ENGINEER");queue.pop(1);
    require(queue.slots().at(1).empty() && queue.count("MTNK")==1);
    ra2::app::BattleBuildQueue full;
    for(int i=0;i<30;++i)require(full.add(1,"E1",0));
    require(!full.add(1,"E1",0) && !full.add(1,"ENGINEER",0));
    for(int remaining=30;remaining>0;--remaining) {
        require(full.count("E1")==remaining && full.slots().at(1).front()=="E1");full.pop(1);
    }
    require(full.add(1,"ENGINEER",0));
    std::cout<<"Battle queue checks passed: thirty per factory, active item, FIFO and cancellation.\n";
}
