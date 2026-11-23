#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ra2::app {
enum class InputKind { Quit, Focus, Key, Motion, Button, Wheel, Resize };
struct Input {
    InputKind kind;
    int x = 0, y = 0;
    std::int32_t key = 0;
    bool pressed = false;
};

// The Linux boundary only. Original game behavior belongs in the engine.
class Window {
public:
    Window(const std::string& title, int width, int height, bool visible = true);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    std::vector<Input> poll();
    // App checks use the same SDL event queue as physical mouse and key input.
    void inject_for_test(const Input& input);
    void present(const std::uint8_t* rgba, int width, int height, std::size_t size);
    void set_visible(bool visible);
    void title(const std::string& text);
    // Read the actual native render target for automated graphics checks.
    std::vector<std::uint8_t> capture(int& width, int& height) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AudioQueue {
public:
    AudioQueue(int sample_rate, unsigned channels);
    ~AudioQueue();
    AudioQueue(const AudioQueue&) = delete;
    AudioQueue& operator=(const AudioQueue&) = delete;
    void append(const std::vector<std::int16_t>& samples);
    void pause(bool paused);
    void clear();
    std::size_t queued_samples() const;
private:
    std::uint32_t device_ = 0;
    unsigned channels_ = 0;
};
} // namespace ra2::app
