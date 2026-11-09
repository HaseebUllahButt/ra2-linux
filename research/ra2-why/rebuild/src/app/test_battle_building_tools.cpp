#include "battle_building_tools.h"
#include <cassert>
#include <iostream>
using namespace ra2;
using namespace ra2::app;
using namespace ra2::session::world;
int main() {
    rules::IniFile rules, art;
    rules.parse("[General]\nRepairPercent=15%\nRepairRate=.016\nRepairStep=8\nRefundPercent=50%\nAlliedSurvivorDivisor=500\nSovietSurvivorDivisor=250\n"
        "[POWER]\nCost=2000\nStrength=1000\nCrewed=no\n[LOCKED]\nUnsellable=yes\nRepairable=no\n");
    art.parse("[POWER]\nFoundation=2x2\n");
    BattleBuildingTools tools(rules, art);
    Snapshot world;
    world.setup_progress=SetupProgress::PrototypeComplete;
    House owner; owner.id=1; owner.cash=1000;
    House enemy; enemy.id=2; enemy.cash=1000;
    world.houses={owner,enemy};
    Entity e; e.id=10; e.house_id=1; e.kind=Kind::Structure;
    e.type="POWER"; e.lifetime=Lifetime::Placed; e.cell=ra2map::Cell{10,10};
    e.resolved_hp=500; e.maximum_hp=1000; e.next=0;
    world.entities={e};
    // Enemy and unit targets cannot spend money or change a building.
    assert(!tools.apply(world,2,10,BuildingTool::Repair).changed);
    assert(!tools.apply(world,2,10,BuildingTool::Sell).sold);
    assert(world.entities[0].resolved_hp==500 && world.houses[0].cash==1000);
    auto unit=e; unit.kind=Kind::Unit;
    assert(!tools.allowed(unit,1,BuildingTool::Sell));
    auto locked=e; locked.type="LOCKED";
    assert(!tools.allowed(locked,1,BuildingTool::Sell));
    assert(!tools.allowed(locked,1,BuildingTool::Repair));
    assert(tools.apply(world,1,10,BuildingTool::Repair).changed);
    assert(tools.repairing(10));
    tools.tick(world,14);
    assert(world.entities[0].resolved_hp==508 && world.houses[0].cash<1000);
    assert(tools.apply(world,1,10,BuildingTool::Repair).changed && !tools.repairing(10));
    const auto cash=world.houses[0].cash;
    tools.tick(world,28);
    assert(world.entities[0].resolved_hp==508 && world.houses[0].cash==cash);
    world.entities[0].resolved_hp=1000;
    assert(!tools.apply(world,1,10,BuildingTool::Repair).changed);
    assert(tools.apply(world,1,10,BuildingTool::Sell).sold);
    assert(world.entities[0].lifetime==Lifetime::Destroyed && world.houses[0].cash==*cash+1000);
    assert(!tools.apply(world,1,10,BuildingTool::Sell).sold);
    assert(world.houses[0].cash==*cash+1000);
    std::cout<<"battle building tools tests passed\n";
}
