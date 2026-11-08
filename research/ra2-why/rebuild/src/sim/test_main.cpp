#include "sim.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ra2::sim;
namespace {
int checks=0;
void check(bool ok,const char* what) { ++checks; if (!ok) throw std::runtime_error(what); }
std::string digest(const Snapshot& s) {
    std::string out=std::to_string(s.frame)+":"+std::to_string(s.queued);
    for (const auto& e:s.entities) out+=e.type+e.house+std::to_string(e.hp.value_or(-1))+std::to_string(e.dead);
    for (const auto& h:s.houses) out+=h.name+std::to_string(h.cash.value_or(-1))+std::to_string(h.spent);
    for (const auto& r:s.results) out+=std::to_string(r.ticket)+std::to_string(static_cast<int>(r.error))+std::to_string(r.value)+r.detail;
    for (auto x:s.random_table) out+=":"+std::to_string(x);
    return out+":"+std::to_string(s.random_first)+":"+std::to_string(s.random_second);
}
}
int main(int argc,char** argv) try {
    const std::string root=argc>1?argv[1]:"/path/to/user/ra2/analysis/extracted";
    auto rules=Ini::from_file(root+"/config/rulesmd.ini");
    auto map=Ini::from_file(root+"/maps/AUSTINTX.MAP");
    Simulation a(rules,map,1), b(rules,map,1);
    auto snap=a.snapshot();
    check(!snap.entities.empty(),"real map entities");
    const auto& tank=a.types().at("HTNK");
    check(tank.strength==rules.read_integer("HTNK","Strength"),"real tank Strength");
    check(tank.primary==rules.read_string("HTNK","Primary"),"real weapon reference");
    const auto& weapon=a.weapons().at(tank.primary);
    check(weapon.damage==rules.read_integer(tank.primary,"Damage"),"real weapon damage");
    check(weapon.warhead==rules.read_string(tank.primary,"Warhead"),"real warhead reference");
    const auto units=map.find_section("Units");
    check(units && !units->entries.empty(),"original map unit rows");
    std::uint32_t vehicle=0;
    for (const auto& e:snap.entities) if (e.kind==Kind::Unit) { vehicle=e.id; break; }
    check(vehicle!=0,"vehicle imported");
    auto row=Ini::split_list(units->entries.front().value);
    const auto& first=snap.entities[vehicle-1];
    check(first.type==row[1] && first.house==row[0],"unit type and owner");
    check(first.cell.x==*Ini::integer_value(row[3]) && first.cell.y==*Ini::integer_value(row[4]),"unit cell");
    check(first.facing==*Ini::integer_value(row[5]) && first.mission==row[6],"unit facing and mission");
    Command c; c.op=Op::ResolvedDamage; c.entity=vehicle; c.amount=weapon.damage.value();
    check(a.issue(c).error==Error::None && b.issue(c).error==Error::None,"damage accepted");
    c.op=Op::RandomRange; c.amount=-10; c.high=17; c.frame=2;
    a.issue(c); b.issue(c);
    c.op=Op::ResolvedDamage; c.amount=std::numeric_limits<int>::max(); c.frame=4;
    a.issue(c); b.issue(c);
    c.amount=-100; c.frame=5; a.issue(c); b.issue(c);
    for (int i=0;i<10;++i) {
        a.tick(); b.tick(); check(digest(a.snapshot())==digest(b.snapshot()),"replay equality each tick");
        if (i==0) check(a.snapshot().entities[vehicle-1].hp==std::max(0,first.hp.value()-weapon.damage.value()),"damage arithmetic");
    }
    check(a.snapshot().entities[vehicle-1].dead && a.snapshot().entities[vehicle-1].hp==0,"death and no resurrection");
    c.frame=0; check(a.issue(c).error==Error::PastFrame,"past command rejected");
    for (auto op:{Op::Move,Op::Attack,Op::Produce,Op::Stop}) {
        c.frame=10; c.op=op; check(a.issue(c).error==Error::Unsupported,"unsupported gameplay rejected");
    }
    auto old=a.snapshot(); old.entities.clear(); check(!a.snapshot().entities.empty(),"snapshot cannot mutate simulation");
    for (const auto* file:{"2PEAKS.MAP","C1A02MD.MAP"}) {
        auto original_map=Ini::from_file(root+"/maps/"+file);
        Simulation imported(rules,original_map,1);
        const auto imported_state=imported.snapshot();
        std::size_t count=0;
        for (const auto* category:{"Structures","Units","Infantry","Aircraft"})
            if (const auto* section=original_map.find_section(category)) count+=section->entries.size();
        check(imported_state.entities.size()==count,"all real map object categories imported");
        std::cout<<file<<" objects="<<count<<"\n";
    }
    // Test original unit row health boundary conversion with real Strength.
    Ini slice;
    std::string input="[Houses]\n0="+first.house+"\n["+first.house+"]\nCredits=0\n[Units]\n";
    for (int health:{0,1,128,255,256}) {
        row[2]=std::to_string(health); input+=std::to_string(health)+"=";
        for (std::size_t j=0;j<row.size();++j) input+=(j?",":"")+row[j];
        input+="\n";
    }
    slice.parse(input); Simulation partial(rules,slice,0);
    const auto maximum=partial.types().at(first.type).strength.value();
    auto ps=partial.snapshot();
    check(ps.entities[0].hp==1,"zero map health becomes one");
    check(ps.entities[2].hp==maximum/2,"half map health");
    check(ps.entities[4].hp==maximum,"full map health");
    // Use map's original house and explicit original Credits units.
    slice.parse("[Houses]\n0="+first.house+"\n["+first.house+"]\nCredits=10\n");
    Simulation money(rules,slice,1);
    c={}; c.op=Op::TakeCash; c.house=first.house; c.amount=700;
    money.issue(c); money.issue(c); money.tick();
    check(money.snapshot().houses[0].cash==0 && money.snapshot().houses[0].spent==1000,"cash-only debit saturates");
    check(money.snapshot().results[0].value==700 && money.snapshot().results[1].value==300,"FIFO debit values");
    c.frame=1; c.amount=-1; money.issue(c); money.tick();
    check(money.snapshot().results[0].error==Error::InvalidAmount,"negative debit rejected");
    // Golden output from original 0065C6D0/0065C780 x86 instructions, emulated with Unicorn.
    const std::uint32_t seeds[]={0,1,0xffffffffu};
    const std::uint32_t golden[][5]={
        {0x49466cf3,0x6a2b5556,0x9089e025,0x9cefdb3b,0x06c7fb77},
        {0x78b76ed5,0x275d74ae,0xda63b931,0xacebbad2,0xe74254d1},
        {0xf9b05e3c,0xe8f24841,0x66815a3f,0x3b17004b,0x40f2b4d8}};
    for (unsigned i=0;i<3;++i) {
        Random original(seeds[i]);
        for (auto expected:golden[i]) check(original.next()==expected,"original x86 random golden vector");
    }
    Random r(1), same(1); auto table=r.table();
    check(r.ranged(7,7)==7 && r.first()==0,"equal range consumes no draw");
    for (unsigned i=0;i<1000;++i) {
        auto x=r.next(); check(x==same.next(),"seed repeat");
        if (i==0) check(x==(table[0]^table[103]),"original XOR recurrence");
    }
    check(r.first()==0 && r.second()==103,"both ring indices wrap");
    Random reverse(9), forward(9);
    for (int i=0;i<1000;++i) {
        auto n=reverse.ranged(17,-10); check(n>=-10 && n<=17 && n==forward.ranged(-10,17),"inclusive reverse range");
    }
    bool rejected=false;
    try { r.ranged(std::numeric_limits<int>::min(),std::numeric_limits<int>::max()); } catch(const std::range_error&) { rejected=true; }
    check(rejected,"overflow range explicit error");
    check(factory_stage_frames(0)==1 && factory_stage_frames(54)==1 && factory_stage_frames(108)==2 && factory_stage_frames(999999)==255,"factory interval clamps");
    House h; h.cash=tank.cost.value(); Factory f; f.begin(tank.cost.value(),108,0);
    f.tick(h,1); check(f.progress==0,"factory waits");
    for (int frame=2;frame<=108;++frame) f.tick(h,frame);
    check(f.done() && f.suspended && f.remaining_cost==0 && h.cash==0 && h.spent==tank.cost.value(),"54 paid factory stages");
    h.cash=0; h.spent=0; f.begin(tank.cost.value(),54,0); f.tick(h,1);
    check(f.progress==0 && f.insufficient_funds && f.remaining_cost==tank.cost.value(),"factory stalls without money");
    h.cash=tank.cost.value(); f.tick(h,2); check(f.progress==1 && !f.insufficient_funds,"factory resumes with money");
    std::cout<<checks<<" checks passed; AUSTINTX.MAP objects="<<snap.entities.size()<<", types="<<a.types().size()<<", weapons="<<a.weapons().size()<<"\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return EXIT_FAILURE; }
