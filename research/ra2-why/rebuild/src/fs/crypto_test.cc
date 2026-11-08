// Standalone implementation-level checks; do not link with mix_archive.cpp.
#include "mix_archive.cpp"
#include <iostream>

int main() {
    using namespace ra2::fs;
    try {
        // Published Blowfish zero-key/zero-plaintext known answer. A repeated
        // zero key expands identically at key lengths 8 and 56.
        Blowfish fish(std::array<std::uint8_t, 56>{});
        std::vector<std::uint8_t> block{0x4e, 0xf9, 0x97, 0x45, 0x61, 0x98, 0xdd, 0x78};
        fish.decrypt(block);
        if (block != std::vector<std::uint8_t>(8)) throw MixError("Blowfish known-answer failure");
        // Exercise the extra carry limb: (n-1)^2 mod n == 1 for an almost
        // 320-bit modulus. This checks carries/borrows over every limb.
        Integer modulus{};
        for (unsigned i = 0; i < 10; ++i) modulus[i] = 0xffffffff;
        modulus[0] -= 16; // n = 2^320 - 17.
        Integer one{}; one[0] = 1;
        auto a = modulus; subtract(a, one);
        if (multiply_mod(a, a, modulus) != one) throw MixError("Wide modular multiply failure");
        Integer small{}; small[0] = 19;
        auto expected = modulus; Integer delta{}; delta[0] = 19; subtract(expected, delta);
        if (multiply_mod(a, small, modulus) != expected) throw MixError("Modular reduction failure");
        std::cout << "Crypto tests passed (Blowfish known answer and 320-bit modular arithmetic).\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
