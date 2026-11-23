#include "depth.h"
#include <iostream>
#include <stdexcept>

int main() try {
    int base, origin, screen, level, source, existing;
    while (std::cin >> base >> origin >> screen >> level >> source >> existing) {
        if (base < 0 || base > 65535 || level < -128 || level > 127 ||
            source < 0 || source > 255 || existing < 0 || existing > 65535)
            throw std::runtime_error("Invalid original terrain depth fixture");
        const auto depth = ra2::render::original_tile_depth(static_cast<std::uint16_t>(base),
            origin, screen, static_cast<std::int8_t>(level), static_cast<std::uint8_t>(source));
        std::cout << depth << ' ' << int(depth <= existing) << '\n';
    }
    if (!std::cin.eof()) throw std::runtime_error("Invalid original terrain depth input");
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
