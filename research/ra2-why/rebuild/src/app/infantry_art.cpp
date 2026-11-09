#include "infantry_art.h"

#include <stdexcept>
#include <utility>

namespace ra2::app {
namespace {
std::string_view sequence_name(InfantryActivity activity) {
    switch (activity) {
    case InfantryActivity::Standing:
        return actors::infantry_motion_sequence(actors::InfantryMotion::Standing, false, false);
    case InfantryActivity::Moving:
        return actors::infantry_motion_sequence(actors::InfantryMotion::Walking, false, false);
    case InfantryActivity::Attacking: return "FireUp";
    }
    throw std::runtime_error("Unsupported infantry activity");
}

const char* palette_name(char theater) {
    switch (theater) {
    case 'T': return "UNITTEM.PAL";
    case 'A': return "UNITSNO.PAL";
    case 'U': return "UNITURB.PAL";
    }
    throw std::runtime_error("Unsupported base infantry theater");
}
}

InfantryArt::InfantryArt(actors::ActorArt actor, gfx::Palette palette)
    : actor_(std::move(actor)), palette_(std::move(palette)) {}

InfantryArt InfantryArt::load(const fs::MixFileSystem& files, const rules::IniFile& art,
                              const rules::IniFile& rules, std::string_view type, char theater) {
    if (files.variant() != fs::GameVariant::RedAlert2)
        throw std::runtime_error("Infantry prototype requires base RA2 archives and INIs");
    if (type != "E1" && type != "E2")
        throw std::runtime_error("Infantry prototype supports E1 and E2");
    const auto* name = palette_name(theater);
    const auto bytes = files.read_file(name);
    if (!bytes) throw std::runtime_error(std::string("Missing original infantry palette: ") + name);
    auto actor = actors::ActorArt::load(files, art, rules, type, actors::Kind::Infantry, theater);
    for (const auto activity : {InfantryActivity::Standing, InfantryActivity::Moving,
                                InfantryActivity::Attacking}) {
        const auto sequence = sequence_name(activity);
        const auto found = actor.sequences().find(sequence);
        if (found == actor.sequences().end() || found->second.count <= 0 ||
            found->second.delay < 0 || !found->second.fixed_facing.empty())
            throw std::runtime_error("Unsupported infantry sequence: " + std::string(sequence));
        // Validate the whole facing range before returning a drawable asset.
        for (unsigned direction = 0; direction < 8; ++direction)
            actor.frame(sequence, direction, static_cast<unsigned>(found->second.count - 1));
    }
    return InfantryArt(std::move(actor), gfx::Palette::from_bytes(*bytes));
}

const InfantrySprite& InfantryArt::draw(std::uint8_t facing, std::uint64_t ticks,
                                       InfantryActivity activity) {
    const auto name = sequence_name(activity);
    const auto& sequence = actor_.sequences().find(name)->second;
    // Base game.exe uses the same delay bytes at 007A3474 as the existing
    // ActorArt helper: Ready=0, Walk=3, FireUp=1.
    const auto step = sequence.delay > 0 ?
        static_cast<unsigned>((ticks / static_cast<unsigned>(sequence.delay)) %
                              static_cast<unsigned>(sequence.count)) : 0;
    const auto index = actor_.frame_for_map(name, facing, step);
    const auto found = images_.find(index);
    if (found != images_.end()) return found->second;
    const auto origin = actor_.main_canvas_origin({0, 0});
    InfantrySprite sprite{actor_.draw_frame(index, palette_),
                          static_cast<int>(origin.x), static_cast<int>(origin.y)};
    // TODO-ORIGINAL: house colors, lighting, shadows, prone/deployed poses,
    // and automatic activity changes remain separate from the main image.
    return images_.emplace(index, std::move(sprite)).first->second;
}
}
