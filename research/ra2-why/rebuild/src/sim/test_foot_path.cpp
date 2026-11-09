#include "foot_path.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
void check(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
std::uint32_t packed(ra2map::Cell cell) { return cell.x|(static_cast<std::uint32_t>(cell.y)<<16); }
struct Fixture {
    FootPathState state;
    FootPathInputs input;
    Coord position;
    bool guard;
    ResolvedFootPath path;
    std::vector<std::array<std::uint32_t,4>> events;
    explicit Fixture(unsigned seed) {
        std::uint32_t value=seed^0x5197ac63u;
        const auto next=[&]() { value=value*1664525u+1013904223u; return value>>16; };
        input.goal.x=50+next()%9; input.goal.y=65+next()%9;
        position.x=(48+next()%16)*256+128; position.x+=next()%97;
        position.y=(64+next()%16)*256+128; position.y-=next()%97;
        position.z=(seed%5)*104;
        input.append_offset=next()%25; input.mark_mode=next()%3; guard=next()%5!=0;
        path.present=true; path.cost_key=next()%99+1;
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
    }
    FootPathCallbacks callbacks() {
        FootPathCallbacks cb;
        cb.can_queue=[&](Coord goal) { events.push_back({0,static_cast<unsigned>(goal.x),static_cast<unsigned>(goal.y),static_cast<unsigned>(goal.z)}); return guard; };
        cb.position=[&]() { return position; };
        cb.enter_goal=[&](ra2map::Cell goal) { events.push_back({1,packed(goal),0,0}); return 0; };
        cb.mark=[&](bool marked) { events.push_back({2,marked,0,0}); return false; };
        cb.find=[&](const FootPathQuery& query) {
            check(query.maximum==2000,"Foot wrapper search maximum differs");
            events.push_back({3,packed(query.goal),query.append_offset,query.mark_mode}); return path;
        };
        cb.current_cell=[&]() { return ra2map::Cell{static_cast<std::uint16_t>(position.x/256),static_cast<std::uint16_t>(position.y/256)}; };
        return cb;
    }
};
bool same(const FootPathState& a,const FootPathState& b) {
    return a.words==b.words && a.field_640==b.field_640 && a.field_644==b.field_644 && a.field_648==b.field_648 &&
        a.cell_558.has_value()==b.cell_558.has_value() && (!a.cell_558 || packed(*a.cell_558)==packed(*b.cell_558));
}
int main(int argc,char** argv) try {
    if (argc<3 || argc>4) throw std::runtime_error("usage: test_foot_path BASE_EXECUTABLE EXPANSION_EXECUTABLE [--vectors]");
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        std::uint32_t total=2166136261u;
        for (unsigned seed=0;seed<512;++seed) {
            Fixture f(seed);
            const auto result=original_unit_basic_path(f.state,f.input,distance,f.callbacks());
            check(result.status==FootPathStatus::Complete,"resolved ordinary Foot wrapper rejected input");
            check(result.accepted==f.guard,"Foot queue guard return differs");
            std::uint32_t h=2166136261u;
            const auto mix=[&](std::uint32_t value) { h=(h^value)*16777619u; };
            mix(result.accepted);
            for (const auto& word:result.state.words) mix(word.value());
            mix(result.state.field_640.value()); mix(result.state.field_644.value()); mix(result.state.field_648.value());
            mix(packed(result.state.cell_558.value())); mix(result.initial_distance.value_or(-1));
            for (const auto& event:f.events) for (auto value:event) mix(value);
            if (argc==4) std::cout<<edition<<" "<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<h<<std::dec<<"\n";
            total=(total^h)*16777619u;
        }
        check(total==0x7e173a4cu,"original Foot queue, distance, or callback order differs");
        if (argc==3) std::cout<<"edition "<<edition<<" ordinary Foot path digest "<<std::hex<<total<<std::dec<<"\n";
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
    cb.enter_goal=[](ra2map::Cell) { return 6; }; reject(f,cb,FootPathStatus::Unsupported);
    cb.enter_goal=[](ra2map::Cell) { return 7; }; reject(f,cb,FootPathStatus::Unsupported); cb=f.callbacks();
    f.path.present=false; reject(f,cb,FootPathStatus::Unsupported); f.path.present=true;
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
    if (argc==3) std::cout<<"1024 original ordinary Foot vectors and "<<checks<<" input checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
