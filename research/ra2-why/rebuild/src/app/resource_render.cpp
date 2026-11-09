#include "resource_render.h"
#include <stdexcept>
namespace ra2::app {
ResourceRender::ResourceRender(const fs::MixFileSystem& files,const rules::IniFile& rules,
        const rules::IniFile& art,char theater):files_(files),rules_(rules),art_(art) {
    if(files.variant()!=fs::GameVariant::RedAlert2) throw std::runtime_error("Resource drawing requires base RA2");
    suffix_=theater=='T'?"TEM":theater=='A'?"SNO":theater=='U'?"URB":"";
    if(suffix_.empty()) throw std::runtime_error("Resource theater is unresolved");
    auto palette=files.read_file("ISO"+suffix_+".PAL");
    if(!palette) throw std::runtime_error("Missing resource terrain palette");
    palette_=gfx::Palette::from_bytes(*palette);
    if(auto* list=rules.find_section("OverlayTypes"))
        for(const auto& entry:list->entries) types_.push_back(
            rules.read_bool(entry.value,"Tiberium",false)?entry.value:std::string{});
}
std::vector<ResourceRender::Sprite> ResourceRender::sprites(const session::world::Snapshot& world,
        actors::ScreenPoint camera,std::vector<std::string>& gaps) {
    std::vector<Sprite> result;
    for(const auto& [key,cell]:world.cells) {
        if(!cell.terrain) continue;
        const int index=cell.terrain->overlay_type_index.value_or(-1);
        if(index<0 || std::size_t(index)>=types_.size()) continue;
        const auto& type=types_[index];
        if(type.empty()) continue;
        try {
            if(!cell.overlay_data_11e || !cell.terrain->floor_world_height)
                throw std::runtime_error("Resource density or floor is unresolved");
            // Base 00474EB0 uses Cell +11E directly as the frame.
            // Playable limit: retain the live overlay variant. The original
            // x*y resource family and slope variant selection remain unresolved.
            // Zero density is a visible last bail; depletion removes the type.
            std::string selected=type;
            auto image=rules_.read_string(selected,"Image",selected);
            if(!art_.read_bool(image,"Theater",false) || art_.read_bool(image,"DemandLoad",false) ||
               art_.read_bool(image,"NewTheater",false)) throw std::runtime_error("Resource art flags are unresolved");
            auto shape=shapes_.find(image);
            if(shape==shapes_.end()) {
                auto bytes=files_.read_file(image+"."+suffix_);
                if(!bytes) throw std::runtime_error("Missing resource SHP "+image);
                shape=shapes_.emplace(image,gfx::ShpImage::parse(std::move(*bytes))).first;
            }
            unsigned frame=*cell.overlay_data_11e;
            if(frame>=12 || frame>=shape->second.header().frame_count) throw std::runtime_error("Resource density frame is unresolved");
            auto cache=std::make_pair(image,frame);
            auto sprite=images_.find(cache);
            if(sprite==images_.end()) sprite=images_.emplace(cache,shape->second.decode_frame(frame,palette_)).first;
            // 00475440/005DC050: resource offset -12 and Tiberium land -1.
            // Cell corner projection plus (0,15-13) equals center minus 13 Y.
            auto anchor=actors::project_world({std::int64_t(key%512)*256+128,
                std::int64_t(key/512)*256+128,*cell.terrain->floor_world_height},camera);
            result.push_back({&sprite->second,{anchor.x-sprite->second.width/2,
                anchor.y-13-sprite->second.height/2}});
        } catch(const std::exception& error) { gaps.push_back("Resource "+std::to_string(key)+": "+error.what()); }
    }
    return result;
}
}
