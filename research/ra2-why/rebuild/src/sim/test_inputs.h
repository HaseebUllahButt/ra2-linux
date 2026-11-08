#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct OriginalTestCell { std::uint16_t x,y; std::uint32_t tile; std::uint8_t subtile,raw,ramp,height; int overlay; };
// Keep the test bridge separate. The helper copy and main renderer use separate header paths.
std::vector<OriginalTestCell> original_test_cells(const std::string& game,const std::string& map,bool base);
std::string original_test_art(const std::string& game,bool base);
