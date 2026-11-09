#include "speed_type.h"
#include <string_view>

namespace ra2::session::world {
namespace {
bool same_ascii_name(std::string_view value, std::string_view name) {
    if (value.size()!=name.size()) return false;
    for (std::size_t i=0;i<value.size();++i) {
        auto ch=value[i];
        if (ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');
        auto expected=name[i];
        if (expected>='A' && expected<='Z') expected=static_cast<char>(expected-'A'+'a');
        if (ch!=expected) return false;
    }
    return true;
}
}
BaseSpeedTypeResult resolve_base_speed_type(const BaseSpeedTypeInput& input) {
    if (!input.initial_type || input.key_state==SpeedKeyState::Unknown) return {};
    auto index=*input.initial_type;
    // 0070ABEA applies the Crusher default before the SpeedType key is read.
    if (index==-1) {
        if (!input.crusher) return {};
        index=*input.crusher?1:2;
    }
    if (input.key_state==SpeedKeyState::Present) {
        // 00482F20 uses a case-insensitive original name lookup.
        if (same_ascii_name(input.key_value,"Track")) index=1;
        else if (same_ascii_name(input.key_value,"Wheel")) index=2;
        else return {SpeedTypeStatus::Unsupported,{}};
    } else if (input.key_state!=SpeedKeyState::Absent) {
        return {SpeedTypeStatus::Unsupported,{}};
    }
    if (index!=1 && index!=2) return {SpeedTypeStatus::Unsupported,{}};
    return {SpeedTypeStatus::Complete,index};
}
}
