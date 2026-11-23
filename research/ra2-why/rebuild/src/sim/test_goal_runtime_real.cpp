#include "goal_runtime.h"
#include "../session/world/setup.h"
#include <iostream>
#include <stdexcept>
using namespace ra2;
namespace sw=ra2::session::world;
int main(int argc,char** argv) try {
    if (argc!=2) throw std::runtime_error("usage: test_goal_runtime_real game_directory");
    const auto assets=sw::BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    const auto packed=sw::read_base_packed_map(assets,{});
    if (!packed || packed->cells.size()!=19900) throw std::runtime_error("real packed map did not load");
    // The seed is a test input. This is not a completed setup RNG boundary.
    const auto random=sim::Random(42).state();
    const auto partial=sw::packed_map_snapshot(*packed,random);
    if (!partial || partial->setup_progress!=sw::SetupProgress::PackedMapRead || !partial->cell_order || partial->cell_order->size()!=19900)
        throw std::runtime_error("partial original map snapshot differs");
    const auto before=*partial;sim::GoalRuntimeSetup input;input.frame=0;
    const auto imported=sim::GoalRuntime::import(*partial,input,std::string(argv[1])+"/game.exe");
    if (imported.first || imported.second.status!=sim::GoalRuntimeStatus::NeedsInput)
        throw std::runtime_error("unfinished real map entered the live runtime");
    if (partial->random.table!=before.random.table || partial->random.first!=before.random.first ||
        partial->random.second!=before.random.second || partial->cells.size()!=before.cells.size() ||
        partial->cell_order!=before.cell_order) throw std::runtime_error("rejected import changed the real snapshot");
    std::cout<<"real MP02T2 packed map: 19900 Cells, incomplete runtime rejected without changes\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
