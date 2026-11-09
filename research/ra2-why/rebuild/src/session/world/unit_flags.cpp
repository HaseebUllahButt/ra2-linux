#include "unit_flags.h"
#include <string_view>

namespace ra2::session::world {
namespace {
bool same_name(std::string_view value,std::string_view expected) {
    if (value.size()!=expected.size()) return false;
    for (std::size_t i=0;i<value.size();++i) {
        auto ch=value[i];
        if (ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');
        if (ch!=expected[i]) return false;
    }
    return true;
}
UnitFlagResult resolve(const std::optional<bool>& initial,const UnitFlagKey& key) {
    if (!initial || key.state==UnitFlagKeyState::Unknown) return {};
    if (key.state==UnitFlagKeyState::Absent) return {UnitFlagStatus::Complete,*initial};
    if (key.state!=UnitFlagKeyState::Present) return {UnitFlagStatus::Unsupported,{}};
    if (same_name(key.value,"yes") || same_name(key.value,"true") || key.value=="1")
        return {UnitFlagStatus::Complete,true};
    if (same_name(key.value,"no") || same_name(key.value,"false") || key.value=="0")
        return {UnitFlagStatus::Complete,false};
    return {UnitFlagStatus::Unsupported,{}};
}
}
BaseUnitTypeConstructorFields base_unit_type_constructor_fields() {
    // Base 005D58CE, 006D9B40, and 0070A67F write these constructor fields.
    return {true,false,false,false,false,-1,0};
}
BaseUnitFlagResults resolve_base_unit_flags(const BaseUnitFlagInput& input) {
    return {resolve(input.initial_is_train,input.is_train),resolve(input.initial_crusher,input.crusher)};
}
}
