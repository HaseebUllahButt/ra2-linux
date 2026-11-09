#include "unit_death.h"
namespace ra2::weapons {
bool base_unit_death_trigger_prefix(UnitDeathTriggerState& state,bool attacker,
    std::optional<bool> special,const UnitDeathTriggerCallback& callback) {
    if (!callback) return false;
    if (state.has_trigger && state.field_290!=-1) {
        if (!special) return false;
        if (*special) return true;
    }
    if (state.has_trigger) {
        if (attacker) callback(7,state);
        if (state.has_trigger) callback(0x30,state);
        if (state.has_trigger) callback(0x1d,state);
    }
    return true;
}
}

namespace ra2::weapons {
bool base_unit_parent_death_trigger_prefix(UnitDeathTriggerState& state,bool attacker,
    const UnitDeathTriggerCallback& callback) {
    if (!callback) return false;
    if (state.active && state.has_trigger && attacker) callback(6,state);
    if (state.active && state.has_trigger && attacker) callback(4,state);
    return true;
}
}
