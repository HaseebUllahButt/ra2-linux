#include "battle_orders.h"
#include "camera_scroll.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ra2;
using namespace ra2::app;
using namespace ra2::session::world;
using sim::skirmish::Order;
using sim::skirmish::OrderKind;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Input button(int key, bool pressed, int x, int y) {
    return {InputKind::Button,x,y,key,pressed};
}
Input key(int symbol, bool pressed=true) { return {InputKind::Key,0,0,symbol,pressed}; }
Entity entity(std::uint32_t id, std::uint32_t house, Kind kind,
              const char* type, std::uint16_t x, std::uint16_t y) {
    Entity result;
    result.id=id;
    result.house_id=house;
    result.kind=kind;
    result.type=type;
    result.lifetime=Lifetime::Placed;
    result.cell=ra2map::Cell{x,y};
    result.resolved_hp=100;
    return result;
}
struct Fixture {
    Snapshot world;
    BattleOrders controller{1};
    BattleView view;
    std::uint64_t now=1000;
    int projections=0, cell_queries=0;
    Fixture() {
        for (const auto role : {HouseRole::Player,HouseRole::Player,HouseRole::Neutral,HouseRole::Special}) {
            House house;
            house.id=static_cast<std::uint32_t>(world.houses.size()+1);
            house.role=role;
            world.houses.push_back(house);
        }
        // Storage order differs from ID order. All fake projections use screen cells.
        world.entities={
            entity(30,1,Kind::Unit,"AMCV",50,60),
            entity(10,1,Kind::Infantry,"E1",80,100),
            entity(20,1,Kind::Unit,"HTNK",130,80),
            entity(50,1,Kind::Unit,"PCV",180,140),
            entity(40,1,Kind::Unit,"smcv",160,120),
            entity(60,1,Kind::Structure,"GAPOWR",115,130),
            entity(70,1,Kind::Aircraft,"ORCA",100,50),
            entity(80,0,Kind::Terrain,"TREE01",195,60),
            entity(200,2,Kind::Unit,"HTNK",220,60),
            entity(210,2,Kind::Infantry,"E2",220,95),
            entity(220,2,Kind::Structure,"NAPOWR",220,130),
            entity(230,2,Kind::Aircraft,"ORCA",240,165),
            entity(300,3,Kind::Unit,"CAR",50,160),
            entity(400,4,Kind::Infantry,"CIV1",90,160)};
        view.battlefield={20,30,240,160};
        view.project=[this](const Entity& object) -> std::optional<BattlePoint> {
            ++projections;
            if (!object.cell) return {};
            return BattlePoint{object.cell->x,object.cell->y};
        };
        view.screen_to_cell=[this](BattlePoint point) -> std::optional<ra2map::Cell> {
            ++cell_queries;
            require(point.x>=20 && point.x<260 && point.y>=30 && point.y<190,
                    "Sidebar point reached the Cell callback");
            return ra2map::Cell{static_cast<std::uint16_t>(100+point.x/10),
                               static_cast<std::uint16_t>(200+point.y/10)};
        };
    }
    Entity& get(std::uint32_t id) {
        const auto found=std::find_if(world.entities.begin(),world.entities.end(),
            [id](const auto& object) { return object.id==id; });
        if (found==world.entities.end()) throw std::runtime_error("Missing test entity");
        return *found;
    }
    std::vector<Order> send(Input input) { return controller.handle(input,world,view,now); }
    std::vector<Order> click(int x, int y) {
        require(send(button(1,true,x,y)).empty(),"Left press issued an order");
        return send(button(1,false,x,y));
    }
    void box(int x1=30, int y1=40, int x2=200, int y2=150) {
        require(send(button(1,true,x1,y1)).empty(),"Box press issued an order");
        require(send({InputKind::Motion,x2,y2}).empty(),"Drag issued an order");
        require(send(button(1,false,x2,y2)).empty(),"Box release issued an order");
    }
    void selected(std::initializer_list<std::uint32_t> expected) const {
        require(controller.selected_ids()==std::vector<std::uint32_t>(expected),
                "Selected IDs differ");
    }
};
void expect_orders(const std::vector<Order>& orders, OrderKind kind,
                   std::initializer_list<std::uint32_t> ids, std::uint32_t target=0,
                   ra2map::Cell cell={}) {
    require(orders.size()==ids.size(),"Order count differs");
    std::size_t index=0;
    for (const auto id : ids) {
        const auto& order=orders[index++];
        require(order.kind==kind && order.house==1 && order.entity==id &&
                order.target==target && order.cell.x==cell.x && order.cell.y==cell.y &&
                order.type.empty(),"Order fields differ");
    }
}
void selection() {
    Fixture f;
    require(f.send(button(1,false,50,60)).empty(),"Stray release issued an order");
    f.selected({});
    f.send(button(1,true,50,60));
    f.selected({});
    require(!f.controller.selection_rectangle(),"Press drew a drag box");
    f.send(button(1,false,50,60));
    f.selected({30});
    f.click(80,100);
    f.selected({10});
    for (const auto point : {BattlePoint{220,60},{220,95},{115,130},{100,50},{195,60},{30,180}}) {
        f.controller.clear();
        require(f.click(point.x,point.y).empty(),"Unowned or empty click issued an order");
        f.selected({});
    }
    f.click(50,60);
    f.send(button(1,true,30,40));
    f.send({InputKind::Motion,33,43});
    require(!f.controller.selection_rectangle(),"Small pointer jitter drew a box");
    f.send({InputKind::Motion,200,150});
    const auto rectangle=f.controller.selection_rectangle();
    require(rectangle && rectangle->x==30 && rectangle->y==40 &&
            rectangle->width==171 && rectangle->height==111,"Screen drag rectangle differs");
    f.selected({30});
    f.send(button(1,false,200,150));
    f.selected({10,20,30,40,50});
    require(!f.controller.selection_rectangle(),"Released drag box remains visible");
    f.box(200,150,30,40);
    f.selected({10,20,30,40,50});
    f.box(25,170,35,180);
    f.selected({});
    // A release can finish a drag without an intervening Motion event.
    f.send(button(1,true,80,100));
    f.send(button(1,false,130,80));
    f.selected({10,20});
}
void battlefield_bounds() {
    Fixture f;
    f.click(50,60);
    const auto projections=f.projections;
    for (const auto point : {BattlePoint{19,60},{260,60},{50,29},{50,190}}) {
        f.click(point.x,point.y);
        require(f.send(button(3,true,point.x,point.y)).empty(),"Outside click issued an order");
        f.selected({30});
    }
    require(f.projections==projections && !f.cell_queries,"Outside clicks queried the map");
    f.box(280,40,30,150);
    f.selected({30});
    f.world.entities.push_back(entity(90,1,Kind::Unit,"HTNK",265,80));
    f.send(button(1,true,30,40));
    f.send({InputKind::Motion,500,500});
    const auto rectangle=f.controller.selection_rectangle();
    require(rectangle && rectangle->x==30 && rectangle->y==40 &&
            rectangle->width==230 && rectangle->height==150,"Drag crossed battlefield bounds");
    f.send(button(1,false,500,500));
    f.selected({10,20,30,40,50});
    // An anchor outside the battlefield cannot be hit from its edge.
    f.click(259,80);
    f.selected({10,20,30,40,50});
    f.world.entities.push_back(entity(91,1,Kind::Unit,"HTNK",20,30));
    f.click(20,30);
    f.selected({91});
    f.view.battlefield.width=0;
    require(f.send(button(3,true,20,30)).empty(),"Empty battlefield issued an order");
    f.click(20,30);
    f.selected({91});
}
void move_attack_stop() {
    Fixture f;
    f.box();
    const auto start=f.world.entities;
    for (const auto point : {BattlePoint{220,60},{220,95},{220,130},{240,165}}) {
        const auto target=point.y==60 ? 200u : point.y==95 ? 210u : point.y==130 ? 220u : 230u;
        expect_orders(f.click(point.x,point.y),OrderKind::Attack,
                      {10,20,30,40,50},target);
        require(f.send(button(3,false,point.x,point.y)).empty(),"Right release duplicated orders");
    }
    require(!f.cell_queries,"Attack queried a ground Cell");
    expect_orders(f.click(30,180),OrderKind::Move,
                  {10,20,30,40,50},0,{103,218});
    // Known neutral and special houses do not become enemy targets.
    expect_orders(f.click(50,160),OrderKind::Move,
                  {10,20,30,40,50},0,{105,216});
    expect_orders(f.click(90,160),OrderKind::Move,
                  {10,20,30,40,50},0,{109,216});
    expect_orders(f.send(key('s')),OrderKind::Stop,{10,20,30,40,50});
    expect_orders(f.send(key('S')),OrderKind::Stop,{10,20,30,40,50});
    require(f.send(key('s',false)).empty(),"Key release duplicated Stop");
    for (std::size_t i=0; i<start.size(); ++i) {
        const auto& before=start[i];
        const auto& after=f.world.entities[i];
        require(before.id==after.id && before.house_id==after.house_id && before.type==after.type &&
                before.lifetime==after.lifetime && before.resolved_hp==after.resolved_hp &&
                before.target_id==after.target_id && before.mission==after.mission &&
                before.cell->x==after.cell->x && before.cell->y==after.cell->y,
                "Input changed a world entity");
    }
    require(f.send(button(3,true,30,180)).empty(),"Right click issued an order");
    f.selected({});
    f.controller.clear();
    const auto queries=f.cell_queries;
    require(f.click(30,180).empty() && f.cell_queries==queries,
            "Empty selection queried a destination");
    require(f.send(key('s')).empty() && f.send(key('d')).empty(),"Empty selection issued key orders");
}
void deploy() {
    Fixture f;
    f.box();
    expect_orders(f.send(key('d')),OrderKind::Deploy,{30,40,50});
    expect_orders(f.send(key('D')),OrderKind::Deploy,{30,40,50});
    require(f.send(key('d',false)).empty(),"Key release duplicated Deploy");
    f.controller.clear();
    require(f.click(50,60).empty(),"First MCV click deployed");
    f.now+=200;
    f.controller.set_placement_active(false);
    expect_orders(f.click(52,62),OrderKind::Deploy,{30});
    require(f.click(50,60).empty(),"Third MCV click duplicated Deploy");
    f.now+=351;
    require(f.click(50,60).empty(),"Slow MCV clicks deployed");
    f.now+=10;
    require(f.click(160,120).empty(),"Clicks on different MCVs deployed");
    f.controller.clear();
    f.click(50,60);
    f.now+=10;
    require(f.click(56,60).empty(),"Distant MCV clicks deployed");
    f.now=500;
    require(f.click(56,60).empty(),"Reversed clock deployed an MCV");
    f.controller.clear();
    f.click(130,80);
    require(f.click(130,80).empty(),"Tank double click deployed");
    require(f.send(key('d')).empty(),"Tank D key deployed");
    f.get(10).type="AMCV";
    f.click(80,100);
    require(f.send(key('d')).empty(),"Infantry with an MCV name deployed");
}
void escape_placement_and_cancel() {
    Fixture f;
    f.click(50,60);
    f.send(button(1,true,30,40));
    f.send({InputKind::Motion,200,150});
    f.controller.set_placement_active(true);
    require(!f.controller.selection_rectangle(),"Placement retained a drag box");
    const auto projections=f.projections;
    require(f.click(80,100).empty() && f.send(button(3,true,220,60)).empty() &&
            f.send(key('s')).empty() && f.send(key('d')).empty(),
            "Placement allowed normal battle input");
    require(f.projections==projections,"Placement queried objects");
    f.selected({30});
    f.send(key(27,false));
    require(f.controller.placement_active(),"Escape release canceled placement");
    f.send(key(27));
    require(!f.controller.placement_active(),"Escape did not cancel placement");
    f.selected({30});
    require(f.click(50,60).empty(),"Canceled placement retained double-click history");
    f.send(key(27));
    f.selected({});
    f.send(button(1,true,30,40));
    f.send({InputKind::Motion,200,150});
    f.send(key(27));
    require(!f.controller.selection_rectangle(),"Escape did not cancel a drag");
    f.send(button(1,false,200,150));
    f.selected({});
    f.click(50,60);
    f.send(button(1,true,30,40));
    f.send({InputKind::Motion,200,150});
    require(f.send(button(3,true,220,60)).empty(),"Cancel click attacked");
    require(!f.controller.selection_rectangle(),"Right order retained a drag");
    f.send(button(1,false,200,150));
    f.selected({});
}
void focus_and_stale_selection() {
    Fixture f;
    f.click(50,60);
    f.send(button(1,true,30,40));
    f.send({InputKind::Motion,200,150});
    f.send({InputKind::Focus,0,0,0,false});
    require(!f.controller.selection_rectangle(),"Focus loss retained a drag");
    require(f.send(key('s')).empty() && f.click(80,100).empty(),"Unfocused input changed orders");
    f.send({InputKind::Focus,0,0,0,true});
    f.send(button(1,false,200,150));
    f.selected({30});
    require(f.click(50,60).empty(),"Focus change retained double-click history");
    f.box();
    f.get(10).house_id=2;
    f.get(20).resolved_hp=0;
    f.get(30).lifetime=Lifetime::Destroyed;
    f.get(40).in_limbo=true;
    f.controller.sync(f.world);
    f.selected({50});
    f.get(50).on_map=false;
    require(f.send(key('s')).empty(),"Stale selection received an order");
    f.selected({});
    f.get(50).on_map.reset();
    f.click(180,140);
    f.selected({50});
    f.get(50).active=false;
    f.controller.sync(f.world);
    f.selected({});
    f.get(50).active.reset();
    f.get(50).resolved_hp.reset();
    f.click(180,140);
    f.selected({50});
    f.world.entities.erase(std::remove_if(f.world.entities.begin(),f.world.entities.end(),
        [](const auto& object) { return object.id==50; }),f.world.entities.end());
    f.controller.sync(f.world);
    f.selected({});
}
void callbacks_and_ordering() {
    Fixture f;
    f.view.project=[](const Entity& object) -> std::optional<BattlePoint> {
        // Simulate a camera offset. Hidden objects have no anchor.
        if (!object.cell || object.id==10) return {};
        return BattlePoint{object.cell->x+5,object.cell->y+8};
    };
    f.box(30,40,205,158);
    f.selected({20,30,40,50});
    f.view.screen_to_cell={};
    require(f.click(30,180).empty(),"Missing Cell callback issued Move");
    expect_orders(f.click(225,68),OrderKind::Attack,{20,30,40,50},200);
    f.view.screen_to_cell=[](BattlePoint) -> std::optional<ra2map::Cell> { return {}; };
    require(f.click(30,180).empty(),"Invalid Cell callback issued Move");
    f.send(button(3,true,30,180));
    f.view.project={};
    f.click(55,68);
    f.selected({});
    f.box();
    f.selected({});
    // Equal hit distances choose the same ID after storage order changes.
    Fixture overlap;
    overlap.world.entities={entity(9,1,Kind::Unit,"HTNK",50,60),
                            entity(2,1,Kind::Unit,"HTNK",50,60)};
    overlap.click(50,60);
    overlap.selected({2});
    std::reverse(overlap.world.entities.begin(),overlap.world.entities.end());
    overlap.click(50,60);
    overlap.selected({2});
    BattleOrders observer(0);
    require(observer.handle(button(1,true,50,60),overlap.world,overlap.view,0).empty(),
            "Observer press issued an order");
    observer.handle(button(1,false,50,60),overlap.world,overlap.view,0);
    require(observer.selected_ids().empty(),"House zero selected units");
}
void event_edges() {
    Fixture f;
    f.click(50,60);
    f.send(button(1,true,30,40));
    f.send({InputKind::Motion,200,150});
    f.send({InputKind::Resize,640,480});
    require(!f.controller.selection_rectangle(),"Resize retained a drag");
    f.send(button(1,false,200,150));
    f.selected({30});
    f.send(button(1,true,30,40));
    f.send({InputKind::Motion,std::numeric_limits<int>::min(),std::numeric_limits<int>::min()});
    const auto rectangle=f.controller.selection_rectangle();
    require(rectangle && rectangle->x==20 && rectangle->y==30 &&
            rectangle->width==11 && rectangle->height==11,"Negative pointer did not clip safely");
    f.send({InputKind::Quit});
    require(!f.controller.selection_rectangle(),"Quit retained a drag");
    f.send(button(1,false,200,150));
    f.selected({30});
    require(f.send(button(2,true,50,60)).empty() && f.send({InputKind::Wheel,0,1}).empty() &&
            f.send(key('x')).empty(),"Unrelated input issued orders");
    f.selected({30});
}
} // namespace

int main() try {
    // Holding arrows for one second moves the same distance at each frame rate.
    for(const unsigned interval:{1u,4u,16u,40u,100u}) {
        CameraScroll scroll;int x=0,y=0;
        for(unsigned now=interval;now<=1000;now+=interval) {
            const auto delta=scroll.step(now,1,1);x+=delta.first;y+=delta.second;
        }
        const unsigned duration=1000/interval*interval;
        require(x==int(duration*200/1000) && y==int(duration*130/1000),"Camera speed depends on frame rate");
    }
    CameraScroll held;int travel=0;
    for(unsigned now=40;now<=800;now+=40)travel+=held.step(now,1,0).first;
    require(travel==160,"An 800 ms arrow hold scrolls too far");
    CameraScroll scroll;
    require(scroll.step(1000,2,-2)==std::make_pair(20,-13),"Camera pause cap or combined input is wrong");
    scroll.reset(1000);
    require(scroll.step(1000,1,1)==std::make_pair(0,0),"Camera moves without elapsed time");
    selection();
    battlefield_bounds();
    move_attack_stop();
    deploy();
    escape_placement_and_cancel();
    focus_and_stale_selection();
    callbacks_and_ordering();
    event_edges();
    std::cout << "Battle input checks passed: selection, bounds, orders, deployment, and cancellation.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
