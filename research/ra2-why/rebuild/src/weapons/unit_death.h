#pragma once
#include <functional>
#include <optional>
namespace ra2::weapons {
struct UnitDeathTriggerState {
    bool has_trigger, active;
    int field_290;
};
using UnitDeathTriggerCallback=std::function<void(int event,UnitDeathTriggerState&)>;
// Base 00707ED0 before parent 006CED90. The callback must execute the
// original trigger synchronously and update the state after each event.
// If field +290 requires the trigger virtual check, its result must be known.
// The caller must still execute the parent death callback and all removal.
bool base_unit_death_trigger_prefix(UnitDeathTriggerState&, bool attacker_present,
    std::optional<bool> special_trigger_check, const UnitDeathTriggerCallback&);
}
