// Standalone implementation-level checks; do not link with mix_archive.cpp.
#include "mix_archive.cpp"
#include <iomanip>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    using namespace ra2::fs;
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--key") {
            std::ifstream input(argv[2], std::ios::binary);
            std::vector<std::uint8_t> wrapped(80);
            if (!input || !input.seekg(4) ||
                !input.read(reinterpret_cast<char*>(wrapped.data()), wrapped.size()))
                throw MixError("Cannot read real archive key block");
            const auto archive = MixArchive::open(argv[2]);
            if (!archive->encrypted()) throw MixError("Archive is not encrypted");
            const auto key = decode_key(wrapped);
            std::cout << "key=" << std::hex << std::setfill('0');
            for (auto byte : key) std::cout << std::setw(2) << unsigned(byte);
            std::cout << '\n';
            // Read the actual embedded INI identified by RE-REPORT-2.md.
            // Print only its public value; no private-side material is used.
            if (const auto* entry = archive->find(0x763c81ddu)) {
                const auto bytes = archive->read(*entry);
                std::istringstream ini(std::string(bytes.begin(), bytes.end()));
                bool public_section = false;
                std::string line;
                while (std::getline(ini, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty() && line.front() == '[') public_section = line == "[PublicKey]";
                    if (public_section && line.compare(0, 2, "1=") == 0)
                        std::cout << "public=" << line.substr(2) << '\n';
                }
            }
            return 0;
        }
        if (argc != 1) throw MixError("Usage: crypto_test [--key ARCHIVE]");
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
        for (std::size_t size : {std::size_t(0), std::size_t(79), std::size_t(81)}) {
            bool rejected = false;
            try { (void)decode_key(std::vector<std::uint8_t>(size)); }
            catch (const MixError&) { rejected = true; }
            if (!rejected) throw MixError("Invalid wrapped key length accepted");
        }
        std::cout << "Crypto tests passed (Blowfish known answer and 320-bit modular arithmetic).\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
