#include "platform.h"
#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace ra2::app;
namespace {
void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}
template<class F> void rejects(F action) {
    bool caught = false;
    try { action(); } catch (const std::runtime_error&) { caught = true; }
    require(caught, "Invalid platform input was accepted");
}
}
int main() {
    try {
        rejects([] { Window invalid("invalid", 0, 32, false); });
        Window window("RA2 native platform check", 64, 32, false);
        std::vector<std::uint8_t> frame(64 * 32 * 4);
        for (std::size_t pixel = 0; pixel < frame.size() / 4; ++pixel) {
            frame[pixel * 4] = static_cast<std::uint8_t>(pixel % 64);
            frame[pixel * 4 + 1] = static_cast<std::uint8_t>(pixel / 64);
            frame[pixel * 4 + 2] = 173;
            frame[pixel * 4 + 3] = 255;
        }
        window.present(frame.data(), 64, 32, frame.size());
        int width = 0, height = 0;
        const auto captured = window.capture(width, height);
        require(width == 64 && height == 32 && captured == frame,
                "Native render output differs from the supplied frame");
        rejects([&] { window.present(frame.data(), 64, 32, frame.size() - 1); });
        rejects([&] { window.present(nullptr, 64, 32, frame.size()); });
        std::vector<std::uint8_t> resized(32 * 16 * 4, 255);
        window.present(resized.data(), 32, 16, resized.size());
        const auto scaled = window.capture(width, height);
        require(std::all_of(scaled.begin(), scaled.end(), [](auto b) { return b == 255; }),
                "Resized native texture was not presented");

        window.poll();
        SDL_Window* native = SDL_GetWindowFromID(1);
        require(native != nullptr, "Native test window is missing");
        const auto id = SDL_GetWindowID(native);
        SDL_Event key{};
        key.type = SDL_KEYDOWN;
        key.key.windowID = id;
        key.key.keysym.sym = SDLK_LEFT;
        require(SDL_PushEvent(&key) == 1, "Cannot inject native key event");
        SDL_Event motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.motion.windowID = id;
        motion.motion.x = 13;
        motion.motion.y = 17;
        require(SDL_PushEvent(&motion) == 1, "Cannot inject native mouse event");
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        require(SDL_PushEvent(&quit) == 1, "Cannot inject native close event");
        const auto input = window.poll();
        require(input.size() == 3, "Native input events were lost");
        require(input[0].kind == InputKind::Key && input[0].key == SDLK_LEFT && input[0].pressed,
                "Native key event changed");
        require(input[1].kind == InputKind::Motion && input[1].x == 13 && input[1].y == 17,
                "Native pointer coordinates changed");
        require(input[2].kind == InputKind::Quit, "Native close event changed");

        rejects([] { AudioQueue invalid(0, 1); });
        AudioQueue audio(22050, 2);
        const std::vector<std::int16_t> pcm{0,0,32767,-32768,1,-1};
        audio.append(pcm);
        require(audio.queued_samples() == pcm.size(), "Native audio queue lost samples");
        rejects([&] { audio.append({1,2,3}); });
        audio.clear();
        require(audio.queued_samples() == 0, "Native audio clear failed");
        std::cout << "Native platform checks passed: pixels, resizing, input, and PCM queue.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
