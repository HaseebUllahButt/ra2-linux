#pragma once
#include "../sim/production_runtime.h"
#include "../sim/damage_runtime.h"
#include <cstdint>
#include <string>
#include <utility>

namespace ra2::app {
class BattleStatus {
public:
    void set(std::string text,std::uint64_t now,bool keep=false) {
        text_=std::move(text);changed_=now;keep_=keep;
    }
    std::string text(std::uint64_t now) const {
        return keep_ || now-changed_<3000?text_:std::string{};
    }
private:
    std::string text_;
    std::uint64_t changed_=0;
    bool keep_=false;
};
inline std::string attack_warning(const sim::PrototypeCombatEvent& event,std::uint32_t player,
                                  const std::string& target,const std::string& source,bool building) {
    using Event=sim::PrototypeCombatEventKind;
    if(event.target_house!=player)return {};
    if(event.kind==Event::Death)return target+" lost to "+source+".";
    if(event.kind!=Event::Damage || event.hp_after>=event.hp_before || event.hp_after<=0)return {};
    if(building)return target+" under attack. H: Base.";
    return target+" hit by "+source+".";
}
inline std::string production_status(sim::production::Status status,const std::string& success,
                                     const std::string& failure) {
    using Status=sim::production::Status;
    if(status==Status::Done)return success;
    if(status==Status::NeedsTurn)return "Turning to deploy.";
    if(status==Status::Waiting)return "Waiting.";
    return failure;
}
// Placement messages: an occupied building space asks the player to move
// the units instead of naming the check.
inline std::string placement_status(sim::production::Status status,const std::string& detail,
                                    const std::string& success,const std::string& failure) {
    using Status=sim::production::Status;
    if(status==Status::Done)return success;
    if(detail=="building space is occupied by a unit")
        return "Move units away from the building space.";
    return production_status(status,success,failure);
}
}
