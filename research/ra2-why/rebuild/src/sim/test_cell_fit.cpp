#include "test_cell_fit_inputs.h"
#include "../rules/ini_file.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
using namespace ra2::sim::test;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char** argv) try {
    if (argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--vectors"))
        throw std::runtime_error("usage: test_cell_fit RULES_INI [--vectors]");
    const bool vectors=argc==3;
    const auto rules=ra2::rules::IniFile::from_file(argv[1]);
    for (const auto edition:{DriveEdition::BaseRa2,DriveEdition::YurisRevenge}) {
        std::uint32_t total=2166136261u;
        for (unsigned seed=0;seed<4096;++seed) {
            CellFitFixture f(seed,edition);const auto r=original_cell_fit(f.query,f.cell,f.callbacks());
            if (r.status!=CellFitStatus::Complete) throw std::runtime_error("Cell fit fixture "+std::to_string(seed)+": "+r.detail);
            const auto h=f.digest(r);total=(total^h)*16777619u;
            if (vectors) std::cout<<(edition==DriveEdition::BaseRa2?"base ":"yuri ")<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<h<<std::dec<<"\n";
        }
        require(total==(edition==DriveEdition::BaseRa2?0xa04450e6u:0xf80fc9b5u),"original Cell fit vectors differ");
        if (!vectors) std::cout<<"4096 Cell fit vectors "<<(edition==DriveEdition::BaseRa2?"base ":"yuri ")<<std::hex<<total<<std::dec<<"\n";
    }
    unsigned checks=0;
    const auto fail=[&](ResolvedCellFitQuery q,CellFitState c,CellFitCallbacks cb,CellFitStatus status) {
        const auto r=original_cell_fit(q,c,cb);require(r.status==status && !r.allowed && !r.detail.empty(),"Cell fit error published a boolean or lost its cause");++checks;
    };
    CellFitFixture f(0,DriveEdition::BaseRa2);auto q=f.query;auto c=f.cell;auto cb=f.callbacks();
    fail(q,{},cb,CellFitStatus::NeedsInput);
    c.bridge.reset();fail(q,c,cb,CellFitStatus::NeedsInput);c=f.cell;
    c.ground_occupancy.reset();fail(q,c,cb,CellFitStatus::NeedsInput);c=f.cell;
    c.overlay_index.reset();fail(q,c,cb,CellFitStatus::NeedsInput);c=f.cell;
    c.land_type.reset();fail(q,c,cb,CellFitStatus::NeedsInput);c=f.cell;
    fail(q,c,{},CellFitStatus::NeedsInput);
    q.component=0xffff;cb.component={};fail(q,c,cb,CellFitStatus::NeedsInput);cb=f.callbacks();
    c.object_cell.reset();fail(q,c,cb,CellFitStatus::NeedsInput);c=f.cell;
    cb.component=[](ra2map::Cell,int,bool)->std::optional<std::uint32_t>{return {};};fail(q,c,cb,CellFitStatus::NeedsInput);
    cb=f.callbacks();
    auto r=original_cell_fit(q,c,cb);require(r.status==CellFitStatus::Complete && r.allowed==false,"FFFF incorrectly disabled the component query");++checks;
    q=f.query;cb.land_speed=[](int,int)->std::optional<float>{return {};};fail(q,c,cb,CellFitStatus::NeedsInput);cb=f.callbacks();
    c.overlay_index=0;c.overlay.reset();fail(q,c,cb,CellFitStatus::NeedsInput);
    c.overlay=CellFitOverlay{};fail(q,c,cb,CellFitStatus::NeedsInput);
    c.overlay=CellFitOverlay{true,std::nullopt,std::nullopt};fail(q,c,cb,CellFitStatus::NeedsInput);
    c.overlay=CellFitOverlay{true,true,std::nullopt};q.movement_zone=1;fail(q,c,cb,CellFitStatus::NeedsInput);
    c=f.cell;q=f.query;q.level=0;c.level.reset();fail(q,c,cb,CellFitStatus::NeedsInput);
    c.level=128;fail(q,c,cb,CellFitStatus::Unsupported);
    c=f.cell;q=f.query;q.speed_type=8;fail(q,c,cb,CellFitStatus::Unsupported);
    q.speed_type=-1;fail(q,c,cb,CellFitStatus::Unsupported);
    q=f.query;c.overlay_index=-2;fail(q,c,cb,CellFitStatus::Unsupported);
    c=f.cell;c.land_type=-1;fail(q,c,cb,CellFitStatus::Unsupported);
    q.speed_type=4;r=original_cell_fit(q,{},{});require(r.status==CellFitStatus::Complete && r.allowed==true,"speed four did not bypass all metadata");++checks;
    q=f.query;c=f.cell;c.ground_occupancy=32;c.overlay_index.reset();
    r=original_cell_fit(q,c,{});require(r.status==CellFitStatus::Complete && r.allowed==false,"known occupancy required later metadata");++checks;
    c=f.cell;c.overlay_index=0;c.overlay=CellFitOverlay{false,std::nullopt,std::nullopt};
    r=original_cell_fit(q,c,cb);require(r.status==CellFitStatus::Complete,"known null overlay pointer required flags");++checks;
    c=f.cell;c.bridge=true;c.ground_occupancy.reset();c.level.reset();
    r=original_cell_fit(q,c,cb);require(r.status==CellFitStatus::Complete && r.allowed==true,"bridge layer read ground occupancy or unused height");++checks;
    c.bridge_occupancy.reset();fail(q,c,cb,CellFitStatus::NeedsInput);
    require(rules.read_string("Tiberium","Track")=="70%" && rules.read_string("Tiberium","Wheel")=="50%","real distinct Tiberium columns changed");++checks;
    CellFitFixture real(16,DriveEdition::YurisRevenge);
    require(real.table[46]==0x3f333333 && real.table[47]==0x3f000000 &&
        fit_float(real.table[46])==static_cast<float>(rules.read_double("Tiberium","Track")) &&
        fit_float(real.table[47])==static_cast<float>(rules.read_double("Tiberium","Wheel")),"real rules speed values differ from compared table inputs");++checks;
    if (!vectors) std::cout<<"8192 original Cell fit vectors and "<<checks<<" input and asset checks pass\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n";return 1; }
