#include "map_pack.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ra2::pack;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action) {
    bool caught = false;
    try { action(); } catch (const std::runtime_error&) { caught = true; }
    require(caught, "Malformed input was accepted");
}
void self_tests() {
    require(decode_base64(" Zm9v\nYmFy ") == Bytes({'f','o','o','b','a','r'}), "base64 words");
    require(decode_base64("Zg==") == Bytes({'f'}), "base64 two pads");
    require(decode_base64("Zm8=") == Bytes({'f','o'}), "base64 one pad");
    for (const auto* bad : {"Zg=", "Zg==AAAA", "Zg=A", "Zh==", "Zm9=", "????"})
        rejects([&] { decode_base64(bad); });

    const Bytes lcw{0x83,'A','B','C',0x00,3,0xc0,0,0,0xff,5,0,1,0,0xfe,4,0,'x',0x80};
    const auto decoded = decode_lcw(lcw, 18);
    require(std::string(decoded.begin(), decoded.end()) == "ABCABCABCBCABCxxxx", "LCW opcode coverage");
    require(decode_lcw({0x81,'A',0x50,1,0x80}, 9) == Bytes(9,'A'), "Overlapping LCW copy");
    require(decode_lcw({0x80}, 0).empty(), "Empty LCW stream");
    for (const Bytes& bad : {Bytes{0x81}, Bytes{0x81,'A'}, Bytes{0,1,0x80},
                           Bytes{0x81,'A',0,0,0x80}, Bytes{0xfe,9,0,'A',0x80},
                           Bytes{0xc0,0,0,0x80}, Bytes{0x80,0}})
        rejects([&] { decode_lcw(bad, 1); });
    rejects([&] { decode_lcw(lcw, 17); });
    rejects([&] { decode_lcw(lcw, 19); });

    const Bytes block{3,0,1,0,0x81,'x',0x80};
    Bytes two = block;
    two.insert(two.end(), block.begin(), block.end());
    require(decode_chunks(two, Codec::Lcw) == Bytes({'x','x'}), "Chunk boundaries");
    rejects([&] { decode_chunks(two, Codec::Lcw, 1); });
    for (const Bytes& bad : {Bytes{1}, Bytes{0,0,1,0}, Bytes{4,0,1,0,0x80},
                           Bytes{1,0,1,32,0x80}, Bytes{1,0,1,0,0x80}})
        rejects([&] { decode_chunks(bad, Codec::Lcw); });
    rejects([&] { decode_chunks(block, Codec::Lzo); });
    ra2::rules::IniFile ini;
    ini.parse("[Pack]\n2=IF4gA==\n1=AwABA\n");
    require(read_section(ini, "Pack", Codec::Lcw) == Bytes({'x'}), "Numeric section order");
    ini.parse("[Pack]\n2=AwABAIF4gA==\n");
    rejects([&] { read_section(ini, "Pack", Codec::Lcw); });
    ini.parse("[IsoMapPack5]\n1=AAAA\n");
    rejects([&] { load_terrain(ini); });
}
}

int main(int argc, char** argv) {
    try {
        self_tests();
        if (argc != 2) throw std::runtime_error("Usage: pack_test <original-map-directory>");
        std::size_t maps = 0, tiles = 0, overlay_maps = 0, preview_bytes = 0;
        for (const auto& file : std::filesystem::directory_iterator(argv[1])) {
            if (!file.is_regular_file()) continue;
            auto ini = ra2::rules::IniFile::from_file(file.path().string());
            if (!ini.find_section("IsoMapPack5")) continue;
            try {
                const auto terrain = load_terrain(ini);
                require(!terrain.tiles.empty(), "Original map has no tiles");
                tiles += terrain.tiles.size();
                if (!terrain.overlay.empty()) ++overlay_maps;
                const auto preview = read_section(ini, "PreviewPack", Codec::Lzo);
                if (!preview.empty()) {
                    const auto size = ini.read_list("Preview", "Size");
                    require(size.size() == 4, "Preview dimensions missing");
                    const int width = std::stoi(size[2]), height = std::stoi(size[3]);
                    require(width > 0 && height > 0 &&
                        preview.size() == std::size_t(width) * std::size_t(height) * 3,
                        "Preview byte count differs from dimensions");
                    preview_bytes += preview.size();
                }
                if (file.path().filename() == "2PEAKS.MAP") {
                    require(terrain.tiles.size() == 16218, "2PEAKS tile count");
                    const auto& t = terrain.tiles.front();
                    require(t.x == 80 && t.y == 1 && t.index == 314 && t.subtile == 1 &&
                            t.height == 0 && t.ice == 0, "2PEAKS first tile record");
                }
                ++maps;
            } catch (const std::exception& error) {
                throw std::runtime_error(file.path().filename().string() + ": " + error.what());
            }
        }
        require(maps > 0, "No original packed maps were tested");
        std::cout << "Pack checks passed: " << maps << " maps, " << tiles << " tiles, "
                  << overlay_maps << " overlay grids, " << preview_bytes << " preview bytes.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
