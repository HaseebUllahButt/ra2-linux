#include "foot_path.h"
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ra2::sim;
void check(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
std::uint32_t packed(ra2map::Cell cell) { return cell.x|(static_cast<std::uint32_t>(cell.y)<<16); }
enum class FootProfile { Near,Ordinary,Asymmetric,Distant,Retarget };
struct Fixture {
    FootPathState state;
    FootPathInputs input;
    Coord position;
    bool guard;
    ResolvedFootPath path;
    int entry=0;
    std::optional<bool> above_bridge;
    bool bridge_goal=false;
    bool distant=false,retarget=false,secondary=false;
    unsigned seed=0;
    FootGoalContents contents;
    FootFailureHouse house;
    std::optional<bool> failure_mode;
    std::optional<ra2map::Cell> destination;
    std::optional<unsigned> mission;
    ra2map::Cell fallback_goal;
    ra2map::Cell candidate;
    std::uint32_t component=0;
    int estimate=0;
    bool retarget_goal_bridge=false,retarget_candidate_bridge=false;
    std::vector<std::array<std::uint32_t,4>> events;
    explicit Fixture(unsigned number,FootProfile profile=FootProfile::Near,DriveEdition edition=DriveEdition::BaseRa2):
        distant(profile==FootProfile::Distant),retarget(profile==FootProfile::Retarget),seed(number) {
        const bool ordinary=profile==FootProfile::Ordinary,asymmetric=profile==FootProfile::Asymmetric;
        std::uint32_t value=seed^0x5197ac63u;
        const auto next=[&]() { value=value*1664525u+1013904223u; return value>>16; };
        input.goal.x=50+next()%9; input.goal.y=65+next()%9;
        position.x=(48+next()%16)*256+128; position.x+=next()%97;
        position.y=(64+next()%16)*256+128; position.y-=next()%97;
        position.z=(seed%5)*104;
        path.present=!asymmetric && !distant && (ordinary || seed%9!=0);
        if (!path.present) {
            position.x=(int(input.goal.x)+int(seed%3)-1)*256+128;
            position.y=(int(input.goal.y)+int((seed/3)%3)-1)*256+128;
        }
        if (distant) {
            position.x=(int(input.goal.x)+2+int(seed%3))*256+128+int(seed%13)-6;
            position.y=(int(input.goal.y)+int((seed/3)%5)-2)*256+128+int(seed%17)-8;
            if (seed%5==0) {
                position.x=(int(input.goal.x)+int(seed%3)-1)*256+128+int(seed%13)-6;
                position.y=(int(input.goal.y)+int((seed/3)%3)-1)*256+128+int(seed%17)-8;
                bridge_goal=true;
            }
        }
        input.append_offset=next()%25; input.mark_mode=next()%3; guard=next()%5!=0;
        path.cost_key=next()%99+1;
        const auto count=next()%48;
        if (seed%11==0) path.cost_key=0;
        path.words=std::vector<std::int32_t>{};
        for (unsigned i=0;i<count;++i) path.words->push_back(next()%8);
        path.words->push_back(-1);
        for (unsigned i=0;i<24;++i) state.words[i]=(seed+i)%8;
        if (seed%7==0) state.words[0]=-1;
        state.field_640=0x12345678; state.field_644=0x3456789a; state.field_648=0x56789abc;
        state.cell_558=ra2map::Cell{0xef01,0xabcd};
        input.frame=1000+seed; input.is_train=false;
        input.field_5d4_is_null=true; input.field_6c8_is_null=true; input.field_6d0=false;
        entry=distant && seed%4==0?7:ordinary?0:seed%4==0?6:seed%6;
        constexpr double delays[]={.01,0.,.333,1./900,1.23456789,-.01,2.};
        input.path_delay=delays[seed%7]; above_bridge=bridge_goal?false:seed%2==0;
        input.invalid_cell=seed%2?ra2map::Cell{0xffff,0xffff}:ra2map::Cell{0x7fff,0x7fff};
        fallback_goal=seed%3==0?*input.invalid_cell:ra2map::Cell{static_cast<std::uint16_t>(50+seed%7),static_cast<std::uint16_t>(64+seed%7)};
        house.field_1ec=seed%5==0?2:0; house.field_1ed=seed%3==0?255:0; failure_mode=seed%2==0;
        contents.enabled=seed%3!=0;
        if (*contents.enabled) {
            contents.ordered_kinds=std::vector<std::optional<int>>{};
            if (seed%3==2) contents.ordered_kinds->push_back(1);
        }
        if (retarget) {
            position.x=int(input.goal.x)*256+128+1024+int(seed%3)*512+int(seed%19)-9;
            position.y=int(input.goal.y)*256+128+(int((seed/3)%3)-1)*256+int(seed%23)-11;
            if (seed%8==4) position={int(input.goal.x)*256+128+1024,int(input.goal.y)*256+128,0};
            entry=seed%3==0?7:6; path.present=seed%7!=0; input.close_enough=576;
            input.retarget=FootRetargetType{edition,int(seed%original_movement_zone_count(edition)),int((seed/5)%8),false};
            constexpr int offsets[][2]={{0,0},{0,0},{1,0},{3,0},{4,0},{12,0},{-2,3},{2,-1}};
            const auto& delta=offsets[seed%8];
            candidate={static_cast<std::uint16_t>(int(input.goal.x)+delta[0]),static_cast<std::uint16_t>(int(input.goal.y)+delta[1])};
            if (seed%8==0 && entry==6) candidate=*input.invalid_cell;
            const int gap=std::max(std::abs(delta[0]),std::abs(delta[1]));
            const int estimates[]={gap+6,gap+7,0x7fffffff,-1}; estimate=estimates[(seed/8)%4];
            component=seed%11==0?0xffffu:seed%13==0?0xffffffffu:100+seed;
            retarget_goal_bridge=seed%2!=0; retarget_candidate_bridge=(seed/2)%2!=0;
            contents.enabled=true; contents.ordered_kinds=std::vector<std::optional<int>>{1,6};
        }
    }
    FootPathCallbacks callbacks() {
        FootPathCallbacks cb;
        cb.can_queue=[&](Coord goal) { events.push_back({0,static_cast<unsigned>(goal.x),static_cast<unsigned>(goal.y),static_cast<unsigned>(goal.z)}); return guard; };
        cb.position=[&]() { return position; };
        cb.enter_goal=[&](ra2map::Cell goal) {
            if (retarget) events.push_back({10,packed(goal),0,0});
            events.push_back({1,packed(goal),0,0}); return entry;
        };
        cb.mark=[&](bool marked) { events.push_back({2,marked,0,0}); return false; };
        cb.find=[&](const FootPathQuery& query) {
            check(query.maximum==2000,"Foot wrapper search maximum differs");
            events.push_back({3,packed(query.goal),query.append_offset,query.mark_mode}); return path;
        };
        cb.current_cell=[&]() { return ra2map::Cell{static_cast<std::uint16_t>(position.x/256),static_cast<std::uint16_t>(position.y/256)}; };
        cb.stop_locomotor=[&]() {
            events.push_back({4,0,0,0});
            if (distant && seed%11==0) {
                position.x=int(input.goal.x)*256+128+301; position.y=int(input.goal.y)*256+128-87;
            }
            return false;
        };
        cb.above_bridge=[&]() { return above_bridge; };
        cb.goal_bridge=[&](ra2map::Cell goal) {
            if (!retarget) return bridge_goal;
            events.push_back({10,packed(goal),0,0});
            return packed(goal)==packed(input.goal)?retarget_goal_bridge:retarget_candidate_bridge;
        };
        if (distant || retarget) {
            cb.goal_contents=[&](ra2map::Cell goal) {
                if (retarget) events.push_back({10,packed(goal),0,0});
                events.push_back({9,packed(goal),0,0}); return contents;
            };
            cb.set_destination=[&](std::optional<ra2map::Cell> goal,bool flag) {
                if (retarget && goal) events.push_back({10,packed(*goal),0,0});
                events.push_back({5,goal.has_value(),goal?packed(*goal):0,flag}); destination=goal;
                if (!goal && seed%7==0) house.field_1ec=1;
                return false;
            };
            cb.failure_slot_3c8=[&]() {
                events.push_back({6,0,0,0}); secondary=true;
                if (seed%4==0) house.field_1ed=0;
                return false;
            };
            cb.failure_house=[&]() { return house; };
            cb.failure_mode=[&]() { return failure_mode; };
            cb.queue_mission=[&](unsigned number,bool flag) {
                events.push_back({7,number,flag,0}); mission=number;
                if (number==11) failure_mode=(seed/2)%2!=0;
                return false;
            };
            cb.failure_goal=[&]() { events.push_back({8,0,0,0}); return fallback_goal; };
        }
        if (retarget) {
            cb.retarget_component=[&](ra2map::Cell source,unsigned zone,bool layer) {
                events.push_back({11,packed(source),zone,layer}); return component;
            };
            cb.nearest_cell=[&](const FootNearestQuery& query) {
                check(query.edition==input.retarget->edition,"Foot nearest-cell edition differs");
                events.push_back({12,packed(query.goal),packed(query.source),static_cast<std::uint32_t>(query.speed_type)});
                events.push_back({13,query.source_component,query.movement_zone,query.above_bridge}); return candidate;
            };
            cb.estimate_path=[&](const FootEstimateQuery& query) {
                events.push_back({14,packed(query.candidate),packed(query.goal),query.candidate_bridge});
                events.push_back({15,query.goal_bridge,static_cast<std::uint32_t>(query.movement_zone_override),static_cast<std::uint32_t>(estimate)}); return estimate;
            };
        }
        return cb;
    }
};
bool same(const FootPathState& a,const FootPathState& b) {
    return a.words==b.words && a.field_640==b.field_640 && a.field_644==b.field_644 && a.field_648==b.field_648 &&
        a.cell_558.has_value()==b.cell_558.has_value() && (!a.cell_558 || packed(*a.cell_558)==packed(*b.cell_558));
}
int main(int argc,char** argv) try {
    if (argc<3 || argc>4) throw std::runtime_error("usage: test_foot_path BASE_EXECUTABLE EXPANSION_EXECUTABLE [--vectors|--ordinary-vectors|--asymmetric-vectors|--distant-vectors|--retarget-vectors]");
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        for (unsigned profile=0;profile<(argc==3?5u:1u);++profile) {
            const bool ordinary=argc==3?profile==0:std::string(argv[3])=="--ordinary-vectors";
            const bool asymmetric=argc==3?profile==2:std::string(argv[3])=="--asymmetric-vectors";
            const bool trace=argc==4 && std::string(argv[3])=="--distant-trace";
            const bool distant=argc==3?profile==3:std::string(argv[3])=="--distant-vectors" || trace;
            const bool retarget=argc==3?profile==4:std::string(argv[3])=="--retarget-vectors";
            std::uint32_t total=2166136261u;
            std::array<unsigned,3> axes{};
            for (unsigned seed=0;seed<(trace?8u:512u);++seed) {
                Fixture f(seed,ordinary?FootProfile::Ordinary:asymmetric?FootProfile::Asymmetric:distant?FootProfile::Distant:retarget?FootProfile::Retarget:FootProfile::Near,
                    edition==1?DriveEdition::BaseRa2:DriveEdition::YurisRevenge);
                if (!retarget) f.input.close_enough=distance.between(f.position,{int(f.input.goal.x)*256+128,int(f.input.goal.y)*256+128,0})+seed%3;
                const auto result=original_unit_basic_path(f.state,f.input,distance,f.callbacks());
                check(result.status==FootPathStatus::Complete,"resolved ordinary Foot wrapper rejected input");
                check(result.accepted==(f.guard && f.path.present),"Foot queue guard or null path return differs");
                if (asymmetric && f.guard) {
                    const int dx=f.position.x-(int(f.input.goal.x)*256+128),dy=f.position.y-(int(f.input.goal.y)*256+128);
                    check(result.state.field_644==dy,"null Foot path replaced the original Y delta");
                    axes[0]+=dx==0 && dy!=0; axes[1]+=dy==0 && dx!=0; axes[2]+=dx*dy<0;
                }
                std::uint32_t h=2166136261u;
                const auto mix=[&](std::uint32_t value) { h=(h^value)*16777619u; };
                mix(result.accepted);
                for (const auto& word:result.state.words) mix(word.value());
                mix(result.state.field_640.value()); mix(result.state.field_644.value()); mix(result.state.field_648.value());
                mix(packed(result.state.cell_558.value())); mix(result.initial_distance.value_or(-1));
                for (const auto& event:f.events) for (auto value:event) mix(value);
                if (distant || retarget) {
                    mix(f.destination?packed(*f.destination):0xffffffffu); mix(f.secondary); mix(f.mission.value_or(0xffffffffu));
                    mix(*f.failure_mode); mix(*f.house.field_1ec); mix(*f.house.field_1ed);
                    mix(f.position.x); mix(f.position.y); mix(f.position.z);
                }
                if (trace) {
                    std::cerr<<seed<<" fields "<<*result.state.field_640<<" "<<*result.state.field_644<<" "<<*result.state.field_648<<" owner "<<unsigned(*f.house.field_1ec)<<" "<<unsigned(*f.house.field_1ed)<<" mode "<<*f.failure_mode<<" mission "<<f.mission.value_or(0xffffffffu)<<" destination "<<(f.destination?packed(*f.destination):0xffffffffu)<<"\n";
                    for (const auto& event:f.events) std::cerr<<"event "<<event[0]<<" "<<event[1]<<" "<<event[2]<<" "<<event[3]<<"\n";
                }
                if (argc==4) std::cout<<edition<<" "<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<h<<std::dec<<"\n";
                total=(total^h)*16777619u;
            }
            if (!trace) check(total==(ordinary?0x7e173a4cu:asymmetric?0x131f4268u:distant?0xffdced71u:retarget?(edition==1?0xd51e380bu:0x22d0692fu):0xdd3d1138u),"original Foot queue, retarget, failure timer, distance, callback order, or private mutations differ");
            if (asymmetric) check(axes[0] && axes[1] && axes[2],"asymmetric Foot vectors missed zero axes or opposite signs");
            if (argc==3) std::cout<<"edition "<<edition<<(ordinary?" ordinary":asymmetric?" asymmetric":distant?" distant failure":retarget?" retarget":" resolved failure")<<" Foot path digest "<<std::hex<<total<<std::dec<<"\n";
        }
    }
    const auto distance=OriginalDistance::from_executable(argv[1]);
    unsigned checks=0;
    const auto reject=[&](Fixture& f,const FootPathCallbacks& cb,FootPathStatus status) {
        const auto result=original_unit_basic_path(f.state,f.input,distance,cb);
        check(result.status==status && same(result.state,f.state) && !result.detail.empty(),"Foot error published queue changes or lost its cause"); ++checks;
    };
    Fixture f(1); f.guard=true; auto cb=f.callbacks();
    auto saved=cb.can_queue; cb.can_queue={}; reject(f,cb,FootPathStatus::NeedsInput); cb.can_queue=saved;
    f.input.append_offset.reset(); reject(f,cb,FootPathStatus::NeedsInput);
    f.input.append_offset=25; reject(f,cb,FootPathStatus::Unsupported); f.input.append_offset=0;
    f.input.is_train=true; reject(f,cb,FootPathStatus::Unsupported); f.input.is_train=false;
    f.input.field_5d4_is_null=false; reject(f,cb,FootPathStatus::Unsupported); f.input.field_5d4_is_null=true;
    cb.enter_goal=[](ra2map::Cell) { return 6; }; reject(f,cb,FootPathStatus::NeedsInput);
    f.input.close_enough=-1; reject(f,cb,FootPathStatus::NeedsInput);
    cb.enter_goal=[](ra2map::Cell) { return 7; }; reject(f,cb,FootPathStatus::NeedsInput); cb=f.callbacks();
    f.path.present=false; f.input.path_delay.reset(); reject(f,cb,FootPathStatus::NeedsInput); f.path.present=true;
    f.path.cost_key.reset(); reject(f,cb,FootPathStatus::NeedsInput); f.path.cost_key=1;
    const auto words=f.path.words; f.path.words.reset(); reject(f,cb,FootPathStatus::NeedsInput); f.path.words=words;
    f.path.words->assign(2001,0); reject(f,cb,FootPathStatus::Unsupported); f.path.words=std::vector<std::int32_t>{0,-1};
    f.input.frame.reset(); reject(f,cb,FootPathStatus::NeedsInput); f.input.frame=1001;
    f.input.field_6c8_is_null=false; reject(f,cb,FootPathStatus::Unsupported);
    f.input.field_6d0.reset(); reject(f,cb,FootPathStatus::NeedsInput); f.input.field_6c8_is_null=true;
    cb.current_cell={}; reject(f,cb,FootPathStatus::NeedsInput); cb=f.callbacks();
    f.path.cost_key=0; f.state.words.fill(std::nullopt); f.input.append_offset=24;
    const auto unknown=original_unit_basic_path(f.state,f.input,distance,cb);
    check(unknown.status==FootPathStatus::Complete && !unknown.state.words[23],"Foot copy wrote unresolved queue tails"); ++checks;
    f.guard=false; f.input.is_train.reset(); f.input.field_5d4_is_null.reset();
    const auto blocked=original_unit_basic_path(f.state,f.input,distance,cb);
    check(blocked.status==FootPathStatus::Complete && !blocked.accepted && blocked.state.words[0]==-1 && blocked.state.field_640==f.state.field_640,"blocked Foot command read later unresolved input"); ++checks;
    Fixture failure(9); failure.guard=true; failure.entry=0; failure.input.append_offset=0;
    failure.position={int(failure.input.goal.x)*256+128,int(failure.input.goal.y)*256+128,0};
    failure.path.present=false; failure.above_bridge=false; auto failed_cb=failure.callbacks();
    failure.input.path_delay=std::numeric_limits<double>::quiet_NaN(); reject(failure,failed_cb,FootPathStatus::Unsupported);
    failure.input.path_delay=1e9; reject(failure,failed_cb,FootPathStatus::Unsupported); failure.input.path_delay=.01;
    failed_cb.stop_locomotor={}; reject(failure,failed_cb,FootPathStatus::NeedsInput); failed_cb=failure.callbacks();
    failure.above_bridge.reset(); reject(failure,failed_cb,FootPathStatus::NeedsInput); failure.above_bridge=false;
    failed_cb.goal_bridge={}; reject(failure,failed_cb,FootPathStatus::NeedsInput);
    failed_cb.goal_bridge=[](ra2map::Cell)->std::optional<bool> { return {}; }; reject(failure,failed_cb,FootPathStatus::NeedsInput);
    failed_cb.goal_bridge=[](ra2map::Cell) { return true; }; reject(failure,failed_cb,FootPathStatus::NeedsInput);
    failure.above_bridge=true; failed_cb.goal_bridge={};
    const auto bridge_layer=original_unit_basic_path(failure.state,failure.input,distance,failed_cb);
    check(bridge_layer.status==FootPathStatus::Complete && !bridge_layer.accepted && bridge_layer.state.field_648==9 && packed(*bridge_layer.state.cell_558)==packed(*failure.state.cell_558),"nearby failed path changed last cell, timer, or read an unused bridge flag"); ++checks;
    failed_cb=failure.callbacks(); failed_cb.stop_locomotor=[&]() { failure.position.x+=1024; return false; };
    reject(failure,failed_cb,FootPathStatus::NeedsInput);
    failure.position.x=int(failure.input.goal.x)*256+128;
    unsigned reads=0; failed_cb=failure.callbacks();
    failed_cb.position=[&]()->std::optional<Coord> { return ++reads==1?std::optional<Coord>{failure.position}:std::nullopt; };
    reject(failure,failed_cb,FootPathStatus::NeedsInput);
    failure.position={int(failure.input.goal.x)*256+128+301,int(failure.input.goal.y)*256+128-87,0};
    failed_cb=failure.callbacks();
    failed_cb.stop_locomotor=[&]() { failure.position.y+=99; return false; };
    const auto moved_stop=original_unit_basic_path(failure.state,failure.input,distance,failed_cb);
    check(moved_stop.status==FootPathStatus::Complete && moved_stop.state.field_644==-87,"Foot failure delta used the position after stop"); ++checks;
    Fixture building(1); building.guard=true; building.entry=7;
    FootGoalContents contents; auto building_cb=building.callbacks();
    building_cb.goal_contents=[&](ra2map::Cell) { return contents; };
    reject(building,building_cb,FootPathStatus::NeedsInput);
    contents.enabled=true; reject(building,building_cb,FootPathStatus::NeedsInput);
    contents.ordered_kinds=std::vector<std::optional<int>>{std::nullopt,6}; reject(building,building_cb,FootPathStatus::NeedsInput);
    contents.ordered_kinds=std::vector<std::optional<int>>{6,std::nullopt}; reject(building,building_cb,FootPathStatus::NeedsInput);
    contents.enabled=false; contents.ordered_kinds.reset();
    check(original_unit_basic_path(building.state,building.input,distance,building_cb).status==FootPathStatus::Complete,"disabled original Building lookup read unknown occupants"); ++checks;
    contents.enabled=true; contents.ordered_kinds=std::vector<std::optional<int>>{1,15,36};
    check(original_unit_basic_path(building.state,building.input,distance,building_cb).status==FootPathStatus::Complete,"known non-Building goal occupants triggered retargeting"); ++checks;
    Fixture distant(2,FootProfile::Distant); distant.guard=true; distant.entry=0; distant.input.append_offset=0;
    auto distant_cb=distant.callbacks(); distant_cb.set_destination={}; reject(distant,distant_cb,FootPathStatus::NeedsInput);
    distant_cb=distant.callbacks(); distant_cb.failure_slot_3c8={}; reject(distant,distant_cb,FootPathStatus::NeedsInput);
    distant_cb=distant.callbacks(); distant_cb.failure_house={}; reject(distant,distant_cb,FootPathStatus::NeedsInput);
    distant_cb=distant.callbacks(); distant.house.field_1ec.reset(); reject(distant,distant_cb,FootPathStatus::NeedsInput); distant.house.field_1ec=0;
    distant.failure_mode.reset(); reject(distant,distant_cb,FootPathStatus::NeedsInput);
    distant.failure_mode=false; distant.house.field_1ed.reset(); reject(distant,distant_cb,FootPathStatus::NeedsInput); distant.house.field_1ed=0;
    distant.failure_mode=true; distant_cb.queue_mission={}; reject(distant,distant_cb,FootPathStatus::NeedsInput);
    distant_cb=distant.callbacks(); distant_cb.failure_goal={}; reject(distant,distant_cb,FootPathStatus::NeedsInput);
    distant_cb=distant.callbacks(); distant.input.invalid_cell.reset(); reject(distant,distant_cb,FootPathStatus::NeedsInput); distant.input.invalid_cell=ra2map::Cell{0x7fff,0x7fff};
    unsigned mode_reads=0;
    distant_cb.failure_mode=[&]()->std::optional<bool> { return ++mode_reads==1?std::optional<bool>{true}:std::nullopt; };
    reject(distant,distant_cb,FootPathStatus::NeedsInput);
    distant_cb=distant.callbacks(); distant.house.field_1ec=1; distant.house.field_1ed.reset(); distant.failure_mode.reset();
    const auto known_first=original_unit_basic_path(distant.state,distant.input,distance,distant_cb);
    check(known_first.status==FootPathStatus::Complete && distant.mission==5,"known owner first flag read unused unknown state"); ++checks;
    Fixture target(2,FootProfile::Retarget); target.guard=true; auto target_cb=target.callbacks();
    const auto type=*target.input.retarget;
    target.input.retarget.reset(); reject(target,target_cb,FootPathStatus::NeedsInput); target.input.retarget=type;
    target.input.retarget->edition.reset(); reject(target,target_cb,FootPathStatus::NeedsInput); target.input.retarget=type;
    target.input.retarget->movement_zone.reset(); reject(target,target_cb,FootPathStatus::NeedsInput);
    target.input.retarget->movement_zone=-1; reject(target,target_cb,FootPathStatus::Unsupported);
    target.input.retarget->movement_zone=12; reject(target,target_cb,FootPathStatus::Unsupported); target.input.retarget=type;
    target.input.retarget->field_d2c.reset(); reject(target,target_cb,FootPathStatus::NeedsInput);
    target.input.retarget->field_d2c=true; reject(target,target_cb,FootPathStatus::Unsupported); target.input.retarget=type;
    const auto layer=target.above_bridge; target.above_bridge.reset(); reject(target,target_cb,FootPathStatus::NeedsInput); target.above_bridge=layer;
    target_cb.current_cell={}; reject(target,target_cb,FootPathStatus::NeedsInput); target_cb=target.callbacks();
    target_cb.retarget_component={}; reject(target,target_cb,FootPathStatus::NeedsInput); target_cb=target.callbacks();
    target.input.retarget->speed_type.reset(); reject(target,target_cb,FootPathStatus::NeedsInput); target.input.retarget=type;
    target_cb.nearest_cell={}; reject(target,target_cb,FootPathStatus::NeedsInput); target_cb=target.callbacks();
    const auto sentinel=target.input.invalid_cell; target.input.invalid_cell.reset(); reject(target,target_cb,FootPathStatus::NeedsInput); target.input.invalid_cell=sentinel;
    const auto candidate=target.candidate; target.candidate={512,512}; reject(target,target_cb,FootPathStatus::Unsupported); target.candidate=candidate;
    target_cb.goal_bridge={}; reject(target,target_cb,FootPathStatus::NeedsInput); target_cb=target.callbacks();
    target_cb.estimate_path={}; reject(target,target_cb,FootPathStatus::NeedsInput); target_cb=target.callbacks();
    target_cb.set_destination={}; reject(target,target_cb,FootPathStatus::NeedsInput);
    target.estimate=8;
    const auto too_long=original_unit_basic_path(target.state,target.input,distance,target_cb);
    check(too_long.status==FootPathStatus::Complete && !target.destination,"retarget estimate above gap plus six was accepted"); ++checks;
    target.estimate=7; target_cb=target.callbacks(); target_cb.mark={};
    reject(target,target_cb,FootPathStatus::NeedsInput);
    check(target.destination && packed(*target.destination)==packed(candidate),"retarget destination did not precede marking in private state"); ++checks;
    target.destination.reset(); target.candidate=*sentinel; target_cb=target.callbacks(); target_cb.goal_bridge={}; target_cb.estimate_path={}; target_cb.set_destination={};
    const auto invalid=original_unit_basic_path(target.state,target.input,distance,target_cb);
    check(invalid.status==FootPathStatus::Complete && !target.destination,"invalid status-six candidate read later unknown bodies"); ++checks;
    Fixture blocked_goal(0,FootProfile::Retarget); blocked_goal.guard=true; blocked_goal.path.present=true;
    auto blocked_cb=blocked_goal.callbacks(); blocked_cb.goal_bridge={}; blocked_cb.estimate_path={}; blocked_goal.input.invalid_cell.reset();
    const auto unfiltered=original_unit_basic_path(blocked_goal.state,blocked_goal.input,distance,blocked_cb);
    check(unfiltered.status==FootPathStatus::Complete && blocked_goal.destination && packed(*blocked_goal.destination)==packed(blocked_goal.input.goal),"Building-goal retarget incorrectly used status-six filters"); ++checks;
    if (argc==3) std::cout<<"5120 original Foot vectors and "<<checks<<" input checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
