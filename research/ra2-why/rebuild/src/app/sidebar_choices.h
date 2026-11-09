#pragma once
#include "battle_hud.h"
#include <algorithm>

namespace ra2::app {
// A discovered cameo keeps its slot while power or prerequisites change.
class SidebarChoices {
public:
    const std::vector<BuildChoice>& update(const std::vector<BuildChoice>& current) {
        for(auto& choice:known_) {choice.enabled=false;choice.progress=0;choice.queued=0;choice.held=false;}
        for(const auto& choice:current) {
            const auto old=std::find_if(known_.begin(),known_.end(),[&](const auto& item){return item.type==choice.type;});
            if(old==known_.end())known_.push_back(choice);
            else *old=choice;
        }
        return known_;
    }
private:
    std::vector<BuildChoice> known_;
};
}
