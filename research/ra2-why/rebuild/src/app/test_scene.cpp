#include "scene.h"
#include "terrain_inputs.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
ra2::rules::IniFile read_ini(const ra2::fs::MixFileSystem& files, const char* name) {
    const auto bytes = files.read_file(name);
    if (!bytes) throw std::runtime_error(std::string("Missing original ") + name);
    ra2::rules::IniFile ini;
    ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    return ini;
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: scene_test <game-dir> <map>");
        using namespace ra2;
        rules::IniFile base_fixture, map_fixture;
        base_fixture.parse("[E1]\nImage=OLD\nStrength=125\nStrength=999\n[E1]\nImage=WRONG\n");
        map_fixture.parse("[E1]\nImage=GI\nImage=WRONG\n[E1]\nStrength=1\n[NewType]\nValue=2\n");
        const auto merged = app::overlay_rules(base_fixture, map_fixture);
        require(merged.read_string("E1", "Image") == "GI" &&
                merged.read_integer("E1", "Strength") == 125 &&
                merged.read_integer("NewType", "Value") == 2,
                "Map overrides changed the first effective source values");
        const auto map = rules::IniFile::from_file(argv[2]);
        const auto packed = pack::load_terrain(map);
        const auto geometry = render::MapGeometry::from_ini(map);
        for (const auto variant : {fs::GameVariant::RedAlert2, fs::GameVariant::YurisRevenge}) {
            const bool expansion = variant == fs::GameVariant::YurisRevenge;
            fs::MixFileSystem files(argv[1]);
            files.mount_language(variant);
            files.bootstrap(variant);
            files.mount_overlays();
            files.mount_resources();
            auto terrain_art = render::TheaterArt::load(files, "TEMPERATE");
            const auto cells = app::terrain_inputs(packed, terrain_art);
            const auto rules = read_ini(files, expansion ? "RULESMD.INI" : "RULES.INI");
            const auto art = read_ini(files, expansion ? "ARTMD.INI" : "ART.INI");
            sim::Simulation simulation(rules, map, 0, cells);
            app::Scene scene(files, art, app::overlay_rules(rules, map), geometry, cells, "TEMPERATE");
            const auto battlefield = render::render_minimap(packed, geometry, terrain_art);
            const auto frame = scene.render(battlefield, simulation.snapshot(),
                {0, 0, battlefield.width, battlefield.height});
            require(frame.drawn > 0 && frame.gaps.empty(), "Original map buildings did not reach the scene");
            require(frame.image.rgba != battlefield.rgba, "The scene did not compose the original buildings");

            // A flat cell gives an independently fixed anchor: (1800,1245).
            // Width 80 and LocalSize (2,6) come from the original test map.
            sim::TerrainCell flat;
            flat.cell = {80,96}; flat.height_level = 0; flat.ramp_index = 0; flat.floor_world_height = 0;
            app::Scene fixture(files, art, rules, geometry, {flat}, "TEMPERATE");
            sim::Entity building;
            building.id = 1; building.kind = sim::Kind::Structure; building.type = "CAAIRP";
            building.cell = flat.cell; building.map_health = 256;
            const auto anchor = fixture.map_anchor(building);
            require(anchor.x == 1800 && anchor.y == 1245, "Actor and tile projection origins differ");
            const auto palette_bytes = files.read_file("UNITTEM.PAL");
            require(palette_bytes.has_value(), "Original actor palette is missing");
            const auto palette = gfx::Palette::from_bytes(*palette_bytes);
            const auto sprite_art = actors::ActorArt::load(files, art, rules, "CAAIRP", actors::Kind::Building);
            const auto sprite = sprite_art.draw_frame(0, palette);
            render::Bitmap empty{battlefield.width, battlefield.height,
                std::vector<std::uint8_t>(battlefield.rgba.size())};
            sim::Snapshot state; state.entities.push_back(building);
            const render::Rectangle view{1550, 1045, 500, 400};
            auto drawn = fixture.render(empty, state, view);
            require(drawn.drawn == 1 && drawn.gaps.empty(), "Original SHP composition failed");
            const int left = 250 - sprite.width / 2, top = 200 - sprite.height / 2;
            std::size_t checked = 0;
            for (int y = 0; y < sprite.height; ++y) for (int x = 0; x < sprite.width; ++x) {
                if (left+x < 0 || top+y < 0 || left+x >= 500 || top+y >= 400) continue;
                const auto source = (std::size_t(y)*sprite.width+x)*4;
                if (!sprite.pixels[source+3]) continue;
                const auto target = (std::size_t(top+y)*500+left+x)*4;
                require(std::equal(sprite.pixels.begin()+source, sprite.pixels.begin()+source+4,
                    drawn.image.rgba.begin()+target), "Original actor pixels moved or changed palette");
                ++checked;
            }
            require(checked > 100, "Original actor pixel check was empty");
            state.entities[0].dead = true;
            require(fixture.render(empty, state, view).drawn == 0, "Dead actor remained visible");
            state.entities[0].dead = false;
            state.entities[0].kind = sim::Kind::Infantry;
            state.entities[0].type = "E1";
            state.entities[0].subcell = 4;
            const auto infantry = fixture.map_anchor(state.entities[0]);
            require(infantry.x == 1800 && infantry.y == 1267, "Original infantry subcell was lost");
            require(fixture.render(empty, state, view).drawn == 1, "Original infantry main image did not draw");
            state.entities[0].kind = sim::Kind::Unit;
            const auto unsupported = fixture.render(empty, state, view);
            require(unsupported.drawn == 0 && unsupported.gaps.size() == 1, "Unresolved vehicle draw was invented");
            // A view beyond the map must not create an out-of-range iterator.
            require(fixture.render(empty, {}, {1000000, 0, 1, 1}).image.rgba.size() == 4,
                    "Empty outside-map view failed");
            std::cout << (expansion ? "Expansion" : "Base") << " scene passed: " << frame.drawn
                      << " map buildings, exact actor pixels, camera, subcells, and dead actor removal.\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
