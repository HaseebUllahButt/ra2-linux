// Connected production run on real base rules: AMCV deploy, power plant,
// barracks and infantry production on a resolved flat test map.
#include "production_runtime.h"
#include "../session/world/world.h"
#include <iostream>
#include <stdexcept>

using namespace ra2::sim::production;
namespace w=ra2::session::world;
namespace {
unsigned checks=0;
void require(bool ok,const std::string& what) { ++checks; if (!ok) throw std::runtime_error(what); }
const char* name(Status s) {
    switch (s) { case Status::Done: return "Done"; case Status::Waiting: return "Waiting"; case Status::NeedsTurn: return "NeedsTurn";
    case Status::Rejected: return "Rejected"; case Status::NeedsInput: return "NeedsInput"; default: return "Unsupported"; }
}
void expect(const Outcome& o,Status s,const std::string& what) {
    if (o.status!=s) throw std::runtime_error(what+": got "+name(o.status)+" ("+o.detail+")");
    ++checks;
}
w::Snapshot world(const Catalog& c,std::uint16_t facing) {
    w::Snapshot s;
    s.setup_progress=w::SetupProgress::Complete;
    s.geometry=ra2::session::PlacementGeometry{100,100,{2,2,96,90}};
    s.tube_vector_count=0;
    s.random=ra2::sim::Random(12345).state();
    for (int y=70;y<130;++y) for (int x=70;x<130;++x) {
        w::CellState cell; cell.allocated=true; cell.heads=ra2::weapons::CellContentHeads{};
        ra2::sim::TerrainCell t; t.cell={std::uint16_t(x),std::uint16_t(y)}; t.land_type=0; t.height_level=0; t.ramp_index=0;
        t.overlay_type_index=-1; t.bridge=false; cell.terrain=t;
        cell.ground_occupancy_124=0x100; cell.cell_flags_140=0;   // Upper bits must survive writes.
        s.cells[std::uint32_t(y*512+x)]=cell;
    }
    w::House h; h.id=1; h.original_index=0; h.name="Player"; h.country="Americans";
    h.cash=10000; h.tech_level=10; h.human_controlled=true; h.house_flag_135=false;
    h.stolen_tech_204=false; h.stolen_tech_205=false; h.stolen_mask_208=0; h.stolen_mask_20c=0; h.stolen_mask_210=0; h.stolen_mask_214=0;
    for (std::size_t i=0;i<c.countries.size();++i) if (c.countries[i]=="Americans") h.country_index=std::int32_t(i);
    h.power_output=0; h.power_drain=0;
    for (auto* m:{&h.buildings_active_5460,&h.buildings_owned_5420,&h.buildings_ever_54a0,&h.units_owned_5430,&h.units_ever_54b0,
                  &h.infantry_owned_5440,&h.infantry_ever_54c0,&h.aircraft_owned_5450,&h.aircraft_ever_54d0}) *m=std::map<std::int32_t,std::int32_t>{};
    s.houses.push_back(h); s.creation_order.push_back({w::CreatedKind::House,1});
    const auto* amcv=c.find("AMCV");
    w::Entity u; u.id=2; u.house_id=1; u.kind=w::Kind::Unit; u.type="AMCV"; u.lifetime=w::Lifetime::Placed;
    u.cell=ra2map::Cell{90,90}; u.position=ra2::sim::Coord{90*256+128,90*256+128,0}; u.layer=w::Layer::Ground;
    u.next=0; u.hull_facing_raw=facing; u.locomotor_moving=false; u.resolved_hp=amcv->strength; u.maximum_hp=amcv->strength;
    u.original_type_index=amcv->heap_index; u.object_field_74=1; u.object_field_76=0; u.on_map=true;
    s.entities.push_back(u); s.creation_order.push_back({w::CreatedKind::Entity,2});
    (*s.houses[0].units_owned_5430)[amcv->heap_index]=1;
    auto& cell=s.cells.at(90*512+90); cell.heads->ground_head=2; *cell.ground_occupancy_124|=0x20;
    return s;
}
FactorySet flags() {
    FactorySet f; f.flags.session_type=1; f.flags.super_weapons=true; f.flags.build_off_ally=false; f.flags.player_house=1;
    f.house_factory_counts[1]={0,0,0,0,0,0}; f.power_blackout_idle[1]=true;
    return f;
}
CanBuild option(const std::vector<std::pair<std::string,CanBuild>>& list,const std::string& id) {
    for (const auto& [n,r]:list) if (n==id) return r;
    throw std::runtime_error("missing option "+id);
}
// Run tick() until the factory for type completes; returns frames used.
std::uint64_t produce(w::Snapshot& s,FactorySet& f,const Catalog& c,const std::string& type,std::uint64_t& frame) {
    const auto cash=*s.houses[0].cash;
    const auto start=begin_production(s,f,c,1,type,frame);
    expect(start,Status::Done,"begin "+type);
    std::uint64_t used=0;
    for (;;) {
        ++frame; ++used; expect(tick(s,f,c,frame),Status::Done,"tick");
        bool done=false;
        for (const auto& x:f.factories) if (x.object_type==type && x.progress==54) done=true;
        if (done) break;
        if (used>20000) throw std::runtime_error("factory did not finish "+type);
    }
    std::cout<<type<<": build frames "<<*start.value<<", finished after "<<used<<" ticks, paid "<<cash-*s.houses[0].cash<<'\n';
    return used;
}
}
int main(int argc,char** argv) try {
    if (argc!=3) throw std::runtime_error("usage: test_production_runtime_world RULES_INI ART_INI");
    const auto rules=ra2::rules::IniFile::from_file(argv[1]); const auto art=ra2::rules::IniFile::from_file(argv[2]);
    ra2::rules::IniFile map;
    const auto c=Catalog::from(rules,art,map);
    for (const auto& g:c.gaps) std::cout<<"catalog gap: "<<g<<'\n';
    require(c.find("GACNST") && c.find("GACNST")->placement.foundation==18,"GACNST 4x4 from art.ini");
    require(c.find("GAPOWR")->placement.foundation==3 && c.find("GAPOWR")->power_output==200,"GAPOWR facts");
    require(c.find("GAPILE")->factory==Rtti::InfantryType && c.find("GAPILE")->power_drain==10,"GAPILE facts");
    // Dock rules from rules.ini: the ORCA and BEAG aircraft dock at the
    // Airforce Command Headquarters (GAAIRC) or its American variant
    // (AMRADR); GAAIRC has NumberOfDocks=4 and UnitReload=yes.
    auto has_dock=[&](const char* type,const char* dock) {
        const auto* t=c.find(type); require(t,type+std::string(" in catalog"));
        for (const auto& d:t->dock) if (d==dock) return true;
        return false;
    };
    require(has_dock("ORCA","GAAIRC") && has_dock("ORCA","AMRADR"),"ORCA docks at GAAIRC and AMRADR");
    require(has_dock("BEAG","GAAIRC") && has_dock("BEAG","AMRADR"),"BEAG docks at GAAIRC and AMRADR");
    const auto* gaairc=c.find("GAAIRC"); require(gaairc,"GAAIRC in catalog");
    require(gaairc->number_of_docks==4,"GAAIRC NumberOfDocks=4");
    require(gaairc->unit_reload,"GAAIRC UnitReload=yes");
    // Wrong facing: the original asks the unit to turn first; occupancy order may change.
    {
        auto s=world(c,0x4000); auto f=flags();
        const auto o=deploy(s,f,c,2,true);
        expect(o,Status::NeedsTurn,"deploy facing"); require(o.value==0x80,"deploy facing 0x80");
        require(s.cells.at(90*512+90).heads->ground_head==2 && (*s.cells.at(90*512+90).ground_occupancy_124&0x120)==0x120,"unit stays marked");
    }
    // Blocked: a tree inside the 4x4 yard footprint.
    {
        auto s=world(c,0x8000); auto f=flags();
        w::Entity tree; tree.id=3; tree.kind=w::Kind::Terrain; tree.type="TREE"; tree.lifetime=w::Lifetime::Placed; tree.next=0;
        tree.cell=ra2map::Cell{91,91}; tree.position=ra2::sim::Coord{}; tree.layer=w::Layer::Ground;
        s.entities.push_back(tree); s.creation_order.push_back({w::CreatedKind::Entity,3});
        s.cells.at(91*512+91).heads->ground_head=3;
        expect(deploy(s,f,c,2,true),Status::Rejected,"deploy onto a tree");
        require(s.entities[1].lifetime==w::Lifetime::Placed,"unit kept after a failed deploy");
    }
    auto s=world(c,0x8000); auto f=flags();
    const auto before=s.random;
    require(option(options(s,f,c,1),"GAPOWR")==CanBuild::Cannot,"no power plant before the yard");
    const auto d=deploy(s,f,c,2,true);
    expect(d,Status::Done,"deploy AMCV");
    const auto* yard=[&]{ for (const auto& e:s.entities) if (e.id==*d.entity) return &e; return (const w::Entity*)nullptr; }();
    require(yard && yard->type=="GACNST" && yard->cell->x==89 && yard->cell->y==89,"yard at unit cell minus one");
    require(s.random.first!=before.first || s.random.second!=before.second,"constructor drew one RNG word");
    require(s.entities[0].lifetime==w::Lifetime::Destroyed,"AMCV removed");
    require(s.cells.at(90*512+90).heads->ground_head==*d.entity,"yard owns the old unit cell");
    require(*s.cells.at(90*512+90).ground_occupancy_124==0x100,"unit bit cleared, upper bits kept");
    for (int y=89;y<93;++y) for (int x=89;x<93;++x) require(s.cells.at(std::uint32_t(y*512+x)).heads->ground_head==*d.entity,"yard footprint linked");
    require((*s.houses[0].buildings_active_5460)[c.find("GACNST")->heap_index]==1,"yard active");
    std::cout<<"AMCV deployed into GACNST at (89,89)\n";
    std::uint64_t frame=0;
    expect(tick(s,f,c,++frame),Status::Done,"tick after deploy");
    const auto opts=options(s,f,c,1);
    require(option(opts,"GAPOWR")==CanBuild::Can,"power plant buildable");
    require(option(opts,"GAPILE")==CanBuild::Cannot,"barracks needs power");
    require(option(opts,"E1")==CanBuild::Cannot,"infantry needs barracks");
    produce(s,f,c,"GAPOWR",frame);
    require(*s.houses[0].cash==10000-800,"power plant cost paid");
    expect(begin_production(s,f,c,1,"GAPOWR",frame),Status::Rejected,"no second building while one waits");
    // Placement: far away fails proximity; next to the yard passes.
    require(passes_proximity(s,f,c,1,"GAPOWR",{115,115})==false,"far cell fails proximity");
    require(passes_proximity(s,f,c,1,"GAPOWR",{93,89})==true,"adjacent cell passes proximity");
    expect(place(s,f,c,1,"GAPOWR",{90,90},frame),Status::Rejected,"cannot place on the yard");
    expect(place(s,f,c,1,"GAPOWR",{93,89},frame),Status::Done,"place power plant");
    expect(tick(s,f,c,++frame),Status::Done,"power tick");
    require(*s.houses[0].power_output==200 && *s.houses[0].power_drain==0,"power recalculated");
    require(option(options(s,f,c,1),"GAPILE")==CanBuild::Can,"barracks after power");
    produce(s,f,c,"GAPILE",frame);
    require(passes_proximity(s,f,c,1,"GAPILE",{89,93})==true,"barracks next to yard");
    expect(place(s,f,c,1,"GAPILE",{89,93},frame),Status::Done,"place barracks");
    expect(tick(s,f,c,++frame),Status::Done,"power tick 2");
    require(*s.houses[0].power_drain==10,"barracks drain");
    require(option(options(s,f,c,1),"E1")==CanBuild::Can,"GI buildable");
    produce(s,f,c,"E1",frame);
    // A wall: the GAWALL overlay index is the [OverlayTypes] position of the
    // same name. place() writes the overlay and the owner on the Cell and
    // clears the factory product like any placed building.
    {
        const ra2::sim::production::CellXY yard_at{std::int16_t(yard->cell->x),std::int16_t(yard->cell->y)};
        const auto gawall=c.find("GAWALL");
        require(gawall && gawall->wall,"GAWALL is a wall type");
        int overlay=-1;
        for (std::size_t i=0;i<c.overlay_names.size();++i) if (c.overlay_names[i]=="GAWALL") overlay=int(i);
        require(overlay>=0,"GAWALL has an overlay entry");
        produce(s,f,c,"GAWALL",frame);
        const auto wall_product=[&]{ for (const auto& x:f.factories) if (x.house_id==1 && x.slot==5) return &x; return (const FactoryState*)nullptr; };
        const auto* factory=wall_product();
        require(factory && factory->object && factory->progress==54,"wall product waits in the factory");
        const ra2::sim::production::CellXY at{std::int16_t(yard_at.x+5),std::int16_t(yard_at.y+5)};
        expect(place(s,f,c,1,"GAWALL",at,frame),Status::Done,"place wall");
        // The wall becomes an overlay only: no live Structure stays on the map.
        for (const auto& e:s.entities)
            require(!(e.type=="GAWALL" && e.kind==w::Kind::Structure && e.lifetime==w::Lifetime::Placed),
                "a wall left a live Structure behind");
        auto& cell=s.cells.at(std::uint32_t(at.y)*512+std::uint32_t(at.x));
        require(cell.terrain && *cell.terrain->overlay_type_index==overlay,"wall overlay written on the Cell");
        require(cell.wall_owner && *cell.wall_owner==1,"wall owner written on the Cell");
        factory=wall_product(); // place() may move the factory vector.
        require(factory && !factory->object && factory->object_type.empty() && factory->progress==0 && factory->suspended,
            "factory product cleared after the wall placement");
        // An occupied Cell rejects the wall and keeps the product.
        produce(s,f,c,"GAWALL",frame);
        const auto* second=[&]{ for (const auto& x:f.factories) if (x.house_id==1 && x.slot==5) return &x; return (const FactoryState*)nullptr; }();
        require(second && second->object,"second wall product waits in the factory");
        (void)second;
        const auto before=s.entities.size();
        expect(place(s,f,c,1,"GAWALL",{yard_at.x,yard_at.y},frame),Status::Rejected,
            "wall on the yard is rejected");
        require(s.entities.size()==before,"a rejected wall placement changes nothing");
        expect(place(s,f,c,1,"GAWALL",at,frame),Status::Rejected,"wall on its own wall is rejected");
    }
    std::cout<<"cash left "<<*s.houses[0].cash<<", frame "<<frame<<'\n';
    require(w::validate(s),"snapshot stays valid");
    std::cout<<"Connected production run: "<<checks<<" checks passed\n";
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
