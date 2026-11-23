#pragma once
#include "../sim/damage_runtime.h"
#include "../ui/score_screen.h"
#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace ra2::app {
class BattleScore {
public:
    explicit BattleScore(const std::vector<std::pair<std::uint32_t,std::string>>& houses)
        : tally_(names(houses)) {
        for(const auto& house:houses)houses_.push_back(house.first);
    }
    void clock(std::uint64_t now,bool playing) {
        if(clock_ready_ && playing_ && now>=last_)elapsed_+=now-last_;
        last_=now;playing_=playing;clock_ready_=true;
    }
    void harvest(std::uint32_t house,std::int32_t before,std::int32_t after) {
        if(after>before)tally_.harvested(index(house),std::uint32_t(std::int64_t(after)-before));
    }
    void combat(const std::vector<sim::PrototypeCombatEvent>& events) {
        for(const auto& event:events)
            if(event.kind==sim::PrototypeCombatEventKind::Death && index(event.target_house)!=ui::ScoreTally::npos &&
                deaths_.insert(event.target_id).second)tally_.destroyed(index(event.source_house),index(event.target_house));
    }
    ui::ScoreScreen finish(bool won) const {
        return tally_.finish(won,std::uint32_t(std::min<std::uint64_t>(elapsed_/1000,std::numeric_limits<std::uint32_t>::max())));
    }
private:
    static std::vector<std::string> names(const std::vector<std::pair<std::uint32_t,std::string>>& houses) {
        std::vector<std::string> out;for(const auto& house:houses)out.push_back(house.second);return out;
    }
    std::size_t index(std::uint32_t house) const {
        const auto at=std::find(houses_.begin(),houses_.end(),house);
        return at==houses_.end()?ui::ScoreTally::npos:std::size_t(at-houses_.begin());
    }
    ui::ScoreTally tally_;
    std::vector<std::uint32_t> houses_;
    std::set<std::uint32_t> deaths_;
    std::uint64_t last_=0,elapsed_=0;
    bool playing_=false,clock_ready_=false;
};
}
