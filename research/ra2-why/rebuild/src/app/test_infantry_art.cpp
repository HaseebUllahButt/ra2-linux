#include "infantry_art.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace ra2;
using Activity = app::InfantryActivity;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function> void require_failure(Function function, const char* message) {
    bool failed = false;
    try { function(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, message);
}

rules::IniFile read_ini(const fs::MixFileSystem& files, const char* name) {
    const auto bytes = files.read_file(name);
    if (!bytes) throw std::runtime_error(std::string("Missing original ") + name);
    rules::IniFile result;
    result.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    return result;
}

std::size_t opaque_pixels(const gfx::RgbaImage& image) {
    std::size_t count = 0;
    for (std::size_t p = 3; p < image.pixels.size(); p += 4) count += image.pixels[p] != 0;
    return count;
}

void check_sprite(const app::InfantrySprite& actual, const gfx::RgbaImage& expected) {
    require(actual.image.width == expected.width && actual.image.height == expected.height &&
            actual.image.pixels == expected.pixels, "Infantry frame or palette changed");
    require(actual.left == -static_cast<int>(expected.width / 2) &&
            actual.top == -static_cast<int>(expected.height / 2), "Infantry draw offset changed");
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: infantry_art_test <game-dir>");
    fs::MixFileSystem files(argv[1]);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    files.mount_overlays();
    files.mount_resources();
    files.mount("TEMPERAT.MIX");
    const auto rules = read_ini(files, "RULES.INI"), art = read_ini(files, "ART.INI");
    const auto palette = gfx::Palette::from_bytes(*files.read_file("UNITTEM.PAL"));
    struct Original {
        const char* type;
        const char* shape;
        int width, height;
        std::size_t standing, moving, attacking;
    };
    // A few base-asset vectors, independent of ActorArt frame selection.
    for (const auto& original : {Original{"E1", "GI.SHP", 78, 66, 265, 240, 273},
                                 Original{"E2", "CONS.SHP", 76, 96, 303, 294, 307}}) {
        auto infantry = app::InfantryArt::load(files, art, rules, original.type);
        const auto shape_bytes = files.read_file(original.shape);
        require(shape_bytes.has_value(), "Original infantry SHP is missing");
        const auto shape = gfx::ShpImage::parse(*shape_bytes);
        require(shape.header().width == original.width && shape.header().height == original.height,
                "Original infantry canvas changed");
        struct Spot { Activity activity; std::uint8_t facing; std::uint64_t tick; std::size_t index; };
        for (const auto spot : {Spot{Activity::Standing, 0, 0, 7},
                                Spot{Activity::Standing, 64, 99, 5},
                                Spot{Activity::Moving, 0, 0, 50},
                                Spot{Activity::Moving, 64, 6, 40},
                                Spot{Activity::Moving, 192, 15, 19},
                                Spot{Activity::Attacking, 0, 0, 206},
                                Spot{Activity::Attacking, 128, 4, 186},
                                Spot{Activity::Attacking, 192, 5, 175}})
            check_sprite(infantry.draw(spot.facing, spot.tick, spot.activity),
                         shape.decode_frame(spot.index, palette));
        const auto& standing = infantry.draw(0, 0, Activity::Standing);
        const auto& moving = infantry.draw(0, 0, Activity::Moving);
        const auto& attacking = infantry.draw(0, 0, Activity::Attacking);
        require(opaque_pixels(standing.image) == original.standing &&
                opaque_pixels(moving.image) == original.moving &&
                opaque_pixels(attacking.image) == original.attacking,
                "Infantry main image lost original pixels");
        require(&infantry.draw(0, std::numeric_limits<std::uint64_t>::max(), Activity::Standing) == &standing,
                "Standing infantry animated");
        require(&infantry.draw(0, 2, Activity::Moving) == &moving &&
                infantry.draw(0, 3, Activity::Moving).image.pixels != moving.image.pixels &&
                &infantry.draw(0, 18, Activity::Moving) == &moving,
                "Walking delay or loop changed");
        require(infantry.draw(0, 1, Activity::Attacking).image.pixels != attacking.image.pixels &&
                &infantry.draw(0, 6, Activity::Attacking) == &attacking,
                "Attack delay or loop changed");
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        check_sprite(infantry.draw(0, maximum, Activity::Moving),
                     shape.decode_frame(50 + (maximum / 3) % 6, palette));
        // Moving and attacking must not replace the cached standing sprite.
        require(&infantry.draw(0, 0, Activity::Standing) == &standing,
                "Infantry cache invalidated a returned sprite");
    }

    for (const auto theater : {'A', 'U'}) {
        files.mount(theater == 'A' ? "SNOW.MIX" : "URBAN.MIX");
        const auto bytes = files.read_file(theater == 'A' ? "UNITSNO.PAL" : "UNITURB.PAL");
        require(bytes.has_value(), "Original theater unit palette is missing");
        auto infantry = app::InfantryArt::load(files, art, rules, "E2", theater);
        const auto shape = gfx::ShpImage::parse(*files.read_file("CONS.SHP"));
        check_sprite(infantry.draw(64, 6, Activity::Moving),
                     shape.decode_frame(40, gfx::Palette::from_bytes(*bytes)));
    }
    require_failure([&] { app::InfantryArt::load(files, art, rules, "ENGINEER"); },
                    "Unsupported infantry type was accepted");
    require_failure([&] { app::InfantryArt::load(files, art, rules, "E1", '?'); },
                    "Invalid infantry theater was accepted");
    fs::MixFileSystem expansion(argv[1]); // The default variant is expansion.
    require_failure([&] { app::InfantryArt::load(expansion, art, rules, "E1"); },
                    "Expansion assets entered the base infantry renderer");
    rules::IniFile incomplete;
    incomplete.parse("[GI]\nSequence=MissingAttack\n[MissingAttack]\nReady=0,1,1\nWalk=8,6,6\n");
    require_failure([&] { app::InfantryArt::load(files, incomplete, rules, "E1"); },
                    "Infantry with no attack sequence was accepted");
    std::cout << "Base E1/E2 art passed: poses, facing, delays, offsets, cache, and three unit palettes.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
