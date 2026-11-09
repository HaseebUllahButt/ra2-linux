#include "csf.h"
#include <cstdio>
#include <fstream>
#include <vector>

using namespace ra2text;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: test_csf <csf-file>\n");
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    CsfFile csf;
    try {
        csf.parse(data.data(), data.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parse failed: %s\n", e.what());
        return 1;
    }
    std::printf("labels: %zu, strings: %zu\n",
                csf.labels().size(), csf.string_count());
    for (const CsfLabel& label : csf.labels()) {
        std::printf("%s (%zu values)\n", label.name.c_str(),
                    label.values.size());
        for (const CsfValue& value : label.values) {
            std::printf("  %s\n", value.text.c_str());
        }
    }
    return 0;
}
