#include "sim.h"
#include "ground.h"
#include "combat.h"
#include "target.h"
#include "distance.h"
#include "floor.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <filesystem>
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
    check(a.types().count("CALA02") && a.types().count("CALOND02"),"declared types survive missing rule sections");
    Ini missing_type;
    missing_type.parse("[Houses]\n0=Neutral\n[Units]\n0=Neutral,NOT_A_REGISTERED_UNIT,256,1,2,0,Guard\n1=Neutral,"+first.type+",256,54,69,0,Guard\n");
    Simulation skipped(rules,missing_type,0);
    check(skipped.snapshot().entities.size()==1 && skipped.snapshot().skipped_objects.size()==1,"original missing type branch skips with diagnostics");
    check(skipped.snapshot().entities[0].mission=="Guard","skipped rows preserve later mission fields");
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
    std::uint32_t ranged_hash=2166136261u;
    for (int i=0;i<1000;++i) {
        auto n=reverse.ranged(17,-10); ranged_hash=(ranged_hash^static_cast<std::uint32_t>(n))*16777619u; check(n>=-10 && n<=17 && n==forward.ranged(-10,17),"inclusive reverse range");
    }
    check(ranged_hash==0xb4adaea7u,"1000 original x86 ranged draws");
    bool rejected=false;
    try { r.ranged(std::numeric_limits<int>::min(),std::numeric_limits<int>::max()); } catch(const std::range_error&) { rejected=true; }
    check(rejected,"overflow range explicit error");
    check(factory_stage_frames(0)==1 && factory_stage_frames(54)==1 && factory_stage_frames(108)==2 && factory_stage_frames(999999)==255,"factory interval clamps");
    DamageInputs damage;
    damage.damage=weapon.damage.value(); damage.maximum=rules.read_integer("CombatDamage","MaxDamage");
    if (!damage.maximum) damage.maximum=rules.read_integer("General","MaxDamage");
    damage.cell_spread=static_cast<float>(rules.read_double("TankOGas","CellSpread"));
    damage.percent_at_max=static_cast<float>(rules.read_double("TankOGas","PercentAtMax"));
    const auto verses=rules.read_list("TankOGas","Verses");
    Ini verse_input; verse_input.parse("[Verse]\nValue="+verses.at(5)+"\n");
    damage.armor_verse=verse_input.read_double("Verse","Value");
    check(total_damage(damage)==damage.damage/4,"original armor multiplier");
    damage.distance=128; check(total_damage(damage)<=damage.damage/4,"original range falloff");
    const int distances[]={0,7,8,16,64,128,256};
    const int original_damage[4][7]={
        {22,22,22,22,21,20,17},{30,29,29,29,28,26,23},
        {2500,2486,2484,2468,2374,2249,1999},{-50,-50,0,0,0,0,0}};
    const int raw_damage[]={90,120,10000,-50};
    for (unsigned i=0;i<4;++i) for (unsigned j=0;j<7;++j) {
        damage.damage=raw_damage[i]; damage.distance=distances[j];
        const auto actual_damage=total_damage(damage);
        if (actual_damage!=original_damage[i][j]) std::cerr<<"damage="<<damage.damage<<" distance="<<damage.distance<<" actual="<<actual_damage<<" expected="<<original_damage[i][j]<<" maximum="<<damage.maximum<<"\n";
        check(actual_damage==original_damage[i][j],"original x86 total damage vector");
    }
    std::uint32_t damage_grid_hash=2166136261u;
    unsigned damage_grid_count=0;
    for (int raw:{90,120,10000}) {
        for (float spread:{0.f,.1f,.3f,.5f,.75f,1.5f}) {
            for (float percent:{0.f,.1f,.5f,.9f,1.f,2.f}) {
                for (double verse:{0.,.1,.25,.9,1.,2.}) {
                    for (int distance:{0,1,7,8,16,64,128,256,1024}) {
                        DamageInputs grid; grid.damage=raw; grid.cell_spread=spread;
                        grid.percent_at_max=percent; grid.armor_verse=verse; grid.distance=distance; grid.maximum=10000;
                        const auto value=total_damage(grid);
                        damage_grid_hash=(damage_grid_hash^static_cast<std::uint32_t>(value))*16777619u;
                        ++damage_grid_count;
                    }
                }
            }
        }
    }
    check(damage_grid_count==5832 && damage_grid_hash==0xaff8a2e1u,"5832 original x86 armor/range cases");
    damage.cell_spread=.5f;
    damage.damage=-50; damage.distance=7; check(total_damage(damage)==-50,"original healing distance seven");
    damage.distance=8; check(total_damage(damage)==0,"original healing distance eight");
    damage.scenario_inert=true; check(total_damage(damage)==0,"original inert scenario damage");
    check(select_resolved_table_target({{1,true,0},{2,true,0}})==1,"original score tie keeps first");
    check(select_resolved_table_target({{1,false,100},{2,true,1},{3,true,2}})==3,"original eligible maximum");
    check(!select_resolved_table_target({{1,true,-1}}),"original score minus one excluded");
    check(threat_categories(0x100)==0x8042 && threat_categories(4)==4 && threat_categories(0)==0,"original threat category conversion");
    std::uint32_t floor_hash=2166136261u; unsigned floor_count=0;
    const int local_points[][2]={{0,0},{0,255},{255,0},{255,255},{128,128},{64,192},{192,64},{-1,-1}};
    for (int level:{-1,0,1,7}) for (int ramp=0;ramp<=20;++ramp) for (const auto& point:local_points) {
        const auto value=original_floor_height(static_cast<std::int8_t>(level),static_cast<std::uint8_t>(ramp),point[0],point[1]);
        floor_hash=(floor_hash^static_cast<std::uint32_t>(value))*16777619u; ++floor_count;
    }
    check(floor_count==672 && floor_hash==0xdbba8a41u,"672 original floor and ramp cases");
    TerrainCell known_floor; known_floor.cell=first.cell;
    known_floor.floor_world_height=original_floor_height(0,0,first.cell.x*256+128,first.cell.y*256+128);
    Simulation located(rules,map,1,TerrainCells{known_floor});
    const auto location=located.snapshot().entities[vehicle-1].position;
    check(location && location->x==first.cell.x*256+128 && location->y==first.cell.y*256+128 && location->z==0,"world input resolves original unit center");
    check(!a.snapshot().entities[vehicle-1].position,"unknown floor leaves world position explicit");
    const auto distance_table=OriginalDistance::from_executable(root+"/../../game/game.exe");
    check(distance_table.square_root(0)==0,"original zero distance");
    check(distance_table.between({0,0,0},{3,4,0})==5,"original table distance");
    check(distance_table.between({0,0,0},{0,0,256},true)==0,"original planar distance");
    check(distance_table.between({0,0,0},{0,0,256})==256,"original vertical distance");
    check(distance_table.between({0,0,0},{1792,0,0})==1792,"original threat distance input");
    const auto expansion_distance=OriginalDistance::from_executable(root+"/../../game/gamemd.exe");
    const double squared_vectors[]={0,1,2,3,25,10000,3211264,123456789,68719476736.0};
    const std::uint32_t distance_bits[]={0,0x3f800000,0x3fb504f3,0x3fddb3d7,0x40a00000,0x42c80000,0x44e00000,0x462d9bd7,0x48800000};
    for (unsigned i=0;i<9;++i) {
        const float value=distance_table.square_root(squared_vectors[i]);
        std::uint32_t bits; std::memcpy(&bits,&value,4);
        check(bits==distance_bits[i],"original lookup square root bits");
        check(expansion_distance.square_root(squared_vectors[i])==value,"base and expansion distance table parity");
    }
    ThreatScoreInputs scoring;
    scoring.coefficients={rules.read_double("General","MyEffectivenessCoefficientDefault"),
        rules.read_double("General","TargetEffectivenessCoefficientDefault"),
        rules.read_double("General","TargetSpecialThreatCoefficientDefault"),
        rules.read_double("General","TargetStrengthCoefficientDefault"),
        rules.read_double("General","TargetDistanceCoefficientDefault")};
    scoring.target_health=tank.strength.value(); scoring.target_strength=tank.strength.value();
    scoring.range_world_units=1280; scoring.resolved_distance_world_units=1792;
    scoring.forward_armor_verse=1; scoring.reverse_armor_verse=1;
    check(threat_score(scoring)==99780,"original threat formula with real coefficients");
    scoring.target_targets_attacker=true;
    check(threat_score(scoring)==100180,"original retaliation sign");
    scoring.preferred_enemy_house=true; scoring.enemy_house_bonus=rules.read_double("General","EnemyHouseThreatBonus");
    check(threat_score(scoring)==100580,"original preferred enemy bonus");
    scoring.explicit_scan_origin=true;
    check(threat_score(scoring)==82730,"original explicit scan origin distance units");
    scoring.target_type_present=false;
    check(threat_score(scoring)==0,"missing original target type score");
    CandidatePrefix prefix; prefix.edition=CandidateEdition::YuriRevenge;
    check(check_candidate_prefix(prefix).status==CandidateStatus::NeedsInput,"missing target evidence stays explicit");
    prefix.threat=0; prefix.fire_status=0; prefix.initial_health=256;
    prefix.weapon_warhead_present=true; prefix.armor_verse=1;
    prefix.attacker_type_604=0; prefix.target_flag_81=false;
    prefix.current_health=100; prefix.target_state_220=0;
    prefix.target_flag_3d5=true; prefix.mission_control_flag_4=false; prefix.target_height=0;
    check(check_candidate_prefix(prefix).status==CandidateStatus::Continue,"verified target prefix continues without claiming eligibility");
    auto trial=prefix; trial.fire_status=5;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original fire status five rejection");
    trial.threat=0x18200; check(check_candidate_prefix(trial).status==CandidateStatus::Continue,"original threat mask bypass");
    trial=prefix; trial.initial_health=0; trial.attacker_type_394=2;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original initial health category check");
    trial=prefix; trial.armor_verse=static_cast<double>(.02f);
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original two percent verse boundary");
    trial.armor_verse=std::numeric_limits<double>::quiet_NaN();
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original unordered verse rejection");
    trial=prefix; trial.attacker_type_604=2; trial.target_slot_50=true; trial.target_cell_ec=1;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original cell category check");
    trial=prefix; trial.target_flag_81=true;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original target flag check");
    trial=prefix; trial.current_health=0;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original zero health rejection");
    trial=prefix; trial.target_state_220=2; trial.cell_visible=false; trial.same_house=false;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original concealed foreign owner check");
    trial.same_house=true; check(check_candidate_prefix(trial).status==CandidateStatus::Continue,"original same house exception");
    trial=prefix; trial.target_flag_3d5=false;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original inactive target check");
    trial=prefix; trial.mission_control_flag_4=true;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original mission control rejection");
    trial=prefix; trial.target_height=-21;
    check(check_candidate_prefix(trial).status==CandidateStatus::Reject,"original height below minus twenty");
    trial.target_height=-20; check(check_candidate_prefix(trial).status==CandidateStatus::Continue,"original height boundary");
    auto base_prefix=prefix; base_prefix.edition=CandidateEdition::BaseRa2;
    base_prefix.armor_verse=0;
    check(check_candidate_prefix(base_prefix).status==CandidateStatus::Continue,"base zero verse preserves original prefix behavior");
    base_prefix.armor_verse=.01;
    check(check_candidate_prefix(base_prefix).status==CandidateStatus::Reject,"base one percent verse rejection");
    base_prefix.armor_verse=.02;
    check(check_candidate_prefix(base_prefix).status==CandidateStatus::Continue,"base two percent verse survives prefix");
    base_prefix.attacker_type_604=2; base_prefix.target_slot_50=true; base_prefix.target_cell_ec=1;
    check(check_candidate_prefix(base_prefix).status==CandidateStatus::Continue,"base omits expansion cell gate");
    GroundMotion ground;
    GroundGuards guards;
    auto center=ground_cell_center(54,69);
    check(center.x==13952 && center.y==17792,"original cell center coordinates");
    check(ground.move_to(center,guards),"ground destination accepted");
    check(ground.destination->x==center.x,"ground destination retained");
    for (unsigned bit=0;bit<4;++bit) {
        GroundGuards blocked;
        if (bit==0) blocked.slot_37c=true;
        if (bit==1) blocked.slot_380=true;
        if (bit==2) blocked.slot_1d4=true;
        if (bit==3) blocked.slot_1d8=true;
        check(!ground.move_to(Coord{1,2,3},blocked) && ground.destination->x==center.x,"all original move guards");
    }
    check(ground.move_to(Coord{1,2,3},guards,400) && ground.destination->z==403,"resolved bridge adjustment");
    check(ground.move_to(std::nullopt,guards) && !ground.destination,"empty destination");
    ground.anchor=Coord{100,200,300}; ground.track=0;
    auto sample=ground.transform({17,-9,42},23);
    check(sample.x==117 && sample.y==191 && sample.facing==23,"unreflected ground curve");
    const int ground_golden[][3]={
#include "ground_golden.inc"
    };
    for (int track=0;track<64;++track) {
        ground.track=track; const auto result=ground.transform({17,-9,42},23);
        check(result.x==ground_golden[track][0] && result.y==ground_golden[track][1] && result.facing==ground_golden[track][2],"original x86 curve result");
    }
    ground.track=8; sample=ground.transform({17,-9,42},23);
    check(sample.x==109 && sample.y==183 && sample.facing==41,"original reflected ground curve");
    for (unsigned i=0;i<24;++i) ground.path[i]=static_cast<int>(i%8);
    ground.consume_one_path_direction();
    check(ground.path[0]==1 && ground.path[22]==7 && ground.path[23]==7,"original 23 direction shift");
    House h; h.cash=tank.cost.value(); Factory f; f.begin(tank.cost.value(),108,0);
    f.tick(h,1); check(f.progress==0,"factory waits");
    std::uint32_t factory_hash=2166136261u;
    auto hash_factory=[&](){
        for (auto value:{static_cast<std::uint32_t>(f.progress),static_cast<std::uint32_t>(f.remaining_cost),static_cast<std::uint32_t>(h.cash.value()),static_cast<std::uint32_t>(h.spent)})
            factory_hash=(factory_hash^value)*16777619u;
    };
    hash_factory();
    for (int frame=2;frame<=108;++frame) { f.tick(h,frame); hash_factory(); }
    check(factory_hash==0x72681cc7u,"108-frame original x86 factory replay");
    check(f.done() && f.suspended && f.remaining_cost==0 && h.cash==0 && h.spent==tank.cost.value(),"54 paid factory stages");
    h.cash=0; h.spent=0; f.begin(tank.cost.value(),54,0); f.tick(h,1);
    check(f.progress==0 && f.insufficient_funds && f.remaining_cost==tank.cost.value(),"factory stalls without money");
    h.cash=tank.cost.value(); f.tick(h,2); check(f.progress==1 && !f.insufficient_funds,"factory resumes with money");
    if (argc>2 && std::string(argv[2])=="--all-maps") {
        std::size_t maps=0, imported=0, skipped_count=0;
        for (const auto& file:std::filesystem::directory_iterator(root+"/maps")) {
            if (file.path().extension()!=".MAP") continue;
            auto original_map=Ini::from_file(file.path().string());
            Simulation imported_map(rules,original_map,1);
            const auto view=imported_map.snapshot();
            std::size_t expected=0;
            for (const auto* category:{"Structures","Units","Infantry","Aircraft"})
                if (const auto* section=original_map.find_section(category)) expected+=section->entries.size();
            check(view.entities.size()+view.skipped_objects.size()==expected,"all-map object accounting");
            for (const auto& object:view.skipped_objects)
                std::cout<<file.path().filename().string()<<" skipped "<<object.type<<": "<<object.reason<<"\n";
            ++maps; imported+=view.entities.size(); skipped_count+=view.skipped_objects.size();
        }
        std::cout<<"All-map audit: "<<maps<<" maps, "<<imported<<" imported objects, "<<skipped_count<<" explicitly skipped\n";
    }
    std::cout<<checks<<" checks passed; AUSTINTX.MAP objects="<<snap.entities.size()<<", types="<<a.types().size()<<", weapons="<<a.weapons().size()<<"\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return EXIT_FAILURE; }
