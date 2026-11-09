#include "test_nearest_cell_inputs.h"
#include "cell_projection.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
using namespace ra2::sim::test;
int main(int argc,char** argv) try {
    if (argc<3 || argc>4) throw std::runtime_error("usage: test_nearest_projection BASE_EXECUTABLE EXPANSION_EXECUTABLE [--vectors|--edges]");
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        for (unsigned profile=0;profile<(argc==3?2u:1u);++profile) {
            const bool edges=argc==3?profile==1:std::string(argv[3])=="--edges";
            std::uint32_t total=2166136261u;
            for (unsigned seed=0;seed<512;++seed) {
                NearestFixture fixture(seed,edition==1?DriveEdition::BaseRa2:DriveEdition::YurisRevenge,edges);
                auto cb=fixture.callbacks();
                cb.project=[&](Coord point)->std::optional<ra2map::Cell> {
                    if (point.z!=0) throw std::runtime_error("native nearest projection input Z differs");
                    fixture.events.push_back({3,static_cast<std::uint32_t>(point.x),static_cast<std::uint32_t>(point.y),static_cast<std::uint32_t>(point.z)});
                    const auto result=original_cell_projection(point,[&](ra2map::Cell cell)->std::optional<ProjectionCellState> {
                        const auto state=fixture.metadata(cell);const bool flag=(fixture.hash(cell)&0x1000)!=0;
                        const auto flags=(*state.bridge?0x100u:0u)|(flag?0x1000u:0u);
                        fixture.events.push_back({4,nearest_key(cell),static_cast<std::uint32_t>(*state.level),flags});
                        return ProjectionCellState{state.level,flag,state.bridge};
                    });
                    if (result.status!=CellProjectionStatus::Complete) throw std::runtime_error(result.detail);
                    fixture.events.push_back({5,nearest_key(*result.answer),0,0});return result.answer;
                };
                const auto result=original_foot_nearest_cell(fixture.input,distance,cb);
                if (result.status!=NearestCellStatus::Complete) throw std::runtime_error(result.detail);
                const auto digest=fixture.digest(result);total=(total^digest)*16777619u;
                if (argc==4) std::cout<<edition<<" "<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
            }
            if (total!=(edges?0x267f0e59u:0x27b77d4eu)) throw std::runtime_error("original nearest search with real projection differs");
            if (argc==3) std::cout<<"edition "<<edition<<(edges?" edge":" normal")<<" nearest with projection digest "<<std::hex<<total<<std::dec<<"\n";
        }
    }
    const auto distance=OriginalDistance::from_executable(argv[1]);
    NearestFixture unknown(3,DriveEdition::BaseRa2);auto cb=unknown.callbacks();
    cb.project=[](Coord point)->std::optional<ra2map::Cell> {
        const auto result=original_cell_projection(point,[](ra2map::Cell) { return ProjectionCellState{0,std::nullopt,false}; });
        return result.status==CellProjectionStatus::Complete?result.answer:std::nullopt;
    };
    const auto blocked=original_foot_nearest_cell(unknown.input,distance,cb);
    if (blocked.status!=NearestCellStatus::NeedsInput || blocked.answer) throw std::runtime_error("unknown source flag 1000 became a nearest-cell answer");
    NearestFixture partial(3,DriveEdition::BaseRa2);cb=partial.callbacks();
    cb.project=[](Coord point)->std::optional<ra2map::Cell> {
        const auto result=original_cell_projection(point,[](ra2map::Cell) { return ProjectionCellState{0,false,std::nullopt}; });
        return result.status==CellProjectionStatus::Complete?result.answer:std::nullopt;
    };
    const auto unused_bridge=original_foot_nearest_cell(partial.input,distance,cb);
    if (unused_bridge.status!=NearestCellStatus::Complete || nearest_key(*unused_bridge.answer)!=nearest_key(partial.input.query.goal))
        throw std::runtime_error("projection required the trial bridge bit when source flag 1000 was clear");
    if (argc==3) std::cout<<"2048 original nearest searches with real projection and two input checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n";return 1; }
