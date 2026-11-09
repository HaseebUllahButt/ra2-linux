#include "platform.h"
#include "terrain_inputs.h"
#include "scene.h"
#include "battle.h"
#include "../loop/game_loop.h"
#include "../render/minimap.h"
#include "../sim/sim.h"
#include "../ui/ui.h"
#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
struct Options {
    std::filesystem::path game, map;
    std::size_t frames = 0;
    bool headless = false;
    bool skirmish = false, autoplay = false,human_input_check=false;
    ra2::fs::GameVariant variant = ra2::fs::GameVariant::RedAlert2;
};
Options options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") result.headless = true;
        else if (arg == "--skirmish") result.skirmish = true;
        else if (arg == "--autoplay") result.autoplay = true;
        else if (arg == "--human-input-check") result.human_input_check = true;
        else if (arg == "--yuri") result.variant = ra2::fs::GameVariant::YurisRevenge;
        else if (arg == "--game-dir" || arg == "--map" || arg == "--frames") {
            if (++i == argc) throw std::runtime_error("Missing value for " + arg);
            if (arg == "--game-dir") result.game = argv[i];
            else if (arg == "--map") result.map = argv[i];
            else {
                const std::string value = argv[i];
                std::size_t parsed = 0;
                const auto count = std::stoull(value, &parsed);
                if (parsed != value.size() || value.empty() || value[0] == '-' || count > 10000000)
                    throw std::runtime_error("Invalid frame budget");
                result.frames = static_cast<std::size_t>(count);
            }
        } else throw std::runtime_error("Unknown argument: " + arg);
    }
    if (result.game.empty())
        throw std::runtime_error("Usage: ra2-native --game-dir <owned-game-folder> [--map <map-file>] [--yuri] [--frames N] [--headless]");
    if (result.headless && !result.frames)
        throw std::runtime_error("Headless checks need a frame budget");
    if (result.skirmish && result.variant != ra2::fs::GameVariant::RedAlert2)
        throw std::runtime_error("Playable prototype supports base RA2 only");
    if (result.autoplay && !result.skirmish)
        throw std::runtime_error("Autoplay needs --skirmish");
    if(result.human_input_check&&result.variant!=ra2::fs::GameVariant::RedAlert2)
        throw std::runtime_error("Human input check needs base RA2");
    if(result.human_input_check&&result.autoplay)
        throw std::runtime_error("Human input check cannot use autoplay");
    return result;
}

class NativePump final : public ra2::loop::MessagePump {
    ra2::app::Window& window_;
    bool headless_;
public:
    std::set<std::int32_t> held;
    explicit NativePump(ra2::app::Window& window, bool headless)
        : window_(window), headless_(headless) {}
    void pump(ra2::loop::State& state) override {
        for (const auto& input : window_.poll()) {
            if (input.kind == ra2::app::InputKind::Quit) state.running = false;
            else if (input.kind == ra2::app::InputKind::Focus && !headless_) {
                state.window_active = input.pressed;
                if (!input.pressed) held.clear();
            } else if (input.kind == ra2::app::InputKind::Key) {
                if (input.pressed) held.insert(input.key);
                else held.erase(input.key);
                if (input.pressed && input.key == SDLK_ESCAPE) state.running = false;
            }
        }
    }
};

// This client checks the real terrain and Linux platform boundary. It does
// not substitute generic unit behavior for the unfinished original simulation.
class ScenePresenter final : public ra2::loop::Renderer {
    ra2::app::Window& window_;
    const ra2::render::Bitmap& map_;
    ra2::app::Scene& scene_;
    const ra2::sim::Simulation& simulation_;
    ra2::render::Bitmap frame_{1024, 768, std::vector<std::uint8_t>(1024 * 768 * 4)};
    int camera_x_ = 0, camera_y_ = 0;
public:
    std::size_t presented = 0;
    std::size_t actors_drawn = 0, actor_gaps = 0;
    ScenePresenter(ra2::app::Window& window, const ra2::render::Bitmap& map,
                   ra2::app::Scene& scene, const ra2::sim::Simulation& simulation)
        : window_(window), map_(map), scene_(scene), simulation_(simulation) {
        camera_x_ = std::max(0, (map.width - frame_.width) / 2);
        camera_y_ = std::max(0, (map.height - frame_.height) / 2);
    }
    void pan(int x, int y) {
        camera_x_ = std::clamp(camera_x_ + x, 0, std::max(0, map_.width - frame_.width));
        camera_y_ = std::clamp(camera_y_ + y, 0, std::max(0, map_.height - frame_.height));
    }
    void present(const ra2::loop::State&) override {
        auto rendered = scene_.render(map_, simulation_.snapshot(),
            {camera_x_, camera_y_, frame_.width, frame_.height});
        actors_drawn = rendered.drawn;
        actor_gaps = rendered.gaps.size();
        frame_ = std::move(rendered.image);
        window_.present(frame_.rgba.data(), frame_.width, frame_.height, frame_.rgba.size());
        ++presented;
    }
    void verify() const {
        int width = 0, height = 0;
        const auto actual = window_.capture(width, height);
        if (width != frame_.width || height != frame_.height || actual != frame_.rgba)
            throw std::runtime_error("Native output differs from the composed scene frame");
    }
};

class MenuPump final : public ra2::loop::MessagePump {
    ra2::app::Window& window_;
    ra2::ui::Menu& menu_;
    bool headless_;
    int pointer_x_ = -1, pointer_y_ = -1;
public:
    bool start_match = false;
    int test_stage=-1;
    MenuPump(ra2::app::Window& window, ra2::ui::Menu& menu, bool headless)
        : window_(window), menu_(menu), headless_(headless) {}
    void pump(ra2::loop::State& state) override {
        using ra2::app::InputKind;
        if(test_stage>=0 && test_stage<3) {
            const int ys[]={200,284,249};
            window_.inject_for_test({InputKind::Button,650,ys[test_stage],SDL_BUTTON_LEFT,true});
            window_.inject_for_test({InputKind::Button,650,ys[test_stage],SDL_BUTTON_LEFT,false});
            ++test_stage;
        }
        for (const auto& input : window_.poll()) {
            if (input.kind == InputKind::Quit) state.running = false;
            else if (input.kind == InputKind::Focus) {
                if (!headless_) state.window_active = input.pressed;
                if (!input.pressed) {
                    menu_.pointer_up(-1, -1);
                    pointer_x_ = pointer_y_ = -1;
                }
            } else if (input.kind == InputKind::Motion) {
                pointer_x_ = input.x; pointer_y_ = input.y;
                menu_.pointer_move(input.x, input.y);
            } else if (input.kind == InputKind::Wheel)
                menu_.pointer_scroll(pointer_x_, pointer_y_, input.y);
            else if (input.kind == InputKind::Button && input.key == SDL_BUTTON_LEFT) {
                pointer_x_ = input.x; pointer_y_ = input.y;
                if (input.pressed) menu_.pointer_down(input.x, input.y);
                else {
                    const auto action = menu_.pointer_up(input.x, input.y);
                    if (action == ra2::ui::Action::Quit) state.running = false;
                    if (action == ra2::ui::Action::StartMatch) {
                        start_match = true;
                        state.running = false;
                    }
                }
            } else if (input.kind == InputKind::Key && input.pressed && input.key == SDLK_ESCAPE)
                menu_.key_down(ra2::ui::Key::Escape);
        }
    }
};

class MenuPresenter final : public ra2::loop::Renderer {
    ra2::app::Window& window_;
    const ra2::ui::Menu& menu_;
    ra2::gfx::RgbaImage frame_;
public:
    std::size_t presented = 0;
    MenuPresenter(ra2::app::Window& window, const ra2::ui::Menu& menu)
        : window_(window), menu_(menu) {}
    void present(const ra2::loop::State&) override {
        frame_ = menu_.render();
        window_.present(frame_.pixels.data(), frame_.width, frame_.height, frame_.pixels.size());
        ++presented;
    }
    void verify() const {
        int width = 0, height = 0;
        const auto actual = window_.capture(width, height);
        if (width != frame_.width || height != frame_.height || actual != frame_.pixels)
            throw std::runtime_error("Native output differs from the original menu composition");
    }
    void capture_menu(const std::filesystem::path& path) const {
        ra2::render::write_ppm({frame_.width,frame_.height,frame_.pixels},path);
    }
};

int run_menu(const Options& args, ra2::fs::MixFileSystem& files) {
    const bool expansion = args.variant == ra2::fs::GameVariant::YurisRevenge;
    if (expansion) files.mount("NTRLMD.MIX");
    files.mount("NEUTRAL.MIX");
    const auto exe = files.read_file(expansion ? "gamemd.exe" : "game.exe");
    const auto csf = files.read_file(expansion ? "RA2MD.CSF" : "RA2.CSF");
    if (!exe || !csf) throw std::runtime_error("Original menu resources are missing");
    ra2text::CsfFile strings;
    strings.parse(csf->data(), csf->size());
    auto menu = ra2::ui::Menu::load(files, *exe, strings);
    menu.enable_prototype_start(!expansion);
    files.mount_resources();
    menu.configure_maps(ra2::ui::load_skirmish_maps(files, strings),
                        ra2::ui::load_skirmish_modes(files, strings));
    ra2::app::Window window("Red Alert 2 native rebuild: menu", 800, 600, !args.headless);
    MenuPump pump(window, menu, args.headless);
    MenuPresenter presenter(window, menu);
    if(args.human_input_check&&!args.skirmish)pump.test_stage=0;
    ra2::loop::SteadyClock clock;
    ra2::loop::GameLoop loop(pump, presenter, clock);
    for (;;) {
        if (args.frames) loop.run(args.human_input_check&&pump.test_stage<0?3:args.frames);
        else while (loop.state().running) loop.step();
        if (args.headless) presenter.verify();
        if(args.human_input_check)if(const auto* capture=std::getenv("RA2_MATCH_CAPTURE")) {
            auto prefix=std::filesystem::path(capture);prefix.replace_extension();
            presenter.capture_menu(prefix.string()+"-10-menu.ppm");
        }
        if (!pump.start_match) break;
        ra2::app::BattleOptions battle;
        battle.game_directory = args.game;
        battle.setup = menu.skirmish_setup();
        battle.map = battle.setup.map_filename.value_or("MP02T2.MAP");
        battle.headless = args.headless;
        battle.frames = args.frames;
        battle.human_input_check=args.human_input_check;
        // Keep only the match visible while it owns input and focus.
        window.set_visible(false);
        ra2::app::run_prototype_battle(battle);
        if(!args.headless)window.set_visible(true);
        if(args.human_input_check) {
            loop.state().frame_counter=0;
            pump.test_stage=-1;
        }
        pump.start_match = false;
        loop.state().running = true;
        loop.state().window_active = true;
        presenter.present(loop.state());
    }
    std::cout << "Native " << (expansion ? "Yuri" : "base RA2") << " menu: "
              << presenter.presented << " frames presented. "
              << (expansion ? "Expansion match start remains unsupported.\n" : "Base skirmish Start is enabled.\n");
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        const auto args = options(argc, argv);
        if (args.headless && SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) != 0)
            throw std::runtime_error("Cannot select headless native video");
        if (args.skirmish) {
            ra2::app::BattleOptions battle;
            battle.game_directory = args.game;
            if (!args.map.empty()) battle.map = args.map.string();
            battle.frames = args.frames;
            battle.headless = args.headless;
            battle.autoplay = args.autoplay;
            battle.human_input_check=args.human_input_check;
            const auto status=ra2::app::run_prototype_battle(battle);
            if((args.headless||args.frames)&&!args.human_input_check)return status;
        }
        ra2::fs::MixFileSystem files(args.game);
        files.mount_language(args.variant);
        files.bootstrap(args.variant);
        files.mount_overlays();
        if (args.map.empty()||args.skirmish) {
            auto menu_args=args;
            if(args.human_input_check&&args.skirmish)menu_args.frames=3;
            return run_menu(menu_args, files);
        }
        files.mount_resources();
        ra2::rules::IniFile ini;
        if (std::filesystem::is_regular_file(args.map))
            ini = ra2::rules::IniFile::from_file(args.map.string());
        else {
            const auto original_map = files.read_file(args.map.generic_string());
            if (!original_map) throw std::runtime_error("Original map is missing: " + args.map.string());
            ini.parse(std::string_view(reinterpret_cast<const char*>(original_map->data()), original_map->size()));
        }
        const auto geometry = ra2::render::MapGeometry::from_ini(ini);
        const auto terrain = ra2::app::allocated_terrain(ra2::pack::load_terrain(ini), geometry);
        auto art = ra2::render::TheaterArt::load(files, ini.read_string("Map", "Theater"));
        const auto cells = ra2::app::terrain_inputs(terrain, art);
        const auto theater = ra2::app::theater_inputs(art);
        const auto rules_name = args.variant == ra2::fs::GameVariant::RedAlert2 ? "RULES.INI" : "RULESMD.INI";
        const auto rules_bytes = files.read_file(rules_name);
        if (!rules_bytes) throw std::runtime_error(std::string("Missing original ") + rules_name);
        ra2::rules::IniFile rules;
        rules.parse(std::string_view(reinterpret_cast<const char*>(rules_bytes->data()), rules_bytes->size()));
        ra2::sim::Simulation simulation(rules, ini, 0, cells, &theater);
        const auto art_name = args.variant == ra2::fs::GameVariant::RedAlert2 ? "ART.INI" : "ARTMD.INI";
        const auto actor_bytes = files.read_file(art_name);
        if (!actor_bytes) throw std::runtime_error(std::string("Missing original ") + art_name);
        ra2::rules::IniFile actor_art;
        actor_art.parse(std::string_view(reinterpret_cast<const char*>(actor_bytes->data()), actor_bytes->size()));
        simulation.load_ground_assets((args.game / (args.variant == ra2::fs::GameVariant::RedAlert2 ?
            "game.exe" : "gamemd.exe")).string(), actor_art);
        ra2map::MapFile map_objects;
        map_objects.parse(ini);
        ra2::app::Scene scene(files, std::move(actor_art), ra2::app::overlay_rules(rules, ini),
            geometry, simulation.terrain(), ini.read_string("Map", "Theater"), map_objects.terrain());
        const auto live_terrain = ra2::app::initialized_terrain(terrain, simulation.terrain());
        const auto battlefield = ra2::render::render_minimap(live_terrain, geometry, art,
            ra2::render::TileIndexSpace::LiveCell);
        ra2::app::Window window("Red Alert 2 native rebuild: scene checks", 1024, 768, !args.headless);
        NativePump pump(window, args.headless);
        ScenePresenter presenter(window, battlefield, scene, simulation);
        ra2::loop::SteadyClock clock;
        ra2::loop::Hooks hooks;
        hooks.update = [&](ra2::loop::State&) {
            simulation.tick();
            // Viewer controls only. Original game camera timing is not claimed.
            const int dx = 30 * (int(pump.held.count(SDLK_RIGHT)) - int(pump.held.count(SDLK_LEFT)));
            const int dy = 15 * (int(pump.held.count(SDLK_DOWN)) - int(pump.held.count(SDLK_UP)));
            presenter.pan(dx, dy);
        };
        ra2::loop::GameLoop loop(pump, presenter, clock, {}, hooks);
        if (args.frames) loop.run(args.frames);
        else while (loop.state().running) loop.step();
        if (args.headless) presenter.verify();
        const auto snapshot = simulation.snapshot();
        if (args.headless && snapshot.frame != loop.state().frame_counter)
            throw std::runtime_error("Native simulation and frame loop diverged");
        std::cout << "Native scene client: " << terrain.tiles.size() << " original tiles, "
                  << presenter.presented << " frames presented, " << snapshot.entities.size()
                  << " map objects, " << snapshot.frame << " simulation ticks, "
                  << presenter.actors_drawn << " visible actors, " << presenter.actor_gaps
                  << " unsupported actor cases.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
