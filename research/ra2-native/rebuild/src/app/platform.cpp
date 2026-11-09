#include "platform.h"
#include <SDL.h>

#include <limits>
#include <stdexcept>

namespace ra2::app {
namespace {
[[noreturn]] void error(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
std::size_t image_size(int width, int height) {
    if (width < 1 || height < 1 || width > 16384 || height > 16384)
        throw std::runtime_error("Invalid native image dimensions");
    return std::size_t(width) * std::size_t(height) * 4;
}
}
struct Window::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int texture_width = 0, texture_height = 0;
    bool initialized = false;
    ~Impl() {
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        if (initialized) SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);
    }
};
Window::Window(const std::string& title, int width, int height, bool visible)
    : impl_(std::make_unique<Impl>()) {
    image_size(width, height);
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
        error("Initialize native video");
    impl_->initialized = true;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    const auto flags = SDL_WINDOW_RESIZABLE | (visible ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
    impl_->window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!impl_->window) error("Create native window");
    impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_ACCELERATED);
    if (!impl_->renderer)
        impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_SOFTWARE);
    if (!impl_->renderer) error("Create native renderer");
    if (SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255) != 0)
        error("Set native border color");
}
Window::~Window() = default;

std::vector<Input> Window::poll() {
    std::vector<Input> inputs;
    SDL_Event event;
    const auto window_id = SDL_GetWindowID(impl_->window);
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT: inputs.push_back({InputKind::Quit}); break;
        case SDL_WINDOWEVENT:
            if (event.window.windowID != window_id) break;
            switch (event.window.event) {
            case SDL_WINDOWEVENT_CLOSE: inputs.push_back({InputKind::Quit}); break;
            case SDL_WINDOWEVENT_FOCUS_GAINED: inputs.push_back({InputKind::Focus,0,0,0,true}); break;
            case SDL_WINDOWEVENT_FOCUS_LOST: inputs.push_back({InputKind::Focus,0,0,0,false}); break;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                inputs.push_back({InputKind::Resize,event.window.data1,event.window.data2}); break;
            default: break;
            }
            break;
        case SDL_KEYDOWN: case SDL_KEYUP:
            if (event.key.windowID != window_id || event.key.repeat) break;
            inputs.push_back({InputKind::Key,0,0,event.key.keysym.sym,event.type == SDL_KEYDOWN});
            break;
        case SDL_MOUSEMOTION:
            if (event.motion.windowID == window_id)
                inputs.push_back({InputKind::Motion,event.motion.x,event.motion.y});
            break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
            if (event.button.windowID == window_id)
                inputs.push_back({InputKind::Button,event.button.x,event.button.y,
                                  event.button.button,event.type == SDL_MOUSEBUTTONDOWN});
            break;
        case SDL_MOUSEWHEEL:
            if (event.wheel.windowID == window_id) {
                const int sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
                inputs.push_back({InputKind::Wheel,event.wheel.x * sign,event.wheel.y * sign});
            }
            break;
        default: break;
        }
    }
    return inputs;
}

void Window::present(const std::uint8_t* rgba, int width, int height, std::size_t size) {
    if (!rgba || size != image_size(width, height))
        throw std::runtime_error("Native frame dimensions differ from pixel buffer");
    if (!impl_->texture || width != impl_->texture_width || height != impl_->texture_height) {
        // Keep original pixels at their aspect ratio. SDL also maps pointer
        // events into these logical coordinates, including after resizing.
        if (SDL_RenderSetLogicalSize(impl_->renderer, width, height) != 0)
            error("Set native logical frame size");
        if (impl_->texture) SDL_DestroyTexture(impl_->texture);
        impl_->texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!impl_->texture) error("Create native frame texture");
        impl_->texture_width = width;
        impl_->texture_height = height;
        if (SDL_SetTextureBlendMode(impl_->texture, SDL_BLENDMODE_NONE) != 0)
            error("Set native frame blend mode");
    }
    if (SDL_UpdateTexture(impl_->texture, nullptr, rgba, width * 4) != 0)
        error("Update native frame");
    if (SDL_RenderClear(impl_->renderer) != 0 ||
        SDL_RenderCopy(impl_->renderer, impl_->texture, nullptr, nullptr) != 0)
        error("Draw native frame");
    SDL_RenderPresent(impl_->renderer);
}
void Window::title(const std::string& text) { SDL_SetWindowTitle(impl_->window, text.c_str()); }
std::vector<std::uint8_t> Window::capture(int& width, int& height) const {
    // A swap can discard the back buffer. Draw the retained texture again.
    if (impl_->texture && (SDL_RenderClear(impl_->renderer) != 0 ||
        SDL_RenderCopy(impl_->renderer, impl_->texture, nullptr, nullptr) != 0))
        error("Prepare native frame capture");
    if (SDL_GetRendererOutputSize(impl_->renderer, &width, &height) != 0)
        error("Read native render size");
    std::vector<std::uint8_t> pixels(image_size(width, height));
    // SDL 2.30 reads only the current viewport, even with a null rectangle.
    // Read the whole physical target so checks include the border pixels.
    SDL_Rect viewport;
    SDL_RenderGetViewport(impl_->renderer, &viewport);
    if (SDL_RenderSetViewport(impl_->renderer, nullptr) != 0)
        error("Set native capture viewport");
    const int status = SDL_RenderReadPixels(impl_->renderer, nullptr, SDL_PIXELFORMAT_RGBA32,
                                            pixels.data(), width * 4);
    const std::string detail = status != 0 ? SDL_GetError() : "";
    if (SDL_RenderSetViewport(impl_->renderer, &viewport) != 0)
        error("Restore native capture viewport");
    if (status != 0) throw std::runtime_error("Read native frame pixels: " + detail);
    return pixels;
}

AudioQueue::AudioQueue(int sample_rate, unsigned channels) : channels_(channels) {
    if (sample_rate < 4000 || sample_rate > 192000 || (channels != 1 && channels != 2))
        throw std::runtime_error("Unsupported native audio format");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) error("Initialize native audio");
    SDL_AudioSpec requested{};
    requested.freq = sample_rate;
    requested.format = AUDIO_S16SYS;
    requested.channels = static_cast<Uint8>(channels);
    requested.samples = 1024;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &requested, nullptr, 0);
    if (!device_) {
        const std::string detail = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error("Open native audio: " + detail);
    }
}
AudioQueue::~AudioQueue() {
    if (device_) SDL_CloseAudioDevice(device_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
void AudioQueue::append(const std::vector<std::int16_t>& samples) {
    constexpr std::size_t max_bytes = 32 * 1024 * 1024;
    if (samples.size() % channels_ || samples.size() > max_bytes / sizeof(std::int16_t))
        throw std::runtime_error("Invalid native PCM sample buffer");
    const std::size_t bytes = samples.size() * sizeof(std::int16_t);
    if (SDL_GetQueuedAudioSize(device_) > max_bytes - bytes)
        throw std::runtime_error("Native audio queue is full");
    if (!samples.empty() && SDL_QueueAudio(device_, samples.data(), static_cast<Uint32>(bytes)) != 0)
        error("Queue native audio");
}
void AudioQueue::pause(bool paused) { SDL_PauseAudioDevice(device_, paused ? 1 : 0); }
void AudioQueue::clear() { SDL_ClearQueuedAudio(device_); }
std::size_t AudioQueue::queued_samples() const {
    return SDL_GetQueuedAudioSize(device_) / sizeof(std::int16_t);
}
} // namespace ra2::app
