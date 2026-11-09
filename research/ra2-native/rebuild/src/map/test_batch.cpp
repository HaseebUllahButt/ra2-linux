// Batch map verification: parse every .map given as argument.
#include "map_file.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ra2map;

int main(int argc, char** argv) {
    int ok = 0, failed = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        if (!in) { ++failed; continue; }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        MapFile map;
        try {
            map.parse(text);
            if (map.theater().empty() || map.size_right() == 0) {
                std::printf("SUSPECT %s: theater=%s size=%d\n", argv[i],
                            map.theater().c_str(), map.size_right());
                ++failed;
                continue;
            }
            ++ok;
        } catch (const std::exception& e) {
            std::printf("FAIL %s: %s\n", argv[i], e.what());
            ++failed;
        }
    }
    std::printf("maps parsed ok: %d, failed: %d\n", ok, failed);
    return failed == 0 ? 0 : 1;
}
