#include "test_rectangle_fit_inputs.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
using namespace ra2::sim::test;
void require(bool v,const char* t) { if (!v) throw std::runtime_error(t); }
int main(int argc,char** argv) try {
    if (argc>2 || (argc==2 && std::string(argv[1])!="--vectors")) throw std::runtime_error("expected optional --vectors");
    const bool vectors=argc==2;
    for (auto edition:{DriveEdition::BaseRa2,DriveEdition::YurisRevenge}) {
        std::uint32_t total=2166136261u;
        for (unsigned seed=0;seed<512;++seed) {
            RectangleFitFixture f(seed,edition);const auto r=original_rectangle_fit(f.query,f.lookup(),f.callbacks());
            if (r.status!=CellFitStatus::Complete) throw std::runtime_error("rectangle fixture "+std::to_string(seed)+": "+r.detail);
            const auto h=f.digest(r);total=(total^h)*16777619u;
            if (vectors) std::cout<<(edition==DriveEdition::BaseRa2?"base ":"yuri ")<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<h<<std::dec<<"\n";
        }
        require(total==(edition==DriveEdition::BaseRa2?0x711b80feu:0xa18ead0du),"original rectangle lookup or predicate vectors differ");
        if (!vectors) std::cout<<"512 rectangle fit vectors "<<(edition==DriveEdition::BaseRa2?"base ":"yuri ")<<std::hex<<total<<std::dec<<"\n";
    }
    unsigned checks=0;RectangleFitQuery q;
    const auto fail=[&](RectangleFitQuery input,CellFitLookup lookup,CellFitCallbacks cb,CellFitStatus status) {
        const auto r=original_rectangle_fit(input,lookup,cb);
        require(r.status==status && !r.allowed && !r.blocked_cell && !r.detail.empty(),"rectangle error published a Cell or boolean");++checks;
    };
    fail(q,{},{},CellFitStatus::NeedsInput);
    fail(q,[](ra2map::Cell)->std::optional<CellFitState>{return {};},{},CellFitStatus::NeedsInput);
    fail(q,[](ra2map::Cell){return CellFitState{};},{},CellFitStatus::NeedsInput);
    q.width=513;fail(q,{},{},CellFitStatus::Unsupported);
    q.width=1;q.height=513;fail(q,{},{},CellFitStatus::Unsupported);
    q.width=0;auto r=original_rectangle_fit(q,{},{});
    require(r.status==CellFitStatus::Complete && r.allowed==true && r.tested==0,"zero width called unresolved services");++checks;
    q.width=1;q.height=-1;r=original_rectangle_fit(q,{},{});
    require(r.status==CellFitStatus::Complete && r.allowed==true && r.tested==0,"negative height changed empty rectangle behavior");++checks;
    q.height=1;q.reject_overlay=true;q.speed_type=4;
    fail(q,[](ra2map::Cell){return CellFitState{};},{},CellFitStatus::NeedsInput);
    CellFitState c;c.overlay_index=0;
    r=original_rectangle_fit(q,[&](ra2map::Cell){return c;},{});
    require(r.status==CellFitStatus::Complete && r.allowed==false && r.blocked_cell && r.tested==1,"overlay rejection did not precede speed-four bypass");++checks;
    q.reject_overlay=false;q.origin={0xffff,0xffff};q.width=2;q.height=2;
    std::vector<std::uint32_t> visits;
    r=original_rectangle_fit(q,[&](ra2map::Cell p){visits.push_back(fit_key(p));return CellFitState{};},{});
    require(r.status==CellFitStatus::Complete && r.allowed==true && r.tested==4 &&
        visits==std::vector<std::uint32_t>{0xffffffffu,0xffffu,0xffff0000u,0u},"rectangle lost X-outer order, wrapping, or speed-four bypass");++checks;
    q.width=2;q.height=2;q.reject_overlay=true;unsigned calls=0;c.overlay_index=-1;
    r=original_rectangle_fit(q,[&](ra2map::Cell){auto copy=c;if (++calls==3) copy.overlay_index=0;return copy;},{});
    require(r.status==CellFitStatus::Complete && r.allowed==false && r.tested==3 && fit_key(*r.blocked_cell)==0xffff0000u,"rectangle did not stop at the first rejected raw Cell");++checks;
    if (!vectors) std::cout<<"1024 original rectangle vectors and "<<checks<<" input checks pass\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n";return 1; }
