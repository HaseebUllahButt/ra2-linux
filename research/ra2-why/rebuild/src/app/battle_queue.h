#pragma once
#include <algorithm>
#include <deque>
#include <iterator>
#include <map>
#include <string>

namespace ra2::app {
// Human sidebar requests. A factory keeps its active product separately.
class BattleBuildQueue {
    std::map<int,std::deque<std::string>> slots_;
public:
    bool add(int slot,const std::string& type,int active_in_slot) {
        auto& line=slots_[slot];
        if(line.size()+std::size_t(std::max(0,active_in_slot))>=30)return false;
        line.push_back(type);return true;
    }
    int count(const std::string& type) const {
        int total=0;
        for(const auto& line:slots_)total+=int(std::count(line.second.begin(),line.second.end(),type));
        return total;
    }
    bool cancel(int slot,const std::string& type) {
        auto found=slots_.find(slot);if(found==slots_.end())return false;
        auto& line=found->second;
        const auto item=std::find(line.rbegin(),line.rend(),type);
        if(item==line.rend())return false;
        line.erase(std::next(item).base());return true;
    }
    const auto& slots() const {return slots_;}
    void pop(int slot) {slots_.at(slot).pop_front();}
};
}
