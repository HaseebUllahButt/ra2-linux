#pragma once

#include "../actors/actors.h"

namespace ra2::app {
enum class InfantryActivity { Standing, Moving, Attacking };

struct InfantrySprite {
    gfx::RgbaImage image;
    int left = 0, top = 0;
};

// Base-game E1/E2 main images. The caller selects activity and projects the
// infantry world position, including its subcell, to a screen anchor.
class InfantryArt {
public:
    // Mount the original archives first. T, A, and U select the temperate,
    // snow, and urban unit palettes. Image and Sequence come from the INIs.
    static InfantryArt load(const fs::MixFileSystem& files, const rules::IniFile& art,
                            const rules::IniFile& rules, std::string_view type,
                            char theater_code = 'T');

    // map_facing is the Snapshot byte, 0..255. elapsed_ticks counts simulation
    // ticks since this activity began, not SHP frames. Ready holds its first
    // frame; Walk and FireUp loop using the original sequence delays.
    // Add left/top to the projected anchor. Cached references stay valid until
    // this InfantryArt is destroyed. No movement or attacks run here.
    const InfantrySprite& draw(std::uint8_t map_facing, std::uint64_t elapsed_ticks,
                               InfantryActivity activity);

private:
    InfantryArt(actors::ActorArt actor, gfx::Palette palette);
    actors::ActorArt actor_;
    gfx::Palette palette_;
    std::map<std::size_t, InfantrySprite> images_;
};
}
