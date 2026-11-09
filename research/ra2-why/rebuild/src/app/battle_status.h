#pragma once
#include "../sim/production_runtime.h"
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
inline std::string production_status(sim::production::Status status,const std::string& success,
                                     const std::string& failure) {
    using Status=sim::production::Status;
    if(status==Status::Done)return success;
    if(status==Status::NeedsTurn)return "Turning to deploy.";
    if(status==Status::Waiting)return "Waiting.";
    return failure;
}
}
