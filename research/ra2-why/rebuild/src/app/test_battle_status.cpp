#include "battle_status.h"
#include <cassert>
#include <iostream>
using namespace ra2::app;
int main() {
    BattleStatus status;
    status.set("Moving.",1000);
    assert(status.text(3999)=="Moving.");
    assert(status.text(4000).empty());
    status.set("Cannot deploy here.",5000);
    assert(status.text(8000).empty());
    status.set("Victory.",9000,true);
    assert(status.text(100000)=="Victory.");
    using Status=ra2::sim::production::Status;
    for(auto code:{Status::Rejected,Status::NeedsInput,Status::Unsupported})
        assert(production_status(code,"Building placed.","Can't build here.")=="Can't build here.");
    assert(production_status(Status::Done,"Building placed.","Can't build here.")=="Building placed.");
    std::cout<<"Status expiry and player placement messages passed\n";
}
