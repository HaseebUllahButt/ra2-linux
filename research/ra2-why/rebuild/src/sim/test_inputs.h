#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <optional>
struct OriginalTestCell { std::uint16_t x,y; std::uint32_t tile,resolved; std::uint8_t subtile,raw,ramp,height; int overlay; };
// Keep the test bridge separate. The helper copy and main renderer use separate header paths.
std::vector<OriginalTestCell> original_test_cells(const std::string& game,const std::string& map,bool base);
std::string original_test_art(const std::string& game,bool base);

struct OriginalTestMetadata { std::uint8_t terrain,ramp; bool valid; };
struct OriginalTestRange { std::uint32_t first,count; };
struct OriginalTestCatalog {
    std::string name,settings;
    std::vector<OriginalTestRange> ranges;
    std::function<std::optional<OriginalTestMetadata>(std::uint32_t,std::uint8_t)> metadata;
};
OriginalTestCatalog original_test_catalog(const std::string& game,const std::string& map,bool base);
