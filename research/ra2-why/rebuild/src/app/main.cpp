#include "platform.h"
#include "../loop/game_loop.h"
#include "../render/minimap.h"
#include <SDL.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
struct Options {
    std::filesystem::path game, map;
    std::size_t frames = 0;
    bool headless = false;
};
Options options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") result.headless = true;
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
    if (result.game.empty() || result.map.empty())
        throw std::runtime_error("Usage: ra2-native --game-dir <owned-game-folder> --map <map-file> [--frames N] [--headless]");
    if (result.headless && !result.frames)
        throw std::runtime_error("Headless checks need a frame budget");
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
class TerrainPresenter final : public ra2::loop::Renderer {
    ra2::app::Window& window_;
    const ra2::render::Bitmap& map_;
    ra2::render::Bitmap frame_{1024, 768, std::vector<std::uint8_t>(1024 * 768 * 4)};
    int camera_x_ = 0, camera_y_ = 0;
public:
    std::size_t presented = 0;
    TerrainPresenter(ra2::app::Window& window, const ra2::render::Bitmap& map)
        : window_(window), map_(map) {
        camera_x_ = std::max(0, (map.width - frame_.width) / 2);
        camera_y_ = std::max(0, (map.height - frame_.height) / 2);
    }
    void pan(int x, int y) {
        camera_x_ = std::clamp(camera_x_ + x, 0, std::max(0, map_.width - frame_.width));
        camera_y_ = std::clamp(camera_y_ + y, 0, std::max(0, map_.height - frame_.height));
    }
    void present(const ra2::loop::State&) override {
        std::fill(frame_.rgba.begin(), frame_.rgba.end(), 0);
        const int width = std::min(frame_.width, map_.width - camera_x_);
        const int height = std::min(frame_.height, map_.height - camera_y_);
        for (int row = 0; row < height; ++row) {
            const auto source = (std::size_t(camera_y_ + row) * map_.width + camera_x_) * 4;
            const auto target = std::size_t(row) * frame_.width * 4;
            std::copy_n(map_.rgba.begin() + source, width * 4, frame_.rgba.begin() + target);
        }
        // The window receives an opaque final frame, including unused terrain.
        for (std::size_t p = 3; p < frame_.rgba.size(); p += 4) frame_.rgba[p] = 255;
        window_.present(frame_.rgba.data(), frame_.width, frame_.height, frame_.rgba.size());
        ++presented;
    }
    void verify() const {
        int width = 0, height = 0;
        const auto actual = window_.capture(width, height);
        if (width != frame_.width || height != frame_.height || actual != frame_.rgba)
            throw std::runtime_error("Native output differs from the original terrain frame");
    }
};
}

int main(int argc, char** argv) {
    try {
        const auto args = options(argc, argv);
        if (args.headless && SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) != 0)
            throw std::runtime_error("Cannot select headless native video");
        const auto ini = ra2::rules::IniFile::from_file(args.map.string());
        const auto terrain = ra2::pack::load_terrain(ini);
        const auto geometry = ra2::render::MapGeometry::from_ini(ini);
        ra2::fs::MixFileSystem files(args.game);
        files.bootstrap();
        auto art = ra2::render::TheaterArt::load(files, ini.read_string("Map", "Theater"));
        const auto battlefield = ra2::render::render_minimap(terrain, geometry, art);
        ra2::app::Window window("Red Alert 2 native rebuild: terrain", 1024, 768, !args.headless);
        NativePump pump(window, args.headless);
        TerrainPresenter presenter(window, battlefield);
        ra2::loop::SteadyClock clock;
        ra2::loop::Hooks hooks;
        hooks.update = [&](ra2::loop::State&) {
            // Viewer controls only. Original game camera timing is not claimed.
            const int dx = 30 * (int(pump.held.count(SDLK_RIGHT)) - int(pump.held.count(SDLK_LEFT)));
            const int dy = 15 * (int(pump.held.count(SDLK_DOWN)) - int(pump.held.count(SDLK_UP)));
            presenter.pan(dx, dy);
        };
        ra2::loop::GameLoop loop(pump, presenter, clock, {}, hooks);
        if (args.frames) loop.run(args.frames);
        else while (loop.state().running) loop.step();
        if (args.headless) presenter.verify();
        std::cout << "Native terrain client: " << terrain.tiles.size() << " original tiles, "
                  << presenter.presented << " frames presented.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
