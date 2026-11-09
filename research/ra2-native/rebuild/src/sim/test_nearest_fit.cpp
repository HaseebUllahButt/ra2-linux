#include "test_nearest_cell_inputs.h"
#include "cell_fit.h"
#include "cell_projection.h"
#include "test_cell_fit_inputs.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
using namespace ra2::sim::test;
struct Fixture : NearestFixture {
    Fixture(unsigned n,DriveEdition edition,bool edges):NearestFixture(n,edition,edges) { if (n%17==0) input.query.movement_zone=12; }
    CellFitState fit_cell(ra2map::Cell p) const {
        const auto m=metadata(p);const auto h=hash(p);
        CellFitState c;c.object_cell=p;c.level=m.level;c.bridge=m.bridge;
        c.ground_occupancy=profile==4 || (h>>4)%8==0?32:0;c.bridge_occupancy=profile==4 || (h>>8)%8==0?32:0;
        c.overlay_index=(profile==2 || profile==5) && h%3==0?0:-1;c.overlay=CellFitOverlay{true,true,(seed&1)!=0};
        c.land_type=(h>>12)%12;return c;
    }
    std::uint32_t speed_bits(int land,int speed) const {
        constexpr std::uint32_t bits[]={0x3f800000,0x3f333333,0x3f000000,0,0xbf800000,0x7fc00001};
        return bits[(seed+land*3+speed)%6];
    }
    NearestCellCallbacks composed() {
        auto cb=callbacks();
        cb.fits=[&](const NearestFitQuery& q)->std::optional<bool> {
            RectangleFitQuery r;r.edition=q.edition;r.origin=q.cell;r.speed_type=q.speed_type;
            r.component=q.component;r.movement_zone=q.movement_zone;r.above_bridge=q.above_bridge;
            CellFitCallbacks fit;
            fit.component=[&](ra2map::Cell p,int zone,bool above) {
                events.push_back({7,nearest_key(p),static_cast<std::uint32_t>(zone),above});return q.component;
            };
            fit.land_speed=[&](int land,int speed) {
                const auto bits=speed_bits(land,speed);events.push_back({8,static_cast<std::uint32_t>(land),static_cast<std::uint32_t>(speed),bits});return fit_float(bits);
            };
            const auto result=original_rectangle_fit(r,[&](ra2map::Cell p) {
                events.push_back({6,nearest_key(p),nearest_key(p),0});return fit_cell(p);
            },fit);
            if (result.status!=CellFitStatus::Complete) return {};
            return result.allowed;
        };
        cb.project=[&](Coord point)->std::optional<ra2map::Cell> {
            if (point.z!=0) throw std::runtime_error("nearest projection Z differs");
            events.push_back({3,static_cast<std::uint32_t>(point.x),static_cast<std::uint32_t>(point.y),0});
            const auto r=original_cell_projection(point,[&](ra2map::Cell p) {
                const auto m=metadata(p);const bool flag=(hash(p)&0x1000)!=0;
                events.push_back({4,nearest_key(p),static_cast<std::uint32_t>(*m.level),(*m.bridge?0x100u:0u)|(flag?0x1000u:0u)});
                return ProjectionCellState{m.level,flag,m.bridge};
            });
            if (r.status!=CellProjectionStatus::Complete) return {};
            events.push_back({5,nearest_key(*r.answer),0,0});return r.answer;
        };
        return cb;
    }
};
int main(int argc,char** argv) try {
    if (argc<3 || argc>4) throw std::runtime_error("usage: test_nearest_fit BASE_EXECUTABLE EXPANSION_EXECUTABLE [--vectors|--edges]");
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        for (unsigned profile=0;profile<(argc==3?2u:1u);++profile) {
            const bool edges=argc==3?profile==1:std::string(argv[3])=="--edges";
            std::uint32_t total=2166136261u;
            for (unsigned seed=0;seed<256;++seed) {
                Fixture f(seed,edition==1?DriveEdition::BaseRa2:DriveEdition::YurisRevenge,edges);
                const auto r=original_foot_nearest_cell(f.input,distance,f.composed());
                if (r.status!=NearestCellStatus::Complete) throw std::runtime_error(r.detail);
                const auto digest=f.digest(r);total=(total^digest)*16777619u;
                if (argc==4) std::cout<<edition<<" "<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
            }
            const std::uint32_t expected=edition==1?(edges?0x1c425af5u:0x9debbbcdu):(edges?0x671a4eecu:0x0f92a915u);
            if (total!=expected) throw std::runtime_error("original composed nearest and Cell fit vectors differ");
            if (argc==3) std::cout<<"edition "<<edition<<(edges?" edge":" normal")<<" nearest with rectangle, Cell fit and projection "<<std::hex<<total<<std::dec<<"\n";
        }
    }
    const auto distance=OriginalDistance::from_executable(argv[1]);
    Fixture f(3,DriveEdition::BaseRa2,false);f.input.query.speed_type=1;f.input.query.source_component=0xffffffffu;
    auto cb=f.composed();
    cb.fits=[&](const NearestFitQuery& request)->std::optional<bool> {
        RectangleFitQuery q;q.origin=request.cell;q.speed_type=request.speed_type;
        const auto result=original_rectangle_fit(q,[&](ra2map::Cell p) {
            auto c=f.fit_cell(p);c.bridge=false;c.ground_occupancy.reset();return c;
        },{});
        return result.status==CellFitStatus::Complete?result.allowed:std::nullopt;
    };
    auto result=original_foot_nearest_cell(f.input,distance,cb);
    if (result.status!=NearestCellStatus::NeedsInput || result.answer) throw std::runtime_error("unknown Cell occupancy became a nearest answer");
    cb.fits=[](const NearestFitQuery&)->std::optional<bool>{return false;};
    result=original_foot_nearest_cell(f.input,distance,cb);
    if (result.status!=NearestCellStatus::Complete || !result.answer || nearest_key(*result.answer)!=0)
        throw std::runtime_error("known blocked Cells did not return the separate nearest empty sentinel");
    if (argc==3) std::cout<<"1024 original composed nearest vectors and two input checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n";return 1; }
