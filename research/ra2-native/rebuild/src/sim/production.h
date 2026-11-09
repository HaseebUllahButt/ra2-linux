#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace ra2::sim {
// Stable identities for resolved original BuildingType objects.
using BuildingTypeId = std::uint32_t;
struct PrerequisiteInputs {
    std::optional<std::vector<std::int32_t>> required;
    std::vector<std::optional<BuildingTypeId>> registry;
    // Element 0 is original category -1; element 5 is category -6.
    // Supply the original cached category lists, not inferred house counts.
    std::array<std::optional<std::vector<BuildingTypeId>>,6> groups;
    std::optional<std::vector<BuildingTypeId>> available;
};
enum class PrerequisiteStatus { Available, Missing, NeedsInput, Unsupported };
// Base 004EF740 / expansion 00505360. This is the prerequisite-list helper,
// not House::CanBuild. The caller supplies the original available-type list.
// Missing registry entries or category lists stay unresolved. Unsupported
// negative categories return Unsupported instead of the original invalid read.
PrerequisiteStatus check_original_prerequisites(const PrerequisiteInputs&);
}
