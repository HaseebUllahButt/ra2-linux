#include "production.h"
#include <algorithm>

namespace ra2::sim {
PrerequisiteStatus check_original_prerequisites(const PrerequisiteInputs& input) {
    if (!input.required) return PrerequisiteStatus::NeedsInput;
    for (const auto requirement : *input.required) {
        if (requirement < 0) {
            if (requirement < -6) return PrerequisiteStatus::Unsupported;
            const auto& group=input.groups[static_cast<unsigned>(-requirement-1)];
            if (!group) return PrerequisiteStatus::NeedsInput;
            if (group->empty()) return PrerequisiteStatus::Missing;
            if (!input.available) return PrerequisiteStatus::NeedsInput;
            bool found=false;
            for (const auto type : *group)
                if (std::find(input.available->begin(),input.available->end(),type)!=input.available->end()) {
                    found=true; break;
                }
            if (!found) return PrerequisiteStatus::Missing;
        } else {
            if (!input.available) return PrerequisiteStatus::NeedsInput;
            // Both original functions reject an empty list before registry lookup.
            if (input.available->empty()) return PrerequisiteStatus::Missing;
            const auto index=static_cast<std::size_t>(requirement);
            if (index>=input.registry.size() || !input.registry[index]) return PrerequisiteStatus::NeedsInput;
            if (std::find(input.available->begin(),input.available->end(),*input.registry[index])==input.available->end())
                return PrerequisiteStatus::Missing;
        }
    }
    return PrerequisiteStatus::Available;
}
}
