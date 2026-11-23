#include "test_nearest_cell_inputs.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
using namespace ra2::sim::test;
void require(bool answer,const char* text) { if (!answer) throw std::runtime_error(text); }
int main(int argc,char** argv) try {
    if (argc<3 || argc>4) throw std::runtime_error("usage: test_nearest_cell BASE_EXECUTABLE EXPANSION_EXECUTABLE [--vectors|--edges]");
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        for (unsigned profile=0;profile<(argc==3?2u:1u);++profile) {
            const bool edges=argc==3?profile==1:std::string(argv[3])=="--edges";
            std::uint32_t total=2166136261u;
            for (unsigned seed=0;seed<512;++seed) {
                NearestFixture fixture(seed,edition==1?DriveEdition::BaseRa2:DriveEdition::YurisRevenge,edges);
                const auto result=original_foot_nearest_cell(fixture.input,distance,fixture.callbacks());
                if (result.status!=NearestCellStatus::Complete) throw std::runtime_error("nearest fixture "+std::to_string(seed)+": "+result.detail);
                const auto digest=fixture.digest(result);total=(total^digest)*16777619u;
                if (argc==4) std::cout<<edition<<" "<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
            }
            require(total==(edges?0x4e70df25u:0xa7f1ecffu),"original nearest scan, group, callback order, or selection differs");
            if (argc==3) std::cout<<"edition "<<edition<<(edges?" edge":" normal")<<" nearest-cell digest "<<std::hex<<total<<std::dec<<"\n";
        }
    }
    const auto distance=OriginalDistance::from_executable(argv[1]);
    unsigned checks=0;
    const auto rejected=[&](const NearestCellInputs& input,const NearestCellCallbacks& cb,NearestCellStatus status) {
        const auto result=original_foot_nearest_cell(input,distance,cb);
        require(result.status==status && !result.answer && !result.detail.empty(),"nearest-cell error published a choice or lost its cause"); ++checks;
    };
    NearestFixture fixture(3,DriveEdition::BaseRa2);const auto input=fixture.input;auto cb=fixture.callbacks();
    fixture.input.empty_cell.reset();rejected(fixture.input,cb,NearestCellStatus::NeedsInput);fixture.input=input;
    fixture.input.map_width.reset();rejected(fixture.input,cb,NearestCellStatus::NeedsInput);fixture.input=input;
    fixture.input.map_height.reset();rejected(fixture.input,cb,NearestCellStatus::NeedsInput);fixture.input=input;
    fixture.input.map_width=-1;rejected(fixture.input,cb,NearestCellStatus::Unsupported);fixture.input=input;
    fixture.input.query.goal={512,512};rejected(fixture.input,cb,NearestCellStatus::Unsupported);fixture.input=input;
    cb.cell={};rejected(fixture.input,cb,NearestCellStatus::NeedsInput);cb=fixture.callbacks();
    cb.cell=[](ra2map::Cell)->std::optional<NearestCellState> { return {}; };rejected(fixture.input,cb,NearestCellStatus::NeedsInput);
    cb.cell=[](ra2map::Cell) { return NearestCellState{true,std::nullopt,false}; };rejected(fixture.input,cb,NearestCellStatus::NeedsInput);
    cb.cell=[](ra2map::Cell) { return NearestCellState{true,128,false}; };rejected(fixture.input,cb,NearestCellStatus::Unsupported);
    fixture.input.query.above_bridge=true;cb.cell=[](ra2map::Cell) { return NearestCellState{true,0,std::nullopt}; };rejected(fixture.input,cb,NearestCellStatus::NeedsInput);
    fixture.input=input;cb=fixture.callbacks();cb.fits={};rejected(fixture.input,cb,NearestCellStatus::NeedsInput);
    cb=fixture.callbacks();cb.project={};rejected(fixture.input,cb,NearestCellStatus::NeedsInput);
    fixture.input.query.source=fixture.input.empty_cell->value;fixture.input.frame.reset();cb=fixture.callbacks();rejected(fixture.input,cb,NearestCellStatus::NeedsInput);
    fixture.input.frame=0x80000000u;rejected(fixture.input,cb,NearestCellStatus::Unsupported);fixture.input=input;
    fixture.input.query.source={0x8000,0x8000};cb=fixture.callbacks();rejected(fixture.input,cb,NearestCellStatus::Unsupported);
    NearestFixture cap(3,DriveEdition::BaseRa2);
    const auto limited=original_foot_nearest_cell(cap.input,distance,cap.callbacks());
    require(limited.status==NearestCellStatus::Complete && limited.admitted.size()==24 && limited.exact_projection.empty() && limited.other_projection.size()==24,"nearest-cell candidate cap or projection groups differ"); ++checks;
    require(nearest_key(limited.admitted[0])==nearest_key(cap.input.query.goal) && nearest_key(limited.admitted[1])==nearest_key(cap.input.query.goal),"nearest-cell radius zero lost the duplicate goal"); ++checks;
    NearestFixture no_fit(4,DriveEdition::BaseRa2);no_fit.input.frame.reset();no_fit.input.query.source=no_fit.input.empty_cell->value;
    const auto absent=original_foot_nearest_cell(no_fit.input,distance,no_fit.callbacks());
    require(absent.status==NearestCellStatus::Complete && nearest_key(*absent.answer)==nearest_key(no_fit.input.empty_cell->value) && absent.probes==3970,"nearest-cell full radius limit or empty result differs"); ++checks;
    NearestFixture tie(6,DriveEdition::BaseRa2);tie.input.frame.reset();
    const auto first=original_foot_nearest_cell(tie.input,distance,tie.callbacks());
    require(first.status==NearestCellStatus::Complete && first.answer->x+1==tie.input.query.goal.x && first.answer->y==tie.input.query.goal.y,"nearest-cell first-distance tie changed"); ++checks;
    NearestFixture unused(3,DriveEdition::BaseRa2);unused.input.query.above_bridge=false;
    unsigned reads=0;auto unused_cb=unused.callbacks();
    unused_cb.cell=[&](ra2map::Cell) { return ++reads==1?NearestCellState{std::nullopt,0,std::nullopt}:NearestCellState{true,std::nullopt,std::nullopt}; };
    unused_cb.fits=[](const NearestFitQuery&) { return false; };unused_cb.project={};
    const auto skipped=original_foot_nearest_cell(unused.input,distance,unused_cb);
    require(skipped.status==NearestCellStatus::Complete && skipped.admitted.empty(),"nearest-cell rejected unused unknown candidate fields"); ++checks;
    unused.input.map_width=0;unused.input.map_height=0;unused_cb.cell=[](ra2map::Cell) { return NearestCellState{std::nullopt,0,std::nullopt}; };unused_cb.fits={};
    const auto no_extent=original_foot_nearest_cell(unused.input,distance,unused_cb);
    require(no_extent.status==NearestCellStatus::Complete && no_extent.probes==0,"zero-extent nearest search read unused callback data"); ++checks;
    NearestFixture unknown(3,DriveEdition::BaseRa2);unsigned cells=0;auto unknown_cb=unknown.callbacks();
    unknown_cb.cell=[&](ra2map::Cell) { return ++cells==1?NearestCellState{true,0,false}:NearestCellState{std::nullopt,0,false}; };
    rejected(unknown.input,unknown_cb,NearestCellStatus::NeedsInput);
    unknown_cb=unknown.callbacks();unknown_cb.fits=[](const NearestFitQuery&)->std::optional<bool> { return {}; };
    rejected(unknown.input,unknown_cb,NearestCellStatus::NeedsInput);
    unknown_cb=unknown.callbacks();unknown_cb.project=[](Coord)->std::optional<ra2map::Cell> { return {}; };
    rejected(unknown.input,unknown_cb,NearestCellStatus::NeedsInput);
    if (argc==3) std::cout<<"2048 original nearest-cell scans and "<<checks<<" input or selection checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n";return 1; }
