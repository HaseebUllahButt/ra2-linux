#pragma once
#include "../ui/ui.h"
#include <cstddef>
#include <filesystem>

namespace ra2::app {
struct BattleOptions {
    std::filesystem::path game_directory;
    std::string map="MP02T2.MAP";
    ui::SkirmishSetup setup;
    std::size_t frames=0;
    bool headless=false, autoplay=false,human_input_check=false;
};
int run_prototype_battle(const BattleOptions&);
}
