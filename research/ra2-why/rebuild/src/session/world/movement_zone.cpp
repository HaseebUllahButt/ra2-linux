#include "movement_zone.h"
#include "sim/movement_zones.h"
#include <string_view>

namespace ra2::session::world {
namespace {
bool same_name(std::string_view value,std::string_view expected) {
    if (value.size()!=expected.size()) return false;
    for (std::size_t i=0;i<value.size();++i) {
        auto a=value[i],b=expected[i];
        if (a>='A' && a<='Z') a=static_cast<char>(a-'A'+'a');
        if (b>='A' && b<='Z') b=static_cast<char>(b-'A'+'a');
        if (a!=b) return false;
    }
    return true;
}
}
BaseMovementZoneResult resolve_base_movement_zone(const BaseMovementZoneInput& input) {
    if (!input.initial_zone || input.key_state==ZoneKeyState::Unknown) return {};
    if (*input.initial_zone<0 || static_cast<unsigned>(*input.initial_zone)>=
        sim::original_movement_zone_count(sim::DriveEdition::BaseRa2))
        return {ZoneStatus::Unsupported,{},{}};
    // Only the base UnitType constructor's zero default has been checked here.
    if (*input.initial_zone!=0) return {ZoneStatus::Unsupported,{},{}};
    auto zone=*input.initial_zone;
    if (input.key_state==ZoneKeyState::Present) {
        if (same_name(input.key_value,"Normal")) zone=0;
        else if (same_name(input.key_value,"Subterannean")) zone=6;
        else return {ZoneStatus::Unsupported,{},{}};
    } else if (input.key_state!=ZoneKeyState::Absent) {
        return {ZoneStatus::Unsupported,{},{}};
    }
    if (zone!=0 && zone!=6) return {ZoneStatus::Unsupported,{},{}};
    // 006DE01A writes +AF5 after the independent zone value.
    return {ZoneStatus::Complete,zone,zone==6};
}
}
