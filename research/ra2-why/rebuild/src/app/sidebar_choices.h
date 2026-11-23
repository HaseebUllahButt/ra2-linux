#pragma once
#include "battle_hud.h"
#include <algorithm>
#include <tuple>
#include <set>
#include <map>
#include "../rules/ini_file.h"

namespace ra2::app {
// A discovered cameo keeps its slot while power or prerequisites change.
class SidebarChoices {
public:
    void use_rules(const rules::IniFile& rules, int side) {
        rules_=&rules;side_=side;units_.clear();keys_.clear();
        if(const auto* types=rules.find_section("VehicleTypes"))
            for(const auto& e:types->entries)units_.insert(e.value);
    }
    const std::vector<BuildChoice>& update(const std::vector<BuildChoice>& current) {
        for(auto& choice:known_) {choice.enabled=false;choice.progress=0;choice.queued=0;choice.held=false;}
        for(const auto& choice:current) {
            const auto old=std::find_if(known_.begin(),known_.end(),[&](const auto& item){return item.type==choice.type;});
            if(old==known_.end())known_.push_back(choice);
            else *old=choice;
        }
        if(rules_) {
            for(const auto& c:known_)if(!keys_.count(c.type)) {
                // Base 0067EA20, insertion 0067ED10. Reader 006DAF60:
                // +5EC AIBasePlanningSide, +55C TechLevel, +ADA Naval,
                // +B46 ConsideredAircraft. Virtual +84 supplies house cost.
                const int group=units_.count(c.type)?
                    (rules_->read_bool(c.type,"Naval")?2:rules_->read_bool(c.type,"ConsideredAircraft")?1:0):0;
                std::string id=c.type;
                for(auto& ch:id)if(ch>='A'&&ch<='Z')ch+=('a'-'A');
                keys_[c.type]=std::make_tuple(rules_->read_integer(c.type,"AIBasePlanningSide",-1)!=side_,
                    group,rules_->read_integer(c.type,"TechLevel",255),c.cost,id);
            }
            std::stable_sort(known_.begin(),known_.end(),[&](const auto& a,const auto& b) {
                return keys_.at(a.type)<keys_.at(b.type);
            });
        }
        return known_;
    }
private:
    std::vector<BuildChoice> known_;
    const rules::IniFile* rules_=nullptr;
    int side_=-1;
    std::set<std::string> units_;
    std::map<std::string,std::tuple<bool,int,int,int,std::string>> keys_;
};
}
