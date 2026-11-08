#include "production.h"
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace ra2::sim;
namespace {
unsigned checks=0;
void require(PrerequisiteInputs input,PrerequisiteStatus expected) {
    ++checks;
    if (check_original_prerequisites(input)!=expected) throw std::runtime_error("prerequisite result differs");
}
PrerequisiteInputs resolved() {
    PrerequisiteInputs input;
    input.required=std::vector<std::int32_t>{};
    input.available=std::vector<BuildingTypeId>{};
    for (unsigned i=0;i<8;++i) input.registry.push_back(i);
    for (auto& group:input.groups) group=std::vector<BuildingTypeId>{};
    return input;
}
void self_test() {
    auto input=resolved();
    require(input,PrerequisiteStatus::Available);
    input.available.reset(); require(input,PrerequisiteStatus::Available);
    input.required.reset(); require(input,PrerequisiteStatus::NeedsInput);
    input=resolved(); input.required=std::vector<std::int32_t>{0};
    require(input,PrerequisiteStatus::Missing);
    input.available=std::vector<BuildingTypeId>{0}; require(input,PrerequisiteStatus::Available);
    input.registry[0].reset(); require(input,PrerequisiteStatus::NeedsInput);
    input.available->clear(); require(input,PrerequisiteStatus::Missing);
    input.available.reset(); require(input,PrerequisiteStatus::NeedsInput);
    input=resolved(); input.required=std::vector<std::int32_t>{-1};
    require(input,PrerequisiteStatus::Missing);
    input.groups[0].reset(); require(input,PrerequisiteStatus::NeedsInput);
    input.groups[0]=std::vector<BuildingTypeId>{3,5};
    input.available=std::vector<BuildingTypeId>{5}; require(input,PrerequisiteStatus::Available);
    input.available=std::vector<BuildingTypeId>{4}; require(input,PrerequisiteStatus::Missing);
    input.required=std::vector<std::int32_t>{-7}; require(input,PrerequisiteStatus::Unsupported);
    input.required=std::vector<std::int32_t>{8}; require(input,PrerequisiteStatus::NeedsInput);
    input=resolved(); input.registry[1]=0;
    input.required=std::vector<std::int32_t>{0,1,0};
    input.available=std::vector<BuildingTypeId>{0}; require(input,PrerequisiteStatus::Available);
    // A known failed prerequisite ends the original loop before later unknown inputs.
    input=resolved(); input.required=std::vector<std::int32_t>{0,-1};
    input.groups[0].reset(); require(input,PrerequisiteStatus::Missing);
    std::cout<<"Original prerequisite helper and unknown inputs: "<<checks<<" checks passed\n";
}
void cases() {
    unsigned required_count;
    while (std::cin>>required_count) {
        if (required_count>1024) throw std::runtime_error("case exceeds input bound");
        auto input=resolved(); input.required->resize(required_count);
        for (auto& item:*input.required) std::cin>>item;
        unsigned count; std::cin>>count;
        if (count>1024) throw std::runtime_error("case exceeds input bound");
        input.available->resize(count); for (auto& item:*input.available) std::cin>>item;
        for (auto& group:input.groups) {
            std::cin>>count;
            if (count>1024) throw std::runtime_error("case exceeds input bound");
            group->resize(count); for (auto& item:*group) std::cin>>item;
        }
        if (!std::cin) throw std::runtime_error("incomplete prerequisite case");
        const auto result=check_original_prerequisites(input);
        if (result!=PrerequisiteStatus::Available && result!=PrerequisiteStatus::Missing)
            throw std::runtime_error("unresolved original comparison case");
        std::cout<<(result==PrerequisiteStatus::Available)<<'\n';
    }
}
}
int main(int argc,char** argv) try {
    if (argc==2 && std::string_view(argv[1])=="--cases") cases();
    else if (argc==1) self_test();
    else throw std::runtime_error("Usage: sim_production_test [--cases]");
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
