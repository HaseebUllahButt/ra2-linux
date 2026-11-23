#include "coarse_category.h"
#include <cmath>
#include <stdexcept>
#include <string>
namespace ra2::sim {
namespace {
template<class T> T need(const std::optional<T>& value,const char* name) {
    if (!value) throw std::invalid_argument(std::string("original coarse category needs ")+name);
    return *value;
}
}
CoarseCategoryResult original_coarse_category(const CoarseCategoryInputs& input) {
    CoarseCategoryResult result;
    const auto write=[&](std::uint8_t category) { result.category=category; result.writes.push_back(category); };
    const auto finish=[&](std::uint8_t category) { write(category); return result; };
    if (!need(input.usable,"the usable-area answer")) return finish(7);
    const int overlay_index=need(input.overlay_index,"the overlay index");
    if (overlay_index<-1) throw std::invalid_argument("original coarse category overlay index is invalid");
    if (overlay_index!=-1) {
        if (!input.overlay) throw std::invalid_argument("original coarse category overlay state is unresolved");
        const auto& overlay=*input.overlay;
        if (need(overlay.flag_22d,"overlay +22D")) return finish(1);
        if (need(overlay.flag_2a8,"overlay +2A8")) return finish(2);
        const auto wheel=need(overlay.land_wheel,"the overlay land Wheel value");
        // FCOMP equality also takes this branch on an unordered comparison.
        if (wheel==0 || std::isnan(wheel)) return finish(6);
        if (need(overlay.flag_2b5,"overlay +2B5")) return finish(6);
        if (need(overlay.flag_2b4,"overlay +2B4")) return finish(0);
    }
    const int land=need(input.land_type,"the final LandType");
    if (land<0) throw std::invalid_argument("original coarse category LandType is invalid");
    if (land==2) return finish(4);
    if (land==6) return finish(3);
    const auto wheel=need(input.land_wheel,"the land Wheel value");
    // FCOMP uses the double constant at base 0079C518 / expansion 007E3808.
    if (static_cast<double>(wheel)<=0.01 || std::isnan(wheel)) return finish(6);
    if (!input.occupants) throw std::invalid_argument("original coarse category ground occupant order is unresolved");
    for (const auto& occupant:*input.occupants) {
        const int kind=need(occupant.runtime_type,"the occupant runtime type");
        if (kind==6) {
            if (need(occupant.building_flag_16c0,"BuildingType +16C0")) {
                if (need(occupant.house_flag_1fa,"the building owner flag +1FA")) return finish(6);
            } else if (need(occupant.building_flag_16bf,"BuildingType +16BF")) {
                const int state=need(occupant.building_state_618,"Building +618");
                if (state!=12 && state!=8) write(6); // The original continues and can overwrite this value.
            }
        } else if (kind==36) {
            if (need(input.session_field_1258,"Session +1258")!=1)
                return finish(need(occupant.terrain_field_2a8,"TerrainType +2A8")==7?2:5);
            return finish(need(occupant.terrain_field_2ac,"TerrainType +2AC")==7?2:5);
        }
    }
    return finish(0);
}
}
